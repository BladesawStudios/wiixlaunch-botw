#pragma once

// Linear RGBA8 -> the block-linear layout NVN's packaged texture path expects.
//
// WHY THIS EXISTS. botw.gfx:CreateTexture promises raw pixels on both backends,
// because that is what GX2 takes and what a module can produce. NVN does not
// take pixels: it takes a MEMORY POOL, and nvnTextureBuilderSetPackagedTextureData
// says "the storage is already in the device's native layout". That layout is
// Tegra block-linear, not rows.
//
// CreateTextureRaw staged linear pixels and handed them to that path anyway, so
// the first module texture on Switch would have rendered as stripes - if it had
// rendered at all; it never got that far, because the pool size was wrong too.
//
// THE LAYOUT, and how it was established rather than guessed. A GOB is 64 bytes
// wide and 8 rows tall - 512 bytes - and inside one, the byte at (xb, y) lives
// at:
//
//     (xb/32)*256 + ((y%8)/2)*64 + ((xb%32)/16)*32 + (y%2)*16 + (xb%16)
//
// GOBs run down a column of `blockHeight` of them, then across, then the next
// band. None of that is guesswork: include/testpic_texture_bytes.hpp in the
// base repo is a real 256x256 packaged texture that NVN accepts and draws, and
// de-swizzling it with these rules produces the WiiXLaunch logo, pixel for
// pixel, while reading it as rows produces stripes. tools/nvn_swizzle_test
// re-derives the whole buffer from that same file on every build and compares
// byte for byte, so if this drifts the build says so.
//
// BLOCK HEIGHT is not a constant. It is the number of GOBs stacked vertically
// before the layout moves right, capped at 16, and chosen as the smallest power
// of two that covers the image's GOB rows - so a 128-tall texture uses 16 and
// an 8-tall one uses 1. Hard-coding 16 would corrupt every small texture,
// including the 8x8 white pixel the GUI builds at startup.

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch::BotW::NvnSwizzle {

constexpr uint32_t kGobWidthBytes = 64;
constexpr uint32_t kGobHeight     = 8;
constexpr uint32_t kGobBytes      = kGobWidthBytes * kGobHeight;   // 512
constexpr uint32_t kMaxBlockHeight = 16;

// Smallest power of two that covers the image's GOB rows, capped at 16.
inline uint32_t BlockHeightFor(uint32_t heightPixels) {
    const uint32_t gobRows = (heightPixels + kGobHeight - 1u) / kGobHeight;
    uint32_t b = 1;
    while (b < gobRows && b < kMaxBlockHeight) b <<= 1;
    return b;
}

// Bytes the tiled image occupies, which is NOT width*height*bpp in general:
// both axes round up to whole GOBs, and the GOB rows round up to a whole block.
inline uint32_t StorageSize(uint32_t width, uint32_t height, uint32_t bpp,
                            uint32_t blockHeight) {
    const uint32_t gobsAcross = ((width * bpp) + kGobWidthBytes - 1u) / kGobWidthBytes;
    uint32_t gobRows = (height + kGobHeight - 1u) / kGobHeight;
    gobRows = ((gobRows + blockHeight - 1u) / blockHeight) * blockHeight;
    return gobsAcross * gobRows * kGobBytes;
}

// Byte offset of pixel (x, y) within the tiled buffer.
inline uint32_t Offset(uint32_t x, uint32_t y, uint32_t width, uint32_t bpp,
                       uint32_t blockHeight) {
    const uint32_t xb = x * bpp;
    const uint32_t gobsAcross = ((width * bpp) + kGobWidthBytes - 1u) / kGobWidthBytes;
    const uint32_t gx = xb / kGobWidthBytes;
    const uint32_t gy = y / kGobHeight;
    const uint32_t gob = (gy / blockHeight) * (gobsAcross * blockHeight)
                       + gx * blockHeight
                       + (gy % blockHeight);
    const uint32_t xi = xb % kGobWidthBytes;
    const uint32_t yi = y % kGobHeight;
    return gob * kGobBytes
         + (xi / 32u) * 256u
         + (yi / 2u) * 64u
         + ((xi % 32u) / 16u) * 32u
         + (yi % 2u) * 16u
         + (xi % 16u);
}

// `dst` must be StorageSize(...) bytes and is written in full - the padding
// GOBs included, because a texture whose height is not a multiple of the block
// leaves real bytes the GPU may sample.
inline void SwizzleRgba8(uint8_t* dst, const uint8_t* src,
                         uint32_t width, uint32_t height) {
    const uint32_t bpp = 4;
    const uint32_t blockHeight = BlockHeightFor(height);
    const uint32_t total = StorageSize(width, height, bpp, blockHeight);
    for (uint32_t i = 0; i < total; ++i) dst[i] = 0;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t o = Offset(x, y, width, bpp, blockHeight);
            const uint32_t s = (y * width + x) * bpp;
            dst[o + 0] = src[s + 0];
            dst[o + 1] = src[s + 1];
            dst[o + 2] = src[s + 2];
            dst[o + 3] = src[s + 3];
        }
    }
}

// The inverse, for tools and tests. Not used at runtime.
inline void UnswizzleRgba8(uint8_t* dst, const uint8_t* src,
                           uint32_t width, uint32_t height) {
    const uint32_t bpp = 4;
    const uint32_t blockHeight = BlockHeightFor(height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t o = Offset(x, y, width, bpp, blockHeight);
            const uint32_t d = (y * width + x) * bpp;
            dst[d + 0] = src[o + 0];
            dst[d + 1] = src[o + 1];
            dst[d + 2] = src[o + 2];
            dst[d + 3] = src[o + 3];
        }
    }
}

} // namespace WiiXLaunch::BotW::NvnSwizzle
