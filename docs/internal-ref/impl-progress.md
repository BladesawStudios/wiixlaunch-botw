# Systems:

## Input:
- ~~Get/SetInputs~~ (controller.hpp)
- GetKeyboardInput() // mouse is not possible without external software.

## GameData:
- ~~Get/SetGameData~~ - controls an insane amount of the game (gamedata.hpp)
- Create new GameData flags - only existing flags are read/writable, store is fixed-size (gamedata.hpp)

## Actor / Player Stats:
- ~~Get/SetLife~~ - also works on weapons for durability (actor.hpp, pouch.hpp)
- ~~Get/SetMaxLife~~ (gamedata.hpp)
- ~~Get/SetStamina~~ (gamedata.hpp)
- ~~Get/SetMaxStamina~~ (gamedata.hpp)
- ~~Get/SetPosition~~ (actor.hpp, player.hpp)
- ~~Get/SetRotation~~ (actor.hpp)
- ~~Get/SetLinearVelocity (add or subtract)~~ (actor.hpp; player velocity specifically needs Get/SetControllerVelocity, since SetLinearVelocity refuses state-1 actors)
- Get/SetRotationalVelocity
- Get/SetRagdoll
- Get/SetActorParams
- Get/SetPlayerAction

## Equipment & Inventory:
- ~~Get/SetEquippedSword~~ (player.hpp getter; equip via pouch.hpp)
- ~~Get/SetEquippedShield~~ (player.hpp)
- ~~Get/SetEquippedBow~~ (player.hpp)
- ~~Get/SetEquippedArmor(Head/Chest/Legs)~~ (pouch.hpp EquipItem, armour.hpp piece query)
- ~~Get/SetArmourEffects~~ (armour.hpp; SetExtraEffect stacks past the 1-effect-per-piece limit)
- ~~Get/SetInventorySlot~~ (pouch.hpp)
- ~~Get/SetFoodEffect~~ (pouch.hpp Get/SetCookData + CookEffectName/CookEffectFromName)
- ~~Get/SetEquipEffect~~ (pouch.hpp Get/SetModifier + ModifierName/ModifierFromName)
- ~~Give/TakeItem~~ (pouch.hpp)

## World / Environment:
- ~~Get/SetGameTime (In-Game Time)~~ (gametime.hpp)
- ~~Get/SetWeather~~ (weather.hpp; HoldWeather/TickWeatherHold keeps it set past the 4-frame countdown SetWeather alone doesn't survive)
- ~~Get/SetClimate~~ (climate.hpp, weather.hpp)
- ~~SetMapRegionUnlock~~ (map.hpp)
- ~~GetCurrentRegion~~ (region.hpp)
- TriggerPBM

## Progression:
- ~~Get/SetCompletionPercent~~ (completion.hpp - percent is real/computed, display can also be overridden independent of save)
- ~~Get/SetKorokCount~~ (completion.hpp)

## Runes:
- Get/SetRuneUnlock
- Get/SetRune
- Rune System (Register/Grant/RemoveAbility)

## Camera:
- ~~Get/SetCameraPosition~~ (camera.hpp)
- Get/SetCameraRotation - only LookAt/Up vectors exist, no Euler rotation (camera.hpp)
- Get/SetCameraParams (layered on top, so only values we pass change)

## Display/System:
- Get/SetResolution - not settable; the GUI READS the colour buffer it is handed (largest of the frame, since the TV and the 854x480 GamePad view are both drawn) via Canvas::DeviceWidth/Height
- ~~read the real framerate~~ (gui_render.hpp: Time::GetMonotonicTicks/mftb, Canvas::FramesPerSecond/DeltaSeconds/TimeSeconds/Phase - animation is time-based so FPS++ at 60 no longer doubles its speed)
- ~~output aspect, including a Cemu ultrawide pack: read from the game's own aspect constant, which such a pack must rewrite anyway~~ (game/display.hpp, gui_render.hpp; GUI::SetOutputAspect overrides)

## Checks & Subscriptions:
- OnBulletTime
- ~~OnKorokGet~~ (events.hpp)
- ~~OnShrineComplete~~ (events.hpp - flag write site unverified)
- ~~OnTowerOpen~~ (events.hpp)
- OnMainQuestComplete
- OnSideQuestComplete
- OnChestOpen
- OnBloodMoon
- OnActorDefeat

## Actors (spawning & query):
- ~~Queryable Actors (query all spawned actors by ID or name instead of pointer)~~ (actor.hpp)
- ~~Spawn/Despawn Actors~~ (actor.hpp)

## AI/Events (.bfevfl):
- AI/Pathfinding stuff (not scoped)
- Run/StopEvent (and run specific actions with their parameters)

## AS/State:
- AS System (not implemented - no rune/ability/action-state system)

## Havok (Physics):
- read-only: GetPhysicsObject, GetHavokMotion, GetHavokVelocity (actor.hpp)
- Raycasting
- no rigid body creation/queries

## XLink (E & S):
- Call E/SLinks (BGM?)

## Graphics:
### NVN (Switch):
- ~~CreateTexture, DrawSprite, DrawMesh, own depth buffer~~ (nvn.hpp)

### GX2 (Wii U):
- ~~CreateTexture, DrawSprite, DrawMesh~~ (gx2.hpp, gfd.hpp, gx2_shader_types.hpp, gx2_imports.hpp)
- ~~CreateTextureFromSurface (pre-tiled BCn/A8 upload), BeginBatch/BatchQuad/EndBatch~~ (gx2.hpp)

## GUI (custom in-game UI, base-game style):
- ~~GX2: fonts (Normal_00/NormalS_00 BFFNT) + layout art (Common.sblarc BFLIM) streamed from the game's own archives at runtime, text with the game's metrics, MessageBox/RoundedBox/SelectFrame/cursors, Button/Toggle/Slider/Selector with D-pad focus~~ (gui/gui.hpp + gui_*.hpp, graphics/bffnt.hpp, bflim.hpp, platform/yaz0.hpp, sarc.hpp) - verified in Cemu 2.6/v208: loader gets 2/2 fonts + 27/27 sprites, dialogue box renders correctly; widget panel not yet seen (test mod: ../BreathOfTheWild_GUITest), see docs/gui.md
- NVN: stub (SupportsGUI=false)
- ~~GX2 blend modes (Blend::Alpha/Additive/Overlay/Multiply/Opaque/Premultiplied/Subtract + FromLyt, batched by (texture, blend))~~ (gx2.hpp)
- ~~group alpha stack (PushAlpha/PopAlpha, the lyt pane-alpha chain)~~ (gui.hpp)
- ~~resolution handling: real device size, Fit/Stretch mapping, device-pixel snapping, exact last-texel edge sampling~~ (gui_render.hpp) - the colour buffer is 854x480 on the GamePad view, not 1280x720
- ~~kerning (KRNG, word offsets, keyed by character code)~~ (bffnt.hpp, gui_text.hpp)
- ~~free quad rotation (libm-free sin/cos; lyt angles need their sign flipped, panes are y-up)~~ (gui_render.hpp)
- ~~cursors/frames drawn as real lyt window frames (corners + stretched edges), measured edge-sampling UVs per sprite, option cursor at its true additive alpha 128, nine-sliced plate with shadow~~ (gui.hpp, gui_render.hpp)
- ~~rounded boxes/outlines as window frames too (the corner art carries transparent padding that only lines up when the edges come from the same texture) - fixes the notch at every corner seen at 1440p; plate = shadow + opaque base + rim over a padded window~~ (gui.hpp)
- ~~edge quads repeat ONE texel column/row (a degenerate UV range) instead of spanning to 1.0 - the span was fading every frame edge out left-to-right wherever the art stops short of the tile~~ (gui.hpp)
- ~~per-sprite component-map override: a BC5 '^t' red channel is sometimes a TEV gradient, not a colour (SelectFrame_04 rendered black-grey-white)~~ (gui_render.hpp, gx2_shader_types.hpp)
- ~~plate corner keeps the layout's 96/240 proportion instead of the largest that fits~~ (gui.hpp)
- ~~NormalS_00 (outlined face) dropped: not loaded, styles use Normal, FontId::NormalSmall falls back~~ (gui_types.hpp, gui_render.hpp, gui_text.hpp)
- ~~MessageBox auto-shrinks past the box's three lines (Canvas::FitToBox)~~ (gui.hpp, gui_text.hpp)
- ~~texture sampler CLAMP constant was 0 = WRAP (GX2 clamp is 2), so every quad edge wrapped into the art's transparent padding - the pale outline around every element~~ (gx2_shader_types.hpp; also fixes GX2::CreateTexture for existing mods)
- ~~plate base fills exactly the visible plate (growing it past the soft rim read as a doubled outline); KeyHint proportions + returns its width~~ (gui.hpp)
- ~~frame corners drawn to the art's extent (0..EdgeU/V), not the tile's - the leftover sliver was a line down every join on art that stops short (select frame)~~ (gui.hpp, gui_render.hpp)
- ~~plate surface radius is the rim's real curve (42/96 of the tile, measured), not 16/96 - square-cornered fill inside a rounded rim~~ (gui.hpp)
- ~~select frame corner keeps the layout's 68/186 proportion~~ (gui.hpp)
- ~~dropped the black blob behind the dialogue speaker name (DialogShadow_00 is a radial QUADRANT, not a blob)~~ (gui.hpp)
- ~~input capture (stop the game seeing menu input): Controller::SetInputCapture/HoldInputCapture blanks buttons, sticks and GamePad touch in the VPAD/KPAD/npad read hooks AFTER our own state is stored, rebuilding the release edge so game code doesn't see a stuck button; injected input still passes. Canvas::CaptureInput() is the per-frame form and lapses if it stops being called. The GUI builds each frame from Controller::OnInputRead (before the game sees the pad) and replays the recorded quads at present time, so even the press that opens a menu is captured~~ (controller.hpp, gui.hpp, gui_render.hpp)
- ~~framebuffer blur behind windows (the game's FBLayout capture): GX2::BlurBackdrop aliases the colour buffer as a texture, downsamples 4x into one of two small render targets and runs four-tap box passes; GUI::SetBackdropBlur opts in (off by default), Canvas::BlurBehind draws it~~ (gx2.hpp, gui.hpp) - rectangular only, no per-pixel mask (needs a two-texture shader)
- exact plate shading (BtnBasic inner texture via TEV) - approximated
- alpha compare (a few materials enable it) - everything is blended instead
- Caption_00 / Special_00 / External_00 fonts - not loaded (loader handles them, just not in the table)

## Layouts (flyt & flan):
- ~~flyt: Pane/PicturePane/Layout - translate/rotate/scale/size/alpha/visible, FindPane, corner colors, load hook+redirect~~ (flyt.hpp)
- flan: not implemented

## Models (fres, fska, fmdl, etc)
- bone accessors and manipulators for any actor: GetBoneByNameInModel(), BoneGet/SetPos/Rot/Scale().
- show/hide sub-objects, change material params: GetMaterialParamIndexByKey(), Get/SetMaterialParamByIndex(), Show/HideSubObjectByName()
### Notes: 
I'd really love to get custom shaders replacing in game ones (basically reconstructing UBO's until we can get 3D geometry to render properly, would enable SO much. It's already been done for particles, so it's certainly possible.)

# Coverage:
| Function | Switch | Wii U |
| --- | --- | --- |
| `actor.hpp::Actor::Get/SetCurrentLife` | X | ✓ |
| `actor.hpp::Actor::GetMaxLife` | X | ✓ |
| `actor.hpp::Actor::Get/SetMatrix, SetMtx, WarpTo, NudgeTo` | X | ✓ |
| `actor.hpp::Actor::Get/SetRotation` | X | ✓ |
| `actor.hpp::Actor::Get/Set/AddLinearVelocity` | X | ✓ |
| `actor.hpp::Actor::Get/SetControllerVelocity` (real player-movable field) | X | ✓ |
| `actor.hpp::Actor::Spawn/SpawnScaled/Delete` | X | ✓ |
| `actor.hpp::Actor::ForEach/ForEachDynamic/ForEachStatic/Query*Actors` | X | ✓ |
| `player.hpp::Player::GetEquippedSword/Shield/Bow` | ✓ | ✓ |
| `player.hpp::Player::Get/SetPosition, NudgePosition` | X | ✓ |
| `player.hpp::Player::ConsumeAttackEvent` | X | ✓ |
| `armour.hpp::Armour::Get/SetPieceEffect, Get/SetArmourEffects` | X | ✓ |
| `armour.hpp::Armour::Get/SetExtraEffect` (stacked, past 1-per-piece) | X | ✓ |
| `pouch.hpp::Pouch::AddItem/RemoveItem/EquipItem` | X | ✓ |
| `pouch.hpp::Pouch::Get/SetModifier, ModifierName/FromName` | X | ✓ |
| `pouch.hpp::Pouch::CookIngredient/Get/SetCookData, CookEffectName/FromName` | X | ✓ |
| `gamedata.hpp::GameData::Get/SetFlag{S32,Bool,F32,Vec3}` | X | ✓ |
| `gamedata.hpp::GameData::Get/SetMaxLife, Get/SetStamina, Get/SetMaxStamina` | X | ✓ |
| `gamedata.hpp::GameData::Get/SetRupees/AddRupees` | X | ✓ |
| `gametime.hpp::Time::Get/SetGameTime, Get/SetDay, Get/SetTimeScale` | X | ✓ |
| `weather.hpp::Weather::Get/SetWeather/ClearWeather, HoldWeather/TickWeatherHold` | X | ✓ |
| `climate.hpp::Climate::Get/SetTemperature` | X | ✓ |
| `completion.hpp::Completion::GetCompletion*, Get/SetKorokCount, SetDisplayed*` | X | ✓ |
| `map.hpp::Map::Get/SetMapRegionUnlock, shrine/beast markers` | X | ✓ |
| `region.hpp::Region::GetRegionAt/GetPlayerRegion, wall system` | X | ✓ |
| `events.hpp::Events::OnKorokGet/OnShrineComplete/OnTowerOpen` | X | ✓ |
| `controller.hpp::Controller::IsPressed/GetLeftStick/GetRightStick` | ✓ | ✓ |
| `controller.hpp::Controller::Send/Hold/Release` | X | ✓ |
| `controller.hpp::Controller::SetInputCapture/HoldInputCapture` | ✓ (untested) | ✓ |
| `camera.hpp::Camera::Get/SetPosition/LookAt/Up` | ✓ | ✓ |
| `flyt.hpp::FLYT::Pane/PicturePane/Layout` | X | ✓ |
| `nvn.hpp::NVN::*` | ✓ | X |
| `gx2.hpp::GX2::*` | X | ✓ |
| `gx2.hpp::GX2::CreateTextureFromSurface, Begin/BatchQuad/EndBatch` | X | ✓ |
| `gui/gui.hpp::GUI::Init/OnFrame, Canvas::*` | X (stub) | ✓ (compiles, untested in-game) |
| `graphics/bffnt.hpp, bflim.hpp, platform/yaz0.hpp, sarc.hpp` | ✓ (parsers are platform-agnostic) | ✓ |
| `fs.hpp::FS::File::Open/ReadAt/Close` | X | ✓ |
| `fs.hpp::FS::ReadFile/WriteFile` | ✓ | ✓ |
| `log.hpp::OSLog` | X | ✓ (Cemu only) |
