#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include "player.hpp"

// WiiXLaunch::BotW::Climate - the ambient temperature (ksys::wm world manager).
//
// NAMING, because this framework now has two things called climate:
// Weather::GetClimate returns WHICH of the twenty climates the player is in -
// the region's personality, HebraFrostClimate and so on. This header is the
// TEMPERATURE that climate produces. Get/SetTemperature are provided as the
// clearer names and Get/SetClimate as aliases.
//
// How the number is arrived at, from 0x03672a88 and 0x03672c40:
//
//   Each of the twenty climates carries two temperature curves, day and night,
//   as eleven altitude bands 100 units apart from 0 to 1000. The manager picks
//   the band, interpolates within it, and does that twice - once against the
//   day curve, once against the night one. The clock then blends the two
//   (0x03661b44): full night 21:00-04:00, ramping to full day across
//   04:00-09:00, full day 09:00-16:00, ramping back across 16:00-21:00.
//
//   So temperature is a function of climate, altitude and time, computed on
//   demand. There is no stored "current temperature" to read - which is why
//   GetTemperature recomputes it the same way rather than finding a field.
//
// On top sit FOUR override slots on the world manager, and the game itself
// only ever writes them from one AI action (the "Weather" demo action, whose
// parameter list at 0x025385b8 is what names them):
//
//   +0x5d8  TemperatureDay          ADDED to the day figure
//   +0x5dc  TemperatureNight        ADDED to the night figure
//   +0x5e0  TemperatureDirectDay    REPLACES the day figure outright
//   +0x5e4  TemperatureDirectNight  REPLACES the night figure outright
//
// A slot is "unset" when it holds 99999.9; the getters test `< 99999.0`.
//
// AND THEY EXPIRE. 0x03677ef0 runs a countdown per slot and, at zero, puts the
// sentinel back - exactly the mechanism that makes an unlocked weather override
// last four frames. Unlike weather there is no lock to opt out of it, so an
// override that is meant to persist has to be re-applied. That is what Tick()
// is for, and why the setters here arm a held value rather than writing once.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Climate {

static constexpr bool SupportsClimate = !WIIXL_SWITCH;

// The temperature the game falls back to when there is no climate to read -
// 23 degrees, from the constant at 0x103033c0.
constexpr float kDefaultTemperature = 23.0f;

// The altitude curve's shape: eleven bands, 100 units apart, 0 to 1000.
constexpr float kAltitudeBandSize = 100.0f;
constexpr float kAltitudeBandTop = 1000.0f;
constexpr int kAltitudeBandCount = 11;

namespace impl {

// The world manager - the same singleton the clock and the weather hang off.
// Duplicated rather than cross-included, matching how this framework already
// repeats shared addresses.
constexpr uintptr_t kWorldMgrPtrWiiU = 0x1047be88;

// --- the computation ------------------------------------------------------
//
// double(double altitude, Manager*). Both walk the eleven altitude bands of the
// current climate's curve, interpolate, then apply this side's two override
// slots - Direct replacing the result, plain being added to it. The day curve
// lives at climateEntry+0x80 and the night one at +0x130, eleven records of 16
// bytes each with the temperature at +0xc; 0x80 + 11*0x10 = 0x130, so the two
// are contiguous and confirm each other.
//
// The altitude argument is the world-space Y. Negative is clamped to zero.
constexpr uintptr_t kGetDayTemperatureWiiU = 0x03672a88;
constexpr uintptr_t kGetNightTemperatureWiiU = 0x03672c40;

// double(void). The day/night blend, 0.0 fully day and 1.0 fully night. Takes
// no argument - it reaches the clock through the world manager itself.
constexpr uintptr_t kGetNightBlendWiiU = 0x03661b44;

using TemperatureFn = double (*)(double altitude, void* worldMgr);
using NightBlendFn = double (*)();

// --- the override slots ---------------------------------------------------
constexpr uintptr_t kOffsetDay = 0x5d8;
constexpr uintptr_t kOffsetNight = 0x5dc;
constexpr uintptr_t kDirectDay = 0x5e0;
constexpr uintptr_t kDirectNight = 0x5e4;

// Their countdowns, decremented by 0x03677ef0. The Day/Night offset pair share
// one; the two Direct slots have their own.
constexpr uintptr_t kOffsetCountdown = 0x618;
constexpr uintptr_t kDirectDayCountdown = 0x61c;
constexpr uintptr_t kDirectNightCountdown = 0x620;

// What 0x03677ef0 writes back when a countdown expires (99999.9, from
// 0x10303388), and what the getters compare against to decide a slot is set
// (99999.0, from 0x103033c8). The two differ deliberately: 99999.9 is not less
// than 99999.0, so a reset slot reads as unset.
constexpr float kUnsetSentinel = 99999.9f;
constexpr float kSetThreshold = 99999.0f;

// How many frames the game's own setter grants an override. Matched here so a
// held value is re-armed well before it lapses.
constexpr int kOverrideFrames = 4;

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

// --- held override state --------------------------------------------------
//
// 0 none, 1 absolute (the Direct slots), 2 an offset (the plain slots).
inline int& HeldMode() { static int v = 0; return v; }
inline float& HeldValue() { static float v = 0.0f; return v; }

// Writes the held value into the manager and re-arms the countdowns. Shared by
// the setters and by Tick.
inline bool Apply() {
#if !WIIXL_SWITCH
    void* world = WorldMgr();
    if (!world || HeldMode() == 0) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(world);
    const float value = HeldValue();

    if (HeldMode() == 1) {
        *reinterpret_cast<float*>(base + kDirectDay) = value;
        *reinterpret_cast<float*>(base + kDirectNight) = value;
        *reinterpret_cast<int32_t*>(base + kDirectDayCountdown) = kOverrideFrames;
        *reinterpret_cast<int32_t*>(base + kDirectNightCountdown) = kOverrideFrames;
    } else {
        *reinterpret_cast<float*>(base + kOffsetDay) = value;
        *reinterpret_cast<float*>(base + kOffsetNight) = value;
        *reinterpret_cast<int32_t*>(base + kOffsetCountdown) = kOverrideFrames;
    }
    return true;
#else
    return false;
#endif
}

// The player's world-space Y, for the altitude the temperature is sampled at.
inline bool PlayerAltitude(float& out) {
#if !WIIXL_SWITCH
    void* player = Player::GetRaw();
    if (!player || !PlausiblePointer(reinterpret_cast<uintptr_t>(player))) return false;
    out = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(player) +
                                    BotW::impl::kPosYOffset);
    return true;
#else
    (void)out;
    return false;
#endif
}

} // namespace impl

// Whether the world manager is up.
inline bool IsAvailable() {
#if !WIIXL_SWITCH
    return impl::WorldMgr() != nullptr;
#else
    return false;
#endif
}

// The day curve's temperature at an altitude, overrides included.
inline bool GetDayTemperature(float altitude, float& out) {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::TemperatureFn>(
        0x0, impl::kGetDayTemperatureWiiU);
    if (!get) return false;

    out = static_cast<float>(get(static_cast<double>(altitude), world));
    return true;
#else
    (void)altitude; (void)out;
    return false;
#endif
}

// The night curve's temperature at an altitude, overrides included.
inline bool GetNightTemperature(float altitude, float& out) {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::TemperatureFn>(
        0x0, impl::kGetNightTemperatureWiiU);
    if (!get) return false;

    out = static_cast<float>(get(static_cast<double>(altitude), world));
    return true;
#else
    (void)altitude; (void)out;
    return false;
#endif
}

// How far through the night the clock is: 0.0 fully day, 1.0 fully night, and
// a ramp across dawn and dusk. The game's own function, so the crossover times
// are its own.
inline bool GetNightBlend(float& out) {
#if !WIIXL_SWITCH
    if (!impl::WorldMgr()) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::NightBlendFn>(
        0x0, impl::kGetNightBlendWiiU);
    if (!get) return false;

    out = static_cast<float>(get());
    return true;
#else
    (void)out;
    return false;
#endif
}

// The ambient temperature at an altitude, in the game's own degrees.
//
// Blends the two curves by the clock exactly as the environment sampler does
// (0x036711f4). The one thing that sampler adds and this does not is a per-area
// addend it takes from another sub-manager, so a reading taken right where an
// area applies one will differ slightly from what the thermometer shows.
inline bool GetTemperatureAt(float altitude, float& out) {
#if !WIIXL_SWITCH
    float day = 0.0f;
    float night = 0.0f;
    float blend = 0.0f;
    if (!GetDayTemperature(altitude, day)) return false;
    if (!GetNightTemperature(altitude, night)) return false;
    if (!GetNightBlend(blend)) return false;

    out = (night - day) * blend + day;
    return true;
#else
    (void)altitude; (void)out;
    return false;
#endif
}

// The ambient temperature where the player is standing.
inline bool GetTemperature(float& out) {
#if !WIIXL_SWITCH
    float altitude = 0.0f;
    if (!impl::PlayerAltitude(altitude)) return false;
    return GetTemperatureAt(altitude, out);
#else
    (void)out;
    return false;
#endif
}

// Whether an override is currently held by this header.
inline bool IsTemperatureOverridden() {
#if !WIIXL_SWITCH
    return impl::HeldMode() != 0;
#else
    return false;
#endif
}

// Whether the GAME currently has any temperature override slot set - which
// includes ones this header did not write. Useful for telling "my override
// lapsed" apart from "something else is driving it".
inline bool IsTemperatureForcedByGame() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(world);
    return *reinterpret_cast<float*>(base + impl::kDirectDay) < impl::kSetThreshold ||
           *reinterpret_cast<float*>(base + impl::kDirectNight) < impl::kSetThreshold ||
           *reinterpret_cast<float*>(base + impl::kOffsetDay) < impl::kSetThreshold ||
           *reinterpret_cast<float*>(base + impl::kOffsetNight) < impl::kSetThreshold;
#else
    return false;
#endif
}

// Pins the ambient temperature to an absolute value, day and night alike,
// through the game's Direct slots.
//
// MUST BE HELD. The game expires every override slot four frames after it is
// written, and unlike weather there is no lock to opt out of - so this arms a
// held value and Tick() re-applies it. Without a Tick call each frame the
// temperature snaps back almost immediately.
//
// Altitude still applies to everything else; this replaces the curve's output,
// so the reading is the same at every height until cleared.
inline bool SetTemperature(float celsius) {
#if !WIIXL_SWITCH
    if (!(celsius == celsius)) return false;                    // NaN
    if (celsius >= impl::kSetThreshold) return false;           // reads as unset
    if (!impl::WorldMgr()) return false;

    impl::HeldMode() = 1;
    impl::HeldValue() = celsius;
    return impl::Apply();
#else
    (void)celsius;
    return false;
#endif
}

// Shifts the ambient temperature by a number of degrees rather than replacing
// it, through the game's additive slots - so the climate, altitude and time of
// day all still show through. Held the same way SetTemperature is.
inline bool SetTemperatureOffset(float degrees) {
#if !WIIXL_SWITCH
    if (!(degrees == degrees)) return false;
    if (degrees >= impl::kSetThreshold) return false;
    if (!impl::WorldMgr()) return false;

    impl::HeldMode() = 2;
    impl::HeldValue() = degrees;
    return impl::Apply();
#else
    (void)degrees;
    return false;
#endif
}

// Drops the override and puts all four slots back to the game's own sentinel,
// rather than waiting the four frames out.
inline bool ClearTemperature() {
#if !WIIXL_SWITCH
    impl::HeldMode() = 0;

    void* world = impl::WorldMgr();
    if (!world) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(world);
    *reinterpret_cast<float*>(base + impl::kDirectDay) = impl::kUnsetSentinel;
    *reinterpret_cast<float*>(base + impl::kDirectNight) = impl::kUnsetSentinel;
    *reinterpret_cast<float*>(base + impl::kOffsetDay) = impl::kUnsetSentinel;
    *reinterpret_cast<float*>(base + impl::kOffsetNight) = impl::kUnsetSentinel;
    *reinterpret_cast<int32_t*>(base + impl::kDirectDayCountdown) = 0;
    *reinterpret_cast<int32_t*>(base + impl::kDirectNightCountdown) = 0;
    *reinterpret_cast<int32_t*>(base + impl::kOffsetCountdown) = 0;
    return true;
#else
    return false;
#endif
}

// Re-applies a held override. Call once per frame from the game thread -
// Player::OnTick is the obvious home - or the override lapses after four
// frames. Cheap and harmless when nothing is held.
inline void Tick() {
#if !WIIXL_SWITCH
    if (impl::HeldMode() != 0) impl::Apply();
#endif
}

// --- aliases -------------------------------------------------------------
//
// The names asked for. Get/SetTemperature above say more precisely what these
// do, and Weather::GetClimate is a different thing entirely - the index of the
// region the player is in, not its temperature.
inline bool GetClimate(float& out) { return GetTemperature(out); }
inline bool SetClimate(float celsius) { return SetTemperature(celsius); }

} // namespace WiiXLaunch::BotW::Climate
