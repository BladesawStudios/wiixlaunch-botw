#pragma once

// botw.player v1 - this module's export surface for compiled mods.
//
// Small on purpose, but not a token: it is here to exercise the two parts of
// the ABI most likely to break, not merely to prove that registration works.
//
//   1. AN OPAQUE HANDLE CROSSES THE BOUNDARY. Player::GetEquippedSword()
//      returns `Actor` BY VALUE, and Actor is {void*, Kind} - exactly the trap
//      surface.hpp warns about. A mod compiled against one layout and run
//      against a host built from another would read a different field with no
//      build error anywhere. So Actor never crosses. The surface hands out an
//      ActorHandle: an integer the host understands and the mod cannot
//      dereference, passed back in to every accessor.
//
//   2. CALLS RETURN DATA, not just perform actions. ActorGetName fills a
//      caller-owned buffer and returns the length; GetPosition writes three
//      floats through a pointer. Both are the shape every future data-returning
//      surface call has to use, since neither a struct nor a std::string may
//      cross.
//
// HANDLES ARE GENERATION-COUNTED, and the reason is about diagnosis as much as
// safety. An actor can despawn between a mod taking a handle and using it. With
// the naive design - the handle IS the pointer - that use-after-despawn becomes
// a dereference of freed memory INSIDE THE HOST. The crash lands in host code,
// reads as a host bug, and gets reported as one.
//
// Once third parties ship compiled binaries this project cannot rebuild or
// inspect, that distinction is the whole difference between a report that can
// be acted on and a wild goose chase through framework code that was working
// correctly. A handle is (generation << 8) | (slot + 1); resolving checks the
// generation still matches, so a stale handle returns cleanly and the mod that
// held it too long is the thing that gets named. Zero is never valid.
//
// The caller-owned name buffer and the uint32_t returns below are the same
// argument applied to smaller things: each turns a silent misbehaviour at the
// boundary into something a bug report can point at.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/botw/game/player.hpp>
#include <wiixlaunch/botw/game/actor.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces {

constexpr const char* kPlayerSurface = "botw.player";
constexpr uint16_t kPlayerVersionMajor = 1;
// 1.1 appends Init, RegisterTick, ConsumeAttackEvent, SupportsAttackTracking,
// GetPlayerActor, the life accessors and the raw-pointer escape hatches.
// Appending bumps the MINOR, so every mod built against v1.0 still resolves.
constexpr uint16_t kPlayerVersionMinor = 1;

// Opaque to a mod. Never a pointer, never a struct.
using ActorHandle = uint32_t;

namespace impl {

// A small ring. Handles are meant to be taken and used within a frame; a mod
// holding one across many frames is holding a reference to an actor that may
// well be gone, which is what the generation check exists to catch.
constexpr uint32_t kHandleSlots = 32;

struct Slot {
    void* ptr = nullptr;
    uint8_t kind = 0;       // mirrors Actor::Kind
    uint32_t generation = 1; // never 0, so a zeroed handle is never valid
    bool used = false;
};

inline Slot g_Slots[kHandleSlots];
inline uint32_t g_NextSlot = 0;

inline ActorHandle Store(const Actor& a) {
    if (!a.IsValid()) return 0;

    const uint32_t slot = g_NextSlot % kHandleSlots;
    g_NextSlot++;

    Slot& s = g_Slots[slot];
    // Reusing an occupied slot invalidates whatever handle pointed at it, which
    // is the intended behaviour: the oldest handle is the one most likely to be
    // stale anyway. Bumping first means the old handle can never resolve again.
    s.generation++;
    if (s.generation == 0) s.generation = 1;
    s.ptr = a.GetRaw();
    s.kind = static_cast<uint8_t>(a.GetKind());
    s.used = true;

    return (s.generation << 8) | (slot + 1);
}

// Returns false for 0, an out-of-range slot, an unused slot, or a generation
// that has moved on.
inline bool Load(ActorHandle h, Actor& out) {
    if (h == 0) return false;
    const uint32_t slot = (h & 0xFF);
    if (slot == 0 || slot > kHandleSlots) return false;
    const Slot& s = g_Slots[slot - 1];
    if (!s.used || !s.ptr) return false;
    if (s.generation != (h >> 8)) return false;
    out = Actor(s.ptr, static_cast<Actor::Kind>(s.kind));
    return true;
}

// --- botw.player v1 entry points -------------------------------------------
//
// Every one obeys surface.hpp's rules: primitives and out-pointers only.

extern "C" inline ActorHandle PlayerGetEquippedSword()  { return Store(Player::GetEquippedSword()); }
extern "C" inline ActorHandle PlayerGetEquippedShield() { return Store(Player::GetEquippedShield()); }
extern "C" inline ActorHandle PlayerGetEquippedBow()    { return Store(Player::GetEquippedBow()); }

// 1 if the handle still refers to a live actor, 0 otherwise. A mod holding a
// handle across frames should check this rather than assume.
extern "C" inline uint32_t ActorIsValid(ActorHandle h) {
    Actor a;
    return Load(h, a) && a.IsValid() ? 1u : 0u;
}

// Copies the actor's name into a caller-owned buffer and returns the number of
// characters written, excluding the terminator. 0 means "no name available",
// which includes a stale handle.
//
// Returning data this way rather than as `const char*` is deliberate: a pointer
// into the module's own static buffer would be valid only until the next call,
// and nothing in the signature would say so.
extern "C" inline uint32_t ActorGetName(ActorHandle h, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';

    Actor a;
    if (!Load(h, a)) return 0;

    const char* name = a.GetName();
    if (!name) return 0;

    uint32_t n = 0;
    while (name[n] != '\0' && n + 1 < cap) {
        out[n] = name[n];
        ++n;
    }
    out[n] = '\0';
    return n;
}

// Writes x, y, z into out[0..2]. Returns 1 on success.
//
// Not `bool` - bool's size is not guaranteed across a compiled boundary, and
// this is exactly the kind of detail that is invisible until it is wrong.
extern "C" inline uint32_t PlayerGetPosition(float* out) {
    if (!out) return 0;
    float x = 0.f, y = 0.f, z = 0.f;
    if (!Player::GetPosition(x, y, z)) return 0;
    out[0] = x; out[1] = y; out[2] = z;
    return 1;
}

// Capability flag, per the module convention: a mod can ask rather than
// discovering by getting zeroes. Switch has no RE'd position offset.
extern "C" inline uint32_t PlayerSupportsPosition() {
    return Player::SupportsPosition ? 1u : 0u;
}

// --- appended in v1.1 ------------------------------------------------------

// Installs Player's per-frame tick hook, which every cached accessor here needs
// - position, the attack counter, and the player pointer itself.
//
// Idempotent in the module, and it has to be: several mods may each call this
// because each needs the cached state, and none can see that another already
// did. Returns 1 if THIS call installed it, 0 if it was already up. Both are
// success; the distinction is there so a boot log can show which module paid
// for it rather than leaving "already installed" indistinguishable from
// "failed".
extern "C" inline uint32_t PlayerInit() {
    return Player::Init() ? 1u : 0u;
}

// A per-frame callback fired right after Player's own cached state has been
// refreshed for this frame.
//
// NOT Player::OnTick. That is a single slot documented as "call again to
// replace it", which is workable for a source mod that can see the whole tree
// and chain by hand, and unworkable for compiled binaries: two .wxlm mods
// cannot see each other, so the second would silently evict the first and the
// mod that stops working is the one that did nothing wrong. This registers into
// a slot of its own, attributed to the calling module.
//
// Distinct from wiixl.core's RegisterTick, which fires at the GX2 swap after
// the frame is drawn. This one is the only place a mod can read THIS frame's
// position or consume THIS frame's attack event. A mod that needs one does not
// want the other.
//
// Returns 1 on success, 0 if refused - the log names which refusal.
extern "C" inline uint32_t PlayerRegisterTick(void (*fn)()) {
    return Player::AddTick(fn) == Player::TickRegister::Ok ? 1u : 0u;
}

// 1 if the player swung since the last call, and CLEARS the flag.
//
// Consuming is the point: two mods both polling would otherwise each see the
// same swing, or race for it. One consumer per swing, first tick to ask.
extern "C" inline uint32_t PlayerConsumeAttackEvent() {
    return Player::ConsumeAttackEvent() ? 1u : 0u;
}

extern "C" inline uint32_t PlayerSupportsAttackTracking() {
    return Player::SupportsAttackTracking ? 1u : 0u;
}

// A handle to Link himself, so the life accessors below need no player-specific
// duplicates - and so the escape hatch has exactly one shape.
//
// Needs PlayerInit to have run and at least one frame to have passed; returns 0
// until then, which is the same answer a despawned actor gives.
extern "C" inline ActorHandle GetPlayerActor() {
    void* raw = Player::GetRaw();
    if (!raw) return 0;
    return Store(Actor(raw));
}

// --- life ------------------------------------------------------------------
//
// REAL SYMBOLS, not offsets a mod reads for itself. The vtable slots these go
// through (+0x2bc for the life pointer, +0xf4 for max) live in actor.hpp, in
// the versioned game module - so correcting one fixes every compiled mod that
// ever shipped, without rebuilding any of them. A mod that had read the offset
// itself would keep reading the wrong place forever.
//
// Quarter-hearts: 4 per heart, so 3 hearts is 12.

extern "C" inline uint32_t ActorSupportsLife() {
    return Actor::SupportsLife ? 1u : 0u;
}

// Current life, or 0 for a stale handle, an unsupported platform, or a pointer
// that failed validation. 0 is also a legitimate value (a dead actor), so a mod
// that needs to tell those apart checks ActorIsValid first.
extern "C" inline int32_t ActorGetLife(ActorHandle h) {
    Actor a;
    if (!Load(h, a)) return 0;
    return a.GetCurrentLife();
}

extern "C" inline int32_t ActorGetMaxLife(ActorHandle h) {
    Actor a;
    if (!Load(h, a)) return 0;
    return a.GetMaxLife();
}

// Returns 1 if the write happened, 0 if the handle was stale or the platform
// does not support it. NOT void: "I set it and nothing changed" and "I never
// set it" are different problems, and a void return makes them identical.
extern "C" inline uint32_t ActorSetLife(ActorHandle h, int32_t life) {
    Actor a;
    if (!Load(h, a)) return 0;
    if constexpr (!Actor::SupportsLife) return 0;
    a.SetCurrentLife(static_cast<int>(life));
    return 1;
}

// --- the escape hatch ------------------------------------------------------
//
// THIS IS OPTING OUT OF VERSIONING, and the name says so at every call site.
//
// Everything else in this surface is a promise: the signature is frozen for the
// life of v1, and when an offset turns out to be wrong it is fixed HERE and
// every compiled mod that ever shipped gets the fix. A raw pointer is the
// opposite. A mod that takes one and reads +0x4d8 out of it has hard-coded a
// layout into a binary nobody can rebuild, and the day that offset is wrong -
// a different game version, a different region, a corrected piece of RE - it
// reads someone else's memory and there is nothing anyone can do about it from
// this side.
//
// It stays available because there is real work that needs it and being unable
// to do that work is worse. But it is a VISIBLE choice: the name is Unsafe, and
// the host logs the module that used it, once, so a boot log shows who opted
// out rather than leaving it discoverable only by reading a mod's source - which
// for a compiled binary means not at all.
//
// If you need this, say so and it probably belongs in the surface as a real
// symbol instead.
namespace escape {

constexpr uint32_t kMaxNoted = 8;
inline char g_Noted[kMaxNoted][17];
inline uint32_t g_NotedCount = 0;

inline void NoteOnce() {
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

    WIIXL_LOG("botw.player: %s took a RAW POINTER - it has opted out of this "
              "surface's versioning and will not get offset fixes", owner);
}

inline uint32_t Count() { return g_NotedCount; }

} // namespace escape

extern "C" inline uintptr_t ActorUnsafeRawPointer(ActorHandle h) {
    Actor a;
    if (!Load(h, a)) return 0;
    escape::NoteOnce();
    return reinterpret_cast<uintptr_t>(a.GetRaw());
}

// APPEND ONLY. Adding an entry bumps kPlayerVersionMinor; changing or removing
// one bumps kPlayerVersionMajor.
inline const Surface::Symbol kPlayerSymbols[] = {
    WIIXL_SURFACE_SYMBOL("GetEquippedSword",   &PlayerGetEquippedSword),
    WIIXL_SURFACE_SYMBOL("GetEquippedShield",  &PlayerGetEquippedShield),
    WIIXL_SURFACE_SYMBOL("GetEquippedBow",     &PlayerGetEquippedBow),
    WIIXL_SURFACE_SYMBOL("ActorIsValid",       &ActorIsValid),
    WIIXL_SURFACE_SYMBOL("ActorGetName",       &ActorGetName),
    WIIXL_SURFACE_SYMBOL("GetPosition",        &PlayerGetPosition),
    WIIXL_SURFACE_SYMBOL("SupportsPosition",   &PlayerSupportsPosition),
    // v1.1. APPENDED, never inserted: a mod built against v1.0 hashes the same
    // seven names and finds them at the same version, so it keeps working.
    WIIXL_SURFACE_SYMBOL("Init",                    &PlayerInit),
    WIIXL_SURFACE_SYMBOL("RegisterTick",            &PlayerRegisterTick),
    WIIXL_SURFACE_SYMBOL("ConsumeAttackEvent",      &PlayerConsumeAttackEvent),
    WIIXL_SURFACE_SYMBOL("SupportsAttackTracking",  &PlayerSupportsAttackTracking),
    WIIXL_SURFACE_SYMBOL("GetPlayerActor",          &GetPlayerActor),
    WIIXL_SURFACE_SYMBOL("ActorGetLife",            &ActorGetLife),
    WIIXL_SURFACE_SYMBOL("ActorGetMaxLife",         &ActorGetMaxLife),
    WIIXL_SURFACE_SYMBOL("ActorSetLife",            &ActorSetLife),
    WIIXL_SURFACE_SYMBOL("SupportsLife",            &ActorSupportsLife),
    // The escape hatch. Named Unsafe at every call site on purpose, and the
    // host logs whoever uses it - see the comment above ActorUnsafeRawPointer.
    WIIXL_SURFACE_SYMBOL("ActorUnsafeRawPointer",   &ActorUnsafeRawPointer),
};

} // namespace impl

// Registers this module's surfaces with the host.
//
// The HOST calls this, before any mod is loaded - not a static constructor. The
// flat Cemu payload does not run them (see docs/loader.md in base WiiXLaunch),
// so a module that self-registered that way would simply never run.
//
// Idempotent: Surface::Register refuses a duplicate name and logs it, so
// calling twice changes nothing.
inline void Register() {
    Surface::Registration player{};
    player.name = kPlayerSurface;
    player.versionMajor = kPlayerVersionMajor;
    player.versionMinor = kPlayerVersionMinor;
    player.symbols = impl::kPlayerSymbols;
    player.symbolCount =
        static_cast<uint32_t>(sizeof(impl::kPlayerSymbols) / sizeof(impl::kPlayerSymbols[0]));
    Surface::Register(player);
}

} // namespace WiiXLaunch::BotW::Surfaces
