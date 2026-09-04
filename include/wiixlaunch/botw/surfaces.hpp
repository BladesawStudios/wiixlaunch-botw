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
#include <wiixlaunch/botw/game/player.hpp>
#include <wiixlaunch/botw/game/actor.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces {

constexpr const char* kPlayerSurface = "botw.player";
constexpr uint16_t kPlayerVersionMajor = 1;
constexpr uint16_t kPlayerVersionMinor = 0;

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
