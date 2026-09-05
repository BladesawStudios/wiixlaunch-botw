#pragma once

// ONE handle table, shared by every surface that hands out an actor.
//
// This started inside botw.player, which was fine while botw.player was the
// only surface that produced one. It stops being fine the moment botw.actor
// exists: GetPlayerActor() hands back a handle, and the obvious next thing a
// mod does is pass it to botw.actor's WarpTo. With a table per surface that
// handle would be meaningless in the other one - either refused, or worse,
// silently resolving to whatever occupied the same slot in the other table.
//
// So there is one table and one handle space, and a handle means the same thing
// to every surface that takes one.
//
// ---------------------------------------------------------------------------
// GENERATION-COUNTED, for the reason botw.player's comment already gave: an
// actor can despawn between a mod taking a handle and using it, and with the
// naive design - the handle IS the pointer - that use-after-despawn is a
// dereference of freed memory INSIDE THE HOST. The crash lands in host code,
// reads as a host bug, and gets reported as one.
//
//   bits  0..15   slot index + 1     (0 is never a valid handle)
//   bits 16..31   generation, moved every time a slot is reused
//
// The encoding changed from botw.player's original 8-bit slot when this moved
// out: 32 slots was enough for "the sword, the shield, the bow" and is not
// enough for a query that returns every Bokoblin on the field. Handles are
// opaque to mods, so widening the slot field is not an ABI change - no
// signature moved, and a mod cannot have been reading the bits.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/botw/game/actor.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::ActorHandles {

// Opaque to a mod. Never a pointer, never a struct.
using Handle = uint32_t;
constexpr Handle kInvalid = 0;

// Enough for a query over a populated area. A ring, so the oldest handle is the
// one that stops resolving - which is also the one most likely to be stale
// anyway, since a mod that held a handle for hundreds of intervening actors was
// holding a reference to something long gone.
constexpr uint32_t kSlots = 256;

namespace impl {

struct Slot {
    void* ptr = nullptr;
    uint8_t kind = 0;          // mirrors Actor::Kind
    uint16_t generation = 1;   // never 0, so a zeroed handle is never valid
    bool used = false;
};

inline Slot g_Slots[kSlots];
inline uint32_t g_Next = 0;

} // namespace impl

inline Handle Store(const Actor& a) {
    if (!a.IsValid()) return kInvalid;

    const uint32_t slot = impl::g_Next % kSlots;
    impl::g_Next++;

    impl::Slot& s = impl::g_Slots[slot];
    // Bumped BEFORE the slot is rewritten, so the handle that pointed here
    // last can never resolve again - not even for the instant between.
    s.generation++;
    if (s.generation == 0) s.generation = 1;
    s.ptr = a.GetRaw();
    s.kind = static_cast<uint8_t>(a.GetKind());
    s.used = true;

    return (static_cast<uint32_t>(s.generation) << 16) | (slot + 1u);
}

// False for 0, an out-of-range slot, an unused slot, or a generation that has
// moved on. Never dereferences the stored pointer - IsValid() on the returned
// Actor is a separate question, and one the caller may legitimately want to ask
// about an actor that has since died.
inline bool Load(Handle h, Actor& out) {
    if (h == kInvalid) return false;
    const uint32_t slot = h & 0xFFFFu;
    if (slot == 0 || slot > kSlots) return false;

    const impl::Slot& s = impl::g_Slots[slot - 1];
    if (!s.used || !s.ptr) return false;
    if (s.generation != static_cast<uint16_t>(h >> 16)) return false;

    out = Actor(s.ptr, static_cast<Actor::Kind>(s.kind));
    return true;
}

// How many slots have ever been handed out. Only for the state report - a mod
// has no use for it and it is deliberately not on any surface.
inline uint32_t Issued() { return impl::g_Next; }

} // namespace WiiXLaunch::BotW::Surfaces::ActorHandles
