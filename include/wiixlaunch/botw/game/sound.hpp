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

// The event names a menu needs. These are the game's own, and they are the
// generic ones rather than a particular screen's: 0x02EC9C5C shows the
// keyboard using mc_Key_Decide, but mc_Decide and mc_CursorMove are what the
// ordinary menus use.
namespace Events {
constexpr const char* kCursorMove = "mc_CursorMove";
constexpr const char* kDecide     = "mc_Decide";
constexpr const char* kCancel     = "mc_Individual_Cancel";
constexpr const char* kUnable     = "mc_DoUnable";
constexpr const char* kOpen       = "mc_Open";
constexpr const char* kClose      = "mc_Close";
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

#endif

} // namespace WiiXLaunch::BotW::Sound
