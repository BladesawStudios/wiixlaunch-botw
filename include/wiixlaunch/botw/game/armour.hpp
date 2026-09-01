#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/hook.hpp>
#include "player.hpp"

// WiiXLaunch::BotW::Armour - the special-status effects Link's worn armour
// grants (ksys::act::ArmorEffect).
//
// Where an armour effect actually lives, and why this header looks the way it
// does:
//
//   * NOT in the pouch. PauseMenuDataMgr::loadFromGameData (0x02eb4518) reads
//     name, value and equip flag for armour and NOTHING else - the modifier
//     union is read only for types 0/1/3 and the cook union only for type 8.
//     There is no PorchArmor_* flag either. So unlike a weapon's bonus, an
//     armour effect is not per-item, not per-save, and Pouch::SetModifier has
//     no armour counterpart.
//
//   * It is a GParamList parameter on the armour ACTOR: the ArmorEffect object
//     holds `armorEffectEffectType` (a name from the 23-entry table below) and
//     `armorEffectEffectLevel`, plus three booleans. That is the only storage,
//     so it is the only thing worth writing.
//
//   * The player SUMS it. ksys::act::Player::updateArmorEffects (0x02d58cc4)
//     walks the three worn pieces, adds up each one's level for a given
//     effect, adds the set bonus, and caches the totals in a block at
//     player+0x1a50. Gameplay reads that cache, so a write to a piece's
//     GParamList is not visible until the cache is rebuilt - which is what
//     Refresh() does, and what every setter here calls for you.
//
// Two levels of API, matching that:
//
//   GetArmourEffects / GetArmourEffect  - what Link currently has, taken from
//                                         the game's own aggregate queries
//   GetPieceEffect / SetPieceEffect     - the actual storage, one worn piece
//   SetArmourEffects                    - convenience: make the total for one
//                                         effect exactly N
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Switch was never
// RE'd for any of this; every call is a no-op returning false there, matching
// the rest of the framework.

namespace WiiXLaunch::BotW::Armour {

// ksys::act::ArmorEffect. These are indices into the game's own 23-entry name
// table at 0x1054cc38, built by 0x0328cbfc - not invented ordering. The four
// "AAndB" entries are real single values an armour piece can declare: a piece
// with ResistColdAndResistAncient counts for BOTH ResistCold and ResistAncient
// at its one level, which the game resolves through the combo table at
// 0x1054cbb8.
enum class Effect : int {
    None = 0,
    ResistHot = 1,
    ResistBurn = 2,
    ResistCold = 3,
    ResistElectric = 4,
    ResistLightning = 5,
    SwimSpeed = 6,
    ClimbSpeed = 7,
    AttackUp = 8,
    Quietness = 9,
    SandMove = 10,
    SnowMove = 11,
    ResistAncient = 12,
    ResistBurnAndResistAncient = 13,
    ResistColdAndResistAncient = 14,
    ResistElectricAndResistAncient = 15,
    SwimSpeedAndResistAncient = 16,
    ResistFreeze = 17,
    WakeWind = 18,
    BeamPowerUp = 19,
    ClimbSpeedHorizontalOnly = 20,
    ResistHotAndWakeWind = 21,
    ClimbSpeedAndBeamPowerUp = 22,
};

// The three armour slots, in the order the player's own holder stores them.
enum class Piece : int {
    Head = 0,
    Upper = 1,
    Lower = 2,
};

static constexpr bool SupportsArmourEffects = !WIIXL_SWITCH;

namespace impl {

// --- reaching the worn pieces -------------------------------------------
//
// ksys::act::Player holds the three armour actors at player+0x1d94, as an
// array of handles (capacity 6, only the first 3 ever walked) starting at
// +0x08 with a 0xc-byte stride. Read straight out of 0x02d58cc4, which passes
// player+0x1d94 to every aggregate query, and out of the queries themselves
// (0x03290c48 and friends) which walk `holder + 8 + 0xc*i` for i in 0..2.
constexpr uintptr_t kArmourHolder = 0x1d94;
constexpr uintptr_t kHolderSlots = 0x08;
constexpr uintptr_t kHolderSlotStride = 0x0c;
constexpr uintptr_t kHolderSetBonus = 0xb8;   // u32 of Cb_* bits, via 0x03290c40
constexpr int kPieceCount = 3;

// A handle record is { BaseProcUnit* unit; s32 id; u8 allowDying; } - the same
// shape Player::GetRaw() already reads out of the player tracker at +0x34.
// Resolution is 0x0378d8dc(unit, id, allowDying), whose whole body is
// `if (id != -1 && unit->[0x3c] == id) { p = unit->[0x40]; ... }`.
constexpr uintptr_t kResolveProcWiiU = 0x0378d8dc;
using GetProcFn = void* (*)(void* unit, uint32_t id, uint8_t allowDying);

// --- the ArmorEffect parameters -----------------------------------------
//
// Every one of these offsets comes from the game's own per-actor accessor,
// which all share one body: actor+0x39c is the actor's parameter resource,
// +0x78 of that is the GParamList, +0x244 is the object array (count at
// +0x240), and the object is taken by index.
//
//   0x0328c6c0  getArmorEffectType             obj[0x19] + 0x2c  const char*
//   0x0328c5a4  getArmorEffectLevel            obj[0x19] + 0x40  s32
//   0x0328bf40  getArmorEffectAncientPowUp     obj[0x19] + 0x50  bool
//   0x0328c05c  getArmorEffectClimbWaterfall   obj[0x19] + 0x60  bool
//   0x0328c178  getArmorEffectSpinAttack       obj[0x19] + 0x70  bool
//   0x0328b8c8  getArmorDefenceAddLevel        obj[0x18] + 0x3c  s32
//
// Each accessor's by-name fallback names the parameter it is reading
// (0x0310d814 "armorEffectEffectType", 0x0310d8a4 "armorEffectEffectLevel",
// 0x0310d8cc "armorEffectAncientPowUp", 0x0310d8f4
// "armorEffectEnableClimbWaterfall", 0x0310d908 "armorEffectEnableSpinAttack",
// 0x0310d7bc "armorDefenceAddLevel"), so the offset-to-name pairing is not
// guessed. The 0x14/0x10-byte spacing is just agl::utl::Parameter layout.
constexpr uintptr_t kActorParams = 0x39c;
constexpr uintptr_t kParamsResourceName = 0x08;
constexpr uintptr_t kParamsGParamList = 0x78;
constexpr uintptr_t kGParamObjectCount = 0x240;
constexpr uintptr_t kGParamObjectArray = 0x244;
constexpr int kObjectArmor = 0x18;
constexpr int kObjectArmorEffect = 0x19;

constexpr uintptr_t kEffectType = 0x2c;    // const char*, sead::SafeString text
constexpr uintptr_t kEffectTypeVtable = 0x30;
constexpr uintptr_t kEffectLevel = 0x40;   // s32
constexpr uintptr_t kAncientPowUp = 0x50;  // bool
constexpr uintptr_t kClimbWaterfall = 0x60;
constexpr uintptr_t kSpinAttack = 0x70;
constexpr uintptr_t kDefenceAddLevel = 0x3c;   // on obj[0x18], not obj[0x19]

// The 23 effect names, as { const char* text; const void* vtable } pairs.
// Built at runtime by 0x0328cbfc, so it reads null before the game has got
// that far - every use here checks.
constexpr uintptr_t kEffectNameTableWiiU = 0x1054cc38;
constexpr int kEffectCount = 23;

// --- the aggregate queries ----------------------------------------------
//
// Each takes the holder (player+0x1d94) and returns the summed level across
// the three worn pieces, honouring the combo entries and, for a few, a set
// bonus. Identified by the name-table entry each one passes to
// getArmorEffectLevelIfType (0x0328c7f4) - not by position.
constexpr uintptr_t kQueryResistHot = 0x03290c48;
constexpr uintptr_t kQueryResistBurn = 0x03290df8;
constexpr uintptr_t kQueryResistCold = 0x03290f84;
constexpr uintptr_t kQueryResistFreeze = 0x03291110;
constexpr uintptr_t kQueryResistLightning = 0x0329129c;
constexpr uintptr_t kQueryResistElectric = 0x03291428;
constexpr uintptr_t kQuerySwimSpeed = 0x032915dc;
constexpr uintptr_t kQueryClimbSpeed = 0x03291768;
constexpr uintptr_t kQueryAttackUp = 0x032918f4;
constexpr uintptr_t kQueryDefence = 0x03291a80;   // armorDefenceAddLevel sum
constexpr uintptr_t kQueryQuietness = 0x03291bfc;
constexpr uintptr_t kQuerySandMove = 0x03291d88;
constexpr uintptr_t kQuerySnowMove = 0x03291f14;
constexpr uintptr_t kQueryResistAncient = 0x032920a0;
constexpr uintptr_t kQueryClimbWaterfall = 0x032923c4;   // any piece, 0/1
constexpr uintptr_t kQuerySpinAttack = 0x0329255c;       // any piece, 0/1
constexpr uintptr_t kQueryClimbSpeedHorizontal = 0x032926f4;
constexpr uintptr_t kQueryWakeWind = 0x03292880;
constexpr uintptr_t kQueryBeamPowerUp = 0x03292a28;

using QueryFn = int (*)(void* holder);

// --- the player's cache -------------------------------------------------
//
// Where updateArmorEffects (0x02d58cc4) stores each query's result. This is
// what gameplay reads, so Refresh() rewrites exactly these words from exactly
// these queries. Offsets and the query each is fed by are lifted one for one
// out of that function's `bl <query>; stw r3,<off>(r29)` pairs.
constexpr uintptr_t kCacheResistHot = 0x1a50;
constexpr uintptr_t kCacheResistCold = 0x1a54;
constexpr uintptr_t kCacheResistBurn = 0x1a58;
constexpr uintptr_t kCacheResistFreeze = 0x1a5c;
constexpr uintptr_t kCacheResistElectric = 0x1a60;
constexpr uintptr_t kCacheResistLightning = 0x1a64;
constexpr uintptr_t kCacheAttackUp = 0x1a68;
constexpr uintptr_t kCacheDefence = 0x1a74;
constexpr uintptr_t kCacheSwimSpeed = 0x1a7c;
constexpr uintptr_t kCacheClimbSpeed = 0x1a80;
constexpr uintptr_t kCacheQuietness = 0x1a00;   // f32, not s32
constexpr uintptr_t kCacheClimbSpeedHorizontal = 0x1aa0;
constexpr uintptr_t kCacheSandMove = 0x1aa4;    // u8, set from (level > 0)
constexpr uintptr_t kCacheSnowMove = 0x1aa5;    // u8, set from (level > 0)
constexpr uintptr_t kCacheSpinAttack = 0x1aa6;  // u8
constexpr uintptr_t kCacheClimbWaterfall = 0x1aa7;  // u8
constexpr uintptr_t kCacheWakeWind = 0x1aa9;    // u8

// ResistAncient and BeamPowerUp are deliberately absent: updateArmorEffects
// feeds their results straight into 0x02d902ec / other setters rather than
// storing a raw word, so there is no cache slot to rewrite. GetArmourEffects
// still reports them - the query is live either way.

inline bool PlausiblePointer(uintptr_t addr) {
    return addr >= 0x10000000 && addr < 0xa0000000;
}

// The holder the game passes to every aggregate query, or null.
inline void* Holder() {
#if !WIIXL_SWITCH
    void* player = Player::GetRaw();
    if (!player || !PlausiblePointer(reinterpret_cast<uintptr_t>(player))) return nullptr;
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(player) + kArmourHolder);
#else
    return nullptr;
#endif
}

// The armour actor in one slot, or null when nothing is worn there (or the
// piece's actor has not finished loading).
//
// Deliberately does NOT take the per-unit lock 0x0378d87c the game's own
// queries take around the resolve. Same reasoning as Pouch::WalkItems: this
// runs on the game thread out of a frame callback, and the two words read are
// single aligned words that cannot tear.
inline void* PieceActor(int piece) {
#if !WIIXL_SWITCH
    void* holder = Holder();
    if (!holder || piece < 0 || piece >= kPieceCount) return nullptr;

    uintptr_t record = reinterpret_cast<uintptr_t>(holder) + kHolderSlots +
                       kHolderSlotStride * static_cast<uintptr_t>(piece);

    void* unit = *reinterpret_cast<void**>(record + 0x00);
    uint32_t id = *reinterpret_cast<uint32_t*>(record + 0x04);
    uint8_t allowDying = *reinterpret_cast<uint8_t*>(record + 0x08);
    if (!unit || id == 0xffffffffu) return nullptr;
    if (!PlausiblePointer(reinterpret_cast<uintptr_t>(unit))) return nullptr;

    auto resolve = WiiXLaunch::GetTargetFunction<GetProcFn>(0x0, kResolveProcWiiU);
    if (!resolve) return nullptr;

    void* actor = resolve(unit, id, allowDying);
    if (!actor || !PlausiblePointer(reinterpret_cast<uintptr_t>(actor))) return nullptr;
    return actor;
#else
    (void)piece;
    return nullptr;
#endif
}

// One GParamList object off an actor, by index, or 0. Mirrors the guard every
// accessor uses: bail when the actor's parameter resource has an empty name
// (that is the case the game answers by falling back to a lookup by actor
// name, which is read-only and so no use to a setter), and bail when the
// object array is shorter than the index rather than reading past it.
inline uintptr_t ParamObject(void* actor, int index) {
#if !WIIXL_SWITCH
    if (!actor) return 0;
    uintptr_t base = reinterpret_cast<uintptr_t>(actor);

    uintptr_t params = *reinterpret_cast<uintptr_t*>(base + kActorParams);
    if (!PlausiblePointer(params)) return 0;

    const char* resourceName = *reinterpret_cast<const char**>(params + kParamsResourceName);
    if (!resourceName || !PlausiblePointer(reinterpret_cast<uintptr_t>(resourceName))) return 0;
    if (resourceName[0] == '\0') return 0;

    uintptr_t gparam = *reinterpret_cast<uintptr_t*>(params + kParamsGParamList);
    if (!PlausiblePointer(gparam)) return 0;

    uint32_t count = *reinterpret_cast<uint32_t*>(gparam + kGParamObjectCount);
    uintptr_t array = *reinterpret_cast<uintptr_t*>(gparam + kGParamObjectArray);
    if (!PlausiblePointer(array)) return 0;
    if (count <= static_cast<uint32_t>(index)) return 0;

    uintptr_t object = *reinterpret_cast<uintptr_t*>(array + 4 * static_cast<uintptr_t>(index));
    if (!PlausiblePointer(object)) return 0;
    return object;
#else
    (void)actor; (void)index;
    return 0;
#endif
}

// The game's own name string for an effect, or null before the table is built.
// Used as the value written into a piece rather than a literal of our own, so
// the comparison in 0x0328c7f4 hits its pointer-equality fast path.
inline const char* TableName(int index) {
#if !WIIXL_SWITCH
    if (index < 0 || index >= kEffectCount) return nullptr;
    uintptr_t entry = kEffectNameTableWiiU + 8 * static_cast<uintptr_t>(index);
    const char* text = *reinterpret_cast<const char* const*>(entry);
    if (!text || !PlausiblePointer(reinterpret_cast<uintptr_t>(text))) return nullptr;
    return text;
#else
    (void)index;
    return nullptr;
#endif
}

inline int RunQuery(uintptr_t address) {
#if !WIIXL_SWITCH
    void* holder = Holder();
    if (!holder) return 0;
    auto query = WiiXLaunch::GetTargetFunction<QueryFn>(0x0, address);
    if (!query) return 0;
    return query(holder);
#else
    (void)address;
    return 0;
#endif
}

} // namespace impl

// The game's own name for an effect, e.g. "ResistCold". Returns "" rather than
// null when the name table has not been built yet (very early boot) or the
// value is out of range - so it is always safe to print.
inline const char* EffectName(Effect effect) {
#if !WIIXL_SWITCH
    const char* name = impl::TableName(static_cast<int>(effect));
    return name ? name : "";
#else
    (void)effect;
    return "";
#endif
}

// The effect an armour name maps to, or Effect::None. Matches against the
// game's own table, so it accepts exactly the strings the game accepts.
inline Effect EffectFromName(const char* name) {
#if !WIIXL_SWITCH
    if (!name || !name[0]) return Effect::None;
    for (int i = 0; i < impl::kEffectCount; ++i) {
        const char* entry = impl::TableName(i);
        if (!entry) continue;
        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == *b) return static_cast<Effect>(i);
    }
    return Effect::None;
#else
    (void)name;
    return Effect::None;
#endif
}

// What one worn piece declares. This is the storage, not a total.
struct PieceEffect {
    bool worn;              // false when the slot is empty or still loading
    const char* actorName;  // e.g. "Armor_002_Head"; "" when unavailable
    Effect effect;
    int level;
    int defence;            // armorDefenceAddLevel, the piece's own defence
    bool ancientPowUp;      // Ancient set: ancient weapon damage bonus
    bool climbWaterfall;    // Zora set: swim up waterfalls
    bool spinAttack;        // Barbarian set: charge attacks
};

// Everything Link currently has, straight from the game's aggregate queries -
// so these are the numbers gameplay uses, set bonuses included, not a sum this
// header worked out for itself.
struct Effects {
    int resistHot;
    int resistBurn;
    int resistCold;
    int resistFreeze;
    int resistElectric;
    int resistLightning;
    int resistAncient;
    int swimSpeed;
    int climbSpeed;
    int climbSpeedHorizontalOnly;
    int attackUp;
    int quietness;
    int sandMove;
    int snowMove;
    int wakeWind;
    int beamPowerUp;
    int defence;            // total armorDefenceAddLevel across the three
    bool climbWaterfall;    // any worn piece declares it
    bool spinAttack;
    uint32_t setBonusFlags; // Cb_* bits; see SetBonus below
};

// Set-bonus bits, from the 13-entry table at 0x1046c7a0 whose first word is
// the bit index. The mask itself lives at holder+0xb8 (0x03290c40 returns
// exactly that address). Two combinations are named rather than bitwise:
// Terror + NightMoveSpeedUp displays as Cb_TerrorAndNightMoveSpeedUp, and
// DecSwimEnergy|DecWallJumpEnergy|ChargeAttackEnergy (mask 0x40101) displays
// as Cb_ChargeAttackEnergy - both read out of 0x03086c2c.
enum SetBonus : uint32_t {
    SetBonusDecSwimEnergy = 1u << 0,
    SetBonusResistElectric = 1u << 1,
    SetBonusResistHot = 1u << 2,
    SetBonusResistFreeze = 1u << 3,
    SetBonusResistFire = 1u << 4,
    SetBonusResistLightning = 1u << 5,
    SetBonusNightMoveSpeedUp = 1u << 6,
    SetBonusNightGlow = 1u << 7,
    SetBonusDecWallJumpEnergy = 1u << 8,
    SetBonusAncientWeaponPowUp = 1u << 10,
    SetBonusTerror = 1u << 12,
    SetBonusChargeAttackEnergy = 1u << 18,
    SetBonusMasterSwordPowUp = 1u << 19,
};

// Whether Link is wearing anything in this slot. Reports the ACTOR, not the
// pouch entry: a piece that is equipped but whose actor has not loaded yet
// reads false, which is also when reading or writing its effect would do
// nothing.
inline bool IsPieceWorn(Piece piece) {
#if !WIIXL_SWITCH
    return impl::PieceActor(static_cast<int>(piece)) != nullptr;
#else
    (void)piece;
    return false;
#endif
}

// The raw armour actor for a slot, for anything not wrapped here.
inline void* GetPieceActor(Piece piece) {
#if !WIIXL_SWITCH
    return impl::PieceActor(static_cast<int>(piece));
#else
    (void)piece;
    return nullptr;
#endif
}

// Reads one worn piece's declared effect. Returns false when nothing is worn
// in that slot or its parameters are not reachable; out.worn says the same
// thing, so a caller that fills a table can ignore the return.
inline bool GetPieceEffect(Piece piece, PieceEffect& out) {
    out.worn = false;
    out.actorName = "";
    out.effect = Effect::None;
    out.level = 0;
    out.defence = 0;
    out.ancientPowUp = false;
    out.climbWaterfall = false;
    out.spinAttack = false;

#if !WIIXL_SWITCH
    void* actor = impl::PieceActor(static_cast<int>(piece));
    if (!actor) return false;

    const char* name = *reinterpret_cast<const char**>(
        reinterpret_cast<uintptr_t>(actor) + 0x04);
    if (name && impl::PlausiblePointer(reinterpret_cast<uintptr_t>(name)) && name[0]) {
        out.actorName = name;
    }

    uintptr_t effectObject = impl::ParamObject(actor, impl::kObjectArmorEffect);
    if (!effectObject) return false;

    const char* type = *reinterpret_cast<const char**>(effectObject + impl::kEffectType);
    out.worn = true;
    out.effect = EffectFromName(type);
    out.level = *reinterpret_cast<int32_t*>(effectObject + impl::kEffectLevel);
    out.ancientPowUp = *reinterpret_cast<uint8_t*>(effectObject + impl::kAncientPowUp) != 0;
    out.climbWaterfall = *reinterpret_cast<uint8_t*>(effectObject + impl::kClimbWaterfall) != 0;
    out.spinAttack = *reinterpret_cast<uint8_t*>(effectObject + impl::kSpinAttack) != 0;

    uintptr_t armorObject = impl::ParamObject(actor, impl::kObjectArmor);
    if (armorObject) {
        out.defence = *reinterpret_cast<int32_t*>(armorObject + impl::kDefenceAddLevel);
    }
    return true;
#else
    (void)piece;
    return false;
#endif
}

// Rewrites the player's cached armour-effect block from the game's own
// queries, which is what makes a change to a piece visible to gameplay.
//
// This does NOT call the game's updateArmorEffects (0x02d58cc4). That function
// recomputes the same cache but also drives PlayerInfo, a pair of sound/effect
// requests and half a dozen further setters, all keyed to an equipment change
// that did not happen here. Rewriting the words it stores is the part that
// matters and the part with no side effects; the trade is that anything the
// game only does on a real equip (VFX, the model) is not re-triggered.
//
// Called for you by SetPieceEffect / SetArmourEffects, so a caller only needs
// this after writing an actor's parameters by hand.
inline bool Refresh() {
#if !WIIXL_SWITCH
    void* player = Player::GetRaw();
    if (!player || !impl::PlausiblePointer(reinterpret_cast<uintptr_t>(player))) return false;
    if (!impl::Holder()) return false;

    uintptr_t p = reinterpret_cast<uintptr_t>(player);

    auto storeInt = [p](uintptr_t offset, int value) {
        *reinterpret_cast<int32_t*>(p + offset) = value;
    };
    auto storeBool = [p](uintptr_t offset, bool value) {
        *reinterpret_cast<uint8_t*>(p + offset) = value ? 1 : 0;
    };

    storeInt(impl::kCacheResistHot, impl::RunQuery(impl::kQueryResistHot));
    storeInt(impl::kCacheResistCold, impl::RunQuery(impl::kQueryResistCold));
    storeInt(impl::kCacheResistBurn, impl::RunQuery(impl::kQueryResistBurn));
    storeInt(impl::kCacheResistFreeze, impl::RunQuery(impl::kQueryResistFreeze));
    storeInt(impl::kCacheResistElectric, impl::RunQuery(impl::kQueryResistElectric));
    storeInt(impl::kCacheResistLightning, impl::RunQuery(impl::kQueryResistLightning));
    storeInt(impl::kCacheAttackUp, impl::RunQuery(impl::kQueryAttackUp));
    storeInt(impl::kCacheDefence, impl::RunQuery(impl::kQueryDefence));
    storeInt(impl::kCacheSwimSpeed, impl::RunQuery(impl::kQuerySwimSpeed));
    storeInt(impl::kCacheClimbSpeed, impl::RunQuery(impl::kQueryClimbSpeed));
    storeInt(impl::kCacheClimbSpeedHorizontal,
             impl::RunQuery(impl::kQueryClimbSpeedHorizontal));

    // The game stores this one as a float, through the standard PPC
    // signed-int-to-double conversion - so an int store here would be read as
    // a denormal, the same trap Pouch::ModifierWantsFloat documents.
    *reinterpret_cast<float*>(p + impl::kCacheQuietness) =
        static_cast<float>(impl::RunQuery(impl::kQueryQuietness));

    // Three the game narrows to a byte itself: it compares the summed level
    // against 0 and stores the flag, not the level.
    storeBool(impl::kCacheSandMove, impl::RunQuery(impl::kQuerySandMove) > 0);
    storeBool(impl::kCacheSnowMove, impl::RunQuery(impl::kQuerySnowMove) > 0);
    storeBool(impl::kCacheWakeWind, impl::RunQuery(impl::kQueryWakeWind) != 0);

    storeBool(impl::kCacheSpinAttack, impl::RunQuery(impl::kQuerySpinAttack) != 0);
    storeBool(impl::kCacheClimbWaterfall, impl::RunQuery(impl::kQueryClimbWaterfall) != 0);
    return true;
#else
    return false;
#endif
}

// Writes one worn piece's effect and level.
//
// This is the real primitive: the effect a piece grants is a parameter on its
// actor and nothing else holds a copy, so this is the whole write. Pass
// Effect::None (level is then ignored and zeroed) to strip a piece.
//
// Two things worth knowing before using it:
//
//   * GParamList is cached per actor RESOURCE, not per instance, so this is a
//     change to "what Armor_002_Head grants" for as long as that resource
//     stays loaded - including any other copy of that actor in the world. It
//     is not saved and does not survive the resource being unloaded, which
//     unequipping the piece will eventually do.
//   * The level is summed with the other two pieces. Setting Head to 3 when
//     Upper already grants 2 of the same effect gives Link 5, not 3. Use
//     SetArmourEffects when you want a total.
//
// Returns false when the slot is empty, its actor is still loading, or the
// effect name table has not been built yet.
inline bool SetPieceEffect(Piece piece, Effect effect, int level) {
#if !WIIXL_SWITCH
    void* actor = impl::PieceActor(static_cast<int>(piece));
    if (!actor) return false;

    uintptr_t object = impl::ParamObject(actor, impl::kObjectArmorEffect);
    if (!object) return false;

    const char* name = impl::TableName(static_cast<int>(effect));
    if (!name) return false;

    // The vtable at +0x30 is left alone deliberately - the comparison in
    // 0x0328c7f4 dispatches through it, and it is the game's own resolver.
    *reinterpret_cast<const char**>(object + impl::kEffectType) = name;
    *reinterpret_cast<int32_t*>(object + impl::kEffectLevel) =
        effect == Effect::None ? 0 : level;

    Refresh();
    return true;
#else
    (void)piece; (void)effect; (void)level;
    return false;
#endif
}

// Writes the three booleans on one worn piece. Separate from the effect
// because they are independent parameters, not levels: a piece can carry both.
inline bool SetPieceFlags(Piece piece, bool ancientPowUp, bool climbWaterfall,
                          bool spinAttack) {
#if !WIIXL_SWITCH
    void* actor = impl::PieceActor(static_cast<int>(piece));
    if (!actor) return false;

    uintptr_t object = impl::ParamObject(actor, impl::kObjectArmorEffect);
    if (!object) return false;

    *reinterpret_cast<uint8_t*>(object + impl::kAncientPowUp) = ancientPowUp ? 1 : 0;
    *reinterpret_cast<uint8_t*>(object + impl::kClimbWaterfall) = climbWaterfall ? 1 : 0;
    *reinterpret_cast<uint8_t*>(object + impl::kSpinAttack) = spinAttack ? 1 : 0;

    Refresh();
    return true;
#else
    (void)piece; (void)ancientPowUp; (void)climbWaterfall; (void)spinAttack;
    return false;
#endif
}

// Sets one worn piece's own defence contribution (armorDefenceAddLevel). The
// player's total is the sum across the three, cached at player+0x1a74.
inline bool SetPieceDefence(Piece piece, int defence) {
#if !WIIXL_SWITCH
    void* actor = impl::PieceActor(static_cast<int>(piece));
    if (!actor) return false;

    uintptr_t object = impl::ParamObject(actor, impl::kObjectArmor);
    if (!object) return false;

    *reinterpret_cast<int32_t*>(object + impl::kDefenceAddLevel) = defence;
    Refresh();
    return true;
#else
    (void)piece; (void)defence;
    return false;
#endif
}

// The level Link currently has for one effect, from the game's own query - so
// it includes the set bonus and resolves the combined "AAndB" declarations.
//
// Effect::None and the four combo values have no query of their own and return
// 0; ask for the component effect instead.
inline int GetArmourEffect(Effect effect) {
#if !WIIXL_SWITCH
    switch (effect) {
        case Effect::ResistHot: return impl::RunQuery(impl::kQueryResistHot);
        case Effect::ResistBurn: return impl::RunQuery(impl::kQueryResistBurn);
        case Effect::ResistCold: return impl::RunQuery(impl::kQueryResistCold);
        case Effect::ResistFreeze: return impl::RunQuery(impl::kQueryResistFreeze);
        case Effect::ResistElectric: return impl::RunQuery(impl::kQueryResistElectric);
        case Effect::ResistLightning: return impl::RunQuery(impl::kQueryResistLightning);
        case Effect::ResistAncient: return impl::RunQuery(impl::kQueryResistAncient);
        case Effect::SwimSpeed: return impl::RunQuery(impl::kQuerySwimSpeed);
        case Effect::ClimbSpeed: return impl::RunQuery(impl::kQueryClimbSpeed);
        case Effect::ClimbSpeedHorizontalOnly:
            return impl::RunQuery(impl::kQueryClimbSpeedHorizontal);
        case Effect::AttackUp: return impl::RunQuery(impl::kQueryAttackUp);
        case Effect::Quietness: return impl::RunQuery(impl::kQueryQuietness);
        case Effect::SandMove: return impl::RunQuery(impl::kQuerySandMove);
        case Effect::SnowMove: return impl::RunQuery(impl::kQuerySnowMove);
        case Effect::WakeWind: return impl::RunQuery(impl::kQueryWakeWind);
        case Effect::BeamPowerUp: return impl::RunQuery(impl::kQueryBeamPowerUp);
        default: return 0;
    }
#else
    (void)effect;
    return 0;
#endif
}

// Everything at once. Returns false when there is no player yet, in which case
// out is zeroed rather than left as it was.
inline bool GetArmourEffects(Effects& out) {
    out.resistHot = 0; out.resistBurn = 0; out.resistCold = 0;
    out.resistFreeze = 0; out.resistElectric = 0; out.resistLightning = 0;
    out.resistAncient = 0; out.swimSpeed = 0; out.climbSpeed = 0;
    out.climbSpeedHorizontalOnly = 0; out.attackUp = 0; out.quietness = 0;
    out.sandMove = 0; out.snowMove = 0; out.wakeWind = 0; out.beamPowerUp = 0;
    out.defence = 0; out.climbWaterfall = false; out.spinAttack = false;
    out.setBonusFlags = 0;

#if !WIIXL_SWITCH
    void* holder = impl::Holder();
    if (!holder) return false;

    out.resistHot = impl::RunQuery(impl::kQueryResistHot);
    out.resistBurn = impl::RunQuery(impl::kQueryResistBurn);
    out.resistCold = impl::RunQuery(impl::kQueryResistCold);
    out.resistFreeze = impl::RunQuery(impl::kQueryResistFreeze);
    out.resistElectric = impl::RunQuery(impl::kQueryResistElectric);
    out.resistLightning = impl::RunQuery(impl::kQueryResistLightning);
    out.resistAncient = impl::RunQuery(impl::kQueryResistAncient);
    out.swimSpeed = impl::RunQuery(impl::kQuerySwimSpeed);
    out.climbSpeed = impl::RunQuery(impl::kQueryClimbSpeed);
    out.climbSpeedHorizontalOnly = impl::RunQuery(impl::kQueryClimbSpeedHorizontal);
    out.attackUp = impl::RunQuery(impl::kQueryAttackUp);
    out.quietness = impl::RunQuery(impl::kQueryQuietness);
    out.sandMove = impl::RunQuery(impl::kQuerySandMove);
    out.snowMove = impl::RunQuery(impl::kQuerySnowMove);
    out.wakeWind = impl::RunQuery(impl::kQueryWakeWind);
    out.beamPowerUp = impl::RunQuery(impl::kQueryBeamPowerUp);
    out.defence = impl::RunQuery(impl::kQueryDefence);
    out.climbWaterfall = impl::RunQuery(impl::kQueryClimbWaterfall) != 0;
    out.spinAttack = impl::RunQuery(impl::kQuerySpinAttack) != 0;
    out.setBonusFlags = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(holder) + impl::kHolderSetBonus);
    return true;
#else
    return false;
#endif
}

// Makes Link's TOTAL for one effect exactly level, by putting it on the first
// worn piece and clearing the other two.
//
// Clearing the others is the point, not a side effect: levels are summed, so
// leaving a piece that already grants the same effect would overshoot. It does
// mean the two cleared pieces lose whatever they declared - which is the
// honest reading of "set the armour effects to X", but is destructive, so
// SetPieceEffect is the call to reach for when you want to keep the rest.
//
// Pass Effect::None to strip all three.
//
// Returns false when nothing is worn at all. A set bonus can still push the
// total above level for a few effects (ResistHot gets +1 from Cb_ResistHot
// when no piece grants it) - GetArmourEffect afterwards is the truth.
inline bool SetArmourEffects(Effect effect, int level) {
#if !WIIXL_SWITCH
    bool placed = false;
    bool any = false;

    for (int i = 0; i < impl::kPieceCount; ++i) {
        Piece piece = static_cast<Piece>(i);
        if (!IsPieceWorn(piece)) continue;
        any = true;

        if (!placed && effect != Effect::None) {
            if (SetPieceEffect(piece, effect, level)) placed = true;
        } else {
            SetPieceEffect(piece, Effect::None, 0);
        }
    }

    if (any) Refresh();
    return any;
#else
    (void)effect; (void)level;
    return false;
#endif
}

// --- extra effects, past the one-per-piece limit --------------------------
//
// A piece of armour stores exactly ONE effect: armorEffectEffectType is a
// single name at obj+0x2c with a single level at +0x40. The game's only way
// past that is the combo table at 0x1054cbb8, six entries of
// { declaredIndex, componentA, componentB } letting one declared value count
// for two effects - and it is hard-capped at two, because
// getArmorEffectLevelIfType (0x0328c7f4) finds the one entry matching the
// declared value and then compares the target against exactly entry[1] and
// entry[2]. The table cannot be lengthened either: its terminator 0x1054cc00
// is compiled in, and other data begins there.
//
// So more than two effects on one piece cannot come from the game's data. It
// can come from that function, which is the single choke point every aggregate
// query calls: hooking it means the game's own updateArmorEffects does the
// summing, the player cache rebuilds itself, and set bonuses still apply. That
// is why this is a hook rather than a per-frame write into the cache at
// player+0x1a50 - the cache route fights the game every frame and has no slot
// at all for ResistAncient or BeamPowerUp.
//
// The table is ADDITIVE: the hook returns the piece's real level plus whatever
// is stored here, so a piece keeps its declared effect and extras stack on top.
// Nothing is saved, and it is keyed by slot rather than by actor, so it follows
// whatever is worn in that slot.
//
// Not covered, because they do not route through 0x0328c7f4: defence
// (armorDefenceAddLevel, its own accessor on obj[0x18]) and the three booleans.
// Both already have their own writes.

constexpr int kEffectSlots = static_cast<int>(Effect::ClimbSpeedAndBeamPowerUp) + 1;

namespace impl {

constexpr uintptr_t kEffectLevelIfTypeWiiU = 0x0328c7f4;

inline int32_t (&ExtraTable())[kPieceCount][kEffectSlots] {
    static int32_t table[kPieceCount][kEffectSlots] = {};
    return table;
}

// True when anything is set, so the hook can bail before doing any work in the
// overwhelmingly common case of no extras at all. updateArmorEffects runs every
// frame and calls through here seventeen times per worn piece.
inline bool& ExtraAny() { static bool any = false; return any; }
inline bool& ExtraHookInstalled() { static bool installed = false; return installed; }

inline void RecomputeExtraAny() {
    auto& table = ExtraTable();
    for (int piece = 0; piece < kPieceCount; ++piece) {
        for (int effect = 0; effect < kEffectSlots; ++effect) {
            if (table[piece][effect] != 0) { ExtraAny() = true; return; }
        }
    }
    ExtraAny() = false;
}

// Which slot this actor is worn in, or -1. Resolved by asking the holder rather
// than by caching pointers, so a piece swapped in the meantime cannot leave a
// stale match behind.
inline int PieceOfActor(const void* actor) {
#if !WIIXL_SWITCH
    if (!actor) return -1;
    for (int i = 0; i < kPieceCount; ++i) {
        if (PieceActor(i) == actor) return i;
    }
#else
    (void)actor;
#endif
    return -1;
}

// The effect a sead::SafeString target names. The queries pass pointers out of
// the game's own name table, so EffectFromName's walk hits on the first
// comparison; the string compare is only there for a caller that built its own.
inline int EffectOfTarget(const void* target);

WIIXL_HOOK_DEFINE_TRAMPOLINE(EffectLevelHook) {
    static int Callback(void* actor, void* target) {
        const int real = Orig(actor, target);
        if (!ExtraAny()) return real;

        const int piece = PieceOfActor(actor);
        if (piece < 0) return real;

        const int effect = EffectOfTarget(target);
        if (effect < 0) return real;

        return real + ExtraTable()[piece][effect];
    }
};

}  // namespace impl

// Arms the extra-effect support. Call once from WiiXLaunch_Init().
//
// Installing costs nothing until something is stored: the hook runs the
// original and tests one bool.
inline bool InitExtraEffects() {
#if !WIIXL_SWITCH
    if (impl::ExtraHookInstalled()) return true;
    impl::EffectLevelHook::Install(0x0, impl::kEffectLevelIfTypeWiiU);
    impl::ExtraHookInstalled() = true;
    return true;
#else
    return false;
#endif
}

// Stacks an extra effect on a piece, on top of whatever it declares. level 0
// removes it. Returns false for a bad slot or effect, or before Init.
inline bool SetExtraEffect(Piece piece, Effect effect, int level) {
#if !WIIXL_SWITCH
    const int slot = static_cast<int>(piece);
    const int which = static_cast<int>(effect);
    if (slot < 0 || slot >= impl::kPieceCount) return false;
    if (which <= 0 || which >= kEffectSlots) return false;
    if (!impl::ExtraHookInstalled()) return false;

    impl::ExtraTable()[slot][which] = level;
    impl::RecomputeExtraAny();
    Refresh();
    return true;
#else
    (void)piece; (void)effect; (void)level;
    return false;
#endif
}

inline int GetExtraEffect(Piece piece, Effect effect) {
#if !WIIXL_SWITCH
    const int slot = static_cast<int>(piece);
    const int which = static_cast<int>(effect);
    if (slot < 0 || slot >= impl::kPieceCount) return 0;
    if (which <= 0 || which >= kEffectSlots) return 0;
    return impl::ExtraTable()[slot][which];
#else
    (void)piece; (void)effect;
    return 0;
#endif
}

// Drops every extra on all three slots.
inline void ClearExtraEffects() {
#if !WIIXL_SWITCH
    auto& table = impl::ExtraTable();
    for (int piece = 0; piece < impl::kPieceCount; ++piece) {
        for (int effect = 0; effect < kEffectSlots; ++effect) table[piece][effect] = 0;
    }
    impl::ExtraAny() = false;
    Refresh();
#endif
}

inline bool ExtraEffectsActive() {
#if !WIIXL_SWITCH
    return impl::ExtraAny();
#else
    return false;
#endif
}

namespace impl {
// Defined after EffectFromName is available.
inline int EffectOfTarget(const void* target) {
#if !WIIXL_SWITCH
    if (!target) return -1;
    // sead::SafeString is { const char* text; const void* vtable } - the
    // inverted layout the rest of this framework already deals with.
    const char* text = *reinterpret_cast<const char* const*>(target);
    if (!text) return -1;

    const Effect effect = EffectFromName(text);
    if (effect == Effect::None) return -1;
    return static_cast<int>(effect);
#else
    (void)target;
    return -1;
#endif
}
}  // namespace impl

} // namespace WiiXLaunch::BotW::Armour
