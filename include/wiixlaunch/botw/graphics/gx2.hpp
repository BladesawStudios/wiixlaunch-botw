#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/mem.hpp>
#include <wiixlaunch/tick.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/debug_log.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include <wiixlaunch/fs.hpp>
#include "gfd.hpp"
#include "gx2_shader_types.hpp"
#include "shaders/spriteui_gsh_bytes.hpp"
#include "shaders/gx2_normals_shader.hpp"

#if WIIXL_CEMU
#include "gx2_imports.hpp"
#elif WIIXL_WIIU
#include <gx2/context.h>
#include <gx2/state.h>
#include <gx2/draw.h>
#include <gx2/shaders.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/clear.h>
#include <gx2/swap.h>
#include <gx2/event.h>
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>

// ===========================================================================
// WiiXLaunch::BotW::GX2 - High-Level GX2 Graphics Injection Framework
// ===========================================================================
// Provides a clean, modular API identical in style to BotW::NVN for injecting
// custom 2D/3D graphics, shaders, textures, and UI elements directly into
// Breath of the Wild's GX2 render loop on Wii U and Cemu.
// ===========================================================================

namespace WiiXLaunch::BotW::GX2 {

constexpr bool SupportsGX2 = true;

// ---------------------------------------------------------------------------
// Struct Layouts & Types matching NVN API style
// ---------------------------------------------------------------------------
using TextureHandle = uintptr_t;
using CommandBuffer = void;
using Device        = void;

struct TextureVertex {
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
};

struct MeshVertex {
    float x, y, z, w;
    float nx, ny, nz, nw;
};

// Returned by LoadMesh. Unlike a texture (whose pixel data CreateTexture
// copies into its own GX2 surface immediately), DrawMesh re-reads this
// pointer every frame - see its per-frame ring-buffer copy - so the buffer
// backing it must outlive the mesh, not just the LoadMesh call.
struct MeshData {
    const MeshVertex* vertices = nullptr;
    size_t vertexCount = 0;
};

namespace Format {
    constexpr int32_t RGBA8        = 0x1a; // GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8
    constexpr int32_t R8           = 0x01;
    constexpr int32_t Float4       = 0x2e;
    constexpr int32_t Float2       = 0x16;
}

namespace Filter {
    constexpr int32_t Nearest      = 0x0;
    constexpr int32_t Linear       = 0x1;
}

namespace WrapMode {
    constexpr int32_t ClampToEdge  = 0x7;
    constexpr int32_t Repeat       = 0x0;
    constexpr int32_t MirrorRepeat = 0x1;
}

using DrawCallback = void (*)(CommandBuffer* cmdBuf, void* dstTexture, int width, int height);
using InitCallback = void (*)();

// ---------------------------------------------------------------------------
// Blend state
// ---------------------------------------------------------------------------
// One GX2SetBlendControl worth of state. Colour and alpha are always
// specified separately here (GX2's separateAlphaBlend flag is set), because
// leaving alpha to follow the colour factors is wrong whenever the colour
// factors read DESTINATION colour - and it is the alpha channel that decides
// what a later draw blends against.
struct BlendState {
    uint32_t colorSrc = GX2Types::kBlendSrcAlpha;
    uint32_t colorDst = GX2Types::kBlendInvSrcAlpha;
    uint32_t colorCombine = GX2Types::kBlendCombineAdd;
    uint32_t alphaSrc = GX2Types::kBlendOne;
    uint32_t alphaDst = GX2Types::kBlendInvSrcAlpha;
    uint32_t alphaCombine = GX2Types::kBlendCombineAdd;

    bool operator==(const BlendState& o) const {
        return colorSrc == o.colorSrc && colorDst == o.colorDst && colorCombine == o.colorCombine &&
               alphaSrc == o.alphaSrc && alphaDst == o.alphaDst && alphaCombine == o.alphaCombine;
    }
    bool operator!=(const BlendState& o) const { return !(*this == o); }
};

namespace Blend {
    // Straight (non-premultiplied) alpha - what a .bflyt material with no
    // blend block gets, and what all but a handful of BotW's UI materials use.
    constexpr BlendState Alpha{GX2Types::kBlendSrcAlpha, GX2Types::kBlendInvSrcAlpha, GX2Types::kBlendCombineAdd,
                               GX2Types::kBlendOne, GX2Types::kBlendInvSrcAlpha, GX2Types::kBlendCombineAdd};
    // Colour already multiplied by its alpha.
    constexpr BlendState Premultiplied{GX2Types::kBlendOne, GX2Types::kBlendInvSrcAlpha, GX2Types::kBlendCombineAdd,
                                       GX2Types::kBlendOne, GX2Types::kBlendInvSrcAlpha, GX2Types::kBlendCombineAdd};
    // lyt (op 1, src 4, dst 1): the game's glow blend - 1557 materials in
    // Layout/Common.sblarc, more than every other explicit blend combined.
    constexpr BlendState Additive{GX2Types::kBlendSrcAlpha, GX2Types::kBlendOne, GX2Types::kBlendCombineAdd,
                                  GX2Types::kBlendOne, GX2Types::kBlendOne, GX2Types::kBlendCombineAdd};
    constexpr BlendState AdditivePremultiplied{GX2Types::kBlendOne, GX2Types::kBlendOne, GX2Types::kBlendCombineAdd,
                                               GX2Types::kBlendOne, GX2Types::kBlendOne, GX2Types::kBlendCombineAdd};
    // lyt (op 1, src 2, dst 4): src*dst + dst*srcAlpha. BotW uses it for
    // overlay ornaments (Message_00's Nt_MsgDeco_02/03, P_Overlay_00, ...).
    constexpr BlendState Overlay{GX2Types::kBlendDstColor, GX2Types::kBlendSrcAlpha, GX2Types::kBlendCombineAdd,
                                 GX2Types::kBlendOne, GX2Types::kBlendInvSrcAlpha, GX2Types::kBlendCombineAdd};
    // lyt (op 1, src 2, dst 0).
    constexpr BlendState Multiply{GX2Types::kBlendDstColor, GX2Types::kBlendZero, GX2Types::kBlendCombineAdd,
                                  GX2Types::kBlendDstAlpha, GX2Types::kBlendZero, GX2Types::kBlendCombineAdd};
    // lyt (op 1, src 1, dst 0) - writes the source through, ignoring alpha.
    constexpr BlendState Opaque{GX2Types::kBlendOne, GX2Types::kBlendZero, GX2Types::kBlendCombineAdd,
                                GX2Types::kBlendOne, GX2Types::kBlendZero, GX2Types::kBlendCombineAdd};
    // lyt (op 3, src 4, dst 1): dst - src*srcAlpha.
    constexpr BlendState Subtract{GX2Types::kBlendSrcAlpha, GX2Types::kBlendOne, GX2Types::kBlendCombineRevSub,
                                  GX2Types::kBlendZero, GX2Types::kBlendOne, GX2Types::kBlendCombineAdd};

    // Translates a .bflyt material blend block - the four bytes
    // (blendOp, sourceFactor, destFactor, logicOp) a BFLYT stores, whose
    // factor numbering is NOT GX2's - into GX2 state, so a value read out of
    // a layout dump can be used directly. logicOp is ignored (GX2 takes it
    // through GX2SetColorControl, and every BotW UI material leaves it at 0).
    //
    //   lyt op:     0 disable, 1 add, 2 subtract, 3 reverse subtract, 4 min, 5 max
    //   lyt factor: 0 zero, 1 one, 2 dstColor, 3 invDstColor, 4 srcAlpha,
    //               5 invSrcAlpha, 6 dstAlpha, 7 invDstAlpha, 8 srcColor, 9 invSrcColor
    inline uint32_t FactorFromLyt(uint8_t f) {
        switch (f) {
        case 0: return GX2Types::kBlendZero;
        case 1: return GX2Types::kBlendOne;
        case 2: return GX2Types::kBlendDstColor;
        case 3: return GX2Types::kBlendInvDstColor;
        case 4: return GX2Types::kBlendSrcAlpha;
        case 5: return GX2Types::kBlendInvSrcAlpha;
        case 6: return GX2Types::kBlendDstAlpha;
        case 7: return GX2Types::kBlendInvDstAlpha;
        case 8: return GX2Types::kBlendSrcColor;
        case 9: return GX2Types::kBlendInvSrcColor;
        default: return GX2Types::kBlendOne;
        }
    }

    inline uint32_t CombineFromLyt(uint8_t op) {
        switch (op) {
        case 2: return GX2Types::kBlendCombineSub;
        case 3: return GX2Types::kBlendCombineRevSub;
        case 4: return GX2Types::kBlendCombineMin;
        case 5: return GX2Types::kBlendCombineMax;
        default: return GX2Types::kBlendCombineAdd;
        }
    }

    inline BlendState FromLyt(uint8_t blendOp, uint8_t srcFactor, uint8_t dstFactor) {
        if (blendOp == 0) return Opaque;   // lyt "disable" writes the source unblended
        BlendState s;
        s.colorSrc = FactorFromLyt(srcFactor);
        s.colorDst = FactorFromLyt(dstFactor);
        s.colorCombine = CombineFromLyt(blendOp);
        // The layouts' own separate-alpha block is not honoured here; keeping
        // straight-alpha accumulation in the alpha channel is right for every
        // colour mode above and harmless on a 2-bit destination alpha.
        s.alphaSrc = GX2Types::kBlendOne;
        s.alphaDst = GX2Types::kBlendInvSrcAlpha;
        s.alphaCombine = GX2Types::kBlendCombineAdd;
        return s;
    }
}

namespace impl {

constexpr uintptr_t kGraphicsContextSlot = 0x1046c92c;
constexpr uintptr_t kContextStateOffset  = 0xf8;

inline void* GetGraphicsContext() {
    return *reinterpret_cast<void**>(kGraphicsContextSlot);
}

// Set while drawing from the middle of the game's frame (the pre-HUD
// callbacks), so every draw path below programs a private context state
// instead of the game's. See RunPreHud.
inline void* g_ContextOverride = nullptr;
// Set the first time a draw path actually asks for the override, so a
// pre-HUD pass that draws nothing switches nothing.
inline bool g_ContextOverrideUsed = false;

inline void* GetContextState() {
    if (g_ContextOverride) {
        g_ContextOverrideUsed = true;
        return g_ContextOverride;
    }
    void* ctx = GetGraphicsContext();
    if (!ctx) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ctx) + kContextStateOffset);
}

// What every draw path programs: the private context state while a mid-frame draw is running (g_ContextOverride, above),
// the game's own otherwise. GetContextState() already makes that choice; this is the name the shader-pass code uses.
inline void* DrawContext() { return GetContextState(); }

using FnSetContextState       = void (*)(void* contextState);
using FnInvalidate            = void (*)(uint32_t mode, void* buffer, uint32_t size);
using FnSetAttribBuffer        = void (*)(uint32_t index, uint32_t size, uint32_t stride, const void* buffer);
using FnCalcFetchShaderSizeEx  = uint32_t (*)(uint32_t attribCount, uint32_t fetchShaderType, uint32_t tessMode);
using FnInitFetchShaderEx      = void (*)(GX2Types::FetchShader* shader, uint8_t* buffer, uint32_t attribCount,
                                           const GX2Types::AttribStream* attribs, uint32_t type, uint32_t tessMode);
using FnSetFetchShader          = void (*)(const GX2Types::FetchShader* shader);
using FnSetVertexShader         = void (*)(const GX2Types::VertexShader* shader);
using FnSetPixelShader          = void (*)(const GX2Types::PixelShader* shader);
using FnSetShaderModeEx         = void (*)(uint32_t mode, uint32_t numVsGpr, uint32_t numVsStack,
                                            uint32_t numGsGpr, uint32_t numGsStack, uint32_t numPsGpr, uint32_t numPsStack);
using FnDrawEx                  = void (*)(uint32_t primitive, uint32_t count, uint32_t startIndex, uint32_t numInstances);
using FnSetViewport              = void (*)(float x, float y, float width, float height, float nearZ, float farZ);
using FnSetScissor               = void (*)(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
using FnSetDepthOnlyControl      = void (*)(uint32_t depthTest, uint32_t depthWrite, uint32_t depthCompare);
using FnSetCullOnlyControl       = void (*)(uint32_t frontFace, uint32_t cullFront, uint32_t cullBack);
using FnSetColorBuffer           = void (*)(GX2Types::ColorBuffer* colorBuffer, uint32_t target);
using FnCalcSurfaceSizeAndAlignment = void (*)(GX2Types::Surface* surface);
using FnInitColorBufferRegs      = void (*)(GX2Types::ColorBuffer* colorBuffer);
using FnCopyColorBufferToScanBuffer = void (*)(const GX2Types::ColorBuffer* colorBuffer, uint32_t scanTarget);
using FnSwapScanBuffers          = void (*)();
using FnSetColorControl          = void (*)(uint32_t rop3, uint32_t targetBlendEnable, uint32_t multiWriteEnable, uint32_t colorWriteEnable);
using FnDrawDone                 = void (*)();
using FnSetTargetChannelMasks = void (*)(uint32_t mask0, uint32_t mask1, uint32_t mask2, uint32_t mask3,
                                         uint32_t mask4, uint32_t mask5, uint32_t mask6, uint32_t mask7);
using FnSetBlendControl       = void (*)(uint32_t target, uint32_t colorSrcBlend, uint32_t colorDstBlend, uint32_t colorCombine,
                                         uint32_t separateAlphaBlend, uint32_t alphaSrcBlend, uint32_t alphaDstBlend, uint32_t alphaCombine);
using FnClearColor            = void (*)(GX2Types::ColorBuffer* colorBuffer, float red, float green, float blue, float alpha);
using FnFlush                 = void (*)();
using FnInitTexture2D         = void (*)(GX2Types::Texture* texture, uint32_t width, uint32_t height, uint32_t depth,
                                         uint32_t mipLevels, uint32_t format, uint32_t aa, uint32_t use);
using FnInitTextureRegs       = void (*)(GX2Types::Texture* texture);
using FnInitSampler           = void (*)(GX2Types::Sampler* sampler, uint32_t clampMode, uint32_t minMagFilterMode);
using FnInitSamplerClamping   = void (*)(GX2Types::Sampler* sampler, uint32_t clampX, uint32_t clampY, uint32_t clampZ);
using FnInitSamplerFilter     = void (*)(GX2Types::Sampler* sampler, uint32_t minFilter, uint32_t magFilter, uint32_t mipFilter);
using FnSetPixelTexture       = void (*)(const GX2Types::Texture* texture, uint32_t unit);
using FnSetPixelSampler       = void (*)(const GX2Types::Sampler* sampler, uint32_t unit);
using FnInitDepthBufferRegs   = void (*)(GX2Types::DepthBuffer* depthBuffer);
using FnSetDepthBuffer         = void (*)(GX2Types::DepthBuffer* depthBuffer);
using FnClearDepthStencilEx   = void (*)(GX2Types::DepthBuffer* depthBuffer, float depth, uint8_t stencil, uint32_t clearFlags);
using FnSetPixelUniformBlock  = void (*)(uint32_t location, uint32_t size, const void* data);
using FnSetupContextStateEx   = void (*)(void* contextState, uint32_t loadDefaults);

#if WIIXL_CEMU
inline FnSetContextState SetContextState() { return WiiXLaunch::Backend::ResolveGx2<FnSetContextState>(WiiXLaunch::Backend::Gx2Import::SetContextState); }
inline FnSetupContextStateEx SetupContextStateEx() { return WiiXLaunch::Backend::ResolveGx2<FnSetupContextStateEx>(WiiXLaunch::Backend::Gx2Import::SetupContextStateEx); }
inline FnInvalidate Invalidate() { return WiiXLaunch::Backend::ResolveGx2<FnInvalidate>(WiiXLaunch::Backend::Gx2Import::Invalidate); }
inline FnSetAttribBuffer SetAttribBuffer() { return WiiXLaunch::Backend::ResolveGx2<FnSetAttribBuffer>(WiiXLaunch::Backend::Gx2Import::SetAttribBuffer); }
inline FnCalcFetchShaderSizeEx CalcFetchShaderSizeEx() { return WiiXLaunch::Backend::ResolveGx2<FnCalcFetchShaderSizeEx>(WiiXLaunch::Backend::Gx2Import::CalcFetchShaderSizeEx); }
inline FnInitFetchShaderEx InitFetchShaderEx() { return WiiXLaunch::Backend::ResolveGx2<FnInitFetchShaderEx>(WiiXLaunch::Backend::Gx2Import::InitFetchShaderEx); }
inline FnSetFetchShader SetFetchShader() { return WiiXLaunch::Backend::ResolveGx2<FnSetFetchShader>(WiiXLaunch::Backend::Gx2Import::SetFetchShader); }
inline FnSetVertexShader SetVertexShader() { return WiiXLaunch::Backend::ResolveGx2<FnSetVertexShader>(WiiXLaunch::Backend::Gx2Import::SetVertexShader); }
inline FnSetPixelShader SetPixelShader() { return WiiXLaunch::Backend::ResolveGx2<FnSetPixelShader>(WiiXLaunch::Backend::Gx2Import::SetPixelShader); }
inline FnSetShaderModeEx SetShaderModeEx() { return WiiXLaunch::Backend::ResolveGx2<FnSetShaderModeEx>(WiiXLaunch::Backend::Gx2Import::SetShaderModeEx); }
inline FnDrawEx DrawEx() { return WiiXLaunch::Backend::ResolveGx2<FnDrawEx>(WiiXLaunch::Backend::Gx2Import::DrawEx); }
inline FnSetViewport SetViewport() { return WiiXLaunch::Backend::ResolveGx2<FnSetViewport>(WiiXLaunch::Backend::Gx2Import::SetViewport); }
inline FnSetScissor SetScissor() { return WiiXLaunch::Backend::ResolveGx2<FnSetScissor>(WiiXLaunch::Backend::Gx2Import::SetScissor); }
inline FnSetDepthOnlyControl SetDepthOnlyControl() { return WiiXLaunch::Backend::ResolveGx2<FnSetDepthOnlyControl>(WiiXLaunch::Backend::Gx2Import::SetDepthOnlyControl); }
inline FnSetCullOnlyControl SetCullOnlyControl() { return WiiXLaunch::Backend::ResolveGx2<FnSetCullOnlyControl>(WiiXLaunch::Backend::Gx2Import::SetCullOnlyControl); }
inline FnSetColorBuffer SetColorBuffer() { return WiiXLaunch::Backend::ResolveGx2<FnSetColorBuffer>(WiiXLaunch::Backend::Gx2Import::SetColorBuffer); }
inline FnCalcSurfaceSizeAndAlignment CalcSurfaceSizeAndAlignment() { return WiiXLaunch::Backend::ResolveGx2<FnCalcSurfaceSizeAndAlignment>(WiiXLaunch::Backend::Gx2Import::CalcSurfaceSizeAndAlignment); }
inline FnInitColorBufferRegs InitColorBufferRegs() { return WiiXLaunch::Backend::ResolveGx2<FnInitColorBufferRegs>(WiiXLaunch::Backend::Gx2Import::InitColorBufferRegs); }
inline FnCopyColorBufferToScanBuffer CopyColorBufferToScanBuffer() { return WiiXLaunch::Backend::ResolveGx2<FnCopyColorBufferToScanBuffer>(WiiXLaunch::Backend::Gx2Import::CopyColorBufferToScanBuffer); }
inline FnSwapScanBuffers SwapScanBuffers() { return WiiXLaunch::Backend::ResolveGx2<FnSwapScanBuffers>(WiiXLaunch::Backend::Gx2Import::SwapScanBuffers); }
inline FnSetColorControl SetColorControl() { return WiiXLaunch::Backend::ResolveGx2<FnSetColorControl>(WiiXLaunch::Backend::Gx2Import::SetColorControl); }
inline FnSetTargetChannelMasks SetTargetChannelMasks() { return WiiXLaunch::Backend::ResolveGx2<FnSetTargetChannelMasks>(WiiXLaunch::Backend::Gx2Import::SetTargetChannelMasks); }
inline FnSetBlendControl SetBlendControl() { return WiiXLaunch::Backend::ResolveGx2<FnSetBlendControl>(WiiXLaunch::Backend::Gx2Import::SetBlendControl); }
inline FnClearColor ClearColor() { return WiiXLaunch::Backend::ResolveGx2<FnClearColor>(WiiXLaunch::Backend::Gx2Import::ClearColor); }
inline FnDrawDone DrawDone() { return WiiXLaunch::Backend::ResolveGx2<FnDrawDone>(WiiXLaunch::Backend::Gx2Import::DrawDone); }
inline FnFlush Flush() { return WiiXLaunch::Backend::ResolveGx2<FnFlush>(WiiXLaunch::Backend::Gx2Import::Flush); }
inline FnInitTextureRegs InitTextureRegs() { return WiiXLaunch::Backend::ResolveGx2<FnInitTextureRegs>(WiiXLaunch::Backend::Gx2Import::InitTextureRegs); }
inline FnInitSampler InitSampler() { return WiiXLaunch::Backend::ResolveGx2<FnInitSampler>(WiiXLaunch::Backend::Gx2Import::InitSampler); }
inline FnInitSamplerClamping InitSamplerClamping() { return WiiXLaunch::Backend::ResolveGx2<FnInitSamplerClamping>(WiiXLaunch::Backend::Gx2Import::InitSamplerClamping); }
inline FnSetPixelTexture SetPixelTexture() { return WiiXLaunch::Backend::ResolveGx2<FnSetPixelTexture>(WiiXLaunch::Backend::Gx2Import::SetPixelTexture); }
inline FnSetPixelSampler SetPixelSampler() { return WiiXLaunch::Backend::ResolveGx2<FnSetPixelSampler>(WiiXLaunch::Backend::Gx2Import::SetPixelSampler); }
inline FnInitDepthBufferRegs InitDepthBufferRegs() { return WiiXLaunch::Backend::ResolveGx2<FnInitDepthBufferRegs>(WiiXLaunch::Backend::Gx2Import::InitDepthBufferRegs); }
inline FnSetDepthBuffer SetDepthBuffer() { return WiiXLaunch::Backend::ResolveGx2<FnSetDepthBuffer>(WiiXLaunch::Backend::Gx2Import::SetDepthBuffer); }
inline FnClearDepthStencilEx ClearDepthStencilEx() { return WiiXLaunch::Backend::ResolveGx2<FnClearDepthStencilEx>(WiiXLaunch::Backend::Gx2Import::ClearDepthStencilEx); }
inline FnSetPixelUniformBlock SetPixelUniformBlock() { return WiiXLaunch::Backend::ResolveGx2<FnSetPixelUniformBlock>(WiiXLaunch::Backend::Gx2Import::SetPixelUniformBlock); }
#elif WIIXL_WIIU
inline FnSetContextState SetContextState() { return reinterpret_cast<FnSetContextState>(&GX2SetContextState); }
inline FnSetupContextStateEx SetupContextStateEx() { return reinterpret_cast<FnSetupContextStateEx>(&GX2SetupContextStateEx); }
inline FnInvalidate Invalidate() { return reinterpret_cast<FnInvalidate>(&GX2Invalidate); }
inline FnSetAttribBuffer SetAttribBuffer() { return reinterpret_cast<FnSetAttribBuffer>(&GX2SetAttribBuffer); }
inline FnCalcFetchShaderSizeEx CalcFetchShaderSizeEx() { return reinterpret_cast<FnCalcFetchShaderSizeEx>(&GX2CalcFetchShaderSizeEx); }
inline FnInitFetchShaderEx InitFetchShaderEx() { return reinterpret_cast<FnInitFetchShaderEx>(&GX2InitFetchShaderEx); }
inline FnSetFetchShader SetFetchShader() { return reinterpret_cast<FnSetFetchShader>(&GX2SetFetchShader); }
inline FnSetVertexShader SetVertexShader() { return reinterpret_cast<FnSetVertexShader>(&GX2SetVertexShader); }
inline FnSetPixelShader SetPixelShader() { return reinterpret_cast<FnSetPixelShader>(&GX2SetPixelShader); }
inline FnSetShaderModeEx SetShaderModeEx() { return reinterpret_cast<FnSetShaderModeEx>(&GX2SetShaderModeEx); }
inline FnDrawEx DrawEx() { return reinterpret_cast<FnDrawEx>(&GX2DrawEx); }
inline FnSetViewport SetViewport() { return reinterpret_cast<FnSetViewport>(&GX2SetViewport); }
inline FnSetScissor SetScissor() { return reinterpret_cast<FnSetScissor>(&GX2SetScissor); }
inline FnSetDepthOnlyControl SetDepthOnlyControl() { return reinterpret_cast<FnSetDepthOnlyControl>(&GX2SetDepthOnlyControl); }
inline FnSetCullOnlyControl SetCullOnlyControl() { return reinterpret_cast<FnSetCullOnlyControl>(&GX2SetCullOnlyControl); }
inline FnSetColorBuffer SetColorBuffer() { return reinterpret_cast<FnSetColorBuffer>(&GX2SetColorBuffer); }
inline FnCalcSurfaceSizeAndAlignment CalcSurfaceSizeAndAlignment() { return reinterpret_cast<FnCalcSurfaceSizeAndAlignment>(&GX2CalcSurfaceSizeAndAlignment); }
inline FnInitColorBufferRegs InitColorBufferRegs() { return reinterpret_cast<FnInitColorBufferRegs>(&GX2InitColorBufferRegs); }
inline FnCopyColorBufferToScanBuffer CopyColorBufferToScanBuffer() { return reinterpret_cast<FnCopyColorBufferToScanBuffer>(&GX2CopyColorBufferToScanBuffer); }
inline FnSwapScanBuffers SwapScanBuffers() { return reinterpret_cast<FnSwapScanBuffers>(&GX2SwapScanBuffers); }
inline FnSetColorControl SetColorControl() { return reinterpret_cast<FnSetColorControl>(&GX2SetColorControl); }
inline FnSetTargetChannelMasks SetTargetChannelMasks() { return reinterpret_cast<FnSetTargetChannelMasks>(&GX2SetTargetChannelMasks); }
inline FnSetBlendControl SetBlendControl() { return reinterpret_cast<FnSetBlendControl>(&GX2SetBlendControl); }
inline FnClearColor ClearColor() { return reinterpret_cast<FnClearColor>(&GX2ClearColor); }
inline FnDrawDone DrawDone() { return reinterpret_cast<FnDrawDone>(&GX2DrawDone); }
inline FnFlush Flush() { return reinterpret_cast<FnFlush>(&GX2Flush); }
inline FnInitTextureRegs InitTextureRegs() { return reinterpret_cast<FnInitTextureRegs>(&GX2InitTextureRegs); }
inline FnInitSampler InitSampler() { return reinterpret_cast<FnInitSampler>(&GX2InitSampler); }
inline FnInitSamplerClamping InitSamplerClamping() { return reinterpret_cast<FnInitSamplerClamping>(&GX2InitSamplerClamping); }
inline FnSetPixelTexture SetPixelTexture() { return reinterpret_cast<FnSetPixelTexture>(&GX2SetPixelTexture); }
inline FnSetPixelSampler SetPixelSampler() { return reinterpret_cast<FnSetPixelSampler>(&GX2SetPixelSampler); }
inline FnInitDepthBufferRegs InitDepthBufferRegs() { return reinterpret_cast<FnInitDepthBufferRegs>(&GX2InitDepthBufferRegs); }
inline FnSetDepthBuffer SetDepthBuffer() { return reinterpret_cast<FnSetDepthBuffer>(&GX2SetDepthBuffer); }
inline FnClearDepthStencilEx ClearDepthStencilEx() { return reinterpret_cast<FnClearDepthStencilEx>(&GX2ClearDepthStencilEx); }
inline FnSetPixelUniformBlock SetPixelUniformBlock() { return reinterpret_cast<FnSetPixelUniformBlock>(&GX2SetPixelUniformBlock); }
#endif

inline void* AllocMEM1(uint32_t size, uint32_t align = 256) {
#if WIIXL_CEMU
    // Arena::AllocHost is bounded and can refuse: the arena runs from the end
    // of the payload to 0x01C00000, the end of Cemu code-cave AREA, and the
    // allocator this replaced used to hand out pointers past that. Nothing here
    // can recover from running out, but silence was the worst of the options -
    // over-allocating should show up in the log rather than as corruption
    // somewhere else entirely. Once is enough; DrawMesh retries its ring buffer
    // every call.
    //
    // AllocHost, not the mod allocator: this module is compiled INTO the
    // payload, so its surfaces are the host memory. It is additionally bounded
    // by where module grants begin, so a font sheet cannot eat memory a loaded
    // .wxlm was promised.
    void* p = WiiXLaunch::Arena::AllocHost(size, align);
    if (p) return p;

    // The arena is full - shader passes, their scene copy, data textures and
    // the GUI's art between them use it up - so the rest comes out of the
    // game's own MEM2 heap. Cemu's GPU reads guest memory wherever it is, and
    // these are taken once, at load, and kept: the game's heap never sees them
    // churn. Before this, the GUI's first font sheet was simply refused and
    // every mod's text drew nothing.
    void* heap = WiiXLaunch::Mem::GetBaseHeapHandle(WiiXLaunch::Mem::BaseHeap::MEM2);
    p = WiiXLaunch::Mem::AllocFromExpHeap(heap, size, static_cast<int32_t>(align));
    static uint32_t s_overflowLogs = 0;
    if (s_overflowLogs < 8) {
        ++s_overflowLogs;
        BotW::OSLog("WiiXLaunch: arena full for the host (%u of %u used, %u free, %u granted to modules) - "
                    "%u bytes %s the game's MEM2 heap\n",
                    WiiXLaunch::Arena::HostUsed(), WiiXLaunch::Arena::Total(), WiiXLaunch::Arena::Free(),
                    WiiXLaunch::Arena::ModuleCarved(), size, p ? "taken from" : "refused by");
    }
    return p;
#elif WIIXL_WIIU
    using FnAllocMEM1 = void* (*)(uint32_t size, uint32_t align);
    auto allocMEM1 = reinterpret_cast<FnAllocMEM1>(0x0309bb68);
    return allocMEM1(size, align);
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Pipeline State Storage
// ---------------------------------------------------------------------------
struct TextureWrapper {
    GX2Types::Texture texture;
    GX2Types::Sampler sampler;
};

// 64 rather than 16: the GUI alone holds ~25 pieces of the game's own UI art
// plus three font sheets (see gui/gui_assets.hpp).
constexpr size_t kMaxTextures = 64;
inline TextureWrapper g_Textures[kMaxTextures]{};
inline uint32_t g_TextureCount = 0;

// Sprite Pipeline (2D textured quads)
inline GX2Types::VertexShader* g_SpriteVertexShader = nullptr;
inline GX2Types::PixelShader* g_SpritePixelShader = nullptr;
inline GX2Types::FetchShader g_SpriteFetchShader{};
alignas(2048) inline uint8_t g_SpriteVsHeaderBuf[2048];
alignas(2048) inline uint8_t g_SpritePsHeaderBuf[2048];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_SpriteVsProgBuf[4096];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_SpritePsProgBuf[4096];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_SpriteFsProgBuf[512];
alignas(256) inline uint8_t g_SpriteRingBuffer[4096];
inline size_t g_SpriteRingSlot = 0;
inline bool g_SpritePipelineReady = false;

// Mesh Pipeline (3D normals meshes + depth testing)
inline GX2Types::VertexShader* g_MeshVertexShader = nullptr;
inline GX2Types::PixelShader* g_MeshPixelShader = nullptr;
inline GX2Types::FetchShader g_MeshFetchShader{};
alignas(2048) inline uint8_t g_MeshVsHeaderBuf[2048];
alignas(2048) inline uint8_t g_MeshPsHeaderBuf[2048];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_MeshVsProgBuf[4096];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_MeshPsProgBuf[4096];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_MeshFsProgBuf[512];
inline uint8_t* g_MeshRingBuffer = nullptr;
inline size_t g_MeshRingBufferSize = 64 * 1024;
inline bool g_MeshPipelineReady = false;

// Private Depth Buffer
inline GX2Types::DepthBuffer g_DepthBuffer{};
inline bool g_DepthBufferReady = false;
inline bool g_DepthClearedThisFrame = false;

// Callback system
constexpr size_t kMaxCallbacks = 16;
inline DrawCallback g_DrawCallbacks[kMaxCallbacks]{};
inline size_t g_DrawCallbackCount = 0;
// Draws that land under the HUD rather than over it - see RunPreHud.
inline DrawCallback g_PreHudCallbacks[kMaxCallbacks]{};
inline size_t g_PreHudCallbackCount = 0;
inline InitCallback g_InitCallbacks[kMaxCallbacks]{};
inline size_t g_InitCallbackCount = 0;
inline bool g_PipelineInitialized = false;

inline void EnsureDepthBuffer(uint32_t width, uint32_t height) {
    if (g_DepthBufferReady) return;

    g_DepthBuffer.surface.dim = GX2Types::kSurfaceDimTexture2D;
    g_DepthBuffer.surface.width = width ? width : 1280;
    g_DepthBuffer.surface.height = height ? height : 720;
    g_DepthBuffer.surface.depth = 1;
    g_DepthBuffer.surface.mipLevels = 1;
    g_DepthBuffer.surface.format = GX2Types::kSurfaceFormatFloatD32;
    g_DepthBuffer.surface.aa = GX2Types::kAaMode1x;
    g_DepthBuffer.surface.use = GX2Types::kSurfaceUseDepthBuffer;
    g_DepthBuffer.surface.tileMode = GX2Types::kTileModeLinearAligned;

    CalcSurfaceSizeAndAlignment()(&g_DepthBuffer.surface);

    g_DepthBuffer.surface.image = AllocMEM1(g_DepthBuffer.surface.imageSize, g_DepthBuffer.surface.alignment);
    if (!g_DepthBuffer.surface.image) {
        // The only allocation here that was never checked. Now that the heap
        // can refuse, going on would register a depth buffer with a null image.
        BotW::OSLog("WiiXLaunch: GX2 depth buffer (%ux%u, %u bytes) would not allocate\n",
                    g_DepthBuffer.surface.width, g_DepthBuffer.surface.height,
                    g_DepthBuffer.surface.imageSize);
        return;
    }
    g_DepthBuffer.viewMip = 0;
    g_DepthBuffer.viewFirstSlice = 0;
    g_DepthBuffer.viewNumSlices = 1;
    g_DepthBuffer.depthClear = 1.0f;
    g_DepthBuffer.stencilClear = 0;

    InitDepthBufferRegs()(&g_DepthBuffer);
    g_DepthBufferReady = true;
    BotW::OSLog("WiiXLaunch: GX2 DepthBuffer initialized (%ux%u img=%p)\n",
        g_DepthBuffer.surface.width, g_DepthBuffer.surface.height, g_DepthBuffer.surface.image);
}

inline void EnsureSpritePipeline() {
    if (g_SpritePipelineReady) return;

    const uint8_t* gshBytes = Shaders::g_SpriteUiGshBytes;
    size_t gshSize = Shaders::kSpriteUiGshBytesSize;

    g_SpriteVertexShader = GFD::GetShader<GX2Types::VertexShader>(
        gshBytes, GFD::kBlockTypeVertexShaderHeader, GFD::kBlockTypeVertexShaderProgram,
        g_SpriteVsHeaderBuf, sizeof(g_SpriteVsHeaderBuf),
        g_SpriteVsProgBuf, sizeof(g_SpriteVsProgBuf));
    g_SpritePixelShader = GFD::GetShader<GX2Types::PixelShader>(
        gshBytes, GFD::kBlockTypePixelShaderHeader, GFD::kBlockTypePixelShaderProgram,
        g_SpritePsHeaderBuf, sizeof(g_SpritePsHeaderBuf),
        g_SpritePsProgBuf, sizeof(g_SpritePsProgBuf));

    if (!g_SpriteVertexShader || !g_SpritePixelShader) {
        WIIXL_LOG("WiiXLaunch: Sprite shader load failed (vs=%p ps=%p)", g_SpriteVertexShader, g_SpritePixelShader);
        return;
    }

    GX2Types::AttribStream attribs[3]{};
    // Stream 0: aPosition (offset 0, Float32x4)
    attribs[0].location = 0;
    attribs[0].buffer = 0;
    attribs[0].offset = 0;
    attribs[0].format = GX2Types::kAttribFormatFloat32x4;
    attribs[0].type = GX2Types::kAttribIndexPerVertex;
    attribs[0].aluDivisor = 0;
    attribs[0].mask = 0x00010203;
    attribs[0].endianSwap = GX2Types::kEndianSwapDefault;

    // Stream 1: aTexCoord (offset 16, Float32x2)
    attribs[1].location = 1;
    attribs[1].buffer = 0;
    attribs[1].offset = 16;
    attribs[1].format = GX2Types::kAttribFormatFloat32x2;
    attribs[1].type = GX2Types::kAttribIndexPerVertex;
    attribs[1].aluDivisor = 0;
    attribs[1].mask = 0x00010405;
    attribs[1].endianSwap = GX2Types::kEndianSwapDefault;

    // Stream 2: aColor (offset 24, Float32x4)
    attribs[2].location = 2;
    attribs[2].buffer = 0;
    attribs[2].offset = 24;
    attribs[2].format = GX2Types::kAttribFormatFloat32x4;
    attribs[2].type = GX2Types::kAttribIndexPerVertex;
    attribs[2].aluDivisor = 0;
    attribs[2].mask = 0x00010203;
    attribs[2].endianSwap = GX2Types::kEndianSwapDefault;

    InitFetchShaderEx()(&g_SpriteFetchShader, g_SpriteFsProgBuf, 3, attribs,
        GX2Types::kFetchShaderTessellationNone, GX2Types::kTessellationModeDiscrete);

    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_SpriteVsProgBuf, sizeof(g_SpriteVsProgBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_SpritePsProgBuf, sizeof(g_SpritePsProgBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_SpriteFsProgBuf, sizeof(g_SpriteFsProgBuf));

    g_SpritePipelineReady = true;
    WIIXL_LOG("WiiXLaunch: GX2 Sprite pipeline ready OK");
}

inline void EnsureMeshPipeline() {
    if (g_MeshPipelineReady) return;

    const uint8_t* gshBytes = Shaders::g_NormalsGshBytes;
    size_t gshSize = Shaders::kNormalsGshBytesSize;

    g_MeshVertexShader = GFD::GetShader<GX2Types::VertexShader>(
        gshBytes, GFD::kBlockTypeVertexShaderHeader, GFD::kBlockTypeVertexShaderProgram,
        g_MeshVsHeaderBuf, sizeof(g_MeshVsHeaderBuf),
        g_MeshVsProgBuf, sizeof(g_MeshVsProgBuf));
    g_MeshPixelShader = GFD::GetShader<GX2Types::PixelShader>(
        gshBytes, GFD::kBlockTypePixelShaderHeader, GFD::kBlockTypePixelShaderProgram,
        g_MeshPsHeaderBuf, sizeof(g_MeshPsHeaderBuf),
        g_MeshPsProgBuf, sizeof(g_MeshPsProgBuf));

    if (!g_MeshVertexShader || !g_MeshPixelShader) {
        WIIXL_LOG("WiiXLaunch: Mesh shader load failed (vs=%p ps=%p)", g_MeshVertexShader, g_MeshPixelShader);
        return;
    }

    GX2Types::AttribStream attribs[2]{};
    // Stream 0: aPosition (offset 0, Float32x4)
    attribs[0].location = 0;
    attribs[0].buffer = 0;
    attribs[0].offset = 0;
    attribs[0].format = GX2Types::kAttribFormatFloat32x4;
    attribs[0].type = GX2Types::kAttribIndexPerVertex;
    attribs[0].aluDivisor = 0;
    attribs[0].mask = 0x00010203;
    attribs[0].endianSwap = GX2Types::kEndianSwapDefault;

    // Stream 1: aNormal (offset 16, Float32x4)
    attribs[1].location = 1;
    attribs[1].buffer = 0;
    attribs[1].offset = 16;
    attribs[1].format = GX2Types::kAttribFormatFloat32x4;
    attribs[1].type = GX2Types::kAttribIndexPerVertex;
    attribs[1].aluDivisor = 0;
    attribs[1].mask = 0x00010203;
    attribs[1].endianSwap = GX2Types::kEndianSwapDefault;

    InitFetchShaderEx()(&g_MeshFetchShader, g_MeshFsProgBuf, 2, attribs,
        GX2Types::kFetchShaderTessellationNone, GX2Types::kTessellationModeDiscrete);

    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_MeshVsProgBuf, sizeof(g_MeshVsProgBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_MeshPsProgBuf, sizeof(g_MeshPsProgBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_MeshFsProgBuf, sizeof(g_MeshFsProgBuf));

    g_MeshPipelineReady = true;
    WIIXL_LOG("WiiXLaunch: GX2 Mesh pipeline ready OK");
}

inline void InitializeAllPipelines() {
    EnsureSpritePipeline();
    EnsureMeshPipeline();

    if (g_SpritePipelineReady && !g_PipelineInitialized) {
        g_PipelineInitialized = true;
        for (size_t i = 0; i < g_InitCallbackCount; ++i) {
            if (g_InitCallbacks[i]) g_InitCallbacks[i]();
        }
    }
}

// ---------------------------------------------------------------------------
// Quad batching (the GUI's draw path)
// ---------------------------------------------------------------------------
// DrawSprite sets the whole pipeline up and syncs the GPU (DrawDone) for
// every quad, which is fine for a handful of sprites and hopeless for text -
// one dialog box is a few hundred glyph quads. The batch path sets state
// once per BeginBatch, appends quads (six vertices each) for one texture at
// a time, and issues a single DrawEx per texture change or flush. No
// DrawDone: the vertex ring is split into two halves that alternate per
// frame, so a frame's vertices are not overwritten until the frame after
// next, by which point the game's own swap has waited for that GPU work.
// One frame's vertices have to fit in HALF of this, since the two halves
// alternate so the GPU is never reading the half being written. A quad is six
// vertices of 40 bytes, so 512 KB per half is about 2180 quads - comfortably
// more than the GUI's own 2048-quad frame limit, which is the point: whichever
// of the two runs out first should be the one that reports it.
//
// It used to be half this, which was under the frame limit, so a text-heavy
// frame quietly ran out of room. What that looks like is not a clean cut-off:
// flushes are dropped whole, so a big run of text vanishes while a two-quad
// pair of arrows right after it still fits and draws. A long dialogue box was
// enough to lose a menu row's label, a button's caption and a key guide while
// everything around them stayed.
constexpr size_t kBatchRingBytes = 1024 * 1024;
// The ring actually got. The host arena is small (about 3 MB on Cemu, shared
// with fonts and render targets), and allocating lazily, after a backdrop
// blur's targets, the full megabyte can be refused - and with no ring at all,
// every batch and every blur tap silently draws nothing. So it steps down: a
// smaller ring only limits how much text one frame can hold.
inline size_t g_BatchHalfBytes = 0;
constexpr uint32_t kBatchMaxQuadsPerFlush = 256;   // 256 quads * 6 verts * 40 B = 60 KB staging
inline uint8_t* g_BatchRing = nullptr;
inline uint32_t g_BatchFrame = 0;                  // selects the live half of the ring
inline size_t g_BatchHalfUsed = 0;                 // bytes of the live half used this frame
inline TextureWrapper* g_BatchTexture = nullptr;
inline TextureVertex g_BatchVerts[kBatchMaxQuadsPerFlush * 6];
inline uint32_t g_BatchVertCount = 0;
inline bool g_BatchActive = false;
inline uint32_t g_BatchDrawCalls = 0;
inline uint32_t g_BatchDroppedQuads = 0;
// Blend is part of the batch key: quads are only merged into one draw while
// both the texture and the blend state stay the same.
inline BlendState g_BatchBlend{};
inline BlendState g_BatchBlendApplied{};
inline bool g_BatchBlendValid = false;

inline void ApplyBlend(const BlendState& b) {
    if (g_BatchBlendValid && b == g_BatchBlendApplied) return;
    SetBlendControl()(0, b.colorSrc, b.colorDst, b.colorCombine, 1, b.alphaSrc, b.alphaDst, b.alphaCombine);
    g_BatchBlendApplied = b;
    g_BatchBlendValid = true;
}

// A second ring for draws made from inside the game's own frame (gui/map_layer.hpp). Those go into the game's command
// list, which the GPU runs a frame or more later - long after the main ring's half has been handed to the next scan-out
// and rewritten, so the GPU read whatever the next frame had put there and the layer flickered. This one is not tied to
// scan-outs at all: it is written straight through and wraps, and is big enough that a wrap comes many frames after the
// vertices it reuses were drawn.
constexpr uint32_t kLateRingBytes = 2u * 1024u * 1024u;
inline uint8_t* g_LateRing = nullptr;
inline size_t g_LateRingUsed = 0;
inline bool g_BatchLate = false;   // set around a late draw: BatchFlush writes to the late ring

inline void BatchNewFrame() {
    g_BatchFrame++;
    g_BatchHalfUsed = 0;
    g_BatchDrawCalls = 0;
    g_BatchDroppedQuads = 0;
}

inline bool EnsureBatchRing() {
    if (g_BatchRing) return true;
    static bool s_failed = false;
    if (s_failed) return false;
    for (size_t bytes = kBatchRingBytes; bytes >= 64 * 1024; bytes /= 2) {
        g_BatchRing = reinterpret_cast<uint8_t*>(AllocMEM1(static_cast<uint32_t>(bytes), 256));
        if (g_BatchRing) {
            g_BatchHalfBytes = bytes / 2;
            if (bytes != kBatchRingBytes) {
                BotW::OSLog("WiiXLaunch: GX2 batch ring is %u KB, not %u - the arena is short\n",
                            static_cast<unsigned int>(bytes / 1024),
                            static_cast<unsigned int>(kBatchRingBytes / 1024));
            }
            return true;
        }
    }
    s_failed = true;
    BotW::OSLog("WiiXLaunch: GX2 batch ring could not be allocated at any size - batches and blur will not draw\n");
    return false;
}

// Vertex memory for one draw outside a batch, from the live half of the batch
// ring - so it lives exactly as long as a flush does, until the frame after
// next. The backdrop blur used to take 256-byte slots from the 16-slot sprite
// ring with no sync; two blurs in a frame need more than 16 taps, so taps
// overwrote vertices the GPU had not read yet and the blur sampled whatever
// offsets a later tap left there. On screen that is a blur that shakes.
inline uint8_t* FrameAlloc(size_t bytes) {
    if (!EnsureBatchRing()) return nullptr;
    bytes = (bytes + 63) & ~static_cast<size_t>(63);
    if (g_BatchHalfUsed + bytes > g_BatchHalfBytes) return nullptr;
    uint8_t* p = g_BatchRing + (g_BatchFrame & 1) * g_BatchHalfBytes + g_BatchHalfUsed;
    g_BatchHalfUsed += bytes;
    return p;
}

inline void BatchFlush() {
    if (g_BatchVertCount == 0 || !g_BatchTexture || !g_BatchRing) {
        g_BatchVertCount = 0;
        return;
    }
    const size_t bytes = g_BatchVertCount * sizeof(TextureVertex);
    uint8_t* dst = nullptr;
    if (g_BatchLate) {
        if (!g_LateRing || bytes > kLateRingBytes) {
            g_BatchVertCount = 0;
            return;
        }
        if (g_LateRingUsed + bytes > kLateRingBytes) g_LateRingUsed = 0;
        dst = g_LateRing + g_LateRingUsed;
        g_LateRingUsed += (bytes + 255u) & ~static_cast<size_t>(255u);
    } else if (g_BatchHalfUsed + bytes > g_BatchHalfBytes) {
        // Frame budget exhausted: drop rather than scribble over vertices
        // the GPU may still be reading from the previous frame. Said once, so
        // this is diagnosable instead of just looking like missing text.
        g_BatchDroppedQuads += g_BatchVertCount / 6;
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            BotW::OSLog("WiiXLaunch: GX2 batch ring full (%u bytes a frame) - dropping quads\n",
                        static_cast<unsigned int>(g_BatchHalfBytes));
        }
        g_BatchVertCount = 0;
        return;
    }
    if (!dst) dst = g_BatchRing + (g_BatchFrame & 1) * g_BatchHalfBytes + g_BatchHalfUsed;
    memcpy(dst, g_BatchVerts, bytes);
    Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, dst, static_cast<uint32_t>(bytes));

    ApplyBlend(g_BatchBlend);
    SetPixelTexture()(&g_BatchTexture->texture, 0);
    SetPixelSampler()(&g_BatchTexture->sampler, 0);
    SetAttribBuffer()(0, static_cast<uint32_t>(bytes), sizeof(TextureVertex), dst);
    DrawEx()(GX2Types::kPrimitiveModeTriangles, g_BatchVertCount, 0, 1);

    // 64 is enough for an attribute buffer, and rounding to 256 wasted most
    // of a kilobyte across a frame's worth of small flushes.
    if (!g_BatchLate) g_BatchHalfUsed += (bytes + 63) & ~static_cast<size_t>(63);
    g_BatchVertCount = 0;
    g_BatchDrawCalls++;
}


// ---------------------------------------------------------------------------
// Backdrop blur
// ---------------------------------------------------------------------------
// The frosted glass BotW puts behind its windows. In the game a pane samples
// FBLayout_00^r, a capture of the framebuffer, which the material blurs; the
// window's own colour then sits over the top of it. Reproducing it here means
// doing the capture and the blur ourselves:
//
//   1. the game's colour buffer is aliased as a TEXTURE (same memory, a
//      texture view over it) so it can be sampled,
//   2. a quarter-size copy is drawn from it into one of two small render
//      targets, four taps at a time, which is both the downsample and the
//      first blur,
//   3. a few more four-tap passes ping-pong between the two targets, each one
//      reaching further, and
//   4. the result is left in target 0, where the UI samples it like any other
//      sprite.
//
// It is off unless a mod asks for it (GUI::SetBackdropBlur), because it costs
// two render targets and a handful of full-target draws every frame it is
// used, and most overlays do not want it.
//
// Two caveats worth stating plainly. Aliasing a colour buffer as a texture is
// not something GX2 promises will work when the surface was not created with
// texture usage - Cemu is happy with it, real hardware may not be. And there
// is no per-pixel mask: the blur is drawn as a rectangle, so a window with
// rounded corners gets blur in the corners too, hidden by whatever is drawn
// over it. Masking properly needs a two-texture shader, which needs the Wii U
// shader compiler this repo does not have.

struct BlurTarget {
    GX2Types::ColorBuffer color{};
    TextureWrapper tex{};      // a texture view over the same image
    bool ready = false;
};

inline BlurTarget g_BlurTargets[2];
inline TextureWrapper g_SceneAlias{};
inline uint32_t g_BlurWidth = 0;
inline uint32_t g_BlurHeight = 0;
inline bool g_BlurAvailable = false;
inline bool g_BlurTried = false;

inline bool EnsureBlurTarget(BlurTarget& t, uint32_t w, uint32_t h,
                             uint32_t format = GX2Types::kSurfaceFormatUnormR8G8B8A8) {
    if (t.ready) return true;

    t.color.surface.dim = GX2Types::kSurfaceDimTexture2D;
    t.color.surface.width = w;
    t.color.surface.height = h;
    t.color.surface.depth = 1;
    t.color.surface.mipLevels = 1;
    t.color.surface.format = format;
    t.color.surface.aa = GX2Types::kAaMode1x;
    t.color.surface.use = GX2Types::kSurfaceUseTextureColorBuffer;
    t.color.surface.tileMode = GX2Types::kTileModeTiled2DThin1;
    CalcSurfaceSizeAndAlignment()(&t.color.surface);
    if (t.color.surface.imageSize == 0) return false;

    t.color.surface.image = AllocMEM1(t.color.surface.imageSize, t.color.surface.alignment);
    if (!t.color.surface.image) {
        BotW::OSLog("WiiXLaunch GUI: backdrop blur target (%ux%u, %u bytes) would not allocate\n",
                    w, h, t.color.surface.imageSize);
        return false;
    }
    t.color.viewMip = 0;
    t.color.viewFirstSlice = 0;
    t.color.viewNumSlices = 1;
    InitColorBufferRegs()(&t.color);

    // The same image, described again as something to sample. Alpha is
    // forced to one: what is in the surface's own alpha is not wanted, and
    // the blur weights every tap through the vertex alpha.
    t.tex.texture.surface = t.color.surface;
    t.tex.texture.viewFirstMip = 0;
    t.tex.texture.viewNumMips = 1;
    t.tex.texture.viewFirstSlice = 0;
    t.tex.texture.viewNumSlices = 1;
    t.tex.texture.compMap = GX2Types::kCompMapRGBOpaque;
    InitTextureRegs()(&t.tex.texture);
    InitSampler()(&t.tex.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexXYFilterModeLinear);
    InitSamplerClamping()(&t.tex.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp,
                          GX2Types::kTexClampModeClamp);

    t.ready = true;
    return true;
}

// Describes the game's colour buffer as a texture so it can be sampled. Redone
// every frame: the buffer the hook is handed is not promised to be the same
// one twice.
inline void AliasScene(GX2Types::ColorBuffer* scene) {
    g_SceneAlias.texture.surface = scene->surface;
    g_SceneAlias.texture.surface.use |= GX2Types::kSurfaceUseTexture;
    g_SceneAlias.texture.viewFirstMip = 0;
    g_SceneAlias.texture.viewNumMips = 1;
    g_SceneAlias.texture.viewFirstSlice = 0;
    g_SceneAlias.texture.viewNumSlices = 1;
    g_SceneAlias.texture.compMap = GX2Types::kCompMapRGBOpaque;
    InitTextureRegs()(&g_SceneAlias.texture);
    InitSampler()(&g_SceneAlias.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexXYFilterModeLinear);
    InitSamplerClamping()(&g_SceneAlias.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp,
                          GX2Types::kTexClampModeClamp);
}

// One full-target quad sampling `src` shifted by (du, dv) in texture
// coordinates, weighted by `weight`. `first` writes the target, the rest add
// to it, so four taps at a quarter each average out to a box blur.
inline void BlurTap(GX2Types::ColorBuffer* target, TextureWrapper* src,
                    float du, float dv, float weight, bool first) {
    void* contextState = DrawContext();
    if (!contextState || !target->surface.image) return;

    const float u0 = du, u1 = 1.0f + du;
    const float v0 = dv, v1 = 1.0f + dv;
    const TextureVertex verts[4] = {
        {-1.0f,  1.0f, 0.0f, 1.0f, u0, v0, 1.0f, 1.0f, 1.0f, weight},
        { 1.0f,  1.0f, 0.0f, 1.0f, u1, v0, 1.0f, 1.0f, 1.0f, weight},
        {-1.0f, -1.0f, 0.0f, 1.0f, u0, v1, 1.0f, 1.0f, 1.0f, weight},
        { 1.0f, -1.0f, 0.0f, 1.0f, u1, v1, 1.0f, 1.0f, 1.0f, weight},
    };

    uint8_t* dst = FrameAlloc(sizeof(verts));
    if (!dst) return;
    memcpy(dst, verts, sizeof(verts));
    Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, dst, sizeof(verts));

    SetContextState()(contextState);
    SetShaderModeEx()(GX2Types::kShaderModeUniformRegister, 0x30, 0x40, 0x0, 0x0, 0xc8, 0xc0);
    SetColorBuffer()(target, 0);
    SetTargetChannelMasks()(0x0F, 0, 0, 0, 0, 0, 0, 0);
    SetViewport()(0.0f, 0.0f, static_cast<float>(target->surface.width),
                  static_cast<float>(target->surface.height), 0.0f, 1.0f);
    SetScissor()(0, 0, target->surface.width, target->surface.height);
    SetDepthOnlyControl()(0, 0, GX2Types::kCompareFuncAlways);
    SetCullOnlyControl()(GX2Types::kFrontFaceCcw, 0, 0);
    SetColorControl()(GX2Types::kLogicOpCopy, 1, 0, 1);
    // First tap replaces the target scaled by its weight, the rest add.
    SetBlendControl()(0, GX2Types::kBlendSrcAlpha,
                      first ? GX2Types::kBlendZero : GX2Types::kBlendOne,
                      GX2Types::kBlendCombineAdd, 1,
                      GX2Types::kBlendOne, GX2Types::kBlendZero, GX2Types::kBlendCombineAdd);

    SetPixelTexture()(&src->texture, 0);
    SetPixelSampler()(&src->sampler, 0);
    SetFetchShader()(&g_SpriteFetchShader);
    SetVertexShader()(g_SpriteVertexShader);
    SetPixelShader()(g_SpritePixelShader);
    SetAttribBuffer()(0, sizeof(verts), sizeof(TextureVertex), dst);
    DrawEx()(GX2Types::kPrimitiveModeTriangleStrip, 4, 0, 1);
}

// A four-tap box from `src` into `target`, `radius` texels of the SOURCE wide.
inline void BlurPass(GX2Types::ColorBuffer* target, TextureWrapper* src, float radius) {
    const float du = radius / static_cast<float>(src->texture.surface.width);
    const float dv = radius / static_cast<float>(src->texture.surface.height);
    BlurTap(target, src, -du, -dv, 0.25f, true);
    BlurTap(target, src,  du, -dv, 0.25f, false);
    BlurTap(target, src, -du,  dv, 0.25f, false);
    BlurTap(target, src,  du,  dv, 0.25f, false);
    Invalidate()(GX2Types::kInvalidateModeColorBuffer | GX2Types::kInvalidateModeTexture,
                 target->surface.image, target->surface.imageSize);
}

// ---------------------------------------------------------------------------
// Shader passes: a mod's own full-screen pixel shader
// ---------------------------------------------------------------------------
// The mod hands over a GFD (what CafeGLSL writes) and the host parses it into its own memory, builds the one-attribute
// fetch shader a full-screen quad needs, and draws it over the frame. The pass samples the frame as texture 0, which
// cannot be the buffer it is drawing into, so the frame is copied into a private target first. Parameters travel as
// float4 vertex attributes, the same on all four vertices, which the vertex shader hands on as varyings: there is no
// uniform block to set up, and the shader runs in the same register mode as the host's own.
constexpr size_t kMaxShaderPasses = 4;
constexpr size_t kPassMaxVec4 = 12;           // float4 parameters a pass may stream with its vertices
constexpr size_t kPassSlotBytes = 1024;       // room for four vertices of a position and kPassMaxVec4 parameters
constexpr size_t kPassSlots = 8;              // one per pass per frame, so the GPU never reads a slot being rewritten
constexpr uint32_t kPassDataMaxSide = 256;    // a pass's data texture, at most this many texels each way

// A pass's data texture: RGBA8 texels the mod writes and the pass samples as texture 2, for whatever does not fit in
// twelve float4s - a lookup table, a mask, a list. Linear rather than tiled so the CPU can write it in place.
//
// ONE texture, rewritten in place; an update racing a queued draw costs at most one frame showing the new data early.
//
// CEMU DOES NOT SEE A REWRITE ON ITS OWN. Its texture cache re-uploads a texture only when a hash of its memory changes,
// and for a small uncompressed texture that hash reads every 4th 32-bit word (LatteTexture_CalculateTextureDataHash) -
// GX2Invalidate only re-arms the check. An update that changes a texel the hash skips (one tile of a mask, say) is never
// uploaded: the pass keeps reading what the texture held when Cemu first saw it. So every row is allocated with at least
// four texels of padding past its width, which the shader never samples, and each update stamps a new counter into all of
// it - so every update changes words the hash reads. (Double-buffering failed the same way, once per buffer.)
constexpr uint32_t kPassDataPadding = 4;

struct ShaderPass {
    GX2Types::VertexShader* vs = nullptr;
    GX2Types::PixelShader* ps = nullptr;
    GX2Types::FetchShader fetch{};
    uint32_t dataVec4 = 0;      // float4 parameters streamed with each vertex: the vertex shader's attributes less its position
    bool used = false;

    TextureWrapper data{};
    uint32_t dataWidth = 0;     // fixed by the first SetShaderPassData: the texture comes from the arena, which never frees
    uint32_t dataHeight = 0;
    uint32_t dataStamp = 0;     // written into the row padding on every update, so Cemu's hash sees the change
    bool hasData = false;
};

inline ShaderPass g_Passes[kMaxShaderPasses];
inline BlurTarget g_PassScene;                // the frame as it was before the pass, sampled as texture 0
inline bool g_PassSceneTried = false;
inline uint32_t g_PassEvent = 0;                       // which HUD pass the pre-UI callbacks are running in
inline uint32_t g_PassSceneEvent = 0xFFFFFFFFu;        // the HUD pass whose frame the scene copy holds
inline bool g_PassSceneReady = false;
alignas(256) inline uint8_t g_PassVertices[kPassSlots * kPassSlotBytes];
inline size_t g_PassSlot = 0;

// The frame copy a pass samples. Full size would be most of the code-cave arena, which also holds the host's textures and
// every mod's memory, and the game's own heaps are not safe to render into from here, so it takes the largest of a few
// fractions of the frame that fits what the arena has free. A smaller copy softens the background inside the lens.
inline bool EnsurePassScene(const GX2Types::ColorBuffer* dst) {
    if (g_PassSceneTried) return g_PassSceneReady;
    g_PassSceneTried = true;

    constexpr uint32_t kFractions[][2] = {{1, 1}, {3, 4}, {1, 2}, {1, 3}, {1, 4}};
    constexpr uint32_t kHeadroom = 192u * 1024u;
    // The whole world is drawn through this copy wherever a pass shades it, so a small one is the world at a small
    // resolution. Which size the arena has room for depended on what else had loaded by the first frame (the GUI's
    // font archive is read into the arena at about the same moment), so the world came up at 1/3 resolution on some
    // boots. Rather than fall that far, 3/4 may come from the game's MEM2 heap - as AllocMEM1 does for anything the
    // arena cannot hold - provided the heap keeps a wide margin for the game.
    constexpr uint32_t kHeapMargin = 16u * 1024u * 1024u;
#if WIIXL_CEMU
    void* mem2 = WiiXLaunch::Mem::GetBaseHeapHandle(WiiXLaunch::Mem::BaseHeap::MEM2);
    const uint32_t heapFree = mem2 ? WiiXLaunch::Mem::GetAllocatableSize(mem2, 256) : 0u;
    BotW::OSLog("WiiXLaunch: shader pass scene copy - arena has %u free, the game's MEM2 heap %u\n",
                WiiXLaunch::Arena::Free(), heapFree);
#endif
    for (const auto& f : kFractions) {
        const uint32_t w = dst->surface.width * f[0] / f[1];
        const uint32_t h = dst->surface.height * f[0] / f[1];
#if WIIXL_CEMU
        const bool arenaFits = WiiXLaunch::Arena::Free() >= w * h * 4u + kHeadroom;
        const bool heapFits = f[0] == 3 && f[1] == 4 && heapFree >= w * h * 4u + kHeapMargin;
        if (!arenaFits && !heapFits) continue;
#endif
        // sRGB like the game's own target, so what the pass samples and what it writes are both linear.
        if (EnsureBlurTarget(g_PassScene, w, h, GX2Types::kSurfaceFormatSrgbR8G8B8A8)) {
            g_PassSceneReady = true;
            BotW::OSLog("WiiXLaunch: shader pass scene copy %ux%u (frame %ux%u)\n", w, h,
                        dst->surface.width, dst->surface.height);
            break;
        }
    }
    if (!g_PassSceneReady) BotW::OSLog("WiiXLaunch: shader pass scene copy: nothing fit\n");
    return g_PassSceneReady;
}

inline void* AllocPassBuffer(uint32_t size, uint32_t align) {
    return size ? AllocMEM1(size, align) : nullptr;
}

// ---------------------------------------------------------------------------
// The scene's depth buffer
// ---------------------------------------------------------------------------
// agl binds its render targets through one function, and a render buffer holds eight colour target pointers from +0x1c
// and the depth target at +0x3c; a target's GX2 buffer struct is at +0xbc. Many 1280x720 depth targets are bound in a
// frame, and only the FIRST full-resolution 3D bind holds the world: the sun-flare occlusion query binds a second one,
// near zero everywhere, which is why a depth taken "from the last bind" collapses the moment sky enters the frame.
// So the first one is latched for the frame and nothing later may take it. This is the rule the underwater mod found.
constexpr uintptr_t kAglBindRenderTargets = 0x03a76e18;
constexpr uint32_t kRbColorTargetOffset = 0x1c;
constexpr uint32_t kRbDepthTargetOffset = 0x3c;
constexpr uint32_t kRenderTargetBufferOffset = 0xbc;
constexpr uint32_t kSceneWidth = 1280;       // the game's own 3D resolution; a graphic pack's upscale happens below GX2
constexpr uint32_t kSceneHeight = 720;
constexpr uint32_t kTvMinWidth = 1000;       // the GamePad scan-out is 854x480; only the TV one ends a frame

inline GX2Types::DepthBuffer* g_SceneDepth = nullptr;
inline bool g_SceneDepthLatched = false;
// How often each stage runs, logged every few seconds. Scan-out fires on every vsync and a new frame is rendered far
// less often, which is the first thing to know about anything drawn at either point.
inline uint32_t g_StatScanOuts = 0;
inline uint32_t g_StatScenes = 0;
inline uint32_t g_StatLayouts = 0;
inline uint32_t g_StatHuds = 0;
inline uint32_t g_StatPreUi = 0;
inline uint32_t g_StatSkipDone = 0;
inline uint32_t g_StatSkipTarget = 0;
inline uint32_t g_StatSkipSize = 0;
inline uint32_t g_StatSizeLogs = 0;
inline TextureWrapper g_SceneDepthTex{};
// The colour buffer agl bound last, whatever it is: where the game's draws are
// landing right now. A draw made from inside the game's own drawing (a layout
// pane's, say) goes here, so it lands in the same image the game is building.
inline GX2Types::ColorBuffer* g_BoundColor = nullptr;

inline bool PlausibleTarget(uintptr_t p) { return p >= 0x10000000u && p < 0xE0000000u && (p & 3u) == 0; }

WIIXL_HOOK_DEFINE_TRAMPOLINE(AglBindRenderTargetsHook) {
    static void Callback(uintptr_t renderBuffer, uint32_t a2, uint32_t a3) {
        Orig(renderBuffer, a2, a3);
        if (PlausibleTarget(renderBuffer)) {
            const uintptr_t boundColor = *reinterpret_cast<uintptr_t*>(renderBuffer + kRbColorTargetOffset);
            auto* bound = PlausibleTarget(boundColor)
                ? reinterpret_cast<GX2Types::ColorBuffer*>(boundColor + kRenderTargetBufferOffset) : nullptr;
            g_BoundColor = bound && bound->surface.image ? bound : nullptr;
        }
        if (g_SceneDepthLatched || !PlausibleTarget(renderBuffer)) return;

        const uintptr_t colorTarget = *reinterpret_cast<uintptr_t*>(renderBuffer + kRbColorTargetOffset);
        const uintptr_t depthTarget = *reinterpret_cast<uintptr_t*>(renderBuffer + kRbDepthTargetOffset);
        if (!PlausibleTarget(colorTarget) || !PlausibleTarget(depthTarget)) return;

        auto* color = reinterpret_cast<GX2Types::ColorBuffer*>(colorTarget + kRenderTargetBufferOffset);
        auto* depth = reinterpret_cast<GX2Types::DepthBuffer*>(depthTarget + kRenderTargetBufferOffset);
        if (!color->surface.image || !depth->surface.image) return;
        if (color->surface.width != kSceneWidth || color->surface.height != kSceneHeight) return;
        if (depth->surface.width != kSceneWidth || depth->surface.height != kSceneHeight) return;

        g_SceneDepth = depth;
        g_SceneDepthLatched = true;
        ++g_StatScenes;

        static uintptr_t s_lastLogged = 0;
        static uint32_t s_logs = 0;
        if (reinterpret_cast<uintptr_t>(depth) != s_lastLogged && s_logs < 6) {
            s_lastLogged = reinterpret_cast<uintptr_t>(depth);
            ++s_logs;
            BotW::OSLog("WiiXLaunch: scene depth %p: format %u, tile mode %u, %ux%u, pitch %u, use %u, image %p, %u bytes\n",
                        depth, depth->surface.format, depth->surface.tileMode, depth->surface.width, depth->surface.height,
                        depth->surface.pitch, depth->surface.use, depth->surface.image, depth->surface.imageSize);
        }
    }
};

// Describes the latched depth buffer as a texture a pass can sample. Point filtered and clamped: interpolating depth
// across a silhouette gives positions that lie on no surface.
inline bool AliasSceneDepth() {
    if (!g_SceneDepth || !g_SceneDepth->surface.image) return false;
    g_SceneDepthTex.texture.surface = g_SceneDepth->surface;
    g_SceneDepthTex.texture.surface.use |= GX2Types::kSurfaceUseTexture;
    g_SceneDepthTex.texture.viewFirstMip = 0;
    g_SceneDepthTex.texture.viewNumMips = 1;
    g_SceneDepthTex.texture.viewFirstSlice = 0;
    g_SceneDepthTex.texture.viewNumSlices = 1;
    g_SceneDepthTex.texture.compMap = GX2Types::kCompMapRGBA;
    InitTextureRegs()(&g_SceneDepthTex.texture);
    InitSampler()(&g_SceneDepthTex.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexXYFilterModePoint);
    InitSamplerClamping()(&g_SceneDepthTex.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp,
                          GX2Types::kTexClampModeClamp);
    return true;
}

// ---------------------------------------------------------------------------
// Drawing before the UI
// ---------------------------------------------------------------------------
// Scan-out is after the HUD is composited, so an effect drawn there lies over hearts and menus. GameScene_Layout
// (0x0304a898) is the game's HUD-compositing draw delegate: with pass 8 and step 1 the 3D scene is finished and
// post-processed and the UI has not been drawn. A draw there goes through a context state the host owns, because
// state written into the game's would be the game's next draw's problem.
//
// That context is built at scan-out and never anywhere else. GX2SetupContextStateEx writes default GPU state, so
// calling it mid-frame lands on whatever the engine had configured, and the engine's next draw runs against state it
// never set - a crash on boot, which moved around as the call site did.
constexpr uintptr_t kGameSceneLayout = 0x0304a898;
constexpr int32_t kLayoutPassHud = 8;
constexpr int32_t kLayoutStepDraw = 1;
constexpr uint32_t kContextStateSize = 0xA100;   // sizeof(GX2ContextState), fixed by the ABI

inline void* g_PassContext = nullptr;
inline bool g_PassContextTried = false;
inline DrawCallback g_PreUiCallbacks[kMaxCallbacks]{};
inline size_t g_PreUiCallbackCount = 0;
inline bool g_PreUiDoneThisFrame = false;

inline void EnsurePassContext() {
    if (g_PassContextTried) return;
    g_PassContextTried = true;

    void* game = GetContextState();
    void* ctx = AllocMEM1(kContextStateSize, 256);
    if (!ctx || !game) {
        BotW::OSLog("WiiXLaunch: pre-UI draw unavailable - no memory for a private context state\n");
        return;
    }
    SetupContextStateEx()(ctx, 0);
    // Setting a context up writes default state; the engine's own goes straight back.
    SetContextState()(game);
    g_PassContext = ctx;
    BotW::OSLog("WiiXLaunch: private GX2 context ready - pre-UI draw armed\n");
}

// Everything a pass needs that must exist before it first draws mid-frame, built at scan-out.
inline void PrimePassResources(const GX2Types::ColorBuffer* tv) {
    bool anyPass = false;
    for (const ShaderPass& p : g_Passes) anyPass = anyPass || p.used;
    if (!anyPass || g_PreUiCallbackCount == 0) return;
    EnsurePassScene(tv);
    EnsurePassContext();
}

// The colour target of the layout pass being drawn right now (a draw step of the game-scene layout callback), or null
// outside one. What a draw from inside the game's UI should land in: the last target agl bound is not it - the UI binds
// small targets of its own mid-pass (100x20 was seen) and goes back to the pass's target without that bind being seen.
inline GX2Types::ColorBuffer* g_LayoutPassColor = nullptr;

WIIXL_HOOK_DEFINE_TRAMPOLINE(GameSceneLayoutHook) {
    static void Callback(void* userPtr, int* step) {
        ++g_StatLayouts;
        const bool hud = step && step[5] == kLayoutPassHud && step[0] == kLayoutStepDraw;
        if (hud) ++g_StatHuds;

        // Every HUD pass, not only those that follow a freshly rendered scene: the game composites about twice per scene
        // and each composite is its own image, so a lens drawn into one is not in the next.
        if (hud && g_PassContext && g_PreUiCallbackCount) {
            if (g_PreUiDoneThisFrame) {
                ++g_StatSkipDone;
            } else {
                const uintptr_t renderBuffer = static_cast<uintptr_t>(static_cast<uint32_t>(step[3]));
                const uintptr_t colorTarget = PlausibleTarget(renderBuffer)
                    ? *reinterpret_cast<uintptr_t*>(renderBuffer + kRbColorTargetOffset) : 0;
                auto* color = PlausibleTarget(colorTarget)
                    ? reinterpret_cast<GX2Types::ColorBuffer*>(colorTarget + kRenderTargetBufferOffset) : nullptr;

                if (!color || !color->surface.image) {
                    ++g_StatSkipTarget;
                } else if (color->surface.width < kTvMinWidth) {
                    // The GamePad's HUD pass, or something that is not the TV.
                    ++g_StatSkipSize;
                    if (g_StatSizeLogs < 4) {
                        ++g_StatSizeLogs;
                        BotW::OSLog("WiiXLaunch: HUD pass skipped, target %ux%u\n", color->surface.width, color->surface.height);
                    }
                } else {
                    void* game = GetContextState();
                    g_ContextOverride = g_PassContext;
                    ++g_PassEvent;
                    for (size_t i = 0; i < g_PreUiCallbackCount; ++i) {
                        if (g_PreUiCallbacks[i]) {
                            g_PreUiCallbacks[i](nullptr, color, static_cast<int>(color->surface.width),
                                                static_cast<int>(color->surface.height));
                        }
                    }
                    g_ContextOverride = nullptr;
                    if (game) SetContextState()(game);
                    g_PreUiDoneThisFrame = true;
                    ++g_StatPreUi;
                }
            }
        }
        GX2Types::ColorBuffer* const outer = g_LayoutPassColor;
        if (step && step[0] == kLayoutStepDraw) {
            const uintptr_t rb = static_cast<uintptr_t>(static_cast<uint32_t>(step[3]));
            const uintptr_t target = PlausibleTarget(rb) ? *reinterpret_cast<uintptr_t*>(rb + kRbColorTargetOffset) : 0;
            auto* color = PlausibleTarget(target)
                ? reinterpret_cast<GX2Types::ColorBuffer*>(target + kRenderTargetBufferOffset) : nullptr;
            g_LayoutPassColor = color && color->surface.image ? color : nullptr;
        }
        Orig(userPtr, step);
        g_LayoutPassColor = outer;
    }
};

// ---------------------------------------------------------------------------
// Drawing under the HUD
// ---------------------------------------------------------------------------
// The swap hook above sees the finished frame, HUD and menus included, so
// anything drawn there covers them - a full-screen effect blurs the hearts.
//
// The HUD is the agl layer "ForMainUI" (render list id 8, created in
// FUN_02c490a8 beside "TV_0", the world). Its layer class (ctor 0x033d3a84)
// pushes a draw method before the layout ("ParticleBeforeLayout") and one
// after; both run 0x033d40b4, which tail-calls 0x033d3ee0(layer, group,
// renderInfo) with group 0xa before the layout and 0xb after. renderInfo[5]
// is the render list id - 0x033d3ee0 itself checks it against 8 and 0xc. So
// 0x033d3ee0 with group 0xa and id 8 runs once per frame, after the world and
// its post effects are in the TV buffer and before a single HUD or menu pane.
// (0x033d40b4 itself cannot be hooked: its displaced prologue holds a bne.)
//
// Drawing there is drawing in the middle of the game's frame, so it gets a
// context state of its own: our register writes go to that state's shadow,
// and switching back to the game's state reloads the game's registers as the
// HUD expects them.
constexpr uintptr_t kHudLayerParticleDraw = 0x033d3ee0;
constexpr int32_t   kHudGroupBeforeLayout = 0xa;
constexpr uint32_t  kMainHudLayerId       = 8;
// kContextStateSize is the one above, beside kGameSceneLayout.
constexpr uint32_t  kRenderInfoScanWords  = 16;

inline void* g_PreHudContextState = nullptr;
inline bool g_PreHudBroken = false;

// MEM2 as the title sees it runs well past 0x50000000 - BotW's render
// buffers sit around 0x61000000 and its cameras near 0x6b000000 - so this
// takes the same range botw.camera's Plausible does, plus MEM1.
inline bool PlausibleGamePtr(uintptr_t p) {
    return (p & 3u) == 0 &&
           ((p >= 0x10000000u && p < 0xA0000000u) || (p >= 0xF4000000u && p < 0xF6000000u));
}

// agl::lyr::RenderInfo, from its constructor in the Switch build
// (RenderInfo(DrawContext*, int, FrameworkType, const RenderBuffer*, bool,
// const Layer*) at 0x7100b63c88) narrowed to 32-bit pointers:
//   +0x00 draw method index   +0x04 display type   +0x08 framework type (u8)
//   +0x0c RenderBuffer*       +0x10 Layer*         +0x14 render list id
//   +0x18 Camera*             +0x1c Projection*    +0x20 Viewport*
//   +0x24 bool                +0x28 DrawContext*
// The id at +0x14 and the projection at +0x1c are the two words 0x033d3ee0
// itself reads, which is what pins the narrowing down.
//
constexpr uint32_t kRenderInfoRenderBuffer = 3;

// Where agl::RenderBuffer keeps its first colour target (a RenderTargetColor*,
// whose GX2ColorBuffer is at +0xbc) is not RE'd for Wii U, and +0x1c - what
// the swap hook reads off ITS argument - is not it. So it is found: the first
// word of the RenderBuffer pointing at something with a believable colour
// buffer at +0xbc. Measured once, then fixed.
constexpr uint32_t kRenderBufferScanWords = 64;
inline int32_t g_ColorTargetWord = -1;

inline bool SaneColorBuffer(const GX2Types::ColorBuffer* cb) {
    return cb && PlausibleGamePtr(reinterpret_cast<uintptr_t>(cb->surface.image)) &&
           cb->surface.width >= 64 && cb->surface.width <= 8192 &&
           cb->surface.height >= 64 && cb->surface.height <= 8192 &&
           cb->surface.dim == GX2Types::kSurfaceDimTexture2D;
}

inline GX2Types::ColorBuffer* TargetColor(uintptr_t target) {
    if (!PlausibleGamePtr(target)) return nullptr;
    auto* cb = reinterpret_cast<GX2Types::ColorBuffer*>(target + 0xbc);
    return SaneColorBuffer(cb) ? cb : nullptr;
}

inline GX2Types::ColorBuffer* HudColorBuffer(const uint32_t* info) {
    const uintptr_t rb = info[kRenderInfoRenderBuffer];
    if (!PlausibleGamePtr(rb)) {
        static uint32_t s_bad = 0;
        if ((s_bad++ % 600) == 0) {
            BotW::OSLog("WiiXLaunch: pre-HUD draw - renderInfo +0x0c is %08x, not a RenderBuffer\n",
                        static_cast<unsigned int>(rb));
        }
        return nullptr;
    }
    const uint32_t* words = reinterpret_cast<const uint32_t*>(rb);

    if (g_ColorTargetWord < 0) {
        for (uint32_t i = 0; i < kRenderBufferScanWords; ++i) {
            if (GX2Types::ColorBuffer* cb = TargetColor(words[i])) {
                g_ColorTargetWord = static_cast<int32_t>(i);
                BotW::OSLog("WiiXLaunch: pre-HUD draw - RenderBuffer %08x +0x%x is the colour target; "
                            "drawing into %p (%ux%u, format 0x%x)\n", static_cast<unsigned int>(rb),
                            i * 4, cb, cb->surface.width, cb->surface.height, cb->surface.format);
                break;
            }
        }
        if (g_ColorTargetWord < 0) {
            // Said now and then rather than once: the first frames are a boot
            // screen, and a buffer that only appears later is not a failure.
            static uint32_t s_misses = 0;
            if ((s_misses++ % 600) == 0) {
                BotW::OSLog("WiiXLaunch: pre-HUD draw - no colour target found in RenderBuffer %08x yet\n",
                            static_cast<unsigned int>(rb));
            }
            return nullptr;
        }
    }
    return TargetColor(words[g_ColorTargetWord]);
}

// The private context state is made at the END of a frame, from the swap
// hook, never from inside the HUD layer. GX2SetupContextStateEx records the
// state's defaults through a display list of its own, and the HUD layer is
// drawn while agl is recording the game's display list for it: setting up
// there nested the two, and Cemu was left executing a command buffer 1 GB
// long. It hung on the loading screen logging command-buffer errors.
inline void EnsurePreHudContextState() {
    if (g_PreHudContextState || g_PreHudBroken) return;
    void* gameState = GetContextState();
    if (!gameState) return;

    g_PreHudContextState = AllocMEM1(kContextStateSize, 256);
    if (!g_PreHudContextState) {
        g_PreHudBroken = true;
        BotW::OSLog("WiiXLaunch: pre-HUD draw - no memory for a context state; disabled\n");
        return;
    }
    SetupContextStateEx()(g_PreHudContextState, 0);
    SetContextState()(gameState);
    BotW::OSLog("WiiXLaunch: pre-HUD draw - private context state ready at %p\n", g_PreHudContextState);
}

// A private context state for drawing from inside the game's layout draw
// (gui/map_layer.hpp). Made at scan-out, for the reason above; asked for by
// setting g_MidLayoutContextWanted.
inline void* g_MidLayoutContext = nullptr;
inline bool g_MidLayoutContextWanted = false;
inline bool g_MidLayoutContextFailed = false;

inline void EnsureMidLayoutContext() {
    if (g_MidLayoutContext || g_MidLayoutContextFailed || !g_MidLayoutContextWanted) return;
    void* gameState = GetContextState();
    if (!gameState) return;
    g_MidLayoutContext = AllocMEM1(kContextStateSize, 256);
    if (!g_MidLayoutContext) {
        g_MidLayoutContextFailed = true;
        BotW::OSLog("WiiXLaunch: mid-layout draw - no memory for a context state; disabled\n");
        return;
    }
    if (!g_LateRing) g_LateRing = static_cast<uint8_t*>(AllocMEM1(kLateRingBytes, 256));
    SetupContextStateEx()(g_MidLayoutContext, 0);
    SetContextState()(gameState);
    BotW::OSLog("WiiXLaunch: mid-layout draw - private context state ready at %p\n", g_MidLayoutContext);
}

inline void RunPreHud(const uint32_t* info) {
    if (g_PreHudBroken || !g_SpritePipelineReady || !g_PreHudContextState) return;

    GX2Types::ColorBuffer* dst = HudColorBuffer(info);
    if (!dst || !dst->surface.image) return;

    void* gameState = GetContextState();
    if (!gameState) return;

    const uint32_t width = dst->surface.width ? dst->surface.width : 1280;
    const uint32_t height = dst->surface.height ? dst->surface.height : 720;

    // Every draw path starts with SetContextState(GetContextState()), so it
    // switches to the private state itself - and only if it draws. A frame
    // where the callbacks draw nothing sends no commands at all.
    g_ContextOverride = g_PreHudContextState;
    g_ContextOverrideUsed = false;
    for (size_t i = 0; i < g_PreHudCallbackCount; ++i) {
        if (g_PreHudCallbacks[i]) {
            g_PreHudCallbacks[i](nullptr, dst, static_cast<int>(width), static_cast<int>(height));
        }
    }
    g_ContextOverride = nullptr;
    if (g_ContextOverrideUsed) SetContextState()(gameState);
}

WIIXL_HOOK_DEFINE_TRAMPOLINE(HudLayerDrawHook) {
    static void Callback(uintptr_t layer, int32_t group, const uint32_t* info) {
        if (group == kHudGroupBeforeLayout && info && info[5] == kMainHudLayerId &&
            g_PreHudCallbackCount) {
            RunPreHud(info);
        }
        Orig(layer, group, info);
    }
};

WIIXL_HOOK_DEFINE_TRAMPOLINE(AglCopyToScanBufferHook) {
    static void Callback(uintptr_t renderBuffer, uintptr_t param2, int32_t param3) {
            if (renderBuffer) {
            uintptr_t rt = *reinterpret_cast<uintptr_t*>(renderBuffer + 0x1c);
            if (rt) {
                auto* tvColorBuffer = reinterpret_cast<GX2Types::ColorBuffer*>(rt + 0xbc);
                if (tvColorBuffer && tvColorBuffer->surface.image) {
                    InitializeAllPipelines();
                    if (g_PreHudCallbackCount) EnsurePreHudContextState();
                    EnsureMidLayoutContext();

                    uint32_t width = tvColorBuffer->surface.width ? tvColorBuffer->surface.width : 1280;
                    uint32_t height = tvColorBuffer->surface.height ? tvColorBuffer->surface.height : 720;

                    g_DepthClearedThisFrame = false;
                    BatchNewFrame();
                    if (width >= kTvMinWidth) PrimePassResources(tvColorBuffer);

                    static uint32_t s_count = 0;
                    if ((s_count++ % 300) == 0) {
                        BotW::OSLog("WiiXLaunch: Injected frame #%u (dst=%p, %ux%u, dstFormat=0x%x, cbCount=%u)\n",
                            s_count, tvColorBuffer, width, height, tvColorBuffer->surface.format,
                            static_cast<unsigned int>(g_DrawCallbackCount));
                    }

                    for (size_t i = 0; i < g_DrawCallbackCount; ++i) {
                        if (g_DrawCallbacks[i]) {
                            g_DrawCallbacks[i](nullptr, tvColorBuffer, static_cast<int>(width), static_cast<int>(height));
                        }
                    }

                    // THE FRAME SOURCE base cannot have. Base does not know GX2
                    // exists, so it owns the tick registry and this supplies the
                    // frame - the same nomination pattern as the load point.
                    //
                    // After the host's own draw callbacks, so a mod's tick sees a
                    // frame the host has already drawn into rather than racing it.
                    WiiXLaunch::Tick::RunAll();

                    // The next frame's first scene bind claims the depth again. Only the TV scan-out ends a frame:
                    // the GamePad's comes from the same one.
                    if (width >= kTvMinWidth) {
                        g_SceneDepthLatched = false;
                        g_PreUiDoneThisFrame = false;
                        if ((++g_StatScanOuts % 1200u) == 0) {
                            BotW::OSLog("WiiXLaunch: per 1200 scan-outs: %u scenes rendered, %u layout events, %u HUD passes, %u pre-UI draws (skipped: %u already drawn, %u no target, %u not the TV)\n",
                                        g_StatScenes, g_StatLayouts, g_StatHuds, g_StatPreUi, g_StatSkipDone,
                                        g_StatSkipTarget, g_StatSkipSize);
                            g_StatScenes = g_StatLayouts = g_StatHuds = g_StatPreUi = 0;
                            g_StatSkipDone = g_StatSkipTarget = g_StatSkipSize = 0;
                        }
                    }
                }
            }
        }

        Orig(renderBuffer, param2, param3);
    }
};

} // namespace impl

// ---------------------------------------------------------------------------
// High-Level Public API (Matches NVN.hpp)
// ---------------------------------------------------------------------------

inline void Init() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    BotW::OSLog("WiiXLaunch: GX2::Init() calling Install on 0x03a75d48\n");
    impl::AglCopyToScanBufferHook::Install(0, 0x03a75d48);
    impl::AglBindRenderTargetsHook::Install(0, impl::kAglBindRenderTargets);
    impl::GameSceneLayoutHook::Install(0, impl::kGameSceneLayout);
    BotW::OSLog("WiiXLaunch: GX2::Init() hook installed successfully\n");

    // Tell base where a frame happens. It cannot know - a frame is a graphics
    // idea and base does not know graphics exists - so a game module nominates
    // one, exactly as it nominates the load point. A host with no game module
    // nominates nothing, and Tick::LogState says so rather than leaving a mod
    // that registered a callback to fail silently.
    WiiXLaunch::Tick::NominateSource("botw GX2 swap, after the host draw callbacks");
#if WIIXL_CEMU
    // The heap this payload has to live in, stated once. It is the tail of our
    // code cave up to the end of Cemu's code-cave area, and it is small - a
    // couple of 1024x1024 font sheets and a render target will eat it.
    // No width specifiers below, and the reason recorded here used to be wrong.
    // This blamed Cemu's OSReport, but OSLog never hands it a width specifier:
    // it formats with WiiXLaunch::Debug::FormatText first and passes the
    // finished string as osReport("%s\n", text). The limitation was FormatText's
    // own - it parsed .precision but not width, so "%08x" fell through to its
    // default branch, which echoed the characters and consumed no argument,
    // leaving every later specifier reading the previous slot. Fixed in
    // debug_log.hpp; width and zero-padding work now.
    BotW::OSLog("WiiXLaunch: arena %p..%p, %u KB\n",
                reinterpret_cast<void*>(WiiXLaunch::Arena::Base()),
                reinterpret_cast<void*>(WiiXLaunch::Backend::kCemuCodeCaveEnd),
                WiiXLaunch::Arena::Total() / 1024u);
#endif
}

inline void RegisterDrawCallback(DrawCallback cb) {
    if (impl::g_DrawCallbackCount < impl::kMaxCallbacks) {
        impl::g_DrawCallbacks[impl::g_DrawCallbackCount++] = cb;
    }
}

// Like RegisterDrawCallback, but `cb` runs before the HUD and menus are drawn,
// so what it draws sits under them. Runs on the TV image only, inside a
// private context state (see impl::RunPreHud); the swap callbacks still need
// Init() for the pipelines and the frame tick.
inline void RegisterPreHudDrawCallback(DrawCallback cb) {
    static bool installed = false;
    if (!installed) {
        installed = true;
        impl::HudLayerDrawHook::Install(0, impl::kHudLayerParticleDraw);
        BotW::OSLog("WiiXLaunch: GX2 pre-HUD draw hooked at 0x%08x\n",
                    static_cast<unsigned int>(impl::kHudLayerParticleDraw));
    }
    if (impl::g_PreHudCallbackCount < impl::kMaxCallbacks) {
        impl::g_PreHudCallbacks[impl::g_PreHudCallbackCount++] = cb;
    }
}

inline void OnInitialized(InitCallback cb) {
    if (impl::g_PipelineInitialized) {
        cb();
    } else if (impl::g_InitCallbackCount < impl::kMaxCallbacks) {
        impl::g_InitCallbacks[impl::g_InitCallbackCount++] = cb;
    }
}

inline Device* GetDevice() {
    return nullptr;
}

inline void* GetGraphicsContext() {
    return impl::GetGraphicsContext();
}

inline void* AllocMEM1(uint32_t size, uint32_t align = 256) {
    return impl::AllocMEM1(size, align);
}

inline TextureHandle CreateTexture(
    const void* rgbaBytes,
    size_t size,
    int width = 0,
    int height = 0,
    int format = 0)
{
    if (!rgbaBytes || impl::g_TextureCount >= impl::kMaxTextures || width <= 0 || height <= 0) return 0;

    auto& wrap = impl::g_Textures[impl::g_TextureCount++];
    wrap.texture.surface.dim = GX2Types::kSurfaceDimTexture2D;
    wrap.texture.surface.width = static_cast<uint32_t>(width);
    wrap.texture.surface.height = static_cast<uint32_t>(height);
    wrap.texture.surface.depth = 1;
    wrap.texture.surface.mipLevels = 1;
    // Format 0 = sRGB RGBA8 (default for artwork); pass explicit format for linear data.
    wrap.texture.surface.format = format ? static_cast<uint32_t>(format)
                                         : GX2Types::kSurfaceFormatSrgbR8G8B8A8;
    wrap.texture.surface.aa = GX2Types::kAaMode1x;
    wrap.texture.surface.use = GX2Types::kSurfaceUseTexture;
    wrap.texture.surface.tileMode = GX2Types::kTileModeTiled1DThin1;

    impl::CalcSurfaceSizeAndAlignment()(&wrap.texture.surface);

    wrap.texture.surface.image = impl::AllocMEM1(wrap.texture.surface.imageSize, wrap.texture.surface.alignment);
    if (!wrap.texture.surface.image) {
        BotW::OSLog("WiiXLaunch: GX2 CreateTexture AllocMEM1 failed (%u bytes)\n", wrap.texture.surface.imageSize);
        return 0;
    }

    auto* src32 = reinterpret_cast<const uint32_t*>(rgbaBytes);
    auto* dst32 = reinterpret_cast<uint32_t*>(wrap.texture.surface.image);
    uint32_t pitch = wrap.texture.surface.pitch;
    uint32_t tilesPerRow = pitch >> 3;

    for (uint32_t y = 0; y < static_cast<uint32_t>(height); ++y) {
        uint32_t ty = y >> 3;
        uint32_t by = y & 7;
        for (uint32_t x = 0; x < static_cast<uint32_t>(width); ++x) {
            uint32_t tx = x >> 3;
            uint32_t bx = x & 7;
            // GX2 ADDR_DISPLAYABLE micro-tile bit order: x0,x1,y0,x2,y1,y2 (see addrlib).
            uint32_t x0 = bx & 1, x1 = (bx >> 1) & 1, x2 = (bx >> 2) & 1;
            uint32_t y0 = by & 1, y1 = (by >> 1) & 1, y2 = (by >> 2) & 1;
            uint32_t elem = x0 | (x1 << 1) | (y0 << 2) | (x2 << 3) | (y1 << 4) | (y2 << 5);
            uint32_t dstIdx = (ty * tilesPerRow + tx) * 64 + elem;
            dst32[dstIdx] = src32[y * width + x];
        }
    }

    wrap.texture.viewFirstMip = 0;
    wrap.texture.viewNumMips = 1;
    wrap.texture.viewFirstSlice = 0;
    wrap.texture.viewNumSlices = 1;
    wrap.texture.compMap = 0x00010203;

    impl::InitTextureRegs()(&wrap.texture);
    impl::Invalidate()(GX2Types::kInvalidateModeCpuTexture, wrap.texture.surface.image, wrap.texture.surface.imageSize);

    impl::InitSampler()(&wrap.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexXYFilterModeLinear);
    impl::InitSamplerClamping()(&wrap.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp);

    WIIXL_LOG("WiiXLaunch: GX2 CreateTexture OK (%dx%d, pitch=%u, img=%p, handle=%p)",
        width, height, wrap.texture.surface.pitch, wrap.texture.surface.image, &wrap);

    return reinterpret_cast<TextureHandle>(&wrap);
}

// ---------------------------------------------------------------------------
// Pre-tiled surfaces (the game's own BFLIM / BFFNT art)
// ---------------------------------------------------------------------------
// CreateTexture takes linear RGBA8 and tiles it on the CPU. The game's UI
// art is already a finished GX2 surface (tiled, BCn-compressed, with the tile
// mode and swizzle recorded next to it), so it is uploaded as-is and the GPU
// addresses it exactly as it does for the game. Nothing is decoded.
struct SurfaceDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = GX2Types::kSurfaceFormatUnormR8G8B8A8;  // GX2 surface format
    uint32_t tileMode = GX2Types::kTileModeTiled2DThin1;
    uint32_t swizzle = 0;                                     // GX2Surface::swizzle (bank/pipe bits at 8..10)
    uint32_t compMap = GX2Types::kCompMapRGBA;
    bool linearFilter = true;
};

// Sets the surface up and allocates its image memory, returning where to
// put the tiled bytes (and how many are expected). Call FinalizeTexture once
// they are in place. Lets a streaming loader write straight into GPU memory.
inline TextureHandle AllocTextureSurface(const SurfaceDesc& desc, void** outImage, uint32_t* outImageSize) {
    if (impl::g_TextureCount >= impl::kMaxTextures || desc.width == 0 || desc.height == 0) return 0;

    auto& wrap = impl::g_Textures[impl::g_TextureCount];
    wrap = impl::TextureWrapper{};
    wrap.texture.surface.dim = GX2Types::kSurfaceDimTexture2D;
    wrap.texture.surface.width = desc.width;
    wrap.texture.surface.height = desc.height;
    wrap.texture.surface.depth = 1;
    wrap.texture.surface.mipLevels = 1;
    wrap.texture.surface.format = desc.format;
    wrap.texture.surface.aa = GX2Types::kAaMode1x;
    wrap.texture.surface.use = GX2Types::kSurfaceUseTexture;
    wrap.texture.surface.tileMode = desc.tileMode;
    wrap.texture.surface.swizzle = desc.swizzle;

    impl::CalcSurfaceSizeAndAlignment()(&wrap.texture.surface);
    if (wrap.texture.surface.imageSize == 0) return 0;

    wrap.texture.surface.image = impl::AllocMEM1(wrap.texture.surface.imageSize, wrap.texture.surface.alignment);
    if (!wrap.texture.surface.image) {
        BotW::OSLog("WiiXLaunch: GX2 AllocTextureSurface alloc failed (%u bytes)\n", wrap.texture.surface.imageSize);
        return 0;
    }

    wrap.texture.viewFirstMip = 0;
    wrap.texture.viewNumMips = 1;
    wrap.texture.viewFirstSlice = 0;
    wrap.texture.viewNumSlices = 1;
    wrap.texture.compMap = desc.compMap;

    impl::InitSampler()(&wrap.sampler, GX2Types::kTexClampModeClamp,
                        desc.linearFilter ? GX2Types::kTexXYFilterModeLinear : 0);
    impl::InitSamplerClamping()(&wrap.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp);

    impl::g_TextureCount++;
    if (outImage) *outImage = wrap.texture.surface.image;
    if (outImageSize) *outImageSize = wrap.texture.surface.imageSize;
    return reinterpret_cast<TextureHandle>(&wrap);
}

inline void FinalizeTexture(TextureHandle handle) {
    if (!handle) return;
    auto* wrap = reinterpret_cast<impl::TextureWrapper*>(handle);
    impl::InitTextureRegs()(&wrap->texture);
    impl::Invalidate()(GX2Types::kInvalidateModeCpuTexture, wrap->texture.surface.image, wrap->texture.surface.imageSize);
}

// One-shot: allocate, copy the tiled bytes in, finalize. `dataSize` must be
// at least the surface's computed image size (it is logged if it is not, so
// a wrong tile mode/swizzle guess shows up as a size mismatch).
inline TextureHandle CreateTextureFromSurface(const SurfaceDesc& desc, const void* data, uint32_t dataSize) {
    if (!data) return 0;
    void* image = nullptr;
    uint32_t imageSize = 0;
    TextureHandle handle = AllocTextureSurface(desc, &image, &imageSize);
    if (!handle) return 0;
    if (dataSize < imageSize) {
        BotW::OSLog("WiiXLaunch: GX2 CreateTextureFromSurface %ux%u fmt=0x%x tile=%u: have %u bytes, surface wants %u - rejecting\n",
                    desc.width, desc.height, desc.format, desc.tileMode, dataSize, imageSize);
        // The slot is spent (this module never frees) but the handle is not handed out.
        return 0;
    }
    memcpy(image, data, imageSize);
    FinalizeTexture(handle);
    return handle;
}

inline bool GetTextureSize(TextureHandle handle, uint32_t& width, uint32_t& height) {
    if (!handle) return false;
    auto* wrap = reinterpret_cast<impl::TextureWrapper*>(handle);
    width = wrap->texture.surface.width;
    height = wrap->texture.surface.height;
    return true;
}

// ---------------------------------------------------------------------------
// Batched quads
// ---------------------------------------------------------------------------
// BeginBatch(dst) once per draw callback, then any number of BatchQuad calls
// (vertices in NDC, the same TL/TR/BL/BR order and per-vertex UV+colour as
// DrawSprite's strip), then EndBatch. Quads for the same texture in a row
// share one draw call. See impl::BatchFlush for the ring-buffer rules.
inline void BeginBatch(void* dstTexture) {
    if (!impl::g_SpritePipelineReady || impl::g_BatchActive) return;
    auto* colorBuf = reinterpret_cast<GX2Types::ColorBuffer*>(dstTexture);
    if (!colorBuf || !colorBuf->surface.image) return;
    void* contextState = impl::GetContextState();
    if (!contextState) return;

    if (!impl::EnsureBatchRing()) return;

    uint32_t dispW = colorBuf->surface.width ? colorBuf->surface.width : 1280;
    uint32_t dispH = colorBuf->surface.height ? colorBuf->surface.height : 720;

    impl::SetContextState()(contextState);
    impl::SetShaderModeEx()(GX2Types::kShaderModeUniformRegister, 0x30, 0x40, 0x0, 0x0, 0xc8, 0xc0);
    impl::SetColorBuffer()(colorBuf, 0);
    impl::SetTargetChannelMasks()(0x0F, 0, 0, 0, 0, 0, 0, 0);
    impl::SetViewport()(0.0f, 0.0f, static_cast<float>(dispW), static_cast<float>(dispH), 0.0f, 1.0f);
    impl::SetScissor()(0, 0, dispW, dispH);
    impl::SetDepthOnlyControl()(0, 0, GX2Types::kCompareFuncAlways);
    impl::SetCullOnlyControl()(GX2Types::kFrontFaceCcw, 0, 0);
    impl::SetColorControl()(GX2Types::kLogicOpCopy, 1, 0, 1);
    impl::SetFetchShader()(&impl::g_SpriteFetchShader);
    impl::SetVertexShader()(impl::g_SpriteVertexShader);
    impl::SetPixelShader()(impl::g_SpritePixelShader);

    impl::g_BatchTexture = nullptr;
    impl::g_BatchVertCount = 0;
    impl::g_BatchBlend = Blend::Alpha;
    impl::g_BatchBlendValid = false;   // force the first flush to program the blend registers
    impl::g_BatchActive = true;
}

inline void BatchQuad(TextureHandle textureHandle, const TextureVertex verts[4],
                      const BlendState& blend = Blend::Alpha) {
    if (!impl::g_BatchActive || !textureHandle || !verts) return;
    auto* wrap = reinterpret_cast<impl::TextureWrapper*>(textureHandle);
    if (wrap != impl::g_BatchTexture || blend != impl::g_BatchBlend ||
        impl::g_BatchVertCount + 6 > impl::kBatchMaxQuadsPerFlush * 6) {
        impl::BatchFlush();
        impl::g_BatchTexture = wrap;
        impl::g_BatchBlend = blend;
    }
    TextureVertex* out = impl::g_BatchVerts + impl::g_BatchVertCount;
    out[0] = verts[0]; out[1] = verts[1]; out[2] = verts[2];
    out[3] = verts[2]; out[4] = verts[1]; out[5] = verts[3];
    impl::g_BatchVertCount += 6;
}

inline void EndBatch() {
    if (!impl::g_BatchActive) return;
    impl::BatchFlush();
    // Leave blending as a batch found it: ordinary alpha. The state is the
    // game's own when the batch is drawn at scan-out, and the game does not
    // always set blending before it draws - a batch ending on additive quads
    // (a mod's glow) left the game rendering additively during a loading
    // screen, and the water it prepared there came out broken for the session.
    if (impl::g_BatchBlendValid && !(impl::g_BatchBlendApplied == Blend::Alpha)) impl::ApplyBlend(Blend::Alpha);
    impl::g_BatchTexture = nullptr;
    impl::g_BatchActive = false;
}


// ---------------------------------------------------------------------------
// Backdrop blur
// ---------------------------------------------------------------------------
// Captures the frame as it stands and blurs it, leaving the result in a
// texture that can be drawn like any other sprite - the frosted glass BotW
// puts behind its windows. See impl's comment above for how it works and what
// it does not promise.
//
// Call it from a draw callback BEFORE drawing anything of your own, since it
// samples the colour buffer and retargets rendering while it runs. Returns 0
// if the targets could not be made.
//
// `downscale` is how much smaller the working copy is than the screen (4 is a
// good default: cheap, and the downsample is itself most of the blur), and
// `passes` is how many extra four-tap passes to run over it.
inline TextureHandle BlurBackdrop(void* dstColorBuffer, uint32_t downscale = 4, uint32_t passes = 2) {
    auto* scene = reinterpret_cast<GX2Types::ColorBuffer*>(dstColorBuffer);
    if (!impl::g_SpritePipelineReady || !scene || !scene->surface.image) return 0;
    if (downscale == 0) downscale = 1;

    const uint32_t w = scene->surface.width / downscale;
    const uint32_t h = scene->surface.height / downscale;
    if (w < 16 || h < 16) return 0;

    // Allocated once, at the first size seen. Nothing in this module is ever
    // freed, so a later size change (the GamePad view is 854x480 where the TV
    // is larger) reuses the targets rather than making a second pair - the
    // result is scaled to whatever it is drawn over either way.
    if (!impl::g_BlurTried) {
        impl::g_BlurTried = true;
        impl::g_BlurWidth = w;
        impl::g_BlurHeight = h;
        impl::g_BlurAvailable = impl::EnsureBlurTarget(impl::g_BlurTargets[0], w, h) &&
                                impl::EnsureBlurTarget(impl::g_BlurTargets[1], w, h);
        BotW::OSLog("WiiXLaunch GUI: backdrop blur %s (%ux%u)\n",
                    impl::g_BlurAvailable ? "ready" : "unavailable", w, h);
    }
    if (!impl::g_BlurAvailable) return 0;

    impl::AliasScene(scene);

    // Downsample into [1], then ping-pong so the result always lands in [0] -
    // callers hold that handle from the frame before they ever see a result.
    impl::BlurPass(&impl::g_BlurTargets[1].color, &impl::g_SceneAlias, static_cast<float>(downscale) * 0.5f);
    uint32_t from = 1;
    for (uint32_t i = 0; i < passes; ++i) {
        const uint32_t to = from ^ 1;
        impl::BlurPass(&impl::g_BlurTargets[to].color, &impl::g_BlurTargets[from].tex, 1.0f + static_cast<float>(i));
        from = to;
    }
    if (from != 0) {
        impl::BlurPass(&impl::g_BlurTargets[0].color, &impl::g_BlurTargets[1].tex, 0.5f);
    }
    return reinterpret_cast<TextureHandle>(&impl::g_BlurTargets[0].tex);
}

// The blurred backdrop's texture, valid whether or not a blur has run yet -
// so it can be referenced when building a frame and filled in when drawing it.
inline TextureHandle BackdropTexture() {
    return reinterpret_cast<TextureHandle>(&impl::g_BlurTargets[0].tex);
}

inline bool BackdropReady() { return impl::g_BlurAvailable; }

// Callbacks like DrawCallback, run before the HUD is drawn: the target is the scene's finished colour buffer and the
// draw goes through a private context state. Only called on frames the pre-UI draw is available (PreUiReady), so a
// mod keeps its scan-out callback for the frames it is not and skips that one when this ran.
inline void RegisterPreUiDrawCallback(DrawCallback cb) {
    if (cb && impl::g_PreUiCallbackCount < impl::kMaxCallbacks) impl::g_PreUiCallbacks[impl::g_PreUiCallbackCount++] = cb;
}

// True once the private context state exists, which is the first scan-out after a shader pass and a pre-UI
// callback are both registered.
inline bool PreUiReady() { return impl::g_PassContext != nullptr; }

// True once a scene's depth buffer has been seen, which DrawShaderPass then binds as texture 1 (point filtered, raw
// [0,1] depth as the game wrote it). It outlives the frame that latched it: the game composites about twice per scene
// it renders, and the composite that follows a scene-less vsync still shows the last scene's depth.
inline bool SceneDepthReady() {
    return impl::g_SceneDepth && impl::g_SceneDepth->surface.image;
}

// Builds a shader pass from a GFD holding one vertex shader (a float2 position, then up to 12 float4 parameters, all
// attributes) and one pixel shader.
// The bytes are copied into host memory, so the caller's copy need not outlive the call. Returns 0 on failure.
using ShaderPassHandle = uint32_t;

inline ShaderPassHandle CreateShaderPass(const void* gsh, size_t size) {
    constexpr uint32_t kVsHeader = GFD::kBlockTypeVertexShaderHeader;
    constexpr uint32_t kVsProgram = GFD::kBlockTypeVertexShaderProgram;
    constexpr uint32_t kPsHeader = GFD::kBlockTypePixelShaderHeader;
    constexpr uint32_t kPsProgram = GFD::kBlockTypePixelShaderProgram;

    size_t slot = 0;
    while (slot < impl::kMaxShaderPasses && impl::g_Passes[slot].used) ++slot;
    if (slot == impl::kMaxShaderPasses) {
        BotW::OSLog("WiiXLaunch: shader pass refused - all %u slots are held\n", static_cast<unsigned>(impl::kMaxShaderPasses));
        return 0;
    }

    const uint32_t vsHeaderSize = GFD::BlockDataSize(gsh, size, kVsHeader);
    const uint32_t vsProgramSize = GFD::BlockDataSize(gsh, size, kVsProgram);
    const uint32_t psHeaderSize = GFD::BlockDataSize(gsh, size, kPsHeader);
    const uint32_t psProgramSize = GFD::BlockDataSize(gsh, size, kPsProgram);
    if (!vsHeaderSize || !vsProgramSize || !psHeaderSize || !psProgramSize) {
        BotW::OSLog("WiiXLaunch: shader pass refused - the GFD lacks a vertex or pixel shader block\n");
        return 0;
    }

    auto* vsHeader = static_cast<uint8_t*>(impl::AllocPassBuffer(vsHeaderSize, 256));
    auto* psHeader = static_cast<uint8_t*>(impl::AllocPassBuffer(psHeaderSize, 256));
    auto* vsProgram = static_cast<uint8_t*>(impl::AllocPassBuffer(vsProgramSize, GX2Types::kShaderProgramAlignment));
    auto* psProgram = static_cast<uint8_t*>(impl::AllocPassBuffer(psProgramSize, GX2Types::kShaderProgramAlignment));
    if (!vsHeader || !psHeader || !vsProgram || !psProgram) {
        BotW::OSLog("WiiXLaunch: shader pass refused - out of memory\n");
        return 0;
    }

    impl::ShaderPass& pass = impl::g_Passes[slot];
    pass.vs = GFD::GetShader<GX2Types::VertexShader>(gsh, kVsHeader, kVsProgram, vsHeader, vsHeaderSize, vsProgram, vsProgramSize);
    pass.ps = GFD::GetShader<GX2Types::PixelShader>(gsh, kPsHeader, kPsProgram, psHeader, psHeaderSize, psProgram, psProgramSize);
    if (!pass.vs || !pass.ps) {
        BotW::OSLog("WiiXLaunch: shader pass refused - the GFD would not parse (vs=%p ps=%p)\n", pass.vs, pass.ps);
        return 0;
    }
    // A float2 position, then float4 parameters. Their number is whatever the vertex shader declares.
    const uint32_t attribCount = pass.vs->attribVarCount;
    if (attribCount < 1 || attribCount > 1 + impl::kPassMaxVec4) {
        BotW::OSLog("WiiXLaunch: shader pass refused - the vertex shader has %u attributes, a pass takes a position and up to %u float4s\n",
                    attribCount, static_cast<unsigned>(impl::kPassMaxVec4));
        return 0;
    }
    pass.dataVec4 = attribCount - 1;

    GX2Types::AttribStream attribs[1 + impl::kPassMaxVec4]{};
    for (uint32_t i = 0; i < attribCount; ++i) {
        attribs[i].location = i;
        attribs[i].buffer = 0;
        attribs[i].offset = i == 0 ? 0u : 8u + 16u * (i - 1);
        attribs[i].format = i == 0 ? GX2Types::kAttribFormatFloat32x2 : GX2Types::kAttribFormatFloat32x4;
        attribs[i].type = GX2Types::kAttribIndexPerVertex;
        attribs[i].aluDivisor = 0;
        attribs[i].mask = i == 0 ? 0x00010405u : 0x00010203u;
        attribs[i].endianSwap = GX2Types::kEndianSwapDefault;
    }

    const uint32_t fetchSize = impl::CalcFetchShaderSizeEx()(attribCount, GX2Types::kFetchShaderTessellationNone,
                                                              GX2Types::kTessellationModeDiscrete);
    auto* fetchProgram = static_cast<uint8_t*>(impl::AllocPassBuffer(fetchSize, GX2Types::kShaderProgramAlignment));
    if (!fetchProgram) {
        BotW::OSLog("WiiXLaunch: shader pass refused - out of memory for the fetch shader\n");
        return 0;
    }
    impl::InitFetchShaderEx()(&pass.fetch, fetchProgram, attribCount, attribs, GX2Types::kFetchShaderTessellationNone,
                              GX2Types::kTessellationModeDiscrete);

    impl::Invalidate()(GX2Types::kInvalidateModeCpuShader, vsProgram, vsProgramSize);
    impl::Invalidate()(GX2Types::kInvalidateModeCpuShader, psProgram, psProgramSize);
    impl::Invalidate()(GX2Types::kInvalidateModeCpuShader, fetchProgram, fetchSize);

    pass.used = true;
    BotW::OSLog("WiiXLaunch: shader pass %u ready (vs %u B, ps %u B)\n", static_cast<unsigned>(slot + 1), vsProgramSize, psProgramSize);
    return static_cast<ShaderPassHandle>(slot + 1);
}

// Draws `pass` over the rectangle (x, y, w, h) of `dstTexture`, in pixels from the top left. `uniforms` are the
// pass's float4 parameters, exactly as many as its vertex shader has attributes beyond the position, and the frame so far is texture 0. The rectangle is the pass's whole cost: pixels
// outside it are never shaded, so a mod that knows where its effect is should say so.
inline bool DrawShaderPass(void* dstTexture, ShaderPassHandle handle, const void* uniforms, uint32_t uniformSize,
                           int32_t x, int32_t y, int32_t w, int32_t h) {
    auto* dst = reinterpret_cast<GX2Types::ColorBuffer*>(dstTexture);
    if (!dst || !dst->surface.image || handle == 0 || handle > impl::kMaxShaderPasses) return false;
    impl::ShaderPass& pass = impl::g_Passes[handle - 1];
    if (!pass.used || uniformSize != pass.dataVec4 * 16u || (uniformSize && !uniforms)) return false;

    void* contextState = impl::DrawContext();
    if (!contextState || !impl::EnsurePassScene(dst)) {
        static bool s_loggedRefusal = false;
        if (!s_loggedRefusal) {
            s_loggedRefusal = true;
            BotW::OSLog("WiiXLaunch: shader pass draw refused - %s\n",
                        contextState ? "no scene copy" : "no context state");
        }
        return false;
    }

    const int32_t dstW = static_cast<int32_t>(dst->surface.width);
    const int32_t dstH = static_cast<int32_t>(dst->surface.height);
    const int32_t x0 = x < 0 ? 0 : x;
    const int32_t y0 = y < 0 ? 0 : y;
    const int32_t x1 = (x + w) > dstW ? dstW : (x + w);
    const int32_t y1 = (y + h) > dstH ? dstH : (y + h);
    if (x1 <= x0 || y1 <= y0) return true;    // nothing on screen is not a failure

    static bool s_loggedFirstDraw = false;
    if (!s_loggedFirstDraw) {
        s_loggedFirstDraw = true;
        BotW::OSLog("WiiXLaunch: shader pass first draw: rect %u,%u %ux%u on %ux%u, %s context, depth %s\n",
                    static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(x1 - x0),
                    static_cast<uint32_t>(y1 - y0), static_cast<uint32_t>(dstW), static_cast<uint32_t>(dstH),
                    impl::g_ContextOverride ? "private" : "game", SceneDepthReady() ? "bound" : "missing");
    }

    // The pass cannot sample the buffer it is blending into. Passes drawn in one HUD pass all sample the frame as it was
    // before the first of them, so it is copied once.
    if (!impl::g_ContextOverride || impl::g_PassSceneEvent != impl::g_PassEvent) {
        impl::AliasScene(dst);
        impl::BlurTap(&impl::g_PassScene.color, &impl::g_SceneAlias, 0.0f, 0.0f, 1.0f, true);
        impl::Invalidate()(GX2Types::kInvalidateModeColorBuffer | GX2Types::kInvalidateModeTexture,
                           impl::g_PassScene.color.surface.image, impl::g_PassScene.color.surface.imageSize);
        impl::g_PassSceneEvent = impl::g_PassEvent;
    }

    // Four vertices, each a position and the same parameters.
    static const float kCorners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
    const uint32_t stride = 8u + pass.dataVec4 * 16u;
    uint8_t* vertices = impl::g_PassVertices + (impl::g_PassSlot++ % impl::kPassSlots) * impl::kPassSlotBytes;
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t* vertex = vertices + i * stride;
        memcpy(vertex, kCorners[i], 8);
        if (uniformSize) memcpy(vertex + 8, uniforms, uniformSize);
    }
    impl::Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, vertices, 4u * stride);

    impl::SetContextState()(contextState);
    impl::SetShaderModeEx()(GX2Types::kShaderModeUniformRegister, 0x30, 0x40, 0x0, 0x0, 0xc8, 0xc0);
    impl::SetColorBuffer()(dst, 0);
    impl::SetTargetChannelMasks()(0x0F, 0, 0, 0, 0, 0, 0, 0);
    impl::SetViewport()(0.0f, 0.0f, static_cast<float>(dstW), static_cast<float>(dstH), 0.0f, 1.0f);
    impl::SetScissor()(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0),
                       static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0));
    impl::SetDepthOnlyControl()(0, 0, GX2Types::kCompareFuncAlways);
    impl::SetCullOnlyControl()(GX2Types::kFrontFaceCcw, 0, 0);
    impl::SetColorControl()(GX2Types::kLogicOpCopy, 1, 0, 1);
    impl::SetBlendControl()(0, 4, 5, 0, 0, 1, 0, 0);

    impl::SetPixelTexture()(&impl::g_PassScene.tex.texture, 0);
    impl::SetPixelSampler()(&impl::g_PassScene.tex.sampler, 0);
    // The scene's depth as texture 1 when this frame has one (SceneDepthReady says so).
    if (impl::AliasSceneDepth()) {
        impl::SetPixelTexture()(&impl::g_SceneDepthTex.texture, 1);
        impl::SetPixelSampler()(&impl::g_SceneDepthTex.sampler, 1);
    }
    // The pass's own data as texture 2, once the mod has given it some.
    if (pass.hasData) {
        impl::SetPixelTexture()(&pass.data.texture, 2);
        impl::SetPixelSampler()(&pass.data.sampler, 2);
    }

    impl::SetFetchShader()(&pass.fetch);
    impl::SetVertexShader()(pass.vs);
    impl::SetPixelShader()(pass.ps);
    impl::SetAttribBuffer()(0, 4u * stride, stride, vertices);
    impl::DrawEx()(GX2Types::kPrimitiveModeTriangleStrip, 4, 0, 1);
    return true;
}

// Gives `pass` a data texture: width x height RGBA8 texels, row by row from the top, which every later draw of the pass
// binds as texture 2 - unorm, point filtered, clamped, so texel (x, y) reads back exactly at its centre. Call it again
// whenever the data changes; the size is fixed by the first call. Returns false for a bad pass or size, or no memory.
// The texels are written in place, with the row-padding stamp that makes Cemu notice (see kPassDataPadding).
inline bool SetShaderPassData(ShaderPassHandle handle, const void* rgba, uint32_t width, uint32_t height) {
    if (handle == 0 || handle > impl::kMaxShaderPasses || !rgba || width == 0 || height == 0 ||
        width > impl::kPassDataMaxSide || height > impl::kPassDataMaxSide) {
        return false;
    }
    impl::ShaderPass& pass = impl::g_Passes[handle - 1];
    if (!pass.used) return false;

    if (!pass.hasData) {
        {
            impl::TextureWrapper& t = pass.data;
            t.texture.surface.dim = GX2Types::kSurfaceDimTexture2D;
            t.texture.surface.width = width;
            t.texture.surface.height = height;
            t.texture.surface.depth = 1;
            t.texture.surface.mipLevels = 1;
            t.texture.surface.format = GX2Types::kSurfaceFormatUnormR8G8B8A8;
            t.texture.surface.aa = GX2Types::kAaMode1x;
            t.texture.surface.use = GX2Types::kSurfaceUseTexture;
            t.texture.surface.tileMode = GX2Types::kTileModeLinearAligned;
            // Sized as if kPassDataPadding texels wider, which guarantees every row that much padding past the width
            // (see kPassDataPadding); the texture itself keeps the real width, so the shader never sees the padding.
            t.texture.surface.width = width + impl::kPassDataPadding;
            impl::CalcSurfaceSizeAndAlignment()(&t.texture.surface);
            t.texture.surface.width = width;
            t.texture.surface.image = impl::AllocPassBuffer(t.texture.surface.imageSize, t.texture.surface.alignment);
            if (!t.texture.surface.image) {
                BotW::OSLog("WiiXLaunch: shader pass %u data refused - out of memory (%u bytes)\n",
                            static_cast<unsigned>(handle), t.texture.surface.imageSize);
                return false;
            }
            memset(t.texture.surface.image, 0, t.texture.surface.imageSize);
            t.texture.viewFirstMip = 0;
            t.texture.viewNumMips = 1;
            t.texture.viewFirstSlice = 0;
            t.texture.viewNumSlices = 1;
            t.texture.compMap = GX2Types::kCompMapRGBA;
            impl::InitTextureRegs()(&t.texture);
            impl::InitSampler()(&t.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexXYFilterModePoint);
            impl::InitSamplerClamping()(&t.sampler, GX2Types::kTexClampModeClamp, GX2Types::kTexClampModeClamp,
                                        GX2Types::kTexClampModeClamp);
        }
        pass.dataWidth = width;
        pass.dataHeight = height;
        pass.hasData = true;
        BotW::OSLog("WiiXLaunch: shader pass %u data %ux%u (pitch %u, %u bytes at %p)\n", static_cast<unsigned>(handle),
                    width, height, pass.data.texture.surface.pitch, pass.data.texture.surface.imageSize,
                    pass.data.texture.surface.image);
    } else if (width != pass.dataWidth || height != pass.dataHeight) {
        BotW::OSLog("WiiXLaunch: shader pass %u data refused - %ux%u, but it was created %ux%u\n",
                    static_cast<unsigned>(handle), width, height, pass.dataWidth, pass.dataHeight);
        return false;
    }

    GX2Types::Texture& tex = pass.data.texture;
    auto* dstRows = static_cast<uint8_t*>(tex.surface.image);
    const auto* srcRows = static_cast<const uint8_t*>(rgba);
    const uint32_t rowBytes = width * 4u;
    const uint32_t pitchBytes = tex.surface.pitch * 4u;
    // Each row's texels, then a fresh stamp across its padding, so this update changes words Cemu's hash reads.
    const uint32_t stamp = ++pass.dataStamp;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = dstRows + y * pitchBytes;
        memcpy(row, srcRows + y * rowBytes, rowBytes);
        auto* padding = reinterpret_cast<uint32_t*>(row + rowBytes);
        for (uint32_t x = width; x < tex.surface.pitch; ++x) *padding++ = stamp;
    }
    impl::Invalidate()(GX2Types::kInvalidateModeCpuTexture, tex.surface.image, tex.surface.imageSize);
    return true;
}


// Reads a texture packaged by scripts/pack_resources.py/pack_texture_gx2.py
// (16-byte big-endian header: width, height, format, dataSize, followed by
// raw RGBA8 pixels) straight off disk and hands it to CreateTexture - the
// on-disk counterpart to a single NVN::CreateTexture(bytes, size) call,
// which gets the same info from a header baked into its own byte array
// instead of a loose file. The read buffer is pure scratch: CreateTexture
// copies pixel data into its own tiled GX2 surface before returning, so one
// shared staging buffer is reused across every LoadTexture call rather than
// keeping the raw file bytes around per texture.
inline TextureHandle LoadTexture(const char* path, size_t maxFileSize = 1024 * 1024) {
    static uint8_t* s_stagingBuffer = nullptr;
    if (!s_stagingBuffer) s_stagingBuffer = reinterpret_cast<uint8_t*>(AllocMEM1(static_cast<uint32_t>(maxFileSize), 256));
    if (!s_stagingBuffer) {
        BotW::OSLog("WiiXLaunch: GX2 LoadTexture '%s' failed: staging buffer alloc failed\n", path);
        return 0;
    }

    size_t readSize = 0;
    if (!WiiXLaunch::FS::ReadFile(path, s_stagingBuffer, maxFileSize, &readSize) || readSize < 16) {
        BotW::OSLog("WiiXLaunch: GX2 LoadTexture '%s' failed: ReadFile\n", path);
        return 0;
    }

    uint32_t width, height, dataSize;
    memcpy(&width, s_stagingBuffer + 0, 4);
    memcpy(&height, s_stagingBuffer + 4, 4);
    memcpy(&dataSize, s_stagingBuffer + 12, 4);

    TextureHandle handle = CreateTexture(s_stagingBuffer + 16, dataSize, static_cast<int>(width), static_cast<int>(height));
    BotW::OSLog("WiiXLaunch: GX2 LoadTexture '%s' -> %ux%u handle=%p\n", path, width, height, reinterpret_cast<void*>(handle));
    return handle;
}

// Reads a mesh packaged by scripts/pack_resources.py (16-byte big-endian
// header: vertexCount, floatStride(=8, unused here), dataSize, 0, followed by
// MeshVertex-compatible float data) straight off disk. Unlike LoadTexture,
// DrawMesh re-reads its vertices pointer every frame (see DrawMesh's
// per-frame ring-buffer copy), so the returned MeshData's buffer has to
// outlive the mesh - each call gets its own permanent allocation, never
// reused or freed (matching this project's general one-shot
// startup-allocation style; nothing here is ever torn down).
inline MeshData LoadMesh(const char* path, size_t maxFileSize = 64 * 1024) {
    MeshData result{};

    auto* buffer = reinterpret_cast<uint8_t*>(AllocMEM1(static_cast<uint32_t>(maxFileSize), 256));
    if (!buffer) {
        BotW::OSLog("WiiXLaunch: GX2 LoadMesh '%s' failed: alloc failed\n", path);
        return result;
    }

    size_t readSize = 0;
    if (!WiiXLaunch::FS::ReadFile(path, buffer, maxFileSize, &readSize) || readSize < 16) {
        BotW::OSLog("WiiXLaunch: GX2 LoadMesh '%s' failed: ReadFile\n", path);
        return result;
    }

    uint32_t vertexCount;
    memcpy(&vertexCount, buffer + 0, 4);

    result.vertices = reinterpret_cast<const MeshVertex*>(buffer + 16);
    result.vertexCount = vertexCount;
    BotW::OSLog("WiiXLaunch: GX2 LoadMesh '%s' -> %u vertices\n", path, vertexCount);
    return result;
}

inline void DrawSprite(
    CommandBuffer* cmdBuf,
    void* dstTexture,
    TextureHandle textureHandle,
    float x, float y,
    float width, float height,
    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
{
    if (!impl::g_SpritePipelineReady || !textureHandle) return;

    auto* wrap = reinterpret_cast<impl::TextureWrapper*>(textureHandle);
    auto* colorBuf = reinterpret_cast<GX2Types::ColorBuffer*>(dstTexture);
    if (!colorBuf || !colorBuf->surface.image) return;

    void* contextState = impl::GetContextState();
    if (!contextState) return;

    float x0 = x;
    float x1 = x + width;
    float y0 = y;
    float y1 = y + height;

    const TextureVertex verts[4] = {
        {x0, y0, 0.0f, 1.0f,   0.0f, 1.0f,   r, g, b, a},
        {x1, y0, 0.0f, 1.0f,   1.0f, 1.0f,   r, g, b, a},
        {x0, y1, 0.0f, 1.0f,   0.0f, 0.0f,   r, g, b, a},
        {x1, y1, 0.0f, 1.0f,   1.0f, 0.0f,   r, g, b, a},
    };

    constexpr size_t kSlotSize = 256;
    constexpr size_t kSlotCount = sizeof(impl::g_SpriteRingBuffer) / kSlotSize;
    size_t slot = (impl::g_SpriteRingSlot++) % kSlotCount;
    uint8_t* dst = impl::g_SpriteRingBuffer + slot * kSlotSize;
    memcpy(dst, verts, sizeof(verts));
    impl::Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, dst, sizeof(verts));

    uint32_t dispW = colorBuf->surface.width ? colorBuf->surface.width : 1280;
    uint32_t dispH = colorBuf->surface.height ? colorBuf->surface.height : 720;

    impl::SetContextState()(contextState);
    impl::SetShaderModeEx()(GX2Types::kShaderModeUniformRegister, 0x30, 0x40, 0x0, 0x0, 0xc8, 0xc0);

    impl::SetColorBuffer()(colorBuf, 0);
    impl::SetTargetChannelMasks()(0x0F, 0, 0, 0, 0, 0, 0, 0);
    impl::SetViewport()(0.0f, 0.0f, static_cast<float>(dispW), static_cast<float>(dispH), 0.0f, 1.0f);
    impl::SetScissor()(0, 0, dispW, dispH);
    impl::SetDepthOnlyControl()(0, 0, GX2Types::kCompareFuncAlways);
    impl::SetCullOnlyControl()(GX2Types::kFrontFaceCcw, 0, 0);
    impl::SetColorControl()(GX2Types::kLogicOpCopy, 1, 0, 1);
    impl::SetBlendControl()(0, 4, 5, 0, 0, 1, 0, 0);

    impl::SetPixelTexture()(&wrap->texture, 0);
    impl::SetPixelSampler()(&wrap->sampler, 0);

    impl::SetFetchShader()(&impl::g_SpriteFetchShader);
    impl::SetVertexShader()(impl::g_SpriteVertexShader);
    impl::SetPixelShader()(impl::g_SpritePixelShader);
    impl::SetAttribBuffer()(0, sizeof(verts), sizeof(TextureVertex), dst);
    impl::DrawEx()(GX2Types::kPrimitiveModeTriangleStrip, 4, 0, 1);
    impl::DrawDone()();
}

inline void DrawMesh(
    CommandBuffer* cmdBuf,
    void* dstTexture,
    const MeshVertex* vertices,
    size_t vertexCount)
{
    if (!impl::g_MeshPipelineReady || !vertices || vertexCount == 0) return;

    auto* colorBuf = reinterpret_cast<GX2Types::ColorBuffer*>(dstTexture);
    if (!colorBuf || !colorBuf->surface.image) return;

    void* contextState = impl::GetContextState();
    if (!contextState) return;

    uint32_t dispW = colorBuf->surface.width ? colorBuf->surface.width : 1280;
    uint32_t dispH = colorBuf->surface.height ? colorBuf->surface.height : 720;

    impl::EnsureDepthBuffer(dispW, dispH);

    if (!impl::g_MeshRingBuffer) {
        impl::g_MeshRingBuffer = reinterpret_cast<uint8_t*>(impl::AllocMEM1(impl::g_MeshRingBufferSize, 256));
    }
    if (!impl::g_MeshRingBuffer) return;

    size_t byteSize = vertexCount * sizeof(MeshVertex);
    if (byteSize > impl::g_MeshRingBufferSize) {
        byteSize = impl::g_MeshRingBufferSize;
        vertexCount = byteSize / sizeof(MeshVertex);
    }
    
    float aspect = static_cast<float>(dispH) / static_cast<float>(dispW);
    auto* dstVerts = reinterpret_cast<MeshVertex*>(impl::g_MeshRingBuffer);
    for (size_t i = 0; i < vertexCount; ++i) {
        dstVerts[i] = vertices[i];
        dstVerts[i].x = vertices[i].x * aspect;
    }
    impl::Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, impl::g_MeshRingBuffer, byteSize);

    impl::SetContextState()(contextState);
    impl::SetShaderModeEx()(GX2Types::kShaderModeUniformRegister, 0x30, 0x40, 0x0, 0x0, 0xc8, 0xc0);

    impl::SetColorBuffer()(colorBuf, 0);
    impl::SetDepthBuffer()(&impl::g_DepthBuffer);

    if (!impl::g_DepthClearedThisFrame) {
        impl::ClearDepthStencilEx()(&impl::g_DepthBuffer, 1.0f, 0, GX2Types::kClearFlagsDepth);
        impl::g_DepthClearedThisFrame = true;
    }

    impl::SetTargetChannelMasks()(0x0F, 0, 0, 0, 0, 0, 0, 0);
    impl::SetViewport()(0.0f, 0.0f, static_cast<float>(dispW), static_cast<float>(dispH), 0.0f, 1.0f);
    impl::SetScissor()(0, 0, dispW, dispH);
    impl::SetDepthOnlyControl()(1, 1, GX2Types::kCompareFuncLessEqual);
    impl::SetCullOnlyControl()(GX2Types::kFrontFaceCcw, 0, 0);
    impl::SetColorControl()(GX2Types::kLogicOpCopy, 0, 0, 1);
    impl::SetBlendControl()(0, 1, 0, 0, 0, 1, 0, 0);

    impl::SetFetchShader()(&impl::g_MeshFetchShader);
    impl::SetVertexShader()(impl::g_MeshVertexShader);
    impl::SetPixelShader()(impl::g_MeshPixelShader);
    impl::SetAttribBuffer()(0, byteSize, sizeof(MeshVertex), impl::g_MeshRingBuffer);
    impl::DrawEx()(GX2Types::kPrimitiveModeTriangles, static_cast<uint32_t>(vertexCount), 0, 1);
    impl::DrawDone()();
}

} // namespace WiiXLaunch::BotW::GX2

#else

namespace WiiXLaunch::BotW::GX2 {

constexpr bool SupportsGX2 = false;

using TextureHandle = uintptr_t;
using CommandBuffer = void;
using Device        = void;

struct TextureVertex {
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
};

struct MeshVertex {
    float x, y, z, w;
    float nx, ny, nz, nw;
};

struct MeshData {
    const MeshVertex* vertices = nullptr;
    size_t vertexCount = 0;
};

inline void Init() {}
inline void RegisterDrawCallback(void (*)(void*, void*, int, int)) {}
inline void RegisterPreHudDrawCallback(void (*)(void*, void*, int, int)) {}
inline void OnInitialized(void (*)()) {}
inline TextureHandle CreateTexture(const void*, size_t, int = 0, int = 0, int = 0) { return 0; }
inline TextureHandle LoadTexture(const char*, size_t = 0) { return 0; }
inline MeshData LoadMesh(const char*, size_t = 0) { return MeshData{}; }
inline void DrawSprite(void*, void*, TextureHandle, float, float, float, float, float = 1, float = 1, float = 1, float = 1) {}
inline void DrawMesh(void*, void*, const MeshVertex*, size_t) {}

struct SurfaceDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint32_t tileMode = 4;
    uint32_t swizzle = 0;
    uint32_t compMap = 0x00010203;
    bool linearFilter = true;
};
inline TextureHandle AllocTextureSurface(const SurfaceDesc&, void**, uint32_t*) { return 0; }
inline void FinalizeTexture(TextureHandle) {}
inline TextureHandle CreateTextureFromSurface(const SurfaceDesc&, const void*, uint32_t) { return 0; }
inline bool GetTextureSize(TextureHandle, uint32_t&, uint32_t&) { return false; }

struct BlendState {
    uint32_t colorSrc = 4, colorDst = 5, colorCombine = 0;
    uint32_t alphaSrc = 1, alphaDst = 5, alphaCombine = 0;
    bool operator==(const BlendState&) const { return true; }
    bool operator!=(const BlendState&) const { return false; }
};
namespace Blend {
    constexpr BlendState Alpha{}, Premultiplied{}, Additive{}, AdditivePremultiplied{};
    constexpr BlendState Overlay{}, Multiply{}, Opaque{}, Subtract{};
    inline BlendState FromLyt(uint8_t, uint8_t, uint8_t) { return BlendState{}; }
}
inline TextureHandle BlurBackdrop(void*, uint32_t = 4, uint32_t = 2) { return 0; }
inline TextureHandle BackdropTexture() { return 0; }
inline bool BackdropReady() { return false; }
using ShaderPassHandle = uint32_t;
inline ShaderPassHandle CreateShaderPass(const void*, size_t) { return 0; }
inline bool SceneDepthReady() { return false; }
inline void RegisterPreUiDrawCallback(void (*)(void*, void*, int, int)) {}
inline bool PreUiReady() { return false; }
inline bool DrawShaderPass(void*, ShaderPassHandle, const void*, uint32_t, int32_t, int32_t, int32_t, int32_t) { return false; }
inline bool SetShaderPassData(ShaderPassHandle, const void*, uint32_t, uint32_t) { return false; }
inline void BeginBatch(void*) {}
inline void BatchQuad(TextureHandle, const TextureVertex*, const BlendState& = Blend::Alpha) {}
inline void EndBatch() {}

} // namespace WiiXLaunch::BotW::GX2

#endif
