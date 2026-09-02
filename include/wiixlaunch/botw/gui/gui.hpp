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
// GX2 only. On Switch (NVN) every call compiles to a no-op and
// SupportsGUI is false - the NVN backend is a stub for now.

#if WIIXL_CEMU || WIIXL_WIIU

#include "gui_render.hpp"
#include "gui_assets.hpp"
#include "gui_text.hpp"
#include "../graphics/gx2.hpp"
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

// Input: edge detection and D-pad/stick navigation with key repeat, in
// terms of Controller's canonical hold bits.
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

inline void ApplyNavigationToFocus() {
    if (g_FocusCountLast <= 0) { g_FocusIndex = 0; return; }
    g_FocusMoved = false;
    if (g_Input.navFired & NavDownBit) { g_FocusIndex = (g_FocusIndex + 1) % g_FocusCountLast; g_FocusMoved = true; }
    if (g_Input.navFired & NavUpBit) { g_FocusIndex = (g_FocusIndex + g_FocusCountLast - 1) % g_FocusCountLast; g_FocusMoved = true; }
}

inline void EnsureWhiteSprite() {
    SpriteInfo& white = SpriteAt(Sprite::White);
    if (white.texture) return;
    // 8x8, a whole GX2 micro-tile: CreateTexture only writes the texels the
    // image covers, so a smaller texture leaves the rest of the tile holding
    // whatever happened to be in that memory.
    static uint8_t pixels[8 * 8 * 4];
    for (uint32_t i = 0; i < sizeof(pixels); ++i) pixels[i] = 0xFF;
    white.texture = GX2::CreateTexture(pixels, sizeof(pixels), 8, 8, GX2Types::kSurfaceFormatUnormR8G8B8A8);
    white.width = 8;
    white.height = 8;
}

inline void OnPipelineReady() {
    g_PipelineReady = true;
    EnsureWhiteSprite();
}

inline void OnDraw(GX2::CommandBuffer* cmdBuf, void* dst, int width, int height);

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
    uint32_t DeviceWidth() const { return impl::g_DeviceWidth; }
    uint32_t DeviceHeight() const { return impl::g_DeviceHeight; }
    // Device pixels per layout pixel (equal on both axes unless the scaling
    // mode is Stretch).
    float PixelScaleX() const { return impl::g_ScaleX; }
    float PixelScaleY() const { return impl::g_ScaleY; }
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
    void Rect(const GUI::Rect& r, Color c, const GX2::BlendState& blend = GX2::Blend::Alpha) {
        impl::EmitRect(r, c, blend);
    }
    void RectGradient(const GUI::Rect& r, Color top, Color bottom, const GX2::BlendState& blend = GX2::Blend::Alpha) {
        impl::EmitRectGradient(r, top, bottom, blend);
    }
    // rotation is in degrees clockwise about the rect's centre. Angles taken
    // from a .bflyt need their sign flipped: lyt panes are y-up.
    void Image(Sprite s, const GUI::Rect& r, Color tint = Colors::White, uint8_t orient = OrientNone,
               const GX2::BlendState& blend = GX2::Blend::Alpha, float rotation = 0.0f) {
        impl::EmitSprite(s, r, tint, orient, blend, rotation);
    }
    void ImageUV(Sprite s, const GUI::Rect& r, float u0, float v0, float u1, float v1,
                 Color tint = Colors::White, uint8_t orient = OrientNone,
                 const GX2::BlendState& blend = GX2::Blend::Alpha) {
        impl::EmitQuad(impl::SpriteTexture(s), r, u0, v0, u1, v1, tint, orient, blend);
    }
    // Draws a sprite at its native pixel size with its top-left at (x, y).
    void ImageAt(Sprite s, float x, float y, Color tint = Colors::White, uint8_t orient = OrientNone,
                 float scale = 1.0f, const GX2::BlendState& blend = GX2::Blend::Alpha) {
        const impl::SpriteInfo& info = impl::SpriteAt(s);
        if (!info.texture) return;
        impl::EmitSprite(s, GUI::Rect{x, y, info.width * scale, info.height * scale}, tint, orient, blend);
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
            const GX2::BlendState& db = GX2::Blend::Overlay;
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
            // The game backs the name with P_Sh_00, which is a BLURRED CAPTURE
            // of the framebuffer (FBLayout_00^r) at alpha 180 - not a shadow
            // sprite. There is no blur here, and the stand-in that was used
            // instead (DialogShadow_00, which is one quarter of a radial
            // gradient and so drew as a hard dark box) looked far worse than
            // nothing. The name's own hard black text shadow carries it.
            TextBox(GUI::Rect{Metrics::kMessageNameX, Metrics::kMessageNameCenterY - 13.0f, 176.0f, 26.0f}, name,
                    Styles::Name().Alpha(alpha), false);
        }
        if (showArrow) {
            // Nt_ArrowMsg_00: the "more" arrow at the bottom-right, bobbing.
            const float bob = ((impl::g_FrameCounter / 8) % 4) * 1.0f;
            ImageAt(Sprite::ArrowMsg, win.Right() - 70.0f, win.Bottom() - 36.0f + bob, Colors::Cream.Scaled(alpha), OrientNone, 0.75f);
        }
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
    void CursorCorners(const GUI::Rect& r, Color c = Colors::Cream.WithAlpha(128), float size = 48.0f,
                       const GX2::BlendState& blend = GX2::Blend::Additive) {
        if (!SpriteReady(Sprite::CursorCorner)) { RoundedOutline(r, c); return; }
        FrameFromCorner(Sprite::CursorCorner, r, size, c, blend);
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
        const float pulse = 1.0f + 0.08f * static_cast<float>((impl::g_FrameCounter / 10) % 2);
        const float s = Metrics::kBoxedCursorArrow * pulse;
        const float o = 3.0f;   // the layout's own diagonal nudge
        const float h = s * 0.5f;
        Image(Sprite::ArrowDown, GUI::Rect{r.x - h - o, r.y - h - o, s, s}, c, OrientNone, GX2::Blend::Alpha, 135.0f);
        Image(Sprite::ArrowDown, GUI::Rect{r.Right() - h + o, r.y - h - o, s, s}, c, OrientNone, GX2::Blend::Alpha, 225.0f);
        Image(Sprite::ArrowDown, GUI::Rect{r.x - h - o, r.Bottom() - h + o, s, s}, c, OrientNone, GX2::Blend::Alpha, 45.0f);
        Image(Sprite::ArrowDown, GUI::Rect{r.Right() - h + o, r.Bottom() - h + o, s, s}, c, OrientNone, GX2::Blend::Alpha, -45.0f);
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
        constexpr float kCornerOfWindow = 96.0f / 240.0f;
        float corner = kCornerOfWindow * r.h / (1.0f - kCornerOfWindow * (kPadT + kPadB));
        if (corner > Metrics::kPlateCorner) corner = Metrics::kPlateCorner;   // never upscale past the art
        const float maxW = r.w / (2.0f - 2.0f * kPadL);
        if (corner > maxW) corner = maxW;
        const GUI::Rect win{r.x - kPadL * corner, r.y - kPadT * corner,
                            r.w + 2.0f * kPadL * corner, r.h + (kPadT + kPadB) * corner};

        if (SpriteReady(Sprite::PlateShadowTop) && SpriteReady(Sprite::PlateShadowBottom)) {
            NineSlice(Sprite::PlateShadowTop, Sprite::PlateShadowBottom,
                      GUI::Rect{win.x, win.y + 4.0f, win.w, win.h}, corner, Colors::Black.WithAlpha(150));
        }
        // The base fills exactly the visible plate. It must not be grown
        // past it: the rim's own edge is soft, so a hard fill poking out
        // beyond it reads as a second, doubled outline.
        RoundedBox(r, fill, kSurfaceRadius * corner);
        NineSlice(Sprite::PlateTop, Sprite::PlateBottom, win, corner, Colors::White);
    }

    // Shrinks a style until its text wraps into `maxLines` at `wrapWidth`.
    // The dialogue box holds exactly three lines of Normal at the layout's
    // own size, so anything longer would otherwise run out of the box; this
    // is what MessageBox uses. Each pass scales by sqrt(maxLines/lines),
    // since shrinking both shortens the lines and fits more on each, and it
    // never goes below `minScale` (past which the text is unreadable anyway).
    TextStyle FitToBox(const char* text, const TextStyle& style, float wrapWidth,
                       int maxLines = 3, float minScale = 0.5f) const {
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
        // BtnDialog_00: a 530x186 frame on a 560x240 window whose visible
        // plate is ~488x176, so the frame runs a little outside the plate.
        if (focused) SelectFrame(r.Inset(-4.0f, -4.0f));
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
    void Label(const GUI::Rect& r, const char* text, const TextStyle& style = Styles::Option()) {
        TextBox(GUI::Rect{r.x + 16.0f, r.y, r.w - 32.0f, r.h}, text, style, false);
    }

private:
    // Four copies of a top-left corner sprite at the rect's corners, drawn
    // to the art's own extent (impl::EmitSpriteArt) so they meet the
    // stretched edges exactly.
    void Corners(Sprite s, const GUI::Rect& r, float size, Color c,
                 const GX2::BlendState& blend = GX2::Blend::Alpha) {
        impl::EmitSpriteArt(s, GUI::Rect{r.x, r.y, size, size}, c, OrientNone, blend);
        impl::EmitSpriteArt(s, GUI::Rect{r.Right() - size, r.y, size, size}, c, OrientFlipH, blend);
        impl::EmitSpriteArt(s, GUI::Rect{r.x, r.Bottom() - size, size, size}, c, OrientFlipV, blend);
        impl::EmitSpriteArt(s, GUI::Rect{r.Right() - size, r.Bottom() - size, size, size}, c, OrientFlipH | OrientFlipV, blend);
    }

    // Corners plus edges stretched from the corner texture's last column /
    // row - how lyt fills a window frame from one LT texture. The edge
    // coordinate is that texel's CENTRE (see impl::LastTexelU), which is the
    // only value that neither bleeds into its neighbour nor runs off the edge
    // whatever the texture's size.
    void FrameFromCorner(Sprite s, const GUI::Rect& r, float corner, Color c,
                         const GX2::BlendState& blend = GX2::Blend::Alpha) {
        Corners(s, r, corner, c, blend);
        const GX2::TextureHandle tex = impl::SpriteTexture(s);
        const float eu = impl::EdgeU(s);
        const float ev = impl::EdgeV(s);
        const float midW = r.w - 2.0f * corner;
        const float midH = r.h - 2.0f * corner;
        // Top / bottom edges repeat the texture's inner column - eu for BOTH
        // coordinates, a degenerate range (see impl::EdgeU) - and the left /
        // right edges repeat its inner row. Spanning a range to 1.0 instead
        // fades the edge out along its own length wherever the art stops
        // short of the tile.
        if (midW > 0.0f) {
            impl::EmitQuad(tex, GUI::Rect{r.x + corner, r.y, midW, corner}, eu, 0.0f, eu, 1.0f, c, OrientNone, blend);
            impl::EmitQuad(tex, GUI::Rect{r.x + corner, r.Bottom() - corner, midW, corner}, eu, 0.0f, eu, 1.0f, c, OrientFlipV, blend);
        }
        if (midH > 0.0f) {
            impl::EmitQuad(tex, GUI::Rect{r.x, r.y + corner, corner, midH}, 0.0f, ev, 1.0f, ev, c, OrientNone, blend);
            impl::EmitQuad(tex, GUI::Rect{r.Right() - corner, r.y + corner, corner, midH}, 0.0f, ev, 1.0f, ev, c, OrientFlipH, blend);
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
        const GX2::TextureHandle tt = impl::SpriteTexture(top);
        const GX2::TextureHandle tb = impl::SpriteTexture(bottom);
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
        if (focused) {
            // W_BaseLine_02 rests at alpha 8 and is animated up on select;
            // the selected value lives in the .bflan, so this is a moderate
            // stand-in. The cursor frame outside it carries the highlight.
            RoundedOutline(r, Colors::OptionOutline.WithAlpha(140), Metrics::kRoundedCorner);
            const float m = Metrics::kOptionCursorMargin;
            CursorCorners(r.Inset(-m, -m));
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

inline void OnDraw(GX2::CommandBuffer*, void* dst, int width, int height) {
    if (!g_Initialized || !g_PipelineReady) return;
    g_FrameCounter++;
    if (!LoaderFinished()) LoaderStep();

    UpdateInput();
    ApplyNavigationToFocus();

    // The colour buffer size is whatever the game handed the hook this frame
    // (854x480 for the GamePad view, more with a Cemu resolution pack), so
    // the layout-to-device mapping is recomputed rather than assumed.
    UpdateViewport(static_cast<uint32_t>(width > 0 ? width : 1280),
                   static_cast<uint32_t>(height > 0 ? height : 720));
    ResetAlpha();

    g_FrameDst = dst;
    g_FocusCount = 0;
    g_QuadsThisFrame = 0;
    GX2::BeginBatch(dst);
    g_FrameOpen = true;
    if (g_FrameCallback) g_FrameCallback(g_Canvas);
    g_FrameOpen = false;
    GX2::EndBatch();
    ResetAlpha();   // a callback that pushed without popping must not leak into the next frame

    g_FocusCountLast = g_FocusCount;
    if (g_FocusIndex >= g_FocusCount) g_FocusIndex = g_FocusCount > 0 ? g_FocusCount - 1 : 0;
    if (g_FocusIndex < 0) g_FocusIndex = 0;
}

} // namespace impl

// Installs the GX2 frame hook (GX2::Init) and starts loading the game's
// fonts and UI art as soon as the graphics pipeline is up. Call once from
// WiiXLaunch_Init(), after Controller::Init() if widgets are to see input.
inline void Init() {
    if (impl::g_Initialized) return;
    impl::g_Initialized = true;
    GX2::Init();
    GX2::RegisterDrawCallback(&impl::OnDraw);
    GX2::OnInitialized(&impl::OnPipelineReady);
    OSLog("WiiXLaunch GUI: initialised\n");
}

// The per-frame builder. One slot; call again to replace.
inline void OnFrame(FrameCallback callback) { impl::g_FrameCallback = callback; }

// True once the asset loader has finished (successfully or not). Fonts and
// sprites that did load draw before this; missing ones draw nothing.
inline bool IsReady() { return impl::LoaderFinished(); }

// How much of the archives to stream per frame while loading (64 KB
// compressed chunks; default 4). Raise to load faster at the cost of a
// longer hitch per frame, or call LoadNow() to block until done.
inline void SetLoadBudget(uint32_t chunksPerFrame) { impl::g_Loader.chunksPerStep = chunksPerFrame ? chunksPerFrame : 1; }

// Overrides for where the assets come from (defaults: Font/Font_US.sbfarc
// then EU/JP; Layout/Common.sblarc loose, else inside Pack/Bootup.pack).
// Must be called before the first frame after Init().
inline void SetAssetPaths(const char* fontArchive, const char* layoutArchive) {
    impl::g_Loader.fontArchivePath = fontArchive;
    impl::g_Loader.layoutArchivePath = layoutArchive;
}

// Runs the loader to completion right now (only meaningful once the GX2
// pipeline is up, i.e. from inside a GX2::OnInitialized callback or later).
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
    int Focus() const { return 0; }
    void SetFocus(int) {}
    int FocusableCount() const { return 0; }
    bool ClaimFocus() { return false; }
    void Rect(const GUI::Rect&, Color, const GX2::BlendState& = GX2::Blend::Alpha) {}
    void RectGradient(const GUI::Rect&, Color, Color, const GX2::BlendState& = GX2::Blend::Alpha) {}
    void Image(Sprite, const GUI::Rect&, Color = Colors::White, uint8_t = 0,
               const GX2::BlendState& = GX2::Blend::Alpha, float = 0.0f) {}
    void ImageUV(Sprite, const GUI::Rect&, float, float, float, float, Color = Colors::White, uint8_t = 0,
                 const GX2::BlendState& = GX2::Blend::Alpha) {}
    void ImageAt(Sprite, float, float, Color = Colors::White, uint8_t = 0, float = 1.0f,
                 const GX2::BlendState& = GX2::Blend::Alpha) {}
    void SpriteSize(Sprite, float& w, float& h) const { w = 0; h = 0; }
    void Text(float, float, const char*, const TextStyle& = Styles::Message()) {}
    void TextBox(const GUI::Rect&, const char*, const TextStyle&, bool = true) {}
    void MeasureText(const char*, const TextStyle&, float& w, float& h, float = 0.0f) const { w = 0; h = 0; }
    TextStyle FitToBox(const char*, const TextStyle& style, float, int = 3, float = 0.5f) const { return style; }
    void MessageWindow(const GUI::Rect&, Color = Colors::MessageWindow, bool = true) {}
    void MessageBox(const char*, const char* = nullptr, float = 1.0f, bool = false) {}
    void RoundedBox(const GUI::Rect&, Color, float = Metrics::kRoundedCorner) {}
    void RoundedOutline(const GUI::Rect&, Color, float = Metrics::kRoundedCorner, float = 0.0f) {}
    void SelectFrame(const GUI::Rect&, Color = Colors::SelectFrame, Color = Colors::SelectGlow) {}
    void CursorCorners(const GUI::Rect&, Color = Colors::Cream, float = 48.0f,
                       const GX2::BlendState& = GX2::Blend::Alpha) {}
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
    void Label(const GUI::Rect&, const char*, const TextStyle& = Styles::Option()) {}
};

using FrameCallback = void (*)(Canvas& canvas);

inline void Init() {}
inline void OnFrame(FrameCallback) {}
inline bool IsReady() { return false; }
inline void SetLoadBudget(uint32_t) {}
inline void SetAssetPaths(const char*, const char*) {}
inline void LoadNow() {}
inline uint32_t QuadsLastFrame() { return 0; }

enum class ScalingMode : uint8_t { Fit, Stretch };
inline void SetScalingMode(ScalingMode) {}
inline ScalingMode GetScalingMode() { return ScalingMode::Fit; }
inline void SetPixelSnapping(bool) {}

} // namespace WiiXLaunch::BotW::GUI

#endif
