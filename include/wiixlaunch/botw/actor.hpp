#pragma once

// Resolved via the consuming project's own include path (-I include), not
// relative paths - this module lives in its own repo (see README.md) and
// only needs to sit alongside a normal WiiXLaunch project's include/, not be
// physically nested inside its include/wiixlaunch/ tree.
#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/call.hpp>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <type_traits>

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
    bool hasScale = false;
    float scale[3] = {1.0f, 1.0f, 1.0f};
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
using InitParamPackBufferFn = void* (*)(void* buffer);

constexpr uintptr_t kPositionKeyWord0 = 0x10072ed4;  // DAT_10072ed4, from doSpawn_Conf98's direct FUN_031f9870 call
constexpr uintptr_t kSafeStringVtable = 0x10263910;  // DAT_10263910, the shared resolver vtable reused everywhere

// "@S", the InstParamPack scale key, at 0x1031b4ec - the same kind of two-byte
// named param as "@P" and written exactly the same way: 12 bytes, type tag 4.
//
// Found from the game's own setter, ksys::act::ActorCreator::addScale at
// 0x037b55a4, which takes ONE float, splats it into a stack vec3 and calls
// 0x031f9870(pack+4, &vec, &key, 0xc, 4). That is the uniform-scale overload;
// the decompilation has a second one taking a sead::Vector3f and writing the
// same key, so a NON-UNIFORM scale is representable and is what SpawnScaled
// writes. The key string sits in a run of them - 0x1031b4ef is "@W",
// 0x1031b514 "@DD", 0x1031b518 "@RL", 0x1031b51c "@TV".
constexpr uintptr_t kScaleKeyWord0 = 0x1031b4ec;

constexpr uintptr_t kActorCreateMgrAddr = 0x1047c2b8;   // global: manager pointer, passed straight through
constexpr uintptr_t kHeapProviderAddr = 0x10463f6c;     // global: pointer to a struct; +0x10 field is the spawn heap
constexpr uint32_t kHeapFieldOffset = 0x10;

// --- ksys::act::Actor transform (Wii U V208) ---
//
// Actor+0x1F8 is the actor's sead::Matrix34f - the object the game's own debug
// name for it calls ACTOR_MATRIX. Row-major 3x4, 12 floats, 0x30 bytes, so it
// spans 0x1F8..0x227:
//
//   m[0][0..3]  0x1F8 0x1FC 0x200 0x204
//   m[1][0..3]  0x208 0x20C 0x210 0x214
//   m[2][0..3]  0x218 0x21C 0x220 0x224
//
// Confirmed two ways. 0x0383b398 copies exactly 12 floats out of
// playerActor+0x1F8 via the matrix-assign helper at 0x03c6fe5c, which fixes
// both the base and the size. And the game reads gameplay position out of the
// translation column - m[0][3]/m[1][3]/m[2][3], i.e. +0x204/+0x214/+0x224 -
// in 0x022b1278 (item-drop spawn), 0x022ae534 and 0x025bba8c, which is where
// the otherwise baffling 0x10 stride between "x", "y" and "z" comes from.
//
// The 0x10 stride is also why the old rotation offsets were wrong. 0x1E4 and
// 0x1F4 sit *before* this matrix and 0x234 sits *after* it (the matrix ends at
// 0x227), so they were three unrelated fields, not pitch/yaw/roll. Rotation
// lives in the 3x3 basis above; there is no Euler triple on the actor.
constexpr uint32_t kActorMatrixOffset = 0x1f8;
constexpr uint32_t kActorMatrixFloats = 12;
constexpr uint32_t kPosXOffset = kActorMatrixOffset + 0x0c;  // 0x204
constexpr uint32_t kPosYOffset = kActorMatrixOffset + 0x1c;  // 0x214
constexpr uint32_t kPosZOffset = kActorMatrixOffset + 0x2c;  // 0x224

// Contiguous sead::Vector3f velocity - see Actor::GetLinearVelocity for how
// this was identified and why it is a different kind of field to the position.
constexpr uint32_t kVelocityOffset = 0x25c;

// setActorMtxOnly(actor, const sead::Matrix34f&, const sead::Vector3f* vel) at
// 0x03798ae8 - the reason writing +0x204/+0x214/+0x224 by hand never moved
// anything. That matrix is the actor's *input* transform; the game reads the
// transform it actually uses from a second matrix at +0x22C, and 0x03798ae8 is
// what keeps the two in step.
//
// NOT ksys::act::Actor::setMtx, despite an earlier comment here saying so. That
// is 0x0379fb54, reached as virtual index 85, and the two are siblings rather
// than caller and callee - this one never calls it. The difference is the whole
// story for anything physics drives: 0x03798ae8 writes the actor side and
// stops, so for an actor with a character controller or a rigid body,
// Actor::updateMtxFromPhysics (virtual index 84) republishes the matrix from
// physics on the very next frame and the write is gone. Use it to place an
// actor physics does not drive; use SetMtx() below for anything else.
//
//   0x03798ae8(actor, mtx, vel):
//     0x03c6fe5c(mtx, actor + 0x1F8)   // copy into ACTOR_MATRIX
//     0x037986e4(actor, mtx)           // publish to actor+0x22C, routed through
//                                      // the attach frame at actor+0x3B8 when
//                                      // the actor is riding/parented
//     if (vel) actor+0x274/0x278/0x27C = vel                 // optional
//
// Poking only the first matrix leaves +0x22C stale, and the actor's own update
// overwrites the first one from the second on the next frame - which is exactly
// the "the field changes and the model doesn't" behaviour already documented on
// Spawn() below. ~30 gameplay call sites use 0x03798ae8, so it is the game's
// normal way to place an actor, not a back door.
using SetActorMtxFn = void (*)(void* actor, const float* mtx34, const float* velocity);
constexpr uintptr_t kSetActorMtxAddr = 0x03798ae8;

// --- ksys::act::Actor::setMtx, the authoritative placement path -------------
//
// virtual void setMtx(const sead::Matrix34f&, bool setActorMtx, bool refresh)
//
// The only function that pushes a transform into every representation at once:
// the actor matrix (+0x1F8), the render model's matrix, and the PHYSICS side -
// preferring the character controller, falling back to the main rigid body -
// then refreshing the physics instance set so cloth and ragdoll do not stretch
// across the move. Every Warp* AI action in the game calls it;
// uking::action::WarpToPos::oneShot_ is the reference, and passes (mtx, 1, 1).
//
// Called virtually rather than by address, because the player OVERRIDES it -
// on this build the base is 0x0379fb54 and Link's override is 0x02d66abc, which
// additionally updates his facing from the matrix basis and his own position
// copies. Going through the vtable gets the right one for whatever actor it is.
//
//   setActorMtx  true  write the actor and model matrices now. false stages
//                      into the +0x228 mirror and sets ActorFlag bit 2, which
//                      updateMtxFromPhysics only ever picks up for an actor
//                      with NO body at all - so false is not what you want for
//                      anything physics drives.
//   refresh      true  reset the physics instance set afterwards (cloth,
//                      ragdoll). Both of the game's warp actions pass true.
//
// Confirmed against a running game: index 85 resolved to Link's override, and
// the two slots this file already pinned by byte offset (getMaxLife, getLife)
// land on real functions under the same arithmetic.
constexpr uint32_t kSetMtxVtableSlot = 0x2ac;   // virtual index 85
using ActorSetMtxFn = void (*)(void* actor, const float* mtx34, int setActorMtx, int refresh);

// --- ksys::act::BaseProcMgr job lists (Wii U V208) ---
// Container geometry for Actor::ForEachDynamic; the derivation and the
// functions it was read from are documented there.
constexpr uintptr_t kBaseProcMgrAddr = 0x1047c244;  // global: BaseProcMgr*
constexpr uint32_t kJobBucketSize = 0xc0;           // one job type: 8 slots x 0x18
constexpr uint32_t kProcListSize = 0x0c;            // {prev, next, s32 count}
constexpr uint32_t kListsPerBucket = kJobBucketSize / kProcListSize;  // 16
constexpr uint32_t kMaxJobTypes = 32;               // sanity bound, not a game constant

// Pointer set used to visit each proc once when it is registered under more
// than one job type. Open addressed, never grows, cleared per traversal;
// a full table degrades to allowing duplicates rather than dropping actors.
struct ProcVisitSet {
    static constexpr size_t kSlots = 2048;  // power of two
    void* slots[kSlots];

    void Clear() {
        for (size_t i = 0; i < kSlots; ++i) slots[i] = nullptr;
    }

    // True if p had not been seen yet.
    bool Add(void* p) {
        size_t i = (reinterpret_cast<uintptr_t>(p) >> 4) & (kSlots - 1);
        for (size_t probe = 0; probe < kSlots; ++probe) {
            if (slots[i] == nullptr) {
                slots[i] = p;
                return true;
            }
            if (slots[i] == p) return false;
            i = (i + 1) & (kSlots - 1);
        }
        return true;
    }
};

inline ProcVisitSet& ProcVisitSetRef() {
    static ProcVisitSet s = {};
    return s;
}

// True while a traversal is in progress, so a nested one does not wipe the
// outer one's visit set out from under it.
inline bool& InProcTraversal() {
    static bool v = false;
    return v;
}

// BaseProcMgr's own lock, at mgr+0x80. Held while walking the job lists -
// BotW runs actor jobs on three threads (BaseProcMgr+0x11C/0x120/0x124, see
// the thread check at 0x0378f210), so the lists mutate under an unlocked
// reader and a stale `next` walks straight into freed memory.
//
// sead::CriticalSection keeps its OSMutex at +0x10 and these two are thin
// wrappers over it - 0x030bb668 is `r3 += 0x10; b OSLockMutex` and 0x030bb69c
// is `r3 += 0x10; b OSUnlockMutex`. Cafe OS mutexes are recursive, so taking
// this on a thread that already holds it is safe; the game's own deleteLater
// (0x0378a374) locks the same field the same way.
constexpr uint32_t kBaseProcMgrLockOffset = 0x80;
using CriticalSectionFn = void (*)(void* cs);
constexpr uintptr_t kCriticalSectionLockAddr = 0x030bb668;
constexpr uintptr_t kCriticalSectionUnlockAddr = 0x030bb69c;

class ProcMgrLock {
public:
    explicit ProcMgrLock(void* cs) : m_Cs(cs) {
        if (m_Cs) WiiXLaunch::GetTargetFunction<CriticalSectionFn>(0x0, kCriticalSectionLockAddr)(m_Cs);
    }
    ~ProcMgrLock() {
        if (m_Cs) WiiXLaunch::GetTargetFunction<CriticalSectionFn>(0x0, kCriticalSectionUnlockAddr)(m_Cs);
    }
    ProcMgrLock(const ProcMgrLock&) = delete;
    ProcMgrLock& operator=(const ProcMgrLock&) = delete;

private:
    void* m_Cs;
};

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

inline void ExecuteSpawn(const char* actorName, void* anchor, float x, float y, float z,
                         const float* scale) {
    ForcedCreateBaseProcThisSpawn() = false;

    void* mgr = *reinterpret_cast<void**>(kActorCreateMgrAddr);
    if (!mgr) return;

    void* heapOwner = *reinterpret_cast<void**>(kHeapProviderAddr);
    if (!heapOwner) return;
    void* heap = *reinterpret_cast<void**>(static_cast<uint8_t*>(heapOwner) + kHeapFieldOffset);

    // One static pack, reused every spawn - so it has to be reset every spawn.
    //
    // InstParamPack::Buffer::init (0x031f9818) zeroes the param count, the used
    // length and the 192-byte body, and the game calls it before filling a pack
    // in. This does not, which is harmless on the first spawn only: after that
    // the count and used length carry over, so each call appends another copy
    // of the position parameter to a buffer that is never cleared.
    alignas(8) static InstParamPack pack = {};
    auto initPackBuffer = WiiXLaunch::GetTargetFunction<InitParamPackBufferFn>(0x0, 0x031f9818);
    initPackBuffer(&pack.count);
    pack.anchor = nullptr;

    auto setAnchor = WiiXLaunch::GetTargetFunction<SetParamPackAnchorFn>(0x0, 0x037b55fc);
    setAnchor(&pack, anchor);

    // "@P" position, or the actor spawns at the pack's zeroed-out default
    // instead of near the anchor.
    auto writeParam = WiiXLaunch::GetTargetFunction<WriteParamFn>(0x0, 0x031f9870);
    alignas(8) static SafeStringRef positionKey = { reinterpret_cast<void*>(kPositionKeyWord0), reinterpret_cast<void*>(kSafeStringVtable) };
    float position[3] = { x, y, z };
    writeParam(&pack.count, position, &positionKey, 0xc, 4);

    // "@S", only when asked for. Left out entirely otherwise, so an ordinary
    // Spawn() writes exactly the pack it always did.
    if (scale) {
        alignas(8) static SafeStringRef scaleKey = { reinterpret_cast<void*>(kScaleKeyWord0), reinterpret_cast<void*>(kSafeStringVtable) };
        float scaleValue[3] = { scale[0], scale[1], scale[2] };
        writeParam(&pack.count, scaleValue, &scaleKey, 0xc, 4);
    }

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

// FUN_024adac8 - an actor per-frame update. Every real spawn is issued from
// inside some actor's own calc/update, never from a generic system-wide
// dispatcher, so a pending Spawn() request is flushed from here too, matching
// that pattern.
//
// This was previously described as "Player-specific". It is not. Logging its
// param_1 across many frames showed it arriving as Weapon_Sword_044 and as
// several other actors, on more than one of the actor job threads. So it fires
// many times per frame for different actors, and its argument is whichever
// actor is currently ticking - never assume it is the player. Anything that
// wants Link must fetch him through Player::GetRaw().
WIIXL_HOOK_DEFINE_TRAMPOLINE(SpawnFlushHook) {
    using RawCallbackFn = void (*)(void* playerPtr);
    static RawCallbackFn& CallbackRef() { static RawCallbackFn fn = nullptr; return fn; }

    static void Callback(void* param1, void* param2) {
        Orig(param1, param2);

        if (param1) {
            PendingSpawn& pending = PendingSpawnRef();
            if (pending.valid) {
                pending.valid = false;
                ExecuteSpawn(pending.name, pending.anchor, pending.pos[0], pending.pos[1],
                             pending.pos[2], pending.hasScale ? pending.scale : nullptr);
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
    enum class Kind : uint8_t {
        Proc = 0,
        Placement = 1
    };

    Actor() : m_Ptr(nullptr), m_Kind(Kind::Proc) {}
    explicit Actor(void* ptr, Kind kind = Kind::Proc) : m_Ptr(ptr), m_Kind(kind) {}

    bool IsValid() const { return m_Ptr != nullptr; }
    void* GetRaw() const { return m_Ptr; }
    Kind GetKind() const { return m_Kind; }
    bool IsPlacement() const { return m_Kind == Kind::Placement; }
    bool IsDynamic() const { return m_Kind == Kind::Proc; }

    const char* GetName() const {
        if (!m_Ptr || !IsReadablePtr(m_Ptr)) return "(none)";
#if WIIXL_SWITCH
        using GetActorUniqueNameFn = const char* (*)(void* actor);
        auto getUniqueName = WiiXLaunch::GetTargetFunction<GetActorUniqueNameFn>(0x11c9bfc, 0x0);
        const char* name = getUniqueName(m_Ptr);
        return name ? name : "(unnamed)";
#else
        if (m_Kind == Kind::Placement) {
            auto* base = static_cast<const uint8_t*>(m_Ptr);
            const char* const* ppName = reinterpret_cast<const char* const*>(base + 0xd8);
            const char* raw = nullptr;
            if (IsReadablePtr(ppName)) raw = *ppName;
            if (!raw || !IsReadablePtr(raw)) raw = reinterpret_cast<const char*>(base + 0xe4);
            if (!raw || !IsReadablePtr(raw) || raw[0] == '\0') return "(unnamed)";
            return raw;
        }

        static char buf[64];
        constexpr uintptr_t kSafeStringOffset = 0x04;
        constexpr uintptr_t kInlineNameOffset = 0x10;

        auto* base = static_cast<const uint8_t*>(m_Ptr);
        const char* raw = *reinterpret_cast<const char* const*>(base + kSafeStringOffset);
        if (!IsReadablePtr(raw)) raw = reinterpret_cast<const char*>(base + kInlineNameOffset);

        int n = 0;
        for (; n < static_cast<int>(sizeof(buf)) - 1; n++) {
            if (raw[n] == '\0') break;
            buf[n] = raw[n];
        }
        buf[n] = '\0';
        return n > 0 ? buf : "(unnamed)";
#endif
    }

    // Asks the game to delete this actor, the same way it deletes its own:
    // ksys::act::BaseProc::deleteLater(DeleteReason). Returns whether the
    // request was accepted - it is refused if the actor is already being
    // deleted or already flagged for it, which is a normal outcome rather
    // than an error.
    //
    // Unlike spawning, this is confirmed on both platforms:
    //   Switch  0x11b9da4  - symbolised in the 1.5.0 binary
    //   Wii U   0x0378a374 - V208, matched against that symbolised copy: same
    //                        early-outs on the state byte and delete flag, the
    //                        same name-string vfunc called twice, the same
    //                        BaseProcMgr singleton and high-priority-thread
    //                        check, the same lock-then-recheck of both
    //                        conditions, and the same virtual dispatch of the
    //                        reason. Only struct/vtable offsets differ, as
    //                        expected between a 64- and a 32-bit build.
    //
    // The Wii U side is corroborated by the game's own use of it: 0x0378a70c
    // calls it as deleteLater(proc, 1) and deleteLater(proc, 2) on the failure
    // paths of actor creation, which fixes both the argument order and the
    // meaning of the second parameter.
    //
    // The Switch address is derived from the symbolised binary rather than
    // observed running; the identity is certain, the runtime behaviour has not
    // been exercised there yet.
    //
    // Confirmed on Wii U by deleting the player's equipped sword: the weapon
    // leaves Link's hand. Its inventory entry stays, which is correct - that is
    // save data, not the actor.
    //
    // Confirmed to remove a spawned weapon from the world as well, once that
    // weapon has been woken (see Spawn). The one case where the proc dies and
    // the model stays is an actor the game never places in the world - armour -
    // which is a property of that actor, not of this call. See Spawn below.
    //
    // The proc's state flags at +0x64 drive this and two neighbouring
    // transitions, via 0x0378a1b8(proc, bit) which sets a bit and queues the
    // proc for state processing:
    //
    //   bit 0  request deletion (what this uses)
    //   bit 1  wake:  Sleep -> Calc, and it stays awake
    //   bit 2  sleep: Calc  -> Sleep
    static constexpr bool SupportsDelete = true;

    bool Delete(uint32_t reason = 0) const {
        if (!m_Ptr) return false;
        using DeleteLaterFn = int (*)(void* proc, uint32_t reason);
        auto deleteLater = WiiXLaunch::GetTargetFunction<DeleteLaterFn>(0x11b9da4, 0x0378a374);
        return deleteLater(m_Ptr, reason) != 0;
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

    // Registers a callback fired from an actor's per-frame update.
    //
    // The Actor passed in is the actor that is currently ticking, NOT the
    // player - see the note on SpawnFlushHook. It fires many times per frame,
    // for different actors, on several threads. Treat it as "a safe point to
    // run per-frame work from", and fetch anything specific yourself.
    using PlayerCallbackFn = void (*)(const Actor& tickingActor);
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
    // met. Spawning does produce visible actors regardless, so the path
    // actually taken has not been fully pinned down.
    //
    // **Spawn what the game itself puts in the world, and it just works.**
    // Confirmed on Wii U with a weapon, an animal and an enemy: a spawned
    // Enemy_Bokoblin_Junior runs its AI, animates, reacts to the player and
    // fights. A spawned weapon falls under gravity with its proc position
    // tracking the model and brings its own sub-actors along (a sword spawns
    // its sheath, which the game then cleans up itself). Actor::Delete removes
    // any of them cleanly. Nothing extra is needed to make this happen.
    //
    // Actors that the game never places in the world do not. Armour is the
    // clear case: an Armor_* actor binds to a skeleton - the player's, or the
    // pause menu doll's - and nothing in the game ever creates one as a
    // free-standing world actor. Spawn one and it *renders*, which makes it
    // look like it worked, but it has no world lifecycle:
    //
    //   * Writing the proc's position moves the field and not the model.
    //   * Deleting the proc destroys it - confirmed by watching its memory get
    //     handed to another actor - and the model stays on screen.
    //
    // That is not a fault in this code. Such an actor is created correctly:
    // compared field for field against the armour the pause menu builds, ours
    // matches on state, job bits, flags, vtable and unit, both at creation and
    // at deletion. There is simply no teardown path for a world armour model,
    // because the game never makes one.
    //
    // So: fine for weapons, animals, enemies, NPCs, props. For armour, expect
    // a model that renders once and can never be moved or removed.
    //
    // Armour also never leaves the Sleep state - it goes Init -> Sleep and
    // never reaches Calc. That is the same story rather than a separate one:
    // an actor with no world behaviour has nothing to run. Actors that do have
    // behaviour reach Calc by themselves, with no help from the caller.
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
        pending.hasScale = false;
        return true;
#endif
    }

    // Spawn() with a per-axis scale, written into the creation params as "@S".
    //
    // The scale a map placement carries is a vec3 and so is this: the game's
    // own uniform-scale helper (0x037b55a4) splats a single float into three
    // and writes the same key, and the second overload in the decompilation
    // takes a sead::Vector3f directly. AirWallCurseGanon is placed at
    // {8, 5, 6} in E-4, so non-uniform is what the game itself ships.
    //
    // NOT MEASURED, and the reason to be careful with it: the physics side of
    // scaling that this repo could read is uniform-only - phys::RigidBody and
    // phys::BoxShape both take setScale(float), which multiplies all three
    // extents by the same number. phys::BoxShape::setExtents does take a vec3,
    // so a per-axis box IS representable and the placement data says the game
    // builds them, but the path from "@S" to those extents was not traced. If a
    // non-uniform wall comes out cubic, that is where it went - fall back to
    // equal components and more, smaller panels.
    //
    // Scale multiplies the actor's own shape, it does not replace it. An
    // AirWallCurseGanon is a 2 x 2 x 2 box, so {t, h, w} gives 2t x 2h x 2w.
    //
    // One request is queued at a time, same as Spawn - a caller placing
    // several actors issues one per tick.
    static bool SpawnScaled(const char* actorName, const Actor& anchor, float x, float y, float z,
                            float scaleX, float scaleY, float scaleZ) {
#if WIIXL_SWITCH
        (void)actorName; (void)anchor; (void)x; (void)y; (void)z;
        (void)scaleX; (void)scaleY; (void)scaleZ;
        return false;
#else
        impl::PendingSpawn& pending = impl::PendingSpawnRef();
        pending.valid = true;
        pending.name = actorName;
        pending.anchor = anchor.GetRaw();
        pending.pos[0] = x;
        pending.pos[1] = y;
        pending.pos[2] = z;
        pending.hasScale = true;
        pending.scale[0] = scaleX;
        pending.scale[1] = scaleY;
        pending.scale[2] = scaleZ;
        return true;
#endif
    }

    // Health / Life accessors (Wii U / Cemu confirmed):
    // On Wii U, Actor+0xe8 is the primary actor vtable pointer.
    //
    // Byte offsets here are right; the index numbers this comment used to give
    // were not. A Wii U vtable entry is EIGHT bytes - {s16 delta; s16 index;
    // void* fn} - with the function pointer at entry+4, so virtual index N
    // lives at byte offset N*8 + 4. Dividing an offset by 4 produces an index
    // that does not exist, which is what "index 61" and "index 175" were.
    //
    //   +0xf4  = 30*8 + 4  -> index 30, GetMaxLife(actor) -> int
    //   +0x2bc = 87*8 + 4  -> index 87, GetCurrentLifePtr(actor) -> int*
    //   +0x2ac = 85*8 + 4  -> index 85, setMtx  (see kSetMtxVtableSlot)
    //   +0x2a4 = 84*8 + 4  -> index 84, updateMtxFromPhysics
    //
    // Verified by reading a live player's vtable: every entry showed delta=0
    // and a .text pointer at +4, and both slots below landed on real functions.
    //
    // Indexing vtable[byteOffset / 4] as a void** still lands on the right word
    // - that is the function pointer's own address - so the code below is
    // correct as written; only the stated indices were wrong.
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

    // Max life cannot be set through the actor. getMaxLife() reads a cache at
    // actor+0x1320; the authoritative, save-backed value is the GameData flag
    // MaxHartValue, and nothing re-syncs the cache from it. Writing here shows
    // the new value for a frame, then reverts, and the HUD plays the
    // heart-container-removed (Horned Statue) effect on the way back down.
    //
    // Use GameData::SetMaxLife (gamedata.hpp), which calls the game's own
    // PlayerInfo::setMaxHeartValue - flag and caches together. Kept here as an
    // explicit no-op so this does not get reimplemented the wrong way again.
    static constexpr bool SupportsSetMaxLife = false;

    bool SetMaxLife(int maxLife) {
        (void)maxLife;
        return false;
    }

    // Convenience helpers converting between integer Life units (4 per heart) and floating-point hearts
    float GetCurrentHearts() const { return GetCurrentLife() / 4.0f; }
    void SetCurrentHearts(float hearts) { SetCurrentLife(static_cast<int>(hearts * 4.0f + 0.5f)); }

    float GetMaxHearts() const { return GetMaxLife() / 4.0f; }
    bool SetMaxHearts(float hearts) { return SetMaxLife(static_cast<int>(hearts * 4.0f + 0.5f)); }



    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // --- ID & State Queries (Wii U / Cemu confirmed) ---
    // Offset +0x50 holds the uint32_t BaseProc/Actor ID.
    // Offset +0x54 holds the lifecycle state byte: 0 = Init, 1 = Calc/Active, 2 = Sleep, 3 = Deleted/Dying.

    uint32_t GetId() const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr)) return 0;
        if (m_Kind == Kind::Placement) {
            return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m_Ptr) + 0x00);
        }
        return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m_Ptr) + 0x50);
#else
        return 0;
#endif
    }

    uint8_t GetState() const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr)) return 0;
        if (m_Kind == Kind::Placement) return 1;
        return *reinterpret_cast<const uint8_t*>(static_cast<const uint8_t*>(m_Ptr) + 0x54);
#else
        return 0;
#endif
    }

    bool IsActive() const { return GetState() == 1; }
    bool IsSleeping() const { return GetState() == 2; }
    bool IsDying() const { return GetState() == 3; }

    // --- Position & Rotation (Wii U / Cemu confirmed) ---
    // Dynamic Procs: the sead::Matrix34f at +0x1F8. Position is its translation
    // column (+0x204/+0x214/+0x224); rotation is its 3x3 basis. See the offset
    // notes and the setMtx write-up in impl above - in particular, why writing
    // the position fields directly never moved an actor.
    // Map Placements: +0x14 (X), +0x18 (Y), +0x1C (Z); +0x30/+0x34/+0x38 rotation.

    static constexpr bool SupportsTransform = !WIIXL_SWITCH;
    static constexpr bool SupportsPosition = !WIIXL_SWITCH;
    static constexpr bool SupportsRotation = !WIIXL_SWITCH;

    // Raw access to the actor's sead::Matrix34f, row-major, 12 floats.
    // Placements have no matrix, so both return false for them.
    bool GetMatrix(float outMtx34[12]) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr) || m_Kind == Kind::Placement) return false;
        const auto* src = reinterpret_cast<const float*>(
            static_cast<const uint8_t*>(m_Ptr) + impl::kActorMatrixOffset);
        for (uint32_t i = 0; i < impl::kActorMatrixFloats; ++i) outMtx34[i] = src[i];
        // An actor whose stored transform is not finite has no transform worth
        // reading, and must not have one written back - see IsFiniteMatrix.
        // SetPosition and SetRotation both read through here, so refusing it
        // here is what keeps them off those actors.
        return IsFiniteMatrix(outMtx34);
#else
        (void)outMtx34;
        return false;
#endif
    }

    // Hands the matrix to ksys::act::Actor::setMtx so the actor's published
    // transform at +0x22C is updated too - a straight write to +0x1F8 is
    // reverted by the actor's own update. velocity is optional (+0x274).
    bool SetMatrix(const float mtx34[12], const Vec3* velocity = nullptr) const {
#if !WIIXL_SWITCH
        // Re-checked here and not only at the traversal: this is the call that
        // writes 48 bytes at +0x1F8 and again at +0x22C, so it refuses anything
        // that does not look like a real BaseProc regardless of how the Actor
        // was constructed.
        if (!IsPlausibleProc(m_Ptr) || m_Kind == Kind::Placement) return false;
        // Never hand a non-finite matrix to the game, whatever produced it.
        if (!IsFiniteMatrix(mtx34)) return false;
        auto setMtx = WiiXLaunch::GetTargetFunction<impl::SetActorMtxFn>(0x0, impl::kSetActorMtxAddr);
        const float vel[3] = { velocity ? velocity->x : 0.0f,
                               velocity ? velocity->y : 0.0f,
                               velocity ? velocity->z : 0.0f };
        setMtx(m_Ptr, mtx34, velocity ? vel : nullptr);
        return true;
#else
        (void)mtx34; (void)velocity;
        return false;
#endif
    }

    // Whether this actor's transform is parented to something else - a held
    // weapon, worn armour, anything riding or mounted.
    //
    // Not a heuristic: +0x3B8 is the exact field setMtx branches on. When it
    // is null 0x037986e4 writes the actor's own matrix straight to +0x22C;
    // when it is not, the write is routed through the attach frame via
    // 0x034d3d40, which combines it against the parent's transform. Moving an
    // attached actor independently of its parent is therefore a different
    // operation from moving a free-standing one, and worth being able to
    // exclude.
    bool IsAttached() const {
#if !WIIXL_SWITCH
        if (!IsPlausibleProc(m_Ptr) || m_Kind == Kind::Placement) return false;
        return *reinterpret_cast<void* const*>(
            static_cast<const uint8_t*>(m_Ptr) + 0x3b8) != nullptr;
#else
        return false;
#endif
    }

    // --- Linear velocity (Wii U / Cemu) ---
    //
    // actor+0x25C is a contiguous sead::Vector3f velocity - unlike the
    // position, which is the strided translation column of the matrix at
    // +0x1F8.
    //
    // Identified from 0x0383b398, which feeds actor+0x25C to 0x03c6fcf0 and
    // uses the result as a speed value. 0x03c6fcf0 is
    // sqrt(v[0]^2 + v[1]^2 + v[2]^2), so the field is three floats wide; the
    // same function separately reads +0x25C and +0x264 and takes
    // sqrt(x^2 + z^2) for horizontal speed, which fixes the component order as
    // x at +0x25C, y at +0x260, z at +0x264.
    //
    // Worth knowing which lever this is. Writing a position on an actor the
    // physics drives does not stick - Link's is restored to the bit-identical
    // original on the next tick, measured. Velocity is the input that a
    // character controller integrates, so it is the one that survives, and it
    // is how the game itself makes an actor move rather than teleport.
    //
    // Not to be confused with actor+0x274, the optional third argument of
    // setMtx (see kSetActorMtxAddr); that one is written only when a caller
    // passes it and is used elsewhere as a scale factor.
    static constexpr bool SupportsVelocity = !WIIXL_SWITCH;

    bool GetLinearVelocity(float& x, float& y, float& z) const {
#if !WIIXL_SWITCH
        if (!IsPlausibleProc(m_Ptr) || m_Kind == Kind::Placement) return false;
        const auto* v = reinterpret_cast<const float*>(
            static_cast<const uint8_t*>(m_Ptr) + impl::kVelocityOffset);
        if (!IsFiniteFloat(v[0]) || !IsFiniteFloat(v[1]) || !IsFiniteFloat(v[2])) return false;
        x = v[0]; y = v[1]; z = v[2];
        return true;
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    Vec3 GetLinearVelocity() const {
        Vec3 v;
        GetLinearVelocity(v.x, v.y, v.z);
        return v;
    }

    bool SetLinearVelocity(float x, float y, float z) const {
#if !WIIXL_SWITCH
        if (!IsPlausibleProc(m_Ptr) || m_Kind == Kind::Placement) return false;
        if (!IsFiniteFloat(x) || !IsFiniteFloat(y) || !IsFiniteFloat(z)) return false;
        auto* v = reinterpret_cast<float*>(
            static_cast<uint8_t*>(m_Ptr) + impl::kVelocityOffset);
        v[0] = x; v[1] = y; v[2] = z;
        return true;
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    bool SetLinearVelocity(const Vec3& v) const {
        return SetLinearVelocity(v.x, v.y, v.z);
    }

    // Adds to the current velocity rather than replacing it - the usual way to
    // launch an actor without discarding the motion it already had.
    bool AddLinearVelocity(float x, float y, float z) const {
        Vec3 v;
        if (!GetLinearVelocity(v.x, v.y, v.z)) return false;
        return SetLinearVelocity(v.x + x, v.y + y, v.z + z);
    }

    // The actor's physics/motion object, via slot 0x30C of the +0xE8 vtable.
    //
    // Needed because the matrix at +0x1F8 is an *output* for anything the
    // physics drives: writing Link's position through setMtx reads back
    // correctly, survives the rest of the frame, then is restored to the
    // bit-identical original on the next tick. The authoritative position for
    // such an actor lives on this object, not on the actor.
    //
    // Slot 0x30C is where the game gets it: 0x0383b398 calls it on the player
    // actor and reads Vector3f fields at +0xAC and +0xC4 out of the result,
    // and 0x024b0cbc calls it and writes +0x158. Both null-check the return,
    // so an actor without physics gives null here too.
    void* GetPhysicsObject() const {
#if !WIIXL_SWITCH
        if (!IsPlausibleProc(m_Ptr) || m_Kind == Kind::Placement) return nullptr;

        void** vtable = *reinterpret_cast<void***>(static_cast<uint8_t*>(m_Ptr) + 0xe8);
        if (!vtable) return nullptr;

        using GetPhysFn = void* (*)(void* actor);
        auto fn = reinterpret_cast<GetPhysFn>(vtable[0x30c / 4]);
        uintptr_t fnAddr = reinterpret_cast<uintptr_t>(fn);
        if (fnAddr < 0x02000000 || fnAddr > 0x04000000) return nullptr;

        void* phys = fn(m_Ptr);
        return IsReadablePtr(phys) ? phys : nullptr;
#else
        return nullptr;
#endif
    }

    bool GetPosition(float& x, float& y, float& z) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr)) return false;
        const auto* base = static_cast<const uint8_t*>(m_Ptr);
        if (m_Kind == Kind::Placement) {
            x = *reinterpret_cast<const float*>(base + 0x14);
            y = *reinterpret_cast<const float*>(base + 0x18);
            z = *reinterpret_cast<const float*>(base + 0x1c);
            return true;
        }
        x = *reinterpret_cast<const float*>(base + impl::kPosXOffset);
        y = *reinterpret_cast<const float*>(base + impl::kPosYOffset);
        z = *reinterpret_cast<const float*>(base + impl::kPosZOffset);
        return true;
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    static bool IsValidActorName(const char* name) {
        if (!name || !IsReadablePtr(name)) return false;
        if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) return false;
        for (size_t i = 0; i < 64; ++i) {
            char c = name[i];
            if (c == '\0') return i >= 2;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        }
        return false;
    }

    // Moves a live actor. Reads the actor's current matrix, replaces only its
    // translation column, and hands the whole thing back through setMtx - see
    // impl::kSetActorMtxAddr for why the write has to go that way round.
    //
    // Returns false for a map placement (Kind::Placement). Those entries are
    // the placement *records* the game reads at load time to decide what to
    // spawn, not the spawned actor, so writing their translate moves nothing -
    // it only corrupts the record. Iterate placements to find things; move the
    // dynamic actor the placement produced.
    bool SetPosition(float x, float y, float z) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr) || m_Kind == Kind::Placement) return false;

        float mtx[12];
        if (!GetMatrix(mtx)) return false;
        mtx[3] = x;
        mtx[7] = y;
        mtx[11] = z;
        return SetMatrix(mtx);
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    // Places the actor through ksys::act::Actor::setMtx - virtual index 85 -
    // which is what the game's own Warp* actions call.
    //
    // Use this rather than SetPosition for anything physics drives. SetPosition
    // writes the actor side only, and Actor::updateMtxFromPhysics (index 84)
    // republishes the matrix from the character controller or the rigid body
    // every frame, so the write is gone before it is drawn. setMtx writes the
    // physics side itself, which is why it sticks.
    //
    // Called through the vtable on purpose: the player overrides this, and the
    // override also updates facing and his own position copies.
    //
    // Returns false when the actor, its vtable or the slot do not look right -
    // never a blind call into whatever the slot happened to hold, so a game
    // update that moves the layout fails visibly instead of crashing.
    bool SetMtx(const float mtx34[12], bool setActorMtx = true,
                bool refreshPhysics = true) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr) || m_Kind == Kind::Placement) return false;
        if (!mtx34) return false;

        uintptr_t base = reinterpret_cast<uintptr_t>(m_Ptr);
        uintptr_t vtable = *reinterpret_cast<uintptr_t*>(base + 0xe8);
        if (vtable < 0x10000000 || vtable >= 0xa0000000 || (vtable & 3)) return false;

        uintptr_t fn = *reinterpret_cast<uintptr_t*>(vtable + impl::kSetMtxVtableSlot);
        if (fn < 0x02000020 || fn >= 0x04347c2c || (fn & 3)) return false;

        reinterpret_cast<impl::ActorSetMtxFn>(fn)(
            m_Ptr, mtx34, setActorMtx ? 1 : 0, refreshPhysics ? 1 : 0);
        return true;
#else
        (void)mtx34; (void)setActorMtx; (void)refreshPhysics;
        return false;
#endif
    }

    // Moves the actor, keeping its current facing, through setMtx.
    //
    // Reads the current matrix and replaces only the translation column, so a
    // move does not also spin the actor. This is the one to reach for when
    // SetPosition "works" and then the actor snaps back.
    bool WarpTo(float x, float y, float z) const {
#if !WIIXL_SWITCH
        float mtx[12];
        if (!GetMatrix(mtx)) return false;
        mtx[3] = x;
        mtx[7] = y;
        mtx[11] = z;
        return SetMtx(mtx, true, true);
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    bool WarpTo(const Vec3& pos) const { return WarpTo(pos.x, pos.y, pos.z); }

    Vec3 GetPosition() const {
        Vec3 pos;
        GetPosition(pos.x, pos.y, pos.z);
        return pos;
    }

    bool SetPosition(const Vec3& pos) const {
        return SetPosition(pos.x, pos.y, pos.z);
    }

    // Euler XYZ in radians, matching sead::Matrix34f::makeR (R = Rz * Ry * Rx).
    // There is no Euler triple stored on the actor - the previous +0x1E4 /
    // +0x1F4 / +0x234 offsets were three unrelated fields either side of the
    // matrix - so this decomposes the 3x3 basis instead.
    //
    // The offsets and the setMtx round trip are confirmed; the choice of Euler
    // convention is sead's documented one rather than something observed in
    // game, so treat the angles' sign/order as the part still worth checking.
    bool GetRotation(float& x, float& y, float& z) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr)) return false;
        if (m_Kind == Kind::Placement) {
            const auto* base = static_cast<const uint8_t*>(m_Ptr);
            x = *reinterpret_cast<const float*>(base + 0x30);
            y = *reinterpret_cast<const float*>(base + 0x34);
            z = *reinterpret_cast<const float*>(base + 0x38);
            return true;
        }

        float m[12];
        if (!GetMatrix(m)) return false;

        // m[8] = m[2][0] = -sin(y)
        float sy = -m[8];
        if (sy > 1.0f) sy = 1.0f;
        if (sy < -1.0f) sy = -1.0f;
        y = std::asin(sy);

        // Gimbal lock: cos(y) ~ 0 leaves x and z degenerate, so fold into x.
        if (std::fabs(sy) > 0.99999f) {
            x = std::atan2(-m[6], m[5]);
            z = 0.0f;
        } else {
            x = std::atan2(m[9], m[10]);
            z = std::atan2(m[4], m[0]);
        }
        return true;
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    // Rebuilds the 3x3 basis from Euler XYZ (radians), keeps the actor's
    // current translation, and publishes through setMtx. Any scale baked into
    // the old basis is dropped - the result is a pure rotation.
    //
    // Returns false for a map placement, for the same reason SetPosition does.
    bool SetRotation(float x, float y, float z) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !IsReadablePtr(m_Ptr) || m_Kind == Kind::Placement) return false;

        float m[12];
        if (!GetMatrix(m)) return false;

        const float sx = std::sin(x), cx = std::cos(x);
        const float sy = std::sin(y), cy = std::cos(y);
        const float sz = std::sin(z), cz = std::cos(z);

        m[0] = cy * cz;                 // m[0][0]
        m[1] = sx * sy * cz - cx * sz;  // m[0][1]
        m[2] = cx * sy * cz + sx * sz;  // m[0][2]
        m[4] = cy * sz;                 // m[1][0]
        m[5] = sx * sy * sz + cx * cz;  // m[1][1]
        m[6] = cx * sy * sz - sx * cz;  // m[1][2]
        m[8] = -sy;                     // m[2][0]
        m[9] = sx * cy;                 // m[2][1]
        m[10] = cx * cy;                // m[2][2]
        // m[3], m[7], m[11] (translation) left as read.

        return SetMatrix(m);
#else
        (void)x; (void)y; (void)z;
        return false;
#endif
    }

    Vec3 GetRotation() const {
        Vec3 rot;
        GetRotation(rot.x, rot.y, rot.z);
        return rot;
    }

    bool SetRotation(const Vec3& rot) const {
        return SetRotation(rot.x, rot.y, rot.z);
    }

    // --- World Query & Iteration ---
    static constexpr bool SupportsWorldQuery = !WIIXL_SWITCH;

    // Zero-allocation visitor over every live BaseProc the game itself knows
    // about.
    //
    // This used to walk a supposed red-black tree at BaseProcMgr+0x6C plus the
    // 256-entry BaseProcUnit pool at 0x105597C0, filtered by IsValidActorName.
    // Neither container is what it was taken for, so in practice only the
    // player ever came out of it. The real one, read off
    // BaseProcMgr::processJobs (0x0378ec20):
    //
    //   mgr+0x64  s32   job-type count      } sead::Buffer<JobTypeBucket>
    //   mgr+0x68  ptr   job-type buckets    }
    //
    //   bucket    0xC0 bytes = 8 priority slots x 0x18
    //   slot      0x18 bytes = 2 lists x 0xC
    //   list      +0x00 prev, +0x04 next/first, +0x08 s32 count
    //
    //   node      +0x04 next, +0x08 BaseProc*, +0x10 priority byte
    //
    // The 0xC0/0x18/0xC nesting is fixed by the iterators the job loop uses:
    // 0x03791fcc strides the bucket by 0x18 up to +0xC0, 0x03791f8c strides
    // that by 0xC and reads {next, count} at +0x04/+0x08, and 0x0379201c
    // advances through node+0x04, stopping when it reaches the list head - the
    // head being the list struct itself, which is what terminates the walk
    // below. node+0x08 is the proc because the job loop dispatches on exactly
    // that: FUN_0378b60c(*(node + 8), jobType).
    //
    // A proc registered for several job types appears in several buckets, so
    // visits are de-duplicated - without that, a caller nudging every actor by
    // a fixed step would move some of them several times per frame.
    //
    // Names are deliberately not filtered here. These pointers come from the
    // manager's own lists, so they are live procs by construction; requiring
    // GetName() to look like an actor name only threw real actors away.
    template <typename CallbackFn>
    static void ForEachDynamic(CallbackFn&& callback) {
#if !WIIXL_SWITCH
        uint8_t* mgr = *reinterpret_cast<uint8_t**>(impl::kBaseProcMgrAddr);
        if (!mgr || !IsReadablePtr(mgr)) return;

        // Held for the whole traversal, callback included. Three job threads
        // mutate these lists, and this hook itself runs on more than one of
        // them, so the lock is what keeps a walk from following a `next` that
        // another core just freed - and what makes the shared visit set below
        // safe without a second lock of our own.
        impl::ProcMgrLock lock(mgr + impl::kBaseProcMgrLockOffset);

        impl::ProcVisitSet& seen = impl::ProcVisitSetRef();
        const bool outermost = !impl::InProcTraversal();
        if (outermost) {
            seen.Clear();
            impl::InProcTraversal() = true;
        }
        struct TraversalScope {
            bool outermost;
            ~TraversalScope() { if (outermost) impl::InProcTraversal() = false; }
        } traversalScope{outermost};

        // Returns false when the callback asked to stop.
        auto visit = [&](void* proc) -> bool {
            if (!IsPlausibleProc(proc)) return true;
            if (!seen.Add(proc)) return true;
            Actor actor(proc, Kind::Proc);
            if constexpr (std::is_invocable_r_v<bool, CallbackFn, const Actor&>) {
                return callback(actor);
            } else {
                callback(actor);
                return true;
            }
        };

        uint32_t jobTypeCount = *reinterpret_cast<uint32_t*>(mgr + 0x64);
        uint8_t* buckets = *reinterpret_cast<uint8_t**>(mgr + 0x68);

        // The game only ever indexes this buffer with a small job-type id;
        // the cap is a sanity bound on a field read out of live memory.
        if (buckets && IsReadablePtr(buckets) && jobTypeCount <= impl::kMaxJobTypes) {
            for (uint32_t jt = 0; jt < jobTypeCount; ++jt) {
                uint8_t* bucket = buckets + jt * impl::kJobBucketSize;

                for (uint32_t li = 0; li < impl::kListsPerBucket; ++li) {
                    uint8_t* list = bucket + li * impl::kProcListSize;
                    int32_t count = *reinterpret_cast<int32_t*>(list + 0x08);
                    if (count <= 0) continue;

                    uint8_t* node = *reinterpret_cast<uint8_t**>(list + 0x04);
                    // count bounds the walk so a torn list cannot spin.
                    for (int32_t n = 0; n < count; ++n) {
                        if (!node || node == list || !IsReadablePtr(node)) break;
                        void* proc = *reinterpret_cast<void**>(node + 0x08);
                        if (!visit(proc)) return;
                        node = *reinterpret_cast<uint8_t**>(node + 0x04);
                    }
                }
            }
        }

        // Player, in case it is not registered for any job type this frame.
        uint8_t** ppPlayerTracker = reinterpret_cast<uint8_t**>(0x10463f38);
        if (ppPlayerTracker && IsReadablePtr(*ppPlayerTracker)) {
            uint8_t* tracker = *ppPlayerTracker;
            void* linkMgr = *reinterpret_cast<void**>(tracker + 0x34);
            uint32_t procId = *reinterpret_cast<uint32_t*>(tracker + 0x38);
            uint8_t flag = *reinterpret_cast<uint8_t*>(tracker + 0x3c);

            if (linkMgr && IsReadablePtr(linkMgr)) {
                using GetProcFn = void* (*)(void* mgr, uint32_t id, uint8_t flag);
                auto getProc = WiiXLaunch::GetTargetFunction<GetProcFn>(0x0, 0x0378d8dc);
                visit(getProc(linkMgr, procId, flag));
            }
        }
#else
        (void)callback;
#endif
    }

    // Zero-allocation visitor over every STATIC map placement object.
    template <typename CallbackFn>
    static void ForEachStatic(CallbackFn&& callback) {
#if !WIIXL_SWITCH
        uint8_t** pPlacementMgr = reinterpret_cast<uint8_t**>(0x1047c318);
        if (pPlacementMgr && *pPlacementMgr && IsReadablePtr(*pPlacementMgr)) {
            uint8_t* pPlacement = *pPlacementMgr;
            uint8_t* pMapPlacement = *reinterpret_cast<uint8_t**>(pPlacement + 0x11c);
            if (pMapPlacement && IsReadablePtr(pMapPlacement)) {
                // 6000 x 0x124 is the array's real fixed capacity, not a guess.
                // Every placement lookup in the game inlines the same
                // sead::SafeArray-style clamp against that exact literal:
                //
                //   base = placementMgr[0x11c] + 0x2D0;
                //   if (mapObject->idx16 < 6000) base += idx16 * 0x124;
                //
                // seen at 0x0379e3d4 (five times), 0x0379f2a0, 0x0313b84c,
                // 0x0313a878 and 0x0313a8e8 - the last writing it as
                // `(uint*)base + idx * 0x49`, which independently confirms the
                // 0x124 stride. So the array spans 0x2D0..0x6D050 and this walk
                // stays inside it; it is not an over-read.
                //
                // What there is no field for is how many slots the *current*
                // region actually populated. The game never iterates this array
                // - it only random-accesses it by the uint16 index each map
                // object carries at +0x04 - so no live count is reachable from
                // any of those paths. Stale slots therefore still hold whatever
                // the last region left there, which is what the name check
                // below is for. Do not drop it.
                uint8_t* entriesBase = pMapPlacement + 0x2d0;
                constexpr size_t kPlacementCount = 6000;
                constexpr size_t kPlacementEntrySize = 0x124;

                for (size_t i = 0; i < kPlacementCount; ++i) {
                    uint8_t* entry = entriesBase + (i * kPlacementEntrySize);
                    if (!IsReadablePtr(entry)) break;

                    const char* const* ppName = reinterpret_cast<const char* const*>(entry + 0xd8);
                    const char* name = nullptr;
                    if (IsReadablePtr(ppName)) name = *ppName;
                    if (!name || !IsReadablePtr(name)) name = reinterpret_cast<const char*>(entry + 0xe4);
                    if (!IsValidActorName(name)) continue;

                    Actor placementActor(entry, Kind::Placement);
                    if constexpr (std::is_invocable_r_v<bool, CallbackFn, const Actor&>) {
                        if (!callback(placementActor)) return;
                    } else {
                        callback(placementActor);
                    }
                }
            }
        }
#else
        (void)callback;
#endif
    }

    // Zero-allocation visitor over EVERY actor (both Dynamic and Static Map Placements).
    template <typename CallbackFn>
    static void ForEach(CallbackFn&& callback) {
        ForEachDynamic(callback);
        ForEachStatic(callback);
    }

    // Returns the total count of valid active actors currently loaded in the world.
    static size_t GetCount() {
        size_t count = 0;
        ForEach([&count](const Actor&) {
            ++count;
        });
        return count;
    }

    // Looks up an actor by its actor name / ID string (e.g. "Enemy_Bokoblin", "Player", etc.).
    static Actor GetActor(const char* name, bool exactMatch = false) {
        if (!name || name[0] == '\0') return Actor(nullptr);
        Actor found(nullptr);
        ForEach([&](const Actor& actor) {
            const char* actorName = actor.GetName();
            if (exactMatch) {
                if (std::strcmp(actorName, name) == 0) {
                    found = actor;
                    return false;
                }
            } else {
                if (std::strstr(actorName, name) != nullptr) {
                    found = actor;
                    return false;
                }
            }
            return true;
        });
        return found;
    }

    // Overload: looks up an actor by its unique numeric BaseProc ID.
    static Actor GetActor(uint32_t id) {
        Actor found(nullptr);
        ForEach([&](const Actor& actor) {
            if (actor.GetId() == id) {
                found = actor;
                return false;
            }
            return true;
        });
        return found;
    }

    // Backwards-compatible alias for GetActor(id)
    static Actor GetById(uint32_t id) { return GetActor(id); }

    // Backwards-compatible alias for GetActor(name)
    static Actor FindByName(const char* name, bool exactMatch = false) { return GetActor(name, exactMatch); }
    static Actor Get(const char* name, bool exactMatch = false) { return GetActor(name, exactMatch); }

    // Queries only DYNAMIC active BaseProc actors.
    static std::vector<Actor> QueryDynamicActors(const char* name = nullptr, bool exactMatch = false) {
        std::vector<Actor> actors;
        ForEachDynamic([&](const Actor& actor) {
            if (name == nullptr || name[0] == '\0') {
                actors.push_back(actor);
            } else {
                const char* actorName = actor.GetName();
                if (exactMatch) {
                    if (std::strcmp(actorName, name) == 0) actors.push_back(actor);
                } else {
                    if (std::strstr(actorName, name) != nullptr) actors.push_back(actor);
                }
            }
        });
        return actors;
    }

    // Alias for QueryDynamicActors
    static std::vector<Actor> GetDynamicActors(const char* name = nullptr, bool exactMatch = false) {
        return QueryDynamicActors(name, exactMatch);
    }

    // Queries only STATIC map placement objects.
    static std::vector<Actor> QueryStaticActors(const char* name = nullptr, bool exactMatch = false) {
        std::vector<Actor> actors;
        ForEachStatic([&](const Actor& actor) {
            if (name == nullptr || name[0] == '\0') {
                actors.push_back(actor);
            } else {
                const char* actorName = actor.GetName();
                if (exactMatch) {
                    if (std::strcmp(actorName, name) == 0) actors.push_back(actor);
                } else {
                    if (std::strstr(actorName, name) != nullptr) actors.push_back(actor);
                }
            }
        });
        return actors;
    }

    // Alias for QueryStaticActors
    static std::vector<Actor> GetStaticActors(const char* name = nullptr, bool exactMatch = false) {
        return QueryStaticActors(name, exactMatch);
    }

    // Queries ALL actors currently loaded in the world (both Dynamic and Static).
    static std::vector<Actor> GetAllActors(const char* name = nullptr, bool exactMatch = false) {
        std::vector<Actor> actors;
        actors.reserve(64);
        ForEach([&](const Actor& actor) {
            if (name == nullptr || name[0] == '\0') {
                actors.push_back(actor);
            } else {
                const char* actorName = actor.GetName();
                if (exactMatch) {
                    if (std::strcmp(actorName, name) == 0) {
                        actors.push_back(actor);
                    }
                } else {
                    if (std::strstr(actorName, name) != nullptr) {
                        actors.push_back(actor);
                    }
                }
            }
        });
        return actors;
    }

    // Backwards-compatible alias for GetAllActors
    static std::vector<Actor> GetAll(const char* name = nullptr, bool exactMatch = false) {
        return GetAllActors(name, exactMatch);
    }

private:
    // Whether a pointer read off an actor is worth dereferencing. Matches the
    // range checks the Life accessors above already use for the same reason:
    // these are fields whose layout is inferred, so a wrong guess has to fail
    // as a null read rather than as a crash. (Unguarded because GetName /
    // IsValidActorName compile on every platform; the address band is
    // per-platform: Wii U MEM2 vs the Switch 39-bit address space.)
    static bool IsReadablePtr(const void* p) {
        uintptr_t v = reinterpret_cast<uintptr_t>(p);
#if WIIXL_SWITCH
        return v >= 0x8000000 && v < (uintptr_t(1) << 40);
#else
        return v >= 0x10000000 && v < 0xa0000000;
#endif
    }

#if !WIIXL_SWITCH

    // Whether a pointer pulled out of a BaseProcMgr job list is really a
    // BaseProc. IsReadablePtr alone is far too weak: it passed 0x62caf01e, a
    // misaligned pointer into a region that holds no actors, which then took a
    // 48-byte matrix write at +0x1F8 and +0x22C from SetPosition.
    //
    // Two checks, both grounded rather than guessed:
    //
    //  - Alignment. A BaseProc holds pointers and floats, so it is at least
    //    4-aligned; every one of the 522 good procs in that same sweep ended
    //    in 0, 4, 8 or C, and the bad one ended in E.
    //  - A vtable at +0xE8. Every BaseProc has one and the game dereferences
    //    it without checking - 0x0378a374 calls slot 0x5C through it and
    //    0x0378b60c calls slot 0xCC - so its absence means this is not a proc.
    //    The accepted band matches what the Life accessors above already use.
    static bool IsPlausibleProc(const void* p) {
        uintptr_t v = reinterpret_cast<uintptr_t>(p);
        if (v == 0 || (v & 3) != 0 || !IsReadablePtr(p)) return false;

        uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(
            static_cast<const uint8_t*>(p) + 0xe8);
        if ((vtable & 3) != 0) return false;
        return vtable >= 0x02000000 && vtable <= 0x10600000;
    }

    // Bit test rather than std::isfinite, so this cannot be affected by the
    // FP mode the game happens to be running in: exponent all ones is NaN or
    // infinity, everything else is finite.
    static bool IsFiniteFloat(float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    // Whether a matrix is usable as a transform at all.
    //
    // Not every actor keeps a real one. BotW's logic-gate actors - LinkTagOr,
    // LinkTagAnd - carry NaN in theirs, presumably because nothing ever
    // initialises a transform they do not use. Reading that, adding to it and
    // handing it back to setMtx writes NaN into +0x1F8, into +0x22C, and
    // through the attach-frame path at 0x034d3d40, which is how a NaN gets
    // into the matrix pipeline the physics and render sides both consume.
    //
    // This doubles as the discriminator that a type check would have given:
    // an object that does not hold a finite transform is not one to move,
    // whatever its class turns out to be.
    static bool IsFiniteMatrix(const float m[12]) {
        for (uint32_t i = 0; i < 12; ++i) {
            if (!IsFiniteFloat(m[i])) return false;
        }
        return true;
    }
#endif

    void* m_Ptr;
    Kind m_Kind = Kind::Proc;
};

} // namespace WiiXLaunch::BotW
