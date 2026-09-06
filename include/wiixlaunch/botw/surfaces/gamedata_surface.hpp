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
// 1.1 appends GetFlagDebug, 1.2 appends InitCompletion. Appending bumps the
// MINOR, so every mod built against v1.0 still resolves.
constexpr uint16_t kVersionMinor = 2;

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

// Hearts and wheels, the units a UI actually shows. The raw forms above stay
// because the game stores those; offering only one of each pair would mean
// every mod doing the conversion, and every mod getting the rounding subtly
// different.
extern "C" inline uint32_t GdGetMaxHearts(float* out) {
    if (!out) return 0;
    *out = GameData::GetMaxHearts();
    return 1;
}

extern "C" inline uint32_t GdSetMaxHearts(float hearts) {
    return GameData::SetMaxHearts(hearts) ? 1u : 0u;
}

extern "C" inline uint32_t GdGetStaminaWheels(float* out) {
    if (!out) return 0;
    *out = GameData::GetStaminaWheels();
    return 1;
}

extern "C" inline uint32_t GdSetStaminaWheels(float wheels) {
    return GameData::SetStaminaWheels(wheels) ? 1u : 0u;
}

extern "C" inline uint32_t GdGetMaxStaminaWheels(float* out) {
    if (!out) return 0;
    *out = GameData::GetMaxStaminaWheels();
    return 1;
}

extern "C" inline uint32_t GdSetMaxStaminaWheels(float wheels) {
    return GameData::SetMaxStaminaWheels(wheels) ? 1u : 0u;
}

// The actor's own maximum, which is not always the save's - a temporary buff
// moves one and not the other, and a mod showing a stamina bar needs the one
// the game is actually clamping against.
extern "C" inline uint32_t GdGetActorMaxStamina(float* out) {
    if (!out) return 0;
    *out = GameData::GetActorMaxStamina();
    return 1;
}

// Queues a delta for the next frame instead of applying it now. The game
// recomputes some counters after a write, so an immediate add can be undone
// within the same frame; this is the form that survives that.
extern "C" inline uint32_t GdQueueFlagS32Delta(const char* name, int32_t delta) {
    return name && GameData::QueueFlagS32Delta(name, static_cast<int>(delta)) ? 1u : 0u;
}

// How many entries a flag store holds, by store index rather than by type - for
// a mod walking every store without knowing how many kinds there are.
extern "C" inline uint32_t GdGetFlagStoreCount(int32_t storeIndex, uint32_t* offset,
                                               int32_t* count) {
    if (!offset || !count) return 0;
    uint32_t off = 0;
    int n = 0;
    if (!GameData::GetFlagStoreCount(static_cast<int>(storeIndex), off, n)) return 0;
    *offset = off;
    *count = static_cast<int32_t>(n);
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

// The completion breakdown, field by field rather than as the module's
// Breakdown struct. Twelve small calls would be worse; one call filling an
// int32[8] in a fixed order is the same shape TimeSurface uses for the calendar:
// collected, total, base, baseTotal, divineBeasts, divineBeastTotal, koroks,
// korokTotal.
extern "C" inline uint32_t GdGetCompletionBreakdown(int32_t* out8, float* outPercent) {
    if (!out8) return 0;
    Completion::Breakdown b{};
    if (!Completion::GetCompletion(b)) return 0;
    out8[0] = b.collected;        out8[1] = b.total;
    out8[2] = b.base;             out8[3] = b.baseTotal;
    out8[4] = b.divineBeasts;     out8[5] = b.divineBeastTotal;
    out8[6] = b.koroks;           out8[7] = b.korokTotal;
    if (outPercent) *outPercent = b.percent;
    return 1;
}

extern "C" inline uint32_t GdGetCompletionPercent(float* out) {
    if (!out) return 0;
    return Completion::GetCompletionPercent(*out) ? 1u : 0u;
}

// The lowest and highest the percentage can currently reach - what is actually
// attainable rather than what the counter says, which differs once DLC content
// is or is not installed.
extern "C" inline uint32_t GdGetReachableRange(float* lowest, float* highest) {
    if (!lowest || !highest) return 0;
    return Completion::GetReachableRange(*lowest, *highest) ? 1u : 0u;
}

extern "C" inline uint32_t GdSetDisplayedPercent(float percent, uint32_t forceVisible) {
    return Completion::SetDisplayedPercent(percent, forceVisible != 0) ? 1u : 0u;
}

// Arms the completion display override. SetDisplayedPercent has nowhere to land
// until this hook is in place; with no override set it runs the original and
// tests one bool, so arming it costs nothing until something uses it.
extern "C" inline uint32_t GdInitCompletion() {
#if WIIXL_SWITCH
    return 0;
#else
    return Completion::Init() ? 1u : 0u;
#endif
}

// --- the manager's own state ------------------------------------------------
//
// FlagDebug is a struct of eight words and two four-word arrays, so it cannot
// cross as itself. Flattened into out-pointers, with the arrays as pointers to
// four elements the caller owns.
//
// This is for diagnosing a REFUSED WRITE: status bit 0x12 blocks writes
// outright, and the gates decide whether a typed call reaches the store at all.
// Without it, "the write was refused" is the whole answer a mod can give.
extern "C" inline uint32_t GdGetFlagDebug(uint32_t* manager, uint32_t* status,
                                          uint32_t* flagByte, uint32_t* mirrorGate,
                                          uint32_t* writeGate,
                                          uint32_t* slotsOut4, uint32_t* coresOut4) {
#if WIIXL_SWITCH
    (void)manager; (void)status; (void)flagByte; (void)mirrorGate; (void)writeGate;
    (void)slotsOut4; (void)coresOut4;
    return 0;
#else
    GameData::FlagDebug info{};
    if (!GameData::GetFlagDebug(info)) return 0;
    if (manager) *manager = static_cast<uint32_t>(info.manager);
    if (status) *status = info.status;
    if (flagByte) *flagByte = info.flagByte;
    if (mirrorGate) *mirrorGate = info.mirrorGate;
    if (writeGate) *writeGate = info.writeGate;
    for (int i = 0; i < 4; ++i) {
        if (slotsOut4) slotsOut4[i] = static_cast<uint32_t>(info.slot[i]);
        if (coresOut4) coresOut4[i] = static_cast<uint32_t>(info.core[i]);
    }
    return 1;
#endif
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
    // v1.1. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("GetFlagDebug",       &GdGetFlagDebug),
    // v1.2.
    WIIXL_SURFACE_SYMBOL("InitCompletion",     &GdInitCompletion),

    WIIXL_SURFACE_SYMBOL("GetStamina",         &GdGetStamina),
    WIIXL_SURFACE_SYMBOL("GetMaxStamina",      &GdGetMaxStamina),
    WIIXL_SURFACE_SYMBOL("SetStamina",         &GdSetStamina),
    WIIXL_SURFACE_SYMBOL("SetMaxStamina",      &GdSetMaxStamina),
    WIIXL_SURFACE_SYMBOL("RecoverStamina",     &GdRecoverStamina),
    WIIXL_SURFACE_SYMBOL("StaminaPerWheel",    &GdStaminaPerWheel),

    WIIXL_SURFACE_SYMBOL("GetMaxHearts",       &GdGetMaxHearts),
    WIIXL_SURFACE_SYMBOL("SetMaxHearts",       &GdSetMaxHearts),
    WIIXL_SURFACE_SYMBOL("GetStaminaWheels",   &GdGetStaminaWheels),
    WIIXL_SURFACE_SYMBOL("SetStaminaWheels",   &GdSetStaminaWheels),
    WIIXL_SURFACE_SYMBOL("GetMaxStaminaWheels", &GdGetMaxStaminaWheels),
    WIIXL_SURFACE_SYMBOL("SetMaxStaminaWheels", &GdSetMaxStaminaWheels),
    WIIXL_SURFACE_SYMBOL("GetActorMaxStamina", &GdGetActorMaxStamina),
    WIIXL_SURFACE_SYMBOL("QueueFlagS32Delta",  &GdQueueFlagS32Delta),
    WIIXL_SURFACE_SYMBOL("GetFlagStoreCount",  &GdGetFlagStoreCount),

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

    WIIXL_SURFACE_SYMBOL("GetCompletionBreakdown", &GdGetCompletionBreakdown),
    WIIXL_SURFACE_SYMBOL("GetCompletionPercent",   &GdGetCompletionPercent),
    WIIXL_SURFACE_SYMBOL("GetReachableRange",      &GdGetReachableRange),
    WIIXL_SURFACE_SYMBOL("SetDisplayedPercent",    &GdSetDisplayedPercent),
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
