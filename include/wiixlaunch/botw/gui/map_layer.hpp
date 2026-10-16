#pragma once

// GUI::MapLayer - draws the GUI's map layer (impl::kLayerMap) from inside the
// game's own drawing of the map, so it sits over the map and under the map's
// icons, cursor, and the Sheikah Slate frame around it, exactly where the
// game would put a layer of its own.
//
// HOW. The map is the AppMap_00 layout. Its panes draw in tree order - the map
// first, then N_IconArea_00 (every icon), then the cursor, the frame's glyph
// strips (P_UeBlack_00 / P_SitaBlack_00) and the button guides. So the layer
// is drawn as N_IconArea_00 starts to draw: that one pane is given a private
// copy of its vtable whose Draw slot runs the layer first, then the pane's own
// Draw. No code is patched and no other pane is touched.
//
// RE (v208):
//   - The map screen keeps N_IconArea_00 at +0x216c and Pa_Map_00 at +0x2170
//     (0x02f5e848 looks both up by name once the layout is built). Screens are
//     found by scanning the UI screen manager (*0x1047e650; count +0x14,
//     array +0x18) for every one whose +0x216c pane is named N_IconArea_00.
//   - A pane's vtable is at +0x08, name char[24] at +0x80. GHS vtable entries
//     are 8 bytes, {this delta, function}, after an 8-byte header.
//   - Pane::Draw (0x03c4ab08) is called through vtable +0x8c: visible bit 0 of
//     +0x44, then DrawSelf (+0x94) if the effective alpha (+0x46) is non-zero,
//     then each child's Draw (+0x8c) from the list at +0x14.
//
// GPU STATE. The game's layout draw is mid-flight, so the layer draws through
// a private GX2 context state (graphics/gx2.hpp, made at scan-out) into the
// colour buffer agl bound last, and the game's state is set back after - the
// same arrangement as the pre-HUD draw.

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU || WIIXL_WIIU

#include "gui.hpp"
#include "../graphics/gx2.hpp"

#include <cstdint>

namespace WiiXLaunch::BotW::GUI::MapLayer {

namespace impl {

constexpr uintptr_t kScreenMgrPtr = 0x1047e650;
constexpr uint32_t kScreenCountOffset = 0x14;
constexpr uint32_t kScreenArrayOffset = 0x18;
constexpr uint32_t kIconAreaOffset = 0x216c;
constexpr uint32_t kPaneVtableOffset = 0x08;
constexpr uint32_t kPaneNameOffset = 0x80;
constexpr uint32_t kDrawSlot = 0x8c;
constexpr const char kIconAreaName[] = "N_IconArea_00";
// The base Pane's vtable ends at +0xb0; a null pane (N_) is a base Pane. Copy
// generously in case a build gives it a subclass.
constexpr uint32_t kVtableWords = 0x100 / 4;

using DrawFn = void (*)(void* pane, void* drawInfo);

// Every screen whose +0x216c is an N_IconArea_00 is hooked: more than one
// screen class may carry that pane (on v208 the map's is screen 42's). They
// share the base Pane vtable, so one private copy serves them all.
constexpr uint32_t kMaxPanes = 8;
constexpr uint32_t kDrawSelfSlot = 0x94;

inline bool g_Enabled = false;
inline uintptr_t g_Panes[kMaxPanes]{};
inline uint32_t g_PaneScreens[kMaxPanes]{};
inline uint32_t g_PaneCount = 0;
inline uintptr_t g_SourceVtable = 0;
inline DrawFn g_OrigDraw = nullptr;
inline DrawFn g_OrigDrawSelf = nullptr;
alignas(16) inline uint32_t g_Vtable[kVtableWords]{};
inline uint32_t g_ScanOuts = 0;
// Set while the hooked Draw runs: it calls DrawSelf, which is hooked too, and
// the layer is drawn once per pane draw. NOT once per frame - the game draws
// the map more than once a frame, each time into a fresh image, and skipping
// the later ones left frames without the layer.
inline bool g_InDraw = false;
// The TV's UI target, the last full-size one a pass drew into. The map is
// drawn once a frame, inside whichever pass - TV or GamePad - is running at
// the time, and the GamePad's is not what the TV shows: drawing into it left
// the TV without the layer on random frames. So a GamePad-sized pass target
// is swapped for this.
inline GX2Types::ColorBuffer* g_TvTarget = nullptr;

inline bool Plausible(uintptr_t p) { return p >= 0x10000000u && p < 0xE0000000u && (p & 3u) == 0; }

inline uint32_t Word(uintptr_t at) { return *reinterpret_cast<const volatile uint32_t*>(at); }

inline uint32_t VtableWord() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_Vtable)); }

inline bool IsIconArea(uintptr_t pane) {
    if (!Plausible(pane) || !Plausible(Word(pane + kPaneVtableOffset))) return false;
    const char* name = reinterpret_cast<const char*>(pane + kPaneNameOffset);
    for (uint32_t i = 0; i < sizeof(kIconAreaName); ++i) {
        if (name[i] != kIconAreaName[i]) return false;
    }
    return true;
}

inline void DrawLayer() {
    namespace G = WiiXLaunch::BotW::GX2::impl;
    // The target of the layout pass the map is being drawn in. Failing that, the last target bound, but only a
    // full-size one: the UI binds small targets of its own mid-pass.
    GX2Types::ColorBuffer* dst = G::g_LayoutPassColor;
    if (!dst && G::g_BoundColor && G::g_BoundColor->surface.width >= G::kTvMinWidth) dst = G::g_BoundColor;
    if (dst && dst->surface.width >= G::kTvMinWidth) {
        g_TvTarget = dst;
    } else if (g_TvTarget) {
        dst = g_TvTarget;
    }
    void* ctx = G::g_MidLayoutContext;
    if (!dst || !dst->surface.image || !ctx || G::g_ContextOverride) return;
    if (GUI::impl::g_ShownLayerQuads[GUI::impl::kLayerMap] == 0) return;
    void* gameState = G::GetContextState();
    if (!gameState) return;

    const uint32_t width = dst->surface.width ? dst->surface.width : 1280;
    const uint32_t height = dst->surface.height ? dst->surface.height : 720;
    G::g_ContextOverride = ctx;
    G::g_ContextOverrideUsed = false;
    G::g_BatchLate = true;   // into the game's command list, which runs late: see kLateRingBytes
    GUI::impl::ReplayLayerNow(dst, width, height, GUI::impl::kLayerMap);
    G::g_BatchLate = false;
    G::g_ContextOverride = nullptr;
    if (G::g_ContextOverrideUsed) G::SetContextState()(gameState);
}

// The Draw and DrawSelf slots of the private vtable.
inline void IconAreaDraw(void* pane, void* drawInfo) {
    DrawLayer();
    g_InDraw = true;
    if (g_OrigDraw) g_OrigDraw(pane, drawInfo);
    g_InDraw = false;
}

inline void IconAreaDrawSelf(void* pane, void* drawInfo) {
    if (!g_InDraw) DrawLayer();
    if (g_OrigDrawSelf) g_OrigDrawSelf(pane, drawInfo);
}

inline bool CodeAddress(uint32_t a) { return a >= 0x02000000u && a < 0x10000000u; }

inline bool Patch(uintptr_t pane, uint32_t screen) {
    const uintptr_t vtable = Word(pane + kPaneVtableOffset);
    if (vtable == VtableWord()) return true;
    if (!g_SourceVtable) {
        for (uint32_t i = 0; i < kVtableWords; ++i) g_Vtable[i] = Word(vtable + i * 4);
        const uint32_t draw = g_Vtable[kDrawSlot / 4];
        const uint32_t drawSelf = g_Vtable[kDrawSelfSlot / 4];
        if (!CodeAddress(draw) || !CodeAddress(drawSelf)) {
            OSLog("WiiXLaunch GUI: map layer - N_IconArea_00's Draw/DrawSelf slots hold %08x/%08x; not patching\n",
                  static_cast<unsigned int>(draw), static_cast<unsigned int>(drawSelf));
            return false;
        }
        g_OrigDraw = reinterpret_cast<DrawFn>(draw);
        g_OrigDrawSelf = reinterpret_cast<DrawFn>(drawSelf);
        g_Vtable[kDrawSlot / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&IconAreaDraw));
        g_Vtable[kDrawSelfSlot / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&IconAreaDrawSelf));
        g_SourceVtable = vtable;
    } else if (vtable != g_SourceVtable) {
        OSLog("WiiXLaunch GUI: map layer - screen %u's N_IconArea_00 has vtable %08x, not %08x; skipped\n", screen,
              static_cast<unsigned int>(vtable), static_cast<unsigned int>(g_SourceVtable));
        return false;
    }
    *reinterpret_cast<volatile uint32_t*>(pane + kPaneVtableOffset) = VtableWord();
    OSLog("WiiXLaunch GUI: map layer - screen %u's N_IconArea_00 at %08x hooked (vtable %08x)\n", screen,
          static_cast<unsigned int>(pane), static_cast<unsigned int>(vtable));
    return true;
}

// Every scan-out: keep every N_IconArea_00 on the private vtable. Cheap once
// hooked - a few pointer compares.
inline void OnScanOut(WiiXLaunch::BotW::GX2::CommandBuffer*, void*, int, int) {
    ++g_ScanOuts;
    bool intact = g_PaneCount > 0;
    for (uint32_t i = 0; i < g_PaneCount && intact; ++i) {
        intact = IsIconArea(g_Panes[i]) && Word(g_Panes[i] + kPaneVtableOffset) == VtableWord();
    }
    if (intact && (g_ScanOuts % 60u) != 0) return;   // a new screen may appear: look again once a second

    const uintptr_t mgr = Word(kScreenMgrPtr);
    if (!Plausible(mgr)) return;
    const uint32_t count = Word(mgr + kScreenCountOffset);
    const uintptr_t screens = Word(mgr + kScreenArrayOffset);
    if (!Plausible(screens) || count == 0 || count > 256) return;
    uint32_t found = 0;
    for (uint32_t i = 0; i < count && found < kMaxPanes; ++i) {
        const uintptr_t screen = Word(screens + i * 4);
        if (!Plausible(screen)) continue;
        const uintptr_t pane = Word(screen + kIconAreaOffset);
        if (!IsIconArea(pane)) continue;
        if (!Patch(pane, i)) continue;
        g_Panes[found] = pane;
        g_PaneScreens[found] = i;
        ++found;
    }
    g_PaneCount = found;
}

} // namespace impl

// Turns the map layer on. Idempotent. Until it is on, map-layer quads are
// recorded but never drawn.
inline void Enable() {
    if (impl::g_Enabled) return;
    impl::g_Enabled = true;
    WiiXLaunch::BotW::GX2::impl::g_MidLayoutContextWanted = true;
    WiiXLaunch::BotW::GX2::RegisterDrawCallback(&impl::OnScanOut);
    OSLog("WiiXLaunch GUI: map layer enabled\n");
}

} // namespace WiiXLaunch::BotW::GUI::MapLayer

#else

namespace WiiXLaunch::BotW::GUI::MapLayer {
inline void Enable() {}
}

#endif
