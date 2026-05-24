#ifdef BUILD_ESP32

#include "hal/hal.hpp"
#include "hal_factory.hpp"
#include "defines.hpp"

#include <Arduino.h>
#include <driver/i2s.h>

namespace dummer { namespace hal {

namespace {

class Esp32Gpio : public IGpio {
  public:
    void pin_mode(int pin, int mode) override {
        switch (mode) {
            case PIN_INPUT:        pinMode(pin, INPUT);        break;
            case PIN_INPUT_PULLUP: pinMode(pin, INPUT_PULLUP); break;
            case PIN_OUTPUT:       pinMode(pin, OUTPUT);       break;
            default:               pinMode(pin, INPUT);        break;
        }
    }
    bool read(int pin) override { return digitalRead(pin) != 0; }
    void write(int pin, bool value) override { digitalWrite(pin, value ? HIGH : LOW); }
};

class Esp32I2s : public II2s {
  public:
    bool init(int sample_rate, int bclk, int lrck, int dout) override {
        i2s_config_t cfg = {};
        cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
        cfg.sample_rate = sample_rate;
        cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
        cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        cfg.dma_buf_count = I2S_DMA_BUF_COUNT;
        cfg.dma_buf_len = I2S_DMA_BUF_LEN;
        cfg.use_apll = false;
        cfg.tx_desc_auto_clear = true;
        cfg.fixed_mclk = 0;

        if (i2s_driver_install(I2S_NUM, &cfg, 0, nullptr) != ESP_OK) return false;

        i2s_pin_config_t pins = {};
        pins.bck_io_num   = bclk;
        pins.ws_io_num    = lrck;
        pins.data_out_num = dout;
        pins.data_in_num  = I2S_PIN_NO_CHANGE;
        if (i2s_set_pin(I2S_NUM, &pins) != ESP_OK) return false;

        i2s_zero_dma_buffer(I2S_NUM);
        return true;
    }

    size_t write_nonblocking(const int16_t* buf, size_t samples) override {
        // PCM5102 wants 32-bit slots with 16-bit data left-shifted into the
        // upper bits. Same sample to both channels (mono).
        size_t written = 0;
        for (size_t i = 0; i < samples; ++i) {
            int32_t s = ((int32_t)buf[i]) << 16;
            int32_t stereo[2] = { s, s };
            size_t bytes_written = 0;
            esp_err_t err = i2s_write(I2S_NUM, stereo, sizeof(stereo),
                                      &bytes_written, /*ticks_to_wait=*/0);
            if (err != ESP_OK || bytes_written < sizeof(stereo)) break;
            ++written;
        }
        return written;
    }
};

class Esp32Serial : public ISerial {
  public:
    void init(int baud) override { Serial.begin(baud); }
    void write(const char* s) override { Serial.print(s); }
};

class Esp32Clock : public IClock {
  public:
    uint64_t micros() override { return (uint64_t)::micros(); }
    uint32_t millis() override { return ::millis(); }
    void delay_ms(uint32_t ms) override { ::delay(ms); }
};

Esp32Gpio   g_gpio;
Esp32I2s    g_i2s;
Esp32Serial g_serial;
Esp32Clock  g_clock;
Hal         g_hal{ &g_gpio, &g_i2s, &g_serial, &g_clock };

} // namespace

Hal* HalFactory::create() { return &g_hal; }

}} // namespace dummer::hal

#endif // BUILD_ESP32
