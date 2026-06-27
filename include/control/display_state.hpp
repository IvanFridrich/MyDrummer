#pragma once
#include <stdint.h>

namespace dummer
{
namespace control
{

// Plain POD read by the display task (core 0) and written by the audio task
// (core 1).  Declared volatile at the point of definition in main.cpp.
// Fields are independent aligned primitives — cross-core reads are safe for
// display purposes; a one-frame stale read is harmless.
//
// pos_samples: recording position in Recording state; play position in Playing.
struct DisplayState
{
    uint8_t  style;               // AutoStyle enum value (0=Off…7=HardRock)
    bool     auto_active;
    uint8_t  speed;               // 0=slow 1=normal 2=fast
    uint8_t  bpm;                 // 0 when auto_active is false
    uint8_t  looper_state;        // LooperState enum value (0=Idle…3=Playing)
    uint32_t loop_length_samples; // total loop duration; valid when Playing
    uint32_t pos_samples;         // record or play position
    uint16_t voice_count;
    bool     reverb_on;
    uint32_t cpu_max_us;
    uint32_t cpu_avg_us;
};

} // namespace control
} // namespace dummer
