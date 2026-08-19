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
        if (!impl::RawPlayerRef()) return false;
        float* pos = impl::PositionCache();
        x = pos[0]; y = pos[1]; z = pos[2];
        return true;
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
