// Unit tests for Mixer.
//
// Tests inject a tiny fake SampleDescriptor table so they don't depend on
// the auto-generated kSampleTable. The mixer's Q15-multiply, accumulator
// summation, saturating clip, and end-of-sample deactivation are all
// exercised.
#ifdef BUILD_NATIVE_TEST

#include <unity.h>
#include <string.h>

#include "defines.hpp"
#include "audio/voice.hpp"
#include "audio/voice_pool.hpp"
#include "audio/mixer.hpp"
#include "samples.hpp"

using ::dummer::audio::Mixer;
using ::dummer::audio::VoicePool;
using ::dummer::audio::Voice;
using ::dummer::audio::SampleDescriptor;

namespace {

// Mirrors the mixer's Q15 multiply so tests can compute expected values.
int16_t mul_q15(int16_t s, int16_t g) {
    return (int16_t)(((int32_t)s * g) >> 15);
}

// Saturating clip helper for sum-of-voices expected values.
int16_t sat_i16(int32_t s) {
    if (s >  32767) s =  32767;
    if (s < -32768) s = -32768;
    return (int16_t)s;
}

constexpr int16_t kRamp[]   = { 100, 200, 300, 400, 500, 600, 700, 800 };
constexpr int16_t kSteady[] = { -100, -100, -100, -100, -100, -100, -100, -100 };
constexpr int16_t kLoud[]   = { 30000, 30000, 30000, 30000 };

// 2000-frame constant sample (every frame = 1000). 2000 frames is longer than
// the worst-case retrigger fade-out tail (~1250 samples from MASTER_VOLUME_Q15
// at coefficient 32530/32768), so the fading voice deactivates from gain → 0
// while the fresh voice is still mid-sample.
#define R10(x)    x, x, x, x, x, x, x, x, x, x
#define R100(x)   R10(x), R10(x), R10(x), R10(x), R10(x), R10(x), R10(x), R10(x), R10(x), R10(x)
#define R1000(x)  R100(x), R100(x), R100(x), R100(x), R100(x), R100(x), R100(x), R100(x), R100(x), R100(x)
#define R2000(x)  R1000(x), R1000(x)
constexpr int16_t kLong[2000] = { R2000(1000) };
#undef R10
#undef R100
#undef R1000
#undef R2000

constexpr SampleDescriptor kTable[] = {
    { kRamp,   sizeof(kRamp)   / sizeof(int16_t) },   // sid 0
    { kSteady, sizeof(kSteady) / sizeof(int16_t) },   // sid 1
    { kLoud,   sizeof(kLoud)   / sizeof(int16_t) },   // sid 2
    { kLoud,   sizeof(kLoud)   / sizeof(int16_t) },   // sid 3 — second loud (different id)
    { nullptr, 0 },                                   // sid 4 — empty
    { kLong,   sizeof(kLong)   / sizeof(int16_t) },   // sid 5 — long constant sample
};
constexpr uint8_t kTableSize = (uint8_t)(sizeof(kTable) / sizeof(kTable[0]));

} // namespace

void setUp() {}
void tearDown() {}

void test_silence_when_no_voices() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    int16_t buf[8];
    memset(buf, 0xAB, sizeof(buf));
    mx.get_samples(buf, 8);
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, buf[i]);
    }
}

void test_single_voice_plays_through() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(0);  // kRamp
    TEST_ASSERT_EQUAL_UINT16(1, pool.active_count());

    int16_t buf[8];
    mx.get_samples(buf, 8);

    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_INT16(mul_q15(kRamp[i], (int16_t)MASTER_VOLUME_Q15), buf[i]);
    }

    // After consuming all 8 samples, voice should deactivate on the next
    // generation pass (position == length triggers deactivate inside the
    // per-sample loop).
    int16_t tail[4];
    mx.get_samples(tail, 4);
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_INT16(0, tail[i]);
    TEST_ASSERT_EQUAL_UINT16(0, pool.active_count());
}

void test_two_voices_sum() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(0);   // kRamp
    pool.trigger(1);   // kSteady

    int16_t buf[8];
    mx.get_samples(buf, 8);

    for (int i = 0; i < 8; ++i) {
        int32_t s = (int32_t)mul_q15(kRamp[i],   (int16_t)MASTER_VOLUME_Q15)
                  + (int32_t)mul_q15(kSteady[i], (int16_t)MASTER_VOLUME_Q15);
        TEST_ASSERT_EQUAL_INT16(sat_i16(s), buf[i]);
    }
}

void test_saturating_clip_on_loud_sum() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    // Trigger two loud voices (different sample IDs so no retrigger fade) —
    // sum should clip at int16 max.
    pool.trigger(2);
    pool.trigger(3);

    int16_t buf[4];
    mx.get_samples(buf, 4);

    for (int i = 0; i < 4; ++i) {
        int32_t naive = 2 * (int32_t)mul_q15(kLoud[i], (int16_t)MASTER_VOLUME_Q15);
        // At unity master volume this naive sum exceeds int16 range and the
        // mixer must saturate. With software attenuation the sum may stay
        // in range; either way, output matches the saturated naive value.
        TEST_ASSERT_EQUAL_INT16(sat_i16(naive), buf[i]);
    }
}

void test_position_advances_across_chunks() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(0);  // kRamp, length 8

    int16_t chunk1[3];
    mx.get_samples(chunk1, 3);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_INT16(mul_q15(kRamp[i], (int16_t)MASTER_VOLUME_Q15), chunk1[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(3, pool.voices()[0].position);

    int16_t chunk2[3];
    mx.get_samples(chunk2, 3);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_INT16(mul_q15(kRamp[3 + i], (int16_t)MASTER_VOLUME_Q15), chunk2[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(6, pool.voices()[0].position);
}

void test_empty_descriptor_is_safe() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(4);   // empty descriptor
    TEST_ASSERT_EQUAL_UINT16(1, pool.active_count());

    int16_t buf[4];
    mx.get_samples(buf, 4);
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_INT16(0, buf[i]);

    // Voice should have been deactivated by the mixer's guard.
    TEST_ASSERT_EQUAL_UINT16(0, pool.active_count());
}

void test_retrigger_fade_decays_old_voice_gain() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(5);  // long sample
    const int16_t initial_gain = pool.voices()[0].gain_q15;
    TEST_ASSERT_EQUAL_INT16((int16_t)MASTER_VOLUME_Q15, initial_gain);

    pool.trigger(5);  // retrigger — voice 0 marked fading, voice 1 fresh
    TEST_ASSERT_EQUAL_UINT16(2, pool.active_count());

    int16_t buf[16];
    mx.get_samples(buf, 16);

    // Voice 0 (the fader) should still be active but with reduced gain.
    TEST_ASSERT_TRUE(pool.voices()[0].active());
    TEST_ASSERT_TRUE_MESSAGE(
        pool.voices()[0].gain_q15 < initial_gain,
        "retrigger-fade voice gain must decrease after a mix chunk");
    TEST_ASSERT_TRUE(pool.voices()[0].gain_q15 > 0);

    // Voice 1 (the fresh hit) must still be at full gain.
    TEST_ASSERT_EQUAL_INT16((int16_t)MASTER_VOLUME_Q15, pool.voices()[1].gain_q15);

    // Output is non-zero (sum of fading + fresh sample value 1000).
    TEST_ASSERT_TRUE(buf[0] != 0);
}

void test_retrigger_fade_eventually_deactivates_old_voice() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(5);
    pool.trigger(5);
    TEST_ASSERT_EQUAL_UINT16(2, pool.active_count());

    int16_t buf[AUDIO_CHUNK];
    // Run enough chunks for the exponential fade to reach 0.
    // ~23 ms = ~1015 samples at 44.1 kHz. With AUDIO_CHUNK = 64, ~16 chunks
    // is plenty. The long sample is 1000 frames so the fresh voice survives.
    for (int i = 0; i < 20; ++i) mx.get_samples(buf, AUDIO_CHUNK);

    // The fading voice must be deactivated by now; only the fresh one remains.
    TEST_ASSERT_EQUAL_UINT16(1, pool.active_count());
}

void test_invalid_sample_id_is_safe() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    // Force a voice to a sample_id outside the table.
    pool.voices()[0].start(/*sid=*/200, /*seq=*/1);
    TEST_ASSERT_EQUAL_UINT16(1, pool.active_count());

    int16_t buf[4];
    mx.get_samples(buf, 4);
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_INT16(0, buf[i]);
    TEST_ASSERT_EQUAL_UINT16(0, pool.active_count());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_silence_when_no_voices);
    RUN_TEST(test_single_voice_plays_through);
    RUN_TEST(test_two_voices_sum);
    RUN_TEST(test_saturating_clip_on_loud_sum);
    RUN_TEST(test_position_advances_across_chunks);
    RUN_TEST(test_empty_descriptor_is_safe);
    RUN_TEST(test_retrigger_fade_decays_old_voice_gain);
    RUN_TEST(test_retrigger_fade_eventually_deactivates_old_voice);
    RUN_TEST(test_invalid_sample_id_is_safe);
    return UNITY_END();
}

#else
int main() { return 0; }
#endif // BUILD_NATIVE_TEST
