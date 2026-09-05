#pragma once

// botw.world v1 - weather, time of day, and temperature.
//
// Three headers behind one surface because they are one thing to a caller: what
// the world is doing right now. A mod asking "is it raining" and a mod asking
// "what time is it" are the same mod often enough that splitting them would
// mean two required surfaces for one obvious question.
//
// ENUMS CROSS AS int32, NAMES AS COPIES. Weather::Type and Time::Division are
// module enums; their VALUES are part of this surface's contract from here on,
// and the name lookups exist so a mod never has to keep its own copy of a table
// that could quietly stop matching. Asking the host what a value is called is
// what keeps a mod's output honest the day a type is added.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/botw/game/weather.hpp>
#include <wiixlaunch/botw/game/gametime.hpp>
#include <wiixlaunch/botw/game/climate.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::WorldSurface {

constexpr const char* kName = "botw.world";
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

// --- capability ------------------------------------------------------------

extern "C" inline uint32_t WSupportsWeather() { return Weather::SupportsWeather ? 1u : 0u; }
extern "C" inline uint32_t WSupportsTime()    { return Time::SupportsGameTime ? 1u : 0u; }
extern "C" inline uint32_t WSupportsClimate() { return Climate::SupportsClimate ? 1u : 0u; }

// Compile-time support is not the same as "there is a world right now" - a
// title screen has neither weather nor a clock. Both questions are asked
// separately because they have different answers at different moments.
extern "C" inline uint32_t WWeatherAvailable() { return Weather::IsAvailable() ? 1u : 0u; }
extern "C" inline uint32_t WTimeAvailable()    { return Time::IsAvailable() ? 1u : 0u; }
extern "C" inline uint32_t WClimateAvailable() { return Climate::IsAvailable() ? 1u : 0u; }

// --- weather ---------------------------------------------------------------

extern "C" inline uint32_t WGetWeather(int32_t* out) {
    if (!out) return 0;
    Weather::Type t{};
    if (!Weather::GetWeather(t)) return 0;
    *out = static_cast<int32_t>(t);
    return 1;
}

extern "C" inline uint32_t WSetWeather(int32_t type, uint32_t lockOut) {
    return Weather::SetWeather(static_cast<Weather::Type>(type), lockOut != 0) ? 1u : 0u;
}

extern "C" inline uint32_t WClearWeather() {
    return Weather::ClearWeather() ? 1u : 0u;
}

extern "C" inline uint32_t WWeatherName(int32_t type, char* out, uint32_t cap) {
    return CopyOut(Weather::WeatherName(static_cast<Weather::Type>(type)), out, cap);
}

extern "C" inline uint32_t WWeatherFromName(const char* name, int32_t* out) {
    if (!name || !out) return 0;
    Weather::Type t{};
    if (!Weather::WeatherFromName(name, t)) return 0;
    *out = static_cast<int32_t>(t);
    return 1;
}

extern "C" inline int32_t WWeatherTypeCount() {
    return static_cast<int32_t>(Weather::kTypeCount);
}

extern "C" inline int32_t WGetWeatherOverride() { return static_cast<int32_t>(Weather::GetWeatherOverride()); }
extern "C" inline uint32_t WIsWeatherOverridden() { return Weather::IsWeatherOverridden() ? 1u : 0u; }
extern "C" inline uint32_t WIsWeatherLocked() { return Weather::IsWeatherLocked() ? 1u : 0u; }
extern "C" inline int32_t WGetMagicWeather() { return static_cast<int32_t>(Weather::GetMagicWeather()); }

extern "C" inline uint32_t WIsRaining()    { return Weather::IsRaining() ? 1u : 0u; }
extern "C" inline uint32_t WIsSnowing()    { return Weather::IsSnowing() ? 1u : 0u; }
extern "C" inline uint32_t WIsThundering() { return Weather::IsThundering() ? 1u : 0u; }

// A hold re-applies the weather every frame, which is a different thing from
// setting it once and a different thing again from locking it out. Three
// separate calls because they are three separate intentions.
extern "C" inline uint32_t WHoldWeather(int32_t type, uint32_t lockOut) {
    return Weather::HoldWeather(static_cast<Weather::Type>(type), lockOut != 0) ? 1u : 0u;
}

extern "C" inline void WReleaseWeather() { Weather::ReleaseWeather(); }
extern "C" inline int32_t WGetWeatherHold() { return static_cast<int32_t>(Weather::GetWeatherHold()); }

// How many frames the hold has left. A hold that is about to lapse and one that
// will run forever are different states, and a mod refreshing a hold needs to
// know which it is looking at rather than re-applying every frame.
extern "C" inline int32_t WGetWeatherHoldFrames() {
    return static_cast<int32_t>(Weather::GetWeatherHoldFrames());
}

// Drives the hold. Called from a tick by whichever mod set it; without this the
// hold is set and never re-applied, which looks exactly like the weather
// override silently failing.
extern "C" inline void WTickWeatherHold() { Weather::TickWeatherHold(); }

extern "C" inline int32_t WGetWeatherClimate() { return static_cast<int32_t>(Weather::GetClimate()); }
extern "C" inline uint32_t WClimateName(int32_t climate, char* out, uint32_t cap) {
    return CopyOut(Weather::ClimateName(static_cast<int>(climate)), out, cap);
}

// --- time ------------------------------------------------------------------

extern "C" inline uint32_t WGetGameTime(float* hours) {
    if (!hours) return 0;
    return Time::GetGameTime(*hours) ? 1u : 0u;
}

extern "C" inline uint32_t WGetGameTimeHM(int32_t* hour, int32_t* minute) {
    if (!hour || !minute) return 0;
    int h = 0, m = 0;
    if (!Time::GetGameTime(h, m)) return 0;
    *hour = static_cast<int32_t>(h);
    *minute = static_cast<int32_t>(m);
    return 1;
}

extern "C" inline uint32_t WSetGameTime(float hours) {
    return Time::SetGameTime(hours) ? 1u : 0u;
}

extern "C" inline uint32_t WSetGameTimeHM(int32_t hour, int32_t minute) {
    return Time::SetGameTime(static_cast<int>(hour), static_cast<int>(minute)) ? 1u : 0u;
}

extern "C" inline uint32_t WGetGameTimeRaw(float* out) {
    if (!out) return 0;
    return Time::GetGameTimeRaw(*out) ? 1u : 0u;
}

extern "C" inline uint32_t WSetGameTimeRaw(float units) {
    return Time::SetGameTimeRaw(units) ? 1u : 0u;
}

extern "C" inline uint32_t WGetDay(int32_t* out) {
    if (!out) return 0;
    int d = 0;
    if (!Time::GetDay(d)) return 0;
    *out = static_cast<int32_t>(d);
    return 1;
}

extern "C" inline uint32_t WSetDay(int32_t day) {
    return Time::SetDay(static_cast<int>(day)) ? 1u : 0u;
}

extern "C" inline uint32_t WGetDivision(int32_t* out) {
    if (!out) return 0;
    Time::Division d{};
    if (!Time::GetDivision(d)) return 0;
    *out = static_cast<int32_t>(d);
    return 1;
}

extern "C" inline uint32_t WGetNextDivision(int32_t* out) {
    if (!out) return 0;
    Time::Division d{};
    if (!Time::GetNextDivision(d)) return 0;
    *out = static_cast<int32_t>(d);
    return 1;
}

extern "C" inline uint32_t WDivisionName(int32_t division, char* out, uint32_t cap) {
    return CopyOut(Time::DivisionName(static_cast<Time::Division>(division)), out, cap);
}

extern "C" inline uint32_t WIsNight() { return Time::IsNight() ? 1u : 0u; }
extern "C" inline uint32_t WIsTimeFlowing() { return Time::IsTimeFlowing() ? 1u : 0u; }

extern "C" inline uint32_t WGetTimeScale(float* out) {
    if (!out) return 0;
    return Time::GetTimeScale(*out) ? 1u : 0u;
}

extern "C" inline uint32_t WSetTimeScale(float scale) {
    return Time::SetTimeScale(scale) ? 1u : 0u;
}

// --- temperature -----------------------------------------------------------

extern "C" inline uint32_t WGetTemperature(float* celsius) {
    if (!celsius) return 0;
    return Climate::GetTemperature(*celsius) ? 1u : 0u;
}

extern "C" inline uint32_t WGetTemperatureAt(float altitude, float* celsius) {
    if (!celsius) return 0;
    return Climate::GetTemperatureAt(altitude, *celsius) ? 1u : 0u;
}

extern "C" inline uint32_t WSetTemperature(float celsius) {
    return Climate::SetTemperature(celsius) ? 1u : 0u;
}

extern "C" inline uint32_t WSetTemperatureOffset(float degrees) {
    return Climate::SetTemperatureOffset(degrees) ? 1u : 0u;
}

extern "C" inline uint32_t WClearTemperature() {
    return Climate::ClearTemperature() ? 1u : 0u;
}

extern "C" inline uint32_t WIsTemperatureOverridden() {
    return Climate::IsTemperatureOverridden() ? 1u : 0u;
}

// The two curves the game blends between over the day, and where in that blend
// it currently is. A mod predicting how cold somewhere will be at night needs
// all three - the current temperature alone cannot answer it.
extern "C" inline uint32_t WGetDayTemperature(float altitude, float* out) {
    if (!out) return 0;
    return Climate::GetDayTemperature(altitude, *out) ? 1u : 0u;
}

extern "C" inline uint32_t WGetNightTemperature(float altitude, float* out) {
    if (!out) return 0;
    return Climate::GetNightTemperature(altitude, *out) ? 1u : 0u;
}

extern "C" inline uint32_t WGetNightBlend(float* out) {
    if (!out) return 0;
    return Climate::GetNightBlend(*out) ? 1u : 0u;
}

// The game forcing a temperature is a different state from a mod overriding
// one, and a mod that cannot tell them apart will fight the game over a value
// it was never going to win.
extern "C" inline uint32_t WIsTemperatureForcedByGame() {
    return Climate::IsTemperatureForcedByGame() ? 1u : 0u;
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsWeather",    &WSupportsWeather),
    WIIXL_SURFACE_SYMBOL("SupportsTime",       &WSupportsTime),
    WIIXL_SURFACE_SYMBOL("SupportsClimate",    &WSupportsClimate),
    WIIXL_SURFACE_SYMBOL("WeatherAvailable",   &WWeatherAvailable),
    WIIXL_SURFACE_SYMBOL("TimeAvailable",      &WTimeAvailable),
    WIIXL_SURFACE_SYMBOL("ClimateAvailable",   &WClimateAvailable),

    WIIXL_SURFACE_SYMBOL("GetWeather",         &WGetWeather),
    WIIXL_SURFACE_SYMBOL("SetWeather",         &WSetWeather),
    WIIXL_SURFACE_SYMBOL("ClearWeather",       &WClearWeather),
    WIIXL_SURFACE_SYMBOL("WeatherName",        &WWeatherName),
    WIIXL_SURFACE_SYMBOL("WeatherFromName",    &WWeatherFromName),
    WIIXL_SURFACE_SYMBOL("WeatherTypeCount",   &WWeatherTypeCount),
    WIIXL_SURFACE_SYMBOL("GetWeatherOverride", &WGetWeatherOverride),
    WIIXL_SURFACE_SYMBOL("IsWeatherOverridden", &WIsWeatherOverridden),
    WIIXL_SURFACE_SYMBOL("IsWeatherLocked",    &WIsWeatherLocked),
    WIIXL_SURFACE_SYMBOL("GetMagicWeather",    &WGetMagicWeather),
    WIIXL_SURFACE_SYMBOL("IsRaining",          &WIsRaining),
    WIIXL_SURFACE_SYMBOL("IsSnowing",          &WIsSnowing),
    WIIXL_SURFACE_SYMBOL("IsThundering",       &WIsThundering),
    WIIXL_SURFACE_SYMBOL("HoldWeather",        &WHoldWeather),
    WIIXL_SURFACE_SYMBOL("ReleaseWeather",     &WReleaseWeather),
    WIIXL_SURFACE_SYMBOL("GetWeatherHold",     &WGetWeatherHold),
    WIIXL_SURFACE_SYMBOL("GetWeatherHoldFrames", &WGetWeatherHoldFrames),
    WIIXL_SURFACE_SYMBOL("TickWeatherHold",    &WTickWeatherHold),
    WIIXL_SURFACE_SYMBOL("GetWeatherClimate",  &WGetWeatherClimate),
    WIIXL_SURFACE_SYMBOL("ClimateName",        &WClimateName),

    WIIXL_SURFACE_SYMBOL("GetGameTime",        &WGetGameTime),
    WIIXL_SURFACE_SYMBOL("GetGameTimeHM",      &WGetGameTimeHM),
    WIIXL_SURFACE_SYMBOL("SetGameTime",        &WSetGameTime),
    WIIXL_SURFACE_SYMBOL("SetGameTimeHM",      &WSetGameTimeHM),
    WIIXL_SURFACE_SYMBOL("GetGameTimeRaw",     &WGetGameTimeRaw),
    WIIXL_SURFACE_SYMBOL("SetGameTimeRaw",     &WSetGameTimeRaw),
    WIIXL_SURFACE_SYMBOL("GetDay",             &WGetDay),
    WIIXL_SURFACE_SYMBOL("SetDay",             &WSetDay),
    WIIXL_SURFACE_SYMBOL("GetDivision",        &WGetDivision),
    WIIXL_SURFACE_SYMBOL("GetNextDivision",    &WGetNextDivision),
    WIIXL_SURFACE_SYMBOL("DivisionName",       &WDivisionName),
    WIIXL_SURFACE_SYMBOL("IsNight",            &WIsNight),
    WIIXL_SURFACE_SYMBOL("IsTimeFlowing",      &WIsTimeFlowing),
    WIIXL_SURFACE_SYMBOL("GetTimeScale",       &WGetTimeScale),
    WIIXL_SURFACE_SYMBOL("SetTimeScale",       &WSetTimeScale),

    WIIXL_SURFACE_SYMBOL("GetDayTemperature",  &WGetDayTemperature),
    WIIXL_SURFACE_SYMBOL("GetNightTemperature", &WGetNightTemperature),
    WIIXL_SURFACE_SYMBOL("GetNightBlend",      &WGetNightBlend),
    WIIXL_SURFACE_SYMBOL("IsTemperatureForcedByGame", &WIsTemperatureForcedByGame),
    WIIXL_SURFACE_SYMBOL("GetTemperature",     &WGetTemperature),
    WIIXL_SURFACE_SYMBOL("GetTemperatureAt",   &WGetTemperatureAt),
    WIIXL_SURFACE_SYMBOL("SetTemperature",     &WSetTemperature),
    WIIXL_SURFACE_SYMBOL("SetTemperatureOffset", &WSetTemperatureOffset),
    WIIXL_SURFACE_SYMBOL("ClearTemperature",   &WClearTemperature),
    WIIXL_SURFACE_SYMBOL("IsTemperatureOverridden", &WIsTemperatureOverridden),
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

} // namespace WiiXLaunch::BotW::Surfaces::WorldSurface
