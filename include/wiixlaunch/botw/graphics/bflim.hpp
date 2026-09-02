#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstdint>
#include <cstddef>

#include "gx2_shader_types.hpp"

// WiiXLaunch::BotW::BFLIM - reads the footer of a Wii U .bflim (the texture
// format inside Layout/*.sblarc "timg/" folders) and maps it onto a GX2
// surface description, so the raw bytes at the start of the file can be
// uploaded untouched via GX2::CreateTextureFromSurface.
//
// A .bflim is <tiled GX2 image data><0x28-byte footer>:
//   footer+0x00 "FLIM" u16 BOM u16 headerSize u32 version u32 fileSize u16 blockCount u16 pad
//   footer+0x14 "imag" u32 blockSize u16 width u16 height u16 alignment
//               u8 format u8 swizzleAndTileMode u32 dataSize
// swizzleAndTileMode: low 5 bits = GX2 tile mode, top 3 bits = the surface
// swizzle (stored in GX2Surface::swizzle as value << 8).
//
// Format byte -> GX2 format, established against the game's own files
// (every "^s" is 16 = BC4 alpha, "^t" 17 = BC5 luminance+alpha, "^d" 1 = A8;
// see docs/gui.md). Single-channel art is routed into the right lanes with a
// component map because the sprite shader multiplies the sampled RGBA by
// the vertex colour rather than doing anything format-specific.

namespace WiiXLaunch::BotW::BFLIM {

constexpr uint32_t kFooterSize = 0x28;

struct Info {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t formatRaw = 0;
    uint32_t tileMode = 0;
    uint32_t swizzle = 0;      // GX2Surface::swizzle value
    uint32_t dataSize = 0;
    uint32_t gx2Format = 0;
    uint32_t compMap = 0;
    bool valid = false;
};

inline bool MapFormat(uint8_t fmt, uint32_t& gx2Format, uint32_t& compMap) {
    switch (fmt) {
    case 0:  gx2Format = GX2Types::kSurfaceFormatUnormR8;       compMap = GX2Types::kCompMapLumOnly;   return true; // L8
    case 1:  gx2Format = GX2Types::kSurfaceFormatUnormR8;       compMap = GX2Types::kCompMapAlphaOnly; return true; // A8
    case 3:  gx2Format = GX2Types::kSurfaceFormatUnormR8G8;     compMap = GX2Types::kCompMapLumAlpha;  return true; // LA8
    case 5:  gx2Format = GX2Types::kSurfaceFormatUnormR5G6B5;   compMap = GX2Types::kCompMapRGBOpaque; return true; // RGB565
    case 9:  gx2Format = GX2Types::kSurfaceFormatUnormR8G8B8A8; compMap = GX2Types::kCompMapRGBA;      return true; // RGBA8
    case 12: gx2Format = GX2Types::kSurfaceFormatUnormBC1;      compMap = GX2Types::kCompMapRGBA;      return true;
    case 13: gx2Format = GX2Types::kSurfaceFormatUnormBC2;      compMap = GX2Types::kCompMapRGBA;      return true;
    case 14: gx2Format = GX2Types::kSurfaceFormatUnormBC3;      compMap = GX2Types::kCompMapRGBA;      return true;
    case 15: gx2Format = GX2Types::kSurfaceFormatUnormBC4;      compMap = GX2Types::kCompMapLumOnly;   return true; // BC4 L
    case 16: gx2Format = GX2Types::kSurfaceFormatUnormBC4;      compMap = GX2Types::kCompMapAlphaOnly; return true; // BC4 A ("^s")
    case 17: gx2Format = GX2Types::kSurfaceFormatUnormBC5;      compMap = GX2Types::kCompMapLumAlpha;  return true; // BC5 ("^t")
    case 20: gx2Format = GX2Types::kSurfaceFormatSrgbR8G8B8A8;  compMap = GX2Types::kCompMapRGBA;      return true;
    case 21: gx2Format = GX2Types::kSurfaceFormatSrgbBC1;       compMap = GX2Types::kCompMapRGBA;      return true;
    case 23: gx2Format = GX2Types::kSurfaceFormatSrgbBC3;       compMap = GX2Types::kCompMapRGBA;      return true;
    default: return false;
    }
}

// `footer` points at the last 0x28 bytes of the file.
inline bool ParseFooter(const uint8_t* footer, Info& out) {
    out = Info{};
    if (!footer) return false;
    if (footer[0] != 'F' || footer[1] != 'L' || footer[2] != 'I' || footer[3] != 'M') return false;
    const uint8_t* imag = footer + 0x14;
    if (imag[0] != 'i' || imag[1] != 'm' || imag[2] != 'a' || imag[3] != 'g') return false;
    out.width = (static_cast<uint32_t>(imag[8]) << 8) | imag[9];
    out.height = (static_cast<uint32_t>(imag[10]) << 8) | imag[11];
    out.formatRaw = imag[14];
    const uint8_t swz = imag[15];
    out.tileMode = swz & 0x1F;
    out.swizzle = static_cast<uint32_t>(swz >> 5) << 8;
    out.dataSize = (static_cast<uint32_t>(imag[16]) << 24) | (static_cast<uint32_t>(imag[17]) << 16) |
                   (static_cast<uint32_t>(imag[18]) << 8) | imag[19];
    if (out.width == 0 || out.height == 0 || out.dataSize == 0) return false;
    if (!MapFormat(out.formatRaw, out.gx2Format, out.compMap)) return false;
    out.valid = true;
    return true;
}

// Whole-file convenience: parses the footer at the end of `file`.
inline bool Parse(const uint8_t* file, size_t fileSize, Info& out) {
    if (!file || fileSize < kFooterSize) return false;
    if (!ParseFooter(file + fileSize - kFooterSize, out)) return false;
    if (out.dataSize + kFooterSize > fileSize) { out.valid = false; return false; }
    return true;
}

} // namespace WiiXLaunch::BotW::BFLIM
