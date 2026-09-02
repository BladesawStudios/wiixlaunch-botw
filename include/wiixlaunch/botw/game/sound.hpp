#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstdint>

#include "../platform/log.hpp"

// WiiXLaunch::BotW::Sound - play the game's own sound events by name.
//
// BotW has a single "play this event" entry point that everything from the
// menus to the keyboard goes through, and it takes the event's NAME rather
// than an id, so a mod can use any sound the game has loaded without knowing
// anything about the audio system.
//
// HOW IT WAS FOUND (v208): the string "TitleCursorMove" is passed to
// 0x0359BD24, which forwards to 0x0359A950 with a manager fetched from
// 0x1047C390+0x20; that resolves the name and returns a handle. The clinching
// evidence is 0x02EC9C5C, the software keyboard's sound dispatcher, which is
// a switch that calls 0x0359BD24 seven times with names like "mc_Key_Decide"
// and "mc_ExitKey_Decide" - the calling convention is unambiguous there.
//
// The argument is a sead string object, and its layout is the surprise: the
// CHARACTER POINTER COMES FIRST and the vtable second. 0x0359A950 reads the
// text as *param and calls a virtual through param[1], so it cannot be the
// other way round. The vtable every caller uses is 0x10263910.
//
// ---------------------------------------------------------------------------
// WHAT IS KNOWN, AND WHAT IS NOT (tested in Cemu against v208)
// ---------------------------------------------------------------------------
// Works: the call reaches the game. The manager globals resolve, Available()
// passes, and the emit path runs - 0x0359BD24 -> 0x0359A950 -> 0x03B91130 ->
// 0x03BA0660, the game's own "searchAndEmit(%s)". The string object matches
// the game's callers word for word; 0x02D11534 builds an identical one.
//
// Does NOT work: almost no event name resolves. "TitleCursorMove" and
// "TitleCursorDecide" return a valid handle and make a sound - but not the
// sound the title screen actually makes. "mc_CursorMove", "mc_Decide",
// "mc_List_FocusMove" and even "mc_CallHorse" all fail to resolve, and that
// last one is a name THE GAME ITSELF passes to this same function from
// 0x02D11534. So it is not the names and it is not the call.
//
// The hypothesis worth testing next, not a conclusion: *(0x1047C390 + 0x20)
// may be a system- or title-scoped sound manager rather than the one holding
// gameplay and menu events, and the game may select a different one at
// runtime even though its code reads the same global. Whoever picks this up
// should start by finding what resource set that manager actually holds, or
// by breaking on 0x03BA1F30 (the name lookup) while the game plays a menu
// sound of its own and seeing which manager it arrives with.
//
// RESOLVED: the call was always correct - the names were wrong. The manager
// searches one small system-scoped resource of 51 events, and none of the
// menu names guessed from the binary are in it. DumpEvents() below prints
// what is, which is how the working names were found. See Events:: above.

namespace WiiXLaunch::BotW::Sound {

#if !WIIXL_SWITCH

namespace impl {

// v208 Wii U / Cemu.
constexpr uintptr_t kPlayEventAddress = 0x0359BD24;   // (SafeString*, u64* outHandle)
constexpr uintptr_t kSafeStringVtable = 0x10263910;
constexpr uintptr_t kManagerRootAddress = 0x1047C390;  // +0x20 is what the call needs

// Text first, vtable second - see the note above. Laid out by hand rather
// than constructed, because nothing here can call the game's constructor.
struct SafeString {
    const char* text;
    const void* vtable;
};

using FnPlayEvent = void (*)(const void* name, void* outHandle);

// The inner call the wrapper forwards to. Worth using directly: it RETURNS
// whether the event resolved to a playing handle, which the wrapper throws
// away - and "did this name resolve" is the only question that matters when a
// menu is silent.
constexpr uintptr_t kPlayEventInnerAddress = 0x0359A950;
using FnPlayEventInner = uint32_t (*)(const void* mgr, const void* name, uint64_t* outHandle);

inline const void* Manager() {
    const uint32_t root = *reinterpret_cast<const volatile uint32_t*>(kManagerRootAddress);
    if (root == 0) return nullptr;
    const uint32_t mgr = *reinterpret_cast<const volatile uint32_t*>(root + 0x20);
    return reinterpret_cast<const void*>(mgr);
}

} // namespace impl

// Names that ACTUALLY RESOLVE, read out of the game with DumpEvents rather
// than guessed from strings in the binary. That distinction cost a lot of
// time: the .rodata is full of plausible names like "mc_CursorMove" and
// "mc_Decide" that are not in the resource this manager searches, and even
// "mc_CallHorse" - which the game itself passes to this very function from
// 0x02D11534 - misses.
//
// What the manager at *(0x1047C390)+0x20 actually holds is a small
// system-scoped set of 51 events, present both on the title screen and in
// game: Amiibo, map markers, the title cursor, the software keyboard, heart
// and stamina upgrades, priest voices, screen changes. No general menu
// cursor sound is among them.
//
// So the menu uses the KEYBOARD's sounds. They are crisp, they are UI sounds
// rather than gameplay ones, and they are always loaded. Run DumpEvents() to
// see the whole list if you want something else.
namespace Events {
constexpr const char* kCursorMove  = "mc_Key_On";        // keyboard key press
constexpr const char* kDecide      = "mc_Key_Decide";    // keyboard confirm
constexpr const char* kTitleMove   = "TitleCursorMove";
constexpr const char* kTitleDecide = "TitleCursorDecide";
constexpr const char* kScreenIn    = "ChangeScreen_A";
constexpr const char* kScreenOut   = "ChangeScreen_B";
constexpr const char* kGameStart   = "GameStart";
} // namespace Events

// True when the sound system is up enough to be called. Worth checking rather
// than assuming: the play function dereferences a global manager, so calling
// it before the game has built one is a null dereference, not a silent no-op.
inline bool Available() {
    const uint32_t root = *reinterpret_cast<const volatile uint32_t*>(impl::kManagerRootAddress);
    if (root == 0) return false;
    const uint32_t mgr = *reinterpret_cast<const volatile uint32_t*>(root + 0x20);
    return mgr != 0;
}

// Play one of the game's sound events. Returns false if the name was empty or
// the sound system is not up; true only means the request was made - whether
// anything is audible depends on the event being in a sound archive that is
// currently loaded, which is why a menu event can be silent on the title
// screen and fine in game.
inline bool Play(const char* eventName) {
    if (!eventName || !eventName[0]) return false;
    const void* mgr = impl::Manager();
    if (!mgr) return false;
    if (*reinterpret_cast<const volatile uint32_t*>(reinterpret_cast<uintptr_t>(mgr) + 0x10) == 0) return false;
    impl::SafeString name{eventName, reinterpret_cast<const void*>(impl::kSafeStringVtable)};
    uint64_t handle = 0;
    auto fn = reinterpret_cast<impl::FnPlayEventInner>(impl::kPlayEventInnerAddress);
    // Unlike the wrapper at 0x0359BD24 this reports whether the name resolved,
    // so a caller can tell "not loaded" from "played".
    return fn(mgr, &name, &handle) != 0;
}

// ---------------------------------------------------------------------------
// Enumerating what is actually loaded
// ---------------------------------------------------------------------------
// Guessing event names was a dead end, so this walks the table the game itself
// searches and prints what is in it.
//
// The structure, read out of 0x03BA1E08 (the lookup, a binary search):
//
//   container = *(*(subsystem + 0x18) + 0xc)   where subsystem = *(manager + 0x10)
//   selector  = *(container + 4)               chooses which resource slot
//   slots     = container + 8                  an array of resource pointers
//   entry     = selector < 2 ? slots[selector] : slots[0]
//   ready     = *(entry + 0x5c)                non-zero when it can be searched
//   count     = *(*(entry + 4) + 8)            how many names
//   order     = *(entry + 0x10)                u16 index table, sorted by name
//   records   = *(entry + 0x14)                0x20 bytes each, [0] is the char*
//
// Only ONE slot is ever searched, which is the likely reason most names miss:
// the event wanted is in the other resource. Both are dumped here.

namespace impl {

inline bool Readable(uint32_t addr) {
    // Cemu maps the game's data high; anything outside this is not a pointer
    // we should follow. Cheap, and it keeps a wrong guess from crashing.
    return addr >= 0x10000000u && addr < 0xF0000000u;
}

inline void DumpSlot(uint32_t entry, uint32_t slot, uint32_t maxNames) {
    if (!Readable(entry)) { OSLog("  slot %u: empty\n", slot); return; }
    const uint8_t ready = *reinterpret_cast<const volatile uint8_t*>(entry + 0x5c);
    const uint32_t header = *reinterpret_cast<const volatile uint32_t*>(entry + 4);
    const uint32_t order = *reinterpret_cast<const volatile uint32_t*>(entry + 0x10);
    const uint32_t records = *reinterpret_cast<const volatile uint32_t*>(entry + 0x14);
    if (!Readable(header) || !Readable(order) || !Readable(records)) {
        OSLog("  slot %u: entry %p ready=%u but tables unreadable\n", slot,
              reinterpret_cast<void*>(entry), ready);
        return;
    }
    const uint32_t count = *reinterpret_cast<const volatile uint32_t*>(header + 8);
    OSLog("  slot %u: entry %p ready=%u, %u events\n", slot,
          reinterpret_cast<void*>(entry), ready, count);
    if (count == 0 || count > 20000) return;
    const uint32_t show = count < maxNames ? count : maxNames;
    for (uint32_t i = 0; i < show; ++i) {
        const uint16_t idx = *reinterpret_cast<const volatile uint16_t*>(order + i * 2);
        const uint32_t rec = records + static_cast<uint32_t>(idx) * 0x20;
        if (!Readable(rec)) continue;
        const uint32_t name = *reinterpret_cast<const volatile uint32_t*>(rec);
        if (!Readable(name)) continue;
        OSLog("    %s\n", reinterpret_cast<const char*>(name));
    }
    if (show < count) OSLog("    ... %u more\n", count - show);
}

} // namespace impl

// Print the sound events the game can currently resolve. `maxNames` caps how
// many per slot so the log stays usable; pass a substring in `filter` to print
// only matching names, which is how to find "the menu cursor one" without
// dumping thousands.
inline void DumpEvents(uint32_t maxNames = 60) {
    const void* mgrv = impl::Manager();
    if (!mgrv) { OSLog("WiiXLaunch Sound: no manager\n"); return; }
    const uint32_t mgr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mgrv));
    const uint32_t sub = *reinterpret_cast<const volatile uint32_t*>(mgr + 0x10);
    if (!impl::Readable(sub)) { OSLog("WiiXLaunch Sound: no subsystem\n"); return; }
    const uint32_t a = *reinterpret_cast<const volatile uint32_t*>(sub + 0x18);
    if (!impl::Readable(a)) { OSLog("WiiXLaunch Sound: no resource holder\n"); return; }
    const uint32_t container = *reinterpret_cast<const volatile uint32_t*>(a + 0xc);
    if (!impl::Readable(container)) { OSLog("WiiXLaunch Sound: no container\n"); return; }
    const uint32_t selector = *reinterpret_cast<const volatile uint32_t*>(container + 4);
    OSLog("WiiXLaunch Sound: container %p selector=%u (only this slot is searched)\n",
          reinterpret_cast<void*>(container), selector);
    for (uint32_t slot = 0; slot < 2; ++slot) {
        const uint32_t entry = *reinterpret_cast<const volatile uint32_t*>(container + 8 + slot * 4);
        impl::DumpSlot(entry, slot, maxNames);
    }
}

// Play the first name in `candidates` that resolves, and report which. For
// sounds whose event lives in a group that is only loaded on some screens -
// the title screen has its own cursor events, the menus another set - so a
// single name is not enough to be audible everywhere.
inline const char* PlayFirstAvailable(const char* const* candidates, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (candidates[i] && candidates[i][0] && Play(candidates[i])) return candidates[i];
    }
    return nullptr;
}

#else

namespace Events {
constexpr const char* kCursorMove = "";
constexpr const char* kDecide     = "";
constexpr const char* kCancel     = "";
constexpr const char* kUnable     = "";
constexpr const char* kOpen       = "";
constexpr const char* kClose      = "";
} // namespace Events

inline bool Available() { return false; }
inline bool Play(const char*) { return false; }
inline const char* PlayFirstAvailable(const char* const*, uint32_t) { return nullptr; }
inline void DumpEvents(uint32_t = 60) {}

#endif

} // namespace WiiXLaunch::BotW::Sound
