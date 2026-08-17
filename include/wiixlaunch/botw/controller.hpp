#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <cmath>
#include <cstdint>

// Unified button/stick reads: Switch (nn::hid) and Wii U/Cemu (VPAD/KPAD).

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
    // Wii U VPAD buttons (also the canonical/target bitspace on this platform)
    constexpr uint32_t kBtnB     = 0x4000;
    constexpr uint32_t kBtnX     = 0x2000;
    constexpr uint32_t kBtnZL    = 0x0080;
    constexpr uint32_t kBtnDDown = 0x0100;
    // Wii U/Cemu: unused (reads never-pressed).
    constexpr uint32_t kBtnA           = 0x0000;
    constexpr uint32_t kBtnY           = 0x0000;
    constexpr uint32_t kBtnStickL      = 0x0000;
    constexpr uint32_t kBtnStickR      = 0x0000;
    constexpr uint32_t kBtnL           = 0x0000;
    constexpr uint32_t kBtnR           = 0x0000;
    constexpr uint32_t kBtnZR          = 0x0000;
    constexpr uint32_t kBtnPlus        = 0x0000;
    constexpr uint32_t kBtnMinus       = 0x0000;
    constexpr uint32_t kBtnDLeft       = 0x0000;
    constexpr uint32_t kBtnDUp         = 0x0000;
    constexpr uint32_t kBtnDRight      = 0x0000;
    constexpr uint32_t kBtnStickLLeft  = 0x0000;
    constexpr uint32_t kBtnStickLUp    = 0x0000;
    constexpr uint32_t kBtnStickLRight = 0x0000;
    constexpr uint32_t kBtnStickLDown  = 0x0000;
    constexpr uint32_t kBtnStickRLeft  = 0x0000;
    constexpr uint32_t kBtnStickRUp    = 0x0000;
    constexpr uint32_t kBtnStickRRight = 0x0000;
    constexpr uint32_t kBtnStickRDown  = 0x0000;
    constexpr uint32_t kBtnLeftSL      = 0x0000;
    constexpr uint32_t kBtnLeftSR      = 0x0000;
    constexpr uint32_t kBtnRightSL     = 0x0000;
    constexpr uint32_t kBtnRightSR     = 0x0000;

    // WPAD Pro Controller buttons (KPADStatus::pro.hold bitspace - different from VPAD!)
    constexpr uint32_t kWpadProB     = 0x0040;
    constexpr uint32_t kWpadProX     = 0x0008;
    constexpr uint32_t kWpadProZL    = 0x0080;
    constexpr uint32_t kWpadProDDown = 0x4000;

    // Core WPAD (bare Wiimote) buttons (outer KPADStatus::hold bitspace - no ZL/ZR exist here)
    constexpr uint32_t kWpadCoreB     = 0x0400; // "B" trigger button
    constexpr uint32_t kWpadCoreA     = 0x0800; // used in place of X (no analog-adjacent button on core Wiimote)
    constexpr uint32_t kWpadCoreDDown = 0x0004;
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

inline State& StateRef() {
    static State s;
    return s;
}

// Right-stick X is inverted here so the shared BotW::Controller reading
// turns the same direction on both platforms (matches Freecam's original
// per-platform inversion).
inline void SetState(uint32_t hold, float lx, float ly, float rx, float ry) {
    State& s = StateRef();
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
    SetState(static_cast<uint32_t>(state->Buttons), lx, ly, rx, ry);
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

enum class PadSource { VPAD, WPAD_PRO, WPAD_CORE };

// Translates a raw hold value from a given controller's native bitspace
// into the canonical VPAD-style bitspace SetState() expects.
inline uint32_t TranslateToCanonical(uint32_t rawHold, PadSource src) {
    uint32_t out = 0;
    switch (src) {
        case PadSource::VPAD:
            return rawHold; // already canonical
        case PadSource::WPAD_PRO:
            if (rawHold & kWpadProB)     out |= kBtnB;
            if (rawHold & kWpadProX)     out |= kBtnX;
            if (rawHold & kWpadProZL)    out |= kBtnZL;
            if (rawHold & kWpadProDDown) out |= kBtnDDown;
            return out;
        case PadSource::WPAD_CORE:
            // No ZL/ZR; map B->kBtnB, A->kBtnX as stand-in.
            if (rawHold & kWpadCoreB)     out |= kBtnB;
            if (rawHold & kWpadCoreA)     out |= kBtnX;
            if (rawHold & kWpadCoreDDown) out |= kBtnDDown;
            return out;
    }
    return rawHold;
}

WIIXL_HOOK_DEFINE_TRAMPOLINE(VPADReadWrapperHook) {
    static void Callback(void* obj) {
        Orig(obj);
        uint8_t* vpad = static_cast<uint8_t*>(obj) + 0x14;
        uint32_t hold = *reinterpret_cast<uint32_t*>(vpad + 0x00);
        float lx = *reinterpret_cast<float*>(vpad + 0x0C);
        float ly = *reinterpret_cast<float*>(vpad + 0x10);
        float rx = *reinterpret_cast<float*>(vpad + 0x14);
        float ry = *reinterpret_cast<float*>(vpad + 0x18);
        SetState(hold, lx, ly, rx, ry);
    }
};

WIIXL_HOOK_DEFINE_TRAMPOLINE(KPADReadExWrapperHook) {
    static void Callback(void* obj) {
        Orig(obj);
        for (int i = 0; i < 4; i++) {
            uint8_t* kpad = static_cast<uint8_t*>(obj) + 0x1118 + (i * 0xF08);
            uint32_t wii_hold = *reinterpret_cast<uint32_t*>(kpad + 0x00);
            // KPADStatus::pro union starts at 0x60 (hold@0x60, leftStick@0x6C,
            // rightStick@0x74), confirmed against wut's WUT_CHECK_OFFSET asserts.
            uint32_t pro_hold = *reinterpret_cast<uint32_t*>(kpad + 0x60);

            if (pro_hold != 0) {
                float lx = *reinterpret_cast<float*>(kpad + 0x6C);
                float ly = *reinterpret_cast<float*>(kpad + 0x70);
                float rx = *reinterpret_cast<float*>(kpad + 0x74);
                float ry = *reinterpret_cast<float*>(kpad + 0x78);
                SetState(TranslateToCanonical(pro_hold, PadSource::WPAD_PRO), lx, ly, rx, ry);
                break;
            } else if (wii_hold != 0) {
                SetState(TranslateToCanonical(wii_hold, PadSource::WPAD_CORE), 0.0f, 0.0f, 0.0f, 0.0f);
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

    static bool IsPressed(Button b) {
        return (impl::StateRef().hold & impl::ButtonBit(b)) != 0;
    }

    static void GetLeftStick(float& x, float& y) {
        impl::State& s = impl::StateRef();
        x = s.leftX; y = s.leftY;
    }

    static void GetRightStick(float& x, float& y) {
        impl::State& s = impl::StateRef();
        x = s.rightX; y = s.rightY;
    }
};

} // namespace WiiXLaunch::BotW
