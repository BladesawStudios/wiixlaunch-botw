#pragma once

// Resolved via the consuming project's own include path (-I include), not
// relative paths - this module lives in its own repo (see README.md) and
// only needs to sit alongside a normal WiiXLaunch project's include/, not be
// physically nested inside its include/wiixlaunch/ tree.
#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/call.hpp>

// No WIIXL_LOG used; consuming project knows its logging signature.

// WiiXLaunch::BotW::Actor - a thin wrapper around a raw ksys::act::Actor*,
// plus the actor-name and actor-spawn RE work ported from the "Actor
// Spawning and Weapon Detection" mod's main.cpp / handwritten-symbols-botw.csv.
// Actor spawning is only confirmed on Wii U/Cemu (see SupportsSpawn) - the
// same InstParamPack/spawnActor/requestCreateBaseProc sequence the original
// mod hand-rolled once per attack is here generalized to take any actor
// name/anchor/position instead.

namespace WiiXLaunch::BotW {

class Actor;

namespace impl {

#if !WIIXL_SWITCH

struct PendingSpawn {
    bool valid = false;
    const char* name = nullptr;
    void* anchor = nullptr;
    float pos[3] = {};
};

inline PendingSpawn& PendingSpawnRef() {
    static PendingSpawn s;
    return s;
}

// Full InstParamPack: 4-byte header (overwritten by setAnchor with the
// anchor actor) + 196-byte Buffer (2-byte count + 2-byte usedLen +
// 192-byte body) that Buffer::add (FUN_031f9870) writes named params into.
struct InstParamPack {
    void* anchor;
    uint16_t count;
    uint16_t usedLen;
    uint8_t body[192];
};
static_assert(sizeof(InstParamPack) == 200, "must match the real 0xC8-byte InstParamPack layout");

// The 2-word {rawPtr, vtable} "SafeString" shape used all over this binary
// for named-param keys, padded well past the 2 fields the writer itself
// reads directly - a virtual call through the object can write back into it
// while resolving/caching the value.
struct SafeStringRef {
    void* rawPtr;
    void* vtable;
    uint8_t padding[24];
};

using SpawnActorFn = int (*)(void* mgr, const char* name, void* heap, void* handleOut, void* paramPack, int unused, int priority);
using SetParamPackAnchorFn = void (*)(void* paramPack, void* anchorActor);
using CreateBaseProcFn = int (*)(void* initializer, void* request);
using WriteParamFn = void (*)(void* pack, void* value, void* keyRef, int sizeBytes, uint8_t typeTag);

constexpr uintptr_t kPositionKeyWord0 = 0x10072ed4;  // DAT_10072ed4, from doSpawn_Conf98's direct FUN_031f9870 call
constexpr uintptr_t kSafeStringVtable = 0x10263910;  // DAT_10263910, the shared resolver vtable reused everywhere

constexpr uintptr_t kActorCreateMgrAddr = 0x1047c2b8;   // global: manager pointer, passed straight through
constexpr uintptr_t kHeapProviderAddr = 0x10463f6c;     // global: pointer to a struct; +0x10 field is the spawn heap
constexpr uint32_t kHeapFieldOffset = 0x10;

// *SpawnedHandle() is a small creation-request/job tracker, not the finished
// actor - handle[0] is the pool-slot pointer allocUnit() claims, handle[4] is
// a "fast path already resolved" byte. Persistent (function-local static,
// not per-call) since the pool-ownership fix below needs it to survive
// across the RequestCreateBaseProcFixHook call that follows each spawn.
inline uint8_t* SpawnedHandle() {
    alignas(8) static uint8_t handle[16] = {};
    return handle;
}

// Gates RequestCreateBaseProcFixHook's forced createBaseProc call so it only
// fires for spawns WE trigger, never for the game's own real spawns.
inline bool& InOurSpawnCall() {
    static bool v = false;
    return v;
}

// Guards RequestCreateBaseProcFixHook's forced createBaseProc call - that
// hook fires 3x per single spawnActor call for reasons not yet understood,
// and without this it would construct 3 actors per spawn instead of 1.
inline bool& ForcedCreateBaseProcThisSpawn() {
    static bool v = false;
    return v;
}

inline void ExecuteSpawn(const char* actorName, void* anchor, float x, float y, float z) {
    ForcedCreateBaseProcThisSpawn() = false;

    void* mgr = *reinterpret_cast<void**>(kActorCreateMgrAddr);
    if (!mgr) return;

    void* heapOwner = *reinterpret_cast<void**>(kHeapProviderAddr);
    if (!heapOwner) return;
    void* heap = *reinterpret_cast<void**>(static_cast<uint8_t*>(heapOwner) + kHeapFieldOffset);

    alignas(8) static InstParamPack pack = {};
    auto setAnchor = WiiXLaunch::GetTargetFunction<SetParamPackAnchorFn>(0x0, 0x037b55fc);
    setAnchor(&pack, anchor);

    // "@P" position, or the actor spawns at the pack's zeroed-out default
    // instead of near the anchor.
    auto writeParam = WiiXLaunch::GetTargetFunction<WriteParamFn>(0x0, 0x031f9870);
    alignas(8) static SafeStringRef positionKey = { reinterpret_cast<void*>(kPositionKeyWord0), reinterpret_cast<void*>(kSafeStringVtable) };
    float position[3] = { x, y, z };
    writeParam(&pack.count, position, &positionKey, 0xc, 4);

    auto spawnActor = WiiXLaunch::GetTargetFunction<SpawnActorFn>(0x0, 0x037b5e8c);
    InOurSpawnCall() = true;
    spawnActor(mgr, actorName, heap, SpawnedHandle(), &pack, 0, 1);
    InOurSpawnCall() = false;
}

// ksys::act::BaseProcInitializer::requestCreateBaseProc_Conf67 (0x03948cb8)
// always reaches state=2 by submitting our task to a queue that never
// actually drains it for spawns we trigger (real spawns work fine through
// the identical code path - the bug is specific to calling it outside the
// game's own spawn flow). Fix: call createBaseProc ourselves, synchronously,
// with the same real request object - only for our own spawn calls, never
// touching real game-triggered spawns.
WIIXL_HOOK_DEFINE_TRAMPOLINE(RequestCreateBaseProcFixHook) {
    static int Callback(void* initializer, void* request) {
        int result = Orig(initializer, request);

        if (!InOurSpawnCall() || ForcedCreateBaseProcThisSpawn()) return result;
        ForcedCreateBaseProcThisSpawn() = true;

        auto createBaseProc = WiiXLaunch::GetTargetFunction<CreateBaseProcFn>(0x0, 0x03948ed8);
        createBaseProc(initializer, request);

        // Set slot+4 for pool return on release.
        void* slot = *reinterpret_cast<void**>(SpawnedHandle());
        if (slot) {
            *reinterpret_cast<void**>(static_cast<uint8_t*>(slot) + 4) = SpawnedHandle();
        }

        return result;
    }
};

// FUN_024adac8 - a Player-specific per-frame update method; every real spawn
// is issued from inside some actor's own calc/update, never from a generic
// system-wide dispatcher, so a pending Spawn() request is flushed from here
// too, matching that pattern. Pure pass-through (Orig called first,
// unconditionally) before the flush runs.
WIIXL_HOOK_DEFINE_TRAMPOLINE(SpawnFlushHook) {
    static void Callback(void* param1, void* param2) {
        Orig(param1, param2);

        PendingSpawn& pending = PendingSpawnRef();
        if (pending.valid) {
            pending.valid = false;
            ExecuteSpawn(pending.name, pending.anchor, pending.pos[0], pending.pos[1], pending.pos[2]);
        }
    }
};

#endif // !WIIXL_SWITCH

} // namespace impl

class Actor {
public:
    Actor() : m_Ptr(nullptr) {}
    explicit Actor(void* ptr) : m_Ptr(ptr) {}

    bool IsValid() const { return m_Ptr != nullptr; }
    void* GetRaw() const { return m_Ptr; }

    // ksys::act::Actor::getUniqueName() const on Switch (0x11c9bfc) - the
    // placement's "UniqueName" tag, or the actor's ActorLink resource name
    // (e.g. "Weapon_Sword_070") when there's no placement, exactly what a
    // dynamically-spawned/held actor has. Wii U/Cemu has no Ghidra-found
    // equivalent function; the same information sits as a plain inline
    // null-terminated buffer at +0x10 on the actor object instead, confirmed
    // via live Cheat Engine string scan (see handwritten-symbols-botw.csv).
    const char* GetName() const {
        if (!m_Ptr) return "(none)";
#if WIIXL_SWITCH
        using GetActorUniqueNameFn = const char* (*)(void* actor);
        auto getUniqueName = WiiXLaunch::GetTargetFunction<GetActorUniqueNameFn>(0x11c9bfc, 0x0);
        const char* name = getUniqueName(m_Ptr);
        return name ? name : "(unnamed)";
#else
        static char buf[64];
        constexpr uintptr_t kActorNameOffset = 0x10;
        auto* p = reinterpret_cast<const char*>(m_Ptr) + kActorNameOffset;
        int n = 0;
        for (; n < static_cast<int>(sizeof(buf)) - 1; n++) {
            if (p[n] == '\0') break;
            buf[n] = p[n];
        }
        buf[n] = '\0';
        return n > 0 ? buf : "(unnamed)";
#endif
    }

    // Actor creation ("spawn an actor near this one") is only confirmed on
    // Wii U/Cemu - no Switch equivalent was ever RE'd (see
    // handwritten-symbols-botw.csv). Spawn() is a silent, safe no-op on
    // Switch (returns false) rather than reading/writing an unconfirmed
    // offset - check SupportsSpawn if you need to branch mod behavior on it.
    static constexpr bool SupportsSpawn = !WIIXL_SWITCH;

    // Installs the spawn-plumbing hooks. Call once from WiiXLaunch_Init().
    static void Init() {
#if !WIIXL_SWITCH
        impl::SpawnFlushHook::Install(0x0, 0x024adac8);
        impl::RequestCreateBaseProcFixHook::Install(0x0, 0x03948cb8);
#endif
    }

    // Queues actorName to spawn near anchor at (x, y, z); the actual
    // creation happens on the next safe per-frame tick (see SpawnFlushHook).
    // Returns false immediately on Switch (SupportsSpawn == false).
    static bool Spawn(const char* actorName, const Actor& anchor, float x, float y, float z) {
#if WIIXL_SWITCH
        (void)actorName; (void)anchor; (void)x; (void)y; (void)z;
        return false;
#else
        impl::PendingSpawn& pending = impl::PendingSpawnRef();
        pending.valid = true;
        pending.name = actorName;
        pending.anchor = anchor.GetRaw();
        pending.pos[0] = x;
        pending.pos[1] = y;
        pending.pos[2] = z;
        return true;
#endif
    }

private:
    void* m_Ptr;
};

} // namespace WiiXLaunch::BotW
