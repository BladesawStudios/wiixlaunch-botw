#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <cmath>
#include <cstdint>

// Unified button/stick reads: Switch (nn::hid) and Wii U/Cemu (VPAD GamePad,
// WPAD Pro Controller, and WPAD Wii Remote with or without a Nunchuk).
// Every Button enum value that has a real equivalent on a given platform's
// connected controller is mapped; the only permanent gaps are Joy-Con-only
// buttons (LeftSL/LeftSR/RightSL/RightSR) on Wii U/Cemu, since no Wii U
// controller has a physical equivalent.

namespace WiiXLaunch::BotW {

enum class Button : uint32_t {
    A,
    B,
    X,
    Y,
    StickL,
    StickR,
    L,
    R,
    ZL,
    ZR,
    Plus,
    Minus,
    DLeft,
    DUp,
    DRight,
    DDown,
    StickLLeft,
    StickLUp,
    StickLRight,
    StickLDown,
    StickRLeft,
    StickRUp,
    StickRRight,
    StickRDown,
    LeftSL,
    LeftSR,
    RightSL,
    RightSR,
};

namespace impl {

// Canonical (VPAD-style) button bits; all platforms map to this.
#if WIIXL_SWITCH
    // nn::hid::NpadButton - full real bit layout (public Nintendo SDK enum).
    constexpr uint32_t kBtnA           = 0x00000001;
    constexpr uint32_t kBtnB           = 0x00000002;
    constexpr uint32_t kBtnX           = 0x00000004;
    constexpr uint32_t kBtnY           = 0x00000008;
    constexpr uint32_t kBtnStickL      = 0x00000010;
    constexpr uint32_t kBtnStickR      = 0x00000020;
    constexpr uint32_t kBtnL           = 0x00000040;
    constexpr uint32_t kBtnR           = 0x00000080;
    constexpr uint32_t kBtnZL          = 0x00000100;
    constexpr uint32_t kBtnZR          = 0x00000200;
    constexpr uint32_t kBtnPlus        = 0x00000400;
    constexpr uint32_t kBtnMinus       = 0x00000800;
    constexpr uint32_t kBtnDLeft       = 0x00001000;
    constexpr uint32_t kBtnDUp         = 0x00002000;
    constexpr uint32_t kBtnDRight      = 0x00004000;
    constexpr uint32_t kBtnDDown       = 0x00008000;
    // Stick tilted fully cardinal (matches nn::hid semantics).
    constexpr uint32_t kBtnStickLLeft  = 0x00010000;
    constexpr uint32_t kBtnStickLUp    = 0x00020000;
    constexpr uint32_t kBtnStickLRight = 0x00040000;
    constexpr uint32_t kBtnStickLDown  = 0x00080000;
    constexpr uint32_t kBtnStickRLeft  = 0x00100000;
    constexpr uint32_t kBtnStickRUp    = 0x00200000;
    constexpr uint32_t kBtnStickRRight = 0x00400000;
    constexpr uint32_t kBtnStickRDown  = 0x00800000;
    // Joy-Con side buttons (SL/SR); reads as never-pressed in current hook style.
    constexpr uint32_t kBtnLeftSL      = 0x01000000;
    constexpr uint32_t kBtnLeftSR      = 0x02000000;
    constexpr uint32_t kBtnRightSL     = 0x04000000;
    constexpr uint32_t kBtnRightSR     = 0x08000000;
#else
    // Wii U VPAD buttons (VPADButtons, vpad/input.h) - this is also the
    // canonical/target bitspace on this platform, so these values ARE the
    // bits SetState()/StateRef() store; no translation needed for VPAD.
    constexpr uint32_t kBtnA           = 0x8000;
    constexpr uint32_t kBtnB           = 0x4000;
    constexpr uint32_t kBtnX           = 0x2000;
    constexpr uint32_t kBtnY           = 0x1000;
    constexpr uint32_t kBtnDLeft       = 0x0800;
    constexpr uint32_t kBtnDRight      = 0x0400;
    constexpr uint32_t kBtnDUp         = 0x0200;
    constexpr uint32_t kBtnDDown       = 0x0100;
    constexpr uint32_t kBtnZL          = 0x0080;
    constexpr uint32_t kBtnZR          = 0x0040;
    constexpr uint32_t kBtnL           = 0x0020;
    constexpr uint32_t kBtnR           = 0x0010;
    constexpr uint32_t kBtnPlus        = 0x0008;
    constexpr uint32_t kBtnMinus       = 0x0004;
    constexpr uint32_t kBtnStickR      = 0x00020000; // right stick click
    constexpr uint32_t kBtnStickL      = 0x00040000; // left stick click
    // VPAD_STICK_*_EMULATION_* - the GamePad's own hardware-computed
    // "stick tilted fully this direction" flags, the direct Wii U
    // equivalent of nn::hid's stick-tilt bits on Switch.
    constexpr uint32_t kBtnStickRLeft  = 0x04000000;
    constexpr uint32_t kBtnStickRRight = 0x02000000;
    constexpr uint32_t kBtnStickRUp    = 0x01000000;
    constexpr uint32_t kBtnStickRDown  = 0x00800000;
    constexpr uint32_t kBtnStickLLeft  = 0x40000000;
    constexpr uint32_t kBtnStickLRight = 0x20000000;
    constexpr uint32_t kBtnStickLUp    = 0x10000000;
    constexpr uint32_t kBtnStickLDown  = 0x08000000;
    // No Wii U controller (GamePad, Pro Controller, or Wii Remote) has a
    // Joy-Con-style SL/SR side button - these stay never-pressed on this
    // platform, not a mapping gap.
    constexpr uint32_t kBtnLeftSL      = 0x0000;
    constexpr uint32_t kBtnLeftSR      = 0x0000;
    constexpr uint32_t kBtnRightSL     = 0x0000;
    constexpr uint32_t kBtnRightSR     = 0x0000;

    // Wii U Pro Controller buttons (WPADButtonsProController, padscore/wpad.h,
    // read via KPADStatus::pro - a different bitspace from VPAD's, translated
    // to canonical below). The Pro Controller has the same physical button
    // set as the GamePad (no touch/motion), so this reaches full parity too.
    constexpr uint32_t kWpadProUp          = 0x00000001;
    constexpr uint32_t kWpadProLeft        = 0x00000002;
    constexpr uint32_t kWpadProZR          = 0x00000004;
    constexpr uint32_t kWpadProX           = 0x00000008;
    constexpr uint32_t kWpadProA           = 0x00000010;
    constexpr uint32_t kWpadProY           = 0x00000020;
    constexpr uint32_t kWpadProB           = 0x00000040;
    constexpr uint32_t kWpadProZL          = 0x00000080;
    constexpr uint32_t kWpadProR           = 0x00000200;
    constexpr uint32_t kWpadProPlus        = 0x00000400;
    constexpr uint32_t kWpadProMinus       = 0x00001000;
    constexpr uint32_t kWpadProL           = 0x00002000;
    constexpr uint32_t kWpadProDown        = 0x00004000;
    constexpr uint32_t kWpadProRight       = 0x00008000;
    constexpr uint32_t kWpadProStickR      = 0x00010000; // right stick click
    constexpr uint32_t kWpadProStickL      = 0x00020000; // left stick click
    constexpr uint32_t kWpadProStickLRight = 0x00040000;
    constexpr uint32_t kWpadProStickLLeft  = 0x00080000;
    constexpr uint32_t kWpadProStickLDown  = 0x00100000;
    constexpr uint32_t kWpadProStickLUp    = 0x00200000;
    constexpr uint32_t kWpadProStickRRight = 0x00400000;
    constexpr uint32_t kWpadProStickRLeft  = 0x00800000;
    constexpr uint32_t kWpadProStickRDown  = 0x01000000;
    constexpr uint32_t kWpadProStickRUp    = 0x02000000;

    // Core WPAD (bare Wii Remote, no Nunchuk) buttons - outer
    // KPADStatus::hold bitspace (WPADButtons, padscore/wpad.h). The Wii
    // Remote by itself has no X/Y/L/R/ZL/ZR/stick-click equivalents; "1"
    // and "2" stand in for the two "extra" face buttons since nothing else
    // maps cleanly, everything else maps 1:1 by name.
    constexpr uint32_t kWpadCoreLeft  = 0x0001;
    constexpr uint32_t kWpadCoreRight = 0x0002;
    constexpr uint32_t kWpadCoreDown  = 0x0004;
    constexpr uint32_t kWpadCoreUp    = 0x0008;
    constexpr uint32_t kWpadCorePlus  = 0x0010;
    constexpr uint32_t kWpadCoreTwo   = 0x0100; // stand-in for X
    constexpr uint32_t kWpadCoreOne   = 0x0200; // stand-in for Y
    constexpr uint32_t kWpadCoreB     = 0x0400;
    constexpr uint32_t kWpadCoreA     = 0x0800;
    constexpr uint32_t kWpadCoreMinus = 0x1000;

    // Nunchuk extension buttons (KPADStatus::nunchuk.hold bitspace, only
    // valid when extensionType is WPAD_EXT_NUNCHUK/WPAD_EXT_MPLUS_NUNCHUK).
    // Z/C stand in for ZL/L, the closest thing a Wii Remote + Nunchuk combo
    // has to shoulder buttons, and the Nunchuk's analog stick becomes the
    // left stick - a bare Wii Remote alone has no analog stick at all.
    constexpr uint32_t kNunchukZ = 0x2000;
    constexpr uint32_t kNunchukC = 0x4000;

    // KPADExtensionType (padscore/wpad.h's WPADExtensionType, read via
    // KPADStatus::extensionType) values this code distinguishes between.
    // Everything else (bare core = 0x00, Classic Controller, Balance Board,
    // MotionPlus without a Nunchuk, etc) falls back to the core.hold path,
    // which is correct for all of them - only a Pro Controller or an
    // attached Nunchuk change what's readable at the 0x60 union offset.
    constexpr uint8_t kExtNunchuk       = 0x01;
    constexpr uint8_t kExtMplusNunchuk  = 0x06;
    constexpr uint8_t kExtProController = 0x1f;
#endif

inline uint32_t ButtonBit(Button b) {
    switch (b) {
        case Button::A:           return kBtnA;
        case Button::B:           return kBtnB;
        case Button::X:           return kBtnX;
        case Button::Y:           return kBtnY;
        case Button::StickL:      return kBtnStickL;
        case Button::StickR:      return kBtnStickR;
        case Button::L:           return kBtnL;
        case Button::R:           return kBtnR;
        case Button::ZL:          return kBtnZL;
        case Button::ZR:          return kBtnZR;
        case Button::Plus:        return kBtnPlus;
        case Button::Minus:       return kBtnMinus;
        case Button::DLeft:       return kBtnDLeft;
        case Button::DUp:         return kBtnDUp;
        case Button::DRight:      return kBtnDRight;
        case Button::DDown:       return kBtnDDown;
        case Button::StickLLeft:  return kBtnStickLLeft;
        case Button::StickLUp:    return kBtnStickLUp;
        case Button::StickLRight: return kBtnStickLRight;
        case Button::StickLDown:  return kBtnStickLDown;
        case Button::StickRLeft:  return kBtnStickRLeft;
        case Button::StickRUp:    return kBtnStickRUp;
        case Button::StickRRight: return kBtnStickRRight;
        case Button::StickRDown:  return kBtnStickRDown;
        case Button::LeftSL:      return kBtnLeftSL;
        case Button::LeftSR:      return kBtnLeftSR;
        case Button::RightSL:     return kBtnRightSL;
        case Button::RightSR:     return kBtnRightSR;
    }
    return 0;
}

struct State {
    uint32_t hold = 0;
    float leftX = 0.0f, leftY = 0.0f, rightX = 0.0f, rightY = 0.0f;
};

// One state per input source, merged on read.
//
// They must stay separate: the VPAD hook runs every frame whether or not the
// GamePad is being touched, so a single shared state let an idle GamePad
// overwrite a Pro Controller's input with zeros a moment after the KPAD hook
// had written it. Both hooks fire every frame, so last-writer-wins silently
// discarded whichever source was actually being used.
inline State& VpadStateRef() {
    static State s;
    return s;
}

inline State& KpadStateRef() {
    static State s;
    return s;
}

inline const State& StateRef() {
    static State merged;
    const State& vpad = VpadStateRef();
    const State& kpad = KpadStateRef();

    merged.hold = vpad.hold | kpad.hold;
    merged.leftX  = (vpad.leftX  != 0.0f) ? vpad.leftX  : kpad.leftX;
    merged.leftY  = (vpad.leftY  != 0.0f) ? vpad.leftY  : kpad.leftY;
    merged.rightX = (vpad.rightX != 0.0f) ? vpad.rightX : kpad.rightX;
    merged.rightY = (vpad.rightY != 0.0f) ? vpad.rightY : kpad.rightY;
    return merged;
}

// Right-stick X is inverted here so the shared BotW::Controller reading
// turns the same direction on both platforms (matches Freecam's original
// per-platform inversion).
inline void StoreState(State& s, uint32_t hold, float lx, float ly, float rx, float ry) {
    s.hold = hold;
    rx = -rx;
    s.leftX  = (std::abs(lx) > 0.1f) ? lx : 0.0f;
    s.leftY  = (std::abs(ly) > 0.1f) ? ly : 0.0f;
    s.rightX = (std::abs(rx) > 0.1f) ? rx : 0.0f;
    s.rightY = (std::abs(ry) > 0.1f) ? ry : 0.0f;
}

#if WIIXL_SWITCH

struct NpadState {
    int64_t updateCount;
    uint64_t Buttons;
    int32_t LStickX;
    int32_t LStickY;
    int32_t RStickX;
    int32_t RStickY;
    uint32_t Flags;
    uint32_t Reserved;
};

inline void ProcessNpadState(NpadState* state) {
    if (!state) return;
    // Skip inactive styles (all-zero data would clobber real input).
    if (!(state->Flags & 0x1)) return;

    float lx = static_cast<float>(state->LStickX) / 32767.0f;
    float ly = static_cast<float>(state->LStickY) / 32767.0f;
    float rx = static_cast<float>(state->RStickX) / 32767.0f;
    float ry = static_cast<float>(state->RStickY) / 32767.0f;
    StoreState(VpadStateRef(), static_cast<uint32_t>(state->Buttons), lx, ly, rx, ry);
}

// nn::hid::GetNpadStates returns void, so there's no result to gate on -
// process whenever the id matches and at least one state was requested.
// state[0] is the most recent sample.
WIIXL_HOOK_DEFINE_TRAMPOLINE(NpadStatesHandheldHook) {
    static void Callback(void* stateArray, int count, const uint32_t& npadId) {
        Orig(stateArray, count, npadId);
        if (count > 0 && npadId == 0x20) ProcessNpadState(static_cast<NpadState*>(stateArray));
    }
};
WIIXL_HOOK_DEFINE_TRAMPOLINE(NpadStatesJoyDualHook) {
    static void Callback(void* stateArray, int count, const uint32_t& npadId) {
        Orig(stateArray, count, npadId);
        if (count > 0 && npadId == 0x0) ProcessNpadState(static_cast<NpadState*>(stateArray));
    }
};
WIIXL_HOOK_DEFINE_TRAMPOLINE(NpadStatesFullKeyHook) {
    static void Callback(void* stateArray, int count, const uint32_t& npadId) {
        Orig(stateArray, count, npadId);
        if (count > 0 && npadId == 0x0) ProcessNpadState(static_cast<NpadState*>(stateArray));
    }
};

#else // WIIXL_WIIU || WIIXL_CEMU

enum class PadSource { VPAD, WPAD_PRO, WPAD_CORE, WPAD_NUNCHUK };

// Translates a raw hold value from a given controller's native bitspace
// into the canonical VPAD-style bitspace SetState() expects. WPAD_NUNCHUK
// translates the Wii Remote's own core.hold (buttons on the remote itself,
// not the Nunchuk's Z/C - see NunchukExtraBits below for those).
inline uint32_t TranslateToCanonical(uint32_t rawHold, PadSource src) {
    uint32_t out = 0;
    switch (src) {
        case PadSource::VPAD:
            return rawHold; // already canonical
        case PadSource::WPAD_PRO:
            if (rawHold & kWpadProUp)          out |= kBtnDUp;
            if (rawHold & kWpadProLeft)         out |= kBtnDLeft;
            if (rawHold & kWpadProZR)           out |= kBtnZR;
            if (rawHold & kWpadProX)            out |= kBtnX;
            if (rawHold & kWpadProA)            out |= kBtnA;
            if (rawHold & kWpadProY)            out |= kBtnY;
            if (rawHold & kWpadProB)            out |= kBtnB;
            if (rawHold & kWpadProZL)           out |= kBtnZL;
            if (rawHold & kWpadProR)            out |= kBtnR;
            if (rawHold & kWpadProPlus)         out |= kBtnPlus;
            if (rawHold & kWpadProMinus)        out |= kBtnMinus;
            if (rawHold & kWpadProL)            out |= kBtnL;
            if (rawHold & kWpadProDown)         out |= kBtnDDown;
            if (rawHold & kWpadProRight)        out |= kBtnDRight;
            if (rawHold & kWpadProStickR)       out |= kBtnStickR;
            if (rawHold & kWpadProStickL)       out |= kBtnStickL;
            if (rawHold & kWpadProStickLRight)  out |= kBtnStickLRight;
            if (rawHold & kWpadProStickLLeft)   out |= kBtnStickLLeft;
            if (rawHold & kWpadProStickLDown)   out |= kBtnStickLDown;
            if (rawHold & kWpadProStickLUp)     out |= kBtnStickLUp;
            if (rawHold & kWpadProStickRRight)  out |= kBtnStickRRight;
            if (rawHold & kWpadProStickRLeft)   out |= kBtnStickRLeft;
            if (rawHold & kWpadProStickRDown)   out |= kBtnStickRDown;
            if (rawHold & kWpadProStickRUp)     out |= kBtnStickRUp;
            return out;
        case PadSource::WPAD_CORE:
        case PadSource::WPAD_NUNCHUK:
            // Same core.hold bitspace either way; a Nunchuk adds Z/C and an
            // analog stick on top (see NunchukExtraBits/stick handling in
            // KPADReadExWrapperHook), it doesn't change what the remote's
            // own buttons mean.
            if (rawHold & kWpadCoreLeft)  out |= kBtnDLeft;
            if (rawHold & kWpadCoreRight) out |= kBtnDRight;
            if (rawHold & kWpadCoreDown)  out |= kBtnDDown;
            if (rawHold & kWpadCoreUp)    out |= kBtnDUp;
            if (rawHold & kWpadCorePlus)  out |= kBtnPlus;
            if (rawHold & kWpadCoreTwo)   out |= kBtnX;
            if (rawHold & kWpadCoreOne)   out |= kBtnY;
            if (rawHold & kWpadCoreB)     out |= kBtnB;
            if (rawHold & kWpadCoreA)     out |= kBtnA;
            if (rawHold & kWpadCoreMinus) out |= kBtnMinus;
            return out;
    }
    return rawHold;
}

// Z -> ZL, C -> L: the closest thing a Wii Remote + Nunchuk combo has to
// shoulder buttons.
inline uint32_t NunchukExtraBits(uint32_t nunchukHold) {
    uint32_t out = 0;
    if (nunchukHold & kNunchukZ) out |= kBtnZL;
    if (nunchukHold & kNunchukC) out |= kBtnL;
    return out;
}

// Input injection state. Applied inside the VPAD read hook below, which is
// what makes it work at all: the hook wraps the game's own read wrapper and
// runs Orig() first, so by the time we get control the buffer holds this
// frame's input and the game has not looked at it yet. Writing there is
// indistinguishable from the player having pressed the button.
struct Injection {
    uint32_t buttons = 0;
    bool overrideLeftStick = false;
    float leftX = 0.0f, leftY = 0.0f;
    bool overrideRightStick = false;
    float rightX = 0.0f, rightY = 0.0f;
    // 0 = inactive. kHoldIndefinitely = until cleared or the watchdog fires.
    uint32_t framesRemaining = 0;
    uint32_t framesHeld = 0;
};

constexpr uint32_t kHoldIndefinitely = 0xFFFFFFFFu;

// A dead-man's switch on injected input. Even an "indefinite" hold expires
// after this many frames unless the client re-sends it.
//
// This exists because of a real failure: a client held buttons indefinitely,
// then the thing pumping the server stopped, so the release could never be
// delivered - leaving buttons jammed down in game with no way to clear them
// short of killing the emulator. Injected input must never be able to outlive
// the connection that asked for it. About 5 seconds at 60fps.
constexpr uint32_t kMaxInjectionFrames = 300;

inline Injection& InjectionRef() {
    static Injection injection;
    return injection;
}

using FrameCallback = void (*)();

inline FrameCallback& FrameCallbackRef() {
    static FrameCallback callback = nullptr;
    return callback;
}

#if !WIIXL_SWITCH

// VPADStatus: hold +0x00, trigger +0x04, release +0x08, sticks from +0x0C.
constexpr uint32_t kVpadHoldOffset = 0x00;
constexpr uint32_t kVpadTriggerOffset = 0x04;
constexpr uint32_t kVpadReleaseOffset = 0x08;

inline uint32_t& PreviousCombinedHold() {
    static uint32_t hold = 0;
    return hold;
}

inline bool& WasInjectingLastFrame() {
    static bool was = false;
    return was;
}

// Rewrites hold/trigger/release only while injecting, plus the one frame after
// it stops so the injected buttons get their release edge. Left alone
// otherwise: the console's own trigger/release bits are none of our business
// when we are not adding anything to them.
inline void ApplyInjection(uint8_t* vpad, uint32_t realHold) {
    Injection& injection = InjectionRef();
    const bool injecting = injection.framesRemaining > 0;

    if (!injecting && !WasInjectingLastFrame()) {
        PreviousCombinedHold() = realHold;
        return;
    }

    const uint32_t combined = injecting ? (realHold | injection.buttons) : realHold;

    const uint32_t previous = PreviousCombinedHold();

    *reinterpret_cast<uint32_t*>(vpad + kVpadHoldOffset) = combined;
    *reinterpret_cast<uint32_t*>(vpad + kVpadTriggerOffset) = combined & ~previous;
    *reinterpret_cast<uint32_t*>(vpad + kVpadReleaseOffset) = previous & ~combined;

    if (injecting && injection.overrideLeftStick) {
        *reinterpret_cast<float*>(vpad + 0x0C) = injection.leftX;
        *reinterpret_cast<float*>(vpad + 0x10) = injection.leftY;
    }
    if (injecting && injection.overrideRightStick) {
        *reinterpret_cast<float*>(vpad + 0x14) = injection.rightX;
        *reinterpret_cast<float*>(vpad + 0x18) = injection.rightY;
    }

    PreviousCombinedHold() = combined;
    WasInjectingLastFrame() = injecting;

    if (injecting) {
        if (injection.framesRemaining != kHoldIndefinitely) injection.framesRemaining--;

        // The watchdog. Counts real frames of injection regardless of what the
        // client asked for, so an indefinite hold still lets go on its own.
        injection.framesHeld++;
        if (injection.framesHeld >= kMaxInjectionFrames) injection.framesRemaining = 0;

        // Expiring clears everything, not just the countdown. Leaving the
        // button mask behind meant a later stick-only request revived it.
        if (injection.framesRemaining == 0) injection = Injection{};
    }
}

#endif

WIIXL_HOOK_DEFINE_TRAMPOLINE(VPADReadWrapperHook) {
    static void Callback(void* obj) {
        Orig(obj);
        uint8_t* vpad = static_cast<uint8_t*>(obj) + 0x14;
        ApplyInjection(vpad, *reinterpret_cast<uint32_t*>(vpad + 0x00));

        uint32_t hold = *reinterpret_cast<uint32_t*>(vpad + 0x00);
        float lx = *reinterpret_cast<float*>(vpad + 0x0C);
        float ly = *reinterpret_cast<float*>(vpad + 0x10);
        float rx = *reinterpret_cast<float*>(vpad + 0x14);
        float ry = *reinterpret_cast<float*>(vpad + 0x18);
        StoreState(VpadStateRef(), hold, lx, ly, rx, ry);

        if (FrameCallback callback = FrameCallbackRef()) callback();
    }
};

WIIXL_HOOK_DEFINE_TRAMPOLINE(KPADReadExWrapperHook) {
    static void Callback(void* obj) {
        Orig(obj);
        // Cleared every frame so letting go of a Wii U controller registers;
        // the loop below only writes when it finds an active one.
        KpadStateRef() = State{};
        for (int i = 0; i < 4; i++) {
            uint8_t* kpad = static_cast<uint8_t*>(obj) + 0x1118 + (i * 0xF08);
            uint32_t coreHold = *reinterpret_cast<uint32_t*>(kpad + 0x00);
            // extensionType (KPADStatus+0x5C) gates which sub-struct the
            // 0x60-byte union actually holds right now - reading e.g.
            // pro.hold when a Nunchuk (not a Pro Controller) is connected
            // would silently reinterpret nunchuk.stick's floats as a
            // button mask instead. Confirmed against wut's WUT_CHECK_OFFSET
            // asserts for KPADStatus::extensionType/pro/nunchuk.
            uint8_t extensionType = *reinterpret_cast<uint8_t*>(kpad + 0x5C);

            if (extensionType == kExtProController) {
                uint32_t proHold = *reinterpret_cast<uint32_t*>(kpad + 0x60);
                float lx = *reinterpret_cast<float*>(kpad + 0x6C);
                float ly = *reinterpret_cast<float*>(kpad + 0x70);
                float rx = *reinterpret_cast<float*>(kpad + 0x74);
                float ry = *reinterpret_cast<float*>(kpad + 0x78);
                // The skip has to consider the sticks, not just the buttons.
                // Testing proHold alone reads as "this slot is idle" whenever
                // no button happens to be down, so a Pro Controller being
                // pushed around with nothing held would fall through to the
                // next slot and leave the cleared state in place - sticks that
                // only report while some other button is held.
                if (proHold == 0 && lx == 0.0f && ly == 0.0f && rx == 0.0f && ry == 0.0f) continue;
                StoreState(KpadStateRef(), TranslateToCanonical(proHold, PadSource::WPAD_PRO), lx, ly, rx, ry);
                break;
            } else if (extensionType == kExtNunchuk || extensionType == kExtMplusNunchuk) {
                // nunchuk.stick@0x60, nunchuk.hold@0x7C (KPADStatus's
                // nunchuk sub-struct: stick, acc, accMagnitude,
                // accVariation, then hold), confirmed against wut's
                // WUT_CHECK_OFFSET asserts.
                float lx = *reinterpret_cast<float*>(kpad + 0x60);
                float ly = *reinterpret_cast<float*>(kpad + 0x64);
                uint32_t nunchukHold = *reinterpret_cast<uint32_t*>(kpad + 0x7C);
                uint32_t canonical = TranslateToCanonical(coreHold, PadSource::WPAD_NUNCHUK) | NunchukExtraBits(nunchukHold);
                if (canonical == 0 && lx == 0.0f && ly == 0.0f) continue;
                StoreState(KpadStateRef(), canonical, lx, ly, 0.0f, 0.0f);
                break;
            } else {
                if (coreHold == 0) continue;
                StoreState(KpadStateRef(), TranslateToCanonical(coreHold, PadSource::WPAD_CORE), 0.0f, 0.0f, 0.0f, 0.0f);
                break;
            }
        }
    }
};

#endif

} // namespace impl

class Controller {
public:
    // Installs the platform input hooks. Call once from WiiXLaunch_Init().
    static void Init() {
#if WIIXL_SWITCH
        impl::NpadStatesHandheldHook::Install(0x01800dd0, 0);
        impl::NpadStatesJoyDualHook::Install(0x01800de0, 0);
        impl::NpadStatesFullKeyHook::Install(0x01800df0, 0);
#else
        impl::VPADReadWrapperHook::Install(0, 0x030d9f24);
        impl::KPADReadExWrapperHook::Install(0, 0x030da168);
#endif
    }

    // Runs once per frame, from the input read the game performs every frame -
    // including on the title screen, during loads and in menus. That makes it a
    // sturdier per-frame hook than Player::OnTick, which stops the moment there
    // is no player actor. Only one callback slot; call again to replace it.
    static void OnFrame(impl::FrameCallback callback) { impl::FrameCallbackRef() = callback; }

    static bool IsPressed(Button b) {
        return (impl::StateRef().hold & impl::ButtonBit(b)) != 0;
    }

    static void GetLeftStick(float& x, float& y) {
        const impl::State& s = impl::StateRef();
        x = s.leftX; y = s.leftY;
    }

    static void GetRightStick(float& x, float& y) {
        const impl::State& s = impl::StateRef();
        x = s.rightX; y = s.rightY;
    }

    // ---- Input injection -------------------------------------------------
    //
    // Wii U/Cemu only. The Switch path reads nn::hid state arrays in a
    // different bitspace and would need its own reverse translation, so this
    // is a no-op there rather than a guess.
    //
    // Injection is applied in the VPAD read hook, so it reaches the game the
    // same way a real GamePad press does - no window focus needed, and it
    // composes with real input rather than replacing it (buttons are OR'd).
    //
    // Caveat worth knowing: this drives the VPAD (GamePad) path. A game being
    // played through a Wii Remote or Pro Controller reads KPAD instead, which
    // is a different bitspace and is not injected into.
    static constexpr bool SupportsInjection = !WIIXL_SWITCH;

    // Even this expires: see impl::kMaxInjectionFrames. Re-send to keep a hold
    // alive; that way a client that goes away cannot jam a button down.
    static constexpr uint32_t HoldIndefinitely = impl::kHoldIndefinitely;
    static constexpr uint32_t MaxInjectionFrames = impl::kMaxInjectionFrames;

    // The raw bit for a button, for building masks to put in Input::buttons.
    static uint32_t MaskFor(Button b) { return impl::ButtonBit(b); }

    // One injection, described completely.
    //
    // Send() REPLACES whatever was in flight - it never merges. That is
    // deliberate: an earlier version let callers set buttons and sticks
    // separately, and a stick-only call would revive a stale button mask left
    // over from a previous call whose countdown had expired. Pressing buttons
    // nobody asked for is about the worst failure mode an input API has.
    struct Input {
        uint32_t buttons = 0;
        bool setLeftStick = false;
        float leftX = 0.0f, leftY = 0.0f;
        bool setRightStick = false;
        float rightX = 0.0f, rightY = 0.0f;
        uint32_t frames = 1;
    };

    static void Send(const Input& input) {
#if !WIIXL_SWITCH
        impl::Injection injection;
        injection.buttons = input.buttons;
        injection.overrideLeftStick = input.setLeftStick;
        injection.leftX = input.leftX;
        injection.leftY = input.leftY;
        injection.overrideRightStick = input.setRightStick;
        injection.rightX = input.rightX;
        injection.rightY = input.rightY;
        injection.framesRemaining = input.frames;
        impl::InjectionRef() = injection;
#else
        (void)input;
#endif
    }

    static void Hold(uint32_t buttonMask, uint32_t frames) {
        Input input;
        input.buttons = buttonMask;
        input.frames = frames;
        Send(input);
    }

    // Ends injection. The hook still runs once more to emit the release edge,
    // so buttons do not get stuck down.
    static void Release() {
#if !WIIXL_SWITCH
        impl::InjectionRef() = impl::Injection{};
#endif
    }

    static bool IsInjecting() {
#if !WIIXL_SWITCH
        return impl::InjectionRef().framesRemaining > 0;
#else
        return false;
#endif
    }

    static uint32_t InjectedButtons() {
#if !WIIXL_SWITCH
        const impl::Injection& injection = impl::InjectionRef();
        return injection.framesRemaining > 0 ? injection.buttons : 0;
#else
        return 0;
#endif
    }
};

} // namespace WiiXLaunch::BotW
