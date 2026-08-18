# WiiXLaunch-BotW

A high-level, cross-platform (Switch / Wii U / Cemu) API for *The Legend of
Zelda: Breath of the Wild*, built on top of [WiiXLaunch](https://github.com/TKVSC-Team/WiiXLaunch).
This is the game-specific knowledge (vtable slots, memory offsets, actor
spawn plumbing) promoted out of individual mods into reusable classes, so a
new mod writes:

```cpp
#include <wiixlaunch.hpp>
#include <wiixlaunch/botw/botw.hpp>

using namespace WiiXLaunch::BotW;

WiiXLaunch::BotW::Actor sword = Player::GetEquippedSword();
if (Controller::IsPressed(Button::X)) { ... }
```

instead of hand-decoding vtable slot arithmetic or raw struct offsets per
project.

This repo is deliberately **not** part of base WiiXLaunch (the template repo
stays a generic, game-agnostic hooking framework) - it's an optional module
for mods that target BotW specifically, added as a git submodule by the
projects that want it.

## What's here

| Header | Provides |
| --- | --- |
| `botw/player.hpp` | `Player` - equipped sword/shield/bow, position, per-swing attack detection, a per-frame `OnTick` callback |
| `botw/actor.hpp` | `Actor` - a thin wrapper around a raw actor pointer (`GetName()`, `Delete()`), plus `Actor::Spawn(name, anchor, x, y, z)` |
| `botw/controller.hpp` | `Controller` - unified button/stick reads across Switch NPad and Wii U VPAD/KPAD (WPAD Pro + Core) |
| `botw/camera.hpp` | `Camera` - typed get/set accessors for a live camera object's position/look-at/up |
| `botw/nvn.hpp` | `NVN` - Switch NVN graphics injection, custom textures, packaged data, samplers, and 2D/3D drawing |
| `botw/gx2.hpp` | `GX2` - Wii U/Cemu GX2 graphics injection, mirrors `NVN`'s API (`Init`, `RegisterDrawCallback`, `CreateTexture`, `LoadTexture`, `LoadMesh`, `DrawSprite`, `DrawMesh`) |
| `botw/fs.hpp` | `FS` - cross-platform file read/write, used internally by `GX2::LoadTexture`/`LoadMesh` and available directly |
| `botw/log.hpp` | `OSLog` - Cemu-only logger (`OSReport`), used internally by `GX2`/`FS` for their own diagnostics |
| `botw/botw.hpp` | Umbrella include for all of the above |

`botw/gfd.hpp`, `botw/gx2_shader_types.hpp`, `botw/gx2_imports.hpp`, `botw/cemu_fs.hpp`, and `botw/cemu_logging.hpp` are internal support headers `gx2.hpp`/`fs.hpp`/`log.hpp` build on (GX2 shader-blob parsing, GX2 type/constant mirrors, and the Cemu "resolve real OS/GX2 calls from a bare code cave" shim tables). Not part of the public API, but worth knowing about if you're digging into how the Cemu side actually works.

Every offset and vtable slot here was reverse-engineered against the game
binaries (see the original mods' `handwritten-symbols-botw.csv` for
provenance/confidence notes on each one) - not guessed or ported from public
symbol databases.

## Platform coverage

Every WiiXLaunch build targets exactly one platform at compile time
(`WIIXL_SWITCH` / `WIIXL_WIIU` / `WIIXL_CEMU`), so capability gaps are plain
`constexpr bool` flags resolved for whichever platform you're building, not
a runtime check:

| Feature | Switch | Wii U / Cemu |
| --- | :---: | :---: |
| Equipped sword/shield/bow | ✅ | ✅ |
| Actor name (`Actor::GetName`) | ✅ | ✅ |
| Actor deletion (`Actor::Delete`) | ✅ | ✅ |
| Controller input | ✅ | ✅ |
| Camera pos/at/up | ✅ | ✅ |
| File read/write (`FS::ReadFile`/`WriteFile`) | ✅ | ✅ |
| Graphics injection (`NVN::SupportsNVN`) | ✅ | N/A |
| Graphics injection (`GX2::SupportsGX2`) | N/A | ✅ |
| Player position (`Player::SupportsPosition`) | ❌ | ✅ |
| Attack-swing tracking (`Player::SupportsAttackTracking`) | ❌ | ✅ |
| Actor spawning (`Actor::SupportsSpawn`) | ❌ | ✅ (see caveat) |
| `OSLog` (Cemu `OSReport`) | N/A | Cemu only, no-op on Wii U |

**Spawning caveat:** a spawned actor renders, but is only half-attached - it
never reaches Calc, and its model is not driven by its proc. It cannot be
repositioned or removed afterwards; deleting it destroys the proc and leaves the
model on screen. Fine for placing something permanent, not for anything that
needs to cycle or clear what it spawned. See `Actor::Spawn` for details.

Switch-unsupported calls are safe no-ops (return `false`/an invalid `Actor`)
rather than reading an offset that was never confirmed - check the
`Supports*` flag if your mod needs to branch on it. Filling in the missing
Switch RE work is welcome; see the confidence notes in the source mods'
`handwritten-symbols-botw.csv`.

## Using this in a mod

Add it as a submodule of your WiiXLaunch project:

```bash
git submodule add https://github.com/TKVSC-Team/WiiXLaunch-BotW vendor/wiixlaunch-botw
git submodule update --init --recursive
```

Then add its `include/` to your build's include path, alongside your
project's own `-I include`:

* `build_switch.bat`/`.sh`, `build_wiiu.bat`/`.sh`, `build_cemu.bat`/`.sh`: add `-I vendor\wiixlaunch-botw\include` (or the Linux-path equivalent) next to the existing `-I include`.
* `CMakeLists.txt` (Switch): add `vendor/wiixlaunch-botw/include` to `target_include_directories`.

No changes to your `main.cpp`'s `#include` lines are needed either way -
`#include <wiixlaunch/botw/botw.hpp>` resolves the same regardless of
whether these headers are locally copied or pulled in as a submodule.

## Design conventions

* **Header-only**, matching WiiXLaunch's own `include/wiixlaunch/*.hpp` - no separate `.cpp` to build or link.
* **Self-installing hooks.** Classes that need a hook expose a static `Init()` (call once from your `WiiXLaunch_Init()`); after that, only call typed getters - no raw offsets or `WIIXL_HOOK_DEFINE_TRAMPOLINE` in your own mod code.
* **Escape hatches, not a cage.** `Player::GetRaw()` / `Actor::GetRaw()` reach the raw pointer for anything not yet wrapped here.
* Depends only on WiiXLaunch's core (`platform.hpp`, `hook.hpp`, `call.hpp`) via `#include <wiixlaunch/...>` - resolved through your project's own include path, not assumed to be physically nested next to these files. No dependency on `WIIXL_LOG`/`debug_log.hpp`, since its signature isn't standardized across every WiiXLaunch project.

## License

GPLv3, matching [WiiXLaunch](https://github.com/TKVSC-Team/WiiXLaunch) - see `LICENSE`.
