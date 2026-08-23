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
// So AirWallCurseGanon is the one to spawn, and the reason is specific: its
// collision is a property of the ACTOR, defined in its own physics resource, so
// it arrives with the actor rather than being assembled by the map loader from
// a placement record a spawned actor does not have. It is also the only one on
// the dedicated EntityAirWall layer with the AirWall material, which is the
// game's own "this is an invisible wall" pairing.
//
// The half-attached spawn caveat on Actor::Spawn does not bite here. A Fixed
// rigid body is built when the actor is constructed and does nothing per frame
// afterwards, and the decompiled AirWallCurseGanon::calc_ is a straight chain
// to its parent with no body of its own - there is nothing for the Calc it
// never reaches to do. It is also an actor the game itself places (four of
// them, in E-4), which is the stated condition for spawn and delete to behave.
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

// The actor spawned as a wall panel. Public because swapping it is a
// legitimate experiment - AirWallHorse is the obvious alternative if the
// EntityAirWall layer turns out to let something through.
constexpr const char* kWallActor = "AirWallCurseGanon";

// How many panels can stand at once. A border crossing a 2 x kBuildRadius box
// produces on the order of ten to thirty; the rest is headroom for a corner
// where three regions meet.
constexpr int kMaxWalls = 96;

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

// The cell the last rebuild was centred on, and the height it was built at.
// A rebuild is edge-triggered off these, not run every frame.
inline int32_t& LastCellX() { static int32_t v = 0x7fffffff; return v; }
inline int32_t& LastCellZ() { static int32_t v = 0x7fffffff; return v; }
inline float& LastBuildY() { static float v = 0.0f; return v; }
inline uint32_t& LastMask() { static uint32_t v = 0xffffffffu; return v; }

// Last position the player stood in an allowed region, for the pushback net.
inline float* SafePosition() { static float p[3] = {}; return p; }
inline bool& SafePositionValid() { static bool v = false; return v; }

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

inline int FindWall(int8_t axis, int32_t cellX, int32_t cellZ) {
    WallRecord* walls = Walls();
    for (int i = 0; i < kMaxWalls; ++i) {
        if (walls[i].used && walls[i].axis == axis && walls[i].cellX == cellX &&
            walls[i].cellZ == cellZ) {
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
inline void WantFace(int8_t axis, int32_t cellX, int32_t cellZ, float buildY) {
    int existing = FindWall(axis, cellX, cellZ);
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

    if (axis == 0) {
        // The face between cellX and cellX + 1: a plane at their shared edge,
        // spanning the whole of cellZ.
        w.pos[0] = static_cast<float>(cellX + 1) * size;
        w.pos[2] = CellCentre(cellZ, size);
    } else {
        w.pos[0] = CellCentre(cellX, size);
        w.pos[2] = static_cast<float>(cellZ + 1) * size;
    }
    w.pos[1] = buildY;
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
    } else if (impl::PushbackEnabled() && impl::SafePositionValid()) {
        float* safe = impl::SafePosition();
        Player::SetPosition(safe[0], safe[1], safe[2]);
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
                if (impl::CellBlocked(info, cx + 1, cz)) impl::WantFace(0, cx, cz, py);
                if (impl::CellBlocked(info, cx - 1, cz)) impl::WantFace(0, cx - 1, cz, py);
                if (impl::CellBlocked(info, cx, cz + 1)) impl::WantFace(1, cx, cz, py);
                if (impl::CellBlocked(info, cx, cz - 1)) impl::WantFace(1, cx, cz - 1, py);
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

        // AirWallCurseGanon's box is 2 x 2 x 2, so its half-extents are 1 and
        // the scale IS the half-extent in each axis.
        const float halfThick = impl::WallThickness() * 0.5f;
        const float halfHeight = impl::WallHeight() * 0.5f;
        const float halfSpan = size * 0.5f;

        const float sx = walls[i].axis == 0 ? halfThick : halfSpan;
        const float sz = walls[i].axis == 0 ? halfSpan : halfThick;

        if (Actor::SpawnScaled(kWallActor, Actor(anchor), walls[i].pos[0],
                               walls[i].pos[1], walls[i].pos[2], sx, halfHeight, sz)) {
            walls[i].spawned = true;
        }
        break;
    }

    return inLocked;
#else
    return false;
#endif
}

}  // namespace WiiXLaunch::BotW::Region
