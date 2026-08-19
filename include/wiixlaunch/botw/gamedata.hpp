#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>

// WiiXLaunch::BotW::GameData - BotW's GameData flag system (ksys::gdt), the
// authoritative, save-backed store for player progression values.
//
// Why this is separate from Actor: values you would expect to live on the
// player actor are often only *cached* there. Max hearts is the clear case -
// Actor::getMaxLife() reads a cache at actor+0x1320 that nothing re-syncs, so
// writing it reverts within a frame and makes the HUD play the
// heart-container-removed animation. The flag is the real thing.
//
// Addresses are Wii U V208 and were derived, not guessed - see
// data/symbols-wiiu-v208.csv for the evidence behind each one.

namespace WiiXLaunch::BotW::GameData {

// Switch offsets were never RE'd for these, so this is Wii U/Cemu only. Calls
// are safe no-ops elsewhere rather than jumping to an address that means
// nothing on that platform.
static constexpr bool SupportsMaxLife = !WIIXL_SWITCH;

namespace impl {

// gdt::getFlag_MaxHartValue(bool debug) -> s32
constexpr uintptr_t kGetFlagMaxHartValueWiiU = 0x02e1a1e0;
// gdt::setFlag_MaxHartValue(s32 value, bool debug)
constexpr uintptr_t kSetFlagMaxHartValueWiiU = 0x02e1a1f0;

// ksys::act::PlayerInfo::setMaxHeartValue(PlayerInfo* this, s32 quarter_hearts).
// Preferred over the raw flag setter: it writes the flag *and* PlayerInfo's own
// cached copy (mMaxHeartValue, an f32 at +0x64), which is what the game itself
// does. Setting only the flag leaves that cache stale.
constexpr uintptr_t kSetMaxHeartValueWiiU = 0x02d4992c;

// The PlayerInfo singleton pointer. Two independent callers of
// setMaxHeartValue load `this` from here, both null-checking first.
constexpr uintptr_t kPlayerInfoInstancePtrWiiU = 0x10463f38;

using GetFlagS32Fn = int (*)(bool debug);
using SetFlagS32Fn = void (*)(int value, bool debug);
using SetMaxHeartValueFn = void (*)(void* playerInfo, int quarterHearts);

// Dereferences the singleton pointer, or null. Range-checked the same way the
// module's other raw reads are: a wrong guess must fail as null, not a crash.
inline void* PlayerInfo() {
#if !WIIXL_SWITCH
    void* inst = *reinterpret_cast<void**>(kPlayerInfoInstancePtrWiiU);
    uintptr_t addr = reinterpret_cast<uintptr_t>(inst);
    if (addr < 0x10000000 || addr > 0xa0000000) return nullptr;
    return inst;
#else
    return nullptr;
#endif
}

} // namespace impl

// Max life in raw units, 4 per heart - the same unit Actor::GetMaxLife()
// reports. (The flag is named "MaxHartValue" in the game's own data, typo and
// all, and holds quarter-hearts.) Returns 0 if unavailable.
inline int GetMaxLife() {
#if !WIIXL_SWITCH
    auto fn = WiiXLaunch::GetTargetFunction<impl::GetFlagS32Fn>(0x0, impl::kGetFlagMaxHartValueWiiU);
    if (!fn) return 0;
    return fn(false);
#else
    return 0;
#endif
}

// Sets max life for real, through the same call the game uses: the GameData
// flag (save-backed, authoritative) plus PlayerInfo's cached copy.
//
// Deliberately does NOT read the flag back to confirm. The flag write is not
// observable synchronously - an immediate re-read still returns the old value,
// and an earlier version of this function reported "refused, nothing changed"
// for writes that had in fact succeeded. A verify that races the thing it is
// verifying is worse than no verify: it produces confident, wrong answers.
// Observe the result with GetMaxLife() on a later frame instead.
//
// Returns true if the call was made.
//
// No range limit. The game does not impose one here either - gdt::setF32/setS32
// validate the handle and write-protection bits, never the value, and neither
// PlayerInfo::setMaxHeartValue nor setStaminaMax clamps. 30 hearts is where the
// HUD stops drawing, not where the data stops accepting. This writes persistent
// save data, so whatever you pass is what your playthrough gets.
inline bool SetMaxLife(int rawUnits) {
#if !WIIXL_SWITCH
    void* playerInfo = impl::PlayerInfo();
    if (!playerInfo) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetMaxHeartValueFn>(0x0, impl::kSetMaxHeartValueWiiU);
    if (!set) return false;

    set(playerInfo, rawUnits);
    return true;
#else
    (void)rawUnits;
    return false;
#endif
}

// Convenience in whole/fractional hearts, matching Actor's hearts helpers.
inline float GetMaxHearts() { return GetMaxLife() / 4.0f; }
inline bool SetMaxHearts(float hearts) {
    return SetMaxLife(static_cast<int>(hearts * 4.0f + 0.5f));
}

// ---------------------------------------------------------------------------
// Stamina
//
// Measured in "wheel units": 1000 = one full wheel, 3000 = the three-wheel cap.
//
// Mind the flag names, which are actively misleading:
//
//   StaminaCurrentMax  -> CURRENT stamina (PlayerInfo +0x68). Reads "current
//                         max", but it is the live value that drains as you
//                         sprint and regenerates when you stop. Confirmed by
//                         sampling it while running: 3000 -> 1882 -> 3000.
//   StaminaMax         -> MAX stamina (PlayerInfo +0x6c). Constant while the
//                         above moves. This is what stamina vessels raise.
//
// The player actor keeps its own copy of the MAX at +0x1324, right after max
// life. recoverStamina() is setStaminaCurrentMax(getMaxStaminaFromPlayerActor())
// - current = max - which is the exact analogue of recoverLife(), and is the
// clearest confirmation of which is which.
// ---------------------------------------------------------------------------

namespace impl {

// PlayerInfo::setStaminaCurrentMax(this, f32) - CURRENT stamina; flag + cache +0x68
constexpr uintptr_t kSetStaminaWiiU = 0x02d49d70;
// PlayerInfo::getStaminaCurrentMax(this) -> f32 - CURRENT stamina
constexpr uintptr_t kGetStaminaWiiU = 0x02d49dc0;
// PlayerInfo::setStaminaMax(this, f32) - MAX stamina; flag + cache +0x6c
constexpr uintptr_t kSetStaminaMaxWiiU = 0x02d49dfc;
// PlayerInfo::getStaminaMax(this) -> f32 - MAX stamina
constexpr uintptr_t kGetStaminaMaxWiiU = 0x02d49e4c;
// PlayerInfo::setMaxStaminaForPlayerActor(this, f32) -> actor+0x1324 (the max)
constexpr uintptr_t kSetActorMaxStaminaWiiU = 0x02d49e88;
// PlayerInfo::getMaxStaminaFromPlayerActor(this) -> f32
constexpr uintptr_t kGetActorMaxStaminaWiiU = 0x02d49e9c;

using GetStaminaFn = float (*)(void* playerInfo);
using SetStaminaFn = void (*)(void* playerInfo, float value);

inline float CallStaminaGetter(uintptr_t wiiuOffset) {
#if !WIIXL_SWITCH
    void* playerInfo = PlayerInfo();
    if (!playerInfo) return 0.0f;
    auto fn = WiiXLaunch::GetTargetFunction<GetStaminaFn>(0x0, wiiuOffset);
    if (!fn) return 0.0f;
    return fn(playerInfo);
#else
    (void)wiiuOffset;
    return 0.0f;
#endif
}

} // namespace impl

static constexpr bool SupportsStamina = !WIIXL_SWITCH;

constexpr float kStaminaPerWheel = 1000.0f;
// What a fully upgraded player has, and where the HUD stops drawing - NOT an
// enforced limit. Used only as a fallback when the real max cannot be read.
constexpr float kStaminaMaxWheels = 3.0f;
constexpr float kStaminaAbsoluteMax = kStaminaPerWheel * kStaminaMaxWheels;

// Live stamina, 0..max. Drains and regenerates as you play.
inline float GetStamina() { return impl::CallStaminaGetter(impl::kGetStaminaWiiU); }

// Max stamina - what stamina vessels raise.
inline float GetMaxStamina() { return impl::CallStaminaGetter(impl::kGetStaminaMaxWiiU); }

// The player actor's own copy of the max (+0x1324). Should match GetMaxStamina();
// exposed so a disagreement is visible rather than mysterious.
inline float GetActorMaxStamina() { return impl::CallStaminaGetter(impl::kGetActorMaxStaminaWiiU); }

// Sets live stamina. No range limit - values above max or below zero are passed
// through as given. The only rejection is NaN, which is not a range check but a
// guard against handing the game a non-value.
inline bool SetStamina(float wheelUnits) {
#if !WIIXL_SWITCH
    if (wheelUnits != wheelUnits) return false;       // NaN

    void* playerInfo = impl::PlayerInfo();
    if (!playerInfo) return false;
    auto fn = WiiXLaunch::GetTargetFunction<impl::SetStaminaFn>(0x0, impl::kSetStaminaWiiU);
    if (!fn) return false;

    fn(playerInfo, wheelUnits);
    return true;
#else
    (void)wheelUnits;
    return false;
#endif
}

// Restores stamina to full - what the game's own recoverStamina() does. Reads
// the actual max rather than assuming three wheels, which matters now that
// SetMaxStamina will accept anything.
inline bool RecoverStamina() {
    float max = GetMaxStamina();
    if (!(max > 0.0f)) max = kStaminaAbsoluteMax;
    return SetStamina(max);
}

// Sets max stamina in both places the game keeps it: the StaminaMax flag (which
// persists to the save) and the player actor's copy. Does NOT touch current
// stamina - that is a separate value, and conflating them is what the flag
// naming tempts you into.
//
// No range limit, same as SetMaxLife - the three-wheel cap is a HUD limit, not
// a data one. NaN is still rejected. The flag write is persistent.
inline bool SetMaxStamina(float wheelUnits) {
#if !WIIXL_SWITCH
    if (wheelUnits != wheelUnits) return false;       // NaN

    void* playerInfo = impl::PlayerInfo();
    if (!playerInfo) return false;

    auto setMax      = WiiXLaunch::GetTargetFunction<impl::SetStaminaFn>(0x0, impl::kSetStaminaMaxWiiU);
    auto setActorMax = WiiXLaunch::GetTargetFunction<impl::SetStaminaFn>(0x0, impl::kSetActorMaxStaminaWiiU);
    if (!setMax || !setActorMax) return false;

    setMax(playerInfo, wheelUnits);
    // Safe with no player actor loaded: the game's own function null-checks
    // PlayerInfo::mPlayerActor (+0x30) and returns.
    setActorMax(playerInfo, wheelUnits);
    return true;
#else
    (void)wheelUnits;
    return false;
#endif
}

// Convenience in wheels (1.0 = one full wheel).
inline float GetStaminaWheels() { return GetStamina() / kStaminaPerWheel; }
inline bool SetStaminaWheels(float wheels) { return SetStamina(wheels * kStaminaPerWheel); }
inline float GetMaxStaminaWheels() { return GetMaxStamina() / kStaminaPerWheel; }
inline bool SetMaxStaminaWheels(float wheels) { return SetMaxStamina(wheels * kStaminaPerWheel); }


// ---------------------------------------------------------------------------
// Rupees
//
// Rupees are not a pouch item and have nothing to do with PauseMenuDataMgr -
// they are a plain scalar GameData flag named "CurrentRupee", which is why the
// pouch walk never turns them up.
//
// Chain, the same one used for the other flags here: the string at 0x101fb72c
// -> name accessor 0x02e1eb90 (a function whose whole body is `return
// "CurrentRupee";`) -> the wrappers below.

static constexpr bool SupportsRupees = !WIIXL_SWITCH;

namespace impl {

// gdt::increaseFlag_CurrentRupee(s32 delta, bool debug). NOT a setter, despite
// the shape: it forwards to 0x02e147a4, which pushes {tag, nameHash, index,
// delta} onto the per-thread ring buffer at manager+0x71c, and that queue holds
// DELTAS. Proven by 0x03204a14, the rupee pickup path: its queued branch pushes
// the pickup amount through the identical call, and its immediate branch reads
// the flag and stores current + amount. Calling this with an absolute value
// adds that value to the wallet - which reads as the total doubling if the
// caller passes the current amount back in.
constexpr uintptr_t kIncreaseFlagCurrentRupeeWiiU = 0x02e17f58;

// gdt::setFlag_s32_byName(Manager*, s32 value, sead::SafeString* name). The
// genuine absolute write, taken from the immediate branch of 0x03204a14, which
// calls it with the already-summed total.
constexpr uintptr_t kSetFlagS32ByNameWiiU = 0x0320400c;

// The generic by-name s32 read. There is no dedicated CurrentRupee getter to
// pair with the setter; this is how the game's own can-afford check at
// 0x02a771bc reads the flag, and the argument shape below is taken from it
// verbatim rather than reconstructed.
constexpr uintptr_t kGetFlagS32ByNameWiiU = 0x0320fadc;

// ksys::gdt::Manager singleton pointer.
constexpr uintptr_t kGameDataManagerPtrWiiU = 0x1046d5b0;

// sead::SafeString's vtable. A stack SafeString is built as { const char*,
// vtable } - string pointer first, vtable second. That is the same inverted
// order as PouchItem::mName, and it is worth not re-learning the hard way.
constexpr uintptr_t kSeadSafeStringVtableWiiU = 0x10263910;

struct SafeString {
    const char* text;
    const void* vtable;
};

using IncreaseFlagCurrentRupeeFn = void (*)(int delta, bool tag);
using SetFlagS32ByNameFn = void (*)(void* manager, int value, const SafeString* name);
using GetFlagS32ByNameFn = int (*)(void* core, int* out, const SafeString* name,
                                   uint8_t flags, int one);

inline void* GameDataManager() {
#if !WIIXL_SWITCH
    void* mgr = *reinterpret_cast<void**>(kGameDataManagerPtrWiiU);
    uintptr_t addr = reinterpret_cast<uintptr_t>(mgr);
    if (addr < 0x10000000 || addr > 0xa0000000) return nullptr;
    return mgr;
#else
    return nullptr;
#endif
}

} // namespace impl

// Reads the CurrentRupee flag. False when there is no GameData manager yet (no
// save loaded) or the flag read itself fails, in which case out is untouched.
inline bool GetRupees(int& out) {
#if !WIIXL_SWITCH
    void* manager = impl::GameDataManager();
    if (!manager) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);

    // 0x02a771bc reads this as **(u32**)(mgr + 0x700): the word at mgr+0x700
    // points at a slot that in turn holds the object the read wants.
    uintptr_t* slot = *reinterpret_cast<uintptr_t**>(base + 0x700);
    if (!slot) return false;
    void* core = *reinterpret_cast<void**>(slot);
    if (!core) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::GetFlagS32ByNameFn>(
        0x0, impl::kGetFlagS32ByNameWiiU);
    if (!get) return false;

    impl::SafeString name = {
        "CurrentRupee",
        reinterpret_cast<const void*>(impl::kSeadSafeStringVtableWiiU),
    };

    int value = 0;
    if (get(core, &value, &name, *reinterpret_cast<uint8_t*>(base + 0x704), 1) == 0) {
        return false;
    }

    out = value;
    return true;
#else
    (void)out;
    return false;
#endif
}

// Adds to the wallet, the same operation a picked-up rupee performs. Negative
// deltas spend. This is the game's own primitive; SetRupees is layered on the
// absolute write instead.
inline bool AddRupees(int delta) {
#if !WIIXL_SWITCH
    auto add = WiiXLaunch::GetTargetFunction<impl::IncreaseFlagCurrentRupeeFn>(
        0x0, impl::kIncreaseFlagCurrentRupeeWiiU);
    if (!add) return false;

    add(delta, false);
    return true;
#else
    (void)delta;
    return false;
#endif
}

// Writes the CurrentRupee flag to an absolute value.
//
// No range limit, for the same reason the rest of this header has none: the
// game clamps nothing here, and 999999 is a wallet display limit rather than a
// storage one. This persists to your save.
inline bool SetRupees(int value) {
#if !WIIXL_SWITCH
    void* manager = impl::GameDataManager();
    if (!manager) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetFlagS32ByNameFn>(
        0x0, impl::kSetFlagS32ByNameWiiU);
    if (!set) return false;

    impl::SafeString name = {
        "CurrentRupee",
        reinterpret_cast<const void*>(impl::kSeadSafeStringVtableWiiU),
    };

    set(manager, value, &name);
    return true;
#else
    (void)value;
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::GameData
