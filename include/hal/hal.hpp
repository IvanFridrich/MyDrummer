// Hardware abstraction layer interfaces.
// Two implementations exist:
//   - hal_esp32.cpp   (compiled when BUILD_ESP32 is defined)
//   - hal_native.cpp  (compiled when BUILD_NATIVE_TEST is defined; recordable stubs)
//
// All audio/control/app code consumes these interfaces only; nothing else
// touches Arduino / FreeRTOS / driver headers directly.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dummer { namespace hal {

// Pin-mode values passed to IGpio::pin_mode. Platform impl maps to native equivalents.
enum PinMode {
    PIN_INPUT        = 0,
    PIN_INPUT_PULLUP = 1,
    PIN_OUTPUT       = 2,
};

struct IGpio {
    virtual ~IGpio() = default;
    virtual void pin_mode(int pin, int mode) = 0;
    virtual bool read(int pin) = 0;
    virtual void write(int pin, bool value) = 0;
};

struct II2s {
    virtual ~II2s() = default;
    virtual bool init(int sample_rate, int bclk, int lrck, int dout) = 0;
    // Blocks until all `samples` are accepted by the DMA ring (portMAX_DELAY on
    // ESP32; instant capture in native tests). Use this from the audio task.
    virtual void write(const int16_t* buf, size_t samples) = 0;
};

struct ISerial {
    virtual ~ISerial() = default;
    virtual void init(int baud) = 0;
    virtual void write(const char* s) = 0;
};

struct IClock {
    virtual ~IClock() = default;
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

}} // namespace dummer::hal
