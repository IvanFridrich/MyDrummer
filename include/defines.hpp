// Central configuration for the Dummer drum machine.
// Every tunable lives here; modules pull values via these macros / enum.
#pragma once

#include <stdint.h>

#ifdef BUILD_ESP32
  #include <driver/i2s.h>
#endif

// ===== Audio =====
#define SAMPLE_RATE                 44100
#define AUDIO_CHUNK                 64
#ifdef BUILD_ESP32
  #define I2S_NUM                   I2S_NUM_0
#endif
#define I2S_BCLK                    26
#define I2S_LRCK                    25
#define I2S_DOUT                    22
// Spec §2 quoted 4×128; bumped to 8 buffers (~23 ms cushion) to ride out
// any timing jitter from the cooperative super-loop.
#define I2S_DMA_BUF_COUNT           8
#define I2S_DMA_BUF_LEN             128

// ===== Voice pool =====
#define MAX_VOICES                  50
#define ENABLE_RETRIGGER_DECAY      1
#define RETRIGGER_FADE_K_Q15        32530    // ~0.99326 -> -60 dB in ~23 ms
#define VOICE_END_RAMP_SAMPLES      64

// Per-voice trigger gain. Spec §2 calls for full-scale output (32767) with
// the analog pot doing the volume work. Until the pot is wired, lower this
// to attenuate in software — try 16384 (-6 dB) or 8192 (-12 dB) if your
// listening device's input is clipping the PCM5102's 2 Vrms peak.
#define MASTER_VOLUME_Q15           16384

// ===== Reverb =====
#define REVERB_ENABLED_DEFAULT      0
#define REVERB_WET_Q15              8192     // 0.25
#define REVERB_COMB_DELAYS          { 1116, 1188, 1277, 1356 }
#define REVERB_COMB_FEEDBACK_Q15    27525    // ~0.84
#define REVERB_ALLPASS_DELAYS       { 556, 441 }
#define REVERB_ALLPASS_COEF_Q15     22938    // ~0.7

// ===== Looper =====
#define LOOPER_MAX_EVENTS           128
#define LOOPER_MAX_DURATION_S       60

// ===== Buttons =====
#define BUTTON_COUNT                7
#define BUTTON_PINS                 { 4, 12, 14, 23, 32, 33, 27 }
#define BUTTON_DEBOUNCE_MS          50
#define BUTTON_LONGPRESS_MS         500
#define BUTTON_ACTIVE_LOW           1

enum class ButtonRole {
  Kick = 0,
  Snare,
  HihatOpen,
  ReverbToggle,
  Looper,
  AutoDrumStyle,
  AutoDrumSpeed,
};

// ===== LEDs ===== (pins TBD before first build; -1 = unused, led_manager no-ops)
#define LED_KICK_PIN                -1
#define LED_SNARE_PIN               -1
#define LED_HIHAT_PIN               -1
#define LED_REVERB_PIN              -1
#define LED_LOOP_DRUM_PIN           -1
#define LED_FLASH_MS                80

// Heartbeat LED for milestone 1 — onboard LED on most ESP32 dev boards.
#define LED_HEARTBEAT_PIN           2
#define LED_HEARTBEAT_PERIOD_MS     1000

// ===== Auto-drummer BPMs =====  (slow / normal / fast)
#define BPM_BLUES        {  70,  90, 110 }
#define BPM_HARDROCK     { 100, 120, 140 }
#define BPM_REGGAE       {  65,  80,  95 }
#define BPM_FUNK         {  90, 105, 125 }
#define BPM_COUNTRY      {  90, 110, 130 }
#define BPM_METRONOME    {  60, 100, 160 }

// ===== Profiler =====
#define PROFILER_LOG_INTERVAL_MS    1000
#define PROFILER_WARN_THRESHOLD_US  2000

// ===== Task / core =====
#define APP_TASK_CORE               1
#define APP_TASK_STACK              8192
#define APP_TASK_PRIORITY           5
