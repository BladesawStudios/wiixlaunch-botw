#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>

// WiiXLaunch::BotW::Pouch - the inventory side of equipped-item state
// (ksys::ui::PauseMenuDataMgr).
//
// Why this is separate from Actor: a weapon's durability lives in THREE places,
// and writing the actor only covers one of them.
//
//   the weapon actor's current life  - the working value combat reads
//   PouchItem::mValue                - what the menu and the damage indicators read
//   GameData flag PorchItem_Value1[] - the save-backed copy
//
// Set only the actor and the number changes for gameplay purposes while the UI
// keeps showing stale state: the full-durability sparkle stays on, and the
// weapon never reports as badly damaged. That is the same behaviour people hit
// doing durability-transfer glitches, and it is not a bug in the write - it is
// two other holders that were never told.
//
// PauseMenuDataMgr::setEquippedWeaponItemValue updates the pouch entry and the
// flag together, which is what makes the display agree.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv.

namespace WiiXLaunch::BotW::Pouch {

// ksys::ui::PouchItemType. Only the weapon types matter here; the game's own
// function early-outs on anything >= 4.
enum class Slot : int {
    Sword = 0,
    Bow = 1,
    Arrow = 2,
    Shield = 3,
};

static constexpr bool SupportsEquippedValue = !WIIXL_SWITCH;

namespace impl {

// ksys::ui::PauseMenuDataMgr::setEquippedWeaponItemValue(this, s32 value, PouchItemType type)
constexpr uintptr_t kSetEquippedWeaponItemValueWiiU = 0x02eb67f4;

// The PauseMenuDataMgr singleton pointer, loaded by the one caller as
// `lis r3,0x1047; lwz r3,-0x6688(r3)`.
constexpr uintptr_t kPauseMenuDataMgrInstancePtrWiiU = 0x10469978;

using SetEquippedWeaponItemValueFn = void (*)(void* pauseMenuDataMgr, int value, int type);

inline void* PauseMenuDataMgr() {
#if !WIIXL_SWITCH
    void* instance = *reinterpret_cast<void**>(kPauseMenuDataMgrInstancePtrWiiU);
    uintptr_t addr = reinterpret_cast<uintptr_t>(instance);
    if (addr < 0x10000000 || addr > 0xa0000000) return nullptr;
    return instance;
#else
    return nullptr;
#endif
}

} // namespace impl

// Writes the equipped item's pouch value and its save flag, through the game's
// own setter - so the menu, the sparkle and the badly-damaged indicator all
// update instead of lagging behind the actor.
//
// Does NOT touch the weapon actor's current life; that is Actor::SetCurrentLife
// and a caller wanting the number to change everywhere should do both.
//
// Returns false only if the singleton is unavailable (no save loaded). The
// game's own function silently does nothing when the slot is empty, so a true
// return means the call was made, not that something was equipped.
inline bool SetEquippedValue(Slot slot, int value) {
#if !WIIXL_SWITCH
    void* manager = impl::PauseMenuDataMgr();
    if (!manager) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetEquippedWeaponItemValueFn>(
        0x0, impl::kSetEquippedWeaponItemValueWiiU);
    if (!set) return false;

    set(manager, value, static_cast<int>(slot));
    return true;
#else
    (void)slot; (void)value;
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::Pouch
