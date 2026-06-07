// Unit tests for AutoDrummer — state machine, event firing, loop repeat.
#ifdef BUILD_NATIVE_TEST

#include <unity.h>

#include "defines.hpp"
#include "audio/auto_drummer.hpp"

using ::dummer::audio::AutoDrummer;
using ::dummer::audio::AutoStyle;

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static uint8_t g_trig[256];
static uint8_t g_vel[256];

static size_t do_tick(AutoDrummer& ad, uint32_t n = 64) {
    return ad.tick(n, g_trig, g_vel, 256);
}

// Cycle to Funk (4th active style: Off→Blues→Country→Jazz→Funk)
static void select_funk(AutoDrummer& ad) {
    ad.next_style();   // Blues
    ad.next_style();   // Country
    ad.next_style();   // Jazz
    ad.next_style();   // Funk
}

// ---------------------------------------------------------------------------
// state machine
// ---------------------------------------------------------------------------

void test_initial_style_is_off() {
    AutoDrummer ad;
    TEST_ASSERT_EQUAL(AutoStyle::Off, ad.style());
    TEST_ASSERT_FALSE(ad.active());
}

void test_next_style_activates_blues() {
    AutoDrummer ad;
    ad.next_style();
    TEST_ASSERT_EQUAL(AutoStyle::Blues, ad.style());
    TEST_ASSERT_TRUE(ad.active());
}

void test_style_cycles_through_all_and_wraps() {
    AutoDrummer ad;
    // 8 presses: Off→Blues→Country→Jazz→Funk→Reggae→Gospel→HardRock→Off
    for (int i = 0; i < 8; ++i) ad.next_style();
    TEST_ASSERT_EQUAL(AutoStyle::Off, ad.style());
}

void test_stop_returns_to_off() {
    AutoDrummer ad;
    ad.next_style();   // Blues
    ad.stop();
    TEST_ASSERT_EQUAL(AutoStyle::Off, ad.style());
    TEST_ASSERT_FALSE(ad.active());
}

void test_speed_cycles() {
    AutoDrummer ad;
    TEST_ASSERT_EQUAL(0, ad.speed());   // default: slow
    ad.next_speed();
    TEST_ASSERT_EQUAL(1, ad.speed());
    ad.next_speed();
    TEST_ASSERT_EQUAL(2, ad.speed());
    ad.next_speed();
    TEST_ASSERT_EQUAL(0, ad.speed());
}

// ---------------------------------------------------------------------------
// tick behaviour
// ---------------------------------------------------------------------------

void test_off_tick_emits_nothing() {
    AutoDrummer ad;
    TEST_ASSERT_EQUAL(0, do_tick(ad));
    TEST_ASSERT_EQUAL(0, do_tick(ad));
}

void test_first_chunk_fires_count_in_event() {
    // Every style begins with a stick click at tick 0 → sample 0.
    // That must fire in the very first 64-sample chunk.
    AutoDrummer ad;
    ad.next_style();   // Blues
    size_t n = do_tick(ad);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n, "first chunk should fire count-in hit");
}

void test_off_after_stop_emits_nothing() {
    AutoDrummer ad;
    ad.next_style();
    do_tick(ad);
    ad.stop();
    TEST_ASSERT_EQUAL(0, do_tick(ad));
    TEST_ASSERT_EQUAL(0, do_tick(ad));
}

void test_intro_fires_events_before_loop() {
    // Intro = bar 0 sticks only (blues, 90 BPM). Loop starts at bar 1.
    // 500 chunks × 64 = 32 000 samples — sticks fire within the first 1378 chunks.
    AutoDrummer ad;
    ad.next_style();   // Blues

    size_t total = 0;
    for (int i = 0; i < 500; ++i) total += do_tick(ad);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, total, "intro must fire some events");
}

// ---------------------------------------------------------------------------
// loop repeat
// ---------------------------------------------------------------------------

void test_funk_loop_repeats_exactly() {
    // Funk at 100 BPM (normal speed=1 — forced explicitly so the math stays exact):
    //   samples_per_tick = 44100*60/(480*100) = 55.125
    //   loop_start_sample = round(1920 * 55.125) = 105840
    //   loop_length_samples = round(7680 * 55.125) = 423360
    //   423360 / 64 = 6615 (exact) → 6615 chunks = one full loop iteration
    //
    // After skipping 1654 chunks (105856 samples), loop_pos = 16.
    // Exactly 6615 chunks later, loop_pos is back to 16 → streams match.
    AutoDrummer ad;
    select_funk(ad);   // Funk, speed=0 (slow, 80 BPM)
    ad.next_speed();   // advance to speed=1 (100 BPM) for exact chunk alignment
    ad.set_humanize(false);   // exact timing — no jitter for this alignment test

    // Skip sticks: 1654 chunks puts us 16 samples past loop_start
    for (int i = 0; i < 1654; ++i) do_tick(ad);

    static uint8_t trig[256], tvel[256];
    size_t count1 = 0, count2 = 0;
    for (int i = 0; i < 6615; ++i) count1 += ad.tick(64, trig, tvel, 256);
    for (int i = 0; i < 6615; ++i) count2 += ad.tick(64, trig, tvel, 256);

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, count1, "loop must fire events");
    TEST_ASSERT_EQUAL_MESSAGE(count1, count2, "loop must repeat exactly");
}

// ---------------------------------------------------------------------------
// humanization (M7)
// ---------------------------------------------------------------------------

void test_humanize_event_count_has_no_systematic_drift() {
    // Humanized timing only shifts WHEN each event fires, never how many: every
    // event fires exactly once per loop iteration (monotonic clamp keeps it in
    // [0, length), and the wrap flush fires all remaining). Counts may wobble by
    // a couple of hits at the counting-window boundary (an event near the edge
    // can land on either side), but that slop is bounded and must NOT grow with
    // the number of iterations — a drop/dup bug would scale with K and be caught.
    AutoDrummer off, on;
    select_funk(off); off.next_speed(); off.set_humanize(false);
    select_funk(on);  on.next_speed();  on.set_humanize(true);

    static uint8_t tr[256], tv[256];
    for (int i = 0; i < 1654; ++i) { off.tick(64, tr, tv, 256); on.tick(64, tr, tv, 256); }

    const int K = 16;
    size_t exact = 0, human = 0;
    for (int i = 0; i < K * 6615; ++i) {
        exact += off.tick(64, tr, tv, 256);
        human += on.tick(64, tr, tv, 256);
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, human, "humanized loop must fire events");
    const long diff = static_cast<long>(human) - static_cast<long>(exact);
    const long slop = (diff < 0) ? -diff : diff;
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(10, slop,
        "humanized event count must not drift from exact (no drop/dup)");
}

void test_humanize_velocity_in_range() {
    // Every emitted velocity must be a valid MIDI velocity (1-127).
    AutoDrummer ad;
    ad.next_style();          // Blues
    ad.set_humanize(true);

    static uint8_t tr[256], tv[256];
    for (int chunk = 0; chunk < 3000; ++chunk) {
        size_t n = ad.tick(64, tr, tv, 256);
        for (size_t i = 0; i < n; ++i) {
            TEST_ASSERT_GREATER_THAN_MESSAGE(0, tv[i], "velocity must be >= 1");
            TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(127, tv[i], "velocity must be <= 127");
        }
    }
}

void test_restart_on_next_style_replays_intro() {
    // Starting style twice should replay the count-in.
    // Humanization disabled so timing is deterministic across restarts.
    AutoDrummer ad;
    ad.set_humanize(false);
    ad.next_style();   // Blues

    // Collect first-chunk count
    size_t first_tick = do_tick(ad);
    TEST_ASSERT_GREATER_THAN(0, first_tick);

    // Advance well into the intro
    for (int i = 0; i < 100; ++i) do_tick(ad);

    // Wrap around: HardRock → Off → Blues restarts from count-in
    for (int i = 0; i < 7; ++i) ad.next_style();   // Blues→…→Off
    ad.next_style();                                  // Off→Blues (fresh start)

    size_t restart_tick = do_tick(ad);
    TEST_ASSERT_EQUAL_MESSAGE(first_tick, restart_tick,
        "restarted intro must fire same events in first chunk");
}

void test_all_styles_fire_events() {
    // Every active style must fire at least one event over its intro.
    AutoDrummer ad;
    for (int s = 1; s < static_cast<int>(AutoStyle::COUNT); ++s) {
        ad.next_style();
        size_t total = 0;
        for (int i = 0; i < 6000; ++i) total += do_tick(ad);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, total,
            "every style must fire events in the intro");
    }
}

// ---------------------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_style_is_off);
    RUN_TEST(test_next_style_activates_blues);
    RUN_TEST(test_style_cycles_through_all_and_wraps);
    RUN_TEST(test_stop_returns_to_off);
    RUN_TEST(test_speed_cycles);
    RUN_TEST(test_off_tick_emits_nothing);
    RUN_TEST(test_first_chunk_fires_count_in_event);
    RUN_TEST(test_off_after_stop_emits_nothing);
    RUN_TEST(test_intro_fires_events_before_loop);
    RUN_TEST(test_funk_loop_repeats_exactly);
    RUN_TEST(test_humanize_event_count_has_no_systematic_drift);
    RUN_TEST(test_humanize_velocity_in_range);
    RUN_TEST(test_restart_on_next_style_replays_intro);
    RUN_TEST(test_all_styles_fire_events);
    return UNITY_END();
}

#else
int main() { return 0; }
#endif // BUILD_NATIVE_TEST
