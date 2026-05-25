// Mixer — sums all active voices, optionally runs the int32 accumulator
// through Reverb, then saturate-clips to int16.
//
// Sample table is passed in at construction so tests can inject fakes.
// Reverb is optional (pass nullptr to skip); the toggle on btn3 flips
// reverb_->enabled() at runtime.
#pragma once

#include "defines.hpp"
#include "samples.hpp"
#include "voice_pool.hpp"

#include <stddef.h>
#include <stdint.h>

namespace dummer { namespace audio {

class Reverb;  // forward — only the pointer is used here

class Mixer {
  public:
    Mixer(VoicePool& pool, const SampleDescriptor* table, uint8_t table_size,
          Reverb* reverb = nullptr);

    // Fill dst[0..n-1] with the next n mono int16 samples. n must be
    // <= AUDIO_CHUNK; larger requests are clamped. Voices whose sample
    // data is exhausted are deactivated.
    void get_samples(int16_t* dst, size_t n);

    Reverb* reverb() { return reverb_; }

  private:
    VoicePool*              pool_;
    const SampleDescriptor* table_;
    uint8_t                 table_size_;
    Reverb*                 reverb_;
};

}} // namespace dummer::audio
