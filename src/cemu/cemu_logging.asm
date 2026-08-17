# --- WiiXLaunch coreinit logging import shims (Cemu code-cave only) ---
#
# Same mechanism as gx2_imports.asm in this directory (an
# `import.<lib>.<Name>` tail-call shim, resolved by Cemu's own patch
# assembler) - split into its own file/table since these are coreinit
# concerns, not gx2 ones. See include/wiixlaunch/botw/cemu_logging.hpp and
# log.hpp for the C++ side.
#
# scripts/deploy.py includes this file into the same codecave as the
# compiled payload, and patches g_CemuLoggingShimTableOffset (see
# cemu_logging.hpp) with THIS table's own offset - the line below tells
# deploy.py which C++ global to patch. Keep
# wiixlaunch_cemu_logging_shim_table in EXACTLY the same order as the
# CemuLogImport enum in that header.
#
# WIIXL_OFFSET_SYMBOL: g_CemuLoggingShimTableOffset

wiixlaunch_cemu_logging_shim_table:
  .int wiixlaunch_cemu_logging_shim_OSReport
  .int wiixlaunch_cemu_logging_shim_MEMAllocFromDefaultHeapEx
  .int wiixlaunch_cemu_logging_shim_MEMFreeToDefaultHeap

wiixlaunch_cemu_logging_shim_OSReport:
  # OSReport is variadic - the PowerPC EABI varargs convention requires the
  # CALLER to set CR bit 6 (cr1's eq bit) to indicate whether any FLOATING
  # POINT args were passed through "...": 0 = none. A real, working graphic
  # pack (BreathOfTheWild/Cheats/PreventRandomSpawns/patch_PreventActorSpawns.asm)
  # does exactly this before its own `bl import.coreinit.OSReport` call.
  # GCC devkitPPC's own variadic-call codegen has no reason to know about
  # this Cafe-SDK-specific bit, so calling through our shim from ordinary
  # C++ left it as whatever garbage was already in CR. This did not crash
  # Cemu's OSReport - it just silently produced nothing, which for a while
  # looked like the cause of the "message never shows up" mystery. The real
  # cause turned out to be simpler and unrelated: Cemu's OSReport
  # implementation (WriteCafeConsole, coreinit_Misc.cpp) line-buffers and
  # only flushes to the log on '\n' - BotW::OSLog (log.hpp) always appends
  # one now. This CR-bit fix is still correct and still required regardless
  # - keeping it clears real garbage, it just wasn't the whole story.
  crxor 4*cr1+eq, 4*cr1+eq, 4*cr1+eq
  b import.coreinit.OSReport

wiixlaunch_cemu_logging_shim_MEMAllocFromDefaultHeapEx:
  b import.coreinit.OSAllocFromMEM2Arena

wiixlaunch_cemu_logging_shim_MEMFreeToDefaultHeap:
  b import.coreinit.OSFreeToMEM2Arena
