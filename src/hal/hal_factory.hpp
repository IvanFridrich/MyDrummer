#pragma once
#include "hal/hal.hpp"

namespace dummer { namespace hal {

struct HalFactory {
    // Returns a pointer to a process-wide HAL instance. The actual impl
    // (esp32 vs native) is chosen at compile time via BUILD_* macros.
    static Hal* create();
};

}} // namespace dummer::hal
