#pragma once

#include <wiixlaunch/platform.hpp>

#include <cstdint>

// WiiXLaunch::BotW::Display - what the frame is actually presented as.
//
// The colour buffer cannot answer this on Cemu. A resolution or ultrawide
// graphic pack scales the render target behind the game's back through
// texture rules, so the GX2 surface the game declares stays 1280x720 whatever
// the player is really looking at.
//
// The game's own aspect ratio constant can, and that is exactly what an
// ultrawide pack has to change for the game to render correctly: Cemu's BotW
// "Graphics" pack (patch_AspectRatio.asm) overwrites three .rodata floats
// with `$width / $height` - the real presented aspect - and repoints the two
// code sites that build projections at a codecave copy of the same value. So
// reading that constant reads the output aspect, whether it is the stock 16:9
// or the 21:9 a pack put there.

namespace WiiXLaunch::BotW::Display {

#if !WIIXL_SWITCH

constexpr bool SupportsAspectRatio = true;

namespace impl {

// V208 .rodata. Ships holding 1.777778 (16/9) and is one of the three the
// Graphics pack rewrites; 0x101BF8E8 (1.777, a rounded variant) and
// 0x1036DD4C are the other two. This one is the exact 16/9 and has a single
// reader, the projection helper at 0x036fefdc, which is what makes it the
// one worth trusting.
constexpr uintptr_t kAspectRatioAddress = 0x1030A57C;

// Everything from 1:1 to past 48:9 (5.33), the widest the pack offers.
// Outside that the address is not holding what we think -
// a different game version, or read before the binary was mapped - and
// saying so is better than scaling the whole UI by a garbage number.
constexpr float kMinPlausibleAspect = 0.5f;
constexpr float kMaxPlausibleAspect = 8.0f;

} // namespace impl

// The aspect ratio the frame is presented at, or 0 if the value read was not
// plausible. 1.7778 on a stock game; 2.3333 with a 21:9 pack.
inline float GetAspectRatio() {
    const float value = *reinterpret_cast<const volatile float*>(impl::kAspectRatioAddress);
    if (value >= impl::kMinPlausibleAspect && value <= impl::kMaxPlausibleAspect) return value;
    return 0.0f;
}

// The aspect as whole-number terms, so it can be shown the way people write
// it: 1.7778 becomes 16:9, 2.3333 becomes 21:9. Returns false when nothing
// sensible fits, and the caller should fall back to printing the ratio.
//
// Named ratios come first, and with a deliberately loose 3% tolerance, because
// the pack writes width/height of the chosen RESOLUTION rather than the aspect
// the user picked from the dropdown. Every resolution its "21:9" category
// offers - 2560x1080, 3440x1440, 3840x1600 - is a different number (2.370,
// 2.389, 2.400), none of them 21/9, and all of them should read back as the
// 21:9 that was selected. 3% covers all three without any two names colliding:
// the closest pair here, 16:10 and 5:3, are 4.2% apart.
inline bool GetAspectTerms(float aspect, int& outWidth, int& outHeight) {
    struct Named { int w, h; float value; };
    static constexpr Named kNamed[] = {
        {5, 4, 1.2500f}, {4, 3, 1.3333f}, {3, 2, 1.5000f}, {16, 10, 1.6000f},
        {5, 3, 1.6667f}, {16, 9, 1.7778f}, {21, 9, 2.3333f}, {32, 10, 3.2000f},
        {32, 9, 3.5556f}, {48, 9, 5.3333f},
    };
    if (aspect <= 0.0f) return false;
    for (const Named& n : kNamed) {
        const float error = aspect > n.value ? aspect / n.value : n.value / aspect;
        if (error < 1.03f) {
            outWidth = n.w;
            outHeight = n.h;
            return true;
        }
    }
    // Nothing named: reduce it to the smallest denominator that lands within
    // a quarter of a percent, which covers the odd resolutions without
    // producing something like 1153:487.
    for (int h = 1; h <= 32; ++h) {
        const float exact = aspect * static_cast<float>(h);
        const int w = static_cast<int>(exact + 0.5f);
        if (w <= 0) continue;
        const float value = static_cast<float>(w) / static_cast<float>(h);
        const float error = aspect > value ? aspect / value : value / aspect;
        if (error < 1.0025f) {
            outWidth = w;
            outHeight = h;
            return true;
        }
    }
    return false;
}

// True when the frame is being presented wider than the 16:9 the game was
// built for - i.e. an ultrawide pack is in play.
inline bool IsUltrawide() {
    const float aspect = GetAspectRatio();
    return aspect > 0.0f && aspect > (16.0f / 9.0f) + 0.01f;
}

#else

constexpr bool SupportsAspectRatio = false;
inline float GetAspectRatio() { return 0.0f; }
inline bool GetAspectTerms(float, int&, int&) { return false; }
inline bool IsUltrawide() { return false; }

#endif

} // namespace WiiXLaunch::BotW::Display
