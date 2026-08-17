#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#if WIIXL_CEMU
#include "cemu_fs.hpp"
#elif WIIXL_WIIU
#include <coreinit/filesystem.h>
#endif

namespace WiiXLaunch::BotW::FS {

namespace impl {

// Static client and command block buffers to avoid heap allocation
alignas(32) inline uint8_t g_FSClient[0x1700];
alignas(32) inline uint8_t g_FSCmdBlock[0xA80];
inline bool g_FSClientReady = false;

inline bool EnsureFSClient() {
#if WIIXL_CEMU
    if (g_FSClientReady) return true;

    using FnFSAddClient = int32_t (*)(void* client, uint32_t errorMask);
    using FnFSInitCmdBlock = void (*)(void* cmdBlock);

    auto addClient = Backend::ResolveCemuFs<FnFSAddClient>(Backend::CemuFsImport::FSAddClient);
    auto initCmdBlock = Backend::ResolveCemuFs<FnFSInitCmdBlock>(Backend::CemuFsImport::FSInitCmdBlock);

    if (addClient && initCmdBlock) {
        int32_t status = addClient(g_FSClient, 0xFFFFFFFF);
        initCmdBlock(g_FSCmdBlock);
        g_FSClientReady = (status == 0);
        WIIXL_LOG("WiiXLaunch: FS client initialized (status=%d)", status);
        return g_FSClientReady;
    }
    return false;
#elif WIIXL_WIIU
    if (g_FSClientReady) return true;
    int32_t status = FSAddClient(reinterpret_cast<FSClient*>(g_FSClient), FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(reinterpret_cast<FSCmdBlock*>(g_FSCmdBlock));
    g_FSClientReady = (status == 0);
    return g_FSClientReady;
#else
    return true;
#endif
}

} // namespace impl

// Reads an entire file from disk (e.g. "WiiXLaunch/logo.bin" or "/vol/content/WiiXLaunch/logo.bin")
inline bool ReadFile(const char* path, void* outBuffer, size_t maxBufferSize, size_t* outReadSize = nullptr) {
    if (!path || !outBuffer || maxBufferSize == 0) return false;
    if (!impl::EnsureFSClient()) return false;

#if WIIXL_CEMU
    using FnFSOpenFile = int32_t (*)(void* client, void* block, const char* path, const char* mode, uint32_t* handle, uint32_t errorMask);
    using FnFSGetStatFile = int32_t (*)(void* client, void* block, uint32_t handle, void* stat, uint32_t errorMask);
    using FnFSReadFile = int32_t (*)(void* client, void* block, void* buffer, uint32_t size, uint32_t count, uint32_t handle, uint32_t unk1, uint32_t errorMask);
    using FnFSCloseFile = int32_t (*)(void* client, void* block, uint32_t handle, uint32_t errorMask);

    auto openFile = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
    auto getStat = Backend::ResolveCemuFs<FnFSGetStatFile>(Backend::CemuFsImport::FSGetStatFile);
    auto readFile = Backend::ResolveCemuFs<FnFSReadFile>(Backend::CemuFsImport::FSReadFile);
    auto closeFile = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);

    if (!openFile || !readFile || !closeFile) return false;

    auto concat2 = [](char* dst, size_t cap, const char* a, const char* b) {
        size_t la = strlen(a);
        size_t lb = strlen(b);
        if (la + lb + 1 > cap) return;
        memcpy(dst, a, la);
        memcpy(dst + la, b, lb);
        dst[la + lb] = '\0';
    };

    // Try both absolute /vol/content paths and relative content paths
    const char* pathsToTry[4] = {
        path,
        nullptr,
        nullptr,
        nullptr
    };
    char altPath1[256];
    char altPath2[256];
    char altPath3[256];
    if (path[0] != '/') {
        concat2(altPath1, sizeof(altPath1), "/vol/content/", path);
        concat2(altPath2, sizeof(altPath2), "content/", path);
        concat2(altPath3, sizeof(altPath3), "/vol/content/WiiXLaunch/", path);
        pathsToTry[1] = altPath1;
        pathsToTry[2] = altPath2;
        pathsToTry[3] = altPath3;
    }

    uint32_t handle = 0;
    int32_t status = -1;
    const char* openedPath = nullptr;

    for (int i = 0; i < 4; ++i) {
        if (!pathsToTry[i]) continue;
        status = openFile(impl::g_FSClient, impl::g_FSCmdBlock, pathsToTry[i], "r", &handle, 218);
        if (status == 0) {
            openedPath = pathsToTry[i];
            break;
        }
    }

    if (status != 0) {
        WIIXL_LOG("WiiXLaunch: FSOpenFile failed for '%s' (status=%d)", path, status);
        return false;
    }

    struct {
        uint32_t flags;
        uint32_t mode;
        uint32_t owner;
        uint32_t group;
        uint32_t size;
        uint32_t pad[19];
    } statBuf{};

    size_t toRead = maxBufferSize;
    if (getStat && getStat(impl::g_FSClient, impl::g_FSCmdBlock, handle, &statBuf, 0xFFFFFFFF) == 0) {
        if (statBuf.size > 0 && statBuf.size <= maxBufferSize) {
            toRead = statBuf.size;
        }
    }

    int32_t readBytes = readFile(impl::g_FSClient, impl::g_FSCmdBlock, outBuffer, 1, toRead, handle, 0, 0xFFFFFFFF);
    closeFile(impl::g_FSClient, impl::g_FSCmdBlock, handle, 0xFFFFFFFF);

    if (readBytes >= 0) {
        if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
        WIIXL_LOG("WiiXLaunch: ReadFile '%s' OK (%d bytes)", openedPath, readBytes);
        return true;
    }

    WIIXL_LOG("WiiXLaunch: FSReadFile failed for '%s' (status=%d)", openedPath, readBytes);
    return false;
#elif WIIXL_WIIU
    FSFileHandle handle = 0;
    FSStatus status = FSOpenFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                 reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                 path, "r", &handle, FS_ERROR_FLAG_ALL);
    if (status != FS_STATUS_OK) return false;

    FSStat stat{};
    size_t toRead = maxBufferSize;
    if (FSGetStatFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                      reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                      handle, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        if (stat.size > 0 && stat.size <= maxBufferSize) {
            toRead = stat.size;
        }
    }

    int32_t readBytes = FSReadFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                   reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                   reinterpret_cast<uint8_t*>(outBuffer), 1, toRead, handle, 0, FS_ERROR_FLAG_ALL);
    FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                handle, FS_ERROR_FLAG_ALL);

    if (readBytes >= 0) {
        if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
        return true;
    }
    return false;
#else
    return false;
#endif
}

// Writes a buffer to a file on disk
inline bool WriteFile(const char* path, const void* buffer, size_t size, size_t* outWrittenSize = nullptr) {
    if (!path || !buffer || size == 0) return false;
    if (!impl::EnsureFSClient()) return false;

#if WIIXL_CEMU
    using FnFSOpenFile = int32_t (*)(void* client, void* block, const char* path, const char* mode, uint32_t* handle, uint32_t errorMask);
    using FnFSWriteFile = int32_t (*)(void* client, void* block, const void* buffer, uint32_t size, uint32_t count, uint32_t handle, uint32_t unk1, uint32_t errorMask);
    using FnFSCloseFile = int32_t (*)(void* client, void* block, uint32_t handle, uint32_t errorMask);

    auto openFile = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
    auto writeFile = Backend::ResolveCemuFs<FnFSWriteFile>(Backend::CemuFsImport::FSWriteFile);
    auto closeFile = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);

    if (!openFile || !writeFile || !closeFile) return false;

    uint32_t handle = 0;
    int32_t status = openFile(impl::g_FSClient, impl::g_FSCmdBlock, path, "w", &handle, 0xFFFFFFFF);
    if (status != 0) {
        WIIXL_LOG("WiiXLaunch: FSOpenFile for write failed for '%s' (status=%d)", path, status);
        return false;
    }

    int32_t writtenBytes = writeFile(impl::g_FSClient, impl::g_FSCmdBlock, buffer, 1, size, handle, 0, 0xFFFFFFFF);
    closeFile(impl::g_FSClient, impl::g_FSCmdBlock, handle, 0xFFFFFFFF);

    if (writtenBytes >= 0) {
        if (outWrittenSize) *outWrittenSize = static_cast<size_t>(writtenBytes);
        WIIXL_LOG("WiiXLaunch: WriteFile '%s' OK (%d bytes)", path, writtenBytes);
        return true;
    }

    WIIXL_LOG("WiiXLaunch: FSWriteFile failed for '%s' (status=%d)", path, writtenBytes);
    return false;
#elif WIIXL_WIIU
    FSFileHandle handle = 0;
    FSStatus status = FSOpenFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                 reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                 path, "w", &handle, FS_ERROR_FLAG_ALL);
    if (status != FS_STATUS_OK) return false;

    int32_t writtenBytes = FSWriteFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                       reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                       reinterpret_cast<const uint8_t*>(buffer), 1, size, handle, 0, FS_ERROR_FLAG_ALL);
    FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                handle, FS_ERROR_FLAG_ALL);

    if (writtenBytes >= 0) {
        if (outWrittenSize) *outWrittenSize = static_cast<size_t>(writtenBytes);
        return true;
    }
    return false;
#else
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::FS
