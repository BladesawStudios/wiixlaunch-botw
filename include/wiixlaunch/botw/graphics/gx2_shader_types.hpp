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
// 0x0D, NOT 0x0f - see vendor/wut/include/gx2/enum.h GX2_ATTRIB_TYPE_32_32_FLOAT.
// This was 0x80f for a long time, which is why every float4 attribute (position,
// tint, and the whole float4-only mesh pipeline) worked while UVs silently
// decoded wrong: v stayed pinned at 0, so a textured quad sampled one texture
// row smeared vertically instead of the image.
constexpr uint32_t kAttribFormatFloat32x2 = 0x80d; // FLAG_SCALED(0x800) | TYPE_32_32_FLOAT(0x0d)
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
// 0x400 (SRGB flag) | 0x01a. PNG/UI artwork is authored sRGB-encoded, and
// BotW's color buffer is an sRGB target, so a texture declared plain UNORM
// gets sampled as though its bytes were linear and then re-encoded to sRGB on
// write - a double encode that shows up as a uniformly brighter/washed-out
// image. Declaring the texture sRGB makes the sampler decode on read so the
// round trip is neutral.
constexpr uint32_t kSurfaceFormatSrgbR8G8B8A8 = 0x41a;
constexpr uint32_t kSurfaceFormatFloatD32 = 0x11;
// Formats the game's own UI assets come in (BFLIM footers / BFFNT sheets) -
// see graphics/bflim.hpp and graphics/bffnt.hpp for which byte maps where.
constexpr uint32_t kSurfaceFormatUnormR8       = 0x001;  // L8 / A8 (one channel, routed via compMap)
constexpr uint32_t kSurfaceFormatUnormR8G8     = 0x007;  // LA8
constexpr uint32_t kSurfaceFormatUnormR5G6B5   = 0x008;
constexpr uint32_t kSurfaceFormatUnormBC1      = 0x031;
constexpr uint32_t kSurfaceFormatUnormBC2      = 0x032;
constexpr uint32_t kSurfaceFormatUnormBC3      = 0x033;
constexpr uint32_t kSurfaceFormatUnormBC4      = 0x034;
constexpr uint32_t kSurfaceFormatUnormBC5      = 0x035;
constexpr uint32_t kSurfaceFormatSrgbBC1       = 0x431;
constexpr uint32_t kSurfaceFormatSrgbBC3       = 0x433;
// Texture component map: (x << 24) | (y << 16) | (z << 8) | w, each a
// selector 0=R 1=G 2=B 3=A 4=zero 5=one. The sprite pixel shader multiplies
// the sampled RGBA by the vertex colour, so single-channel art is routed
// into the alpha (or all three colour) lanes here rather than in a shader.
constexpr uint32_t kCompMapRGBA        = 0x00010203;
constexpr uint32_t kCompMapAlphaOnly   = 0x05050500; // RGB = 1, A = R  (A8, BC4 alpha)
constexpr uint32_t kCompMapLumOnly     = 0x00000005; // RGB = R, A = 1  (L8, BC4 luminance)
constexpr uint32_t kCompMapLumAlpha    = 0x00000001; // RGB = R, A = G  (LA8, BC5 "^t")
constexpr uint32_t kCompMapRGBOpaque   = 0x00010205; // RGB, A = 1      (RGB565)
// RGB = 1, A = G. For a BC5 ("^t") sprite whose red channel is NOT a white
// mask but a gradient the game feeds to a TEV stage as the ratio between two
// material colours (SelectFrame_04, Nt_CursorCircle). Routing that gradient
// into RGB paints the shape black-to-white instead of tinting it, so those
// sprites take this map and let the vertex colour supply the colour.
constexpr uint32_t kCompMapShapeFromG  = 0x05050501;

// GX2BlendMode - the src/dst factors GX2SetBlendControl takes (wut gx2/enum.h).
constexpr uint32_t kBlendZero            = 0;
constexpr uint32_t kBlendOne             = 1;
constexpr uint32_t kBlendSrcColor        = 2;
constexpr uint32_t kBlendInvSrcColor     = 3;
constexpr uint32_t kBlendSrcAlpha        = 4;
constexpr uint32_t kBlendInvSrcAlpha     = 5;
constexpr uint32_t kBlendDstAlpha        = 6;
constexpr uint32_t kBlendInvDstAlpha     = 7;
constexpr uint32_t kBlendDstColor        = 8;
constexpr uint32_t kBlendInvDstColor     = 9;
constexpr uint32_t kBlendSrcAlphaSat     = 10;

// GX2BlendCombineMode - how the two weighted terms are combined.
constexpr uint32_t kBlendCombineAdd      = 0;
constexpr uint32_t kBlendCombineSub      = 1;  // src - dst
constexpr uint32_t kBlendCombineMin      = 2;
constexpr uint32_t kBlendCombineMax      = 3;
constexpr uint32_t kBlendCombineRevSub   = 4;  // dst - src

// The scan buffer BotW renders into is UNORM_R10_G10_B10_A2 (seen live:
// "dstFormat=0x19"), so its alpha channel is only 2 bits and is never
// displayed. Blend factors that read DESTINATION alpha are therefore close
// to useless on it - prefer the source-alpha factors below.
constexpr uint32_t kSurfaceFormatUnormR10G10B10A2 = 0x019;
constexpr uint32_t kAaMode1x = 0;
constexpr uint32_t kSurfaceUseTexture = 1;
constexpr uint32_t kSurfaceUseDepthBuffer = 4;
constexpr uint32_t kTileModeLinearAligned = 1;
constexpr uint32_t kTileModeTiled1DThin1 = 2;
constexpr uint32_t kTileModeTiled2DThin1 = 4;
constexpr uint32_t kScanTargetTv = 1;
constexpr uint32_t kLogicOpCopy = 0xCC;
// GX2TexClampMode. CLAMP is 2, NOT 0 - 0 is WRAP (checked against
// vendor/wut/include/gx2/enum.h). This was 0 for a long time, so every
// texture sampled with wrap addressing: at a quad's edge the sampler reached
// past the last texel and came back around to the FIRST one, which for the
// game's corner art is its transparent padding. The result was a pale seam
// along every corner-to-edge join in every frame - visible as an outline
// around each element once the UI was run at 1440p.
constexpr uint32_t kTexClampModeWrap = 0;
constexpr uint32_t kTexClampModeMirror = 1;
constexpr uint32_t kTexClampModeClamp = 2;
constexpr uint32_t kTexXYFilterModePoint = 0;
constexpr uint32_t kTexXYFilterModeLinear = 1;
constexpr uint32_t kClearFlagsDepth = 1;

}

#endif
