#pragma once

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "gui_types.hpp"
#include "gui_render.hpp"
#include "gui_backend.hpp"
#include "../graphics/bflim.hpp"
#include "../graphics/bffnt.hpp"
#include "../platform/fs.hpp"
#include "../platform/yaz0.hpp"
#include "../platform/sarc.hpp"
#include "../platform/log.hpp"

// GUI asset loader: borrows the game's own fonts and UI art at runtime.
//
// Nothing is shipped with the mod. On first use the loader opens the game's
// own files through the same content mount the game reads them from -
//
//   Font/Font_US.sbfarc      (Yaz0 SARC)  -> Normal_00.bffnt, NormalS_00.bffnt
//   Pack/Bootup.pack         (SARC)       -> Layout/Common.sblarc (Yaz0 SARC)
//                                          -> timg/*.bflim  (see gui_render.hpp's table)
//
// - and streams the pieces it wants straight into GPU memory. The layout
// archive is 31 MB decompressed and the whole Cemu payload heap is 6 MB, so
// the archives are never held in memory: a 4 KB-window Yaz0 decoder
// (platform/yaz0.hpp) runs over 64 KB compressed chunks read at file
// offsets (FS::File::ReadAt) and a sink copies out only the byte ranges the
// SARC node table says belong to the wanted files. Font glyph sheets are
// written directly into their GX2 surfaces; each small .bflim is staged in
// one 32 KB buffer and uploaded the moment its last byte lands.
//
// Loading is incremental: LoadStep() consumes a few chunks per call and is
// driven from the draw callback, so the ~6 MB of compressed data is walked
// over a couple of dozen frames with no hitch. LoadAll() does it in one go.
//
// Memory kept afterwards: ~1.5 MB of glyph sheets, ~250 KB of UI art, ~20 KB
// of font tables, plus 160 KB of loader scratch that this module (like the
// rest of GX2 here) never frees.

namespace WiiXLaunch::BotW::GUI::impl {

constexpr uint32_t kChunkBytes = 64 * 1024;
constexpr uint32_t kHeadBytes = 64 * 1024;          // SARC header + node table capture
// Ceiling on a single .bflim, not a budget: the staging buffer is allocated
// once, sized to the largest file the sprite table actually asks for. It used
// to be a flat 32 KB, which skipped 179 of the layout archive's 917 textures -
// everything from the dialogue window base (393 KB) upward. Nothing in the
// table is near this, so the cost is unchanged until a bigger sprite is added.
constexpr uint32_t kSpriteStagingLimit = 1024 * 1024;
constexpr uint32_t kFontHeaderBytes = 0x2000;       // the game's fonts start sheet data at 0x2000
constexpr uint32_t kDefaultChunksPerStep = 4;

enum class LoadPhase : uint8_t {
    NotStarted = 0,
    FontOpen,
    FontStream,
    LayoutOpen,
    LayoutStream,
    Finished,
};

struct RangeTarget {
    uint32_t offset = 0;    // absolute offset within the decompressed archive
    uint32_t size = 0;
    uint32_t written = 0;
    bool wanted = false;
    bool done = false;
};

struct FontLoad {
    RangeTarget range;
    uint8_t* header = nullptr;      // first kFontHeaderBytes of the .bffnt
    bool parsed = false;
    bool failed = false;
    void* sheetImage[BFFNT::kMaxSheets] = {};
    uint32_t sheetImageSize = 0;
    uint8_t* tail = nullptr;        // CWDH/CMAP region
    uint32_t tailStart = 0;
    uint32_t tailLength = 0;
};

struct LoaderState {
    LoadPhase phase = LoadPhase::NotStarted;
    bool failed = false;
    uint32_t chunksPerStep = kDefaultChunksPerStep;

    // Path overrides (nullptr = defaults).
    const char* fontArchivePath = nullptr;
    const char* layoutArchivePath = nullptr;

    FS::File file;
    uint32_t streamPos = 0;         // next compressed byte to read (absolute file offset)
    uint32_t streamEnd = 0;
    Yaz0::StreamDecoder decoder;

    uint8_t* chunk = nullptr;       // kChunkBytes, 64-aligned
    uint8_t* head = nullptr;        // kHeadBytes
    uint32_t headFilled = 0;
    bool rangesResolved = false;
    SARC::Header sarc;

    FontLoad fonts[static_cast<size_t>(FontId::Count)];

    RangeTarget spriteRanges[static_cast<size_t>(Sprite::Count)];
    uint8_t* staging = nullptr;     // sized to the largest wanted sprite
    uint32_t stagingBytes = 0;
    int32_t stagingSprite = -1;
    uint32_t spritesWanted = 0;
    uint32_t spritesLoaded = 0;
    uint32_t fontsLoaded = 0;
    uint32_t bytesDecompressed = 0;
};

inline LoaderState g_Loader;

inline bool LoaderFinished() { return g_Loader.phase == LoadPhase::Finished; }

// ---------------------------------------------------------------------------
// Sink helpers
// ---------------------------------------------------------------------------

inline void CaptureHead(uint32_t offset, const uint8_t* data, uint32_t length) {
    if (g_Loader.headFilled >= kHeadBytes) return;
    const uint32_t copied = Yaz0::CopyOverlap(0, kHeadBytes, g_Loader.head, offset, data, length);
    if (copied) {
        const uint32_t end = offset + length;
        g_Loader.headFilled = end < kHeadBytes ? end : kHeadBytes;
    }
}

// Once enough of the archive has gone by to hold its node table, look every
// wanted file up. Returns false while more bytes are needed.
inline bool TryResolveRanges() {
    LoaderState& L = g_Loader;
    if (L.rangesResolved) return true;
    if (L.headFilled < SARC::kHeaderSize + SARC::kSfatHeaderSize) return false;
    if (!L.sarc.valid) {
        if (!SARC::ParseHeader(L.head, L.headFilled, L.sarc)) {
            OSLog("WiiXLaunch GUI: archive is not a big-endian SARC - giving up on this phase\n");
            L.rangesResolved = true;   // nothing will match; stream runs out harmlessly
            return true;
        }
    }
    const uint32_t needed = L.sarc.NodeTableEnd();
    if (needed > kHeadBytes) {
        OSLog("WiiXLaunch GUI: SARC node table (%u bytes) exceeds capture buffer\n", needed);
        L.rangesResolved = true;
        return true;
    }
    if (L.headFilled < needed) return false;

    if (L.phase == LoadPhase::FontStream) {
        for (size_t i = 0; i < static_cast<size_t>(FontId::Count); ++i) {
            if (!g_Fonts[i].load) continue;
            FontLoad& f = L.fonts[i];
            uint32_t off = 0, size = 0;
            if (SARC::Find(L.head, L.headFilled, L.sarc, g_Fonts[i].archivePath, off, size) && size > kFontHeaderBytes) {
                f.range.offset = off;
                f.range.size = size;
                f.range.wanted = true;
            } else {
                OSLog("WiiXLaunch GUI: font '%s' not in archive\n", g_Fonts[i].archivePath);
            }
        }
    } else {
        for (size_t i = 0; i < static_cast<size_t>(Sprite::Count); ++i) {
            const char* path = g_Sprites[i].archivePath;
            if (!path || !path[0]) continue;
            uint32_t off = 0, size = 0;
            if (SARC::Find(L.head, L.headFilled, L.sarc, path, off, size)) {
                if (size > kSpriteStagingLimit) {
                    OSLog("WiiXLaunch GUI: '%s' is %u bytes, over the %u single-file limit - skipped\n", path, size, kSpriteStagingLimit);
                    continue;
                }
                if (size > L.stagingBytes) L.stagingBytes = size;
                L.spriteRanges[i].offset = off;
                L.spriteRanges[i].size = size;
                L.spriteRanges[i].wanted = true;
                L.spritesWanted++;
            } else {
                OSLog("WiiXLaunch GUI: '%s' not in archive\n", path);
            }
        }
    }
    // Now that every wanted file's size is known, the staging buffer can be
    // exactly big enough. Deliberately after the loop: before it, nothing knows
    // how large the biggest sprite is.
    if (L.phase != LoadPhase::FontStream && L.stagingBytes > 0 && !L.staging) {
        L.staging = reinterpret_cast<uint8_t*>(Backend::AllocMEM1(L.stagingBytes, 64));
        if (!L.staging) {
            OSLog("WiiXLaunch GUI: sprite staging buffer of %u bytes would not allocate\n", L.stagingBytes);
        }
    }
    L.rangesResolved = true;
    return true;
}

inline void FinishSprite(size_t index) {
    LoaderState& L = g_Loader;
    RangeTarget& r = L.spriteRanges[index];
    SpriteInfo& sprite = g_Sprites[index];
    r.done = true;
    L.stagingSprite = -1;

    BFLIM::Info info;
    if (!BFLIM::Parse(L.staging, r.size, info)) {
        OSLog("WiiXLaunch GUI: '%s': bad BFLIM footer (format %u)\n", sprite.archivePath, info.formatRaw);
        return;
    }
    Backend::SurfaceDesc desc;
    desc.width = info.width;
    desc.height = info.height;
    desc.format = info.gx2Format;
    desc.tileMode = info.tileMode;
    desc.swizzle = info.swizzle;
    desc.compMap = sprite.compMap ? sprite.compMap : info.compMap;
    desc.linearFilter = true;
    sprite.texture = Backend::CreateTextureFromSurface(desc, L.staging, info.dataSize);
    if (sprite.texture) {
        sprite.width = info.width;
        sprite.height = info.height;
        L.spritesLoaded++;
    } else {
        OSLog("WiiXLaunch GUI: '%s': texture upload failed\n", sprite.archivePath);
    }
}

inline void SinkSprites(uint32_t offset, const uint8_t* data, uint32_t length) {
    LoaderState& L = g_Loader;
    const uint32_t runEnd = offset + length;
    for (size_t i = 0; i < static_cast<size_t>(Sprite::Count); ++i) {
        RangeTarget& r = L.spriteRanges[i];
        if (!r.wanted || r.done) continue;
        if (runEnd <= r.offset || offset >= r.offset + r.size) continue;
        if (!L.staging) continue;
        if (L.stagingSprite != static_cast<int32_t>(i)) {
            if (L.stagingSprite >= 0) {
                // Two files interleaved in one run cannot happen (ranges are
                // disjoint and delivered in order), but never trust it.
                OSLog("WiiXLaunch GUI: staging clash on '%s'\n", g_Sprites[i].archivePath);
                L.spriteRanges[L.stagingSprite].done = true;
            }
            L.stagingSprite = static_cast<int32_t>(i);
        }
        r.written += Yaz0::CopyOverlap(r.offset, r.size, L.staging, offset, data, length);
        if (r.written >= r.size) FinishSprite(i);
    }
}

inline void ParseFontHeader(size_t index) {
    LoaderState& L = g_Loader;
    FontLoad& f = L.fonts[index];
    BFFNT::Font& font = g_Fonts[index].font;
    f.parsed = true;
    if (!font.ParseHeader(f.header, kFontHeaderBytes)) {
        OSLog("WiiXLaunch GUI: font '%s': header parse failed\n", g_Fonts[index].archivePath);
        f.failed = true;
        return;
    }
    if (font.tglp.sheetDataOffset > kFontHeaderBytes || font.SheetsEnd() > f.range.size) {
        OSLog("WiiXLaunch GUI: font '%s': unexpected layout (sheets at 0x%x)\n", g_Fonts[index].archivePath, font.tglp.sheetDataOffset);
        f.failed = true;
        return;
    }
    uint32_t gx2Format = 0, compMap = 0;
    if (!BFFNT::MapSheetFormat(font.tglp.sheetFormat, gx2Format, compMap)) {
        OSLog("WiiXLaunch GUI: font '%s': sheet format %u unsupported\n", g_Fonts[index].archivePath, font.tglp.sheetFormat);
        f.failed = true;
        return;
    }
    for (uint32_t s = 0; s < font.tglp.sheetCount; ++s) {
        Backend::SurfaceDesc desc;
        desc.width = font.tglp.sheetWidth;
        desc.height = font.tglp.sheetHeight;
        desc.format = gx2Format;
        desc.tileMode = Backend::kTileModeTiled2DThin1;
        desc.swizzle = 0;
        desc.compMap = compMap;
        desc.linearFilter = true;
        void* image = nullptr;
        uint32_t imageSize = 0;
        font.sheetTextures[s] = Backend::AllocTextureSurface(desc, &image, &imageSize);
        if (!font.sheetTextures[s] || imageSize != font.tglp.sheetSize) {
            OSLog("WiiXLaunch GUI: font '%s': sheet %u surface %u bytes vs file %u - unsupported tiling?\n",
                  g_Fonts[index].archivePath, s, imageSize, font.tglp.sheetSize);
            f.failed = true;
            return;
        }
        f.sheetImage[s] = image;
        f.sheetImageSize = imageSize;
    }
    f.tailStart = font.TailStart();
    if (f.tailStart >= f.range.size) {
        OSLog("WiiXLaunch GUI: font '%s': no CWDH/CMAP tail\n", g_Fonts[index].archivePath);
        f.failed = true;
        return;
    }
    f.tailLength = f.range.size - f.tailStart;
    f.tail = reinterpret_cast<uint8_t*>(Backend::AllocMEM1(f.tailLength, 64));
    if (!f.tail) {
        OSLog("WiiXLaunch GUI: font '%s': tail allocation of %u bytes failed\n",
              g_Fonts[index].archivePath, f.tailLength);
        f.failed = true;
        return;
    }
}

inline void FinishFont(size_t index) {
    LoaderState& L = g_Loader;
    FontLoad& f = L.fonts[index];
    BFFNT::Font& font = g_Fonts[index].font;
    f.range.done = true;
    if (f.failed || !f.parsed) return;
    for (uint32_t s = 0; s < font.tglp.sheetCount && s < BFFNT::kMaxSheets; ++s) {
        if (font.sheetTextures[s]) Backend::FinalizeTexture(font.sheetTextures[s]);
    }
    if (!font.SetTables(f.tail, f.tailStart, f.tailLength)) {
        OSLog("WiiXLaunch GUI: font '%s': CWDH/CMAP tables rejected\n", g_Fonts[index].archivePath);
        f.failed = true;
        return;
    }
    L.fontsLoaded++;
    OSLog("WiiXLaunch GUI: font '%s' ready: %ux%u cells, %u sheet(s) %ux%u fmt %u\n",
          g_Fonts[index].archivePath, font.tglp.cellWidth, font.tglp.cellHeight,
          font.tglp.sheetCount, font.tglp.sheetWidth, font.tglp.sheetHeight, font.tglp.sheetFormat);
}

inline void SinkFonts(uint32_t offset, const uint8_t* data, uint32_t length) {
    LoaderState& L = g_Loader;
    const uint32_t runEnd = offset + length;
    for (size_t i = 0; i < static_cast<size_t>(FontId::Count); ++i) {
        FontLoad& f = L.fonts[i];
        if (!f.range.wanted || f.range.done) continue;
        if (runEnd <= f.range.offset || offset >= f.range.offset + f.range.size) continue;

        const uint32_t base = f.range.offset;
        // Header region (always captured; the parse needs the first ~0x60 bytes).
        f.range.written += Yaz0::CopyOverlap(base, kFontHeaderBytes, f.header, offset, data, length);
        if (!f.parsed && runEnd >= base + 0x60) ParseFontHeader(i);

        if (f.parsed && !f.failed) {
            const BFFNT::Font& font = g_Fonts[i].font;
            for (uint32_t s = 0; s < font.tglp.sheetCount && s < BFFNT::kMaxSheets; ++s) {
                if (!f.sheetImage[s]) continue;
                f.range.written += Yaz0::CopyOverlap(base + font.tglp.sheetDataOffset + s * font.tglp.sheetSize,
                                                     font.tglp.sheetSize,
                                                     reinterpret_cast<uint8_t*>(f.sheetImage[s]),
                                                     offset, data, length);
            }
            f.range.written += Yaz0::CopyOverlap(base + f.tailStart, f.tailLength, f.tail, offset, data, length);
        }
        if (runEnd >= base + f.range.size) FinishFont(i);
    }
}

inline void LoaderSink(void*, uint32_t offset, const uint8_t* data, uint32_t length) {
    LoaderState& L = g_Loader;
    L.bytesDecompressed += length;
    if (!L.rangesResolved) {
        CaptureHead(offset, data, length);
        if (!TryResolveRanges()) return;
    }
    if (L.phase == LoadPhase::FontStream) SinkFonts(offset, data, length);
    else SinkSprites(offset, data, length);
}

// ---------------------------------------------------------------------------
// Phases
// ---------------------------------------------------------------------------

inline bool LoaderAllocScratch() {
    LoaderState& L = g_Loader;
    if (!L.chunk) L.chunk = reinterpret_cast<uint8_t*>(Backend::AllocMEM1(kChunkBytes, 64));
    if (!L.head) L.head = reinterpret_cast<uint8_t*>(Backend::AllocMEM1(kHeadBytes, 64));
    for (size_t i = 0; i < static_cast<size_t>(FontId::Count); ++i) {
        if (!L.fonts[i].header) L.fonts[i].header = reinterpret_cast<uint8_t*>(Backend::AllocMEM1(kFontHeaderBytes, 64));
        if (!L.fonts[i].header) return false;
    }
    return L.chunk && L.head;      // staging comes later, once its size is known
}

inline void BeginStream(uint32_t start, uint32_t end) {
    LoaderState& L = g_Loader;
    L.streamPos = start;
    L.streamEnd = end;
    L.headFilled = 0;
    L.rangesResolved = false;
    L.sarc = SARC::Header{};
    L.decoder.Reset(&LoaderSink, nullptr);
}

inline void PhaseFontOpen() {
    LoaderState& L = g_Loader;
    const char* candidates[4] = { L.fontArchivePath, "Font/Font_US.sbfarc", "Font/Font_EU.sbfarc", "Font/Font_JP.sbfarc" };
    for (int i = 0; i < 4; ++i) {
        if (!candidates[i]) continue;
        if (L.file.Open(candidates[i]) && L.file.Size() > Yaz0::kHeaderSize) {
            OSLog("WiiXLaunch GUI: fonts from '%s' (%u bytes)\n", candidates[i], L.file.Size());
            BeginStream(0, L.file.Size());
            L.phase = LoadPhase::FontStream;
            return;
        }
        L.file.Close();
    }
    OSLog("WiiXLaunch GUI: no font archive found - text will not render\n");
    L.phase = LoadPhase::LayoutOpen;
}

inline void PhaseLayoutOpen() {
    LoaderState& L = g_Loader;
    // A loose Layout/Common.sblarc (a modded content folder) wins over the pack.
    const char* loose = L.layoutArchivePath ? L.layoutArchivePath : "Layout/Common.sblarc";
    if (L.file.Open(loose) && L.file.Size() > Yaz0::kHeaderSize) {
        OSLog("WiiXLaunch GUI: UI art from '%s' (%u bytes)\n", loose, L.file.Size());
        BeginStream(0, L.file.Size());
        L.phase = LoadPhase::LayoutStream;
        return;
    }
    L.file.Close();

    if (L.file.Open("Pack/Bootup.pack") && L.file.Size() > SARC::kHeaderSize) {
        const uint32_t got = L.file.ReadAt(0, L.head, kHeadBytes);
        SARC::Header pack;
        uint32_t off = 0, size = 0;
        if (SARC::ParseHeader(L.head, got, pack) && got >= pack.NodeTableEnd() &&
            SARC::Find(L.head, got, pack, "Layout/Common.sblarc", off, size) && size > Yaz0::kHeaderSize) {
            OSLog("WiiXLaunch GUI: UI art from Pack/Bootup.pack -> Layout/Common.sblarc (@%u, %u bytes)\n", off, size);
            BeginStream(off, off + size);
            L.phase = LoadPhase::LayoutStream;
            return;
        }
        OSLog("WiiXLaunch GUI: Layout/Common.sblarc not found inside Pack/Bootup.pack\n");
        L.file.Close();
    }
    OSLog("WiiXLaunch GUI: no layout archive found - UI art will not render\n");
    L.phase = LoadPhase::Finished;
}

inline void FinishStream() {
    LoaderState& L = g_Loader;
    L.decoder.Flush();
    if (L.phase == LoadPhase::FontStream) {
        for (size_t i = 0; i < static_cast<size_t>(FontId::Count); ++i) {
            if (L.fonts[i].range.wanted && !L.fonts[i].range.done) {
                OSLog("WiiXLaunch GUI: font '%s' incomplete (%u of %u bytes)\n",
                      g_Fonts[i].archivePath, L.fonts[i].range.written, L.fonts[i].range.size);
            }
        }
        L.file.Close();
        OSLog("WiiXLaunch GUI: font phase done (%u loaded), opening the layout archive\n", L.fontsLoaded);
        L.phase = LoadPhase::LayoutOpen;
    } else {
        L.file.Close();
        L.phase = LoadPhase::Finished;
        OSLog("WiiXLaunch GUI: assets ready - %u font(s), %u/%u sprites, %u bytes decompressed\n",
              L.fontsLoaded, L.spritesLoaded, L.spritesWanted, L.bytesDecompressed);
    }
}

// Pumps one compressed chunk through the decoder. Returns false when the
// current stream is exhausted.
inline bool PumpChunk() {
    LoaderState& L = g_Loader;
    if (L.streamPos >= L.streamEnd || L.decoder.IsDone() || L.decoder.HasError()) return false;
    uint32_t want = L.streamEnd - L.streamPos;
    if (want > kChunkBytes) want = kChunkBytes;
    const uint32_t got = L.file.ReadAt(L.streamPos, L.chunk, want);
    if (got == 0) return false;
    L.streamPos += got;
    if (!L.decoder.Feed(L.chunk, got)) {
        OSLog("WiiXLaunch GUI: Yaz0 stream error at %u\n", L.streamPos);
        return false;
    }
    return true;
}

// One increment of loading. Safe to call every frame until LoaderFinished().
inline void LoaderStep() {
    LoaderState& L = g_Loader;
    switch (L.phase) {
    case LoadPhase::NotStarted:
        if (!LoaderAllocScratch()) {
            OSLog("WiiXLaunch GUI: loader scratch allocation failed\n");
            L.failed = true;
            L.phase = LoadPhase::Finished;
            return;
        }
        L.phase = LoadPhase::FontOpen;
        [[fallthrough]];
    case LoadPhase::FontOpen:
        PhaseFontOpen();
        return;
    case LoadPhase::FontStream:
    case LoadPhase::LayoutStream:
        for (uint32_t i = 0; i < L.chunksPerStep; ++i) {
            if (!PumpChunk()) { FinishStream(); return; }
        }
        return;
    case LoadPhase::LayoutOpen:
        PhaseLayoutOpen();
        return;
    case LoadPhase::Finished:
        return;
    }
}

inline void LoaderRunToCompletion() {
    for (uint32_t guard = 0; guard < 100000 && !LoaderFinished(); ++guard) LoaderStep();
}

} // namespace WiiXLaunch::BotW::GUI::impl

#endif
