#pragma once

// Resolving real GX2 functions from the Cemu raw code-cave build
// specifically - WIIXL_WIIU (real Aroma/wups plugin) needs none of this, it
// links against wut's gx2.rpl import stubs normally.
//
// The problem this solves: our Cemu payload is a raw, self-relocating blob
// dropped into a code cave (see wiixl_cemu_backend.hpp/main.cpp's
// WiiXLaunch_Cemu_Init) - it is never loaded as a real RPL module, so there
// is no OS import-resolution for anything it calls. BotW's own GX2 calls
// turned out to go through an internal display-list-recording wrapper, not
// a plain resolved function pointer, so there was nothing to "borrow" the
// way NVN's GOT cells were borrowed on Switch.
//
// The real, working mechanism (confirmed against a real installed Cemu
// graphic pack - BreathOfTheWild/Mods/FPS++/patch_VSync.asm, which calls
// `bl import.gx2.GX2SetSwapInterval`): Cemu's OWN patch/codecave assembler
// understands `import.<lib>.<FunctionName>` labels and resolves them to the
// real function address itself, at patch-load time. We can't get our
// GCC-compiled payload to emit that syntax directly, but we CAN place a
// small, hand-written .asm block (see src/cemu/gx2_imports.asm in this same
// module, which scripts/deploy.py auto-includes into the same codecave as
// the compiled payload) containing one tiny tail-call shim per GX2 function
// we need, plus a table of their resolved addresses. Our C++ code just reads
// real, Cemu-resolved function pointers out of that table.
//
// Locating the table: it's placed somewhere after our compiled payload's
// bytes within the same codecave (see deploy.py's per-file offset-symbol
// patching), so its address is g_CodeCaveBase + g_Gx2ShimTableOffset.
// deploy.py finds this exact global (matched via the
// "WIIXL_OFFSET_SYMBOL: g_Gx2ShimTableOffset" comment in gx2_imports.asm)
// and patches its deploy-time-computed offset into the flat binary before
// embedding it, the same way it already patches ADDR32/ADDR16 relocations
// elsewhere in the same payload.
//
// Logging (OSReport) used to live in this same table - it's been split out
// to cemu_logging.hpp/cemu_logging.asm, its own separate shim table, since
// it's a coreinit concern rather than a gx2 one.

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU

#include <cstdint>

extern "C" {
    // Patched by scripts/deploy.py at deploy time - left at 0 in the actual
    // compiled ELF, never meant to be read before that patch has happened.
    inline uint32_t g_Gx2ShimTableOffset = 0;
}

namespace WiiXLaunch::Backend {

// Keep this enum's order EXACTLY in sync with wiixlaunch_gx2_shim_table in
// src/cemu/gx2_imports.asm - each entry here is that table's Nth slot.
enum class Gx2Import : uint32_t {
    Init = 0,
    SetupContextStateEx,
    SetViewport,
    SetScissor,
    ClearColor,
    SwapScanBuffers,
    SetSwapInterval,
    DrawDone,
    Count
};

extern "C" uintptr_t g_CodeCaveBase;

inline uintptr_t* Gx2ShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_Gx2ShimTableOffset);
}

// Usage: auto gx2Init = WiiXLaunch::Backend::ResolveGx2<void(*)(uint32_t*)>(Gx2Import::Init);
template <typename FnPtr>
inline FnPtr ResolveGx2(Gx2Import fn) {
    return reinterpret_cast<FnPtr>(Gx2ShimTable()[static_cast<uint32_t>(fn)]);
}

}

#endif
