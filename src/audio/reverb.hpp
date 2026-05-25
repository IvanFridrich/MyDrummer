// Reverb — classic Schroeder topology (Freeverb-style).
//
//   in → [4 parallel comb filters, summed] → [allpass 556] → [allpass 441] → wet
//   out = (1 - wet_gain) * in + wet_gain * wet
//
// Delay lines hold int16, processing uses int32 accumulators with saturation
// at every delay-line write. Total static RAM ~11.6 KB.
//
// Off by default (REVERB_ENABLED_DEFAULT). Runtime toggle via set_enabled().
#pragma once

#include "defines.hpp"

#include <stddef.h>
#include <stdint.h>

namespace dummer { namespace audio {

class Reverb {
  public:
    Reverb();

    // In-place processing on the mixer's int32 accumulator. Each sample is
    // replaced with the wet/dry mix. No-op when disabled.
    void process_inplace(int32_t* acc, size_t n);

    void set_enabled(bool on) { enabled_ = on; }
    bool enabled() const      { return enabled_; }

    // Clear all delay-line state. Call when toggling on, or in tests.
    void reset();

  private:
    // Comb-filter delay lengths from spec section 5: { 1116, 1188, 1277, 1356 }.
    static constexpr uint32_t kCombLen0 = 1116;
    static constexpr uint32_t kCombLen1 = 1188;
    static constexpr uint32_t kCombLen2 = 1277;
    static constexpr uint32_t kCombLen3 = 1356;

    // Allpass delay lengths: { 556, 441 }.
    static constexpr uint32_t kAllpassLen0 = 556;
    static constexpr uint32_t kAllpassLen1 = 441;

    int16_t comb0_buf_[kCombLen0];
    int16_t comb1_buf_[kCombLen1];
    int16_t comb2_buf_[kCombLen2];
    int16_t comb3_buf_[kCombLen3];
    int16_t allpass0_buf_[kAllpassLen0];
    int16_t allpass1_buf_[kAllpassLen1];

    uint32_t comb0_idx_;
    uint32_t comb1_idx_;
    uint32_t comb2_idx_;
    uint32_t comb3_idx_;
    uint32_t allpass0_idx_;
    uint32_t allpass1_idx_;

    bool enabled_;
};

}} // namespace dummer::audio
