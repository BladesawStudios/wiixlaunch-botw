# --- WiiXLaunch GX2 import shims (Cemu code-cave only) ---
#
# Cemu's own patch assembler resolves `import.<lib>.<Name>` labels to the
# real gx2.rpl function address at patch-load time - proven by a real,
# installed, working graphic pack (BreathOfTheWild/Mods/FPS++/
# patch_VSync.asm calls `bl import.gx2.GX2SetSwapInterval` this exact way).
# Each shim below is a pure tail-call (`b`, not `bl`) so it forwards
# arguments/return untouched - calling a shim has the exact same effect as
# calling the real GX2 function directly.
#
# scripts/deploy.py includes this file into the same codecave as the
# compiled payload (right after its raw bytes, alongside cemu_logging.asm),
# and patches g_Gx2ShimTableOffset (see
# include/wiixlaunch/botw/gx2_imports.hpp) with THIS table's own offset -
# the line below tells deploy.py which C++ global to patch. Keep
# wiixlaunch_gx2_shim_table in EXACTLY the same order as the Gx2Import enum
# in that header - add new functions to both, in the same position, together.
#
# WIIXL_OFFSET_SYMBOL: g_Gx2ShimTableOffset

wiixlaunch_gx2_shim_table:
  .int wiixlaunch_gx2_shim_Init
  .int wiixlaunch_gx2_shim_SetupContextStateEx
  .int wiixlaunch_gx2_shim_SetViewport
  .int wiixlaunch_gx2_shim_SetScissor
  .int wiixlaunch_gx2_shim_ClearColor
  .int wiixlaunch_gx2_shim_SwapScanBuffers
  .int wiixlaunch_gx2_shim_SetSwapInterval
  .int wiixlaunch_gx2_shim_DrawDone

wiixlaunch_gx2_shim_Init:
  b import.gx2.GX2Init
wiixlaunch_gx2_shim_SetupContextStateEx:
  b import.gx2.GX2SetupContextStateEx
wiixlaunch_gx2_shim_SetViewport:
  b import.gx2.GX2SetViewport
wiixlaunch_gx2_shim_SetScissor:
  b import.gx2.GX2SetScissor
wiixlaunch_gx2_shim_ClearColor:
  b import.gx2.GX2ClearColor
wiixlaunch_gx2_shim_SwapScanBuffers:
  b import.gx2.GX2SwapScanBuffers
wiixlaunch_gx2_shim_SetSwapInterval:
  b import.gx2.GX2SetSwapInterval
wiixlaunch_gx2_shim_DrawDone:
  b import.gx2.GX2DrawDone
