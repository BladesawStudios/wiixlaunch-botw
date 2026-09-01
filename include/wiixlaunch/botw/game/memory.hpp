#pragma once

// Resolved via the consuming project's own include path (-I include), not
// relative paths - this module lives in its own repo (see README.md) and
// only needs to sit alongside a normal WiiXLaunch project's include/, not be
// physically nested inside its include/wiixlaunch/ tree.
#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <cstdint>
#include <cstddef>
#include <cstring>

// WiiXLaunch::BotW::Memory - allocate/free through the game's OWN in-game
// heap (InGameAlloc/InGameFree, V208 Wii U) instead of new/malloc.
//
// Why this matters: every BotW subsystem that owns long-lived engine memory
// (particle resource buffers, emitter sets, anything registered with a
// manager that later frees it itself) expects that memory to come from ITS
// heap, allocated through ITS allocator. Handing it malloc'd/new'd memory
// works right up until the engine's own teardown tries to release it through
// InGameFree/the heap's free-list, which is undefined behaviour on a pointer
// that heap never allocated. Route through here instead so ownership matches
// what the engine will eventually try to release.

namespace WiiXLaunch::BotW {

class Memory {
public:
    // Wii U/Cemu only - no Switch equivalent has been RE'd. Alloc/Free/
    // GetMainGameHeap are safe, silent no-ops on Switch (return nullptr)
    // rather than reading/writing an unconfirmed offset.
    static constexpr bool SupportsAlloc = !WIIXL_SWITCH;

    // The heap InGameAlloc actually uses for gameplay/effect allocations.
    //
    // InGameAlloc(heap=null, ...) does not fail cleanly - it falls back to a
    // per-thread "current heap" TLS slot (FUN_030aa1ac in the V208 decompile)
    // that a mod's own calling thread never populates, so that lookup returns
    // null and the allocation fails every time. 0x10463f6c+0x10 is the exact
    // heap ksys::act::ActorCreator::spawnActor itself is handed (see
    // Actor::impl::kHeapProviderAddr in actor.hpp) and is proven live and
    // allocatable from an arbitrary hooked calling thread - pass it
    // explicitly instead of relying on the TLS fallback.
    static void* GetMainGameHeap() {
#if WIIXL_SWITCH
        return nullptr;
#else
        constexpr uintptr_t kProviderAddr = 0x10463f6c;
        constexpr uint32_t kHeapFieldOffset = 0x10;
        void* heapOwner = *reinterpret_cast<void**>(kProviderAddr);
        if (!IsPlausibleHeapPtr(heapOwner)) return nullptr;
        void* heap = *reinterpret_cast<void**>(static_cast<uint8_t*>(heapOwner) + kHeapFieldOffset);
        return IsPlausibleHeapPtr(heap) ? heap : nullptr;
#endif
    }

    // Allocates `size` bytes from the main game heap, zero-initialised,
    // aligned to `align` (default 256). Returns nullptr on failure - either
    // the heap is out of memory, or GetMainGameHeap() couldn't resolve it
    // (e.g. called before the world/heap exists yet).
    static void* Alloc(size_t size, int32_t align = 256) {
#if WIIXL_SWITCH
        (void)size; (void)align;
        return nullptr;
#else
        if (size == 0) return nullptr;
        int32_t a = (align > 0) ? align : 128;

        using InGameAllocFn = void* (*)(void* heap, uint32_t size, int32_t align);
        auto inGameAlloc = WiiXLaunch::GetTargetFunction<InGameAllocFn>(0x0, kInGameAllocAddr);

        void* heap = GetMainGameHeap();
        void* p = inGameAlloc(heap, static_cast<uint32_t>(size), a);
        if (p) {
            std::memset(p, 0, size);
            WIIXL_LOG("WiiXLaunch::Memory: InGameAlloc(%u bytes, align %d) -> %p",
                      static_cast<unsigned int>(size), a, p);
            return p;
        }

        WIIXL_LOG("WiiXLaunch::Memory: InGameAlloc FAILED for %u bytes (heap=%p)",
                  static_cast<unsigned int>(size), heap);
        return nullptr;
#endif
    }

    // Frees memory obtained from Alloc(). Safe to call with nullptr.
    static void Free(void* ptr) {
#if WIIXL_SWITCH
        (void)ptr;
#else
        if (!ptr) return;
        using InGameFreeFn = void (*)(void* ptr);
        auto inGameFree = WiiXLaunch::GetTargetFunction<InGameFreeFn>(0x0, kInGameFreeAddr);
        inGameFree(ptr);
#endif
    }

private:
#if !WIIXL_SWITCH
    static constexpr uintptr_t kInGameAllocAddr = 0x0308e4c8;
    static constexpr uintptr_t kInGameFreeAddr  = 0x0308e650;

    // Address-band sanity check for a pointer whose layout is inferred
    // rather than declared - MEM2 on Wii U/Cemu.
    static bool IsPlausibleHeapPtr(const void* p) {
        uintptr_t v = reinterpret_cast<uintptr_t>(p);
        return v >= 0x10000000 && v < 0xe0000000;
    }
#endif
};

} // namespace WiiXLaunch::BotW
