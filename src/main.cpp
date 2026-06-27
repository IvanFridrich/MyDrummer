// Milestone 6: AutoDrummer wired end-to-end.
// Btn4 (Looper): short = arm/record/play/stop; long = clear.
// Btn5 (AutoStyle): short = cycle style; long = stop auto-drummer.
// Btn6 (AutoSpeed): short = cycle BPM; long = reset to normal speed.
// Looper and AutoDrummer are mutually exclusive: starting one stops the other.
//
// Audio loop: poll buttons → tick LEDs → 2×(tick scheduler + mix AUDIO_CHUNK)
// → blocking I2S write of full I2S_DMA_BUF_LEN buffer (portMAX_DELAY).
// Loop profiler logs max/avg CPU time per second to serial.
#ifdef BUILD_ESP32

// Framework headers use old-style casts; silence the warning for the include
// block only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#pragma GCC diagnostic pop

#include <esp_task_wdt.h>

#include "defines.hpp"
#include "hal/hal.hpp"
#include "hal/hal_factory.hpp"
#include "util/log.hpp"
#include "control/button_manager.hpp"
#include "control/led_manager.hpp"
#include "control/display_state.hpp"
#include "control/display_ui.hpp"
#include "audio/voice_pool.hpp"
#include "audio/mixer.hpp"
#include "audio/reverb.hpp"
#include "audio/looper.hpp"
#include "audio/auto_drummer.hpp"
#include "samples.hpp"

using ::dummer::audio::AutoDrummer;
using ::dummer::audio::AutoStyle;
using ::dummer::audio::kSampleTable;
using ::dummer::audio::Looper;
using ::dummer::audio::Mixer;
using ::dummer::audio::Reverb;
using ::dummer::audio::SAMPLE_COUNT;
using ::dummer::audio::SampleId;
using ::dummer::audio::VoicePool;
using ::dummer::audio::LooperState;
using ::dummer::control::ButtonEvent;
using ::dummer::control::ButtonEventType;
using ::dummer::control::ButtonManager;
using ::dummer::control::DisplayState;
using ::dummer::control::DisplayUi;
using ::dummer::control::LedId;
using ::dummer::control::LedManager;
using ::dummer::hal::Hal;
using ::dummer::hal::HalFactory;

namespace
{

Hal*           g_hal        = nullptr;
ButtonManager* g_buttons    = nullptr;
LedManager*    g_leds       = nullptr;
VoicePool*     g_pool       = nullptr;
Mixer*         g_mixer      = nullptr;
Reverb*        g_reverb     = nullptr;
Looper*        g_looper     = nullptr;
AutoDrummer*   g_auto       = nullptr;
DisplayUi*     g_display_ui = nullptr;

volatile DisplayState g_display_state = {};

static uint8_t bpm_for(uint8_t style_val, uint8_t speed);

constexpr uint8_t INVALID_SAMPLE = 0xFF;

#if LOG_LEVEL >= 3
static const char* style_name(AutoStyle s)
{
    switch (s)
    {
    case AutoStyle::Blues:
        return "Blues";
    case AutoStyle::Country:
        return "Country";
    case AutoStyle::Jazz:
        return "Jazz";
    case AutoStyle::Funk:
        return "Funk";
    case AutoStyle::Reggae:
        return "Reggae";
    case AutoStyle::Gospel:
        return "Gospel";
    case AutoStyle::HardRock:
        return "HardRock";
    default:
        return "Off";
    }
}

static const char* speed_name(uint8_t idx)
{
    switch (idx)
    {
    case 0:
        return "slow";
    case 2:
        return "fast";
    default:
        return "normal";
    }
}

static const char* event_name(ButtonEventType t)
{
    switch (t)
    {
    case ButtonEventType::Press:
        return "PRESS";
    case ButtonEventType::Release:
        return "RELEASE";
    case ButtonEventType::LongPress:
        return "LONG";
    default:
        return "?";
    }
}
#endif

// Button index → drum sample (INVALID_SAMPLE for non-drum buttons)
static constexpr uint8_t kButtonSample[BUTTON_COUNT] = {
    static_cast<uint8_t>(SampleId::SAMPLE_KICK),       // Kick
    static_cast<uint8_t>(SampleId::SAMPLE_SNARE),      // Snare
    static_cast<uint8_t>(SampleId::SAMPLE_HIHAT_OPEN), // HihatOpen
    INVALID_SAMPLE,                                    // ReverbToggle
    INVALID_SAMPLE,                                    // Looper
    INVALID_SAMPLE,                                    // AutoDrumStyle
    INVALID_SAMPLE,                                    // AutoDrumSpeed
};

// Button index → LED
static constexpr LedId kButtonLed[BUTTON_COUNT] = {
    LedId::Kick,     // Kick
    LedId::Snare,    // Snare
    LedId::Hihat,    // HihatOpen
    LedId::LoopDrum, // ReverbToggle
    LedId::LoopDrum, // Looper
    LedId::LoopDrum, // AutoDrumStyle
    LedId::LoopDrum, // AutoDrumSpeed
};

// SampleId → LED (indexed by SampleId enum value)
static constexpr LedId kSampleLed[static_cast<uint8_t>(SAMPLE_COUNT)] = {
    LedId::Kick,     // SAMPLE_KICK
    LedId::Snare,    // SAMPLE_SNARE
    LedId::Snare,    // SAMPLE_TOM_HI
    LedId::Snare,    // SAMPLE_TOM_MID
    LedId::Snare,    // SAMPLE_TOM_LO
    LedId::Hihat,    // SAMPLE_HIHAT_CLOSED
    LedId::Hihat,    // SAMPLE_HIHAT_OPEN
    LedId::Hihat,    // SAMPLE_HIHAT_PEDAL
    LedId::Hihat,    // SAMPLE_CRASH
    LedId::Hihat,    // SAMPLE_RIDE
    LedId::Kick,     // SAMPLE_STICKS
    LedId::LoopDrum, // SAMPLE_COWBELL
    LedId::LoopDrum, // SAMPLE_TAMBOURINE
};

uint8_t drum_sample_for(uint8_t idx)
{
    return (idx < BUTTON_COUNT) ? kButtonSample[idx] : INVALID_SAMPLE;
}

LedId led_for_button(uint8_t idx)
{
    return (idx < BUTTON_COUNT) ? kButtonLed[idx] : LedId::LoopDrum;
}

LedId led_for_sample(uint8_t sid)
{
    return (sid < static_cast<uint8_t>(SAMPLE_COUNT)) ? kSampleLed[sid] : LedId::LoopDrum;
}

static void handle_press(ButtonRole role, const ButtonEvent& ev)
{
    if (role == ButtonRole::ReverbToggle)
    {
        const bool now_on = !g_reverb->enabled();
        if (now_on)
            g_reverb->reset();
        g_reverb->set_enabled(now_on);
        g_leds->set_steady(LedId::Reverb, now_on);
        g_display_state.reverb_on = now_on;
        LOG_I("REVERB", "%s", now_on ? "ON" : "OFF");
    }
    else if (role == ButtonRole::Looper)
    {
        if (g_auto->active())
        {
            g_auto->stop();
            g_leds->set_steady(LedId::LoopDrum, false);
            g_display_state.auto_active = false;
        }
        g_looper->on_button_short();
        g_display_state.looper_state        = static_cast<uint8_t>(g_looper->state());
        g_display_state.loop_length_samples = g_looper->loop_length();
    }
    else if (role == ButtonRole::AutoDrumStyle)
    {
        g_looper->stop();
        g_display_state.looper_state        = static_cast<uint8_t>(LooperState::Idle);
        g_display_state.loop_length_samples = 0;
        g_auto->next_style();
        g_leds->set_steady(LedId::LoopDrum, g_auto->active());
        g_display_state.style       = static_cast<uint8_t>(g_auto->style());
        g_display_state.auto_active = g_auto->active();
        g_display_state.speed       = g_auto->speed();
        g_display_state.bpm         = bpm_for(static_cast<uint8_t>(g_auto->style()), g_auto->speed());
        LOG_I("AUTO", "style=%s speed=%s", style_name(g_auto->style()),
              speed_name(g_auto->speed()));
    }
    else if (role == ButtonRole::AutoDrumSpeed)
    {
        g_auto->next_speed();
        g_display_state.speed = g_auto->speed();
        g_display_state.bpm   = bpm_for(static_cast<uint8_t>(g_auto->style()), g_auto->speed());
        LOG_I("AUTO", "speed=%s", speed_name(g_auto->speed()));
    }
    else
    {
        const uint8_t sid = drum_sample_for(ev.index);
        if (sid != INVALID_SAMPLE)
        {
            g_pool->trigger(sid);
            g_leds->flash(led_for_button(ev.index));
            g_looper->on_drum_hit(sid);
        }
    }
}

static void handle_long_press(ButtonRole role)
{
    if (role == ButtonRole::Looper)
    {
        g_looper->on_button_long();
        g_display_state.looper_state        = static_cast<uint8_t>(LooperState::Idle);
        g_display_state.loop_length_samples = 0;
    }
    else if (role == ButtonRole::AutoDrumStyle)
    {
        g_auto->stop();
        g_leds->set_steady(LedId::LoopDrum, false);
        g_display_state.auto_active = false;
        g_display_state.style       = static_cast<uint8_t>(AutoStyle::Off);
        LOG_I("AUTO", "stopped");
    }
    else if (role == ButtonRole::AutoDrumSpeed)
    {
        while (g_auto->speed() != 1)
            g_auto->next_speed();
        g_display_state.speed = g_auto->speed();
        g_display_state.bpm   = bpm_for(static_cast<uint8_t>(g_auto->style()), g_auto->speed());
        LOG_I("AUTO", "speed reset to normal");
    }
}

// BPM table mirrors auto_drummer.cpp; indexed by AutoStyle enum value (0=Off…7=HardRock).
static uint8_t bpm_for(uint8_t style_val, uint8_t speed)
{
    static const uint8_t kBpm[8][3] = {
        {  0,   0,   0}, // Off
        { 70,  90, 110}, // Blues
        { 85, 100, 120}, // Country
        { 80, 120, 160}, // Jazz
        { 80, 100, 120}, // Funk
        { 65,  80,  95}, // Reggae
        { 70,  90, 110}, // Gospel
        {100, 120, 140}, // HardRock
    };
    return (style_val < 8u && speed < 3u) ? kBpm[style_val][speed] : 0u;
}

void display_task(void*)
{
    g_display_ui->init();
    DisplayState snap = {};
    for (;;)
    {
        snap.style               = g_display_state.style;
        snap.auto_active         = g_display_state.auto_active;
        snap.speed               = g_display_state.speed;
        snap.bpm                 = g_display_state.bpm;
        snap.looper_state        = g_display_state.looper_state;
        snap.loop_length_samples = g_display_state.loop_length_samples;
        snap.pos_samples         = g_display_state.pos_samples;
        snap.voice_count         = g_display_state.voice_count;
        snap.reverb_on           = g_display_state.reverb_on;
        snap.cpu_max_us          = g_display_state.cpu_max_us;
        snap.cpu_avg_us          = g_display_state.cpu_avg_us;
        g_display_ui->render(snap);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_MS));
#pragma GCC diagnostic pop
    }
}

void app_task(void*)
{
    static int16_t buf[I2S_DMA_BUF_LEN]; // one DMA-slot-sized output buffer

    uint32_t loop_max_us = 0;
    uint64_t loop_total  = 0;
    uint32_t loop_n      = 0;
    uint32_t last_log_ms = 0;

    if (esp_task_wdt_add(nullptr) != ESP_OK)
        LOG_E("TWDT", "register failed — no watchdog coverage");

    for (;;)
    {
        const uint64_t t0 = g_hal->clock->micros();

        // 1. Drain all queued button events this iteration.
        for (;;)
        {
            ButtonEvent ev = g_buttons->poll();
            if (ev.type == ButtonEventType::None)
                break;
            LOG_I("BTN", "%s idx=%u pin=%d", event_name(ev.type), static_cast<unsigned>(ev.index),
                  g_buttons->pin_of(ev.index));
            const auto role = static_cast<ButtonRole>(ev.index);
            if (ev.type == ButtonEventType::Press)
                handle_press(role, ev);
            else if (ev.type == ButtonEventType::LongPress)
                handle_long_press(role);
        }

        // 2. LED timers.
        g_leds->tick();

        // 3+5. Two AUDIO_CHUNK halves → fill the full DMA buffer.
        //      Scheduler and mixer are ticked per half so event timing stays
        //      accurate at AUDIO_CHUNK granularity.
        for (int half = 0; half < 2; ++half)
        {
            uint8_t trig[MAX_CONCURRENT_TRIG];
            uint8_t vel[MAX_CONCURRENT_TRIG];
            // Looper wins when active; its live hits play at full velocity.
            // Auto-drummer supplies per-hit (humanized) velocities.
            size_t     fired     = g_looper->tick(AUDIO_CHUNK, trig, MAX_CONCURRENT_TRIG);
            const bool from_auto = (fired == 0);
            if (from_auto)
                fired = g_auto->tick(AUDIO_CHUNK, trig, vel, MAX_CONCURRENT_TRIG);
            for (size_t i = 0; i < fired; ++i)
            {
                g_pool->trigger(trig[i],
                                from_auto ? vel[i] : static_cast<uint8_t>(MIDI_VELOCITY_MAX));
                g_leds->flash(led_for_sample(trig[i]));
                g_leds->flash(LedId::LoopDrum);
            }
            g_mixer->get_samples(buf + half * AUDIO_CHUNK, AUDIO_CHUNK);
        }

        // Update display state: voice count + looper position (cheap volatile writes).
        g_display_state.voice_count = static_cast<uint16_t>(g_pool->active_count());
        const LooperState ls = g_looper->state();
        if (ls == LooperState::Recording)
            g_display_state.pos_samples = g_looper->record_pos();
        else if (ls == LooperState::Playing)
            g_display_state.pos_samples = g_looper->play_pos();

        // Profiler: measure CPU work before the blocking write.
        const uint32_t dt = static_cast<uint32_t>(g_hal->clock->micros() - t0);
        if (dt > loop_max_us)
            loop_max_us = dt;
        loop_total += dt;
        ++loop_n;
        if (dt > PROFILER_WARN_THRESHOLD_US)
            LOG_W("LOOP", "slow iteration %u us", static_cast<unsigned>(dt));

        const uint32_t now_ms = g_hal->clock->millis();
        if (now_ms - last_log_ms >= PROFILER_LOG_INTERVAL_MS)
        {
            const uint32_t avg = static_cast<uint32_t>(loop_total / loop_n);
            LOG_I("LOOP", "cpu max=%u avg=%u us  n=%u", static_cast<unsigned>(loop_max_us),
                  static_cast<unsigned>(avg), static_cast<unsigned>(loop_n));
            g_display_state.cpu_max_us = loop_max_us;
            g_display_state.cpu_avg_us = avg;
            loop_max_us = 0;
            loop_total  = 0;
            loop_n      = 0;
            last_log_ms = now_ms;
        }

        // 4. Blocking write — yields to RTOS until DMA accepts the full buffer.
        //    No vTaskDelay needed; write() blocks internally via portMAX_DELAY.
        g_hal->i2s->write(buf, I2S_DMA_BUF_LEN);
        esp_task_wdt_reset(); // confirm loop completed; if write() stalls WDT fires
    }
}

} // namespace

void setup()
{
    g_hal = HalFactory::create();
    g_hal->serial->init(115200);
    ::dummer::log::set_sink(g_hal->serial);
    LOG_I("BOOT", "sr=%d chunk=%d dma=%dx%d buf=%d ms", SAMPLE_RATE, AUDIO_CHUNK, I2S_DMA_BUF_COUNT,
          I2S_DMA_BUF_LEN, 1000 * I2S_DMA_BUF_COUNT * I2S_DMA_BUF_LEN / SAMPLE_RATE);

    if (!g_hal->i2s->init(SAMPLE_RATE, I2S_BCLK, I2S_LRCK, I2S_DOUT))
    {
        LOG_E("BOOT", "I2S init failed — halting");
        while (true)
            vTaskDelay(pdMS_TO_TICKS(500));
    }

    static ButtonManager bm(g_hal->gpio, g_hal->clock);
    static LedManager    lm(g_hal->gpio, g_hal->clock);
    static VoicePool     vp;
    static Reverb        rv;
    static Mixer         mx(vp, kSampleTable, static_cast<uint8_t>(SAMPLE_COUNT), &rv);
    static Looper        lo;
    static AutoDrummer   ad;

    static DisplayUi du(*g_hal->display);

    g_buttons    = &bm;
    g_leds       = &lm;
    g_pool       = &vp;
    g_mixer      = &mx;
    g_reverb     = &rv;
    g_looper     = &lo;
    g_auto       = &ad;
    g_display_ui = &du;

    g_buttons->begin();
    g_leds->begin();

    BaseType_t ok = xTaskCreatePinnedToCore(app_task, "dummer", APP_TASK_STACK, nullptr,
                                            APP_TASK_PRIORITY, nullptr, APP_TASK_CORE);
    // pdPASS is `((BaseType_t)1)` — wrap the comparison to keep -Wold-style-cast quiet.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
    if (ok != pdPASS)
    {
        LOG_E("BOOT", "xTaskCreatePinnedToCore failed — halting");
        while (true)
            vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (xTaskCreatePinnedToCore(display_task, "display", DISPLAY_TASK_STACK, nullptr,
                                DISPLAY_TASK_PRIORITY, nullptr, DISPLAY_TASK_CORE) != pdPASS)
        LOG_W("BOOT", "display task creation failed");
#pragma GCC diagnostic pop
}

void loop()
{
    // Intentionally empty — all real work runs on the pinned task.
}

#endif // BUILD_ESP32
