#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include "gamedata.hpp"

// WiiXLaunch::BotW::Events - edge-triggered callbacks for the things worth
// noticing: a Korok seed found, a shrine completed, a tower activated.
//
// The game has no notification for any of these. Each one is a GameData flag
// that quietly becomes true (or a counter that goes up), so this polls and
// reports the EDGE - the frame a value changed - which is the same shape as
// Player::ConsumeAttackEvent.
//
// What is watched:
//
//   OnKorokGet         HiddenKorok_Number going UP. Reports the new total and
//                      how many were gained, so a stack of seeds handed over at
//                      once reads correctly.
//   OnShrineComplete   Clear_DungeonNNN going false -> true. The flag family is
//                      established repo knowledge - gamedata.hpp already names
//                      Clear_Dungeon000 as one of the IsOneTrigger flags, and
//                      being one-trigger is itself consistent with "completed",
//                      since those are the flags the ordinary setter refuses to
//                      clear once true.
//
//                      NOTE this header used to justify the choice by calling
//                      it the other half of the Enter_/Clear_ pair each
//                      shrine's map marker caches. That was wrong: the marker
//                      class carrying that pair (0x02e95e2c) turned out to be
//                      the DIVINE BEAST marker, and shrine markers are a
//                      different class entirely - see the correction at the top
//                      of botw/map.hpp. The flag name is still believed right;
//                      the reasoning that backed it is not, and no write site
//                      has been traced. Verify before trusting the exact frame
//                      it fires on.
//   OnTowerOpen        MapTower_NN going false -> true, towers 1-15.
//
// COST. Polling 136 shrines by name every frame would mean 136 string hashes
// and binary searches per frame, which is silly. Instead the flag INDICES are
// resolved once - the same trick the game itself uses for every flag it cares
// about - and each frame is then an indexed read per watched flag. Arming is
// lazy: it happens on the first Tick that finds a loaded save.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Events {

static constexpr bool SupportsEvents = !WIIXL_SWITCH;

// total is the new HiddenKorok_Number; gained is how much it rose by this tick.
using KorokCallback = void (*)(int total, int gained);

// shrine is the Dungeon index, so 0 is Dungeon000.
using ShrineCallback = void (*)(int shrine);

// tower is 1-15, matching MapTower_01 upward.
using TowerCallback = void (*)(int tower);

// How far to probe for shrine flags. Vanilla runs Dungeon000-119 and the DLC
// adds 120-135; the slack costs a handful of one-off name lookups at arming
// time and nothing per frame. Indices that do not resolve are simply not
// watched, so no count is ever assumed.
constexpr int kShrineProbeLimit = 152;

constexpr int kFirstTower = 1;
constexpr int kTowerCount = 15;

// More than this many shrines or towers flipping true in ONE tick is not
// gameplay - it is a save being loaded, or a mod calling Map::SetMapUnlockAll.
// Those resync silently instead of firing a storm of callbacks. A genuine
// double-completion in a single frame is not a thing, so this cannot swallow a
// real event.
constexpr int kBulkChangeThreshold = 3;

namespace impl {

// ksys::gdt, all already recorded by gamedata.hpp - reached through its impl
// rather than duplicated, since this header includes it anyway.
//
//   0x0320f45c  findFlagIndexByName(container, SafeString* name, int) -> index,
//               or -1. The third argument is passed 0 here, matching the game's
//               own use of it in 0x0321072c.
//   0x03234a64  readBoolByIndex(u32* out, container, index, u8 flags)
//   0x03234b08  readS32ByIndex(s32* out, container, index, u8 flags)
//
// Containers are core+0x04 for bools and core+0x10 for s32.

constexpr const char* kKorokFlag = "HiddenKorok_Number";

// Every watched flag, resolved once and then read by index.
struct Watch {
    bool armed;

    // Latched for the Consume* pollers: set on the same condition that fires a
    // callback, cleared when read. Independent of the callbacks, so a caller can
    // use either style or both.
    bool pendingShrine;
    bool pendingTower;

    int korokIndex;
    int korokLast;

    int shrineIndex[kShrineProbeLimit];
    bool shrineLast[kShrineProbeLimit];

    int towerIndex[kTowerCount];
    bool towerLast[kTowerCount];

    KorokCallback onKorok;
    ShrineCallback onShrine;
    TowerCallback onTower;
};

inline Watch& State() {
    static Watch w = {};
    return w;
}

// Zero-padded decimal appended to p.
inline char* AppendPadded(char* p, char* end, int value, int digits) {
    char tmp[12];
    int n = 0;
    if (value < 0) value = 0;
    do {
        tmp[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && n < 11);
    while (n < digits && n < 11) tmp[n++] = '0';
    while (n > 0 && p < end) *p++ = tmp[--n];
    return p;
}

// "<prefix><zero-padded index>" into a shared buffer, consumed immediately.
inline const char* FlagName(const char* prefix, int index, int digits) {
    static char buf[48];
    char* p = buf;
    char* const end = buf + sizeof(buf) - 1;
    for (const char* s = prefix; s && *s && p < end; ++s) *p++ = *s;
    p = AppendPadded(p, end, index, digits);
    *p = '\0';
    return buf;
}

inline const char* ShrineFlag(int shrine) { return FlagName("Clear_Dungeon", shrine, 3); }
inline const char* TowerFlag(int tower) { return FlagName("MapTower_", tower, 2); }

// Resolves a flag name to its index within one container, or -1.
inline int FindIndex(GameData::impl::FlagAccess& access, uintptr_t containerOffset,
                     const char* name) {
#if !WIIXL_SWITCH
    auto find = WiiXLaunch::GetTargetFunction<GameData::impl::FindFlagIndexByNameFn>(
        0x0, GameData::impl::kFindFlagIndexByNameWiiU);
    if (!find || !name) return -1;

    void* container = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(access.core) + containerOffset);
    GameData::impl::SafeString key = GameData::impl::MakeSafeString(name);
    return find(container, &key, 0);
#else
    (void)access; (void)containerOffset; (void)name;
    return -1;
#endif
}

inline bool ReadBoolAt(GameData::impl::FlagAccess& access, int index, bool& out) {
#if !WIIXL_SWITCH
    if (index < 0) return false;

    auto read = WiiXLaunch::GetTargetFunction<GameData::impl::GetFlagBoolByIndexFn>(
        0x0, GameData::impl::kGetFlagBoolByIndexWiiU);
    if (!read) return false;

    void* container = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(access.core) + GameData::impl::kBoolContainerOffset);

    uint32_t value = 0;
    if (read(&value, container, index, access.flags) == 0) return false;

    out = value != 0;
    return true;
#else
    (void)access; (void)index; (void)out;
    return false;
#endif
}

inline bool ReadS32At(GameData::impl::FlagAccess& access, int index, int& out) {
#if !WIIXL_SWITCH
    if (index < 0) return false;

    auto read = WiiXLaunch::GetTargetFunction<GameData::impl::GetFlagS32ByIndexFn>(
        0x0, GameData::impl::kGetFlagS32ByIndexWiiU);
    if (!read) return false;

    void* container = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(access.core) + GameData::impl::kS32ContainerOffset);

    int value = 0;
    if (read(&value, container, index, access.flags) == 0) return false;

    out = value;
    return true;
#else
    (void)access; (void)index; (void)out;
    return false;
#endif
}

// Resolves every index and snapshots the current state WITHOUT firing. Returns
// false while there is no save loaded, which is why Tick keeps retrying.
inline bool Arm() {
#if !WIIXL_SWITCH
    GameData::impl::FlagAccess access;
    if (!GameData::impl::ResolveFlagAccess(access)) return false;

    Watch& w = State();

    w.korokIndex = FindIndex(access, GameData::impl::kS32ContainerOffset, kKorokFlag);
    w.korokLast = 0;
    if (w.korokIndex >= 0) ReadS32At(access, w.korokIndex, w.korokLast);

    for (int i = 0; i < kShrineProbeLimit; ++i) {
        w.shrineIndex[i] = FindIndex(access, GameData::impl::kBoolContainerOffset,
                                     ShrineFlag(i));
        w.shrineLast[i] = false;
        if (w.shrineIndex[i] >= 0) ReadBoolAt(access, w.shrineIndex[i], w.shrineLast[i]);
    }

    for (int i = 0; i < kTowerCount; ++i) {
        w.towerIndex[i] = FindIndex(access, GameData::impl::kBoolContainerOffset,
                                    TowerFlag(kFirstTower + i));
        w.towerLast[i] = false;
        if (w.towerIndex[i] >= 0) ReadBoolAt(access, w.towerIndex[i], w.towerLast[i]);
    }

    w.pendingShrine = false;
    w.pendingTower = false;
    w.armed = true;
    return true;
#else
    return false;
#endif
}

} // namespace impl

// Registers a callback. Passing nullptr clears it. Safe to call before the game
// has loaded a save - arming happens on the first Tick that can.
inline void OnKorokGet(KorokCallback callback) { impl::State().onKorok = callback; }
inline void OnShrineComplete(ShrineCallback callback) { impl::State().onShrine = callback; }
inline void OnTowerOpen(TowerCallback callback) { impl::State().onTower = callback; }

// Whether a shrine has been completed since this was last asked.
//
// The polling half of OnShrineComplete, shaped like Player::ConsumeAttackEvent:
// true exactly once per completion, then false until the next one. Use it when
// you want to ask rather than be told, and do not care WHICH shrine - the
// callback is the one that tells you that.
//
// Latched on the same condition the callback fires on, so a save load or a
// Map::SetMapUnlockAll is swallowed here too. Registering a callback is not
// required and does not interfere.
//
// Call at most once per frame per consumer: two consumers polling the same
// event will fight over it, exactly as they would over ConsumeAttackEvent.
inline bool ConsumeShrineComplete() {
#if !WIIXL_SWITCH
    impl::Watch& w = impl::State();
    if (!w.pendingShrine) return false;
    w.pendingShrine = false;
    return true;
#else
    return false;
#endif
}

// The same for a tower being activated.
inline bool ConsumeTowerOpen() {
#if !WIIXL_SWITCH
    impl::Watch& w = impl::State();
    if (!w.pendingTower) return false;
    w.pendingTower = false;
    return true;
#else
    return false;
#endif
}

// Whether the watches have resolved against a loaded save yet.
inline bool IsArmed() {
#if !WIIXL_SWITCH
    return impl::State().armed;
#else
    return false;
#endif
}

// Re-reads everything as the new baseline WITHOUT firing callbacks.
//
// Tick does this for itself when it sees a bulk change, so the usual reason to
// call it by hand is after deliberately rewriting a pile of flags and not
// wanting the next tick to have an opinion about it.
inline bool Resync() {
#if !WIIXL_SWITCH
    impl::State().armed = false;
    return impl::Arm();
#else
    return false;
#endif
}

// Polls every watched flag and fires whatever changed. Call once per frame from
// the game thread.
//
// Player::OnTick is the obvious home, but that slot holds only one callback, so
// this is left for the caller to place rather than taking it:
//
//   Player::OnTick([] { Events::Tick(); myOwnPerFrameWork(); });
//
// Only rises are reported: a seed count going down, or a flag being cleared,
// updates the baseline silently. Nothing here fires on the first armed tick
// either, so loading a 120-shrine save is quiet.
inline void Tick() {
#if !WIIXL_SWITCH
    impl::Watch& w = impl::State();

    GameData::impl::FlagAccess access;
    if (!GameData::impl::ResolveFlagAccess(access)) {
        // No save loaded. Drop the arming so a later load re-baselines rather
        // than firing every flag that differs from the last save's.
        w.armed = false;
        return;
    }

    if (!w.armed) {
        impl::Arm();
        return;
    }

    // --- Koroks: report the rise, however big -----------------------------
    if (w.korokIndex >= 0) {
        int total = w.korokLast;
        if (impl::ReadS32At(access, w.korokIndex, total) && total != w.korokLast) {
            const int gained = total - w.korokLast;
            w.korokLast = total;
            if (gained > 0 && w.onKorok) w.onKorok(total, gained);
        }
    }

    // --- shrines and towers: collect the rises, then report -------------
    //
    // The risen indices are RECORDED rather than recounted. Rewriting the
    // baseline first and then re-walking it cannot tell a fresh rise from a
    // flag that was already true, which would fire for shrines cleared hours
    // ago. Only kBulkChangeThreshold of them can be reported anyway, so the
    // buffer is tiny.
    int risen[kBulkChangeThreshold];
    int shrineRises = 0;
    for (int i = 0; i < kShrineProbeLimit; ++i) {
        if (w.shrineIndex[i] < 0) continue;

        bool value = w.shrineLast[i];
        if (!impl::ReadBoolAt(access, w.shrineIndex[i], value)) continue;
        if (value == w.shrineLast[i]) continue;

        w.shrineLast[i] = value;
        if (!value) continue;                       // a clear, not a completion
        if (shrineRises < kBulkChangeThreshold) risen[shrineRises] = i;
        ++shrineRises;
    }

    // A handful is gameplay; a pile is a save load or an unlock-all. Either way
    // the baseline above is already updated - only the reporting is skipped.
    if (shrineRises > 0 && shrineRises <= kBulkChangeThreshold) {
        w.pendingShrine = true;
        if (w.onShrine) {
            for (int i = 0; i < shrineRises; ++i) w.onShrine(risen[i]);
        }
    }

    int risenTowers[kBulkChangeThreshold];
    int towerRises = 0;
    for (int i = 0; i < kTowerCount; ++i) {
        if (w.towerIndex[i] < 0) continue;

        bool value = w.towerLast[i];
        if (!impl::ReadBoolAt(access, w.towerIndex[i], value)) continue;
        if (value == w.towerLast[i]) continue;

        w.towerLast[i] = value;
        if (!value) continue;
        if (towerRises < kBulkChangeThreshold) risenTowers[towerRises] = i;
        ++towerRises;
    }

    if (towerRises > 0 && towerRises <= kBulkChangeThreshold) {
        w.pendingTower = true;
        if (w.onTower) {
            for (int i = 0; i < towerRises; ++i) w.onTower(kFirstTower + risenTowers[i]);
        }
    }
#endif
}

} // namespace WiiXLaunch::BotW::Events
