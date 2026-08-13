#pragma once

#include <wiixlaunch/platform.hpp>
#include <cstdint>

// WiiXLaunch::BotW::Camera - field-layout accessors for the live camera
// object a LookAtCameraHook callback receives (sead::LookAtCamera on Switch
// / its Wii U equivalent, both confirmed working - ported from the Freecam
// mod's raw pos/at/up offset reads).
//
// Installs no hook itself: overriding the camera IS the mod's own behavior
// (when to override, by how much, on what input), so the hook stays owned
// by the mod - only the struct layout (which floats are pos/at/up) is game
// knowledge worth centralizing here.

namespace WiiXLaunch::BotW {

class Camera {
public:
#if WIIXL_SWITCH
    static constexpr uint32_t kPosOffset = 0x38;
    static constexpr uint32_t kAtOffset  = 0x44;
    static constexpr uint32_t kUpOffset  = 0x50;
#else
    // FUN_030bf280 = the real doUpdateMatrix equivalent, confirmed via
    // Ghidra: matches Switch's sead::LookAtCamera::doUpdateMatrix
    // algorithmic shape exactly (2 normalize passes, 2 cross products,
    // 12-float matrix output).
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
