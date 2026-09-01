#pragma once

// BotW/Cafe-SDK logging: OSLog reaches Cemu's log window (adds "\n" for flush).

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <cstdarg>

#if WIIXL_CEMU
#include "cemu_logging.hpp"
#endif

namespace WiiXLaunch::BotW {

inline void OSLog(const char* fmt, ...) {
#if WIIXL_CEMU
    using OSReportFn = void (*)(const char*, ...);
    auto osReport = WiiXLaunch::Backend::ResolveCemuLogging<OSReportFn>(WiiXLaunch::Backend::CemuLogImport::OSReport);
    if (!osReport) return;

    // Libc-free formatter (no crt0 in Cemu codecave).
    char text[WiiXLaunch::Debug::kMaxLogTextLen];
    constexpr uint32_t cap = sizeof(text) - 1;

    va_list args;
    va_start(args, fmt);
    uint32_t len = WiiXLaunch::Debug::FormatText(text, cap, fmt, args);
    va_end(args);
    text[len] = '\0';
    if (len == 0) return;

    // Add "\n" for console flush; caller should not include trailing '\n'.
    osReport("%s\n", text);
#endif
}

}
