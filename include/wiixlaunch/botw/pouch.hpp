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
    ArmorHead = 4,
    ArmorUpper = 5,
    ArmorLower = 6,
};

// The game's own setter refuses anything >= 4 (isPouchItemNotWeapon). Arrows
// are type 2 and so still go through it; the armour slots are read-only here.
inline bool IsWeaponSlot(Slot slot) { return static_cast<int>(slot) < 4; }

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


// PauseMenuDataMgr field offsets, all read straight out of 0x02eb67f4.
constexpr uintptr_t kItemListHead = 0x4c;   // sead::OffsetList head
constexpr uintptr_t kItemListFirst = 0x50;
constexpr uintptr_t kItemListNodeOffset = 0x58;

// PouchItem field offsets.
constexpr uintptr_t kItemType = 0x08;
constexpr uintptr_t kItemValue = 0x10;
constexpr uintptr_t kItemEquipped = 0x14;
constexpr uintptr_t kItemName = 0x18;       // char* at +0x00, vptr at +0x04

constexpr uintptr_t kCritSection = 0x10;
constexpr uintptr_t kIsPouchForQuest = 0x38128;

// ksys::gdt::setFlag_PorchItem_Value1(s32 value, s32 index, bool debug) and the
// sead crit-section pair the pouch functions lock with.
constexpr uintptr_t kSetFlagPorchItemValue1WiiU = 0x02e1a6f8;
constexpr uintptr_t kCritSectionLockWiiU = 0x030bb668;
constexpr uintptr_t kCritSectionUnlockWiiU = 0x030bb69c;

using SetFlagIndexedFn = void (*)(int value, int index, bool debug);
using CritSectionFn = void (*)(void* critSection);

// A hard stop on the walk. The pouch holds a few hundred entries at most, so
// anything past this is a corrupt list rather than a long one, and spinning
// forever here would hang the game thread the HTTP pump runs on.
constexpr int kMaxItemsWalked = 2048;

inline bool PlausiblePointer(uintptr_t addr) {
    return addr >= 0x10000000 && addr < 0xa0000000;
}

// The equipped entry of a given type, or null. Same traversal the game does -
// start at +0x50, stop when the node comes back around to the +0x4c sentinel,
// step through the link at nodeOffset+4.
//
// Deliberately does NOT take the crit section at this+0x10. Every caller runs
// on the game thread out of the frame hook, the fields read here are single
// aligned words that cannot tear, and taking a lock the game also takes from
// inside a request handler is a stall risk with nothing to buy it.
inline uintptr_t FindEquipped(int type, int* outIndex = nullptr) {
#if !WIIXL_SWITCH
    if (outIndex) *outIndex = -1;
    void* manager = PauseMenuDataMgr();
    if (!manager) return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    int32_t nodeOffset = *reinterpret_cast<int32_t*>(base + kItemListNodeOffset);
    uintptr_t sentinel = base + kItemListHead - nodeOffset;
    uintptr_t node = *reinterpret_cast<uintptr_t*>(base + kItemListFirst) - nodeOffset;

    for (int walked = 0; node != sentinel && walked < kMaxItemsWalked; ++walked) {
        if (!PlausiblePointer(node)) return 0;
        if (*reinterpret_cast<uint8_t*>(node + kItemEquipped) != 0 &&
            *reinterpret_cast<int32_t*>(node + kItemType) == type) {
            if (outIndex) *outIndex = walked;
            return node;
        }
        node = *reinterpret_cast<uintptr_t*>(node + nodeOffset + 4) - nodeOffset;
    }
#else
    (void)type; (void)outIndex;
#endif
    return 0;
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

    // Armour is deliberately not writable. The write itself is easy - the game
    // only blocks it with a type check, and mirroring mValue into
    // PorchItem_Value1 by hand works - but there is nothing worth writing to.
    // An armour entry's value is consumed by nothing: loadFromGameData restores
    // it generically and saveToGameData writes it back with the +1 bump meant
    // for weapon durability, so it ratchets up by one per save/load cycle and
    // means nothing. Measured live: an equipped Armor_171_Head, which can be
    // neither dyed nor upgraded, drifted -1 -> 2 across three reloads. See the
    // 0x02eb3764 row in symbols-wiiu-v208.csv.
    if (!IsWeaponSlot(slot)) return false;

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


// The equipped entry's value: durability for Sword/Bow/Shield, a count for
// Arrow. Reads PouchItem::mValue, which is the copy the menu draws from - so
// unlike the weapon actor's current life it reflects what the player sees.
//
// Returns false when nothing of that type is equipped, which is the useful
// distinction SetEquippedValue cannot make (the game's setter silently does
// nothing on an empty slot).
inline bool GetEquippedValue(Slot slot, int& out) {
#if !WIIXL_SWITCH
    uintptr_t item = impl::FindEquipped(static_cast<int>(slot));
    if (!item) return false;
    out = *reinterpret_cast<int32_t*>(item + impl::kItemValue);
    return true;
#else
    (void)slot; (void)out;
    return false;
#endif
}

// The equipped entry's actor name, e.g. "Item_Arrow_001", or null.
//
// The name object sits at item+0x18 - both 0x02eb7a70 and 0x02eb7988 call
// 0x03084748(item + 0x18) and 0x0308812c(item + 0x18). 0x0308812c is what pins
// the layout down: it does `(**(code **)(param_1[1] + 0x1c))(param_1)` and then
// `lookup(mgr, *param_1)`, so word 1 is the vptr and word 0 is the string
// pointer handed to the actor-info lookup. The char* is therefore at +0x18
// itself, not at +0x1c where a plain sead::SafeString would put it.
//
// Confirmed at runtime against the equipped arrow. Still validates the pointer
// and the first character, since a stale or partially built entry would
// otherwise hand back garbage.
inline const char* GetEquippedName(Slot slot) {
#if !WIIXL_SWITCH
    uintptr_t item = impl::FindEquipped(static_cast<int>(slot));
    if (!item) return nullptr;

    uintptr_t text = *reinterpret_cast<uintptr_t*>(item + impl::kItemName);
    if (!impl::PlausiblePointer(text)) return nullptr;

    const char* name = reinterpret_cast<const char*>(text);
    if (name[0] < 0x20 || name[0] > 0x7e) return nullptr;
    return name;
#else
    (void)slot;
    return nullptr;
#endif
}

} // namespace WiiXLaunch::BotW::Pouch
