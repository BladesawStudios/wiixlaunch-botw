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
// Returns true if the call was made. Bogus values are refused rather than
// committed, since this writes persistent player progression - 4..120 raw units
// is 1..30 hearts, the game's own normal range.
inline bool SetMaxLife(int rawUnits) {
#if !WIIXL_SWITCH
    if (rawUnits < 4 || rawUnits > 120) return false;

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

} // namespace WiiXLaunch::BotW::GameData
