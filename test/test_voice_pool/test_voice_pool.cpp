// Unit tests for VoicePool.
//
// Verifies slot allocation, free-slot reuse, pool-full steal-oldest, and
// natural deactivation on sample exhaustion (driven by the Mixer, but we
// invoke deactivate() directly here to keep this test focused).
#ifdef BUILD_NATIVE_TEST

#include <unity.h>

#include "defines.hpp"
#include "audio/voice.hpp"
#include "audio/voice_pool.hpp"

using ::dummer::audio::Voice;
using ::dummer::audio::VoicePool;

namespace {

uint16_t count_with_sample(const VoicePool& pool, uint8_t sid) {
    const Voice* v = pool.voices();
    uint16_t n = 0;
    for (uint16_t i = 0; i < VoicePool::CAPACITY; ++i) {
        if (v[i].active() && v[i].sample_id == sid) ++n;
    }
    return n;
}

} // namespace

void setUp() {}
void tearDown() {}

void test_pool_starts_empty() {
    VoicePool pool;
    TEST_ASSERT_EQUAL_UINT16(0, pool.active_count());
}

void test_single_trigger_allocates_one_voice() {
    VoicePool pool;
    pool.trigger(3);
    TEST_ASSERT_EQUAL_UINT16(1, pool.active_count());
    TEST_ASSERT_EQUAL_UINT16(1, count_with_sample(pool, 3));

    Voice* v = pool.voices();
    // The first triggered voice should land in slot 0.
    TEST_ASSERT_TRUE(v[0].active());
    TEST_ASSERT_EQUAL_UINT8(3, v[0].sample_id);
    TEST_ASSERT_EQUAL_UINT32(0, v[0].position);
    TEST_ASSERT_EQUAL_INT16((int16_t)MASTER_VOLUME_Q15, v[0].gain_q15);
}

void test_freed_slot_is_reused() {
    VoicePool pool;
    pool.trigger(1);
    pool.trigger(2);
    pool.trigger(3);
    TEST_ASSERT_EQUAL_UINT16(3, pool.active_count());

    // Naturally finish the middle voice.
    pool.voices()[1].deactivate();
    TEST_ASSERT_EQUAL_UINT16(2, pool.active_count());

    pool.trigger(4);
    // Slot 1 was the first free one, so the new voice should take it.
    TEST_ASSERT_TRUE(pool.voices()[1].active());
    TEST_ASSERT_EQUAL_UINT8(4, pool.voices()[1].sample_id);
    TEST_ASSERT_EQUAL_UINT16(3, pool.active_count());
}

void test_pool_full_steals_oldest() {
    VoicePool pool;

    // Fill the pool — each trigger gets a unique seq, increasing.
    for (uint16_t i = 0; i < VoicePool::CAPACITY; ++i) {
        pool.trigger((uint8_t)(i % 8));
    }
    TEST_ASSERT_EQUAL_UINT16(VoicePool::CAPACITY, pool.active_count());

    // Capture the oldest voice's seq (slot 0, the first triggered).
    const uint32_t oldest_seq = pool.voices()[0].trigger_seq;

    // Trigger once more — pool is full, so this should steal the oldest.
    pool.trigger(99);

    // Active count unchanged; total voices still == CAPACITY.
    TEST_ASSERT_EQUAL_UINT16(VoicePool::CAPACITY, pool.active_count());

    // The slot that held the oldest seq is now playing sample 99 with a
    // brand-new (larger) seq.
    bool found_new = false;
    for (uint16_t i = 0; i < VoicePool::CAPACITY; ++i) {
        const Voice& v = pool.voices()[i];
        if (v.sample_id == 99) {
            found_new = true;
            TEST_ASSERT_GREATER_THAN_UINT32(oldest_seq, v.trigger_seq);
            TEST_ASSERT_EQUAL_UINT32(0, v.position);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_new, "new sample 99 not found after steal");
}

void test_retrigger_fades_old_voice_and_starts_new() {
    // Retrigger: old voice is marked RetriggerFadeOut, a fresh voice is
    // allocated so the new hit starts at full gain with no hard cut.
    VoicePool pool;
    pool.trigger(5);
    TEST_ASSERT_EQUAL_UINT16(1, pool.active_count());

    pool.trigger(5);
    // Two voices active: the fading original + the fresh one.
    TEST_ASSERT_EQUAL_UINT16(2, pool.active_count());

    // Original slot (index 0) must be in fade-out.
    TEST_ASSERT_TRUE(pool.voices()[0].active());
    TEST_ASSERT_EQUAL_INT(
        (int)dummer::audio::FadeState::RetriggerFadeOut,
        (int)pool.voices()[0].fade);

    // Fresh slot (index 1) starts clean at position 0, fade None.
    TEST_ASSERT_TRUE(pool.voices()[1].active());
    TEST_ASSERT_EQUAL_UINT32(0, pool.voices()[1].position);
    TEST_ASSERT_EQUAL_INT(
        (int)dummer::audio::FadeState::None,
        (int)pool.voices()[1].fade);
}

void test_trigger_seq_monotonic() {
    VoicePool pool;
    pool.trigger(0);
    const uint32_t s0 = pool.voices()[0].trigger_seq;
    pool.trigger(1);
    const uint32_t s1 = pool.voices()[1].trigger_seq;
    pool.trigger(2);
    const uint32_t s2 = pool.voices()[2].trigger_seq;
    TEST_ASSERT_GREATER_THAN_UINT32(s0, s1);
    TEST_ASSERT_GREATER_THAN_UINT32(s1, s2);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pool_starts_empty);
    RUN_TEST(test_single_trigger_allocates_one_voice);
    RUN_TEST(test_freed_slot_is_reused);
    RUN_TEST(test_pool_full_steals_oldest);
    RUN_TEST(test_retrigger_fades_old_voice_and_starts_new);
    RUN_TEST(test_trigger_seq_monotonic);
    return UNITY_END();
}

#else
int main() { return 0; }
#endif // BUILD_NATIVE_TEST
