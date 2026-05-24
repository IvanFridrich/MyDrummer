// VoicePool — fixed-size pool of MAX_VOICES voices, no heap allocation.
//
// trigger(sample_id) allocates a free slot, or steals the oldest active
// voice when the pool is full. Retrigger of the same sample marks the
// already-playing voice as RetriggerFadeOut (see ENABLE_RETRIGGER_DECAY
// in defines.hpp) and allocates a fresh slot for the new hit.
#pragma once

#include "defines.hpp"
#include "voice.hpp"

#include <stdint.h>

namespace dummer { namespace audio {

class VoicePool {
  public:
    static constexpr uint16_t CAPACITY = MAX_VOICES;

    VoicePool();

    // Start a new voice on the given sample. Allocates a free slot or
    // steals the oldest active voice if the pool is full.
    void trigger(uint8_t sample_id);

    // Direct access — the mixer iterates the array each chunk.
    Voice*       voices()       { return voices_; }
    const Voice* voices() const { return voices_; }

    uint16_t active_count() const;

  private:
    Voice    voices_[CAPACITY];
    uint32_t next_seq_;
};

}} // namespace dummer::audio
