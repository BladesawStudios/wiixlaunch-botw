#pragma once

// botw.actor v1 - the world, as a mod can reach it.
//
// actor.hpp is the largest header in the module and had five symbols exposed,
// all of them through botw.player. This is the rest: finding actors, moving
// them, spawning them, deleting them, reading and writing their transforms.
//
// ---------------------------------------------------------------------------
// TWO THINGS IN THE MODULE'S API CANNOT CROSS, and both are the same problem
// wearing different clothes.
//
//   std::vector<Actor> - the query functions return one. A mod has no allocator
//   the host shares and no guarantee about the container's layout, so the
//   vector stays on this side and the RESULT is read out by index.
//
//   ForEachDynamic(CallbackFn&&) - a template taking a callable. A compiled
//   binary cannot supply one, and a function pointer would not be the same
//   thing.
//
// Both become Query-then-read: one call runs the search and reports how many it
// found, and the results are fetched one at a time. The results are HANDLES, so
// an actor that dies between the query and the read is refused rather than
// dereferenced.
//
// The buffer is fixed and the count is capped. A query that matches more than
// kMaxQueryResults reports the cap and says so in the log, rather than
// truncating quietly - "I got 256 Bokoblins" and "there were 400 and you were
// given 256" are different facts and a mod deciding what to do needs the second.
//
// ---------------------------------------------------------------------------
// VECTORS CROSS AS float[3], MATRICES AS float[12]. Same rule as everywhere
// else: no struct, and the caller owns the storage.
//
// ---------------------------------------------------------------------------
// 1.1 appends QueryRay, QueryScanned and GetPhysicsObject - what an actor
// picker needs, found by porting one (the BotW debug tool) to a .wxlm.
//
// A picker has to consider every actor in the world, live and placed - over
// 4000 on the field - and keep the handful near an aim line. Query cannot do
// that: it caps at 256 and filters only by name, so a mod would be choosing
// which 256 of 4000 to look at before it knew where any of them were. QueryRay
// does the geometry on this side, inside the walk, and hands back only what
// the ray passes close to.
//
// A mod that also needs fields no surface names - the debug tool dumps HashId
// and the map-object link, which are research, not API - takes the actor's
// address through botw.player's ActorUnsafeRawPointer. It accepts any actor
// handle, since the handle table is shared, and logs who used it; there is
// deliberately no second, unlogged way to get one here.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/botw/game/actor.hpp>
#include <wiixlaunch/botw/surfaces/actor_handles.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::ActorSurface {

constexpr const char* kName = "botw.actor";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 1;

// A query can match more actors than this; the count reported is what was
// stored, and the log says when matches were dropped.
constexpr uint32_t kMaxQueryResults = 256;

namespace H = ActorHandles;

namespace impl {

inline H::Handle g_Results[kMaxQueryResults];
inline uint32_t g_ResultCount = 0;
inline uint32_t g_LastQueryMatched = 0;   // before the cap
inline uint32_t g_LastQueryScanned = 0;   // visited at all, matched or not

inline uint32_t CopyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src) return 0;
    uint32_t n = 0;
    while (src[n] && n + 1 < cap) { out[n] = src[n]; ++n; }
    out[n] = '\0';
    return n;
}

// --- lifecycle -------------------------------------------------------------

// Installs the actor-system hooks. Idempotent in the module, for the same
// reason Player::Init is: several mods may each need it and none can see that
// another already did.
extern "C" inline uint32_t AcInit() {
    Actor::Init();
    return 1;
}

extern "C" inline int32_t AcCount() {
    return static_cast<int32_t>(Actor::GetCount());
}

// --- finding ---------------------------------------------------------------
//
// kind: 0 dynamic, 1 static, 2 all. One call with a selector rather than three
// symbols, because a mod filtering "everything called Bokoblin" almost never
// cares which list it came from, and the ones that do pass a different number.

extern "C" inline int32_t AcQuery(int32_t kind, const char* name, uint32_t exactMatch) {
    g_ResultCount = 0;
    g_LastQueryMatched = 0;
    g_LastQueryScanned = 0;

    auto take = [&](const Actor& a) {
        ++g_LastQueryMatched;
        if (g_ResultCount >= kMaxQueryResults) return;
        const H::Handle h = H::Store(a);
        if (h != H::kInvalid) g_Results[g_ResultCount++] = h;
    };

    // Walked with the iterators, not the helpers that return a std::vector. This payload links newlib's malloc but has
    // no _sbrk, so the first push_back sends malloc to address 0 - the start of the payload, which is the boot stub -
    // and the game's file-system constructor is then resumed with an allocation size for its `this`.
    const bool exact = exactMatch != 0;
    const bool filtered = name && name[0] != '\0';
    auto visit = [&](const Actor& a) {
        ++g_LastQueryScanned;
        if (filtered) {
            const char* actorName = a.GetName();
            const bool match = exact ? std::strcmp(actorName, name) == 0 : std::strstr(actorName, name) != nullptr;
            if (!match) return;
        }
        take(a);
    };
    if (kind == 0) {
        Actor::ForEachDynamic(visit);
    } else if (kind == 1) {
        Actor::ForEachStatic(visit);
    } else {
        Actor::ForEach(visit);
    }

    if (g_LastQueryMatched > g_ResultCount) {
        WIIXL_LOG("botw.actor: query matched %u actor(s), %u returned - the rest "
                  "were dropped at the result cap", g_LastQueryMatched, g_ResultCount);
    }
    return static_cast<int32_t>(g_ResultCount);
}

// How many the query actually matched, before the cap. A mod that got the cap
// back can tell the difference between "that is all of them" and "there were
// more" - which a count alone cannot say.
extern "C" inline int32_t AcQueryMatched() {
    return static_cast<int32_t>(g_LastQueryMatched);
}

extern "C" inline uint32_t AcQueryAt(int32_t index) {
    if (index < 0 || static_cast<uint32_t>(index) >= g_ResultCount) return H::kInvalid;
    return g_Results[index];
}

// QueryRay's flags.
constexpr uint32_t kRayHorizontal = 1u;   // measure in the XZ plane, ignore height
constexpr uint32_t kRaySkipOrigin = 2u;   // drop actors sitting exactly at (0,0,0)

// Kept nearest-first while the walk runs, then turned into handles.
struct RayHit {
    void* ptr;
    uint8_t kind;
    float along;
};
inline RayHit g_RayHits[kMaxQueryResults];

// v1.1. Every actor the ray passes within maxMiss of, between minAlong and
// maxAlong along it, NEAREST FIRST - the order matters more than it looks:
// when more than kMaxQueryResults match, the farthest are the ones dropped,
// so the cap costs a picker the actors it was least likely to want.
//
// origin3/dir3 are float[3]; dir need not be unit length, it is normalised
// here. kind is Query's selector (0 dynamic, 1 static, 2 all). Read the
// results with QueryAt, QueryMatched and QueryScanned, exactly as Query's.
//
// flags is a mask. 1 (kRayHorizontal) flattens both the ray and each offset
// onto the ground plane, for an aim direction that has a yaw and no pitch: the
// miss is then measured sideways only, and height is the mod's to judge.
//
// 2 (kRaySkipOrigin) exists because the cap would otherwise be spent on noise.
// Event tags, system actors and anything never placed report (0,0,0), and
// they are most of the live list - one debug-tool dump found 3380 of 4278
// actors "on the ray" because the player faced the map centre. A real actor
// at exactly the origin is lost with them.
//
// The handles are made after the walk, outside the proc manager's lock, so an
// actor can die between being measured and being handed out. That is the same
// promise every handle makes - a generation check, not a liveness guarantee -
// and IsValid answers the rest.
extern "C" inline int32_t AcQueryRay(const float* origin3, const float* dir3,
                                     float minAlong, float maxAlong, float maxMiss,
                                     int32_t kind, uint32_t flags) {
    g_ResultCount = 0;
    g_LastQueryMatched = 0;
    g_LastQueryScanned = 0;
    if (!origin3 || !dir3 || !(maxMiss >= 0.0f) || !(maxAlong >= minAlong)) return 0;

    const bool horizontal = (flags & kRayHorizontal) != 0;
    const bool skipOrigin = (flags & kRaySkipOrigin) != 0;

    float dx = dir3[0], dy = horizontal ? 0.0f : dir3[1], dz = dir3[2];
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(length > 0.0001f)) return 0;
    dx /= length; dy /= length; dz /= length;

    const float ox = origin3[0], oy = origin3[1], oz = origin3[2];
    const float maxMissSq = maxMiss * maxMiss;
    uint32_t kept = 0;

    auto visit = [&](const Actor& a) {
        ++g_LastQueryScanned;

        float x, y, z;
        if (!a.GetPosition(x, y, z)) return;
        // NaN fails every comparison below, so a torn transform drops out.
        if (skipOrigin && x > -0.001f && x < 0.001f && y > -0.001f && y < 0.001f &&
            z > -0.001f && z < 0.001f) {
            return;
        }

        const float vx = x - ox, vy = horizontal ? 0.0f : y - oy, vz = z - oz;
        const float along = vx * dx + vy * dy + vz * dz;
        if (!(along >= minAlong) || !(along <= maxAlong)) return;

        const float px = vx - along * dx, py = vy - along * dy, pz = vz - along * dz;
        if (!(px * px + py * py + pz * pz <= maxMissSq)) return;

        ++g_LastQueryMatched;

        // Insertion into a sorted run. A full run drops whichever is farthest,
        // which may be the newcomer.
        uint32_t slot = kept;
        while (slot > 0 && g_RayHits[slot - 1].along > along) --slot;
        if (slot >= kMaxQueryResults) return;
        const uint32_t last = kept < kMaxQueryResults ? kept : kMaxQueryResults - 1;
        for (uint32_t i = last; i > slot; --i) g_RayHits[i] = g_RayHits[i - 1];
        g_RayHits[slot] = RayHit{a.GetRaw(), static_cast<uint8_t>(a.GetKind()), along};
        if (kept < kMaxQueryResults) ++kept;
    };

    if (kind == 0) {
        Actor::ForEachDynamic(visit);
    } else if (kind == 1) {
        Actor::ForEachStatic(visit);
    } else {
        Actor::ForEach(visit);
    }

    for (uint32_t i = 0; i < kept; ++i) {
        const H::Handle h = H::Store(Actor(g_RayHits[i].ptr,
                                           static_cast<Actor::Kind>(g_RayHits[i].kind)));
        if (h != H::kInvalid) g_Results[g_ResultCount++] = h;
    }

    if (g_LastQueryMatched > g_ResultCount) {
        WIIXL_LOG("botw.actor: ray query matched %u actor(s), %u returned - the "
                  "farthest were dropped at the result cap", g_LastQueryMatched, g_ResultCount);
    }
    return static_cast<int32_t>(g_ResultCount);
}

// v1.1. How many actors the last Query or QueryRay visited, matched or not. A
// picker reporting "nothing on the ray" needs to say whether it looked at
// four thousand actors or at none - an empty world and a broken walk give the
// same empty result otherwise.
extern "C" inline int32_t AcQueryScanned() {
    return static_cast<int32_t>(g_LastQueryScanned);
}

// The single-result lookups, which do not disturb the query buffer - a mod
// mid-enumeration can still ask about something by name.
extern "C" inline uint32_t AcFindByName(const char* name, uint32_t exactMatch) {
    if (!name) return H::kInvalid;
    return H::Store(Actor::GetActor(name, exactMatch != 0));
}

extern "C" inline uint32_t AcFindById(uint32_t id) {
    return H::Store(Actor::GetActor(id));
}

extern "C" inline uint32_t AcIsValidActorName(const char* name) {
    return name && Actor::IsValidActorName(name) ? 1u : 0u;
}

// --- what an actor is ------------------------------------------------------

extern "C" inline uint32_t AcIsValid(uint32_t handle) {
    Actor a;
    return H::Load(handle, a) && a.IsValid() ? 1u : 0u;
}

extern "C" inline uint32_t AcGetName(uint32_t handle, char* out, uint32_t cap) {
    Actor a;
    if (!H::Load(handle, a)) { if (out && cap) out[0] = '\0'; return 0; }
    return CopyOut(a.GetName(), out, cap);
}

extern "C" inline uint32_t AcGetId(uint32_t handle, uint32_t* out) {
    Actor a;
    if (!out || !H::Load(handle, a)) return 0;
    *out = a.GetId();
    return 1;
}

extern "C" inline uint32_t AcGetState(uint32_t handle, uint32_t* out) {
    Actor a;
    if (!out || !H::Load(handle, a)) return 0;
    *out = a.GetState();
    return 1;
}

extern "C" inline int32_t AcGetKind(uint32_t handle) {
    Actor a;
    if (!H::Load(handle, a)) return -1;
    return static_cast<int32_t>(a.GetKind());
}

extern "C" inline uint32_t AcIsActive(uint32_t handle) {
    Actor a; return H::Load(handle, a) && a.IsActive() ? 1u : 0u;
}
extern "C" inline uint32_t AcIsDying(uint32_t handle) {
    Actor a; return H::Load(handle, a) && a.IsDying() ? 1u : 0u;
}
extern "C" inline uint32_t AcIsSleeping(uint32_t handle) {
    Actor a; return H::Load(handle, a) && a.IsSleeping() ? 1u : 0u;
}
extern "C" inline uint32_t AcIsAttached(uint32_t handle) {
    Actor a; return H::Load(handle, a) && a.IsAttached() ? 1u : 0u;
}
extern "C" inline uint32_t AcIsDynamic(uint32_t handle) {
    Actor a; return H::Load(handle, a) && a.IsDynamic() ? 1u : 0u;
}
extern "C" inline uint32_t AcIsPlacement(uint32_t handle) {
    Actor a; return H::Load(handle, a) && a.IsPlacement() ? 1u : 0u;
}

// v1.1. The physics object behind a live actor, from the actor's own vtable
// slot 0x30C - a game call, which is why it is here and not left to a mod
// holding a raw pointer. 0 for a placement, a dead handle, or an actor
// without physics.
extern "C" inline uintptr_t AcGetPhysicsObject(uint32_t handle) {
    Actor a;
    if (!H::Load(handle, a)) return 0;
    return reinterpret_cast<uintptr_t>(a.GetPhysicsObject());
}

// --- transform -------------------------------------------------------------

extern "C" inline uint32_t AcGetPosition(uint32_t handle, float* out3) {
    Actor a;
    if (!out3 || !H::Load(handle, a)) return 0;
    return a.GetPosition(out3[0], out3[1], out3[2]) ? 1u : 0u;
}

// SetPosition, WarpTo and NudgeTo are three symbols because they are three
// different acts on a physics object, and the module keeps them apart for
// reasons its own comments give at length. Collapsing them here would hand a
// mod one call whose behaviour depends on something it cannot see.
extern "C" inline uint32_t AcSetPosition(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.SetPosition(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcWarpTo(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.WarpTo(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcNudgeTo(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.NudgeTo(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcGetRotation(uint32_t handle, float* out3) {
    Actor a;
    if (!out3 || !H::Load(handle, a)) return 0;
    return a.GetRotation(out3[0], out3[1], out3[2]) ? 1u : 0u;
}

extern "C" inline uint32_t AcSetRotation(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.SetRotation(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcSpinBy(uint32_t handle, float yaw, float pitch, float roll) {
    Actor a;
    return H::Load(handle, a) && a.SpinBy(yaw, pitch, roll) ? 1u : 0u;
}

// The 3x4 matrix, as twelve floats the caller owns.
extern "C" inline uint32_t AcGetMatrix(uint32_t handle, float* out12) {
    Actor a;
    if (!out12 || !H::Load(handle, a)) return 0;
    return a.GetMatrix(out12) ? 1u : 0u;
}

extern "C" inline uint32_t AcSetMatrix(uint32_t handle, const float* mtx12) {
    Actor a;
    if (!mtx12 || !H::Load(handle, a)) return 0;
    return a.SetMatrix(mtx12) ? 1u : 0u;
}

// --- physics ---------------------------------------------------------------

extern "C" inline uint32_t AcGetLinearVelocity(uint32_t handle, float* out3) {
    Actor a;
    if (!out3 || !H::Load(handle, a)) return 0;
    return a.GetLinearVelocity(out3[0], out3[1], out3[2]) ? 1u : 0u;
}

extern "C" inline uint32_t AcSetLinearVelocity(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.SetLinearVelocity(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcAddLinearVelocity(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.AddLinearVelocity(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcGetHavokVelocity(uint32_t handle, float* out3) {
    Actor a;
    if (!out3 || !H::Load(handle, a)) return 0;
    return a.GetHavokVelocity(out3[0], out3[1], out3[2]) ? 1u : 0u;
}

// The controller vectors, which are what a walking actor is actually steered
// by - distinct from the physics velocity above, and confusing the two is the
// classic way to make an enemy vibrate in place.
extern "C" inline uint32_t AcGetControllerVelocity(uint32_t handle, float* out3) {
    Actor a;
    if (!out3 || !H::Load(handle, a)) return 0;
    return a.GetControllerVelocity(out3[0], out3[1], out3[2]) ? 1u : 0u;
}

extern "C" inline uint32_t AcSetControllerVelocity(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.SetControllerVelocity(x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcGetControllerDirection(uint32_t handle, float* out3) {
    Actor a;
    if (!out3 || !H::Load(handle, a)) return 0;
    return a.GetControllerDirection(out3[0], out3[1], out3[2]) ? 1u : 0u;
}

extern "C" inline uint32_t AcSetControllerDirection(uint32_t handle, float x, float y, float z) {
    Actor a;
    return H::Load(handle, a) && a.SetControllerDirection(x, y, z) ? 1u : 0u;
}

// --- life ------------------------------------------------------------------
//
// The same accessors botw.player exposes, on any actor rather than only Link.
// Duplicated deliberately: a mod that only wants Link's health should not have
// to declare botw.actor as a dependency, and one enumerating enemies should not
// have to go through a player surface.

extern "C" inline int32_t AcGetLife(uint32_t handle) {
    Actor a;
    return H::Load(handle, a) ? static_cast<int32_t>(a.GetCurrentLife()) : 0;
}

extern "C" inline int32_t AcGetMaxLife(uint32_t handle) {
    Actor a;
    return H::Load(handle, a) ? static_cast<int32_t>(a.GetMaxLife()) : 0;
}

extern "C" inline uint32_t AcSetLife(uint32_t handle, int32_t life) {
    Actor a;
    if (!H::Load(handle, a)) return 0;
    if constexpr (!Actor::SupportsLife) return 0;
    a.SetCurrentLife(static_cast<int>(life));
    return 1;
}

extern "C" inline uint32_t AcSetMaxLife(uint32_t handle, int32_t maxLife) {
    Actor a;
    return H::Load(handle, a) && a.SetMaxLife(static_cast<int>(maxLife)) ? 1u : 0u;
}

extern "C" inline uint32_t AcSupportsLife() { return Actor::SupportsLife ? 1u : 0u; }

// Hearts rather than quarter-hearts. Both are offered because both are the
// natural unit somewhere: the game stores quarters, and a UI shows hearts. A
// mod converting between them itself is a mod that will one day round the wrong
// way on a half-heart.
extern "C" inline uint32_t AcGetHearts(uint32_t handle, float* out) {
    Actor a;
    if (!out || !H::Load(handle, a)) return 0;
    *out = a.GetCurrentHearts();
    return 1;
}

extern "C" inline uint32_t AcSetHearts(uint32_t handle, float hearts) {
    Actor a;
    if (!H::Load(handle, a)) return 0;
    a.SetCurrentHearts(hearts);
    return 1;
}

extern "C" inline uint32_t AcGetMaxHearts(uint32_t handle, float* out) {
    Actor a;
    if (!out || !H::Load(handle, a)) return 0;
    *out = a.GetMaxHearts();
    return 1;
}

extern "C" inline uint32_t AcSetMaxHearts(uint32_t handle, float hearts) {
    Actor a;
    return H::Load(handle, a) && a.SetMaxHearts(hearts) ? 1u : 0u;
}

// The matrix write that goes through the game's own setMtx, which is a
// different act from SetMatrix: it tells the engine the actor moved rather than
// editing the numbers behind its back. setActorMtx picks which of the two the
// game is told about.
extern "C" inline uint32_t AcSetMtx(uint32_t handle, const float* mtx12,
                                    uint32_t setActorMtx) {
    Actor a;
    if (!mtx12 || !H::Load(handle, a)) return 0;
    return a.SetMtx(mtx12, setActorMtx != 0) ? 1u : 0u;
}

// --- creating and destroying ----------------------------------------------

// Spawned relative to an anchor actor, which is how the game's own spawn works:
// the anchor supplies the manager and the heap. Pass the player's handle for
// "near Link".
extern "C" inline uint32_t AcSpawn(const char* actorName, uint32_t anchorHandle,
                                   float x, float y, float z) {
    Actor anchor;
    if (!actorName || !H::Load(anchorHandle, anchor)) return 0;
    return Actor::Spawn(actorName, anchor, x, y, z) ? 1u : 0u;
}

extern "C" inline uint32_t AcSpawnScaled(const char* actorName, uint32_t anchorHandle,
                                         float x, float y, float z,
                                         float scaleX, float scaleY, float scaleZ) {
    Actor anchor;
    if (!actorName || !H::Load(anchorHandle, anchor)) return 0;
    return Actor::SpawnScaled(actorName, anchor, x, y, z, scaleX, scaleY, scaleZ) ? 1u : 0u;
}

extern "C" inline uint32_t AcDelete(uint32_t handle, uint32_t reason) {
    Actor a;
    return H::Load(handle, a) && a.Delete(reason) ? 1u : 0u;
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Init",                &AcInit),
    WIIXL_SURFACE_SYMBOL("Count",               &AcCount),
    WIIXL_SURFACE_SYMBOL("SupportsLife",        &AcSupportsLife),

    WIIXL_SURFACE_SYMBOL("Query",               &AcQuery),
    WIIXL_SURFACE_SYMBOL("QueryMatched",        &AcQueryMatched),
    WIIXL_SURFACE_SYMBOL("QueryAt",             &AcQueryAt),
    WIIXL_SURFACE_SYMBOL("FindByName",          &AcFindByName),
    WIIXL_SURFACE_SYMBOL("FindById",            &AcFindById),
    WIIXL_SURFACE_SYMBOL("IsValidActorName",    &AcIsValidActorName),

    WIIXL_SURFACE_SYMBOL("IsValid",             &AcIsValid),
    WIIXL_SURFACE_SYMBOL("GetName",             &AcGetName),
    WIIXL_SURFACE_SYMBOL("GetId",               &AcGetId),
    WIIXL_SURFACE_SYMBOL("GetState",            &AcGetState),
    WIIXL_SURFACE_SYMBOL("GetKind",             &AcGetKind),
    WIIXL_SURFACE_SYMBOL("IsActive",            &AcIsActive),
    WIIXL_SURFACE_SYMBOL("IsDying",             &AcIsDying),
    WIIXL_SURFACE_SYMBOL("IsSleeping",          &AcIsSleeping),
    WIIXL_SURFACE_SYMBOL("IsAttached",          &AcIsAttached),
    WIIXL_SURFACE_SYMBOL("IsDynamic",           &AcIsDynamic),
    WIIXL_SURFACE_SYMBOL("IsPlacement",         &AcIsPlacement),

    WIIXL_SURFACE_SYMBOL("GetPosition",         &AcGetPosition),
    WIIXL_SURFACE_SYMBOL("SetPosition",         &AcSetPosition),
    WIIXL_SURFACE_SYMBOL("WarpTo",              &AcWarpTo),
    WIIXL_SURFACE_SYMBOL("NudgeTo",             &AcNudgeTo),
    WIIXL_SURFACE_SYMBOL("GetRotation",         &AcGetRotation),
    WIIXL_SURFACE_SYMBOL("SetRotation",         &AcSetRotation),
    WIIXL_SURFACE_SYMBOL("SpinBy",              &AcSpinBy),
    WIIXL_SURFACE_SYMBOL("GetMatrix",           &AcGetMatrix),
    WIIXL_SURFACE_SYMBOL("SetMatrix",           &AcSetMatrix),

    WIIXL_SURFACE_SYMBOL("GetLinearVelocity",   &AcGetLinearVelocity),
    WIIXL_SURFACE_SYMBOL("SetLinearVelocity",   &AcSetLinearVelocity),
    WIIXL_SURFACE_SYMBOL("AddLinearVelocity",   &AcAddLinearVelocity),
    WIIXL_SURFACE_SYMBOL("GetHavokVelocity",    &AcGetHavokVelocity),
    WIIXL_SURFACE_SYMBOL("GetControllerVelocity",  &AcGetControllerVelocity),
    WIIXL_SURFACE_SYMBOL("SetControllerVelocity",  &AcSetControllerVelocity),
    WIIXL_SURFACE_SYMBOL("GetControllerDirection", &AcGetControllerDirection),
    WIIXL_SURFACE_SYMBOL("SetControllerDirection", &AcSetControllerDirection),

    WIIXL_SURFACE_SYMBOL("GetLife",             &AcGetLife),
    WIIXL_SURFACE_SYMBOL("GetMaxLife",          &AcGetMaxLife),
    WIIXL_SURFACE_SYMBOL("SetLife",             &AcSetLife),
    WIIXL_SURFACE_SYMBOL("SetMaxLife",          &AcSetMaxLife),

    WIIXL_SURFACE_SYMBOL("GetHearts",           &AcGetHearts),
    WIIXL_SURFACE_SYMBOL("SetHearts",           &AcSetHearts),
    WIIXL_SURFACE_SYMBOL("GetMaxHearts",        &AcGetMaxHearts),
    WIIXL_SURFACE_SYMBOL("SetMaxHearts",        &AcSetMaxHearts),
    WIIXL_SURFACE_SYMBOL("SetMtx",              &AcSetMtx),

    WIIXL_SURFACE_SYMBOL("Spawn",               &AcSpawn),
    WIIXL_SURFACE_SYMBOL("SpawnScaled",         &AcSpawnScaled),
    WIIXL_SURFACE_SYMBOL("Delete",              &AcDelete),

    // v1.1. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("QueryRay",            &AcQueryRay),
    WIIXL_SURFACE_SYMBOL("QueryScanned",        &AcQueryScanned),
    WIIXL_SURFACE_SYMBOL("GetPhysicsObject",    &AcGetPhysicsObject),
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

} // namespace WiiXLaunch::BotW::Surfaces::ActorSurface
