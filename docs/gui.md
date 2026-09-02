# GUI - custom in-game UI in the base game's style (GX2)

`include/wiixlaunch/botw/gui/` draws immediate-mode UI into BotW's frame on
Wii U / Cemu, using the game's **own** fonts and layout art so that a mod's
windows come out in the game's look rather than an approximation of it. This
file is the companion to the headers: how it is put together, what was
measured out of the game to get there, and what is still approximate.

NVN (Switch) is a stub: `GUI::SupportsGUI` is `false` there and every call
is a no-op. The renderer is split so that an NVN backend only has to provide
`gui_render.hpp`'s handful of functions.

## Files

| Header | What it is |
| --- | --- |
| `gui/gui.hpp` | The public API: `GUI::Init()`, `GUI::OnFrame()`, `GUI::Canvas` (primitives, game-styled composites, focus-navigated widgets). Includes everything below. |
| `gui/gui_types.hpp` | `Color`, `Rect`, `TextStyle`, `Styles::` presets, `Sprite` ids, `Metrics::` - the numbers read out of the layouts. Platform-agnostic. |
| `gui/gui_render.hpp` | GX2 renderer core: the sprite/font tables and the one primitive (`EmitQuad`: layout pixels -> NDC -> `GX2::BatchQuad`). |
| `gui/gui_text.hpp` | UTF-8 -> glyph quads with nw::font's layout rules (cell scaling, advances, wrapping, shadow pass). |
| `gui/gui_assets.hpp` | The streaming loader that pulls fonts and textures out of the game's archives at runtime. |
| `graphics/bffnt.hpp` | BFFNT font parser (FINF/TGLP/CWDH/CMAP, glyph lookup, cell UVs). |
| `graphics/bflim.hpp` | BFLIM footer parser -> GX2 surface description. |
| `platform/yaz0.hpp` | Streaming Yaz0 decoder with a 4 KB window. |
| `platform/sarc.hpp` | SARC header / node-table lookup by name hash. |
| `platform/fs.hpp` | `FS::File` - open + positioned reads (`FSReadFileWithPos`), added for the loader. |
| `tools/preview_ui_assets.py` | Offline de-tiler/decoder: `.bflim` to PNG, `.bffnt` header + sheets, `.bflyt` pane tree. Every number in this file came out of it. |
| `tools/preview_ui_compose.py` | The same drawing rules as the renderer, in Python, so a composite can be built and looked at as a PNG without launching the game. |
| `graphics/gx2.hpp` | Added: `SurfaceDesc` / `AllocTextureSurface` / `CreateTextureFromSurface` (upload pre-tiled surfaces as-is) and `BeginBatch` / `BatchQuad` / `EndBatch` (many quads per draw call, no per-quad `GX2DrawDone`). |

## Using it

```cpp
#include <wiixlaunch/botw/botw.hpp>
using namespace WiiXLaunch::BotW;

static bool  s_Open  = false;
static bool  s_God   = false;
static float s_Speed = 1.0f;

static void Frame(GUI::Canvas& c) {
    if (c.Pressed(Button::Minus)) s_Open = !s_Open;
    if (!s_Open) return;

    c.RoundedBox({420, 140, 440, 300}, GUI::Colors::MessageWindow, 16);
    c.TextBox({420, 150, 440, 40}, "Debug", GUI::Styles::Title());
    c.Toggle({460, 210, 360, 40}, "Invincible", s_God);
    c.Slider({460, 260, 360, 40}, "Speed", s_Speed, 0.5f, 3.0f, 0.1f);
    if (c.Button({460, 310, 360, 40}, "Close")) s_Open = false;

    c.MessageBox("This is the game's own dialogue box, font and colours.", "WiiXLaunch");
}

extern "C" void WiiXLaunch_Init() {
    // ... backend init ...
    Controller::Init();     // before GUI::Init so widgets see input
    GUI::Init();            // installs the GX2 frame hook, starts asset loading
    GUI::OnFrame(&Frame);
}
```

* Coordinates are the game's own layout space: **1280 x 720, origin top-left,
  y down**, whatever the real colour buffer size is (a Cemu resolution pack
  just scales).
* `Frame` runs once per rendered frame, on the render thread, inside the same
  `aglCopyToScanBuffer` hook `GX2::RegisterDrawCallback` uses - after the game
  has finished its frame, before it is presented. Keep it cheap and do not
  call game code from it.
* Widgets are navigated with the D-pad / left stick (with key repeat), `A`
  activates, `B` is only *reported* (`Canvas::Cancel()`) - what it closes is
  the mod's call. Focus order is issue order; each frame's widget list may
  differ (the focus index is clamped).
* The GUI does not swallow input - the game still sees the buttons. A mod that
  opens a menu will usually want to pause or block on its side; see
  `Controller` for injection, and note there is no "eat input" facility yet.
* `GUI::IsReady()` turns true once the loader is done (fonts and art take a
  couple of dozen frames to stream in on Cemu; see below). Text and sprites
  that have not arrived yet draw nothing - a frame callback may run before
  the first glyph is available.

### Styles and composites

`GUI::Styles::` are `TextStyle` presets copied from specific text panes in the
game's layouts; `GUI::Colors::` and `GUI::Metrics::` likewise. `Canvas` has:

| Call | Base-game source |
| --- | --- |
| `MessageWindow(rect)` / `MessageBox(text, name)` | `Message_00.bflyt` `W_Base_00`: `Nt_MsgWindowL_00^s` as a window frame (96-wide cap on a 192-tall texture, mirrored right), black, pane alpha 230, 914x192 at 0.67 scale centred 235 px below screen centre; `Nt_MsgDecoL_00/02` ornaments at +-284 / +-278; `T_Message_00` Normal (23.2, 31.2) at 0.9, cream `(255,252,198)`, shadow (+2,+2) black 100; `T_Name_00` NormalS (19.2, 25.5) white, hard shadow. |
| `RoundedBox(rect, color, 8)` | `PaOptionBtn_00` `W_Base_00`: an 8 px window frame from `CornerR3_00^s` (corners + stretched edges) with a filled centre, black alpha 200. |
| `RoundedOutline(rect, color, 8)` | `PaOptionBtn_00` `W_BaseLine_02`: the same frame construction from `CornerLineR2_00^s`, cream `(255,252,198)`; rests at alpha 8 in the layout and is animated up on select. |
| `CursorCorners(rect)` | `PaOptionBtn_00` `Window_00`: `Nt_CursorS_00^s` as a 48 px frame (corners + stretched edges) on a box 28 px larger than the row, cream at alpha 128, additive (the game adds an animated cloud texture through it). |
| `SelectFrame(rect)` | `BtnDialog_00` `W_SelectFrame_00/01`: `SelectFrame_04^t` 68 px corners with stretched edges, `SelectFrameGlow_00^s` under it in `(0,193,242)` alpha 102. |
| `Plate(rect)` / `PlateButton` | `BtnDialog_00` `W_Pict_00`: a nine-slice of `BtnBasic_08T^t` / `08B^t` over the `08TS`/`08BS` shadow with an opaque fill beneath, `T_BtnDialog` Normal 1:1 in `(40,40,40)`. The corner keeps the layout's proportion (96 px on a 240 px window, so about 0.55 of the button's height) rather than being made as large as fits, which turned small buttons into fat pills. **Approximate** - see below. |
| `CursorBrackets(rect)` | `Nt_Cursor_00^t` 64 px bracket corners (the inventory cursor). |
| `BoxedCursor(rect)` | `PaBoxedCursor_00`: `Nt_ArrowS_02^s` triangles at 0.21 scale, rotated 45 degrees off vertical so they point diagonally outward, 3 px out from each corner. |
| `ButtonIcon` / `KeyHint` | `Nt_KeyTexA/B/X/Y/L/ZL_00^d`, 48 px. |
| `Button` / `Toggle` / `Slider` / `Selector` | Option-row style: the three `PaOptionBtn_00` pieces above plus `T_Text_00` NormalS (19.2, 25.5) white. |

### Blending

Every draw call takes a `GX2::BlendState`; `GX2::Blend::` holds the presets,
and `GX2::Blend::FromLyt(op, src, dst)` converts the four bytes a `.bflyt`
material stores straight into GX2 state. **The two numbering schemes are not
the same** - lyt factor 2 is destination colour, GX2's factor 2 is source
colour - so a value copied out of a layout dump has to go through that
mapping:

| lyt factor | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| meaning | zero | one | dstColor | invDstColor | srcAlpha | invSrcAlpha | dstAlpha | invDstAlpha | srcColor | invSrcColor |
| GX2 value | 0 | 1 | 8 | 9 | 4 | 5 | 6 | 7 | 2 | 3 |

lyt blend ops map 1→ADD(0), 2→SUB(1), 3→REV_SUB(4), 4→MIN(2), 5→MAX(3); op 0
("disable") writes the source unblended.

What the game actually uses, counted over every material in
`Layout/Common.sblarc`:

| lyt (op, src, dst) | uses | preset | where |
| --- | ---: | --- | --- |
| *no blend block* | 3046 | `Blend::Alpha` | the default - straight alpha |
| (1, 4, 1) | 1557 | `Blend::Additive` | glows, the option cursor (`Window_00LT`) |
| (1, 4, 5) | 588 | `Blend::Alpha` | alpha, written out explicitly |
| (1, 2, 4) | 93 | `Blend::Overlay` | `src*dst + dst*srcAlpha`; the message-box ornaments |
| (1, 1, 0) | 40 | `Blend::Opaque` | source through, alpha ignored |
| (1, 1, 5) | 6 | `Blend::Premultiplied` | |
| (3, 4, 1) | 4 | `Blend::Subtract` | |

Alpha is always blended separately (`ONE, INV_SRC_ALPHA`) rather than being
left to follow the colour factors, which would be wrong for every mode whose
colour factors read the destination. Note the scan buffer BotW hands the hook
is `UNORM_R10_G10_B10_A2`, so its alpha is 2 bits and is never displayed:
destination-alpha factors are not useful there.

Quads batch by (texture, blend), so mixing modes costs one extra draw call
per switch, not per quad.

### Transparency

Three things stack, the same way lyt stacks them:

1. the texture's own alpha (a `^s`/`^d` sprite is nothing but alpha),
2. the per-vertex colour alpha you pass in,
3. the group alpha - `Canvas::PushAlpha(f)` / `PopAlpha()`, the immediate-mode
   stand-in for a pane's alpha multiplying down its subtree. Everything
   emitted inside it, text shadows included, is multiplied by `f`, so a whole
   window fades as one rather than each element fading independently.

The one thing not reproduced is the backdrop: the game's windows sit over a
blurred **capture of the framebuffer** (`FBLayout_00^r`, `P_Sh_00`), which is
why the real dialogue box looks like frosted glass rather than flat black.
`MessageWindow` draws the black at the pane's own alpha (230), which reads
correctly over dark scenes and slightly flatter over bright ones.

### Resolution and pixel alignment

The colour buffer is **not** 1280x720: BotW renders the GamePad view at
854x480 (confirmed live from the hook), and a Cemu resolution pack can make
the TV buffer any size. Layout coordinates stay 1280x720; the mapping onto
the buffer is recomputed every frame from the size the hook is given.

* `ScalingMode::Fit` (default) scales uniformly and centres, so a buffer that
  is not 16:9 letterboxes instead of stretching the UI. `ScalingMode::Stretch`
  fills the buffer. On any 16:9 buffer the two are identical.
* `Canvas::DeviceWidth/Height`, `PixelScaleX/Y`, `ViewportOffsetX/Y` report
  the real numbers; `SnapX/SnapY` round a layout coordinate to a whole device
  pixel.
* Pixel snapping is available (`GUI::SetPixelSnapping`) but **off by
  default**, because the buffer the game declares is not always what is
  actually rendered: under a Cemu resolution pack the game still describes a
  1280x720 colour buffer while Cemu renders a scaled-up texture behind its
  back, so a "device pixel" here would be several real ones and snapping
  would misalign rather than sharpen. Turn it on for a native-resolution
  target. The same caveat applies to what `DeviceWidth/Height` report: they
  are the buffer the game declares. Nothing else is affected - drawing goes
  out in normalised coordinates, so the UI is rasterised at whatever the real
  output resolution is. Glyphs never snap: rounding each glyph's own edges would jitter the spacing between them,
  and at the sub-1:1 scale of the 854x480 buffer it would eat into the
  letterforms.
* Stretching an edge means repeating **one** column or row, so an edge quad
  uses the same texture coordinate for both of its ends on that axis - a
  degenerate range. Giving it a range that runs to 1.0 makes the edge fade
  out along its own length wherever the art stops short of the tile, which is
  where the "alpha goes from what we set on the left to transparent on the
  right" came from: the selection frame's glow ends twelve texels early, so
  its edges faded to nothing across the button.
* Which column that is comes from `impl::EdgeU/EdgeV`: the innermost texel
  the artwork actually covers. For most of the game's corner art that is the
  last texel; the selection frame's arc stops one texel short and its glow
  twelve, so those carry a measured override in the sprite table.

### Frames, corners and rotation

The game's corner art is authored as the **top-left** piece with the stroke
running to the texture's right column and bottom row, and lyt mirrors it for
the other three corners - `Canvas::Corners` does the same. Two things follow
that were wrong in the first version:

* **A cursor is a frame, not four corners.** `PaOptionBtn_00`'s `Window_00`
  is a window pane (332x96 around a 276x40 row - the 28px margin) with a 48px
  frame, so lyt draws the corner brackets *and* the stretched edges between
  them. Drawing corners alone left four bright marks floating near the row's
  ends. Its material is additive (lyt 1,4,1) at **pane alpha 128**; the first
  version used 230, which is what made the focused row glow so hot.
* **Rotation is not limited to quarter turns.** `PaBoxedCursor_00` rotates its
  downward arrow by lyt -135/-225/-45/+45 at the four corners, so the arrows
  point diagonally outward. `EmitQuad` takes a free rotation (its sine and
  cosine come from a small polynomial - the payload links no math library),
  and **angles from a .bflyt need their sign flipped**, because lyt panes are
  y-up while the screen is y-down.

**A `^t` sprite's red channel is not always a colour.** These are BC5, two
channels, and the component map decides where each lands. For `BtnBasic` the
red channel is the rim's white highlight and mapping it to RGB is right. For
`SelectFrame_04` it is a gradient the game feeds to a TEV stage as the ratio
between two material colours (`fore` `(0,157,206)`, `back` `(0,193,242)`), and
where the shape is solid it ranges 0-216 with a median of 55 - so routing it
into RGB painted the frame black-through-white instead of tinting it. Those
sprites take `kCompMapShapeFromG` (RGB = 1, alpha = green) and let the vertex
colour supply the colour.

**Padding is part of the art, and lyt applies it uniformly.** `CornerR3_00`
is an 8x8 tile whose quarter disc occupies only the bottom-right 6x6; the two
texels above and left of it are transparent. `CornerLineR2_00`'s stroke sits
in its bottom-right 4x4. `Nt_CursorS_00`'s bracket starts 23 texels in, and
`BtnBasic_08T`'s rim starts 36 texels in from the left and 40 from the top.
A window pane draws each corner from the whole tile *and* stretches the same
tile's last column and row for the edges, so the padding shifts everything
inward by the same amount and nothing ever misaligns. The first version drew
the corners from the tile but the edges as flat rectangles running to the
rect's edge - a notch at every corner of every rounded box, and bracket
ticks on every outline, visible the moment the game was run at 1440p. Every
frame is now built the lyt way (`FrameFromCorner`: corners plus edges from
the measured edge texel, and for `RoundedBox` a flat centre inside the
frame), which also means the visible box sits 2 px inside its rect at the
8 px frame size, exactly as the game's option rows do.

The light plate (`Canvas::Plate`) takes the VISIBLE plate as its rect.
`BtnBasic_08T/08B` are only the rim and an inner glow - their interior alpha
falls to 17%, because in the game the surface comes from a third, projected
texture the material combines in - so the rim and its shadow (`08TS/08BS`,
black at alpha 150, 4 px down, as `BtnDialog_00`'s `W_Shadow_00` has it) are
nine-sliced over the rect grown by the art's padding, and an opaque rounded
fill goes under the rim to stand in for the surface texture. Without that
fill the plate was a translucent grey.

### Text

**Only `Normal_00` is loaded.** `NormalS_00` is the same face with a black
outline baked into every glyph - the game uses it over busy scenery, but as a
general UI font that permanent stroke reads as a smudge, so no style asks for
it and the loader skips it (saving 512 KB of the payload's 6 MB heap).
`FontId::NormalSmall` still resolves: `ResolveFont` falls back to `Normal_00`,
so a style or mod naming it keeps working. The presets that the layouts set in
NormalS (`Name`, `Option`, `Button`, `System`, `Small`) keep the layout's cap
height and take their width from Normal's own 31:39 cell so the text is not
condensed.

`MessageBox` shrinks text that does not fit. The box holds exactly three lines
at the layout's own size, so `Canvas::FitToBox` re-wraps at progressively
smaller scales (each pass by the square root of the line-count ratio, since
shrinking both shortens lines and fits more per line) until it fits or hits a
floor of half size. It is public, so any box can use it.

Sizes are the layouts' own convention: `(sizeX, sizeY)` is the pixel size the
font's `(width, height)` cell is scaled to, so `(23.2, 31.2)` on `Normal_00`
(31x39) is the dialogue box's 0.75 x 0.8. Lines advance by `lineFeed`, which
in both loaded fonts equals the cell height, so stacked cells land exactly
where lyt puts them.

Kerning comes from the font's `KRNG` block and is on by default
(`TextStyle::kerning`). BotW's pairs are small - -2 to +2 glyph units, under a
pixel at dialogue size - but they are what keeps `AV`, `To` and `P,` from
reading loose beside the game's own text. `KRNG`'s offsets are in 16-bit
**words**, not bytes, and the tables are keyed by character code rather than
glyph index (see `graphics/bffnt.hpp`).

### Loading and memory

`GUI::Init()` calls `GX2::Init()` and, once the pipeline is up, starts the
loader, which runs a few 64 KB chunks per frame from the draw callback
(`GUI::SetLoadBudget` changes how many; `GUI::LoadNow()` blocks). What it
opens, through the same content mount the game reads:

1. `Font/Font_US.sbfarc` (then `_EU`, `_JP`; `GUI::SetAssetPaths` overrides) -
   Yaz0 SARC - `Normal_00.bffnt`, `NormalS_00.bffnt`.
2. `Layout/Common.sblarc` loose if present, else `Pack/Bootup.pack` (plain
   SARC, ~30 MB, read at offsets) - `Layout/Common.sblarc` (Yaz0 SARC, 6 MB
   compressed / 31 MB decompressed) - the `timg/*.bflim` in
   `gui_render.hpp`'s table.

Nothing is buffered whole: Yaz0 back-references reach at most 0x1000 bytes,
so `Yaz0::StreamDecoder` keeps a 4 KB window and hands decompressed runs to a
sink tagged with their offset. The sink captures the first 64 KB (SARC header
+ node table), resolves the wanted files by name hash, then copies just those
byte ranges: font glyph sheets straight into their GX2 surfaces, each small
`.bflim` into one 32 KB staging buffer that is uploaded the moment its last
byte arrives. Everything else streams past. This matters because the Cemu
payload's entire heap is **6 MB** (`deploy.py` reserves it after the code
cave) and this module never frees anything.

Kept afterwards: ~1.5 MB of glyph sheets (`Normal_00` is two 1024x1024 BC4
sheets, `NormalS_00` one 512x1024 A8), ~250 KB of UI art, ~20 KB of font
tables, 160 KB of loader scratch, plus the 512 KB vertex ring in `gx2.hpp`.
`GX2::DrawMesh`'s private depth buffer (3.6 MB) is allocated only if a mod
uses it - a mod using both is close to the heap limit.

## What was measured, and how

All of it against the Wii U **v208** update files (Cemu's
`mlc01/usr/title/0005000e/101c9400/content`), decoded offline with the
`tools/preview_ui_assets.py` de-tiler (an addrlib port) and BFLYT dumper.
Nothing here is taken from format documentation without checking it against
the real files.

### Fonts (`Font/Font_US.sbfarc`)

| File | FINF w x h, ascent, lineFeed | Sheets | Format | Notes |
| --- | --- | --- | --- | --- |
| `Normal_00.bffnt` | 31x39, 32, 39 | 2 x 1024x1024, 32 cols x 25 rows | 12 | The dialogue / menu font. |
| `NormalS_00.bffnt` | 24x30, 23, 30 | 1 x 512x1024, 20 x 33 | 8 | Same face with a baked outline; HUD, names, option rows. |
| `Caption_00.bffnt` | 18x22 | 1 x 512x1024 | 12 | Not loaded. |
| `Special_00.bffnt` | 91x104 | 2 x 1024x1024 | 12 | Not loaded (titles). |
| `External_00.bffnt` | 42x39 | 1 x 128x1024 | 12 | Not loaded (button glyphs as text). |

Findings the code depends on:

* **Sheet format 12 is BC4, not BC1.** The usual BFFNT format table says 12 =
  BC1; decoding `Normal_00`'s sheets as BC1 gives noise, as BC4 gives clean
  glyphs. 8 is A8 as documented.
* **Sheets are GX2 2D-tiled (tile mode 4, swizzle 0)** and their byte size
  equals what `GX2CalcSurfaceSizeAndAlignment` computes for that surface, so
  they upload as-is (the loader checks the sizes match and refuses otherwise).
* **Sheets are stored upside down** relative to the layout textures: glyph row
  0 sits at the bottom of the GPU texture and every glyph is mirrored
  vertically. `BFFNT::Font::GlyphUV` flips V. (BFLIM art is upright - the
  `Nt_KeyTexA_00` "A" decodes the right way up with the same de-tiler.)
* Cells are `(cellWidth+1) x (cellHeight+1)` with a one-pixel border; glyph
  pixels start at `+1,+1` (checked: sheet columns 0, 32, 64 ... are empty).
* Layout text sizes `(sizeX, sizeY)` are the pixel size the FINF `(width,
  height)` cell is scaled to: `Message_00`'s (23.2, 31.2) is Normal at 0.75 x
  0.8, `BtnDialog_00`'s (31, 39) is 1:1, `AppSystemWindow_00`'s (24, 30) is
  NormalS at 1:1.

### Layout textures (`Layout/Common.sblarc`, 917 `timg/*.bflim`)

The footer's format byte, by the name suffix convention the game uses:

| Suffix | Format byte | GX2 | Meaning | Component map used |
| --- | --- | --- | --- | --- |
| `^s` | 16 | BC4 | alpha only ("shape") | RGB = 1, A = R |
| `^t` | 17 | BC5 | luminance + alpha | RGB = R, A = G |
| `^d` | 1 | A8 | alpha only | RGB = 1, A = R |
| `^r` | 15 | BC4 | luminance (capture / noise) | RGB = R, A = 1 |
| `^f` | 3 | RGB565 | opaque colour | RGB, A = 1 |

Tile mode and swizzle come from the footer (`low 5 bits`, `top 3 bits << 8`)
and, again, every file's data size matches the GX2-computed surface size for
those parameters (e.g. `Nt_DialogWindowBase_00^t` 488x736 BC5 tile 4 -> pitch
128, height 192 blocks -> 393216 bytes = the file's `dataSize`).

The pieces the GUI loads, with what they are:

| Sprite | File | Size | Used for |
| --- | --- | --- | --- |
| `MsgWindowCap` | `Nt_MsgWindowL_00^s` | 96x192 | Dialogue box left cap (right is mirrored). |
| `MsgWindowCapSmall` | `Nt_MsgWindowSL_00^s` | 40x78 | Choice-button cap (`PaMessageBtn_00`). |
| `MsgDeco`, `MsgDecoLine` | `Nt_MsgDecoL_00^s`, `_02^s` | 24x74, 12x72 | Ornaments at the box ends. |
| `CornerRound`, `CornerLine` | `CornerR3_00^s`, `CornerLineR2_00^s` | 8x8 | Rounded box / outline corners. |
| `CursorCorner` | `Nt_CursorS_00^s` | 48x48 | Option cursor corner. |
| `CursorBracket` | `Nt_Cursor_00^t` | 64x64 | Inventory cursor corner. |
| `SelectFrame`, `SelectFrameGlow` | `SelectFrame_04^t`, `SelectFrameGlow_00^s` | 68x68, 71x71 | Selection frame. |
| `ArrowDown`, `ArrowGlow` | `Nt_ArrowS_02^s`, `Nt_ArrowSGlow_02^s` | 75x75, 90x90 | Triangles (boxed cursor, value arrows). |
| `ArrowMsg`, `ArrowMsgMinus` | `Nt_ArrowMsg_00^d`, `_01^d` | 32x32 | "More text" arrow. |
| `KeyA` .. `KeyZL` | `Nt_KeyTex?_00^d` | 48x48 | Button glyphs. |
| `Glow`, `Shadow` | `CircleEnv32_00^t`, `DialogShadow_00^s` | 32x32, 128x128 | Soft glow / shadow blobs. |
| `PlateTop/Bottom(+Shadow)` | `BtnBasic_08T^t`, `08B^t`, `08TS^s`, `08BS^s` | 96x96 | Confirm-dialog plate corners. |
| `CursorCircle` | `Nt_CursorCircle_00^t` | 32x64 | Half-ring cursor. |

Window frames in the layouts are authored as a single top-left corner texture
that lyt mirrors for the other corners (`W_Base_00` has one `LT` frame;
`BtnDialog_00`'s four-frame plate lists `08T` for both LT and RT with no flip
flag) - `Canvas::Corners`/`FrameFromCorner` do the same, and edges are the
corner texture's inner column / row stretched, as lyt does.

### Recipe: `Message_00.bflyt`

The dump the dialogue box comes from (pane, then the relevant numbers):

```
wnd1 W_Base_00   alpha=230 pos=(0,-235) scale=0.67 size=914x192 frameSize=(96,96,192,192) frames=1 flags=0xb
     content mat W_Base_00C: back=(0,0,0,255) tex=[Nt_MsgWindowL_00^s, FBLayout_00^r]  (the ^r is a framebuffer capture = blur)
pic1 Nt_MsgDeco_00/03 pos=(-284,-235) scale=0.67 size=24x74 tex=Nt_MsgDecoL_00^s     (and mirrored at +284)
pic1 Nt_DecoLineL_01  pos=(-278,-235) scale=0.67 size=12x72 tex=Nt_MsgDecoL_02^s
txt1 T_Message_00 pos=(0,-235) scale=0.90 size=495x94 font=Normal_00 size=(23.2,31.2)
     top/bottom=(255,255,255,255) mat back=(255,252,198,255) align=left lineAlign=middle
     shadow off=(2,-2) top/bot=(0,0,0,100)
txt1 T_Name_00 pos=(-248,-174) size=176x26 font=NormalS_00 size=(19.2,25.5) white, shadow (2,-1) black
pic1 P_Sh_00  pos=(-157,-174) size=210x40 alpha=180 tex=FBLayout_00^r   (blurred backdrop behind the name)
```

Layout y is up and relative to screen centre, hence `Metrics::kMessageWindowCenterY = 360 + 235`.
Text colour is the material's white/"back" colour multiplied into the pane's
vertex colours - that is where the cream comes from.

### Recipe: `PaOptionBtn_00.bflyt` (option rows)

```
wnd1 W_Base_00      size=276x40 frameSize=8  tex=CornerR3_00^s      mat back=(0,0,0,200)
wnd1 W_BaseLine_02  size=276x40 frameSize=8  tex=CornerLineR2_00^s  mat back=(255,252,198,8->180 when focused)
wnd1 Window_00      size=332x96 frameSize=48 tex=Nt_CursorS_00^s + Kumo64_00^r  additive, cream   (the cursor)
txt1 T_Text_00      size=240x25.5 font=NormalS_00 size=(19.2,25.5) white
```

### Recipe: `BtnDialog_00.bflyt` (confirm dialog)

```
wnd1 W_Pict_00        size=560x240 frameSize=96 frames=4: LT/RT=BtnBasic_08T^t, LB/RB=BtnBasic_08B^t (+ProjectTex_01^o, BtnBasic_08?I+t via TEV)
wnd1 W_Shadow_00      size=560x240 frameSize=96 frames=4: 08TS^s / 08BS^s, black, alpha 150
txt1 T_BtnDialog      size=320x48 font=Normal_00 size=(31,39) top/bottom=(40,40,40)
wnd1 W_SelectFrame_00 size=530x186 frameSize=71 tex=SelectFrameGlow_00^s mat back=(0,193,242,102)
wnd1 W_SelectFrame_01 size=530x186 frameSize=68 tex=SelectFrame_04^t
```

## What is approximate

* **Backdrop blur.** See Transparency above - the biggest single difference
  from the real thing.
* **The plate** (`Canvas::Plate`). `BtnBasic_08T/B` only carry the rim and
  shadow; the plate's fill comes from a projected inner texture combined in
  the material's TEV stages. The GUI draws a flat rounded fill under the rim
  9-slice instead. Right shape and colours, not the exact shading.
* **Cursor shimmer.** `Window_00`'s additive cloud texture (`Kumo64_00^r`)
  and the various glow animations are not reproduced; the cursor is static
  cream.
* **Alpha test.** A few materials (the selection frame among them) enable
  lyt's alpha compare. Everything here is blended instead, which for UI art
  with soft edges is what you want anyway.
* **Per-material separate alpha blending.** A handful of layouts carry a
  separate alpha blend block; the GUI always uses `ONE, INV_SRC_ALPHA` for
  alpha, which is right for every colour mode it exposes.
* **Sprite size cap.** Textures over 32 KB are skipped by the loader (none of
  the ones in the table are).

## Runtime status

Compiled (devkitPPC 15.1) for the Cemu and Wii U targets and the Switch
stub, with no dynamic static initialisers in the object (the Cemu payload
has no C runtime).

**Run in Cemu 2.6 against v208 (2026-09-01)** with the
`BreathOfTheWild_GUITest` mod next to this repo: the loader reports
`2/2 fonts, 27/27 sprites, 34533412 bytes decompressed` about 1.2 s after the
first injected frame (`Font/Font_US.sbfarc`, then `Layout/Common.sblarc` at
offset 4857632 inside `Pack/Bootup.pack`), and the dialogue box renders as
intended - cap art, ornaments, cream Normal text with its shadow, the
NormalS name tag and the "more" arrow, all upright, at the game's own
placement. The first run found one bug: `SARC::ParseHeader` read the node
count from the SFAT header-size field, so every lookup failed; fixed. The
widget panel (option rows, plate button, select frame, cursors) was closed
by a stray B press before it could be captured, so it has NOT been seen on
screen yet - that is the next thing to check (the panel's art, then D-pad
focus movement, A on a Toggle, left/right on the Slider). The test mod now
ignores B so the panel stays up.

A second pass went over blending, transparency, resolution handling and
kerning. A third fixed what a screenshot of the running menu showed to be
wrong: the option cursor was drawn as four corners at alpha 230 instead of a
frame at 128 (a hot glow around every focused row), the boxed cursor's arrows
were quarter-turned instead of rotated 45 degrees, the selection frame's
edges sampled an empty texel and so drew nothing, and the plate had no
stretched centre. Those were found and fixed against the decoded art offline
(`tools/preview_ui_assets.py` plus a compositor that renders the same rules),
and they compile for all three targets. A second screenshot at 1440p then
showed the corner-padding problem described under "Frames, corners and
rotation" (notches on every rounded box, ticks on every outline, a grey
translucent plate). A third pass, from a 1440p screenshot of the menu, fixed
the rest: edge quads spanning a texture-coordinate range instead of repeating
one column (the left-to-right transparency ramp), `SelectFrame`'s red channel
being routed into RGB (black-grey-white instead of a tint), the plate corner
being sized to fit rather than to the layout's proportion, and the outlined
font. All of it is verified with the offline compositor - bilinear, at 2x and
4x, which is how the ramp was finally pinned down - but has NOT been seen in
the game since. Wii U hardware: untested.
