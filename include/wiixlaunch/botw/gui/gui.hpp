#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstdint>
#include <cstddef>

#include "gui_types.hpp"
#include "../game/controller.hpp"   // Button, for the input queries on both the real and stub Canvas

// ===========================================================================
// WiiXLaunch::BotW::GUI - custom in-game GUIs in the base game's own style
// ===========================================================================
//
// An immediate-mode GUI drawn into BotW's frame through the GX2 injection
// hook (graphics/gx2.hpp), built from the game's OWN fonts and UI art: the
// loader (gui_assets.hpp) streams Normal_00/NormalS_00 out of Font_XX.sbfarc
// and the dialogue box, cursor, corner, arrow and button-glyph textures out
// of Layout/Common.sblarc at runtime, so a mod ships no assets and its
// windows come out pixel-for-pixel in the game's look. gui_types.hpp holds
// the colours, font sizes and measurements read out of the layouts.
//
//   #include <wiixlaunch/botw/botw.hpp>
//   using namespace WiiXLaunch::BotW;
//
//   static bool s_Open = false;
//   static bool s_God = false;
//   static float s_Speed = 1.0f;
//
//   static void Frame(GUI::Canvas& c) {
//       if (c.Pressed(Button::Minus)) s_Open = !s_Open;
//       if (!s_Open) return;
//       c.RoundedBox({420, 140, 440, 300}, GUI::Colors::MessageWindow, 16);
//       c.TextBox({420, 150, 440, 40}, "Debug", GUI::Styles::Title());
//       c.Toggle({460, 210, 360, 40}, "Invincible", s_God);
//       c.Slider({460, 260, 360, 40}, "Speed", s_Speed, 0.5f, 3.0f, 0.1f);
//       if (c.Button({460, 310, 360, 40}, "Close")) s_Open = false;
//       c.MessageBox("Press A to select. This is the game's dialogue box,\n"
//                    "its font, its colours.", "WiiXLaunch");
//   }
//
//   extern "C" void WiiXLaunch_Init() {
//       ...
//       Controller::Init();
//       GUI::Init();
//       GUI::OnFrame(&Frame);
//   }
//
// Coordinates are the game's 1280x720 layout pixels, origin top-left.
// Widgets are focus-navigated with the D-pad / left stick; A activates, B
// is reported through Canvas::Cancel() for the mod to act on. Focus order is
// the order the widgets are issued in each frame.
//
// Call Canvas::CaptureInput() every frame a menu is open and the game stops
// seeing the pad, so the D-pad does not walk Link around underneath it. It is
// never automatic: an overlay that only draws must not swallow input.
//
// GX2 only. On Switch (NVN) every call compiles to a no-op and
// SupportsGUI is false - the NVN backend is a stub for now.

#if WIIXL_CEMU || WIIXL_WIIU

#include "gui_render.hpp"
#include "gui_assets.hpp"
#include "gui_text.hpp"
#include "gui_backend.hpp"
#include "../game/controller.hpp"
#include "../platform/log.hpp"

namespace WiiXLaunch::BotW::GUI {

constexpr bool SupportsGUI = true;

class Canvas;
using FrameCallback = void (*)(Canvas& canvas);

namespace impl {

inline FrameCallback g_FrameCallback = nullptr;
inline bool g_Initialized = false;
inline bool g_PipelineReady = false;
inline uint32_t g_FrameCounter = 0;
// Set by the input-hook build pass, cleared by the present hook - so the
// present hook can tell whether a frame was built for it this time round.
inline bool g_FrameBuilt = false;

// Input: edge detection and D-pad/stick navigation with key repeat, in
// terms of Controller's canonical hold bits.
// How long each Canvas::CaptureInput() call holds the pad for. Comfortably
// more than one game frame, so capture survives a dropped or slow frame, and
// short enough that it lapses almost immediately once the calls stop.
constexpr uint32_t kCaptureHoldFrames = 8;

constexpr int kNavRepeatDelay = 18;
constexpr int kNavRepeatRate = 6;
constexpr float kStickThreshold = 0.5f;

enum NavBit : uint32_t { NavUpBit = 1, NavDownBit = 2, NavLeftBit = 4, NavRightBit = 8 };

struct InputState {
    uint32_t hold = 0;
    uint32_t prevHold = 0;
    uint32_t navHeld = 0;
    uint32_t navFired = 0;
    uint32_t navLast = 0;
    int navFrames = 0;
    float leftX = 0.0f, leftY = 0.0f;
    float rightX = 0.0f, rightY = 0.0f;
};
inline InputState g_Input;

inline uint32_t Bit(Button b) { return Controller::MaskFor(b); }

inline void UpdateInput() {
    InputState& in = g_Input;
    in.prevHold = in.hold;
    in.hold = 0;
    // Rebuild a hold mask from the public accessor so this never depends on
    // Controller's internals.
    static const Button kAll[] = {
        Button::A, Button::B, Button::X, Button::Y, Button::StickL, Button::StickR, Button::L, Button::R,
        Button::ZL, Button::ZR, Button::Plus, Button::Minus, Button::DLeft, Button::DUp, Button::DRight,
        Button::DDown, Button::StickLLeft, Button::StickLUp, Button::StickLRight, Button::StickLDown,
        Button::StickRLeft, Button::StickRUp, Button::StickRRight, Button::StickRDown,
    };
    for (Button b : kAll) if (Controller::IsPressed(b)) in.hold |= Bit(b);
    Controller::GetLeftStick(in.leftX, in.leftY);
    Controller::GetRightStick(in.rightX, in.rightY);

    uint32_t nav = 0;
    if ((in.hold & (Bit(Button::DUp) | Bit(Button::StickLUp))) || in.leftY > kStickThreshold) nav |= NavUpBit;
    if ((in.hold & (Bit(Button::DDown) | Bit(Button::StickLDown))) || in.leftY < -kStickThreshold) nav |= NavDownBit;
    if ((in.hold & (Bit(Button::DLeft) | Bit(Button::StickLLeft))) || in.leftX < -kStickThreshold) nav |= NavLeftBit;
    if ((in.hold & (Bit(Button::DRight) | Bit(Button::StickLRight))) || in.leftX > kStickThreshold) nav |= NavRightBit;

    in.navFired = 0;
    if (nav == 0) {
        in.navFrames = 0;
    } else if (nav != in.navLast) {
        in.navFired = nav;
        in.navFrames = 0;
    } else {
        in.navFrames++;
        if (in.navFrames >= kNavRepeatDelay && ((in.navFrames - kNavRepeatDelay) % kNavRepeatRate) == 0) in.navFired = nav;
    }
    in.navLast = nav;
    in.navHeld = nav;
}

inline bool PressedNow(Button b) { return (g_Input.hold & Bit(b)) && !(g_Input.prevHold & Bit(b)); }
inline bool HeldNow(Button b) { return (g_Input.hold & Bit(b)) != 0; }

// Focus: widgets register in issue order; index == g_FocusIndex is focused.
inline int g_FocusIndex = 0;
inline int g_FocusCount = 0;        // this frame's running count
inline int g_FocusCountLast = 0;    // last frame's total, for wrap-around
inline bool g_FocusMoved = false;

// When the focus last landed somewhere new, so a row can animate in rather
// than snap. One stamp is enough: only one widget is focused at a time, so
// there is nothing to track per widget - which is what makes this possible at
// all in an immediate-mode GUI with no per-widget storage.
inline int64_t g_FocusChangedTicks = 0;
inline int g_FocusPrevIndex = -1;

inline void ApplyNavigationToFocus() {
    if (g_FocusCountLast <= 0) { g_FocusIndex = 0; return; }
    g_FocusMoved = false;
    if (g_Input.navFired & NavDownBit) { g_FocusIndex = (g_FocusIndex + 1) % g_FocusCountLast; g_FocusMoved = true; }
    if (g_Input.navFired & NavUpBit) { g_FocusIndex = (g_FocusIndex + g_FocusCountLast - 1) % g_FocusCountLast; g_FocusMoved = true; }
    // Catches SetFocus and the end-of-frame clamp as well as navigation, so
    // anything that moves the cursor animates.
    if (g_FocusIndex != g_FocusPrevIndex) {
        g_FocusPrevIndex = g_FocusIndex;
        g_FocusChangedTicks = g_NowTicks;
    }
}

// 0 the instant the focus moves, 1 once the row has settled. Smoothstep, so it
// eases in and out rather than ramping linearly into place.
inline float FocusFade(float seconds) {
    if (g_FocusChangedTicks == 0 || g_NowTicks == 0 || seconds <= 0.0f) return 1.0f;
    const int64_t elapsed = g_NowTicks - g_FocusChangedTicks;
    if (elapsed <= 0) return 0.0f;
    const float span = seconds * static_cast<float>(WiiXLaunch::Time::kTicksPerSecond);
    const float t = static_cast<float>(elapsed) / span;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

inline void EnsureWhiteSprite() {
    SpriteInfo& white = SpriteAt(Sprite::White);
    if (white.texture) return;
    // 8x8, a whole GX2 micro-tile: CreateTexture only writes the texels the
    // image covers, so a smaller texture leaves the rest of the tile holding
    // whatever happened to be in that memory.
    static uint8_t pixels[8 * 8 * 4];
    for (uint32_t i = 0; i < sizeof(pixels); ++i) pixels[i] = 0xFF;
    white.texture = Backend::CreateTexture(pixels, sizeof(pixels), 8, 8, Backend::kSurfaceFormatUnormR8G8B8A8);
    white.width = 8;
    white.height = 8;
}

inline void OnPipelineReady() {
    EnsureWhiteSprite();
    EnsureRecord();
    // Last: it is what lets the input hook start building frames, and both
    // allocations above have to exist before one does.
    g_PipelineReady = true;
}

inline void BuildFrame();
inline void OnDraw(Backend::CommandBuffer* cmdBuf, void* dst, int width, int height);

} // namespace impl

// ---------------------------------------------------------------------------
// Canvas - what a frame callback draws with
// ---------------------------------------------------------------------------
class Canvas {
public:
    // ---- frame / assets ---------------------------------------------------
    uint32_t Frame() const { return impl::g_FrameCounter; }
    float Width() const { return kVirtualWidth; }
    float Height() const { return kVirtualHeight; }

    // ---- output resolution ------------------------------------------------
    // Layout coordinates stay 1280x720 whatever the colour buffer is; these
    // report what that buffer actually is, for anything that wants to align
    // to real pixels. BotW renders the GamePad view at 854x480, and a Cemu
    // resolution pack can make the TV buffer any size.
    // The largest colour buffer of the last frame. BotW draws two - the TV
    // and the 854x480 GamePad view - and the GUI goes into both; this reports
    // the bigger one rather than whichever was drawn last.
    uint32_t DeviceWidth() const { return impl::g_ReportWidth; }
    uint32_t DeviceHeight() const { return impl::g_ReportHeight; }
    // Device pixels per layout pixel, for that same buffer.
    float PixelScaleX() const { return impl::g_ReportScaleX; }
    float PixelScaleY() const { return impl::g_ReportScaleY; }

    // ---- time -------------------------------------------------------------
    // Animate from these, never from Frame(): BotW runs at 30 and the FPS++
    // pack makes it 60, so anything counting frames runs at double speed.
    float DeltaSeconds() const { return impl::g_DeltaSeconds; }
    float TimeSeconds() const { return impl::g_TimeSeconds; }
    // Measured, smoothed, and the game's real rate - not an assumption.
    float FramesPerSecond() const { return impl::g_Fps; }
    // Position in a repeating cycle of `period` seconds, 0 to 1 - a sawtooth,
    // for anything that should move at a constant rate.
    float Phase(float periodSeconds) const { return impl::Phase(periodSeconds); }
    // A smooth 0..1 oscillation over the same period, easing in and out. Use
    // this for anything that pulses, bobs or breathes; stepping a Phase into
    // a handful of states reads as stutter.
    float Wave(float periodSeconds) const { return impl::Wave(periodSeconds); }
    // Where the 1280x720 rectangle sits inside the buffer (non-zero only
    // when Fit is letterboxing a non-16:9 buffer).
    float ViewportOffsetX() const { return impl::g_OffsetX; }
    float ViewportOffsetY() const { return impl::g_OffsetY; }
    // Rounds a layout coordinate to the nearest whole device pixel.
    float SnapX(float x) const {
        return impl::g_ScaleX > 0.0f ? (impl::RoundToPixel(x * impl::g_ScaleX + impl::g_OffsetX) - impl::g_OffsetX) / impl::g_ScaleX : x;
    }
    float SnapY(float y) const {
        return impl::g_ScaleY > 0.0f ? (impl::RoundToPixel(y * impl::g_ScaleY + impl::g_OffsetY) - impl::g_OffsetY) / impl::g_ScaleY : y;
    }

    // ---- group alpha ------------------------------------------------------
    // The immediate-mode stand-in for lyt's pane alpha chain: everything
    // drawn while a factor is pushed - fills, art, text AND text shadows -
    // has its alpha multiplied by it, so a whole window fades as one.
    void PushAlpha(float factor) { impl::PushAlpha(factor); }
    void PopAlpha() { impl::PopAlpha(); }
    float CurrentAlpha() const { return impl::CurrentAlpha(); }
    bool AssetsReady() const { return impl::LoaderFinished(); }
    bool FontReady(FontId f = FontId::Normal) const { return impl::FontAt(f).IsReady(); }
    bool SpriteReady(Sprite s) const { return impl::SpriteTexture(s) != 0; }

    // ---- input ------------------------------------------------------------
    bool Pressed(Button b) const { return impl::PressedNow(b); }
    bool Held(Button b) const { return impl::HeldNow(b); }
    bool Accept() const { return impl::PressedNow(Button::A); }
    bool Cancel() const { return impl::PressedNow(Button::B); }
    // Navigation pulses (D-pad or left stick, with key repeat).
    bool NavUp() const { return (impl::g_Input.navFired & impl::NavUpBit) != 0; }
    bool NavDown() const { return (impl::g_Input.navFired & impl::NavDownBit) != 0; }
    bool NavLeft() const { return (impl::g_Input.navFired & impl::NavLeftBit) != 0; }
    bool NavRight() const { return (impl::g_Input.navFired & impl::NavRightBit) != 0; }
    void GetLeftStick(float& x, float& y) const { x = impl::g_Input.leftX; y = impl::g_Input.leftY; }
    void GetRightStick(float& x, float& y) const { x = impl::g_Input.rightX; y = impl::g_Input.rightY; }

    // Draws the frame behind `r`, blurred, as a backdrop - BotW's frosted
    // glass. Off unless GUI::SetBackdropBlur(true) has been called, and a
    // no-op until the blur targets exist (the frame after it is enabled), so
    // it is always safe to call.
    //
    // This is the raw form and it is easy to misuse: it draws a full-strength
    // opaque rectangle, so it has to be issued BEFORE the window that covers
    // it, and something translucent has to be drawn over it or all you get is
    // a blurry picture of the scene. FrostedBox() below does both in the right
    // order and is what most callers want.
    void BlurBehind(const GUI::Rect& r, Color tint = Colors::White) {
        if (!impl::g_BlurEnabled || !Backend::BackdropReady()) return;
        impl::g_BlurRequested = true;
        // The backdrop holds the whole screen, so the piece of it behind this
        // rectangle is just the rectangle in screen fractions.
        impl::EmitQuad(Backend::BackdropTexture(), r,
                       r.x / kVirtualWidth, r.y / kVirtualHeight,
                       r.Right() / kVirtualWidth, r.Bottom() / kVirtualHeight, tint);
    }

    // BlurBehind faded out on ALL FOUR sides, so it reads as a soft patch of
    // the scene rather than a rectangle of it. A 3x3 grid of quads: the middle
    // is solid, the edges ramp to transparent, the corners ramp in both
    // directions at once. Fewer quads cannot do it - fading only left and right
    // leaves hard horizontal edges, which is exactly what a first attempt at
    // this looked like, and one quad with opposite ends clear interpolates to
    // clear the whole way across.
    //
    // `tint` MULTIPLIES the sampled scene, so a dark tint gives a shadow and
    // white gives the scene at face value. Same caveat as BlurBehind: issue it
    // BEFORE whatever sits on top.
    void BlurBehindFaded(const GUI::Rect& r, Color tint = Colors::White,
                         float fadeX = 20.0f, float fadeY = 10.0f) {
        if (!impl::g_BlurEnabled || !Backend::BackdropReady()) return;
        if (r.w <= 0.0f || r.h <= 0.0f) return;
        if (fadeX * 2.0f > r.w) fadeX = r.w * 0.5f;
        if (fadeY * 2.0f > r.h) fadeY = r.h * 0.5f;
        impl::g_BlurRequested = true;
        const Backend::TextureHandle tex = Backend::BackdropTexture();

        const float xs[4] = { r.x, r.x + fadeX, r.Right() - fadeX, r.Right() };
        const float ys[4] = { r.y, r.y + fadeY, r.Bottom() - fadeY, r.Bottom() };
        const float ax[4] = { 0.0f, 1.0f, 1.0f, 0.0f };   // alpha at each grid line
        const float ay[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
        for (int gy = 0; gy < 3; ++gy) {
            if (ys[gy + 1] <= ys[gy]) continue;
            for (int gx = 0; gx < 3; ++gx) {
                if (xs[gx + 1] <= xs[gx]) continue;
                const float a00 = ax[gx] * ay[gy],       a10 = ax[gx + 1] * ay[gy];
                const float a01 = ax[gx] * ay[gy + 1],   a11 = ax[gx + 1] * ay[gy + 1];
                if (a00 <= 0.0f && a10 <= 0.0f && a01 <= 0.0f && a11 <= 0.0f) continue;
                impl::EmitQuad(tex, xs[gx], ys[gy], xs[gx + 1], ys[gy + 1],
                               xs[gx] / kVirtualWidth,     ys[gy] / kVirtualHeight,
                               xs[gx + 1] / kVirtualWidth, ys[gy + 1] / kVirtualHeight,
                               tint.Scaled(a00), tint.Scaled(a10),
                               tint.Scaled(a01), tint.Scaled(a11));
            }
        }
    }

    // Take the pad away from the game for as long as this keeps being called:
    // call it every frame a menu is up, and the player's buttons, sticks and
    // touches stop reaching the game while the GUI carries on reading them.
    // Nothing here calls it for you - a HUD overlay that draws every frame
    // must not swallow input - and because it lapses a few frames after the
    // last call, a menu that stops drawing cannot leave the pad dead.
    void CaptureInput() { Controller::HoldInputCapture(impl::kCaptureHoldFrames); }
    bool IsInputCaptured() const { return Controller::IsInputCaptured(); }

    // ---- focus ------------------------------------------------------------
    int Focus() const { return impl::g_FocusIndex; }
    void SetFocus(int index) { impl::g_FocusIndex = index < 0 ? 0 : index; }
    int FocusableCount() const { return impl::g_FocusCountLast; }
    // Registers a focusable slot and reports whether it is the focused one.
    // The widgets below call this; a custom widget can too.
    bool ClaimFocus() {
        const int index = impl::g_FocusCount++;
        return index == impl::g_FocusIndex;
    }

    // ---- primitives -------------------------------------------------------
    void Rect(const GUI::Rect& r, Color c, const Backend::BlendState& blend = Backend::Blend::Alpha) {
        impl::EmitRect(r, c, blend);
    }
    void RectGradient(const GUI::Rect& r, Color top, Color bottom, const Backend::BlendState& blend = Backend::Blend::Alpha) {
        impl::EmitRectGradient(r, top, bottom, blend);
    }
    // rotation is in degrees clockwise about the rect's centre. Angles taken
    // from a .bflyt need their sign flipped: lyt panes are y-up.
    void Image(Sprite s, const GUI::Rect& r, Color tint = Colors::White, uint8_t orient = OrientNone,
               const Backend::BlendState& blend = Backend::Blend::Alpha, float rotation = 0.0f) {
        impl::EmitSprite(s, r, tint, orient, blend, rotation);
    }
    void ImageUV(Sprite s, const GUI::Rect& r, float u0, float v0, float u1, float v1,
                 Color tint = Colors::White, uint8_t orient = OrientNone,
                 const Backend::BlendState& blend = Backend::Blend::Alpha) {
        impl::EmitQuad(impl::SpriteTexture(s), r, u0, v0, u1, v1, tint, orient, blend);
    }
    // Draws a sprite at its native pixel size with its top-left at (x, y).
    void ImageAt(Sprite s, float x, float y, Color tint = Colors::White, uint8_t orient = OrientNone,
                 float scale = 1.0f, const Backend::BlendState& blend = Backend::Blend::Alpha) {
        const impl::SpriteInfo& info = impl::SpriteAt(s);
        if (!info.texture) return;
        impl::EmitSprite(s, GUI::Rect{x, y, info.width * scale, info.height * scale}, tint, orient, blend);
    }
    // ---- a mod's own textures ---------------------------------------------
    // The Sprite overloads above index the fixed table of the GAME's art. These
    // take a handle the mod owns - from GUI::LoadTexture, or anything else that
    // produces a Backend::TextureHandle - and put it through the same path:
    // layout-pixel mapping, the group-alpha stack, blend, orientation, pixel
    // snapping and batching. Without them a mod-loaded texture could be created
    // but never drawn through the GUI.
    //
    // The handle must outlive the frame. Nothing here owns or frees it.
    void Image(Backend::TextureHandle tex, const GUI::Rect& r, Color tint = Colors::White,
               uint8_t orient = OrientNone, const Backend::BlendState& blend = Backend::Blend::Alpha,
               float rotation = 0.0f) {
        impl::EmitQuad(tex, r, 0.0f, 0.0f, 1.0f, 1.0f, tint, orient, blend, true, rotation);
    }
    void ImageUV(Backend::TextureHandle tex, const GUI::Rect& r, float u0, float v0, float u1, float v1,
                 Color tint = Colors::White, uint8_t orient = OrientNone,
                 const Backend::BlendState& blend = Backend::Blend::Alpha) {
        impl::EmitQuad(tex, r, u0, v0, u1, v1, tint, orient, blend);
    }
    // At its native pixel size, top-left at (x, y). False if the size is not
    // known, in which case nothing is drawn.
    bool ImageAt(Backend::TextureHandle tex, float x, float y, Color tint = Colors::White,
                 uint8_t orient = OrientNone, float scale = 1.0f,
                 const Backend::BlendState& blend = Backend::Blend::Alpha) {
        uint32_t w = 0, h = 0;
        if (!tex || !Backend::GetTextureSize(tex, w, h) || w == 0 || h == 0) return false;
        impl::EmitQuad(tex, GUI::Rect{x, y, w * scale, h * scale}, 0.0f, 0.0f, 1.0f, 1.0f, tint, orient, blend);
        return true;
    }
    bool TextureSize(Backend::TextureHandle tex, float& w, float& h) const {
        uint32_t tw = 0, th = 0;
        if (!tex || !Backend::GetTextureSize(tex, tw, th)) { w = 0.0f; h = 0.0f; return false; }
        w = static_cast<float>(tw);
        h = static_cast<float>(th);
        return true;
    }

    void SpriteSize(Sprite s, float& w, float& h) const {
        const impl::SpriteInfo& info = impl::SpriteAt(s);
        w = static_cast<float>(info.width);
        h = static_cast<float>(info.height);
    }

    // ---- text -------------------------------------------------------------
    // (x, y) is the top-left of the text; for Align::Center/Right it is the
    // centre / right edge instead. No wrapping.
    void Text(float x, float y, const char* text, const TextStyle& style = Styles::Message()) {
        GUI::Rect box{x, y, 0.0f, 0.0f};
        if (style.align == Align::Center) box.x = x;   // zero-width box centres on x
        impl::DrawTextBox(box, text, style, 0.0f);
    }
    // Lays text out inside a box: alignment from the style, wrapping at the
    // box width when `wrap` is set.
    void TextBox(const GUI::Rect& box, const char* text, const TextStyle& style, bool wrap = true) {
        impl::DrawTextBox(box, text, style, wrap ? box.w : 0.0f);
    }
    void MeasureText(const char* text, const TextStyle& style, float& w, float& h, float wrapWidth = 0.0f) const {
        impl::MeasureText(style, text, wrapWidth, w, h);
    }

    // ---- the game's own composites ----------------------------------------

    // The dialogue box: Nt_MsgWindowL_00 as a left cap, its inner column
    // stretched across, and the cap mirrored on the right - exactly how
    // Message_00's W_Base_00 window pane is built (frame 96 wide on a 192
    // tall texture, black content, pane alpha 230).
    void MessageWindow(const GUI::Rect& r, Color color = Colors::MessageWindow, bool decorations = true) {
        const float capW = Metrics::kMessageCapWidth * (r.h / Metrics::kMessageCapHeight);
        // No backdrop blur here on purpose. The window is a pill, and a blur
        // can only be drawn as a rectangle, so the only part of it that could
        // be frosted without the rectangle showing through the rounded ends is
        // the straight middle - which then reads as a bright band across the
        // centre of the box, exactly the thing the blur is supposed to avoid.
        // Frost a rounded box with Canvas::FrostedBox instead, and leave the
        // dialogue box as the game's own black over the scene, which at its
        // pane alpha of 230 is most of the way there anyway.
        if (SpriteReady(Sprite::MsgWindowCap) && r.w >= capW * 2.0f) {
            const float capU = impl::EdgeU(Sprite::MsgWindowCap);
            const float capV = impl::EdgeV(Sprite::MsgWindowCap);
            impl::EmitQuad(impl::SpriteTexture(Sprite::MsgWindowCap), GUI::Rect{r.x, r.y, capW, r.h}, 0.0f, 0.0f, capU, capV, color);
            impl::EmitQuad(impl::SpriteTexture(Sprite::MsgWindowCap), GUI::Rect{r.x + capW, r.y, r.w - 2.0f * capW, r.h},
                           capU, 0.0f, capU, capV, color);
            impl::EmitQuad(impl::SpriteTexture(Sprite::MsgWindowCap), GUI::Rect{r.Right() - capW, r.y, capW, r.h},
                           0.0f, 0.0f, capU, capV, color, OrientFlipH);
        } else {
            RoundedBox(r, color, r.h * 0.5f);
        }
        if (decorations) {
            // Nt_MsgDeco_0x / Nt_DecoLine: the ornaments at +-284 / +-278
            // from the box centre, at 0.67 scale, in the box's colour but full alpha.
            const float cx = r.CenterX();
            const float cy = r.CenterY();
            const float k = r.h / Metrics::kMessageWindowHeight;
            const float dw = Metrics::kMessageDecoWidth * k;
            const float dh = Metrics::kMessageDecoHeight * k;
            const float lw = 12.0f * 0.67f * k;
            const float lh = 72.0f * 0.67f * k;
            const Color deco = Colors::Cream;
            // Nt_MsgDeco_02/03 and Nt_DecoLineL/R_01 are the visible copies of
            // the ornaments, and their materials carry lyt blend (1,2,4) -
            // src*dst + dst*srcAlpha - not plain alpha.
            const Backend::BlendState& db = Backend::Blend::Overlay;
            Image(Sprite::MsgDeco, GUI::Rect{cx - Metrics::kMessageDecoOffset * k - dw * 0.5f, cy - dh * 0.5f, dw, dh}, deco, OrientNone, db);
            Image(Sprite::MsgDeco, GUI::Rect{cx + Metrics::kMessageDecoOffset * k - dw * 0.5f, cy - dh * 0.5f, dw, dh}, deco, OrientFlipH, db);
            Image(Sprite::MsgDecoLine, GUI::Rect{cx - 278.0f * k - lw * 0.5f, cy - lh * 0.5f, lw, lh}, deco, OrientNone, db);
            Image(Sprite::MsgDecoLine, GUI::Rect{cx + 278.0f * k - lw * 0.5f, cy - lh * 0.5f, lw, lh}, deco, OrientFlipH, db);
        }
    }

    // A complete dialogue box where the game puts it: 612x129 centred 235 px
    // below screen centre, Normal font text (cream, soft shadow) in a 445x85
    // box, the speaker name in NormalSmall above the left edge.
    void MessageBox(const char* text, const char* name = nullptr, float alpha = 1.0f, bool showArrow = false) {
        const GUI::Rect win{kVirtualWidth * 0.5f - Metrics::kMessageWindowWidth * 0.5f,
                            Metrics::kMessageWindowCenterY - Metrics::kMessageWindowHeight * 0.5f,
                            Metrics::kMessageWindowWidth, Metrics::kMessageWindowHeight};
        MessageWindow(win, Colors::MessageWindow.Scaled(alpha), true);
        const GUI::Rect textBox{kVirtualWidth * 0.5f - Metrics::kMessageTextWidth * 0.5f,
                                Metrics::kMessageWindowCenterY - Metrics::kMessageTextHeight * 0.5f,
                                Metrics::kMessageTextWidth, Metrics::kMessageTextHeight};
        TextBox(textBox, text, FitToBox(text, Styles::Message().Alpha(alpha), textBox.w), true);
        if (name && name[0]) {
            // The game backs the name with P_Sh_00: a BLURRED CAPTURE of the
            // framebuffer (FBLayout_00^r) at alpha 180, not a shadow sprite.
            // That is exactly what BlurBehind provides, so this is the real
            // thing rather than a stand-in - the earlier stand-in
            // (DialogShadow_00, one quarter of a radial gradient) drew as a
            // hard dark box and was worse than nothing.
            //
            // Sized to the text rather than to the pane, so a short name gets a
            // short backing, and faded at the ends because a hard-edged
            // rectangle of blurred scene is the failure the stand-in had. When
            // blur is off or not yet ready this draws nothing and the name's
            // own hard black shadow carries it, exactly as before.
            const GUI::Rect nameBox{Metrics::kMessageNameX, Metrics::kMessageNameCenterY - 13.0f, 176.0f, 26.0f};
            float nameW = 0.0f, nameH = 0.0f;
            MeasureText(name, Styles::Name(), nameW, nameH);
            if (nameW > 0.0f) {
                // DARKENED, not the scene at face value. `P_Sh_00` is a shadow
                // pane - white text has to read against it - and over a bright
                // sky an untinted capture came out as a pale slab that made the
                // name harder to read than no backing at all. The tint
                // multiplies the sample, so this is blurred scene pulled well
                // down towards black. Alpha 180 is the game's own figure.
                BlurBehindFaded(GUI::Rect{nameBox.x - 18.0f, nameBox.y - 7.0f, nameW + 36.0f, nameBox.h + 14.0f},
                                Color{60, 60, 62, 180}.Scaled(alpha), 26.0f, 12.0f);
            }
            TextBox(nameBox, name, Styles::Name().Alpha(alpha), false);
        }
        if (showArrow) {
            // Nt_ArrowMsg_00: the "more" arrow at the bottom-right, bobbing.
            const float bob = 4.0f * impl::Wave(1.1f);
            ImageAt(Sprite::ArrowMsg, win.Right() - 70.0f, win.Bottom() - 36.0f + bob, Colors::Cream.Scaled(alpha), OrientNone, 0.75f);
        }
    }

    // A rounded box over a blurred backdrop - the game's frosted glass, in
    // the order it has to happen: blur first, then the translucent box over
    // it. With the blur off this is just RoundedBox, so it is always safe to
    // use, and a colour with some alpha left in it (the game's windows sit at
    // 230 of 255) is what lets the blur show through at all.
    //
    // The blur is inset by half the corner radius so its square corners stay
    // inside the rounded shape: a point half a radius in from the corner is
    // still within the corner's arc, so nothing pokes out.
    void FrostedBox(const GUI::Rect& r, Color color, float radius = Metrics::kRoundedCorner) {
        // A blur can only be drawn as a rectangle, and the shape it has to
        // fill is a rounded box, so the shape is covered with rectangles that
        // all stay inside it:
        //
        //   - a full-width middle band between the two arcs,
        //   - top and bottom bands between the arcs horizontally, and
        //   - a staircase of five slices inside each corner's quarter disc.
        //
        // The bands alone leave the four corners sharp while everything
        // around them is frosted, which is plainly visible; the staircase
        // takes those to about 87% covered, the rest being the very tip of
        // each corner where the panel is nearly transparent anyway.
        //
        // The geometry comes from the art: CornerR3's quarter disc runs from
        // texel 2 to 7 of its eight, so within a corner drawn at `radius` the
        // arc has radius 0.75*radius centred at (radius, radius) - which also
        // means the box RoundedBox actually draws is inset from `r` by a
        // quarter of the radius, and the blur has to be inset the same or it
        // shows as a bright fringe around the whole panel.
        if (radius > 0.0f && r.w > radius * 2.0f && r.h > radius * 2.0f) {
            const float pad = radius * 0.25f;              // transparent margin in the art
            const float arc = radius * 0.75f;              // the arc's own radius
            const GUI::Rect vis = r.Inset(pad, pad);

            BlurBehind(GUI::Rect{vis.x, vis.y + arc, vis.w, vis.h - 2.0f * arc});
            BlurBehind(GUI::Rect{vis.x + arc, vis.y, vis.w - 2.0f * arc, arc});
            BlurBehind(GUI::Rect{vis.x + arc, vis.Bottom() - arc, vis.w - 2.0f * arc, arc});

            // Half-widths of a six-step staircase inscribed in a unit quarter
            // disc, each measured at the slice's OUTER edge so every slice is
            // wholly inside the arc. The sixth step has no width and is left out.
            // Measured at each slice's OUTER edge, so its far corner lands
            // exactly ON the arc; trimmed by 4% so it lands just inside it
            // instead, clear of the one texel over which the art's edge fades.
            static const float kSlice[5] = {0.94657f, 0.90510f, 0.83139f, 0.71555f, 0.53066f};
            const float cx[2] = {vis.x + arc, vis.Right() - arc};
            const float cy[2] = {vis.y + arc, vis.Bottom() - arc};
            for (int corner = 0; corner < 4; ++corner) {
                const float ox = (corner & 1) ? 1.0f : -1.0f;   // outward, horizontally
                const float oy = (corner & 2) ? 1.0f : -1.0f;   // outward, vertically
                const float px = cx[corner & 1];
                const float py = cy[(corner >> 1) & 1];
                for (int i = 0; i < 5; ++i) {
                    const float inner = py + oy * arc * (static_cast<float>(i) / 6.0f);
                    const float outer = py + oy * arc * (static_cast<float>(i + 1) / 6.0f);
                    const float w = arc * kSlice[i];
                    const float x0 = ox < 0.0f ? px - w : px;
                    const float y0 = oy < 0.0f ? outer : inner;
                    BlurBehind(GUI::Rect{x0, y0, w, arc / 6.0f});
                }
            }
        } else {
            BlurBehind(r);
        }
        RoundedBox(r, color, radius);
    }

    // A rounded box built the way PaOptionBtn_00's W_Base panes are: a lyt
    // window frame from CornerR3_00 (8 px frame) with the centre filled.
    //
    // CornerR3's 8x8 art is a 6x6 quarter disc with two transparent texels
    // of padding at its top and left, so the visible box sits 2 px (times
    // radius/8) inside the rect on every side. That is not something to
    // correct by hand: the edges are stretched from the same texture's last
    // column and row, which carry the same padding, so corners and edges
    // line up by construction. The first version filled the edges with flat
    // rectangles that ran to the rect's edge while the corners did not - a
    // notch at every corner, plainly visible at 1440p.
    void RoundedBox(const GUI::Rect& r, Color c, float radius = Metrics::kRoundedCorner) {
        if (!SpriteReady(Sprite::CornerRound) || radius <= 0.0f || r.w < radius * 2.0f || r.h < radius * 2.0f) {
            impl::EmitRect(r, c);
            return;
        }
        FrameFromCorner(Sprite::CornerRound, r, radius, c);
        impl::EmitRect(GUI::Rect{r.x + radius, r.y + radius, r.w - 2.0f * radius, r.h - 2.0f * radius}, c);
    }

    // The outline the focused option row gets (W_BaseLine_02): the same
    // window-frame construction from CornerLineR2_00, whose 8x8 art is a
    // rounded ~1.5 px stroke in its bottom-right 4x4. Nothing is drawn as a
    // flat line - the edges come from the texture, so their thickness and
    // inset match the corners exactly. `thickness` only feeds the fallback.
    void RoundedOutline(const GUI::Rect& r, Color c, float radius = Metrics::kRoundedCorner, float thickness = 0.0f) {
        if (thickness <= 0.0f) thickness = radius * 0.25f;
        if (!SpriteReady(Sprite::CornerLine) || r.w < radius * 2.0f || r.h < radius * 2.0f) {
            impl::EmitRect(GUI::Rect{r.x, r.y, r.w, thickness}, c);
            impl::EmitRect(GUI::Rect{r.x, r.Bottom() - thickness, r.w, thickness}, c);
            impl::EmitRect(GUI::Rect{r.x, r.y, thickness, r.h}, c);
            impl::EmitRect(GUI::Rect{r.Right() - thickness, r.y, thickness, r.h}, c);
            return;
        }
        FrameFromCorner(Sprite::CornerLine, r, radius, c);
    }

    // The pause-menu / confirm-dialog selection frame: SelectFrame_04's
    // rounded stroke corners with its edges stretched between them, and the
    // Sheikah-blue glow under it (BtnDialog_00 W_SelectFrame_00/01).
    void SelectFrame(const GUI::Rect& r, Color frame = Colors::SelectFrame, Color glow = Colors::SelectGlow) {
        // BtnDialog_00's frame is 68 px on a 530x186 window, so the corner is
        // about 0.37 of the height. Taking the largest corner that fits
        // instead makes the stroke far heavier than the game's and, since the
        // top and bottom bands are each a corner tall, fills the middle with
        // glow.
        constexpr float kCornerOfHeight = 68.0f / 186.0f;
        float corner = kCornerOfHeight * r.h;
        if (corner > Metrics::kSelectFrameCorner) corner = Metrics::kSelectFrameCorner;
        const float maxCorner = (r.w < r.h ? r.w : r.h) * 0.5f;
        if (corner > maxCorner) corner = maxCorner;
        if (glow.a && SpriteReady(Sprite::SelectFrameGlow)) {
            const float g = corner * (71.0f / 68.0f);
            FrameFromCorner(Sprite::SelectFrameGlow, r.Inset(-(g - corner), -(g - corner)), g, glow);
        }
        if (frame.a && SpriteReady(Sprite::SelectFrame)) FrameFromCorner(Sprite::SelectFrame, r, corner, frame);
        else RoundedOutline(r, frame, 12.0f, 3.0f);
    }

    // The option-menu cursor: four L-shaped corners (Nt_CursorS_00) in cream,
    // drawn 28 px outside the row like PaOptionBtn_00's Window_00 does.
    // The option cursor, as PaOptionBtn_00 builds it: a WINDOW pane
    // (Window_00, 332x96 around a 276x40 row - hence the 28px margin) with a
    // 48px frame, so lyt draws the four corner brackets AND the stretched
    // edges between them, not four loose corners. Its material blends
    // additively (lyt 1,4,1) and its pane alpha is 128, which is why the real
    // cursor brightens the row instead of drawing a hard bracket over it -
    // the first version used 230 and corners only, and read as a hot outline.
    // `shimmer` is the cloud drift the game runs through this frame; pass 0
    // for a static cursor. See Metrics::kCursorShimmer.
    void CursorCorners(const GUI::Rect& r, Color c = Colors::Cream.WithAlpha(128), float size = 48.0f,
                       const Backend::BlendState& blend = Backend::Blend::Additive,
                       float shimmer = Metrics::kCursorShimmer) {
        if (!SpriteReady(Sprite::CursorCorner)) { RoundedOutline(r, c); return; }
        FrameFromCorner(Sprite::CursorCorner, r, size, c, blend, shimmer);
    }

    // The thin bracket cursor (Nt_Cursor_00). Like the option cursor this is
    // a frame, not four loose corners: the art's stroke runs to the texture
    // edge, so the stretched edges join the brackets up.
    void CursorBrackets(const GUI::Rect& r, Color c = Colors::White, float size = 64.0f) {
        if (!SpriteReady(Sprite::CursorBracket)) { RoundedOutline(r, c); return; }
        float corner = size;
        const float maxCorner = (r.w < r.h ? r.w : r.h) * 0.5f;
        if (corner > maxCorner) corner = maxCorner;
        FrameFromCorner(Sprite::CursorBracket, r, corner, c);
    }

    // PaBoxedCursor_00: four arrows at the corners, each rotated 45 degrees
    // off vertical so it points diagonally OUTWARD - the "this is selected"
    // cursor the inventory uses. The layout rotates its (downward) arrow art
    // by lyt -135/-225/-45/+45 about Z at the four corners, which on screen
    // (y down) is +135/+225/+45/-45; each arrow also sits 3px diagonally out
    // from its corner, at 0.21 scale of the 75px sprite.
    void BoxedCursor(const GUI::Rect& r, Color c = Colors::White) {
        // A slow breathe rather than a two-state blink: the blink was only
        // ever tolerable because it ran at double speed.
        const float pulse = 1.0f + 0.06f * impl::Wave(1.4f);
        const float s = Metrics::kBoxedCursorArrow * pulse;
        const float o = 3.0f;   // the layout's own diagonal nudge
        const float h = s * 0.5f;
        Image(Sprite::ArrowDown, GUI::Rect{r.x - h - o, r.y - h - o, s, s}, c, OrientNone, Backend::Blend::Alpha, 135.0f);
        Image(Sprite::ArrowDown, GUI::Rect{r.Right() - h + o, r.y - h - o, s, s}, c, OrientNone, Backend::Blend::Alpha, 225.0f);
        Image(Sprite::ArrowDown, GUI::Rect{r.x - h - o, r.Bottom() - h + o, s, s}, c, OrientNone, Backend::Blend::Alpha, 45.0f);
        Image(Sprite::ArrowDown, GUI::Rect{r.Right() - h + o, r.Bottom() - h + o, s, s}, c, OrientNone, Backend::Blend::Alpha, -45.0f);
    }

    // The light confirm-dialog plate (BtnDialog_00's W_Pict_00): a flat
    // rounded fill under a 9-slice of BtnBasic_08T/08B, which carry the rim
    // and shadow. (The game adds a projected inner texture on top; this is
    // the close-enough version.)
    // `r` is the VISIBLE plate. BtnBasic_08T/08B are only the rim and an
    // inner glow (their interior alpha falls to 17%; in the game the surface
    // comes from a third, projected texture the material combines in), and
    // that art sits inside a 96px tile with 36 px of padding on the left, 40
    // on top and 24 below. So the window the rim and its shadow are
    // nine-sliced over is `r` grown by that padding, and an opaque rounded
    // fill goes under the rim first to stand in for the surface texture.
    // Without it the plate was a translucent grey over whatever was behind.
    // The bounds Plate(r) actually draws into, and the corner size it uses.
    // BIGGER than r, because the rim art sits outside the fill, and ASYMMETRIC
    // vertically, because the art's top padding (40/96) is larger than its
    // bottom (24/96). Anything that has to line up with a plate has to use
    // this rather than r - a symmetric inset of r lands inside the plate at the
    // top and nearly on its edge at the bottom.
    GUI::Rect PlateBounds(const GUI::Rect& r, float* outCorner = nullptr) const {
        constexpr float kPadL = 36.0f / 96.0f, kPadT = 40.0f / 96.0f, kPadB = 24.0f / 96.0f;
        constexpr float kCornerOfWindow = 96.0f / 240.0f;
        float corner = kCornerOfWindow * r.h / (1.0f - kCornerOfWindow * (kPadT + kPadB));
        if (corner > Metrics::kPlateCorner) corner = Metrics::kPlateCorner;
        const float maxW = r.w / (2.0f - 2.0f * kPadL);
        if (corner > maxW) corner = maxW;
        if (outCorner) *outCorner = corner;
        return GUI::Rect{r.x - kPadL * corner, r.y - kPadT * corner,
                         r.w + 2.0f * kPadL * corner, r.h + (kPadT + kPadB) * corner};
    }

    void Plate(const GUI::Rect& r, Color fill = Colors::Plate) {
        if (!SpriteReady(Sprite::PlateTop) || !SpriteReady(Sprite::PlateBottom)) {
            RoundedBox(r, fill, 20.0f);
            return;
        }
        // Art padding as fractions of the 96px tile (measured, see docs/gui.md).
        constexpr float kPadL = 36.0f / 96.0f, kPadT = 40.0f / 96.0f, kPadB = 24.0f / 96.0f;
        // The rim's own corner curve, measured off the art: the plate's edge
        // settles at x=36 / y=39 of the 96px tile and the arc reaches that
        // straight run at about 80, so the visible corner radius is ~42/96 -
        // not the 16/96 first assumed, which gave the fill much tighter
        // corners than the rim around it and read as a square-cornered box
        // inside a rounded one.
        constexpr float kSurfaceRadius = 42.0f / 96.0f;
        // Corner size keeps BtnDialog_00's proportion rather than being made
        // as large as fits: its plate is a 560x240 window with a 96px frame,
        // so the corner is 0.4 of the window's height. Solving that for a
        // window of r.h + (padT+padB)*corner gives the line below - about
        // 0.55*r.h. Taking the largest corner that fit instead turned every
        // small button into a fat pill, which is what "way too big" was.
        float corner = 0.0f;
        const GUI::Rect win = PlateBounds(r, &corner);   // never upscales past the art

        if (SpriteReady(Sprite::PlateShadowTop) && SpriteReady(Sprite::PlateShadowBottom)) {
            NineSlice(Sprite::PlateShadowTop, Sprite::PlateShadowBottom,
                      GUI::Rect{win.x, win.y + 4.0f, win.w, win.h}, corner, Colors::Black.WithAlpha(150));
        }
        // The base fills exactly the visible plate. It must not be grown
        // past it: the rim's own edge is soft, so a hard fill poking out
        // beyond it reads as a second, doubled outline.
        RoundedBox(r, fill, kSurfaceRadius * corner);

        // The satin sheen. The game gets it by projecting ProjectTex_01^o
        // through a BC5 normal map (BtnBasic_08TI/BI+t) in the material's TEV
        // stages. Measured, those normal maps are flat 128/127 across the
        // whole stretched region - there is no baked gradient to copy, the
        // shading IS the projection - so this draws the projected texture
        // itself, stretched over the plate and MULTIPLIED. Same soft diagonal
        // bands; no normal-map lighting. The texture is 216-255, so it darkens
        // by at most 15% and usually under 6%, which is why it needs no
        // strength control: it cannot be loud.
        //
        // Three quads rather than one, because the fill is a ROUNDED rect and
        // a square quad's corners would fall outside it - multiply would then
        // darken the scene behind the plate in four little squares, which is
        // the same class of mistake as a hard-edged blur. A middle band plus
        // top and bottom strips covers everything except the four corner
        // squares, which is exactly where the rounding is. UVs are taken from
        // each piece's position in the whole plate, so the pattern is
        // continuous across the seams.
        //
        // Skipped while a group alpha is pushed: Blend::Multiply takes its
        // alpha factors from the destination, so the sheen cannot fade with
        // the panel around it, and a plate that fades out while its sheen
        // stays put looks worse than one with no sheen.
        if (SpriteReady(Sprite::PlateSheen) && CurrentAlpha() >= 0.999f) {
            const float rad = kSurfaceRadius * corner;
            const Backend::TextureHandle sheenTex = impl::SpriteTexture(Sprite::PlateSheen);
            auto sheen = [&](float x0, float y0, float x1, float y1) {
                if (x1 <= x0 || y1 <= y0) return;
                impl::EmitQuad(sheenTex, GUI::Rect{x0, y0, x1 - x0, y1 - y0},
                               (x0 - r.x) / r.w, (y0 - r.y) / r.h,
                               (x1 - r.x) / r.w, (y1 - r.y) / r.h,
                               Colors::White, OrientNone, Backend::Blend::Multiply);
            };
            sheen(r.x,       r.y + rad,          r.Right(),       r.Bottom() - rad);
            sheen(r.x + rad, r.y,                r.Right() - rad, r.y + rad);
            sheen(r.x + rad, r.Bottom() - rad,   r.Right() - rad, r.Bottom());
        }

        NineSlice(Sprite::PlateTop, Sprite::PlateBottom, win, corner, Colors::White);
    }

    // Shrinks a style until its text wraps into `maxLines` at `wrapWidth`.
    // The dialogue box holds exactly three lines of Normal at the layout's
    // own size, so anything longer would otherwise run out of the box; this
    // is what MessageBox uses. Each pass scales by sqrt(maxLines/lines),
    // since shrinking both shortens the lines and fits more on each, and it
    // never goes below `minScale` (past which the text is unreadable anyway).
    // minScale 0.4: a paragraph of a few hundred characters needs about that
    // much shrinking to reach the dialogue box's three lines, and stopping at
    // 0.5 left it at four - which fits the box, but is not what this promises.
    TextStyle FitToBox(const char* text, const TextStyle& style, float wrapWidth,
                       int maxLines = 3, float minScale = 0.4f) const {
        TextStyle s = style;
        if (!text || wrapWidth <= 0.0f || maxLines <= 0) return s;
        for (int pass = 0; pass < 6; ++pass) {
            const int lines = impl::CountLines(s, text, wrapWidth);
            if (lines <= maxLines || lines == 0) break;
            float k = static_cast<float>(maxLines) / static_cast<float>(lines);
            // sqrt, cheaply and without libm: two Newton steps are plenty here.
            float r = 0.5f * (k + 1.0f);
            r = 0.5f * (r + k / r);
            r = 0.5f * (r + k / r);
            s.scale *= r;
            if (s.scale <= minScale) { s.scale = minScale; break; }
        }
        return s;
    }

    // A controller button glyph (Nt_KeyTexA_00 etc.). The art is a 48 px
    // tile and the glyph fills it, so this is the drawn size.
    void ButtonIcon(Sprite key, float x, float y, float size = 32.0f, Color c = Colors::White) {
        Image(key, GUI::Rect{x, y, size, size}, c);
    }
    // Icon plus its label, laid out the way the game's key guides read: the
    // label is set at about two thirds of the icon's height and centred on
    // it, and (x, y) is the guide's top-left. Returns the width used, so
    // several can be placed in a row without measuring by hand.
    float KeyHint(Sprite key, const char* label, float x, float y, Color c = Colors::White,
                  float iconSize = 32.0f) {
        ButtonIcon(key, x, y, iconSize, c);
        const float textY = iconSize * 0.68f;
        TextStyle st = Styles::Small().WithColor(c);
        st.sizeY = textY;
        st.sizeX = textY * (31.0f / 39.0f);   // Normal's own cell ratio
        st.valign = VAlign::Middle;
        const float gap = iconSize * 0.2f;
        float w = 0.0f, h = 0.0f;
        MeasureText(label, st, w, h);
        TextBox(GUI::Rect{x + iconSize + gap, y, w + 4.0f, iconSize}, label, st, false);
        return iconSize + gap + w;
    }

    // ---- widgets ----------------------------------------------------------
    // All are option-row styled (PaOptionBtn_00): a black rounded box, white
    // NormalSmall label, and on focus the cream outline plus corner cursor.

    // Returns true on the frame the focused button is activated with A.
    bool Button(const GUI::Rect& r, const char* label, Color box = Colors::OptionBox) {
        const bool focused = ClaimFocus();
        DrawOptionRow(r, focused, box);
        TextBox(GUI::Rect{r.x + 16.0f, r.y, r.w - 32.0f, r.h}, label, Styles::Option(), false);
        return focused && Accept();
    }

    // The confirm-dialog style: light plate, dark Normal-font label, blue
    // select frame on focus.
    bool PlateButton(const GUI::Rect& r, const char* label) {
        const bool focused = ClaimFocus();
        Plate(r);
        TextBox(r, label, Styles::Plate(), false);
        // Clearance scaled to the plate's corner, not a fixed nudge and not
        // the plate's outer bound.
        //
        // Measured rather than assumed: the rim art's visible pixels start at
        // 0.365/0.406 of its 96px tile against the 0.375/0.417 padding Plate()
        // assumes, so the rim becomes visible essentially AT r - and
        // SelectFrame_04^t's stroke starts 1-2 px into its own 68px tile, so
        // the frame draws on the rect it is given. Neither art hides an offset;
        // the rect was just the wrong size.
        //
        // Two knobs, both scaled to the corner so they hold at any button size.
        // `clear` is how far the frame stands off the fill; `drop` shifts it
        // down, because the plate does not sit centred in what it draws - the
        // rim art's top padding is 40/96 of the corner against 24/96 at the
        // bottom, and Plate's shadow is offset down another 4 px, so a frame
        // centred on r rides high on the button it is framing.
        //
        // Arrived at by eye against the running game, not from the layout:
        // r.Inset(-4,-4) read tight, the plate's full outer bound overflowed
        // everywhere, and an even 0.25 of the corner still overhung the top.
        if (focused) {
            float corner = 0.0f;
            PlateBounds(r, &corner);
            const float clearX = 0.17f * corner;  // 4.1 px on the 170x44 button
            const float clearY = 0.21f * corner;  // 5.0 px - the height reads right
            const float drop   = 0.08f * corner;  // 2.0 px down
            SelectFrame(r.Inset(-clearX, -clearY).Offset(0.0f, drop));
        }
        return focused && Accept();
    }

    // Label on the left, ON/OFF on the right. Returns true when toggled.
    bool Toggle(const GUI::Rect& r, const char* label, bool& value, const char* onText = "ON", const char* offText = "OFF") {
        const bool focused = ClaimFocus();
        DrawOptionRow(r, focused, Colors::OptionBox);
        TextBox(GUI::Rect{r.x + 16.0f, r.y, r.w * 0.6f, r.h}, label, Styles::Option(), false);
        bool changed = false;
        if (focused && (Accept() || NavLeft() || NavRight())) { value = !value; changed = true; }
        DrawValueWithArrows(r, value ? onText : offText, focused);
        return changed;
    }

    // Label on the left, a bar and its value on the right. Left/right adjust
    // by `step`. Returns true when the value changed.
    bool Slider(const GUI::Rect& r, const char* label, float& value, float minValue, float maxValue, float step,
                const char* valueText = nullptr) {
        const bool focused = ClaimFocus();
        DrawOptionRow(r, focused, Colors::OptionBox);
        TextBox(GUI::Rect{r.x + 16.0f, r.y, r.w * 0.45f, r.h}, label, Styles::Option(), false);
        bool changed = false;
        if (focused && NavLeft() && value > minValue) { value -= step; if (value < minValue) value = minValue; changed = true; }
        if (focused && NavRight() && value < maxValue) { value += step; if (value > maxValue) value = maxValue; changed = true; }

        const float barX = r.x + r.w * 0.5f;
        const float barW = r.w * 0.5f - 96.0f;
        const float barH = 10.0f;
        const float barY = r.CenterY() - barH * 0.5f;
        const float t = maxValue > minValue ? (value - minValue) / (maxValue - minValue) : 0.0f;
        RoundedBox(GUI::Rect{barX, barY, barW, barH}, Colors::White.WithAlpha(60), barH * 0.5f);
        if (t > 0.0f) RoundedBox(GUI::Rect{barX, barY, barW * (t < 1.0f ? t : 1.0f), barH}, focused ? Colors::Cream : Colors::White, barH * 0.5f);

        char buf[24];
        const char* shown = valueText;
        if (!shown) { FormatFixed(buf, sizeof(buf), value, step < 1.0f ? 2 : 0); shown = buf; }
        TextBox(GUI::Rect{r.Right() - 88.0f, r.y, 72.0f, r.h}, shown, Styles::Option().WithAlign(Align::Right, VAlign::Middle), false);
        return changed;
    }

    // Label on the left, "< option >" on the right. Left/right cycle, A
    // advances. Returns true when the index changed.
    bool Selector(const GUI::Rect& r, const char* label, int& index, const char* const* options, int count) {
        const bool focused = ClaimFocus();
        DrawOptionRow(r, focused, Colors::OptionBox);
        TextBox(GUI::Rect{r.x + 16.0f, r.y, r.w * 0.5f, r.h}, label, Styles::Option(), false);
        bool changed = false;
        if (count > 0 && focused) {
            if (NavLeft()) { index = (index + count - 1) % count; changed = true; }
            if (NavRight() || Accept()) { index = (index + 1) % count; changed = true; }
        }
        if (index < 0) index = 0;
        if (count > 0 && index >= count) index = count - 1;
        DrawValueWithArrows(r, count > 0 ? options[index] : "", focused);
        return changed;
    }

    // A plain label row that takes no focus (section headings inside a list).
    // A list of `count` rows in a box that shows only as many as fit, with the
    // rest scrolled off. Returns true on the frame a row is activated with A;
    // `index` is the highlighted row and is updated as focus moves.
    //
    // How it fits the focus model: EVERY row claims a focus slot, not just the
    // visible ones, so the D-pad walks the whole list and the slot indices stay
    // put as it scrolls. Only the visible window is drawn. Without that, the
    // focusable count would change while scrolling and focus would jump to a
    // different item under the cursor.
    //
    // The window position is derived from `index` rather than stored, which
    // keeps the widget immediate-mode with no per-list state to own or reset:
    // the focused row sits in the middle of the window except at the two ends,
    // where it clamps.
    bool List(const GUI::Rect& r, const char* const* items, int count, int& index,
              float rowHeight = Metrics::kListRowHeight, float rowGap = Metrics::kListRowGap) {
        if (count <= 0 || rowHeight <= 0.0f) return false;
        const int base = impl::g_FocusCount;
        int focusedRow = -1;
        for (int i = 0; i < count; ++i) {
            if (ClaimFocus()) focusedRow = i;
        }
        if (focusedRow >= 0) index = focusedRow;
        if (index < 0) index = 0;
        if (index >= count) index = count - 1;
        (void)base;

        const float step = rowHeight + rowGap;
        int visible = static_cast<int>((r.h + rowGap) / step);
        if (visible < 1) visible = 1;
        if (visible > count) visible = count;
        const int maxFirst = count - visible;
        int first = index - (visible - 1) / 2;
        if (first > maxFirst) first = maxFirst;
        if (first < 0) first = 0;

        const bool scrolls = count > visible;
        const float trackW = Metrics::kListScrollbarWidth;
        const float rowW = scrolls ? r.w - trackW - Metrics::kListScrollbarGap : r.w;

        for (int i = first; i < first + visible; ++i) {
            const GUI::Rect row{r.x, r.y + (i - first) * step, rowW, rowHeight};
            DrawOptionRow(row, i == focusedRow, Colors::OptionBox);
            TextBox(GUI::Rect{row.x + 16.0f, row.y, row.w - 32.0f, row.h},
                    items[i], Styles::Option(), false);
        }

        if (scrolls) {
            // Track the full height, thumb proportional to the window and
            // positioned by `first`, so its size says how much is off-screen.
            const float trackX = r.Right() - trackW;
            const float trackH = visible * step - rowGap;
            Rect(GUI::Rect{trackX, r.y, trackW, trackH}, Colors::Dim);
            const float thumbH = trackH * static_cast<float>(visible) / static_cast<float>(count);
            const float travel = trackH - thumbH;
            const float t = maxFirst > 0 ? static_cast<float>(first) / static_cast<float>(maxFirst) : 0.0f;
            Rect(GUI::Rect{trackX, r.y + travel * t, trackW, thumbH}, Colors::OptionOutline);
        }

        return focusedRow >= 0 && Accept();
    }

    void Label(const GUI::Rect& r, const char* text, const TextStyle& style = Styles::Option()) {
        TextBox(GUI::Rect{r.x + 16.0f, r.y, r.w - 32.0f, r.h}, text, style, false);
    }

private:
    // Four copies of a top-left corner sprite at the rect's corners, drawn
    // to the art's own extent (impl::EmitSpriteArt) so they meet the
    // stretched edges exactly.
    // `shimmer` modulates each corner's brightness by the cloud field at its
    // centre (see impl::CloudField). One value per corner rather than per
    // vertex because these go through EmitSpriteArt, which carries the art's
    // padding insets and takes a single colour.
    void Corners(Sprite s, const GUI::Rect& r, float size, Color c,
                 const Backend::BlendState& blend = Backend::Blend::Alpha, float shimmer = 0.0f) {
        const float h = size * 0.5f;
        const float lx = r.x + h, rx = r.Right() - h;
        const float ty = r.y + h, by = r.Bottom() - h;
        impl::EmitSpriteArt(s, GUI::Rect{r.x, r.y, size, size}, impl::ShimmerAt(c, lx, ty, shimmer), OrientNone, blend);
        impl::EmitSpriteArt(s, GUI::Rect{r.Right() - size, r.y, size, size}, impl::ShimmerAt(c, rx, ty, shimmer), OrientFlipH, blend);
        impl::EmitSpriteArt(s, GUI::Rect{r.x, r.Bottom() - size, size, size}, impl::ShimmerAt(c, lx, by, shimmer), OrientFlipV, blend);
        impl::EmitSpriteArt(s, GUI::Rect{r.Right() - size, r.Bottom() - size, size, size}, impl::ShimmerAt(c, rx, by, shimmer), OrientFlipH | OrientFlipV, blend);
    }

    // Corners plus edges stretched from the corner texture's last column /
    // row - how lyt fills a window frame from one LT texture. The edge
    // coordinate is that texel's CENTRE (see impl::LastTexelU), which is the
    // only value that neither bleeds into its neighbour nor runs off the edge
    // whatever the texture's size.
    void FrameFromCorner(Sprite s, const GUI::Rect& r, float corner, Color c,
                         const Backend::BlendState& blend = Backend::Blend::Alpha, float shimmer = 0.0f) {
        Corners(s, r, corner, c, blend, shimmer);
        const Backend::TextureHandle tex = impl::SpriteTexture(s);
        const float eu = impl::EdgeU(s);
        const float ev = impl::EdgeV(s);
        const float midW = r.w - 2.0f * corner;
        const float midH = r.h - 2.0f * corner;
        // Top / bottom edges repeat the texture's inner column - eu for BOTH
        // coordinates, a degenerate range (see impl::EdgeU) - and the left /
        // right edges repeat its inner row. Spanning a range to 1.0 instead
        // fades the edge out along its own length wherever the art stops
        // short of the tile.
        // The edges take the field at BOTH ends and let the hardware
        // interpolate between them, so the shimmer runs along an edge
        // continuously instead of stepping from piece to piece.
        const float half = corner * 0.5f;
        if (midW > 0.0f) {
            const float x0 = r.x + corner, x1 = r.Right() - corner;
            const Color tl = impl::ShimmerAt(c, x0, r.y + half, shimmer);
            const Color tr = impl::ShimmerAt(c, x1, r.y + half, shimmer);
            const Color bl = impl::ShimmerAt(c, x0, r.Bottom() - half, shimmer);
            const Color br = impl::ShimmerAt(c, x1, r.Bottom() - half, shimmer);
            impl::EmitQuad(tex, r.x + corner, r.y, r.Right() - corner, r.y + corner,
                           eu, 0.0f, eu, 1.0f, tl, tr, tl, tr, OrientNone, blend);
            impl::EmitQuad(tex, r.x + corner, r.Bottom() - corner, r.Right() - corner, r.Bottom(),
                           eu, 0.0f, eu, 1.0f, bl, br, bl, br, OrientFlipV, blend);
        }
        if (midH > 0.0f) {
            const float y0 = r.y + corner, y1 = r.Bottom() - corner;
            const Color lt = impl::ShimmerAt(c, r.x + half, y0, shimmer);
            const Color lb = impl::ShimmerAt(c, r.x + half, y1, shimmer);
            const Color rt = impl::ShimmerAt(c, r.Right() - half, y0, shimmer);
            const Color rb = impl::ShimmerAt(c, r.Right() - half, y1, shimmer);
            impl::EmitQuad(tex, r.x, r.y + corner, r.x + corner, r.Bottom() - corner,
                           0.0f, ev, 1.0f, ev, lt, lt, lb, lb, OrientNone, blend);
            impl::EmitQuad(tex, r.Right() - corner, r.y + corner, r.Right(), r.Bottom() - corner,
                           0.0f, ev, 1.0f, ev, rt, rt, rb, rb, OrientFlipH, blend);
        }
    }

    // Top-row corner texture for the top, bottom-row texture for the bottom
    // (BtnBasic_08T / 08B), mirrored for the right side, edges from the
    // textures' inner strips.
    // A full nine-slice from two corner textures - top-row art for the upper
    // corners, bottom-row art for the lower ones, mirrored horizontally for
    // the right side, exactly the four frames BtnDialog_00's plate lists.
    // Unlike the window frames above this also fills the CENTRE, because
    // BtnBasic's corner art is the plate's own surface: its innermost texel
    // is the surface colour, so stretching that over the middle gives the
    // whole plate from the same two textures instead of guessing a fill.
    void NineSlice(Sprite top, Sprite bottom, const GUI::Rect& r, float corner, Color c) {
        const Backend::TextureHandle tt = impl::SpriteTexture(top);
        const Backend::TextureHandle tb = impl::SpriteTexture(bottom);
        const float eu = impl::EdgeU(top);
        const float evT = impl::EdgeV(top);
        const float evB = impl::EdgeV(bottom);
        const float midW = r.w - 2.0f * corner;
        const float midH = r.h - 2.0f * corner;

        // centre: the top art's innermost texel, repeated. For BtnBasic that
        // is the faint tail of the rim's inner glow (alpha 17%), which is why
        // Plate() puts an opaque fill underneath first.
        if (midW > 0.0f && midH > 0.0f) {
            impl::EmitQuad(tt, GUI::Rect{r.x + corner, r.y + corner, midW, midH}, eu, evT, eu, evT, c, OrientNone);
        }
        // left / right edges: the top art's inner row, repeated
        if (midH > 0.0f) {
            impl::EmitQuad(tt, GUI::Rect{r.x, r.y + corner, corner, midH}, 0.0f, evT, 1.0f, evT, c, OrientNone);
            impl::EmitQuad(tt, GUI::Rect{r.Right() - corner, r.y + corner, corner, midH}, 0.0f, evT, 1.0f, evT, c, OrientFlipH);
        }
        // top / bottom edges: each art's inner column, repeated
        if (midW > 0.0f) {
            impl::EmitQuad(tt, GUI::Rect{r.x + corner, r.y, midW, corner}, eu, 0.0f, eu, 1.0f, c, OrientNone);
            impl::EmitQuad(tb, GUI::Rect{r.x + corner, r.Bottom() - corner, midW, corner}, eu, 0.0f, eu, evB, c, OrientNone);
        }
        Image(top, GUI::Rect{r.x, r.y, corner, corner}, c, OrientNone);
        Image(top, GUI::Rect{r.Right() - corner, r.y, corner, corner}, c, OrientFlipH);
        impl::EmitQuad(tb, GUI::Rect{r.x, r.Bottom() - corner, corner, corner}, 0.0f, 0.0f, 1.0f, evB, c, OrientNone);
        impl::EmitQuad(tb, GUI::Rect{r.Right() - corner, r.Bottom() - corner, corner, corner}, 0.0f, 0.0f, 1.0f, evB, c, OrientFlipH);
    }

    void DrawOptionRow(const GUI::Rect& r, bool focused, Color box) {
        RoundedBox(r, box, Metrics::kRoundedCorner);
        // W_BaseLine_02 rests at alpha 8 on EVERY row and is animated up on
        // select - the row that was previously drawn with no line at all until
        // it gained focus, and then snapped to full. The resting line is what
        // the layout does; the ramp is a smoothstep standing in for the
        // .bflan curve, which nothing here parses.
        const float t = focused ? impl::FocusFade(Metrics::kFocusFadeSeconds) : 0.0f;
        const float lineA = static_cast<float>(Metrics::kOptionLineRest) +
                            (static_cast<float>(Metrics::kOptionLineSelected) -
                             static_cast<float>(Metrics::kOptionLineRest)) * t;
        RoundedOutline(r, Colors::OptionOutline.WithAlpha(static_cast<uint8_t>(lineA + 0.5f)),
                       Metrics::kRoundedCorner);
        if (focused) {
            // The cursor frame comes up with it. At t = 0 the colour is fully
            // transparent and EmitQuad drops the whole frame, so a row that
            // has just lost focus costs nothing.
            const float m = Metrics::kOptionCursorMargin;
            CursorCorners(r.Inset(-m, -m), Colors::Cream.WithAlpha(128).Scaled(t));
        }
    }

    void DrawValueWithArrows(const GUI::Rect& r, const char* value, bool focused) {
        const float arrow = 20.0f;
        const float valueW = r.w * 0.34f;
        const float valueX = r.Right() - 16.0f - arrow - valueW;
        TextBox(GUI::Rect{valueX, r.y, valueW, r.h}, value, Styles::Option().WithAlign(Align::Center, VAlign::Middle), false);
        const Color ac = focused ? Colors::Cream : Colors::White.WithAlpha(110);
        Image(Sprite::ArrowDown, GUI::Rect{valueX - arrow, r.CenterY() - arrow * 0.5f, arrow, arrow}, ac, OrientRotate90);
        Image(Sprite::ArrowDown, GUI::Rect{valueX + valueW, r.CenterY() - arrow * 0.5f, arrow, arrow}, ac, OrientRotate270);
    }

    // Fixed-point number formatting (no printf-float in the payload).
    static void FormatFixed(char* out, size_t cap, float value, int decimals) {
        size_t n = 0;
        auto put = [&](char ch) { if (n + 1 < cap) out[n++] = ch; };
        if (value < 0.0f) { put('-'); value = -value; }
        int scale = 1;
        for (int i = 0; i < decimals; ++i) scale *= 10;
        uint32_t whole = static_cast<uint32_t>(value);
        uint32_t frac = static_cast<uint32_t>((value - static_cast<float>(whole)) * static_cast<float>(scale) + 0.5f);
        if (frac >= static_cast<uint32_t>(scale)) { whole++; frac = 0; }
        char digits[12];
        int d = 0;
        do { digits[d++] = static_cast<char>('0' + whole % 10); whole /= 10; } while (whole && d < 11);
        while (d > 0) put(digits[--d]);
        if (decimals > 0) {
            put('.');
            for (int i = decimals - 1; i >= 0; --i) {
                uint32_t div = 1;
                for (int k = 0; k < i; ++k) div *= 10;
                put(static_cast<char>('0' + (frac / div) % 10));
            }
        }
        out[n] = '\0';
    }
};

namespace impl {

inline Canvas g_Canvas;

// Builds a frame: samples input, runs the mod's callback and records what it
// draws. This runs from Controller's input hook, before the game is allowed
// to see the pad - which is what lets Canvas::CaptureInput() hide the very
// press that opened a menu. It touches no GPU state.
inline void BuildFrame() {
    if (!g_Initialized || !g_PipelineReady || !EnsureRecord()) return;

    g_FrameCounter++;
    UpdateClock();
    UpdateInput();
    ApplyNavigationToFocus();
    ResetAlpha();

    g_RecordCount = 0;
    g_RecordBlendCount = 0;
    g_RecordOverflowed = false;
    g_BlurRequested = false;
    g_FocusCount = 0;
    g_QuadsThisFrame = 0;

    g_Recording = true;
    g_FrameOpen = true;
    if (g_FrameCallback) g_FrameCallback(g_Canvas);
    g_FrameOpen = false;
    g_Recording = false;
    ResetAlpha();   // a callback that pushed without popping must not leak into the next frame

    g_FocusCountLast = g_FocusCount;
    if (g_FocusIndex >= g_FocusCount) g_FocusIndex = g_FocusCount > 0 ? g_FocusCount - 1 : 0;
    if (g_FocusIndex < 0) g_FocusIndex = 0;

    g_FrameBuilt = true;
    // The frame just built has read last frame's tally; start a fresh one for
    // the draw passes that follow.
    g_ReportBestArea = 0;

    if (g_RecordOverflowed) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            OSLog("WiiXLaunch GUI: frame needs more than %u quads - the rest were dropped\n", kMaxRecordedQuads);
        }
    }
}

inline void OnDraw(Backend::CommandBuffer*, void* dst, int width, int height) {
    impl::UpdateFrameRate(dst);  // per scan buffer, deduped; see gui_render.hpp
    if (!g_Initialized || !g_PipelineReady) return;
    if (!LoaderFinished()) LoaderStep();

    // The colour buffer size is whatever the game handed the hook this frame
    // (854x480 for the GamePad view, more with a Cemu resolution pack), so
    // the layout-to-device mapping is recomputed rather than assumed. The
    // record holds layout coordinates, so it is correct whatever this is.
    UpdateViewport(static_cast<uint32_t>(width > 0 ? width : 1280),
                   static_cast<uint32_t>(height > 0 ? height : 720));
    g_FrameDst = dst;

    // Fallback: with no input hook installed (Controller::Init() not called),
    // nothing built a frame, so build it here. That is the old behaviour, and
    // it costs the one-frame capture delay - the game will have read the pad
    // before this runs.
    if (!g_FrameBuilt) BuildFrame();
    g_FrameBuilt = false;

    ReplayRecord(dst);
}

} // namespace impl

// Installs the GX2 frame hook (Backend::Init) and starts loading the game's
// fonts and UI art as soon as the graphics pipeline is up. Call once from
// WiiXLaunch_Init(), after Controller::Init() if widgets are to see input.
inline void Init() {
    if (impl::g_Initialized) return;
    impl::g_Initialized = true;
    Backend::Init();
    Backend::RegisterDrawCallback(&impl::OnDraw);
    Backend::OnInitialized(&impl::OnPipelineReady);
    // Build each frame from the input hook rather than at present time. That
    // is what makes Canvas::CaptureInput() able to hide the press that opened
    // a menu: at present time the game has already read the pad. Needs
    // Controller::Init() to have installed the hooks - without it the frame is
    // built at present time instead and capture lags by one frame.
    Controller::OnInputRead(&impl::BuildFrame);
    OSLog("WiiXLaunch GUI: initialised\n");
}

// The per-frame builder. One slot; call again to replace.
inline void OnFrame(FrameCallback callback) { impl::g_FrameCallback = callback; }

// Load a mod's own texture from the content mount - a `.bflim`, the format the
// game's own art is in, so tools/preview_ui_assets.py and pack_texture_gx2.py
// both understand it. Returns 0 on failure. The GUI never frees it.
//
// Only meaningful once the graphics pipeline is up, i.e. from inside a
// GUI/GX2 initialisation callback or later. Draw it with the Canvas::Image
// overloads that take a handle.
inline Backend::TextureHandle LoadTexture(const char* path, size_t maxFileSize = 1024 * 1024) {
    return Backend::LoadTexture(path, maxFileSize);
}

// True once the asset loader has finished (successfully or not). Fonts and
// sprites that did load draw before this; missing ones draw nothing.
inline bool IsReady() { return impl::LoaderFinished(); }

// How much of the archives to stream per frame while loading (64 KB
// compressed chunks; default 4). Raise to load faster at the cost of a
// longer hitch per frame, or call LoadNow() to block until done.
inline void SetLoadBudget(uint32_t chunksPerFrame) { impl::g_Loader.chunksPerStep = chunksPerFrame ? chunksPerFrame : 1; }

// Load one of the game's other faces as well as Normal. Must be called before
// the first frame after Init(), like SetAssetPaths - by the time the loader has
// started, the archive has already gone past.
//
// Only Normal_00 is loaded by default: all six are 2.9 MB of glyph sheets and
// the payload heap is 6 MB. FontSheetBytes(id) is what one costs. A style
// naming a font that was never requested falls back to Normal, so this changes
// how text looks, never whether it appears.
inline void RequestFont(FontId f, bool load = true) {
    if (f < FontId::Count) impl::g_Fonts[static_cast<size_t>(f)].load = load;
}
inline bool FontRequested(FontId f) {
    return f < FontId::Count && impl::g_Fonts[static_cast<size_t>(f)].load;
}
// Glyph sheet bytes this font takes on the heap once loaded.
inline uint32_t FontSheetBytes(FontId f) {
    return f < FontId::Count ? impl::g_Fonts[static_cast<size_t>(f)].sheetBytes : 0;
}

// Overrides for where the assets come from (defaults: Font/Font_US.sbfarc
// then EU/JP; Layout/Common.sblarc loose, else inside Pack/Bootup.pack).
// Must be called before the first frame after Init().
inline void SetAssetPaths(const char* fontArchive, const char* layoutArchive) {
    impl::g_Loader.fontArchivePath = fontArchive;
    impl::g_Loader.layoutArchivePath = layoutArchive;
}

// Runs the loader to completion right now (only meaningful once the GX2
// pipeline is up, i.e. from inside a Backend::OnInitialized callback or later).
inline void LoadNow() {
    if (!impl::g_PipelineReady) return;
    impl::LoaderRunToCompletion();
}

inline uint32_t QuadsLastFrame() { return impl::g_QuadsThisFrame; }

// How the 1280x720 layout rectangle is mapped onto the real colour buffer.
// Fit (the default) keeps the aspect ratio and centres, so a non-16:9 buffer
// letterboxes instead of stretching the UI; Stretch fills the buffer. The
// two are identical on any 16:9 buffer.
using ScalingMode = impl::ScalingMode;
inline void SetScalingMode(ScalingMode mode) { impl::g_ScalingMode = mode; }
inline ScalingMode GetScalingMode() { return impl::g_ScalingMode; }

// Snap flat fills and art to whole device pixels (on by default; ignored
// when a layout pixel is smaller than a device pixel, and never applied to
// glyphs). Turn it off for smoothly animating positions.
inline void SetPixelSnapping(bool on) { impl::g_SnapToPixels = on; }

// Override the aspect ratio the frame is presented at. 0 - the default - is
// AUTOMATIC and is normally what you want: it reads the game's own aspect
// constant, which a Cemu ultrawide pack has to rewrite for the game itself to
// render correctly, so 21:9 is handled with no configuration at all. See
// game/display.hpp. Set it only to override that.
inline void SetOutputAspect(float aspect) { impl::g_OutputAspect = aspect > 0.0f ? aspect : 0.0f; }
inline float GetOutputAspect() { return impl::g_OutputAspect; }

// What the mapping is actually using, however it was arrived at - detected,
// overridden, or the colour buffer's own shape.
inline float GetEffectiveOutputAspect() {
    const float bufferAspect = static_cast<float>(impl::g_ReportWidth) /
                               static_cast<float>(impl::g_ReportHeight);
    return impl::ResolveOutputAspect(bufferAspect);
}

// The game's measured frame rate, smoothed. Real, not assumed: BotW is 30 by
// default and 60 with the FPS++ pack.
inline float FramesPerSecond() { return impl::g_Fps; }

// Backdrop blur - BotW's frosted glass behind its windows. OFF by default:
// it costs two render targets (a quarter-size pair, ~460 KB) and a handful of
// full-screen draws on any frame that uses it, and an overlay that is not
// drawing windows has no use for it. Turning it on makes Canvas::BlurBehind()
// work, and MessageWindow/MessageBox pick it up automatically.
//
// `downscale` is how much smaller the working copy is than the screen and
// `passes` how many extra four-tap passes run over it: 4 and 2 is a soft,
// cheap frost; 2 and 4 is heavier in both senses.
inline void SetBackdropBlur(bool enabled, uint32_t downscale = 4, uint32_t passes = 2) {
    impl::g_BlurEnabled = enabled;
    impl::g_BlurDownscale = downscale ? downscale : 1;
    impl::g_BlurPasses = passes;
}

inline bool IsBackdropBlurEnabled() { return impl::g_BlurEnabled; }

} // namespace WiiXLaunch::BotW::GUI

#else

// Switch / NVN: not implemented. Everything is a no-op so a mod's GUI code
// compiles unchanged; check SupportsGUI to branch.
namespace WiiXLaunch::BotW::GUI {

constexpr bool SupportsGUI = false;

class Canvas {
public:
    uint32_t Frame() const { return 0; }
    float Width() const { return kVirtualWidth; }
    float Height() const { return kVirtualHeight; }
    uint32_t DeviceWidth() const { return 0; }
    uint32_t DeviceHeight() const { return 0; }
    float PixelScaleX() const { return 1.0f; }
    float PixelScaleY() const { return 1.0f; }
    float ViewportOffsetX() const { return 0.0f; }
    float ViewportOffsetY() const { return 0.0f; }
    float SnapX(float x) const { return x; }
    float SnapY(float y) const { return y; }
    void PushAlpha(float) {}
    void PopAlpha() {}
    float CurrentAlpha() const { return 1.0f; }
    bool AssetsReady() const { return false; }
    bool FontReady(FontId = FontId::Normal) const { return false; }
    // (Switch stub; the real ones are in the GX2 build.)
    bool SpriteReady(Sprite) const { return false; }
    bool Pressed(Button) const { return false; }
    bool Held(Button) const { return false; }
    bool Accept() const { return false; }
    bool Cancel() const { return false; }
    bool NavUp() const { return false; }
    bool NavDown() const { return false; }
    bool NavLeft() const { return false; }
    bool NavRight() const { return false; }
    void GetLeftStick(float& x, float& y) const { x = 0; y = 0; }
    void GetRightStick(float& x, float& y) const { x = 0; y = 0; }
    void CaptureInput() {}
    bool IsInputCaptured() const { return false; }
    float DeltaSeconds() const { return 0.0f; }
    float TimeSeconds() const { return 0.0f; }
    float FramesPerSecond() const { return 0.0f; }
    float Phase(float) const { return 0.0f; }
    float Wave(float) const { return 0.0f; }
    void BlurBehind(const GUI::Rect&, Color = Colors::White) {}
    void BlurBehindFaded(const GUI::Rect&, Color = Colors::White, float = 20.0f, float = 10.0f) {}
    void FrostedBox(const GUI::Rect&, Color, float = Metrics::kRoundedCorner) {}
    int Focus() const { return 0; }
    void SetFocus(int) {}
    int FocusableCount() const { return 0; }
    bool ClaimFocus() { return false; }
    void Rect(const GUI::Rect&, Color, const Backend::BlendState& = Backend::Blend::Alpha) {}
    void RectGradient(const GUI::Rect&, Color, Color, const Backend::BlendState& = Backend::Blend::Alpha) {}
    void Image(Sprite, const GUI::Rect&, Color = Colors::White, uint8_t = 0,
               const Backend::BlendState& = Backend::Blend::Alpha, float = 0.0f) {}
    void ImageUV(Sprite, const GUI::Rect&, float, float, float, float, Color = Colors::White, uint8_t = 0,
                 const Backend::BlendState& = Backend::Blend::Alpha) {}
    void ImageAt(Sprite, float, float, Color = Colors::White, uint8_t = 0, float = 1.0f,
                 const Backend::BlendState& = Backend::Blend::Alpha) {}
    void SpriteSize(Sprite, float& w, float& h) const { w = 0; h = 0; }
    void Image(Backend::TextureHandle, const GUI::Rect&, Color = Colors::White, uint8_t = 0,
               const Backend::BlendState& = Backend::Blend::Alpha, float = 0.0f) {}
    void ImageUV(Backend::TextureHandle, const GUI::Rect&, float, float, float, float,
                 Color = Colors::White, uint8_t = 0,
                 const Backend::BlendState& = Backend::Blend::Alpha) {}
    bool ImageAt(Backend::TextureHandle, float, float, Color = Colors::White, uint8_t = 0,
                 float = 1.0f, const Backend::BlendState& = Backend::Blend::Alpha) { return false; }
    bool TextureSize(Backend::TextureHandle, float& w, float& h) const { w = 0; h = 0; return false; }
    void Text(float, float, const char*, const TextStyle& = Styles::Message()) {}
    void TextBox(const GUI::Rect&, const char*, const TextStyle&, bool = true) {}
    void MeasureText(const char*, const TextStyle&, float& w, float& h, float = 0.0f) const { w = 0; h = 0; }
    TextStyle FitToBox(const char*, const TextStyle& style, float, int = 3, float = 0.4f) const { return style; }
    void MessageWindow(const GUI::Rect&, Color = Colors::MessageWindow, bool = true) {}
    void MessageBox(const char*, const char* = nullptr, float = 1.0f, bool = false) {}
    void RoundedBox(const GUI::Rect&, Color, float = Metrics::kRoundedCorner) {}
    void RoundedOutline(const GUI::Rect&, Color, float = Metrics::kRoundedCorner, float = 0.0f) {}
    void SelectFrame(const GUI::Rect&, Color = Colors::SelectFrame, Color = Colors::SelectGlow) {}
    void CursorCorners(const GUI::Rect&, Color = Colors::Cream, float = 48.0f,
                       const Backend::BlendState& = Backend::Blend::Alpha, float = 0.0f) {}
    // (stub Canvas::Image carries the rotation parameter too, below)
    void CursorBrackets(const GUI::Rect&, Color = Colors::White, float = 64.0f) {}
    void BoxedCursor(const GUI::Rect&, Color = Colors::White) {}
    void Plate(const GUI::Rect&, Color = Colors::Plate) {}
    void ButtonIcon(Sprite, float, float, float = 32.0f, Color = Colors::White) {}
    float KeyHint(Sprite, const char*, float, float, Color = Colors::White, float = 32.0f) { return 0.0f; }
    bool Button(const GUI::Rect&, const char*, Color = Colors::OptionBox) { return false; }
    bool PlateButton(const GUI::Rect&, const char*) { return false; }
    bool Toggle(const GUI::Rect&, const char*, bool&, const char* = "ON", const char* = "OFF") { return false; }
    bool Slider(const GUI::Rect&, const char*, float&, float, float, float, const char* = nullptr) { return false; }
    bool Selector(const GUI::Rect&, const char*, int&, const char* const*, int) { return false; }
    bool List(const GUI::Rect&, const char* const*, int, int&, float = 40.0f, float = 2.0f) { return false; }
    void Label(const GUI::Rect&, const char*, const TextStyle& = Styles::Option()) {}
};

using FrameCallback = void (*)(Canvas& canvas);

inline void Init() {}
inline void OnFrame(FrameCallback) {}
inline bool IsReady() { return false; }
inline void SetLoadBudget(uint32_t) {}
inline void SetAssetPaths(const char*, const char*) {}
inline Backend::TextureHandle LoadTexture(const char*, size_t = 0) { return 0; }
inline void LoadNow() {}
inline uint32_t QuadsLastFrame() { return 0; }

enum class ScalingMode : uint8_t { Fit, Stretch };
inline void SetScalingMode(ScalingMode) {}
inline ScalingMode GetScalingMode() { return ScalingMode::Fit; }
inline void SetPixelSnapping(bool) {}
inline void RequestFont(FontId, bool = true) {}
inline bool FontRequested(FontId) { return false; }
inline uint32_t FontSheetBytes(FontId) { return 0; }
inline void SetOutputAspect(float) {}
inline float GetOutputAspect() { return 0.0f; }
inline float GetEffectiveOutputAspect() { return 16.0f / 9.0f; }
inline float FramesPerSecond() { return 0.0f; }
inline void SetBackdropBlur(bool, uint32_t = 4, uint32_t = 2) {}
inline bool IsBackdropBlurEnabled() { return false; }

} // namespace WiiXLaunch::BotW::GUI

#endif
