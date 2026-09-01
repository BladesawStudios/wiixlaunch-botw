#pragma once

// Resolving coreinit's OSReport from the Cemu raw code-cave build, via the
// same "import.<lib>.<Name> shim table" mechanism as gx2_imports.hpp - see
// that file for the full explanation of why this indirection is needed at
// all (the payload is a raw codecave blob, never a real RPL module, so
// there's no OS import resolution for anything it calls). Split into its
// own header/table/asm file since it's a coreinit concern, not a gx2 one,
// even though the underlying mechanism (a Cemu-resolved `import.<lib>.<Name>`
// tail-call shim) is identical.
//
// Reached via this table: WiiXLaunch::BotW::OSLog (vendor/wiixlaunch-botw/
// include/wiixlaunch/botw/log.hpp), which calls import.coreinit.OSReport
// through here with the PowerPC EABI varargs CR-bit-6 fix applied (see
// cemu_logging.asm) and a trailing '\n' (required - Cemu's own OSReport
// implementation line-buffers and only flushes to the log on '\n', which
// was the real reason earlier calls here never showed up anywhere despite
// not crashing and being genuinely reached).

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU

#include <cstdint>

extern "C" {
    // Patched by scripts/deploy.py at deploy time - left at 0 in the actual
    // compiled ELF, never meant to be read before that patch has happened.
    __attribute__((section(".data"))) inline uint32_t g_CemuLoggingShimTableOffset = 0;
}

namespace WiiXLaunch::Backend {

// Keep this enum's order EXACTLY in sync with wiixlaunch_cemu_logging_shim_table
// in src/cemu/cemu_logging.asm - each entry here is that table's Nth slot.
enum class CemuLogImport : uint32_t {
    OSReport = 0,
    MEMAllocFromDefaultHeapEx,
    MEMFreeToDefaultHeap,
    Count
};

extern "C" uintptr_t g_CodeCaveBase;

inline uintptr_t* CemuLoggingShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuLoggingShimTableOffset);
}

// Usage: auto osReport = WiiXLaunch::Backend::ResolveCemuLogging<void(*)(const char*, ...)>(CemuLogImport::OSReport);
template <typename FnPtr>
inline FnPtr ResolveCemuLogging(CemuLogImport fn) {
    return reinterpret_cast<FnPtr>(CemuLoggingShimTable()[static_cast<uint32_t>(fn)]);
}

}

#endif
