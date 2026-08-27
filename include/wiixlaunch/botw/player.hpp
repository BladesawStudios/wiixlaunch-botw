#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/call.hpp>
#include "actor.hpp"

// WiiXLaunch::BotW::Player - ksys::act::Player state, ported from the
// "Actor Spawning and Weapon Detection" mod's PlayerTickHook/CheckForAttack.
// Position and attack-swing tracking are only confirmed on Wii U/Cemu (see
// SupportsPosition/SupportsAttackTracking) - Switch equivalents were never
// RE'd (see handwritten-symbols-botw.csv). The equipped sword/shield/bow
// getters are confirmed on both platforms.

namespace WiiXLaunch::BotW {

namespace impl {

using GetEquippedWeaponFn = void* (*)(void* player);

inline void*& RawPlayerRef() {
    static void* p = nullptr;
    return p;
}

using TickCallback = void (*)();

inline TickCallback& TickCallbackRef() {
    static TickCallback cb = nullptr;
    return cb;
}

#if !WIIXL_SWITCH

// Position offsets come from actor.hpp - kPosXOffset/kPosYOffset/kPosZOffset
// are the translation column of the actor's sead::Matrix34f at +0x1F8, which
// is why they are 0x10 apart rather than 4. They used to be redeclared here.

// Attack counter at +0x4d8 (increments by 1 per swing).
constexpr uint32_t kAttackCounterOffset = 0x4d8;

// --- moving the player ----------------------------------------------------
//
// docs/actor-transforms.md is emphatic that nothing written on the ACTOR
// sticks: setMtx, the three position copies, the capsule centre at phys+0x1F0
// and the linear velocity were all measured being restored to the
// bit-identical prior value on the next tick. The authoritative position lives
// on the physics object the +0xE8 vtable hands out at slot 0x30C.
//
// What is new here is WHERE on that object. The doc only ever wrote +0x1F0.
// 0x0383b398 - the telemetry pass that computes how fast Link is moving -
// fetches the physics object through slot 0x30C and reads a Vector3f at +0xAC
// and another at +0xC4, differencing them to get a per-frame delta. So +0xAC
// is the current position and +0xC4 the previous one, and neither has been
// tried as a write target.
//
// UNVERIFIED. +0xAC may itself be an output of the Havok body hanging off
// phys+0x14, in which case writing it reverts like everything else. Finding a
// real setter would mean going through that body, and it was not found in this
// pass. Hence the hold below: if a single write loses to the sync, re-applying
// every frame is the next best thing, and it is what SetPosition arms by
// default.
constexpr uint32_t kPhysObjectVtableSlot = 0x30c;
constexpr uint32_t kPhysPositionOffset = 0xac;
constexpr uint32_t kPhysPrevPositionOffset = 0xc4;

using GetPhysObjectFn = void* (*)(void* actor);

inline float* HeldPosition() { static float p[3] = {}; return p; }
inline int& HeldFrames() { static int f = 0; return f; }

inline bool PlausibleActorPtr(const void* p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000000 && v < 0xf0000000;
}

// The physics object, or null for an actor physics does not drive.
inline void* PhysObject(void* actor) {
    if (!PlausibleActorPtr(actor)) return nullptr;

    uintptr_t vtable = *reinterpret_cast<uintptr_t*>(
        reinterpret_cast<uintptr_t>(actor) + 0xe8);
    if (!PlausibleActorPtr(reinterpret_cast<void*>(vtable))) return nullptr;

    auto get = *reinterpret_cast<GetPhysObjectFn*>(vtable + kPhysObjectVtableSlot);
    if (!get) return nullptr;

    void* obj = get(actor);
    return PlausibleActorPtr(obj) ? obj : nullptr;
}

// Writes the physics-side position at +0xAC.
//
// DOES NOT MOVE THE PLAYER, and is kept only so the finding is not lost. It was
// SetPosition's implementation until it was measured: the write lands, and
// Actor::updateMtxFromPhysics republishes the matrix from the character
// controller on the next frame, so the player is back before anything draws.
// Re-applying it every frame does not help either - a 300-frame hold that was
// still running reverted just the same, because the sync runs after the tick
// hook. +0xAC is a mirror of the Havok body, not an input to it.
//
// Use Actor::SetMtx / Actor::WarpTo. Nothing in this file calls this any more.
//
// It writes both the current and the previous copy, so the delta 0x0383b398
// computes comes out as zero rather than as one enormous frame of velocity.
inline bool WritePhysPosition(void* actor, float x, float y, float z) {
    void* obj = PhysObject(actor);
    if (!obj) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(obj);
    float* current = reinterpret_cast<float*>(base + kPhysPositionOffset);
    float* previous = reinterpret_cast<float*>(base + kPhysPrevPositionOffset);

    current[0] = x;  current[1] = y;  current[2] = z;
    previous[0] = x; previous[1] = y; previous[2] = z;
    return true;
}

inline float* PositionCache() {
    static float pos[3] = {};
    return pos;
}

inline uint32_t& LastAttackCount() {
    static uint32_t v = 0;
    return v;
}

inline bool& AttackTrackingInitialized() {
    static bool v = false;
    return v;
}

inline bool& AttackEventPending() {
    static bool v = false;
    return v;
}

#endif // !WIIXL_SWITCH

// Poll equipped items every frame (game never calls these getters itself).
WIIXL_HOOK_DEFINE_TRAMPOLINE(PlayerTickHook) {
    static void Callback(void* player) {
        Orig(player);
        RawPlayerRef() = player;

#if !WIIXL_SWITCH
        float* pos = PositionCache();
        pos[0] = *reinterpret_cast<float*>(static_cast<uint8_t*>(player) + kPosXOffset);
        pos[1] = *reinterpret_cast<float*>(static_cast<uint8_t*>(player) + kPosYOffset);
        pos[2] = *reinterpret_cast<float*>(static_cast<uint8_t*>(player) + kPosZOffset);

        uint32_t count = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(player) + kAttackCounterOffset);
        if (!AttackTrackingInitialized()) {
            LastAttackCount() = count;
            AttackTrackingInitialized() = true;
        } else if (count != LastAttackCount()) {
            LastAttackCount() = count;
            AttackEventPending() = true;
        }

        // Re-apply a held position before the user callback, so a mod polling
        // GetPosition in its own OnTick sees the held value rather than
        // whatever physics last wrote.
        if (HeldFrames() > 0) {
            float* held = HeldPosition();
            // Through setMtx, the same as SetPosition - re-applying the old
            // physics-mirror write would pin nothing.
            Actor(player).WarpTo(held[0], held[1], held[2]);
            --HeldFrames();
        }
#endif

        if (TickCallback cb = TickCallbackRef()) cb();
    }
};

} // namespace impl

class Player {
public:
    // Installs the per-frame tick hook. Call once from WiiXLaunch_Init(),
    // before relying on any other Player method.
    static void Init() {
        impl::PlayerTickHook::Install(0x873374, 0x02d67cf4);
    }

    // Escape hatch to the raw ksys::act::Player* (GameROMPlayer).
    static void* GetRaw() {
#if !WIIXL_SWITCH
        auto isValidPtr = [](const void* p) {
            uintptr_t v = reinterpret_cast<uintptr_t>(p);
            return v >= 0x10000000 && v < 0xa0000000;
        };

        uint8_t** ppPlayerTracker = reinterpret_cast<uint8_t**>(0x10463f38);
        if (ppPlayerTracker && isValidPtr(*ppPlayerTracker)) {
            uint8_t* tracker = *ppPlayerTracker;
            void* linkMgr = *reinterpret_cast<void**>(tracker + 0x34);
            uint32_t procId = *reinterpret_cast<uint32_t*>(tracker + 0x38);
            uint8_t flag = *reinterpret_cast<uint8_t*>(tracker + 0x3c);

            if (linkMgr && isValidPtr(linkMgr)) {
                using GetProcFn = void* (*)(void* mgr, uint32_t id, uint8_t flag);
                auto getProc = WiiXLaunch::GetTargetFunction<GetProcFn>(0x0, 0x0378d8dc);
                void* playerActor = getProc(linkMgr, procId, flag);
                if (playerActor && isValidPtr(playerActor)) return playerActor;
            }
        }
#endif
        return impl::RawPlayerRef();
    }

    // Registers a callback run once per frame, right after Player's own
    // cached state (weapon getters, position, attack tracking) has been
    // refreshed for that frame - the place to put per-frame mod logic
    // (e.g. checking ConsumeAttackEvent()) without installing your own tick
    // hook. Only one callback slot; call again to replace it.
    static void OnTick(impl::TickCallback callback) { impl::TickCallbackRef() = callback; }

    static Actor GetEquippedSword() {
        void* player = impl::RawPlayerRef();
        if (!player) return Actor();
        auto getSword = WiiXLaunch::GetTargetFunction<impl::GetEquippedWeaponFn>(0x86e908, 0x02d58914);
        return Actor(getSword(player));
    }

    static Actor GetEquippedShield() {
        void* player = impl::RawPlayerRef();
        if (!player) return Actor();
        auto getShield = WiiXLaunch::GetTargetFunction<impl::GetEquippedWeaponFn>(0x86ea6c, 0x02d589d0);
        return Actor(getShield(player));
    }

    static Actor GetEquippedBow() {
        void* player = impl::RawPlayerRef();
        if (!player) return Actor();
        auto getBow = WiiXLaunch::GetTargetFunction<impl::GetEquippedWeaponFn>(0x86ebd0, 0x02d58a8c);
        return Actor(getBow(player));
    }

    // Player position is only confirmed on Wii U/Cemu - see kPosXOffset etc.
    // above; no Switch offset was ever RE'd. GetPosition() returns false on
    // Switch instead of reading an unconfirmed offset.
    static constexpr bool SupportsPosition = !WIIXL_SWITCH;

    static bool GetPosition(float& x, float& y, float& z) {
#if WIIXL_SWITCH
        (void)x; (void)y; (void)z;
        return false;
#else
        if (impl::RawPlayerRef()) {
            float* pos = impl::PositionCache();
            x = pos[0]; y = pos[1]; z = pos[2];
            return true;
        }

        // Cache is cold - Init() has not run, or no frame has passed since it
        // did. Resolve the player directly and read the live matrix instead of
        // refusing. GetRaw does not depend on the tick hook, so this works for
        // a mod that never calls Init at all.
        auto* base = static_cast<const uint8_t*>(GetRaw());
        if (!base) return false;

        uintptr_t addr = reinterpret_cast<uintptr_t>(base);
        if (addr < 0x10000000 || addr >= 0xf0000000) return false;

        x = *reinterpret_cast<const float*>(base + impl::kPosXOffset);
        y = *reinterpret_cast<const float*>(base + impl::kPosYOffset);
        z = *reinterpret_cast<const float*>(base + impl::kPosZOffset);
        return true;
#endif
    }

    // Moves the player.
    //
    // Goes through ksys::act::Actor::setMtx (virtual index 85) - the path every
    // Warp* action in the game uses, with uking::action::WarpToPos::oneShot_ as
    // the reference. See Actor::SetMtx.
    //
    // WHY THE EARLIER ATTEMPTS DID NOT WORK, since this file used to say the
    // real setter was unfound: writing the actor matrix, the position fields or
    // the physics position mirror at +0xAC were all reverted within a frame,
    // and the cause is Actor::updateMtxFromPhysics (virtual index 84), which
    // runs every frame and does
    //
    //     if (physics && characterController)  mMtx <- characterController
    //     else if (mainBody)                   mMtx <- mainBody
    //     else if (physicsMtx && flag)         mMtx <- physicsMtx
    //
    // Link has a character controller, so his matrix is republished from it
    // every frame; the third branch - the one a mirror write aims at - is only
    // reachable for an actor with no body at all. setMtx works because it
    // writes the controller itself.
    //
    // Confirmed moving the player on Wii U V208 under Cemu: a 50-unit move in
    // X and Z held to within 0.15 and stayed there. Judge a teleport on X and Z
    // and never on Y - teleport straight up and the player falls back to the
    // same ground, which is indistinguishable from a failed write.
    //
    // holdFrames re-applies the move from the tick hook for that many frames.
    // It defaults to 0 now: it existed to fight the physics sync, and the
    // correct call does not need to. Pass a positive count to PIN the player in
    // place - that is now its only real use - and note it needs Init() to have
    // run, since it rides the tick hook.
    //
    // Returns whether the call was made.
    static bool SetPosition(float x, float y, float z, int holdFrames = 0) {
#if WIIXL_SWITCH
        (void)x; (void)y; (void)z; (void)holdFrames;
        return false;
#else
        void* player = GetRaw();
        if (!player) return false;

        if (!Actor(player).WarpTo(x, y, z)) return false;

        if (holdFrames > 0) {
            float* held = impl::HeldPosition();
            held[0] = x; held[1] = y; held[2] = z;
            impl::HeldFrames() = holdFrames;
        }
        return true;
#endif
    }

    // SetPosition for corrections applied every frame.
    //
    // Goes through Actor::NudgeTo, which skips setMtx's physics-instance
    // reset. SetPosition is a warp and resets that set every call - fine once,
    // ruinous at sixty times a second: a region pushback built on it dragged a
    // swimming player under, because the swim state was being reset each frame.
    //
    // No hold option on purpose. A hold re-applies the position for N frames,
    // which is the same repetition problem wearing a different hat.
    static bool NudgePosition(float x, float y, float z) {
#if WIIXL_SWITCH
        (void)x; (void)y; (void)z;
        return false;
#else
        void* player = GetRaw();
        if (!player) return false;
        return Actor(player).NudgeTo(x, y, z);
#endif
    }

    // Drops a hold early.
    static void ClearPositionHold() {
#if !WIIXL_SWITCH
        impl::HeldFrames() = 0;
#endif
    }

    // Whether a hold is still re-applying.
    static bool IsPositionHeld() {
#if WIIXL_SWITCH
        return false;
#else
        return impl::HeldFrames() > 0;
#endif
    }

    // The player's physics object, for anything not wrapped here. Null when
    // physics does not drive the actor.
    static void* GetPhysicsObject() {
#if WIIXL_SWITCH
        return nullptr;
#else
        return impl::PhysObject(GetRaw());
#endif
    }

    // Attack-swing tracking is only confirmed on Wii U/Cemu - see
    // kAttackCounterOffset above. ConsumeAttackEvent() always returns false
    // on Switch.
    static constexpr bool SupportsAttackTracking = !WIIXL_SWITCH;

    // Edge-triggered: returns true exactly once per swing, then false until
    // the next one. Call at most once per frame per consumer.
    static bool ConsumeAttackEvent() {
#if WIIXL_SWITCH
        return false;
#else
        if (!impl::AttackEventPending()) return false;
        impl::AttackEventPending() = false;
        return true;
#endif
    }
};

} // namespace WiiXLaunch::BotW
