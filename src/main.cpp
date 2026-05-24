// Milestone 1: project skeleton. Boots, blinks the heartbeat LED, echoes
// button events to serial. No audio yet — that arrives in milestone 2.
//
// Architecture (spec §4): setup() configures HAL + modules, then spawns one
// FreeRTOS task pinned to core 1. That task runs the unbounded super-loop.
// Arduino's loop() stays empty.
#ifdef BUILD_ESP32

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "defines.hpp"
#include "hal/hal.hpp"
#include "hal/hal_factory.hpp"
#include "util/log.hpp"
#include "control/button_manager.hpp"
#include "control/led_manager.hpp"

using ::dummer::hal::HalFactory;
using ::dummer::hal::Hal;
using ::dummer::control::ButtonManager;
using ::dummer::control::ButtonEvent;
using ::dummer::control::ButtonEventType;
using ::dummer::control::LedManager;
using ::dummer::control::LedId;

namespace {

Hal*          g_hal = nullptr;
ButtonManager* g_buttons = nullptr;
LedManager*    g_leds = nullptr;

const char* event_name(ButtonEventType t) {
    switch (t) {
        case ButtonEventType::Press:     return "PRESS";
        case ButtonEventType::Release:   return "RELEASE";
        case ButtonEventType::LongPress: return "LONG";
        default:                         return "?";
    }
}

LedId led_for_button(uint8_t idx) {
    switch ((ButtonRole)idx) {
        case ButtonRole::Kick:      return LedId::Kick;
        case ButtonRole::Snare:     return LedId::Snare;
        case ButtonRole::HihatOpen: return LedId::Hihat;
        default:                    return LedId::LoopDrum;
    }
}

void app_task(void*) {
    // Super-loop. Must never block for more than ~1 ms anywhere.
    for (;;) {
        // 1. Drain all button events this iteration.
        for (;;) {
            ButtonEvent ev = g_buttons->poll();
            if (ev.type == ButtonEventType::None) break;
            LOG_I("BTN", "%s idx=%u pin=%d",
                  event_name(ev.type), (unsigned)ev.index,
                  g_buttons->pin_of(ev.index));
            if (ev.type == ButtonEventType::Press) {
                g_leds->flash(led_for_button(ev.index));
            }
        }

        // 2. Advance LED timers (flash expiry, heartbeat).
        g_leds->tick();

        // 3. (milestone 2+: scheduler, audio pull, I2S drain, profiler)

        // Yield to the IDLE/WDT every iteration; 1 tick = 1 ms by default.
        vTaskDelay(1);
    }
}

} // namespace

void setup() {
    g_hal = HalFactory::create();
    g_hal->serial->init(115200);
    ::dummer::log::set_sink(g_hal->serial);
    LOG_I("BOOT", "Dummer milestone 1 — skeleton up");

    static ButtonManager bm(g_hal->gpio, g_hal->clock);
    static LedManager    lm(g_hal->gpio, g_hal->clock);
    g_buttons = &bm;
    g_leds    = &lm;
    g_buttons->begin();
    g_leds->begin();

    BaseType_t ok = xTaskCreatePinnedToCore(
        app_task, "dummer", APP_TASK_STACK, nullptr,
        APP_TASK_PRIORITY, nullptr, APP_TASK_CORE);
    if (ok != pdPASS) {
        LOG_E("BOOT", "xTaskCreatePinnedToCore failed");
    }
}

void loop() {
    // Intentionally empty — all real work runs on the pinned task.
}

#endif // BUILD_ESP32
