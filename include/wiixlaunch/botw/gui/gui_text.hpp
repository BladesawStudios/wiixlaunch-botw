#pragma once

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include <cstdint>
#include <cstddef>

#include "gui_types.hpp"
#include "gui_render.hpp"
#include "../graphics/bffnt.hpp"

// GUI text: UTF-8 in, glyph quads out, laid out the way nw::font/lyt lays
// the game's own text out.
//
// A TextStyle's (sizeX, sizeY) is the pixel size the font's (width, height)
// cell is scaled to - the exact convention the .bflyt text panes use, so
// numbers lifted from a layout reproduce the game's text at the same size.
// Per glyph: the cell (glyphWidth x cellHeight) is drawn at pen + left, the
// pen advances by charWidth, both scaled; lines advance by lineFeed. Colours
// are per-vertex top/bottom, as in the layouts. A shadow is the same text
// drawn first at an offset (which is also how lyt does it).

namespace WiiXLaunch::BotW::GUI::impl {

constexpr int kMaxTextLines = 32;

inline uint32_t DecodeUtf8(const char*& p) {
    const uint8_t c = static_cast<uint8_t>(*p);
    if (c == 0) return 0;
    if (c < 0x80) { ++p; return c; }
    if ((c & 0xE0) == 0xC0 && p[1]) {
        const uint32_t cp = (static_cast<uint32_t>(c & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
        p += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && p[1] && p[2]) {
        const uint32_t cp = (static_cast<uint32_t>(c & 0x0F) << 12) |
                            ((static_cast<uint8_t>(p[1]) & 0x3F) << 6) | (static_cast<uint8_t>(p[2]) & 0x3F);
        p += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        const uint32_t cp = (static_cast<uint32_t>(c & 0x07) << 18) | ((static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
                            ((static_cast<uint8_t>(p[2]) & 0x3F) << 6) | (static_cast<uint8_t>(p[3]) & 0x3F);
        p += 4;
        return cp;
    }
    ++p;
    return '?';
}

struct FontScale {
    float sx = 1.0f;
    float sy = 1.0f;
    float lineHeight = 0.0f;
    float cellHeight = 0.0f;
};

inline bool ResolveFont(const TextStyle& st, const BFFNT::Font*& font, FontScale& fs) {
    if (st.font >= FontId::Count) return false;
    font = &FontAt(st.font);
    // Fall back rather than drawing nothing: NormalS_00 is not loaded (see
    // gui_render.hpp), and a mod naming a font the loader skipped should
    // still get text.
    if (!font->IsReady()) font = &FontAt(FontId::Normal);
    if (!font->IsReady()) return false;
    fs.sx = st.sizeX * st.scale / static_cast<float>(font->finf.width);
    fs.sy = st.sizeY * st.scale / static_cast<float>(font->finf.height);
    fs.lineHeight = static_cast<float>(font->finf.lineFeed) * fs.sy + st.lineSpace * st.scale;
    fs.cellHeight = static_cast<float>(font->tglp.cellHeight) * fs.sy;
    return true;
}

inline float Advance(const BFFNT::Font& font, const FontScale& fs, const TextStyle& st, uint16_t glyph) {
    const BFFNT::GlyphWidths w = font.Widths(glyph);
    return static_cast<float>(w.charWidth) * fs.sx + st.charSpace * st.scale;
}

// Kerning between two adjacent character codes, in layout pixels. The KRNG
// table is keyed by character code (not glyph index) and its amounts are in
// the same glyph units as the widths, so it scales with the font size like
// everything else. BotW's pairs are small (-2..+2 units) but they are what
// keeps "AV", "To" and "P," from reading loose beside the game's own text.
inline float Kern(const BFFNT::Font& font, const FontScale& fs, const TextStyle& st, uint32_t prev, uint32_t cur) {
    if (!st.kerning || prev == 0) return 0.0f;
    const int32_t k = font.Kerning(prev, cur);
    return k ? static_cast<float>(k) * fs.sx : 0.0f;
}

struct LineSpan {
    const char* begin = nullptr;
    const char* end = nullptr;
    float width = 0.0f;
};

// Splits text into lines at '\n' and, when wrapWidth > 0, at the last space
// that keeps a line within wrapWidth (a single word longer than the width is
// broken mid-word). Returns the line count.
inline int LayoutLines(const BFFNT::Font& font, const FontScale& fs, const TextStyle& st, const char* text,
                       float wrapWidth, LineSpan* out, int maxLines) {
    int n = 0;
    const char* p = text;
    while (p && *p && n < maxLines) {
        const char* lineStart = p;
        const char* lastSpace = nullptr;
        float widthAtLastSpace = 0.0f;
        float w = 0.0f;
        const char* q = p;
        bool emitted = false;
        uint32_t prev = 0;
        while (*q) {
            const char* before = q;
            const uint32_t cp = DecodeUtf8(q);
            if (cp == '\n') {
                out[n++] = LineSpan{lineStart, before, w};
                p = q;
                emitted = true;
                break;
            }
            if (cp == '\r') continue;
            const float adv = Advance(font, fs, st, font.GlyphIndex(cp)) + Kern(font, fs, st, prev, cp);
            prev = cp;
            if (wrapWidth > 0.0f && w + adv > wrapWidth && before > lineStart) {
                if (lastSpace) {
                    out[n++] = LineSpan{lineStart, lastSpace, widthAtLastSpace};
                    p = lastSpace + 1;
                } else {
                    out[n++] = LineSpan{lineStart, before, w};
                    p = before;
                }
                emitted = true;
                break;
            }
            w += adv;
            if (cp == ' ') {
                lastSpace = before;
                widthAtLastSpace = w - adv;
            }
        }
        if (!emitted) {
            out[n++] = LineSpan{lineStart, q, w};
            p = q;
        }
    }
    return n;
}

// How many lines the text wraps to at this width, 0 if the font is missing.
inline int CountLines(const TextStyle& st, const char* text, float wrapWidth) {
    const BFFNT::Font* font = nullptr;
    FontScale fs;
    if (!text || !*text || !ResolveFont(st, font, fs)) return 0;
    LineSpan lines[kMaxTextLines];
    return LayoutLines(*font, fs, st, text, wrapWidth, lines, kMaxTextLines);
}

inline void MeasureText(const TextStyle& st, const char* text, float wrapWidth, float& outW, float& outH) {
    outW = 0.0f;
    outH = 0.0f;
    const BFFNT::Font* font = nullptr;
    FontScale fs;
    if (!text || !ResolveFont(st, font, fs)) return;
    LineSpan lines[kMaxTextLines];
    const int n = LayoutLines(*font, fs, st, text, wrapWidth, lines, kMaxTextLines);
    for (int i = 0; i < n; ++i) if (lines[i].width > outW) outW = lines[i].width;
    outH = static_cast<float>(n) * fs.lineHeight;
}

// Draws one laid-out line with its top-left at (x, y).
inline void DrawRun(const BFFNT::Font& font, const FontScale& fs, const TextStyle& st, const LineSpan& line,
                    float x, float y, Color top, Color bottom) {
    float pen = x;
    const char* p = line.begin;
    uint32_t prev = 0;
    while (p < line.end) {
        const uint32_t cp = DecodeUtf8(p);
        if (cp == 0) break;
        if (cp == '\r') continue;
        pen += Kern(font, fs, st, prev, cp);
        prev = cp;
        const uint16_t glyph = font.GlyphIndex(cp);
        const BFFNT::GlyphWidths w = font.Widths(glyph);
        if (w.glyphWidth > 0 && cp != ' ') {
            uint32_t sheet = 0;
            float u0, v0, u1, v1;
            if (font.GlyphUV(glyph, sheet, u0, v0, u1, v1) && font.sheetTextures[sheet]) {
                const float gx = pen + static_cast<float>(w.left) * fs.sx;
                const float gw = static_cast<float>(w.glyphWidth) * fs.sx;
                // snap = false: rounding every glyph's own edges to device
                // pixels would jitter the spacing between them, and at the
                // sub-1:1 scale BotW's 854x480 GamePad buffer produces it
                // would visibly eat into the letterforms.
                EmitQuad(font.sheetTextures[sheet], gx, y, gx + gw, y + fs.cellHeight,
                         u0, v0, u1, v1, top, top, bottom, bottom, OrientNone, st.blend, false);
            }
        }
        pen += static_cast<float>(w.charWidth) * fs.sx + st.charSpace * st.scale;
    }
}

// Lays text out inside `box` (align/valign from the style; wrapWidth > 0
// wraps at that width, typically box.w) and draws it, shadow pass first.
inline void DrawTextBox(const Rect& box, const char* text, const TextStyle& st, float wrapWidth) {
    const BFFNT::Font* font = nullptr;
    FontScale fs;
    if (!text || !*text || !ResolveFont(st, font, fs)) return;
    if (st.colorTop.a == 0 && st.colorBottom.a == 0 && (!st.shadow || st.shadowColor.a == 0)) return;

    LineSpan lines[kMaxTextLines];
    const int n = LayoutLines(*font, fs, st, text, wrapWidth, lines, kMaxTextLines);
    if (n == 0) return;

    const float totalH = static_cast<float>(n) * fs.lineHeight;
    float y = box.y;
    if (st.valign == VAlign::Middle) y = box.y + (box.h - totalH) * 0.5f;
    else if (st.valign == VAlign::Bottom) y = box.Bottom() - totalH;

    for (int pass = st.shadow && st.shadowColor.a ? 0 : 1; pass < 2; ++pass) {
        const float dx = pass == 0 ? st.shadowX * st.scale : 0.0f;
        const float dy = pass == 0 ? st.shadowY * st.scale : 0.0f;
        const Color top = pass == 0 ? st.shadowColor : st.colorTop;
        const Color bottom = pass == 0 ? st.shadowColor : st.colorBottom;
        float ly = y;
        for (int i = 0; i < n; ++i) {
            float x = box.x;
            if (st.align == Align::Center) x = box.x + (box.w - lines[i].width) * 0.5f;
            else if (st.align == Align::Right) x = box.Right() - lines[i].width;
            DrawRun(*font, fs, st, lines[i], x + dx, ly + dy, top, bottom);
            ly += fs.lineHeight;
        }
    }
}

} // namespace WiiXLaunch::BotW::GUI::impl

#endif
