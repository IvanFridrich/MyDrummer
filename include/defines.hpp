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
#define ENABLE_END_RAMP             1
#define VOICE_END_RAMP_SAMPLES      64       // linear taper to zero on natural end

// ===== Audio math =====
#define MIDI_VELOCITY_MAX           127
#define Q15_UNITY                   32767    // Q15 full-scale (1.0 - 1 LSB)

// Per-voice trigger gain. Spec §2 calls for full-scale output (Q15_UNITY) with
// the analog pot doing the volume work. Until the pot is wired, lower this
// to attenuate in software — try 16384 (-6 dB) or 8192 (-12 dB) if your
// listening device's input is clipping the PCM5102's 2 Vrms peak.
#define MASTER_VOLUME_Q15           16384

// ===== Reverb =====
#define REVERB_ENABLED_DEFAULT      0
#define REVERB_WET_Q15              4096     // 0.125
#define REVERB_COMB_DELAYS          { 1116, 1188, 1277, 1356 }
#define REVERB_COMB_FEEDBACK_Q15    24576    // ~0.75
#define REVERB_ALLPASS_DELAYS       { 556, 441 }
#define REVERB_ALLPASS_COEF_Q15     22938    // ~0.7

// ===== Looper =====
#define LOOPER_MAX_EVENTS           128
#define LOOPER_MAX_DURATION_S       60
#define MAX_CONCURRENT_TRIG         16   // max triggers fired in one AUDIO_CHUNK tick

// ===== Buttons =====
#define BUTTON_COUNT                7
#define BUTTON_PINS                 { 4, 12, 14, 23, 32, 33, 27 }
#define BUTTON_DEBOUNCE_MS          15
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

// ===== Auto-drummer =====
#define AUTO_DRUMMER_PPQN            480   // ticks per quarter note (matches gen_patterns.py)
#define AUTO_DRUMMER_MAX_INTRO       64    // max events in count-in + intro (bars 0-2)
#define AUTO_DRUMMER_MAX_LOOP        256   // max events in the looping section

// BPM tables: slow / normal / fast (normal matches the generated .mid files)
#define BPM_BLUES        {  70,  90, 110 }
#define BPM_JAZZ         {  80, 120, 160 }
#define BPM_FUNK         {  80, 100, 120 }
#define BPM_REGGAE       {  65,  80,  95 }
#define BPM_GOSPEL       {  70,  90, 110 }
#define BPM_HARDROCK     { 100, 120, 140 }

// ===== Humanization (M7) =====
// Subtle randomisation of auto-drummer timing and velocity for a less
// machine-like feel. Timing jitter is +/- HUMANIZE_TIME_SAMPLES per event,
// re-drawn every loop iteration; velocity jitter is +/- HUMANIZE_VEL.
// Driven by a deterministic 16-bit LFSR (no heap, no <random>).
#define HUMANIZE_ENABLED_DEFAULT    1
#define HUMANIZE_TIME_SAMPLES       220      // ~+/-5 ms at 44.1 kHz
#define HUMANIZE_VEL                8        // +/-8 MIDI velocity steps
#define HUMANIZE_LFSR_SEED          0xACE1u

// ===== Profiler =====
#define PROFILER_LOG_INTERVAL_MS    1000
#define PROFILER_WARN_THRESHOLD_US  2000

// ===== Task / core =====
#define APP_TASK_CORE               1
#define APP_TASK_STACK              8192
#define APP_TASK_PRIORITY           5
