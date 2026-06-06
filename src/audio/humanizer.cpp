// Humanizer implementation — see humanizer.hpp for the API contract.
#include "audio/humanizer.hpp"

namespace dummer { namespace audio {

// 16-bit Galois LFSR, maximal-length taps (x^16 + x^14 + x^13 + x^11 + 1).
static constexpr uint16_t kLfsrTaps = 0xB400u;

Humanizer::Humanizer(uint16_t seed)
    : state_(seed == 0u ? static_cast<uint16_t>(HUMANIZE_LFSR_SEED) : seed)
    , enabled_(HUMANIZE_ENABLED_DEFAULT != 0) {}

void Humanizer::reseed(uint16_t seed) {
    state_ = (seed == 0u) ? static_cast<uint16_t>(HUMANIZE_LFSR_SEED) : seed;
}

uint16_t Humanizer::next_u16() {
    const uint16_t lsb = static_cast<uint16_t>(state_ & 1u);
    state_ = static_cast<uint16_t>(state_ >> 1);
    if (lsb) state_ = static_cast<uint16_t>(state_ ^ kLfsrTaps);
    return state_;
}

int32_t Humanizer::jitter(int32_t range) {
    if (!enabled_ || range <= 0) return 0;
    const uint32_t span = static_cast<uint32_t>(2 * range + 1);
    const uint32_t r    = static_cast<uint32_t>(next_u16()) % span;
    return static_cast<int32_t>(r) - range;
}

uint8_t Humanizer::humanize_velocity(uint8_t base_vel) {
    if (!enabled_) return base_vel;
    int32_t v = static_cast<int32_t>(base_vel) + jitter(HUMANIZE_VEL);
    if (v < 1)   v = 1;
    if (v > 127) v = 127;
    return static_cast<uint8_t>(v);
}

}} // namespace dummer::audio
