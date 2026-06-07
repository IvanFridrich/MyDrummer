// Looper — event-based single-layer loop recorder/player.
//
// FSM: Idle → Arming → Recording → Playing → Idle
//      Any state → Idle via long-press or stop().
//
// Events are timestamped in samples relative to t0 (first drum hit).
// tick() advances the clock; in Playing state it emits sample IDs for any
// events whose timestamp falls in the current AUDIO_CHUNK window.
#pragma once

#include "defines.hpp"

#include <stddef.h>
#include <stdint.h>

namespace dummer { namespace audio {

enum class LooperState : uint8_t { Idle, Arming, Recording, Playing };

class Looper {
  public:
    Looper();

    // Button events from the app layer (btn5).
    void on_button_short();
    void on_button_long();  // stop + clear from any state

    // Call on every manual drum trigger.
    // In Arming: starts Recording, captures this hit at t=0.
    // In Recording: captures the hit at the current record position.
    // Other states: no-op.
    void on_drum_hit(uint8_t sample_id);

    // Advance the internal clock by n_samples.
    // In Recording: tracks elapsed time; auto-finalises at
    //               LOOPER_MAX_DURATION_S * SAMPLE_RATE.
    // In Playing:   emits triggered sample IDs into out_sample_ids[0..return-1].
    //               max_out is the capacity of out_sample_ids.
    // Returns number of events fired this tick (0 in Recording/Idle/Arming).
    size_t tick(uint32_t n_samples, uint8_t* out_sample_ids, size_t max_out);

    LooperState state() const { return state_; }

    // Stop playback/recording and clear all loop data. Idempotent.
    // Called by the auto-drummer for mutual exclusion.
    void stop();

  private:
    struct Event {
        uint32_t time_samples;
        uint8_t  sample_id;
    };

    void store_event(uint8_t sample_id);
    void start_playing();

    LooperState state_;
    Event       events_[LOOPER_MAX_EVENTS];
    uint16_t    event_count_;
    uint32_t    record_pos_;   // samples since t0, during Recording
    uint32_t    play_pos_;     // playhead offset within loop, during Playing
    uint32_t    loop_length_;  // loop duration in samples
    uint16_t    play_idx_;     // index of next event to emit
};

}} // namespace dummer::audio
