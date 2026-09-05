#pragma once

// botw.map v1 - map regions, shrines, and the beast markers.
//
// The map is stored as save flags, so almost everything here could in principle
// be done through botw.gamedata's flag calls - and doing it that way would mean
// every mod hard-coding flag NAMES like "MapTower_07" and the padding rules for
// building them. Those names are a game detail, and a game detail in a compiled
// binary is exactly what these surfaces exist to prevent. So the naming stays
// in map.hpp and a mod says "region 7".
//
// Region numbers are 1..kRegionCount inclusive, which is the game's own
// numbering rather than a zero-based one invented here. RegionFirst/RegionCount
// are exported so a mod loops over what the host says exists instead of a
// constant it copied.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/botw/game/map.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::MapSurface {

constexpr const char* kName = "botw.map";
constexpr uint16_t kVersionMajor = 1;
// 1.1 appends the five ...Level symbols. The module's writers have always
// taken TWO permission flags - force, which clears a latch, and
// bypassPermission, which writes flags the game marks event-system-only - and
// this surface passed only the first, so the second was unreachable through it.
// Appending rather than widening the existing symbols: a mod built against v1.0
// still resolves, and still gets exactly what it asked for.
constexpr uint16_t kVersionMinor = 1;

namespace impl {

extern "C" inline uint32_t MSupportsMap() { return Map::SupportsMap ? 1u : 0u; }
extern "C" inline uint32_t MIsAvailable() { return Map::IsAvailable() ? 1u : 0u; }

extern "C" inline int32_t MRegionFirst() { return static_cast<int32_t>(Map::kFirstRegion); }
extern "C" inline int32_t MRegionCount() { return static_cast<int32_t>(Map::kRegionCount); }

// The flag store is only re-read when asked. A mod that has just written flags
// through botw.gamedata and then reads a region here would otherwise see the
// cached answer, which is the sort of staleness that looks like a write having
// silently failed.
extern "C" inline uint32_t MRefreshRegions() { return Map::RefreshRegions() ? 1u : 0u; }

// --- regions ---------------------------------------------------------------

extern "C" inline uint32_t MGetRegionUnlock(int32_t region, uint32_t* out) {
    if (!out) return 0;
    bool v = false;
    if (!Map::GetMapRegionUnlock(static_cast<int>(region), v)) return 0;
    *out = v ? 1u : 0u;
    return 1;
}

// `force` writes the flag even where the game would have refused it. Its own
// parameter rather than a second symbol, because unlike the flag store's
// permission bypass this one is routine - unlocking a region the player has not
// reached is the ordinary reason to call this at all.
extern "C" inline uint32_t MSetRegionUnlock(int32_t region, uint32_t unlocked, uint32_t force) {
    return Map::SetMapRegionUnlock(static_cast<int>(region), unlocked != 0, force != 0)
               ? 1u : 0u;
}

extern "C" inline int32_t MSetRegionUnlockAll(uint32_t unlocked, uint32_t force) {
    return static_cast<int32_t>(Map::SetMapRegionUnlockAll(unlocked != 0, force != 0));
}

extern "C" inline uint32_t MGetRegionScaleLevel(int32_t region, int32_t* out) {
    if (!out) return 0;
    int v = 0;
    if (!Map::GetMapRegionScaleLevel(static_cast<int>(region), v)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

// The cached read, kept distinct from the live one. Reading every region live
// walks the flag store once per region; a mod drawing a whole map wants the
// cache, and a mod that just wrote wants the live value. Collapsing them would
// force one of those two to be wrong.
extern "C" inline uint32_t MGetCachedRegionScaleLevel(int32_t region, int32_t* out) {
    if (!out) return 0;
    int v = 0;
    if (!Map::GetCachedRegionScaleLevel(static_cast<int>(region), v)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

extern "C" inline uint32_t MSetRegionScaleLevel(int32_t region, int32_t level) {
    return Map::SetMapRegionScaleLevel(static_cast<int>(region), static_cast<int>(level))
               ? 1u : 0u;
}

extern "C" inline uint32_t MGetRegionMarker(int32_t region, int32_t* out) {
    if (!out) return 0;
    int v = 0;
    if (!Map::GetMapRegionMarker(static_cast<int>(region), v)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

extern "C" inline uint32_t MSetRegionMarker(int32_t region, int32_t id) {
    return Map::SetMapRegionMarker(static_cast<int>(region), static_cast<int>(id)) ? 1u : 0u;
}

extern "C" inline uint32_t MSetRegionActivated(int32_t region, uint32_t activated, uint32_t force) {
    return Map::SetMapRegionActivated(static_cast<int>(region), activated != 0, force != 0)
               ? 1u : 0u;
}

// --- the two-level permission forms ----------------------------------------
//
// level 0 writes normally, 1 clears a latch, 2 also writes flags the game marks
// event-system-only. The single-flag symbols above are level 0 and level 1
// only; these reach the third case.
extern "C" inline uint32_t MSetRegionUnlockLevel(int32_t region, uint32_t unlocked,
                                                 int32_t level) {
    return Map::SetMapRegionUnlock(static_cast<int>(region), unlocked != 0,
                                   level >= 1, level >= 2) ? 1u : 0u;
}

extern "C" inline int32_t MSetRegionUnlockAllLevel(uint32_t unlocked, int32_t level) {
    return static_cast<int32_t>(
        Map::SetMapRegionUnlockAll(unlocked != 0, level >= 1, level >= 2));
}

extern "C" inline uint32_t MSetRegionActivatedLevel(int32_t region, uint32_t activated,
                                                    int32_t level) {
    return Map::SetMapRegionActivated(static_cast<int>(region), activated != 0,
                                      level >= 1, level >= 2) ? 1u : 0u;
}

extern "C" inline uint32_t MSetShrineUnlockLevel(int32_t shrine, uint32_t unlocked,
                                                 int32_t level) {
    return Map::SetMapUnlock(static_cast<int>(shrine), unlocked != 0,
                             level >= 1, level >= 2) ? 1u : 0u;
}

extern "C" inline int32_t MSetShrineUnlockAllLevel(uint32_t unlocked, int32_t level) {
    return static_cast<int32_t>(
        Map::SetMapUnlockAll(unlocked != 0, level >= 1, level >= 2));
}

extern "C" inline int32_t MCountUnlockedRegions() {
    return static_cast<int32_t>(Map::CountUnlockedRegions());
}

// --- shrines ---------------------------------------------------------------

extern "C" inline int32_t MShrineCount() {
    return static_cast<int32_t>(Map::GetShrineCount());
}

extern "C" inline uint32_t MSetShrineUnlock(int32_t shrine, uint32_t unlocked, uint32_t force) {
    return Map::SetMapUnlock(static_cast<int>(shrine), unlocked != 0, force != 0) ? 1u : 0u;
}

extern "C" inline int32_t MSetShrineUnlockAll(uint32_t unlocked, uint32_t force) {
    return static_cast<int32_t>(Map::SetMapUnlockAll(unlocked != 0, force != 0));
}

extern "C" inline int32_t MCountUnlockedShrines() {
    return static_cast<int32_t>(Map::CountUnlockedShrines());
}

extern "C" inline uint32_t MGetShrineUnlock(int32_t shrine, uint32_t* out) {
    if (!out) return 0;
    bool v = false;
    if (!Map::GetMapUnlock(static_cast<int>(shrine), v)) return 0;
    *out = v ? 1u : 0u;
    return 1;
}

// --- beast markers ---------------------------------------------------------
//
// Enumerated by index rather than handed over as a table: the struct behind
// each one must not cross, and a count-plus-accessor pair is the same shape
// every other enumeration in these surfaces uses.

extern "C" inline uint32_t MInitBeastMarkers() {
    Map::InitBeastMarkers();
    return 1;
}

extern "C" inline int32_t MRefreshBeastMarkers() {
    return static_cast<int32_t>(Map::RefreshBeastMarkers());
}

// One marker's contents. TrackedBeastMarker is a struct and must not cross, so
// the name is copied into the caller's buffer and the two flags come back as
// out-parameters - the same shape every other read here uses.
extern "C" inline uint32_t MGetBeastMarker(int32_t index, char* nameOut, uint32_t cap,
                                           uint32_t* entered, uint32_t* cleared,
                                           uint32_t* flags) {
    Map::TrackedBeastMarker m{};
    if (!Map::GetTrackedBeastMarker(static_cast<int>(index), m)) {
        if (nameOut && cap) nameOut[0] = 0;
        return 0;
    }
    if (nameOut && cap) {
        uint32_t n = 0;
        while (m.name && m.name[n] && n + 1 < cap) { nameOut[n] = m.name[n]; ++n; }
        nameOut[n] = 0;
    }
    if (entered) *entered = m.entered ? 1u : 0u;
    if (cleared) *cleared = m.cleared ? 1u : 0u;
    if (flags) *flags = m.flags;
    return 1;
}

// Drops what the tracker has learned, so the next refresh starts clean. For a
// mod that has moved or removed beasts and does not want the stale positions
// blended in.
extern "C" inline uint32_t MForgetBeastMarkers() {
    Map::ForgetBeastMarkers();
    return 1;
}

extern "C" inline int32_t MBeastMarkerCount() {
    return static_cast<int32_t>(Map::TrackedBeastMarkers());
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsMap",             &MSupportsMap),
    WIIXL_SURFACE_SYMBOL("IsAvailable",             &MIsAvailable),
    WIIXL_SURFACE_SYMBOL("RegionFirst",             &MRegionFirst),
    WIIXL_SURFACE_SYMBOL("RegionCount",             &MRegionCount),
    WIIXL_SURFACE_SYMBOL("RefreshRegions",          &MRefreshRegions),

    WIIXL_SURFACE_SYMBOL("GetRegionUnlock",         &MGetRegionUnlock),
    WIIXL_SURFACE_SYMBOL("SetRegionUnlock",         &MSetRegionUnlock),
    WIIXL_SURFACE_SYMBOL("SetRegionUnlockAll",      &MSetRegionUnlockAll),
    WIIXL_SURFACE_SYMBOL("GetRegionScaleLevel",     &MGetRegionScaleLevel),
    WIIXL_SURFACE_SYMBOL("GetCachedRegionScaleLevel", &MGetCachedRegionScaleLevel),
    WIIXL_SURFACE_SYMBOL("SetRegionScaleLevel",     &MSetRegionScaleLevel),
    WIIXL_SURFACE_SYMBOL("GetRegionMarker",         &MGetRegionMarker),
    WIIXL_SURFACE_SYMBOL("SetRegionMarker",         &MSetRegionMarker),
    WIIXL_SURFACE_SYMBOL("SetRegionActivated",      &MSetRegionActivated),
    WIIXL_SURFACE_SYMBOL("CountUnlockedRegions",    &MCountUnlockedRegions),

    WIIXL_SURFACE_SYMBOL("ShrineCount",             &MShrineCount),
    WIIXL_SURFACE_SYMBOL("SetShrineUnlock",         &MSetShrineUnlock),
    WIIXL_SURFACE_SYMBOL("SetShrineUnlockAll",      &MSetShrineUnlockAll),
    WIIXL_SURFACE_SYMBOL("CountUnlockedShrines",    &MCountUnlockedShrines),

    WIIXL_SURFACE_SYMBOL("GetShrineUnlock",         &MGetShrineUnlock),
    WIIXL_SURFACE_SYMBOL("InitBeastMarkers",        &MInitBeastMarkers),
    WIIXL_SURFACE_SYMBOL("RefreshBeastMarkers",     &MRefreshBeastMarkers),
    WIIXL_SURFACE_SYMBOL("GetBeastMarker",          &MGetBeastMarker),
    WIIXL_SURFACE_SYMBOL("ForgetBeastMarkers",      &MForgetBeastMarkers),
    WIIXL_SURFACE_SYMBOL("BeastMarkerCount",        &MBeastMarkerCount),

    // v1.1. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("SetRegionUnlockLevel",    &MSetRegionUnlockLevel),
    WIIXL_SURFACE_SYMBOL("SetRegionUnlockAllLevel", &MSetRegionUnlockAllLevel),
    WIIXL_SURFACE_SYMBOL("SetRegionActivatedLevel", &MSetRegionActivatedLevel),
    WIIXL_SURFACE_SYMBOL("SetShrineUnlockLevel",    &MSetShrineUnlockLevel),
    WIIXL_SURFACE_SYMBOL("SetShrineUnlockAllLevel", &MSetShrineUnlockAllLevel),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::MapSurface
