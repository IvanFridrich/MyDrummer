# Dummer — ESP32 Drum Machine

Final project specification. Hand this to the assistant once the workspace is prepared.

---

## 1. Goal

A standalone hardware drum machine on ESP32 DevKitC with external I2S DAC. Plays drum samples on button press, with optional reverb, a one-layer looper, and an auto-drummer with several styles. Single-core super-loop architecture, fully testable via a stubbed HAL.

---

## 2. Hardware

### MCU
- ESP32 DevKitC (240 MHz, single-core use — pin task to core 1)
- Serial UART for logs at 115200 Bd

### DAC — PCM5102 board
- I2S master, TX, 32-bit slots with 16-bit data left-shifted into the upper bits
- `I2S_NUM_0`, `I2S_MODE_MASTER | I2S_MODE_TX`, `I2S_BITS_PER_SAMPLE_32BIT`, `I2S_CHANNEL_FMT_RIGHT_LEFT`, `I2S_COMM_FORMAT_STAND_I2S`
- No MCLK needed (PCM5102 has internal PLL)
- BCLK = GPIO26, LRCK = GPIO25, DOUT = GPIO22
- DMA: 4 buffers × 128 samples
- Sample rate: 44 100 Hz
- Mono — same sample written to both channels

### Output volume
- Analog potentiometer **on the DAC output line** (not on ADC).
- Software always outputs full-scale; no ADC reading, no software gain stage.

### Buttons — 7 total
- Pins (active low, internal `INPUT_PULLUP`): `{4, 12, 14, 23, 32, 33, 27}`
- Roles (in pin order):

| Index | Pin | Role |
|-------|-----|------|
| 0 | 4  | Kick |
| 1 | 12 | Snare |
| 2 | 14 | Hihat open |
| 3 | 23 | Reverb on/off |
| 4 | 32 | Looper |
| 5 | 33 | Auto-drummer style cycle |
| 6 | 27 | Auto-drummer speed cycle |

### LEDs — 5 total
- Kick (yellow), Snare (yellow), Hihat (yellow), Reverb (red), Looper+Autodrum (blue)
- GPIO pins **TBD** — to be filled into `defines.hpp` before first build.

---

## 3. Toolchain

- **PlatformIO** under VSCode (PlatformIO extension)
- **Arduino framework** with **FreeRTOS** (one task pinned to core 1; `loop()` empty)
- **C++17** for production and tests
- **Unity** test framework
- Build environments:
  - `release` — `-O3`, no logging
  - `debug` — `-Og -g3`, LOG_E/LOG_W/LOG_I enabled
  - `native_test` — `platform = native`, stubbed HAL, Unity tests run on host

### Scripts (Windows `.bat`)
Located in `scripts/`. Wrappers over PlatformIO commands:

- `build_all.bat` — runs `gen_patterns.py` + `wav_to_cpp.py` + `midi_to_cpp.py`, then builds release + debug + native_test, then runs unit tests. Fails fast.
- `build_release.bat`
- `build_debug.bat`
- `test.bat`
- `flash.bat` — uploads debug build by default
- `monitor.bat` — opens serial monitor at 115200, with `log2file` filter so output is mirrored to a file.

Every code change must end with a green `build_all.bat`.

---

## 4. Architecture

### Super-loop, one core
- `setup()` initialises HAL, audio engine, controls, then spawns one FreeRTOS task pinned to core 1.
- That task runs an unbounded super-loop. `loop()` (Arduino) stays empty.
- The loop must never block for more than ~1 ms anywhere. Button polling happens every iteration.

### Loop iteration
1. **Poll buttons** (debounce + edge detection + long-press timing).
2. **Tick LED manager** (handle 80 ms flashes).
3. **Tick scheduler** — auto-drummer or looper may emit voice triggers based on current time.
4. **Try to drain staging buffer to I2S** (non-blocking write, timeout = 0).
5. If staging buffer empty → **`audio.get_samples(CHUNK)`** into staging.
6. **Profile** loop iteration time.

The I2S write is non-blocking. Partial writes advance the staging head; samples are never regenerated. If the DMA is full this iteration, the loop just keeps polling buttons and tries again next time.

### Modules
```
hal/           — interfaces + two implementations (esp32 / native)
audio/
  voice          — single sample iterator over flash, fade state
  voice_pool     — 50-voice pool; retrigger fade-out logic
  mixer          — get_samples(N): sums voices, applies optional reverb
  reverb         — Schroeder 4 comb + 2 allpass
  looper         — event-based recorder/scheduler
  auto_drummer   — SMF parser + scheduler
control/
  button_manager — 7 buttons, debounce, edge, long-press, state
  led_manager    — flash and engine-driven modes; stacking allowed
util/
  log            — LOG_E/W/I macros, compiled out in release
  loop_profiler  — min/max/avg per second
app/
  app            — wires control events to audio engine; FSM for looper/autodrummer exclusion
main.cpp         — setup + task spawn; loop() empty
```

### Design patterns
- **Factory** for HAL selection. `HalFactory::create()` returns the right impl at compile time based on `BUILD_ESP32` / `BUILD_NATIVE_TEST`.
- All hardware access goes through HAL interfaces — modules under `audio/`, `control/`, `app/` are platform-agnostic and unit-testable on host.

---

## 5. Audio engine

### Sample storage
- WAV files: 16-bit mono 44.1 kHz, dropped by user into `assets/samples/`.
- Python script `scripts/wav_to_cpp.py` reads them and emits `generated/samples.hpp` + `generated/samples.cpp`. Data is `static constexpr int16_t[]` stored in flash.
- A `SampleDescriptor { const int16_t* data; uint32_t length; }` per sample.

### Voice
- Iterator over a `SampleDescriptor`. State: `position`, `gain_q15`, `fade_state` (none / retrigger_fade_out / end_ramp).
- ~16 bytes per voice.
- Per-sample generation: read `data[position]`, multiply by `gain_q15`, advance position, update fade.

### Voice pool
- `MAX_VOICES = 50`. Fixed array, no heap allocation.
- `trigger(sample_id)`:
  1. Scan pool for an active voice playing the same sample_id; if found, mark it `retrigger_fade_out` (it will fade exponentially).
  2. Allocate a free slot (or steal the oldest active voice if pool is full).
  3. Initialise the new voice playing from position 0.

### Retrigger / click avoidance
- **Exponential fade on retrigger of the same sample**: `gain *= RETRIGGER_FADE_K_Q15` per sample. With `k ≈ 0.99326` (Q15 = 32530), gain reaches −60 dB in ~23 ms.
- **Universal end ramp**: every voice does a 64-sample linear ramp to zero on natural sample end, to mask non-zero-ending WAVs.
- Both behaviours toggled by defines so they can be A/B-compared.

### Mixer
- `get_samples(int16_t* dst, size_t n)`:
  1. Zero an int32 accumulator of length n.
  2. For each active voice, generate n samples and add to accumulator.
  3. If reverb is enabled, run accumulator through reverb in place.
  4. Saturate-clip to int16, write to `dst`.
- Audio chunk size `AUDIO_CHUNK = 64` samples (~1.45 ms).

### Reverb — Schroeder
- 4 parallel comb filters → 2 series allpass filters (mono).
- Comb delays (samples): `{1116, 1188, 1277, 1356}`. Feedback ≈ 0.84.
- Allpass delays: `{556, 441}`. Coefficient ≈ 0.7.
- Wet/dry mix fixed at 0.25 (Q15 = 8192). Adjustable via define.
- Total delay-line RAM ≈ 12 KB.
- Off by default; btn4 toggles; red LED indicates state.

---

## 6. Buttons

- 7 buttons, active low, internal pull-up.
- Debounce: 50 ms (each button has independent debouncer).
- Events emitted: `PRESS` (debounced falling edge), `RELEASE` (debounced rising edge), `LONG_PRESS` (held 500 ms after press, fires once).
- Plus a queryable debounced state: `is_pressed(i)`.
- Pin list and roles per the table in section 2.

### Long-press semantics
| Button | Short press | Long press (500 ms) |
|--------|-------------|---------------------|
| Kick / Snare / Hihat | Trigger sample | No effect |
| Reverb | Toggle reverb | No effect |
| Looper | See looper FSM | Clear loop immediately and return to idle |
| Autodrum style | Cycle to next style | Stop autodrummer immediately (skip end-of-bar) |
| Autodrum speed | Cycle to next speed | Reset speed to `normal` |

---

## 7. LEDs

- 5 LEDs, GPIO pins TBD.
- Manual drum-button press: matching LED flashes for **80 ms**.
- When the auto-drummer or looper triggers a sample, the matching drum LED also flashes 80 ms (so visual feedback works whether you played it or the engine did).
- Blue LED (`LED_LOOP_DRUM`) lights briefly on every event emitted by either the looper or the auto-drummer.
- Reverb LED is steady on while reverb is enabled, off otherwise (not a flash).
- Multiple LEDs may flash simultaneously — no mutual exclusion, no priority.

---

## 8. Looper — event-based

### Capacity
- `LOOPER_MAX_EVENTS = 128`
- Each event = `{ uint32_t time_samples; uint8_t sample_id; }`. Tiny.
- `LOOPER_MAX_DURATION_S = 60` — safety cap; if user never presses btn5 the second time, recording auto-finalises at 60 s.

### FSM
```
Idle ──btn5──> Arming ──first drum hit──> Recording ──btn5──> Playing ──btn5──> Idle
        │                                        │                       │
        └── btn5 again (no hit yet) ─> Idle      └── overflow / 60 s ──> Playing
        │
        └── long-press btn5 ─> Idle (any state)
```

- **Idle → Arming**: btn5 short press. Looper waits for the first drum hit.
- **Arming → Recording**: first kick/snare/hihat press. `t0` set to that moment.
- **Arming → Idle**: btn5 short-pressed again without any hit → cancel.
- **Recording → Playing**: btn5 short-pressed. Loop length = now − t0. Playback starts immediately from beginning of loop.
- **Recording overflow**: if 128 events filled, ignore further events (debug log) but keep recording length until btn5 or 60 s.
- **Playing → Idle**: btn5 short-pressed → stop and clear loop.
- **Any → Idle**: btn5 long-pressed → stop and clear.

Manual drumming during Recording or Playing is allowed and audible. Manual hits during Recording are also captured into events.

### Mutual exclusion with auto-drummer
- Pressing btn5 while the auto-drummer is active **stops the auto-drummer immediately** then enters Arming.
- Looper and auto-drummer cannot run simultaneously. Only:
  - Manual + Looper, or
  - Manual + Auto-drummer.

---

## 9. Auto-drummer

### Patterns
- Standard MIDI File format 0, channel 10, GM drum map.
- Built-in: `blues`, `jazz`, `funk`, `reggae`, `gospel`, `hardrock`.
- Stored as `static constexpr uint8_t[]` SMF blobs in `generated/patterns.cpp`, emitted by `scripts/midi_to_cpp.py`.
- Sample mapping (GM note → internal sample): kick=35/36, snare=38/40, hihat closed=42, hihat open=46, ride=51, crash=49, tom=45/47/48/50, cowbell=56, sticks=31.

### Pattern structure (improv-friendly design)
Each `.mid` file follows the same structure:
- **Bar 0**: Count-in — 4 stick clicks on quarter notes (plays once).
- **Bars 1–N**: Intro — more elaborate arrangement to set the mood (plays once).
- **[LOOP_START meta marker]**
- **Main loop**: Simple, steady groove designed to support improvisation.
  - Blues / Jazz: **12-bar** form with crash accents on chord changes (IV at bar 5, V at bar 9, turnaround fill at bar 12) so musicians always know where they are.
  - Funk / Reggae: **4-bar** vamp — crash/fill at bar 4 marks the restart.
  - Gospel / Hardrock: **8-bar** — crash on bar 1 and bar 5, fill on bar 8.
- Main-loop groove is kept intentionally simple so it supports improvisation without distracting.
- Fills and crashes at phrase boundaries signal the structure to the improviser.

### Speed per style
- Three BPM presets per style (slow / normal / fast):

| Style    | Slow | Normal | Fast |
|----------|------|--------|------|
| Blues    |  70  |   90   | 110  |
| Jazz     |  80  |  120   | 160  |
| Funk     |  80  |  100   | 120  |
| Reggae   |  65  |   80   |  95  |
| Gospel   |  70  |   90   | 110  |
| Hardrock | 100  |  120   | 140  |

- Speed setting persists across style changes (so cycling style at "fast" stays fast for the next style too).

### Control
- **btn6 short press** — cycle: `off → blues → jazz → funk → reggae → gospel → hardrock → off`.
- **btn7 short press** — cycle: `slow → normal → fast → slow` for the current style.
- **btn6 long press** — stop immediately.
- Entering a style: count-in bar (sticks) is embedded in the MIDI file and plays once before the loop.
- Changing style while playing: finishes the current bar then switches at the next downbeat.
- Changing speed while playing: takes effect at the next downbeat.

### Mutual exclusion with looper
- Pressing btn6 while the looper is active (Arming / Recording / Playing) **stops and clears the looper**, then starts the new style.

---

## 10. Humanization

Makes auto-drummer and looper playback feel less mechanical by adding small
random deviations to timing and velocity on every triggered event.

### Timing jitter
- Each event gets a signed random offset drawn uniformly from
  `[-HUMANIZE_TIMING_MAX, +HUMANIZE_TIMING_MAX]` samples.
- `HUMANIZE_TIMING_MAX = 220` (≈ 5 ms at 44 100 Hz). Adjustable via define.
- Positive offset delays the hit; negative fires it slightly early.
- Result is clamped so no event fires before the current loop start.

### Velocity variation
- Each event's base velocity gets ±`HUMANIZE_VELOCITY_MAX` random variation.
- `HUMANIZE_VELOCITY_MAX = 8` (out of 127). Adjustable via define.
- Result is clamped to [1, 127].

### PRNG
- 16-bit Galois LFSR, seeded from `esp_random()` on ESP32 and from a fixed
  constant in `BUILD_NATIVE_TEST` (so unit tests stay deterministic).
- One advance per triggered event; stored as a field inside `AutoDrummer`
  and `Looper` respectively.

### Toggle
- `HUMANIZE_ENABLED = 1` by default.
- Set to `0` to disable (deterministic mode for tests and debugging).

### Does not apply to
- Manual drum button presses (those are already human).

---

## 11. Logging

- `LOG_E(tag, fmt, ...)`, `LOG_W(tag, fmt, ...)`, `LOG_I(tag, fmt, ...)`.
- `tag` is a short module name string (`"MIXER"`, `"LOOPER"`, etc.).
- Compiled out entirely in release.
- Serial 115200, written via the HAL serial interface.

---

## 12. Loop profiler

- Active in debug only (compiled out in release).
- Tracks iteration time (`esp_timer_get_time()`).
- Reports min / max / avg per 1 s window via `LOG_I("PROF", …)`.
- Warns if any iteration exceeds 2 ms.

---

## 13. HAL interfaces

`include/hal/hal.hpp` declares:

```cpp
struct IGpio {
    virtual void pin_mode(int pin, int mode) = 0;
    virtual bool read(int pin) = 0;
    virtual void write(int pin, bool value) = 0;
};

struct II2s {
    virtual bool init(int sample_rate, int bclk, int lrck, int dout) = 0;
    // Writes up to `bytes`, returns bytes actually written (0 = busy). Never blocks.
    virtual size_t write_nonblocking(const int16_t* buf, size_t samples) = 0;
};

struct ISerial {
    virtual void init(int baud) = 0;
    virtual void write(const char* s) = 0;
};

struct IClock {
    virtual uint64_t micros() = 0;
    virtual uint32_t millis() = 0;
    virtual void delay_ms(uint32_t ms) = 0;
};

struct Hal {
    IGpio*   gpio;
    II2s*    i2s;
    ISerial* serial;
    IClock*  clock;
};
```

`HalFactory::create()` returns a populated `Hal*`:
- `hal_esp32.cpp` is compiled when `BUILD_ESP32` is defined.
- `hal_native.cpp` is compiled when `BUILD_NATIVE_TEST` is defined and provides recordable stubs (button state can be scripted, I2S writes captured, etc.).

No `IAdc` — volume is analog hardware only.

---

## 14. `defines.hpp` — central config

```cpp
// ===== Audio =====
#define SAMPLE_RATE                 44100
#define AUDIO_CHUNK                 64
#define I2S_NUM                     I2S_NUM_0
#define I2S_BCLK                    26
#define I2S_LRCK                    25
#define I2S_DOUT                    22
#define I2S_DMA_BUF_COUNT           4
#define I2S_DMA_BUF_LEN             128

// ===== Voice pool =====
#define MAX_VOICES                  50
#define ENABLE_RETRIGGER_DECAY      1
#define RETRIGGER_FADE_K_Q15        32530    // ~0.99326 -> -60 dB in ~23 ms
#define VOICE_END_RAMP_SAMPLES      64

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

// ===== LEDs ===== (pins TBD before first build)
#define LED_KICK_PIN                /* TBD */
#define LED_SNARE_PIN               /* TBD */
#define LED_HIHAT_PIN               /* TBD */
#define LED_REVERB_PIN              /* TBD */
#define LED_LOOP_DRUM_PIN           /* TBD */
#define LED_FLASH_MS                80

// ===== Auto-drummer BPMs =====  (slow / normal / fast)
#define BPM_BLUES        { 70,  90, 110 }
#define BPM_JAZZ         { 80, 120, 160 }
#define BPM_FUNK         { 80, 100, 120 }
#define BPM_REGGAE       { 65,  80,  95 }
#define BPM_GOSPEL       { 70,  90, 110 }
#define BPM_HARDROCK     {100, 120, 140 }

// ===== Humanization =====
#define HUMANIZE_ENABLED         1
#define HUMANIZE_TIMING_MAX      220    // samples (≈5 ms at 44100 Hz)
#define HUMANIZE_VELOCITY_MAX    8      // ± out of 127

// ===== Profiler =====
#define PROFILER_LOG_INTERVAL_MS    1000
#define PROFILER_WARN_THRESHOLD_US  2000
```

---

## 15. File layout

```
Dummer/
├── platformio.ini
├── .gitignore                    # ignore .pio/, generated/, *.log, build/
├── SPEC.md                       # this document
├── README.md
├── scripts/
│   ├── build_all.bat
│   ├── build_release.bat
│   ├── build_debug.bat
│   ├── test.bat
│   ├── flash.bat
│   ├── monitor.bat
│   ├── wav_to_cpp.py
│   ├── midi_to_cpp.py
│   └── gen_patterns.py
├── assets/
│   ├── samples/                  # USER drops 16-bit mono 44.1 kHz WAVs here
│   │   ├── kick.wav
│   │   ├── snare.wav
│   │   ├── hihat_open.wav
│   │   ├── hihat_closed.wav
│   │   ├── ride.wav
│   │   ├── cowbell.wav
│   │   ├── sticks.wav
│   │   ├── crash.wav
│   │   └── tom.wav
│   └── patterns/                 # SMF MIDI files for auto-drummer styles
│       ├── blues.mid
│       ├── hardrock.mid
│       ├── reggae.mid
│       ├── funk.mid
│       ├── country.mid
│       └── metronome.mid
├── generated/                    # auto-generated, gitignored
│   ├── samples.hpp / .cpp
│   └── patterns.hpp / .cpp
├── include/
│   └── defines.hpp
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── app.hpp / .cpp
│   ├── audio/
│   │   ├── voice.hpp / .cpp
│   │   ├── voice_pool.hpp / .cpp
│   │   ├── mixer.hpp / .cpp
│   │   ├── reverb.hpp / .cpp
│   │   ├── looper.hpp / .cpp
│   │   └── auto_drummer.hpp / .cpp
│   ├── control/
│   │   ├── button_manager.hpp / .cpp
│   │   └── led_manager.hpp / .cpp
│   ├── util/
│   │   ├── log.hpp
│   │   └── loop_profiler.hpp / .cpp
│   └── hal/
│       ├── hal.hpp
│       ├── hal_factory.hpp / .cpp
│       ├── hal_esp32.cpp        # compiled when BUILD_ESP32
│       └── hal_native.cpp       # compiled when BUILD_NATIVE_TEST
└── test/
    ├── test_voice_pool/
    ├── test_mixer/
    ├── test_reverb/
    ├── test_button_manager/
    ├── test_looper/
    └── test_auto_drummer/
```

---

## 16. `platformio.ini` outline

```ini
[platformio]
default_envs = debug

[env]
framework = arduino
build_unflags = -std=gnu++11
build_flags = -std=gnu++17

[env:release]
platform = espressif32
board = esp32dev
build_flags = ${env.build_flags} -O3 -DBUILD_ESP32 -DBUILD_RELEASE -DLOG_LEVEL=0
monitor_speed = 115200

[env:debug]
platform = espressif32
board = esp32dev
build_type = debug
build_flags = ${env.build_flags} -Og -g3 -DBUILD_ESP32 -DBUILD_DEBUG -DLOG_LEVEL=3
monitor_speed = 115200
monitor_filters = log2file, time

[env:native_test]
platform = native
test_framework = unity
build_flags = ${env.build_flags} -DBUILD_NATIVE_TEST -DLOG_LEVEL=3
```

---

## 17. Workflow

1. **Every change** to source code → run `build_all.bat`. All three builds must pass and all unit tests must be green before reporting work as done.
2. **Frequent commits** — small, focused.
3. The assistant should **ask for review / approval at meaningful checkpoints** (HAL skeleton, first sound, looper FSM, etc.) before continuing.
4. After each meaningful checkpoint, the assistant should **ask the user to test on real hardware** and report back.
5. UI / hardware behaviour cannot be verified from code alone; treat unit-test green as necessary-but-not-sufficient.

---

## 18. Open items the user still needs to provide

1. **5 LED GPIO pins** — fill into `LED_*_PIN` in `defines.hpp`.
2. **WAV samples** in `assets/samples/` — 16-bit mono 44.1 kHz, names per file layout above.
3. **MIDI patterns** in `assets/patterns/` — one SMF file per style. If absent on first build, `midi_to_cpp.py` will emit empty stubs and the auto-drummer will run with placeholder beats until real files are supplied.

---

## 19. First milestones (suggested ordering, each ends with a check-in)

1. Project skeleton: `platformio.ini`, `defines.hpp`, HAL interfaces, both HAL impls, blink + button-echo to serial. Three builds pass; trivial unit test on `button_manager` runs.
2. WAV pipeline: `wav_to_cpp.py`, generated sample headers, `Voice` + `VoicePool`, `Mixer` with no reverb. Manual drumming works on real hardware.
3. Retrigger fade + end ramp. Verify by ear and on a captured I2S log in tests.
4. Reverb + reverb toggle + LED.
5. Looper FSM end-to-end.
6. Auto-drummer — all 6 styles (blues, jazz, funk, reggae, gospel, hardrock). Blues and jazz use 12-bar form with crash accents on chord changes. `scripts/midi_to_cpp.py` + `auto_drummer.hpp/cpp` + full unit tests.
7. Humanization — timing jitter (±5 ms) and velocity variation (±8) on auto-drummer and looper playback. Galois LFSR PRNG, seeded from `esp_random()`. Toggle via `HUMANIZE_ENABLED` define.
8. Loop profiler + perf check under worst case (50 voices + reverb + autodrummer).

Each milestone ends with: green `build_all.bat`, a commit, and a real-hardware test request to the user.
