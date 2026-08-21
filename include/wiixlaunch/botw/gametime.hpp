#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>

// WiiXLaunch::BotW::Time - the in-game clock (ksys::wm world manager).
//
// How the clock actually works, which is what shapes this API:
//
//   * The world manager owns a time sub-manager, and that object holds the
//     clock as an f32 at +0x98. A day is 360.0 units long, so one unit is four
//     in-game minutes and fifteen units is an hour. At the default rate a full
//     day takes about 24 real minutes.
//
//   * The value is ALSO a GameData flag, "WM_Time". Those are not two copies
//     that drift: the time manager's calc (0x0365f558) reads the flag into
//     +0x98 at the top of every frame, advances it, and writes it back at the
//     bottom. The flag is the save-backed storage and +0x98 is the working
//     copy, refreshed from it each frame.
//
//   * There is a proper way to jump the clock. calc checks a pending field at
//     +0xa0 (-1.0 when there is nothing pending) and, when it holds a value,
//     moves the clock there and notifies the next sub-manager. That is the
//     path a campfire rest goes through, and it is what SetGameTime uses -
//     rather than only slamming +0x98, which would leave the sky and the
//     schedule systems unaware anything happened.
//
// SetGameTime writes all three - the flag, the working copy and the pending
// field - so the value is correct whether you read it back immediately, save
// straight away, or let the game run a frame.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Time {

// The eight time divisions the game splits a day into. Values are the game's
// own, from the chain of comparisons at the end of calc, and each one drives
// the matching WM_Is* GameData flag.
//
//   NightB    [  0,  60)   00:00 - 04:00
//   MorningA  [ 60, 105)   04:00 - 07:00
//   MorningB  [105, 150)   07:00 - 10:00
//   NoonA     [150, 195)   10:00 - 13:00
//   NoonB     [195, 255)   13:00 - 17:00
//   EveningA  [255, 285)   17:00 - 19:00
//   EveningB  [285, 315)   19:00 - 21:00
//   NightA    [315, 360)   21:00 - 24:00
enum class Division : int {
    MorningA = 0,
    MorningB = 1,
    NoonA = 2,
    NoonB = 3,
    EveningA = 4,
    EveningB = 5,
    NightA = 6,
    NightB = 7,
};

static constexpr bool SupportsGameTime = !WIIXL_SWITCH;

// Clock units. The raw value the game stores is in 1/15ths of an hour.
constexpr float kUnitsPerDay = 360.0f;
constexpr float kUnitsPerHour = 15.0f;
constexpr float kMinutesPerUnit = 4.0f;

// The rate field's stock value, 1/120 units per tick. Exposed because the only
// sane way to talk about the clock's speed is as a multiple of this.
constexpr float kDefaultTimeRate = 1.0f / 120.0f;

namespace impl {

// --- reaching the time manager ------------------------------------------
//
// ksys::wm::Manager is a singleton whose pointer lives at 0x1047be88, read as
// `lis r3,0x1048; lwz r3,-0x4178(r3)` by its callers (0x02c41264 is the
// clearest, right before it calls the manager's init at 0x03673f6c). It owns
// an array of sub-managers: the pointer array at +0x464 and its length at
// +0x45c, which every user bounds-checks against before indexing.
//
// Sub-manager 0 is the time manager. Fixed by 0x03673f6c, which passes
// `((void**)(worldMgr + 0x464))[0]` to 0x03661870 - the function that goes on
// to cache the WM_Time / WM_NumberOfDays / WM_TimeDivision flag indices.
constexpr uintptr_t kWorldMgrPtrWiiU = 0x1047be88;
constexpr uintptr_t kSubMgrCount = 0x45c;
constexpr uintptr_t kSubMgrArray = 0x464;
constexpr int kTimeMgrIndex = 0;

// char. calc reads it before doing anything and returns immediately when it is
// zero, so it gates the whole clock: no advance, no pending jump, no flag
// write-back. Loading screens and some cutscenes are when you will see it low.
constexpr uintptr_t kTimeFlowing = 0x648;

// --- the time manager's fields ------------------------------------------
//
// All of these are read straight out of calc (0x0365f558) and the two init
// functions (0x03660e8c, 0x03661870).
constexpr uintptr_t kDivision = 0x18;       // s32, current Division
constexpr uintptr_t kNextDivision = 0x1c;   // s32, the one after it
constexpr uintptr_t kTime = 0x98;           // f32, 0 .. 360
constexpr uintptr_t kPendingTime = 0xa0;    // f32, -1.0 when nothing pending
constexpr uintptr_t kTimeRate = 0xa4;       // f32, units per tick
constexpr uintptr_t kBloodMoonTimer = 0xa8; // f32, mirrors WM_BloodyMoonTimer
constexpr uintptr_t kSpeedRatio = 0xb0;     // f32, max(rate / default, 1.0)
constexpr uintptr_t kTimeFlagIndex = 0xb4;  // WM_Time
constexpr uintptr_t kDayFlagIndex = 0xb8;   // WM_NumberOfDays
constexpr uintptr_t kDivisionFlagIndex = 0xbc;  // WM_TimeDivision
constexpr uintptr_t kDayCount = 0x11c;      // s32, mirrors WM_NumberOfDays
constexpr uintptr_t kTimeRequest = 0x129;   // u8, see below

// The pending field is only consumed when this byte is 0 or 0xff. It is a
// "jump to a fixed time" request the game uses internally: codes 0x09 to 0x20
// are midnight through 23:00 on the hour (code = hour + 9), and calc's switch
// overwrites the clock with the matching constant. 0 is the normal state, set
// by the manager's init. Nothing here writes it - the pending field takes an
// arbitrary time, which the request codes cannot.
constexpr uint8_t kRequestNone = 0;
constexpr uint8_t kRequestIdle = 0xff;

// --- writing the flags ---------------------------------------------------
//
// ksys::wm's own by-index flag writers, the ones calc uses to push the clock
// back into GameData at the end of every frame. Both write the flag into the
// two stores at gdtMgr+0x70c and +0x710 and honour the manager's own guard
// bits, which is exactly why they are worth calling rather than reimplementing.
//
//   0x0366275c  setF32FlagByIndex(f32 value, gdtMgr, u32 index)
//   0x03662598  setS32FlagByIndex(gdtMgr, s32 value, u32 index)
//
// The f32 one takes its value in f1 and only gdtMgr/index in r3/r4, which is
// why the value is first in the signature but the pointer is the first GPR.
constexpr uintptr_t kGdtMgrPtrWiiU = 0x1046d5b0;
constexpr uintptr_t kSetF32FlagByIndexWiiU = 0x0366275c;
constexpr uintptr_t kSetS32FlagByIndexWiiU = 0x03662598;

using SetF32FlagFn = int (*)(float value, void* gdtMgr, uint32_t index);
using SetS32FlagFn = int (*)(void* gdtMgr, int32_t value, uint32_t index);

constexpr uint32_t kInvalidFlagIndex = 0xffffffffu;

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

// The time sub-manager, or null before the world manager has been built.
inline uintptr_t TimeMgr() {
#if !WIIXL_SWITCH
    void* world = WorldMgr();
    if (!world) return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(world);
    uint32_t count = *reinterpret_cast<uint32_t*>(base + kSubMgrCount);
    if (count <= static_cast<uint32_t>(kTimeMgrIndex)) return 0;

    uintptr_t array = *reinterpret_cast<uintptr_t*>(base + kSubMgrArray);
    if (!PlausiblePointer(array)) return 0;

    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(
        array + 4 * static_cast<uintptr_t>(kTimeMgrIndex));
    if (!PlausiblePointer(mgr)) return 0;
    return mgr;
#else
    return 0;
#endif
}

inline void* GdtMgr() {
#if !WIIXL_SWITCH
    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kGdtMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return nullptr;
    return reinterpret_cast<void*>(mgr);
#else
    return nullptr;
#endif
}

// Folds any value into [0, 360). Loops rather than using fmod so this header
// stays free of <cmath>, and clamps the iteration count so a NaN or a wild
// float cannot spin the game thread.
inline float WrapRaw(float units) {
    if (!(units == units)) return 0.0f;   // NaN
    int guard = 0;
    while (units < 0.0f && guard++ < 4096) units += kUnitsPerDay;
    guard = 0;
    while (units >= kUnitsPerDay && guard++ < 4096) units -= kUnitsPerDay;
    if (units < 0.0f || units >= kUnitsPerDay) return 0.0f;
    return units;
}

} // namespace impl

// Whether the clock is reachable - a world manager exists and its time
// sub-manager is built. False on the title screen and during early boot.
inline bool IsAvailable() {
#if !WIIXL_SWITCH
    return impl::TimeMgr() != 0;
#else
    return false;
#endif
}

// Whether the game is currently advancing the clock. False does NOT mean the
// clock is broken - loading and some cutscenes park it - but it does mean a
// SetGameTime will sit in the pending field until the game resumes, so the
// notify half of the jump is deferred. The value itself still lands.
inline bool IsTimeFlowing() {
#if !WIIXL_SWITCH
    void* world = impl::WorldMgr();
    if (!world) return false;
    return *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(world) + impl::kTimeFlowing) != 0;
#else
    return false;
#endif
}

// The clock in the game's own units: 0 at midnight, 360 at the next midnight,
// so 15 per hour. Returns false when there is no clock yet, and leaves out
// alone in that case.
inline bool GetGameTimeRaw(float& out) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;
    out = *reinterpret_cast<float*>(mgr + impl::kTime);
    return true;
#else
    (void)out;
    return false;
#endif
}

// The clock in hours, 0.0 to 24.0.
inline bool GetGameTime(float& hours) {
#if !WIIXL_SWITCH
    float raw = 0.0f;
    if (!GetGameTimeRaw(raw)) return false;
    hours = raw / kUnitsPerHour;
    return true;
#else
    (void)hours;
    return false;
#endif
}

// The clock split into whole hours and minutes. Minutes land on multiples of
// four unless the clock is mid-tick, since one raw unit IS four minutes.
inline bool GetGameTime(int& hour, int& minute) {
#if !WIIXL_SWITCH
    float raw = 0.0f;
    if (!GetGameTimeRaw(raw)) return false;

    raw = impl::WrapRaw(raw);
    int h = static_cast<int>(raw / kUnitsPerHour);
    if (h > 23) h = 23;
    const float intoHour = raw - static_cast<float>(h) * kUnitsPerHour;

    int m = static_cast<int>(intoHour * kMinutesPerUnit);
    if (m < 0) m = 0;
    if (m > 59) m = 59;

    hour = h;
    minute = m;
    return true;
#else
    (void)hour; (void)minute;
    return false;
#endif
}

// Moves the clock, in the game's raw units. Values outside [0, 360) are folded
// into range rather than refused, so passing 375 gives 15 (01:00).
//
// Writes three places, all of which the game itself keeps in step:
//
//   the WM_Time flag   the save-backed storage, and what calc reloads from at
//                      the top of the next frame
//   +0x98              the working copy, so an immediate GetGameTime agrees
//   +0xa0              the pending-jump field, which is what makes calc treat
//                      this as a jump and notify the sub-manager that follows
//                      the clock, rather than as an ordinary tick
//
// The three cannot fight: calc loads the flag, then applies the pending value,
// and both hold the same number.
//
// Returns false only when there is no clock yet. Two caveats it cannot report:
// if the game has its own jump request in flight (the byte at +0x129 is
// neither 0 nor 0xff) calc will use that instead of the pending field, and
// while IsTimeFlowing() is false the pending half waits.
inline bool SetGameTimeRaw(float units) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;

    const float value = impl::WrapRaw(units);

    void* gdt = impl::GdtMgr();
    const uint32_t index = *reinterpret_cast<uint32_t*>(mgr + impl::kTimeFlagIndex);
    if (gdt && index != impl::kInvalidFlagIndex) {
        auto setFlag = WiiXLaunch::GetTargetFunction<impl::SetF32FlagFn>(
            0x0, impl::kSetF32FlagByIndexWiiU);
        if (setFlag) setFlag(value, gdt, index);
    }

    *reinterpret_cast<float*>(mgr + impl::kTime) = value;
    *reinterpret_cast<float*>(mgr + impl::kPendingTime) = value;
    return true;
#else
    (void)units;
    return false;
#endif
}

// Moves the clock, in hours. 5.5 is half past five in the morning.
inline bool SetGameTime(float hours) {
#if !WIIXL_SWITCH
    return SetGameTimeRaw(hours * kUnitsPerHour);
#else
    (void)hours;
    return false;
#endif
}

// Moves the clock to a wall-clock time. Minutes are rounded down to the four
// minute grid the raw unit imposes.
inline bool SetGameTime(int hour, int minute) {
#if !WIIXL_SWITCH
    const float raw = static_cast<float>(hour) * kUnitsPerHour +
                      static_cast<float>(minute) / kMinutesPerUnit;
    return SetGameTimeRaw(raw);
#else
    (void)hour; (void)minute;
    return false;
#endif
}

// How many days have passed - the WM_NumberOfDays flag. Incremented by the
// day-rollover handler at 0x0365f214, which is also where the Blood Moon
// schedule is consulted.
inline bool GetDay(int& out) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;
    out = *reinterpret_cast<int32_t*>(mgr + impl::kDayCount);
    return true;
#else
    (void)out;
    return false;
#endif
}

// Sets the day counter, working copy and flag together, the same pair calc
// writes back every frame.
//
// This does NOT run the rollover handler, so nothing keyed to "a day passed"
// fires - no Blood Moon roll, no shop restock. It changes the number, which is
// what you want for a display or a condition and not what you want if you are
// trying to simulate sleeping through a week.
inline bool SetDay(int day) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;

    void* gdt = impl::GdtMgr();
    const uint32_t index = *reinterpret_cast<uint32_t*>(mgr + impl::kDayFlagIndex);
    if (gdt && index != impl::kInvalidFlagIndex) {
        auto setFlag = WiiXLaunch::GetTargetFunction<impl::SetS32FlagFn>(
            0x0, impl::kSetS32FlagByIndexWiiU);
        if (setFlag) setFlag(gdt, day, index);
    }

    *reinterpret_cast<int32_t*>(mgr + impl::kDayCount) = day;
    return true;
#else
    (void)day;
    return false;
#endif
}

// Which of the eight divisions the clock is in. Read-only on purpose: calc
// recomputes it from the time every frame and pushes it to WM_TimeDivision, so
// writing it would last exactly one frame. Move the clock instead.
inline bool GetDivision(Division& out) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;

    const int32_t value = *reinterpret_cast<int32_t*>(mgr + impl::kDivision);
    if (value < 0 || value > 7) return false;
    out = static_cast<Division>(value);
    return true;
#else
    (void)out;
    return false;
#endif
}

// The division the clock is heading into next.
inline bool GetNextDivision(Division& out) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;

    const int32_t value = *reinterpret_cast<int32_t*>(mgr + impl::kNextDivision);
    if (value < 0 || value > 7) return false;
    out = static_cast<Division>(value);
    return true;
#else
    (void)out;
    return false;
#endif
}

// A printable name for a division. These are this header's names for the
// game's numbers, taken from the WM_Is* flag each one drives.
inline const char* DivisionName(Division division) {
    switch (division) {
        case Division::MorningA: return "MorningA";
        case Division::MorningB: return "MorningB";
        case Division::NoonA: return "NoonA";
        case Division::NoonB: return "NoonB";
        case Division::EveningA: return "EveningA";
        case Division::EveningB: return "EveningB";
        case Division::NightA: return "NightA";
        case Division::NightB: return "NightB";
        default: return "";
    }
}

// Night by the game's own reckoning - the two divisions that raise
// WM_IsNightA / WM_IsNightB, which is 21:00 to 04:00.
inline bool IsNight() {
#if !WIIXL_SWITCH
    Division division = Division::NightA;
    if (!GetDivision(division)) return false;
    return division == Division::NightA || division == Division::NightB;
#else
    return false;
#endif
}

// How fast the clock runs, as a multiple of the stock rate. 1.0 is normal;
// the game itself only ever sets the stock value, so anything else here is
// yours.
inline bool GetTimeScale(float& out) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;
    out = *reinterpret_cast<float*>(mgr + impl::kTimeRate) / kDefaultTimeRate;
    return true;
#else
    (void)out;
    return false;
#endif
}

// Changes how fast the clock runs. 0 freezes it, 2 doubles it.
//
// Reset to stock whenever the manager re-initialises from GameData - loading a
// save, in practice - so a mod that wants a permanent change should reapply it
// rather than set it once.
//
// Note the game derives a separate "speed ratio" from this field as
// max(rate / stock, 1.0) and hands that to other systems, so scales above 1
// are visible to more than just the clock, while scales below 1 are not.
inline bool SetTimeScale(float scale) {
#if !WIIXL_SWITCH
    uintptr_t mgr = impl::TimeMgr();
    if (!mgr) return false;
    if (!(scale == scale) || scale < 0.0f) return false;   // NaN or negative
    *reinterpret_cast<float*>(mgr + impl::kTimeRate) = kDefaultTimeRate * scale;
    return true;
#else
    (void)scale;
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::Time
