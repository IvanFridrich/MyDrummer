// Unit tests for ButtonManager — runs on host via PlatformIO native_test env.
//
// Uses the native HAL stubs (NativeGpio, NativeClock) to script raw pin
// transitions and time, then asserts the debounced events emitted.
//
// Polling model: the manager observes a level change only when poll() is
// called. So the standard test pattern is:
//   1. Change the simulated pin.
//   2. poll() once to let the manager record the edge.
//   3. Advance time past BUTTON_DEBOUNCE_MS.
//   4. poll() to assert the debounced event.
#ifdef BUILD_NATIVE_TEST

#include <unity.h>

#include "defines.hpp"
#include "hal/hal.hpp"
#include "hal/hal_native.hpp"
#include "control/button_manager.hpp"

using namespace ::dummer::hal;
using namespace ::dummer::hal::native;
using ::dummer::control::ButtonEvent;
using ::dummer::control::ButtonEventType;
using ::dummer::control::ButtonManager;

namespace
{

constexpr int kKickPin               = 4; // index 0 in BUTTON_PINS
constexpr int kAllPins[BUTTON_COUNT] = BUTTON_PINS;

ButtonManager make_bm()
{
    return ButtonManager(&gpio(), &clock());
}

void drain(ButtonManager& bm)
{
    while (bm.poll().type != ButtonEventType::None)
    {
    }
}

} // namespace

void setUp()
{
    reset_all();
    // Buttons are active-low with internal pull-up — idle = HIGH on every pin.
    for (int p : kAllPins)
        gpio().set_pin(p, true);
}

void tearDown()
{
}

void test_pin_modes_are_input_pullup()
{
    ButtonManager bm = make_bm();
    bm.begin();
    for (int p : kAllPins)
    {
        TEST_ASSERT_EQUAL(PIN_INPUT_PULLUP, gpio().get_mode(p));
    }
}

void test_no_event_when_idle()
{
    ButtonManager bm = make_bm();
    bm.begin();
    clock().advance_ms(100);
    TEST_ASSERT_EQUAL((int)ButtonEventType::None, (int)bm.poll().type);
    TEST_ASSERT_FALSE(bm.is_pressed(0));
}

void test_press_fires_only_after_debounce()
{
    ButtonManager bm = make_bm();
    bm.begin();

    // Edge: pin drops LOW. poll() records the edge timestamp.
    gpio().set_pin(kKickPin, false);
    bm.poll();
    TEST_ASSERT_FALSE(bm.is_pressed(0));

    // Still inside the debounce window — no event yet.
    clock().advance_ms(BUTTON_DEBOUNCE_MS - 10);
    TEST_ASSERT_EQUAL((int)ButtonEventType::None, (int)bm.poll().type);
    TEST_ASSERT_FALSE(bm.is_pressed(0));

    // Crossed debounce — PRESS fires.
    clock().advance_ms(20);
    ButtonEvent ev = bm.poll();
    TEST_ASSERT_EQUAL((int)ButtonEventType::Press, (int)ev.type);
    TEST_ASSERT_EQUAL_UINT8(0, ev.index);
    TEST_ASSERT_TRUE(bm.is_pressed(0));

    // No second press event for the same edge.
    TEST_ASSERT_EQUAL((int)ButtonEventType::None, (int)bm.poll().type);
}

void test_release_event()
{
    ButtonManager bm = make_bm();
    bm.begin();

    // Press.
    gpio().set_pin(kKickPin, false);
    bm.poll();
    clock().advance_ms(BUTTON_DEBOUNCE_MS + 5);
    drain(bm);
    TEST_ASSERT_TRUE(bm.is_pressed(0));

    // Release.
    gpio().set_pin(kKickPin, true);
    bm.poll();
    clock().advance_ms(BUTTON_DEBOUNCE_MS + 5);
    ButtonEvent ev = bm.poll();
    TEST_ASSERT_EQUAL((int)ButtonEventType::Release, (int)ev.type);
    TEST_ASSERT_FALSE(bm.is_pressed(0));
}

void test_long_press_fires_once()
{
    ButtonManager bm = make_bm();
    bm.begin();

    // Press.
    gpio().set_pin(kKickPin, false);
    bm.poll();
    clock().advance_ms(BUTTON_DEBOUNCE_MS + 5);
    ButtonEvent ev = bm.poll();
    TEST_ASSERT_EQUAL((int)ButtonEventType::Press, (int)ev.type);

    // Held but below long-press threshold.
    clock().advance_ms(BUTTON_LONGPRESS_MS - BUTTON_DEBOUNCE_MS - 100);
    TEST_ASSERT_EQUAL((int)ButtonEventType::None, (int)bm.poll().type);

    // Cross the threshold — LongPress fires once.
    clock().advance_ms(200);
    ev = bm.poll();
    TEST_ASSERT_EQUAL((int)ButtonEventType::LongPress, (int)ev.type);

    // Still held — no further LongPress.
    clock().advance_ms(500);
    TEST_ASSERT_EQUAL((int)ButtonEventType::None, (int)bm.poll().type);
}

void test_bounce_is_ignored()
{
    ButtonManager bm = make_bm();
    bm.begin();

    // Rapid toggling inside the debounce window — never stable long enough
    // to register as a press. Ends back at HIGH (idle).
    for (int i = 0; i < 8; ++i)
    {
        gpio().set_pin(kKickPin, false);
        bm.poll();
        clock().advance_ms(2);
        gpio().set_pin(kKickPin, true);
        bm.poll();
        clock().advance_ms(2);
    }
    clock().advance_ms(BUTTON_DEBOUNCE_MS + 5);
    TEST_ASSERT_EQUAL((int)ButtonEventType::None, (int)bm.poll().type);
    TEST_ASSERT_FALSE(bm.is_pressed(0));
}

void test_independent_buttons()
{
    ButtonManager bm = make_bm();
    bm.begin();

    // Press button index 2 (hihat).
    gpio().set_pin(kAllPins[2], false);
    bm.poll();
    clock().advance_ms(BUTTON_DEBOUNCE_MS + 5);
    ButtonEvent ev = bm.poll();
    TEST_ASSERT_EQUAL((int)ButtonEventType::Press, (int)ev.type);
    TEST_ASSERT_EQUAL_UINT8(2, ev.index);

    TEST_ASSERT_FALSE(bm.is_pressed(0));
    TEST_ASSERT_TRUE(bm.is_pressed(2));
    TEST_ASSERT_FALSE(bm.is_pressed(6));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_pin_modes_are_input_pullup);
    RUN_TEST(test_no_event_when_idle);
    RUN_TEST(test_press_fires_only_after_debounce);
    RUN_TEST(test_release_event);
    RUN_TEST(test_long_press_fires_once);
    RUN_TEST(test_bounce_is_ignored);
    RUN_TEST(test_independent_buttons);
    return UNITY_END();
}

#else
int main()
{
    return 0;
}
#endif // BUILD_NATIVE_TEST
