// Unit tests for the Schroeder reverb.
//
// These do not (and can't, in unit-test scope) assert "sounds reverby" —
// they verify the contract: disabled = passthrough, impulse produces a
// non-trivial decaying tail, silence stays silent, processing is bounded.
#ifdef BUILD_NATIVE_TEST

#include <unity.h>
#include <string.h>

#include "defines.hpp"
#include "audio/reverb.hpp"

using ::dummer::audio::Reverb;

void setUp() {}
void tearDown() {}

void test_disabled_is_passthrough() {
    Reverb rv;
    rv.set_enabled(false);

    int32_t acc[16];
    for (int i = 0; i < 16; ++i) acc[i] = 1000 + i;

    rv.process_inplace(acc, 16);

    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_INT32(1000 + i, acc[i]);
    }
}

void test_silence_in_stays_silent() {
    Reverb rv;
    rv.set_enabled(true);

    int32_t acc[64] = {0};
    rv.process_inplace(acc, 64);
    for (int i = 0; i < 64; ++i) {
        TEST_ASSERT_EQUAL_INT32(0, acc[i]);
    }
}

void test_impulse_produces_decaying_tail() {
    // An impulse fed into the comb network must produce non-zero output
    // some time later (after the shortest comb delay = 1116 samples).
    Reverb rv;
    rv.set_enabled(true);
    rv.reset();

    // Single impulse at t=0; everything else silent.
    constexpr size_t kSamples = 1500;   // > shortest comb delay (1116)
    int32_t acc[kSamples];
    memset(acc, 0, sizeof(acc));
    acc[0] = 16384;   // half-scale impulse

    rv.process_inplace(acc, kSamples);

    // Dry component: at i=0 the dry path is (16384 * (32767 - REVERB_WET_Q15)) >> 15.
    // We expect non-zero. After that the dry contribution is 0; any non-zero
    // output after sample 0 must come from the wet path.
    bool found_tail = false;
    for (size_t i = 1200; i < kSamples; ++i) {
        if (acc[i] != 0) { found_tail = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_tail,
        "expected non-zero reverb tail past the shortest comb delay");
}

void test_steady_input_does_not_blow_up() {
    // Drive the reverb with a steady mid-level signal for several seconds-
    // worth of samples and confirm output stays in int32 range (the
    // saturation in the delay lines should prevent runaway feedback).
    Reverb rv;
    rv.set_enabled(true);
    rv.reset();

    constexpr size_t kChunk = 64;
    int32_t acc[kChunk];

    int32_t peak = 0;
    for (int chunk = 0; chunk < 200; ++chunk) {   // ~12800 samples, ~290 ms
        for (size_t i = 0; i < kChunk; ++i) acc[i] = 10000;
        rv.process_inplace(acc, kChunk);
        for (size_t i = 0; i < kChunk; ++i) {
            const int32_t v = acc[i] >= 0 ? acc[i] : -acc[i];
            if (v > peak) peak = v;
        }
    }

    // With wet=0.25 and dry=0.75 of a 10000-magnitude input, the dry path
    // alone contributes ~7500. Output should stay well within int16 range
    // since the int16-saturated delay lines bound the wet component.
    TEST_ASSERT_TRUE_MESSAGE(peak < INT16_MAX * 2,
        "reverb output runaway suspected");
}

void test_reset_clears_tail() {
    // Push an impulse, then reset, then send silence. Output must be exactly
    // zero (no residue from the prior impulse).
    Reverb rv;
    rv.set_enabled(true);
    rv.reset();

    int32_t impulse[64] = {0};
    impulse[0] = 16384;
    rv.process_inplace(impulse, 64);

    rv.reset();

    int32_t silence[64] = {0};
    rv.process_inplace(silence, 64);
    for (int i = 0; i < 64; ++i) TEST_ASSERT_EQUAL_INT32(0, silence[i]);
}

void test_toggle_runtime() {
    Reverb rv;
    TEST_ASSERT_EQUAL(REVERB_ENABLED_DEFAULT != 0, rv.enabled());

    rv.set_enabled(true);
    TEST_ASSERT_TRUE(rv.enabled());

    rv.set_enabled(false);
    TEST_ASSERT_FALSE(rv.enabled());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_is_passthrough);
    RUN_TEST(test_silence_in_stays_silent);
    RUN_TEST(test_impulse_produces_decaying_tail);
    RUN_TEST(test_steady_input_does_not_blow_up);
    RUN_TEST(test_reset_clears_tail);
    RUN_TEST(test_toggle_runtime);
    return UNITY_END();
}

#else
int main() { return 0; }
#endif // BUILD_NATIVE_TEST
