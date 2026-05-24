#include "button_manager.hpp"

namespace dummer { namespace control {

namespace {
constexpr int kPins[] = BUTTON_PINS;
static_assert(sizeof(kPins) / sizeof(kPins[0]) == BUTTON_COUNT,
              "BUTTON_PINS size must match BUTTON_COUNT");
} // namespace

bool ButtonManager::pin_to_pressed(bool raw_level) {
#if BUTTON_ACTIVE_LOW
    return !raw_level;
#else
    return raw_level;
#endif
}

ButtonManager::ButtonManager(::dummer::hal::IGpio* gpio, ::dummer::hal::IClock* clock)
    : gpio_(gpio), clock_(clock) {
    for (uint8_t i = 0; i < COUNT; ++i) {
        pins_[i] = kPins[i];
        State& s = state_[i];
        s.raw_level         = true;   // idle high (pull-up)
        s.debounced_pressed = false;
        s.last_change_ms    = 0;
        s.press_started_ms  = 0;
        s.long_press_fired  = false;
        s.pend_press        = false;
        s.pend_release      = false;
        s.pend_long         = false;
    }
}

void ButtonManager::begin() {
    using namespace ::dummer::hal;
    const int mode = BUTTON_ACTIVE_LOW ? PIN_INPUT_PULLUP : PIN_INPUT;
    for (uint8_t i = 0; i < COUNT; ++i) {
        gpio_->pin_mode(pins_[i], mode);
        // Seed raw_level so first poll doesn't fabricate an edge.
        state_[i].raw_level = gpio_->read(pins_[i]);
    }
}

bool ButtonManager::is_pressed(uint8_t index) const {
    if (index >= COUNT) return false;
    return state_[index].debounced_pressed;
}

int ButtonManager::pin_of(uint8_t index) const {
    if (index >= COUNT) return -1;
    return pins_[index];
}

bool ButtonManager::pop_pending(ButtonEvent& out) {
    for (uint8_t i = 0; i < COUNT; ++i) {
        State& s = state_[i];
        if (s.pend_press)   { s.pend_press   = false; out = { ButtonEventType::Press,     i }; return true; }
        if (s.pend_long)    { s.pend_long    = false; out = { ButtonEventType::LongPress, i }; return true; }
        if (s.pend_release) { s.pend_release = false; out = { ButtonEventType::Release,   i }; return true; }
    }
    return false;
}

ButtonEvent ButtonManager::poll() {
    const uint32_t now = clock_->millis();

    for (uint8_t i = 0; i < COUNT; ++i) {
        State& s = state_[i];

        const bool raw = gpio_->read(pins_[i]);
        if (raw != s.raw_level) {
            s.raw_level      = raw;
            s.last_change_ms = now;
        }

        const bool stable = (uint32_t)(now - s.last_change_ms) >= (uint32_t)BUTTON_DEBOUNCE_MS;
        const bool target_pressed = pin_to_pressed(s.raw_level);

        if (stable && target_pressed != s.debounced_pressed) {
            s.debounced_pressed = target_pressed;
            if (target_pressed) {
                s.press_started_ms = now;
                s.long_press_fired = false;
                s.pend_press       = true;
            } else {
                s.pend_release     = true;
            }
        }

        // Long-press detection: while held, after BUTTON_LONGPRESS_MS since
        // the debounced press began, fire LongPress exactly once.
        if (s.debounced_pressed && !s.long_press_fired) {
            const uint32_t held = (uint32_t)(now - s.press_started_ms);
            if (held >= (uint32_t)BUTTON_LONGPRESS_MS) {
                s.long_press_fired = true;
                s.pend_long        = true;
            }
        }
    }

    ButtonEvent ev{ ButtonEventType::None, 0 };
    pop_pending(ev);
    return ev;
}

}} // namespace dummer::control
