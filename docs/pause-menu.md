# The pause menu (Wii U v208)

Everything here was verified in-game or read out of the binary. Addresses are
Wii U v208. The goal driving it is a **new top-level section** - a fourth stop
on the L/R run that currently goes Adventure Log <- Inventory -> System.

## Vocabulary

The game's own split, which is worth keeping straight:

- **Section** - a top-level L/R stop: Adventure Log, Inventory, System.
- **Tab** - a category inside the inventory (`PaCategoryBtnS_00`, seven of them).
- **Page** - twenty item slots inside a tab (`PaPage20_00`, three of them).

## Pane tree

Walked at runtime from the System section upwards:

```
RootPane
  N_Slide_00 / N_Slide_01 / N_Slide_02      one per section
    N_InOutSave_03 (and siblings)
      Pa_Save_00 / Pa_Quest_00 / ...        the section content
```

Captured via `FLYT::Layout::OnLoaded`, with the root pane reached by
`Layout::GetRootPane()` - **not** `FindPane("RootPane")`, which searches a
root's children and so can never match the root itself.

| Layout | Root pane | Notes |
|---|---|---|
| `PaPauseQuest_00` | `Pa_Quest_00` | 1480x720 |
| `PaPauseSave_00` | `Pa_Save_00` | 1046x608, the System section |
| `PaMap_00` | `Pa_Map_00` | 24000x20000 - overlaps everything, see below |
| `PaAppSystemWindow_00` | `Pa_AppSystemWindow_00` | 1280x716 |
| `PaPageChallenge5_00` | `Pa_Page_00` | 486x406 |
| `PaPage20_00` | `Pa_PagePorch_00/01/02` | 538x508, three of them |

`PauseMenu_00` itself never comes through `Layout_LoadLayoutArchive`; only its
children do. The whole menu is built in one burst at construction - all seven
tabs and all three pages - so the load hook is a one-shot signal, not a
per-open or per-tab one, and the pointers have to be cached.

## Sections hide by position, not by alpha

Inactive sections keep `alpha=255` and `visible=1` throughout. They are slid
off-screen: Adventure Log sat at x=-100 while the inventory's page was parked
at x=-409 and the challenge page at x=-1563.

So "is this section showing" is a question about position. Use the root's
**centre being inside the 1280x720 canvas** rather than a rectangle overlap -
`Pa_Map_00` is a 24000x20000 canvas that overlaps the screen no matter where
the menu is, and an overlap test reports it as always visible.

## The slide

Measured frame by frame off a real L/R press. One transition of `N_Slide_02`:

```
1700 1550 1400 1250 1099 | 949 816 702 603 519 446 383 330 284 244 ...
     -150 -150 -150 -151 | -150 -133 -114 -99 -84 -73 -63 -53 -46 -40
```

A constant-velocity phase clamped at 150/frame, then a decay whose successive
ratios are 0.864, 0.860, 0.857, 0.857, 0.861 - a flat 6/7. The law is exactly:

```c
step = clamp(x / 7.0f, -150.0f, +150.0f);
x -= step;
```

**Section pitch is 1700.** (An early sample read 1390; it started mid-slide, at
422, so it measured a partial transition. Don't trust a pitch measured from a
trace that didn't start at rest.)

The game does **snap-and-ease**: on a section change the section's base
position jumps a full 1700 one way while the slide pane jumps 1700 the other,
so nothing moves at that instant, then the slide decays to 0 and carries the
section to its new home. Slide values rest near 0 but not exactly - one was
observed resting at -42 - so capture the base rather than assuming zero.

Pressing R at System moves the strip a little way and springs back (7 -> -8 ->
-19 -> ... -> -42). That is the game's "nothing further this way" bounce, and
it means R there is otherwise a free input.

## Screen object and states

`FUN_02ff7fdc` is the static initialiser. It registers **11 states** at
`0x10543f90`, stride `0x30`: state id, name pointer, `&DAT_1024ad7c`, then four
`(vtable index, 0xc)` pairs.

| State | vtable indices |
|---|---|
| `ksys::ui::ScreenBaseEx::StateID_Init` | 0x45-0x48 |
| `StateID_Pouch` | 0x9a-0x9d |
| `StateID_SystemWindowOpen` | 0x9e-0xa1 |
| `StateID_RecipeOpen` | 0xa2-0xa5 |
| `StateID_WarningWindowOpen` | 0xa6-0xa9 |
| `StateID_CantUseHeroSoulWindowOpen` | 0xaa-0xad |
| `StateID_AddStockNum` | 0xae-0xb1 |
| `StateID_RotateMode` | 0xb2-0xb5 |
| `StateID_Save` | 0xb6-0xb9 |
| `StateID_Quest` | 0xba-0xbd |
| `StateID_ChangeDisplay` | 0xbe-0xc1 |

The same initialiser writes a screen descriptor around `0x10543dec`:

```
0x10543dec  "PauseMenu_00"   + sead SafeString vtable at 0x10543df0
0x10543df4  3                 count
0x10543df8  -> 0x105441a0     SafeString[3]: "N_Slide_01", "N_Slide_02", "N_Slide_00"
0x10543dfc  3                 count
0x10543e00  -> 0x1046b264
```

## Why a fourth section is not a two-value patch

That `3` at `0x10543df4` looks like the section count. It is not - it is only a
sead SafeArray bounds check, used as `if (i < count) p = base + i*8; else p = base`.

`FUN_02ff68d8` is what consumes it, and there the real bound is hardcoded:

```c
iVar14 = param_1 + 0x1e48;      // section records, stride 0x28
...
if (2 < (int)uVar8) break;      // three iterations, baked into the instruction stream
if (uVar8 < 3) { ... }          // and again
```

Worse, the destination array is **inline in the screen object**: three records
of 0x28 from `+0x1e48` end at `+0x1ec0`, and `+0x1ec0` is already a different
field, written later in that same function. There is no room for a fourth
record.

So the native route needs, at minimum:

1. The section index variable and the clamp that produces the bounce at System.
2. The record array relocated out of the screen object, with every access in
   `FUN_02ff68d8` and its callers repointed.
3. The hardcoded `3`s in `FUN_02ff68d8` patched.
4. An `N_Slide_03` pane added to `PauseMenu_00.bflyt` (a romfs edit), the name
   array extended to four entries and the count raised.
5. A twelfth state, or a branch off `StateID_ChangeDisplay`.
6. The header label, which the game draws from its own state.

That is a real project, not an afternoon. It is also the only route that gives
a section the game itself believes in.

## What the overlay route gets you, and where it stops

Driving `N_Slide_02` with the law above makes the System section genuinely
slide away on the game's own transform while custom content rides in on the
same curve. Driving `RootPane` instead takes the entire pause menu with it -
header, footer and all - which is what it takes to stop the result reading as a
box drawn on top of the menu.

Neither is a section the game knows about. Input has to be taken with
`Canvas::CaptureInput()` for the whole time the page is up, or the menu acts on
the same press - L both leaves the custom page and steps the game one section
left, landing two back.

Pane alpha at `+0x45` is what `.bflan` animations drive, so any alpha override
has to be re-applied every frame; a single write is painted over on the next.
And hiding a section root does not take the section's cursor with it - the
cursor is a sibling, not a child - so a page that owns the screen should be
drawn opaque rather than relying on the hide being complete.
