#pragma once

// botw.armour v1 - what Link is wearing and what it does for him.
//
// Split out from botw.pouch rather than folded into it, which is a change from
// how these six were originally sketched. The reason is what a mod would have
// to declare: armour effects and the inventory are asked about by different
// mods for different reasons, and a mod that only wants to know whether the
// Climbing Set is on should not be refused on a host where the pouch walk is
// unavailable. One surface per thing a mod can independently need.
//
// EFFECTS ARE (kind, level) PAIRS, not a struct. Armour::Effects holds a table
// and PieceEffect is three fields; neither may cross, so a mod asks for the
// level of one effect at a time. That is also the shape callers actually want -
// "how much climb speed do I have" rather than "give me everything and let me
// find it".
//
// The extra-effect table is this module's own addition on top of the game's:
// SetExtraEffect stacks a bonus the game has no idea about, and the game's own
// query hook adds it in. Kept separate from SetPieceEffect for exactly that
// reason - one edits the armour, the other edits the answer.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/botw/game/armour.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::ArmourSurface {

constexpr const char* kName = "botw.armour";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

inline uint32_t CopyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src) return 0;
    uint32_t n = 0;
    while (src[n] && n + 1 < cap) { out[n] = src[n]; ++n; }
    out[n] = '\0';
    return n;
}

extern "C" inline uint32_t ASupportsArmour() {
    return Armour::SupportsArmourEffects ? 1u : 0u;
}

// Head, chest, legs. Exported rather than assumed, because "3" is a game fact
// and a mod looping over it should get it from the host.
extern "C" inline int32_t APieceCount() {
    return static_cast<int32_t>(Armour::impl::kPieceCount);
}

extern "C" inline int32_t AEffectSlots() {
    return static_cast<int32_t>(Armour::kEffectSlots);
}

// The cached read of what is worn. Refresh re-reads the actors; without it a
// mod that just changed armour sees the previous set, which reads as a write
// having failed.
extern "C" inline uint32_t ARefresh() {
    return Armour::Refresh() ? 1u : 0u;
}

extern "C" inline uint32_t AIsPieceWorn(int32_t piece) {
    return Armour::IsPieceWorn(static_cast<Armour::Piece>(piece)) ? 1u : 0u;
}

// --- effects ---------------------------------------------------------------

extern "C" inline uint32_t AEffectName(int32_t effect, char* out, uint32_t cap) {
    return CopyOut(Armour::EffectName(static_cast<Armour::Effect>(effect)), out, cap);
}

extern "C" inline int32_t AEffectFromName(const char* name) {
    if (!name) return -1;
    return static_cast<int32_t>(Armour::EffectFromName(name));
}

// One piece's effect: which kind, and at what level. Two out-pointers rather
// than a packed return, so "no effect" is a status of 0 and never a level that
// happens to be zero.
extern "C" inline uint32_t AGetPieceEffect(int32_t piece, int32_t* effect, int32_t* level) {
    if (!effect || !level) return 0;
    Armour::PieceEffect pe{};
    if (!Armour::GetPieceEffect(static_cast<Armour::Piece>(piece), pe)) return 0;
    *effect = static_cast<int32_t>(pe.effect);
    *level = static_cast<int32_t>(pe.level);
    return 1;
}

extern "C" inline uint32_t ASetPieceEffect(int32_t piece, int32_t effect, int32_t level) {
    return Armour::SetPieceEffect(static_cast<Armour::Piece>(piece),
                                  static_cast<Armour::Effect>(effect),
                                  static_cast<int>(level)) ? 1u : 0u;
}

extern "C" inline uint32_t ASetPieceDefence(int32_t piece, int32_t defence) {
    return Armour::SetPieceDefence(static_cast<Armour::Piece>(piece),
                                   static_cast<int>(defence)) ? 1u : 0u;
}

extern "C" inline uint32_t ASetPieceFlags(int32_t piece, uint32_t ancientPowUp,
                                          uint32_t climbWaterfall, uint32_t climbJumpless) {
    return Armour::SetPieceFlags(static_cast<Armour::Piece>(piece),
                                 ancientPowUp != 0, climbWaterfall != 0,
                                 climbJumpless != 0) ? 1u : 0u;
}

// The whole set's level for one effect, which is what the game itself asks.
extern "C" inline int32_t AGetArmourEffect(int32_t effect) {
    return static_cast<int32_t>(Armour::GetArmourEffect(static_cast<Armour::Effect>(effect)));
}

extern "C" inline uint32_t ASetArmourEffects(int32_t effect, int32_t level) {
    return Armour::SetArmourEffects(static_cast<Armour::Effect>(effect),
                                    static_cast<int>(level)) ? 1u : 0u;
}

// --- the extra table -------------------------------------------------------
//
// A bonus stacked on top of whatever the armour really gives, applied through
// the game's own query. Its own calls, because "make this armour better" and
// "lie to the game about this armour" are different acts and a mod should have
// had to choose.

extern "C" inline uint32_t ASetExtraEffect(int32_t piece, int32_t effect, int32_t level) {
    return Armour::SetExtraEffect(static_cast<Armour::Piece>(piece),
                                  static_cast<Armour::Effect>(effect),
                                  static_cast<int>(level)) ? 1u : 0u;
}

extern "C" inline int32_t AGetExtraEffect(int32_t piece, int32_t effect) {
    return static_cast<int32_t>(Armour::GetExtraEffect(static_cast<Armour::Piece>(piece),
                                                       static_cast<Armour::Effect>(effect)));
}

extern "C" inline void AClearExtraEffects() {
    Armour::ClearExtraEffects();
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsArmour",   &ASupportsArmour),
    WIIXL_SURFACE_SYMBOL("PieceCount",       &APieceCount),
    WIIXL_SURFACE_SYMBOL("EffectSlots",      &AEffectSlots),
    WIIXL_SURFACE_SYMBOL("Refresh",          &ARefresh),
    WIIXL_SURFACE_SYMBOL("IsPieceWorn",      &AIsPieceWorn),

    WIIXL_SURFACE_SYMBOL("EffectName",       &AEffectName),
    WIIXL_SURFACE_SYMBOL("EffectFromName",   &AEffectFromName),
    WIIXL_SURFACE_SYMBOL("GetPieceEffect",   &AGetPieceEffect),
    WIIXL_SURFACE_SYMBOL("SetPieceEffect",   &ASetPieceEffect),
    WIIXL_SURFACE_SYMBOL("SetPieceDefence",  &ASetPieceDefence),
    WIIXL_SURFACE_SYMBOL("SetPieceFlags",    &ASetPieceFlags),
    WIIXL_SURFACE_SYMBOL("GetArmourEffect",  &AGetArmourEffect),
    WIIXL_SURFACE_SYMBOL("SetArmourEffects", &ASetArmourEffects),

    WIIXL_SURFACE_SYMBOL("SetExtraEffect",   &ASetExtraEffect),
    WIIXL_SURFACE_SYMBOL("GetExtraEffect",   &AGetExtraEffect),
    WIIXL_SURFACE_SYMBOL("ClearExtraEffects", &AClearExtraEffects),
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

} // namespace WiiXLaunch::BotW::Surfaces::ArmourSurface
