#include "reverb.hpp"
#include "util/dsp.hpp"

#include <string.h>

namespace dummer
{
namespace audio
{

namespace
{

// Schroeder feedback-comb: y[n] = x[n] + g * y[n - M].
// Returns the delayed sample at the read pointer; stores input + feedback*bufout
// back into the buffer, advances index. All math in int32 with int16 saturation
// on the store.
inline int32_t comb_step(int16_t* buf, uint32_t length, uint32_t& idx, int32_t in)
{
    const int32_t bufout = buf[idx];
    const int32_t fed    = ::dummer::util::q15_mul(bufout, REVERB_COMB_FEEDBACK_Q15);
    buf[idx]             = ::dummer::util::sat_i16(in + fed);
    if (++idx >= length) idx = 0;
    return bufout;
}

// Freeverb-style allpass: out = -in + bufout; store = in + g * bufout.
inline int32_t allpass_step(int16_t* buf, uint32_t length, uint32_t& idx, int32_t in)
{
    const int32_t bufout = buf[idx];
    const int32_t store  = in + ::dummer::util::q15_mul(bufout, REVERB_ALLPASS_COEF_Q15);
    buf[idx]             = ::dummer::util::sat_i16(store);
    if (++idx >= length) idx = 0;
    return bufout - in;
}

} // namespace

Reverb::Reverb()
    : comb0_idx_(0), comb1_idx_(0), comb2_idx_(0), comb3_idx_(0), allpass0_idx_(0),
      allpass1_idx_(0), enabled_(REVERB_ENABLED_DEFAULT != 0)
{
    reset();
}

void Reverb::reset()
{
    memset(comb0_buf_, 0, sizeof(comb0_buf_));
    memset(comb1_buf_, 0, sizeof(comb1_buf_));
    memset(comb2_buf_, 0, sizeof(comb2_buf_));
    memset(comb3_buf_, 0, sizeof(comb3_buf_));
    memset(allpass0_buf_, 0, sizeof(allpass0_buf_));
    memset(allpass1_buf_, 0, sizeof(allpass1_buf_));
    comb0_idx_ = comb1_idx_ = comb2_idx_ = comb3_idx_ = 0;
    allpass0_idx_ = allpass1_idx_ = 0;
}

#ifdef BUILD_ESP32
__attribute__((section(".iram1.text")))
#endif
void Reverb::process_inplace(int32_t* acc, size_t n)
{
    if (!enabled_)
        return;

    constexpr int32_t kWet = REVERB_WET_Q15;
    constexpr int32_t kDry = Q15_UNITY - kWet;

    for (size_t i = 0; i < n; ++i)
    {
        // The pre-reverb accumulator may already exceed int16 — clip before
        // feeding it into the delay lines (which are int16).
        const int32_t dry = acc[i];
        const int32_t in  = ::dummer::util::sat_i16(dry);

        // 4 parallel feedback combs, summed.
        int32_t comb_sum = 0;
        comb_sum += comb_step(comb0_buf_, kCombLen0, comb0_idx_, in);
        comb_sum += comb_step(comb1_buf_, kCombLen1, comb1_idx_, in);
        comb_sum += comb_step(comb2_buf_, kCombLen2, comb2_idx_, in);
        comb_sum += comb_step(comb3_buf_, kCombLen3, comb3_idx_, in);

        // 2 series allpasses.
        int32_t ap = allpass_step(allpass0_buf_, kAllpassLen0, allpass0_idx_, comb_sum);
        ap         = allpass_step(allpass1_buf_, kAllpassLen1, allpass1_idx_, ap);

        // Wet/dry mix — keep the dry path in int32 so the saturating clip
        // downstream in the mixer still owns the final int16 conversion.
        acc[i] = ::dummer::util::q15_mul(dry, kDry) + ::dummer::util::q15_mul(ap, kWet);
    }
}

} // namespace audio
} // namespace dummer
