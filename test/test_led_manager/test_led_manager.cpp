// Unit tests for LedManager — runs on host via PlatformIO native_test env.
//
// Defines.hpp leaves most LED pins at -1 (no-op) until hardware is wired,
// so each test uses set_pin_for_test() to assign synthetic pins (100, 101,
// ...) it can then observe via NativeGpio::get_written().
#ifdef BUILD_NATIVE_TEST

#include <unity.h>

#include "defines.hpp"
#include "hal/hal.hpp"
#include "hal/hal_native.hpp"
#include "control/led_manager.hpp"

using ::dummer::control::LedId;
using ::dummer::control::LedManager;
using ::dummer::hal::native::clock;
using ::dummer::hal::native::gpio;
using ::dummer::hal::native::reset_all;

namespace
{

constexpr int kKickTestPin      = 100;
constexpr int kSnareTestPin     = 101;
constexpr int kHeartbeatTestPin = 102;

LedManager make_lm()
{
    return LedManager(&gpio(), &clock());
}

void wire_test_pins(LedManager& lm)
{
    lm.set_pin_for_test(LedId::Kick, kKickTestPin);
    lm.set_pin_for_test(LedId::Snare, kSnareTestPin);
    lm.set_pin_for_test(LedId::Heartbeat, kHeartbeatTestPin);
    lm.begin();
}

} // namespace

void setUp()
{
    reset_all();
}
void tearDown()
{
}

void test_begin_drives_pins_low_and_sets_output_mode()
{
    LedManager lm = make_lm();
    wire_test_pins(lm);
    TEST_ASSERT_EQUAL(::dummer::hal::PIN_OUTPUT, gpio().get_mode(kKickTestPin));
    TEST_ASSERT_EQUAL(::dummer::hal::PIN_OUTPUT, gpio().get_mode(kSnareTestPin));
    TEST_ASSERT_EQUAL(::dummer::hal::PIN_OUTPUT, gpio().get_mode(kHeartbeatTestPin));
    TEST_ASSERT_FALSE(gpio().get_written(kKickTestPin));
    TEST_ASSERT_FALSE(gpio().get_written(kSnareTestPin));
}

void test_flash_raises_pin_then_clears_after_window()
{
    LedManager lm = make_lm();
    wire_test_pins(lm);

    lm.flash(LedId::Kick);
    TEST_ASSERT_TRUE(gpio().get_written(kKickTestPin));

    // Inside the flash window — still on.
    clock().advance_ms(LED_FLASH_MS - 1);
    lm.tick();
    TEST_ASSERT_TRUE(gpio().get_written(kKickTestPin));

    // Past the window — tick must clear the pin.
    clock().advance_ms(2);
    lm.tick();
    TEST_ASSERT_FALSE(gpio().get_written(kKickTestPin));
}

void test_steady_holds_level_until_cleared()
{
    LedManager lm = make_lm();
    wire_test_pins(lm);

    lm.set_steady(LedId::Snare, true);
    TEST_ASSERT_TRUE(gpio().get_written(kSnareTestPin));

    // Many ticks later — still on (no expiry).
    clock().advance_ms(500);
    lm.tick();
    TEST_ASSERT_TRUE(gpio().get_written(kSnareTestPin));

    lm.set_steady(LedId::Snare, false);
    TEST_ASSERT_FALSE(gpio().get_written(kSnareTestPin));
}

void test_flash_returns_to_steady_state_after_window()
{
    LedManager lm = make_lm();
    wire_test_pins(lm);

    // Snare is steady-on; flashing it should still leave it ON after the
    // flash window expires.
    lm.set_steady(LedId::Snare, true);
    lm.flash(LedId::Snare);
    TEST_ASSERT_TRUE(gpio().get_written(kSnareTestPin));

    clock().advance_ms(LED_FLASH_MS + 5);
    lm.tick();
    TEST_ASSERT_TRUE(gpio().get_written(kSnareTestPin)); // steady remembered

    // Flip steady off without re-flashing — pin should drop on the next call.
    lm.set_steady(LedId::Snare, false);
    TEST_ASSERT_FALSE(gpio().get_written(kSnareTestPin));
}

void test_heartbeat_toggles_at_half_period()
{
    LedManager lm = make_lm();
    wire_test_pins(lm);

    constexpr uint32_t half = LED_HEARTBEAT_PERIOD_MS / 2;

    // First tick at t=0 arms the next-toggle timestamp but does not toggle.
    lm.tick();
    TEST_ASSERT_FALSE(gpio().get_written(kHeartbeatTestPin));

    // Just before the half-period — still off.
    clock().advance_ms(half - 1);
    lm.tick();
    TEST_ASSERT_FALSE(gpio().get_written(kHeartbeatTestPin));

    // Cross the boundary — heartbeat toggles ON.
    clock().advance_ms(2);
    lm.tick();
    TEST_ASSERT_TRUE(gpio().get_written(kHeartbeatTestPin));

    // Another half-period — toggles OFF.
    clock().advance_ms(half);
    lm.tick();
    TEST_ASSERT_FALSE(gpio().get_written(kHeartbeatTestPin));
}

void test_no_op_on_unconfigured_pin()
{
    // All LED pins from defines.hpp default to -1 except heartbeat; with no
    // set_pin_for_test() calls, flash and set_steady must silently no-op
    // rather than writing to pin -1 (which would corrupt the NativeGpio map).
    LedManager lm = make_lm();
    lm.begin();

    lm.flash(LedId::Kick);
    lm.set_steady(LedId::Reverb, true);
    clock().advance_ms(LED_FLASH_MS + 10);
    lm.tick();

    // NativeGpio::get_written returns false for unset pins. No -1 entry.
    TEST_ASSERT_EQUAL(-1, gpio().get_mode(-1));
}

void test_out_of_range_led_id_is_safe()
{
    LedManager lm = make_lm();
    lm.begin();
    // Cast a deliberately-invalid value through LedId; must not crash or
    // write past the slots array.
    lm.flash(static_cast<LedId>(99));
    lm.set_steady(static_cast<LedId>(99), true);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_drives_pins_low_and_sets_output_mode);
    RUN_TEST(test_flash_raises_pin_then_clears_after_window);
    RUN_TEST(test_steady_holds_level_until_cleared);
    RUN_TEST(test_flash_returns_to_steady_state_after_window);
    RUN_TEST(test_heartbeat_toggles_at_half_period);
    RUN_TEST(test_no_op_on_unconfigured_pin);
    RUN_TEST(test_out_of_range_led_id_is_safe);
    return UNITY_END();
}

#else
int main()
{
    return 0;
}
#endif // BUILD_NATIVE_TEST
