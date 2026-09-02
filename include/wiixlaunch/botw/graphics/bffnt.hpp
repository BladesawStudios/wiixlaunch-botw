#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstdint>
#include <cstddef>

#include "gx2_shader_types.hpp"

// WiiXLaunch::BotW::BFFNT - the NintendoWare bitmap font format the game's
// text is set in (Font/Font_XX.sbfarc -> Normal_00.bffnt etc.), parsed just
// far enough to draw text with the game's own glyph sheets:
//
//   file header 0x14: "FFNT" u16 BOM u16 headerSize u32 version u32 fileSize u16 blockCount u16 pad
//   FINF (0x20): u8 fontType u8 height u8 width u8 ascent u16 lineFeed u16 alterCharIndex
//                s8 defaultLeft u8 defaultGlyphWidth u8 defaultCharWidth u8 encoding
//                u32 tglpOffset u32 cwdhOffset u32 cmapOffset   (block BODY offsets, i.e. magic + 8)
//   TGLP (0x20): u8 cellWidth u8 cellHeight u8 sheetCount u8 maxCharWidth u32 sheetSize
//                u16 baselinePos u16 sheetFormat u16 columns u16 rows u16 sheetWidth u16 sheetHeight
//                u32 sheetDataOffset          ... then the sheets, back to back
//   CWDH: u16 startIndex u16 endIndex u32 nextOffset, then {s8 left, u8 glyphWidth, u8 charWidth} per glyph
//   CMAP: u16 codeBegin u16 codeEnd u16 method u16 pad u32 nextOffset, then
//         method 0: u16 firstIndex / 1: u16 index per code / 2: u16 count, {u16 code, u16 index}...
//   KRNG: u16 firstCount, {u16 firstChar, u16 offset}*firstCount, then per
//         first char {u16 pairCount, {u16 secondChar, s16 amount}*pairCount}.
//         The offsets are in 16-BIT WORDS from the start of the block body,
//         not bytes (checked against both fonts: the sub-tables then tile the
//         region after the index table exactly, 3434 of 3434 bytes in
//         Normal_00). Both tables are sorted, so both lookups binary-search.
//         Amounts are in the same glyph units as the CWDH widths (BotW's are
//         -2..+2) and apply between two CHARACTER CODES, not glyph indices.
//
// Things established against the real files rather than taken on trust
// (see docs/gui.md): sheet format 12 here is BC4 (not the BC1 some format
// tables say) and 8 is A8; every sheet is a GX2 2D-tiled surface (tile mode
// 4, swizzle 0) whose byte size equals the surface GX2 computes for it, so
// it is uploaded as-is; and the sheets are stored upside down relative to
// the layout art - glyph row 0 sits at the BOTTOM of the GPU texture and
// each glyph is vertically mirrored, so glyph V coordinates run the other
// way (see GlyphUV). Cells are (cellWidth + 1) x (cellHeight + 1) with a
// one-pixel border, glyph pixels start at +1,+1.
//
// The loader keeps only the header region and the CWDH/CMAP tail in RAM
// (a few KB); the sheets go straight into GPU memory.

namespace WiiXLaunch::BotW::BFFNT {

constexpr uint32_t kFileHeaderSize = 0x14;
constexpr uint32_t kMaxSheets = 4;
constexpr uint16_t kNoGlyph = 0xFFFF;

inline uint16_t RdU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t RdU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

struct Finf {
    uint8_t fontType = 0;
    uint8_t height = 0;         // cell height, the unit font sizes are expressed against
    uint8_t width = 0;          // max glyph width, likewise
    uint8_t ascent = 0;
    uint16_t lineFeed = 0;
    uint16_t alterCharIndex = 0;
    int8_t defaultLeft = 0;
    uint8_t defaultGlyphWidth = 0;
    uint8_t defaultCharWidth = 0;
    uint8_t encoding = 0;       // 1 = UTF-16
    uint32_t tglpOffset = 0;
    uint32_t cwdhOffset = 0;
    uint32_t cmapOffset = 0;
};

struct Tglp {
    uint8_t cellWidth = 0;
    uint8_t cellHeight = 0;
    uint8_t sheetCount = 0;
    uint8_t maxCharWidth = 0;
    uint32_t sheetSize = 0;
    uint16_t baselinePos = 0;
    uint16_t sheetFormat = 0;
    uint16_t columns = 0;
    uint16_t rows = 0;
    uint16_t sheetWidth = 0;
    uint16_t sheetHeight = 0;
    uint32_t sheetDataOffset = 0;
};

struct GlyphWidths {
    int8_t left = 0;
    uint8_t glyphWidth = 0;
    uint8_t charWidth = 0;
};

// Sheet format -> GX2 format / component map. Only what the game ships.
inline bool MapSheetFormat(uint16_t fmt, uint32_t& gx2Format, uint32_t& compMap) {
    switch (fmt) {
    case 8:  gx2Format = GX2Types::kSurfaceFormatUnormR8;  compMap = GX2Types::kCompMapAlphaOnly; return true; // A8
    case 7:  gx2Format = GX2Types::kSurfaceFormatUnormR8;  compMap = GX2Types::kCompMapLumOnly;   return true; // L8
    case 12: gx2Format = GX2Types::kSurfaceFormatUnormBC4; compMap = GX2Types::kCompMapAlphaOnly; return true; // BC4 (measured)
    case 15: gx2Format = GX2Types::kSurfaceFormatUnormBC4; compMap = GX2Types::kCompMapAlphaOnly; return true;
    default: return false;
    }
}

class Font {
public:
    Finf finf;
    Tglp tglp;
    uint32_t fileSize = 0;

    // GPU-side sheets, filled in by whoever uploads them (gui/gui_assets.hpp).
    uintptr_t sheetTextures[kMaxSheets] = {};

    bool HeaderValid() const { return m_HeaderOk; }
    bool TablesValid() const { return m_TablesOk; }
    bool IsReady() const { return m_HeaderOk && m_TablesOk && tglp.sheetCount > 0 && sheetTextures[0] != 0; }

    // Parses the file header, FINF and TGLP out of the first bytes of the
    // file. Everything before the sheet data (tglp.sheetDataOffset, 0x2000 in
    // the game's fonts) is enough.
    bool ParseHeader(const uint8_t* buf, uint32_t length) {
        m_HeaderOk = false;
        if (!buf || length < kFileHeaderSize + 8) return false;
        if (buf[0] != 'F' || buf[1] != 'F' || buf[2] != 'N' || buf[3] != 'T') return false;
        if (!(buf[4] == 0xFE && buf[5] == 0xFF)) return false;   // big-endian (Wii U) only
        const uint32_t headerSize = RdU16(buf + 6);
        fileSize = RdU32(buf + 8);
        const uint32_t finfOff = headerSize;
        if (finfOff + 8 + 0x18 > length) return false;
        if (buf[finfOff] != 'F' || buf[finfOff + 1] != 'I' || buf[finfOff + 2] != 'N' || buf[finfOff + 3] != 'F') return false;
        const uint8_t* f = buf + finfOff + 8;
        finf.fontType = f[0];
        finf.height = f[1];
        finf.width = f[2];
        finf.ascent = f[3];
        finf.lineFeed = RdU16(f + 4);
        finf.alterCharIndex = RdU16(f + 6);
        finf.defaultLeft = static_cast<int8_t>(f[8]);
        finf.defaultGlyphWidth = f[9];
        finf.defaultCharWidth = f[10];
        finf.encoding = f[11];
        finf.tglpOffset = RdU32(f + 12);
        finf.cwdhOffset = RdU32(f + 16);
        finf.cmapOffset = RdU32(f + 20);

        if (finf.tglpOffset < 8 || finf.tglpOffset + 0x18 > length) return false;
        const uint8_t* magic = buf + finf.tglpOffset - 8;
        if (magic[0] != 'T' || magic[1] != 'G' || magic[2] != 'L' || magic[3] != 'P') return false;
        const uint8_t* t = buf + finf.tglpOffset;
        tglp.cellWidth = t[0];
        tglp.cellHeight = t[1];
        tglp.sheetCount = t[2];
        tglp.maxCharWidth = t[3];
        tglp.sheetSize = RdU32(t + 4);
        tglp.baselinePos = RdU16(t + 8);
        tglp.sheetFormat = RdU16(t + 10);
        tglp.columns = RdU16(t + 12);
        tglp.rows = RdU16(t + 14);
        tglp.sheetWidth = RdU16(t + 16);
        tglp.sheetHeight = RdU16(t + 18);
        tglp.sheetDataOffset = RdU32(t + 20);

        if (tglp.sheetCount == 0 || tglp.sheetCount > kMaxSheets || tglp.columns == 0 || tglp.rows == 0) return false;
        if (tglp.sheetSize == 0 || tglp.sheetWidth == 0 || tglp.sheetHeight == 0) return false;
        if (finf.height == 0 || finf.width == 0) return false;
        m_HeaderOk = true;
        return true;
    }

    // File offset where the CWDH/CMAP tail starts: the first byte after the
    // last sheet (both tables follow the sheets in the game's fonts).
    uint32_t SheetsEnd() const { return tglp.sheetDataOffset + tglp.sheetSize * tglp.sheetCount; }
    uint32_t TailStart() const {
        uint32_t a = finf.cwdhOffset >= 8 ? finf.cwdhOffset - 8 : 0;
        uint32_t b = finf.cmapOffset >= 8 ? finf.cmapOffset - 8 : 0;
        uint32_t t = a < b ? a : b;
        return t < SheetsEnd() ? SheetsEnd() : t;
    }

    // Hands over the tail of the file [tailFileOffset, tailFileOffset +
    // length). The buffer must stay alive for as long as the font is used.
    bool SetTables(const uint8_t* tail, uint32_t tailFileOffset, uint32_t length) {
        m_Tail = tail;
        m_TailOffset = tailFileOffset;
        m_TailLength = length;
        m_TablesOk = false;
        m_Kerning = nullptr;
        m_KerningLength = 0;
        if (!tail || !m_HeaderOk) return false;
        // Sanity: both chains must start inside the tail.
        if (!InTail(finf.cwdhOffset, 8) || !InTail(finf.cmapOffset, 12)) return false;
        m_TablesOk = true;
        FindKerning();
        return true;
    }

    bool HasKerning() const { return m_Kerning != nullptr; }

    // Kerning between two character codes, in glyph units (the same units as
    // GlyphWidths::charWidth). 0 when the pair is not in the table.
    int32_t Kerning(uint32_t firstCode, uint32_t secondCode) const {
        if (!m_Kerning || firstCode > 0xFFFF || secondCode > 0xFFFF) return 0;
        const uint16_t first = static_cast<uint16_t>(firstCode);
        const uint16_t second = static_cast<uint16_t>(secondCode);
        const uint16_t count = RdU16(m_Kerning);
        if (count == 0 || 2u + count * 4u > m_KerningLength) return 0;

        // Index table: {u16 firstChar, u16 wordOffset}, sorted by firstChar.
        const uint8_t* index = m_Kerning + 2;
        uint32_t lo = 0, hi = count;
        uint32_t wordOffset = 0;
        bool found = false;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t c = RdU16(index + mid * 4);
            if (c == first) { wordOffset = RdU16(index + mid * 4 + 2); found = true; break; }
            if (c < first) lo = mid + 1; else hi = mid;
        }
        if (!found) return 0;

        const uint32_t byteOffset = wordOffset * 2u;
        if (byteOffset + 2u > m_KerningLength) return 0;
        const uint8_t* table = m_Kerning + byteOffset;
        const uint16_t pairs = RdU16(table);
        if (pairs == 0 || byteOffset + 2u + pairs * 4u > m_KerningLength) return 0;

        // Pair list: {u16 secondChar, s16 amount}, sorted by secondChar.
        lo = 0; hi = pairs;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t c = RdU16(table + 2 + mid * 4);
            if (c == second) {
                return static_cast<int16_t>(RdU16(table + 4 + mid * 4));
            }
            if (c < second) lo = mid + 1; else hi = mid;
        }
        return 0;
    }

    // Character (Unicode code point, BMP) -> glyph index, or the font's own
    // fallback glyph if unmapped.
    uint16_t GlyphIndex(uint32_t codepoint) const {
        if (!m_TablesOk) return kNoGlyph;
        if (codepoint > 0xFFFF) return finf.alterCharIndex;
        const uint16_t code = static_cast<uint16_t>(codepoint);
        uint32_t off = finf.cmapOffset;
        for (int guard = 0; guard < 64 && off != 0; ++guard) {
            if (!InTail(off, 12)) break;
            const uint8_t* c = Tail(off);
            const uint16_t begin = RdU16(c);
            const uint16_t end = RdU16(c + 2);
            const uint16_t method = RdU16(c + 4);
            const uint32_t next = RdU32(c + 8);
            if (code >= begin && code <= end) {
                const uint8_t* d = c + 12;
                if (method == 0) {
                    if (!InTail(off + 12, 2)) break;
                    const uint16_t first = RdU16(d);
                    if (first == kNoGlyph) break;
                    return static_cast<uint16_t>(first + (code - begin));
                } else if (method == 1) {
                    const uint32_t idx = static_cast<uint32_t>(code - begin);
                    if (!InTail(off + 12 + idx * 2, 2)) break;
                    const uint16_t g = RdU16(d + idx * 2);
                    if (g == kNoGlyph) break;
                    return g;
                } else if (method == 2) {
                    if (!InTail(off + 12, 2)) break;
                    const uint16_t count = RdU16(d);
                    if (!InTail(off + 14, static_cast<uint32_t>(count) * 4)) break;
                    // Pairs are sorted by code; a linear scan is fine at these sizes.
                    for (uint32_t i = 0; i < count; ++i) {
                        const uint16_t pc = RdU16(d + 2 + i * 4);
                        if (pc == code) {
                            const uint16_t g = RdU16(d + 4 + i * 4);
                            return g == kNoGlyph ? finf.alterCharIndex : g;
                        }
                        if (pc > code) break;
                    }
                    break;
                }
            }
            off = next;
        }
        return finf.alterCharIndex;
    }

    GlyphWidths Widths(uint16_t glyph) const {
        GlyphWidths w;
        w.left = finf.defaultLeft;
        w.glyphWidth = finf.defaultGlyphWidth;
        w.charWidth = finf.defaultCharWidth;
        if (!m_TablesOk || glyph == kNoGlyph) return w;
        uint32_t off = finf.cwdhOffset;
        for (int guard = 0; guard < 64 && off != 0; ++guard) {
            if (!InTail(off, 8)) break;
            const uint8_t* c = Tail(off);
            const uint16_t start = RdU16(c);
            const uint16_t end = RdU16(c + 2);
            const uint32_t next = RdU32(c + 4);
            if (glyph >= start && glyph <= end) {
                const uint32_t idx = static_cast<uint32_t>(glyph - start);
                if (!InTail(off + 8 + idx * 3, 3)) break;
                const uint8_t* e = c + 8 + idx * 3;
                w.left = static_cast<int8_t>(e[0]);
                w.glyphWidth = e[1];
                w.charWidth = e[2];
                return w;
            }
            off = next;
        }
        return w;
    }

    // Sheet index and the glyph cell's top-left pixel in ORIGINAL (top-down)
    // sheet coordinates.
    bool CellOrigin(uint16_t glyph, uint32_t& sheet, uint32_t& x, uint32_t& y) const {
        if (!m_HeaderOk || glyph == kNoGlyph) return false;
        const uint32_t perSheet = static_cast<uint32_t>(tglp.columns) * tglp.rows;
        sheet = glyph / perSheet;
        if (sheet >= tglp.sheetCount) return false;
        const uint32_t rem = glyph % perSheet;
        const uint32_t col = rem % tglp.columns;
        const uint32_t row = rem / tglp.columns;
        x = col * (static_cast<uint32_t>(tglp.cellWidth) + 1) + 1;
        y = row * (static_cast<uint32_t>(tglp.cellHeight) + 1) + 1;
        return true;
    }

    // Texture coordinates of the glyph's cell (width = glyphWidth, height =
    // cellHeight) on its sheet as the GPU sees it - i.e. with the vertical
    // flip already applied: v0 is the coordinate for the TOP of the glyph.
    bool GlyphUV(uint16_t glyph, uint32_t& sheet, float& u0, float& v0, float& u1, float& v1) const {
        uint32_t x = 0, y = 0;
        if (!CellOrigin(glyph, sheet, x, y)) return false;
        const GlyphWidths w = Widths(glyph);
        const float sw = static_cast<float>(tglp.sheetWidth);
        const float sh = static_cast<float>(tglp.sheetHeight);
        u0 = static_cast<float>(x) / sw;
        u1 = static_cast<float>(x + w.glyphWidth) / sw;
        v0 = (sh - static_cast<float>(y)) / sh;
        v1 = (sh - static_cast<float>(y + tglp.cellHeight)) / sh;
        return true;
    }

private:
    // Nothing in FINF points at KRNG, and walking there means hopping the
    // CWDH/CMAP chains; since the block always sits in the tail this scans
    // the tail for its magic (4-byte aligned) and validates the header.
    void FindKerning() {
        if (!m_Tail || m_TailLength < 16) return;
        for (uint32_t p = 0; p + 8 <= m_TailLength; p += 4) {
            if (m_Tail[p] != 'K' || m_Tail[p + 1] != 'R' || m_Tail[p + 2] != 'N' || m_Tail[p + 3] != 'G') continue;
            const uint32_t size = RdU32(m_Tail + p + 4);
            if (size < 10 || p + size > m_TailLength) return;
            const uint8_t* body = m_Tail + p + 8;
            const uint32_t bodyLen = size - 8;
            const uint16_t count = RdU16(body);
            if (count == 0 || 2u + count * 4u > bodyLen) return;
            m_Kerning = body;
            m_KerningLength = bodyLen;
            return;
        }
    }

    bool InTail(uint32_t fileOffset, uint32_t bytes) const {
        if (!m_Tail || fileOffset < m_TailOffset) return false;
        const uint32_t rel = fileOffset - m_TailOffset;
        return rel + bytes <= m_TailLength;
    }
    const uint8_t* Tail(uint32_t fileOffset) const { return m_Tail + (fileOffset - m_TailOffset); }

    const uint8_t* m_Tail = nullptr;
    uint32_t m_TailOffset = 0;
    uint32_t m_TailLength = 0;
    const uint8_t* m_Kerning = nullptr;     // KRNG block body
    uint32_t m_KerningLength = 0;
    bool m_HeaderOk = false;
    bool m_TablesOk = false;
};

} // namespace WiiXLaunch::BotW::BFFNT
