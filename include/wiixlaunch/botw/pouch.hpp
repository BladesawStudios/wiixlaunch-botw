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
    Material = 7,
    Food = 8,
    KeyItem = 9,
};

// 7/8/9 are never equipped, so nothing keyed off the equipped byte will find
// them - they are reachable only by enumerating the pouch. See ForEachOfType.
inline bool IsEquippableSlot(Slot slot) { return static_cast<int>(slot) < 7; }

// Whether an entry's value field means anything. It does for every type except
// armour: durability for weapons, a count for arrows, materials and key items.
// Armour entries carry a number too, but nothing reads it and the save path
// ratchets it upward once per save/load cycle - see SetEquippedValue. Callers
// should omit it rather than report a figure that invites a meaning.
inline bool HasMeaningfulValue(Slot slot) {
    return slot != Slot::ArmorHead && slot != Slot::ArmorUpper && slot != Slot::ArmorLower;
}

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

// Weapon modifier, from 0x02eb0a1c: the bitmask lands at item+0x6c and its
// magnitude at item+0x64, and only for types 0-3. Those two words are the same
// union food uses for cook data, which is why the game gates on type here - a
// meal and a sword cannot both be using it.
constexpr uintptr_t kItemModifierFlags = 0x6c;
constexpr uintptr_t kItemModifierValue = 0x64;

// Cook data, type 8 only - the same words the modifier uses, read differently.
//
// Which flag feeds which word is traced from loadFromGameData (0x02eb4518) and
// the handles those readers use:
//
//   +0x64, +0x68   StaminaRecover  (vector2f, both components stored as ints)
//                  via 0x02e1ac8c, handle 0x1046795c
//   +0x6c          CookEffect1     (first component, as an int)
//                  via 0x02e17d68, handle 0x10464770
//   +0x70, +0x74   CookEffect0     (vector2f, kept as floats)
//                  via 0x02e179c8, handle 0x1046476c
//   +0x78, +0x80   ingredient count, then up to 5 names written through the
//                  array at +0x80 from CookMaterialName0-4
//
// Meanings below are CONFIRMED against known meals, except where noted.
constexpr uintptr_t kItemCookHealth = 0x64;       // quarter-hearts: 20 = 5 hearts
constexpr uintptr_t kItemCookDuration = 0x68;     // seconds: 250 = 4:10
constexpr uintptr_t kItemCookSellPrice = 0x6c;    // rupees, unverified
constexpr uintptr_t kItemCookEffectId = 0x70;     // float; -1 none, 10 attack, 13 speed
constexpr uintptr_t kItemCookEffectLevel = 0x74;  // float tier: 2 = "attack up 2"

// NOT the ingredient count - it reads 5 on every item regardless of how many
// ingredients went in, so it is the capacity of the name array at +0x80, which
// is what loadFromGameData bounds its five reads against. The ingredients
// themselves are the CookMaterialName0-4 entries in that array.
constexpr uintptr_t kItemCookNameSlots = 0x78;

// Pointer to the ingredient-name array. loadFromGameData writes five entries
// through it as **(int**)(item + 0x80) and *(*(int**)(item+0x80) + 4) etc, so
// it is an array of pointers to name objects - each laid out { char* text;
// vtable } exactly like PouchItem::mName, pointer first.
constexpr uintptr_t kItemCookNames = 0x80;
constexpr int kItemCookNameCount = 5;

// Food effect ids, as stored in the float at item+0x70.
//
// AttackUp and AllSpeed are confirmed against dishes of known effect, and
// LifeMaxUp is all but certain - a dish reading id 2 had level 4 and a duration
// of ZERO, and hearty meals are exactly the ones with no timer. DefenseUp was
// seen on a plausible dish. The remainder are the mapping in common use and are
// NOT verified here; the API takes the raw number so a wrong name costs
// nothing.
//
// Note the id is NOT the CEI index from Cooking/CookData.sbyml - AttackUp is
// CEI entry 8 but effect id 10. CookData's System.CEI does give the real per
// effect tuning, which is what to respect when building a believable dish:
// LifeRecover cap 120, LifeMaxUp 4-108, the resistances 1-2 or 1-3 with a
// 120-second base, AllSpeed/AttackUp/DefenseUp 1-3 with 20-30 second bases,
// Quietness 1-3 at 90, Fireproof 1-2 at 120.
// There are exactly TWELVE cook effects - the full set, from System.CEI in
// Cooking/CookData.sbyml with every name hash resolved. CEI also gives each
// one's tuning, which is what to respect for a believable dish:
//
//   effect            base time  mult  tier range  crit add
//   StaminaRecover        0      1.40     1-15         2
//   GutsPerformance       0      0.50     1-20         2
//   LifeRecover           0      2.00     1-120       12
//   LifeMaxUp             0      1.00     4-108        4
//   ResistHot           120      0.35     1-2          1
//   ResistCold          120      0.35     1-2          1
//   ResistElectric      120      0.50     1-3          1
//   AllSpeed             30      0.45     1-3          1
//   AttackUp             20      0.45     1-3          1
//   DefenseUp            20      0.45     1-3          1
//   Quietness            90      0.35     1-3          1
//   Fireproof           120      0.30     1-2          1
//
// The CEI index is NOT the effect id. Every id below is CONFIRMED in game, by
// writing each one onto a dish and reading what the menu showed.
//
// 3, 7, 8 and 9 are genuinely unused: setting one gives a timed effect with no
// icon and no name, rather than falling back to anything. That the gaps behave
// like gaps is good evidence the rest of the mapping is exactly right.
enum CookEffectId : int {
    CookEffectNone = -1,
    CookLifeRecover = 1,
    CookLifeMaxUp = 2,        // hearty - no duration
    CookResistHot = 4,
    CookResistCold = 5,
    CookResistElectric = 6,
    CookAttackUp = 10,
    CookDefenseUp = 11,
    CookQuietness = 12,       // stealth
    CookAllSpeed = 13,
    CookStaminaRecover = 14,  // CEI's name; "GutsRecover" elsewhere
    CookGutsPerformance = 15, // extra stamina wheel
    CookFireproof = 16,
};

constexpr uintptr_t kCritSection = 0x10;
constexpr uintptr_t kIsPouchForQuest = 0x38128;

// ksys::gdt::setFlag_PorchItem_Value1(s32 value, s32 index, bool debug) and the
// sead crit-section pair the pouch functions lock with.
constexpr uintptr_t kSetFlagPorchItemValue1WiiU = 0x02e1a6f8;

// gdt::setFlag_PorchItem_EquipFlag(bool value, int index, bool debug). Paired
// with the value setter above; 0x02eb7988 clears both when it drops an item.
constexpr uintptr_t kSetFlagPorchItemEquipWiiU = 0x02e1a6c8;

// ksys::ui::getPouchItemType(sead::SafeString* name, int) -> PouchItemType, or
// -1 when the name is not a pouch item at all.
constexpr uintptr_t kGetPouchItemTypeWiiU = 0x02eae914;

// PauseMenuDataMgr::registerEquipped(mgr, PouchItem*). NOT CALLED - see below.
//
// It appends the item to the 5-entry equipped cache at mgr+0x37ea8 (8 bytes
// each: PouchItem* at +0, flags at +4 and +5), or flags the existing entry when
// the item is already cached, then calls 0x02a116f0 with the item's name and
// the object at *(0x10463f6c + 0x10).
//
// That trailing call was read as the pouch -> world equip link. It is not.
// Tested live: calling this and then opening and closing the menu SPAWNS the
// weapon as an actor in the world rather than swapping what Link is holding, so
// 0x02a116f0 creates an actor from a name. The call is left out for that
// reason; the constant stays because the cache mechanics it documents are real
// and it is the wrong turn worth not repeating.
//
// What it did establish: the cache IS consumed when the menu closes. So the
// menu-close path is the consumer to understand, and a correctly formed cache
// entry is probably part of a real equip - just not sufficient on its own, and
// not with this function's side effect attached.
constexpr uintptr_t kRegisterEquippedWiiU = 0x02eba358;

// ksys::act::Player equipment-REMOVED notify: (player, sead::SafeString* name).
// NOT CALLED - see below.
//
// Resolves the named actor's profile and raises a per-category flag on the
// player, each behind its own lock: WeaponSmallSword / LargeSword / Spear ->
// +0xf00 (lock +0xec4), WeaponShield -> +0xf40 (lock +0xf04), WeaponBow ->
// +0xf80 (lock +0xf44).
//
// Read as "re-read your equipment". It is not - it means "the thing you are
// holding is gone". Its only caller is trashItem, which fires it when the item
// being destroyed happens to be equipped. Tested live: calling it made the
// player DROP the held weapon as a world actor while the pouch entry survived,
// duplicating the item. Kept documented so the mistake is not repeated.
constexpr uintptr_t kNotifyEquipRemovedWiiU = 0x02d36a10;
constexpr uintptr_t kGetPlayerForEquipWiiU = 0x02d497c4;
constexpr uintptr_t kPlayerHolderPtrWiiU = 0x10463f38;

// PauseMenuDataMgr::rebuildEquippedArray(mgr). Zeroes the four entries at
// manager+0x3812c, then walks the pouch and files every item with type < 4 and
// the equipped byte set into its slot by type. A pure recompute from the same
// +0x14 flags EquipItem writes, so it cannot spawn or drop anything - it just
// makes the game's own equipped array agree with the pouch.
//
// The only site in the whole executable that builds manager+0x3812c.
constexpr uintptr_t kRebuildEquippedWiiU = 0x02ebf690;

// requestEquipmentActor(weaponMgr, slot, sead::SafeString* name, int value,
//                       sead::SafeString* debugTag)
//
// Asks the equipment manager to load the actor for one slot. Each slot owns a
// 0x5c-byte record at weaponMgr+0x4c; this fills one in, and once the record
// reaches state 2 the player's equipment update takes the actor with
// 0x02a157c8 and attaches it, clearing the record.
//
// Copied from PauseMenuDataMgr 0x02eb8180, which calls
// FUN_02a15e44(DAT_10463708, 3, item + 0x18, *(int*)(item + 0x10), &tag) when
// restoring a head-armour actor. The tag is only a debug label; the game passes
// the literal "PauseMenuDataMgr".
//
// NOTE the player's update (0x02d6f224) takes and attaches UNCONDITIONALLY on
// every pass - the dirty flags at player+0xf00/0xf40/0xf80 only drive the
// DETACH half. That is why setting a flag dropped the held weapon and attached
// nothing: the detach ran with no actor requested. Requesting the actor is the
// whole job; the flag must stay untouched.
constexpr uintptr_t kRequestEquipActorWiiU = 0x02a15e44;
constexpr uintptr_t kEquipmentMgrPtrWiiU = 0x10463708;

// ksys::act::Player equipment update. The game runs it on menu close, which is
// why an equip only became visible after pausing and unpausing. Calling it
// ourselves applies the swap without touching the menu.
constexpr uintptr_t kPlayerEquipUpdateWiiU = 0x02d6f224;

// Slot record geometry, from 0x02a157c8: records start at equipmentMgr+0x4c and
// are 0x17 words (0x5c bytes) each, with the state word at index 0x13. State 2
// means the actor has finished loading and is ready to be taken.
constexpr uintptr_t kEquipRecordsBase = 0x4c;
constexpr uintptr_t kEquipRecordStride = 0x5c;
constexpr uintptr_t kEquipRecordState = 0x4c;
constexpr int kEquipRecordReady = 2;

// Frames to wait for the actor before giving up. The request is asynchronous,
// so firing the update immediately just takes nothing; polling the state word
// beats guessing a delay.
constexpr int kEquipRefreshTimeout = 180;

// Pouch type -> equipment-manager slot. 0 right hand, 1 left hand/shield,
// 2 bow, 3/4/5 the armour pieces; from 0x02d6f224 and 0x02eb8180. Arrows are a
// pouch type with no actor slot.
inline int EquipSlotForType(int type) {
    switch (type) {
        case 0: return 0;   // sword
        case 1: return 2;   // bow
        case 3: return 1;   // shield
        case 4: return 3;   // head
        case 5: return 4;   // chest
        case 6: return 5;   // legs
        default: return -1; // arrows and everything else
    }
}

// The REAL equipped array: 4 entries at manager+0x3812c, indexed by
// PouchItemType 0-3 (sword, bow, arrow, shield). trashItem compares against it
// to decide whether the item being dropped is the equipped one. Not to be
// confused with manager+0x37ea8, which is the drop queue.
constexpr uintptr_t kEquippedItems = 0x3812c;

// PauseMenuDataMgr::addItem(mgr, name, type, list, value, a, b, c).
//
// The high-level add: it refuses items tagged 0x17919f47, dedupes key items by
// name, stacks type 0 by name, then does the real insert and the bookkeeping
// (0x02eb3d38 dirty flag, 0x02eb2884, 0x02eb3460 tab-head rebuild).
//
// Argument shape is not inferred - it is copied from 0x02eb5ab8, which calls
// addItem(mgr, name, getPouchItemType(name, 0), mgr + 0x4c, 1, 0, 0, 0) between
// a crit-section lock and a saveToGameData. The list argument is the item list
// head at mgr+0x4c, the same list everything else here walks.
constexpr uintptr_t kAddPouchItemWiiU = 0x02eb3df0;

// PauseMenuDataMgr::saveToGameData(mgr, list). Called by 0x02eb5ab8 right after
// an add so the new item reaches the save flags.
constexpr uintptr_t kSaveToGameDataWiiU = 0x02eb3764;

// PauseMenuDataMgr::setItemModifier(mgr, PouchItem*, const s32 pair[2]).
//
// pair[0] is the modifier bitmask, pair[1] its magnitude. A null pointer, or a
// zero bitmask, clears the modifier instead. addItem routes its 7th argument
// through here for types 0/1/3, which is how a weapon loaded from a save gets
// the bonus recorded in PorchSword_FlagSp / PorchSword_ValueSp.
//
// Preferred over writing item+0x6c and item+0x64 directly: it owns the type
// gate, so it cannot corrupt a food item's cook data by writing weapon fields
// into the shared union.
constexpr uintptr_t kSetItemModifierWiiU = 0x02eb0a1c;

// sead::SafeString's vtable - see the same constant in gamedata.hpp. A stack
// SafeString is { const char*, vtable }, pointer first.
constexpr uintptr_t kSeadSafeStringVtableWiiU = 0x10263910;

struct SafeString {
    const char* text;
    const void* vtable;
};
constexpr uintptr_t kCritSectionLockWiiU = 0x030bb668;
constexpr uintptr_t kCritSectionUnlockWiiU = 0x030bb69c;

using SetFlagIndexedFn = void (*)(int value, int index, bool debug);
using CritSectionFn = void (*)(void* critSection);
using GetPouchItemTypeFn = int (*)(const SafeString* name, int unused);
using AddPouchItemFn = void (*)(void* manager, const SafeString* name, int type, void* list,
                                int value, int a, int b, int c);
using SaveToGameDataFn = void (*)(void* manager, void* list);
using SetItemModifierFn = void (*)(void* manager, void* item, const int32_t* pair);
using RegisterEquippedFn = void (*)(void* manager, void* item);
using GetPlayerFn = void* (*)(void* holder);
using NotifyEquipRemovedFn = void (*)(void* player, const void* name);
using RebuildEquippedFn = void (*)(void* manager);
using RequestEquipActorFn = void (*)(void* equipmentManager, int slot, const void* name,
                                     int value, const void* debugTag);
using PlayerEquipUpdateFn = void (*)(void* player);

// Slot whose actor we are waiting on, and how many frames are left to wait.
inline int& PendingEquipSlot() { static int slot = -1; return slot; }
inline int& PendingEquipFrames() { static int frames = 0; return frames; }


inline SafeString MakeSafeString(const char* text) {
    SafeString s = { text, reinterpret_cast<const void*>(kSeadSafeStringVtableWiiU) };
    return s;
}

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
// Walks the pouch in list order, calling visit(node, index) on each entry and
// stopping early if it returns false. Returns entries visited.
//
// The traversal is the game's own, from 0x02eb67f4: start at +0x50, stop when
// the node comes back around to the +0x4c sentinel, step through the link at
// nodeOffset+4. The index it hands the visitor is the pouch index the
// PorchItem_* flags are keyed by, not a position within one category.
template <typename Fn>
inline int WalkItems(Fn visit) {
#if !WIIXL_SWITCH
    void* manager = PauseMenuDataMgr();
    if (!manager) return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    int32_t nodeOffset = *reinterpret_cast<int32_t*>(base + kItemListNodeOffset);
    uintptr_t sentinel = base + kItemListHead - nodeOffset;
    uintptr_t node = *reinterpret_cast<uintptr_t*>(base + kItemListFirst) - nodeOffset;

    int walked = 0;
    while (node != sentinel && walked < kMaxItemsWalked) {
        if (!PlausiblePointer(node)) break;
        const bool keepGoing = visit(node, walked);
        ++walked;
        if (!keepGoing) break;
        node = *reinterpret_cast<uintptr_t*>(node + nodeOffset + 4) - nodeOffset;
    }
    return walked;
#else
    (void)visit;
    return 0;
#endif
}

// The name pointer off an entry, validated. See GetEquippedName for why the
// char* is at +0x18 rather than the +0x1c a plain sead::SafeString would use.
inline const char* ReadName(uintptr_t item) {
#if !WIIXL_SWITCH
    uintptr_t text = *reinterpret_cast<uintptr_t*>(item + kItemName);
    if (!PlausiblePointer(text)) return nullptr;

    const char* name = reinterpret_cast<const char*>(text);
    if (name[0] < 0x20 || name[0] > 0x7e) return nullptr;
    return name;
#else
    (void)item;
    return nullptr;
#endif
}

inline uintptr_t FindEquipped(int type, int* outIndex = nullptr) {
#if !WIIXL_SWITCH
    if (outIndex) *outIndex = -1;

    uintptr_t found = 0;
    int foundIndex = -1;
    WalkItems([&](uintptr_t node, int index) {
        if (*reinterpret_cast<uint8_t*>(node + kItemEquipped) != 0 &&
            *reinterpret_cast<int32_t*>(node + kItemType) == type) {
            found = node;
            foundIndex = index;
            return false;
        }
        return true;
    });

    if (found && outIndex) *outIndex = foundIndex;
    return found;
#else
    (void)type; (void)outIndex;
    return 0;
#endif
}

// Asks the equipment manager to load an item's actor for its slot, and arms the
// per-frame watcher that attaches it. Shared by EquipItem and SetModifier: a
// changed modifier is only visible once the weapon actor is rebuilt, which is
// the same problem equipping has.
inline void RequestActorForItem(uintptr_t item, int type) {
#if !WIIXL_SWITCH
    const int slot = EquipSlotForType(type);
    if (slot < 0) return;

    auto request = WiiXLaunch::GetTargetFunction<RequestEquipActorFn>(
        0x0, kRequestEquipActorWiiU);
    void* equipment = *reinterpret_cast<void**>(kEquipmentMgrPtrWiiU);
    if (!request || !equipment) return;

    SafeString tag = MakeSafeString("BotW_API");
    request(equipment, slot,
            reinterpret_cast<const void*>(item + kItemName),
            *reinterpret_cast<int32_t*>(item + kItemValue),
            &tag);

    PendingEquipSlot() = slot;
    PendingEquipFrames() = kEquipRefreshTimeout;
#else
    (void)item; (void)type;
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

    return impl::ReadName(item);
#else
    (void)slot;
    return nullptr;
#endif
}

// The PouchItemType the game would classify a name as, or -1 if it is not a
// pouch item. Wraps 0x02eae914, which hashes the actor's profile and tags
// rather than pattern-matching the name, so it is the game's own answer.
inline int GetTypeForName(const char* name) {
#if !WIIXL_SWITCH
    if (!name) return -1;
    auto getType = WiiXLaunch::GetTargetFunction<impl::GetPouchItemTypeFn>(
        0x0, impl::kGetPouchItemTypeWiiU);
    if (!getType) return -1;

    impl::SafeString key = impl::MakeSafeString(name);
    return getType(&key, 0);
#else
    (void)name;
    return -1;
#endif
}

// Adds an item to the pouch by actor name, e.g. "Item_Fruit_A" or
// "Weapon_Sword_070". value is the stack count for things that stack and the
// durability for weapons; the game passes 1 for an ordinary pickup.
//
// This mirrors 0x02eb5ab8 exactly: lock, classify, add, save, unlock. Stacking
// behaviour is the game's own - materials merge into an existing entry, weapons
// become new entries - because addItem decides that, not this.
//
// Returns false if there is no pouch yet or the name is not a pouch item.
inline bool AddItem(const char* name, int value = 1) {
#if !WIIXL_SWITCH
    void* manager = impl::PauseMenuDataMgr();
    if (!manager || !name) return false;

    auto add = WiiXLaunch::GetTargetFunction<impl::AddPouchItemFn>(0x0, impl::kAddPouchItemWiiU);
    auto save = WiiXLaunch::GetTargetFunction<impl::SaveToGameDataFn>(0x0, impl::kSaveToGameDataWiiU);
    auto lock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionLockWiiU);
    auto unlock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionUnlockWiiU);
    if (!add || !save || !lock || !unlock) return false;

    const int type = GetTypeForName(name);
    if (type < 0) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    void* critSection = reinterpret_cast<void*>(base + impl::kCritSection);
    void* list = reinterpret_cast<void*>(base + impl::kItemListHead);
    impl::SafeString key = impl::MakeSafeString(name);

    lock(critSection);
    add(manager, &key, type, list, value, 0, 0, 0);
    save(manager, list);
    unlock(critSection);
    return true;
#else
    (void)name; (void)value;
    return false;
#endif
}

// Removes count of an item by name, or everything of that name when count <= 0.
// Returns how many were actually taken.
//
// Removal in this game does NOT unlink the list node: both 0x02eb7988 and the
// arrow consume at 0x02eb6624 zero mValue (and mEquipped), then re-sync that
// slot's save flags by index. This does the same, which is why it cannot
// corrupt the list the way a hand-rolled unlink could. Counts cascade across
// stacks exactly as the arrow consume does.
inline int RemoveItem(const char* name, int count = 1) {
#if !WIIXL_SWITCH
    void* manager = impl::PauseMenuDataMgr();
    if (!manager || !name) return 0;

    auto lock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionLockWiiU);
    auto unlock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionUnlockWiiU);
    auto setValue = WiiXLaunch::GetTargetFunction<impl::SetFlagIndexedFn>(
        0x0, impl::kSetFlagPorchItemValue1WiiU);
    auto setEquip = WiiXLaunch::GetTargetFunction<impl::SetFlagIndexedFn>(
        0x0, impl::kSetFlagPorchItemEquipWiiU);
    if (!lock || !unlock || !setValue || !setEquip) return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    void* critSection = reinterpret_cast<void*>(base + impl::kCritSection);
    const bool questPouch = *reinterpret_cast<uint8_t*>(base + impl::kIsPouchForQuest) != 0;

    int taken = 0;
    int remaining = count;

    lock(critSection);
    impl::WalkItems([&](uintptr_t node, int index) {
        const char* entry = impl::ReadName(node);
        if (!entry) return true;

        // compare without pulling in <cstring>
        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a != *b) return true;

        int32_t* value = reinterpret_cast<int32_t*>(node + impl::kItemValue);
        const int have = *value;
        if (have <= 0) return true;

        const bool all = count <= 0 || remaining >= have;
        const int32_t left = all ? 0 : have - remaining;
        taken += all ? have : remaining;
        if (!all) remaining = 0; else remaining -= have;

        *value = left;
        if (left == 0) *reinterpret_cast<uint8_t*>(node + impl::kItemEquipped) = 0;

        // keep the save flags in step, the way the game's own removals do
        if (!questPouch && index >= 0) {
            setValue(left, index, false);
            if (left == 0) setEquip(0, index, false);
        }

        return count <= 0 || remaining > 0;
    });
    unlock(critSection);

    return taken;
#else
    (void)name; (void)count;
    return 0;
#endif
}

// Equips the first pouch entry matching name, and unequips whatever else of the
// same type was equipped.
//
// NOT a call to a game function - no dedicated equip routine turned up. This
// performs the two writes the game itself performs when equipment changes, in
// the same order and behind the same guard:
//
//   * the equipped byte at item+0x14, which is what every lookup keys off -
//     0x02eb67f4 finds "the equipped item of type N" as the first node with
//     +0x14 set and +0x08 == N, which is exactly what this maintains
//   * the matching PorchItem_EquipFlag by pouch index, skipped when the quest
//     pouch byte at manager+0x38128 is set, copying 0x02eb7988
//
// What it does NOT touch is the equipped-pointer cache at manager+0x37ea8 that
// saveToGameData consults for its +1 durability bump. Nothing here reads that
// cache, but the game does, so treat a swap as settling rather than instant:
// the menu and the actor holding the weapon may not catch up until they next
// refresh. Verify in game before relying on it.
//
// Returns false when no entry of that name exists.
inline bool EquipItem(const char* name) {
#if !WIIXL_SWITCH
    void* manager = impl::PauseMenuDataMgr();
    if (!manager || !name) return false;

    auto lock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionLockWiiU);
    auto unlock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionUnlockWiiU);
    auto setEquip = WiiXLaunch::GetTargetFunction<impl::SetFlagIndexedFn>(
        0x0, impl::kSetFlagPorchItemEquipWiiU);
    if (!lock || !unlock || !setEquip) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    void* critSection = reinterpret_cast<void*>(base + impl::kCritSection);
    const bool questPouch = *reinterpret_cast<uint8_t*>(base + impl::kIsPouchForQuest) != 0;

    lock(critSection);

    // first pass: locate the target and learn its type
    uintptr_t target = 0;
    int targetIndex = -1;
    int targetType = -1;
    impl::WalkItems([&](uintptr_t node, int index) {
        const char* entry = impl::ReadName(node);
        if (!entry) return true;
        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a != *b) return true;

        target = node;
        targetIndex = index;
        targetType = *reinterpret_cast<int32_t*>(node + impl::kItemType);
        return false;
    });

    if (!target) {
        unlock(critSection);
        return false;
    }

    // second pass: exactly one item of this type ends up equipped
    impl::WalkItems([&](uintptr_t node, int index) {
        if (*reinterpret_cast<int32_t*>(node + impl::kItemType) != targetType) return true;

        const bool wanted = node == target;
        uint8_t* equipped = reinterpret_cast<uint8_t*>(node + impl::kItemEquipped);
        if (*equipped == (wanted ? 1 : 0)) return true;

        *equipped = wanted ? 1 : 0;
        if (!questPouch && index >= 0) setEquip(wanted ? 1 : 0, index, false);
        return true;
    });

    unlock(critSection);

    // Bring the game's equipped array at manager+0x3812c into line with the
    // flags just written. It takes the pouch lock itself, hence after unlock.
    auto rebuild = WiiXLaunch::GetTargetFunction<impl::RebuildEquippedFn>(
        0x0, impl::kRebuildEquippedWiiU);
    if (rebuild) rebuild(manager);

    impl::RequestActorForItem(target, targetType);

    (void)targetIndex;
    return true;
#else
    (void)name;
    return false;
#endif
}

// Applies a pending equip without waiting for the player to open the menu.
//
// Call once per frame from the game thread. Equipping only files a request; the
// actor loads asynchronously and is attached by the player's equipment update,
// which the game itself only runs on menu close. So this waits for the slot
// record to reach the ready state and then runs that update directly, which is
// what makes a swap appear immediately instead of on the next pause.
//
// Deliberately polls rather than sleeping a fixed number of frames: load time
// varies, and calling the update early simply takes nothing and wastes the
// request. Gives up after kEquipRefreshTimeout frames so a failed load cannot
// leave this running forever.
inline void TickEquipRefresh() {
#if !WIIXL_SWITCH
    int& slot = impl::PendingEquipSlot();
    if (slot < 0) return;

    int& frames = impl::PendingEquipFrames();
    if (--frames <= 0) {
        slot = -1;
        return;
    }

    void* equipment = *reinterpret_cast<void**>(impl::kEquipmentMgrPtrWiiU);
    if (!equipment) { slot = -1; return; }

    uintptr_t record = reinterpret_cast<uintptr_t>(equipment) + impl::kEquipRecordsBase
                     + static_cast<uintptr_t>(slot) * impl::kEquipRecordStride;
    if (*reinterpret_cast<int32_t*>(record + impl::kEquipRecordState) != impl::kEquipRecordReady) {
        return;   // still loading, try again next frame
    }

    auto getPlayer = WiiXLaunch::GetTargetFunction<impl::GetPlayerFn>(
        0x0, impl::kGetPlayerForEquipWiiU);
    auto update = WiiXLaunch::GetTargetFunction<impl::PlayerEquipUpdateFn>(
        0x0, impl::kPlayerEquipUpdateWiiU);
    slot = -1;
    if (!getPlayer || !update) return;

    void* holder = *reinterpret_cast<void**>(impl::kPlayerHolderPtrWiiU);
    if (!holder) return;
    void* player = getPlayer(holder);
    if (player) update(player);
#endif
}

// Weapon modifier bits. ALL CONFIRMED from the decoder at 0x02eb7e24, which
// maps a mask to a display/sort index and covers every bit exhaustively:
//
//   0x1   AddPower     attack up          index 1  (0 when yellow)
//   0x2   AddLife      durability up      index 5  (4 when yellow)
//   0x4   Critical     critical hit       index 7
//   0x8   LongThrow    throw distance     index 6
//   0x10  SpreadFire   multishot          index 10
//   0x20  ZoomRapid    burst AND zoom     index 11
//   0x40  RapidFire    quick shot         index 9
//   0x80  SurfMaster   shield surf        index 8
//   0x100 AddGuard     guard up           index 3  (2 when yellow)
//   none                                  index 12
//
// The yellow tier bit is only consulted for AddPower, AddLife and AddGuard -
// the decoder tests it in exactly those three branches and nowhere else - which
// matches the name table having Plus forms only for those three. Setting it
// alongside any other bonus does nothing.
//
// PRIORITY, straight from the order of the decoder's tests, is what makes a
// combined mask show a single bonus:
//
//   0x1 > 0x2 > 0x8 > 0x4 > 0x100 > 0x80 > 0x10 > 0x20 > 0x40
//
// That predicts the observed cases: 3 (0x1|0x2) shows Attack Up, 6 (0x2|0x4)
// shows Durability Up, and 100 decimal (0x4|0x20|0x40) shows Critical Hit.
//
// Names come from the effect table built at 0x0308b9b8. Which bits a weapon
// class actually honours is a separate question - a sword ignores the bow bits.
//
// THE MAGNITUDE AT item+0x64 IS NOT ALWAYS AN INTEGER. Weapon param lists
// declare a roll range per modifier, and the declared types differ:
//
//   0x1   AddPower    int    flat attack     (rolls 0-9, yellow to 16)
//   0x2   AddLife     int    flat durability (1-7, yellow to 18)
//   0x100 AddGuard    int    flat guard      (0-12, yellow to 26)
//   0x8   LongThrow   int    throw distance  (range declared 1.5-2.0 as floats,
//                                              but the stored magnitude is an
//                                              int - large values throw far)
//   0x40  RapidFire   FLOAT  multiplier      (1.0 - 1.3), float BITS
//   0x4, 0x10, 0x20, 0x80  Critical, SpreadFire, ZoomRapid, SurfMaster:
//                          declared True/False, so no RANDOM range - but they
//                          do still use the magnitude. SpreadFire and
//                          SurfMaster were both seen changing with it in play.
//                          Their type (int or float) is not established.
//
// So for LongThrow and RapidFire the word holds float BITS, not a number.
// Writing integer 1 there is a denormal near zero, which is why Quick Shot with
// a magnitude of 1 does nothing observable. Use ModifierWantsFloat to decide.
//
// From SharpWeaponAddAtkMin/Max, AddLifeMin/Max, AddCrit, AddGuardMin/Max and
// the Powered variants across 193 weapon param lists; SharpWeaponPer is 10.0
// everywhere, the chance of rolling a bonus at all.
enum ModifierBit : uint32_t {
    ModifierNone       = 0,
    ModifierAttackUp   = 0x001,   // AddPower
    ModifierDurability = 0x002,   // AddLife
    ModifierCritical   = 0x004,   // Critical
    ModifierLongThrow  = 0x008,   // LongThrow
    ModifierMultiShot  = 0x010,   // SpreadFire
    ModifierZoomRapid  = 0x020,   // ZoomRapid - burst and zoom together
    ModifierRapidFire  = 0x040,   // RapidFire, shown as Quick Shot
    ModifierSurfMaster = 0x080,   // SurfMaster
    ModifierGuardUp    = 0x100,   // AddGuard
    ModifierYellow     = 0x80000000,   // tier; only for AttackUp/Durability/GuardUp
};

// Whether a modifier's magnitude is float bits rather than an integer.
//
// RapidFire only, and that is measured rather than inferred: written as the
// float 1.3 it visibly speeds the draw, while integer 1 did nothing (as float
// bits, 1 is a denormal near zero).
//
// LongThrow is an INT despite its roll range being declared in floats
// (PoweredSharpAddThrowMin/Max are 1.5 and 2.0). Large integers make throws go
// a long way, and switching it to float parsing broke that - so the param range
// and the stored magnitude are not the same units here. Trusting the behaviour
// over the declaration.
inline bool ModifierWantsFloat(uint32_t flags) {
    return (flags & ModifierRapidFire) != 0;
}

// Reinterprets a float as the raw word the magnitude field expects.
inline int32_t ModifierFloatBits(float value) {
    union { float f; int32_t i; } bits;
    bits.f = value;
    return bits.i;
}

// One ingredient name off a food item, or null. Slot is 0..4.
inline const char* CookIngredient(uintptr_t item, int slot) {
#if !WIIXL_SWITCH
    if (slot < 0 || slot >= impl::kItemCookNameCount) return nullptr;

    void** names = *reinterpret_cast<void***>(item + impl::kItemCookNames);
    if (!impl::PlausiblePointer(reinterpret_cast<uintptr_t>(names))) return nullptr;

    void* entry = names[slot];
    if (!impl::PlausiblePointer(reinterpret_cast<uintptr_t>(entry))) return nullptr;

    // same inverted layout as PouchItem::mName: text pointer first, vtable after
    uintptr_t text = *reinterpret_cast<uintptr_t*>(entry);
    if (!impl::PlausiblePointer(text)) return nullptr;

    const char* name = reinterpret_cast<const char*>(text);
    if (name[0] < 0x20 || name[0] > 0x7e) return nullptr;
    return name;
#else
    (void)item; (void)slot;
    return nullptr;
#endif
}

// Overwrites a food item's cook data. Only the fields you pass are touched, so
// nudging a duration will not blank the effect.
//
// Works on recipe dishes (Item_Cook_*), which never stack and so get one pouch
// entry each to carry their own effect. It does NOT work on roasted or chilled
// foods: those STACK - Item_Roast_01 at 5, Item_Chilled_02 at 9 - and a stack is
// a single entry shared by every copy, with nowhere to hang per-instance data.
// They all read health 0, duration 0, effect -1 untouched, and the write lands
// but the game never reads it. Measured: replacement works on Item_Cook_*, not
// on roast or chilled.
//
// Type 8 only: on a weapon these same words are the modifier, and writing cook
// data there would silently corrupt the bonus. Direct writes rather than a game
// call, because nothing found sets these outside loadFromGameData's bulk
// restore - they are plain fields on the item, the same class of write as
// durability.
struct CookData {
    bool setHealth = false;      int32_t health = 0;
    bool setDuration = false;    int32_t duration = 0;
    bool setSellPrice = false;   int32_t sellPrice = 0;
    bool setEffectId = false;    float effectId = 0.0f;
    bool setEffectLevel = false; float effectLevel = 0.0f;
};

inline bool SetCookData(uintptr_t item, const CookData& data) {
#if !WIIXL_SWITCH
    void* manager = impl::PauseMenuDataMgr();
    if (!manager || !item) return false;
    if (*reinterpret_cast<int32_t*>(item + impl::kItemType) != 8) return false;

    auto lock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionLockWiiU);
    auto unlock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionUnlockWiiU);
    if (!lock || !unlock) return false;

    void* critSection = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(manager) + impl::kCritSection);

    lock(critSection);
    if (data.setHealth)
        *reinterpret_cast<int32_t*>(item + impl::kItemCookHealth) = data.health;
    if (data.setDuration)
        *reinterpret_cast<int32_t*>(item + impl::kItemCookDuration) = data.duration;
    if (data.setSellPrice)
        *reinterpret_cast<int32_t*>(item + impl::kItemCookSellPrice) = data.sellPrice;
    if (data.setEffectId)
        *reinterpret_cast<float*>(item + impl::kItemCookEffectId) = data.effectId;
    if (data.setEffectLevel)
        *reinterpret_cast<float*>(item + impl::kItemCookEffectLevel) = data.effectLevel;
    unlock(critSection);
    return true;
#else
    (void)item; (void)data;
    return false;
#endif
}

// Finds a pouch entry. Pass a name, an index, or both.
//
//   name only          the first entry with that name
//   index only         whatever occupies that pouch slot, whatever it is
//   name and index     must AGREE, or nothing is returned
//
// Pouch indices are positions in one list shared by every category and they
// shift whenever anything is added or removed, so a stale index addresses a
// different item. When a caller supplies both, treating the mismatch as a
// refusal is what stops a write landing on the wrong thing - cook data aimed at
// a meal once landed on an Item_Chilled_03 sitting at the index. Index alone is
// still allowed: with no name given there is nothing to contradict, and it is
// the only way to target a slot whose actor you mean to change.
inline uintptr_t FindItem(const char* name, int index = -1) {
#if !WIIXL_SWITCH
    const bool haveName = name && name[0];

    uintptr_t found = 0;
    impl::WalkItems([&](uintptr_t node, int at) {
        if (index >= 0 && at != index) return true;

        if (!haveName) {
            // index-only: take the slot as it is
            found = node;
            return false;
        }

        const char* entry = impl::ReadName(node);
        if (!entry) return index >= 0 ? false : true;

        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a != *b) return index >= 0 ? false : true;

        found = node;
        return false;
    });
    return found;
#else
    (void)name; (void)index;
    return 0;
#endif
}

// The actor name on a pouch entry, or "" - handy for echoing back which item a
// write actually landed on, rather than which one the caller thought it meant.
inline const char* ItemName(uintptr_t item) {
#if !WIIXL_SWITCH
    const char* name = item ? impl::ReadName(item) : nullptr;
    return name ? name : "";
#else
    (void)item;
    return "";
#endif
}

// Whether a pouch entry holds more than one of something. Stacking foods are
// the case that matters: their cook data is never read.
inline bool ItemStacks(uintptr_t item) {
#if !WIIXL_SWITCH
    return item && *reinterpret_cast<int32_t*>(item + impl::kItemValue) > 1;
#else
    (void)item;
    return false;
#endif
}

// The LAST entry with this name, which after an add is the one just added -
// addItem appends, and food never stacks, so a fresh meal is always last.
inline uintptr_t FindNewestItem(const char* name) {
#if !WIIXL_SWITCH
    if (!name) return 0;
    uintptr_t found = 0;
    impl::WalkItems([&](uintptr_t node, int) {
        const char* entry = impl::ReadName(node);
        if (!entry) return true;
        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == *b) found = node;
        return true;   // keep going; we want the last match
    });
    return found;
#else
    (void)name;
    return 0;
#endif
}

// Reads a weapon's modifier. False when no entry of that name exists or it is
// not a type the modifier union applies to (0-3: sword, bow, arrow, shield).
inline bool GetModifier(const char* name, uint32_t& flags, int32_t& value) {
#if !WIIXL_SWITCH
    if (!name) return false;

    uintptr_t found = 0;
    impl::WalkItems([&](uintptr_t node, int) {
        const char* entry = impl::ReadName(node);
        if (!entry) return true;
        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a != *b) return true;
        found = node;
        return false;
    });

    if (!found) return false;
    if (*reinterpret_cast<int32_t*>(found + impl::kItemType) > 3) return false;

    flags = *reinterpret_cast<uint32_t*>(found + impl::kItemModifierFlags);
    value = *reinterpret_cast<int32_t*>(found + impl::kItemModifierValue);
    return true;
#else
    (void)name; (void)flags; (void)value;
    return false;
#endif
}

// Writes the modifier onto an already-resolved entry. Take this overload when
// the caller has picked a slot: a name alone always finds the FIRST match, so
// two copies of the same weapon are otherwise indistinguishable.
inline bool SetModifierAt(uintptr_t found, uint32_t flags, int32_t value) {
#if !WIIXL_SWITCH
    void* manager = impl::PauseMenuDataMgr();
    if (!manager || !found) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetItemModifierFn>(
        0x0, impl::kSetItemModifierWiiU);
    auto lock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionLockWiiU);
    auto unlock = WiiXLaunch::GetTargetFunction<impl::CritSectionFn>(0x0, impl::kCritSectionUnlockWiiU);
    if (!set || !lock || !unlock) return false;

    if (*reinterpret_cast<int32_t*>(found + impl::kItemType) > 3) return false;

    const int32_t pair[2] = { static_cast<int32_t>(flags), value };
    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    void* critSection = reinterpret_cast<void*>(base + impl::kCritSection);

    const int type = *reinterpret_cast<int32_t*>(found + impl::kItemType);
    const bool equipped = *reinterpret_cast<uint8_t*>(found + impl::kItemEquipped) != 0;

    lock(critSection);
    set(manager, reinterpret_cast<void*>(found), pair);
    unlock(critSection);

    // Deliberately NOT refreshing the actor here.
    //
    // The obvious move is to rebuild the held weapon so the change shows at
    // once, and that is what this did for a while. It made things worse:
    // 0x02a15e44 takes only a name and a durability, carrying no modifier, so
    // the rebuilt actor comes back WITHOUT the bonus - measured, Long Throw and
    // Shield Surf stopped having any effect once the refresh was added, having
    // worked when the item was re-equipped by hand.
    //
    // Re-equipping through the game applies it properly, because that path
    // reads the whole PouchItem. EquipItem still refreshes, since there the
    // name and durability are all the request needs.
    (void)equipped;
    (void)type;

    return true;
#else
    (void)found; (void)flags; (void)value;
    return false;
#endif
}

// Sets a weapon's modifier, or clears it when flags is 0. Goes through the
// game's own setter so the type gate is respected.
//
// The pouch is updated immediately; the weapon Link is holding keeps whatever
// stats it spawned with, so re-equip the item to see a change in the world.
inline bool SetModifier(const char* name, uint32_t flags, int32_t value) {
#if !WIIXL_SWITCH
    return SetModifierAt(FindItem(name), flags, value);
#else
    (void)name; (void)flags; (void)value;
    return false;
#endif
}

// One pouch entry, as handed to ForEachOfType.
struct Entry {
    uintptr_t node;     // the PouchItem itself, for readers that need more
    const char* name;   // null if the entry's name pointer did not validate
    int value;          // count for materials, durability for weapons
    int index;          // pouch index, the key the PorchItem_* flags use
    bool equipped;
    uint32_t modifierFlags;   // weapons only (type 0-3); 0 for everything else
    int32_t modifierValue;

    // food only (type 8) - the same words, read as cook data. isFood says
    // which of the two readings applies.
    bool isFood;
    int32_t cookHealth;
    int32_t cookDuration;
    int32_t cookSellPrice;
    float cookEffectId;
    float cookEffectLevel;
    int32_t cookNameSlots;   // array capacity, always 5 - not a count
};

// Visits every entry of one type in pouch order. visit returns false to stop
// early. Returns the number of MATCHING entries seen, which for an interrupted
// walk is the number visited rather than the number that exist.
//
// This is the only way to reach Material (7), Food (8) and KeyItem (9): none of
// them are ever equipped, so every lookup keyed off the equipped byte misses
// them entirely. It works for the equippable types too - enumerating every
// sword you own rather than just the one in hand.
template <typename Fn>
inline int ForEachOfType(Slot slot, Fn visit) {
#if !WIIXL_SWITCH
    const int type = static_cast<int>(slot);
    int matched = 0;

    impl::WalkItems([&](uintptr_t node, int index) {
        if (*reinterpret_cast<int32_t*>(node + impl::kItemType) != type) return true;

        Entry entry;
        entry.node = node;
        entry.name = impl::ReadName(node);
        entry.value = *reinterpret_cast<int32_t*>(node + impl::kItemValue);
        entry.index = index;
        entry.equipped = *reinterpret_cast<uint8_t*>(node + impl::kItemEquipped) != 0;

        // the modifier union only means this for types 0-3; on food the same
        // words are cook data, so reporting them there would be nonsense
        const bool weapon = type <= 3;
        entry.modifierFlags = weapon
            ? *reinterpret_cast<uint32_t*>(node + impl::kItemModifierFlags) : 0u;
        entry.modifierValue = weapon
            ? *reinterpret_cast<int32_t*>(node + impl::kItemModifierValue) : 0;

        entry.isFood = type == 8;
        entry.cookHealth = entry.isFood
            ? *reinterpret_cast<int32_t*>(node + impl::kItemCookHealth) : 0;
        entry.cookDuration = entry.isFood
            ? *reinterpret_cast<int32_t*>(node + impl::kItemCookDuration) : 0;
        entry.cookSellPrice = entry.isFood
            ? *reinterpret_cast<int32_t*>(node + impl::kItemCookSellPrice) : 0;
        entry.cookEffectId = entry.isFood
            ? *reinterpret_cast<float*>(node + impl::kItemCookEffectId) : 0.0f;
        entry.cookEffectLevel = entry.isFood
            ? *reinterpret_cast<float*>(node + impl::kItemCookEffectLevel) : 0.0f;
        entry.cookNameSlots = entry.isFood
            ? *reinterpret_cast<int32_t*>(node + impl::kItemCookNameSlots) : 0;

        ++matched;
        return visit(entry);
    });

    return matched;
#else
    (void)slot; (void)visit;
    return 0;
#endif
}

// How many entries of a type the pouch holds.
inline int CountOfType(Slot slot) {
    return ForEachOfType(slot, [](const Entry&) { return true; });
}

} // namespace WiiXLaunch::BotW::Pouch
