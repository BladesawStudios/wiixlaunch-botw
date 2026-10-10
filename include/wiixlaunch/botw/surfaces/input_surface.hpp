#pragma once

// botw.input v1 - reading the controller, and pressing buttons the player did
// not press.
//
// ---------------------------------------------------------------------------
// INJECTION IS THE DANGEROUS HALF, and it is worth saying why it is here at all
// rather than being refused.
//
// A mod that can synthesise input can do anything the player can do, which is
// a great deal - and unlike a hook or a patch, nothing about it is visible in
// the boot log after the fact. So the host reports it: RegisterTick and
// InstallHook already name their owner, and Hold/Send here are attributed the
// same way, because "the game pressed A on its own" should have a module's name
// next to it rather than being a mystery.
//
// Buttons cross as a MASK, not an enum value. The module's Button enum is a
// set of individual buttons and a caller almost always wants several at once;
// handing over one at a time would mean a call per button and a different
// answer depending on the order they arrived in.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/botw/game/controller.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::InputSurface {

constexpr const char* kName = "botw.input";
constexpr uint16_t kVersionMajor = 1;
// 1.1 appends RegisterFrame and the two calls that report it. Appending bumps
// the MINOR, so every mod built against v1.0 still resolves.
constexpr uint16_t kVersionMinor = 1;

namespace impl {

// --- the per-frame callback -------------------------------------------------
//
// Controller::OnFrame is ONE SLOT and "call again to replace it", so two mods
// asking for it means the second silently unhooks the first. This fans it out
// the way botw.gfx and botw.gui fan out their single backend callbacks:
// N attributed slots, one registration with the module.
//
// It matters more here than it looks. This callback runs from the input read
// the game performs EVERY frame - on the title screen, during loads, in menus -
// whereas botw.player's tick stops the moment there is no player actor. A
// server pumped from the player tick is a server that dies at the title screen,
// which is exactly when you need it to undo whatever left you there, including
// releasing a button it is holding down.
constexpr uint32_t kMaxFrameCallbacks = 8;
constexpr uint32_t kOwnerLen = 24;

using ModFrameFn = void (*)();

struct FrameEntry {
    ModFrameFn fn = nullptr;
    char owner[kOwnerLen] = {0};
    uint32_t calls = 0;
    bool inUse = false;
};

inline FrameEntry g_Frames[kMaxFrameCallbacks];
inline uint32_t g_FrameCount = 0;
inline bool g_FrameHooked = false;

// Written BEFORE each call and cleared after, so a frame that never returns
// names the module it was inside. A frozen sequence means a hang inside a
// callback; an advancing one means the game stopped calling us. Those look
// identical from outside and are completely different problems.
struct FrameInFlight {
    uint32_t magic;
    uint32_t sequence;
    uint32_t depth;
    char owner[kOwnerLen];
};

constexpr uint32_t kFrameMagic = 0x57584946u;   // 'WXIF'
inline FrameInFlight g_FrameInFlight = { kFrameMagic, 0, 0, {0} };

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kOwnerLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline bool SameOwner(const char* a, const char* b) {
    for (uint32_t i = 0; i < kOwnerLen; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

// The one callback the module sees.
inline void DispatchFrame() {
    for (uint32_t i = 0; i < g_FrameCount; ++i) {
        FrameEntry& e = g_Frames[i];
        if (!e.inUse || !e.fn) continue;

        g_FrameInFlight.sequence++;
        g_FrameInFlight.depth++;
        CopyOwner(g_FrameInFlight.owner, e.owner);
        WiiXLaunch::ModContext::SetCurrent(e.owner);

        e.fn();

        WiiXLaunch::ModContext::SetCurrent(nullptr);
        g_FrameInFlight.depth--;
        g_FrameInFlight.owner[0] = '\0';
        e.calls++;
    }
}

// "Until released", as a value this surface owns.
//
// Controller::HoldIndefinitely exists only in the module's Wii U/Cemu branch,
// and reaching for it directly broke the Switch build. `if constexpr
// (!SupportsInjection) return 0;` reads like a guard and is not one: in a
// non-template function BOTH branches still have to COMPILE. if constexpr picks
// which one runs, not which one has to be well-formed. That is the same shape
// as the optimizer rule in docs/modules.md - something that looks like it
// protects the code below it and does not - and it was caught only because
// every change here builds all three targets.
#if !WIIXL_SWITCH
constexpr uint32_t kHoldForever = Controller::HoldIndefinitely;
#else
constexpr uint32_t kHoldForever = 0xFFFFFFFFu;
#endif

// Who has synthesised input this session, said once each.
//
// Not a permission check - a mod that got this far is a mod the user installed.
// It is attribution, for the same reason the raw-pointer escape hatch is
// logged: from outside, a game pressing its own buttons is indistinguishable
// from a broken controller, and the log is the only place that difference can
// be recorded.
constexpr uint32_t kMaxNoted = 8;
inline char g_Noted[kMaxNoted][17];
inline uint32_t g_NotedCount = 0;

inline void NoteInjectorOnce() {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') owner = "<host>";

    for (uint32_t i = 0; i < g_NotedCount; ++i) {
        bool same = true;
        for (uint32_t c = 0; c < 17; ++c) {
            if (g_Noted[i][c] != owner[c]) { same = false; break; }
            if (owner[c] == '\0') break;
        }
        if (same) return;
    }

    if (g_NotedCount < kMaxNoted) {
        uint32_t i = 0;
        for (; i + 1 < 17 && owner[i]; ++i) g_Noted[g_NotedCount][i] = owner[i];
        g_Noted[g_NotedCount][i] = '\0';
        ++g_NotedCount;
    }

    WIIXL_LOG("botw.input: %s is SYNTHESISING CONTROLLER INPUT - the game will "
              "see button presses the player did not make", owner);
}

// --- capability ------------------------------------------------------------

extern "C" inline uint32_t ISupportsInjection() {
    return Controller::SupportsInjection ? 1u : 0u;
}

// Installs the controller read hooks. Idempotent in the module, for the same
// reason Player::Init is: several mods may each need it and none can see that
// another already did.
extern "C" inline uint32_t IInit() {
    Controller::Init();
    return 1;
}

// --- reading ---------------------------------------------------------------

// The buttons held this frame, VPAD and KPAD merged. MaskFor turns one Button
// enumerator into its bit, so a mod tests `held & MaskFor(kA)` rather than
// keeping its own copy of the bit layout - the sort of table that stops
// matching without anyone noticing. Read through the module's
// own merged state rather than one source: both hooks fire every frame, and
// taking either alone means an idle GamePad zeroes a Pro Controller's input.
// Registers a per-frame callback. Init() first, because registering into a
// dispatcher nothing calls is the failure this exists to prevent.
extern "C" inline uint32_t IRegisterFrame(impl::ModFrameFn fn) {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') {
        WIIXL_LOG("botw.input: refused - a frame callback belongs to a module "
                  "and none is running");
        return 0;
    }
    if (!fn) {
        WIIXL_LOG("botw.input: %s passed a null frame callback", owner);
        return 0;
    }
    for (uint32_t i = 0; i < impl::g_FrameCount; ++i) {
        if (impl::SameOwner(impl::g_Frames[i].owner, owner)) {
            WIIXL_LOG("botw.input: %s already has a frame callback - do what you "
                      "need from the one you have", owner);
            return 0;
        }
    }
    if (impl::g_FrameCount >= impl::kMaxFrameCallbacks) {
        WIIXL_LOG("botw.input: %s refused - all %u frame slots are taken",
                  owner, impl::kMaxFrameCallbacks);
        return 0;
    }

    IInit();
    if (!impl::g_FrameHooked) {
        impl::g_FrameHooked = true;
        Controller::OnFrame(&impl::DispatchFrame);
        WIIXL_LOG("botw.input: one frame callback registered with the module; "
                  "modules are dispatched from it, attributed");
    }

    impl::FrameEntry& e = impl::g_Frames[impl::g_FrameCount++];
    e.fn = fn;
    e.calls = 0;
    e.inUse = true;
    impl::CopyOwner(e.owner, owner);

    WIIXL_LOG("botw.input: %s registered a frame callback (%u of %u)",
              e.owner, impl::g_FrameCount, impl::kMaxFrameCallbacks);
    return 1;
}

extern "C" inline uint32_t IFrameCallbackCount() { return impl::g_FrameCount; }

// What the frame dispatcher has been doing. Reported at the load point the same
// way PlayerTick is.
extern "C" inline void ILogFrameState() {
    if (impl::g_FrameCount == 0) {
        WIIXL_LOG("botw.input: no module registered a frame callback");
        return;
    }
    WIIXL_LOG("botw.input: %u module(s) registered a frame callback, driven by "
              "the game's own input read (survives the title screen)",
              impl::g_FrameCount);
    for (uint32_t i = 0; i < impl::g_FrameCount; ++i) {
        WIIXL_LOG("botw.input:   %u. %s", i + 1, impl::g_Frames[i].owner);
    }
    if (impl::g_FrameInFlight.depth) {
        WIIXL_LOG("botw.input: IN FLIGHT inside %s at sequence %u - if this line "
                  "repeats with the same sequence, that module is not returning",
                  impl::g_FrameInFlight.owner, impl::g_FrameInFlight.sequence);
    }
}

extern "C" inline uint32_t IHeldButtons() {
    return WiiXLaunch::BotW::impl::StateRef().hold;
}

extern "C" inline uint32_t IMaskFor(int32_t button) {
    return Controller::MaskFor(static_cast<Button>(button));
}

extern "C" inline uint32_t IIsPressed(int32_t button) {
    return Controller::IsPressed(static_cast<Button>(button)) ? 1u : 0u;
}

// Sticks through a float[2]: x, y. Two calls rather than one taking four
// pointers, because "which stick" is a decision the call site should show.
extern "C" inline uint32_t IGetLeftStick(float* out2) {
    if (!out2) return 0;
    float x = 0.f, y = 0.f;
    Controller::GetLeftStick(x, y);
    out2[0] = x; out2[1] = y;
    return 1;
}

extern "C" inline uint32_t IGetRightStick(float* out2) {
    if (!out2) return 0;
    float x = 0.f, y = 0.f;
    Controller::GetRightStick(x, y);
    out2[0] = x; out2[1] = y;
    return 1;
}

// --- injection -------------------------------------------------------------

// Holds a mask for a number of frames, then releases on its own. A frame count
// rather than an open-ended hold, because a mod that sets a button and crashes
// should not leave the game holding it forever.
extern "C" inline uint32_t IHold(uint32_t buttonMask, uint32_t frames) {
    if constexpr (!Controller::SupportsInjection) {
        (void)buttonMask; (void)frames;
        return 0;
    }
    NoteInjectorOnce();
    Controller::Hold(buttonMask, frames);
    return 1;
}

// Indefinite, and deliberately its own symbol. "Press A for 8 frames" and
// "press A until I say stop" have very different failure modes, and a mod
// choosing the second should have written the second.
extern "C" inline uint32_t IHoldIndefinitely(uint32_t buttonMask) {
    if constexpr (!Controller::SupportsInjection) {
        (void)buttonMask;
        return 0;
    }
    NoteInjectorOnce();
    Controller::Hold(buttonMask, kHoldForever);
    return 1;
}

// The full form, flattened. Controller::Input is a struct and a struct must not
// cross this boundary - a mod compiled against one layout and run against
// another would read a different field with no error anywhere. So the fields
// become parameters, and the two "did the caller mean to set this stick"
// booleans stay explicit rather than being inferred from a zero, because
// centring a stick and leaving it alone are different instructions.
extern "C" inline uint32_t ISend(uint32_t buttons,
                                 uint32_t setLeftStick, float leftX, float leftY,
                                 uint32_t setRightStick, float rightX, float rightY,
                                 uint32_t frames) {
    if constexpr (!Controller::SupportsInjection) {
        (void)buttons; (void)setLeftStick; (void)leftX; (void)leftY;
        (void)setRightStick; (void)rightX; (void)rightY; (void)frames;
        return 0;
    }
    NoteInjectorOnce();

    Controller::Input in;
    in.buttons = buttons;
    in.setLeftStick = setLeftStick != 0;
    in.leftX = leftX;
    in.leftY = leftY;
    in.setRightStick = setRightStick != 0;
    in.rightX = rightX;
    in.rightY = rightY;
    in.frames = frames ? frames : 1u;
    Controller::Send(in);
    return 1;
}

// The frame count that means "until released". Exported so a mod does not
// hard-code 0xFFFFFFFF, which is the sort of constant that stops being true
// quietly.
extern "C" inline uint32_t IHoldForever() {
    return kHoldForever;
}

extern "C" inline void IRelease() {
    if constexpr (Controller::SupportsInjection) Controller::Release();
}

extern "C" inline uint32_t IIsInjecting() {
    return Controller::IsInjecting() ? 1u : 0u;
}

extern "C" inline uint32_t IInjectedButtons() {
    return Controller::InjectedButtons();
}

// How many modules have synthesised input, for the state report and for a mod
// that wants to know it is not alone.
extern "C" inline uint32_t IInjectorCount() { return g_NotedCount; }

// Input capture stops the GAME seeing the controller while a mod owns it - a
// menu being driven with the same stick that would otherwise move Link.
//
// The held form is the one to use. SetInputCapture latches until something
// clears it, so a mod that crashes with capture on leaves the player unable to
// move; HoldInputCapture expires on its own after a few frames, so a mod that
// stops calling it simply gives control back.
extern "C" inline uint32_t IHoldInputCapture(uint32_t frames) {
    Controller::HoldInputCapture(frames ? frames : 8u);
    return 1;
}

extern "C" inline uint32_t ISetInputCapture(uint32_t on) {
    Controller::SetInputCapture(on != 0);
    return 1;
}

extern "C" inline uint32_t IIsInputCaptured() {
    return Controller::IsInputCaptured() ? 1u : 0u;
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsInjection", &ISupportsInjection),
    WIIXL_SURFACE_SYMBOL("Init",              &IInit),
    WIIXL_SURFACE_SYMBOL("HeldButtons",       &IHeldButtons),
    WIIXL_SURFACE_SYMBOL("MaskFor",           &IMaskFor),
    WIIXL_SURFACE_SYMBOL("IsPressed",         &IIsPressed),
    WIIXL_SURFACE_SYMBOL("GetLeftStick",      &IGetLeftStick),
    WIIXL_SURFACE_SYMBOL("GetRightStick",     &IGetRightStick),
    WIIXL_SURFACE_SYMBOL("HoldInputCapture",  &IHoldInputCapture),
    WIIXL_SURFACE_SYMBOL("SetInputCapture",   &ISetInputCapture),
    WIIXL_SURFACE_SYMBOL("IsInputCaptured",   &IIsInputCaptured),
    WIIXL_SURFACE_SYMBOL("Hold",              &IHold),
    WIIXL_SURFACE_SYMBOL("HoldIndefinitely",  &IHoldIndefinitely),
    WIIXL_SURFACE_SYMBOL("Send",              &ISend),
    WIIXL_SURFACE_SYMBOL("HoldForever",       &IHoldForever),
    WIIXL_SURFACE_SYMBOL("Release",           &IRelease),
    WIIXL_SURFACE_SYMBOL("IsInjecting",       &IIsInjecting),
    WIIXL_SURFACE_SYMBOL("InjectedButtons",   &IInjectedButtons),
    WIIXL_SURFACE_SYMBOL("InjectorCount",     &IInjectorCount),

    // v1.1. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("RegisterFrame",     &IRegisterFrame),
    WIIXL_SURFACE_SYMBOL("FrameCallbackCount", &IFrameCallbackCount),
    WIIXL_SURFACE_SYMBOL("LogFrameState",     &ILogFrameState),
};

} // namespace impl

// The load point asks for this by the same name every other surface uses.
inline void LogState() { impl::ILogFrameState(); }

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::InputSurface
