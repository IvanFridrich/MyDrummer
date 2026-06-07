// LOG_E / LOG_W / LOG_I — leveled logging through the HAL serial interface.
//
// Compiled out entirely in release. LOG_LEVEL is set per build env:
//   release      -> LOG_LEVEL=0 (everything stripped, no-op statements)
//   debug        -> LOG_LEVEL=3
//   native_test  -> LOG_LEVEL=3
//
// Levels: 1=E, 2=W, 3=I. A macro is active iff LOG_LEVEL >= its level.
#pragma once

#include "hal/hal.hpp"

#include <stdarg.h>
#include <stdio.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

namespace dummer
{
namespace log
{

// Single sink — set once by app at startup; null sink is a no-op.
inline ::dummer::hal::ISerial*& sink()
{
    static ::dummer::hal::ISerial* s = nullptr;
    return s;
}

inline void set_sink(::dummer::hal::ISerial* s)
{
    sink() = s;
}

inline void emit(char level, const char* tag, const char* fmt, ...)
{
    if (!sink())
        return;
    char buf[160];
    int  n = snprintf(buf, sizeof(buf), "[%c][%s] ", level, tag);
    if (n < 0)
        return;
    if (static_cast<size_t>(n) >= sizeof(buf))
        n = sizeof(buf) - 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), fmt, ap);
    va_end(ap);
    // Force a trailing newline; reserve last 2 slots.
    size_t len = 0;
    while (len < sizeof(buf) && buf[len])
        ++len;
    if (len + 1 >= sizeof(buf))
        len = sizeof(buf) - 2;
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    sink()->write(buf);
}

} // namespace log
} // namespace dummer

#if LOG_LEVEL >= 1
#define LOG_E(tag, fmt, ...) ::dummer::log::emit('E', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_E(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= 2
#define LOG_W(tag, fmt, ...) ::dummer::log::emit('W', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_W(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= 3
#define LOG_I(tag, fmt, ...) ::dummer::log::emit('I', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_I(tag, fmt, ...) ((void)0)
#endif
