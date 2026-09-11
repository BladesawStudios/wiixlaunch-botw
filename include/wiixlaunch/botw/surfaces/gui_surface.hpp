#pragma once

// botw.gui v1 - the immediate-mode UI, reachable by mods.
//
// ---------------------------------------------------------------------------
// THREE THINGS IN THE GUI'S API CANNOT CROSS, and each one shaped a decision.
//
// 1. THE CANVAS IS A REFERENCE PARAMETER. FrameCallback is
//    void(*)(Canvas&), and a mod cannot receive a C++ reference to a host
//    object. So the canvas is IMPLICIT: this surface holds the one the module
//    handed it for the duration of the frame, and every draw call below uses
//    it. Outside a frame callback there is no canvas and every draw call
//    refuses - which is correct, because outside a frame there is nothing to
//    draw onto.
//
// 2. TextStyle IS EIGHTEEN FIELDS including a nested BlendState. Flattening it
//    into every Text call would mean eighteen parameters per call site, which
//    is not an API anybody would use correctly. So styles live on the HOST in a
//    small table, a mod builds one with setters and holds an id. That is the
//    ordinary answer for a large options struct at a boundary, and it has a
//    second benefit here: a style is built once and reused, rather than being
//    marshalled on every line of text.
//
// 3. Rect AND Color ARE SMALL ENOUGH TO FLATTEN. A rect is four floats and a
//    colour is one packed RGBA word, so they are passed by value and no table
//    is needed. Drawing 0xFF0000FF is more legible at the call site than
//    building a colour object first.
//
// ---------------------------------------------------------------------------
// FRAME CALLBACKS ARE ATTRIBUTED, like every other callback in this project.
// The module's OnFrame is a single slot; this surface takes it once and
// dispatches to modules itself, recording the owner before each call so a frame
// that never returns names the module that was drawing.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/botw/gui/gui.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::GuiSurface {

constexpr const char* kName = "botw.gui";
constexpr uint16_t kVersionMajor = 1;
// 1.1 appends ImageEx, which is the first way a mod can reach orientation,
// blending or rotation on a sprite. Appending bumps the MINOR, so every mod
// built against v1.0 still resolves.
constexpr uint16_t kVersionMinor = 1;

constexpr uint32_t kMaxFrameCallbacks = 8;
constexpr uint32_t kMaxStyles = 32;
constexpr uint32_t kOwnerLen = 17;

namespace impl {

using ModFrameFn = void (*)();

struct FrameEntry {
    ModFrameFn fn;
    char owner[kOwnerLen];
    bool inUse;
};

inline FrameEntry g_Frames[kMaxFrameCallbacks];
inline uint32_t g_FrameCount = 0;
inline bool g_Hooked = false;

// The canvas for the frame currently being built, and nothing outside it.
inline GUI::Canvas* g_Canvas = nullptr;

// Its own magic, so a hang while drawing the UI is distinguishable in a dump
// from a hang in a host tick, a player tick, or a raw draw callback.
struct GuiInFlight {
    uint32_t magic;              // 'WXGU'
    uint32_t sequence;
    uint32_t depth;
    char     owner[kOwnerLen];
    char     pad[3];
};

constexpr uint32_t kGuiMagic = 0x57584755u;   // 'WXGU'
inline GuiInFlight g_InFlight = { kGuiMagic, 0, 0, {0}, {0} };

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

inline void DispatchFrame(GUI::Canvas& canvas) {
    g_Canvas = &canvas;

    for (uint32_t i = 0; i < g_FrameCount; ++i) {
        FrameEntry& e = g_Frames[i];
        if (!e.inUse || !e.fn) continue;

        g_InFlight.sequence++;
        g_InFlight.depth++;
        CopyOwner(g_InFlight.owner, e.owner);
        WiiXLaunch::ModContext::SetCurrent(e.owner);

        e.fn();

        WiiXLaunch::ModContext::SetCurrent(nullptr);
        g_InFlight.depth--;
        g_InFlight.owner[0] = '\0';
    }

    // Cleared on the way out, so a draw call from outside a frame refuses
    // rather than writing through a pointer that was valid last frame.
    g_Canvas = nullptr;
}

// --- colours and styles ----------------------------------------------------

inline GUI::Color FromRGBA(uint32_t rgba) {
    GUI::Color c;
    c.r = static_cast<uint8_t>((rgba >> 24) & 0xFF);
    c.g = static_cast<uint8_t>((rgba >> 16) & 0xFF);
    c.b = static_cast<uint8_t>((rgba >> 8) & 0xFF);
    c.a = static_cast<uint8_t>(rgba & 0xFF);
    return c;
}

inline GUI::Rect MakeRect(float x, float y, float w, float h) {
    GUI::Rect r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

struct StyleSlot {
    GUI::TextStyle style;
    bool used = false;
};

inline StyleSlot g_Styles[kMaxStyles];

inline GUI::TextStyle* StyleAt(uint32_t id) {
    if (id == 0 || id > kMaxStyles) return nullptr;
    StyleSlot& s = g_Styles[id - 1];
    return s.used ? &s.style : nullptr;
}

// --- lifecycle -------------------------------------------------------------

extern "C" inline uint32_t GuiInit() {
    GUI::Init();
    if (!g_Hooked) {
        g_Hooked = true;
        GUI::OnFrame(&DispatchFrame);
        WIIXL_LOG("botw.gui: frame callback slot taken by the host; modules are "
                  "dispatched from it, attributed");
    }
    return 1;
}

extern "C" inline uint32_t GuiIsReady() { return GUI::IsReady() ? 1u : 0u; }

extern "C" inline uint32_t GuiRegisterFrame(ModFrameFn fn) {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') {
        WIIXL_LOG("botw.gui: refused - a frame callback belongs to a module and "
                  "none is running");
        return 0;
    }
    if (!fn) {
        WIIXL_LOG("botw.gui: %s passed a null frame callback", owner);
        return 0;
    }
    for (uint32_t i = 0; i < g_FrameCount; ++i) {
        if (SameOwner(g_Frames[i].owner, owner)) {
            WIIXL_LOG("botw.gui: %s already has a frame callback", owner);
            return 0;
        }
    }
    if (g_FrameCount >= kMaxFrameCallbacks) {
        WIIXL_LOG("botw.gui: %s refused - all %u frame slots are taken",
                  owner, kMaxFrameCallbacks);
        return 0;
    }

    GuiInit();

    FrameEntry& e = g_Frames[g_FrameCount++];
    e.fn = fn;
    e.inUse = true;
    CopyOwner(e.owner, owner);

    WIIXL_LOG("botw.gui: %s registered a frame callback (%u of %u)",
              e.owner, g_FrameCount, kMaxFrameCallbacks);
    return 1;
}

extern "C" inline uint32_t GuiFrameCallbackCount() { return g_FrameCount; }

extern "C" inline uint32_t GuiSetAssetPaths(const char* fontArchive, const char* layoutArchive) {
    GUI::SetAssetPaths(fontArchive, layoutArchive);
    return 1;
}

extern "C" inline uint32_t GuiLoadNow() { GUI::LoadNow(); return 1; }

extern "C" inline uint32_t GuiRequestFont(int32_t fontId, uint32_t load) {
    GUI::RequestFont(static_cast<GUI::FontId>(fontId), load != 0);
    return 1;
}

extern "C" inline uint32_t GuiFontRequested(int32_t fontId) {
    return GUI::FontRequested(static_cast<GUI::FontId>(fontId)) ? 1u : 0u;
}

extern "C" inline uint32_t GuiSetBackdropBlur(uint32_t enabled, uint32_t downscale, uint32_t passes) {
    GUI::SetBackdropBlur(enabled != 0, downscale ? downscale : 4, passes ? passes : 2);
    return 1;
}

extern "C" inline uint32_t GuiSetPixelSnapping(uint32_t on) {
    GUI::SetPixelSnapping(on != 0);
    return 1;
}

extern "C" inline uint32_t GuiSetOutputAspect(float aspect) {
    GUI::SetOutputAspect(aspect);
    return 1;
}

extern "C" inline uint32_t GuiGetEffectiveOutputAspect(float* out) {
    if (!out) return 0;
    *out = GUI::GetEffectiveOutputAspect();
    return 1;
}

extern "C" inline uint32_t GuiSetUiSounds(uint32_t on) {
    GUI::SetUiSounds(on != 0);
    return 1;
}

// --- styles ----------------------------------------------------------------
//
// A style starts as the game's message style and is edited from there, because
// starting from a default-constructed one would mean every mod re-specifying
// the font, the shadow and the spacing before its first line of text.

extern "C" inline uint32_t GuiStyleCreate() {
    for (uint32_t i = 0; i < kMaxStyles; ++i) {
        if (g_Styles[i].used) continue;
        g_Styles[i].used = true;
        g_Styles[i].style = GUI::Styles::Message();
        return i + 1;
    }
    WIIXL_LOG("botw.gui: all %u style slots are in use", kMaxStyles);
    return 0;
}

extern "C" inline uint32_t GuiStyleRelease(uint32_t id) {
    if (id == 0 || id > kMaxStyles) return 0;
    g_Styles[id - 1].used = false;
    return 1;
}

extern "C" inline uint32_t GuiStyleFont(uint32_t id, int32_t fontId) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->font = static_cast<GUI::FontId>(fontId);
    return 1;
}

extern "C" inline uint32_t GuiStyleSize(uint32_t id, float x, float y) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->sizeX = x; s->sizeY = y;
    return 1;
}

// Top and bottom separately, because the game's own text is frequently a
// vertical gradient and collapsing them would lose that.
extern "C" inline uint32_t GuiStyleColor(uint32_t id, uint32_t rgbaTop, uint32_t rgbaBottom) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->colorTop = FromRGBA(rgbaTop);
    s->colorBottom = FromRGBA(rgbaBottom);
    return 1;
}

extern "C" inline uint32_t GuiStyleAlign(uint32_t id, int32_t align, int32_t valign) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->align = static_cast<GUI::Align>(align);
    s->valign = static_cast<GUI::VAlign>(valign);
    return 1;
}

extern "C" inline uint32_t GuiStyleShadow(uint32_t id, uint32_t on, uint32_t rgba,
                                          float dx, float dy) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->shadow = on != 0;
    s->shadowColor = FromRGBA(rgba);
    s->shadowX = dx;
    s->shadowY = dy;
    return 1;
}

extern "C" inline uint32_t GuiStyleScale(uint32_t id, float k) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->scale = k;
    return 1;
}

extern "C" inline uint32_t GuiStyleSpacing(uint32_t id, float charSpace, float lineSpace) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->charSpace = charSpace;
    s->lineSpace = lineSpace;
    return 1;
}

extern "C" inline uint32_t GuiStyleKerning(uint32_t id, uint32_t on) {
    GUI::TextStyle* s = StyleAt(id);
    if (!s) return 0;
    s->kerning = on != 0;
    return 1;
}

// --- canvas state ----------------------------------------------------------
//
// All of these return 0 outside a frame callback, which is the only time a
// canvas exists.

extern "C" inline uint32_t GuiInFrame() { return g_Canvas != nullptr ? 1u : 0u; }

extern "C" inline uint32_t GuiCanvasSize(float* w, float* h) {
    if (!g_Canvas || !w || !h) return 0;
    *w = g_Canvas->Width();
    *h = g_Canvas->Height();
    return 1;
}

extern "C" inline uint32_t GuiDeltaSeconds(float* out) {
    if (!g_Canvas || !out) return 0;
    *out = g_Canvas->DeltaSeconds();
    return 1;
}

extern "C" inline uint32_t GuiTimeSeconds(float* out) {
    if (!g_Canvas || !out) return 0;
    *out = g_Canvas->TimeSeconds();
    return 1;
}

extern "C" inline uint32_t GuiFrameNumber() {
    return g_Canvas ? g_Canvas->Frame() : 0u;
}

extern "C" inline uint32_t GuiAssetsReady() {
    return g_Canvas && g_Canvas->AssetsReady() ? 1u : 0u;
}

extern "C" inline uint32_t GuiPushAlpha(float factor) {
    if (!g_Canvas) return 0;
    g_Canvas->PushAlpha(factor);
    return 1;
}

extern "C" inline uint32_t GuiPopAlpha() {
    if (!g_Canvas) return 0;
    g_Canvas->PopAlpha();
    return 1;
}

// --- input, as the UI sees it ----------------------------------------------
//
// Distinct from botw.input: these are edge-triggered and navigation-aware,
// which is what a menu wants, where botw.input reports raw held state.

extern "C" inline uint32_t GuiPressed(int32_t button) {
    return g_Canvas && g_Canvas->Pressed(static_cast<Button>(button)) ? 1u : 0u;
}

extern "C" inline uint32_t GuiHeld(int32_t button) {
    return g_Canvas && g_Canvas->Held(static_cast<Button>(button)) ? 1u : 0u;
}

extern "C" inline uint32_t GuiNav(int32_t direction) {
    if (!g_Canvas) return 0;
    switch (direction) {
        case 0: return g_Canvas->NavUp() ? 1u : 0u;
        case 1: return g_Canvas->NavDown() ? 1u : 0u;
        case 2: return g_Canvas->NavLeft() ? 1u : 0u;
        case 3: return g_Canvas->NavRight() ? 1u : 0u;
        default: return 0;
    }
}

extern "C" inline uint32_t GuiAccept() { return g_Canvas && g_Canvas->Accept() ? 1u : 0u; }
extern "C" inline uint32_t GuiCancel() { return g_Canvas && g_Canvas->Cancel() ? 1u : 0u; }

extern "C" inline uint32_t GuiSticks(float* left2, float* right2) {
    if (!g_Canvas) return 0;
    if (left2) g_Canvas->GetLeftStick(left2[0], left2[1]);
    if (right2) g_Canvas->GetRightStick(right2[0], right2[1]);
    return 1;
}

// Stops the game seeing the input while a menu is open. Held rather than
// latched, so a mod that stops calling it releases the game's input rather than
// wedging the controller.
extern "C" inline uint32_t GuiCaptureInput() {
    if (!g_Canvas) return 0;
    g_Canvas->CaptureInput();
    return 1;
}

extern "C" inline uint32_t GuiIsInputCaptured() {
    return g_Canvas && g_Canvas->IsInputCaptured() ? 1u : 0u;
}

// --- drawing ---------------------------------------------------------------

extern "C" inline uint32_t GuiRect(float x, float y, float w, float h, uint32_t rgba) {
    if (!g_Canvas) return 0;
    g_Canvas->Rect(MakeRect(x, y, w, h), FromRGBA(rgba));
    return 1;
}

extern "C" inline uint32_t GuiRectGradient(float x, float y, float w, float h,
                                           uint32_t rgbaTop, uint32_t rgbaBottom) {
    if (!g_Canvas) return 0;
    g_Canvas->RectGradient(MakeRect(x, y, w, h), FromRGBA(rgbaTop), FromRGBA(rgbaBottom));
    return 1;
}

extern "C" inline uint32_t GuiRoundedBox(float x, float y, float w, float h,
                                         uint32_t rgba, float radius) {
    if (!g_Canvas) return 0;
    g_Canvas->RoundedBox(MakeRect(x, y, w, h), FromRGBA(rgba), radius);
    return 1;
}

extern "C" inline uint32_t GuiRoundedOutline(float x, float y, float w, float h,
                                             uint32_t rgba, float radius, float thickness) {
    if (!g_Canvas) return 0;
    g_Canvas->RoundedOutline(MakeRect(x, y, w, h), FromRGBA(rgba), radius, thickness);
    return 1;
}

extern "C" inline uint32_t GuiFrostedBox(float x, float y, float w, float h,
                                         uint32_t rgba, float radius) {
    if (!g_Canvas) return 0;
    g_Canvas->FrostedBox(MakeRect(x, y, w, h), FromRGBA(rgba), radius);
    return 1;
}

extern "C" inline uint32_t GuiMessageWindow(float x, float y, float w, float h,
                                            uint32_t rgba, uint32_t decorations) {
    if (!g_Canvas) return 0;
    g_Canvas->MessageWindow(MakeRect(x, y, w, h), FromRGBA(rgba), decorations != 0);
    return 1;
}

extern "C" inline uint32_t GuiSelectFrame(float x, float y, float w, float h,
                                          uint32_t frame, uint32_t glow) {
    if (!g_Canvas) return 0;
    g_Canvas->SelectFrame(MakeRect(x, y, w, h), FromRGBA(frame), FromRGBA(glow));
    return 1;
}

extern "C" inline uint32_t GuiBlurBehind(float x, float y, float w, float h, uint32_t tint) {
    if (!g_Canvas) return 0;
    g_Canvas->BlurBehind(MakeRect(x, y, w, h), FromRGBA(tint));
    return 1;
}

extern "C" inline uint32_t GuiText(float x, float y, const char* text, uint32_t styleId) {
    if (!g_Canvas || !text) return 0;
    const GUI::TextStyle* s = StyleAt(styleId);
    g_Canvas->Text(x, y, text, s ? *s : GUI::Styles::Message());
    return 1;
}

extern "C" inline uint32_t GuiTextBox(float x, float y, float w, float h,
                                      const char* text, uint32_t styleId, uint32_t wrap) {
    if (!g_Canvas || !text) return 0;
    const GUI::TextStyle* s = StyleAt(styleId);
    g_Canvas->TextBox(MakeRect(x, y, w, h), text, s ? *s : GUI::Styles::Message(), wrap != 0);
    return 1;
}

extern "C" inline uint32_t GuiMeasureText(const char* text, uint32_t styleId,
                                          float* w, float* h, float wrapWidth) {
    if (!g_Canvas || !text || !w || !h) return 0;
    const GUI::TextStyle* s = StyleAt(styleId);
    g_Canvas->MeasureText(text, s ? *s : GUI::Styles::Message(), *w, *h, wrapWidth);
    return 1;
}

// Sprites are the GUI's own atlas, by id. A mod's own image goes through
// botw.gfx's texture handles instead - see GuiImageTexture below.
extern "C" inline uint32_t GuiImage(int32_t sprite, float x, float y, float w, float h,
                                    uint32_t tint) {
    if (!g_Canvas) return 0;
    g_Canvas->Image(static_cast<GUI::Sprite>(sprite), MakeRect(x, y, w, h), FromRGBA(tint));
    return 1;
}

// Everything Image drops.
//
// Canvas::Image takes orientation, a BlendState and a rotation, and the wrapper
// above passes none of them - so until now a mod could draw a sprite and could
// not flip it, rotate it, or blend it additively. Nothing reported that: the
// coverage gate counts ENTRY POINTS, and Image was covered. A parameter that
// never crosses the boundary is invisible to a check that measures functions.
//
// BLEND IS A PRESET ID, not the struct. BlendState is six enum words, and the
// surface's own rule at the top of this file is that a large options struct
// goes in a host-side table rather than being flattened - but blends do not
// need a table either, because the interesting ones are already named. These
// eight are the presets in graphics/gx2.hpp, in the order declared there, and
// they cover what BotW's own materials use: 1557 of them are Additive alone.
//
// Out-of-range falls back to Alpha rather than refusing. A blend is a visual
// choice, and failing a draw over one would turn a cosmetic mistake into a
// missing element the mod author then has to go looking for.
enum GuiBlendPreset : int32_t {
    kBlendAlpha = 0,
    kBlendPremultiplied,
    kBlendAdditive,
    kBlendAdditivePremultiplied,
    kBlendOverlay,
    kBlendMultiply,
    kBlendOpaque,
    kBlendSubtract,
    kBlendPresetCount,
};

inline const GX2::BlendState& BlendFromId(int32_t id) {
    switch (id) {
        case kBlendPremultiplied:          return GX2::Blend::Premultiplied;
        case kBlendAdditive:               return GX2::Blend::Additive;
        case kBlendAdditivePremultiplied:  return GX2::Blend::AdditivePremultiplied;
        case kBlendOverlay:                return GX2::Blend::Overlay;
        case kBlendMultiply:               return GX2::Blend::Multiply;
        case kBlendOpaque:                 return GX2::Blend::Opaque;
        case kBlendSubtract:               return GX2::Blend::Subtract;
        default:                           return GX2::Blend::Alpha;
    }
}

// orient is the GUI::Orient bitmask: 0 none, 1 flip H, 2 flip V, 4 rotate 90,
// 8 rotate 180, 12 rotate 270. rotation is degrees, applied about the rect's
// centre, and is independent of the orient flags.
extern "C" inline uint32_t GuiImageEx(int32_t sprite, float x, float y, float w, float h,
                                      uint32_t tint, uint32_t orient, int32_t blend,
                                      float rotation) {
    if (!g_Canvas) return 0;
    g_Canvas->Image(static_cast<GUI::Sprite>(sprite), MakeRect(x, y, w, h),
                    FromRGBA(tint), static_cast<uint8_t>(orient),
                    BlendFromId(blend), rotation);
    return 1;
}

extern "C" inline uint32_t GuiSpriteReady(int32_t sprite) {
    return g_Canvas && g_Canvas->SpriteReady(static_cast<GUI::Sprite>(sprite)) ? 1u : 0u;
}

extern "C" inline uint32_t GuiSpriteSize(int32_t sprite, float* w, float* h) {
    if (!g_Canvas || !w || !h) return 0;
    g_Canvas->SpriteSize(static_cast<GUI::Sprite>(sprite), *w, *h);
    return 1;
}

extern "C" inline uint32_t GuiFontReady(int32_t fontId) {
    return g_Canvas && g_Canvas->FontReady(static_cast<GUI::FontId>(fontId)) ? 1u : 0u;
}

// --- widgets ---------------------------------------------------------------
//
// The interactive ones take a POINTER to the value they edit, where the module
// takes a reference. Same contract - the widget reads it, draws it, and writes
// it back when the user changes it - expressed as something that can cross.
// Each returns 1 when the value changed this frame, which is the whole point:
// a menu redraws every frame and only acts on the frames where something moved.

extern "C" inline uint32_t GuiButton(float x, float y, float w, float h,
                                     const char* label, uint32_t boxRgba) {
    if (!g_Canvas || !label) return 0;
    return g_Canvas->Button(MakeRect(x, y, w, h), label, FromRGBA(boxRgba)) ? 1u : 0u;
}

extern "C" inline uint32_t GuiPlateButton(float x, float y, float w, float h,
                                          const char* label) {
    if (!g_Canvas || !label) return 0;
    return g_Canvas->PlateButton(MakeRect(x, y, w, h), label) ? 1u : 0u;
}

extern "C" inline uint32_t GuiToggle(float x, float y, float w, float h,
                                     const char* label, uint32_t* value,
                                     const char* onText, const char* offText) {
    if (!g_Canvas || !label || !value) return 0;
    bool v = (*value != 0);
    const bool changed = g_Canvas->Toggle(MakeRect(x, y, w, h), label, v,
                                          onText ? onText : "ON",
                                          offText ? offText : "OFF");
    *value = v ? 1u : 0u;
    return changed ? 1u : 0u;
}

extern "C" inline uint32_t GuiSlider(float x, float y, float w, float h,
                                     const char* label, float* value,
                                     float minValue, float maxValue, float step) {
    if (!g_Canvas || !label || !value) return 0;
    return g_Canvas->Slider(MakeRect(x, y, w, h), label, *value,
                            minValue, maxValue, step) ? 1u : 0u;
}

// options is an array of C strings the MOD owns and which must outlive the
// call - it is read during the draw and not retained.
extern "C" inline uint32_t GuiSelector(float x, float y, float w, float h,
                                       const char* label, int32_t* index,
                                       const char* const* options, int32_t count) {
    if (!g_Canvas || !label || !index || !options || count <= 0) return 0;
    int i = static_cast<int>(*index);
    const bool changed = g_Canvas->Selector(MakeRect(x, y, w, h), label, i, options, count);
    *index = static_cast<int32_t>(i);
    return changed ? 1u : 0u;
}

extern "C" inline uint32_t GuiList(float x, float y, float w, float h,
                                   const char* const* items, int32_t count,
                                   int32_t* index) {
    if (!g_Canvas || !items || count <= 0 || !index) return 0;
    int i = static_cast<int>(*index);
    const bool changed = g_Canvas->List(MakeRect(x, y, w, h), items, count, i);
    *index = static_cast<int32_t>(i);
    return changed ? 1u : 0u;
}

extern "C" inline uint32_t GuiLabel(float x, float y, float w, float h,
                                    const char* text, uint32_t styleId) {
    if (!g_Canvas || !text) return 0;
    const GUI::TextStyle* st = StyleAt(styleId);
    if (st) g_Canvas->Label(MakeRect(x, y, w, h), text, *st);
    else g_Canvas->Label(MakeRect(x, y, w, h), text);
    return 1;
}

extern "C" inline uint32_t GuiPlate(float x, float y, float w, float h, uint32_t rgba) {
    if (!g_Canvas) return 0;
    g_Canvas->Plate(MakeRect(x, y, w, h), FromRGBA(rgba));
    return 1;
}

extern "C" inline uint32_t GuiMessageBox(const char* text, const char* name,
                                         float alpha, uint32_t showArrow) {
    if (!g_Canvas || !text) return 0;
    g_Canvas->MessageBox(text, name, alpha, showArrow != 0);
    return 1;
}

extern "C" inline uint32_t GuiButtonIcon(int32_t sprite, float x, float y,
                                         float size, uint32_t rgba) {
    if (!g_Canvas) return 0;
    g_Canvas->ButtonIcon(static_cast<GUI::Sprite>(sprite), x, y, size, FromRGBA(rgba));
    return 1;
}

// Returns the width it drew, so a row of hints can be laid out left to right
// without the mod measuring each one first.
extern "C" inline uint32_t GuiKeyHint(int32_t sprite, const char* label,
                                      float x, float y, uint32_t rgba, float* outWidth) {
    if (!g_Canvas || !label) return 0;
    const float w = g_Canvas->KeyHint(static_cast<GUI::Sprite>(sprite), label, x, y,
                                      FromRGBA(rgba));
    if (outWidth) *outWidth = w;
    return 1;
}

extern "C" inline uint32_t GuiCursorCorners(float x, float y, float w, float h,
                                            uint32_t rgba, float size) {
    if (!g_Canvas) return 0;
    g_Canvas->CursorCorners(MakeRect(x, y, w, h), FromRGBA(rgba), size);
    return 1;
}

extern "C" inline uint32_t GuiCursorBrackets(float x, float y, float w, float h,
                                             uint32_t rgba, float size) {
    if (!g_Canvas) return 0;
    g_Canvas->CursorBrackets(MakeRect(x, y, w, h), FromRGBA(rgba), size);
    return 1;
}

extern "C" inline uint32_t GuiBoxedCursor(float x, float y, float w, float h, uint32_t rgba) {
    if (!g_Canvas) return 0;
    g_Canvas->BoxedCursor(MakeRect(x, y, w, h), FromRGBA(rgba));
    return 1;
}

extern "C" inline uint32_t GuiImageUV(int32_t sprite, float x, float y, float w, float h,
                                      float u0, float v0, float u1, float v1,
                                      uint32_t tint) {
    if (!g_Canvas) return 0;
    g_Canvas->ImageUV(static_cast<GUI::Sprite>(sprite), MakeRect(x, y, w, h),
                      u0, v0, u1, v1, FromRGBA(tint));
    return 1;
}

extern "C" inline uint32_t GuiImageAt(int32_t sprite, float x, float y, uint32_t tint) {
    if (!g_Canvas) return 0;
    g_Canvas->ImageAt(static_cast<GUI::Sprite>(sprite), x, y, FromRGBA(tint));
    return 1;
}

extern "C" inline uint32_t GuiBlurBehindFaded(float x, float y, float w, float h,
                                              uint32_t tint) {
    if (!g_Canvas) return 0;
    g_Canvas->BlurBehindFaded(MakeRect(x, y, w, h), FromRGBA(tint));
    return 1;
}

// --- canvas geometry and timing -------------------------------------------

extern "C" inline uint32_t GuiDeviceSize(uint32_t* w, uint32_t* h) {
    if (!g_Canvas || !w || !h) return 0;
    *w = g_Canvas->DeviceWidth();
    *h = g_Canvas->DeviceHeight();
    return 1;
}

extern "C" inline uint32_t GuiPixelScale(float* out2) {
    if (!g_Canvas || !out2) return 0;
    out2[0] = g_Canvas->PixelScaleX();
    out2[1] = g_Canvas->PixelScaleY();
    return 1;
}

extern "C" inline uint32_t GuiViewportOffset(float* out2) {
    if (!g_Canvas || !out2) return 0;
    out2[0] = g_Canvas->ViewportOffsetX();
    out2[1] = g_Canvas->ViewportOffsetY();
    return 1;
}

extern "C" inline uint32_t GuiSnap(float x, float y, float* out2) {
    if (!g_Canvas || !out2) return 0;
    out2[0] = g_Canvas->SnapX(x);
    out2[1] = g_Canvas->SnapY(y);
    return 1;
}

// A 0..1 sawtooth and a -1..1 sine over the given period. Both come from the
// canvas's own clock rather than a mod's frame counter, so animations stay in
// step with the UI and with each other.
extern "C" inline uint32_t GuiPhase(float periodSeconds, float* out) {
    if (!g_Canvas || !out) return 0;
    *out = g_Canvas->Phase(periodSeconds);
    return 1;
}

extern "C" inline uint32_t GuiWave(float periodSeconds, float* out) {
    if (!g_Canvas || !out) return 0;
    *out = g_Canvas->Wave(periodSeconds);
    return 1;
}

extern "C" inline uint32_t GuiFramesPerSecond(float* out) {
    if (!g_Canvas || !out) return 0;
    *out = g_Canvas->FramesPerSecond();
    return 1;
}

extern "C" inline uint32_t GuiCurrentAlpha(float* out) {
    if (!g_Canvas || !out) return 0;
    *out = g_Canvas->CurrentAlpha();
    return 1;
}

// --- more GUI-level settings ----------------------------------------------

extern "C" inline uint32_t GuiSetScalingMode(int32_t mode) {
    GUI::SetScalingMode(static_cast<GUI::ScalingMode>(mode));
    return 1;
}

extern "C" inline int32_t GuiGetScalingMode() {
    return static_cast<int32_t>(GUI::GetScalingMode());
}

extern "C" inline uint32_t GuiGetOutputAspect(float* out) {
    if (!out) return 0;
    *out = GUI::GetOutputAspect();
    return 1;
}

extern "C" inline uint32_t GuiIsBackdropBlurEnabled() {
    return GUI::IsBackdropBlurEnabled() ? 1u : 0u;
}

extern "C" inline uint32_t GuiSetUiSoundEvents(const char* cursorMove, const char* decide) {
    GUI::SetUiSoundEvents(cursorMove, decide);
    return 1;
}

extern "C" inline uint32_t GuiAreUiSoundsEnabled() {
    return GUI::AreUiSoundsEnabled() ? 1u : 0u;
}

extern "C" inline uint32_t GuiSetLoadBudget(uint32_t bytesPerFrame) {
    GUI::SetLoadBudget(bytesPerFrame);
    return 1;
}

extern "C" inline uint32_t GuiFontSheetBytes(int32_t fontId) {
    return GUI::FontSheetBytes(static_cast<GUI::FontId>(fontId));
}

// --- focus -----------------------------------------------------------------

extern "C" inline uint32_t GuiSetFocus(int32_t index) {
    if (!g_Canvas) return 0;
    g_Canvas->SetFocus(index);
    return 1;
}

extern "C" inline uint32_t GuiClaimFocus() {
    return g_Canvas && g_Canvas->ClaimFocus() ? 1u : 0u;
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Init",                &GuiInit),
    WIIXL_SURFACE_SYMBOL("IsReady",             &GuiIsReady),
    WIIXL_SURFACE_SYMBOL("RegisterFrame",       &GuiRegisterFrame),
    WIIXL_SURFACE_SYMBOL("FrameCallbackCount",  &GuiFrameCallbackCount),
    WIIXL_SURFACE_SYMBOL("SetAssetPaths",       &GuiSetAssetPaths),
    WIIXL_SURFACE_SYMBOL("LoadNow",             &GuiLoadNow),
    WIIXL_SURFACE_SYMBOL("RequestFont",         &GuiRequestFont),
    WIIXL_SURFACE_SYMBOL("FontRequested",       &GuiFontRequested),
    WIIXL_SURFACE_SYMBOL("SetBackdropBlur",     &GuiSetBackdropBlur),
    WIIXL_SURFACE_SYMBOL("SetPixelSnapping",    &GuiSetPixelSnapping),
    WIIXL_SURFACE_SYMBOL("SetOutputAspect",     &GuiSetOutputAspect),
    WIIXL_SURFACE_SYMBOL("GetEffectiveOutputAspect", &GuiGetEffectiveOutputAspect),
    WIIXL_SURFACE_SYMBOL("SetUiSounds",         &GuiSetUiSounds),

    WIIXL_SURFACE_SYMBOL("StyleCreate",         &GuiStyleCreate),
    WIIXL_SURFACE_SYMBOL("StyleRelease",        &GuiStyleRelease),
    WIIXL_SURFACE_SYMBOL("StyleFont",           &GuiStyleFont),
    WIIXL_SURFACE_SYMBOL("StyleSize",           &GuiStyleSize),
    WIIXL_SURFACE_SYMBOL("StyleColor",          &GuiStyleColor),
    WIIXL_SURFACE_SYMBOL("StyleAlign",          &GuiStyleAlign),
    WIIXL_SURFACE_SYMBOL("StyleShadow",         &GuiStyleShadow),
    WIIXL_SURFACE_SYMBOL("StyleScale",          &GuiStyleScale),
    WIIXL_SURFACE_SYMBOL("StyleSpacing",        &GuiStyleSpacing),
    WIIXL_SURFACE_SYMBOL("StyleKerning",        &GuiStyleKerning),

    WIIXL_SURFACE_SYMBOL("InFrame",             &GuiInFrame),
    WIIXL_SURFACE_SYMBOL("CanvasSize",          &GuiCanvasSize),
    WIIXL_SURFACE_SYMBOL("DeltaSeconds",        &GuiDeltaSeconds),
    WIIXL_SURFACE_SYMBOL("TimeSeconds",         &GuiTimeSeconds),
    WIIXL_SURFACE_SYMBOL("FrameNumber",         &GuiFrameNumber),
    WIIXL_SURFACE_SYMBOL("AssetsReady",         &GuiAssetsReady),
    WIIXL_SURFACE_SYMBOL("PushAlpha",           &GuiPushAlpha),
    WIIXL_SURFACE_SYMBOL("PopAlpha",            &GuiPopAlpha),

    WIIXL_SURFACE_SYMBOL("Pressed",             &GuiPressed),
    WIIXL_SURFACE_SYMBOL("Held",                &GuiHeld),
    WIIXL_SURFACE_SYMBOL("Nav",                 &GuiNav),
    WIIXL_SURFACE_SYMBOL("Accept",              &GuiAccept),
    WIIXL_SURFACE_SYMBOL("Cancel",              &GuiCancel),
    WIIXL_SURFACE_SYMBOL("Sticks",              &GuiSticks),
    WIIXL_SURFACE_SYMBOL("CaptureInput",        &GuiCaptureInput),
    WIIXL_SURFACE_SYMBOL("IsInputCaptured",     &GuiIsInputCaptured),

    WIIXL_SURFACE_SYMBOL("Rect",                &GuiRect),
    WIIXL_SURFACE_SYMBOL("RectGradient",        &GuiRectGradient),
    WIIXL_SURFACE_SYMBOL("RoundedBox",          &GuiRoundedBox),
    WIIXL_SURFACE_SYMBOL("RoundedOutline",      &GuiRoundedOutline),
    WIIXL_SURFACE_SYMBOL("FrostedBox",          &GuiFrostedBox),
    WIIXL_SURFACE_SYMBOL("MessageWindow",       &GuiMessageWindow),
    WIIXL_SURFACE_SYMBOL("SelectFrame",         &GuiSelectFrame),
    WIIXL_SURFACE_SYMBOL("BlurBehind",          &GuiBlurBehind),
    WIIXL_SURFACE_SYMBOL("Text",                &GuiText),
    WIIXL_SURFACE_SYMBOL("TextBox",             &GuiTextBox),
    WIIXL_SURFACE_SYMBOL("MeasureText",         &GuiMeasureText),
    WIIXL_SURFACE_SYMBOL("Image",               &GuiImage),
    WIIXL_SURFACE_SYMBOL("ImageEx",             &GuiImageEx),
    WIIXL_SURFACE_SYMBOL("SpriteReady",         &GuiSpriteReady),
    WIIXL_SURFACE_SYMBOL("SpriteSize",          &GuiSpriteSize),
    WIIXL_SURFACE_SYMBOL("FontReady",           &GuiFontReady),

    WIIXL_SURFACE_SYMBOL("Button",              &GuiButton),
    WIIXL_SURFACE_SYMBOL("PlateButton",         &GuiPlateButton),
    WIIXL_SURFACE_SYMBOL("Toggle",              &GuiToggle),
    WIIXL_SURFACE_SYMBOL("Slider",              &GuiSlider),
    WIIXL_SURFACE_SYMBOL("Selector",            &GuiSelector),
    WIIXL_SURFACE_SYMBOL("List",                &GuiList),
    WIIXL_SURFACE_SYMBOL("Label",               &GuiLabel),
    WIIXL_SURFACE_SYMBOL("Plate",               &GuiPlate),
    WIIXL_SURFACE_SYMBOL("MessageBox",          &GuiMessageBox),
    WIIXL_SURFACE_SYMBOL("ButtonIcon",          &GuiButtonIcon),
    WIIXL_SURFACE_SYMBOL("KeyHint",             &GuiKeyHint),
    WIIXL_SURFACE_SYMBOL("CursorCorners",       &GuiCursorCorners),
    WIIXL_SURFACE_SYMBOL("CursorBrackets",      &GuiCursorBrackets),
    WIIXL_SURFACE_SYMBOL("BoxedCursor",         &GuiBoxedCursor),
    WIIXL_SURFACE_SYMBOL("ImageUV",             &GuiImageUV),
    WIIXL_SURFACE_SYMBOL("ImageAt",             &GuiImageAt),
    WIIXL_SURFACE_SYMBOL("BlurBehindFaded",     &GuiBlurBehindFaded),

    WIIXL_SURFACE_SYMBOL("DeviceSize",          &GuiDeviceSize),
    WIIXL_SURFACE_SYMBOL("PixelScale",          &GuiPixelScale),
    WIIXL_SURFACE_SYMBOL("ViewportOffset",      &GuiViewportOffset),
    WIIXL_SURFACE_SYMBOL("Snap",                &GuiSnap),
    WIIXL_SURFACE_SYMBOL("Phase",               &GuiPhase),
    WIIXL_SURFACE_SYMBOL("Wave",                &GuiWave),
    WIIXL_SURFACE_SYMBOL("FramesPerSecond",     &GuiFramesPerSecond),
    WIIXL_SURFACE_SYMBOL("CurrentAlpha",        &GuiCurrentAlpha),

    WIIXL_SURFACE_SYMBOL("SetScalingMode",      &GuiSetScalingMode),
    WIIXL_SURFACE_SYMBOL("GetScalingMode",      &GuiGetScalingMode),
    WIIXL_SURFACE_SYMBOL("GetOutputAspect",     &GuiGetOutputAspect),
    WIIXL_SURFACE_SYMBOL("IsBackdropBlurEnabled", &GuiIsBackdropBlurEnabled),
    WIIXL_SURFACE_SYMBOL("SetUiSoundEvents",    &GuiSetUiSoundEvents),
    WIIXL_SURFACE_SYMBOL("AreUiSoundsEnabled",  &GuiAreUiSoundsEnabled),
    WIIXL_SURFACE_SYMBOL("SetLoadBudget",       &GuiSetLoadBudget),
    WIIXL_SURFACE_SYMBOL("FontSheetBytes",      &GuiFontSheetBytes),

    WIIXL_SURFACE_SYMBOL("SetFocus",            &GuiSetFocus),
    WIIXL_SURFACE_SYMBOL("ClaimFocus",          &GuiClaimFocus),
};

} // namespace impl

inline void LogState() {
    if (impl::g_FrameCount == 0) {
        WIIXL_LOG("botw.gui: no module registered a frame callback");
        return;
    }
    WIIXL_LOG("botw.gui: %u module(s) building UI", impl::g_FrameCount);
    for (uint32_t i = 0; i < impl::g_FrameCount; ++i) {
        WIIXL_LOG("botw.gui:   %u. %s", i + 1, impl::g_Frames[i].owner);
    }
}

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::GuiSurface
