#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>

// WiiXLaunch::BotW::Weather - the weather system (ksys::wm world manager).
//
// How weather is decided, which is why this header has both a "what is it" and
// a "what did someone force" half:
//
//   * Normally nobody sets the weather at all. The world manager knows which
//     of twenty climates the player is standing in (an index at +0x5f8) and
//     the climate sub-manager rolls the weather from that climate's own
//     probability tables, by time of day. That is the whole of ordinary play.
//
//   * On top of that sits a single OVERRIDE byte at +0x649. It is 0xff for
//     "nobody is forcing anything", and 0-8 to pin the weather to one type.
//     The resolver (0x036723a8) gives it absolute priority over the climate
//     roll. In the retail game only three script-driven callers ever set it,
//     so during normal play it stays 0xff.
//
//   * An override EXPIRES, and quickly. +0x60c is a frame countdown, not a
//     state: the world manager's update (0x03677ef0) decrements it every frame
//     and, once it reaches zero, puts +0x649 back to 0xff and clears +0x64d
//     and +0x64e. setWeather seeds it with 4, so an unlocked override lasts
//     four frames. That is long enough for the forecast UI to notice and far
//     too short for the sky to finish changing, which is exactly what it looks
//     like: the weather visibly tries to change and then gives up.
//
//     The lock is what prevents this. 0x03677ef0 skips the countdown entirely
//     while +0x64e is set, so a LOCKED override is the only one that holds.
//     (The skip is itself gated on 0x031cad5c, a cutscene/loading check, so
//     even a locked override expires during those.)
//
//   * Above even that is "magic weather" - the Wizzrobe storms - at +0x610,
//     -1 when idle. It beats both, and nothing here writes it.
//
// So GetWeather asks the game's own resolver what the weather actually IS,
// while SetWeather writes the override and ClearWeather puts it back to 0xff
// and hands control back to the climate.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Weather {

// The nine weather types, in the game's own order. Taken from the name table
// 0x0367a35c builds at 0x10558b70 and cross-checked against the enum string
// the game ships for its own debug menu, "Bluesky,Cloudy,Rain,HeavyRain,Snow,
// HeavySnow,ThunderStorm,ThunderRain,BlueskyRain".
enum class Type : int {
    Bluesky = 0,
    Cloudy = 1,
    Rain = 2,
    HeavyRain = 3,
    Snow = 4,
    HeavySnow = 5,
    ThunderStorm = 6,
    ThunderRain = 7,
    BlueskyRain = 8,
};

constexpr int kTypeCount = 9;

// What the override byte reads when nothing is forcing the weather.
constexpr int kNoOverride = -1;

static constexpr bool SupportsWeather = !WIIXL_SWITCH;

namespace impl {

// The world manager singleton - the same object the clock hangs off, so this
// is the constant botw/gametime.hpp already documents. Duplicated rather than
// cross-included, matching how the framework already repeats shared addresses
// (the sead crit-section pair lives in both actor.hpp and pouch.hpp).
constexpr uintptr_t kWorldMgrPtrWiiU = 0x1047be88;

// --- fields --------------------------------------------------------------
//
// s32, the climate the player is in - an index into the twenty-entry climate
// name table below. Fixed by 0x03678d78, which writes it from
// 0x036728c0(mgr, position) and keeps the value it replaced in +0x5fc; the
// same index is then used to subscript the climate array at +0x1e8, whose
// bound is +0x1e4.
constexpr uintptr_t kClimateIndex = 0x5f8;
constexpr uintptr_t kPrevClimateIndex = 0x5fc;

// u8, the weather override. 0xff = none; 0-8 pins the weather. Initialised to
// 0xff by 0x03673f6c (`li r9,0xff; stb r9,0x649(r30)`) and the only thing
// 0x03679688 writes that actually decides the weather.
constexpr uintptr_t kOverride = 0x649;

// u8, set from setWeather's last argument. While nonzero, a setWeather that
// does NOT pass that argument is refused outright - a lock, not a priority.
constexpr uintptr_t kLocked = 0x64e;

// u8 and s32 that setWeather also writes. +0x64d is its third argument; the
// sky code at 0x03655de8 snaps its haze value instantly when +0x64d is zero
// and eases into it otherwise, so 1 is the smooth transition.
//
// +0x60c is the override's lifetime IN FRAMES, which setWeather always seeds
// with 4. 0x03677ef0 counts it down and clears the override at zero unless the
// lock is set. Both are zero after the manager's init, so ClearWeather
// restores them to that rather than leaving half a request standing - a stale
// +0x60c would also make the game's own priority-respecting callers refuse to
// change the weather.
constexpr uintptr_t kRequestFlag = 0x64d;
constexpr uintptr_t kRequestState = 0x60c;

// s32, magic (Wizzrobe) weather. -1 when idle, and it outranks the override.
constexpr uintptr_t kMagicWeather = 0x610;

// char. The world simulation gate - the same byte the clock checks. With it
// clear the resolver skips the climate computation entirely and answers from
// the override alone.
constexpr uintptr_t kWorldSimGate = 0x648;

// --- functions -----------------------------------------------------------
//
// 0x03672890  getCurrentWeather(worldMgr) -> u32
//             Two calls: getCurrentClimate (0x036723a0, which returns +0x5f8)
//             then the resolver 0x036723a8. THE answer to "what is the weather
//             right now" - it folds in the climate roll, the override and the
//             magic weather in that order of precedence.
//
// 0x03679688  setWeather(worldMgr, u32 type, u8 flag, bool respectPriority,
//                        bool lock)
//             Rejects type > 8. Refuses when the lock byte is set and lock is
//             false. Refuses when a request is already standing
//             (+0x60c != 0), respectPriority is true, and the new type is
//             lower than the standing one - so the priority rule is "never
//             downgrade", and passing false skips it. Then writes +0x64d,
//             +0x60c = 4, +0x64e and +0x649.
//
// 0x03672318  getWeatherOverride(worldMgr) -> u8, the raw +0x649.
// 0x036722bc  getWeatherTypeName(u32) -> const char*, out of the table below.
// 0x036723a0  getCurrentClimate(worldMgr) -> s32, the raw +0x5f8.
constexpr uintptr_t kGetCurrentWeatherWiiU = 0x03672890;
constexpr uintptr_t kSetWeatherWiiU = 0x03679688;

using GetWeatherFn = uint32_t (*)(void* worldMgr);
using SetWeatherFn = void (*)(void* worldMgr, uint32_t type, uint8_t flag,
                              int respectPriority, int lock);

// --- name tables ----------------------------------------------------------
//
// Both are { const char* text; const void* vtable } pairs, 8 bytes each,
// written at runtime by 0x0367a35c - so they read null before the game has got
// that far, and every use here checks.
//
// The weather table's length is pinned by 0x036722bc clamping its argument at
// 8, and the climate table's by 0x036728c0 looping exactly 0x14 times.
constexpr uintptr_t kWeatherNameTableWiiU = 0x10558b70;
constexpr uintptr_t kClimateNameTableWiiU = 0x10558bb8;
constexpr int kClimateCount = 20;

inline bool PlausiblePointer(uintptr_t addr) {
    return addr >= 0x10000000 && addr < 0xa0000000;
}

inline void* WorldMgr() {
#if !WIIXL_SWITCH
    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kWorldMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return nullptr;
    return reinterpret_cast<void*>(mgr);
#else
    return nullptr;
#endif
}

inline const char* TableName(uintptr_t table, int count, int index) {
#if !WIIXL_SWITCH
    if (index < 0 || index >= count) return nullptr;
    const char* text = *reinterpret_cast<const char* const*>(
        table + 8 * static_cast<uintptr_t>(index));
    if (!text || !PlausiblePointer(reinterpret_cast<uintptr_t>(text))) return nullptr;
    return text;
#else
    (void)table; (void)count; (void)index;
    return nullptr;
#endif
}

} // namespace impl

// Whether the weather system is reachable. False on the title screen and
// during early boot.
inline bool IsAvailable() {
#if !WIIXL_SWITCH
    return impl::WorldMgr() != nullptr;
#else
    return false;
#endif
}

// The game's own name for a weather type, e.g. "HeavyRain". Returns "" rather
// than null when the table has not been built yet or the value is out of
// range, so it is always safe to print.
inline const char* WeatherName(Type type) {
#if !WIIXL_SWITCH
    const char* name = impl::TableName(impl::kWeatherNameTableWiiU, kTypeCount,
                                       static_cast<int>(type));
    return name ? name : "";
#else
    (void)type;
    return "";
#endif
}

// The weather type a name maps to. Returns false when it matches none of the
// nine - matching is against the game's own table, so it accepts exactly the
// strings the game accepts.
inline bool WeatherFromName(const char* name, Type& out) {
#if !WIIXL_SWITCH
    if (!name || !name[0]) return false;
    for (int i = 0; i < kTypeCount; ++i) {
        const char* entry = impl::TableName(impl::kWeatherNameTableWiiU, kTypeCount, i);
        if (!entry) continue;
        const char* a = entry;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == *b) { out = static_cast<Type>(i); return true; }
    }
    return false;
#else
    (void)name; (void)out;
    return false;
#endif
}

// The weather right now, through the game's own resolver - so it accounts for
// the climate roll, any override, and Wizzrobe magic weather, in the game's
// own order of precedence.
//
// Returns false when there is no world manager yet, or when the resolver hands
// back something outside 0-8 (it has a defensive path that can return the
// no-override sentinel), rather than reporting a nonsense type.
inline bool GetWeather(Type& out) {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::GetWeatherFn>(
        0x0, impl::kGetCurrentWeatherWiiU);
    if (!get) return false;

    const uint32_t value = get(world);
    if (value >= static_cast<uint32_t>(kTypeCount)) return false;

    out = static_cast<Type>(value);
    return true;
#else
    (void)out;
    return false;
#endif
}

// The forced weather, or kNoOverride (-1) when the climate is in charge.
// Distinct from GetWeather: this is what somebody asked for, not what the sky
// is doing.
inline int GetWeatherOverride() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return kNoOverride;

    const uint8_t value = *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(world) + impl::kOverride);
    if (value >= static_cast<uint8_t>(kTypeCount)) return kNoOverride;
    return static_cast<int>(value);
#else
    return kNoOverride;
#endif
}

inline bool IsWeatherOverridden() {
#if !WIIXL_SWITCH
    return GetWeatherOverride() != kNoOverride;
#else
    return false;
#endif
}

// Whether a lock is in effect. A locked override refuses every unlocked
// setWeather, the game's own script-driven ones included, until it is cleared.
inline bool IsWeatherLocked() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;
    return *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(world) + impl::kLocked) != 0;
#else
    return false;
#endif
}

// Forces the weather, through the game's own setter.
//
// A locked override holds until ClearWeather, which is what makes "keep it
// raining" a one-line mod, and equally what stops the game's natural weather
// ever coming back on its own. Clear it when you are done. An UNLOCKED one
// times out after four frames - see lockOut below.
//
// lockOut passes the setter's last argument, and it does two things. It makes
// the override REFUSE every later unlocked setWeather, the game's own scripted
// ones included, so a cutscene cannot take the weather off you - and, the part
// that matters more, it is what stops the four-frame countdown described at
// the top of this header from wiping the override almost immediately.
//
// So lockOut is not a refinement: without it a forced weather reverts within
// four frames, having got just far enough for the forecast to flicker. Pass it
// for anything you want to persist.
//
// The refusal works against the caller too, so a lockOut already standing
// makes an ordinary SetWeather fail; use ClearWeather first, or pass lockOut
// again.
//
// Deliberately skips the setter's priority gate (the "never downgrade to a
// milder type while a request is standing" rule), because a caller asking for
// Bluesky means Bluesky.
//
// Returns whether the override actually took, read back off the manager rather
// than assumed - the game's setter is silent about a refusal.
inline bool SetWeather(Type type, bool lockOut = false) {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;

    const int index = static_cast<int>(type);
    if (index < 0 || index >= kTypeCount) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetWeatherFn>(
        0x0, impl::kSetWeatherWiiU);
    if (!set) return false;

    set(world, static_cast<uint32_t>(index), 1, 0, lockOut ? 1 : 0);

    return *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(world) + impl::kOverride) ==
        static_cast<uint8_t>(index);
#else
    (void)type; (void)lockOut;
    return false;
#endif
}

// Hands the weather back to the climate system.
//
// Not a call to the game's setter - that one rejects anything above 8, so it
// cannot express "no override" at all. This writes the four fields directly,
// restoring exactly the values the world manager's own init leaves them at:
// override 0xff, lock 0, request flag 0, request state 0.
//
// Clearing the request state matters as much as the override byte. Leaving it
// at 4 with the override at 0xff would make every priority-respecting caller
// compare its type against 255 and refuse, which would quietly break the
// game's own weather scripting.
//
// The sky does not snap - the climate roll takes over from the next update.
inline bool ClearWeather() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(world);
    *reinterpret_cast<uint8_t*>(base + impl::kOverride) = 0xff;
    *reinterpret_cast<uint8_t*>(base + impl::kLocked) = 0;
    *reinterpret_cast<uint8_t*>(base + impl::kRequestFlag) = 0;
    *reinterpret_cast<int32_t*>(base + impl::kRequestState) = 0;
    return true;
#else
    return false;
#endif
}

// Wizzrobe magic weather, or -1 when idle. Read-only: it outranks the override,
// so a mod that ignores it will wonder why SetWeather appears to do nothing
// during a Wizzrobe fight.
inline int GetMagicWeather() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return -1;
    return *reinterpret_cast<int32_t*>(
        reinterpret_cast<uintptr_t>(world) + impl::kMagicWeather);
#else
    return -1;
#endif
}

// The climate the player is standing in, as an index into the twenty-entry
// table, or -1. This is the region's weather personality - Hebra is
// HebraFrostClimate, the desert GerudoDesertClimate - and it is what decides
// the weather whenever nothing is overriding it.
inline int GetClimate() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return -1;

    const int32_t value = *reinterpret_cast<int32_t*>(
        reinterpret_cast<uintptr_t>(world) + impl::kClimateIndex);
    if (value < 0 || value >= impl::kClimateCount) return -1;
    return value;
#else
    return -1;
#endif
}

// The game's own name for a climate index, e.g. "TabantaAridClimate", or "".
inline const char* ClimateName(int climate) {
#if !WIIXL_SWITCH
    const char* name = impl::TableName(impl::kClimateNameTableWiiU,
                                       impl::kClimateCount, climate);
    return name ? name : "";
#else
    (void)climate;
    return "";
#endif
}

// Convenience classifications. These are THIS header's reading of the nine
// types, not a flag the game exposes - the game asks "which type is it" and
// each system decides for itself what that means. Handy for a mod that only
// cares whether it should be worried about metal weapons.
inline bool IsRaining(Type type) {
    return type == Type::Rain || type == Type::HeavyRain ||
           type == Type::ThunderRain || type == Type::BlueskyRain;
}

inline bool IsSnowing(Type type) {
    return type == Type::Snow || type == Type::HeavySnow;
}

// The two types that carry lightning strikes.
inline bool IsThundering(Type type) {
    return type == Type::ThunderStorm || type == Type::ThunderRain;
}

inline bool IsRaining() {
#if !WIIXL_SWITCH
    Type type = Type::Bluesky;
    return GetWeather(type) && IsRaining(type);
#else
    return false;
#endif
}

inline bool IsSnowing() {
#if !WIIXL_SWITCH
    Type type = Type::Bluesky;
    return GetWeather(type) && IsSnowing(type);
#else
    return false;
#endif
}

inline bool IsThundering() {
#if !WIIXL_SWITCH
    Type type = Type::Bluesky;
    return GetWeather(type) && IsThundering(type);
#else
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::Weather
