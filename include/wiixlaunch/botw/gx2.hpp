#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/debug_log.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include "gfd.hpp"
#include "gx2_shader_types.hpp"

#if WIIXL_CEMU
#include "gx2_imports.hpp"
#elif WIIXL_WIIU
#include <gx2/draw.h>
#include <gx2/shaders.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <gx2/event.h>
#endif

#include <cstdint>
#include <cstddef>

// ===========================================================================
// WiiXLaunch::BotW::GX2 - GX2 (Wii U) graphics injection, Cemu + real Wii U.
// ===========================================================================
// Mirrors NVN.hpp's role on Switch. Findings this was built on (via Ghidra
// on the u-king RPX, symbols unstripped for GX2 - no signature scanning
// needed, unlike NVN on Switch):
//
// - DAT_1046c92c: a FIXED global address (no ASLR/relocation - Wii U RPXs
//   load at a fixed base) holding a live ~0x120-byte "GX2 device/context"
//   object, allocated and filled in by BotW's own GX2 init function
//   (FUN_0309bbb0: GX2Init, GX2SetupContextStateEx, GX2SetSwapInterval,
//   TV+DRC color/depth buffer reg setup). *(DAT_1046c92c + 0xf8) is the
//   live GX2ContextState* - the direct equivalent of NVN's
//   GetGraphicsNvn()/GetDevice().
// - DAT_1046c954: holds pointer to sead::Framework holder.
//   *(DAT_1046c954 + 0x14) is sead::Framework*, where +0xd0 is BotW's live
//   TV GX2ColorBuffer and +0x208 is the TV GX2DepthBuffer.
// - Once-per-frame hook: FUN_02c57ed8 (0x02c57ed8) - single caller
//   (RootTask::calc's steady-state per-frame tick).

namespace WiiXLaunch::BotW::GX2 {

constexpr bool SupportsGX2 = true;

namespace impl {

// Fixed RPX addresses - identical on Cemu and real Wii U (both load the
// RPX's .text/.data at the same fixed base; see docs/hooks.md).
constexpr uintptr_t kGraphicsContextSlot = 0x1046c92c; // DAT_1046c92c
constexpr uintptr_t kContextStateOffset  = 0xf8;
constexpr uintptr_t kFrameworkHolderSlot = 0x1046c954; // DAT_1046c954
constexpr uintptr_t kFrameworkOffset     = 0x14;
constexpr uintptr_t kTvColorBufferOffset = 0xd0;

inline void* GetGraphicsContext() {
    return *reinterpret_cast<void**>(kGraphicsContextSlot);
}

inline void* GetContextState() {
    void* ctx = GetGraphicsContext();
    if (!ctx) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ctx) + kContextStateOffset);
}

inline GX2Types::ColorBuffer* GetTvColorBuffer() {
    auto* holder = *reinterpret_cast<uintptr_t**>(kFrameworkHolderSlot);
    if (!holder) return nullptr;
    auto framework = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(holder) + kFrameworkOffset);
    if (!framework) return nullptr;
    return reinterpret_cast<GX2Types::ColorBuffer*>(framework + kTvColorBufferOffset);
}

// ---------------------------------------------------------------------
// Platform-specific GX2 function access. Cemu resolves through the
// import-shim table (gx2_imports.hpp/gx2_imports.asm - the payload is a
// raw codecave blob, never loaded as a real RPL module, see that header
// for the full explanation). Real Wii U just calls wut's real linked
// imports directly - no shim table needed there at all.
// ---------------------------------------------------------------------

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

#elif WIIXL_WIIU

// Real, directly-linked wut imports - reinterpret_cast just adapts wut's
// real (more strongly-typed) signatures to the same function pointer
// shapes used above, so the pipeline code below doesn't need an #if split.
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

#endif

// ---------------------------------------------------------------------
// Minimal quad pipeline - one flat-color shader, one 4-vertex triangle
// strip, static storage (no heap). Deliberately not general (single
// hardcoded vertex layout: position + color).
// ---------------------------------------------------------------------

struct QuadVertex {
    float x, y, z, w;
    float r, g, b, a;
};

alignas(64) inline QuadVertex g_QuadVertices[4] = {
    {-0.5f, -0.5f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f},
    { 0.5f, -0.5f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f},
    {-0.5f,  0.5f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f},
    { 0.5f,  0.5f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f},
};

alignas(2048) inline uint8_t g_VertexShaderHeaderBuf[2048];
alignas(2048) inline uint8_t g_PixelShaderHeaderBuf[2048];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_VertexProgramBuf[4096];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_PixelProgramBuf[4096];
alignas(GX2Types::kShaderProgramAlignment) inline uint8_t g_FetchShaderBuf[512];

inline GX2Types::VertexShader* g_VertexShader = nullptr;
inline GX2Types::PixelShader* g_PixelShader = nullptr;
inline GX2Types::FetchShader g_FetchShader{};
inline bool g_QuadPipelineReady = false;

constexpr uint32_t kColorBufferWidth = 1280;
constexpr uint32_t kColorBufferHeight = 720;

inline GX2Types::ColorBuffer g_ColorBuffer{};

inline void EnsureColorBuffer() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    g_ColorBuffer.surface.dim = GX2Types::kSurfaceDimTexture2D;
    g_ColorBuffer.surface.width = kColorBufferWidth;
    g_ColorBuffer.surface.height = kColorBufferHeight;
    g_ColorBuffer.surface.depth = 1;
    g_ColorBuffer.surface.mipLevels = 1;
    g_ColorBuffer.surface.format = GX2Types::kSurfaceFormatUnormR8G8B8A8;
    g_ColorBuffer.surface.aa = GX2Types::kAaMode1x;
    g_ColorBuffer.surface.use = GX2Types::kSurfaceUseColorBufferTexture;
    g_ColorBuffer.surface.tileMode = GX2Types::kTileModeLinearAligned;

    CalcSurfaceSizeAndAlignment()(&g_ColorBuffer.surface);

    using FnAllocMEM1 = void* (*)(uint32_t size, uint32_t align);
    auto allocMEM1 = reinterpret_cast<FnAllocMEM1>(0x0309bb68);
    void* mem1Image = allocMEM1(g_ColorBuffer.surface.imageSize, g_ColorBuffer.surface.alignment);

    WIIXL_LOG("WiiXLaunch: GX2 init colorbuffer size=%u align=%u mem1=%p",
        g_ColorBuffer.surface.imageSize, g_ColorBuffer.surface.alignment, mem1Image);

    if (!mem1Image) return;

    g_ColorBuffer.surface.image = mem1Image;
    g_ColorBuffer.viewMip = 0;
    g_ColorBuffer.viewFirstSlice = 0;
    g_ColorBuffer.viewNumSlices = 1;

    InitColorBufferRegs()(&g_ColorBuffer);
}

// Called once (from WiiXLaunch::BotW::GX2::LoadTestQuad) with the compiled
// .gsh bytes the mod supplies (see scripts/pack_shader_gx2.py) - mirrors
// NVN::CreateTexture taking its bytes as a parameter rather than this
// framework header hardcoding a specific asset.
inline bool EnsureQuadPipeline(const uint8_t* gshBytes, size_t gshSize) {
    if (g_QuadPipelineReady) return true;
    if (!gshBytes || gshSize == 0) return false;

    g_VertexShader = GFD::GetShader<GX2Types::VertexShader>(
        gshBytes, GFD::kBlockTypeVertexShaderHeader, GFD::kBlockTypeVertexShaderProgram,
        g_VertexShaderHeaderBuf, sizeof(g_VertexShaderHeaderBuf),
        g_VertexProgramBuf, sizeof(g_VertexProgramBuf));
    g_PixelShader = GFD::GetShader<GX2Types::PixelShader>(
        gshBytes, GFD::kBlockTypePixelShaderHeader, GFD::kBlockTypePixelShaderProgram,
        g_PixelShaderHeaderBuf, sizeof(g_PixelShaderHeaderBuf),
        g_PixelProgramBuf, sizeof(g_PixelProgramBuf));

    WIIXL_LOG("WiiXLaunch: GX2 quad shaders vs=%p ps=%p", g_VertexShader, g_PixelShader);
    if (!g_VertexShader || !g_PixelShader) return false;

    WIIXL_LOG("WiiXLaunch: attribVarCount=%u", g_VertexShader->attribVarCount);

    GX2Types::AttribStream attribs[2]{};
    uint32_t count = (g_VertexShader->attribVarCount > 0) ? g_VertexShader->attribVarCount : 1;
    if (count > 2) count = 2;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t loc = (i < g_VertexShader->attribVarCount) ? g_VertexShader->attribVars[i].location : i;
        const char* name = (i < g_VertexShader->attribVarCount) ? g_VertexShader->attribVars[i].name : "";
        uint32_t offset = 0;
        if (strstr(name, "Col") || strstr(name, "col") || strstr(name, "Color") || strstr(name, "Colour")) {
            offset = 16;
        } else {
            offset = 0;
        }

        WIIXL_LOG("WiiXLaunch: stream[%u] name='%s' loc=%u offset=%u", i, name, loc, offset);
        attribs[i].location = loc;
        attribs[i].buffer = 0;
        attribs[i].offset = offset;
        attribs[i].format = GX2Types::kAttribFormatFloat32x4;
        attribs[i].type = GX2Types::kAttribIndexPerVertex;
        attribs[i].aluDivisor = 0;
        attribs[i].mask = 0x00010203;
        attribs[i].endianSwap = GX2Types::kEndianSwapDefault;
    }

    InitFetchShaderEx()(&g_FetchShader, g_FetchShaderBuf, count, attribs,
        GX2Types::kFetchShaderTessellationNone, GX2Types::kTessellationModeDiscrete);

    // GPU cache invalidation after CPU writes - required before the GPU
    // reads shader program bytes/vertex data it wasn't the one to write.
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_VertexProgramBuf, sizeof(g_VertexProgramBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_PixelProgramBuf, sizeof(g_PixelProgramBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuShader, g_FetchShaderBuf, sizeof(g_FetchShaderBuf));
    Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, g_QuadVertices, sizeof(g_QuadVertices));

    g_QuadPipelineReady = true;
    WIIXL_LOG("WiiXLaunch: GX2 quad pipeline ready");
    return true;
}

inline void DrawTestQuad(GX2Types::ColorBuffer* tvColorBuffer, uint32_t count) {
    if (!g_QuadPipelineReady || !tvColorBuffer || !tvColorBuffer->surface.image) return;

    void* contextState = GetContextState();
    if (!contextState) return;

    uint32_t width = tvColorBuffer->surface.width ? tvColorBuffer->surface.width : 1280;
    uint32_t height = tvColorBuffer->surface.height ? tvColorBuffer->surface.height : 720;

    if (count == 1 || (count % 300) == 0) {
        WIIXL_LOG("WiiXLaunch: GX2 drawing quad over BotW cb=%p (%ux%u img=%p)",
            tvColorBuffer, width, height, tvColorBuffer->surface.image);
    }

    // 1. Re-bind BotW's live context state
    SetContextState()(contextState);

    // 2. Configure shader mode for Uniform Register mode
    SetShaderModeEx()(GX2Types::kShaderModeUniformRegister, 0x30, 0x40, 0x0, 0x0, 0xc8, 0xc0);

    // 3. Bind BotW's actual active TV ColorBuffer with alpha blending
    SetColorBuffer()(tvColorBuffer, 0);
    SetTargetChannelMasks()(0x0F, 0, 0, 0, 0, 0, 0, 0);
    SetViewport()(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
    SetScissor()(0, 0, width, height);
    SetDepthOnlyControl()(0, 0, GX2Types::kCompareFuncAlways);
    SetCullOnlyControl()(GX2Types::kFrontFaceCcw, 0, 0);
    SetColorControl()(GX2Types::kLogicOpCopy, 1, 0, 1);
    SetBlendControl()(0, 4, 5, 0, 0, 1, 0, 0); // SrcAlpha, InvSrcAlpha

    // 4. Invalidate vertex buffer before draw
    Invalidate()(GX2Types::kInvalidateModeCpuAttributeBuffer, g_QuadVertices, sizeof(g_QuadVertices));

    // 5. Issue draw directly into BotW's TV ColorBuffer right before scan buffer copy
    SetFetchShader()(&g_FetchShader);
    SetVertexShader()(g_VertexShader);
    SetPixelShader()(g_PixelShader);
    SetAttribBuffer()(0, sizeof(g_QuadVertices), sizeof(QuadVertex), g_QuadVertices);
    DrawEx()(GX2Types::kPrimitiveModeTriangleStrip, 4, 0, 1);
    DrawDone()();
}

WIIXL_HOOK_DEFINE_TRAMPOLINE(AglCopyToScanBufferHook) {
    static void Callback(uintptr_t renderBuffer, uintptr_t param2, int32_t param3) {
        if (renderBuffer) {
            uintptr_t rt = *reinterpret_cast<uintptr_t*>(renderBuffer + 0x1c);
            if (rt) {
                auto* tvColorBuffer = reinterpret_cast<GX2Types::ColorBuffer*>(rt + 0xbc);
                if (tvColorBuffer && tvColorBuffer->surface.image) {
                    static uint32_t s_count = 0;
                    s_count++;
                    if (s_count == 1 || (s_count % 300) == 0) {
                        WIIXL_LOG("WiiXLaunch: AglCopyToScanBuffer fired #%u cb=%p (%ux%u img=%p)",
                            s_count, tvColorBuffer, tvColorBuffer->surface.width, tvColorBuffer->surface.height, tvColorBuffer->surface.image);
                    }
                    DrawTestQuad(tvColorBuffer, s_count);
                }
            }
        }

        Orig(renderBuffer, param2, param3);
    }
};

}

// Call once from your mod's WiiXLaunch_Init() (see docs/modules.md's
// "self-installing hooks" convention). Switch offset is 0 (n/a) - Wii U and
// Cemu share the same RPX offset, so one Install() call covers both.
inline void Init() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    // Hook agl::RenderBuffer::copyToScanBuffer (0x03a75d48) which receives the
    // active TV ColorBuffer at *(renderBuffer + 0x1c) + 0xbc right before calling
    // GX2CopyColorBufferToScanBuffer.
    impl::AglCopyToScanBufferHook::Install(0, 0x03a75d48);
}

// Supplies the compiled quad shader bytes (see scripts/pack_shader_gx2.py)
// and builds the draw pipeline from them. Safe to call before or after
// Init() - the pipeline just won't draw anything until both this has run
// AND the frame hook has fired at least once.
inline void LoadTestQuad(const uint8_t* gshBytes, size_t gshSize) {
    impl::EnsureQuadPipeline(gshBytes, gshSize);
}

}

#else

namespace WiiXLaunch::BotW::GX2 {

constexpr bool SupportsGX2 = false;

inline void Init() {}
inline void LoadTestQuad(const uint8_t*, size_t) {}

}

#endif
