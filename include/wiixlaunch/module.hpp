#pragma once

// THE FIXED PATH A HOST LOOKS FOR.
//
// src/main.cpp picks a game module up with __has_include(<wiixlaunch/module.hpp>)
// and calls WiiXLaunch::GameModule::Register(). Base therefore never names a
// game, and a checkout with no module in vendor/ still compiles - it registers
// base's surfaces, says so in the log, and refuses by name any .wxlm that
// wanted a game surface.
//
// Every wiixlaunch-<game> module provides this one header. It is the whole
// contract: a name for the log, and a registration call. Anything else the
// module offers is reached through its own headers, by mods, not by the host.
//
// Note what is NOT here: no load point. On Cemu the load point is an address
// inside the game, so the module nominates it (see botw/load_point.hpp). On
// Wii U the plugin lifecycle supplies one and the host does not need a module
// to have it, which is why src/wiiu_plugin.cpp drives the loader directly.

#include "botw/botw.hpp"
#include "botw/surfaces.hpp"

namespace WiiXLaunch::GameModule {

// For the host's log line, so a boot log says which game module is installed
// before any surface list is printed.
inline constexpr const char* kName = "botw";

inline void Register() {
    WiiXLaunch::BotW::Surfaces::Register();
}

} // namespace WiiXLaunch::GameModule
