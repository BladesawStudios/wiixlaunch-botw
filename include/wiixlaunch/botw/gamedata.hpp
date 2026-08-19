#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>

// WiiXLaunch::BotW::GameData - BotW's GameData flag system (ksys::gdt), the
// authoritative, save-backed store for player progression values.
//
// Why this is separate from Actor: values you would expect to live on the
// player actor are often only *cached* there. Max hearts is the clear case -
// Actor::getMaxLife() reads a cache at actor+0x1320 that nothing re-syncs, so
// writing it reverts within a frame and makes the HUD play the
// heart-container-removed animation. The flag is the real thing.
//
// Addresses are Wii U V208 and were derived, not guessed - see
// data/symbols-wiiu-v208.csv for the evidence behind each one.

namespace WiiXLaunch::BotW::GameData {

// Switch offsets were never RE'd for these, so this is Wii U/Cemu only. Calls
// are safe no-ops elsewhere rather than jumping to an address that means
// nothing on that platform.
static constexpr bool SupportsMaxLife = !WIIXL_SWITCH;

namespace impl {

// gdt::getFlag_MaxHartValue(bool debug) -> s32
constexpr uintptr_t kGetFlagMaxHartValueWiiU = 0x02e1a1e0;
// gdt::setFlag_MaxHartValue(s32 value, bool debug)
constexpr uintptr_t kSetFlagMaxHartValueWiiU = 0x02e1a1f0;

// ksys::act::PlayerInfo::setMaxHeartValue(PlayerInfo* this, s32 quarter_hearts).
// Preferred over the raw flag setter: it writes the flag *and* PlayerInfo's own
// cached copy (mMaxHeartValue, an f32 at +0x64), which is what the game itself
// does. Setting only the flag leaves that cache stale.
constexpr uintptr_t kSetMaxHeartValueWiiU = 0x02d4992c;

// The PlayerInfo singleton pointer. Two independent callers of
// setMaxHeartValue load `this` from here, both null-checking first.
constexpr uintptr_t kPlayerInfoInstancePtrWiiU = 0x10463f38;

using GetFlagS32Fn = int (*)(bool debug);
using SetFlagS32Fn = void (*)(int value, bool debug);
using SetMaxHeartValueFn = void (*)(void* playerInfo, int quarterHearts);

// Dereferences the singleton pointer, or null. Range-checked the same way the
// module's other raw reads are: a wrong guess must fail as null, not a crash.
inline void* PlayerInfo() {
#if !WIIXL_SWITCH
    void* inst = *reinterpret_cast<void**>(kPlayerInfoInstancePtrWiiU);
    uintptr_t addr = reinterpret_cast<uintptr_t>(inst);
    if (addr < 0x10000000 || addr > 0xa0000000) return nullptr;
    return inst;
#else
    return nullptr;
#endif
}

} // namespace impl

// Max life in raw units, 4 per heart - the same unit Actor::GetMaxLife()
// reports. (The flag is named "MaxHartValue" in the game's own data, typo and
// all, and holds quarter-hearts.) Returns 0 if unavailable.
inline int GetMaxLife() {
#if !WIIXL_SWITCH
    auto fn = WiiXLaunch::GetTargetFunction<impl::GetFlagS32Fn>(0x0, impl::kGetFlagMaxHartValueWiiU);
    if (!fn) return 0;
    return fn(false);
#else
    return 0;
#endif
}

// Sets max life for real, through the same call the game uses: the GameData
// flag (save-backed, authoritative) plus PlayerInfo's cached copy.
//
// Deliberately does NOT read the flag back to confirm. The flag write is not
// observable synchronously - an immediate re-read still returns the old value,
// and an earlier version of this function reported "refused, nothing changed"
// for writes that had in fact succeeded. A verify that races the thing it is
// verifying is worse than no verify: it produces confident, wrong answers.
// Observe the result with GetMaxLife() on a later frame instead.
//
// Returns true if the call was made.
//
// No range limit. The game does not impose one here either - gdt::setF32/setS32
// validate the handle and write-protection bits, never the value, and neither
// PlayerInfo::setMaxHeartValue nor setStaminaMax clamps. 30 hearts is where the
// HUD stops drawing, not where the data stops accepting. This writes persistent
// save data, so whatever you pass is what your playthrough gets.
inline bool SetMaxLife(int rawUnits) {
#if !WIIXL_SWITCH
    void* playerInfo = impl::PlayerInfo();
    if (!playerInfo) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetMaxHeartValueFn>(0x0, impl::kSetMaxHeartValueWiiU);
    if (!set) return false;

    set(playerInfo, rawUnits);
    return true;
#else
    (void)rawUnits;
    return false;
#endif
}

// Convenience in whole/fractional hearts, matching Actor's hearts helpers.
inline float GetMaxHearts() { return GetMaxLife() / 4.0f; }
inline bool SetMaxHearts(float hearts) {
    return SetMaxLife(static_cast<int>(hearts * 4.0f + 0.5f));
}

// ---------------------------------------------------------------------------
// Stamina
//
// Measured in "wheel units": 1000 = one full wheel, 3000 = the three-wheel cap.
//
// Mind the flag names, which are actively misleading:
//
//   StaminaCurrentMax  -> CURRENT stamina (PlayerInfo +0x68). Reads "current
//                         max", but it is the live value that drains as you
//                         sprint and regenerates when you stop. Confirmed by
//                         sampling it while running: 3000 -> 1882 -> 3000.
//   StaminaMax         -> MAX stamina (PlayerInfo +0x6c). Constant while the
//                         above moves. This is what stamina vessels raise.
//
// The player actor keeps its own copy of the MAX at +0x1324, right after max
// life. recoverStamina() is setStaminaCurrentMax(getMaxStaminaFromPlayerActor())
// - current = max - which is the exact analogue of recoverLife(), and is the
// clearest confirmation of which is which.
// ---------------------------------------------------------------------------

namespace impl {

// PlayerInfo::setStaminaCurrentMax(this, f32) - CURRENT stamina; flag + cache +0x68
constexpr uintptr_t kSetStaminaWiiU = 0x02d49d70;
// PlayerInfo::getStaminaCurrentMax(this) -> f32 - CURRENT stamina
constexpr uintptr_t kGetStaminaWiiU = 0x02d49dc0;
// PlayerInfo::setStaminaMax(this, f32) - MAX stamina; flag + cache +0x6c
constexpr uintptr_t kSetStaminaMaxWiiU = 0x02d49dfc;
// PlayerInfo::getStaminaMax(this) -> f32 - MAX stamina
constexpr uintptr_t kGetStaminaMaxWiiU = 0x02d49e4c;
// PlayerInfo::setMaxStaminaForPlayerActor(this, f32) -> actor+0x1324 (the max)
constexpr uintptr_t kSetActorMaxStaminaWiiU = 0x02d49e88;
// PlayerInfo::getMaxStaminaFromPlayerActor(this) -> f32
constexpr uintptr_t kGetActorMaxStaminaWiiU = 0x02d49e9c;

using GetStaminaFn = float (*)(void* playerInfo);
using SetStaminaFn = void (*)(void* playerInfo, float value);

inline float CallStaminaGetter(uintptr_t wiiuOffset) {
#if !WIIXL_SWITCH
    void* playerInfo = PlayerInfo();
    if (!playerInfo) return 0.0f;
    auto fn = WiiXLaunch::GetTargetFunction<GetStaminaFn>(0x0, wiiuOffset);
    if (!fn) return 0.0f;
    return fn(playerInfo);
#else
    (void)wiiuOffset;
    return 0.0f;
#endif
}

} // namespace impl

static constexpr bool SupportsStamina = !WIIXL_SWITCH;

constexpr float kStaminaPerWheel = 1000.0f;
// What a fully upgraded player has, and where the HUD stops drawing - NOT an
// enforced limit. Used only as a fallback when the real max cannot be read.
constexpr float kStaminaMaxWheels = 3.0f;
constexpr float kStaminaAbsoluteMax = kStaminaPerWheel * kStaminaMaxWheels;

// Live stamina, 0..max. Drains and regenerates as you play.
inline float GetStamina() { return impl::CallStaminaGetter(impl::kGetStaminaWiiU); }

// Max stamina - what stamina vessels raise.
inline float GetMaxStamina() { return impl::CallStaminaGetter(impl::kGetStaminaMaxWiiU); }

// The player actor's own copy of the max (+0x1324). Should match GetMaxStamina();
// exposed so a disagreement is visible rather than mysterious.
inline float GetActorMaxStamina() { return impl::CallStaminaGetter(impl::kGetActorMaxStaminaWiiU); }

// Sets live stamina. No range limit - values above max or below zero are passed
// through as given. The only rejection is NaN, which is not a range check but a
// guard against handing the game a non-value.
inline bool SetStamina(float wheelUnits) {
#if !WIIXL_SWITCH
    if (wheelUnits != wheelUnits) return false;       // NaN

    void* playerInfo = impl::PlayerInfo();
    if (!playerInfo) return false;
    auto fn = WiiXLaunch::GetTargetFunction<impl::SetStaminaFn>(0x0, impl::kSetStaminaWiiU);
    if (!fn) return false;

    fn(playerInfo, wheelUnits);
    return true;
#else
    (void)wheelUnits;
    return false;
#endif
}

// Restores stamina to full - what the game's own recoverStamina() does. Reads
// the actual max rather than assuming three wheels, which matters now that
// SetMaxStamina will accept anything.
inline bool RecoverStamina() {
    float max = GetMaxStamina();
    if (!(max > 0.0f)) max = kStaminaAbsoluteMax;
    return SetStamina(max);
}

// Sets max stamina in both places the game keeps it: the StaminaMax flag (which
// persists to the save) and the player actor's copy. Does NOT touch current
// stamina - that is a separate value, and conflating them is what the flag
// naming tempts you into.
//
// No range limit, same as SetMaxLife - the three-wheel cap is a HUD limit, not
// a data one. NaN is still rejected. The flag write is persistent.
inline bool SetMaxStamina(float wheelUnits) {
#if !WIIXL_SWITCH
    if (wheelUnits != wheelUnits) return false;       // NaN

    void* playerInfo = impl::PlayerInfo();
    if (!playerInfo) return false;

    auto setMax      = WiiXLaunch::GetTargetFunction<impl::SetStaminaFn>(0x0, impl::kSetStaminaMaxWiiU);
    auto setActorMax = WiiXLaunch::GetTargetFunction<impl::SetStaminaFn>(0x0, impl::kSetActorMaxStaminaWiiU);
    if (!setMax || !setActorMax) return false;

    setMax(playerInfo, wheelUnits);
    // Safe with no player actor loaded: the game's own function null-checks
    // PlayerInfo::mPlayerActor (+0x30) and returns.
    setActorMax(playerInfo, wheelUnits);
    return true;
#else
    (void)wheelUnits;
    return false;
#endif
}

// Convenience in wheels (1.0 = one full wheel).
inline float GetStaminaWheels() { return GetStamina() / kStaminaPerWheel; }
inline bool SetStaminaWheels(float wheels) { return SetStamina(wheels * kStaminaPerWheel); }
inline float GetMaxStaminaWheels() { return GetMaxStamina() / kStaminaPerWheel; }
inline bool SetMaxStaminaWheels(float wheels) { return SetMaxStamina(wheels * kStaminaPerWheel); }


// ---------------------------------------------------------------------------
// Rupees
//
// Rupees are not a pouch item and have nothing to do with PauseMenuDataMgr -
// they are a plain scalar GameData flag named "CurrentRupee", which is why the
// pouch walk never turns them up.
//
// Chain, the same one used for the other flags here: the string at 0x101fb72c
// -> name accessor 0x02e1eb90 (a function whose whole body is `return
// "CurrentRupee";`) -> the wrappers below.

static constexpr bool SupportsRupees = !WIIXL_SWITCH;

namespace impl {

// gdt::increaseFlag_CurrentRupee(s32 delta, bool debug). NOT a setter, despite
// the shape: it forwards to 0x02e147a4, which pushes {tag, nameHash, index,
// delta} onto the per-thread ring buffer at manager+0x71c, and that queue holds
// DELTAS. Proven by 0x03204a14, the rupee pickup path: its queued branch pushes
// the pickup amount through the identical call, and its immediate branch reads
// the flag and stores current + amount. Calling this with an absolute value
// adds that value to the wallet - which reads as the total doubling if the
// caller passes the current amount back in.
constexpr uintptr_t kIncreaseFlagCurrentRupeeWiiU = 0x02e17f58;

// gdt::setFlag_s32_byName(Manager*, s32 value, sead::SafeString* name). The
// genuine absolute write, taken from the immediate branch of 0x03204a14, which
// calls it with the already-summed total.
constexpr uintptr_t kSetFlagS32ByNameWiiU = 0x0320400c;

// The generic by-name s32 read. There is no dedicated CurrentRupee getter to
// pair with the setter; this is how the game's own can-afford check at
// 0x02a771bc reads the flag, and the argument shape below is taken from it
// verbatim rather than reconstructed.
constexpr uintptr_t kGetFlagS32ByNameWiiU = 0x0320fadc;

// ksys::gdt::Manager singleton pointer.
constexpr uintptr_t kGameDataManagerPtrWiiU = 0x1046d5b0;

// sead::SafeString's vtable. A stack SafeString is built as { const char*,
// vtable } - string pointer first, vtable second. That is the same inverted
// order as PouchItem::mName, and it is worth not re-learning the hard way.
constexpr uintptr_t kSeadSafeStringVtableWiiU = 0x10263910;

struct SafeString {
    const char* text;
    const void* vtable;
};

using IncreaseFlagCurrentRupeeFn = void (*)(int delta, bool tag);
using SetFlagS32ByNameFn = void (*)(void* manager, int value, const SafeString* name);
using IncreaseFlagS32ByNameFn = void (*)(int delta, const SafeString* name, uint8_t tag);

// gdt::increaseFlag_s32_byName. 0x02e17f58 is the CurrentRupee-specific wrapper
// around this; called directly it reaches any s32 flag.
constexpr uintptr_t kIncreaseFlagS32ByNameWiiU = 0x02e147a4;
using GetFlagS32ByNameFn = int (*)(void* core, int* out, const SafeString* name,
                                   uint8_t flags, int one);

inline void* GameDataManager() {
#if !WIIXL_SWITCH
    void* mgr = *reinterpret_cast<void**>(kGameDataManagerPtrWiiU);
    uintptr_t addr = reinterpret_cast<uintptr_t>(mgr);
    if (addr < 0x10000000 || addr > 0xa0000000) return nullptr;
    return mgr;
#else
    return nullptr;
#endif
}

} // namespace impl

// Reads the CurrentRupee flag. False when there is no GameData manager yet (no
// save loaded) or the flag read itself fails, in which case out is untouched.
inline bool GetRupees(int& out) {
#if !WIIXL_SWITCH
    void* manager = impl::GameDataManager();
    if (!manager) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);

    // 0x02a771bc reads this as **(u32**)(mgr + 0x700): the word at mgr+0x700
    // points at a slot that in turn holds the object the read wants.
    uintptr_t* slot = *reinterpret_cast<uintptr_t**>(base + 0x700);
    if (!slot) return false;
    void* core = *reinterpret_cast<void**>(slot);
    if (!core) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::GetFlagS32ByNameFn>(
        0x0, impl::kGetFlagS32ByNameWiiU);
    if (!get) return false;

    impl::SafeString name = {
        "CurrentRupee",
        reinterpret_cast<const void*>(impl::kSeadSafeStringVtableWiiU),
    };

    int value = 0;
    if (get(core, &value, &name, *reinterpret_cast<uint8_t*>(base + 0x704), 1) == 0) {
        return false;
    }

    out = value;
    return true;
#else
    (void)out;
    return false;
#endif
}

// Adds to the wallet, the same operation a picked-up rupee performs. Negative
// deltas spend. This is the game's own primitive; SetRupees is layered on the
// absolute write instead.
// Defined further down, in the generic flag section. Rupees are just the
// best-known s32 flag, so the rupee helpers are thin wrappers over it rather
// than a second implementation.
inline bool AddFlagS32(const char* name, int delta);

inline bool AddRupees(int delta) {
    return AddFlagS32("CurrentRupee", delta);
}

// Writes the CurrentRupee flag to an absolute value.
//
// No range limit, for the same reason the rest of this header has none: the
// game clamps nothing here, and 999999 is a wallet display limit rather than a
// storage one. This persists to your save.
inline bool SetRupees(int value) {
#if !WIIXL_SWITCH
    void* manager = impl::GameDataManager();
    if (!manager) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetFlagS32ByNameFn>(
        0x0, impl::kSetFlagS32ByNameWiiU);
    if (!set) return false;

    impl::SafeString name = {
        "CurrentRupee",
        reinterpret_cast<const void*>(impl::kSeadSafeStringVtableWiiU),
    };

    set(manager, value, &name);
    return true;
#else
    (void)value;
    return false;
#endif
}


// ---------------------------------------------------------------------------
// Generic flag access
//
// Rupees turned out to be one instance of a general mechanism, so this exposes
// the mechanism. Everything below is s32 flags only - see the note on stores.
//
// How the store is shaped, from 0x0320fadc and the two halves it splits into:
//
//   manager             0x1046d5b0 (a pointer; null before a save loads)
//   core                **(void***)(manager + 0x700), with a u8 at +0x704
//                       that every read takes as an argument
//   s32 container       core + 0x10, laid out { u32 count; u32; void** entries }
//   entries[i]          a flag object; virtual slot +0x14 returns its name hash
//
// Names are NOT stored - 0x0320f45c CRC32s the name via 0x030c5d30 and
// 0x03208cd4 binary-searches the sorted hashes. So a flag can be read by name,
// but enumerating the store yields hashes, and turning one back into a name
// means matching it against a list of candidate names you already have.

static constexpr bool SupportsFlags = !WIIXL_SWITCH;

namespace impl {

// (container*, sead::SafeString* name, int) -> index, or -1 when absent.
constexpr uintptr_t kFindFlagIndexByNameWiiU = 0x0320f45c;

// (s32* out, container*, int index, u8 flags) -> non-zero on success. Reached
// through the stub at 0x0320f9b4, which just adds the 0x10 container offset and
// tail-calls; the argument order here is from that stub's register moves, not
// from Ghidra's parameter recovery, which gets it wrong.
constexpr uintptr_t kGetFlagS32ByIndexWiiU = 0x03234b08;

// The bool equivalents. The stub table is ordered like the containers, so the
// bool stubs (0x0320f994 and 0x0320f9a4, both adding 0x04 and branching to the
// same reader) sit immediately BEFORE the s32 one - which is why walking
// forward from s32 never finds them.
constexpr uintptr_t kGetFlagBoolByIndexWiiU = 0x03234a64;

// (core, out, sead::SafeString* name, u8 flags, int one) -> non-zero on success.
// Same shape as the s32 reader at 0x0320fadc; it just resolves the index
// against core+0x04 instead of core+0x10.
constexpr uintptr_t kGetFlagBoolByNameWiiU = 0x0320fa78;

// (Manager*, bool value, sead::SafeString* name) -> non-zero on success. Found
// through 0x02e147a4, which uses it to raise IsChangedByDebug. Structurally
// identical to the s32 setter 0x0320400c, down to the manager+0x730 bit 0x12
// guard and the mirrored write to the second core, differing only in the typed
// setter it forwards to (0x032163cc rather than 0x03216440).
constexpr uintptr_t kSetFlagBoolByNameWiiU = 0x032002d4;

// The typed bool setter 0x032002d4 forwards to, exposed directly so its last
// argument can be changed.
//
//   (core, bool value, sead::SafeString* name, u8 flags, int one, int force)
//
// That last argument is a latch bypass. Many story flags are declared
// IsOneTrigger in the ROM's gamedata - Clear_Dungeon000, Open_Dungeon000 and
// IsGet_PlayerStole2 all are - and once such a flag is true the ordinary setter
// will not clear it: 0x032354bc falls through to the guarded setter at vtable
// +0xa4, which refuses and returns 0. With force non-zero it takes the
// unconditional path at vtable +0xac instead. Every wrapper in the game passes
// 0 here; nothing in the binary ever forces.
constexpr uintptr_t kSetFlagBoolTypedWiiU = 0x032163cc;

// Manager fields 0x032002d4 reads, mirrored here because forcing means
// reimplementing that wrapper rather than calling it.
constexpr uintptr_t kManagerStatusBits = 0x730;   // bit 0x12 blocks all writes
constexpr uintptr_t kManagerWriteGate = 0x716;    // non-zero blocks bool writes
constexpr uintptr_t kManagerMirrorGate = 0x715;   // non-zero -> write both cores
constexpr uintptr_t kManagerFlagByte = 0x714;
constexpr uintptr_t kManagerBoolCore = 0x70c;
constexpr uintptr_t kManagerBoolCoreMirror = 0x710;

constexpr uintptr_t kS32ContainerOffset = 0x10;
constexpr uintptr_t kBoolContainerOffset = 0x04;

// The containers are 0x0c apart and the first is at core+0x04, not at the s32
// one. Confirmed against a live v208 save: all 18 stores in the canonical gdt
// type order, with bool at 0x04 (42025 entries) sitting BEFORE s32 at 0x10.
// Every non-bool count matches its ROM gamedata category exactly. The store
// after s32 is f32, not bool - a probe that only walks forward from the s32
// container misses the largest store in the game. Full table in
// data/symbols-wiiu-v208.csv.
constexpr uintptr_t kFirstContainerOffset = 0x04;
constexpr uintptr_t kContainerStride = 0x0c;
constexpr int kFlagStoreCount = 18;
constexpr uintptr_t kContainerCount = 0x00;
constexpr uintptr_t kContainerEntries = 0x08;

// Virtual slot returning a flag object's name hash, from the binary search in
// 0x03208cd4.
constexpr uintptr_t kFlagHashVtableSlot = 0x14;

// A store with more entries than this is a bad read rather than a big save.
constexpr int kMaxFlagsInStore = 100000;

using FindFlagIndexByNameFn = int (*)(void* container, const SafeString* name, int one);
using GetFlagS32ByIndexFn = int (*)(int* out, void* container, int index, uint8_t flags);

// bool reads land in a word that is deliberately over-sized and zero-filled:
// whether the game writes one byte or four, testing the whole word against zero
// gives the right answer on big-endian, where a single-byte store lands in the
// most significant byte.
using GetFlagBoolByIndexFn = int (*)(uint32_t* out, void* container, int index, uint8_t flags);
using GetFlagBoolByNameFn = int (*)(void* core, uint32_t* out, const SafeString* name,
                                    uint8_t flags, int one);
using SetFlagBoolByNameFn = int (*)(void* manager, int value, const SafeString* name);
using SetFlagBoolTypedFn = int (*)(void* core, int value, const SafeString* name,
                                   uint8_t flags, int one, int force);
using GetFlagHashFn = uint32_t (*)(void* flagObject);

// Sanity check for a pointer read out of game memory: catches null, small
// integers and obvious garbage without pretending to know the memory map.
//
// The upper bound was 0xa0000000 and that was wrong. gdt's bool write core,
// **(void***)(manager + 0x70c), measured live at 0xa0000238 - just past it - so
// the check silently rejected a pointer the game itself dereferences on every
// bool write, and any code guarding on it failed for a reason that looked
// nothing like a bad bound. Only the read core at manager+0x700 (0x676eac48 in
// the same session) fell inside the old range, which is why reads worked and
// writes did not.
inline bool Plausible(const void* p) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr >= 0x10000000 && addr < 0xf0000000;
}

// The manager, the flag core and the u8 every read wants, resolved together
// because no caller needs one without the others.
struct FlagAccess {
    void* manager;
    void* core;
    void* s32Container;
    uint8_t flags;
};

inline bool ResolveFlagAccess(FlagAccess& out) {
#if !WIIXL_SWITCH
    void* manager = GameDataManager();
    if (!manager) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    uintptr_t* slot = *reinterpret_cast<uintptr_t**>(base + 0x700);
    if (!Plausible(slot)) return false;

    void* core = *reinterpret_cast<void**>(slot);
    if (!Plausible(core)) return false;

    out.manager = manager;
    out.core = core;
    out.s32Container = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(core) + kS32ContainerOffset);
    out.flags = *reinterpret_cast<uint8_t*>(base + 0x704);
    return true;
#else
    (void)out;
    return false;
#endif
}

inline SafeString MakeSafeString(const char* text) {
    SafeString s = { text, reinterpret_cast<const void*>(kSeadSafeStringVtableWiiU) };
    return s;
}

// Resolves one store slot: validates the index against the container's count
// and hands back the container plus the flag object's name hash. Shared by
// every typed enumerator, which then only has to do its own read.
inline bool StoreSlot(uint32_t containerOffset, int index, FlagAccess& access,
                      void*& container, uint32_t& hash) {
#if !WIIXL_SWITCH
    if (index < 0 || !ResolveFlagAccess(access)) return false;

    uintptr_t at = reinterpret_cast<uintptr_t>(access.core) + containerOffset;

    int count = *reinterpret_cast<int*>(at + kContainerCount);
    if (count < 0 || count > kMaxFlagsInStore || index >= count) return false;

    void** entries = *reinterpret_cast<void***>(at + kContainerEntries);
    if (!Plausible(entries)) return false;

    void* object = entries[index];
    if (!Plausible(object)) return false;

    uintptr_t vtable = *reinterpret_cast<uintptr_t*>(object);
    if (!Plausible(reinterpret_cast<void*>(vtable))) return false;

    auto getHash = *reinterpret_cast<GetFlagHashFn*>(vtable + kFlagHashVtableSlot);
    if (!getHash) return false;

    container = reinterpret_cast<void*>(at);
    hash = getHash(object);
    return true;
#else
    (void)containerOffset; (void)index; (void)access; (void)container; (void)hash;
    return false;
#endif
}

// Entry count of one store, or 0 when unavailable.
inline int StoreCount(uint32_t containerOffset) {
#if !WIIXL_SWITCH
    FlagAccess access;
    if (!ResolveFlagAccess(access)) return 0;

    uintptr_t at = reinterpret_cast<uintptr_t>(access.core) + containerOffset;
    int count = *reinterpret_cast<int*>(at + kContainerCount);
    if (count < 0 || count > kMaxFlagsInStore) return 0;
    return count;
#else
    (void)containerOffset;
    return 0;
#endif
}

} // namespace impl

// Reads any s32 flag by name. False when there is no save loaded or the name is
// not an s32 flag - a bool or f32 flag of that name lives in a different store
// and will read as absent here rather than as the wrong type.
inline bool GetFlagS32(const char* name, int& out) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    if (!name || !impl::ResolveFlagAccess(access)) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::GetFlagS32ByNameFn>(
        0x0, impl::kGetFlagS32ByNameWiiU);
    if (!get) return false;

    impl::SafeString key = impl::MakeSafeString(name);
    int value = 0;
    if (get(access.core, &value, &key, access.flags, 1) == 0) return false;

    out = value;
    return true;
#else
    (void)name; (void)out;
    return false;
#endif
}

// Writes any s32 flag by name, absolutely.
inline bool SetFlagS32(const char* name, int value) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    if (!name || !impl::ResolveFlagAccess(access)) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetFlagS32ByNameFn>(
        0x0, impl::kSetFlagS32ByNameWiiU);
    if (!set) return false;

    impl::SafeString key = impl::MakeSafeString(name);
    set(access.manager, value, &key);
    return true;
#else
    (void)name; (void)value;
    return false;
#endif
}

// The game's deferred add: pushes the delta onto the per-thread queue at
// manager+0x71c, which drains on a later frame. The write lands, but it is NOT
// observable in a read taken straight afterwards, so a caller that reports the
// value back immediately will report the old one.
//
// AddFlagS32 is what you usually want. This is here because it is the game's
// own primitive and it is atomic against other writers, which the read-add-set
// alternative is not.
inline bool QueueFlagS32Delta(const char* name, int delta) {
#if !WIIXL_SWITCH
    if (!name) return false;

    auto add = WiiXLaunch::GetTargetFunction<impl::IncreaseFlagS32ByNameFn>(
        0x0, impl::kIncreaseFlagS32ByNameWiiU);
    if (!add) return false;

    impl::SafeString key = impl::MakeSafeString(name);
    add(delta, &key, 0);
    return true;
#else
    (void)name; (void)delta;
    return false;
#endif
}

// Adds to any s32 flag by name. Negative deltas subtract.
//
// Read, add, absolute store - which is exactly what 0x03204a14 does in its
// immediate branch when the delta queue is disabled. Chosen over the queued
// primitive so the result is observable straight away; queueing leaves a reader
// looking at the pre-write value for a frame, which makes any API that echoes
// the value back a liar. The tradeoff is that this is not atomic against
// another writer touching the same flag in the same frame - see
// QueueFlagS32Delta if that matters more than immediacy.
inline bool AddFlagS32(const char* name, int delta) {
#if !WIIXL_SWITCH
    int current = 0;
    if (!GetFlagS32(name, current)) return false;
    return SetFlagS32(name, current + delta);
#else
    (void)name; (void)delta;
    return false;
#endif
}

// How many s32 flags the save holds. 0 when unavailable, which is also a
// legitimate-looking answer, so check SupportsFlags and a known flag first if
// the distinction matters.
inline int FlagS32Count() { return impl::StoreCount(impl::kS32ContainerOffset); }

// How many bool flags the save holds - by far the largest store, around 42000.
inline int FlagBoolCount() { return impl::StoreCount(impl::kBoolContainerOffset); }

// One s32 flag by store index, giving its name hash and value. Enumeration is
// hash-only by necessity: the store does not keep names.
inline bool GetFlagS32ByIndex(int index, uint32_t& hash, int& value) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    void* container = nullptr;
    uint32_t foundHash = 0;
    if (!impl::StoreSlot(impl::kS32ContainerOffset, index, access, container, foundHash)) {
        return false;
    }

    auto read = WiiXLaunch::GetTargetFunction<impl::GetFlagS32ByIndexFn>(
        0x0, impl::kGetFlagS32ByIndexWiiU);
    if (!read) return false;

    int readValue = 0;
    if (read(&readValue, container, index, access.flags) == 0) return false;

    hash = foundHash;
    value = readValue;
    return true;
#else
    (void)index; (void)hash; (void)value;
    return false;
#endif
}

// Reads any bool flag by name - the store holding every IsGet_*, shrine, Korok
// and story flag.
inline bool GetFlagBool(const char* name, bool& out) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    if (!name || !impl::ResolveFlagAccess(access)) return false;

    auto get = WiiXLaunch::GetTargetFunction<impl::GetFlagBoolByNameFn>(
        0x0, impl::kGetFlagBoolByNameWiiU);
    if (!get) return false;

    impl::SafeString key = impl::MakeSafeString(name);
    uint32_t value = 0;
    if (get(access.core, &value, &key, access.flags, 1) == 0) return false;

    out = value != 0;
    return true;
#else
    (void)name; (void)out;
    return false;
#endif
}

// Writes any bool flag by name.
inline bool SetFlagBool(const char* name, bool value) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    if (!name || !impl::ResolveFlagAccess(access)) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetFlagBoolByNameFn>(
        0x0, impl::kSetFlagBoolByNameWiiU);
    if (!set) return false;

    impl::SafeString key = impl::MakeSafeString(name);
    return set(access.manager, value ? 1 : 0, &key) != 0;
#else
    (void)name; (void)value;
    return false;
#endif
}

// Clears or sets a bool flag past the two things that normally stop it.
//
// There are TWO independent refusals, and they come from different flag
// properties declared in the ROM's gamedata:
//
//   IsOneTrigger      the flag latches; once true the guarded setter at vtable
//                     +0xa4 refuses to clear it. Bypassed by the force argument
//                     to 0x032354bc.
//   IsProgramWritable false means the flag is meant to be driven only by the
//                     event and quest system, never by a program write. This is
//                     checked earlier, in the `if (param_5 != 0)` block at the
//                     top of 0x032354bc, where param_5 is the manager's flag
//                     byte from +0x714: it calls vtable +0x34 then 0x0324d1d0
//                     and bails returning 0 when that says no. Passing 0 for
//                     that argument skips the block entirely.
//
// bypassPermission drives the second. Nearly every quest flag needs it -
// measured, only 9 of 103 Divine Beast quest flags are program-writable, while
// all 25 of the beast state flags are - so without it a quest reset mostly
// fails while looking like a latch problem.
//
// This is a faithful copy of 0x032002d4 - same status-bit guard, same write
// gate, same mirrored second write - with exactly one argument changed. It is
// separate from SetFlagBool rather than a parameter on it because latching is
// the game's own rule about its own save data: the default path should respect
// it, and stepping outside should have to be asked for.
//
// Forcing a story flag backwards can leave a quest in a state its event flow
// never produces, which the game has no reason to be able to recover from.
// Back up the save first.
inline bool SetFlagBoolForced(const char* name, bool value, bool bypassPermission = false) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    if (!name || !impl::ResolveFlagAccess(access)) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(access.manager);

    if ((*reinterpret_cast<uint32_t*>(base + impl::kManagerStatusBits) >> 0x12) & 1) return false;
    if (*reinterpret_cast<char*>(base + impl::kManagerWriteGate) != 0) return false;

    auto set = WiiXLaunch::GetTargetFunction<impl::SetFlagBoolTypedFn>(
        0x0, impl::kSetFlagBoolTypedWiiU);
    if (!set) return false;

    uintptr_t* slot = *reinterpret_cast<uintptr_t**>(base + impl::kManagerBoolCore);
    if (!impl::Plausible(slot)) return false;
    void* core = *reinterpret_cast<void**>(slot);
    if (!impl::Plausible(core)) return false;

    impl::SafeString key = impl::MakeSafeString(name);

    // Zero here is what skips the writability check; the manager's real byte
    // (1 in practice) is what enables it.
    const uint8_t flags = bypassPermission
                        ? 0
                        : *reinterpret_cast<uint8_t*>(base + impl::kManagerFlagByte);
    const int written = value ? 1 : 0;

    if (set(core, written, &key, flags, 1, 1) == 0) return false;

    // The wrapper mirrors into the second core when this gate is set; skipping
    // it would leave the two disagreeing.
    if (*reinterpret_cast<char*>(base + impl::kManagerMirrorGate) != 0) {
        uintptr_t* mirrorSlot = *reinterpret_cast<uintptr_t**>(base + impl::kManagerBoolCoreMirror);
        if (impl::Plausible(mirrorSlot)) {
            void* mirror = *reinterpret_cast<void**>(mirrorSlot);
            if (impl::Plausible(mirror)) set(mirror, written, &key, flags, 1, 1);
        }
    }

    return true;
#else
    (void)name; (void)value;
    return false;
#endif
}

// Raw view of the manager fields both write paths depend on. Exists because a
// forced write can fail for several indistinguishable reasons - a status bit, a
// gate byte, or a core pointer that does not resolve - and guessing between
// them costs a rebuild and a game reload each time.
struct FlagDebug {
    uintptr_t manager;
    uint32_t status;      // +0x730, bit 0x12 blocks writes
    uint8_t flagByte;     // +0x714, passed to every typed call
    uint8_t mirrorGate;   // +0x715
    uint8_t writeGate;    // +0x716
    uintptr_t slot[4];    // raw words at +0x6fc, +0x700, +0x70c, +0x710
    uintptr_t core[4];    // those dereferenced once more, 0 if implausible
};

inline bool GetFlagDebug(FlagDebug& out) {
#if !WIIXL_SWITCH
    void* manager = impl::GameDataManager();
    if (!manager) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(manager);
    out.manager = base;
    out.status = *reinterpret_cast<uint32_t*>(base + impl::kManagerStatusBits);
    out.flagByte = *reinterpret_cast<uint8_t*>(base + impl::kManagerFlagByte);
    out.mirrorGate = *reinterpret_cast<uint8_t*>(base + impl::kManagerMirrorGate);
    out.writeGate = *reinterpret_cast<uint8_t*>(base + impl::kManagerWriteGate);

    static const uintptr_t kOffsets[4] = { 0x6fc, 0x700, 0x70c, 0x710 };
    for (int i = 0; i < 4; ++i) {
        uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + kOffsets[i]);
        out.slot[i] = slot;
        out.core[i] = impl::Plausible(reinterpret_cast<void*>(slot))
                    ? *reinterpret_cast<uintptr_t*>(slot)
                    : 0;
    }
    return true;
#else
    (void)out;
    return false;
#endif
}

// One bool flag by store index, giving its name hash and value.
inline bool GetFlagBoolByIndex(int index, uint32_t& hash, bool& value) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    void* container = nullptr;
    uint32_t foundHash = 0;
    if (!impl::StoreSlot(impl::kBoolContainerOffset, index, access, container, foundHash)) {
        return false;
    }

    auto read = WiiXLaunch::GetTargetFunction<impl::GetFlagBoolByIndexFn>(
        0x0, impl::kGetFlagBoolByIndexWiiU);
    if (!read) return false;

    uint32_t readValue = 0;
    if (read(&readValue, container, index, access.flags) == 0) return false;

    hash = foundHash;
    value = readValue != 0;
    return true;
#else
    (void)index; (void)hash; (void)value;
    return false;
#endif
}

// Walks the flag stores, reporting each one's offset and entry count. Store 0
// is bool at core+0x04 and store 1 is the s32 one at core+0x10.
//
// Returns false once the offset stops looking like a container, or past the end
// of the type list.
inline bool GetFlagStoreCount(int storeIndex, uint32_t& offset, int& count) {
#if !WIIXL_SWITCH
    impl::FlagAccess access;
    if (storeIndex < 0 || storeIndex >= impl::kFlagStoreCount ||
        !impl::ResolveFlagAccess(access)) return false;

    const uint32_t at = impl::kFirstContainerOffset +
                        static_cast<uint32_t>(storeIndex) * impl::kContainerStride;
    uintptr_t container = reinterpret_cast<uintptr_t>(access.core) + at;

    int found = *reinterpret_cast<int*>(container + impl::kContainerCount);
    if (found < 0 || found > impl::kMaxFlagsInStore) return false;

    void** entries = *reinterpret_cast<void***>(container + impl::kContainerEntries);
    if (found > 0 && !impl::Plausible(entries)) return false;

    offset = at;
    count = found;
    return true;
#else
    (void)storeIndex; (void)offset; (void)count;
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::GameData
