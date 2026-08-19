# Actor transforms, enumeration and physics (BotW Wii U v208)

Research notes from the session that built `Actor::SetPosition`, `ForEachDynamic` and
`Get/SetLinearVelocity`. All addresses are **Wii U v208** unless stated otherwise. Offsets
were read out of Ghidra and, where marked *measured*, confirmed on a running game in Cemu 2.6.

The headline result is the last section: **you cannot move a physics-driven actor by writing
any field on the actor.** Everything on the actor is an output of a per-frame physics sync.

---

## 1. Actor / BaseProc layout

| Offset | Type | What |
|---|---|---|
| `+0x04` | `char*` | `sead::SafeString` name pointer (vtable at `+0x08`) |
| `+0x10` | `char[]` | inline name buffer |
| `+0x50` | `u32` | proc id |
| `+0x54` | `u8` | state: 0 init, 1 calc, 2 sleep, 3 deleted |
| `+0x64` | `u32` | state flag bits: 0 delete request, 1 wake, 2 sleep |
| `+0x68` / `+0x6C` | `BaseProc*` | child / sibling chains, walked by `0x0378B60C` |
| `+0xE0` | ptr | back-pointer to the owning `BaseProcUnit` |
| `+0xE8` | vtable | the actor vtable (see below) |
| `+0x1F8` | `sead::Matrix34f` | **ACTOR_MATRIX**, row-major 3x4, 48 bytes, spans `0x1F8..0x227` |
| `+0x22C` | `sead::Matrix34f` | published transform; what the rest of the engine reads |
| `+0x25C` | `sead::Vector3f` | linear velocity |
| `+0x274` | `sead::Vector3f` | optional 3rd argument of `setMtx` |
| `+0x28C` | `sead::Vector3f` | position copy, +1.500 above feet *(measured)* |
| `+0x2B0` | `sead::Vector3f` | position-ish, ~+1.39, jitters frame to frame — animated bone *(measured)* |
| `+0x2BC` | `sead::Vector3f` | position copy, +0.910 above feet *(measured)* |
| `+0x2D4` | `sead::Vector3f` | position copy at the feet, dy 0.000 *(measured)* |
| `+0x3B8` | ptr | attach frame; non-null means the transform is parented |
| `+0x430` | `u8` | dirty flag set by `0x03798C20` |
| `+0x4D8` | `u32` | attack counter (player) |
| `+0x4FC` | ptr | map object link |

### Position is a matrix column, not a vector

The reason position offsets are `0x10` apart rather than 4: they are the translation column
of the `Matrix34f` at `+0x1F8`.

```
m[0][0..3]  0x1F8 0x1FC 0x200 0x204   <- x
m[1][0..3]  0x208 0x20C 0x210 0x214   <- y
m[2][0..3]  0x218 0x21C 0x220 0x224   <- z
```

Confirmed twice: `0x0383B398` copies exactly 12 floats out of `playerActor+0x1F8` via the
matrix-assign helper `0x03C6FE5C`, which fixes both base and size; and the game reads gameplay
position from `+0x204/+0x214/+0x224` in `0x022B1278`, `0x022AE534` and `0x025BBA8C`.

**Rotation** is the 3x3 basis of that same matrix. There is no Euler triple on the actor. The
offsets `0x1E4 / 0x1F4 / 0x234` that this module used to call pitch/yaw/roll were wrong —
`0x1E4` and `0x1F4` sit *before* the matrix, `0x234` sits *after* it.

### Actor vtable (`+0xE8`)

| Slot | What |
|---|---|
| `0x0C` | no-arg call returning a pointer, null when not applicable. Callers use it to reject a proc: `0x024ADAC8`, `0x0313DC6C`, `0x024AA0D0`, `0x024B0CBC`. Looks like a downcast; **semantics not confirmed** |
| `0x5C` | delete-reason dispatch (`deleteLater`) |
| `0xCC` | per-job-type handler (`0x0378B60C`) |
| `0xF4` | `getMaxLife` -> int |
| `0x2BC` | `getLifePtr` -> int* (`[0]` current, `[1]` max) |
| `0x30C` | physics / motion object |

### Physics object (vtable slot `0x30C`)

| Offset | What |
|---|---|
| `+0xAC`, `+0xC4` | `Vector3f` pair, differenced by `0x0383B398` |
| `+0x158` | float written by `0x024B0CBC` |
| `+0x1F0` | `Vector3f` capsule centre, +0.800 above the feet *(measured)* |

---

## 2. Setting a transform: `setMtx` @ `0x03798AE8`

```c
void setMtx(Actor* actor, const Matrix34f* mtx, const Vector3f* velocity /*nullable*/)
```

```
0x03798AE8:
  0x03C6FE5C(mtx, actor + 0x1F8)    // copy into ACTOR_MATRIX
  0x037986E4(actor, mtx)            // publish to actor+0x22C, routed through the attach
                                    // frame at actor+0x3B8 via 0x034D3D40 when parented
  if (velocity) actor+0x274/0x278/0x27C = velocity
```

Verified from disassembly: `r3`=actor, `r4`=matrix, `r5`=velocity. Note Ghidra's C output for
`0x037986E4` looks like it calls the 4-argument `0x034D3D40` with only 3 args — it doesn't,
`037986F4: or r6,r4,r4` sets the 4th explicitly. Neither path takes a lock.

Around 30 gameplay call sites use `0x03798AE8`, so it is the game's normal way to place an
actor. Writing `+0x204/+0x214/+0x224` directly leaves `+0x22C` stale and is not equivalent.

---

## 3. Enumerating every live actor

The manager singleton is at **`0x1047C244`**.

| Offset | What |
|---|---|
| `+0x64` | `s32` job-type count |
| `+0x68` | pointer to the job-type bucket array |
| `+0x70..0x7F` | `sead::OffsetList` state-processing queue: prev, next, `s32` count, `s32` node offset |
| `+0x80` | `sead::CriticalSection` (its `OSMutex` is at `+0x10`) |
| `+0x11C/0x120/0x124` | the three actor job threads |

```
bucket  0xC0 bytes = 8 priority slots x 0x18
slot    0x18 bytes = 2 lists x 0xC
list    +0x00 prev, +0x04 next/first, +0x08 s32 count
node    +0x04 next, +0x08 BaseProc*, +0x10 priority, +0x12 sublist index << 1
```

Read off `BaseProcMgr::processJobs` (`0x0378EC20`) and its iterators: `0x03791FCC` strides the
bucket by `0x18` to `+0xC0`, `0x03791F8C` strides that by `0xC` reading `{next,count}` at
`+0x04/+0x08`, and `0x0379201C` advances via `node+0x04`, terminating when `next` equals the
list struct itself. `node+0x08` is the proc because the job loop dispatches on exactly that:
`0x0378B60C(*(node+8), jobType)`.

Practical notes:

- **Take the lock at `mgr+0x80`** while walking. Three job threads mutate these lists.
  `0x030BB668` is `r3 += 0x10; b OSLockMutex`, `0x030BB69C` is the unlock. Cafe OS mutexes are
  recursive. Without it the walk followed freed nodes and the counts varied run to run
  (285 vs 252 on concurrent traversals; a stable 519+ once locked).
- **De-duplicate.** A proc registered for several job types appears in several buckets.
- **Do not filter by name.** These are live procs by construction; a name filter threw real
  actors away.
- **Validate pointers.** One entry in 523 came back as `0x62CAF01E` — misaligned, outside the
  actor heap, unnamed. A plain range check passed it and a 48-byte matrix write followed.
  Require 4-byte alignment plus a plausible vtable at `+0xE8`.

### Things that are *not* the enumeration

Previously believed and wrong:

- A red-black tree at `BaseProcMgr+0x6C`. There is no tree there; `+0x6C` sits immediately
  before the `OffsetList` at `+0x70`.
- The 256-entry `BaseProcUnit` pool at `0x105597C0`. The pool is real — the static init at
  `0x0378D23C` sets up base `0x105597C0`, count `0x100`, stride `0x25C` — but its units carry a
  `CriticalSection` at `+0x0C` and are not the manager's list nodes.

Both produced nothing, which is why only the player ever came out of `ForEachDynamic`.

---

## 4. Map placements

`PlacementMgr` singleton `0x1047C318` -> `+0x11C` MapPlacement -> `+0x2D0` array.

```
entry stride 0x124, capacity 6000  (array spans +0x2D0 .. +0x6D050)
  +0x00  id
  +0x04  flags (bits 0x20 0x40 0x80 0x1000 0x10000 0x4000 0x80000 0x2000000 all tested)
  +0x14/0x18/0x1C  position
  +0x30/0x34/0x38  rotation
  +0xA4, +0xAC     floats
  +0xD8  name pointer
  +0xE4  inline name
```

**6000 is the real fixed capacity**, not a guess. Every lookup inlines the same
`sead::SafeArray` clamp against that literal: `0x0379E3D4` (five sites), `0x0379F2A0`,
`0x0313B84C`, `0x0313A878`, `0x0313A8E8` — the last writing it as `(uint*)base + idx * 0x49`,
which independently confirms the `0x124` stride.

**There is no live count.** The game never iterates this array; it random-accesses by the
`u16` index each map object carries at `+0x04`. So slots the current region didn't populate
still hold whatever the previous region left, and a name-validity check is the only thing
separating live records from ghosts.

Placements are the load-time spawn records, not the spawned actors. Writing their translate
moves nothing; `SetPosition` refuses `Kind::Placement`.

---

## 5. The physics sync — why nothing on the actor sticks

Found with a Cemu write watchpoint on `actor+0x2D4`, not by reading code: Ghidra never
analysed the region and there is no function object there.

**`0x038079F4` .. `0x038087B8`** is the physics -> actor transform sync. It walks physics
entities (stride `0x78`, counter at `r1+0x2C`), builds each transform with paired-single math,
and dispatches it into the actor via `0x03986044`:

```c
FUN_03986044(mgr, key, matrix):
    entity = mgr->[0x24][key[0]]
    (*(*entity + 0x74) + 0x6C)(entity, matrix, key[1])   // virtual, per entity type
```

This runs every frame and rewrites the whole actor-side transform state.

### Measured: every actor field is an output

Written, then read back over the following eight ticks. In each case the write lands and reads
back correctly within the same call, survives the rest of the frame, and is restored on the
next tick — to the **bit-identical** prior value.

| Field written | Result |
|---|---|
| `+0x1F8` matrix, via `setMtx` | reverted after 1 frame |
| `+0x2D4` feet copy | reverted |
| `+0x28C` (+1.5) and `+0x2BC` (+0.91) copies | reverted |
| `phys+0x1F0` capsule centre | reverted |
| `+0x25C` linear velocity | zeroed after 1 frame |

All five written together in one press: still reverted. Velocity does **not** behave as a
physics input here despite being the field the engine reads for speed.

Conclusion: for any actor the physics drives, the actor object is a complete mirror. Moving it
requires reaching the physics entity the sync loop reads from, which is behind
`(*(*entity + 0x74) + 0x6C)` and has not been located.

**Untested:** whether `setMtx` visibly moves an actor that physics does *not* drive. The
523-actor sweep changed fields and read them back, but nothing was confirmed on screen, and
props have physics too.

### Suggested next step

Set a write watchpoint on `phys+0x1F0` and then **fast-travel**. The game teleports Link
constantly (shrine exits, travel gates) and that path must write the master correctly.
Catching the writer during a legitimate teleport yields the real API rather than another
field to poke.

---

## 6. `Actor::OnUpdate` does not give you the player

`SpawnFlushHook` is installed at **`0x024ADAC8`**, which this module described as "a
Player-specific per-frame update method". It is not. Logging its `param_1` showed it arriving
as `Weapon_Sword_044` on all 11 presses, with per-frame traces matching several different
actors, on more than one job thread.

It is a generic actor update: it fires many times per frame, for different actors. Anything
that wants Link must fetch him through `Player::GetRaw()` (tracker `0x10463F38` -> `+0x34`
linkMgr, `+0x38` procId, `+0x3C` flag -> `getProc` `0x0378D8DC`).

A plain `static bool` edge check in such a callback fires on several threads at once; use an
atomic exchange.

---

## 7. Function reference

| Address | What |
|---|---|
| `0x03798AE8` | `Actor::setMtx(actor, Matrix34f*, Vector3f* vel)` |
| `0x037985E0` | get actor matrix (reads `+0x22C`, or through the attach frame) |
| `0x037986E4` | publish matrix to `+0x22C` |
| `0x03798C20` | place actor: `setMtx` + life + dirty flag `+0x430` |
| `0x03C6FE5C` | `Matrix34f` assign — copies 12 floats `(src, dst)` |
| `0x03C6FCF0` | `Vector3f` length — `sqrt(v0^2+v1^2+v2^2)` |
| `0x034D3D40` | combine transform through attach frame `(out, mgr, frame, mtx)` |
| `0x0378A374` | `BaseProc::deleteLater(proc, reason)` |
| `0x0378A1B8` | set state bit and queue the proc |
| `0x037905FC` | push proc onto the state-processing list at `mgr+0x70` |
| `0x0378D8DC` | `BaseProcHandle::getProc(mgr, id, flag)` |
| `0x0378EC20` | `BaseProcMgr::processJobs(mgr, jobType, prioMask, alsoPending)` |
| `0x03791FCC` `0x03791F8C` `0x0379201C` `0x037920D8` | job list iterators |
| `0x0378B60C` | `BaseProc::processJob(proc, jobType)` |
| `0x0378F210` | is the current thread one of the manager's job threads |
| `0x030BB668` / `0x030BB69C` | `sead::CriticalSection` lock / unlock |
| `0x0378CA84` | `BaseProcUnitPool::alloc` |
| `0x038079F4`–`0x038087B8` | physics -> actor transform sync loop (no Ghidra function) |
| `0x03986044` | dispatch a transform into a physics entity |
| `0x024ADAC8` | generic actor update (**not** player-specific) |
| `0x037B5E8C` | `spawnActor` |
| `0x03948CB8` / `0x03948ED8` | `requestCreateBaseProc` / `createBaseProc` |

## 8. Globals

| Address | What |
|---|---|
| `0x1047C244` | `BaseProcMgr*` |
| `0x1047C318` | `PlacementMgr*` |
| `0x1047C2B8` | actor create manager |
| `0x10463F6C` | heap provider (`+0x10` is the spawn heap) |
| `0x10463F38` | player tracker |
| `0x105597C0` | `BaseProcUnit` pool, 256 x `0x25C` |
| `0x1047C3D8` | attach-frame manager (`+0xF0` used by `0x037986E4`) |
| `0x10263910` | shared `SafeString` resolver vtable |

---

## 9. Gotchas

- **`WIIXL_LOG` has a limited formatter.** `%x` is not implemented and prints literally. Floats
  go through a fixed-point printer that emits `184467440737095516.15` (`ULLONG_MAX/100`) for
  NaN and Inf — useful as a NaN detector, confusing otherwise.
- **Non-finite transforms exist.** `LinkTagOr` and `LinkTagAnd` carry NaN in their matrix.
  Reading, adding to it and handing it back to `setMtx` puts NaN into `+0x1F8`, `+0x22C` and
  the attach path. `GetMatrix` rejects non-finite matrices for this reason, which doubles as a
  cheap "is this a real spatial actor" test.
- **Sweeping every actor every frame is not a good test.** At 60fps a per-frame `+0.5` launches
  all ~520 world actors upward at 30 units/second, including the camera and the system
  singletons, and any resulting crash is unattributable. Make world-wide operations
  edge-triggered.
