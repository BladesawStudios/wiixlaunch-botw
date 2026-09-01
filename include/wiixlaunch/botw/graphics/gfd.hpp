#pragma once

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

// Minimal GFD (GX2 shader container, "Gfx2"/"BLK{" magic) reader - a
// from-scratch, header-only port of wut's libgfd (vendor/wut/libraries/
// libgfd/src/gfd.c, GFDGetVertexShader/GFDGetPixelShader/
// _GFDGetGenericBlock/_GFDRelocateBlock), NOT a wrapper around it.
//
// Why reimplement instead of just linking libgfd: Cemu has no wut linked at
// all (raw codecave blob, see gx2_imports.hpp) so its GFD parsing has to be
// self-contained C++; real Wii U COULD just call GFDGetVertexShader/
// GFDGetPixelShader directly, but using this same code on both platforms
// keeps shader-loading behavior identical everywhere instead of maintaining
// two different code paths.
//
// Format recipe (confirmed against a real compiled .gsh - see
// scripts/pack_shader_gx2.py's output - AND wut's own struct offset
// assertions in vendor/wut/libraries/libgfd/include/gfd.h /
// vendor/wut/include/gx2/shaders.h, not guessed):
//   - GFDHeader (0x20 bytes, magic "Gfx2") at file offset 0.
//   - A sequence of GFDBlockHeader (0x20 bytes, magic "BLK{") + payload,
//     back to back (next block = this block's start + headerSize + dataSize).
//   - A shader's HEADER block payload is NOT just the raw GX2VertexShader/
//     GX2PixelShader struct - it's that struct followed by all its
//     variable-length arrays (uniformBlocks, uniformVars, attribVars, ...),
//     followed by a GFDRelocationHeader (0x28 bytes) at the very end of the
//     block's data. Copy the WHOLE block (blockHeader->dataSize bytes, not
//     sizeof(ShaderT)) into a buffer big enough to hold the extra array
//     data, then relocate in place.
//   - Relocation: pointer-typed fields in the copied struct/arrays don't
//     hold real pointers - they hold offsets relative to the copied buffer's
//     own start, tagged in the top 12 bits (0xD0600000 = "points at data
//     within this buffer", 0xCA700000 = "points at text/string data within
//     this buffer" - stripped instrument to build real pointers). The
//     relocation header's patch table (patchCount entries, at
//     patchOffset within the block data) lists which field OFFSETS need
//     this treatment; each listed field itself holds a tagged offset that
//     becomes a real pointer by adding the copied buffer's own address.
//   - The PROGRAM block (compiled GPU bytecode) is separate, always
//     immediately after its HEADER block in a real compiler-emitted file -
//     shader->program (a fixed-address struct field, unaffected by the
//     relocation patch table) is set directly to wherever the caller copies
//     that bytecode.

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch::BotW::GFD {

constexpr uint32_t kHeaderMagic          = 0x47667832; // "Gfx2"
constexpr uint32_t kBlockHeaderMagic     = 0x424C4B7B; // "BLK{"
constexpr uint32_t kRelocationMagic      = 0x7D424C4B; // "}KLB" (relocation header)
constexpr uint32_t kBlockVersionMajor    = 1;

constexpr uint32_t kPatchMask = 0xFFF00000;
constexpr uint32_t kPatchData = 0xD0600000;
constexpr uint32_t kPatchText = 0xCA700000;

constexpr uint32_t kBlockTypeEndOfFile           = 1;
constexpr uint32_t kBlockTypeVertexShaderHeader  = 3;
constexpr uint32_t kBlockTypeVertexShaderProgram = 5;
constexpr uint32_t kBlockTypePixelShaderHeader   = 6;
constexpr uint32_t kBlockTypePixelShaderProgram  = 7;

struct Header {
    uint32_t magic;
    uint32_t headerSize;
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint32_t gpuVersion;
    uint32_t align;
    uint32_t unk1;
    uint32_t unk2;
};
static_assert(sizeof(Header) == 0x20);

struct BlockHeader {
    uint32_t magic;
    uint32_t headerSize;
    uint32_t majorVersion;
    uint32_t minorVersion;
    uint32_t type;
    uint32_t dataSize;
    uint32_t id;
    uint32_t index;
};
static_assert(sizeof(BlockHeader) == 0x20);

struct RelocationHeader {
    uint32_t magic;
    uint32_t headerSize;
    uint32_t unk1;
    uint32_t dataSize;
    uint32_t dataOffset;
    uint32_t textSize;
    uint32_t textOffset;
    uint32_t patchBase;
    uint32_t patchCount;
    uint32_t patchOffset;
};
static_assert(sizeof(RelocationHeader) == 0x28);

inline bool RelocateBlock(const BlockHeader* blockHeader, const uint8_t* blockData, uint8_t* dst) {
    if (blockHeader->dataSize < sizeof(RelocationHeader)) return false;
    auto* relocHeader = reinterpret_cast<const RelocationHeader*>(blockData + blockHeader->dataSize - sizeof(RelocationHeader));
    if (relocHeader->magic != kRelocationMagic) return false;
    if ((relocHeader->patchOffset & kPatchMask) != kPatchData) return false;

    auto* patchTable = reinterpret_cast<const uint32_t*>(blockData + (relocHeader->patchOffset & ~kPatchMask));

    for (uint32_t i = 0; i < relocHeader->patchCount; i++) {
        uint32_t offset = patchTable[i];
        if (offset == 0) continue;

        uint32_t tag = offset & kPatchMask;
        if (tag != kPatchData && tag != kPatchText) return false;

        auto* target = reinterpret_cast<uint32_t*>(dst + (offset & ~kPatchMask));
        uint32_t targetTag = *target & kPatchMask;
        if (targetTag != kPatchData && targetTag != kPatchText) return false;

        *target = reinterpret_cast<uintptr_t>(dst + (*target & ~kPatchMask));
    }
    return true;
}

// headerBuffer must be at least as large as the real file's block dataSize
// (not just sizeof(ShaderT) - see the file-level comment above); 2KB covers
// any shader simple enough not to need GX2R buffers. programBuffer must be
// GX2_SHADER_PROGRAM_ALIGNMENT (0x100) aligned, per the real GX2 API's own
// requirement for shader program memory.
template <typename ShaderT>
inline ShaderT* GetShader(const void* file, uint32_t headerBlockType, uint32_t programBlockType,
                           uint8_t* headerBuffer, size_t headerBufferSize,
                           uint8_t* programBuffer, size_t programBufferSize) {
    auto* fileHeader = reinterpret_cast<const Header*>(file);
    if (fileHeader->magic != kHeaderMagic) return nullptr;

    const uint8_t* ptr = static_cast<const uint8_t*>(file) + fileHeader->headerSize;
    bool gotHeader = false;
    bool gotProgram = false;

    while (true) {
        auto* blockHeader = reinterpret_cast<const BlockHeader*>(ptr);
        if (blockHeader->magic != kBlockHeaderMagic || blockHeader->majorVersion != kBlockVersionMajor) break;

        const uint8_t* dataPtr = ptr + blockHeader->headerSize;

        if (blockHeader->type == headerBlockType && !gotHeader) {
            if (blockHeader->dataSize > headerBufferSize) return nullptr;
            __builtin_memcpy(headerBuffer, dataPtr, blockHeader->dataSize);
            if (!RelocateBlock(blockHeader, headerBuffer, headerBuffer)) return nullptr;
            gotHeader = true;
        } else if (blockHeader->type == programBlockType && !gotProgram) {
            if (blockHeader->dataSize > programBufferSize) return nullptr;
            // Fixed-address field write, independent of the header's own
            // relocation patch table (see file-level comment) - relies on
            // the header block always preceding the program block, true for
            // every real compiler-emitted .gsh.
            reinterpret_cast<ShaderT*>(headerBuffer)->program = programBuffer;
            __builtin_memcpy(programBuffer, dataPtr, blockHeader->dataSize);
            gotProgram = true;
        } else if (blockHeader->type == kBlockTypeEndOfFile) {
            break;
        }

        if (gotHeader && gotProgram) return reinterpret_cast<ShaderT*>(headerBuffer);
        ptr = dataPtr + blockHeader->dataSize;
    }

    return (gotHeader && gotProgram) ? reinterpret_cast<ShaderT*>(headerBuffer) : nullptr;
}

}

#endif
