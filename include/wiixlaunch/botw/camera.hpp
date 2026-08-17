#pragma once

#include <wiixlaunch/platform.hpp>
#include <cstdint>

// Camera field accessors (pos/at/up); mod installs its own hook to use these.

namespace WiiXLaunch::BotW {

class Camera {
public:
#if WIIXL_SWITCH
    static constexpr uint32_t kPosOffset = 0x38;
    static constexpr uint32_t kAtOffset  = 0x44;
    static constexpr uint32_t kUpOffset  = 0x50;
#else
    // Wii U offsets confirmed via Ghidra (matches Switch layout).
    static constexpr uint32_t kPosOffset = 0x34;
    static constexpr uint32_t kAtOffset  = 0x40;
    static constexpr uint32_t kUpOffset  = 0x4C;
#endif

    static void GetPosition(void* camera, float& x, float& y, float& z) { Get(camera, kPosOffset, x, y, z); }
    static void SetPosition(void* camera, float x, float y, float z) { Set(camera, kPosOffset, x, y, z); }

    static void GetLookAt(void* camera, float& x, float& y, float& z) { Get(camera, kAtOffset, x, y, z); }
    static void SetLookAt(void* camera, float x, float y, float z) { Set(camera, kAtOffset, x, y, z); }

    static void GetUp(void* camera, float& x, float& y, float& z) { Get(camera, kUpOffset, x, y, z); }
    static void SetUp(void* camera, float x, float y, float z) { Set(camera, kUpOffset, x, y, z); }

private:
    static void Get(void* camera, uint32_t offset, float& x, float& y, float& z) {
        float* f = reinterpret_cast<float*>(static_cast<uint8_t*>(camera) + offset);
        x = f[0]; y = f[1]; z = f[2];
    }
    static void Set(void* camera, uint32_t offset, float x, float y, float z) {
        float* f = reinterpret_cast<float*>(static_cast<uint8_t*>(camera) + offset);
        f[0] = x; f[1] = y; f[2] = z;
    }
};

} // namespace WiiXLaunch::BotW
