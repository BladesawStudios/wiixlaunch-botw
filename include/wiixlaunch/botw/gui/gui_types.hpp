#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstdint>
#include <cstddef>

#include "../graphics/gx2.hpp"   // GX2::BlendState / GX2::Blend presets

// WiiXLaunch::BotW::GUI - shared value types and the game's own UI numbers.
//
// Every colour, font size and pixel measurement in here was read out of the
// game's layouts (Layout/Common.sblarc: Message_00.bflyt, PaOptionBtn_00,
// BtnDialog_00, PaBoxedCursor_00 ...) - see docs/gui.md for the dump each
// one comes from. A mod that wants to look like the base game uses the
// Styles:: presets untouched; everything is plain data so it can also be
// tweaked per call.
//
// Coordinates: the layout space the game itself authors in - 1280 x 720,
// origin top-left, y down, in pixels. The renderer maps this onto whatever
// the real colour buffer is (720p, or a graphic-pack resolution on Cemu).

namespace WiiXLaunch::BotW::GUI {

constexpr float kVirtualWidth = 1280.0f;
constexpr float kVirtualHeight = 720.0f;

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;

    constexpr Color WithAlpha(uint8_t alpha) const { return Color{r, g, b, alpha}; }
    // Multiplies alpha by a 0..1 factor (pane alpha stacking, fades).
    constexpr Color Scaled(float factor) const {
        float v = static_cast<float>(a) * factor;
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        return Color{r, g, b, static_cast<uint8_t>(v + 0.5f)};
    }
};

namespace Colors {
    constexpr Color White{255, 255, 255, 255};
    constexpr Color Black{0, 0, 0, 255};
    constexpr Color Transparent{0, 0, 0, 0};
    // Dialogue text: Message_00 T_Message_00 material colour. The warm
    // off-white every line of dialogue in the game is set in.
    constexpr Color MessageText{255, 252, 198, 255};
    constexpr Color Cream = MessageText;
    // Message_00 T_Message_00 shadow (offset +2,+2).
    constexpr Color MessageShadow{0, 0, 0, 100};
    // Name plate / menu labels use a solid black shadow.
    constexpr Color HardShadow{0, 0, 0, 255};
    // Message_00 W_Base_00: black material under pane alpha 230.
    constexpr Color MessageWindow{0, 0, 0, 230};
    // PaOptionBtn_00 W_Base_00C: the rounded box behind an option row.
    constexpr Color OptionBox{0, 0, 0, 200};
    // PaOptionBtn_00 W_BaseLine_02LT: the cream outline of the focused row.
    constexpr Color OptionOutline{255, 252, 198, 180};
    // BtnDialog_00 T_BtnDialog: dark text on the light button plate.
    constexpr Color PlateText{40, 40, 40, 255};
    constexpr Color Plate{236, 236, 236, 255};
    constexpr Color PlateEdge{255, 255, 255, 255};
    // BtnDialog_00 W_SelectFrame_00LT: the Sheikah-blue selection glow.
    constexpr Color SelectGlow{0, 193, 242, 102};
    constexpr Color SelectFrame{255, 255, 255, 255};
    constexpr Color Dim{255, 255, 255, 64};
}

// The six faces Font/Font_XX.sbfarc ships. Only Normal is loaded by default -
// together they are 2.9 MB of glyph sheets against a 6 MB payload heap - so ask
// for any other with GUI::RequestFont() before the first frame. An unloaded
// font falls back to Normal rather than drawing nothing (see ResolveFont), so
// naming one you forgot to request still renders text.
//
// Cell sizes and coverage below are measured from the v208 US archive.
enum class FontId : uint8_t {
    // Normal_00, 31x39. ASCII, Latin-1, Cyrillic (U+0401-U+0451) and kana
    // (U+3000-U+30FC). Every style in here is set in it.
    Normal = 0,
    // NormalS_00, 24x30. The same coverage as Normal with a black outline
    // baked into every glyph. The game uses it over busy scenery; as a general
    // UI font that permanent stroke reads as a heavy smudge.
    NormalSmall,
    // Caption_00, 18x22. Latin plus the CJK/kana blocks (U+3041-U+FF5E) - the
    // subtitle face, and the one to use when text has to be small and dense.
    Caption,
    // Ancient_00, 13x14. The Sheikah script, mapped over plain ASCII
    // (U+0020-U+007B): write ordinary text and it comes out in the glyphs the
    // Shrines and the Slate are lettered in.
    Ancient,
    // Special_00, 91x104. A display face at more than three times Normal's
    // size, ASCII and Latin-1 only. For a single word, not a paragraph.
    Special,
    // External_00, 42x39. No letters at all: 80 glyphs in the private-use
    // range U+E040-U+E08F, which is where the game keeps its button and
    // control icons for setting inline with text.
    External,
    Count
};

enum class Align : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

struct TextStyle {
    FontId font = FontId::Normal;
    // Font size the way the layouts express it: the pixel size the font's
    // (width, height) cell is scaled to. (31, 39) draws Normal at 1:1.
    float sizeX = 23.2f;
    float sizeY = 31.2f;
    Color colorTop = Colors::MessageText;
    Color colorBottom = Colors::MessageText;
    bool shadow = true;
    Color shadowColor = Colors::MessageShadow;
    float shadowX = 2.0f;
    float shadowY = 2.0f;
    float charSpace = 0.0f;
    float lineSpace = 0.0f;
    Align align = Align::Left;
    VAlign valign = VAlign::Top;
    // Extra uniform scale on top of sizeX/sizeY (a pane scale in the layout).
    float scale = 1.0f;
    // Apply the font's KRNG pair kerning. On for the same reason the game
    // has the table at all; turn it off for tabular figures, where a fixed
    // advance per digit matters more than tight pairs.
    bool kerning = true;
    // Almost every text material in the game's layouts blends straight
    // alpha; the exceptions are glows, which are additive.
    GX2::BlendState blend = GX2::Blend::Alpha;

    constexpr TextStyle WithFont(FontId f) const { TextStyle s = *this; s.font = f; return s; }
    constexpr TextStyle WithColor(Color c) const { TextStyle s = *this; s.colorTop = c; s.colorBottom = c; return s; }
    constexpr TextStyle WithSize(float x, float y) const { TextStyle s = *this; s.sizeX = x; s.sizeY = y; return s; }
    constexpr TextStyle WithAlign(Align a, VAlign v = VAlign::Top) const { TextStyle s = *this; s.align = a; s.valign = v; return s; }
    constexpr TextStyle WithScale(float k) const { TextStyle s = *this; s.scale = k; return s; }
    constexpr TextStyle NoShadow() const { TextStyle s = *this; s.shadow = false; return s; }
    constexpr TextStyle WithKerning(bool on) const { TextStyle s = *this; s.kerning = on; return s; }
    constexpr TextStyle WithBlend(const GX2::BlendState& b) const { TextStyle s = *this; s.blend = b; return s; }
    constexpr TextStyle Alpha(float factor) const {
        TextStyle s = *this;
        s.colorTop = s.colorTop.Scaled(factor);
        s.colorBottom = s.colorBottom.Scaled(factor);
        s.shadowColor = s.shadowColor.Scaled(factor);
        return s;
    }
};

namespace Styles {
    // Message_00 T_Message_00: Normal (23.2, 31.2) under a 0.9 pane scale,
    // cream, soft shadow (+2, +2, black 100), vertically centred lines.
    constexpr TextStyle Message() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 23.2f; s.sizeY = 31.2f; s.scale = 0.9f;
        s.colorTop = Colors::MessageText; s.colorBottom = Colors::MessageText;
        s.shadow = true; s.shadowColor = Colors::MessageShadow; s.shadowX = 2.0f; s.shadowY = 2.0f;
        s.align = Align::Left; s.valign = VAlign::Middle;
        return s;
    }
    // Message_00 T_Name_00: the speaker's name above the box. The layout sets
    // it in NormalS at (19.2, 25.5); this uses Normal at the same cap height,
    // with the width taken from Normal's own 31:39 cell so it is not
    // condensed. White, hard shadow (+2, +1).
    constexpr TextStyle Name() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 20.3f; s.sizeY = 25.5f;
        s.colorTop = Colors::White; s.colorBottom = Colors::White;
        s.shadow = true; s.shadowColor = Colors::HardShadow; s.shadowX = 2.0f; s.shadowY = 1.0f;
        s.align = Align::Left; s.valign = VAlign::Middle;
        return s;
    }
    // OptionWindow_00 T_OptionTitle_00: Normal (26.4, 33.2), white, +1,+1 shadow.
    constexpr TextStyle Title() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 26.4f; s.sizeY = 33.2f;
        s.colorTop = Colors::White; s.colorBottom = Colors::White;
        s.shadow = true; s.shadowColor = Colors::HardShadow; s.shadowX = 1.0f; s.shadowY = 1.0f;
        s.align = Align::Center; s.valign = VAlign::Middle;
        return s;
    }
    // PaOptionBtn_00 T_Text_00: option rows. NormalS (19.2, 25.5) in the
    // layout; Normal at the same height here.
    constexpr TextStyle Option() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 20.3f; s.sizeY = 25.5f;
        s.colorTop = Colors::White; s.colorBottom = Colors::White;
        s.shadow = true; s.shadowColor = Colors::HardShadow; s.shadowX = 1.0f; s.shadowY = 1.0f;
        s.align = Align::Left; s.valign = VAlign::Middle;
        return s;
    }
    // PaCommonBtnThin_00 / PaMessageBtn_00 T_Btn_00: button labels, centred.
    constexpr TextStyle Button() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 22.7f; s.sizeY = 28.5f;
        s.colorTop = Colors::White; s.colorBottom = Colors::White;
        s.shadow = true; s.shadowColor = Colors::HardShadow; s.shadowX = 1.0f; s.shadowY = 1.0f;
        s.align = Align::Center; s.valign = VAlign::Middle;
        return s;
    }
    // BtnDialog_00 T_BtnDialog: dark text on the light plate. Normal at 1:1.
    constexpr TextStyle Plate() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 31.0f; s.sizeY = 39.0f;
        s.colorTop = Colors::PlateText; s.colorBottom = Colors::PlateText;
        s.shadow = false;
        s.align = Align::Center; s.valign = VAlign::Middle;
        return s;
    }
    // AppSystemWindow_00 T_Text_01: system prompts.
    constexpr TextStyle System() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 23.8f; s.sizeY = 30.0f;
        s.colorTop = Colors::White; s.colorBottom = Colors::White;
        s.shadow = true; s.shadowColor = Colors::HardShadow; s.shadowX = 1.0f; s.shadowY = 1.0f;
        s.align = Align::Center; s.valign = VAlign::Middle;
        return s;
    }
    // PaCommonBtnThin_00 T_New_00: tiny tags.
    constexpr TextStyle Small() {
        TextStyle s;
        s.font = FontId::Normal; s.sizeX = 13.1f; s.sizeY = 16.5f;
        s.colorTop = Colors::White; s.colorBottom = Colors::White;
        s.shadow = true; s.shadowColor = Colors::HardShadow; s.shadowX = 1.0f; s.shadowY = 1.0f;
        s.align = Align::Left; s.valign = VAlign::Middle;
        return s;
    }
}

// The pieces of the game's own UI art the GUI draws with. Each is a texture
// out of Layout/Common.sblarc (timg/<name>.bflim), uploaded as-is.
enum class Sprite : uint8_t {
    White = 0,          // generated 4x4 white - flat rectangles
    MsgWindowCap,       // Nt_MsgWindowL_00^s  96x192  message box end cap (left; mirrored for right)
    MsgWindowCapSmall,  // Nt_MsgWindowSL_00^s 40x78   choice-button end cap
    MsgDeco,            // Nt_MsgDecoL_00^s    24x74   ornament at the box ends
    MsgDecoLine,        // Nt_MsgDecoL_02^s    12x72   thin ornament line
    CornerRound,        // CornerR3_00^s        8x8    quarter disc, rounded-box corners
    CornerLine,         // CornerLineR2_00^s    8x8    rounded-outline corner
    CursorCorner,       // Nt_CursorS_00^s     48x48   L-shaped corner of the option cursor
    CursorBracket,      // Nt_Cursor_00^t      64x64   thin bracket corner (item cursor)
    SelectFrame,        // SelectFrame_04^t    68x68   rounded stroke corner of the selection frame
    SelectFrameGlow,    // SelectFrameGlow_00^s 71x71  its glow
    ArrowDown,          // Nt_ArrowS_02^s      75x75   triangle (boxed cursor / scroll arrows)
    ArrowGlow,          // Nt_ArrowSGlow_02^s  90x90   triangle glow
    ArrowMsg,           // Nt_ArrowMsg_00^d    32x32   "more text" arrow
    ArrowMsgMinus,      // Nt_ArrowMsg_01^d    32x32
    KeyA,               // Nt_KeyTexA_00^d     48x48   controller button glyphs
    KeyB,               // Nt_KeyTexB_00^d
    KeyX,               // Nt_KeyTexX_00^d
    KeyY,               // Nt_KeyTexY_00^d
    KeyL,               // Nt_KeyTexL_00^d
    KeyZL,              // Nt_KeyTexZL_00^d
    Glow,               // CircleEnv32_00^t    32x32   soft radial glow
    Shadow,             // DialogShadow_00^s  128x128  ONE QUADRANT of a soft radial
                        //   shadow (alpha runs 0 at its top-left to 245 at its
                        //   bottom-right) - mirror it into four corners, as
                        //   Canvas::Corners does; a single stretched quad of it
                        //   is a hard-edged dark box, not a blob.
    PlateTop,           // BtnBasic_08T^t      96x96   light plate corner (top row)
    PlateBottom,        // BtnBasic_08B^t      96x96   light plate corner (bottom row)
    PlateShadowTop,     // BtnBasic_08TS^s     96x96   its drop shadow
    PlateShadowBottom,  // BtnBasic_08BS^s
    CursorCircle,       // Nt_CursorCircle_00^t 32x64  half-ring cursor
    Count
};

// How a sprite quad's texture is oriented. Rotation is clockwise, applied
// before the flips.
enum Orient : uint8_t {
    OrientNone = 0,
    OrientFlipH = 1,
    OrientFlipV = 2,
    OrientRotate90 = 4,
    OrientRotate180 = 8,
    OrientRotate270 = 12,
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    constexpr float Right() const { return x + w; }
    constexpr float Bottom() const { return y + h; }
    constexpr float CenterX() const { return x + w * 0.5f; }
    constexpr float CenterY() const { return y + h * 0.5f; }
    constexpr Rect Inset(float dx, float dy) const { return Rect{x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};

// Measurements straight out of Message_00.bflyt (see docs/gui.md).
namespace Metrics {
    // W_Base_00: 914x192 under a 0.67 pane scale, centred, 235 px below centre.
    constexpr float kMessageWindowWidth = 914.0f * 0.67f;   // 612
    constexpr float kMessageWindowHeight = 192.0f * 0.67f;  // 129
    constexpr float kMessageWindowCenterY = kVirtualHeight * 0.5f + 235.0f;  // 595
    constexpr float kMessageCapWidth = 96.0f;     // Nt_MsgWindowL_00 is 96x192: cap width at 1:1 height
    constexpr float kMessageCapHeight = 192.0f;
    // Nt_MsgDeco_0x: 24x74 at 0.67, at +-284 px from centre.
    constexpr float kMessageDecoOffset = 284.0f;
    constexpr float kMessageDecoWidth = 24.0f * 0.67f;
    constexpr float kMessageDecoHeight = 74.0f * 0.67f;
    // T_Message_00: 495x94 text box under a 0.9 scale, centred in the window.
    constexpr float kMessageTextWidth = 495.0f * 0.9f;
    constexpr float kMessageTextHeight = 94.0f * 0.9f;
    // T_Name_00 sits 61 px above the window's centre line, starting 248 px left of centre.
    constexpr float kMessageNameX = kVirtualWidth * 0.5f - 248.0f;
    constexpr float kMessageNameCenterY = kVirtualHeight * 0.5f + 174.0f;
    // PaOptionBtn_00: 276x40 rounded box, 8 px corners, cursor 332x96 (28 px margins).
    constexpr float kOptionRowWidth = 276.0f;
    constexpr float kOptionRowHeight = 40.0f;
    constexpr float kRoundedCorner = 8.0f;
    constexpr float kOptionCursorMargin = 28.0f;
    // PaBoxedCursor_00: arrows 75x75 at 0.21 (~16 px) 64 px out from the centre.
    constexpr float kBoxedCursorArrow = 75.0f * 0.21f;
    // How far the option cursor's shimmer swings either side of its base
    // brightness (0 = static). The game's cloud is stronger than this, but it
    // is masked per-texel where ours is per-vertex, so a softer swing reads
    // closer than matching its amplitude would.
    constexpr float kCursorShimmer = 0.45f;
    // Scrolling list: row pitch and the track down its right edge.
    constexpr float kListRowHeight = 40.0f;
    constexpr float kListRowGap = 2.0f;
    constexpr float kListScrollbarWidth = 6.0f;
    constexpr float kListScrollbarGap = 8.0f;
    // BtnDialog_00: 560x240 plate with 96 px corners, 530x186 select frame with 68 px corners.
    constexpr float kPlateCorner = 96.0f;
    constexpr float kSelectFrameCorner = 68.0f;
}

} // namespace WiiXLaunch::BotW::GUI
