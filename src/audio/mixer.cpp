#include "mixer.hpp"
#include "voice.hpp"

namespace dummer { namespace audio {

Mixer::Mixer(VoicePool& pool, const SampleDescriptor* table, uint8_t table_size)
    : pool_(&pool), table_(table), table_size_(table_size) {}

void Mixer::get_samples(int16_t* dst, size_t n) {
    constexpr size_t kMaxChunk = static_cast<size_t>(AUDIO_CHUNK);
    if (n > kMaxChunk) n = kMaxChunk;

    int32_t acc[AUDIO_CHUNK];
    for (size_t i = 0; i < n; ++i) acc[i] = 0;

    Voice* voices = pool_->voices();
    for (uint16_t v = 0; v < VoicePool::CAPACITY; ++v) {
        Voice& vc = voices[v];
        if (!vc.active()) continue;
        if (vc.sample_id >= table_size_) { vc.deactivate(); continue; }

        const SampleDescriptor& d = table_[vc.sample_id];
        if (d.data == nullptr || d.length == 0) { vc.deactivate(); continue; }

        for (size_t i = 0; i < n; ++i) {
            if (vc.position >= d.length) { vc.deactivate(); break; }
            const int32_t sample = static_cast<int32_t>(d.data[vc.position]);
            acc[i] += (sample * vc.gain_q15) >> 15;
            ++vc.position;
#if ENABLE_RETRIGGER_DECAY
            if (vc.fade == FadeState::RetriggerFadeOut) {
                const int32_t decayed =
                    (static_cast<int32_t>(vc.gain_q15) * RETRIGGER_FADE_K_Q15) >> 15;
                vc.gain_q15 = static_cast<int16_t>(decayed);
                if (vc.gain_q15 == 0) { vc.deactivate(); break; }
            }
#endif
        }
    }

    for (size_t i = 0; i < n; ++i) {
        int32_t s = acc[i];
        if (s >  INT16_MAX) s = INT16_MAX;
        if (s <  INT16_MIN) s = INT16_MIN;
        dst[i] = static_cast<int16_t>(s);
    }
}

}} // namespace dummer::audio
