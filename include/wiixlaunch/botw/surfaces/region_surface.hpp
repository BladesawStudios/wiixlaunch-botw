#pragma once

// botw.region v1 - where the player is allowed to be.
//
// A whole header that was missed the first time round, and only found because
// the coverage scan started counting it. It had been bundled under "map" in the
// planning and never actually exposed - which is exactly the sort of thing
// nobody notices until something counts.
//
// What it does: region.hpp knows which of the fifteen map regions a point falls
// in, which of them the player has unlocked, and can build INVISIBLE WALLS at
// the boundaries so an unexplored region cannot be walked into. That last part
// is a mod mechanic rather than a game feature - the walls are actors the module
// spawns and moves as the player approaches a boundary.
//
// The wall settings are global rather than per-mod, and that is worth saying
// out loud: two mods both driving the walls will fight, and the host cannot
// arbitrate because there is one set of walls in the world. Unlike hooks and
// ticks, this is a shared resource with no sensible way to split it, so the
// rule is the ordinary one for shared state - last writer wins, and the log
// records who has touched it.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/botw/game/region.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::RegionSurface {

constexpr const char* kName = "botw.region";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// Who has changed the wall settings. Named once each, for the reason the input
// injector is: shared state with no owner produces behaviour nobody can trace
// back to a decision.
constexpr uint32_t kMaxNoted = 8;
inline char g_Noted[kMaxNoted][17];
inline uint32_t g_NotedCount = 0;

inline void NoteWallOwnerOnce() {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') owner = "<host>";

    for (uint32_t i = 0; i < g_NotedCount; ++i) {
        bool same = true;
        for (uint32_t c = 0; c < 17; ++c) {
            if (g_Noted[i][c] != owner[c]) { same = false; break; }
            if (owner[c] == '\0') break;
        }
        if (same) return;
    }
    if (g_NotedCount < kMaxNoted) {
        uint32_t i = 0;
        for (; i + 1 < 17 && owner[i]; ++i) g_Noted[g_NotedCount][i] = owner[i];
        g_Noted[g_NotedCount][i] = '\0';
        ++g_NotedCount;
    }

    if (g_NotedCount > 1) {
        WIIXL_LOG("botw.region: %s is the %u module to change the region walls - "
                  "there is ONE set of walls in the world and they will fight",
                  owner, g_NotedCount);
    } else {
        WIIXL_LOG("botw.region: %s is driving the region walls", owner);
    }
}

extern "C" inline uint32_t RgSupportsRegion() { return Region::SupportsRegion ? 1u : 0u; }

// --- where things are ------------------------------------------------------

extern "C" inline int32_t RgGetPlayerRegion() {
    return static_cast<int32_t>(Region::GetPlayerRegion());
}

extern "C" inline int32_t RgGetRegionAt(float x, float z) {
    return static_cast<int32_t>(Region::GetRegionAt(x, z));
}

// --- which regions are open ------------------------------------------------
//
// A bitmask of the fifteen regions, so a mod reads or writes the whole set in
// one call. Region 1 is bit 0, matching the game's own numbering rather than a
// zero-based one invented here.

extern "C" inline uint32_t RgGetUnlockMask() { return Region::GetUnlockMask(); }

extern "C" inline uint32_t RgSetUnlockMask(uint32_t mask) {
    Region::SetUnlockMask(mask);
    return 1;
}

// Rebuilds the mask from the tower flags, which is what the player's actual
// progress says. A mod that has been overriding the mask calls this to give
// control back.
extern "C" inline int32_t RgSyncFromTowers() {
    return static_cast<int32_t>(Region::SyncFromTowers());
}

extern "C" inline int32_t RgCountLockedRegions() {
    return static_cast<int32_t>(Region::CountLockedRegions());
}

extern "C" inline uint32_t RgAllowedAt(float x, float z) {
    return Region::AllowedAt(x, z) ? 1u : 0u;
}

extern "C" inline uint32_t RgIsBlockedAt(float x, float z) {
    return Region::IsBlockedAt(x, z) ? 1u : 0u;
}

// --- the walls -------------------------------------------------------------

extern "C" inline uint32_t RgSetWallsEnabled(uint32_t enabled) {
    NoteWallOwnerOnce();
    Region::SetWallsEnabled(enabled != 0);
    return 1;
}

extern "C" inline uint32_t RgGetWallsEnabled() {
    return Region::GetWallsEnabled() ? 1u : 0u;
}

extern "C" inline uint32_t RgSetWallGeometry(float cellSize, float height, float thickness) {
    NoteWallOwnerOnce();
    Region::SetWallGeometry(cellSize, height, thickness);
    return 1;
}

extern "C" inline uint32_t RgGetWallGeometry(float* out3) {
    if (!out3) return 0;
    out3[0] = Region::GetCellSize();
    out3[1] = Region::GetWallHeight();
    out3[2] = Region::GetWallThickness();
    return 1;
}

extern "C" inline uint32_t RgSetBuildRadius(float radius) {
    NoteWallOwnerOnce();
    Region::SetBuildRadius(radius);
    return 1;
}

extern "C" inline uint32_t RgGetBuildRadius(float* out) {
    if (!out) return 0;
    *out = Region::GetBuildRadius();
    return 1;
}

// Pushback shoves the player back out rather than only blocking them, which is
// a different feel and a different failure mode - its own switch because a mod
// wanting one rarely wants the other by accident.
extern "C" inline uint32_t RgSetPushbackEnabled(uint32_t enabled) {
    NoteWallOwnerOnce();
    Region::SetPushbackEnabled(enabled != 0);
    return 1;
}

extern "C" inline uint32_t RgGetPushbackEnabled() {
    return Region::GetPushbackEnabled() ? 1u : 0u;
}

extern "C" inline uint32_t RgSetWallYaw(int32_t index) {
    Region::SetWallYaw(static_cast<int>(index));
    return 1;
}

extern "C" inline int32_t RgGetWallYaw() { return static_cast<int32_t>(Region::GetWallYaw()); }

extern "C" inline uint32_t RgSetWallPitch(int32_t index) {
    Region::SetWallPitch(static_cast<int>(index));
    return 1;
}

extern "C" inline int32_t RgGetWallPitch() { return static_cast<int32_t>(Region::GetWallPitch()); }

extern "C" inline int32_t RgGetWallCount() { return static_cast<int32_t>(Region::GetWallCount()); }

extern "C" inline int32_t RgRemoveAllWalls() {
    return static_cast<int32_t>(Region::RemoveAllWalls());
}

// --- the boundary marker ---------------------------------------------------

extern "C" inline uint32_t RgShowMarkerAt(float x, float y, float z,
                                          float towardX, float towardZ) {
    Region::ShowMarkerAt(x, y, z, towardX, towardZ);
    return 1;
}

extern "C" inline uint32_t RgHideMarker() { Region::HideMarker(); return 1; }

extern "C" inline uint32_t RgSetMarkerEnabled(uint32_t on) {
    Region::SetMarkerEnabled(on != 0);
    return 1;
}

extern "C" inline uint32_t RgGetMarkerEnabled() {
    return Region::GetMarkerEnabled() ? 1u : 0u;
}

extern "C" inline uint32_t RgSetMarkerScale(float scale) {
    Region::SetMarkerScale(scale);
    return 1;
}

extern "C" inline uint32_t RgGetMarkerScale(float* out) {
    if (!out) return 0;
    *out = Region::GetMarkerScale();
    return 1;
}

extern "C" inline uint32_t RgSetMarkerOffset(float units) {
    Region::SetMarkerOffset(units);
    return 1;
}

extern "C" inline uint32_t RgGetMarkerOffset(float* out) {
    if (!out) return 0;
    *out = Region::GetMarkerOffset();
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsRegion",     &RgSupportsRegion),
    WIIXL_SURFACE_SYMBOL("GetPlayerRegion",    &RgGetPlayerRegion),
    WIIXL_SURFACE_SYMBOL("GetRegionAt",        &RgGetRegionAt),

    WIIXL_SURFACE_SYMBOL("GetUnlockMask",      &RgGetUnlockMask),
    WIIXL_SURFACE_SYMBOL("SetUnlockMask",      &RgSetUnlockMask),
    WIIXL_SURFACE_SYMBOL("SyncFromTowers",     &RgSyncFromTowers),
    WIIXL_SURFACE_SYMBOL("CountLockedRegions", &RgCountLockedRegions),
    WIIXL_SURFACE_SYMBOL("AllowedAt",          &RgAllowedAt),
    WIIXL_SURFACE_SYMBOL("IsBlockedAt",        &RgIsBlockedAt),

    WIIXL_SURFACE_SYMBOL("SetWallsEnabled",    &RgSetWallsEnabled),
    WIIXL_SURFACE_SYMBOL("GetWallsEnabled",    &RgGetWallsEnabled),
    WIIXL_SURFACE_SYMBOL("SetWallGeometry",    &RgSetWallGeometry),
    WIIXL_SURFACE_SYMBOL("GetWallGeometry",    &RgGetWallGeometry),
    WIIXL_SURFACE_SYMBOL("SetBuildRadius",     &RgSetBuildRadius),
    WIIXL_SURFACE_SYMBOL("GetBuildRadius",     &RgGetBuildRadius),
    WIIXL_SURFACE_SYMBOL("SetPushbackEnabled", &RgSetPushbackEnabled),
    WIIXL_SURFACE_SYMBOL("GetPushbackEnabled", &RgGetPushbackEnabled),
    WIIXL_SURFACE_SYMBOL("SetWallYaw",         &RgSetWallYaw),
    WIIXL_SURFACE_SYMBOL("GetWallYaw",         &RgGetWallYaw),
    WIIXL_SURFACE_SYMBOL("SetWallPitch",       &RgSetWallPitch),
    WIIXL_SURFACE_SYMBOL("GetWallPitch",       &RgGetWallPitch),
    WIIXL_SURFACE_SYMBOL("GetWallCount",       &RgGetWallCount),
    WIIXL_SURFACE_SYMBOL("RemoveAllWalls",     &RgRemoveAllWalls),

    WIIXL_SURFACE_SYMBOL("ShowMarkerAt",       &RgShowMarkerAt),
    WIIXL_SURFACE_SYMBOL("HideMarker",         &RgHideMarker),
    WIIXL_SURFACE_SYMBOL("SetMarkerEnabled",   &RgSetMarkerEnabled),
    WIIXL_SURFACE_SYMBOL("GetMarkerEnabled",   &RgGetMarkerEnabled),
    WIIXL_SURFACE_SYMBOL("SetMarkerScale",     &RgSetMarkerScale),
    WIIXL_SURFACE_SYMBOL("GetMarkerScale",     &RgGetMarkerScale),
    WIIXL_SURFACE_SYMBOL("SetMarkerOffset",    &RgSetMarkerOffset),
    WIIXL_SURFACE_SYMBOL("GetMarkerOffset",    &RgGetMarkerOffset),
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

} // namespace WiiXLaunch::BotW::Surfaces::RegionSurface
