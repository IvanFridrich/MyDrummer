// Milestone 5: Looper FSM wired end-to-end.
// Drum buttons trigger samples + feed the looper recorder.
// Btn5 (Looper): short = arm/record/play/stop; long = clear.
// Recorded events play back on loop; each playback trigger flashes the
// matching drum LED and the blue LoopDrum LED.
//
// Super-loop (spec §4): poll buttons → tick LEDs → tick scheduler →
// drain staging to I2S (non-blocking) → if staging empty, pull a fresh
// chunk from the mixer.
#ifdef BUILD_ESP32

// Framework headers use old-style casts; silence the warning for the include
// block only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#pragma GCC diagnostic pop

#include "defines.hpp"
#include "hal/hal.hpp"
#include "hal/hal_factory.hpp"
#include "util/log.hpp"
#include "control/button_manager.hpp"
#include "control/led_manager.hpp"
#include "audio/voice_pool.hpp"
#include "audio/mixer.hpp"
#include "audio/reverb.hpp"
#include "audio/looper.hpp"
#include "samples.hpp"

using ::dummer::hal::HalFactory;
using ::dummer::hal::Hal;
using ::dummer::control::ButtonManager;
using ::dummer::control::ButtonEvent;
using ::dummer::control::ButtonEventType;
using ::dummer::control::LedManager;
using ::dummer::control::LedId;
using ::dummer::audio::VoicePool;
using ::dummer::audio::Mixer;
using ::dummer::audio::Reverb;
using ::dummer::audio::Looper;
using ::dummer::audio::SampleId;
using ::dummer::audio::kSampleTable;
using ::dummer::audio::SAMPLE_COUNT;

namespace {

Hal*           g_hal     = nullptr;
ButtonManager* g_buttons = nullptr;
LedManager*    g_leds    = nullptr;
VoicePool*     g_pool    = nullptr;
Mixer*         g_mixer   = nullptr;
Reverb*        g_reverb  = nullptr;
Looper*        g_looper  = nullptr;

constexpr uint8_t INVALID_SAMPLE = 0xFF;

[[maybe_unused]] const char* event_name(ButtonEventType t) {
    switch (t) {
        case ButtonEventType::Press:     return "PRESS";
        case ButtonEventType::Release:   return "RELEASE";
        case ButtonEventType::LongPress: return "LONG";
        default:                         return "?";
    }
}

uint8_t drum_sample_for(uint8_t idx) {
    switch (static_cast<ButtonRole>(idx)) {
        case ButtonRole::Kick:      return static_cast<uint8_t>(SampleId::SAMPLE_KICK);
        case ButtonRole::Snare:     return static_cast<uint8_t>(SampleId::SAMPLE_SNARE);
        case ButtonRole::HihatOpen: return static_cast<uint8_t>(SampleId::SAMPLE_HIHAT_OPEN);
        default:                    return INVALID_SAMPLE;
    }
}

LedId led_for_button(uint8_t idx) {
    switch (static_cast<ButtonRole>(idx)) {
        case ButtonRole::Kick:      return LedId::Kick;
        case ButtonRole::Snare:     return LedId::Snare;
        case ButtonRole::HihatOpen: return LedId::Hihat;
        default:                    return LedId::LoopDrum;
    }
}

LedId led_for_sample(uint8_t sid) {
    switch (static_cast<SampleId>(sid)) {
        case SampleId::SAMPLE_KICK:       return LedId::Kick;
        case SampleId::SAMPLE_SNARE:      return LedId::Snare;
        case SampleId::SAMPLE_HIHAT_OPEN: return LedId::Hihat;
        default:                          return LedId::LoopDrum;
    }
}

void app_task(void*) {
    static int16_t staging[AUDIO_CHUNK];
    size_t staging_head  = 0;
    size_t staging_count = 0;

    for (;;) {
        // 1. Drain all queued button events this iteration.
        for (;;) {
            ButtonEvent ev = g_buttons->poll();
            if (ev.type == ButtonEventType::None) break;
            LOG_I("BTN", "%s idx=%u pin=%d",
                  event_name(ev.type), static_cast<unsigned>(ev.index),
                  g_buttons->pin_of(ev.index));
            if (ev.type == ButtonEventType::Press) {
                if (static_cast<ButtonRole>(ev.index) == ButtonRole::ReverbToggle) {
                    const bool now_on = !g_reverb->enabled();
                    if (now_on) g_reverb->reset();
                    g_reverb->set_enabled(now_on);
                    g_leds->set_steady(LedId::Reverb, now_on);
                    LOG_I("REVERB", "%s", now_on ? "ON" : "OFF");
                } else if (static_cast<ButtonRole>(ev.index) == ButtonRole::Looper) {
                    g_looper->on_button_short();
                } else {
                    const uint8_t sid = drum_sample_for(ev.index);
                    if (sid != INVALID_SAMPLE) {
                        g_pool->trigger(sid);
                        g_leds->flash(led_for_button(ev.index));
                        g_looper->on_drum_hit(sid);
                    }
                }
            } else if (ev.type == ButtonEventType::LongPress) {
                if (static_cast<ButtonRole>(ev.index) == ButtonRole::Looper) {
                    g_looper->on_button_long();
                }
            }
        }

        // 2. LED timers / heartbeat.
        g_leds->tick();

        // 3. Tick scheduler — emit looper playback triggers.
        {
            constexpr size_t kMaxTrig = 8;
            uint8_t trig[kMaxTrig];
            const size_t fired = g_looper->tick(AUDIO_CHUNK, trig, kMaxTrig);
            for (size_t i = 0; i < fired; ++i) {
                g_pool->trigger(trig[i]);
                g_leds->flash(led_for_sample(trig[i]));
                g_leds->flash(LedId::LoopDrum);
            }
        }

        // 4. Non-blocking drain of staging buffer into I2S DMA.
        if (staging_count > 0) {
            size_t written = g_hal->i2s->write_nonblocking(
                staging + staging_head, staging_count);
            staging_head  += written;
            staging_count -= written;
        }

        // 5. Refill staging if empty.
        if (staging_count == 0) {
            g_mixer->get_samples(staging, AUDIO_CHUNK);
            staging_head  = 0;
            staging_count = AUDIO_CHUNK;
        }

        // 6. Yield 1 ms — IDLE / WDT keep healthy. Buttons still polled
        //    every iteration. DMA holds ~11 ms of audio so this is safe.
        vTaskDelay(1);
    }
}

} // namespace

void setup() {
    g_hal = HalFactory::create();
    g_hal->serial->init(115200);
    ::dummer::log::set_sink(g_hal->serial);
    LOG_I("BOOT", "Dummer milestone 5 — looper active");

    if (!g_hal->i2s->init(SAMPLE_RATE, I2S_BCLK, I2S_LRCK, I2S_DOUT)) {
        LOG_E("BOOT", "I2S init failed");
    }

    static ButtonManager bm(g_hal->gpio, g_hal->clock);
    static LedManager    lm(g_hal->gpio, g_hal->clock);
    static VoicePool     vp;
    static Reverb        rv;
    static Mixer         mx(vp, kSampleTable, static_cast<uint8_t>(SAMPLE_COUNT), &rv);
    static Looper        lo;

    g_buttons = &bm;
    g_leds    = &lm;
    g_pool    = &vp;
    g_mixer   = &mx;
    g_reverb  = &rv;
    g_looper  = &lo;

    g_buttons->begin();
    g_leds->begin();

    BaseType_t ok = xTaskCreatePinnedToCore(
        app_task, "dummer", APP_TASK_STACK, nullptr,
        APP_TASK_PRIORITY, nullptr, APP_TASK_CORE);
    // pdPASS is `((BaseType_t)1)` — wrap the comparison to keep -Wold-style-cast quiet.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
    if (ok != pdPASS) {
        LOG_E("BOOT", "xTaskCreatePinnedToCore failed");
    }
#pragma GCC diagnostic pop
}

void loop() {
    // Intentionally empty — all real work runs on the pinned task.
}

#endif // BUILD_ESP32
