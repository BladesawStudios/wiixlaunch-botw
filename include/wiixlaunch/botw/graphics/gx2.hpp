#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/debug_log.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include "../platform/fs.hpp"
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

namespace impl {

constexpr uintptr_t kGraphicsContextSlot = 0x1046c92c;
constexpr uintptr_t kContextStateOffset  = 0xf8;

inline void* GetGraphicsContext() {
    return *reinterpret_cast<void**>(kGraphicsContextSlot);
}

inline void* GetContextState() {
    void* ctx = GetGraphicsContext();
    if (!ctx) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ctx) + kContextStateOffset);
}

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

#if WIIXL_CEMU
inline FnSetContextState SetContextState() { return WiiXLaunch::Backend::ResolveGx2<FnSetContextState>(WiiXLaunch::Backend::Gx2Import::SetContextState); }
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
#elif WIIXL_WIIU
inline FnSetContextState SetContextState() { return reinterpret_cast<FnSetContextState>(&GX2SetContextState); }
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
#endif

inline void* AllocMEM1(uint32_t size, uint32_t align = 256) {
#if WIIXL_CEMU
    return WiiXLaunch::Backend::AllocCemuHeap(size, align);
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

constexpr size_t kMaxTextures = 16;
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

WIIXL_HOOK_DEFINE_TRAMPOLINE(AglCopyToScanBufferHook) {
    static void Callback(uintptr_t renderBuffer, uintptr_t param2, int32_t param3) {
            if (renderBuffer) {
            uintptr_t rt = *reinterpret_cast<uintptr_t*>(renderBuffer + 0x1c);
            if (rt) {
                auto* tvColorBuffer = reinterpret_cast<GX2Types::ColorBuffer*>(rt + 0xbc);
                if (tvColorBuffer && tvColorBuffer->surface.image) {
                    InitializeAllPipelines();

                    uint32_t width = tvColorBuffer->surface.width ? tvColorBuffer->surface.width : 1280;
                    uint32_t height = tvColorBuffer->surface.height ? tvColorBuffer->surface.height : 720;

                    g_DepthClearedThisFrame = false;

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
    BotW::OSLog("WiiXLaunch: GX2::Init() hook installed successfully\n");
}

inline void RegisterDrawCallback(DrawCallback cb) {
    if (impl::g_DrawCallbackCount < impl::kMaxCallbacks) {
        impl::g_DrawCallbacks[impl::g_DrawCallbackCount++] = cb;
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
    if (!FS::ReadFile(path, s_stagingBuffer, maxFileSize, &readSize) || readSize < 16) {
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
    if (!FS::ReadFile(path, buffer, maxFileSize, &readSize) || readSize < 16) {
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
inline void OnInitialized(void (*)()) {}
inline TextureHandle CreateTexture(const void*, size_t, int = 0, int = 0, int = 0) { return 0; }
inline TextureHandle LoadTexture(const char*, size_t = 0) { return 0; }
inline MeshData LoadMesh(const char*, size_t = 0) { return MeshData{}; }
inline void DrawSprite(void*, void*, TextureHandle, float, float, float, float, float = 1, float = 1, float = 1, float = 1) {}
inline void DrawMesh(void*, void*, const MeshVertex*, size_t) {}

} // namespace WiiXLaunch::BotW::GX2

#endif
