#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include "gamedata.hpp"

// WiiXLaunch::BotW::Map - what the map has revealed: the tower regions and the
// travel destinations.
//
// Two independent systems that people tend to lump together:
//
//   REGIONS are the fifteen towers. Each owns three GameData flags -
//   MapTower_NN (bool, activated), MapTower_NN_OpenCenterPos (vec3, where the
//   revealed circle is centred) and MapTower_NN_OpenScaleLevel (s32, how far it
//   reaches). The centre reads like static configuration and the SCALE LEVEL is
//   the state, so revealing a region means raising that, not just flipping the
//   bool. The map manager caches both arrays - centres at +0x510, scale levels
//   at +0x5c4 - so a flag write is invisible until they are reloaded, which is
//   what RefreshRegions does and what every setter here calls for you.
//
//   TRAVEL DESTINATIONS are map markers. Each marker caches two flag INDICES
//   built from its own name at runtime (0x02e95e2c): Enter_<name> and
//   Clear_<name>. Shrines are named Dungeon000 upward, so a shrine's travel
//   entry is the Enter_DungeonNNN bool. Indices, not values, are cached - the
//   map reads the value live - so unlike regions these need no refresh.
//
// HONESTY WARNING on that second half. That Enter_<name> is the gate on travel
// is INFERRED, not proven: it is the flag the marker caches, it is named like
// discovery, and shrine travel unlocks on discovery - but the code that decides
// "this marker is a destination" was not traced. Divine Beasts and the DLC's
// travel points are not covered at all. Test the shrine half before relying on
// it, and see the confidence column in the symbols file.
//
// Neither half is guessy about WHICH flags exist: names are probed rather than
// counted, so a name the save does not have simply fails and is skipped. That
// is what makes the same code right for a vanilla save and a DLC one without
// hard-coding either.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Map {

static constexpr bool SupportsMap = !WIIXL_SWITCH;

// The fifteen towers, numbered 1-15 the way the flags are.
constexpr int kFirstRegion = 1;
constexpr int kRegionCount = 15;
constexpr int kLastRegion = kFirstRegion + kRegionCount - 1;

// How far SetMapUnlockAll probes for shrines. Vanilla runs Dungeon000-119 and
// the DLC adds 120-135; the extra slack costs a handful of failed name lookups
// and means a future index would still be found. Nothing is assumed to exist -
// see GetShrineCount.
constexpr int kShrineProbeLimit = 152;

namespace impl {

// The map manager. The same singleton botw/completion.hpp reads its completion
// tallies from at +0x508 and +0x50c: the tower arrays below sit immediately
// after those two words in the same object, and the loaders that fill them live
// in the same code module as the rest of the map manager's methods. The two
// loaders take `this` as an argument rather than fetching the global
// themselves, so the linkage is from contiguity rather than a load instruction
// - solid, but recorded as such.
constexpr uintptr_t kMapMgrPtrWiiU = 0x104698f8;

// 15 x sead::Vector3f, one per tower: the centre of the area that tower
// reveals. Filled by 0x02e9ec9c from the MapTower_NN_OpenCenterPos flags.
constexpr uintptr_t kTowerCentres = 0x510;

// 15 x s32, one per tower: how far the reveal reaches. Filled by 0x02e9f044,
// which builds "MapTower_%02d_OpenScaleLevel" in a loop - which is what fixes
// both this offset and the flag naming.
constexpr uintptr_t kTowerScaleLevels = 0x5c4;

// 15 x 12 bytes starting here, one per tower, each stamped with its 1-based
// tower number by 0x02e9f114. Not read by this header; recorded because the
// loop bound (0x18 to 0xcc, stride 12) is a third independent confirmation
// that fifteen is the tower count.
constexpr uintptr_t kTowerRecords = 0x18;

// The two cache loaders. Called after a flag write so the change is visible
// without waiting for the map to rebuild itself.
constexpr uintptr_t kLoadTowerCentresWiiU = 0x02e9ec9c;
constexpr uintptr_t kLoadTowerScaleLevelsWiiU = 0x02e9f044;

using LoadTowerDataFn = void (*)(void* mapMgr);

// ksys::gdt: an s32 flag's declared maximum by name. The same call
// botw/completion.hpp uses to read the Korok cap; duplicated rather than
// cross-included, matching how this framework already repeats shared addresses.
// It takes the core hanging off the gdt manager at +0x70c, not the +0x700 read
// core gamedata.hpp uses.
constexpr uintptr_t kGdtMgrPtrWiiU = 0x1046d5b0;
constexpr uintptr_t kFlagCoreOffset = 0x70c;
constexpr uintptr_t kGetFlagMaxByNameWiiU = 0x0321072c;

using GetFlagMaxFn = int (*)(void* core, int* out, const void* name);

inline bool PlausiblePointer(uintptr_t addr) {
    return addr >= 0x10000000 && addr < 0xa0000000;
}

inline void* MapMgr() {
#if !WIIXL_SWITCH
    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kMapMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return nullptr;
    return reinterpret_cast<void*>(mgr);
#else
    return nullptr;
#endif
}

inline bool FlagMax(const char* name, int& out) {
#if !WIIXL_SWITCH
    if (!name) return false;

    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kGdtMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return false;

    uintptr_t slot = *reinterpret_cast<uintptr_t*>(mgr + kFlagCoreOffset);
    if (!PlausiblePointer(slot)) return false;

    uintptr_t core = *reinterpret_cast<uintptr_t*>(slot);
    if (!PlausiblePointer(core)) return false;

    auto get = WiiXLaunch::GetTargetFunction<GetFlagMaxFn>(0x0, kGetFlagMaxByNameWiiU);
    if (!get) return false;

    GameData::impl::SafeString key = GameData::impl::MakeSafeString(name);
    int value = 0;
    if (get(reinterpret_cast<void*>(core), &value, &key) == 0) return false;

    out = value;
    return true;
#else
    (void)name; (void)out;
    return false;
#endif
}

// Zero-padded decimal appended to p.
inline char* AppendPadded(char* p, char* end, int value, int digits) {
    char tmp[12];
    int n = 0;
    if (value < 0) value = 0;
    do {
        tmp[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && n < 11);
    while (n < digits && n < 11) tmp[n++] = '0';
    while (n > 0 && p < end) *p++ = tmp[--n];
    return p;
}

// Builds "<prefix><zero-padded index><suffix>" into a shared buffer.
//
// One buffer, so the result is only valid until the next call - every caller
// here consumes it immediately. Fine on the game thread, which is the only
// place any of this is meant to run.
inline const char* FlagName(const char* prefix, int index, int digits,
                            const char* suffix = nullptr) {
    static char buf[64];
    char* p = buf;
    char* const end = buf + sizeof(buf) - 1;

    for (const char* s = prefix; s && *s && p < end; ++s) *p++ = *s;
    p = AppendPadded(p, end, index, digits);
    for (const char* s = suffix; s && *s && p < end; ++s) *p++ = *s;

    *p = '\0';
    return buf;
}

// "MapTower_07"
inline const char* TowerFlag(int tower) { return FlagName("MapTower_", tower, 2); }

// "MapTower_07_OpenScaleLevel"
inline const char* TowerScaleFlag(int tower) {
    return FlagName("MapTower_", tower, 2, "_OpenScaleLevel");
}

// "Location_MapTower07" - note the game's own inconsistency: this family has no
// underscore before the number, while MapTower_07 does.
inline const char* TowerLocationFlag(int tower) {
    return FlagName("Location_MapTower", tower, 2);
}

// "Enter_Dungeon042"
inline const char* ShrineFlag(int shrine) {
    return FlagName("Enter_Dungeon", shrine, 3);
}

inline bool ValidRegion(int tower) {
    return tower >= kFirstRegion && tower <= kLastRegion;
}

} // namespace impl

// Whether the map manager is up. False on the title screen and early boot.
inline bool IsAvailable() {
#if !WIIXL_SWITCH
    return impl::MapMgr() != nullptr;
#else
    return false;
#endif
}

// Reloads the two tower caches from GameData, which is what makes a region
// change visible without waiting for the map to rebuild itself.
//
// Calls the game's own two loaders and nothing else. The full map reload
// (0x02ea1628) runs eight more of these and takes a second argument that was
// never identified, so it is deliberately left alone - the two that matter for
// regions are small, self-contained, and only write the arrays they own.
inline bool RefreshRegions() {
#if !WIIXL_SWITCH
    void* mgr = impl::MapMgr();
    if (!mgr) return false;

    auto centres = WiiXLaunch::GetTargetFunction<impl::LoadTowerDataFn>(
        0x0, impl::kLoadTowerCentresWiiU);
    auto scales = WiiXLaunch::GetTargetFunction<impl::LoadTowerDataFn>(
        0x0, impl::kLoadTowerScaleLevelsWiiU);
    if (!centres || !scales) return false;

    centres(mgr);
    scales(mgr);
    return true;
#else
    return false;
#endif
}

// --- regions -------------------------------------------------------------

// Whether a tower's region is revealed. tower is 1-15.
inline bool GetMapRegionUnlock(int tower, bool& out) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;
    return GameData::GetFlagBool(impl::TowerFlag(tower), out);
#else
    (void)tower; (void)out;
    return false;
#endif
}

// A tower's reveal reach, as the game stores it.
inline bool GetMapRegionScaleLevel(int tower, int& out) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;
    return GameData::GetFlagS32(impl::TowerScaleFlag(tower), out);
#else
    (void)tower; (void)out;
    return false;
#endif
}

// The scale level as the MAP currently has it cached, which is what is actually
// being drawn. Differs from GetMapRegionScaleLevel only between a flag write
// and a RefreshRegions - handy for telling "the write did not land" apart from
// "the map has not noticed yet".
inline bool GetCachedRegionScaleLevel(int tower, int& out) {
#if !WIIXL_SWITCH
    void* mgr = impl::MapMgr();
    if (!mgr || !impl::ValidRegion(tower)) return false;

    const int slot = tower - kFirstRegion;
    out = *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(mgr) +
                                      impl::kTowerScaleLevels + 4 * slot);
    return true;
#else
    (void)tower; (void)out;
    return false;
#endif
}

// Sets a tower's reveal reach directly, for callers that want partial reveals
// rather than all-or-nothing. Refreshes the map's cache.
inline bool SetMapRegionScaleLevel(int tower, int level) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;
    if (!GameData::SetFlagS32(impl::TowerScaleFlag(tower), level)) return false;
    RefreshRegions();
    return true;
#else
    (void)tower; (void)level;
    return false;
#endif
}

// Reveals or hides a tower's region, tower 1-15.
//
// Writes three flags, because "unlocked" means all three to the game:
//
//   MapTower_NN                   the tower counts as activated
//   MapTower_NN_OpenScaleLevel    the reveal reach - set to the flag's own
//                                 declared maximum when unlocking, 0 when not
//   Location_MapTowerNN           the tower's marker and name on the map
//
// The reach is read from the flag's declared maximum rather than hard-coded, so
// this stays right if the DLC or a future version changes it. If that maximum
// cannot be read the reach is left alone and this returns false, rather than
// guessing a number and half-revealing the region.
//
// Refreshes the map's cache, so the change shows without reopening the map.
inline bool SetMapRegionUnlock(int tower, bool unlocked) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;

    bool ok = GameData::SetFlagBool(impl::TowerFlag(tower), unlocked);

    int level = 0;
    if (unlocked) {
        int max = 0;
        if (!impl::FlagMax(impl::TowerScaleFlag(tower), max) || max <= 0) ok = false;
        else level = max;
    }
    if (ok && !GameData::SetFlagS32(impl::TowerScaleFlag(tower), level)) ok = false;

    // The marker is cosmetic next to the reveal, so a failure here is reported
    // but does not stop the rest.
    if (!GameData::SetFlagBool(impl::TowerLocationFlag(tower), unlocked)) ok = false;

    RefreshRegions();
    return ok;
#else
    (void)tower; (void)unlocked;
    return false;
#endif
}

// Every region at once. Returns how many towers were fully written; 15 means
// all of them landed.
inline int SetMapRegionUnlockAll(bool unlocked) {
#if !WIIXL_SWITCH
    int done = 0;
    for (int tower = kFirstRegion; tower <= kLastRegion; ++tower) {
        if (SetMapRegionUnlock(tower, unlocked)) ++done;
    }
    return done;
#else
    (void)unlocked;
    return 0;
#endif
}

// How many of the fifteen are revealed.
inline int CountUnlockedRegions() {
#if !WIIXL_SWITCH
    int count = 0;
    for (int tower = kFirstRegion; tower <= kLastRegion; ++tower) {
        bool value = false;
        if (GetMapRegionUnlock(tower, value) && value) ++count;
    }
    return count;
#else
    return 0;
#endif
}

// --- travel destinations -------------------------------------------------
//
// INFERRED, all of it - see the warning at the top of this header. The flag is
// the one each shrine's map marker caches, which is strong circumstantial
// evidence and not a proof.

// Whether a shrine is a travel destination. shrine is the Dungeon index, so 0
// is Dungeon000. Returns false when no flag of that index exists, which is also
// how you find out where the range ends.
inline bool GetMapUnlock(int shrine, bool& out) {
#if !WIIXL_SWITCH
    if (shrine < 0) return false;
    return GameData::GetFlagBool(impl::ShrineFlag(shrine), out);
#else
    (void)shrine; (void)out;
    return false;
#endif
}

// Unlocks or locks one shrine as a travel destination.
//
// A real, save-backed write to Enter_DungeonNNN. Be deliberate: that flag reads
// as "you have been inside this shrine", so anything else keyed off it moves
// too. Unlocking every shrine is not the same as having visited them.
inline bool SetMapUnlock(int shrine, bool unlocked) {
#if !WIIXL_SWITCH
    if (shrine < 0) return false;
    return GameData::SetFlagBool(impl::ShrineFlag(shrine), unlocked);
#else
    (void)shrine; (void)unlocked;
    return false;
#endif
}

// How many shrine flags the loaded save actually has - probed, not assumed, by
// walking indices until they stop resolving. Vanilla answers 120 and a DLC save
// 136, without either number appearing here.
inline int GetShrineCount() {
#if !WIIXL_SWITCH
    int count = 0;
    for (int i = 0; i < kShrineProbeLimit; ++i) {
        bool value = false;
        if (!GameData::GetFlagBool(impl::ShrineFlag(i), value)) continue;
        count = i + 1;
    }
    return count;
#else
    return 0;
#endif
}

// Every shrine at once. Returns how many flags were written, which is also how
// many exist - indices with no flag are skipped rather than counted.
inline int SetMapUnlockAll(bool unlocked) {
#if !WIIXL_SWITCH
    int done = 0;
    for (int i = 0; i < kShrineProbeLimit; ++i) {
        bool value = false;
        if (!GameData::GetFlagBool(impl::ShrineFlag(i), value)) continue;
        if (SetMapUnlock(i, unlocked)) ++done;
    }
    return done;
#else
    (void)unlocked;
    return 0;
#endif
}

// How many shrines are currently travel destinations.
inline int CountUnlockedShrines() {
#if !WIIXL_SWITCH
    int count = 0;
    for (int i = 0; i < kShrineProbeLimit; ++i) {
        bool value = false;
        if (GetMapUnlock(i, value) && value) ++count;
    }
    return count;
#else
    return 0;
#endif
}

} // namespace WiiXLaunch::BotW::Map
