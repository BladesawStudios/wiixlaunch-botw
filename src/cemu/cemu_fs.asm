# --- WiiXLaunch coreinit filesystem import shims (Cemu code-cave only) ---
#
# Same mechanism as gx2_imports.asm and cemu_logging.asm.
# scripts/deploy.py includes this file into the codecave and patches
# g_CemuFsShimTableOffset with wiixlaunch_cemu_fs_shim_table's offset.
#
# WIIXL_OFFSET_SYMBOL: g_CemuFsShimTableOffset

wiixlaunch_cemu_fs_shim_table:
  .int wiixlaunch_cemu_fs_shim_FSAddClient
  .int wiixlaunch_cemu_fs_shim_FSDelClient
  .int wiixlaunch_cemu_fs_shim_FSInitCmdBlock
  .int wiixlaunch_cemu_fs_shim_FSOpenFile
  .int wiixlaunch_cemu_fs_shim_FSGetStatFile
  .int wiixlaunch_cemu_fs_shim_FSReadFile
  .int wiixlaunch_cemu_fs_shim_FSWriteFile
  .int wiixlaunch_cemu_fs_shim_FSCloseFile
  .int wiixlaunch_cemu_fs_shim_FSReadFileWithPos

wiixlaunch_cemu_fs_shim_FSAddClient:
  b import.coreinit.FSAddClient
wiixlaunch_cemu_fs_shim_FSDelClient:
  b import.coreinit.FSDelClient
wiixlaunch_cemu_fs_shim_FSInitCmdBlock:
  b import.coreinit.FSInitCmdBlock
wiixlaunch_cemu_fs_shim_FSOpenFile:
  b import.coreinit.FSOpenFile
wiixlaunch_cemu_fs_shim_FSGetStatFile:
  b import.coreinit.FSGetStatFile
wiixlaunch_cemu_fs_shim_FSReadFile:
  b import.coreinit.FSReadFile
wiixlaunch_cemu_fs_shim_FSWriteFile:
  b import.coreinit.FSWriteFile
wiixlaunch_cemu_fs_shim_FSCloseFile:
  b import.coreinit.FSCloseFile
wiixlaunch_cemu_fs_shim_FSReadFileWithPos:
  b import.coreinit.FSReadFileWithPos
