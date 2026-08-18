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

// The creation-request tracker handed to spawnActor. Not the finished actor,
// and not the pooled unit either - it stays small.
//
// Read at 0x0378a70c (V208 Wii U), which takes it as `handle` alongside the
// new proc:
//
//   +0x00  int   request state; the create path only runs when this is 1
//   +0x04  byte  "already resolved" flag
//
// Nothing has been observed reading past +0x08, and 0x0378a70c is the only
// caller of BaseProcHandle::setProc, so 16 bytes is generous rather than tight.
//
// Word 0 was previously described here as a pool-slot pointer. It is not: it is
// the request-state int above, which is why the "set slot+4" write that used to
// follow the forced createBaseProc call below never did anything.
//
// The handle never receives the created actor. That lands on the pooled
// BaseProcUnit instead - 0x25C bytes from the 256-entry pool at 0x105597c0
// (see the pool set-up at 0x0378d22c) - whose own layout is:
//
//   +0x00  state (0..5)          +0x08  BaseProc*
//   +0x04  link; 0x105597b8 is the "detached" sentinel
//   +0x220 sead::CriticalSection
//
// and a proc points back at its unit through BaseProc+0xE0. Getting a spawned
// actor back means going through one of those, not through this handle - see
// the note on Actor::Spawn.
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

        // A "set slot+4 for pool return on release" write used to sit here,
        // reading handle word 0 as a pool-slot pointer. Word 0 is the request
        // state (see SpawnedHandle), so the value was only ever 0, 1 or 2: the
        // write was dead whenever the state was 0, and a store to address 0x5
        // or 0x6 for any other value. Removed rather than repaired - the unit
        // it was reaching for is pooled and released by the game itself.

        return result;
    }
};

// FUN_024adac8 - a Player-specific per-frame update method; every real spawn
// is issued from inside some actor's own calc/update, never from a generic
// system-wide dispatcher, so a pending Spawn() request is flushed from here
// too, matching that pattern. Also provides a safe per-frame tick callback.
WIIXL_HOOK_DEFINE_TRAMPOLINE(SpawnFlushHook) {
    using RawCallbackFn = void (*)(void* playerPtr);
    static RawCallbackFn& CallbackRef() { static RawCallbackFn fn = nullptr; return fn; }

    static void Callback(void* param1, void* param2) {
        Orig(param1, param2);

        if (param1) {
            PendingSpawn& pending = PendingSpawnRef();
            if (pending.valid) {
                pending.valid = false;
                ExecuteSpawn(pending.name, pending.anchor, pending.pos[0], pending.pos[1], pending.pos[2]);
            }

            if (RawCallbackFn cb = CallbackRef()) {
                cb(param1);
            }
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

    // Installs the spawn/player-tick plumbing hooks. Call once from WiiXLaunch_Init().
    static void Init() {
#if !WIIXL_SWITCH
        impl::SpawnFlushHook::Install(0x0, 0x024adac8);
        impl::RequestCreateBaseProcFixHook::Install(0x0, 0x03948cb8);
#endif
    }

    // Registers a callback fired on the Player's per-frame update loop.
    using PlayerCallbackFn = void (*)(const Actor& player);
    static void OnUpdate(PlayerCallbackFn callback) {
#if !WIIXL_SWITCH
        static PlayerCallbackFn s_Callback = nullptr;
        s_Callback = callback;
        impl::SpawnFlushHook::CallbackRef() = [](void* ptr) {
            if (s_Callback) s_Callback(Actor(ptr));
        };
#else
        (void)callback;
#endif
    }

    // Queues actorName to spawn near anchor at (x, y, z); the actual
    // creation happens on the next safe per-frame tick (see SpawnFlushHook).
    // Returns false immediately on Switch (SupportsSpawn == false).
    //
    // The return value says the request was queued, not that an actor exists,
    // and there is still no way to get the created actor back: the handle only
    // ever carries request state, and the proc is stored on the pooled
    // BaseProcUnit instead (see impl::SpawnedHandle for both layouts).
    //
    // Worth knowing before relying on this: 0x0378a70c only runs the create
    // path when the handle's word 0 is 1, and takes a deleteLater branch
    // otherwise. This handle is left zeroed, so that condition is not currently
    // met. Spawning does produce visible, working actors regardless, so the
    // path actually taken has not been fully pinned down.
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

    // Health / Life accessors (Wii U / Cemu confirmed):
    // On Wii U, Actor+0xe8 is the primary actor vtable pointer.
    // Slot +0xf4 (index 61) is GetMaxLife(actor) -> int
    // Slot +0x2bc (700, index 175) is GetCurrentLifePtr(actor) -> int*
    int GetCurrentLife() const {
#if !WIIXL_SWITCH
        if (!m_Ptr) return 0;
        uint8_t* ptr = static_cast<uint8_t*>(m_Ptr);
        void** vtable = *reinterpret_cast<void***>(ptr + 0xe8);
        if (!vtable) return 0;
        uintptr_t vtAddr = reinterpret_cast<uintptr_t>(vtable);
        if (vtAddr < 0x02000000 || vtAddr > 0x10600000) return 0;

        using GetLifePtrFn = int* (*)(void* actor);
        auto fn = reinterpret_cast<GetLifePtrFn>(vtable[0x2bc / 4]);
        if (!fn) return 0;
        uintptr_t fnAddr = reinterpret_cast<uintptr_t>(fn);
        if (fnAddr < 0x02000000 || fnAddr > 0x04000000) return 0;

        int* pLife = fn(m_Ptr);
        if (!pLife) return 0;
        uintptr_t lifeAddr = reinterpret_cast<uintptr_t>(pLife);
        if (lifeAddr < 0x10000000 || lifeAddr > 0xa0000000 || (lifeAddr & 3) != 0) return 0;

        return *pLife;
#else
        return 0;
#endif
    }

    void SetCurrentLife(int life) {
#if !WIIXL_SWITCH
        if (!m_Ptr) return;
        uint8_t* ptr = static_cast<uint8_t*>(m_Ptr);
        void** vtable = *reinterpret_cast<void***>(ptr + 0xe8);
        if (!vtable) return;
        uintptr_t vtAddr = reinterpret_cast<uintptr_t>(vtable);
        if (vtAddr < 0x02000000 || vtAddr > 0x10600000) return;

        using GetLifePtrFn = int* (*)(void* actor);
        auto fn = reinterpret_cast<GetLifePtrFn>(vtable[0x2bc / 4]);
        if (!fn) return;
        uintptr_t fnAddr = reinterpret_cast<uintptr_t>(fn);
        if (fnAddr < 0x02000000 || fnAddr > 0x04000000) return;

        int* pLife = fn(m_Ptr);
        if (!pLife) return;
        uintptr_t lifeAddr = reinterpret_cast<uintptr_t>(pLife);
        if (lifeAddr < 0x10000000 || lifeAddr > 0xa0000000 || (lifeAddr & 3) != 0) return;

        *pLife = life;
#else
        (void)life;
#endif
    }

    int GetMaxLife() const {
#if !WIIXL_SWITCH
        if (!m_Ptr) return 0;
        uint8_t* ptr = static_cast<uint8_t*>(m_Ptr);
        void** vtable = *reinterpret_cast<void***>(ptr + 0xe8);
        if (!vtable) return 0;
        uintptr_t vtAddr = reinterpret_cast<uintptr_t>(vtable);
        if (vtAddr < 0x02000000 || vtAddr > 0x10600000) return 0;

        using GetMaxLifeFn = int (*)(void* actor);
        auto fn = reinterpret_cast<GetMaxLifeFn>(vtable[0xf4 / 4]);
        if (!fn) return 0;
        uintptr_t fnAddr = reinterpret_cast<uintptr_t>(fn);
        if (fnAddr < 0x02000000 || fnAddr > 0x04000000) return 0;

        return fn(m_Ptr);
#else
        return 0;
#endif
    }

    void SetMaxLife(int maxLife) {
#if !WIIXL_SWITCH
        if (!m_Ptr) return;
        uint8_t* ptr = static_cast<uint8_t*>(m_Ptr);
        void** vtable = *reinterpret_cast<void***>(ptr + 0xe8);
        if (!vtable) return;
        uintptr_t vtAddr = reinterpret_cast<uintptr_t>(vtable);
        if (vtAddr < 0x02000000 || vtAddr > 0x10600000) return;

        using GetLifePtrFn = int* (*)(void* actor);
        auto fn = reinterpret_cast<GetLifePtrFn>(vtable[0x2bc / 4]);
        if (!fn) return;
        uintptr_t fnAddr = reinterpret_cast<uintptr_t>(fn);
        if (fnAddr < 0x02000000 || fnAddr > 0x04000000) return;

        int* pLife = fn(m_Ptr);
        if (!pLife) return;
        uintptr_t lifeAddr = reinterpret_cast<uintptr_t>(pLife);
        if (lifeAddr < 0x10000000 || lifeAddr > 0xa0000000 || (lifeAddr & 3) != 0) return;

        *(pLife + 1) = maxLife;
#else
        (void)maxLife;
#endif
    }

    // Convenience helpers converting between integer Life units (4 per heart) and floating-point hearts
    float GetCurrentHearts() const { return GetCurrentLife() / 4.0f; }
    void SetCurrentHearts(float hearts) { SetCurrentLife(static_cast<int>(hearts * 4.0f + 0.5f)); }

    float GetMaxHearts() const { return GetMaxLife() / 4.0f; }
    void SetMaxHearts(float hearts) { SetMaxLife(static_cast<int>(hearts * 4.0f + 0.5f)); }

private:
    void* m_Ptr;
};

} // namespace WiiXLaunch::BotW
