#pragma once

// Resolved via the consuming project's own include path (-I include), not
// relative paths - this module lives in its own repo (see README.md) and
// only needs to sit alongside a normal WiiXLaunch project's include/, not be
// physically nested inside its include/wiixlaunch/ tree.
#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/botw/game/memory.hpp>
#include <wiixlaunch/fs.hpp>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

// WiiXLaunch::BotW::Vfx - spawn any .sesetlist/.esetlist from the content
// folder as a live particle effect at any position, scale and rotation,
// through the game's own particle system (V208 Wii U/Cemu).
//
// This is the generalized form of the resId-management / registerResource /
// createEmitterSet sequence originally worked out (and hardened against real
// crashes and visual corruption) for TotK-Abilities-BotW's Ascend rune - see
// that project's totk/ascend.hpp for the fixed-slot version this replaces.
// Three lessons from that work are load-bearing here and repeated inline
// below rather than re-earned by the next caller:
//
//   1. registerResource does NOT refuse an occupied resId - it silently
//      DESTROYS whatever the game already had there. Our resIds are handed
//      out counting DOWN from the top of the engine's 256-entry resource
//      table, as far as possible from the game's own bottom-up allocator
//      (which starts at 10 and grows up), and FindOrLoadResource refuses to
//      register onto a slot it finds already occupied as a second check.
//
//   2. Effect resource buffers must be allocated 0x2000-aligned. Each
//      texture's GX2Surface is placed at bufferBase + <its offset in the
//      file>, and for a macro-tiled surface the LOW ADDRESS BITS of that sum
//      are the tiling bank/pipe interleave bits GX2 uses to address it. A
//      buffer that is only 256-aligned (the allocator's default) reads every
//      texture starting mid-tile, which decodes as stair-stepped, shifted
//      blocks - this was measured and fixed once already; do not "simplify"
//      the alignment back down.
//
//   3. An emitter set self-invalidates the instant its last particle dies -
//      the engine reuses that memory for the next set it creates. A raw
//      `handle != 0` check is not enough: IsValid() re-checks the set's
//      generation word the same way the engine's own per-frame walker does,
//      and Stop()/Update() both gate on it so a stale handle can never be
//      used to reposition or kill somebody else's live effect.
//
// Wii U/Cemu only - unconfirmed on Switch, see SupportsVfx.

namespace WiiXLaunch::BotW {

class Vfx {
public:
    static constexpr bool SupportsVfx = !WIIXL_SWITCH;

    // Identifies one live emitter set. {0, 0} (default-constructed) is never
    // a valid handle - IsValid() rejects it before touching memory.
    struct Handle {
        uint32_t ptr = 0;
        uint32_t generation = 0;
    };

    // Loads (once per unique path, cached thereafter) and spawns a fresh,
    // independent instance of esetlistPath at (x, y, z). Two Spawn() calls
    // for the same path run as two fully independent live effects - Stop one
    // and the other keeps playing.
    //
    // esetlistPath is a content-relative path, e.g.
    // "Effect/SplPwr_Tooreroof_Ring.sesetlist", resolved the same way
    // WiiXLaunch::FS::ReadFile resolves any other content path.
    //
    // scale is per-axis and multiplies the eset's own authored size; rotation
    // is degrees about the world X, Y and Z axes respectively, applied in
    // that order (see BuildTransformMatrix). Defaults give the eset's
    // authored size with no rotation, matching a plain SpawnFx(path, x, y, z)
    // call.
    //
    // Returns a zeroed Handle (IsValid() == false) on any failure: unloadable
    // file, exhausted resource cache, or the particle system not being ready
    // yet (e.g. called before a world is loaded).
    static Handle Spawn(const char* esetlistPath, float x, float y, float z,
                        float scaleX = 1.0f, float scaleY = 1.0f, float scaleZ = 1.0f,
                        float rotXDeg = 0.0f, float rotYDeg = 0.0f, float rotZDeg = 0.0f) {
#if WIIXL_SWITCH
        (void)esetlistPath; (void)x; (void)y; (void)z;
        (void)scaleX; (void)scaleY; (void)scaleZ;
        (void)rotXDeg; (void)rotYDeg; (void)rotZDeg;
        return {};
#else
        int idx = FindOrLoadResource(esetlistPath);
        if (idx < 0) return {};

        void* ptclSys = GetPtclSys();
        if (!ptclSys) return {};

        // enroll MUST be 1. FUN_03b66238 (createEmitterSet) links the new set
        // into one of two lists depending on this flag: enroll=0 goes to a
        // "manual" list nothing ever calc's or draws; enroll=1 goes to the
        // "active" list the engine's own per-frame update/draw walker
        // actually traverses.
        uint32_t handleWords[2] = {};
        using CreateEmitterSetFn = uint32_t (*)(void*, uint32_t*, int, int, uint32_t, uint32_t, uint32_t, int);
        auto createEmitterSet = WiiXLaunch::GetTargetFunction<CreateEmitterSetFn>(0x0, kCreateEmitterSetAddr);
        uint32_t ok = createEmitterSet(ptclSys, handleWords, /*esetId*/ 0,
                                       /*resId*/ s_Resources[idx].resId,
                                       /*groupId*/ 0, 0, 0, /*enroll*/ 1);
        if (!ok || handleWords[0] == 0 || !IsMappedPtr(reinterpret_cast<void*>(handleWords[0]))) {
            return {};
        }

        Handle handle{ handleWords[0], handleWords[1] };
        SetTransform(handle, x, y, z, scaleX, scaleY, scaleZ, rotXDeg, rotYDeg, rotZDeg);
        WIIXL_LOG("WiiXLaunch::Vfx: Spawn(%s) -> handle=0x%x @ (%.1f, %.1f, %.1f)",
                  esetlistPath, handle.ptr, x, y, z);
        return handle;
#endif
    }

    // Moves/rescales/rerotates an already-live handle in place, without
    // respawning it (a respawn restarts the whole effect from frame one,
    // which reads as flicker for anything mid-animation). Returns false if
    // the handle is not currently valid - it may have already finished and
    // self-invalidated, which is a normal outcome, not an error.
    static bool Update(Handle handle, float x, float y, float z,
                       float scaleX = 1.0f, float scaleY = 1.0f, float scaleZ = 1.0f,
                       float rotXDeg = 0.0f, float rotYDeg = 0.0f, float rotZDeg = 0.0f) {
#if WIIXL_SWITCH
        (void)handle; (void)x; (void)y; (void)z;
        (void)scaleX; (void)scaleY; (void)scaleZ;
        (void)rotXDeg; (void)rotYDeg; (void)rotZDeg;
        return false;
#else
        if (!IsValid(handle)) return false;
        SetTransform(handle, x, y, z, scaleX, scaleY, scaleZ, rotXDeg, rotYDeg, rotZDeg);
        return true;
#endif
    }

    // True if handle still refers to a live emitter set. Checks the set's
    // generation word, not just a non-null pointer - see point 3 in the
    // header comment for why that distinction matters.
    static bool IsValid(Handle handle) {
#if WIIXL_SWITCH
        (void)handle;
        return false;
#else
        if (handle.ptr == 0) return false;
        auto* set = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(handle.ptr));
        if (!IsMappedPtr(set)) return false;
        return *reinterpret_cast<uint32_t*>(set + kGenerationOffset) == handle.generation;
#endif
    }

    // Stops and releases a live effect. Safe to call on an already-invalid
    // handle (no-op) - never assume you are the only thing that might have
    // stopped it already.
    static void Stop(Handle handle) {
#if !WIIXL_SWITCH
        if (!IsValid(handle)) return;
        void* ptclSys = GetPtclSys();
        if (!ptclSys) return;
        using KillEmitterSetFn = void (*)(void*, uint32_t, uint32_t);
        auto killEmitterSet = WiiXLaunch::GetTargetFunction<KillEmitterSetFn>(0x0, kKillEmitterSetAddr);
        killEmitterSet(ptclSys, handle.ptr, 1);
#else
        (void)handle;
#endif
    }

private:
#if !WIIXL_SWITCH
    // No default member initializers: an array of this type is a static
    // member of Vfx itself (s_Resources, below), and a nested class's default
    // member initializers are not usable until the OUTERMOST enclosing
    // class's definition is complete - so relying on them here would fail to
    // compile at exactly that use site. Zero-initialization of the static
    // array below gives every field the same zero/null/false starting value
    // these would have provided anyway.
    struct EffectResource {
        char path[192];
        int resId;
        void* buffer;
        bool registered;
    };

    // How many distinct esetlist paths this module can have loaded at once
    // (repeated Spawn() calls for an already-loaded path are cheap and don't
    // consume another slot). Sized generously for "any mod using this
    // vendor module" while keeping kResIdBase (below) comfortably clear of
    // the game's own bottom-up resId allocator.
    static constexpr int kMaxEffectResources = 64;
    static inline EffectResource s_Resources[kMaxEffectResources] = {};
    static inline int s_ResourceCount = 0;

    static constexpr int kResIdTableSize = 0x100;
    static constexpr int kResIdBase = kResIdTableSize - kMaxEffectResources;  // 192
    static_assert(kResIdBase > 10, "must stay clear of the game's own resId range");

    static int ResIdFor(int index) { return kResIdBase + index; }

    struct NwPtclHeapWrapper {
        void* vtable;
        void* heap;
    };
    static inline NwPtclHeapWrapper s_HeapWrapper = {};
    static inline bool s_HeapWrapperInited = false;

    static constexpr uintptr_t kXlinkSysProviderAddr = 0x1047f018;
    static constexpr uint32_t kPtclSysOffset          = 0x6d0;
    static constexpr uint32_t kResourceTableOffset    = 0x68;
    static constexpr uintptr_t kHeapWrapperVtableAddr = 0x1032b9d8;
    static constexpr uintptr_t kCreateEmitterSetAddr  = 0x03b66238;
    static constexpr uintptr_t kRegisterResourceAddr  = 0x03b66018;
    static constexpr uintptr_t kSetMatrixAddr         = 0x03b59550;
    static constexpr uintptr_t kKillEmitterSetAddr    = 0x03b66714;
    // Offset of an emitter set's generation word, bumped whenever the engine
    // recycles that memory for a new set. See IsValid().
    static constexpr uint32_t kGenerationOffset       = 0x14;

    static bool IsMappedPtr(const void* ptr) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        return addr >= 0x00010000 && addr < 0xf8000000 && (addr & 3) == 0;
    }

    static void* GetPtclSys() {
        void* xlinkSys = *reinterpret_cast<void**>(kXlinkSysProviderAddr);
        if (!IsMappedPtr(xlinkSys)) return nullptr;
        void* ptclSys = *reinterpret_cast<void**>(static_cast<uint8_t*>(xlinkSys) + kPtclSysOffset);
        return IsMappedPtr(ptclSys) ? ptclSys : nullptr;
    }

    static void EnsureHeapWrapper() {
        if (s_HeapWrapperInited) return;
        // This fake vtable's slot 0x14 (called by registerResource for its
        // own internal allocations) reads heap+0xc as a real vtable pointer
        // and calls +0x34 (tryAlloc) on it - the same shape Memory::Alloc's
        // InGameAlloc call needs. A null heap here dereferences address 0xc
        // and crashes, so this must be the same proven-good heap Memory uses.
        s_HeapWrapper.vtable = reinterpret_cast<void*>(kHeapWrapperVtableAddr);
        s_HeapWrapper.heap = WiiXLaunch::BotW::Memory::GetMainGameHeap();
        s_HeapWrapperInited = true;
    }

    // Finds an already-loaded+registered resource by path, or loads and
    // registers a new one. Returns its index into s_Resources, or -1 on any
    // failure (see the header comment for why each of these checks exists).
    static int FindOrLoadResource(const char* path) {
        if (!path || !path[0]) return -1;

        for (int i = 0; i < s_ResourceCount; ++i) {
            if (std::strncmp(s_Resources[i].path, path, sizeof(s_Resources[i].path)) == 0) {
                return s_Resources[i].registered ? i : -1;
            }
        }

        if (s_ResourceCount >= kMaxEffectResources) {
            WIIXL_LOG("WiiXLaunch::Vfx: resource cache full (%d/%d slots); cannot load %s",
                      s_ResourceCount, kMaxEffectResources, path);
            return -1;
        }

        void* ptclSys = GetPtclSys();
        if (!ptclSys) return -1;

        EnsureHeapWrapper();

        int idx = s_ResourceCount;
        EffectResource& res = s_Resources[idx];
        std::strncpy(res.path, path, sizeof(res.path) - 1);
        res.path[sizeof(res.path) - 1] = '\0';
        res.resId = ResIdFor(idx);

        if (!res.buffer) {
            constexpr size_t kVfxBufSize = 3 * 1024 * 1024;
            constexpr int32_t kVfxBufAlign = 0x2000;  // see header comment, point 2
            res.buffer = WiiXLaunch::BotW::Memory::Alloc(kVfxBufSize, kVfxBufAlign);
            if (!res.buffer) {
                WIIXL_LOG("WiiXLaunch::Vfx: AllocMem failed for %s", res.path);
                return -1;
            }
            size_t readBytes = 0;
            bool ok = WiiXLaunch::FS::ReadFile(res.path, res.buffer, kVfxBufSize, &readBytes);
            if (!ok || readBytes == 0) {
                WIIXL_LOG("WiiXLaunch::Vfx: failed to load %s", res.path);
                return -1;
            }
            WIIXL_LOG("WiiXLaunch::Vfx: loaded %s (%u bytes at %p)", res.path,
                      static_cast<unsigned int>(readBytes), res.buffer);
        }

        uint32_t* resTable = *reinterpret_cast<uint32_t**>(
            static_cast<uint8_t*>(ptclSys) + kResourceTableOffset);
        if (IsMappedPtr(resTable) && resTable[res.resId] != 0) {
            WIIXL_LOG("WiiXLaunch::Vfx: REFUSING to register %s at resId=%d -- occupied by 0x%x "
                      "(registering would destroy the game's own resource)",
                      res.path, res.resId, resTable[res.resId]);
            return -1;
        }

        using RegisterResFn = uint32_t (*)(void*, void*, void*, uint32_t, uint32_t, uint32_t);
        auto registerRes = WiiXLaunch::GetTargetFunction<RegisterResFn>(0x0, kRegisterResourceAddr);
        uint32_t ret = registerRes(ptclSys, &s_HeapWrapper, res.buffer, res.resId, 0, 0);
        res.registered = (ret != 0);
        WIIXL_LOG("WiiXLaunch::Vfx: registerResource(%s resId=%d) -> %u", res.path, res.resId, ret);
        if (!res.registered) return -1;

        ++s_ResourceCount;
        return idx;
    }

    struct Mat3 { float m[3][3]; };

    static Mat3 RotX(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Mat3{{ {1,0,0}, {0,c,-s}, {0,s,c} }};
    }
    static Mat3 RotY(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Mat3{{ {c,0,s}, {0,1,0}, {-s,0,c} }};
    }
    static Mat3 RotZ(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Mat3{{ {c,-s,0}, {s,c,0}, {0,0,1} }};
    }
    static Mat3 Mat3Mul(const Mat3& a, const Mat3& b) {
        Mat3 r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) sum += a.m[i][k] * b.m[k][j];
                r.m[i][j] = sum;
            }
        return r;
    }

    // Builds the row-major 3x4 transform the engine's SetMatrix expects
    // (see UpdateFxMatrix in TotK-Abilities-BotW's ascend.hpp for the
    // confirmed shape: each row is {basisX, basisY, basisZ, translation}).
    // The vertex shader computes centre = M * vec4(localPos, 1), so the
    // basis IS the effect's world rotation+scale and the last column is its
    // world position.
    //
    // Rotation is a standard right-handed rotation about the world X axis,
    // then Y, then Z (R = Rz * Ry * Rx applied to local space) - the usual
    // fixed-axis roll/pitch/yaw composition. Scale is applied in local space
    // before rotation (M = R * diag(sx, sy, sz)), i.e. it multiplies the
    // eset's own authored axes, not the world axes.
    static void BuildTransformMatrix(float outMtx[12], float px, float py, float pz,
                                     float sx, float sy, float sz,
                                     float rxDeg, float ryDeg, float rzDeg) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        Mat3 r = Mat3Mul(RotZ(rzDeg * kDegToRad),
                         Mat3Mul(RotY(ryDeg * kDegToRad), RotX(rxDeg * kDegToRad)));
        const float scale[3] = { sx, sy, sz };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] *= scale[j];

        outMtx[0] = r.m[0][0]; outMtx[1] = r.m[0][1]; outMtx[2] = r.m[0][2]; outMtx[3] = px;
        outMtx[4] = r.m[1][0]; outMtx[5] = r.m[1][1]; outMtx[6] = r.m[1][2]; outMtx[7] = py;
        outMtx[8] = r.m[2][0]; outMtx[9] = r.m[2][1]; outMtx[10] = r.m[2][2]; outMtx[11] = pz;
    }

    static void SetTransform(Handle handle, float x, float y, float z,
                             float scaleX, float scaleY, float scaleZ,
                             float rotXDeg, float rotYDeg, float rotZDeg) {
        alignas(16) float mtx[12];
        BuildTransformMatrix(mtx, x, y, z, scaleX, scaleY, scaleZ, rotXDeg, rotYDeg, rotZDeg);
        using SetMatrixFn = void (*)(uint32_t, const float*);
        auto setMatrix = WiiXLaunch::GetTargetFunction<SetMatrixFn>(0x0, kSetMatrixAddr);
        setMatrix(handle.ptr, mtx);
    }
#endif // !WIIXL_SWITCH
};

} // namespace WiiXLaunch::BotW
