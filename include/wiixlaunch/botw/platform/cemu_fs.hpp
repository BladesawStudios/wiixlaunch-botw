#pragma once

// Resolving coreinit's Filesystem APIs from the Cemu raw code-cave build,
// via the same import shim table mechanism as cemu_logging.asm / gx2_imports.asm.

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU

#include <cstdint>

extern "C" {
    // Patched by scripts/deploy.py at deploy time.
    __attribute__((section(".data"))) inline uint32_t g_CemuFsShimTableOffset = 0;
}

namespace WiiXLaunch::Backend {

// Keep this enum's order EXACTLY in sync with wiixlaunch_cemu_fs_shim_table
// in src/cemu/cemu_fs.asm.
enum class CemuFsImport : uint32_t {
    FSAddClient = 0,
    FSDelClient,
    FSInitCmdBlock,
    FSOpenFile,
    FSGetStatFile,
    FSReadFile,
    FSWriteFile,
    FSCloseFile,
    Count
};

extern "C" uintptr_t g_CodeCaveBase;

inline uintptr_t* CemuFsShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuFsShimTableOffset);
}

template <typename FnPtr>
inline FnPtr ResolveCemuFs(CemuFsImport fn) {
    return reinterpret_cast<FnPtr>(CemuFsShimTable()[static_cast<uint32_t>(fn)]);
}

} // namespace WiiXLaunch::Backend

#endif
