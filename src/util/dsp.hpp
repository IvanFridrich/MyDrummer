#pragma once

#include <stdint.h>

namespace dummer
{
namespace util
{

inline int16_t sat_i16(int32_t v)
{
    if (v > INT16_MAX)
        v = INT16_MAX;
    if (v < INT16_MIN)
        v = INT16_MIN;
    return static_cast<int16_t>(v);
}

inline int32_t q15_mul(int32_t a, int32_t b)
{
    return (a * b) >> 15;
}

} // namespace util
} // namespace dummer
