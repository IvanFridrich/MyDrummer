// AutoDrummer implementation — see auto_drummer.hpp for the API contract.
#include "audio/auto_drummer.hpp"
#include "patterns.hpp"
#include "samples.hpp"
#include "util/log.hpp"

#include <string.h>   // memset

namespace dummer { namespace audio {

// ---------------------------------------------------------------------------
// BPM table: indexed by [style-1][speed_idx] (style 0 = Off, excluded)
// ---------------------------------------------------------------------------

static constexpr uint16_t kBpmTable[6][3] = {
    {  70,  90, 110 },   // Blues
    {  80, 120, 160 },   // Jazz
    {  80, 100, 120 },   // Funk
    {  65,  80,  95 },   // Reggae
    {  70,  90, 110 },   // Gospel
    { 100, 120, 140 },   // HardRock
};

// ---------------------------------------------------------------------------
// GM drum note → SampleId mapping
// ---------------------------------------------------------------------------

uint8_t AutoDrummer::gm_to_sample_id(uint8_t gm_note) {
    switch (gm_note) {
        case 36:                    return static_cast<uint8_t>(SampleId::SAMPLE_KICK);
        case 38: case 40:           return static_cast<uint8_t>(SampleId::SAMPLE_SNARE);
        case 50: case 48:           return static_cast<uint8_t>(SampleId::SAMPLE_TOM_HI);
        case 47: case 43:           return static_cast<uint8_t>(SampleId::SAMPLE_TOM_MID);
        case 45: case 41:           return static_cast<uint8_t>(SampleId::SAMPLE_TOM_LO);
        case 42:                    return static_cast<uint8_t>(SampleId::SAMPLE_HIHAT_CLOSED);
        case 46:                    return static_cast<uint8_t>(SampleId::SAMPLE_HIHAT_OPEN);
        case 44:                    return static_cast<uint8_t>(SampleId::SAMPLE_HIHAT_PEDAL);
        case 49: case 57:           return static_cast<uint8_t>(SampleId::SAMPLE_CRASH);
        case 51: case 53: case 59:  return static_cast<uint8_t>(SampleId::SAMPLE_RIDE);
        case 31:                    return static_cast<uint8_t>(SampleId::SAMPLE_STICKS);
        case 56:                    return static_cast<uint8_t>(SampleId::SAMPLE_COWBELL);
        case 54:                    return static_cast<uint8_t>(SampleId::SAMPLE_TAMBOURINE);
        default:                    return 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AutoDrummer::AutoDrummer()
    : style_(AutoStyle::Off)
    , speed_idx_(0)            // slow speed by default
    , intro_count_(0)
    , loop_count_(0)
    , loop_start_sample_(0)
    , loop_length_samples_(1)  // non-zero guard
    , pos_samples_(0)
    , intro_event_idx_(0)
    , in_loop_(false)
    , loop_pos_samples_(0)
    , loop_event_idx_(0)
    , humanizer_()
{
    memset(intro_, 0, sizeof(intro_));
    memset(loop_,  0, sizeof(loop_));
}

// ---------------------------------------------------------------------------
// load_pattern — pre-compute sample positions for the current style + speed
// ---------------------------------------------------------------------------

void AutoDrummer::load_pattern() {
    if (style_ == AutoStyle::Off) return;

    const uint8_t     pat_idx = static_cast<uint8_t>(style_) - 1u;
    const PatternInfo& pat    = kDrumPatterns[pat_idx];
    const uint16_t    bpm     = kBpmTable[pat_idx][speed_idx_];

    // samples_per_tick as float (computed once, used only here)
    const float spt = static_cast<float>(SAMPLE_RATE) * 60.0f
                    / static_cast<float>(AUTO_DRUMMER_PPQN * bpm);

    loop_start_sample_   = static_cast<uint32_t>(pat.loop_start_tick * spt + 0.5f);
    loop_length_samples_ = static_cast<uint32_t>(
        (pat.loop_end_tick - pat.loop_start_tick) * spt + 0.5f);
    if (loop_length_samples_ == 0) loop_length_samples_ = 1;

    intro_count_ = 0;
    loop_count_  = 0;

    for (uint16_t i = 0; i < pat.count; ++i) {
        const DrumEvent& ev  = pat.events[i];
        const uint8_t    sid = gm_to_sample_id(ev.note);
        if (sid == 0xFF) continue;

        if (ev.tick < pat.loop_start_tick) {
            if (intro_count_ < kMaxIntro) {
                intro_[intro_count_].time = static_cast<uint32_t>(ev.tick * spt + 0.5f);
                intro_[intro_count_].note = sid;
                intro_[intro_count_].vel  = ev.vel;
                ++intro_count_;
            } else {
                LOG_W("AUTO", "intro event cap (%u) exceeded; note dropped", kMaxIntro);
            }
        } else {
            if (loop_count_ < kMaxLoop) {
                const uint32_t offset = ev.tick - pat.loop_start_tick;
                loop_[loop_count_].time = static_cast<uint32_t>(offset * spt + 0.5f);
                loop_[loop_count_].note = sid;
                loop_[loop_count_].vel  = ev.vel;
                ++loop_count_;
            } else {
                LOG_W("AUTO", "loop event cap (%u) exceeded; note dropped", kMaxLoop);
            }
        }
    }

    // Seed the jittered fire times for the first pass through each phase.
    rejitter_intro();
    rejitter_loop();

    // Reset playback to start of count-in
    pos_samples_     = 0;
    intro_event_idx_ = 0;
    in_loop_         = false;
    loop_pos_samples_= 0;
    loop_event_idx_  = 0;
}

// ---------------------------------------------------------------------------
// rejitter — apply fresh +/- timing jitter to one phase's events
//
// Effective time = base + jitter, clamped to be monotonically non-decreasing
// and strictly below the phase bound. The running-max clamp guarantees the
// event scan in tick() (which assumes ascending fire times) never reorders,
// and the upper clamp keeps every event inside the wrap window so none is
// dropped at the loop boundary.
// ---------------------------------------------------------------------------

void AutoDrummer::rejitter_intro() {
    const uint32_t hi = (loop_start_sample_ > 0) ? loop_start_sample_ - 1u : 0u;
    uint32_t prev = 0;
    for (uint16_t i = 0; i < intro_count_; ++i) {
        int64_t t = static_cast<int64_t>(intro_[i].time)
                  + humanizer_.jitter(HUMANIZE_TIME_SAMPLES);
        if (t < static_cast<int64_t>(prev)) t = static_cast<int64_t>(prev);
        if (t > static_cast<int64_t>(hi))   t = static_cast<int64_t>(hi);
        intro_[i].play = static_cast<uint32_t>(t);
        prev = intro_[i].play;
    }
}

void AutoDrummer::rejitter_loop() {
    const uint32_t hi = (loop_length_samples_ > 0) ? loop_length_samples_ - 1u : 0u;
    uint32_t prev = 0;
    for (uint16_t i = 0; i < loop_count_; ++i) {
        int64_t t = static_cast<int64_t>(loop_[i].time)
                  + humanizer_.jitter(HUMANIZE_TIME_SAMPLES);
        if (t < static_cast<int64_t>(prev)) t = static_cast<int64_t>(prev);
        if (t > static_cast<int64_t>(hi))   t = static_cast<int64_t>(hi);
        loop_[i].play = static_cast<uint32_t>(t);
        prev = loop_[i].play;
    }
}

// ---------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------

void AutoDrummer::next_style() {
    const uint8_t next = (static_cast<uint8_t>(style_) + 1u)
                       % static_cast<uint8_t>(AutoStyle::COUNT);
    style_ = static_cast<AutoStyle>(next);
    if (style_ != AutoStyle::Off) {
        load_pattern();
    }
}

void AutoDrummer::next_speed() {
    speed_idx_ = static_cast<uint8_t>((speed_idx_ + 1u) % 3u);
    if (style_ != AutoStyle::Off) {
        load_pattern();
    }
}

void AutoDrummer::stop() {
    style_    = AutoStyle::Off;
    in_loop_  = false;
}

// ---------------------------------------------------------------------------
// tick — advance clock and emit triggers
// ---------------------------------------------------------------------------

size_t AutoDrummer::tick(uint32_t n_samples, uint8_t* trig, uint8_t* vel, size_t max_trig) {
    if (style_ == AutoStyle::Off) return 0;

    size_t out = 0;

    // --- Intro phase -------------------------------------------------------
    if (!in_loop_) {
        const uint32_t new_pos = pos_samples_ + n_samples;

        while (intro_event_idx_ < intro_count_ &&
               intro_[intro_event_idx_].play < new_pos) {
            if (out < max_trig) {
                trig[out] = intro_[intro_event_idx_].note;
                vel[out]  = humanizer_.humanize_velocity(intro_[intro_event_idx_].vel);
                ++out;
            }
            ++intro_event_idx_;
        }

        if (new_pos >= loop_start_sample_) {
            in_loop_          = true;
            loop_pos_samples_ = new_pos - loop_start_sample_;
            loop_event_idx_   = 0;
            rejitter_loop();   // fresh jitter for the first loop iteration

            // Fire any loop events that already fall within this chunk
            while (loop_event_idx_ < loop_count_ &&
                   loop_[loop_event_idx_].play < loop_pos_samples_) {
                if (out < max_trig) {
                    trig[out] = loop_[loop_event_idx_].note;
                    vel[out]  = humanizer_.humanize_velocity(loop_[loop_event_idx_].vel);
                    ++out;
                }
                ++loop_event_idx_;
            }
        }

        pos_samples_ = new_pos;
        return out;
    }

    // --- Loop phase --------------------------------------------------------
    uint32_t new_loop_pos = loop_pos_samples_ + n_samples;

    if (new_loop_pos >= loop_length_samples_) {
        // Fire remaining events up to the loop boundary
        while (loop_event_idx_ < loop_count_ &&
               loop_[loop_event_idx_].play < loop_length_samples_) {
            if (out < max_trig) {
                trig[out] = loop_[loop_event_idx_].note;
                vel[out]  = humanizer_.humanize_velocity(loop_[loop_event_idx_].vel);
                ++out;
            }
            ++loop_event_idx_;
        }
        // Wrap → new iteration gets a fresh set of jittered fire times.
        new_loop_pos    -= loop_length_samples_;
        loop_event_idx_  = 0;
        rejitter_loop();
    }

    // Fire events in the remainder of the chunk
    while (loop_event_idx_ < loop_count_ &&
           loop_[loop_event_idx_].play < new_loop_pos) {
        if (out < max_trig) {
            trig[out] = loop_[loop_event_idx_].note;
            vel[out]  = humanizer_.humanize_velocity(loop_[loop_event_idx_].vel);
            ++out;
        }
        ++loop_event_idx_;
    }

    loop_pos_samples_ = new_loop_pos;
    return out;
}

}} // namespace dummer::audio
