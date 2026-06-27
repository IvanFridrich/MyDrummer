#include "looper.hpp"
#include "util/log.hpp"

namespace dummer
{
namespace audio
{

Looper::Looper()
    : state_(LooperState::Idle), event_count_(0), record_pos_(0), play_pos_(0), loop_length_(0),
      play_idx_(0)
{
}

void Looper::on_button_short()
{
    switch (state_)
    {
    case LooperState::Idle:
        state_ = LooperState::Arming;
        LOG_I("LOOPER", "Arming");
        break;
    case LooperState::Arming:
        state_ = LooperState::Idle;
        LOG_I("LOOPER", "Arm cancelled");
        break;
    case LooperState::Recording:
        start_playing();
        if (state_ == LooperState::Playing)
            LOG_I("LOOPER", "Playing (%lu samples)", static_cast<unsigned long>(loop_length_));
        break;
    case LooperState::Playing:
        stop();
        LOG_I("LOOPER", "Stopped");
        break;
    }
}

void Looper::on_button_long()
{
    stop();
    LOG_I("LOOPER", "Long-press clear");
}

void Looper::on_drum_hit(uint8_t sample_id)
{
    if (state_ == LooperState::Arming)
    {
        record_pos_  = 0;
        event_count_ = 0;
        state_       = LooperState::Recording;
        LOG_I("LOOPER", "Recording started");
        store_event(sample_id);
    }
    else if (state_ == LooperState::Recording)
    {
        store_event(sample_id);
    }
}

size_t Looper::tick(uint32_t n, uint8_t* out, size_t max_out)
{
    if (state_ == LooperState::Recording)
    {
        record_pos_ += n;
        const uint32_t max_s = static_cast<uint32_t>(LOOPER_MAX_DURATION_S) * SAMPLE_RATE;
        if (record_pos_ >= max_s)
        {
            record_pos_ = max_s;
            start_playing();
            LOG_I("LOOPER", "Auto-finalised at max duration");
        }
        return 0;
    }

    if (state_ != LooperState::Playing || loop_length_ == 0)
        return 0;

    size_t         count      = 0;
    const uint32_t window_end = play_pos_ + n;

    auto emit_until = [&](uint32_t limit) {
        while (play_idx_ < event_count_ && count < max_out)
        {
            if (events_[play_idx_].time_samples >= limit)
                break;
            out[count++] = events_[play_idx_].sample_id;
            ++play_idx_;
        }
    };

    if (window_end < loop_length_)
    {
        emit_until(window_end);
        play_pos_ = window_end;
    }
    else
    {
        emit_until(loop_length_);
        const uint32_t new_pos = window_end - loop_length_;
        play_pos_              = 0;
        play_idx_              = 0;
        emit_until(new_pos);
        play_pos_ = new_pos;
    }

    return count;
}

void Looper::stop()
{
    state_       = LooperState::Idle;
    event_count_ = 0;
    record_pos_  = 0;
    play_pos_    = 0;
    loop_length_ = 0;
    play_idx_    = 0;
}

void Looper::store_event(uint8_t sample_id)
{
    if (event_count_ >= LOOPER_MAX_EVENTS)
    {
        LOG_W("LOOPER", "event buffer full");
        return;
    }
    events_[event_count_++] = {record_pos_, sample_id};
}

void Looper::start_playing()
{
    if (record_pos_ == 0)
    {
        stop();
        LOG_W("LOOPER", "empty recording discarded");
        return;
    }
    loop_length_ = record_pos_;
    play_pos_    = 0;
    play_idx_    = 0;
    state_       = LooperState::Playing;
}

} // namespace audio
} // namespace dummer
