// AutoDrummer — MIDI-pattern-based drum machine with 6 styles.
//
// Styles cycle: Off → Blues → Country → Jazz → Funk → Reggae → Gospel → HardRock → Off.
// Each style has three BPM tiers (slow/normal/fast); speed cycles independently.
//
// tick() advances the internal clock and emits sample IDs into the caller's
// trigger array, exactly like Looper::tick(). AutoDrummer and Looper are
// mutually exclusive; the app layer calls stop() on one when starting the other.
#pragma once

#include "defines.hpp"
#include "audio/humanizer.hpp"

#include <stddef.h>
#include <stdint.h>

namespace dummer { namespace audio {

enum class AutoStyle : uint8_t {
    Off      = 0,
    Blues    = 1,
    Country  = 2,
    Jazz     = 3,
    Funk     = 4,
    Reggae   = 5,
    Gospel   = 6,
    HardRock = 7,
    COUNT    = 8,
};

class AutoDrummer {
  public:
    AutoDrummer();

    // Cycle to next style. Restarts playback from the count-in.
    // Off → Blues → Country → Jazz → Funk → Reggae → Gospel → HardRock → Off.
    void next_style();

    // Cycle to next BPM tier (slow → normal → fast → slow).
    // Reloads the current pattern with the new BPM.
    void next_speed();

    // Stop and return to Off. Idempotent.
    void stop();

    AutoStyle style()  const { return style_; }
    bool      active() const { return style_ != AutoStyle::Off; }
    uint8_t   speed()  const { return speed_idx_; }

    // Humanization (M7): timing/velocity jitter. On by default
    // (HUMANIZE_ENABLED_DEFAULT). Toggling reseeds nothing; it just gates jitter.
    void set_humanize(bool on) { humanizer_.set_enabled(on); }
    bool humanize() const { return humanizer_.enabled(); }

    // Advance the clock by n_samples.
    // Fires any events that fall in the current window into trig[0..return-1]
    // with matching (humanized) velocities in vel[0..return-1].
    // max_trig caps the output; extra events are silently dropped.
    // Returns number of sample IDs written (0 when Off).
    size_t tick(uint32_t n_samples, uint8_t* trig, uint8_t* vel, size_t max_trig);

  private:
    static constexpr uint16_t kMaxIntro = AUTO_DRUMMER_MAX_INTRO;
    static constexpr uint16_t kMaxLoop  = AUTO_DRUMMER_MAX_LOOP;

    AutoStyle style_;
    uint8_t   speed_idx_;   // 0=slow 1=normal 2=fast

    struct PatternEvent {
        uint32_t time;   // base time (absolute for intro, offset-from-loop-start for loop)
        uint32_t play;   // jittered fire time used by tick()
        uint8_t  note;   // SampleId
        uint8_t  vel;    // base MIDI velocity (1-127)
    };

    // Pre-computed sample positions built by load_pattern().
    PatternEvent intro_[kMaxIntro];
    uint16_t     intro_count_;

    PatternEvent loop_[kMaxLoop];
    uint16_t     loop_count_;

    uint32_t  loop_start_sample_;        // absolute sample where loop begins
    uint32_t  loop_length_samples_;      // length of one loop iteration

    // Playback state
    uint32_t  pos_samples_;              // absolute sample counter
    uint16_t  intro_event_idx_;          // next intro event to fire
    bool      in_loop_;                  // true once intro has played through
    uint32_t  loop_pos_samples_;         // position within current loop iteration
    uint16_t  loop_event_idx_;           // next loop event to fire

    Humanizer humanizer_;

    void    load_pattern();
    // Refill *_play_ from *_times_ with fresh +/- timing jitter, clamped to be
    // monotonic and within range so the event scan never reorders or overruns.
    void    rejitter_intro();
    void    rejitter_loop();
    static uint8_t gm_to_sample_id(uint8_t gm_note);
};

}} // namespace dummer::audio
