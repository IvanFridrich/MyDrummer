// Mixer — sums all active voices, saturate-clips to int16, writes into the
// caller's mono buffer.
//
// Sample table is passed in at construction so tests can inject fakes.
// Milestone 4 will add the optional in-place reverb stage between
// accumulation and clipping.
#pragma once

#include "defines.hpp"
#include "samples.hpp"
#include "voice_pool.hpp"

#include <stddef.h>
#include <stdint.h>

namespace dummer { namespace audio {

class Mixer {
  public:
    Mixer(VoicePool& pool, const SampleDescriptor* table, uint8_t table_size);

    // Fill dst[0..n-1] with the next n mono int16 samples. n must be
    // <= AUDIO_CHUNK; larger requests are clamped. Voices whose sample
    // data is exhausted are deactivated.
    void get_samples(int16_t* dst, size_t n);

  private:
    VoicePool*              pool_;
    const SampleDescriptor* table_;
    uint8_t                 table_size_;
};

}} // namespace dummer::audio
