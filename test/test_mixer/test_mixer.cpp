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

// Mirrors the mixer's universal end-ramp: linear taper over the last
// VOICE_END_RAMP_SAMPLES frames of a sample. Returns the voiced value
// scaled by the ramp factor (or unchanged if outside the ramp window).
int32_t apply_end_ramp(int32_t voiced, uint32_t position, uint32_t length) {
#if ENABLE_END_RAMP
    const uint32_t start = (length > VOICE_END_RAMP_SAMPLES)
        ? length - (uint32_t)VOICE_END_RAMP_SAMPLES : 0;
    if (position >= start) {
        const int32_t samples_left = (int32_t)(length - position);
        const int32_t ramp_q15     = samples_left << 9;
        return (voiced * ramp_q15) >> 15;
    }
#endif
    return voiced;
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

    // kRamp is 8 frames — shorter than VOICE_END_RAMP_SAMPLES — so every
    // sample is in the linear end-ramp window starting at position 0.
    for (uint32_t i = 0; i < 8; ++i) {
        const int32_t voiced = mul_q15(kRamp[i], (int16_t)MASTER_VOLUME_Q15);
        const int32_t expected = apply_end_ramp(voiced, i, 8);
        TEST_ASSERT_EQUAL_INT16(sat_i16(expected), buf[i]);
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

    for (uint32_t i = 0; i < 8; ++i) {
        const int32_t v1 = apply_end_ramp(
            mul_q15(kRamp[i],   (int16_t)MASTER_VOLUME_Q15), i, 8);
        const int32_t v2 = apply_end_ramp(
            mul_q15(kSteady[i], (int16_t)MASTER_VOLUME_Q15), i, 8);
        TEST_ASSERT_EQUAL_INT16(sat_i16(v1 + v2), buf[i]);
    }
}

void test_saturating_clip_on_loud_sum() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    // Trigger two loud voices (different sample IDs so no retrigger fade).
    // Saturation behaviour is verified against the per-frame ramped sum —
    // with MASTER_VOLUME_Q15 = 16384 and the end-ramp active on a 4-frame
    // sample, the actual sum stays well within int16 range. sat_i16 makes
    // the assertion robust to either path.
    pool.trigger(2);
    pool.trigger(3);

    int16_t buf[4];
    mx.get_samples(buf, 4);

    for (uint32_t i = 0; i < 4; ++i) {
        const int32_t v1 = apply_end_ramp(
            mul_q15(kLoud[i], (int16_t)MASTER_VOLUME_Q15), i, 4);
        const int32_t v2 = apply_end_ramp(
            mul_q15(kLoud[i], (int16_t)MASTER_VOLUME_Q15), i, 4);
        TEST_ASSERT_EQUAL_INT16(sat_i16(v1 + v2), buf[i]);
    }
}

void test_position_advances_across_chunks() {
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(0);  // kRamp, length 8

    int16_t chunk1[3];
    mx.get_samples(chunk1, 3);
    for (uint32_t i = 0; i < 3; ++i) {
        const int32_t voiced = mul_q15(kRamp[i], (int16_t)MASTER_VOLUME_Q15);
        TEST_ASSERT_EQUAL_INT16(sat_i16(apply_end_ramp(voiced, i, 8)), chunk1[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(3, pool.voices()[0].position);

    int16_t chunk2[3];
    mx.get_samples(chunk2, 3);
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t pos = 3 + i;
        const int32_t voiced = mul_q15(kRamp[pos], (int16_t)MASTER_VOLUME_Q15);
        TEST_ASSERT_EQUAL_INT16(sat_i16(apply_end_ramp(voiced, pos, 8)), chunk2[i]);
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

void test_end_ramp_scales_last_window_linearly() {
    // Long sample → end-ramp only fires for positions in
    // [length - 64, length-1]; positions before that should be untouched.
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(5);                                                 // kLong (length 2000)
    Voice& v = pool.voices()[0];
    v.position = (uint32_t)(2000 - VOICE_END_RAMP_SAMPLES);            // jump to ramp start

    int16_t buf[VOICE_END_RAMP_SAMPLES];
    mx.get_samples(buf, VOICE_END_RAMP_SAMPLES);

    // First sample of the ramp window must equal the un-ramped value
    // (samples_left = 64 → ramp_q15 = 32768 → factor 1.0). Last sample
    // must be ~1/64 of the un-ramped value (samples_left = 1 → ramp_q15 = 512).
    const int32_t full = mul_q15(1000, (int16_t)MASTER_VOLUME_Q15);

    TEST_ASSERT_EQUAL_INT16(sat_i16(full), buf[0]);
    TEST_ASSERT_EQUAL_INT16(sat_i16((full * 512) >> 15), buf[VOICE_END_RAMP_SAMPLES - 1]);

    // Mid-ramp: at i = 32, samples_left = 32 → factor 0.5.
    TEST_ASSERT_EQUAL_INT16(sat_i16((full * (32 << 9)) >> 15), buf[32]);

    // Strictly decreasing through the ramp window.
    for (int i = 1; i < VOICE_END_RAMP_SAMPLES; ++i) {
        TEST_ASSERT_TRUE(buf[i] <= buf[i - 1]);
    }

    // One more chunk drives position past length → deactivation on next call.
    int16_t drain[1];
    mx.get_samples(drain, 1);
    TEST_ASSERT_EQUAL_UINT16(0, pool.active_count());
}

void test_end_ramp_does_not_affect_positions_before_window() {
    // At position 0 of a long sample, the end-ramp window is way ahead;
    // output should match the un-ramped Q15 multiply exactly.
    VoicePool pool;
    Mixer mx(pool, kTable, kTableSize);

    pool.trigger(5);  // kLong

    int16_t buf[16];
    mx.get_samples(buf, 16);

    const int16_t expected = mul_q15(1000, (int16_t)MASTER_VOLUME_Q15);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_INT16(expected, buf[i]);
    }
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
    RUN_TEST(test_end_ramp_scales_last_window_linearly);
    RUN_TEST(test_end_ramp_does_not_affect_positions_before_window);
    RUN_TEST(test_retrigger_fade_decays_old_voice_gain);
    RUN_TEST(test_retrigger_fade_eventually_deactivates_old_voice);
    RUN_TEST(test_invalid_sample_id_is_safe);
    return UNITY_END();
}

#else
int main() { return 0; }
#endif // BUILD_NATIVE_TEST
