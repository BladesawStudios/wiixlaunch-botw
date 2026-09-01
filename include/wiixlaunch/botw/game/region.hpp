#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/hook.hpp>
#include "gamedata.hpp"
#include "actor.hpp"
#include "player.hpp"

// WiiXLaunch::BotW::Region - the fifteen tower regions as PLACES rather than as
// map reveal, and physical walls on the borders between them.
//
// botw/map.hpp already owns the map side: which regions are drawn, how far the
// reveal reaches, where the markers are. This is the other half - where a
// region actually IS in the world, whether the player is allowed in it, and
// what stops them if they are not.
//
// --- WHERE A REGION IS -----------------------------------------------------
//
// The game ships the answer as a file. Ecosystem/MapTower.beco is one of the
// four "ecomaps": a run-length-encoded raster over the whole of MainField, one
// s16 value per cell, and for this file that value is the tower region. So
// "which region is this point in" is a lookup the game already does, not
// something to approximate from the fifteen MapTower_NN_OpenCenterPos flags.
//
// The raster is x in [-5000, 4999] at one cell per world unit and z in
// [-4000, 4000] at `divisor` (2) units per row, 4000 rows. Every cell carries a
// value; there is no "no region" hole anywhere on the field.
//
// Ecosystem::init (0x032aeb68) loads it into the Ecosystem singleton and
// 0x032aeb44 unpacks it into three pointers at +0x120 - the header, the row
// offset table, and the row data - which is the EcoMapInfo this header reads.
// The lookup itself (impl::MapArea) is a reimplementation of the game's
// Ecosystem::getMapArea rather than a call into it: getMapArea is thirty lines
// of clamp-and-scan with no side effects, and reimplementing it avoids having
// to pin down an address for a function that only exists as an inlined copy in
// several callers.
//
// MEASURED, and worth stating because it is the one thing the whole header
// rests on: the ecomap value plus one is the MapTower_NN number. All fifteen
// tower actors in TitleBG.pack's static placements stand inside the region
// numbered for their own tower, fifteen for fifteen, no collisions:
//
//   MapTower_01  (-2173,  455, -2034)   MapTower_09  (  -32,  206,  2962)
//   MapTower_02  (-3614,  371,  -990)   MapTower_10  ( 2174,  435, -1557)
//   MapTower_03  (-3666,  397,  1829)   MapTower_11  ( 3308,  520, -1500)
//   MapTower_04  (-2307,  456,  2437)   MapTower_12  ( 2258,  237,  -109)
//   MapTower_05  (  884,  276, -1606)   MapTower_13  ( 2736,  262,  2134)
//   MapTower_06  ( -789,  124,   442)   MapTower_14  ( 1331,  196,  3274)
//   MapTower_07  ( -560,  172,  1695)   MapTower_15  (-1755,  254,  -774)
//   MapTower_08  ( 1017,  110,  1714)
//
// The MapTower_NN_OpenCenterPos flags do NOT line up as cleanly - three of the
// fifteen sit inside a different region than their own number. That is not a
// contradiction: those are the centres of the revealed circle on the MAP, they
// are configuration for a UI, and nothing says one has to sit inside its own
// ground footprint. Do not use them as region positions; that is what this
// ecomap is for.
//
// --- WHAT STOPS THE PLAYER -------------------------------------------------
//
// BotW has purpose-built invisible walls, five of them, and only some are
// usable from a mod:
//
//   AirWall              "world's end". PhysicsUser is Dummy - its shape comes
//                        from its map placement's Shape/Scale through the Area
//                        system, so a runtime spawn of it is an actor with no
//                        collision at all.
//   AirWallForE3         "player entry forbidden", the E3 demo's boundary.
//                        Also PhysicsUser Dummy, AreaBase AI. Same story.
//   AirWallCurseGanon    the Blight Ganon arena walls. Carries its OWN
//                        Actor/Physics/AirWallCurseGanon.bphysics: one box
//                        rigid body, motion_type Fixed, contact layer
//                        EntityAirWall, material AirWall, extents 2 x 2 x 2.
//   AirWallHorse         same shape, but layer EntityGround and material
//                        Undefined - built to stop horses.
//   CastleBarrier        the castle malice barrier. PhysicsUser Dummy again.
//
// That reasoning led to AirWallCurseGanon, and it was WRONG - not about the
// physics resource, but about what the resource does. Tested live, with the
// player sealed inside a box of five confirmed-live panels: he walked out.
// So did the Dark Beast arena line (FldObj_GanonBeast_BattleAreaLine_A_01),
// which is on EntityGround and names nobody in its ignore mask.
//
// The layer is the point. EntityAirWall is not a layer the player's character
// controller tests, and AirWallHorse - EntityGround, but listing Player in its
// ignore mask - is the barrier that keeps horses out of Death Mountain and
// Gerudo. The whole AirWall family is built to let Link through selectively.
// No amount of scaling, height or spawn-queue fixing changes that.
//
// What works is ordinary solid geometry. A treasure chest stops the player; so
// does a shrine stone block, which is what kWallActor now names. Both were
// verified the same way - ringed around the player, who could not get out.
// Being visible is a feature, not a side effect: an invisible barrier teaches
// the player nothing about where the boundary is.
//
// Size comes from the creation params: Actor::SpawnScaled writes "@S", the same
// per-axis scale a map placement carries. The four real ones are placed at
// {4,6,4}, {8,5,6} and so on, so non-uniform is what the game itself ships.
// Read the caveat on SpawnScaled before leaning on that.
//
// --- WHAT THIS COSTS -------------------------------------------------------
//
// A region border is not short. Measured off the ecomap, the interior borders
// total about 112 km of cell edges and a single region's perimeter runs 5-20
// km. Walling one outright is thousands of actors and is not a thing to
// attempt. So walls are built LOCALLY: only the border within kBuildRadius of
// the player, rebuilt when the player moves a cell, and taken down again when
// they leave. That turns a 15 km perimeter into a handful of panels.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Region {

static constexpr bool SupportsRegion = !WIIXL_SWITCH;

// The fifteen towers, numbered the way the flags are - same convention as
// botw/map.hpp, and the same numbers.
constexpr int kFirstRegion = 1;
constexpr int kRegionCount = 15;
constexpr int kLastRegion = kFirstRegion + kRegionCount - 1;

// Returned by GetRegionAt when the lookup could not be made at all: the
// Ecosystem singleton is not up, or the resource has not finished loading.
// DISTINCT from a valid region - do not treat it as region 0.
constexpr int kNoRegion = 0;

// The actor spawned as a wall panel.
//
// This must be ordinary SOLID GEOMETRY, not one of the AirWall actors.
// Those are gameplay-filtered volumes: AirWallCurseGanon sits on the
// EntityAirWall layer, which the player's character controller never
// tests, and AirWallHorse is on EntityGround but names Player in its
// ignore mask - it is the barrier that keeps HORSES out of Death
// Mountain and Gerudo, and Link walks through it by design. Both were
// tried live, sealed in a box around the player, and both let him out.
//
// A shrine stone block collides, holds its spawn position, honours the
// "@S" scale, and - unlike every AirWall - can actually be SEEN, which
// a barrier the player is expected to respect rather needs.
constexpr const char* kWallActor = "CastleBarrier";

// Roughly how big the tile is, in world units - measured by eye in game, not
// read from the pack. Used as the vertical course height so a stacked band
// meets edge to edge rather than overlapping or leaving gaps.
constexpr float kWallActorTileSize = 8.0f;

// Default distance the marker panel sits past the corrected position, in
// world units. Runtime-tunable via regionConfig markerOffset.
//
// The correction puts the player just inside the border, so a panel drawn at
// that exact point is centred on Link. It only needs nudging off him - three
// units put it visibly out in front, which read as wrong.
constexpr float kMarkerOffsetDefault = 0.5f;

// CastleBarrier - the Hyrule Castle malice barrier - is the wall.
//
// It brings three things no other candidate did, in ONE actor per tile:
//   * collision, so it actually stops the player;
//   * the game's own "You can't go any further" message;
//   * it only becomes visible when the player is nearly touching it, so a
//     border reads as a boundary rather than as a stone monolith.
//
// It is a small square, roughly eight to ten units, so a border is a LINE of
// tiles - which is why cell size wants to be around eight rather than twenty.
//
// This replaced a stone-block wall that needed ten stacked courses and ~170
// actors for one border patch, because collision scaled uniformly from X only
// and the creation scale capped at 12. None of that applies here: the tile is
// spawned at its authored scale and never resized.
//
// Rotation is applied AFTER spawning - Actor::Spawn sets position and scale
// and nothing else. Measured: setMtx does turn the actor and the basis holds
// (identity -> 90 degree yaw, still there 2.5s later).
// Eight steps of 45 degrees, not four of 90.
//
// Quarter turns can only align a tile to an axis, and a region border rarely
// runs along one - seen in game as scattered slabs at odd angles rather than a
// line. Diagonals are what let consecutive tiles meet edge to edge.
constexpr int kWallYawCount = 8;
inline const float* WallYawCos() { static const float v[8] = { 1.000000f, 0.707107f, 0.000000f, -0.707107f, -1.000000f, -0.707107f, 0.000000f, 0.707107f }; return v; }
inline const float* WallYawSin() { static const float v[8] = { 0.000000f, 0.707107f, 1.000000f, 0.707107f, 0.000000f, -0.707107f, -1.000000f, -0.707107f }; return v; }

// Hard ceiling on live panels.
//
// Lowered from 256 after a live crash: cellSize 3 with eight courses asked for
// 248 panels, 88 had spawned when the game died. The spawn queue drains one
// per tick, so a large request is also a long stall - the table cap is the
// only thing standing between a bad config and a crash, so it is set where a
// bad config merely looks sparse.
//
// Walls are an opt-in extra anyway; PUSHBACK is what enforces region locking.
constexpr int kMaxWalls = 64;

// Smallest pushback correction worth applying, in world units.
//
// Pushback is a position write, and a position write costs state: it resets
// swimming, and at one per frame it pinned the player underwater. Below this
// distance the correction is invisible, so it is skipped and the frame is left
// alone. Large enough to silence the standing-still case, small enough that
// nobody walks through a border a fraction of a unit at a time.
constexpr float kMinCorrection = 0.05f;

namespace impl {

// --- the region raster -----------------------------------------------------

// ksys::eco::Ecosystem, the singleton. From 0x034134d0, which loads this word
// into r3 and calls Ecosystem::init (0x032aeb68) with the heap in r4.
constexpr uintptr_t kEcosystemPtrWiiU = 0x1046d6ac;

// The MapTower EcoMapInfo - three pointers, unpacked by 0x032aeb44 from the
// loaded resource. Its siblings are FieldMapArea at +0x114 and LoadBalancer at
// +0x12c, which is what fixes the stride at 0xc and this one at +0x120.
constexpr uintptr_t kMapTowerEcoMapOffset = 0x120;

// Ecosystem/MapTower.beco's own header magic, checked before anything is read
// through the pointers - a mis-derived offset then reads as "not ready" rather
// than as a wild scan.
constexpr uint32_t kEcoMapMagic = 0x00112233;

struct EcoMapHeader {
    uint32_t magic;
    int32_t numRows;
    int32_t divisor;
    uint32_t reserved;
};

struct EcoMapInfo {
    const EcoMapHeader* header;
    const int32_t* rowOffsets;
    const char* rows;
};

struct EcoMapSegment {
    int16_t value;
    int16_t length;
};

inline bool PlausiblePointer(uintptr_t addr) {
    // Same bounds map.hpp settled on for the map manager: the managers live
    // above 0xa0000000 and a tighter cap read every one of them as absent.
    return addr >= 0x10000000 && addr < 0xf0000000;
}

inline const EcoMapInfo* MapTowerEcoMap() {
#if !WIIXL_SWITCH
    uintptr_t eco = *reinterpret_cast<uintptr_t*>(kEcosystemPtrWiiU);
    if (!PlausiblePointer(eco)) return nullptr;

    auto* info = reinterpret_cast<const EcoMapInfo*>(eco + kMapTowerEcoMapOffset);
    if (!PlausiblePointer(reinterpret_cast<uintptr_t>(info->header))) return nullptr;
    if (!PlausiblePointer(reinterpret_cast<uintptr_t>(info->rowOffsets))) return nullptr;
    if (!PlausiblePointer(reinterpret_cast<uintptr_t>(info->rows))) return nullptr;

    const EcoMapHeader* h = info->header;
    if (h->magic != kEcoMapMagic) return nullptr;
    if (h->numRows < 2 || h->numRows > 0x10000) return nullptr;
    if (h->divisor != 1 && h->divisor != 2 && h->divisor != 10) return nullptr;

    return info;
#else
    return nullptr;
#endif
}

// ksys::eco::Ecosystem::getMapArea, reimplemented. Returns the raw ecomap
// value - 0-based for MapTower.beco - or -1 where the row holds no segment
// covering x, which the game also reports as -1.
//
// The two clamps, the round-half-away-from-zero, the divisor applied to z only
// (and to x as well when it is 10, which no ecomap this reads uses), the row
// clamp to numRows - 2 and the doubled segment offsets are all the game's; this
// is a transcription, not a redesign.
inline int MapArea(const EcoMapInfo* info, float posX, float posZ) {
#if !WIIXL_SWITCH
    if (!info) return -1;

    if (posX < -5000.0f) posX = -5000.0f;
    if (posX > 4999.0f) posX = 4999.0f;
    if (posZ < -4000.0f) posZ = -4000.0f;
    if (posZ > 4000.0f) posZ = 4000.0f;

    const float fx = posX + 5000.0f;
    const float fz = posZ + 4000.0f;
    int x = static_cast<int>(fx + (fx >= 0.0f ? 0.5f : -0.5f));
    int z = static_cast<int>(fz + (fz >= 0.0f ? 0.5f : -0.5f)) / info->header->divisor;

    int row = z;
    if (row < 0) row = 0;
    if (row > info->header->numRows - 2) row = info->header->numRows - 2;

    if (info->header->divisor == 10) x /= 10;

    const int32_t begin = info->rowOffsets[row];
    const int32_t end = info->rowOffsets[row + 1];
    if (begin >= end) return -1;

    // Offsets are halved and relative to the start of the row section.
    auto* segment = reinterpret_cast<const EcoMapSegment*>(info->rows + 2 * begin);
    auto* segmentEnd = reinterpret_cast<const EcoMapSegment*>(info->rows + 2 * end);

    int total = 0;
    while (true) {
        total += segment->length;
        if (x < total) break;
        ++segment;
        if (segment >= segmentEnd) return -1;
    }
    return segment->value;
#else
    (void)info; (void)posX; (void)posZ;
    return -1;
#endif
}

inline bool ValidRegion(int region) {
    return region >= kFirstRegion && region <= kLastRegion;
}

// --- lock state ------------------------------------------------------------

// Bit N-1 set means region N is UNLOCKED. Everything unlocked by default, so a
// mod that calls nothing behaves exactly as the game does.
inline uint32_t& UnlockMask() {
    static uint32_t mask = 0xffffffffu;
    return mask;
}

// --- wall bookkeeping ------------------------------------------------------

// One built panel. Identified by the cell FACE it sits on rather than by a
// pointer, because a spawned actor cannot be handed back (see Actor::Spawn) -
// the actor is re-found by name and position when it is time to remove it.
struct WallRecord {
    bool used = false;
    bool spawned = false;   // false while still queued behind the one-per-tick spawn
    bool wanted = false;    // cleared each rebuild, set again if still needed
    int8_t axis = 0;        // 0 = panel faces along X, 1 = faces along Z
    int32_t cellX = 0;
    int32_t cellZ = 0;
    int8_t layer = 0;      // vertical course; 0 is the bottom of the stack
    bool oriented = false; // rotation applied; spawning cannot set facing
    float pos[3] = {};
};

inline WallRecord* Walls() {
    static WallRecord walls[kMaxWalls];
    return walls;
}

inline bool& Armed() { static bool v = false; return v; }
inline bool& WallsEnabled() { static bool v = true; return v; }
inline bool& PushbackEnabled() { static bool v = false; return v; }

inline float& CellSize() { static float v = 20.0f; return v; }
inline float& BuildRadius() { static float v = 160.0f; return v; }
inline float& WallHeight() { static float v = 80.0f; return v; }
inline float& WallThickness() { static float v = 4.0f; return v; }

// Which quarter turn a face-along-X panel gets; a face-along-Z panel gets the
// next one round. 0..3.
//
// Tunable because the wall actor's authored facing is not written down and was
// wrong the first time it was tried in game - guessing once and hard-coding it
// would mean a rebuild per guess.
inline int& WallYawIndex() { static int v = 0; return v; }

// Quarter turns about X. 0..3, and 0 is correct for this actor.
//
// The tile is already a VERTICAL panel - do not pitch it. Kept only because
// a different wall actor might ship a flat plate, and because guessing this
// wrong once already cost a build.
inline int& WallPitchIndex() { static int v = 0; return v; }

// The cell the last rebuild was centred on, and the height it was built at.
// A rebuild is edge-triggered off these, not run every frame.
inline int32_t& LastCellX() { static int32_t v = 0x7fffffff; return v; }
inline int32_t& LastCellZ() { static int32_t v = 0x7fffffff; return v; }
inline float& LastBuildY() { static float v = 0.0f; return v; }
inline uint32_t& LastMask() { static uint32_t v = 0xffffffffu; return v; }

// Last position the player stood in an allowed region, for the pushback net.
inline float* SafePosition() { static float p[3] = {}; return p; }
inline bool& SafePositionValid() { static bool v = false; return v; }

// The single barrier panel shown where the player is being stopped.
//
// This replaces tiling the whole border. Per-cell placement needed one actor
// per face and multiplied combinatorially when the spacing was tightened -
// 22 panels at one setting, 248 at the next, which crashed the game. The
// boundary does not need covering: it needs a visual AT THE CONTACT POINT,
// which is what the game's own Dark Beast barrier does.
//
// One actor, moved as the player slides along the border, not respawned.
inline bool& MarkerSpawned() { static bool v = false; return v; }
inline float* MarkerPos() { static float p[3] = {}; return p; }
inline int& MarkerIdleTicks() { static int v = 0; return v; }
// Consecutive ticks the marker actor could not be found.
//
// A spawn request is accepted a frame or more before the actor exists, so a
// single failed search means nothing. Treating it as "gone" and spawning a
// replacement leaks a panel every time the first one then turns up - seen in
// game as stray panels standing around.
inline int& MarkerMissTicks() { static int v = 0; return v; }
inline bool& MarkerEnabled() { static bool v = true; return v; }
inline float& MarkerOffset() { static float v = kMarkerOffsetDefault; return v; }
// How much bigger than authored the marker panel is drawn.
//
// Applied at spawn AND in the matrix basis: setMtx writes the whole
// transform, so moving the panel with a unit basis would quietly reset it to
// its authored size on the first move.
inline float& MarkerScale() { static float v = 10.0f; return v; }

// Which way the last slide turned: +1, -1, or 0 for undecided.
//
// Only used to break ties. Trying the side that worked last frame first stops
// the search alternating between mirror-image answers on a serrated border.
inline int8_t& LastTurnSide() { static int8_t v = 0; return v; }

// Rotations applied when searching for a direction the player can still move,
// as cos/sin pairs. The payload has no libm, so they are precomputed.
//
// The correction used to choose between exactly two outcomes - keep X or keep
// Z - which quantises movement along a diagonal border into axis-aligned
// jerks. Rotating the player's own movement in small steps and taking the
// first angle that is still legal is continuous instead, so it is felt as
// sliding along a surface.
//
// A smoothed normal was tried first and REVERTED: averaging a ring of samples
// around the player does not reliably point inward near a concave stretch, so
// it pushed the player the wrong way across the border and pinned them there.
// Every candidate here is tested with AllowedAt before it is used, so that
// failure cannot happen - the worst case is no movement, never a trap.
struct TurnStep { float cosA, sinA; };
inline const TurnStep* TurnSteps() {
    static const TurnStep steps[9] = {
    { 1.000000f, 0.000000f },
    { 0.980785f, 0.195090f },
    { 0.923880f, 0.382683f },
    { 0.831470f, 0.555570f },
    { 0.707107f, 0.707107f },
    { 0.555570f, 0.831470f },
    { 0.382683f, 0.923880f },
    { 0.195090f, 0.980785f },
    { 0.000000f, 1.000000f },
    };
    return steps;
}
constexpr int kTurnStepCount = 9;

inline int32_t FloorDiv(float value, float size) {
    const float q = value / size;
    int32_t i = static_cast<int32_t>(q);
    if (q < 0.0f && static_cast<float>(i) != q) --i;
    return i;
}

inline float CellCentre(int32_t cell, float size) {
    return (static_cast<float>(cell) + 0.5f) * size;
}

// Whether the region covering a cell centre is one the player may not enter.
// A cell whose lookup fails is treated as ALLOWED: failing open keeps a
// half-loaded ecomap from walling the player in.
inline bool CellBlocked(const EcoMapInfo* info, int32_t cellX, int32_t cellZ) {
    const int value = MapArea(info, CellCentre(cellX, CellSize()), CellCentre(cellZ, CellSize()));
    if (value < 0) return false;
    const int region = value + 1;
    if (!ValidRegion(region)) return false;
    return (UnlockMask() & (1u << (region - 1))) == 0;
}

inline int FindWall(int8_t axis, int32_t cellX, int32_t cellZ, int8_t layer) {
    WallRecord* walls = Walls();
    for (int i = 0; i < kMaxWalls; ++i) {
        if (walls[i].used && walls[i].axis == axis && walls[i].cellX == cellX &&
            walls[i].cellZ == cellZ && walls[i].layer == layer) {
            return i;
        }
    }
    return -1;
}

inline int FreeWallSlot() {
    WallRecord* walls = Walls();
    for (int i = 0; i < kMaxWalls; ++i) {
        if (!walls[i].used) return i;
    }
    return -1;
}

// Marks the face between (cellX, cellZ) and its neighbour one step along
// `axis` as needed. The face is named by the LOWER of the two cells, so the
// same face requested from either side lands on one record.
inline void WantFace(int8_t axis, int32_t cellX, int32_t cellZ, float buildY,
                     int8_t layer, float cubeSide, float totalHeight) {
    int existing = FindWall(axis, cellX, cellZ, layer);
    if (existing >= 0) {
        Walls()[existing].wanted = true;
        return;
    }

    const int slot = FreeWallSlot();
    if (slot < 0) return;  // table full; the far side of the sweep goes unwalled

    const float size = CellSize();
    WallRecord& w = Walls()[slot];
    w.used = true;
    w.spawned = false;
    w.wanted = true;
    w.axis = axis;
    w.cellX = cellX;
    w.cellZ = cellZ;
    w.layer = layer;

    if (axis == 0) {
        // The face between cellX and cellX + 1: a plane at their shared edge,
        // spanning the whole of cellZ.
        w.pos[0] = static_cast<float>(cellX + 1) * size;
        w.pos[2] = CellCentre(cellZ, size);
    } else {
        w.pos[0] = CellCentre(cellX, size);
        w.pos[2] = static_cast<float>(cellZ + 1) * size;
    }
    // Courses are stacked around the build height, so the wall reaches as far
    // below the player as above - a border on a slope still gets sealed.
    w.pos[1] = buildY - totalHeight * 0.5f + cubeSide * (static_cast<float>(layer) + 0.5f);
}

}  // namespace impl

// --- region lookup ---------------------------------------------------------

// Whether the region raster is loaded and readable. False on the title screen,
// during a load, and on Switch.
inline bool IsAvailable() {
#if !WIIXL_SWITCH
    return impl::MapTowerEcoMap() != nullptr;
#else
    return false;
#endif
}

// The tower region covering a world position, 1-15, or kNoRegion if the raster
// is not up. y is not part of the lookup - the raster is a top-down map, so a
// point on a mountain and the ground under it are the same region.
inline int GetRegionAt(float x, float z) {
#if !WIIXL_SWITCH
    const int value = impl::MapArea(impl::MapTowerEcoMap(), x, z);
    if (value < 0) return kNoRegion;
    const int region = value + 1;
    return impl::ValidRegion(region) ? region : kNoRegion;
#else
    (void)x; (void)z;
    return kNoRegion;
#endif
}

// Whether the player is allowed to stand at (x, z).
//
// Deliberately matches the inLocked test in Tick, including treating a failed
// lookup as allowed: a raster that has not finished loading must not trap the
// player inside a region it cannot identify.
inline bool AllowedAt(float x, float z) {
    const int region = GetRegionAt(x, z);
    if (region == kNoRegion) return true;
    if (!impl::ValidRegion(region)) return true;
    return (impl::UnlockMask() & (1u << (region - 1))) != 0;
}

// The region the player is standing in, or kNoRegion. Needs Player::Init() to
// have run, or at least a player the tracker can resolve.
inline int GetPlayerRegion() {
#if !WIIXL_SWITCH
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!Player::GetPosition(x, y, z)) return kNoRegion;
    return GetRegionAt(x, z);
#else
    return kNoRegion;
#endif
}

// --- lock state ------------------------------------------------------------

// Whether the player is allowed into a region. Unlocked by default.
//
// This is the MOD's state, not the game's - it is deliberately NOT the
// MapTower_NN flag, so a region can be revealed on the map and still walled
// off, or open on the ground while still dark on the map. SyncFromTowers()
// below is there for the common case where they should agree.
inline bool GetRegionUnlock(int region, bool& out) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(region)) return false;
    out = (impl::UnlockMask() & (1u << (region - 1))) != 0;
    return true;
#else
    (void)region; (void)out;
    return false;
#endif
}

inline bool SetRegionUnlock(int region, bool unlocked) {
#if !WIIXL_SWITCH
    if (!impl::ValidRegion(region)) return false;
    const uint32_t bit = 1u << (region - 1);
    if (unlocked) impl::UnlockMask() |= bit;
    else impl::UnlockMask() &= ~bit;
    return true;
#else
    (void)region; (void)unlocked;
    return false;
#endif
}

inline int SetRegionUnlockAll(bool unlocked) {
#if !WIIXL_SWITCH
    impl::UnlockMask() = unlocked ? 0xffffffffu : 0u;
    return kRegionCount;
#else
    (void)unlocked;
    return 0;
#endif
}

// The whole mask at once, bit N-1 per region N. Handy for saving the lock set
// into a mod's own storage, which is the only place it lives - nothing here
// writes it into the save.
inline uint32_t GetUnlockMask() {
#if !WIIXL_SWITCH
    return impl::UnlockMask();
#else
    return 0xffffffffu;
#endif
}

inline void SetUnlockMask(uint32_t mask) {
#if !WIIXL_SWITCH
    impl::UnlockMask() = mask;
#else
    (void)mask;
#endif
}

// Copies the fifteen MapTower_NN flags into the lock mask: a region is open on
// the ground exactly when its tower has been activated. This is the "the towers
// gate the world" setup in one call. Returns how many flags were read.
//
// Call it after a save loads, and again from an Events::OnTowerOpen callback -
// nothing here watches the flags by itself.
inline int SyncFromTowers() {
#if !WIIXL_SWITCH
    int read = 0;
    for (int region = kFirstRegion; region <= kLastRegion; ++region) {
        bool value = false;
        char name[16];
        // "MapTower_NN"
        const char* prefix = "MapTower_";
        int n = 0;
        for (const char* s = prefix; *s; ++s) name[n++] = *s;
        name[n++] = static_cast<char>('0' + (region / 10) % 10);
        name[n++] = static_cast<char>('0' + region % 10);
        name[n] = '\0';

        if (!GameData::GetFlagBool(name, value)) continue;
        ++read;
        SetRegionUnlock(region, value);
    }
    return read;
#else
    return 0;
#endif
}

inline int CountLockedRegions() {
#if !WIIXL_SWITCH
    int count = 0;
    for (int region = kFirstRegion; region <= kLastRegion; ++region) {
        if ((impl::UnlockMask() & (1u << (region - 1))) == 0) ++count;
    }
    return count;
#else
    return 0;
#endif
}

// Whether a world position is inside a region the player may not enter. A
// position the raster cannot resolve is NOT blocked - see CellBlocked.
inline bool IsBlockedAt(float x, float z) {
#if !WIIXL_SWITCH
    const int region = GetRegionAt(x, z);
    if (region == kNoRegion) return false;
    return (impl::UnlockMask() & (1u << (region - 1))) == 0;
#else
    (void)x; (void)z;
    return false;
#endif
}

// --- walls -----------------------------------------------------------------

// Panel geometry. Defaults are a 20 m grid, 80 m tall panels 4 m thick, built
// out to 160 m around the player.
//
// Cell size trades panel count against how closely the wall follows the border:
// the wall is a staircase on this grid, so 20 m means a wall that can sit up to
// 20 m off the true border. Height is the one to raise if players get over the
// wall by climbing a slope next to it - the panel is centred on the player's
// height when it was built, so it reaches half of this above and below.
inline void SetWallGeometry(float cellSize, float height, float thickness) {
#if !WIIXL_SWITCH
    if (cellSize > 1.0f) impl::CellSize() = cellSize;
    if (height > 1.0f) impl::WallHeight() = height;
    if (thickness > 0.5f) impl::WallThickness() = thickness;
    impl::LastCellX() = 0x7fffffff;  // force a rebuild
#else
    (void)cellSize; (void)height; (void)thickness;
#endif
}

inline void SetBuildRadius(float radius) {
#if !WIIXL_SWITCH
    if (radius > 20.0f) impl::BuildRadius() = radius;
    impl::LastCellX() = 0x7fffffff;
#else
    (void)radius;
#endif
}

// Turns wall building off without unlocking anything - IsBlockedAt and the
// pushback net keep working. Turning it off takes the standing panels down.
inline void SetWallsEnabled(bool enabled);

// The safety net. When on, a player who ends up inside a locked region anyway -
// through a gap in the staircase, over the top of a panel, off a paraglider, or
// out of a shrine that happens to sit across the border - is put back at the
// last place they stood in an allowed region.
//
// Off by default, because it leans on Player::SetPosition, which that header is
// explicit about not having proven. Turn it on and watch what happens before
// relying on it.
inline void SetPushbackEnabled(bool enabled) {
#if !WIIXL_SWITCH
    impl::PushbackEnabled() = enabled;
#else
    (void)enabled;
#endif
}

// Reading the configuration back.
//
// Setters without getters make a partial update impossible: SetWallGeometry
// takes all three values at once, so a caller changing only the height has no
// way to pass the other two through at their current values without reaching
// into impl. These exist so it does not have to.
//
// Note the setters ignore values below their floors rather than clamping to
// them, so a get/modify/set round trip is lossless.
inline float GetCellSize() {
#if !WIIXL_SWITCH
    return impl::CellSize();
#else
    return 0.0f;
#endif
}

inline float GetWallHeight() {
#if !WIIXL_SWITCH
    return impl::WallHeight();
#else
    return 0.0f;
#endif
}

inline float GetWallThickness() {
#if !WIIXL_SWITCH
    return impl::WallThickness();
#else
    return 0.0f;
#endif
}

inline float GetBuildRadius() {
#if !WIIXL_SWITCH
    return impl::BuildRadius();
#else
    return 0.0f;
#endif
}

inline bool GetWallsEnabled() {
#if !WIIXL_SWITCH
    return impl::WallsEnabled();
#else
    return false;
#endif
}

inline bool GetPushbackEnabled() {
#if !WIIXL_SWITCH
    return impl::PushbackEnabled();
#else
    return false;
#endif
}

// How many panels are standing right now, queued ones included.
inline int GetWallCount() {
#if !WIIXL_SWITCH
    int count = 0;
    for (int i = 0; i < kMaxWalls; ++i) {
        if (impl::Walls()[i].used) ++count;
    }
    return count;
#else
    return 0;
#endif
}

// Deletes every panel this header has spawned and forgets them.
//
// Panels are re-found rather than remembered: Actor::Spawn cannot hand the
// created actor back, so this walks the live actors once, matches kWallActor by
// name and the recorded position to within half a cell, and deletes what it
// finds. One traversal for the lot, since ForEachDynamic takes the proc
// manager's lock for the whole walk.
inline int RemoveAllWalls() {
#if !WIIXL_SWITCH
    impl::WallRecord* walls = impl::Walls();

    int pending = 0;
    for (int i = 0; i < kMaxWalls; ++i) {
        if (walls[i].used && walls[i].spawned) ++pending;
    }

    int removed = 0;
    if (pending > 0) {
        const float tolerance = impl::CellSize() * 0.5f;
        Actor::ForEachDynamic([&](const Actor& actor) {
            const char* name = actor.GetName();
            if (!name) return true;

            bool match = true;
            for (const char* a = name, *b = kWallActor;; ++a, ++b) {
                if (*a != *b) { match = false; break; }
                if (*a == '\0') break;
            }
            if (!match) return true;

            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!actor.GetPosition(x, y, z)) return true;

            for (int i = 0; i < kMaxWalls; ++i) {
                if (!walls[i].used || !walls[i].spawned) continue;
                const float dx = x - walls[i].pos[0];
                const float dz = z - walls[i].pos[2];
                const float dy = y - walls[i].pos[1];
                if (dx > -tolerance && dx < tolerance && dz > -tolerance && dz < tolerance &&
                    dy > -tolerance && dy < tolerance) {
                    actor.Delete();
                    walls[i].used = false;
                    ++removed;
                    break;
                }
            }
            return true;
        });
    }

    for (int i = 0; i < kMaxWalls; ++i) walls[i].used = false;
    impl::LastCellX() = 0x7fffffff;
    return removed;
#else
    return 0;
#endif
}

// Sets the quarter turn applied to wall panels (0..3), and forces a rebuild so
// existing panels are re-made at the new facing.
inline void SetWallYaw(int index) {
#if !WIIXL_SWITCH
    impl::WallYawIndex() = index & (kWallYawCount - 1);
    RemoveAllWalls();
    impl::LastCellX() = 0x7fffffff;
#else
    (void)index;
#endif
}

// Sets the quarter turn about X (0..3) that stands the tile up, and rebuilds.
inline void SetWallPitch(int index) {
#if !WIIXL_SWITCH
    impl::WallPitchIndex() = index & 3;
    RemoveAllWalls();
    impl::LastCellX() = 0x7fffffff;
#else
    (void)index;
#endif
}

inline int GetWallPitch() {
#if !WIIXL_SWITCH
    return impl::WallPitchIndex();
#else
    return 0;
#endif
}

inline int GetWallYaw() {
#if !WIIXL_SWITCH
    return impl::WallYawIndex();
#else
    return 0;
#endif
}

inline void SetWallsEnabled(bool enabled) {
#if !WIIXL_SWITCH
    if (impl::WallsEnabled() == enabled) return;
    impl::WallsEnabled() = enabled;
    if (!enabled) RemoveAllWalls();
    impl::LastCellX() = 0x7fffffff;
#else
    (void)enabled;
#endif
}

// Arms the module. Cheap - there is no hook of its own to install, because
// everything here runs off Tick().
//
// Actor::Init() and Player::Init() still have to have been called: Tick needs
// the player position and the spawn queue's flush hook.
inline bool Init() {
#if !WIIXL_SWITCH
    impl::Armed() = true;
    impl::LastCellX() = 0x7fffffff;
    return true;
#else
    return false;
#endif
}

inline void HideMarker();

// Shows the barrier panel at a point on the border, facing the player.
//
// Spawns it once and MOVES it thereafter: Actor::Spawn cannot hand the actor
// back, so it is re-found by name near where it was last put - the same trick
// RemoveAllWalls uses - and then placed with setMtx.
//
// The facing is built straight from the direction to the player rather than
// from an angle, because the payload has no atan2. WallYawIndex composes an
// extra quarter turn on top, since the actor's authored facing is not
// documented and was wrong on the first attempt.
inline void ShowMarkerAt(float x, float y, float z, float towardX, float towardZ) {
#if !WIIXL_SWITCH
    if (!impl::MarkerEnabled()) return;

    // Face the BORDER, not the player.
    //
    // Deriving the facing from the direction to Link made the panel swing
    // round as he moved - it tracked him instead of lying along the boundary.
    // Sampling which side of the point is allowed gives a normal that belongs
    // to the border itself and does not move when the player does.
    //
    // The probe reaches past the raster's own step (about two units) so it
    // reads the local run of the border rather than one stair tread.
    const float probe = 4.0f;
    float dx = 0.0f;
    float dz = 0.0f;
    if (AllowedAt(x + probe, z)) dx += 1.0f;
    if (AllowedAt(x - probe, z)) dx -= 1.0f;
    if (AllowedAt(x, z + probe)) dz += 1.0f;
    if (AllowedAt(x, z - probe)) dz -= 1.0f;

    float len2 = dx * dx + dz * dz;
    if (len2 < 1.0e-6f) {
        // Ring gave nothing to steer by - fall back to the caller's direction.
        dx = towardX - x;
        dz = towardZ - z;
        len2 = dx * dx + dz * dz;
    }
    if (len2 < 1.0e-6f) { dx = 0.0f; dz = 1.0f; }
    else {
        float len = len2;
        for (int k = 0; k < 12; ++k) len = 0.5f * (len + len2 / len);
        dx /= len;
        dz /= len;
    }

    // Compose the direction with the tuning offset: angles add.
    const int idx = impl::WallYawIndex() & (kWallYawCount - 1);
    const float co = WallYawCos()[idx];
    const float so = WallYawSin()[idx];
    const float c = dz * co - dx * so;
    const float sn = dx * co + dz * so;

    float* mp = impl::MarkerPos();

    if (!impl::MarkerSpawned()) {
        void* anchor = Player::GetRaw();
        if (!anchor) return;
        const float sc = impl::MarkerScale();
        if (!Actor::SpawnScaled(kWallActor, Actor(anchor), x, y, z, sc, sc, sc)) return;
        impl::MarkerSpawned() = true;
        mp[0] = x; mp[1] = y; mp[2] = z;
        return;                     // it exists next tick; orient it then
    }

    Actor found;
    float best = 400.0f;            // generous: it may have been nudged
    Actor::ForEachDynamic([&](const Actor& actor) {
        const char* name = actor.GetName();
        if (!name) return true;
        bool match = true;
        for (const char* a = name, *b = kWallActor;; ++a, ++b) {
            if (*a != *b) { match = false; break; }
            if (*a == 0) break;
        }
        if (!match) return true;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        if (!actor.GetPosition(ax, ay, az)) return true;
        const float ddx = ax - mp[0];
        const float ddz = az - mp[2];
        const float d2 = ddx * ddx + ddz * ddz;
        if (d2 <= best) { best = d2; found = actor; }
        return true;
    });

    if (!found.IsValid()) {
        // Not necessarily gone: a spawn is accepted before the actor exists.
        // Only give up after a run of misses, and sweep first, so a late
        // arrival cannot leave a second panel standing.
        if (++impl::MarkerMissTicks() > 30) {
            HideMarker();
            impl::MarkerMissTicks() = 0;
        }
        return;
    }
    impl::MarkerMissTicks() = 0;

    // Basis scaled, or the move resets the panel to its authored size.
    const float ms = impl::MarkerScale();
    const float mtx[12] = {
        c * ms,  0.0f,   sn * ms, x,
          0.0f,    ms,      0.0f, y,
       -sn * ms,  0.0f,    c * ms, z,
    };
    found.SetMtx(mtx, true, false);
    mp[0] = x; mp[1] = y; mp[2] = z;
#else
    (void)x; (void)y; (void)z; (void)towardX; (void)towardZ;
#endif
}

// Removes the marker panel, if one is up.
inline void HideMarker() {
#if !WIIXL_SWITCH
    if (!impl::MarkerSpawned()) return;
    float* mp = impl::MarkerPos();
    Actor::ForEachDynamic([&](const Actor& actor) {
        const char* name = actor.GetName();
        if (!name) return true;
        bool match = true;
        for (const char* a = name, *b = kWallActor;; ++a, ++b) {
            if (*a != *b) { match = false; break; }
            if (*a == 0) break;
        }
        if (!match) return true;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        if (!actor.GetPosition(ax, ay, az)) return true;
        const float ddx = ax - mp[0];
        const float ddz = az - mp[2];
        if (ddx * ddx + ddz * ddz <= 400.0f) actor.Delete();
        return true;
    });
    impl::MarkerSpawned() = false;
#endif
}

inline void SetMarkerEnabled(bool on) {
#if !WIIXL_SWITCH
    impl::MarkerEnabled() = on;
    if (!on) HideMarker();
#else
    (void)on;
#endif
}

inline void SetMarkerOffset(float units) {
#if !WIIXL_SWITCH
    impl::MarkerOffset() = units;
#else
    (void)units;
#endif
}

inline void SetMarkerScale(float scale) {
#if !WIIXL_SWITCH
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 40.0f) scale = 40.0f;   // the spawn scale cap bites well below this
    impl::MarkerScale() = scale;
    HideMarker();                        // respawn at the new size
#else
    (void)scale;
#endif
}

inline float GetMarkerScale() {
#if !WIIXL_SWITCH
    return impl::MarkerScale();
#else
    return 0.0f;
#endif
}

inline float GetMarkerOffset() {
#if !WIIXL_SWITCH
    return impl::MarkerOffset();
#else
    return 0.0f;
#endif
}

inline bool GetMarkerEnabled() {
#if !WIIXL_SWITCH
    return impl::MarkerEnabled();
#else
    return false;
#endif
}

// Run this once per frame, from the mod's own Player::OnTick or
// Actor::OnUpdate callback.
//
// It is NOT self-installing, unlike the rest of the framework, and that is on
// purpose: player.hpp has exactly one OnTick slot, and a module that quietly
// took it would break the mod that wanted it. Call it yourself.
//
// What a tick does:
//
//   * nothing at all, in one comparison, when no region is locked;
//   * records the player's position while they are somewhere allowed, which is
//     what the pushback net puts them back to;
//   * rebuilds the wanted panel set when the player crosses into a new cell,
//     when the lock mask changes, or when they climb or fall far enough that
//     the standing panels no longer cover their height;
//   * spawns ONE queued panel, because Actor::Spawn holds one request at a
//     time. A full rebuild of thirty panels therefore takes thirty frames to
//     finish - half a second, and only after a rebuild.
//
// Returns whether the player is currently inside a locked region.
inline bool Tick() {
#if !WIIXL_SWITCH
    if (!impl::Armed()) return false;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (!Player::GetPosition(px, py, pz)) return false;

    const impl::EcoMapInfo* info = impl::MapTowerEcoMap();
    if (!info) return false;

    const int playerRegion = GetRegionAt(px, pz);
    const bool inLocked = playerRegion != kNoRegion &&
                          (impl::UnlockMask() & (1u << (playerRegion - 1))) == 0;

    if (!inLocked) {
        float* safe = impl::SafePosition();
        safe[0] = px; safe[1] = py; safe[2] = pz;
        impl::SafePositionValid() = true;

        // Back inside: drop the panel after a moment, so brushing the border
        // does not leave one standing in open ground behind you.
        if (impl::MarkerSpawned()) {
            if (++impl::MarkerIdleTicks() > 90) HideMarker();
        }

        // Back inside, so the next crossing starts its search fresh.
        impl::LastTurnSide() = 0;
    } else if (impl::PushbackEnabled() && impl::SafePositionValid()) {
        float* safe = impl::SafePosition();

        // Velocity was tried and does NOT work from outside the engine.
        // motion+0x1b0 is the real Havok storage - the actor cache at +0x25c
        // is copied from it - but the character controller recomputes it every
        // frame from its own input, so a write is an output being overwritten.
        // Measured with the player verifiably still (0.00 idle drift): writing
        // +/-20 on X and on Z moved him 0.0 units on every axis. Changing this
        // to a velocity push silently disables pushback.
        //
        // Y is never corrected: a region boundary is a 2D shape, and restoring
        // the old height fights whatever owns the vertical motion - it sank a
        // swimming player to the river bed.
        const float ny = py;

        // Take this frame's movement and rotate it until it fits.
        //
        // The player wanted to move from safe to (px, pz). Straight ahead is
        // blocked, so try the same distance turned a little to each side and
        // take the first angle that is still inside the region. Small steps
        // make that continuous, which is what "sliding along a wall" feels
        // like; the old keep-X-or-keep-Z rule had only two answers and so
        // moved in jerks along a diagonal border.
        //
        // Every candidate is checked with AllowedAt before use, so the worst
        // outcome is no movement - never a position outside the region.
        float dx = px - safe[0];
        float dz = pz - safe[2];

        float nx = safe[0], nz = safe[2];
        const impl::TurnStep* turns = impl::TurnSteps();
        int8_t& side = impl::LastTurnSide();

        bool resolved = false;
        for (int step = 0; step < impl::kTurnStepCount && !resolved; ++step) {
            const float c = turns[step].cosA;
            const float s = turns[step].sinA;

            // Prefer whichever way we turned last frame, so the search does
            // not flip between mirror-image answers on a serrated border.
            for (int attempt = 0; attempt < 2 && !resolved; ++attempt) {
                const bool positive = (attempt == 0) == (side >= 0);
                const float sn = positive ? s : -s;

                const float rx = dx * c - dz * sn;
                const float rz = dx * sn + dz * c;

                const float cx = safe[0] + rx;
                const float cz = safe[2] + rz;
                if (AllowedAt(cx, cz)) {
                    nx = cx; nz = cz;
                    if (step > 0) side = positive ? 1 : -1;
                    resolved = true;
                }

                if (s == 0.0f) break;      // straight ahead has no mirror
            }
        }


        // Only write when the correction is worth something. This ran every
        // frame the player was outside, including while floating still against
        // the boundary, and a position write per frame resets swim state.
        const float cdx = nx - px;
        const float cdz = nz - pz;
        if (cdx * cdx + cdz * cdz > kMinCorrection * kMinCorrection) {
            Player::NudgePosition(nx, ny, nz);

            // A panel where the player was stopped, turned to face them.
            // Put the panel ON THE BORDER, not on the player.
            //
            // (nx, nz) is where the player was just pushed TO, so using it
            // directly spawned the panel centred on Link. The boundary is
            // outward of that - along the direction he was trying to go - so
            // the panel is offset that way and turned to face back inside.
            float ox = px - nx;
            float oz = pz - nz;
            float ol2 = ox * ox + oz * oz;
            if (ol2 > 1.0e-6f) {
                float ol = ol2;
                for (int k = 0; k < 12; ++k) ol = 0.5f * (ol + ol2 / ol);
                ox /= ol;
                oz /= ol;
            } else {
                ox = 0.0f; oz = 1.0f;
            }
            const float mx = nx + ox * impl::MarkerOffset();
            const float mz = nz + oz * impl::MarkerOffset();
            ShowMarkerAt(mx, ny, mz, nx, nz);
            impl::MarkerIdleTicks() = 0;
        }

        safe[0] = nx; safe[1] = py; safe[2] = nz;
    }

    // Everything below this line is wall building, and none of it is worth a
    // single lookup while the whole map is open.
    if (!impl::WallsEnabled() || impl::UnlockMask() == 0xffffffffu) {
        if (GetWallCount() > 0) RemoveAllWalls();
        return inLocked;
    }

    const float size = impl::CellSize();
    const int32_t cellX = impl::FloorDiv(px, size);
    const int32_t cellZ = impl::FloorDiv(pz, size);

    const bool movedCell = cellX != impl::LastCellX() || cellZ != impl::LastCellZ();
    const bool maskChanged = impl::UnlockMask() != impl::LastMask();
    const float dy = py - impl::LastBuildY();
    const bool heightStale = dy > impl::WallHeight() * 0.4f || dy < -impl::WallHeight() * 0.4f;

    if (movedCell || maskChanged || heightStale) {
        // A panel is centred on the height it was built at, and WantFace only
        // re-marks a face it already has a record for - it does not move one.
        // So when the player has climbed or fallen out of the standing panels'
        // reach, the whole set has to come down and go back up at the new
        // height rather than be re-marked in place. 0.4 of the height is the
        // point where the player is nearly past the top or bottom edge.
        if (heightStale) RemoveAllWalls();

        impl::LastCellX() = cellX;
        impl::LastCellZ() = cellZ;
        impl::LastMask() = impl::UnlockMask();
        impl::LastBuildY() = py;

        impl::WallRecord* walls = impl::Walls();
        // Stack geometry. Collision is a CUBE of half-extent scale*4 taken
        // from the X scale alone, and the creation scale is capped, so
        // height comes from stacking courses rather than from a taller block.
        // The cube must also be at least as wide as a cell or the courses
        // would not meet side to side.
        // The actor's box is authored to size, so it is spawned at scale 1
        // and stacked only if a taller wall than the box is asked for.
        // A vertical BAND of tiles, not a single sheet.
        //
        // WantFace puts a panel at the height the rebuild happened, so on any
        // slope every tile sat at one elevation and floated or sank - seen in
        // game. There is no terrain-height query available here, so instead of
        // guessing the ground the band is stacked around the player's height
        // and some course lands at the right level whatever the slope does.
        //
        // Courses are one tile tall so they meet rather than overlap. The
        // count comes from WallHeight, so regionConfig {"height": N} tunes how
        // much vertical range is covered - and how many actors it costs.
        const float wallHeight = impl::WallHeight();
        const float cubeSide = kWallActorTileSize;
        int8_t layers = static_cast<int8_t>(wallHeight / cubeSide);
        if (layers < 1) layers = 1;
        if (layers > 4) layers = 4;        // 8 courses x many faces crashed the game

        for (int i = 0; i < kMaxWalls; ++i) walls[i].wanted = false;

        const int reach = static_cast<int>(impl::BuildRadius() / size) + 1;
        for (int32_t dz2 = -reach; dz2 <= reach; ++dz2) {
            for (int32_t dx2 = -reach; dx2 <= reach; ++dx2) {
                const int32_t cx = cellX + dx2;
                const int32_t cz = cellZ + dz2;
                if (impl::CellBlocked(info, cx, cz)) continue;

                // An allowed cell walls off each blocked neighbour. Naming the
                // face by its lower cell means the pair only ever produces one
                // panel, whichever side is visited first.
                // Every course of the stack is its own record. Marking only
                // the bottom one would leave a wall the height of a single
                // block, which a windbomb clears trivially.
                for (int8_t k = 0; k < layers; ++k) {
                    if (impl::CellBlocked(info, cx + 1, cz))
                        impl::WantFace(0, cx, cz, py, k, cubeSide, wallHeight);
                    if (impl::CellBlocked(info, cx - 1, cz))
                        impl::WantFace(0, cx - 1, cz, py, k, cubeSide, wallHeight);
                    if (impl::CellBlocked(info, cx, cz + 1))
                        impl::WantFace(1, cx, cz, py, k, cubeSide, wallHeight);
                    if (impl::CellBlocked(info, cx, cz - 1))
                        impl::WantFace(1, cx, cz - 1, py, k, cubeSide, wallHeight);
                }
            }
        }

        // Anything no longer wanted comes down. Records that never got as far
        // as a spawn are just dropped; the rest are re-found and deleted.
        bool anyToRemove = false;
        for (int i = 0; i < kMaxWalls; ++i) {
            if (!walls[i].used || walls[i].wanted) continue;
            if (!walls[i].spawned) { walls[i].used = false; continue; }
            anyToRemove = true;
        }

        if (anyToRemove) {
            const float tolerance = size * 0.5f;
            Actor::ForEachDynamic([&](const Actor& actor) {
                const char* name = actor.GetName();
                if (!name) return true;

                bool match = true;
                for (const char* a = name, *b = kWallActor;; ++a, ++b) {
                    if (*a != *b) { match = false; break; }
                    if (*a == '\0') break;
                }
                if (!match) return true;

                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                if (!actor.GetPosition(ax, ay, az)) return true;

                for (int i = 0; i < kMaxWalls; ++i) {
                    if (!walls[i].used || walls[i].wanted || !walls[i].spawned) continue;
                    const float ddx = ax - walls[i].pos[0];
                    const float ddz = az - walls[i].pos[2];
                    const float ddy = ay - walls[i].pos[1];
                    if (ddx > -tolerance && ddx < tolerance && ddz > -tolerance &&
                        ddz < tolerance && ddy > -tolerance && ddy < tolerance) {
                        actor.Delete();
                        walls[i].used = false;
                        break;
                    }
                }
                return true;
            });

            // Whatever the sweep did not find is dropped anyway rather than
            // held forever - a record with no actor behind it would block its
            // slot and stop that face being rebuilt.
            for (int i = 0; i < kMaxWalls; ++i) {
                if (walls[i].used && !walls[i].wanted) walls[i].used = false;
            }
        }
    }

    // One spawn per tick: the pending-spawn slot holds a single request.
    //
    // The anchor is the player. A spawn with a null anchor is not attempted at
    // all - the pack's anchor setter is handed straight to the game.
    void* anchor = Player::GetRaw();
    if (!anchor) return inLocked;

    impl::WallRecord* walls = impl::Walls();
    for (int i = 0; i < kMaxWalls; ++i) {
        if (!walls[i].used || walls[i].spawned) continue;

        // ONE uniform scale, not a per-axis panel shape.
        //
        // The creation param scales the MODEL per axis but the COLLISION
        // shape only uniformly, from the X component. Measured on a live
        // block by dropping the player onto it: resting height came out at
        // exactly sx * 4 for every combination tried, with sy and sz making
        // no difference at all (sx=1,sy=10,sz=10 -> 4.0; sx=10,sy=1,sz=1 ->
        // 40.0; sx=3 -> 12.0).
        //
        // A thin panel therefore rendered as a wall but collided as a small
        // cube in the middle of it, so faces running along Z stopped the
        // player and faces running along X did not - a barrier that works
        // half the time, which is worse than one that never works.
        //
        // The cost of uniform scale is that a panel is as THICK as it is
        // tall. That is accepted deliberately: neighbouring panels overlap
        // heavily at kCellSize spacing, which also removes the corner gaps
        // the per-axis version left.
        // Same cube the build step sized the stack around: one course per
        // record, side == cell size. Deriving it
        // twice is deliberate - the spawn step runs on ticks where no
        // rebuild happened and has no other way to know it.
        // Scale 1: the pack's own box is the panel. Scaling it would hit the
        // uniform-from-X collision bug that made half the block wall a
        // hologram.
        // Same scale as the marker panel. At authored size these tiles are
        // ~8 units and read as scattered squares; scaled up they cover real
        // ground, so far fewer are needed to line a border.
        const float s = impl::MarkerScale();
        (void)size;

        if (Actor::SpawnScaled(kWallActor, Actor(anchor), walls[i].pos[0],
                               walls[i].pos[1], walls[i].pos[2], s, s, s)) {
            walls[i].spawned = true;
            walls[i].oriented = false;
        }
        break;
    }

    // Turn one freshly spawned panel to face its border.
    //
    // Spawning sets position and scale only, and cannot hand the actor back,
    // so the panel is re-found by name and position - the same trick
    // RemoveAllWalls uses - and then rotated through setMtx. Measured live:
    // the basis goes identity -> yaw and stays put.
    //
    // One per tick, like spawning: this walks the proc list, which is not
    // free, and a panel being unrotated for a frame is not visible anyway
    // since the tile only draws when the player is nearly touching it.
    for (int i = 0; i < kMaxWalls; ++i) {
        if (!walls[i].used || !walls[i].spawned || walls[i].oriented) continue;

        // A face along X wants the tile turned a quarter turn from a face
        // along Z. WallYawIndex picks which of the two is which, because the
        // authored facing is not documented anywhere and was simply wrong the
        // first time it was tried in game.
        const int yawIdx = (impl::WallYawIndex() + (walls[i].axis == 0 ? 0 : 2))
                           & (kWallYawCount - 1);
        const float cy = WallYawCos()[yawIdx];
        const float sy = WallYawSin()[yawIdx];

        // Pitch stands the plate up; yaw then turns it to face the border.
        const int pitchIdx = (impl::WallPitchIndex() * 2) & (kWallYawCount - 1);
        const float cp = WallYawCos()[pitchIdx];
        const float sp = WallYawSin()[pitchIdx];

        const float wx = walls[i].pos[0];
        const float wz = walls[i].pos[2];

        Actor found;
        float best = size * size;          // within a cell of where it was put
        Actor::ForEachDynamic([&](const Actor& actor) {
            const char* name = actor.GetName();
            if (!name) return true;
            bool match = true;
            for (const char* a = name, *b = kWallActor;; ++a, ++b) {
                if (*a != *b) { match = false; break; }
                if (*a == '\0') break;
            }
            if (!match) return true;

            float ax = 0.0f, ay = 0.0f, az = 0.0f;
            if (!actor.GetPosition(ax, ay, az)) return true;
            const float dx = ax - wx;
            const float dz = az - wz;
            const float d2 = dx * dx + dz * dz;
            if (d2 <= best) { best = d2; found = actor; }
            return true;
        });

        if (found.IsValid()) {
            float px2 = 0.0f, py2 = 0.0f, pz2 = 0.0f;
            found.GetPosition(px2, py2, pz2);
            // Ry(yaw) * Rx(pitch), row-major 3x4 with translation in column 3.
            // Basis scaled: setMtx writes the whole transform, so turning a
            // panel with a unit basis would snap it back to authored size.
            const float ws = impl::MarkerScale();
            const float mtx[12] = {
                  cy * ws,  sy * sp * ws,  sy * cp * ws, px2,
                     0.0f,       cp * ws,      -sp * ws, py2,
                 -sy * ws,  cy * sp * ws,  cy * cp * ws, pz2,
            };
            // refresh=false - turning a settled actor, not warping it.
            found.SetMtx(mtx, true, false);
        }

        // Marked either way: a panel whose actor cannot be found is not going
        // to be found next tick either, and retrying forever would walk the
        // proc list every frame for nothing.
        walls[i].oriented = true;
        break;
    }

    return inLocked;
#else
    return false;
#endif
}

}  // namespace WiiXLaunch::BotW::Region
