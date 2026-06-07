#include "voice_pool.hpp"

namespace dummer
{
namespace audio
{

VoicePool::VoicePool() : next_seq_(0)
{
    for (uint16_t i = 0; i < CAPACITY; ++i)
        voices_[i].deactivate();
}

#ifdef BUILD_ESP32
__attribute__((section(".iram1.text")))
#endif
void VoicePool::trigger(uint8_t sample_id, uint8_t velocity)
{
    // 1. Retrigger fade-out: mark any active voice for the same sample so the
    //    mixer exponentially decays its gain to silence. A fresh voice is then
    //    allocated below so the new hit starts cleanly at full gain.
#if ENABLE_RETRIGGER_DECAY
    for (uint16_t i = 0; i < CAPACITY; ++i)
    {
        if (voices_[i].active() && voices_[i].sample_id == sample_id &&
            voices_[i].fade == FadeState::None)
        {
            voices_[i].fade = FadeState::RetriggerFadeOut;
        }
    }
#endif
    // 2. Find a free slot.
    int slot = -1;
    for (uint16_t i = 0; i < CAPACITY; ++i)
    {
        if (!voices_[i].active())
        {
            slot = static_cast<int>(i);
            break;
        }
    }
    // 3. Pool full → steal the voice with the lowest trigger_seq (oldest).
    if (slot < 0)
    {
        uint32_t oldest_seq = voices_[0].trigger_seq;
        slot                = 0;
        for (uint16_t i = 1; i < CAPACITY; ++i)
        {
            if (voices_[i].trigger_seq < oldest_seq)
            {
                oldest_seq = voices_[i].trigger_seq;
                slot       = static_cast<int>(i);
            }
        }
    }
    voices_[slot].start(sample_id, ++next_seq_);
    if (velocity > MIDI_VELOCITY_MAX)
        velocity = MIDI_VELOCITY_MAX;
    const int32_t gain = (static_cast<int32_t>(MASTER_VOLUME_Q15) * velocity) / MIDI_VELOCITY_MAX;
    voices_[slot].gain_q15 = static_cast<int16_t>(gain);
}

uint16_t VoicePool::active_count() const
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < CAPACITY; ++i)
    {
        if (voices_[i].active())
            ++n;
    }
    return n;
}

} // namespace audio
} // namespace dummer
