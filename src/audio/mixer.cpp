#include "mixer.hpp"
#include "reverb.hpp"
#include "voice.hpp"

namespace dummer
{
namespace audio
{

Mixer::Mixer(VoicePool& pool, const SampleDescriptor* table, uint8_t table_size, Reverb* reverb)
    : pool_(&pool), table_(table), table_size_(table_size), reverb_(reverb)
{
}

static void mix_voice(Voice& vc, const SampleDescriptor& d, int32_t* acc, size_t n)
{
#if ENABLE_END_RAMP
    const uint32_t end_ramp_start =
        (d.length > VOICE_END_RAMP_SAMPLES)
            ? d.length - static_cast<uint32_t>(VOICE_END_RAMP_SAMPLES)
            : 0;
#endif

    for (size_t i = 0; i < n; ++i)
    {
        if (vc.position >= d.length)
        {
            vc.deactivate();
            break;
        }
        const int32_t sample = static_cast<int32_t>(d.data[vc.position]);
        int32_t       voiced = (sample * vc.gain_q15) >> 15;
#if ENABLE_END_RAMP
        if (vc.position >= end_ramp_start)
        {
            const int32_t samples_left = static_cast<int32_t>(d.length - vc.position);
            const int32_t ramp_q15     = samples_left * (32768 / VOICE_END_RAMP_SAMPLES);
            voiced                     = (voiced * ramp_q15) >> 15;
        }
#endif
        acc[i] += voiced;
        ++vc.position;
#if ENABLE_RETRIGGER_DECAY
        if (vc.fade == FadeState::RetriggerFadeOut)
        {
            const int32_t decayed =
                (static_cast<int32_t>(vc.gain_q15) * RETRIGGER_FADE_K_Q15) >> 15;
            vc.gain_q15 = static_cast<int16_t>(decayed);
            if (vc.gain_q15 == 0)
            {
                vc.deactivate();
                break;
            }
        }
#endif
    }
}

void Mixer::get_samples(int16_t* dst, size_t n)
{
    constexpr size_t kMaxChunk = static_cast<size_t>(AUDIO_CHUNK);
    if (n > kMaxChunk)
        n = kMaxChunk;

    int32_t acc[AUDIO_CHUNK];
    for (size_t i = 0; i < n; ++i)
        acc[i] = 0;

    Voice* voices = pool_->voices();
    for (uint16_t v = 0; v < VoicePool::CAPACITY; ++v)
    {
        Voice& vc = voices[v];
        if (!vc.active())
            continue;
        if (vc.sample_id >= table_size_)
        {
            vc.deactivate();
            continue;
        }

        const SampleDescriptor& d = table_[vc.sample_id];
        if (d.data == nullptr || d.length == 0)
        {
            vc.deactivate();
            continue;
        }

        mix_voice(vc, d, acc, n);
    }

    if (reverb_ != nullptr)
    {
        reverb_->process_inplace(acc, n);
    }

    for (size_t i = 0; i < n; ++i)
    {
        int32_t s = acc[i];
        if (s > INT16_MAX)
            s = INT16_MAX;
        if (s < INT16_MIN)
            s = INT16_MIN;
        dst[i] = static_cast<int16_t>(s);
    }
}

} // namespace audio
} // namespace dummer
