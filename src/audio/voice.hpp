// Voice — iterator over a SampleDescriptor with Q15 gain.
//
// One Voice plays one SampleDescriptor from `position` to `length`. When
// position reaches the end the voice becomes inactive. `fade` drives the
// retrigger fade-out (and, later, the end-of-sample ramp) inside Mixer.
#pragma once

#include "samples.hpp"

#include <stdint.h>

namespace dummer { namespace audio {

enum class FadeState : uint8_t {
    None = 0,
    RetriggerFadeOut,   // exponentially decay gain_q15 each sample, then stop
};

struct Voice {
    static constexpr uint8_t INACTIVE = 0xFF;

    uint8_t   sample_id;        // INACTIVE when slot is free
    uint32_t  position;         // index into SampleDescriptor::data
    int16_t   gain_q15;         // Q15 amplitude; 32767 = full scale
    FadeState fade;
    uint32_t  trigger_seq;      // monotonically increasing; lowest = oldest

    bool active() const { return sample_id != INACTIVE; }

    // Reset to a freshly-triggered state on the given sample.
    void start(uint8_t sid, uint32_t seq) {
        sample_id   = sid;
        position    = 0;
        gain_q15    = 32767;
        fade        = FadeState::None;
        trigger_seq = seq;
    }

    void deactivate() {
        sample_id = INACTIVE;
        position  = 0;
        gain_q15  = 0;
        fade      = FadeState::None;
    }
};

}} // namespace dummer::audio
