#pragma once

// WiiXLaunch::BotW::OSLog - a BotW/Cafe-SDK-specific logging channel,
// deliberately separate from the base framework's WIIXL_LOG (debug_log.hpp):
// WIIXL_LOG has no dependency on this vendor module and always works (ring
// buffer on Cemu, SvcLogger on Switch, a notification toast on Wii U).
// OSLog is the opt-in extra on top - it goes through the same
// import-shim-table mechanism as real GX2 calls (see cemu_logging.hpp) to
// reach coreinit's real OSReport, so a call here can show up directly in
// Cemu's own log window instead of requiring tools/ring_log_reader.
//
// Root cause of the long-standing "reaches OSReport but never shows up"
// mystery (found via Cemu-src, Cafe/OS/libs/coreinit/coreinit_Misc.cpp,
// WriteCafeConsole): Cemu's own OSReport implementation buffers characters
// into a 270-byte line buffer and only flushes to the log (log.txt/the log
// window) when it sees a '\n', or when that buffer fills up. Every call
// site in this project was missing a trailing newline, so messages just
// accumulated silently and were never flushed - not a crash, not a
// calling-convention bug. Fixed below by always appending "\n".
//
// (OSReportDirect, an earlier experiment that branched straight to the
// exact thunk address BotW's own compiled code uses for OSReport
// - 0x04005160 - turned out to be a red herring: Ghidra's own symbol table
// has the real OSReport at 0xc0005160; 0x04005160 is a local RPL-import
// trampoline inside BotW's own binary that depends on caller-side context
// - a device/vtable object pointer in r4 - that only BotW's own
// compiler-generated import call sites set up. Nothing to do with the real
// import.coreinit.OSReport path this file actually uses.)

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

    // Same libc-free formatter WIIXL_LOG uses (see debug_log.hpp) - the
    // Cemu codecave has no crt0, so .bss is never zeroed and newlib's
    // vsnprintf can't safely run here. "%s" + text, not `text` itself, so
    // an unrecognized format specifier this formatter left literally in
    // `text` can't be misread as a NEW format string by OSReport's own
    // printf-style parsing.
    char text[WiiXLaunch::Debug::kMaxLogTextLen];
    constexpr uint32_t cap = sizeof(text) - 1;

    va_list args;
    va_start(args, fmt);
    uint32_t len = WiiXLaunch::Debug::FormatText(text, cap, fmt, args);
    va_end(args);
    text[len] = '\0';
    if (len == 0) return;

    // "%s\n", not "%s" - see the WriteCafeConsole note above. Callers of
    // OSLog should NOT include their own trailing '\n'.
    osReport("%s\n", text);
#endif
}

}
