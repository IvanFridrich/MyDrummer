// LED manager — flash (timed pulse) + steady on/off + heartbeat.
// Pins set to -1 are treated as "unused" and silently no-op.
//
// Hardware access flows through hal::IGpio + hal::IClock so this is
// host-testable as well.
#pragma once

#include "defines.hpp"
#include "hal/hal.hpp"

#include <stdint.h>

namespace dummer { namespace control {

enum class LedId : uint8_t {
    Kick = 0,
    Snare,
    Hihat,
    Reverb,
    LoopDrum,
    Heartbeat,
    Count
};

class LedManager {
  public:
    LedManager(::dummer::hal::IGpio* gpio, ::dummer::hal::IClock* clock);

    void begin();

    // Trigger an 80 ms flash on the named LED.
    void flash(LedId id);

    // Steady-on/off for LEDs that are state indicators (e.g. reverb on).
    void set_steady(LedId id, bool on);

    // Tick to advance flash timers / heartbeat. Call every super-loop iteration.
    void tick();

#ifdef BUILD_NATIVE_TEST
    // Test hook — defines.hpp leaves most LED pins at -1 (no-op). Tests need
    // real pin numbers to exercise the flash / steady paths.
    void set_pin_for_test(LedId id, int pin) {
        const auto i = static_cast<uint8_t>(id);
        if (i < static_cast<uint8_t>(LedId::Count)) slots_[i].pin = pin;
    }
#endif

  private:
    struct Slot {
        int      pin;
        bool     steady_on;      // when not flashing, this is the level we hold
        bool     flashing;
        uint32_t flash_until_ms;
    };

    ::dummer::hal::IGpio*  gpio_;
    ::dummer::hal::IClock* clock_;
    static constexpr size_t kSlotCount = static_cast<size_t>(LedId::Count);
    Slot                   slots_[kSlotCount];
    uint32_t               heartbeat_next_toggle_ms_;
    bool                   heartbeat_level_;

    void apply(uint8_t i, bool on);
};

}} // namespace dummer::control
