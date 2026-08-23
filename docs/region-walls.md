# Region borders and invisible walls (BotW, Wii U v208)

Research notes behind `botw/region.hpp` and `Actor::SpawnScaled`. Addresses are **Wii U
v208** and come from the same Ghidra database as the rest of this repo. Everything about
actor parameters, placements and the region raster was read out of romfs; the romfs dump
used was the **Switch** one (SARC byte-order mark `FF FE`, `System/Version.txt` 1.3.1), which
is byte-order-swapped from the Wii U's but identical in content. That matters in exactly one
place, noted below.

Two independent problems, and they are worth keeping apart:

1. **Where is the border?** — answered by a file the game already ships.
2. **What physically stops the player?** — answered by an actor the game already has.

---

## 1. Where a region is: `Ecosystem/MapTower.beco`

BotW's ecosystem layer keeps four rasters over MainField, loaded by `Ecosystem::init`
(**`0x032aeb68`**) out of `Pack/Bootup.pack`:

| File | Handle | `EcoMapInfo` |
|---|---|---|
| `Ecosystem/FieldMapArea.beco` | `this+0x14` | `this+0x114` |
| `Ecosystem/AreaData.byml` | `this+0x44` | — |
| **`Ecosystem/MapTower.beco`** | **`this+0x74`** | **`this+0x120`** |
| `Ecosystem/StatusEffectList.byml` | `this+0xa4` | — |
| `Ecosystem/LoadBalancer.beco` | `this+0xd4` | `this+0x12c` |

`0x032aeb44` is `setEcoMapInfo` — three stores, `{header, header+0x10, header+0x10 +
4*header->num_rows}`, which is what fixes `EcoMapInfo` at 3 pointers / 0xc bytes and
therefore the `0x114 / 0x120 / 0x12c` stride.

The **`Ecosystem` singleton pointer is `0x1046d6ac`**, from `0x034134d0`:

```
034134f4: lis  r12,0x1047
034134f8: lwz  r3,-0x2954(r12)      ; r3 = *(void**)0x1046d6ac
034134fc: or   r4,r31,r31           ; r4 = heap
03413500: bl   0x032aeb68           ; Ecosystem::init
```

### File format

```
struct EcoMapHeader { u32 magic /*0x00112233*/; s32 num_rows; s32 divisor; u32 reserved; };
struct Segment      { s16 value; s16 length; };          // run-length, one row at a time
```

then `s32 rowOffsets[num_rows]`, then the row data. Row offsets are **halved** and relative
to the start of the row section, so a row starts at `rows + 2 * rowOffsets[row]`.

For `MapTower.beco`: `num_rows` 4000, `divisor` 2. The raster covers x in `[-5000, 4999]` at
one cell per world unit, z in `[-4000, 4000]` at two units per row. Decoded, every one of the
40 million cells carries a value in `0..14` — **there is no hole anywhere on the field**.

> **Endianness.** The Switch dump's `.beco` is little-endian and has to be read as such by
> any tool. The Wii U's is big-endian, which is native there, so `region.hpp` reads the
> in-memory structure with plain loads and no swapping. Do not carry a byte-swap over from a
> tool that parsed the Switch file.

### The lookup

`Ecosystem::getMapArea(info, x, z)` clamps, rounds half-away-from-zero, divides z by
`divisor` (and x too, but only when `divisor` is 10 — no ecomap here uses that), clamps the
row to `num_rows - 2`, then walks that row's segments accumulating `length` until the running
total passes x. `region.hpp` reimplements it rather than calling it: it is thirty lines of
pure arithmetic with no side effects, and it appears inlined into its callers rather than as
one function worth pinning an address to.

### Measured: value + 1 == `MapTower_NN`

Every `FldObj_MapTower_A_01` / `_First` / `MapTowerLong_A_01` placement in `TitleBG.pack`'s
static mubins, looked up against the decoded raster. Fifteen towers, fifteen regions, each
tower inside the region carrying its own number and no two towers sharing one:

| Tower | Placement (x, y, z) | Section | Region |
|---|---|---|---|
| 01 | (-2173, 455, -2034) | C-2 | 1 |
| 02 | (-3614, 371, -990) | B-4 | 2 |
| 03 | (-3666, 397, 1829) | B-6 | 3 |
| 04 | (-2307, 456, 2437) | C-7 | 4 |
| 05 | (884, 276, -1606) | F-3 | 5 |
| 06 | (-789, 124, 442) | E-5 | 6 |
| 07 | (-560, 172, 1695) | E-6 | 7 |
| 08 | (1017, 110, 1714) | G-6 | 8 |
| 09 | (-32, 206, 2962) | E-7 | 9 |
| 10 | (2174, 435, -1557) | H-3 | 10 |
| 11 | (3308, 520, -1500) | I-3 | 11 |
| 12 | (2258, 237, -109) | H-4 | 12 |
| 13 | (2736, 262, 2134) | H-7 | 13 |
| 14 | (1331, 196, 3274) | G-8 | 14 |
| 15 | (-1755, 254, -774) | D-4 | 15 |

Tower 03 is the only one not called `FldObj_MapTower_A_01` — it is `FldObj_MapTowerLong_A_01`,
the tall Wasteland variant, and tower 07 additionally has a `_First` copy at the same spot.

### Do not use `MapTower_NN_OpenCenterPos` for this

The obvious alternative — the fifteen vec3 flags `botw/map.hpp` already reads — does **not**
partition the world. Checked the same way, three of the fifteen centres land inside a
different region than their own number (02 → 15, 11 → 10, 13 → 8), and `MapTower_02`'s centre
`(-1870, 0, -1150)` is not even inside region 2's bounding box. They are the centres of the
revealed circle on the *map screen*: UI configuration, not ground truth.

Their defaults, for reference (from `gamedata.ssarc`, all with `OpenScaleLevel` init 1 —
except tower 07 at 2 — and max 3):

```
01 (-2050,   0, -2560)   06 ( -110,   0,   150)   11 ( 2580,   0, -2400)
02 (-1870,   0, -1150)   07 (-1000,   0,  1900)   12 ( 2490,   0,  -167)
03 (-2770,   0,  1224)   08 ( 1017,   0,  1715)   13 ( 1810,   0,  1730)
04 (-2770,   0,  2640)   09 (  -30,   0,  2963)   14 ( 2190,   0,  2900)
05 (    6,   0, -2380)   10 ( 2100,   0, -2380)   15 (-2080,   0,   -90)
```

### Region footprints, measured off the raster

Bounding box in world units, and cell count (1 cell = 1 x 2 world units):

| Region | x | z | cells | perimeter |
|---|---|---|---|---|
| 1 | -5000 … -415 | -4000 … -1512 | 4.17 M | ~12.9 km |
| 2 | -5000 … -2788 | -2480 … -2 | 1.84 M | ~11.9 km |
| 3 | -5000 … -1700 | -62 … 2394 | 2.37 M | ~17.2 km |
| 4 | -5000 … -387 | 1244 … 3996 | 3.79 M | ~15.4 km |
| 5 | -2104 … 1750 | -4000 … -662 | 3.32 M | ~18.8 km |
| 6 | -1872 … 1317 | -1342 … 1990 | 2.95 M | ~16.0 km |
| 7 | -1572 … -401 | 1452 … 2396 | 0.43 M | ~5.1 km |
| 8 | -770 … 2582 | 670 … 2628 | 1.63 M | ~16.6 km |
| 9 | -1391 … 1260 | 1838 … 3996 | 2.10 M | ~11.6 km |
| 10 | 963 … 4474 | -4000 … -460 | 3.21 M | ~17.3 km |
| 11 | 1290 … 4999 | -4000 … -120 | 2.79 M | ~15.0 km |
| 12 | 726 … 4999 | -1952 … 1620 | 3.23 M | ~20.5 km |
| 13 | 1794 … 4999 | 176 … 3262 | 3.03 M | ~16.9 km |
| 14 | 957 … 4999 | 1942 … 3996 | 2.44 M | ~11.5 km |
| 15 | -4278 … -793 | -1732 … 1546 | 2.70 M | ~16.7 km |

Region 7 is the Great Plateau, and its size is the giveaway — a tenth of the map's smallest
neighbour. Total interior border across all fifteen: about **112 km** of cell edges.

**This is why walls are built locally.** A single region's perimeter at a 20 m panel spacing
is 600-1000 actors, and the whole border is over 5000. Nothing spawns a region's worth of
wall; `region.hpp` builds only what is inside `SetBuildRadius` of the player.

---

## 2. What stops the player: the `AirWall` actor family

Five actors, from `Actor/Pack/`:

| Actor | `ActorNameJpn` | `PhysicsUser` | `ProfileUser` | Root AI |
|---|---|---|---|---|
| `AirWall` | 空気壁（世界の果て用） "world's end" | **Dummy** | AirWall | `AirWallAction` |
| `AirWallForE3` | 空気壁（プレイヤー進入禁止） "player entry forbidden" | **Dummy** | AirWall | `AreaBase` |
| `AirWallCurseGanon` | 空気壁(カースガノン用) | `AirWallCurseGanon` | MapConstActive | `AirWallCurseGanon` |
| `AirWallHorse` | horse blocker | `AirWallHorse` | — | `AirWallHorse` |
| `CastleBarrier` | the castle malice barrier | **Dummy** | — | `AirWallAction` |

### Why the world-boundary one is the wrong pick

`AirWall` is the actor that stops you at the edge of the world, and it is the first thing you
reach for. It cannot be spawned into a wall: `PhysicsUser: Dummy` means it owns no collision
of its own. Its shape comes from its **map placement** — `!Parameters: {Shape: Box}` plus a
`Scale` vec3 — through the Area system, and a runtime-spawned actor has no placement record
for that to read. Its 25 placements in MainField are huge single slabs, e.g.

```
A-5_Dynamic  AirWall  Shape Box  Scale [322.8, 922.4, 159.3]  Translate [-4955.8, 903.7, 821.9]
```

`AirWallForE3` is the same story with two extra placement params (`AirWallCollision` int,
`EnableCharacterOn` bool) and an `AreaBase` root — it is the E3 demo's "you may not go
there" boundary, 43 of them, and equally unspawnable. `CastleBarrier` adds two behaviours,
`AirWallMaterialSpecify` and `CastleBarrierCollisionSpecify`, on top of `AirWallAction`.

### Why the Blight Ganon arena walls are the right pick

`AirWallCurseGanon` carries `Actor/Physics/AirWallCurseGanon.bphysics`:

```
RigidBody_0   motion_type Fixed       layer  EntityAirWall
              mass 1.0                volume 8.0
              bounding_extents [2,2,2]
              contact_mask 1630159    groundhit HitAll
              navmesh STATIC_AIR_WALL_FOR_HORSE
ShapeParam_0  shape_type box          material AirWall / AirWall
              translate_0 [0,0,0]  translate_1 [2,2,2]  convex_radius 0.05
```

The collision is a property of the **actor**, built from the actor's own physics resource
when it is constructed. That is the whole difference. It is also the only one of the five on
the dedicated `EntityAirWall` contact layer with the `AirWall` surface material — the game's
own "this is an invisible wall" pairing, and a distinct thing from `AirWallHorse`, which is
`EntityGround` / `Undefined` with an explicit `ground_hit_type_mask` and exists to stop
horses.

Four are placed, all in `E-4_Dynamic.smubin`, at `Scale` `[4,6,4]`, `[8,5,6]`, `[4,7,4]` and
one more — **non-uniform, which is the game itself demonstrating that a per-axis wall panel
is a supported shape.**

The decomp (`Game/AI/Action/actionAirWallCurseGanon.cpp`) has every override chaining
straight to `AirWallHorse` with no body of its own, `calc_` included. So the framework's
standing caveat — a spawned actor never reaches Calc — costs nothing here: a `Fixed` rigid
body is built at construction and does nothing per frame afterwards. And it is an actor the
game itself places in the world, which is `Actor::Spawn`'s stated condition for spawn and
delete to behave.

---

## 3. Sizing a spawned actor: the `"@S"` param

`Actor::Spawn` already writes `"@P"` into the `InstParamPack`. **`"@S"` is the scale**, and it
is written exactly the same way — 12 bytes, type tag 4.

| What | Address |
|---|---|
| `"@P"` key string | `0x10072ed4` (already recorded) |
| **`"@S"` key string** | **`0x1031b4ec`** |
| `ActorCreator::addScale(float, pack)` | `0x037b55a4` |
| `InstParamPack::Buffer::add` | `0x031f9870` (already recorded) |

`0x037b55a4` is three stores and a call — it splats one float into a stack vec3 and writes it
under `&DAT_1031b4ec` with size `0xc` and tag `4`:

```c
void addScale(float scale, InstParamPack* pack) {
    float v[3] = {scale, scale, scale};
    FUN_031f9870(pack + 4, v, &key_at_1031b4ec, 0xc, 4);
}
```

That is the **uniform** overload. The decomp's `ActorCreator::addScale` has a second one
taking a `sead::Vector3f` and writing the same key, and `InstParamPack::Buffer::addScale`
is declared as `add(const sead::Vector3f&, "@S")` — so a non-uniform scale is representable
and is what `Actor::SpawnScaled` writes.

The key sits in a run of them, useful if more are ever wanted:

```
0x1031b4ec  "@S"    scale       0x1031b514  "@DD"   delete distance squared
0x1031b4ef  "@W"    wait        0x1031b518  "@RL"   resource lane
                                0x1031b51c  "@TV"   translation velocity
```

`0x037b55fc` is a separate three-instruction function (`stw r4,0(r3); or r3,r4,r4;
b 0x0378bdc4`) — the anchor setter this repo already calls. `0x037b5608` is a larger variant
that sets the anchor *and* writes `"@W"`.

### The honest gap

The path from `"@S"` to a box rigid body's extents was **not traced**. What could be read
says scaling on the physics side is uniform: `phys::RigidBody::setScale`,
`phys::RigidBodyFromShape::updateScale_` and `phys::BoxShape::setScale` all take a single
`float` and multiply all three extents by it. `phys::BoxShape::setExtents` *does* take a
`sead::Vector3f`, so a per-axis box is representable, and the four real `AirWallCurseGanon`
placements prove the game builds them — but which of those two routes a spawn param takes was
not followed through.

**So if a non-uniform panel comes out cubic, that is where it went.** Fall back to equal scale
components and a smaller cell size — more, smaller panels.

---

## 4. What `region.hpp` does with all this

- `GetRegionAt(x, z)` — the ecomap lookup, +1, validated to 1-15.
- `Get`/`SetRegionUnlock`, `Get`/`SetUnlockMask` — the lock set, held by the module, **not**
  in the save. `SyncFromTowers()` copies the fifteen `MapTower_NN` flags in for the "towers
  gate the world" setup.
- `Tick()` — called by the mod, once a frame, deliberately not self-installing (`player.hpp`
  has one `OnTick` slot and taking it would break whoever wanted it).

Wall building, per tick:

- costs one comparison while nothing is locked;
- rebuilds the wanted panel set only when the player crosses a cell, the lock mask changes,
  or they move far enough vertically that the standing panels no longer cover their height;
- names each panel by the cell **face** it sits on, lower cell first, so the two cells
  sharing a border produce one panel and not two;
- spawns **one** panel per tick, because the pending-spawn slot holds a single request. A
  thirty-panel rebuild therefore takes thirty frames.

Removal is the awkward part and worth understanding before changing anything: `Actor::Spawn`
cannot hand the created actor back, so panels are **re-found** — one `ForEachDynamic` sweep
matching `AirWallCurseGanon` by name and the recorded position to within half a cell, then
`Delete`. One sweep for the whole batch, since that traversal holds the proc manager's lock.
A record whose actor the sweep does not find is dropped anyway rather than kept forever,
because a record with nothing behind it would hold its slot and stop that face rebuilding.

`SetPushbackEnabled` is the safety net for everything the staircase misses — a gap, the top of
a panel, a paraglider, a shrine exit across a border. It puts the player back at the last
position they held in an allowed region, through `Player::SetPosition`, which that header is
explicit about not having proven. Off by default for that reason.

---

## 5. Untested

Nothing in this document has been run. Every claim above is from the binary, the decomp or
romfs; the things that need a running game are:

1. **Does a spawned `AirWallCurseGanon` collide at all?** The argument that it should is
   given above and is a good one, but "Fixed rigid body survives a half-attached spawn" is
   reasoning, not a measurement.
2. **Does `"@S"` produce a non-uniform box?** See the gap in §3.
3. **Does `Delete` remove the rigid body, or only the proc?** `Actor::Spawn` says deletion is
   clean for actors the game places, and this is one. Watch for a wall that stays solid after
   its region is unlocked.
4. **Does the actor's pack load away from E-4?** `AirWallCurseGanon` is only placed in the
   castle sanctum, so its resources will not be resident when the player is anywhere else and
   the spawn has to pull them in. If that turns out to be a problem, `AirWallHorse` has 111
   placements spread across the field and the same box shape.
5. **Panel height.** Panels are centred on the player's height at build time and are 80 m
   tall by default. Whether that is enough where a border runs up a cliff is a thing to look
   at, not a thing to calculate.

---

## 6. The lead not taken: the Great Plateau containment

The game already ships a region lock. `data/symbols-wiiu-v208.csv` records it: clearing
`IsGet_PlayerStole2` arms the Great Plateau containment, boundary fog appears and disappears
live with the flag, and a continuous spatial check teleports the player to a fixed return
point at approximately `(-1021.8, 253.3, 1792.9)`. That is a nicer effect than a wall — it
has visuals and it is the game's own — and if its boundary and return point turned out to be
per-region data, retargeting it would beat spawning anything.

**Checked against the raster, and it does not line up.** The return point and the `StartPos`
`GameStart` marker are both inside region 7; the measured firing point `(-942.1, 127.3,
1398.9)` is outside it, in region 6. All consistent so far — but region 7 spans z 1472-2364
at that x, so the firing point is **69 m past** the region's edge. The containment shape is
region 7 plus roughly a 70 m skirt at that bearing, or something else entirely that merely
correlates with it. That agrees with the existing note that standing at the base of the
Plateau does not trigger the teleport.

One sample in one direction. Enough to rule the raster out as the exact boundary, not enough
to say what the boundary is. Finding the code behind that spatial check — and whether the
return point is a constant or a lookup — is the follow-up worth doing before anyone invests
further in spawned panels.
