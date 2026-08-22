#pragma once

// WiiXLaunch::BotW - a high-level, cross-platform (Switch/Wii U/Cemu) API
// over the RE work behind the Freecam and Actor Spawning example mods.
// #include this instead of hand-rolling offsets/vtable slots per mod.
//
//   WiiXLaunch::BotW::Player::GetEquippedSword()
//   WiiXLaunch::BotW::Controller::IsPressed(WiiXLaunch::BotW::Button::X)
//
// See docs/overview.md for the framework this sits on top of.

#include "actor.hpp"
#include "player.hpp"
#include "gamedata.hpp"
#include "pouch.hpp"
#include "armour.hpp"
#include "gametime.hpp"
#include "weather.hpp"
#include "climate.hpp"
#include "completion.hpp"
#include "map.hpp"
#include "events.hpp"
#include "controller.hpp"
#include "camera.hpp"
#include "flyt.hpp"
#include "nvn.hpp"
#include "log.hpp"
#include "gx2.hpp"
