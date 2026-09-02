#pragma once

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include <cstdint>
#include <cstddef>

#include "gui_types.hpp"
#include "../graphics/gx2.hpp"
#include "../graphics/bffnt.hpp"

// GUI renderer core (GX2): the sprite/font tables the loader fills in, and
// the one primitive everything else is built from - a textured quad in
// layout pixels, pushed through GX2::BatchQuad in NDC.
//
// NVN is not implemented: on Switch the whole GUI compiles to no-ops (see
// gui.hpp). The split is deliberate so an NVN backend only has to provide
// this file's handful of functions.

namespace WiiXLaunch::BotW::GUI::impl {

struct SpriteInfo {
    const char* archivePath;     // path inside Layout/Common.sblarc, or "" for generated
    GX2::TextureHandle texture;
    uint32_t width;
    uint32_t height;
    // Where to sample when stretching this sprite's edges (see EdgeU/EdgeV).
    // Measured offline from the decoded art: the innermost column/row that
    // still carries the frame's own stroke, as a texture coordinate. 0 means
    // "use the last texel", which is right for every sprite whose art runs to
    // the texture edge. The exceptions are what this field exists for:
    // SelectFrame's arc stops a texel short, its glow stops 12 texels short,
    // and BtnBasic_08B's plate surface ends three quarters of the way down.
    float edgeU;
    float edgeV;
    // Overrides the component map BFLIM would pick for this sprite; 0 keeps
    // the format's default. See GX2Types::kCompMapShapeFromG.
    uint32_t compMap;
};

// Order must match GUI::Sprite.
inline SpriteInfo g_Sprites[static_cast<size_t>(Sprite::Count)] = {
    { "",                                0, 4, 4, 0.0f,    0.0f,    0 },
    { "timg/Nt_MsgWindowL_00^s.bflim",   0, 0, 0, 0.0f,    0.9818f, 0 },
    { "timg/Nt_MsgWindowSL_00^s.bflim",  0, 0, 0, 0.0f,    0.9551f, 0 },
    { "timg/Nt_MsgDecoL_00^s.bflim",     0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_MsgDecoL_02^s.bflim",     0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/CornerR3_00^s.bflim",        0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/CornerLineR2_00^s.bflim",    0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_CursorS_00^s.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_Cursor_00^t.bflim",       0, 0, 0, 0.0f,    0.0f,    GX2Types::kCompMapShapeFromG },
    { "timg/SelectFrame_04^t.bflim",     0, 0, 0, 0.9779f, 0.9779f, GX2Types::kCompMapShapeFromG },
    { "timg/SelectFrameGlow_00^s.bflim", 0, 0, 0, 0.8380f, 0.8380f, 0 },
    { "timg/Nt_ArrowS_02^s.bflim",       0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_ArrowSGlow_02^s.bflim",   0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_ArrowMsg_00^d.bflim",     0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_ArrowMsg_01^d.bflim",     0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_KeyTexA_00^d.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_KeyTexB_00^d.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_KeyTexX_00^d.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_KeyTexY_00^d.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_KeyTexL_00^d.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_KeyTexZL_00^d.bflim",     0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/CircleEnv32_00^t.bflim",     0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/DialogShadow_00^s.bflim",    0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/BtnBasic_08T^t.bflim",       0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/BtnBasic_08B^t.bflim",       0, 0, 0, 0.0f,    0.7448f, 0 },
    { "timg/BtnBasic_08TS^s.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/BtnBasic_08BS^s.bflim",      0, 0, 0, 0.0f,    0.0f,    0 },
    { "timg/Nt_CursorCircle_00^t.bflim", 0, 0, 0, 0.0f,    0.0f,    GX2Types::kCompMapShapeFromG },
};

struct FontInfo {
    const char* archivePath;     // inside Font/Font_XX.sbfarc
    BFFNT::Font font;
    bool load;                   // false: skipped by the loader (see below)
};

// Order must match GUI::FontId. NormalS_00 is deliberately NOT loaded: it is
// the same face with a black outline baked into every glyph, which reads as a
// heavy stroke at any size a mod is likely to use, and it costs 512 KB of the
// payload's 6 MB heap. FontId::NormalSmall therefore falls back to Normal_00
// (see ResolveFont), so styles and mods that name it still render.
inline FontInfo g_Fonts[static_cast<size_t>(FontId::Count)] = {
    { "Normal_00.bffnt",  BFFNT::Font{}, true },
    { "NormalS_00.bffnt", BFFNT::Font{}, false },
};

inline SpriteInfo& SpriteAt(Sprite s) { return g_Sprites[static_cast<size_t>(s)]; }
inline BFFNT::Font& FontAt(FontId f) { return g_Fonts[static_cast<size_t>(f)].font; }

// Per-frame render target, set by the draw callback.
inline void* g_FrameDst = nullptr;
inline bool g_FrameOpen = false;
inline uint32_t g_QuadsThisFrame = 0;

// ---------------------------------------------------------------------------
// Virtual (1280x720 layout) space -> device pixels -> NDC
// ---------------------------------------------------------------------------
// The colour buffer is NOT always 1280x720: BotW renders the GamePad view at
// 854x480 (seen live), and a Cemu resolution pack can make the TV buffer any
// size at all. Mapping straight to NDC, as the first version did, silently
// stretches the UI whenever the buffer's aspect ratio is not 16:9 - which is
// exactly what an ultrawide resolution pack produces.
//
// So the layout rectangle is fitted into the buffer the way the game's own
// UI behaves: uniform scale, centred, with the leftover as margin
// (ScalingMode::Fit, the default). Stretch is kept for anyone who wants the
// old behaviour, and both are identical on a 16:9 buffer.
enum class ScalingMode : uint8_t { Fit, Stretch };

inline ScalingMode g_ScalingMode = ScalingMode::Fit;
inline uint32_t g_DeviceWidth = 1280;
inline uint32_t g_DeviceHeight = 720;
inline float g_ScaleX = 1.0f;      // device pixels per layout pixel
inline float g_ScaleY = 1.0f;
inline float g_OffsetX = 0.0f;     // device-pixel origin of the layout rectangle
inline float g_OffsetY = 0.0f;
// Snapping quad edges to whole device pixels keeps flat fills and 1px rules
// off half-pixels (which bilinear filtering turns into a soft double edge),
// and is only worth doing when a layout pixel is at least a device pixel.
//
// Off by default. Snapping is only meaningful when the buffer the game
// declares IS the output resolution, and under a Cemu resolution pack it is
// not: the game still describes a 1280x720 colour buffer while Cemu renders
// the scaled-up texture behind its back, so "device pixels" here would be
// 2x2 real ones. Turn it on for a native-resolution target.
inline bool g_SnapToPixels = false;

inline void UpdateViewport(uint32_t deviceWidth, uint32_t deviceHeight) {
    g_DeviceWidth = deviceWidth ? deviceWidth : 1280;
    g_DeviceHeight = deviceHeight ? deviceHeight : 720;
    const float sx = static_cast<float>(g_DeviceWidth) / kVirtualWidth;
    const float sy = static_cast<float>(g_DeviceHeight) / kVirtualHeight;
    if (g_ScalingMode == ScalingMode::Stretch) {
        g_ScaleX = sx;
        g_ScaleY = sy;
        g_OffsetX = 0.0f;
        g_OffsetY = 0.0f;
    } else {
        const float s = sx < sy ? sx : sy;
        g_ScaleX = s;
        g_ScaleY = s;
        g_OffsetX = (static_cast<float>(g_DeviceWidth) - kVirtualWidth * s) * 0.5f;
        g_OffsetY = (static_cast<float>(g_DeviceHeight) - kVirtualHeight * s) * 0.5f;
    }
}

inline float RoundToPixel(float v) {
    return v >= 0.0f ? static_cast<float>(static_cast<int32_t>(v + 0.5f))
                     : -static_cast<float>(static_cast<int32_t>(-v + 0.5f));
}

// Layout pixels -> NDC, through device pixels so snapping is meaningful.
inline void ToNdc(float x, float y, float& nx, float& ny, bool snap) {
    float dx = x * g_ScaleX + g_OffsetX;
    float dy = y * g_ScaleY + g_OffsetY;
    if (snap && g_SnapToPixels && g_ScaleX >= 1.0f && g_ScaleY >= 1.0f) {
        dx = RoundToPixel(dx);
        dy = RoundToPixel(dy);
    }
    nx = dx * (2.0f / static_cast<float>(g_DeviceWidth)) - 1.0f;
    ny = 1.0f - dy * (2.0f / static_cast<float>(g_DeviceHeight));
}

// ---------------------------------------------------------------------------
// Alpha stack
// ---------------------------------------------------------------------------
// lyt multiplies a pane's alpha by its parent's all the way down the tree, so
// fading a window fades everything in it in one step. Immediate mode has no
// tree, so the same effect is a stack: everything emitted while a factor is
// pushed has its alpha (colour AND shadow) multiplied by it.
constexpr size_t kMaxAlphaDepth = 8;
inline float g_AlphaStack[kMaxAlphaDepth] = {1.0f};
inline size_t g_AlphaDepth = 0;

inline float CurrentAlpha() { return g_AlphaStack[g_AlphaDepth]; }

inline void PushAlpha(float factor) {
    if (g_AlphaDepth + 1 >= kMaxAlphaDepth) return;
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    g_AlphaStack[g_AlphaDepth + 1] = g_AlphaStack[g_AlphaDepth] * factor;
    g_AlphaDepth++;
}

inline void PopAlpha() {
    if (g_AlphaDepth > 0) g_AlphaDepth--;
}

inline void ResetAlpha() {
    g_AlphaDepth = 0;
    g_AlphaStack[0] = 1.0f;
}

inline void FillVertex(GX2::TextureVertex& v, float x, float y, float u, float t, Color c, bool snap) {
    ToNdc(x, y, v.x, v.y, snap);
    v.z = 0.0f;
    v.w = 1.0f;
    v.u = u;
    v.v = t;
    const float a = CurrentAlpha();
    v.r = c.r * (1.0f / 255.0f);
    v.g = c.g * (1.0f / 255.0f);
    v.b = c.b * (1.0f / 255.0f);
    v.a = c.a * (1.0f / 255.0f) * a;
}

// Applies an Orient to the four UV corners (TL, TR, BL, BR).
inline void OrientUVs(uint8_t orient, float (&u)[4], float (&v)[4]) {
    auto swap = [](float& a, float& b) { float t = a; a = b; b = t; };
    const uint8_t rot = orient & 12;
    for (int r = 0; r < rot / 4; ++r) {
        // 90 degrees clockwise: new TL = old BL, new TR = old TL, new BR = old TR, new BL = old BR.
        const float tu[4] = { u[2], u[0], u[3], u[1] };
        const float tv[4] = { v[2], v[0], v[3], v[1] };
        for (int i = 0; i < 4; ++i) { u[i] = tu[i]; v[i] = tv[i]; }
    }
    if (orient & OrientFlipH) { swap(u[0], u[1]); swap(v[0], v[1]); swap(u[2], u[3]); swap(v[2], v[3]); }
    if (orient & OrientFlipV) { swap(u[0], u[2]); swap(v[0], v[2]); swap(u[1], u[3]); swap(v[1], v[3]); }
}

// Sine/cosine of an angle in degrees, without libm: the Cemu payload links
// no math library, and rotation is only ever needed to a fraction of a
// degree. Range-reduced to a quarter turn, then a 7th-order minimax sine
// (error under 1e-6 over the range).
inline void SinCosDeg(float degrees, float& outSin, float& outCos) {
    float t = degrees * (1.0f / 360.0f);
    t -= static_cast<float>(static_cast<int32_t>(t));      // fractional turns
    if (t < 0.0f) t += 1.0f;
    // Quadrant reduction to [0, 0.25) turns, tracking the signs/swap.
    int quadrant = static_cast<int>(t * 4.0f);
    float f = t * 4.0f - static_cast<float>(quadrant);     // 0..1 within the quadrant
    const float x = f * 1.57079632679f;                    // radians into the quadrant
    const float x2 = x * x;
    // sin(x) and cos(x) on [0, pi/2]
    const float s = x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
    const float c = 1.0f - x2 * (0.5f - x2 * (0.04166667f - x2 * 0.00138889f));
    switch (quadrant & 3) {
    case 0: outSin = s;  outCos = c;  break;
    case 1: outSin = c;  outCos = -s; break;
    case 2: outSin = -s; outCos = -c; break;
    default: outSin = -c; outCos = s; break;
    }
}

// The primitive: a textured quad covering [x0,x1) x [y0,y1) layout pixels,
// sampling the texture rectangle (u0,v0)-(u1,v1) (v0 = top), per-corner
// colours (TL, TR, BL, BR), optionally flipped/quarter-turned (orient) and
// freely rotated about its own centre (rotation, degrees clockwise on
// screen), in one blend mode.
//
// A note on rotation angles taken from a .bflyt: lyt panes are y-UP, so a
// pane's Z rotation appears on screen with the opposite sign. The boxed
// cursor's arrows, for instance, are lyt -135/-225/-45/+45 and are drawn
// here as +135/+225/+45/-45.
inline void EmitQuad(GX2::TextureHandle tex, float x0, float y0, float x1, float y1,
                     float u0, float v0, float u1, float v1,
                     Color cTL, Color cTR, Color cBL, Color cBR, uint8_t orient = OrientNone,
                     const GX2::BlendState& blend = GX2::Blend::Alpha, bool snap = true,
                     float rotation = 0.0f) {
    if (!g_FrameOpen || !tex) return;
    if (cTL.a == 0 && cTR.a == 0 && cBL.a == 0 && cBR.a == 0) return;
    if (CurrentAlpha() <= 0.0f) return;
    float u[4] = { u0, u1, u0, u1 };
    float v[4] = { v0, v0, v1, v1 };
    if (orient) OrientUVs(orient, u, v);

    float px[4] = { x0, x1, x0, x1 };
    float py[4] = { y0, y0, y1, y1 };
    if (rotation != 0.0f) {
        float s = 0.0f, c = 1.0f;
        SinCosDeg(rotation, s, c);
        const float cx = (x0 + x1) * 0.5f;
        const float cy = (y0 + y1) * 0.5f;
        for (int i = 0; i < 4; ++i) {
            const float dx = px[i] - cx;
            const float dy = py[i] - cy;
            px[i] = cx + dx * c - dy * s;
            py[i] = cy + dx * s + dy * c;
        }
        snap = false;   // a rotated quad has no axis-aligned edges to snap
    }

    GX2::TextureVertex verts[4];
    FillVertex(verts[0], px[0], py[0], u[0], v[0], cTL, snap);
    FillVertex(verts[1], px[1], py[1], u[1], v[1], cTR, snap);
    FillVertex(verts[2], px[2], py[2], u[2], v[2], cBL, snap);
    FillVertex(verts[3], px[3], py[3], u[3], v[3], cBR, snap);
    GX2::BatchQuad(tex, verts, blend);
    g_QuadsThisFrame++;
}

inline void EmitQuad(GX2::TextureHandle tex, const Rect& r, float u0, float v0, float u1, float v1,
                     Color c, uint8_t orient = OrientNone, const GX2::BlendState& blend = GX2::Blend::Alpha,
                     bool snap = true, float rotation = 0.0f) {
    EmitQuad(tex, r.x, r.y, r.Right(), r.Bottom(), u0, v0, u1, v1, c, c, c, c, orient, blend, snap, rotation);
}

inline GX2::TextureHandle SpriteTexture(Sprite s) {
    if (s >= Sprite::Count) return 0;
    return SpriteAt(s).texture;
}

inline void EmitSprite(Sprite s, const Rect& r, Color c, uint8_t orient = OrientNone,
                       const GX2::BlendState& blend = GX2::Blend::Alpha, float rotation = 0.0f) {
    EmitQuad(SpriteTexture(s), r, 0.0f, 0.0f, 1.0f, 1.0f, c, orient, blend, true, rotation);
}

// Texture coordinate to repeat when stretching a frame's edge.
//
// Stretching means sampling ONE column (or row) over and over, so an edge
// quad has to use this value for both of its texture coordinates on that
// axis - a DEGENERATE range. Giving it a range that runs to 1.0 instead
// makes the edge fade out across its own length wherever the art stops short
// of the tile: the selection frame's glow ends twelve texels early, so its
// edges faded from full strength on the left to nothing on the right. That
// was the left-to-right transparency ramp.
//
// For most of the game's corner art the right column is the last texel,
// sampled at its centre - the only coordinate that can neither bleed into a
// neighbour nor run off the edge. Art that stops short carries a measured
// override in SpriteInfo.
inline float EdgeU(Sprite s) {
    const SpriteInfo& info = SpriteAt(s);
    if (info.edgeU > 0.0f) return info.edgeU;
    return info.width ? 1.0f - 0.5f / static_cast<float>(info.width) : 1.0f;
}

inline float EdgeV(Sprite s) {
    const SpriteInfo& info = SpriteAt(s);
    if (info.edgeV > 0.0f) return info.edgeV;
    return info.height ? 1.0f - 0.5f / static_cast<float>(info.height) : 1.0f;
}

// Like EmitSprite, but stopping at the innermost texel the artwork covers
// instead of the texture's geometric edge - the same coordinate the stretched
// edges repeat. Frame corners must use this: where a sprite's art stops short
// of its tile (the selection frame stops one texel short), drawing the corner
// out to 1.0 leaves a transparent sliver exactly where the stretched edge
// begins, and that sliver is a visible line down every join.
inline void EmitSpriteArt(Sprite s, const Rect& r, Color c, uint8_t orient = OrientNone,
                          const GX2::BlendState& blend = GX2::Blend::Alpha) {
    EmitQuad(SpriteTexture(s), r, 0.0f, 0.0f, EdgeU(s), EdgeV(s), c, orient, blend);
}

// A flat rectangle: the white sprite tinted.
inline void EmitRect(const Rect& r, Color c, const GX2::BlendState& blend = GX2::Blend::Alpha) {
    EmitQuad(SpriteTexture(Sprite::White), r, 0.25f, 0.25f, 0.75f, 0.75f, c, OrientNone, blend);
}

inline void EmitRectGradient(const Rect& r, Color top, Color bottom,
                             const GX2::BlendState& blend = GX2::Blend::Alpha) {
    EmitQuad(SpriteTexture(Sprite::White), r.x, r.y, r.Right(), r.Bottom(), 0.25f, 0.25f, 0.75f, 0.75f,
             top, top, bottom, bottom, OrientNone, blend);
}

} // namespace WiiXLaunch::BotW::GUI::impl

#endif
