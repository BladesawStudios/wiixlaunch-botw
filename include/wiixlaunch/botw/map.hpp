#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/hook.hpp>
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
//   TRAVEL DESTINATIONS are map markers, and there is more than one KIND of
//   marker. The one that caches Enter_<name>/Clear_<name> flag indices
//   (0x02e95e2c, indices at +0x60 and +0x64, name at +0x58) turns out to be the
//   DIVINE BEAST marker: tracked live, the only objects that ever reach its
//   state update are RemainsWind, RemainsElectric, RemainsFire and RemainsWater,
//   two instances each. Its state update 0x02e9609c folds those flags into bits
//   at +0x2c - 0x20000 for Enter_, 0x80000 for Clear_.
//
//   SHRINES are a different class. Its setup (0x02e93538) strips the Location_
//   prefix, matches Dungeon, and parses the digits into an s32 at +0x54, with
//   +0x58/+0x5c/+0x60 initialised to -1. Name at +0x4c, not +0x58. So a shrine
//   marker does not carry the Enter_/Clear_ index pair at all.
//
// CORRECTION, measured. This header used to say a shrine's travel entry was the
// Enter_DungeonNNN bool. That was inferred from the wrong marker class and it
// does not hold: clearing Enter_Dungeon009 on a finished save sticks - the flag
// really does go false - and the shrine stays on the map as a travel point.
// Whatever gates shrine travel has not been found. SetMapUnlock below still
// writes a real flag, and that flag really does mean "you have been inside this
// shrine", but do not expect it to move the map.
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
    // 0xf0000000, not 0xa0000000. This reads the SAME map manager singleton
    // completion.hpp does, and there the lower cap rejected it outright - the
    // manager is allocated above 0xa0000000 and every read looked like "the
    // game has not built it yet". Measured, not theoretical.
    return addr >= 0x10000000 && addr < 0xf0000000;
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
    // 65 and aligned, not 64. `end` below is a real relocation to an interior
    // byte of this buffer, and the Cemu packager can only label a WORD-ALIGNED
    // target - at 64 bytes it lands on buf+0x3f and the build fails outright.
    // At 65 it is buf+0x40. Usable capacity is unchanged; completion.hpp's
    // FormatOverride needed the same treatment.
    alignas(4) static char buf[65];
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

// Writes a bool flag, optionally past the guards that stop it.
//
// Needed because these flags LATCH. MapTower_NN and Enter_DungeonNNN are both
// IsOneTrigger, so the ordinary setter happily sets them true and refuses to
// clear them - locking a region failed outright while unlocking worked, which
// reads as a broken setter rather than as the latch it is.
//
// Defaulting force off matches gamedata.hpp: latching is the game's own rule
// about its own save data, so the default respects it and stepping outside has
// to be asked for.
inline bool WriteBoolFlag(const char* name, bool value, bool force,
                          bool bypassPermission) {
#if !WIIXL_SWITCH
    return force ? GameData::SetFlagBoolForced(name, value, bypassPermission)
                 : GameData::SetFlagBool(name, value);
#else
    (void)name; (void)value; (void)force; (void)bypassPermission;
    return false;
#endif
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
// Calls the game's own two loaders and nothing else, and that is the right
// call for a different reason than first assumed.
//
// 0x02ea1628 is NOT a fuller version of this. Its unidentified second argument
// is a sead HEAP: it hands that argument to 0x0308e60c(size, heap, align, name)
// over and over, allocating the manager's marker pools. It is construction, not
// a reload, and calling it on a live map would re-allocate everything out from
// under the UI. The one branch to it comes from a small wrapper at 0x02f5dad4
// that fetches the manager and tail-calls it.
//
// The two loaders used here are the opposite: small, self-contained, and they
// only overwrite the arrays they own.
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

// The tower's marker - its pin and name on the map, and what makes it a travel
// destination. SEPARATE from the reveal: the region can be revealed with no
// marker on it, and a marker can sit on unrevealed ground.
//
// Location_MapTowerNN is an s32 holding an ID, NOT a 0/1 flag. Measured on a
// finished save the fifteen towers read 3, 14, 15, 16, 17, 19, 30, 32, 36, 64,
// 69 and so on - scattered, not sequential, and not per-tower constants. Zero
// hides the marker; writing 1 produces a valid but WRONG marker (it draws
// orange, the undiscovered styling). What the number means is not traced -
// the scatter fits a discovery-order counter shared across every Location_*
// flag in the game, but that is a reading of the values, not a proof.
//
// So treat the original value as data worth keeping: read it before you
// overwrite it, because nothing here can reconstruct it.
inline bool GetMapRegionMarker(int tower, int& out) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;
    return GameData::GetFlagS32(impl::TowerLocationFlag(tower), out);
#else
    (void)tower; (void)out;
    return false;
#endif
}

inline bool SetMapRegionMarker(int tower, int id) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;
    return GameData::SetFlagS32(impl::TowerLocationFlag(tower), id);
#else
    (void)tower; (void)id;
    return false;
#endif
}

// Whether the tower counts as activated, on its own. The reveal and the marker
// both stay where they are.
inline bool SetMapRegionActivated(int tower, bool activated, bool force = false,
                                  bool bypassPermission = false) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;
    return impl::WriteBoolFlag(impl::TowerFlag(tower), activated, force,
                               bypassPermission);
#else
    (void)tower; (void)activated; (void)force; (void)bypassPermission;
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
//   Location_MapTowerNN           the tower's marker and name on the map.
//                                 An s32, NOT a bool - it lives in the s32
//                                 store, so a SetFlagBool on it just fails.
//
// The reach is read from the flag's declared maximum rather than hard-coded, so
// this stays right if the DLC or a future version changes it. If that maximum
// cannot be read the reach is left alone and this returns false, rather than
// guessing a number and half-revealing the region.
//
// Refreshes the map's cache, so the change shows without reopening the map.
inline bool SetMapRegionUnlock(int tower, bool unlocked, bool force = false,
                               bool bypassPermission = false) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(tower)) return false;

    bool ok = impl::WriteBoolFlag(impl::TowerFlag(tower), unlocked, force,
                                  bypassPermission);

    int level = 0;
    if (unlocked) {
        int max = 0;
        if (!impl::FlagMax(impl::TowerScaleFlag(tower), max) || max <= 0) ok = false;
        else level = max;
    }
    if (ok && !GameData::SetFlagS32(impl::TowerScaleFlag(tower), level)) ok = false;

    // The marker is cosmetic next to the reveal, so a failure here is reported
    // but does not stop the rest.
    //
    // 1/0 is an assumption: this is an s32 rather than the bool the name
    // suggests, and what a discovered location stores was not traced. Compare
    // Location_MapTowerNN on a save that has the tower to confirm.
    if (!GameData::SetFlagS32(impl::TowerLocationFlag(tower), unlocked ? 1 : 0)) ok = false;

    RefreshRegions();
    return ok;
#else
    (void)tower; (void)unlocked; (void)force; (void)bypassPermission;
    return false;
#endif
}

// Every region at once. Returns how many towers were fully written; 15 means
// all of them landed.
inline int SetMapRegionUnlockAll(bool unlocked, bool force = false,
                                 bool bypassPermission = false) {
#if !WIIXL_SWITCH
    int done = 0;
    for (int tower = kFirstRegion; tower <= kLastRegion; ++tower) {
        if (SetMapRegionUnlock(tower, unlocked, force, bypassPermission)) ++done;
    }
    return done;
#else
    (void)unlocked; (void)force; (void)bypassPermission;
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

// --- Divine Beast travel markers ------------------------------------------
//
// The problem this solves: clearing Enter_DungeonNNN does nothing visible. The
// flag write lands and the map goes on showing the shrine, which reads as "the
// flag is not the one" and is not.
//
// What the map actually reads is a BIT, not the flag. 0x02e9609c is the
// marker's state update: it queries the two indices cached at +0x60 and +0x64
// and folds the answers into the flag word at +0x2c - Enter_<name> sets
// 0x20000, Clear_<name> sets 0x80000, each cleared when its flag is false.
// Nothing re-runs it after a flag write, so the bit keeps whatever it was built
// with. Re-running it is the whole fix.
//
// Getting at the markers is the awkward part. 0x02e9609c has no direct callers
// - it is reached only through vtable slot 0x1021a664 - and the manager's
// marker pool is a sead::Buffer of pointers at +0x1ec threaded through separate
// node pools, which is more structure than is worth deriving when getting it
// wrong means walking a bad pointer.
//
// So the markers are collected rather than located: the hook below sits on the
// state update and records each `this` the game passes it. Whatever the game
// considers a marker, we have a pointer to it, without knowing how the
// container is shaped. RefreshBeastMarkers then replays the same function over what
// was collected.
//
// SCOPE, measured rather than assumed - and narrower than this started out.
// The only objects that ever reach 0x02e9609c are the four Divine Beast
// markers, two instances each, eight in total, and that does not grow however
// long the map is left open. Shrines are a different class entirely
// (0x02e93538, name at +0x4c, shrine number at +0x54) and never pass through
// here. Hence the Beast naming: these functions are not a general marker
// refresh and calling them will not move a shrine.
//
// The secondary limitation: this only knows about markers the game has updated
// at least once since the hook was installed, so TrackedBeastMarkers() reads
// zero until the game has touched them.

namespace impl {

// The marker state update. Signature is (marker) and it returns nothing.
constexpr uintptr_t kBeastMarkerUpdateStateWiiU = 0x02e9609c;

// The marker fields this reads. +0x58 is the sead::SafeString the flag names
// are built from; +0x2c is the state word 0x02e9609c writes.
constexpr uintptr_t kBeastMarkerName = 0x58;
constexpr uintptr_t kBeastMarkerFlags = 0x2c;
constexpr uint32_t kBeastMarkerEnteredBit = 0x20000;   // Enter_<name>
constexpr uint32_t kBeastMarkerClearedBit = 0x80000;   // Clear_<name>

// Vanilla has 120 shrines, the DLC 136, plus towers, villages, stables and the
// rest - a few hundred markers in total. 1024 is slack over that; going over
// just means the newest are not tracked, not that anything breaks.
constexpr int kBeastMarkerTrackLimit = 1024;

inline void** BeastMarkerTable() {
    static void* table[kBeastMarkerTrackLimit] = {};
    return table;
}

inline int& BeastMarkerCount() { static int count = 0; return count; }
inline bool& BeastMarkerHookInstalled() { static bool installed = false; return installed; }

// Linear scan because the table is small and this only runs when the game
// updates a marker, not per frame.
inline void RememberBeastMarker(void* marker) {
    if (!marker || !PlausiblePointer(reinterpret_cast<uintptr_t>(marker))) return;

    void** table = BeastMarkerTable();
    int& count = BeastMarkerCount();
    for (int i = 0; i < count; ++i) {
        if (table[i] == marker) return;
    }
    if (count >= kBeastMarkerTrackLimit) return;
    table[count++] = marker;
}

WIIXL_HOOK_DEFINE_TRAMPOLINE(BeastMarkerStateHook) {
    static void Callback(void* marker) {
        Orig(marker);
        RememberBeastMarker(marker);
    }
};

}  // namespace impl

// Arms the marker tracking. Call once from WiiXLaunch_Init().
//
// Installing costs nothing at runtime: the hook runs the original and adds one
// pointer comparison per marker update.
inline bool InitBeastMarkers() {
#if !WIIXL_SWITCH
    if (impl::BeastMarkerHookInstalled()) return true;
    impl::BeastMarkerStateHook::Install(0x0, impl::kBeastMarkerUpdateStateWiiU);
    impl::BeastMarkerHookInstalled() = true;
    return true;
#else
    return false;
#endif
}

// How many distinct markers have been seen. Zero means the game has not updated
// any since boot, which usually means the map has not been opened yet.
inline int TrackedBeastMarkers() {
#if !WIIXL_SWITCH
    return impl::BeastMarkerCount();
#else
    return 0;
#endif
}

// Re-runs the state update on every tracked marker, so a flag written since the
// last update is reflected in the bits the map draws from.
//
// Returns how many were refreshed. Zero with markers tracked means the hook is
// not installed; zero with none tracked means nothing has been seen yet.
//
// Pointers are re-validated before use but not owned, so a marker freed since
// it was seen would be stale. In practice the pool lives as long as the map
// manager does.
inline int RefreshBeastMarkers() {
#if !WIIXL_SWITCH
    if (!impl::BeastMarkerHookInstalled()) return 0;

    auto update = WiiXLaunch::GetTargetFunction<void (*)(void*)>(
        0x0, impl::kBeastMarkerUpdateStateWiiU);
    if (!update) return 0;

    void** table = impl::BeastMarkerTable();
    const int count = impl::BeastMarkerCount();

    int done = 0;
    for (int i = 0; i < count; ++i) {
        void* marker = table[i];
        if (!marker || !impl::PlausiblePointer(reinterpret_cast<uintptr_t>(marker))) continue;
        update(marker);
        ++done;
    }
    return done;
#else
    return 0;
#endif
}

// What one tracked marker is, for working out what the hook is actually
// catching. index runs 0 .. TrackedBeastMarkers()-1.
//
// The name is the marker's own sead::SafeString at +0x58, which is the same
// string the Enter_/Clear_ flag names were built from - so a shrine reads
// "Dungeon009" and its flags are Enter_Dungeon009 and Clear_Dungeon009.
struct TrackedBeastMarker {
    const char* name;   // "" when the string does not validate
    uint32_t flags;     // +0x2c, where 0x20000 is Enter_ and 0x80000 is Clear_
    bool entered;
    bool cleared;
};

inline bool GetTrackedBeastMarker(int index, TrackedBeastMarker& out) {
    out.name = "";
    out.flags = 0;
    out.entered = false;
    out.cleared = false;

#if !WIIXL_SWITCH
    if (index < 0 || index >= impl::BeastMarkerCount()) return false;

    void* marker = impl::BeastMarkerTable()[index];
    if (!marker || !impl::PlausiblePointer(reinterpret_cast<uintptr_t>(marker))) return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(marker);
    const char* text = *reinterpret_cast<const char* const*>(base + impl::kBeastMarkerName);
    if (text && impl::PlausiblePointer(reinterpret_cast<uintptr_t>(text))) out.name = text;

    out.flags = *reinterpret_cast<uint32_t*>(base + impl::kBeastMarkerFlags);
    out.entered = (out.flags & impl::kBeastMarkerEnteredBit) != 0;
    out.cleared = (out.flags & impl::kBeastMarkerClearedBit) != 0;
    return true;
#else
    (void)index;
    return false;
#endif
}

// Forgets every tracked marker, for a caller that has reason to think the pool
// has been rebuilt underneath it.
inline void ForgetBeastMarkers() {
#if !WIIXL_SWITCH
    impl::BeastMarkerCount() = 0;
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
//
// It does NOT change the map. Measured: the flag goes false and the shrine
// stays a travel destination - see the correction at the top of this header.
// The name is kept for now because the flag it writes is the right flag for
// "visited"; it is the travel half of the name that is unproven.
inline bool SetMapUnlock(int shrine, bool unlocked, bool force = false,
                         bool bypassPermission = false) {
#if !WIIXL_SWITCH
    if (shrine < 0) return false;
    // Enter_DungeonNNN latches too, so locking one needs force just as a region
    // does. Unlocking never has.
    return impl::WriteBoolFlag(impl::ShrineFlag(shrine), unlocked, force,
                               bypassPermission);
#else
    (void)shrine; (void)unlocked; (void)force; (void)bypassPermission;
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
inline int SetMapUnlockAll(bool unlocked, bool force = false,
                           bool bypassPermission = false) {
#if !WIIXL_SWITCH
    int done = 0;
    for (int i = 0; i < kShrineProbeLimit; ++i) {
        bool value = false;
        if (!GameData::GetFlagBool(impl::ShrineFlag(i), value)) continue;
        if (SetMapUnlock(i, unlocked, force, bypassPermission)) ++done;
    }
    return done;
#else
    (void)unlocked; (void)force; (void)bypassPermission;
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
