#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/hook.hpp>
#include <wiixlaunch/call.hpp>

#if WIIXL_SWITCH

#include <cstdint>
#include <cstddef>
#include "shaders/default_ui_shader.hpp"

// ===========================================================================
// WiiXLaunch::BotW::NVN - High-Level NVN Graphics Injection Framework
// ===========================================================================
// Provides a clean, modular API for injecting custom 2D/3D graphics, shaders,
// textures, and UI elements directly into Breath of the Wild's NVN render loop.
// ===========================================================================

namespace WiiXLaunch::BotW::NVN {

constexpr bool SupportsNVN = true;

// ---------------------------------------------------------------------------
// Struct Layouts & Opaque NVN Types
// ---------------------------------------------------------------------------
struct NVNdevice             { alignas(8) uint8_t reserved[0x80]; };
struct NVNcommandBuffer      { alignas(8) uint8_t reserved[0x80]; };
struct NVNprogram            { alignas(8) uint8_t reserved[0x100]; };
struct NVNmemoryPool         { alignas(8) uint8_t reserved[0x100]; };
struct NVNmemoryPoolBuilder  { alignas(8) uint8_t reserved[0x80]; };
struct NVNbuffer             { alignas(8) uint8_t reserved[0x80]; };
struct NVNbufferBuilder      { alignas(8) uint8_t reserved[0x80]; };
struct NVNtexture            { alignas(8) uint8_t reserved[0x100]; };
struct NVNtextureBuilder     { alignas(8) uint8_t reserved[0x100]; };
struct NVNsampler            { alignas(8) uint8_t reserved[0x80]; };
struct NVNsamplerBuilder     { alignas(8) uint8_t reserved[0x80]; };
struct NVNtexturePool        { alignas(8) uint8_t reserved[0x80]; };
struct NVNsamplerPool        { alignas(8) uint8_t reserved[0x80]; };
struct NVNvertexAttribState  { alignas(4) uint8_t reserved[0x4]; };
struct NVNvertexStreamState  { alignas(8) uint8_t reserved[0x8]; };
struct NVNdepthStencilState  { alignas(8) uint8_t reserved[0x80]; };
struct NVNpolygonState       { alignas(8) uint8_t reserved[0x80]; };
struct NVNcolorState         { alignas(8) uint8_t reserved[0x80]; };
struct NVNchannelMaskState   { alignas(8) uint8_t reserved[0x80]; };
struct NVNblendState         { alignas(8) uint8_t reserved[0x80]; };

using TextureHandle = uint64_t;
using CommandBuffer = NVNcommandBuffer;
using Device        = NVNdevice;
using Program       = NVNprogram;

struct TextureVertex {
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
};

namespace Format {
    constexpr int32_t RGBA8        = 0x25;
    constexpr int32_t R8           = 0x01;
    constexpr int32_t Float4       = 0x2e; // NVN_FORMAT_RGBA32F, confirmed against PrimitiveDrawMgrNvn
    constexpr int32_t Float2       = 0x16; // NVN_FORMAT_RG32F, confirmed against the working nvn_overlay.hpp texture pipeline
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

namespace Stage {
    constexpr int32_t Vertex       = 0;
    constexpr int32_t Fragment     = 1;
}

namespace StageMask {
    constexpr uint32_t Vertex         = 0x1;
    constexpr uint32_t Fragment       = 0x2;
    constexpr uint32_t VertexFragment = 0x3;
}

namespace Topology {
    constexpr int32_t Triangles     = 4;
    constexpr int32_t TriangleStrip = 5;
}

using DrawCallback = void (*)(CommandBuffer* cmdBuf, void* dstTexture, int width, int height);
using InitCallback = void (*)();

namespace impl {

namespace Offset {
    constexpr uintptr_t kGraphicsNvnInstanceSlot        = 0x2594c10;
    constexpr uintptr_t kGraphicsNvnDeviceOffset        = 0x30;
    constexpr uintptr_t kMemoryPoolBuilderSetDevice     = 0x2596a88;
    constexpr uintptr_t kMemoryPoolBuilderSetDefaults   = 0x2596a90;
    constexpr uintptr_t kMemoryPoolBuilderSetStorage    = 0x2596a98;
    constexpr uintptr_t kMemoryPoolBuilderSetFlags      = 0x2596aa0;
    constexpr uintptr_t kMemoryPoolInitialize           = 0x2596ac0;
    constexpr uintptr_t kBufferBuilderSetDevice         = 0x2596b88;
    constexpr uintptr_t kBufferBuilderSetDefaults       = 0x2596b90;
    constexpr uintptr_t kBufferBuilderSetStorage        = 0x2596b98;
    constexpr uintptr_t kBufferInitialize               = 0x2596bb8;
    constexpr uintptr_t kBufferGetAddress               = 0x2596bd8;
    constexpr uintptr_t kBufferMap                      = 0x2596bd0;
    constexpr uintptr_t kProgramInitialize              = 0x2596a68;
    constexpr uintptr_t kProgramSetShaders              = 0x2596a80;
    constexpr uintptr_t kBlendStateSetDefaults          = 0x2596fd8;
    constexpr uintptr_t kBlendStateSetBlendTarget       = 0x2596fe0;
    constexpr uintptr_t kBlendStateSetBlendFunc         = 0x2596fe8;
    constexpr uintptr_t kBlendStateSetBlendEquation     = 0x2596ff0;
    constexpr uintptr_t kColorStateSetDefaults          = 0x2597050;
    constexpr uintptr_t kColorStateSetBlendEnable       = 0x2597058;
    constexpr uintptr_t kChannelMaskStateSetDefaults    = 0x2597088;
    constexpr uintptr_t kChannelMaskStateSetChannelMask = 0x25970a0;
    constexpr uintptr_t kPolygonStateSetDefaults        = 0x2597158;
    constexpr uintptr_t kPolygonStateSetCullFace        = 0x2597160;
    constexpr uintptr_t kDepthStencilStateSetDefaults   = 0x25971a0;
    constexpr uintptr_t kDepthStencilStateSetDepthTestEnable = 0x25971a8;
    constexpr uintptr_t kDepthStencilStateSetDepthWriteEnable = 0x25971b0;
    constexpr uintptr_t kVertexAttribStateSetDefaults   = 0x2597208;
    constexpr uintptr_t kVertexAttribStateSetFormat     = 0x2597210;
    constexpr uintptr_t kVertexAttribStateSetStreamIndex= 0x2597218;
    constexpr uintptr_t kVertexStreamStateSetDefaults   = 0x2597230;
    constexpr uintptr_t kVertexStreamStateSetStride     = 0x2597238;
    constexpr uintptr_t kCommandBufferEndRecording      = 0x25972c8;
    constexpr uintptr_t kCommandBufferBindProgram       = 0x2597320;
    constexpr uintptr_t kCommandBufferBindBlendState    = 0x25972e0;
    constexpr uintptr_t kCommandBufferBindChannelMaskState = 0x25972e8;
    constexpr uintptr_t kCommandBufferBindColorState    = 0x25972f0;
    constexpr uintptr_t kCommandBufferBindPolygonState  = 0x2597300;
    constexpr uintptr_t kCommandBufferBindDepthStencilState = 0x2597308;
    constexpr uintptr_t kCommandBufferBindVertexAttribState = 0x2597310;
    constexpr uintptr_t kCommandBufferBindVertexStreamState = 0x2597318;
    constexpr uintptr_t kCommandBufferBindVertexBuffer  = 0x2597328;
    constexpr uintptr_t kCommandBufferDrawArrays        = 0x25973d0;
    constexpr uintptr_t kCommandBufferSetViewport       = 0x2597448;
    constexpr uintptr_t kCommandBufferSetScissor        = 0x2597460;
    constexpr uintptr_t kCommandBufferSetRenderTargets  = 0x2597598;
    constexpr uintptr_t kCommandBufferSetTexturePool    = 0x25975e8;
    constexpr uintptr_t kCommandBufferSetSamplerPool    = 0x25975f0;
}

inline void armDCacheFlush(void* addr, size_t size) {
    uintptr_t start = reinterpret_cast<uintptr_t>(addr) & ~0x3FULL;
    uintptr_t end = (reinterpret_cast<uintptr_t>(addr) + size + 0x3FULL) & ~0x3FULL;
    for (uintptr_t p = start; p < end; p += 64) {
        asm volatile("dc cvac, %0" : : "r"(p) : "memory");
    }
    asm volatile("dsb sy" : : : "memory");
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
template <typename Fn>
inline Fn ReadIndirect(uintptr_t base) {
    if (!base) return nullptr;
    uintptr_t cell = *reinterpret_cast<uintptr_t*>(base);
    if (!cell) return nullptr;
    return reinterpret_cast<Fn>(*reinterpret_cast<void**>(cell));
}
#pragma GCC diagnostic pop

template <typename Fn>
inline Fn NvnFn(uintptr_t offset) {
    return ReadIndirect<Fn>(WiiXLaunch::ResolveTarget(offset));
}

inline void* ResolveNvnByName(const char* name) {
    static uintptr_t s_SdkBase = 0;
    if (!s_SdkBase) {
        auto sdkInfo = exl::util::GetModuleInfo(exl::util::ModuleIndex::Sdk);
        s_SdkBase = sdkInfo.m_Total.m_Start;
    }
    if (!s_SdkBase) return nullptr;

    using ResolverFn = void* (*)(long, const char*);
    auto resolver = reinterpret_cast<ResolverFn>(s_SdkBase + 0x2e0534);
    return resolver(0, name);
}

template<typename Fn>
inline Fn ResolveFn(const char* name) {
    return reinterpret_cast<Fn>(ResolveNvnByName(name));
}

using FnMemoryPoolBuilderSetDefaults = void (*)(NVNmemoryPoolBuilder*);
using FnMemoryPoolBuilderSetDevice   = void (*)(NVNmemoryPoolBuilder*, Device*);
using FnMemoryPoolBuilderSetFlags    = void (*)(NVNmemoryPoolBuilder*, int);
using FnMemoryPoolBuilderSetStorage  = void (*)(NVNmemoryPoolBuilder*, void*, size_t);
using FnMemoryPoolInitialize         = int  (*)(NVNmemoryPool*, const NVNmemoryPoolBuilder*);

using FnBufferBuilderSetDefaults     = void (*)(NVNbufferBuilder*);
using FnBufferBuilderSetDevice       = void (*)(NVNbufferBuilder*, Device*);
using FnBufferBuilderSetStorage      = void (*)(NVNbufferBuilder*, NVNmemoryPool*, ptrdiff_t, size_t);
using FnBufferInitialize             = int  (*)(NVNbuffer*, const NVNbufferBuilder*);
using FnBufferGetAddress             = uint64_t (*)(const NVNbuffer*);
using FnBufferMap                    = void* (*)(const NVNbuffer*);

using FnTextureBuilderSetDevice      = void (*)(NVNtextureBuilder*, Device*);
using FnTextureBuilderSetDefaults    = void (*)(NVNtextureBuilder*);
using FnTextureBuilderSetTarget      = void (*)(NVNtextureBuilder*, int);
using FnTextureBuilderSetSize2D      = void (*)(NVNtextureBuilder*, int, int);
using FnTextureBuilderSetFormat      = void (*)(NVNtextureBuilder*, int);
using FnTextureBuilderSetStorage     = void (*)(NVNtextureBuilder*, NVNmemoryPool*, ptrdiff_t);
using FnTextureBuilderSetPackagedTextureData = void (*)(NVNtextureBuilder*, const void*);
using FnTextureInitialize            = uint8_t (*)(NVNtexture*, const NVNtextureBuilder*);
using FnTexturePoolInitialize        = uint8_t (*)(NVNtexturePool*, NVNmemoryPool*, ptrdiff_t, int);
using FnTexturePoolRegisterTexture   = void (*)(const NVNtexturePool*, int, const NVNtexture*, const void*);
using FnDeviceGetTextureHandle       = TextureHandle (*)(const Device*, int, int);

using FnSamplerBuilderSetDevice      = void (*)(NVNsamplerBuilder*, Device*);
using FnSamplerBuilderSetDefaults    = void (*)(NVNsamplerBuilder*);
using FnSamplerBuilderSetMinMagFilter = void (*)(NVNsamplerBuilder*, int, int);
using FnSamplerBuilderSetWrapMode    = void (*)(NVNsamplerBuilder*, int, int, int);
using FnSamplerInitialize            = uint8_t (*)(NVNsampler*, const NVNsamplerBuilder*);
using FnSamplerPoolInitialize        = uint8_t (*)(NVNsamplerPool*, NVNmemoryPool*, ptrdiff_t, int);
using FnSamplerPoolRegisterSampler   = void (*)(const NVNsamplerPool*, int, const NVNsampler*);

using FnProgramInitialize            = uint8_t (*)(Program*, Device*);
struct NVNshaderData                 { uint64_t data; uint64_t control; };
using FnProgramSetShaders            = uint8_t (*)(Program*, int, const NVNshaderData*);

using FnVertexAttribStateSetDefaults = void (*)(NVNvertexAttribState*);
using FnVertexAttribStateSetFormat   = void (*)(NVNvertexAttribState*, int, ptrdiff_t);
using FnVertexAttribStateSetStreamIndex = void (*)(NVNvertexAttribState*, int);
using FnVertexStreamStateSetDefaults = void (*)(NVNvertexStreamState*);
using FnVertexStreamStateSetStride   = void (*)(NVNvertexStreamState*, ptrdiff_t);

using FnBlendStateSetDefaults        = void (*)(NVNblendState*);
using FnBlendStateSetBlendTarget     = void (*)(NVNblendState*, int);
using FnBlendStateSetBlendFunc       = void (*)(NVNblendState*, int, int, int, int);
using FnBlendStateSetBlendEquation   = void (*)(NVNblendState*, int, int);

using FnColorStateSetDefaults        = void (*)(NVNcolorState*);
using FnColorStateSetBlendEnable     = void (*)(NVNcolorState*, int, uint8_t);

using FnChannelMaskStateSetDefaults  = void (*)(NVNchannelMaskState*);
using FnChannelMaskStateSetChannelMask = void (*)(NVNchannelMaskState*, int, uint8_t, uint8_t, uint8_t, uint8_t);

using FnPolygonStateSetDefaults      = void (*)(NVNpolygonState*);
using FnPolygonStateSetCullFace      = void (*)(NVNpolygonState*, int);

using FnDepthStencilStateSetDefaults = void (*)(NVNdepthStencilState*);
using FnDepthStencilStateSetDepthTestEnable = void (*)(NVNdepthStencilState*, uint8_t);
using FnDepthStencilStateSetDepthWriteEnable = void (*)(NVNdepthStencilState*, uint8_t);

using FnCommandBufferSetViewport     = void (*)(CommandBuffer*, int, int, int, int);
using FnCommandBufferSetScissor      = void (*)(CommandBuffer*, int, int, int, int);
using FnCommandBufferBindProgram     = void (*)(CommandBuffer*, const Program*, uint32_t);
using FnCommandBufferBindBlendState  = void (*)(CommandBuffer*, const NVNblendState*);
using FnCommandBufferBindColorState  = void (*)(CommandBuffer*, const NVNcolorState*);
using FnCommandBufferBindPolygonState= void (*)(CommandBuffer*, const NVNpolygonState*);
using FnCommandBufferBindDepthStencilState = void (*)(CommandBuffer*, const NVNdepthStencilState*);
using FnCommandBufferBindChannelMaskState = void (*)(CommandBuffer*, const NVNchannelMaskState*);
using FnCommandBufferBindVertexAttribState = void (*)(CommandBuffer*, int, const NVNvertexAttribState*);
using FnCommandBufferBindVertexStreamState = void (*)(CommandBuffer*, int, const NVNvertexStreamState*);
using FnCommandBufferBindVertexBuffer = void (*)(CommandBuffer*, int, uint64_t, size_t);
using FnCommandBufferBindTexture     = void (*)(CommandBuffer*, int32_t, int, TextureHandle);
using FnCommandBufferSetTexturePool  = void (*)(CommandBuffer*, const NVNtexturePool*);
using FnCommandBufferSetSamplerPool  = void (*)(CommandBuffer*, const NVNsamplerPool*);
using FnCommandBufferSetRenderTargets = void (*)(CommandBuffer*, int, const void* const*, const void*, const void*, const void*);
using FnCommandBufferDrawArrays      = void (*)(CommandBuffer*, int, int, int);
using FnCommandBufferEndRecording    = uint64_t (*)(CommandBuffer*);

inline void*& RawGameFramework() {
    static void* p = nullptr;
    return p;
}

inline constexpr size_t kMaxCallbacks = 16;

inline DrawCallback* DrawCallbacks() {
    static DrawCallback cbs[kMaxCallbacks] = {};
    return cbs;
}

inline size_t& DrawCallbackCount() {
    static size_t count = 0;
    return count;
}

inline InitCallback* InitCallbacks() {
    static InitCallback cbs[kMaxCallbacks] = {};
    return cbs;
}

inline size_t& InitCallbackCount() {
    static size_t count = 0;
    return count;
}

inline bool& PipelineInitialized() {
    static bool init = false;
    return init;
}

inline void* GetGraphicsNvn() {
    return ReadIndirect<void*>(WiiXLaunch::ResolveTarget(Offset::kGraphicsNvnInstanceSlot));
}

inline Device* GetDevice() {
    void* gn = GetGraphicsNvn();
    if (!gn) return nullptr;
    return *reinterpret_cast<Device**>(static_cast<uint8_t*>(gn) + Offset::kGraphicsNvnDeviceOffset);
}

inline NVNmemoryPool g_TexDescMemoryPool{};
inline NVNmemoryPool g_SmpDescMemoryPool{};
inline NVNtexturePool g_TexturePool{};
inline NVNsamplerPool g_SamplerPool{};
alignas(4096) inline uint8_t g_TexDescMemory[0x4000]{};
alignas(4096) inline uint8_t g_SmpDescMemory[0x4000]{};
inline int& NextTextureId() {
    static int id = 256;
    return id;
}
inline int& NextSamplerId() {
    static int id = 256;
    return id;
}

// Fixed pool of static storage for created textures so no heap 'new' is needed
constexpr size_t kMaxStaticTextures = 16;
inline NVNmemoryPool g_StaticTexturePools[kMaxStaticTextures]{};
inline NVNtexture    g_StaticTextures[kMaxStaticTextures]{};
inline NVNsampler    g_StaticSamplers[kMaxStaticTextures]{};
inline size_t        g_StaticTextureCount = 0;

inline NVNmemoryPool g_SpriteCodePool{};
inline NVNbuffer g_SpriteCodeBuffer{};
alignas(4096) inline uint8_t g_SpriteCodeMemory[0x10000]{};
inline Program g_DefaultUiProgram{};
inline bool g_DefaultUiProgramReady = false;

inline NVNmemoryPool g_SpriteDataPool{};
inline NVNbuffer g_SpriteDataBuffer{};
alignas(4096) inline uint8_t g_SpriteDataMemory[0x4000]{};
inline NVNvertexAttribState g_SpriteAttribStates[3]{};
inline NVNvertexStreamState g_SpriteStreamState{};

inline NVNdepthStencilState g_OverlayDepthStencilState{};
inline NVNpolygonState      g_OverlayPolygonState{};
inline NVNcolorState        g_OverlayColorState{};
inline NVNblendState        g_OverlayBlendState{};
inline NVNchannelMaskState  g_OverlayChannelMaskState{};

inline FnCommandBufferEndRecording g_OrigCommandBufferEndRecording = nullptr;

inline void EnsureDescriptorPools(Device* device) {
    static bool ready = false;
    if (ready) return;

    auto poolBuilderSetDefaults = ResolveFn<FnMemoryPoolBuilderSetDefaults>("nvnMemoryPoolBuilderSetDefaults");
    auto poolBuilderSetDevice   = ResolveFn<FnMemoryPoolBuilderSetDevice>("nvnMemoryPoolBuilderSetDevice");
    auto poolBuilderSetFlags    = ResolveFn<FnMemoryPoolBuilderSetFlags>("nvnMemoryPoolBuilderSetFlags");
    auto poolBuilderSetStorage  = ResolveFn<FnMemoryPoolBuilderSetStorage>("nvnMemoryPoolBuilderSetStorage");
    auto poolInitialize         = ResolveFn<FnMemoryPoolInitialize>("nvnMemoryPoolInitialize");
    auto texPoolInitialize      = ResolveFn<FnTexturePoolInitialize>("nvnTexturePoolInitialize");
    auto smpPoolInitialize      = ResolveFn<FnSamplerPoolInitialize>("nvnSamplerPoolInitialize");

    if (!poolBuilderSetDefaults || !poolBuilderSetDevice || !poolBuilderSetFlags || !poolBuilderSetStorage ||
        !poolInitialize || !texPoolInitialize || !smpPoolInitialize) {
        WIIXL_LOG("WiiXLaunch: NVN EnsureDescriptorPools symbol resolution failed");
        return;
    }

    constexpr int kNumDescriptors = 320;

    NVNmemoryPoolBuilder texDescBuilder{};
    poolBuilderSetDefaults(&texDescBuilder);
    poolBuilderSetDevice(&texDescBuilder, device);
    poolBuilderSetFlags(&texDescBuilder, 0x22);
    poolBuilderSetStorage(&texDescBuilder, g_TexDescMemory, sizeof(g_TexDescMemory));
    poolInitialize(&g_TexDescMemoryPool, &texDescBuilder);
    texPoolInitialize(&g_TexturePool, &g_TexDescMemoryPool, 0, kNumDescriptors);

    NVNmemoryPoolBuilder smpDescBuilder{};
    poolBuilderSetDefaults(&smpDescBuilder);
    poolBuilderSetDevice(&smpDescBuilder, device);
    poolBuilderSetFlags(&smpDescBuilder, 0x22);
    poolBuilderSetStorage(&smpDescBuilder, g_SmpDescMemory, sizeof(g_SmpDescMemory));
    poolInitialize(&g_SmpDescMemoryPool, &smpDescBuilder);
    smpPoolInitialize(&g_SamplerPool, &g_SmpDescMemoryPool, 0, kNumDescriptors);

    ready = true;
    WIIXL_LOG("WiiXLaunch: NVN EnsureDescriptorPools OK");
}

inline Program* LoadSeadBinaryProgram(
    void* graphicsNvn,
    const uint8_t* seadBytes, size_t seadSize,
    uint8_t* codePoolMemory, size_t codePoolMemorySize,
    NVNmemoryPool* codeMemoryPool, NVNbuffer* codeBuffer,
    Program* programStorage, const char* label)
{
    if (!graphicsNvn) return nullptr;
    Device* device = GetDevice();
    if (!device) return nullptr;

    if (seadSize > codePoolMemorySize) {
        WIIXL_LOG("WiiXLaunch: %s sead binary (%u bytes) exceeds pool memory (%u bytes)",
            label, static_cast<unsigned int>(seadSize), static_cast<unsigned int>(codePoolMemorySize));
        return nullptr;
    }

    // 1. Copy sead-formatted binary into code pool memory FIRST
    __builtin_memcpy(codePoolMemory, seadBytes, seadSize);

    // 2. Initialize Code Memory Pool (flag 0x62)
    NVNmemoryPoolBuilder codePoolBuilder{};
    NvnFn<FnMemoryPoolBuilderSetDefaults>(Offset::kMemoryPoolBuilderSetDefaults)(&codePoolBuilder);
    NvnFn<FnMemoryPoolBuilderSetDevice>(Offset::kMemoryPoolBuilderSetDevice)(&codePoolBuilder, device);
    NvnFn<FnMemoryPoolBuilderSetFlags>(Offset::kMemoryPoolBuilderSetFlags)(&codePoolBuilder, 0x62);
    NvnFn<FnMemoryPoolBuilderSetStorage>(Offset::kMemoryPoolBuilderSetStorage)(&codePoolBuilder, codePoolMemory, codePoolMemorySize);
    int poolResult = NvnFn<FnMemoryPoolInitialize>(Offset::kMemoryPoolInitialize)(codeMemoryPool, &codePoolBuilder);
    WIIXL_LOG("WiiXLaunch: %s CodePoolInitialize result=%d", label, poolResult);
    if (!poolResult) return nullptr;

    // 3. Initialize Buffer on the Code Memory Pool
    NVNbufferBuilder codeBufBuilder{};
    NvnFn<FnBufferBuilderSetDevice>(Offset::kBufferBuilderSetDevice)(&codeBufBuilder, device);
    NvnFn<FnBufferBuilderSetDefaults>(Offset::kBufferBuilderSetDefaults)(&codeBufBuilder);
    NvnFn<FnBufferBuilderSetStorage>(Offset::kBufferBuilderSetStorage)(&codeBufBuilder, codeMemoryPool, 0, codePoolMemorySize);
    uint8_t bufResult = NvnFn<FnBufferInitialize>(Offset::kBufferInitialize)(codeBuffer, &codeBufBuilder);
    WIIXL_LOG("WiiXLaunch: %s CodeBufferInitialize result=%d", label, bufResult);
    if (!bufResult) return nullptr;

    uint64_t codeGpuBase = NvnFn<FnBufferGetAddress>(Offset::kBufferGetAddress)(codeBuffer);

    // 4. Match device ISA version fields in the control headers inside the pool
    int32_t deviceIsaA = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(device) + 0x2c);
    int32_t deviceIsaB = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(device) + 0x30);

    uint32_t* hdr = reinterpret_cast<uint32_t*>(codePoolMemory);
    uint32_t offVertCtrl = hdr[0];
    uint32_t offFragCtrl = hdr[1];
    uint32_t offVertCode = hdr[2];
    uint32_t offFragCode = hdr[3];

    uint8_t* vertControl = codePoolMemory + offVertCtrl;
    uint8_t* fragControl = codePoolMemory + offFragCtrl;

    uint32_t* vWords = reinterpret_cast<uint32_t*>(vertControl);
    uint32_t* fWords = reinterpret_cast<uint32_t*>(fragControl);

    vWords[2] = 0x0e; // Match BotW NVN 4.4.0 driver control header version
    fWords[2] = 0x0e;

    vWords[3] = deviceIsaA;
    vWords[4] = deviceIsaB;
    fWords[3] = deviceIsaA;
    fWords[4] = deviceIsaB;

    NVNshaderData stages[2] = {
        { codeGpuBase + offVertCode, reinterpret_cast<uint64_t>(vertControl) },
        { codeGpuBase + offFragCode, reinterpret_cast<uint64_t>(fragControl) },
    };

    auto programInitialize = NvnFn<FnProgramInitialize>(Offset::kProgramInitialize);
    auto programSetShaders = NvnFn<FnProgramSetShaders>(Offset::kProgramSetShaders);

    uint8_t initResult = programInitialize(programStorage, device);
    WIIXL_LOG("WiiXLaunch: %s ProgramInitialize result=%d", label, initResult);
    if (!initResult) return nullptr;

    uint8_t setShadersResult = programSetShaders(programStorage, 2, stages);
    WIIXL_LOG("WiiXLaunch: %s ProgramSetShaders result=%d", label, setShadersResult);
    if (!setShadersResult) return nullptr;

    return programStorage;
}

inline void EnsureDefaultUiSpritePipeline() {
    if (g_DefaultUiProgramReady) return;
    void* gn = GetGraphicsNvn();
    Device* dev = GetDevice();
    if (!gn || !dev) return;

    Program* prog = LoadSeadBinaryProgram(
        gn, Shaders::g_TextureQuadSeadBin, Shaders::kTextureQuadSeadBinSize,
        g_SpriteCodeMemory, sizeof(g_SpriteCodeMemory),
        &g_SpriteCodePool, &g_SpriteCodeBuffer,
        &g_DefaultUiProgram, "DefaultUiSprite");
    if (!prog) return;

    auto poolBuilderSetDefaults = NvnFn<FnMemoryPoolBuilderSetDefaults>(Offset::kMemoryPoolBuilderSetDefaults);
    auto poolBuilderSetDevice   = NvnFn<FnMemoryPoolBuilderSetDevice>(Offset::kMemoryPoolBuilderSetDevice);
    auto poolBuilderSetFlags    = NvnFn<FnMemoryPoolBuilderSetFlags>(Offset::kMemoryPoolBuilderSetFlags);
    auto poolBuilderSetStorage  = NvnFn<FnMemoryPoolBuilderSetStorage>(Offset::kMemoryPoolBuilderSetStorage);
    auto poolInitialize         = NvnFn<FnMemoryPoolInitialize>(Offset::kMemoryPoolInitialize);

    auto bufBuilderSetDefaults  = NvnFn<FnBufferBuilderSetDefaults>(Offset::kBufferBuilderSetDefaults);
    auto bufBuilderSetDevice    = NvnFn<FnBufferBuilderSetDevice>(Offset::kBufferBuilderSetDevice);
    auto bufBuilderSetStorage   = NvnFn<FnBufferBuilderSetStorage>(Offset::kBufferBuilderSetStorage);
    auto bufferInitialize       = NvnFn<FnBufferInitialize>(Offset::kBufferInitialize);

    auto vaDefaults = NvnFn<FnVertexAttribStateSetDefaults>(Offset::kVertexAttribStateSetDefaults);
    auto vaFormat   = NvnFn<FnVertexAttribStateSetFormat>(Offset::kVertexAttribStateSetFormat);
    auto vaStream   = NvnFn<FnVertexAttribStateSetStreamIndex>(Offset::kVertexAttribStateSetStreamIndex);
    auto vsDefaults = NvnFn<FnVertexStreamStateSetDefaults>(Offset::kVertexStreamStateSetDefaults);
    auto vsStride   = NvnFn<FnVertexStreamStateSetStride>(Offset::kVertexStreamStateSetStride);

    auto dsSetDefaults = NvnFn<FnDepthStencilStateSetDefaults>(Offset::kDepthStencilStateSetDefaults);
    auto dsSetTest     = NvnFn<FnDepthStencilStateSetDepthTestEnable>(Offset::kDepthStencilStateSetDepthTestEnable);
    auto dsSetWrite    = NvnFn<FnDepthStencilStateSetDepthWriteEnable>(Offset::kDepthStencilStateSetDepthWriteEnable);

    auto polySetDefaults = NvnFn<FnPolygonStateSetDefaults>(Offset::kPolygonStateSetDefaults);
    auto polySetCull     = NvnFn<FnPolygonStateSetCullFace>(Offset::kPolygonStateSetCullFace);

    auto blendSetDefaults = NvnFn<FnBlendStateSetDefaults>(Offset::kBlendStateSetDefaults);

    auto colSetDefaults   = NvnFn<FnColorStateSetDefaults>(Offset::kColorStateSetDefaults);
    auto colSetBlend      = NvnFn<FnColorStateSetBlendEnable>(Offset::kColorStateSetBlendEnable);

    auto chanSetDefaults  = NvnFn<FnChannelMaskStateSetDefaults>(Offset::kChannelMaskStateSetDefaults);
    auto chanSetMask      = NvnFn<FnChannelMaskStateSetChannelMask>(Offset::kChannelMaskStateSetChannelMask);

    if (!poolBuilderSetDefaults || !poolBuilderSetDevice || !poolBuilderSetFlags || !poolBuilderSetStorage ||
        !poolInitialize || !bufBuilderSetDefaults || !bufBuilderSetDevice || !bufBuilderSetStorage ||
        !bufferInitialize || !vaDefaults || !vaFormat || !vaStream || !vsDefaults || !vsStride) {
        return;
    }

    NVNmemoryPoolBuilder dataPoolBuilder{};
    poolBuilderSetDefaults(&dataPoolBuilder);
    poolBuilderSetDevice(&dataPoolBuilder, dev);
    poolBuilderSetFlags(&dataPoolBuilder, 0x22);
    poolBuilderSetStorage(&dataPoolBuilder, g_SpriteDataMemory, sizeof(g_SpriteDataMemory));
    poolInitialize(&g_SpriteDataPool, &dataPoolBuilder);

    NVNbufferBuilder dataBufferBuilder{};
    bufBuilderSetDefaults(&dataBufferBuilder);
    bufBuilderSetDevice(&dataBufferBuilder, dev);
    bufBuilderSetStorage(&dataBufferBuilder, &g_SpriteDataPool, 0, sizeof(g_SpriteDataMemory));
    bufferInitialize(&g_SpriteDataBuffer, &dataBufferBuilder);

    vaDefaults(&g_SpriteAttribStates[0]);
    vaFormat(&g_SpriteAttribStates[0], Format::Float4, 0);
    vaStream(&g_SpriteAttribStates[0], 0);

    vaDefaults(&g_SpriteAttribStates[1]);
    vaFormat(&g_SpriteAttribStates[1], Format::Float2, 16);
    vaStream(&g_SpriteAttribStates[1], 0);

    vaDefaults(&g_SpriteAttribStates[2]);
    vaFormat(&g_SpriteAttribStates[2], Format::Float4, 24);
    vaStream(&g_SpriteAttribStates[2], 0);

    vsDefaults(&g_SpriteStreamState);
    vsStride(&g_SpriteStreamState, sizeof(TextureVertex));

    if (dsSetDefaults) dsSetDefaults(&g_OverlayDepthStencilState);
    if (dsSetTest) dsSetTest(&g_OverlayDepthStencilState, 0);
    if (dsSetWrite) dsSetWrite(&g_OverlayDepthStencilState, 0);

    if (polySetDefaults) polySetDefaults(&g_OverlayPolygonState);
    if (polySetCull) polySetCull(&g_OverlayPolygonState, 0);

    // Disable blend completely so we guarantee we can see our texture with no alpha issues.
    // NVN ui blending is more complex and might depend on frame buffer state.
    if (blendSetDefaults) blendSetDefaults(&g_OverlayBlendState);
    if (colSetDefaults) colSetDefaults(&g_OverlayColorState);
    if (colSetBlend) colSetBlend(&g_OverlayColorState, 0, 0);

    if (chanSetDefaults) chanSetDefaults(&g_OverlayChannelMaskState);
    if (chanSetMask) chanSetMask(&g_OverlayChannelMaskState, 0, 1, 1, 1, 1);

    g_DefaultUiProgramReady = true;
    WIIXL_LOG("WiiXLaunch: NVN EnsureDefaultUiSpritePipeline OK");
}

inline void InitializeAllPipelines(void* gameFramework) {
    RawGameFramework() = gameFramework;
    Device* dev = GetDevice();
    if (!dev) return;

    EnsureDescriptorPools(dev);
    EnsureDefaultUiSpritePipeline();

    if (g_DefaultUiProgramReady && !PipelineInitialized()) {
        PipelineInitialized() = true;
        for (size_t i = 0; i < InitCallbackCount(); ++i) {
            if (InitCallbacks()[i]) InitCallbacks()[i]();
        }
    }
}

inline uint64_t HookedCommandBufferEndRecording(CommandBuffer* cmdBuf) {
    if (PipelineInitialized() && RawGameFramework()) {
        void* mainCmdBuf = *reinterpret_cast<void**>(static_cast<uint8_t*>(RawGameFramework()) + 0x158);
        if (cmdBuf == mainCmdBuf) {
            void* gn = GetGraphicsNvn();
            if (gn) {
                void* displayBuffer = *reinterpret_cast<void**>(static_cast<uint8_t*>(gn) + 0x50);
                if (displayBuffer) {
                    uint32_t activeIdx = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(displayBuffer) + 0x20);
                    if (activeIdx >= 3) activeIdx = 0;
                    void* dstTexture = *reinterpret_cast<void**>(static_cast<uint8_t*>(displayBuffer) + 0x28 + activeIdx * 8);
                    int width = 1280;
                    int height = 720;
                    float fW = *reinterpret_cast<float*>(static_cast<uint8_t*>(displayBuffer) + 8);
                    float fH = *reinterpret_cast<float*>(static_cast<uint8_t*>(displayBuffer) + 12);
                    if (fW > 0.0f && fH > 0.0f) {
                        width = static_cast<int>(fW);
                        height = static_cast<int>(fH);
                    }
                    if (dstTexture) {
                        static uint32_t s_DrawCount = 0;
                        if ((s_DrawCount++ % 120) == 0) {
                            WIIXL_LOG("WiiXLaunch: Injected frame #%u (dst=%p, %dx%d, cbCount=%u)",
                                s_DrawCount, dstTexture, width, height, static_cast<unsigned int>(DrawCallbackCount()));
                        }
                        for (size_t i = 0; i < DrawCallbackCount(); ++i) {
                            if (DrawCallbacks()[i]) {
                                DrawCallbacks()[i](cmdBuf, dstTexture, width, height);
                            }
                        }
                    }
                }
            }
        }
    }
    return g_OrigCommandBufferEndRecording(cmdBuf);
}

inline void EnsureEndRecordingHookInstalled() {
    static bool installed = false;
    if (installed) return;
    uintptr_t targetStart = exl::util::modules::GetTargetStart();
    uintptr_t slotAddr = targetStart + Offset::kCommandBufferEndRecording;
    uintptr_t cell = *reinterpret_cast<uintptr_t*>(slotAddr);
    void** fnPtrLoc = reinterpret_cast<void**>(cell);
    if (fnPtrLoc && *fnPtrLoc) {
        g_OrigCommandBufferEndRecording = reinterpret_cast<FnCommandBufferEndRecording>(*fnPtrLoc);
        *fnPtrLoc = reinterpret_cast<void*>(&HookedCommandBufferEndRecording);
        installed = true;
        WIIXL_LOG("WiiXLaunch: NVN EndRecording hook installed OK (orig=%p, cell=%p)", g_OrigCommandBufferEndRecording, fnPtrLoc);
    }
}

WIIXL_HOOK_DEFINE_TRAMPOLINE(ProcDrawHook) {
    static void Callback(void* gameFramework) {
        RawGameFramework() = gameFramework;
        void* gn = GetGraphicsNvn();
        if (gn) {
            InitializeAllPipelines(gameFramework);
            EnsureEndRecordingHookInstalled();
        }
        Orig(gameFramework);
    }
};

} // namespace impl

inline void Init() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    impl::ProcDrawHook::Install(0xaf90a8, 0);
}

inline void RegisterDrawCallback(DrawCallback cb) {
    if (impl::DrawCallbackCount() < impl::kMaxCallbacks) {
        impl::DrawCallbacks()[impl::DrawCallbackCount()++] = cb;
    }
}

inline void OnInitialized(InitCallback cb) {
    if (impl::PipelineInitialized()) {
        cb();
    } else if (impl::InitCallbackCount() < impl::kMaxCallbacks) {
        impl::InitCallbacks()[impl::InitCallbackCount()++] = cb;
    }
}

inline Device* GetDevice() {
    return impl::GetDevice();
}

inline void* GetGraphicsNvn() {
    return impl::GetGraphicsNvn();
}

inline TextureHandle CreateTexture(
    const void* packagedData,
    size_t packagedSize,
    int minFilter = Filter::Linear,
    int magFilter = Filter::Linear,
    int wrapMode = WrapMode::ClampToEdge)
{
    Device* device = GetDevice();
    if (!device || !packagedData || impl::g_StaticTextureCount >= impl::kMaxStaticTextures) return 0;

    auto poolBuilderSetDefaults = impl::ResolveFn<impl::FnMemoryPoolBuilderSetDefaults>("nvnMemoryPoolBuilderSetDefaults");
    auto poolBuilderSetDevice   = impl::ResolveFn<impl::FnMemoryPoolBuilderSetDevice>("nvnMemoryPoolBuilderSetDevice");
    auto poolBuilderSetFlags    = impl::ResolveFn<impl::FnMemoryPoolBuilderSetFlags>("nvnMemoryPoolBuilderSetFlags");
    auto poolBuilderSetStorage  = impl::ResolveFn<impl::FnMemoryPoolBuilderSetStorage>("nvnMemoryPoolBuilderSetStorage");
    auto poolInitialize         = impl::ResolveFn<impl::FnMemoryPoolInitialize>("nvnMemoryPoolInitialize");

    auto texBuilderSetDevice    = impl::ResolveFn<impl::FnTextureBuilderSetDevice>("nvnTextureBuilderSetDevice");
    auto texBuilderSetDefaults  = impl::ResolveFn<impl::FnTextureBuilderSetDefaults>("nvnTextureBuilderSetDefaults");
    auto texBuilderSetTarget    = impl::ResolveFn<impl::FnTextureBuilderSetTarget>("nvnTextureBuilderSetTarget");
    auto texBuilderSetSize2D    = impl::ResolveFn<impl::FnTextureBuilderSetSize2D>("nvnTextureBuilderSetSize2D");
    auto texBuilderSetFormat    = impl::ResolveFn<impl::FnTextureBuilderSetFormat>("nvnTextureBuilderSetFormat");
    auto texBuilderSetStorage   = impl::ResolveFn<impl::FnTextureBuilderSetStorage>("nvnTextureBuilderSetStorage");
    auto texBuilderSetPackaged  = impl::ResolveFn<impl::FnTextureBuilderSetPackagedTextureData>("nvnTextureBuilderSetPackagedTextureData");
    auto texInitialize          = impl::ResolveFn<impl::FnTextureInitialize>("nvnTextureInitialize");
    auto texPoolRegisterTexture = impl::ResolveFn<impl::FnTexturePoolRegisterTexture>("nvnTexturePoolRegisterTexture");
    auto deviceGetTextureHandle = impl::ResolveFn<impl::FnDeviceGetTextureHandle>("nvnDeviceGetTextureHandle");

    auto smpBuilderSetDevice    = impl::ResolveFn<impl::FnSamplerBuilderSetDevice>("nvnSamplerBuilderSetDevice");
    auto smpBuilderSetDefaults  = impl::ResolveFn<impl::FnSamplerBuilderSetDefaults>("nvnSamplerBuilderSetDefaults");
    auto smpBuilderSetMinMagFilter = impl::ResolveFn<impl::FnSamplerBuilderSetMinMagFilter>("nvnSamplerBuilderSetMinMagFilter");
    auto smpBuilderSetWrapMode  = impl::ResolveFn<impl::FnSamplerBuilderSetWrapMode>("nvnSamplerBuilderSetWrapMode");
    auto smpInitialize          = impl::ResolveFn<impl::FnSamplerInitialize>("nvnSamplerInitialize");
    auto smpPoolRegisterSampler = impl::ResolveFn<impl::FnSamplerPoolRegisterSampler>("nvnSamplerPoolRegisterSampler");

    if (!poolBuilderSetDefaults || !poolBuilderSetDevice || !poolBuilderSetFlags || !poolBuilderSetStorage || !poolInitialize ||
        !texBuilderSetDevice || !texBuilderSetDefaults || !texBuilderSetTarget || !texBuilderSetSize2D ||
        !texBuilderSetFormat || !texBuilderSetStorage || !texBuilderSetPackaged || !texInitialize ||
        !texPoolRegisterTexture || !deviceGetTextureHandle || !smpBuilderSetDevice || !smpBuilderSetDefaults ||
        !smpBuilderSetMinMagFilter || !smpBuilderSetWrapMode || !smpInitialize || !smpPoolRegisterSampler) {
        WIIXL_LOG("WiiXLaunch: CreateTexture symbol resolution failed");
        return 0;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(packagedData);
    uint32_t width  = *reinterpret_cast<const uint32_t*>(bytes + 0x40);
    uint32_t height = *reinterpret_cast<const uint32_t*>(bytes + 0x44);
    uint32_t format = *reinterpret_cast<const uint32_t*>(bytes + 0x50);
    constexpr size_t kHeaderSize = 0x200;

    size_t idx = impl::g_StaticTextureCount++;
    NVNmemoryPool* pool = &impl::g_StaticTexturePools[idx];
    NVNmemoryPoolBuilder poolBuilder{};
    poolBuilderSetDefaults(&poolBuilder);
    poolBuilderSetDevice(&poolBuilder, device);
    poolBuilderSetFlags(&poolBuilder, 0x21);
    poolBuilderSetStorage(&poolBuilder, const_cast<void*>(packagedData), packagedSize);
    if (!poolInitialize(pool, &poolBuilder)) {
        WIIXL_LOG("WiiXLaunch: CreateTexture poolInitialize failed");
        return 0;
    }

    NVNtexture* texture = &impl::g_StaticTextures[idx];
    NVNtextureBuilder texBuilder{};
    texBuilderSetDefaults(&texBuilder);
    texBuilderSetDevice(&texBuilder, device);
    texBuilderSetTarget(&texBuilder, 1);
    texBuilderSetFormat(&texBuilder, format ? format : Format::RGBA8);
    texBuilderSetSize2D(&texBuilder, width, height);
    texBuilderSetPackaged(&texBuilder, bytes + kHeaderSize);
    texBuilderSetStorage(&texBuilder, pool, kHeaderSize);
    if (!texInitialize(texture, &texBuilder)) {
        WIIXL_LOG("WiiXLaunch: CreateTexture texInitialize failed");
        return 0;
    }

    int texId = impl::NextTextureId()++;
    texPoolRegisterTexture(&impl::g_TexturePool, texId, texture, nullptr);

    NVNsampler* sampler = &impl::g_StaticSamplers[idx];
    NVNsamplerBuilder smpBuilder{};
    smpBuilderSetDefaults(&smpBuilder);
    smpBuilderSetDevice(&smpBuilder, device);
    smpBuilderSetMinMagFilter(&smpBuilder, minFilter, magFilter);
    smpBuilderSetWrapMode(&smpBuilder, wrapMode, wrapMode, wrapMode);
    if (!smpInitialize(sampler, &smpBuilder)) {
        WIIXL_LOG("WiiXLaunch: CreateTexture smpInitialize failed");
        return 0;
    }

    int smpId = impl::NextSamplerId()++;
    smpPoolRegisterSampler(&impl::g_SamplerPool, smpId, sampler);

    TextureHandle handle = deviceGetTextureHandle(device, texId, smpId);
    WIIXL_LOG("WiiXLaunch: CreateTexture OK (%ux%u, fmt=0x%x, handle=%p, texId=%d, smpId=%d)",
        width, height, format, reinterpret_cast<void*>(handle), texId, smpId);
    return handle;
}

inline void DrawSprite(
    CommandBuffer* cmdBuf,
    void* dstTexture,
    TextureHandle textureHandle,
    float x, float y,
    float width, float height,
    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
{
    if (!impl::g_DefaultUiProgramReady || !textureHandle) return;

    auto mapBuf                = impl::NvnFn<impl::FnBufferMap>(impl::Offset::kBufferMap);
    auto getBufAddr            = impl::NvnFn<impl::FnBufferGetAddress>(impl::Offset::kBufferGetAddress);
    auto setRenderTargets      = impl::NvnFn<impl::FnCommandBufferSetRenderTargets>(impl::Offset::kCommandBufferSetRenderTargets);
    auto setViewport           = impl::NvnFn<impl::FnCommandBufferSetViewport>(impl::Offset::kCommandBufferSetViewport);
    auto setScissor            = impl::NvnFn<impl::FnCommandBufferSetScissor>(impl::Offset::kCommandBufferSetScissor);
    auto bindProgram           = impl::NvnFn<impl::FnCommandBufferBindProgram>(impl::Offset::kCommandBufferBindProgram);
    auto bindVertexAttribState = impl::NvnFn<impl::FnCommandBufferBindVertexAttribState>(impl::Offset::kCommandBufferBindVertexAttribState);
    auto bindVertexStreamState = impl::NvnFn<impl::FnCommandBufferBindVertexStreamState>(impl::Offset::kCommandBufferBindVertexStreamState);
    auto bindVertexBuffer      = impl::NvnFn<impl::FnCommandBufferBindVertexBuffer>(impl::Offset::kCommandBufferBindVertexBuffer);
    auto setTexturePool        = impl::NvnFn<impl::FnCommandBufferSetTexturePool>(impl::Offset::kCommandBufferSetTexturePool);
    auto setSamplerPool        = impl::NvnFn<impl::FnCommandBufferSetSamplerPool>(impl::Offset::kCommandBufferSetSamplerPool);
    auto bindTexture           = impl::ResolveFn<impl::FnCommandBufferBindTexture>("nvnCommandBufferBindTexture");
    auto drawArrays            = impl::NvnFn<impl::FnCommandBufferDrawArrays>(impl::Offset::kCommandBufferDrawArrays);

    if (!mapBuf || !getBufAddr || !bindProgram || !bindVertexAttribState ||
        !bindVertexStreamState || !bindVertexBuffer || !setTexturePool || !setSamplerPool ||
        !bindTexture || !drawArrays) {
        return;
    }

    void* mapped = mapBuf(&impl::g_SpriteDataBuffer);
    if (!mapped) return;

    float x0 = x;
    float x1 = x + width;
    float y0 = y;
    float y1 = y + height;

    const TextureVertex verts[6] = {
        {x0, y1, 0.0f, 1.0f,   0.0f, 0.0f,   r, g, b, a},
        {x0, y0, 0.0f, 1.0f,   0.0f, 1.0f,   r, g, b, a},
        {x1, y0, 0.0f, 1.0f,   1.0f, 1.0f,   r, g, b, a},
        {x0, y1, 0.0f, 1.0f,   0.0f, 0.0f,   r, g, b, a},
        {x1, y0, 0.0f, 1.0f,   1.0f, 1.0f,   r, g, b, a},
        {x1, y1, 0.0f, 1.0f,   1.0f, 0.0f,   r, g, b, a},
    };
    __builtin_memcpy(mapped, verts, sizeof(verts));
    impl::armDCacheFlush(mapped, sizeof(verts));

    uint64_t gpuBase = getBufAddr(&impl::g_SpriteDataBuffer);

    const void* colorTargets[1] = { dstTexture };
    if (setRenderTargets) setRenderTargets(cmdBuf, 1, colorTargets, nullptr, nullptr, nullptr);

    int dispWidth = 1280;
    int dispHeight = 720;
    void* gn = impl::GetGraphicsNvn();
    if (gn) {
        void* displayBuffer = *reinterpret_cast<void**>(static_cast<uint8_t*>(gn) + 0x50);
        if (displayBuffer) {
            float fW = *reinterpret_cast<float*>(static_cast<uint8_t*>(displayBuffer) + 8);
            float fH = *reinterpret_cast<float*>(static_cast<uint8_t*>(displayBuffer) + 12);
            if (fW > 0.0f && fH > 0.0f) {
                dispWidth = static_cast<int>(fW);
                dispHeight = static_cast<int>(fH);
            }
        }
    }

    if (setViewport) setViewport(cmdBuf, 0, 0, dispWidth, dispHeight);
    if (setScissor)  setScissor(cmdBuf, 0, 0, dispWidth, dispHeight);

    setTexturePool(cmdBuf, &impl::g_TexturePool);
    setSamplerPool(cmdBuf, &impl::g_SamplerPool);

    // Deliberately no depth-stencil/polygon/color/blend/channel-mask binds here.
    // Binding our own guessed-at state objects previously produced yellow, then
    // solid-black, then invisible output - inheriting the game's last-bound
    // state (same approach as the proven working DrawTextureQuadDirect) is what
    // actually renders correctly.
    bindProgram(cmdBuf, &impl::g_DefaultUiProgram, StageMask::VertexFragment);
    bindVertexAttribState(cmdBuf, 3, impl::g_SpriteAttribStates);
    bindVertexStreamState(cmdBuf, 1, &impl::g_SpriteStreamState);
    bindVertexBuffer(cmdBuf, 0, gpuBase, sizeof(verts));
    bindTexture(cmdBuf, Stage::Fragment, 0, textureHandle);
    drawArrays(cmdBuf, Topology::Triangles, 0, 6);
}

} // namespace WiiXLaunch::BotW::NVN

#else

namespace WiiXLaunch::BotW::NVN {

constexpr bool SupportsNVN = false;

using TextureHandle = uint64_t;
using CommandBuffer = void;
using Device        = void;

inline void Init() {}
inline void RegisterDrawCallback(void (*)(void*, void*, int, int)) {}
inline void OnInitialized(void (*)()) {}
inline Device* GetDevice() { return nullptr; }
inline void* GetGraphicsNvn() { return nullptr; }
inline TextureHandle CreateTexture(const void*, size_t, int = 0, int = 0, int = 0) { return 0; }
inline void DrawSprite(void*, void*, TextureHandle, float, float, float, float, float = 1, float = 1, float = 1, float = 1) {}

} // namespace WiiXLaunch::BotW::NVN

#endif
