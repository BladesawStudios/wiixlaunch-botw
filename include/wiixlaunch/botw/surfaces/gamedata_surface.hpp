#pragma once

// botw.gamedata v1 - the save file, as a mod can see it.
//
// Rupees, hearts, stamina, completion, and the flag store the game keeps
// everything else in. This is the widest single unlock of the six surfaces: it
// is what four of the API server's routes were waiting on.
//
// ---------------------------------------------------------------------------
// EVERY OFFSET STAYS ON THIS SIDE. That is the whole point of a surface rather
// than an escape hatch. GetFlagS32 walks a container at an offset pinned in
// gamedata.hpp; when that offset turns out to be wrong for some version it is
// corrected there, and every compiled mod that ever shipped gets the fix
// without being rebuilt. A mod that had read the container itself would keep
// reading the wrong place forever, and nothing on this side could reach it.
//
// ---------------------------------------------------------------------------
// FLOATS CROSS AS OUT-POINTERS, NOT RETURNS. A float return is fine in
// principle - both sides are the same ABI - but every other data-returning
// entry in this project already uses `uint32_t status, T* out`, and one call
// shaped differently is one call whose failure mode is different. So a getter
// that can fail returns 1/0 and writes through a pointer, and "it failed" is
// never confused with "the value happens to be zero".
//
// ---------------------------------------------------------------------------
// STRINGS ARE COPIED INTO THE CALLER'S BUFFER. A `const char*` into the game's
// own memory would be valid until the next call and nothing in the signature
// would say so. Same rule as ActorGetName.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/botw/game/gamedata.hpp>
#include <wiixlaunch/botw/game/completion.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::GameDataSurface {

constexpr const char* kName = "botw.gamedata";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// Copies into a caller-owned buffer and returns the length written, excluding
// the terminator. 0 means there was nothing to give.
inline uint32_t CopyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src) return 0;
    uint32_t n = 0;
    while (src[n] && n + 1 < cap) { out[n] = src[n]; ++n; }
    out[n] = '\0';
    return n;
}

// --- capability flags ------------------------------------------------------
//
// Separate flags, not one "supported", because they are separately confirmed:
// the stamina getters and the flag store were RE'd independently and could
// perfectly well differ on some future platform. A mod that needs one should
// not be told no because another is missing.
extern "C" inline uint32_t GdSupportsRupees()  { return GameData::SupportsRupees ? 1u : 0u; }
extern "C" inline uint32_t GdSupportsFlags()   { return GameData::SupportsFlags ? 1u : 0u; }
extern "C" inline uint32_t GdSupportsStamina() { return GameData::SupportsStamina ? 1u : 0u; }
extern "C" inline uint32_t GdSupportsMaxLife() { return GameData::SupportsMaxLife ? 1u : 0u; }
extern "C" inline uint32_t GdSupportsCompletion() { return Completion::SupportsCompletion ? 1u : 0u; }

// --- rupees ----------------------------------------------------------------

extern "C" inline uint32_t GdGetRupees(int32_t* out) {
    if (!out) return 0;
    int value = 0;
    if (!GameData::GetRupees(value)) return 0;
    *out = static_cast<int32_t>(value);
    return 1;
}

extern "C" inline uint32_t GdSetRupees(int32_t value) {
    return GameData::SetRupees(static_cast<int>(value)) ? 1u : 0u;
}

extern "C" inline uint32_t GdAddRupees(int32_t delta) {
    return GameData::AddRupees(static_cast<int>(delta)) ? 1u : 0u;
}

// --- hearts ----------------------------------------------------------------
//
// Raw units, quarter-hearts, 4 per heart - the same unit botw.player's life
// accessors use, so a mod never has to know which of two conventions a given
// call speaks.

extern "C" inline int32_t GdGetMaxLife() {
    return static_cast<int32_t>(GameData::GetMaxLife());
}

extern "C" inline uint32_t GdSetMaxLife(int32_t rawUnits) {
    return GameData::SetMaxLife(static_cast<int>(rawUnits)) ? 1u : 0u;
}

// --- stamina ---------------------------------------------------------------
//
// Wheel units. kStaminaPerWheel is exported too, because a mod that wants the
// raw figure should not have to hard-code the conversion - that constant is
// exactly the kind of thing that belongs on this side of the boundary.

extern "C" inline uint32_t GdGetStamina(float* out) {
    if (!out) return 0;
    *out = GameData::GetStamina();
    return 1;
}

extern "C" inline uint32_t GdGetMaxStamina(float* out) {
    if (!out) return 0;
    *out = GameData::GetMaxStamina();
    return 1;
}

extern "C" inline uint32_t GdSetStamina(float units) {
    return GameData::SetStamina(units) ? 1u : 0u;
}

extern "C" inline uint32_t GdSetMaxStamina(float units) {
    return GameData::SetMaxStamina(units) ? 1u : 0u;
}

extern "C" inline uint32_t GdRecoverStamina() {
    return GameData::RecoverStamina() ? 1u : 0u;
}

extern "C" inline uint32_t GdStaminaPerWheel(float* out) {
    if (!out) return 0;
    *out = GameData::kStaminaPerWheel;
    return 1;
}

// --- flags, by name --------------------------------------------------------
//
// Four types, each with its own pair, rather than one call taking a type tag.
// A tag would make "you asked for the wrong type" a runtime value a mod has to
// check; separate calls make it a compile-time fact at the call site.

extern "C" inline uint32_t GdGetFlagS32(const char* name, int32_t* out) {
    if (!name || !out) return 0;
    int value = 0;
    if (!GameData::GetFlagS32(name, value)) return 0;
    *out = static_cast<int32_t>(value);
    return 1;
}

extern "C" inline uint32_t GdSetFlagS32(const char* name, int32_t value) {
    return name && GameData::SetFlagS32(name, static_cast<int>(value)) ? 1u : 0u;
}

extern "C" inline uint32_t GdAddFlagS32(const char* name, int32_t delta) {
    return name && GameData::AddFlagS32(name, static_cast<int>(delta)) ? 1u : 0u;
}

extern "C" inline uint32_t GdGetFlagBool(const char* name, uint32_t* out) {
    if (!name || !out) return 0;
    bool value = false;
    if (!GameData::GetFlagBool(name, value)) return 0;
    *out = value ? 1u : 0u;
    return 1;
}

extern "C" inline uint32_t GdSetFlagBool(const char* name, uint32_t value) {
    return name && GameData::SetFlagBool(name, value != 0) ? 1u : 0u;
}

// The permission bypass, as its own symbol rather than a parameter on the one
// above. Forcing a flag the game would have refused is a different act with
// different consequences, and it should read differently at the call site.
extern "C" inline uint32_t GdSetFlagBoolForced(const char* name, uint32_t value,
                                               uint32_t bypassPermission) {
    return name && GameData::SetFlagBoolForced(name, value != 0, bypassPermission != 0)
               ? 1u : 0u;
}

extern "C" inline uint32_t GdGetFlagF32(const char* name, float* out) {
    if (!name || !out) return 0;
    return GameData::GetFlagF32(name, *out) ? 1u : 0u;
}

extern "C" inline uint32_t GdSetFlagF32(const char* name, float value) {
    return name && GameData::SetFlagF32(name, value) ? 1u : 0u;
}

// Vec3 through a float[3], never as a struct - see the ABI rules in
// loader/surface.hpp for why a struct must not cross.
extern "C" inline uint32_t GdGetFlagVec3(const char* name, float* out3) {
    if (!name || !out3) return 0;
    GameData::Vec3 v{};
    if (!GameData::GetFlagVec3(name, v)) return 0;
    out3[0] = v.x; out3[1] = v.y; out3[2] = v.z;
    return 1;
}

extern "C" inline uint32_t GdSetFlagVec3(const char* name, const float* in3) {
    if (!name || !in3) return 0;
    GameData::Vec3 v{};
    v.x = in3[0]; v.y = in3[1]; v.z = in3[2];
    return GameData::SetFlagVec3(name, v) ? 1u : 0u;
}

// --- flags, by index -------------------------------------------------------
//
// What /api/flags needs: the store holds tens of thousands of entries and a mod
// cannot know their names. Enumeration hands back the HASH rather than a name
// because the game does not store names either - it stores hashes, and
// pretending otherwise would mean inventing a reverse table nobody has.

extern "C" inline int32_t GdFlagS32Count()  { return static_cast<int32_t>(GameData::FlagS32Count()); }
extern "C" inline int32_t GdFlagBoolCount() { return static_cast<int32_t>(GameData::FlagBoolCount()); }
extern "C" inline int32_t GdFlagF32Count()  { return static_cast<int32_t>(GameData::FlagF32Count()); }
extern "C" inline int32_t GdFlagVec3Count() { return static_cast<int32_t>(GameData::FlagVec3Count()); }

extern "C" inline uint32_t GdGetFlagS32ByIndex(int32_t index, uint32_t* hash, int32_t* value) {
    if (!hash || !value) return 0;
    uint32_t h = 0; int v = 0;
    if (!GameData::GetFlagS32ByIndex(static_cast<int>(index), h, v)) return 0;
    *hash = h; *value = static_cast<int32_t>(v);
    return 1;
}

extern "C" inline uint32_t GdGetFlagBoolByIndex(int32_t index, uint32_t* hash, uint32_t* value) {
    if (!hash || !value) return 0;
    uint32_t h = 0; bool v = false;
    if (!GameData::GetFlagBoolByIndex(static_cast<int>(index), h, v)) return 0;
    *hash = h; *value = v ? 1u : 0u;
    return 1;
}

extern "C" inline uint32_t GdGetFlagF32ByIndex(int32_t index, uint32_t* hash, float* value) {
    if (!hash || !value) return 0;
    uint32_t h = 0;
    if (!GameData::GetFlagF32ByIndex(static_cast<int>(index), h, *value)) return 0;
    *hash = h;
    return 1;
}

extern "C" inline uint32_t GdGetFlagVec3ByIndex(int32_t index, uint32_t* hash, float* out3) {
    if (!hash || !out3) return 0;
    uint32_t h = 0;
    GameData::Vec3 v{};
    if (!GameData::GetFlagVec3ByIndex(static_cast<int>(index), h, v)) return 0;
    *hash = h;
    out3[0] = v.x; out3[1] = v.y; out3[2] = v.z;
    return 1;
}

// --- completion ------------------------------------------------------------

extern "C" inline uint32_t GdGetCompletionParts(int32_t* whole, int32_t* hundredths) {
    if (!whole || !hundredths) return 0;
    int w = 0, h = 0;
    if (!Completion::GetCompletionParts(w, h)) return 0;
    *whole = static_cast<int32_t>(w);
    *hundredths = static_cast<int32_t>(h);
    return 1;
}

extern "C" inline uint32_t GdGetKorokCount(int32_t* out) {
    if (!out) return 0;
    int v = 0;
    if (!Completion::GetKorokCount(v)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

extern "C" inline uint32_t GdGetKorokTotal(int32_t* out) {
    if (!out) return 0;
    int v = 0;
    if (!Completion::GetKorokTotal(v)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

extern "C" inline uint32_t GdSetKorokCount(int32_t count) {
    return Completion::SetKorokCount(static_cast<int>(count)) ? 1u : 0u;
}

extern "C" inline uint32_t GdIsGameClear() {
    return Completion::IsGameClear() ? 1u : 0u;
}

// The percentage the HUD shows, overridden. Separate from the real figure on
// purpose: this changes what is displayed and nothing else, and a mod should
// not be able to confuse "the save is 40% done" with "the corner of the screen
// says 40%".
extern "C" inline uint32_t GdSetDisplayedParts(int32_t whole, int32_t hundredths,
                                               uint32_t forceVisible) {
    return Completion::SetDisplayedParts(static_cast<int>(whole),
                                         static_cast<int>(hundredths),
                                         forceVisible != 0) ? 1u : 0u;
}

extern "C" inline void GdClearDisplayOverride() {
    Completion::ClearDisplayOverride();
}

extern "C" inline uint32_t GdIsDisplayOverridden() {
    return Completion::IsDisplayOverridden() ? 1u : 0u;
}

// --- the table -------------------------------------------------------------
//
// APPEND ONLY. Adding an entry bumps the minor; changing or removing one bumps
// the major.
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsRupees",     &GdSupportsRupees),
    WIIXL_SURFACE_SYMBOL("SupportsFlags",      &GdSupportsFlags),
    WIIXL_SURFACE_SYMBOL("SupportsStamina",    &GdSupportsStamina),
    WIIXL_SURFACE_SYMBOL("SupportsMaxLife",    &GdSupportsMaxLife),
    WIIXL_SURFACE_SYMBOL("SupportsCompletion", &GdSupportsCompletion),

    WIIXL_SURFACE_SYMBOL("GetRupees",          &GdGetRupees),
    WIIXL_SURFACE_SYMBOL("SetRupees",          &GdSetRupees),
    WIIXL_SURFACE_SYMBOL("AddRupees",          &GdAddRupees),

    WIIXL_SURFACE_SYMBOL("GetMaxLife",         &GdGetMaxLife),
    WIIXL_SURFACE_SYMBOL("SetMaxLife",         &GdSetMaxLife),

    WIIXL_SURFACE_SYMBOL("GetStamina",         &GdGetStamina),
    WIIXL_SURFACE_SYMBOL("GetMaxStamina",      &GdGetMaxStamina),
    WIIXL_SURFACE_SYMBOL("SetStamina",         &GdSetStamina),
    WIIXL_SURFACE_SYMBOL("SetMaxStamina",      &GdSetMaxStamina),
    WIIXL_SURFACE_SYMBOL("RecoverStamina",     &GdRecoverStamina),
    WIIXL_SURFACE_SYMBOL("StaminaPerWheel",    &GdStaminaPerWheel),

    WIIXL_SURFACE_SYMBOL("GetFlagS32",         &GdGetFlagS32),
    WIIXL_SURFACE_SYMBOL("SetFlagS32",         &GdSetFlagS32),
    WIIXL_SURFACE_SYMBOL("AddFlagS32",         &GdAddFlagS32),
    WIIXL_SURFACE_SYMBOL("GetFlagBool",        &GdGetFlagBool),
    WIIXL_SURFACE_SYMBOL("SetFlagBool",        &GdSetFlagBool),
    WIIXL_SURFACE_SYMBOL("SetFlagBoolForced",  &GdSetFlagBoolForced),
    WIIXL_SURFACE_SYMBOL("GetFlagF32",         &GdGetFlagF32),
    WIIXL_SURFACE_SYMBOL("SetFlagF32",         &GdSetFlagF32),
    WIIXL_SURFACE_SYMBOL("GetFlagVec3",        &GdGetFlagVec3),
    WIIXL_SURFACE_SYMBOL("SetFlagVec3",        &GdSetFlagVec3),

    WIIXL_SURFACE_SYMBOL("FlagS32Count",       &GdFlagS32Count),
    WIIXL_SURFACE_SYMBOL("FlagBoolCount",      &GdFlagBoolCount),
    WIIXL_SURFACE_SYMBOL("FlagF32Count",       &GdFlagF32Count),
    WIIXL_SURFACE_SYMBOL("FlagVec3Count",      &GdFlagVec3Count),
    WIIXL_SURFACE_SYMBOL("GetFlagS32ByIndex",  &GdGetFlagS32ByIndex),
    WIIXL_SURFACE_SYMBOL("GetFlagBoolByIndex", &GdGetFlagBoolByIndex),
    WIIXL_SURFACE_SYMBOL("GetFlagF32ByIndex",  &GdGetFlagF32ByIndex),
    WIIXL_SURFACE_SYMBOL("GetFlagVec3ByIndex", &GdGetFlagVec3ByIndex),

    WIIXL_SURFACE_SYMBOL("GetCompletionParts", &GdGetCompletionParts),
    WIIXL_SURFACE_SYMBOL("GetKorokCount",      &GdGetKorokCount),
    WIIXL_SURFACE_SYMBOL("GetKorokTotal",      &GdGetKorokTotal),
    WIIXL_SURFACE_SYMBOL("SetKorokCount",      &GdSetKorokCount),
    WIIXL_SURFACE_SYMBOL("IsGameClear",        &GdIsGameClear),
    WIIXL_SURFACE_SYMBOL("SetDisplayedParts",  &GdSetDisplayedParts),
    WIIXL_SURFACE_SYMBOL("ClearDisplayOverride", &GdClearDisplayOverride),
    WIIXL_SURFACE_SYMBOL("IsDisplayOverridden",  &GdIsDisplayOverridden),
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

} // namespace WiiXLaunch::BotW::Surfaces::GameDataSurface
