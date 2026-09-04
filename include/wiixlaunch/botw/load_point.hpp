#pragma once

// BotW's load point nomination.
//
// The load point is a per-game, per-platform fact, so base WiiXLaunch cannot
// know it - it owns the mechanism and never the address. Including this header
// is what nominates BotW's. A host with no game module installed nominates
// nothing, deploy.py emits no `.origin`, and it says so rather than producing a
// host that boots and silently loads no mods.
//
// WHY THIS ADDRESS (v208 Wii U RPX, read in Ghidra):
//
//   FUN_03098928 is the game's FS bring-up, and is also the function the Cemu
//   entry hook already sits on. It runs exactly once - FUN_03098a64 wraps it in
//   an `if (singleton == 0)` guard - and internally does:
//
//     030989bc  bl 0x04004ed0     FSInit()
//     030989c0  addi r3,r31,0x24  client = this + 0x24
//     030989c4  li   r4,0
//     030989c8  bl 0x04004e78     FSAddClient(client, 0)
//     030989cc  lis  r11,0x30a    <- LOAD POINT
//
// There is no cleaner site: the instruction after FUN_03098a64 returns is a
// vtable dispatch (0309f284 bctrl), so any post-FS-init point is necessarily
// mid-function.
//
// WHY NOMINATE AT ALL, given the Cemu probe found FS already usable at the
// entry hook itself: that result is emulator-specific. Cemu HLEs coreinit, so
// the filesystem is live from process start and the game's own FSInit concerns
// the game's client rather than the subsystem. Aroma runs against real IOSU and
// Switch has its own romfs mount timing; neither has been probed. Nomination is
// the only mechanism validated for the case where FS is genuinely not ready
// early, which is exactly what those two may turn out to be. See
// docs/loader.md in base WiiXLaunch.
//
// THE BRANCH IS EMITTED BY THE PACK, NOT WRITTEN AT RUNTIME. deploy.py puts
// `.origin = 0x030989CC / b wiixlaunch_loadpoint_stub` in patch_*.asm next to
// the entry hook. Writing it from WiiXLaunch_Init would mean modifying code
// inside a function Cemu may already have recompiled on entry at 0x03098928,
// and it would break the rule that only the host pack writes into game memory.
//
// SAFETY OF THE SITE, checked rather than assumed: nothing xrefs 0x030989CC, so
// the branch cannot be landed on from elsewhere, and the displaced instruction
// is position-independent (`lis r11,0x30a` - no relative branch, no PC-relative
// addressing), so it re-executes correctly from the codecave. Its pair, the
// `subi r11,r11,0x7880` at 0x030989D4 that completes the address load, is one
// instruction past the return address and is left untouched.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/load_point.hpp>
#include <wiixlaunch/loader/loader.hpp>
#include <wiixlaunch/loader/core_surface.hpp>

#if WIIXL_CEMU

WIIXL_DECLARE_LOAD_POINT(0x030989CC);

extern "C" void WiiXLaunch_LoadPointStub();

// Runs at the load point: this is where modules are read and started.
//
// The probe stays first. It costs one FSOpenFile and confirms the filesystem is
// actually usable at this site before the loader assumes it, which is the thing
// the whole stage-1 measurement established and the one assumption most likely
// to change if the game version or the emulator moves.
//
// `used` because the ONLY caller is the asm stub below, which the compiler
// cannot see - an inline function no C++ expression odr-uses is never emitted,
// and the link fails with an undefined reference from the asm block. Same rule
// as the deploy.py-patched globals: a symbol written or called by something
// outside the compiler's view has to be pinned. See docs/modules.md.
extern "C" __attribute__((used)) inline void WiiXLaunch_LoadPointProbe() {
    WiiXLaunch::LoadPoint::Probe("post-fsaddclient");

    // What this host is and what it offers, logged before any module is read,
    // so a rejection further down can be read against it.
    WIIXL_LOG("[loader] host ABI v%u, format v%u",
              WiiXLaunch::Core::kAbiVersion, WiiXLaunch::Wxlm::kFormatVersion);
    WiiXLaunch::Surface::LogRegistered();

    // One module, by fixed name. Enumerating the directory and loading several
    // is the next stage; this proves one end to end first.
    const WiiXLaunch::Wxlm::Reject r =
        WiiXLaunch::Loader::Load("WiiXLaunch/mods/sample.wxlm");
    if (r == WiiXLaunch::Wxlm::Reject::None) {
        WiiXLaunch::Loader::RunPhase(WiiXLaunch::Wxlm::Phase::Load);
    } else if (r == WiiXLaunch::Wxlm::Reject::ReadFailed) {
        // No modules present is the default state of a fresh host, not a fault.
        WIIXL_LOG("[loader] no module at WiiXLaunch/mods/sample.wxlm - nothing to load, "
                  "the game boots normally");
    }
}

// Register-preserving stub. The frame layout matches WiiXLaunch_Cemu_Init
// exactly (0x2000 bytes, r2-r31 at 0x1F80, LR at 0x2004, CR at 0x2008) because
// that one is known to work; this is not the place to invent a new one.
//
// Only ONE instruction is displaced, not four: the pack emits a single `b`, the
// same shape as the entry hook, rather than a 16-byte long jump. It has to run
// AFTER the restore, because r11 is inside the r2-r31 range lmw rewrites.
asm(
    ".section .text.WiiXLaunch_LoadPointStub\n"
    ".global WiiXLaunch_LoadPointStub\n"
    "WiiXLaunch_LoadPointStub:\n"
    "mflr 0\n"
    "stwu 1, -0x2000(1)\n"
    "stw 0, 0x2004(1)\n"
    "mfcr 0\n"
    "stw 0, 0x2008(1)\n"
    "stmw 2, 0x1F80(1)\n"

    "bl WiiXLaunch_LoadPointProbe\n"

    "lmw 2, 0x1F80(1)\n"
    "lwz 0, 0x2008(1)\n"
    "mtcr 0\n"
    "lwz 0, 0x2004(1)\n"
    "mtlr 0\n"
    "addi 1, 1, 0x2000\n"

    // The one instruction displaced from 0x030989CC by the pack's `b`.
    "lis 11, 0x30a\n"

    // Back to 0x030989D0. Literal immediates, so no relocation entry is
    // emitted and deploy.py leaves them alone - correct for a game address
    // that is already absolute.
    "lis 12, 0x0309\n"
    "ori 12, 12, 0x89d0\n"
    "mtctr 12\n"
    "bctr\n"
);

#endif // WIIXL_CEMU
