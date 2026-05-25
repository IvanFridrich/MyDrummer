// Unit tests for the Looper FSM.
//
// Tests cover state transitions, event capture, playback timing (including
// loop wrap), overflow guard, auto-finalise at max duration, and stop/clear.
#ifdef BUILD_NATIVE_TEST

#include <unity.h>
#include <string.h>

#include "defines.hpp"
#include "audio/looper.hpp"

using ::dummer::audio::Looper;
using ::dummer::audio::LooperState;

void setUp() {}
void tearDown() {}

// ── helpers ──────────────────────────────────────────────────────────────────

static uint8_t g_out[LOOPER_MAX_EVENTS];

static size_t do_tick(Looper& lo, uint32_t n = 64) {
    return lo.tick(n, g_out, LOOPER_MAX_EVENTS);
}

// ── state machine ────────────────────────────────────────────────────────────

void test_initial_state_is_idle() {
    Looper lo;
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_short_press_arms() {
    Looper lo;
    lo.on_button_short();
    TEST_ASSERT_EQUAL(LooperState::Arming, lo.state());
}

void test_second_short_press_cancels_arm() {
    Looper lo;
    lo.on_button_short();  // Arming
    lo.on_button_short();  // cancel
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_long_press_from_idle_stays_idle() {
    Looper lo;
    lo.on_button_long();
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_long_press_from_arming_returns_idle() {
    Looper lo;
    lo.on_button_short();  // Arming
    lo.on_button_long();
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_drum_hit_starts_recording() {
    Looper lo;
    lo.on_button_short();  // Arming
    lo.on_drum_hit(0);
    TEST_ASSERT_EQUAL(LooperState::Recording, lo.state());
}

void test_short_press_stops_recording_and_plays() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);
    do_tick(lo);  // advance record clock
    lo.on_button_short();
    TEST_ASSERT_EQUAL(LooperState::Playing, lo.state());
}

void test_short_press_during_playing_returns_idle() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);
    do_tick(lo);
    lo.on_button_short();  // Playing
    lo.on_button_short();  // → Idle
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_long_press_during_recording_clears() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);
    lo.on_button_long();
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_stop_clears_all() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);
    do_tick(lo);
    lo.on_button_short();  // Playing
    lo.stop();
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
    TEST_ASSERT_EQUAL(0, do_tick(lo));  // no events after clear
}

// ── idle / arming emit nothing ────────────────────────────────────────────────

void test_idle_tick_emits_nothing() {
    Looper lo;
    TEST_ASSERT_EQUAL(0, do_tick(lo));
}

void test_arming_tick_emits_nothing() {
    Looper lo;
    lo.on_button_short();
    TEST_ASSERT_EQUAL(0, do_tick(lo));
}

void test_recording_tick_emits_nothing() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);
    TEST_ASSERT_EQUAL(0, do_tick(lo));
}

// ── playback ─────────────────────────────────────────────────────────────────

void test_single_event_plays_back_at_start_of_loop() {
    // Record one hit at t=0; loop_length = 128 (2 chunks).
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(1);      // event at t=0
    do_tick(lo);            // record_pos_ = 64
    do_tick(lo);            // record_pos_ = 128
    lo.on_button_short();   // Playing, loop_length=128

    // Chunk 1 [0, 64): event at t=0 fires.
    size_t n = do_tick(lo);
    TEST_ASSERT_EQUAL_MESSAGE(1, n, "event should fire in first chunk");
    TEST_ASSERT_EQUAL_INT(1, g_out[0]);

    // Chunk 2 [64, 128): no event.
    n = do_tick(lo);
    TEST_ASSERT_EQUAL_MESSAGE(0, n, "no event in second chunk");

    // Chunk 3: loop wraps — event fires again.
    n = do_tick(lo);
    TEST_ASSERT_EQUAL_MESSAGE(1, n, "event should fire at loop restart");
    TEST_ASSERT_EQUAL_INT(1, g_out[0]);
}

void test_two_events_play_back_in_order() {
    // Hit 1 at t=0, hit 2 at t=64.  loop_length=128.
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(1);      // event {t=0, sid=1}
    do_tick(lo);            // record_pos_=64
    lo.on_drum_hit(2);      // event {t=64, sid=2}
    do_tick(lo);            // record_pos_=128
    lo.on_button_short();   // Playing

    // Chunk [0, 64): sid=1
    size_t n = do_tick(lo);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_INT(1, g_out[0]);

    // Chunk [64, 128) wraps to [0,0): sid=2 from tail, nothing from head
    n = do_tick(lo);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_INT(2, g_out[0]);
}

void test_loop_repeats_exactly() {
    // Run 3 full loop iterations and confirm event count matches.
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(5);
    do_tick(lo);  // 64 samples recorded
    do_tick(lo);  // 128 samples recorded
    lo.on_button_short();  // loop_length=128

    size_t total = 0;
    for (int tick = 0; tick < 6; ++tick) {  // 3 full loops = 6 chunks
        total += do_tick(lo);
    }
    // 3 loops × 1 event = 3
    TEST_ASSERT_EQUAL_MESSAGE(3, total, "event should fire once per loop");
}

// ── overflow ──────────────────────────────────────────────────────────────────

void test_overflow_does_not_crash_and_remains_recording() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);  // first hit → Recording, event at t=0

    // Fill the buffer (event_count_ is already 1 from the first hit)
    for (int i = 1; i < LOOPER_MAX_EVENTS; ++i) {
        do_tick(lo);
        lo.on_drum_hit(0);
    }
    // One more beyond capacity
    do_tick(lo);
    lo.on_drum_hit(0);

    // Still recording, not crashed
    TEST_ASSERT_EQUAL(LooperState::Recording, lo.state());
}

// ── auto-finalise ─────────────────────────────────────────────────────────────

void test_auto_finalise_at_max_duration() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(0);  // Recording

    const uint32_t max_s =
        static_cast<uint32_t>(LOOPER_MAX_DURATION_S) * SAMPLE_RATE;
    // Advance past max with a single large tick.
    lo.tick(max_s + 64, g_out, LOOPER_MAX_EVENTS);

    TEST_ASSERT_EQUAL_MESSAGE(LooperState::Playing, lo.state(),
        "should auto-finalise to Playing at max duration");
}

// ── drum hits in idle / playing state ─────────────────────────────────────────

void test_drum_hit_in_idle_is_noop() {
    Looper lo;
    lo.on_drum_hit(3);  // should not crash
    TEST_ASSERT_EQUAL(LooperState::Idle, lo.state());
}

void test_drum_hit_in_playing_is_not_captured() {
    Looper lo;
    lo.on_button_short();
    lo.on_drum_hit(1);  // Recording, 1 event
    do_tick(lo);
    lo.on_button_short();  // Playing, loop_length=64

    // Manual hit during playback — must not add a second event.
    lo.on_drum_hit(2);

    // Only the original event should play back.
    size_t n = do_tick(lo);
    TEST_ASSERT_EQUAL_MESSAGE(1, n, "only original event should play");
    TEST_ASSERT_EQUAL_INT(1, g_out[0]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_short_press_arms);
    RUN_TEST(test_second_short_press_cancels_arm);
    RUN_TEST(test_long_press_from_idle_stays_idle);
    RUN_TEST(test_long_press_from_arming_returns_idle);
    RUN_TEST(test_drum_hit_starts_recording);
    RUN_TEST(test_short_press_stops_recording_and_plays);
    RUN_TEST(test_short_press_during_playing_returns_idle);
    RUN_TEST(test_long_press_during_recording_clears);
    RUN_TEST(test_stop_clears_all);
    RUN_TEST(test_idle_tick_emits_nothing);
    RUN_TEST(test_arming_tick_emits_nothing);
    RUN_TEST(test_recording_tick_emits_nothing);
    RUN_TEST(test_single_event_plays_back_at_start_of_loop);
    RUN_TEST(test_two_events_play_back_in_order);
    RUN_TEST(test_loop_repeats_exactly);
    RUN_TEST(test_overflow_does_not_crash_and_remains_recording);
    RUN_TEST(test_auto_finalise_at_max_duration);
    RUN_TEST(test_drum_hit_in_idle_is_noop);
    RUN_TEST(test_drum_hit_in_playing_is_not_captured);
    return UNITY_END();
}

#else
int main() { return 0; }
#endif // BUILD_NATIVE_TEST
