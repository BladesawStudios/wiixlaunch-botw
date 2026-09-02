#pragma once

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include <wiixlaunch/time.hpp>

#include <cstdint>
#include <cstddef>

#include "gui_types.hpp"
#include "../game/display.hpp"
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
    uint32_t sheetBytes;         // glyph sheet data: what loading it costs
    bool load;                   // set by GUI::RequestFont before the 1st frame
};

// Order must match GUI::FontId. sheetBytes is sheetSize x sheetCount read out
// of each font's TGLP in the v208 US archive; all six start their sheet data at
// 0x2000 and are format 12 (BC4) except NormalS_00, which is 8 (A8), so all six
// load through the same path.
//
// Only Normal is on by default. The six together are 2.9 MB of the payload's
// 6 MB heap, which is too much to spend without being asked, and one face is
// what almost any mod needs. GUI::RequestFont(id) turns another on; anything
// not loaded falls back to Normal in ResolveFont rather than drawing nothing.
inline FontInfo g_Fonts[static_cast<size_t>(FontId::Count)] = {
    { "Normal_00.bffnt",   BFFNT::Font{}, 1024u * 1024u, true  },
    { "NormalS_00.bffnt",  BFFNT::Font{},  512u * 1024u, false },
    { "Caption_00.bffnt",  BFFNT::Font{},  256u * 1024u, false },
    { "Ancient_00.bffnt",  BFFNT::Font{},   64u * 1024u, false },
    { "Special_00.bffnt",  BFFNT::Font{}, 1024u * 1024u, false },
    { "External_00.bffnt", BFFNT::Font{},   64u * 1024u, false },
};

inline SpriteInfo& SpriteAt(Sprite s) { return g_Sprites[static_cast<size_t>(s)]; }
inline BFFNT::Font& FontAt(FontId f) { return g_Fonts[static_cast<size_t>(f)].font; }

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------
// Animation is driven by elapsed TIME, never by a frame count. BotW runs at 30
// but the FPS++ graphic pack makes it 60, which doubled the rate of anything
// counting frames - cursors blinked twice as fast, arrows bobbed twice as
// fast. Time::GetMonotonicTicks is the Espresso timebase read with mftb: no
// import, no OS call, and it keeps real time under Cemu too.
inline int64_t g_LastTicks = 0;
inline int64_t g_NowTicks = 0;
inline float g_DeltaSeconds = 0.0f;
inline float g_TimeSeconds = 0.0f;
inline float g_Fps = 0.0f;

inline void UpdateClock() {
    const int64_t now = WiiXLaunch::Time::GetMonotonicTicks();
    g_NowTicks = now;
    if (g_LastTicks != 0) {
        const int64_t elapsed = now - g_LastTicks;
        float dt = static_cast<float>(elapsed) / static_cast<float>(WiiXLaunch::Time::kTicksPerSecond);
        // A load screen, a breakpoint or a first frame must not make an
        // animation jump; clamp rather than let a huge delta through.
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.25f) dt = 0.25f;
        g_DeltaSeconds = dt;
        g_TimeSeconds += dt;
        if (dt > 0.0f) {
            const float instant = 1.0f / dt;
            g_Fps = g_Fps > 0.0f ? (g_Fps * 0.9f + instant * 0.1f) : instant;
        }
    }
    g_LastTicks = now;
}

// Position within a repeating cycle of `period` seconds, 0 to 1 - the unit
// every animation here is built from.
//
// Taken from the tick count with an integer modulo rather than from
// accumulated seconds: a float holding elapsed time loses resolution as it
// grows, and an animation that is smooth at boot would gradually start to
// step. This stays exact however long the game has been running.
inline float Phase(float period) {
    if (period <= 0.0f || g_NowTicks == 0) return 0.0f;
    const int64_t ticks = static_cast<int64_t>(period * static_cast<float>(WiiXLaunch::Time::kTicksPerSecond));
    if (ticks <= 0) return 0.0f;
    const int64_t within = g_NowTicks % ticks;
    return static_cast<float>(within) / static_cast<float>(ticks);
}

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
// The draw hook runs once per colour buffer, and BotW has two: the TV at its
// full size and the GamePad at 854x480. Both get drawn into - the mapping
// above is recomputed for each - but "what resolution is this?" has to mean
// one of them, and the answer a mod wants is the big one. So the reported
// pair is the largest buffer seen during a frame rather than whichever
// happened to be drawn last, which is what made the readout flip to 854x480
// once frames started being built ahead of the draw passes.
inline uint32_t g_ReportWidth = 1280;
inline uint32_t g_ReportHeight = 720;
inline uint32_t g_ReportBestArea = 0;
inline float g_ReportScaleX = 1.0f;
inline float g_ReportScaleY = 1.0f;

// The aspect ratio the finished frame is actually PRESENTED at, when that is
// not the colour buffer's own. 0 means "work it out", which is the default
// and is what handles ultrawide without being told anything.
//
// The colour buffer cannot answer it: a Cemu pack scales the render target
// behind the game's back and the declared GX2 surface stays 1280x720. But the
// GAME's own aspect constant can, because an ultrawide pack has to rewrite
// that for the game itself to render correctly - see game/display.hpp. So the
// automatic path reads it, and falls back to the buffer's own shape only if
// what it finds is not a plausible aspect.
inline float g_OutputAspect = 0.0f;

inline float ResolveOutputAspect(float bufferAspect) {
    if (g_OutputAspect > 0.0f) return g_OutputAspect;      // told explicitly
    const float detected = Display::GetAspectRatio();
    return detected > 0.0f ? detected : bufferAspect;
}
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
    const float bufW = static_cast<float>(g_DeviceWidth);
    const float bufH = static_cast<float>(g_DeviceHeight);

    if (g_ScalingMode == ScalingMode::Stretch) {
        g_ScaleX = bufW / kVirtualWidth;
        g_ScaleY = bufH / kVirtualHeight;
        g_OffsetX = 0.0f;
        g_OffsetY = 0.0f;
    } else {
        // How much wider a buffer pixel is DISPLAYED than it is tall. 1 when
        // the buffer is presented at its own shape; 1.31 when a 16:9 buffer is
        // stretched across a 21:9 screen.
        const float bufAspect = bufW / bufH;
        const float outAspect = ResolveOutputAspect(bufAspect);
        const float pixelAspect = outAspect / bufAspect;

        // A layout pixel has to come out square on the SCREEN, so the
        // horizontal scale is divided by that - which is what stops the UI
        // stretching when the output is wider than the buffer says.
        float sy = bufH / kVirtualHeight;
        const float syLimit = pixelAspect * bufW / kVirtualWidth;
        if (sy > syLimit) sy = syLimit;
        g_ScaleY = sy;
        g_ScaleX = sy / pixelAspect;
        g_OffsetX = (bufW - kVirtualWidth * g_ScaleX) * 0.5f;
        g_OffsetY = (bufH - kVirtualHeight * g_ScaleY) * 0.5f;
    }

    // The report pair is the largest buffer of the frame - see below.
    const uint32_t area = g_DeviceWidth * g_DeviceHeight;
    if (area > g_ReportBestArea) {
        g_ReportBestArea = area;
        g_ReportWidth = g_DeviceWidth;
        g_ReportHeight = g_DeviceHeight;
        g_ReportScaleX = g_ScaleX;
        g_ReportScaleY = g_ScaleY;
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

// ---------------------------------------------------------------------------
// The frame record
// ---------------------------------------------------------------------------
// A frame is BUILT in the input hook and DRAWN in the present hook, so what
// the builder produces has to be held somewhere in between.
//
// It is built there because that is the only place a mod's callback can still
// decide to hide the input the game is about to read. Running it at present
// time - after the game has already acted on the pad - is what let the press
// that opens a menu reach the game as well.
//
// Positions are kept in LAYOUT space, not NDC: the viewport is only known at
// present time, so converting on replay keeps a recorded frame correct even
// if the colour buffer changes size between the two. Colours are stored with
// the alpha stack already folded in, since that stack is a build-time notion.
struct RecordedQuad {
    GX2::TextureHandle tex;
    uint8_t blendIndex;
    uint8_t snap;
    float x[4], y[4];
    float u[4], v[4];
    Color color[4];
};

constexpr uint32_t kMaxRecordedQuads = 2048;   // ~180 KB, allocated once
constexpr uint32_t kMaxRecordedBlends = 8;

// Backdrop blur. Off unless a mod turns it on: it costs two render targets
// and a handful of full-screen draws per frame it is used. g_BlurRequested is
// set while building a frame and read when drawing it, so the blur passes only
// run on frames that actually asked for one.
inline bool g_BlurEnabled = false;
inline uint32_t g_BlurDownscale = 4;
inline uint32_t g_BlurPasses = 2;
inline bool g_BlurRequested = false;

inline RecordedQuad* g_Record = nullptr;
inline uint32_t g_RecordCount = 0;
inline bool g_RecordOverflowed = false;
inline GX2::BlendState g_RecordBlends[kMaxRecordedBlends]{};
inline uint32_t g_RecordBlendCount = 0;
inline bool g_Recording = false;

inline bool EnsureRecord() {
    if (g_Record) return true;
    g_Record = reinterpret_cast<RecordedQuad*>(GX2::AllocMEM1(sizeof(RecordedQuad) * kMaxRecordedQuads, 64));
    return g_Record != nullptr;
}

inline uint8_t RecordBlend(const GX2::BlendState& blend) {
    for (uint32_t i = 0; i < g_RecordBlendCount; ++i) {
        if (g_RecordBlends[i] == blend) return static_cast<uint8_t>(i);
    }
    if (g_RecordBlendCount >= kMaxRecordedBlends) return 0;
    g_RecordBlends[g_RecordBlendCount] = blend;
    return static_cast<uint8_t>(g_RecordBlendCount++);
}

// A smooth 0..1 oscillation over `period` seconds - one cosine, easing in and
// out of both ends. This is what UI motion should be built from: Phase() on
// its own is a sawtooth, and quantising it into a few steps (which is what the
// first version did, counting frames) reads as stutter the moment it runs at
// the right speed rather than double.
inline float Wave(float period) {
    float s = 0.0f, c = 1.0f;
    SinCosDeg(Phase(period) * 360.0f, s, c);
    return 0.5f - 0.5f * c;
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

    if (g_RecordCount >= kMaxRecordedQuads || !g_Record) {
        g_RecordOverflowed = true;
        return;
    }

    const float alpha = CurrentAlpha();
    const Color corners[4] = { cTL.Scaled(alpha), cTR.Scaled(alpha), cBL.Scaled(alpha), cBR.Scaled(alpha) };

    // Filled before it is counted, so a reader can never see a half-written
    // entry - the build and the replay are not guaranteed to be the same
    // thread.
    RecordedQuad& q = g_Record[g_RecordCount];
    q.tex = tex;
    q.blendIndex = RecordBlend(blend);
    q.snap = snap ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        q.x[i] = px[i];
        q.y[i] = py[i];
        q.u[i] = u[i];
        q.v[i] = v[i];
        q.color[i] = corners[i];
    }
    g_RecordCount++;
    g_QuadsThisFrame++;
}

// Hands the frame the input hook built to the GPU. Runs in the present hook,
// where a context state and a colour buffer exist.
inline void ReplayRecord(void* dst) {
    if (!g_Record || g_RecordCount == 0) return;
    // Before anything is drawn: the blur samples the colour buffer as it
    // stands and retargets rendering while it runs, so it has to happen
    // before the batch sets itself up. The recorded quads that use it already
    // hold its texture handle.
    // Also run it once when it has not been set up yet: the targets can only
    // be sized from a real colour buffer, and Canvas::BlurBehind() will not
    // ask for a blur until they exist. Without this the two wait on each
    // other forever.
    if (g_BlurEnabled && (g_BlurRequested || !GX2::BackdropReady())) {
        GX2::BlurBackdrop(dst, g_BlurDownscale, g_BlurPasses);
    }
    GX2::BeginBatch(dst);
    // Colours were recorded with the alpha stack folded in already.
    const float saved = g_AlphaStack[0];
    g_AlphaStack[0] = 1.0f;
    const size_t savedDepth = g_AlphaDepth;
    g_AlphaDepth = 0;
    for (uint32_t i = 0; i < g_RecordCount; ++i) {
        const RecordedQuad& q = g_Record[i];
        GX2::TextureVertex verts[4];
        for (int k = 0; k < 4; ++k) {
            FillVertex(verts[k], q.x[k], q.y[k], q.u[k], q.v[k], q.color[k], q.snap != 0);
        }
        GX2::BatchQuad(q.tex, verts, g_RecordBlends[q.blendIndex]);
    }
    GX2::EndBatch();
    g_AlphaStack[0] = saved;
    g_AlphaDepth = savedDepth;
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
