#pragma once

// botw.vfx and botw.flyt - particle effects, and the game's own UI layouts.
//
// Both hand out object identities and both therefore need host handle tables,
// for the reason every other one in this project exists: a raw pointer handed
// to a compiled binary is a pointer the host will later dereference on that
// binary's say-so, and a stale one is a crash in host code that reads as a host
// bug.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/botw/game/vfx.hpp>
#include <wiixlaunch/botw/game/flyt.hpp>

#include <cstdint>

// ===========================================================================
// botw.vfx - spawning the game's particle effects.
//
// The module's Handle is {ptr, generation} - two words, and a struct, so it
// cannot cross. It is already generation-checked on the module's side, which is
// the right design; this adds a host handle on top so a mod never holds the
// pointer half at all.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::VfxSurface {

constexpr const char* kName = "botw.vfx";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

constexpr uint32_t kMaxEffects = 64;

namespace impl {

struct Slot {
    Vfx::Handle handle{};
    uint16_t generation = 1;
    bool used = false;
};

inline Slot g_Slots[kMaxEffects];

inline uint32_t Store(const Vfx::Handle& h) {
    if (!Vfx::IsValid(h)) return 0;
    for (uint32_t i = 0; i < kMaxEffects; ++i) {
        if (g_Slots[i].used) continue;
        Slot& s = g_Slots[i];
        s.handle = h;
        s.used = true;
        s.generation++;
        if (s.generation == 0) s.generation = 1;
        return (static_cast<uint32_t>(s.generation) << 16) | (i + 1u);
    }
    WIIXL_LOG("botw.vfx: all %u effect slots are held", kMaxEffects);
    return 0;
}

inline Slot* Find(uint32_t handle) {
    if (handle == 0) return nullptr;
    const uint32_t slot = handle & 0xFFFFu;
    if (slot == 0 || slot > kMaxEffects) return nullptr;
    Slot& s = g_Slots[slot - 1];
    if (!s.used) return nullptr;
    if (s.generation != static_cast<uint16_t>(handle >> 16)) return nullptr;
    return &s;
}

extern "C" inline uint32_t VfxSupports() { return Vfx::SupportsVfx ? 1u : 0u; }

extern "C" inline uint32_t VfxSpawn(const char* esetlistPath,
                                    float x, float y, float z,
                                    float sx, float sy, float sz,
                                    float rx, float ry, float rz) {
    if (!esetlistPath) return 0;
    return Store(Vfx::Spawn(esetlistPath, x, y, z, sx, sy, sz, rx, ry, rz));
}

extern "C" inline uint32_t VfxUpdate(uint32_t handle,
                                     float x, float y, float z,
                                     float sx, float sy, float sz,
                                     float rx, float ry, float rz) {
    Slot* s = Find(handle);
    if (!s) return 0;
    return Vfx::Update(s->handle, x, y, z, sx, sy, sz, rx, ry, rz) ? 1u : 0u;
}

extern "C" inline uint32_t VfxIsValid(uint32_t handle) {
    Slot* s = Find(handle);
    return s && Vfx::IsValid(s->handle) ? 1u : 0u;
}

// Stops the effect AND releases the slot, so the mod's handle goes stale in the
// same call. An effect that has been stopped is not something a later Update
// should quietly do nothing to.
extern "C" inline uint32_t VfxStop(uint32_t handle) {
    Slot* s = Find(handle);
    if (!s) return 0;
    Vfx::Stop(s->handle);
    s->used = false;
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsVfx",         &VfxSupports),
    WIIXL_SURFACE_SYMBOL("Spawn",               &VfxSpawn),
    WIIXL_SURFACE_SYMBOL("Update",              &VfxUpdate),
    WIIXL_SURFACE_SYMBOL("IsValid",             &VfxIsValid),
    WIIXL_SURFACE_SYMBOL("Stop",                &VfxStop),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::VfxSurface


// ===========================================================================
// botw.flyt - the game's own UI layouts, and the panes inside them.
//
// TWO DEPARTURES FROM THE MODULE'S API, both for the same reason.
//
// OnLoaded is a single callback slot taking a Layout by value. This surface
// takes that slot once and dispatches to modules, attributed, handing over a
// layout HANDLE and the name - so several mods can watch for layouts loading
// and none can take the notification from another.
//
// SetRedirect takes a function returning a `const char*` that the game then
// reads. A compiled mod returning a pointer into its own image would be handing
// the game memory whose lifetime it cannot promise, and a mod that got that
// wrong would corrupt a filename in a way nothing could attribute. So the
// redirect is a TABLE instead: a mod says "when the game asks for A, give it
// B", both strings are copied here, and the host owns what the game reads.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::FlytSurface {

constexpr const char* kName = "botw.flyt";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

constexpr uint32_t kMaxLayouts = 32;
constexpr uint32_t kMaxPanes = 128;
constexpr uint32_t kMaxWatchers = 8;
constexpr uint32_t kMaxRedirects = 16;
constexpr uint32_t kNameLen = 64;
constexpr uint32_t kOwnerLen = 17;

namespace impl {

// --- handle tables ---------------------------------------------------------

template <typename T, uint32_t N>
struct Table {
    struct Slot {
        void* ptr = nullptr;
        uint16_t generation = 1;
        bool used = false;
    };
    Slot slots[N];
    uint32_t next = 0;

    uint32_t Store(void* p) {
        if (!p) return 0;
        const uint32_t i = next % N;
        ++next;
        Slot& s = slots[i];
        s.generation++;
        if (s.generation == 0) s.generation = 1;
        s.ptr = p;
        s.used = true;
        return (static_cast<uint32_t>(s.generation) << 16) | (i + 1u);
    }

    void* Load(uint32_t handle) const {
        if (handle == 0) return nullptr;
        const uint32_t i = handle & 0xFFFFu;
        if (i == 0 || i > N) return nullptr;
        const Slot& s = slots[i - 1];
        if (!s.used || !s.ptr) return nullptr;
        if (s.generation != static_cast<uint16_t>(handle >> 16)) return nullptr;
        return s.ptr;
    }
};

inline Table<void, kMaxLayouts> g_Layouts;
inline Table<void, kMaxPanes> g_Panes;

inline uint32_t CopyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src) return 0;
    uint32_t n = 0;
    while (src[n] && n + 1 < cap) { out[n] = src[n]; ++n; }
    out[n] = '\0';
    return n;
}

// --- the attributed load watchers -----------------------------------------

using ModLoadedFn = void (*)(uint32_t layoutHandle, const char* name);

struct Watcher {
    ModLoadedFn fn;
    char owner[kOwnerLen];
    bool inUse;
};

inline Watcher g_Watchers[kMaxWatchers];
inline uint32_t g_WatcherCount = 0;
inline bool g_Hooked = false;

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kOwnerLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline void OnLayoutLoaded(FLYT::Layout layout, const char* name) {
    const uint32_t handle = g_Layouts.Store(layout.GetRaw());
    for (uint32_t i = 0; i < g_WatcherCount; ++i) {
        Watcher& w = g_Watchers[i];
        if (!w.inUse || !w.fn) continue;
        WiiXLaunch::ModContext::SetCurrent(w.owner);
        w.fn(handle, name ? name : "");
        WiiXLaunch::ModContext::SetCurrent(nullptr);
    }
}

// --- the redirect table ----------------------------------------------------

struct Redirect {
    char from[kNameLen];
    char to[kNameLen];
    char owner[kOwnerLen];
    bool used;
};

inline Redirect g_Redirects[kMaxRedirects];
inline uint32_t g_RedirectCount = 0;

inline bool NameEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    for (uint32_t i = 0; i < kNameLen; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

// The game reads the returned pointer, so it points into g_Redirects, which
// outlives the call and every frame after it.
inline const char* RedirectFor(void* self, const char* name) {
    (void)self;
    if (!name) return nullptr;
    for (uint32_t i = 0; i < g_RedirectCount; ++i) {
        if (g_Redirects[i].used && NameEquals(g_Redirects[i].from, name)) {
            return g_Redirects[i].to;
        }
    }
    return nullptr;
}

// --- entry points ----------------------------------------------------------

extern "C" inline uint32_t FlytInit() {
    if (!g_Hooked) {
        g_Hooked = true;
        FLYT::Layout::InstallLoadHook();
        FLYT::Layout::OnLoaded(&OnLayoutLoaded);
        FLYT::Layout::SetRedirect(&RedirectFor);
        WIIXL_LOG("botw.flyt: load hook installed; the layout callback and the "
                  "redirect are held by the host so every module can use them");
    }
    return 1;
}

extern "C" inline uint32_t FlytOnLayoutLoaded(ModLoadedFn fn) {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0' || !fn) return 0;
    for (uint32_t i = 0; i < g_WatcherCount; ++i) {
        if (NameEquals(g_Watchers[i].owner, owner)) {
            WIIXL_LOG("botw.flyt: %s already watches layout loads", owner);
            return 0;
        }
    }
    if (g_WatcherCount >= kMaxWatchers) {
        WIIXL_LOG("botw.flyt: %s refused - all %u watcher slots are taken",
                  owner, kMaxWatchers);
        return 0;
    }

    FlytInit();

    Watcher& w = g_Watchers[g_WatcherCount++];
    w.fn = fn;
    w.inUse = true;
    CopyOwner(w.owner, owner);
    WIIXL_LOG("botw.flyt: %s is watching layout loads (%u of %u)",
              w.owner, g_WatcherCount, kMaxWatchers);
    return 1;
}

// "When the game asks for `from`, hand it `to` instead." Both copied here, so
// the game never reads a string owned by a module.
extern "C" inline uint32_t FlytAddRedirect(const char* from, const char* to) {
    if (!from || !to) return 0;
    const char* owner = WiiXLaunch::ModContext::Current();

    if (g_RedirectCount >= kMaxRedirects) {
        WIIXL_LOG("botw.flyt: %s refused a redirect - all %u slots are taken",
                  owner ? owner : "<host>", kMaxRedirects);
        return 0;
    }

    FlytInit();

    Redirect& r = g_Redirects[g_RedirectCount++];
    CopyOut(from, r.from, kNameLen);
    CopyOut(to, r.to, kNameLen);
    CopyOut(owner ? owner : "<host>", r.owner, kOwnerLen);
    r.used = true;

    WIIXL_LOG("botw.flyt: %s redirects '%s' to '%s'",
              r.owner, r.from, r.to);
    return 1;
}

// --- layouts and panes -----------------------------------------------------

extern "C" inline uint32_t FlytRootPane(uint32_t layoutHandle) {
    void* p = g_Layouts.Load(layoutHandle);
    if (!p) return 0;
    return g_Panes.Store(FLYT::Layout(p).GetRootPane().GetRaw());
}

extern "C" inline uint32_t FlytFindPane(uint32_t layoutHandle, const char* name) {
    void* p = g_Layouts.Load(layoutHandle);
    if (!p || !name) return 0;
    return g_Panes.Store(FLYT::Layout(p).FindPane(name).GetRaw());
}

extern "C" inline uint32_t FlytFindChild(uint32_t paneHandle, const char* name,
                                         uint32_t recursive) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !name) return 0;
    return g_Panes.Store(FLYT::Pane(p).FindChild(name, recursive != 0).GetRaw());
}

extern "C" inline uint32_t FlytPaneName(uint32_t paneHandle, char* out, uint32_t cap) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) { if (out && cap) out[0] = '\0'; return 0; }
    return CopyOut(FLYT::Pane(p).GetName(), out, cap);
}

extern "C" inline uint32_t FlytSetTranslate(uint32_t paneHandle, float x, float y, float z) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetTranslate(x, y, z);
    return 1;
}

extern "C" inline uint32_t FlytGetGlobalTranslate(uint32_t paneHandle, float* out3) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out3) return 0;
    FLYT::Pane(p).GetGlobalTranslate(out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t FlytSetGlobalTranslate(uint32_t paneHandle, float x, float y, float z) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetGlobalTranslate(x, y, z);
    return 1;
}

extern "C" inline uint32_t FlytSetRotate(uint32_t paneHandle, float x, float y, float z) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetRotate(x, y, z);
    return 1;
}

extern "C" inline uint32_t FlytSetScale(uint32_t paneHandle, float x, float y) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetScale(x, y);
    return 1;
}

extern "C" inline uint32_t FlytSetSize(uint32_t paneHandle, float w, float h) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetSize(w, h);
    return 1;
}

extern "C" inline uint32_t FlytSetVisible(uint32_t paneHandle, uint32_t visible) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetVisible(visible != 0);
    return 1;
}

// --- reading a pane back ---------------------------------------------------
//
// The setters were there from the start; without the getters a mod could move a
// pane and never find out where the game had put it, which makes anything
// relative - nudging, animating, restoring - impossible.

extern "C" inline uint32_t FlytGetTranslate(uint32_t paneHandle, float* out3) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out3) return 0;
    FLYT::Pane(p).GetTranslate(out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t FlytGetParentGlobalTranslate(uint32_t paneHandle, float* out3) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out3) return 0;
    FLYT::Pane(p).GetParentGlobalTranslate(out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t FlytGetRotate(uint32_t paneHandle, float* out3) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out3) return 0;
    FLYT::Pane(p).GetRotate(out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t FlytGetScale(uint32_t paneHandle, float* out2) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out2) return 0;
    FLYT::Pane(p).GetScale(out2[0], out2[1]);
    return 1;
}

extern "C" inline uint32_t FlytGetSize(uint32_t paneHandle, float* out2) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out2) return 0;
    FLYT::Pane(p).GetSize(out2[0], out2[1]);
    return 1;
}

extern "C" inline uint32_t FlytIsVisible(uint32_t paneHandle) {
    void* p = g_Panes.Load(paneHandle);
    return p && FLYT::Pane(p).IsVisible() ? 1u : 0u;
}

// A pane's own alpha, 0-255. The EFFECTIVE alpha is that multiplied down the
// parent chain, which is what actually reaches the screen - a mod fading
// something in needs the first, and one asking "can this be seen" needs the
// second, so both are here.
extern "C" inline uint32_t FlytGetAlpha(uint32_t paneHandle, uint32_t* out) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out) return 0;
    *out = FLYT::Pane(p).GetAlpha();
    return 1;
}

extern "C" inline uint32_t FlytSetAlpha(uint32_t paneHandle, uint32_t alpha) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::Pane(p).SetAlpha(static_cast<uint8_t>(alpha & 0xFF));
    return 1;
}

extern "C" inline uint32_t FlytGetEffectiveAlpha(uint32_t paneHandle, float* out) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !out) return 0;
    *out = FLYT::Pane(p).GetEffectiveAlpha();
    return 1;
}

// --- picture panes ---------------------------------------------------------
//
// Only a PicturePane has vertex colours. The handle is the same one - a pane is
// a pane - and these refuse rather than reinterpreting a pane that has no
// colour to set, since writing four bytes into whatever happens to be at that
// offset on a plain pane is exactly the kind of quiet corruption this whole
// boundary exists to prevent.

extern "C" inline uint32_t FlytSetColor(uint32_t paneHandle, uint32_t rgba) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::PicturePane(p).SetColor(static_cast<uint8_t>((rgba >> 24) & 0xFF),
                                  static_cast<uint8_t>((rgba >> 16) & 0xFF),
                                  static_cast<uint8_t>((rgba >> 8) & 0xFF),
                                  static_cast<uint8_t>(rgba & 0xFF));
    return 1;
}

extern "C" inline uint32_t FlytGetCornerColor(uint32_t paneHandle, int32_t corner,
                                              uint32_t* rgba) {
    void* p = g_Panes.Load(paneHandle);
    if (!p || !rgba) return 0;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    FLYT::PicturePane(p).GetCornerColor(static_cast<int>(corner), r, g, b, a);
    *rgba = (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
            (static_cast<uint32_t>(b) << 8) | a;
    return 1;
}

extern "C" inline uint32_t FlytSetCornerColor(uint32_t paneHandle, int32_t corner,
                                              uint32_t rgba) {
    void* p = g_Panes.Load(paneHandle);
    if (!p) return 0;
    FLYT::PicturePane(p).SetCornerColor(static_cast<int>(corner),
                                        static_cast<uint8_t>((rgba >> 24) & 0xFF),
                                        static_cast<uint8_t>((rgba >> 16) & 0xFF),
                                        static_cast<uint8_t>((rgba >> 8) & 0xFF),
                                        static_cast<uint8_t>(rgba & 0xFF));
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Init",               &FlytInit),
    WIIXL_SURFACE_SYMBOL("OnLayoutLoaded",     &FlytOnLayoutLoaded),
    WIIXL_SURFACE_SYMBOL("AddRedirect",        &FlytAddRedirect),

    WIIXL_SURFACE_SYMBOL("RootPane",           &FlytRootPane),
    WIIXL_SURFACE_SYMBOL("FindPane",           &FlytFindPane),
    WIIXL_SURFACE_SYMBOL("FindChild",          &FlytFindChild),
    WIIXL_SURFACE_SYMBOL("PaneName",           &FlytPaneName),
    WIIXL_SURFACE_SYMBOL("SetTranslate",       &FlytSetTranslate),
    WIIXL_SURFACE_SYMBOL("GetGlobalTranslate", &FlytGetGlobalTranslate),
    WIIXL_SURFACE_SYMBOL("SetGlobalTranslate", &FlytSetGlobalTranslate),
    WIIXL_SURFACE_SYMBOL("SetRotate",          &FlytSetRotate),
    WIIXL_SURFACE_SYMBOL("SetScale",           &FlytSetScale),
    WIIXL_SURFACE_SYMBOL("SetSize",            &FlytSetSize),
    WIIXL_SURFACE_SYMBOL("SetVisible",         &FlytSetVisible),
    WIIXL_SURFACE_SYMBOL("GetTranslate",       &FlytGetTranslate),
    WIIXL_SURFACE_SYMBOL("GetParentGlobalTranslate", &FlytGetParentGlobalTranslate),
    WIIXL_SURFACE_SYMBOL("GetRotate",          &FlytGetRotate),
    WIIXL_SURFACE_SYMBOL("GetScale",           &FlytGetScale),
    WIIXL_SURFACE_SYMBOL("GetSize",            &FlytGetSize),
    WIIXL_SURFACE_SYMBOL("IsVisible",          &FlytIsVisible),
    WIIXL_SURFACE_SYMBOL("GetAlpha",           &FlytGetAlpha),
    WIIXL_SURFACE_SYMBOL("SetAlpha",           &FlytSetAlpha),
    WIIXL_SURFACE_SYMBOL("GetEffectiveAlpha",  &FlytGetEffectiveAlpha),
    WIIXL_SURFACE_SYMBOL("SetColor",           &FlytSetColor),
    WIIXL_SURFACE_SYMBOL("GetCornerColor",     &FlytGetCornerColor),
    WIIXL_SURFACE_SYMBOL("SetCornerColor",     &FlytSetCornerColor),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::FlytSurface
