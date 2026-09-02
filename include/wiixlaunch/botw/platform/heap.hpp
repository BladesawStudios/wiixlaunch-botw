#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstddef>
#include <cstdint>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

#include "log.hpp"

// WiiXLaunch::BotW::Heap - allocating out of the GAME's memory instead of the
// payload's own.
//
// The Cemu payload's built-in heap is the tail of the code cave it was loaded
// into: it starts after the payload and ends at 0x02000000, where the game's
// code begins (see wiixl_cemu_backend.hpp). That is a few megabytes, shared
// with any other WiiXLaunch payload, and nothing frees. A mod that wants more
// than that - several font faces, large render targets, a mesh buffer - has to
// get it from somewhere else, and the game is already holding a MEM1 heap.
//
// BotW has a wrapper for exactly that at 0x0309BB68. Disassembled (v208):
//
//     mfspr r0,LR ; stwu r1,-0x10(r1) ; ... r31 = r3, r30 = r4
//     li    r3,0x0
//     bl    0x04004fb8        ; base heap handle 0 = MEM1
//     or    r4,r31 ; or r5,r30
//     bl    0x04004f70        ; allocate(handle, size, align)
//     ... blr                 ; r3 falls through as the return
//
// so it is `void* (uint32_t size, uint32_t align)`. This is not a new risk in
// kind: the Wii U build of GX2::AllocMEM1 has always called this same address
// directly, and Cemu runs the same RPX at the same addresses, which is why the
// GX2 hook at 0x03a75d48 works.
//
// It is opt-in because it cannot be right for everyone: the address is v208's,
// the game's heaps do not exist during early boot, and a mod that stays inside
// the code cave should not pay for this. See UseGameHeap() for when to call it.

namespace WiiXLaunch::BotW::Heap {

#if WIIXL_CEMU || WIIXL_WIIU

// v208 Wii U / Cemu. A different game version has something else here.
constexpr uintptr_t kGameAllocMem1Address = 0x0309BB68;

using FnGameAlloc = void* (*)(uint32_t size, uint32_t align);

// One allocation from the game's MEM1 heap. Never freed - like everything else
// in this module, and unlike the game's own use of that heap, so a mod that
// calls this in a loop will exhaust the game's memory rather than its own.
inline void* GameAllocMem1(size_t size, size_t align) {
    auto fn = reinterpret_cast<FnGameAlloc>(kGameAllocMem1Address);
    return fn(static_cast<uint32_t>(size), static_cast<uint32_t>(align ? align : 256));
}

#endif

#if WIIXL_CEMU

// MEASURED, v208, from the title screen onwards: this DOES NOT WORK, and the
// probe below reports it rather than pretending. BotW carves MEM1 up for its
// own heaps during boot, so the MEM1 BASE heap - the one handle 0 returns - has
// nothing left and the allocation comes back null.
//
// Use WiiXLaunch::Mem::UseCoreinitHeap() instead (wiixlaunch/mem.hpp). It asks
// coreinit for the MEM2 base heap, which on this title reports ~68 MB free, and
// it needs no game-specific address at all. This function is kept because the
// wrapper is correct and a different title or an earlier moment may still have
// MEM1 room, but it is not the one to reach for.
//
// Route every later Backend::AllocCemuHeap - and so every GX2::AllocMEM1 -
// through the game's MEM1 heap instead of the payload's code cave.
//
// WHEN: after the game's heaps exist. From a GX2::OnInitialized callback or
// later is safe; from a module entry point it is not - the payload's entry hook
// runs long before coreinit has set the base heaps up, and the probe below will
// simply fail there.
//
// Allocations already handed out by the code-cave heap stay valid; nothing is
// freed or moved. Backend::CemuHeapUsed() keeps reporting the code cave only.
//
// Probes with a small allocation first and refuses to install if it comes back
// null, so a bad address or an early call fails here rather than at the first
// texture. The probe's 256 bytes are deliberately leaked.
//
// Returns true if the provider is now installed.
inline bool UseGameHeap() {
    void* probe = GameAllocMem1(256, 256);
    if (!probe) {
        OSLog("WiiXLaunch: game MEM1 heap not available (probe failed) - staying on the code cave\n");
        return false;
    }
    WiiXLaunch::Backend::SetHeapProvider(&GameAllocMem1);
    OSLog("WiiXLaunch: allocating from the game's MEM1 heap (probe at %p); code cave had %u of %u bytes used\n",
          probe, static_cast<uint32_t>(WiiXLaunch::Backend::CemuHeapUsed()),
          static_cast<uint32_t>(WiiXLaunch::Backend::CemuHeapLimit()));
    return true;
}

// Back to the payload's own code-cave heap. Anything already allocated from the
// game stays where it is.
inline void UseCodeCaveHeap() {
    WiiXLaunch::Backend::SetHeapProvider(nullptr);
    OSLog("WiiXLaunch: allocating from the payload code cave (%u of %u bytes used)\n",
          static_cast<uint32_t>(WiiXLaunch::Backend::CemuHeapUsed()),
          static_cast<uint32_t>(WiiXLaunch::Backend::CemuHeapLimit()));
}

inline bool UsingGameHeap() {
    return WiiXLaunch::Backend::GetHeapProvider() == &GameAllocMem1;
}

// What is left of the payload's own heap, whether or not the game's is in use.
inline size_t CodeCaveUsed() { return WiiXLaunch::Backend::CemuHeapUsed(); }
inline size_t CodeCaveLimit() { return WiiXLaunch::Backend::CemuHeapLimit(); }

#else

// Wii U builds already call the game's allocator directly in GX2::AllocMEM1,
// so there is nothing to swap; Switch has neither.
inline bool UseGameHeap() { return false; }
inline void UseCodeCaveHeap() {}
inline bool UsingGameHeap() { return false; }
inline size_t CodeCaveUsed() { return 0; }
inline size_t CodeCaveLimit() { return 0; }

#endif

} // namespace WiiXLaunch::BotW::Heap
