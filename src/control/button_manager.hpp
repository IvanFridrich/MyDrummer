// Button manager: 7 buttons, debounce, edge detection, long-press timing.
//
// All hardware access flows through hal::IGpio + hal::IClock, so this module
// is fully testable on native via the stubbed HAL.
//
// Configuration is taken from defines.hpp (BUTTON_COUNT, BUTTON_PINS,
// BUTTON_DEBOUNCE_MS, BUTTON_LONGPRESS_MS, BUTTON_ACTIVE_LOW).
#pragma once

#include "defines.hpp"
#include "hal/hal.hpp"

#include <stdint.h>

namespace dummer
{
namespace control
{

enum class ButtonEventType : uint8_t
{
    None = 0,
    Press,     // debounced falling edge (button pressed)
    Release,   // debounced rising edge (button released)
    LongPress, // held > BUTTON_LONGPRESS_MS, fires once
};

struct ButtonEvent
{
    ButtonEventType type;
    uint8_t         index; // 0..BUTTON_COUNT-1
};

class ButtonManager
{
  public:
    ButtonManager(::dummer::hal::IGpio* gpio, ::dummer::hal::IClock* clock);

    // Configure pin modes. Call once in setup().
    void begin();

    // Poll all buttons. Returns the next pending event, or {None,0} if none.
    // Call repeatedly until None is returned to drain the queue.
    ButtonEvent poll();

    // Debounced state — true while the button is held down (after debounce).
    bool is_pressed(uint8_t index) const;

    // Direct access to pin for tests / diagnostics.
    int pin_of(uint8_t index) const;

    static constexpr uint8_t COUNT = BUTTON_COUNT;

  private:
    struct State
    {
        bool     raw_level;         // last sampled raw value
        bool     debounced_pressed; // true = currently pressed (post-debounce)
        uint32_t last_change_ms;    // when raw_level last toggled
        uint32_t press_started_ms;  // when debounced press began
        bool     long_press_fired;  // already emitted LongPress for this press
        // Pending event queue per button — at most one of each type can be
        // queued, in priority order Press > LongPress > Release.
        bool pend_press;
        bool pend_release;
        bool pend_long;
    };

    ::dummer::hal::IGpio*  gpio_;
    ::dummer::hal::IClock* clock_;
    int                    pins_[COUNT];
    State                  state_[COUNT];

    // Drain helper: scan all buttons for any queued event and return it.
    bool pop_pending(ButtonEvent& out);

    static bool pin_to_pressed(bool raw_level);
};

} // namespace control
} // namespace dummer
