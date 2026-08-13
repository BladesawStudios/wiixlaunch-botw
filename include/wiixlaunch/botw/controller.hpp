#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <cmath>
#include <cstdint>

// WiiXLaunch::BotW::Controller - unified button/stick reads across Switch
// (nn::hid::GetNpadStates) and Wii U/Cemu (VPAD + KPAD/WPAD Pro/Core),
// ported from the Freecam mod's input hooks. All bit positions here are
// confirmed working on both platforms today.
//
// v1 button set is deliberately limited to what Freecam actually used
// (B, X, ZL, DDown) - expanding to the full official nn::hid/VPAD_BUTTON_*
// set from the vendored SDK headers is a fast, low-risk follow-up, not
// invented/guessed here.

namespace WiiXLaunch::BotW {

enum class Button : uint32_t {
    B,
    X,
    ZL,
    DDown,
};

namespace impl {

// Canonical (VPAD-style) bit values - the shared bitspace every platform's
// raw input gets translated into before ButtonBit()/IsPressed() reads it.
#if WIIXL_SWITCH
    // nn::hid::NpadButton bit positions: A=0, B=1, X=2, Y=3, L=6, R=7, ZL=8, ZR=9, Down=15
    constexpr uint32_t kBtnB     = 0x0002;
    constexpr uint32_t kBtnX     = 0x0004;
    constexpr uint32_t kBtnZL    = 0x0100;
    constexpr uint32_t kBtnDDown = 0x8000;
#else
    // Wii U VPAD buttons (also the canonical/target bitspace on this platform)
    constexpr uint32_t kBtnB     = 0x4000;
    constexpr uint32_t kBtnX     = 0x2000;
    constexpr uint32_t kBtnZL    = 0x0080;
    constexpr uint32_t kBtnDDown = 0x0100;

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
        case Button::B:     return kBtnB;
        case Button::X:     return kBtnX;
        case Button::ZL:    return kBtnZL;
        case Button::DDown: return kBtnDDown;
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
    // NpadAttribute::IsConnected - skip styles that aren't active (e.g. the
    // Handheld state while docked), otherwise their all-zero data would
    // clobber the real controller's input.
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
            // Core Wiimote has no ZL/ZR/analog trigger - no reliable combo
            // modifier exists here; mapping B->kBtnB and A->kBtnX as a
            // stand-in.
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
