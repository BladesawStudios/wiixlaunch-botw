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
  .int wiixlaunch_gx2_shim_SetContextState
  .int wiixlaunch_gx2_shim_Invalidate
  .int wiixlaunch_gx2_shim_SetColorBuffer
  .int wiixlaunch_gx2_shim_SetAttribBuffer
  .int wiixlaunch_gx2_shim_CalcFetchShaderSizeEx
  .int wiixlaunch_gx2_shim_InitFetchShaderEx
  .int wiixlaunch_gx2_shim_SetFetchShader
  .int wiixlaunch_gx2_shim_SetVertexShader
  .int wiixlaunch_gx2_shim_SetPixelShader
  .int wiixlaunch_gx2_shim_SetShaderModeEx
  .int wiixlaunch_gx2_shim_DrawEx
  .int wiixlaunch_gx2_shim_SetDepthOnlyControl
  .int wiixlaunch_gx2_shim_SetCullOnlyControl
  .int wiixlaunch_gx2_shim_CalcSurfaceSizeAndAlignment
  .int wiixlaunch_gx2_shim_InitColorBufferRegs
  .int wiixlaunch_gx2_shim_CopyColorBufferToScanBuffer
  .int wiixlaunch_gx2_shim_SetColorControl
  .int wiixlaunch_gx2_shim_SetTargetChannelMasks
  .int wiixlaunch_gx2_shim_SetBlendControl
  .int wiixlaunch_gx2_shim_Flush

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
wiixlaunch_gx2_shim_SetContextState:
  b import.gx2.GX2SetContextState
wiixlaunch_gx2_shim_Invalidate:
  b import.gx2.GX2Invalidate
wiixlaunch_gx2_shim_SetColorBuffer:
  b import.gx2.GX2SetColorBuffer
wiixlaunch_gx2_shim_SetAttribBuffer:
  b import.gx2.GX2SetAttribBuffer
wiixlaunch_gx2_shim_CalcFetchShaderSizeEx:
  b import.gx2.GX2CalcFetchShaderSizeEx
wiixlaunch_gx2_shim_InitFetchShaderEx:
  b import.gx2.GX2InitFetchShaderEx
wiixlaunch_gx2_shim_SetFetchShader:
  b import.gx2.GX2SetFetchShader
wiixlaunch_gx2_shim_SetVertexShader:
  b import.gx2.GX2SetVertexShader
wiixlaunch_gx2_shim_SetPixelShader:
  b import.gx2.GX2SetPixelShader
wiixlaunch_gx2_shim_SetShaderModeEx:
  b import.gx2.GX2SetShaderModeEx
wiixlaunch_gx2_shim_DrawEx:
  b import.gx2.GX2DrawEx
wiixlaunch_gx2_shim_SetDepthOnlyControl:
  b import.gx2.GX2SetDepthOnlyControl
wiixlaunch_gx2_shim_SetCullOnlyControl:
  b import.gx2.GX2SetCullOnlyControl
wiixlaunch_gx2_shim_CalcSurfaceSizeAndAlignment:
  b import.gx2.GX2CalcSurfaceSizeAndAlignment
wiixlaunch_gx2_shim_InitColorBufferRegs:
  b import.gx2.GX2InitColorBufferRegs
wiixlaunch_gx2_shim_CopyColorBufferToScanBuffer:
  b import.gx2.GX2CopyColorBufferToScanBuffer
wiixlaunch_gx2_shim_SetColorControl:
  b import.gx2.GX2SetColorControl
wiixlaunch_gx2_shim_SetTargetChannelMasks:
  b import.gx2.GX2SetTargetChannelMasks
wiixlaunch_gx2_shim_SetBlendControl:
  b import.gx2.GX2SetBlendControl
wiixlaunch_gx2_shim_Flush:
  b import.gx2.GX2Flush
