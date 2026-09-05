#pragma once

// botw.gfx v1 - drawing into the game's frame.
//
// One surface over GX2 (Wii U, Cemu) and NVN (Switch), because a mod that wants
// to draw a sprite wants to draw a sprite. The two backends have different
// handle widths and different extras; the shared calls are here under one name
// and the extras report themselves through capability flags, the same way
// wiixl.net covers three completely different socket implementations.
//
// ---------------------------------------------------------------------------
// A DRAW CALLBACK IS A TICK THAT DRAWS, and it gets the same treatment.
//
// The module's RegisterDrawCallback appends to a list, so several callbacks
// already coexist - but nothing attributes them. A mod that crashes or spins
// inside one takes the frame down, and the report is "my game freezes with
// these mods installed" with a stack in graphics code that names nobody.
//
// So this surface registers ONE callback with the module and dispatches to
// modules itself, with the owner recorded before each call and cleared after -
// the same in-flight record the host tick and the player tick carry, with its
// own magic so the three cannot be confused in a dump.
//
// ---------------------------------------------------------------------------
// TEXTURES ARE HOST HANDLES, not the backend's.
//
// GX2's TextureHandle is a uintptr_t and NVN's is a uint64_t. Neither may be
// handed to a mod: one is a pointer into host memory, and both would let a mod
// pass a number the host would then dereference. The same argument as sockets
// and actors, and the same answer - a generation-counted handle that a stale
// value fails rather than resolves.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>

#if WIIXL_SWITCH
#include <wiixlaunch/botw/graphics/nvn.hpp>
#else
#include <wiixlaunch/botw/graphics/gx2.hpp>
#endif

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::GfxSurface {

constexpr const char* kName = "botw.gfx";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

#if WIIXL_SWITCH
namespace Backend = WiiXLaunch::BotW::NVN;
constexpr bool kIsGX2 = false;
#else
namespace Backend = WiiXLaunch::BotW::GX2;
constexpr bool kIsGX2 = true;
#endif

constexpr uint32_t kMaxDrawCallbacks = 8;
constexpr uint32_t kMaxTextures = 64;
constexpr uint32_t kMaxMeshes = 32;
constexpr uint32_t kOwnerLen = 17;

namespace impl {

// --- the attributed draw dispatch -----------------------------------------

using ModDrawFn = void (*)(uintptr_t cmdBuf, uintptr_t dstTexture, int32_t w, int32_t h);

struct DrawEntry {
    ModDrawFn fn;
    char owner[kOwnerLen];
    uint32_t calls;
    bool inUse;
};

inline DrawEntry g_Draws[kMaxDrawCallbacks];
inline uint32_t g_DrawCount = 0;
inline bool g_Hooked = false;

// Its own magic, so a dump can tell a hang in a DRAW callback from a hang in a
// host tick or a player tick. Three registries, three records; sharing one
// would mean the freeze that matters most is the one you cannot identify.
struct DrawInFlight {
    uint32_t magic;              // 'WXGD'
    uint32_t sequence;
    uint32_t depth;
    char     owner[kOwnerLen];
    char     pad[3];
};

constexpr uint32_t kDrawMagic = 0x57584744u;   // 'WXGD'
inline DrawInFlight g_InFlight = { kDrawMagic, 0, 0, {0}, {0} };

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kOwnerLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline bool SameOwner(const char* a, const char* b) {
    for (uint32_t i = 0; i < kOwnerLen; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

// The one callback the module sees. Everything a mod registered runs from here.
inline void DispatchDraw(Backend::CommandBuffer* cmdBuf, void* dstTexture,
                         int width, int height) {
    for (uint32_t i = 0; i < g_DrawCount; ++i) {
        DrawEntry& e = g_Draws[i];
        if (!e.inUse || !e.fn) continue;

        // BEFORE the call, so a frozen frame names the module that froze it.
        g_InFlight.sequence++;
        g_InFlight.depth++;
        CopyOwner(g_InFlight.owner, e.owner);
        WiiXLaunch::ModContext::SetCurrent(e.owner);

        e.fn(reinterpret_cast<uintptr_t>(cmdBuf),
             reinterpret_cast<uintptr_t>(dstTexture),
             static_cast<int32_t>(width), static_cast<int32_t>(height));

        WiiXLaunch::ModContext::SetCurrent(nullptr);
        g_InFlight.depth--;
        g_InFlight.owner[0] = '\0';
        e.calls++;
    }
}

// --- texture handles -------------------------------------------------------

struct TexSlot {
    Backend::TextureHandle native = 0;
    uint16_t generation = 1;
    bool used = false;
    char owner[kOwnerLen] = {};
};

inline TexSlot g_Textures[kMaxTextures];

// Meshes, held the same way textures are. MeshData is a pointer and a count
// into host memory; handing a mod either half would be handing it something the
// host later dereferences on its say-so.
struct MeshSlot {
#if !WIIXL_SWITCH
    Backend::MeshData data{};
#endif
    uint16_t generation = 1;
    bool used = false;
};

inline MeshSlot g_Meshes[kMaxMeshes];

inline uint32_t StoreTexture(Backend::TextureHandle native) {
    if (!native) return 0;
    const char* owner = WiiXLaunch::ModContext::Current();

    for (uint32_t i = 0; i < kMaxTextures; ++i) {
        if (g_Textures[i].used) continue;
        TexSlot& s = g_Textures[i];
        s.native = native;
        s.used = true;
        s.generation++;
        if (s.generation == 0) s.generation = 1;
        CopyOwner(s.owner, owner ? owner : "<host>");
        return (static_cast<uint32_t>(s.generation) << 16) | (i + 1u);
    }

    WIIXL_LOG("botw.gfx: %s asked for a texture and all %u slots are held",
              owner ? owner : "<host>", kMaxTextures);
    return 0;
}

inline bool LoadTexture(uint32_t handle, Backend::TextureHandle& out) {
    if (handle == 0) return false;
    const uint32_t slot = handle & 0xFFFFu;
    if (slot == 0 || slot > kMaxTextures) return false;
    const TexSlot& s = g_Textures[slot - 1];
    if (!s.used || !s.native) return false;
    if (s.generation != static_cast<uint16_t>(handle >> 16)) return false;
    out = s.native;
    return true;
}

// --- capability ------------------------------------------------------------

extern "C" inline uint32_t GfxIsGX2() { return kIsGX2 ? 1u : 0u; }

// GX2 has a sprite batcher, MEM1, a backdrop blur and surface allocation that
// NVN does not. A mod asks rather than discovering by getting zeroes.
extern "C" inline uint32_t GfxSupportsBatching() { return kIsGX2 ? 1u : 0u; }
extern "C" inline uint32_t GfxSupportsBackdrop() { return kIsGX2 ? 1u : 0u; }

extern "C" inline uint32_t GfxInit() {
    Backend::Init();
    if (!g_Hooked) {
        g_Hooked = true;
        Backend::RegisterDrawCallback(&DispatchDraw);
        WIIXL_LOG("botw.gfx: one draw callback registered with the backend; "
                  "modules are dispatched from it, attributed");
    }
    return 1;
}

// --- registering to draw ---------------------------------------------------

extern "C" inline uint32_t GfxRegisterDraw(ModDrawFn fn) {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') {
        WIIXL_LOG("botw.gfx: refused - a draw callback belongs to a module and "
                  "none is running");
        return 0;
    }
    if (!fn) {
        WIIXL_LOG("botw.gfx: %s passed a null draw callback", owner);
        return 0;
    }
    for (uint32_t i = 0; i < g_DrawCount; ++i) {
        if (SameOwner(g_Draws[i].owner, owner)) {
            WIIXL_LOG("botw.gfx: %s already has a draw callback - draw everything "
                      "you need from the one you have", owner);
            return 0;
        }
    }
    if (g_DrawCount >= kMaxDrawCallbacks) {
        WIIXL_LOG("botw.gfx: %s refused - all %u draw slots are taken",
                  owner, kMaxDrawCallbacks);
        return 0;
    }

    // Registering implies wanting the backend up. A mod that forgot Init would
    // otherwise register into a dispatcher nothing calls.
    GfxInit();

    DrawEntry& e = g_Draws[g_DrawCount++];
    e.fn = fn;
    e.calls = 0;
    e.inUse = true;
    CopyOwner(e.owner, owner);

    WIIXL_LOG("botw.gfx: %s registered a draw callback (%u of %u)",
              e.owner, g_DrawCount, kMaxDrawCallbacks);
    return 1;
}

extern "C" inline uint32_t GfxDrawCallbackCount() { return g_DrawCount; }

// --- textures --------------------------------------------------------------

extern "C" inline uint32_t GfxCreateTexture(const void* rgba, uint32_t size,
                                            int32_t width, int32_t height,
                                            int32_t format) {
    if (!rgba || size == 0 || width <= 0 || height <= 0) return 0;
    return StoreTexture(Backend::CreateTexture(rgba, size, width, height, format));
}

// Loads from the title's filesystem. The path is whatever the module's loader
// accepts; a mod's own resources live under its scoped directory and are read
// through wiixl.core, so this is for game content and host resources.
extern "C" inline uint32_t GfxLoadTexture(const char* path, uint32_t maxFileSize) {
#if WIIXL_SWITCH
    // NVN has no file loader in the module - the Switch texture path was never
    // finished, and the header for it says so. Reported through
    // SupportsLoadTexture rather than returning 0 and letting a mod conclude the
    // file was missing.
    (void)path; (void)maxFileSize;
    return 0;
#else
    if (!path) return 0;
    return StoreTexture(Backend::LoadTexture(path, maxFileSize ? maxFileSize : (1024u * 1024u)));
#endif
}

extern "C" inline uint32_t GfxSupportsLoadTexture() { return kIsGX2 ? 1u : 0u; }

extern "C" inline uint32_t GfxGetTextureSize(uint32_t texture, uint32_t* w, uint32_t* h) {
    if (!w || !h) return 0;
#if WIIXL_SWITCH
    (void)texture;
    return 0;
#else
    Backend::TextureHandle native = 0;
    if (!LoadTexture(texture, native)) return 0;
    return Backend::GetTextureSize(native, *w, *h) ? 1u : 0u;
#endif
}

// --- drawing ---------------------------------------------------------------
//
// cmdBuf and dstTexture come straight from the draw callback and are only valid
// inside it. They are passed back as integers because that is what a mod was
// handed; nothing here dereferences them beyond giving them to the backend.

extern "C" inline uint32_t GfxDrawSprite(uintptr_t cmdBuf, uintptr_t dstTexture,
                                         uint32_t texture,
                                         float x, float y, float w, float h,
                                         float r, float g, float b, float a) {
    Backend::TextureHandle native = 0;
    if (!cmdBuf || !dstTexture || !LoadTexture(texture, native)) return 0;

    Backend::DrawSprite(reinterpret_cast<Backend::CommandBuffer*>(cmdBuf),
                        reinterpret_cast<void*>(dstTexture),
                        native, x, y, w, h, r, g, b, a);
    return 1;
}

// Vertices as a flat float array: eight floats each - x,y,z,w then nx,ny,nz,nw,
// matching MeshVertex exactly. Flat rather than a struct pointer for the usual
// reason, and the count is vertices, not floats, because getting that wrong is
// a buffer overrun rather than a wrong picture.
extern "C" inline uint32_t GfxDrawMesh(uintptr_t cmdBuf, uintptr_t dstTexture,
                                       const float* vertices, uint32_t vertexCount) {
    if (!cmdBuf || !dstTexture || !vertices || vertexCount == 0) return 0;

    Backend::DrawMesh(reinterpret_cast<Backend::CommandBuffer*>(cmdBuf),
                      reinterpret_cast<void*>(dstTexture),
                      reinterpret_cast<const Backend::MeshVertex*>(vertices),
                      vertexCount);
    return 1;
}

// --- the GX2-only extras ---------------------------------------------------
//
// Present on both platforms so a mod's call sites do not need #ifs; they report
// 0 on Switch, which SupportsBatching told the mod to expect.

extern "C" inline uint32_t GfxBeginBatch(uintptr_t dstTexture) {
#if WIIXL_SWITCH
    (void)dstTexture;
    return 0;
#else
    if (!dstTexture) return 0;
    Backend::BeginBatch(reinterpret_cast<void*>(dstTexture));
    return 1;
#endif
}

// Four vertices, ten floats each, in TextureVertex order:
// x,y,z,w, u,v, r,g,b,a. Forty floats, and the count is checked rather than
// trusted because a short array here is a read past the end of a mod's buffer.
extern "C" inline uint32_t GfxBatchQuad(uint32_t texture, const float* verts40,
                                        uint32_t floatCount) {
#if WIIXL_SWITCH
    (void)texture; (void)verts40; (void)floatCount;
    return 0;
#else
    if (!verts40 || floatCount != 40) return 0;
    Backend::TextureHandle native = 0;
    if (!LoadTexture(texture, native)) return 0;
    Backend::BatchQuad(native, reinterpret_cast<const Backend::TextureVertex*>(verts40));
    return 1;
#endif
}

extern "C" inline uint32_t GfxEndBatch() {
#if WIIXL_SWITCH
    return 0;
#else
    Backend::EndBatch();
    return 1;
#endif
}

extern "C" inline uint32_t GfxBackdropReady() {
#if WIIXL_SWITCH
    return 0;
#else
    return Backend::BackdropReady() ? 1u : 0u;
#endif
}

extern "C" inline uint32_t GfxBackdropTexture() {
#if WIIXL_SWITCH
    return 0;
#else
    return StoreTexture(Backend::BackdropTexture());
#endif
}

extern "C" inline uintptr_t GfxAllocMEM1(uint32_t size, uint32_t align) {
#if WIIXL_SWITCH
    (void)size; (void)align;
    return 0;
#else
    if (size == 0) return 0;
    return reinterpret_cast<uintptr_t>(Backend::AllocMEM1(size, align ? align : 256));
#endif
}

// --- meshes ----------------------------------------------------------------

// Loads a mesh off the title's filesystem. MeshData is {pointer, count} and
// cannot cross, so the vertices stay in host memory and the mod gets a handle
// plus the count - it draws with DrawMeshHandle rather than shipping the
// vertices back across for every frame.
extern "C" inline uint32_t GfxLoadMesh(const char* path, uint32_t maxFileSize,
                                       uint32_t* outVertexCount) {
#if WIIXL_SWITCH
    (void)path; (void)maxFileSize;
    if (outVertexCount) *outVertexCount = 0;
    return 0;
#else
    if (!path) return 0;
    const Backend::MeshData m = Backend::LoadMesh(path, maxFileSize ? maxFileSize : (64u * 1024u));
    if (!m.vertices || m.vertexCount == 0) return 0;

    for (uint32_t i = 0; i < kMaxMeshes; ++i) {
        if (g_Meshes[i].used) continue;
        MeshSlot& slot = g_Meshes[i];
        slot.data = m;
        slot.used = true;
        slot.generation++;
        if (slot.generation == 0) slot.generation = 1;
        if (outVertexCount) *outVertexCount = static_cast<uint32_t>(m.vertexCount);
        return (static_cast<uint32_t>(slot.generation) << 16) | (i + 1u);
    }
    WIIXL_LOG("botw.gfx: all %u mesh slots are held", kMaxMeshes);
    return 0;
#endif
}

extern "C" inline uint32_t GfxDrawMeshHandle(uintptr_t cmdBuf, uintptr_t dstTexture,
                                             uint32_t mesh) {
#if WIIXL_SWITCH
    (void)cmdBuf; (void)dstTexture; (void)mesh;
    return 0;
#else
    if (!cmdBuf || !dstTexture) return 0;
    const uint32_t slot = mesh & 0xFFFFu;
    if (slot == 0 || slot > kMaxMeshes) return 0;
    MeshSlot& m = g_Meshes[slot - 1];
    if (!m.used || m.generation != static_cast<uint16_t>(mesh >> 16)) return 0;

    Backend::DrawMesh(reinterpret_cast<Backend::CommandBuffer*>(cmdBuf),
                      reinterpret_cast<void*>(dstTexture),
                      m.data.vertices, m.data.vertexCount);
    return 1;
#endif
}

// --- the backdrop blur -----------------------------------------------------
//
// Renders the frame so far into a blurred texture a mod can draw over. It costs
// two render targets and several full-target draws EVERY FRAME IT IS USED, so
// it is asked for rather than always on, and the handle it returns is the same
// kind of texture handle everything else here uses.
extern "C" inline uint32_t GfxBlurBackdrop(uintptr_t dstColorBuffer,
                                           uint32_t downscale, uint32_t passes) {
#if WIIXL_SWITCH
    (void)dstColorBuffer; (void)downscale; (void)passes;
    return 0;
#else
    if (!dstColorBuffer) return 0;
    return StoreTexture(Backend::BlurBackdrop(reinterpret_cast<void*>(dstColorBuffer),
                                              downscale ? downscale : 4,
                                              passes ? passes : 2));
#endif
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("IsGX2",              &GfxIsGX2),
    WIIXL_SURFACE_SYMBOL("SupportsBatching",   &GfxSupportsBatching),
    WIIXL_SURFACE_SYMBOL("SupportsBackdrop",   &GfxSupportsBackdrop),
    WIIXL_SURFACE_SYMBOL("Init",               &GfxInit),

    WIIXL_SURFACE_SYMBOL("RegisterDraw",       &GfxRegisterDraw),
    WIIXL_SURFACE_SYMBOL("DrawCallbackCount",  &GfxDrawCallbackCount),

    WIIXL_SURFACE_SYMBOL("CreateTexture",      &GfxCreateTexture),
    WIIXL_SURFACE_SYMBOL("LoadTexture",        &GfxLoadTexture),
    WIIXL_SURFACE_SYMBOL("SupportsLoadTexture", &GfxSupportsLoadTexture),
    WIIXL_SURFACE_SYMBOL("GetTextureSize",     &GfxGetTextureSize),

    WIIXL_SURFACE_SYMBOL("DrawSprite",         &GfxDrawSprite),
    WIIXL_SURFACE_SYMBOL("DrawMesh",           &GfxDrawMesh),
    WIIXL_SURFACE_SYMBOL("LoadMesh",           &GfxLoadMesh),
    WIIXL_SURFACE_SYMBOL("DrawMeshHandle",     &GfxDrawMeshHandle),
    WIIXL_SURFACE_SYMBOL("BlurBackdrop",       &GfxBlurBackdrop),

    WIIXL_SURFACE_SYMBOL("BeginBatch",         &GfxBeginBatch),
    WIIXL_SURFACE_SYMBOL("BatchQuad",          &GfxBatchQuad),
    WIIXL_SURFACE_SYMBOL("EndBatch",           &GfxEndBatch),
    WIIXL_SURFACE_SYMBOL("BackdropReady",      &GfxBackdropReady),
    WIIXL_SURFACE_SYMBOL("BackdropTexture",    &GfxBackdropTexture),
    WIIXL_SURFACE_SYMBOL("AllocMEM1",          &GfxAllocMEM1),
};

} // namespace impl

// Reported at the load point, beside the tick registries.
inline void LogState() {
    if (impl::g_DrawCount == 0) {
        WIIXL_LOG("botw.gfx: no module registered a draw callback");
        return;
    }
    WIIXL_LOG("botw.gfx: %u module(s) drawing into the frame", impl::g_DrawCount);
    for (uint32_t i = 0; i < impl::g_DrawCount; ++i) {
        WIIXL_LOG("botw.gfx:   %u. %s", i + 1, impl::g_Draws[i].owner);
    }
}

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::GfxSurface
