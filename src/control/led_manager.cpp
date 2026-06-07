#include "led_manager.hpp"

namespace dummer
{
namespace control
{

namespace
{
constexpr int kPins[static_cast<size_t>(LedId::Count)] = {
    LED_KICK_PIN,   LED_SNARE_PIN,     LED_HIHAT_PIN,
    LED_REVERB_PIN, LED_LOOP_DRUM_PIN, LED_HEARTBEAT_PIN,
};
} // namespace

LedManager::LedManager(::dummer::hal::IGpio* gpio, ::dummer::hal::IClock* clock)
    : gpio_(gpio), clock_(clock), heartbeat_next_toggle_ms_(0), heartbeat_level_(false)
{
    for (size_t i = 0; i < static_cast<size_t>(LedId::Count); ++i)
    {
        slots_[i].pin            = kPins[i];
        slots_[i].steady_on      = false;
        slots_[i].flashing       = false;
        slots_[i].flash_until_ms = 0;
    }
}

void LedManager::apply(uint8_t i, bool on)
{
    if (slots_[i].pin < 0)
        return;
    gpio_->write(slots_[i].pin, on);
}

void LedManager::begin()
{
    using namespace ::dummer::hal;
    for (size_t i = 0; i < static_cast<size_t>(LedId::Count); ++i)
    {
        if (slots_[i].pin < 0)
            continue;
        gpio_->pin_mode(slots_[i].pin, PIN_OUTPUT);
        gpio_->write(slots_[i].pin, false);
    }
}

void LedManager::flash(LedId id)
{
    const auto i = static_cast<uint8_t>(id);
    if (i >= static_cast<uint8_t>(LedId::Count))
        return;
    if (slots_[i].pin < 0)
        return;
    slots_[i].flashing       = true;
    slots_[i].flash_until_ms = clock_->millis() + static_cast<uint32_t>(LED_FLASH_MS);
    apply(i, true);
}

void LedManager::set_steady(LedId id, bool on)
{
    const auto i = static_cast<uint8_t>(id);
    if (i >= static_cast<uint8_t>(LedId::Count))
        return;
    slots_[i].steady_on = on;
    if (!slots_[i].flashing)
        apply(i, on);
}

void LedManager::tick()
{
    const uint32_t     now                  = clock_->millis();
    constexpr uint32_t kHeartbeatHalfPeriod = static_cast<uint32_t>(LED_HEARTBEAT_PERIOD_MS / 2);

    // Expire one-shot flashes. Signed-cast handles wrap-around correctly.
    for (size_t i = 0; i < static_cast<size_t>(LedId::Count); ++i)
    {
        Slot& s = slots_[i];
        if (s.flashing && static_cast<int32_t>(now - s.flash_until_ms) >= 0)
        {
            s.flashing = false;
            apply(static_cast<uint8_t>(i), s.steady_on);
        }
    }

    // Heartbeat: toggle the dedicated heartbeat LED on a fixed period.
    const auto hb = static_cast<uint8_t>(LedId::Heartbeat);
    if (slots_[hb].pin >= 0 && !slots_[hb].flashing)
    {
        if (heartbeat_next_toggle_ms_ == 0)
        {
            heartbeat_next_toggle_ms_ = now + kHeartbeatHalfPeriod;
        }
        else if (static_cast<int32_t>(now - heartbeat_next_toggle_ms_) >= 0)
        {
            heartbeat_level_ = !heartbeat_level_;
            apply(hb, heartbeat_level_);
            heartbeat_next_toggle_ms_ = now + kHeartbeatHalfPeriod;
        }
    }
}

} // namespace control
} // namespace dummer
