// Unit tests for Humanizer — LFSR PRNG, timing jitter, velocity jitter.
#ifdef BUILD_NATIVE_TEST

#include <unity.h>

#include "defines.hpp"
#include "audio/humanizer.hpp"

using ::dummer::audio::Humanizer;

void setUp()
{
}
void tearDown()
{
}

// ---------------------------------------------------------------------------
// LFSR
// ---------------------------------------------------------------------------

void test_lfsr_never_returns_zero()
{
    // If the LFSR ever locks at 0 it produces 0 forever; verify it stays live
    // by confirming jitter() produces non-zero values over many iterations.
    Humanizer h;
    h.set_enabled(true);
    bool seen_nonzero = false;
    for (int i = 0; i < 200000 && !seen_nonzero; ++i)
    {
        if (h.jitter(1) != 0)
            seen_nonzero = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(seen_nonzero, "Galois LFSR state must never be 0");
}

void test_same_seed_is_deterministic()
{
    Humanizer a(0x1234);
    Humanizer b(0x1234);
    a.set_enabled(true);
    b.set_enabled(true);
    for (int i = 0; i < 1000; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(a.jitter(100), b.jitter(100));
    }
}

void test_reseed_restarts_stream()
{
    Humanizer h(0xBEEF);
    h.set_enabled(true);
    int32_t first[16];
    for (int i = 0; i < 16; ++i)
        first[i] = h.jitter(100);

    h.reseed(0xBEEF);
    for (int i = 0; i < 16; ++i)
    {
        TEST_ASSERT_EQUAL_INT32_MESSAGE(first[i], h.jitter(100),
                                        "reseed must reproduce the same stream");
    }
}

void test_zero_seed_falls_back_to_default()
{
    // A zero LFSR state would lock up; the ctor must substitute the default.
    Humanizer h(0);
    h.set_enabled(true);
    bool any_nonzero = false;
    for (int i = 0; i < 32; ++i)
    {
        if (h.jitter(1) != 0)
        {
            any_nonzero = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_nonzero, "zero seed must not lock the LFSR at 0");
}

// ---------------------------------------------------------------------------
// timing jitter
// ---------------------------------------------------------------------------

void test_jitter_stays_within_range_and_spans_both_signs()
{
    Humanizer h;
    h.set_enabled(true);
    int32_t lo = 1000, hi = -1000;
    for (int i = 0; i < 5000; ++i)
    {
        const int32_t j = h.jitter(HUMANIZE_TIME_SAMPLES);
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(-HUMANIZE_TIME_SAMPLES, j);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(HUMANIZE_TIME_SAMPLES, j);
        if (j < lo)
            lo = j;
        if (j > hi)
            hi = j;
    }
    TEST_ASSERT_LESS_THAN_MESSAGE(0, lo, "jitter should produce negative offsets");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, hi, "jitter should produce positive offsets");
}

void test_jitter_zero_when_disabled()
{
    Humanizer h;
    h.set_enabled(false);
    for (int i = 0; i < 1000; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(0, h.jitter(HUMANIZE_TIME_SAMPLES));
    }
}

void test_jitter_zero_for_zero_range()
{
    Humanizer h;
    h.set_enabled(true);
    for (int i = 0; i < 100; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(0, h.jitter(0));
        TEST_ASSERT_EQUAL_INT32(0, h.jitter(-5));
    }
}

// ---------------------------------------------------------------------------
// velocity jitter
// ---------------------------------------------------------------------------

void test_velocity_clamped_to_midi_range()
{
    Humanizer h;
    h.set_enabled(true);
    for (int i = 0; i < 5000; ++i)
    {
        const uint8_t v1   = h.humanize_velocity(1);   // near floor
        const uint8_t v2   = h.humanize_velocity(127); // near ceiling
        const uint8_t vmid = h.humanize_velocity(64);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, v1);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(127, v2);
        TEST_ASSERT_TRUE(vmid >= 1 && vmid <= 127);
    }
}

void test_velocity_passthrough_when_disabled()
{
    Humanizer h;
    h.set_enabled(false);
    for (int i = 0; i < 100; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(64, h.humanize_velocity(64));
        TEST_ASSERT_EQUAL_UINT8(100, h.humanize_velocity(100));
    }
}

void test_velocity_varies_within_eight_when_enabled()
{
    Humanizer h;
    h.set_enabled(true);
    bool varied = false;
    for (int i = 0; i < 2000; ++i)
    {
        const int v = static_cast<int>(h.humanize_velocity(64));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(64 - HUMANIZE_VEL, v);
        TEST_ASSERT_LESS_OR_EQUAL_INT(64 + HUMANIZE_VEL, v);
        if (v != 64)
            varied = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(varied, "enabled humanizer must actually vary velocity");
}

// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_lfsr_never_returns_zero);
    RUN_TEST(test_same_seed_is_deterministic);
    RUN_TEST(test_reseed_restarts_stream);
    RUN_TEST(test_zero_seed_falls_back_to_default);
    RUN_TEST(test_jitter_stays_within_range_and_spans_both_signs);
    RUN_TEST(test_jitter_zero_when_disabled);
    RUN_TEST(test_jitter_zero_for_zero_range);
    RUN_TEST(test_velocity_clamped_to_midi_range);
    RUN_TEST(test_velocity_passthrough_when_disabled);
    RUN_TEST(test_velocity_varies_within_eight_when_enabled);
    return UNITY_END();
}

#else
int main()
{
    return 0;
}
#endif // BUILD_NATIVE_TEST
