// Test-side accessors for the native HAL stubs.
// Tests can script GPIO levels, advance the simulated clock, and inspect
// captured I2S writes / serial output.
#pragma once

#ifdef BUILD_NATIVE_TEST

#include "hal/hal.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dummer
{
namespace hal
{
namespace native
{

class NativeGpio : public IGpio
{
  public:
    void pin_mode(int pin, int mode) override;
    bool read(int pin) override;
    void write(int pin, bool value) override;

    // --- test helpers ---
    void set_pin(int pin, bool value); // simulate external input
    bool get_written(int pin) const;
    int  get_mode(int pin) const;
    void reset();

  private:
    std::map<int, bool> values_; // current pin state (input/output)
    std::map<int, int>  modes_;
};

class NativeI2s : public II2s
{
  public:
    bool init(int sample_rate, int bclk, int lrck, int dout) override;
    void write(const int16_t* buf, size_t samples) override;

    // --- test helpers ---
    const std::vector<int16_t>& captured() const
    {
        return captured_;
    }
    void clear()
    {
        captured_.clear();
    }

    int sample_rate() const
    {
        return sample_rate_;
    }
    int bclk() const
    {
        return bclk_;
    }
    int lrck() const
    {
        return lrck_;
    }
    int dout() const
    {
        return dout_;
    }
    bool initialised() const
    {
        return initialised_;
    }

  private:
    std::vector<int16_t> captured_;
    int                  sample_rate_ = 0;
    int                  bclk_ = 0, lrck_ = 0, dout_ = 0;
    bool                 initialised_ = false;
};

class NativeSerial : public ISerial
{
  public:
    void init(int baud) override;
    void write(const char* s) override;

    // --- test helpers ---
    const std::string& buffer() const
    {
        return buffer_;
    }
    int baud() const
    {
        return baud_;
    }
    void clear()
    {
        buffer_.clear();
    }

  private:
    std::string buffer_;
    int         baud_ = 0;
};

class NativeClock : public IClock
{
  public:
    uint64_t micros() override
    {
        return now_us_;
    }
    uint32_t millis() override
    {
        return static_cast<uint32_t>(now_us_ / 1000);
    }
    void delay_ms(uint32_t ms) override
    {
        now_us_ += static_cast<uint64_t>(ms) * 1000;
    }

    // --- test helpers ---
    void advance_us(uint64_t us)
    {
        now_us_ += us;
    }
    void advance_ms(uint32_t ms)
    {
        now_us_ += static_cast<uint64_t>(ms) * 1000;
    }
    void set_now_us(uint64_t us)
    {
        now_us_ = us;
    }

  private:
    uint64_t now_us_ = 0;
};

// Direct access to the singletons for tests.
NativeGpio&   gpio();
NativeI2s&    i2s();
NativeSerial& serial();
NativeClock&  clock();

// Wipe all stub state (call between tests).
void reset_all();

} // namespace native
} // namespace hal
} // namespace dummer

#endif // BUILD_NATIVE_TEST
