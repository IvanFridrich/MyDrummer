// Humanizer — deterministic LFSR-based timing/velocity jitter (Milestone 7).
//
// A 16-bit Galois LFSR provides a cheap, repeatable pseudo-random stream with
// no heap and no <random>. jitter(range) returns a value in [-range, +range];
// humanize_velocity() applies +/- HUMANIZE_VEL to a base MIDI velocity, clamped
// to [1, 127]. When disabled, both are pass-through (jitter = 0, velocity kept).
#pragma once

#include "defines.hpp"

#include <stdint.h>

namespace dummer { namespace audio {

class Humanizer {
  public:
    explicit Humanizer(uint16_t seed = HUMANIZE_LFSR_SEED);

    void set_enabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    // Restart the pseudo-random stream. A zero seed falls back to the default
    // (an all-zero LFSR state would lock up).
    void reseed(uint16_t seed);

    // Advance the LFSR and return the new 16-bit state (never 0).
    uint16_t next_u16();

    // Symmetric jitter in [-range, +range]. Returns 0 when disabled or range<=0.
    int32_t jitter(int32_t range);

    // base +/- HUMANIZE_VEL, clamped to [1, 127]. Pass-through when disabled.
    uint8_t humanize_velocity(uint8_t base_vel);

  private:
    uint16_t state_;
    bool     enabled_;
};

}} // namespace dummer::audio
