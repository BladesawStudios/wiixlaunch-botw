#pragma once

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

// Real GX2 shader struct layouts, ported byte-for-byte from
// vendor/wut/include/gx2/shaders.h (offsets there are asserted via
// WUT_CHECK_OFFSET against Nintendo's real ABI, not guessed) - needed on
// Cemu since nothing there can #include wut's real headers (no wut linked
// into a raw codecave blob, see gx2_imports.hpp). Real Wii U COULD use
// wut's own gx2/shaders.h directly, but sharing these definitions keeps
// GFD parsing (gfd.hpp) identical on both platforms instead of two
// diverging implementations.
//
// Only the fields GFD parsing/relocation and a minimal draw actually touch
// are given real meaning in comments; every field must still be present
// with the exact right size/offset, since gfd.hpp memcpy's real compiler
// output directly into these structs.

#include <cstdint>

namespace WiiXLaunch::BotW::GX2Types {

struct RBuffer {
    uint32_t flags;
    uint32_t elemSize;
    uint32_t elemCount;
    void* buffer;
};
static_assert(sizeof(RBuffer) == 0x10);

struct UniformBlock {
    const char* name;
    uint32_t offset;
    uint32_t size;
};
static_assert(sizeof(UniformBlock) == 0x0C);

struct UniformVar {
    const char* name;
    uint32_t type;
    uint32_t count;
    uint32_t offset;
    int32_t block;
};
static_assert(sizeof(UniformVar) == 0x14);

struct UniformInitialValue {
    float value[4];
    uint32_t offset;
};
static_assert(sizeof(UniformInitialValue) == 0x14);

struct LoopVar {
    uint32_t offset;
    uint32_t value;
};
static_assert(sizeof(LoopVar) == 0x08);

struct SamplerVar {
    const char* name;
    uint32_t type;
    uint32_t location;
};
static_assert(sizeof(SamplerVar) == 0x0C);

struct AttribVar {
    const char* name;
    uint32_t type;
    uint32_t count;
    uint32_t location;
};
static_assert(sizeof(AttribVar) == 0x10);

struct VertexShader {
    struct {
        uint32_t sq_pgm_resources_vs;
        uint32_t vgt_primitiveid_en;
        uint32_t spi_vs_out_config;
        uint32_t num_spi_vs_out_id;
        uint32_t spi_vs_out_id[10];
        uint32_t pa_cl_vs_out_cntl;
        uint32_t sq_vtx_semantic_clear;
        uint32_t num_sq_vtx_semantic;
        uint32_t sq_vtx_semantic[32];
        uint32_t vgt_strmout_buffer_en;
        uint32_t vgt_vertex_reuse_block_cntl;
        uint32_t vgt_hos_reuse_depth;
    } regs;

    uint32_t size;
    void* program;
    uint32_t mode;

    uint32_t uniformBlockCount;
    UniformBlock* uniformBlocks;

    uint32_t uniformVarCount;
    UniformVar* uniformVars;

    uint32_t initialValueCount;
    UniformInitialValue* initialValues;

    uint32_t loopVarCount;
    LoopVar* loopVars;

    uint32_t samplerVarCount;
    SamplerVar* samplerVars;

    uint32_t attribVarCount;
    AttribVar* attribVars;

    uint32_t ringItemsize;

    uint32_t hasStreamOut;
    uint32_t streamOutStride[4];

    RBuffer gx2rBuffer;
};
static_assert(sizeof(VertexShader) == 0x134);
static_assert(offsetof(VertexShader, size) == 0xD0);
static_assert(offsetof(VertexShader, program) == 0xD4);
static_assert(offsetof(VertexShader, mode) == 0xD8);
static_assert(offsetof(VertexShader, attribVarCount) == 0x104);
static_assert(offsetof(VertexShader, attribVars) == 0x108);

struct PixelShader {
    struct {
        uint32_t sq_pgm_resources_ps;
        uint32_t sq_pgm_exports_ps;
        uint32_t spi_ps_in_control_0;
        uint32_t spi_ps_in_control_1;
        uint32_t num_spi_ps_input_cntl;
        uint32_t spi_ps_input_cntls[32];
        uint32_t cb_shader_mask;
        uint32_t cb_shader_control;
        uint32_t db_shader_control;
        uint32_t spi_input_z;
    } regs;

    uint32_t size;
    void* program;
    uint32_t mode;

    uint32_t uniformBlockCount;
    UniformBlock* uniformBlocks;

    uint32_t uniformVarCount;
    UniformVar* uniformVars;

    uint32_t initialValueCount;
    UniformInitialValue* initialValues;

    uint32_t loopVarCount;
    LoopVar* loopVars;

    uint32_t samplerVarCount;
    SamplerVar* samplerVars;

    RBuffer gx2rBuffer;
};
static_assert(sizeof(PixelShader) == 0xE8);
static_assert(offsetof(PixelShader, size) == 0xA4);
static_assert(offsetof(PixelShader, program) == 0xA8);

struct FetchShader {
    uint32_t type;
    struct {
        uint32_t sq_pgm_resources_fs;
    } regs;
    uint32_t size;
    void* program;
    uint32_t attribCount;
    uint32_t numDivisors;
    uint32_t divisors[2];
};
static_assert(sizeof(FetchShader) == 0x20);

struct AttribStream {
    uint32_t location;
    uint32_t buffer;
    uint32_t offset;
    uint32_t format;
    uint32_t type;
    uint32_t aluDivisor;
    uint32_t mask;
    uint32_t endianSwap;
};
static_assert(sizeof(AttribStream) == 0x20);

// Ported from vendor/wut/include/gx2/surface.h (WUT_CHECK_OFFSET-asserted
// real layout). Used to build our OWN independent render target - see
// gx2.hpp's file-level comment on why: BotW's own live color buffer isn't
// reliably reachable (every static-analysis path either dead-ended in
// vtable-indirect calls with no found xrefs, or only fired conditionally/
// once, not every frame), and GX2's bound color buffer does NOT survive
// the GX2Flush() BotW performs right before our per-frame hook runs
// (confirmed via Cemu's own GX2 API log - BotW re-asserts
// GX2SetColorBuffer itself immediately after every such flush).
struct Surface {
    uint32_t dim;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mipLevels;
    uint32_t format;
    uint32_t aa;
    uint32_t use;
    uint32_t imageSize;
    void* image;
    uint32_t mipmapSize;
    void* mipmaps;
    uint32_t tileMode;
    uint32_t swizzle;
    uint32_t alignment;
    uint32_t pitch;
    uint32_t mipLevelOffset[13];
};
static_assert(sizeof(Surface) == 0x74);

struct ColorBuffer {
    Surface surface;
    uint32_t viewMip;
    uint32_t viewFirstSlice;
    uint32_t viewNumSlices;
    void* aaBuffer;
    uint32_t aaSize;
    uint32_t regs[5];
};
static_assert(sizeof(ColorBuffer) == 0x9C);
static_assert(offsetof(ColorBuffer, regs) == 0x88);

struct Texture {
    Surface surface;
    uint32_t viewFirstMip;
    uint32_t viewNumMips;
    uint32_t viewFirstSlice;
    uint32_t viewNumSlices;
    uint32_t compMap;
    uint32_t regs[5];
};
static_assert(sizeof(Texture) == 0x9C);

struct Sampler {
    uint32_t regs[3];
};
static_assert(sizeof(Sampler) == 0x0C);

struct DepthBuffer {
    Surface surface;
    uint32_t viewMip;
    uint32_t viewFirstSlice;
    uint32_t viewNumSlices;
    void* hiZPtr;
    uint32_t hiZSize;
    float depthClear;
    uint32_t stencilClear;
    uint32_t regs[7];
};
static_assert(sizeof(DepthBuffer) == 0xAC);

// Real GX2 constants this project actually uses (values confirmed against
// vendor/wut/include/gx2/enum.h, not guessed).
constexpr uint32_t kShaderProgramAlignment = 0x100;
constexpr uint32_t kAttribFormatFloat32x4 = 0x813; // FLAG_SCALED(0x800) | TYPE_32_32_32_32_FLOAT(0x13)
constexpr uint32_t kAttribFormatFloat32x2 = 0x80f; // FLAG_SCALED(0x800) | TYPE_32_32_FLOAT(0x0f)
constexpr uint32_t kAttribIndexPerVertex = 0;
constexpr uint32_t kEndianSwapDefault = 3;
constexpr uint32_t kFetchShaderTessellationNone = 0;
constexpr uint32_t kTessellationModeDiscrete = 0;
constexpr uint32_t kShaderModeUniformRegister = 0;
constexpr uint32_t kPrimitiveModeTriangles = 4;
constexpr uint32_t kPrimitiveModeTriangleStrip = 6;
constexpr uint32_t kInvalidateModeCpuAttributeBuffer = (1 << 6) | (1 << 0);
constexpr uint32_t kInvalidateModeCpuTexture = (1 << 6) | (1 << 2);
constexpr uint32_t kInvalidateModeCpuShader = (1 << 6) | (1 << 3);
constexpr uint32_t kCompareFuncLessEqual = 3;
constexpr uint32_t kCompareFuncAlways = 7;
constexpr uint32_t kFrontFaceCcw = 0;
constexpr uint32_t kSurfaceDimTexture2D = 1;
constexpr uint32_t kSurfaceFormatUnormR8G8B8A8 = 0x01a;
constexpr uint32_t kSurfaceFormatFloatD32 = 0x11;
constexpr uint32_t kAaMode1x = 0;
constexpr uint32_t kSurfaceUseTexture = 1;
constexpr uint32_t kSurfaceUseDepthBuffer = 4;
constexpr uint32_t kTileModeLinearAligned = 1;
constexpr uint32_t kTileModeTiled1DThin1 = 2;
constexpr uint32_t kTileModeTiled2DThin1 = 4;
constexpr uint32_t kScanTargetTv = 1;
constexpr uint32_t kLogicOpCopy = 0xCC;
constexpr uint32_t kTexClampModeClamp = 0;
constexpr uint32_t kTexXYFilterModeLinear = 1;
constexpr uint32_t kClearFlagsDepth = 1;

}

#endif
