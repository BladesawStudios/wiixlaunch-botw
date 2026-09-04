#pragma once

// WiiXLaunch::BotW - a high-level, cross-platform (Switch/Wii U/Cemu) API
// over the RE work behind the Freecam and Actor Spawning example mods.
// #include this instead of hand-rolling offsets/vtable slots per mod.
//
//   WiiXLaunch::BotW::Player::GetEquippedSword()
//   WiiXLaunch::BotW::Controller::IsPressed(WiiXLaunch::BotW::Button::X)
//
// See docs/overview.md for the framework this sits on top of.

#include "game/actor.hpp"
#include "game/memory.hpp"
#include "game/vfx.hpp"
#include "game/player.hpp"
#include "game/gamedata.hpp"
#include "game/pouch.hpp"
#include "game/armour.hpp"
#include "game/gametime.hpp"
#include "game/weather.hpp"
#include "game/climate.hpp"
#include "game/completion.hpp"
#include "game/map.hpp"
#include "game/region.hpp"
#include "game/events.hpp"
#include "game/controller.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/sound.hpp"
#include "game/flyt.hpp"
#include "graphics/nvn.hpp"
#include "platform/log.hpp"
#include "platform/heap.hpp"
#include "graphics/gx2.hpp"
#include "gui/gui.hpp"

// Nominates BotW's load point to the host (see load_point.hpp) and declares
// this module's export surfaces. Including botw.hpp is what makes a project a
// BotW host; the project still calls Surfaces::Register() explicitly, because
// the flat Cemu payload does not run static constructors.
#include "load_point.hpp"
#include "surfaces.hpp"
