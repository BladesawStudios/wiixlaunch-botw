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
constexpr uint16_t kVersionMinor = 0;

namespace impl {

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

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsInjection", &ISupportsInjection),
    WIIXL_SURFACE_SYMBOL("Init",              &IInit),
    WIIXL_SURFACE_SYMBOL("HeldButtons",       &IHeldButtons),
    WIIXL_SURFACE_SYMBOL("MaskFor",           &IMaskFor),
    WIIXL_SURFACE_SYMBOL("IsPressed",         &IIsPressed),
    WIIXL_SURFACE_SYMBOL("GetLeftStick",      &IGetLeftStick),
    WIIXL_SURFACE_SYMBOL("GetRightStick",     &IGetRightStick),
    WIIXL_SURFACE_SYMBOL("Hold",              &IHold),
    WIIXL_SURFACE_SYMBOL("HoldIndefinitely",  &IHoldIndefinitely),
    WIIXL_SURFACE_SYMBOL("Send",              &ISend),
    WIIXL_SURFACE_SYMBOL("HoldForever",       &IHoldForever),
    WIIXL_SURFACE_SYMBOL("Release",           &IRelease),
    WIIXL_SURFACE_SYMBOL("IsInjecting",       &IIsInjecting),
    WIIXL_SURFACE_SYMBOL("InjectedButtons",   &IInjectedButtons),
    WIIXL_SURFACE_SYMBOL("InjectorCount",     &IInjectorCount),
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

} // namespace WiiXLaunch::BotW::Surfaces::InputSurface
