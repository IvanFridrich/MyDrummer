#ifdef BUILD_NATIVE_TEST

#include "hal_native.hpp"
#include "hal_factory.hpp"

namespace dummer { namespace hal { namespace native {

// ----- NativeGpio -----
void NativeGpio::pin_mode(int pin, int mode) { modes_[pin] = mode; }

bool NativeGpio::read(int pin) {
    auto it = values_.find(pin);
    // Default for unset pin: high (matches INPUT_PULLUP idle, active-low buttons).
    return it == values_.end() ? true : it->second;
}

void NativeGpio::write(int pin, bool value) { values_[pin] = value; }

void NativeGpio::set_pin(int pin, bool value) { values_[pin] = value; }

bool NativeGpio::get_written(int pin) const {
    auto it = values_.find(pin);
    return it == values_.end() ? false : it->second;
}

int NativeGpio::get_mode(int pin) const {
    auto it = modes_.find(pin);
    return it == modes_.end() ? -1 : it->second;
}

void NativeGpio::reset() { values_.clear(); modes_.clear(); }

// ----- NativeI2s -----
bool NativeI2s::init(int sample_rate, int bclk, int lrck, int dout) {
    sample_rate_ = sample_rate; bclk_ = bclk; lrck_ = lrck; dout_ = dout;
    initialised_ = true;
    return true;
}

void NativeI2s::write(const int16_t* buf, size_t samples) {
    captured_.insert(captured_.end(), buf, buf + samples);
}

// ----- NativeSerial -----
void NativeSerial::init(int baud) { baud_ = baud; }
void NativeSerial::write(const char* s) { buffer_ += s; }

// ----- Singletons -----
namespace {
NativeGpio   g_gpio;
NativeI2s    g_i2s;
NativeSerial g_serial;
NativeClock  g_clock;
Hal          g_hal{ &g_gpio, &g_i2s, &g_serial, &g_clock };
}

NativeGpio&   gpio()   { return g_gpio; }
NativeI2s&    i2s()    { return g_i2s; }
NativeSerial& serial() { return g_serial; }
NativeClock&  clock()  { return g_clock; }

void reset_all() {
    g_gpio.reset();
    g_i2s.clear();
    g_serial.clear();
    g_clock.set_now_us(0);
}

}}} // namespace dummer::hal::native

namespace dummer { namespace hal {
Hal* HalFactory::create() { return &native::g_hal; }
}}

#endif // BUILD_NATIVE_TEST
