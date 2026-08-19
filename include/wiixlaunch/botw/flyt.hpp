#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/hook.hpp>

#include <cstdint>

// WiiXLaunch::BotW::FLYT - runtime access to BotW's NintendoWare-derived "lyt"
// UI layout system (.bflyt / .bflan resources; chunk tags PAN1/PIC1/TXT1/...).
//
// RE status (full notes: vendor/wiixlaunch-botw/docs/flyt_notes.md):
//   - Base Pane struct (translate/rotate/scale/size/alpha/flags/name) is
//     CONFIRMED by decompiling the PAN1 chunk parser (Wii U 0x03c4952c),
//     field write order matches exactly - high confidence.
//   - FindPaneByName (Wii U 0x03009f84) is CONFIRMED via ~45 real call sites
//     in the game's own UI code, two of which look up "RootPane" the same
//     way Layout::GetRootPane() below does.
//   - There is still no plain BotW::FLYT::GetArchiveInMemory(name) here, and
//     there won't be one that calls Layout_LoadLayoutArchive (Wii U
//     0x03a314ec) cold. It IS confirmed real/reachable (a global dispatch
//     table at DAT_1035c514+0x8c) and fully traced through to the PAN1
//     parser, and a live Cemu breakpoint (flyt_notes.md §8) confirmed
//     `localeMgr` just needs to point at valid (not necessarily meaningful)
//     memory - but `buildCtx` is built entirely from raw PRT1-chunk fields
//     (two of its words are float ratios computed from chunk data), so it
//     genuinely cannot be safely fabricated from outside the real parser.
//     Instead, hook it - see Layout::InstallLoadHook()/OnLoaded()/SetRedirect()
//     below - and either capture the Layout*s the game loads on its own, or
//     redirect just the name argument to swap in a different .bflyt for a
//     call the game is already making. Layout::LoadAdditional() still exists
//     for raw/experimental use if you build buildCtx yourself from a real
//     PRT1 chunk, but is not meant for general use.
//   - Color is confirmed to live on visual pane subtypes, not the base Pane:
//     Picture panes carry 4x RGBA8 corner colors at +0xa8 (confirmed via
//     decompile). Text/Window panes presumably work the same way by
//     convention but weren't individually decompiled - treat PicturePane's
//     offset as unverified if reused for another subtype.
//   - Only Wii U/Cemu addresses were RE'd (no Switch Ghidra program was
//     available this pass) - FindPaneByName is Wii U/Cemu-only for now and
//     is a silent no-op on Switch. The struct-offset accessors themselves
//     are platform-agnostic and expected (not yet verified) to also hold on
//     Switch, since it's the same engine/compiler-ABI-family build.
//   - A few bytes are still unresolved and deliberately NOT exposed here:
//     the exact bit within the flags byte that means "visible" (bit 0 is
//     used below as NintendoWare's conventional position - verify before
//     relying on it), and a handful of unidentified fields at Pane+0xc/+0x78
//     /+0x7c. See flyt_notes.md §6.2 before extending this file.

namespace WiiXLaunch::BotW::FLYT {

class Pane;

namespace impl {

// Offsets into the base, 0xa4 (164)-byte Pane object, confirmed by
// decompiling the PAN1 chunk parser (Wii U 0x03c4952c) field-by-field.
constexpr uint32_t kPaneVtableOffset      = 0x08;
constexpr uint32_t kPaneTranslateOffset   = 0x1c; // 3x f32 (x, y, z)
constexpr uint32_t kPaneRotateOffset      = 0x28; // 3x f32 (x, y, z) - unit unconfirmed; NintendoWare convention is degrees
constexpr uint32_t kPaneScaleOffset       = 0x34; // 2x f32 (x, y)
constexpr uint32_t kPaneSizeOffset        = 0x3c; // 2x f32 (width, height)
constexpr uint32_t kPaneFlagsOffset       = 0x44; // u8 - individual bit meanings not confirmed except bit 0x10, forced on at construction
constexpr uint32_t kPaneAlphaOffset       = 0x45; // u8 - "resource"/source alpha; safe to write directly
constexpr uint32_t kPaneAlphaMirrorOffset = 0x46; // u8 - "effective" alpha, recomputed from +0x45 + parent alpha every frame - do NOT write this directly, it gets stomped
constexpr uint32_t kPaneOriginOffset      = 0x47; // u8 - anchor/origin enum
constexpr uint32_t kPaneNameOffset        = 0x80; // char[24], NUL-terminated
constexpr uint32_t kPaneStructSize        = 0xa4;

// Confirmed absent on the base Pane; visual subtypes append their own data
// right after the base 0xa4 bytes. Picture pane confirmed via Wii U
// 0x03c48290, which copies this straight from the PIC1 chunk.
constexpr uint32_t kPictureCornerColorsOffset = 0xa8; // 4x RGBA8 (TL, TR, BL, BR), 16 bytes total

// Best guess, NOT individually confirmed - see flyt_notes.md §6.2.
constexpr uint8_t kVisibleFlagBit = 0x01;

// FindPaneByName(Layout* layout, const void* safeString, void* outParamOrNull) -> Pane*
// Wii U 0x030813d0: searches the red-black tree at layout+0x28 and supports
// '/'-separated hierarchical paths ("Outer/Inner/Leaf").
//
// (Note: 0x03009f84 is a Screen-class helper that loads *(screen + 0x18) as the Layout*
// and tail-calls 0x030813d0. Since we operate directly on a Layout*, we call 0x030813d0).
//
// ABI NOTE: r4 is expected to point to a sead::SafeString struct
// { const char* str, void* vtable = &DAT_10263910 }, NOT a bare C-string.
using FindPaneByNameFn = void* (*)(void* layout, const void* safeString, void* outParamOrNull);

// Layout_LoadLayoutArchive(Layout* self, const char* bflytName, void* buildCtx, void* localeMgr) -> Layout*
// Wii U 0x03a314ec, confirmed reachable via a global dispatch table
// (DAT_1035c514+0x8c) - see flyt_notes.md §7.3 for the fully decompiled/
// disassembled pipeline. `self` must be a live, already-built Layout-family
// instance (88 bytes, +0x38 dispatch table, +0x20 ResourceAccessor) - this
// loads an ADDITIONAL .bflyt out of the archive `self` already has mounted,
// it does not mount a new archive from disk. `localeMgr` is unconditionally
// dereferenced (*(int*)(localeMgr + 0x18)) inside the callee - it must be
// non-null and point at a real object, or this crashes; its exact required
// shape is unconfirmed. `buildCtx` is passed straight through to the PAN1
// parser and its nullability is unconfirmed too.
using LoadLayoutArchiveFn = void* (*)(void* self, const char* bflytName, void* buildCtx, void* localeMgr);

// Hooks Layout_LoadLayoutArchive itself rather than trying to call it cold -
// buildCtx is built from raw PRT1-chunk fields inside the real parser (see
// flyt_notes.md §8.2) and can't be safely fabricated here, so this observes/
// redirects the game's own real calls instead of reimplementing them.
WIIXL_HOOK_DEFINE_TRAMPOLINE(LoadLayoutArchiveHook) {
    using OnLoadedFn = void (*)(void* resultLayout, const char* name);
    using RedirectFn = const char* (*)(void* self, const char* name);

    static OnLoadedFn& OnLoadedRef() { static OnLoadedFn fn = nullptr; return fn; }
    static RedirectFn& RedirectRef() { static RedirectFn fn = nullptr; return fn; }

    static void* Callback(void* self, const char* name, void* buildCtx, void* localeMgr) {
        const char* effectiveName = name;
        if (RedirectFn redirect = RedirectRef()) {
            if (const char* newName = redirect(self, name)) effectiveName = newName;
        }
        void* result = Orig(self, effectiveName, buildCtx, localeMgr);
        if (OnLoadedFn onLoaded = OnLoadedRef()) onLoaded(result, effectiveName);
        return result;
    }
};

} // namespace impl

// A single pane in a Layout's pane tree. Cheap, copyable handle around a raw
// Pane* - does not own or validate the pointer beyond a null check.
class Pane {
public:
    Pane() : m_Ptr(nullptr) {}
    explicit Pane(void* ptr) : m_Ptr(ptr) {}

    bool IsValid() const { return m_Ptr != nullptr; }
    void* GetRaw() const { return m_Ptr; }

    const char* GetName() const {
        if (!m_Ptr) return "(none)";
        return reinterpret_cast<const char*>(Byte(impl::kPaneNameOffset));
    }

    void GetTranslate(float& x, float& y, float& z) const { Get3(impl::kPaneTranslateOffset, x, y, z); }
    void SetTranslate(float x, float y, float z) {
        Set3(impl::kPaneTranslateOffset, x, y, z);
        if (m_Ptr) *Byte(impl::kPaneFlagsOffset) |= 0x10;
    }

    // Traverses up the parent chain to sum all parent translations (+0x0c is mParent)
    void GetParentGlobalTranslate(float& outX, float& outY, float& outZ) const {
        outX = 0.0f; outY = 0.0f; outZ = 0.0f;
        if (!m_Ptr) return;
        void* parent = *reinterpret_cast<void**>(static_cast<uint8_t*>(m_Ptr) + 0x0c);
        while (parent) {
            float px = 0, py = 0, pz = 0;
            Pane(parent).GetTranslate(px, py, pz);
            outX += px; outY += py; outZ += pz;
            parent = *reinterpret_cast<void**>(static_cast<uint8_t*>(parent) + 0x0c);
        }
    }

    // Calculates the pane's absolute screen/layout position independent of parent hierarchy
    void GetGlobalTranslate(float& outX, float& outY, float& outZ) const {
        GetTranslate(outX, outY, outZ);
        float px = 0, py = 0, pz = 0;
        GetParentGlobalTranslate(px, py, pz);
        outX += px; outY += py; outZ += pz;
    }

    // Sets the pane's position in absolute screen/layout coordinates
    // (compensating for all parent offsets in the layout tree)
    void SetGlobalTranslate(float globalX, float globalY, float globalZ) {
        float px = 0, py = 0, pz = 0;
        GetParentGlobalTranslate(px, py, pz);
        SetTranslate(globalX - px, globalY - py, globalZ - pz);
    }

    // Unit unconfirmed (NintendoWare convention: degrees, XYZ Euler order).
    void GetRotate(float& x, float& y, float& z) const { Get3(impl::kPaneRotateOffset, x, y, z); }
    void SetRotate(float x, float y, float z) {
        Set3(impl::kPaneRotateOffset, x, y, z);
        if (m_Ptr) *Byte(impl::kPaneFlagsOffset) |= 0x10;
    }

    void GetScale(float& x, float& y) const { Get2(impl::kPaneScaleOffset, x, y); }
    void SetScale(float x, float y) {
        Set2(impl::kPaneScaleOffset, x, y);
        if (m_Ptr) *Byte(impl::kPaneFlagsOffset) |= 0x10;
    }

    void GetSize(float& w, float& h) const { Get2(impl::kPaneSizeOffset, w, h); }
    void SetSize(float w, float h) {
        Set2(impl::kPaneSizeOffset, w, h);
        if (m_Ptr) *Byte(impl::kPaneFlagsOffset) |= 0x10;
    }

    // "Resource"/source alpha (+0x45) only - the mirrored "effective" alpha
    // at +0x46 is recomputed from this (combined with parent alpha) every
    // frame, so writing +0x45 is the correct way to change it persistently.
    uint8_t GetAlpha() const { return m_Ptr ? *Byte(impl::kPaneAlphaOffset) : 0; }
    void SetAlpha(uint8_t alpha) { if (m_Ptr) *Byte(impl::kPaneAlphaOffset) = alpha; }

    // "Effective" alpha (+0x46) - recomputed by the engine every frame from
    // this pane's own alpha combined with its ancestors'. READ-ONLY (writes
    // get stomped, see SetAlpha above), but as a *read* it is the truest
    // "is this actually on screen" signal available: unlike the visible
    // flag (whose bit position is still unconfirmed, see §6.2) it also
    // catches the case where a parent or the whole screen fades out while
    // this pane's own state never changes.
    uint8_t GetEffectiveAlpha() const { return m_Ptr ? *Byte(impl::kPaneAlphaMirrorOffset) : 0; }

    // Raw flags byte (+0x44). Exposed for diagnostics - individual bit
    // meanings beyond 0x10 are not confirmed.
    uint8_t GetFlagsRaw() const { return m_Ptr ? *Byte(impl::kPaneFlagsOffset) : 0; }

    // Bit meaning not individually confirmed - see flyt_notes.md §6.2. Bit 0
    // is used here as NintendoWare's conventional "visible" flag position;
    // verify against your own target build before shipping a mod on it.
    bool IsVisible() const { return m_Ptr && (*Byte(impl::kPaneFlagsOffset) & impl::kVisibleFlagBit) != 0; }
    void SetVisible(bool visible) {
        if (!m_Ptr) return;
        uint8_t& flags = *Byte(impl::kPaneFlagsOffset);
        if (visible) flags |= impl::kVisibleFlagBit;
        else flags &= static_cast<uint8_t>(~impl::kVisibleFlagBit);
    }

    // Finds a descendant child pane by name.
    // Wii U 0x03c49ee0: Pane::FindPaneByName(Pane* this, const char* name, bool recursive)
    //
    // Deliberately calls the game's own real function instead of
    // reimplementing tree traversal by hand: an earlier version here walked
    // a hand-decoded "circular list with sentinel at +0x14" that was never
    // actually confirmed against real behavior, and it silently broke the
    // HUD-loaded detection - OnLayoutLoaded() calls this once per heart pane
    // right before it sets s_PanesLoaded = true, so a bad offset in that
    // traversal (infinite loop / bad pointer) can prevent that flag from
    // ever being set, with no crash or error to point at the cause. Calling
    // the game's own tested function avoids re-litigating that risk.
    Pane FindChild(const char* name, bool recursive = true) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !name) return Pane();
        using PaneFindByNameFn = void* (*)(void* pane, const char* name, int recursive);
        auto findFn = WiiXLaunch::GetTargetFunction<PaneFindByNameFn>(0x0, 0x03c49ee0);
        return Pane(findFn(m_Ptr, name, recursive ? 1 : 0));
#else
        (void)name; (void)recursive;
        return Pane();
#endif
    }

    // Raw byte offset into this pane object, for reaching subtype-specific
    // data past the base 0xa4-byte region (e.g. PicturePane's corner
    // colors) without needing a dedicated wrapper for every pane subtype.
    uint8_t* FieldAt(uint32_t offset) const { return m_Ptr ? static_cast<uint8_t*>(m_Ptr) + offset : nullptr; }

protected:
    uint8_t* Byte(uint32_t offset) const { return static_cast<uint8_t*>(m_Ptr) + offset; }

    void Get2(uint32_t offset, float& a, float& b) const {
        if (!m_Ptr) { a = b = 0.0f; return; }
        float* f = reinterpret_cast<float*>(Byte(offset));
        a = f[0]; b = f[1];
    }
    void Set2(uint32_t offset, float a, float b) {
        if (!m_Ptr) return;
        float* f = reinterpret_cast<float*>(Byte(offset));
        f[0] = a; f[1] = b;
    }
    void Get3(uint32_t offset, float& a, float& b, float& c) const {
        if (!m_Ptr) { a = b = c = 0.0f; return; }
        float* f = reinterpret_cast<float*>(Byte(offset));
        a = f[0]; b = f[1]; c = f[2];
    }
    void Set3(uint32_t offset, float a, float b, float c) {
        if (!m_Ptr) return;
        float* f = reinterpret_cast<float*>(Byte(offset));
        f[0] = a; f[1] = b; f[2] = c;
    }

    void* m_Ptr;
};

// Specialization of Pane for PicturePane instances (the dominant UI element
// type in BotW - every icon, badge, and colored rect is one).
class PicturePane : public Pane {
public:
    PicturePane() : Pane() {}
    explicit PicturePane(void* ptr) : Pane(ptr) {}
    explicit PicturePane(const Pane& pane) : Pane(pane.GetRaw()) {}

    // Corner vertex colors at +0xa8 (4 x RGBA8888, 16 bytes total).
    void GetCornerColor(int corner, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const {
        if (!m_Ptr || corner < 0 || corner >= 4) return;
        uint8_t* p = FieldAt(0xa8 + corner * 4);
        r = p[0]; g = p[1]; b = p[2]; a = p[3];
    }

    void SetCornerColor(int corner, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (!m_Ptr || corner < 0 || corner >= 4) return;
        uint8_t* p = FieldAt(0xa8 + corner * 4);
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    }

    // Convenience: sets all four corners to the same flat color.
    void SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        for (int i = 0; i < 4; i++) SetCornerColor(i, r, g, b, a);
    }
};

// A loaded layout (a parsed .bflyt) - owns a pane tree reachable by name or
// '/'-separated path via FindPane. Construct from a raw Layout* your own
// hook already has (e.g. a UI screen class's member, captured from an
// Init/Calc hook) - see the file-level comment for why there's no
// GetArchiveInMemory(name) yet.
class Layout {
public:
    Layout() : m_Ptr(nullptr) {}
    explicit Layout(void* ptr) : m_Ptr(ptr) {}

    bool IsValid() const { return m_Ptr != nullptr; }
    void* GetRaw() const { return m_Ptr; }

    // Direct accessor to the layout's root pane (Layout + 0x0c).
    Pane GetRootPane() const {
        if (!m_Ptr) return Pane();
        return Pane(*reinterpret_cast<void**>(static_cast<uint8_t*>(m_Ptr) + 0x0c));
    }

    // name may be a single pane name ("N_Heart_02") or a '/'-separated
    // hierarchical path ("N_InOut_00/N_Heart_02"). Returns an invalid Pane if not found.
    Pane FindPane(const char* name) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !name) return Pane();

        Pane root = GetRootPane();
        if (!root.IsValid()) return Pane();

        const char* leaf = std::strrchr(name, '/');
        const char* queryName = leaf ? (leaf + 1) : name;

        return root.FindChild(queryName);
#else
        (void)name;
        return Pane();
#endif
    }

    // Installs the Layout_LoadLayoutArchive hook. Call once from
    // WiiXLaunch_Init() before relying on OnLoaded()/SetRedirect() below.
    static void InstallLoadHook() {
#if !WIIXL_SWITCH
        impl::LoadLayoutArchiveHook::Install(0x0, 0x03a314ec);
#endif
    }

    // Registers a callback fired with (loadedLayout, name) every time the
    // game itself loads/parses a named .bflyt - including nested Parts
    // (prt1) sub-layouts, which are loaded through this same function. Use
    // this to grab and cache Layout*s by name as the game loads them
    // naturally, instead of trying to invoke the loader yourself. Requires
    // InstallLoadHook() to have been called first.
    static void OnLoaded(void (*callback)(Layout loadedLayout, const char* name)) {
        static void (*s_Callback)(Layout, const char*) = nullptr;
        s_Callback = callback;
        impl::LoadLayoutArchiveHook::OnLoadedRef() = [](void* resultLayout, const char* name) {
            if (s_Callback) s_Callback(Layout(resultLayout), name);
        };
    }

    // Registers a callback that can redirect which .bflyt gets loaded for a
    // given (self, name) pair - return a different name to substitute it,
    // or nullptr to leave it unchanged. The buildCtx/localeMgr arguments
    // pass through to the real call untouched; only the name is
    // substitutable this way. Requires InstallLoadHook() to have been
    // called first.
    static void SetRedirect(const char* (*redirect)(void* self, const char* name)) {
        impl::LoadLayoutArchiveHook::RedirectRef() = redirect;
    }

    // EXPERIMENTAL / advanced use only - see impl::LoadLayoutArchiveFn and
    // flyt_notes.md §8 before touching this. Loads an additional named
    // .bflyt out of the archive this Layout already has mounted, as a new
    // Layout instance. `this` must be a real, already-built Layout - not
    // one you constructed by hand. `localeMgr` MUST be a valid, non-null
    // pointer (a live Cemu breakpoint confirmed it only needs to point at
    // *readable* memory, not any specific content, for a top-level Layout -
    // see flyt_notes.md §8.1) - but `buildCtx` is built from raw PRT1-chunk
    // fields inside the real parser (§8.2) and generally CANNOT be safely
    // fabricated by hand; prefer InstallLoadHook()/OnLoaded()/SetRedirect()
    // above instead of calling this directly.
    Layout LoadAdditional(const char* bflytName, void* buildCtx, void* localeMgr) const {
#if !WIIXL_SWITCH
        if (!m_Ptr || !localeMgr) return Layout();
        auto loadLayoutArchive = WiiXLaunch::GetTargetFunction<impl::LoadLayoutArchiveFn>(0x0, 0x03a314ec);
        return Layout(loadLayoutArchive(m_Ptr, bflytName, buildCtx, localeMgr));
#else
        (void)bflytName; (void)buildCtx; (void)localeMgr;
        return Layout();
#endif
    }

private:
    void* m_Ptr;
};

} // namespace WiiXLaunch::BotW::FLYT
