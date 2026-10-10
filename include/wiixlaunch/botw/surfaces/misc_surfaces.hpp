#pragma once

// Five small surfaces that would each be a lonely file: camera, display,
// events, sound and memory. One header, five REGISTRATIONS - a mod still
// declares only what it needs, and a host missing one is still refused by name.
//
// They are grouped by how they are written, never by what they are: nothing
// here can see anything else here, and splitting the file later would change no
// mod's dependencies.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/botw/game/camera.hpp>
#include <wiixlaunch/botw/game/display.hpp>
#include <wiixlaunch/botw/game/events.hpp>
#include <wiixlaunch/botw/game/sound.hpp>
#include <wiixlaunch/botw/game/memory.hpp>

#include <cstdint>

// ===========================================================================
// botw.camera - the camera's position, target and up vector.
//
// EVERY CALL TAKES A CAMERA POINTER THE MOD ALREADY HAS, which makes this the
// one surface here shaped like the escape hatch, and it is worth being honest
// about why. The game does not expose a camera object a mod can look up; the
// module's own accessors take a `void*` because the only way to get one is to
// hook the camera update and be handed it. So a mod hooks that function through
// wiixl.core, gets the pointer as an argument, and passes it here.
//
// What this surface buys even so: the FIELD OFFSETS stay on this side. They
// differ between Switch and Wii U, they were confirmed in Ghidra, and a mod
// reaching +0x34 itself would be wrong on one platform and unfixable on both.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::CameraSurface {

constexpr const char* kName = "botw.camera";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// A pointer a mod hands back is not one the host issued, so it cannot be
// generation-checked the way an actor handle is. The least this can do is
// refuse the obviously impossible before writing through it.
inline bool Plausible(uintptr_t camera) {
    return camera >= 0x10000000u && camera < 0xa0000000u && (camera & 3u) == 0u;
}

extern "C" inline uint32_t CamGetPosition(uintptr_t camera, float* out3) {
    if (!out3 || !Plausible(camera)) return 0;
    Camera::GetPosition(reinterpret_cast<void*>(camera), out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t CamSetPosition(uintptr_t camera, float x, float y, float z) {
    if (!Plausible(camera)) return 0;
    Camera::SetPosition(reinterpret_cast<void*>(camera), x, y, z);
    return 1;
}

extern "C" inline uint32_t CamGetLookAt(uintptr_t camera, float* out3) {
    if (!out3 || !Plausible(camera)) return 0;
    Camera::GetLookAt(reinterpret_cast<void*>(camera), out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t CamSetLookAt(uintptr_t camera, float x, float y, float z) {
    if (!Plausible(camera)) return 0;
    Camera::SetLookAt(reinterpret_cast<void*>(camera), x, y, z);
    return 1;
}

extern "C" inline uint32_t CamGetUp(uintptr_t camera, float* out3) {
    if (!out3 || !Plausible(camera)) return 0;
    Camera::GetUp(reinterpret_cast<void*>(camera), out3[0], out3[1], out3[2]);
    return 1;
}

extern "C" inline uint32_t CamSetUp(uintptr_t camera, float x, float y, float z) {
    if (!Plausible(camera)) return 0;
    Camera::SetUp(reinterpret_cast<void*>(camera), x, y, z);
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("GetPosition", &CamGetPosition),
    WIIXL_SURFACE_SYMBOL("SetPosition", &CamSetPosition),
    WIIXL_SURFACE_SYMBOL("GetLookAt",   &CamGetLookAt),
    WIIXL_SURFACE_SYMBOL("SetLookAt",   &CamSetLookAt),
    WIIXL_SURFACE_SYMBOL("GetUp",       &CamGetUp),
    WIIXL_SURFACE_SYMBOL("SetUp",       &CamSetUp),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::CameraSurface


// ===========================================================================
// botw.display - the output aspect ratio.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::DisplaySurface {

constexpr const char* kName = "botw.display";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t DspSupports() { return Display::SupportsAspectRatio ? 1u : 0u; }

extern "C" inline uint32_t DspGetAspectRatio(float* out) {
    if (!out) return 0;
    *out = Display::GetAspectRatio();
    return *out > 0.0f ? 1u : 0u;
}

// The ratio as whole numbers - 16 and 9 rather than 1.7778 - because that is
// what goes in a label, and rounding a float back into a ratio at the call site
// is how "21:9" becomes "64:27".
extern "C" inline uint32_t DspGetAspectTerms(float aspect, int32_t* w, int32_t* h) {
    if (!w || !h) return 0;
    int iw = 0, ih = 0;
    if (!Display::GetAspectTerms(aspect, iw, ih)) return 0;
    *w = static_cast<int32_t>(iw);
    *h = static_cast<int32_t>(ih);
    return 1;
}

extern "C" inline uint32_t DspIsUltrawide() { return Display::IsUltrawide() ? 1u : 0u; }

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsAspectRatio", &DspSupports),
    WIIXL_SURFACE_SYMBOL("GetAspectRatio",      &DspGetAspectRatio),
    WIIXL_SURFACE_SYMBOL("GetAspectTerms",      &DspGetAspectTerms),
    WIIXL_SURFACE_SYMBOL("IsUltrawide",         &DspIsUltrawide),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::DisplaySurface


// ===========================================================================
// botw.events - koroks, shrines and towers.
//
// POLLED, NOT CALLED BACK, and that is a change of shape from the module's own
// API rather than a translation of it.
//
// Events exposes OnKorokGet/OnShrineComplete/OnTowerOpen: one callback slot
// each, last writer wins. For source mods that is workable. For compiled
// binaries it is the collision this architecture exists to remove - two .wxlm
// mods that both want to know about shrines cannot see each other, and the
// second silently takes the event away from the first.
//
// The module already provides ConsumeShrineComplete/ConsumeTowerOpen, which is
// the right shape: an event with a consumer rather than a subscriber. Korok had
// only the callback, so THIS SURFACE takes that one slot and turns it into a
// consumable count. The slot is used exactly once, by the host, and no mod can
// take it from another.
//
// A consumed event is gone. That is deliberate and it is the trade: two mods
// polling means the first to ask gets it. The alternative - fan-out to every
// registered mod - would need per-mod queues and a policy for the mod that
// never polls, and this surface does not pretend to have one.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::EventsSurface {

constexpr const char* kName = "botw.events";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

inline int32_t g_KorokTotal = 0;
inline int32_t g_KorokGained = 0;
inline bool g_KorokPending = false;
inline bool g_KorokHooked = false;

inline void OnKorok(int total, int gained) {
    g_KorokTotal = static_cast<int32_t>(total);
    g_KorokGained = static_cast<int32_t>(gained);
    g_KorokPending = true;
}

extern "C" inline uint32_t EvSupportsEvents() { return Events::SupportsEvents ? 1u : 0u; }

// Arms the event system, and takes the korok callback slot once. Idempotent:
// several mods will each call this and only the first does anything.
extern "C" inline uint32_t EvInit() {
    if (!g_KorokHooked) {
        g_KorokHooked = true;
        Events::OnKorokGet(&OnKorok);
        WIIXL_LOG("botw.events: korok callback slot taken by the host, so every "
                  "module can poll for it");
    }
    return Events::Resync() ? 1u : 0u;
}

extern "C" inline uint32_t EvIsArmed() { return Events::IsArmed() ? 1u : 0u; }
extern "C" inline uint32_t EvResync() { return Events::Resync() ? 1u : 0u; }

// Drive from a tick. The module's own Tick is what notices state changes; a
// host with no mod calling this simply never reports an event.
extern "C" inline void EvTick() { Events::Tick(); }

extern "C" inline uint32_t EvConsumeShrineComplete() {
    return Events::ConsumeShrineComplete() ? 1u : 0u;
}

extern "C" inline uint32_t EvConsumeTowerOpen() {
    return Events::ConsumeTowerOpen() ? 1u : 0u;
}

extern "C" inline uint32_t EvConsumeKorokGet(int32_t* total, int32_t* gained) {
    if (!g_KorokPending) return 0;
    g_KorokPending = false;
    if (total) *total = g_KorokTotal;
    if (gained) *gained = g_KorokGained;
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsEvents",        &EvSupportsEvents),
    WIIXL_SURFACE_SYMBOL("Init",                  &EvInit),
    WIIXL_SURFACE_SYMBOL("IsArmed",               &EvIsArmed),
    WIIXL_SURFACE_SYMBOL("Resync",                &EvResync),
    WIIXL_SURFACE_SYMBOL("Tick",                  &EvTick),
    WIIXL_SURFACE_SYMBOL("ConsumeShrineComplete", &EvConsumeShrineComplete),
    WIIXL_SURFACE_SYMBOL("ConsumeTowerOpen",      &EvConsumeTowerOpen),
    WIIXL_SURFACE_SYMBOL("ConsumeKorokGet",       &EvConsumeKorokGet),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::EventsSurface


// ===========================================================================
// botw.sound - playing the game's own sound events.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::SoundSurface {

constexpr const char* kName = "botw.sound";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t SndAvailable() { return Sound::Available() ? 1u : 0u; }

extern "C" inline uint32_t SndPlay(const char* eventName) {
    return eventName && Sound::Play(eventName) ? 1u : 0u;
}

// The module also has PlayFirstAvailable, which takes an array of C strings.
// It is NOT exposed: Play already reports whether an event existed, so the
// fallback loop is three lines a mod owns and can see, rather than an array of
// pointers marshalled across a boundary for no gain.
//
// Dumps the event names the game knows to the log. A discovery aid, not
// something to call in a tick - it prints up to maxNames lines.
extern "C" inline uint32_t SndDumpEvents(uint32_t maxNames) {
    Sound::DumpEvents(maxNames);
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Available",  &SndAvailable),
    WIIXL_SURFACE_SYMBOL("Play",       &SndPlay),
    WIIXL_SURFACE_SYMBOL("DumpEvents", &SndDumpEvents),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::SoundSurface


// ===========================================================================
// botw.memory - the GAME's heap, which is not the mod's arena.
//
// wiixl.core's Alloc hands out bytes from the module's own arena grant: bounded,
// attributed, and freed never. This is different memory for a different purpose
// - the game's main heap, which is where something has to live if the GAME is
// going to read it. A texture the game draws or a buffer it writes into cannot
// come from a module arena the game knows nothing about.
//
// It is also the one allocator here that can LEAK, since the game's heap is not
// carved per module and nothing reclaims what a mod forgets. Free exists and a
// mod is expected to use it; the host cannot do it for them, and this surface
// does not pretend otherwise.
// ===========================================================================
namespace WiiXLaunch::BotW::Surfaces::MemorySurface {

constexpr const char* kName = "botw.memory";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t MemSupportsAlloc() { return Memory::SupportsAlloc ? 1u : 0u; }

extern "C" inline uintptr_t MemAlloc(uint32_t size, int32_t align) {
    if (size == 0) return 0;
    return reinterpret_cast<uintptr_t>(Memory::Alloc(size, align ? align : 256));
}

extern "C" inline void MemFree(uintptr_t ptr) {
    if (ptr) Memory::Free(reinterpret_cast<void*>(ptr));
}

extern "C" inline uintptr_t MemGetMainGameHeap() {
    return reinterpret_cast<uintptr_t>(Memory::GetMainGameHeap());
}

// The module keeps IsPlausibleHeapPtr private, so this asks the question the
// way a caller can: a pointer is plausible if it sits inside the heap the
// module hands out from. Reimplementing the module's private check would be a
// second copy of a rule that could drift; deriving it from GetMainGameHeap
// cannot.
extern "C" inline uint32_t MemIsPlausibleHeapPtr(uintptr_t p) {
    if (!p || (p & 3u) != 0u) return 0;
    return (p >= 0x10000000u && p < 0xa0000000u) ? 1u : 0u;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsAlloc",      &MemSupportsAlloc),
    WIIXL_SURFACE_SYMBOL("Alloc",              &MemAlloc),
    WIIXL_SURFACE_SYMBOL("Free",               &MemFree),
    WIIXL_SURFACE_SYMBOL("GetMainGameHeap",    &MemGetMainGameHeap),
    WIIXL_SURFACE_SYMBOL("IsPlausibleHeapPtr", &MemIsPlausibleHeapPtr),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::MemorySurface
