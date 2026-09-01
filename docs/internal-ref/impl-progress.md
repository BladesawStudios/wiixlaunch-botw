# Systems:

## Input:
- ~~Get/SetInputs~~ (controller.hpp)

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
- Get/SetResolution
- Get/SetFramerate

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

## Layouts (flyt & flan):
- ~~flyt: Pane/PicturePane/Layout - translate/rotate/scale/size/alpha/visible, FindPane, corner colors, load hook+redirect~~ (flyt.hpp)
- flan: not implemented

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
| `camera.hpp::Camera::Get/SetPosition/LookAt/Up` | ✓ | ✓ |
| `flyt.hpp::FLYT::Pane/PicturePane/Layout` | X | ✓ |
| `nvn.hpp::NVN::*` | ✓ | X |
| `gx2.hpp::GX2::*` | X | ✓ |
| `fs.hpp::FS::ReadFile/WriteFile` | ✓ | ✓ |
| `log.hpp::OSLog` | X | ✓ (Cemu only) |
