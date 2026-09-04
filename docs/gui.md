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
| `gui/gui_backend.hpp` | **The porting seam.** Everything the GUI needs from a graphics API, in one namespace: 20 names, aliased to GX2 today. The only GUI file that mentions GX2 at all. |
| `gui/gui_render.hpp` | Renderer core: the sprite/font tables and the one primitive (`EmitQuad`: layout pixels -> NDC -> `Backend::BatchQuad`). |
| `gui/gui_text.hpp` | UTF-8 -> glyph quads with nw::font's layout rules (cell scaling, advances, wrapping, shadow pass). |
| `gui/gui_assets.hpp` | The streaming loader that pulls fonts and textures out of the game's archives at runtime. |
| `graphics/bffnt.hpp` | BFFNT font parser (FINF/TGLP/CWDH/CMAP, glyph lookup, cell UVs). |
| `graphics/bflim.hpp` | BFLIM footer parser -> GX2 surface description. |
| `platform/yaz0.hpp` | Streaming Yaz0 decoder with a 4 KB window. |
| `platform/sarc.hpp` | SARC header / node-table lookup by name hash. |
| `<wiixlaunch/fs.hpp>` (base) | `FS::File` - open + positioned reads (`FSReadFileWithPos`), added for the loader. Lives in base WiiXLaunch, not this module. |
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
* `Frame` runs once per frame from **Controller's input hook**, right after
  the pad is sampled and before the game is allowed to see it - not at present
  time. That is what lets `CaptureInput()` hide the very press that opened a
  menu (see below). What it draws is recorded and replayed by the
  `aglCopyToScanBuffer` hook a moment later, so drawing still happens where a
  GPU context exists. Two consequences worth knowing: it runs on the game's
  own thread, which makes calling game code from it safer than before, and it
  needs `Controller::Init()` - without it the frame is built at present time
  instead and capture goes back to lagging by one frame.
* Widgets are navigated with the D-pad / left stick (with key repeat), `A`
  activates, `B` is only *reported* (`Canvas::Cancel()`) - what it closes is
  the mod's call. Focus order is issue order; each frame's widget list may
  differ (the focus index is clamped).
* `Canvas::CaptureInput()` takes the pad away from the game: call it every
  frame a menu is up and the player's buttons, sticks and GamePad touches stop
  reaching the game while the GUI carries on reading them. It applies to the
  input being read *at that moment*, so the press that opens a menu never
  reaches the game either - the frame is built inside the input hook precisely
  so that this decision can still be acted on. (A Wii Remote or Pro Controller
  press can still slip through on the opening frame if the game happens to
  read KPAD before VPAD that frame; the GamePad cannot.) It is never
  automatic, because an overlay that merely draws must not swallow input, and
  it lapses a few frames after the last call, so a menu that stops drawing
  cannot leave the pad dead. `Controller::SetInputCapture(true)` is the
  latched form for non-GUI callers. Input the mod injects with
  `Controller::Send()` still reaches the game - that is the mod acting, not
  the player.
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
| `SelectFrame(rect)` | `BtnDialog_00` `W_SelectFrame_00/01`: `SelectFrame_04^t` with stretched edges, `SelectFrameGlow_00^s` under it in `(0,193,242)` alpha 102. The corner keeps the layout's proportion (68 px on a 186 px window, ~0.37 of the height); sizing it to fit made the stroke heavy and, since the top and bottom bands are each a corner tall, filled the middle with glow. |
| `Plate(rect)` / `PlateButton` | `BtnDialog_00` `W_Pict_00`: a nine-slice of `BtnBasic_08T^t` / `08B^t` over the `08TS`/`08BS` shadow with an opaque fill beneath, `T_BtnDialog` Normal 1:1 in `(40,40,40)`. The corner keeps the layout's proportion (96 px on a 240 px window, so about 0.55 of the button's height) rather than being made as large as fits, which turned small buttons into fat pills. **Approximate** - see below. |
| `CursorBrackets(rect)` | `Nt_Cursor_00^t` 64 px bracket corners (the inventory cursor). |
| `BoxedCursor(rect)` | `PaBoxedCursor_00`: `Nt_ArrowS_02^s` triangles at 0.21 scale, rotated 45 degrees off vertical so they point diagonally outward, 3 px out from each corner. |
| `ButtonIcon` / `KeyHint` | `Nt_KeyTexA/B/X/Y/L/ZL_00^d`. The glyph fills its 48 px tile, so the icon size is the drawn size (32 px by default); `KeyHint` sets the label at about two thirds of that, centres it on the icon and returns the width it used, so a row of hints places itself. |
| `Button` / `Toggle` / `Slider` / `Selector` | Option-row style: the three `PaOptionBtn_00` pieces above plus `T_Text_00` NormalS (19.2, 25.5) white. |
| `List(rect, items, count, index)` | The same option-row style, for more rows than the box can show. EVERY row claims a focus slot, not just the visible ones, so the D-pad walks the whole list and the slot indices stay put while it scrolls - claiming only the visible ones would change the focusable count mid-scroll and move focus to a different item. The window position is derived from `index` rather than stored, keeping the widget stateless: the focused row sits mid-window except at the ends, where it clamps. A track appears on the right only when the list actually scrolls, its thumb sized by the visible fraction. Returns true on the frame a row is activated with A. Windowing checked exhaustively over 15,930 count/height/index combinations: the focused row is never off-screen, the window never leaves range, and it never draws past the end. |

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

### Backdrop blur

The frosted glass BotW puts behind its windows: a pane samples
`FBLayout_00^r`, a capture of the framebuffer, which the material blurs, and
the window's own colour sits over it. `GUI::SetBackdropBlur(true)` does the
same here, and `Canvas::BlurBehind(rect)` draws it; `MessageWindow` and
`MessageBox` pick it up on their own once it is on. **It is off by default** -
it costs two render targets and a handful of full-screen draws on any frame
that uses it, and an overlay that draws no windows has no use for it.

How it works, in `GX2::BlurBackdrop`:

1. the game's colour buffer is described a second time as a **texture** over
   the same memory, so it can be sampled;
2. a quarter-size copy is drawn from it into one of two small render targets,
   four taps at a time - that downsample is itself most of the blur;
3. a few more four-tap passes ping-pong between the two targets, each reaching
   further, ending in target 0;
4. the UI samples that like any other sprite.

`downscale` (default 4) and `passes` (default 2) trade cost against how soft
it is. The targets are a quarter-size pair, about 460 KB, allocated once at
the first size seen and never freed, like everything else here.

`Canvas::FrostedBox(rect, colour, radius)` is the composed form and what
most callers want: it draws the blur and then the translucent box over it, in
that order. A blur can only be drawn as a rectangle, so it covers the
rounded shape with rectangles that all stay inside it: a full-width middle
band, top and bottom bands between the arcs, and a five-step staircase
inscribed in each corner's quarter disc. One inset rectangle cannot do it -
inset far enough to hide its corners and it leaves an unfrosted border, inset
less and square corners show through the round ones - and the bands alone
leave the four corners sharp while everything around them is frosted, which
is just as visible. Together they cover 99% or better.

The geometry comes from the art rather than being guessed: `CornerR3`'s
quarter disc runs from texel 2 to 7 of its eight, so a corner drawn at
`radius` has an arc of 0.75*radius centred at (radius, radius), and the box
actually drawn is inset from its rect by a quarter of the radius. Each
staircase slice is measured at its outer edge, where its far corner would
land exactly on the arc, then trimmed by 4% so it lands just inside.

`MessageWindow` deliberately does NOT frost itself. The window is a pill, and
since a blur can only be a rectangle, the only part that could be frosted
without its corners showing past the rounded ends is the straight middle -
which reads as a bright band across the centre of the box. The raw `BlurBehind()` is a full-strength opaque rectangle of the
blurred scene, so on its own, or issued after the contents it sits behind, it
simply paints over them - which looks like the UI itself got blurred, and is
the easiest mistake to make with it.

Three things it does not do. **There is no per-pixel mask**: the blur is drawn
as a rectangle, so a window with rounded corners has blur in its corners too,
covered by whatever is drawn over it. Masking properly needs a shader that
samples two textures, and building one needs the Wii U shader compiler this
repo does not have. **Aliasing a colour buffer as a texture** is not something
GX2 promises when the surface was not created with texture usage - Cemu is
happy with it, real hardware may not be. And at the alpha the game's own
windows use (230, so 90% opaque) the blur is a subtle thing: it is most
visible behind something more translucent.

### Frame rate

**Animate from time, never from a frame count.** BotW runs at 30, and the
FPS++ graphic pack makes it 60 - anything counting frames then runs at double
speed, which is what made the cursors blink and the arrows bob twice as fast.
`Canvas::DeltaSeconds()`, `TimeSeconds()`, `Phase(period)` and `Wave(period)`
are the units to build on, and everything here uses them.

The clock is `WiiXLaunch::Time::GetMonotonicTicks`, the Espresso timebase read
with `mftb`: no import, no OS call, and Cemu keeps it on real time. Deltas are
clamped to a quarter second so a load screen or a breakpoint cannot make an
animation jump. `Canvas::FramesPerSecond()` reports the game's measured rate,
smoothed - measured, not assumed.

`Phase` is taken from the tick count with an integer modulo rather than from
accumulated seconds. A float holding elapsed time loses resolution as it grows,
so an animation smooth at boot would quietly start to step after a long
session; the modulo is exact however long the game has been running.

**Move smoothly, not in steps.** Getting the rate right exposed a second
problem the doubled speed had been hiding: the cursor pulse was a two-state
blink and the message arrow bobbed in four fixed positions, and at the correct
speed both read as stutter rather than as motion. `Wave(period)` is the fix - a
cosine, 0 to 1, easing in and out of both ends, built on `SinCosDeg` so it
needs no libm. The cursor now breathes over 1.4 s and the arrow bobs 4 px over
1.1 s, continuously. `Phase` stays the right unit for anything that should run
at a constant rate, like a loading bar; a triangle folded out of it turns
around in a single frame at each end, which is visible as a flick.

`Display::GetAspectTerms` turns the ratio into whole numbers, so a UI can show
16:9 rather than 1.78:1. The named ratios are matched first, with a deliberately
loose 3% tolerance, because the pack writes width/height of the chosen
RESOLUTION rather than the aspect picked from the dropdown: its "21:9" category
offers 2560x1080, 3440x1440 and 3840x1600, which are 2.370, 2.389 and 2.400 -
three different numbers, none of them 21/9, all of which should read back as the
21:9 that was selected. 3% covers all three without any two names colliding, the
closest pair (16:10 and 5:3) being 4.2% apart. All twelve values the pack can
produce were checked against the category they come from. Anything unrecognised
falls back to the smallest denominator within 0.25%, and then to the decimal.

### Resolution and pixel alignment

The colour buffer is **not** 1280x720: BotW renders the GamePad view at
854x480 (confirmed live from the hook), and a Cemu resolution pack can make
the TV buffer any size. Layout coordinates stay 1280x720; the mapping onto
the buffer is recomputed every frame from the size the hook is given.

* `ScalingMode::Fit` (default) keeps the layout's shape on the SCREEN and
  centres it, so a buffer that is not 16:9 letterboxes instead of stretching
  the UI. `ScalingMode::Stretch` fills the buffer.
* **Ultrawide is detected**, and does not need configuring. The colour buffer
  cannot answer it - a Cemu pack scales the render target behind the game's
  back and the declared GX2 surface stays 1280x720 - but the GAME's own aspect
  constant can, because an ultrawide pack has to rewrite that for the game
  itself to render correctly. `Display::GetAspectRatio()` reads it (see
  `game/display.hpp`); the mapping then divides the horizontal scale by how
  much wider a buffer pixel is displayed than it is tall, and pillarboxes the
  result. `GUI::SetOutputAspect()` overrides it, and
  `GUI::GetEffectiveOutputAspect()` reports what is actually in use.
* **The output RESOLUTION is not detectable**, and the contrast with aspect is
  the whole point. An ultrawide pack has to rewrite the game's own aspect
  constant, because the game could not render correctly otherwise - so the
  value is sitting in `.rodata` at a fixed address for anyone to read.
  Resolution is different: it is applied entirely outside the game, by
  `[TextureRedefine]` rules that rewrite surface dimensions as Cemu sees them
  created (`overwriteWidth = ($width/$gameWidth) * 1280`). The game is never
  told, and never needs to be. `Canvas::DeviceWidth/Height` therefore report
  1280x720 under a 1440p pack, and that is the true size of the buffer the game
  declared - it is not the size of the picture.

  The only place `$width` reaches game-visible memory at all is a UI padding
  constant in the Graphics pack's own patch
  (`(($gameWidth/$gameHeight)/($width/$height)) * (($width/2) - 640)`), which is
  algebraically solvable for the width given the aspect - but it lives at
  `.origin = codecave`, an address that moves with graphic-pack load order, so
  it is not something to build on.

  None of this affects how the UI looks. Quads go out in normalised
  coordinates, so they rasterise into whatever surface Cemu actually allocated:
  at a 1440p pack the GUI is genuinely drawn at 1440p, not upscaled from 720p.
  The buffer size is a diagnostic, not a scale factor - which is why the test
  mod labels it `buf`.
* The draw hook runs once per colour buffer, and BotW has two: the TV and the
  854x480 GamePad view. Both are drawn into, with the mapping recomputed for
  each. `Canvas::DeviceWidth/Height` and `PixelScaleX/Y` report the LARGEST
  buffer of the last frame rather than whichever was drawn last, so the answer
  does not flip between the two screens; `ViewportOffsetX/Y` and `SnapX/SnapY`
  work against the pass currently being drawn.
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

**A frame corner must be drawn to the artwork's extent, not the tile's.**
`Canvas::Corners` samples `0..EdgeU/EdgeV` rather than `0..1`, for the same
reason the edges repeat that texel: where the art stops short of its tile,
drawing the corner all the way out leaves a transparent sliver exactly where
the stretched edge begins, and that sliver is a line down every join. The
selection frame stops one texel short, so it showed the seam even after the
sampler was fixed.

**The sampler must clamp, and GX2's CLAMP is 2.** `GX2TexClampMode` numbers
WRAP as 0, MIRROR 1, CLAMP 2, so a constant of 0 named "clamp" - which is
what `gx2_shader_types.hpp` had - makes every texture wrap. At a quad's edge
the sampler then reaches past the last texel and comes back to the FIRST one,
which for the game's corner art is its transparent padding, so every
corner-to-edge join in every frame carried a pale seam. That is what an
element's "outline" was at 1440p; the fix is the single constant.

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

**All six of the archive's faces can be loaded; only `Normal_00` is by
default.** The six together are 2.9 MB of glyph sheets against a 6 MB payload
heap, which is too much to spend uninvited, and one face is what almost any mod
needs. `GUI::RequestFont(id)` turns another on and must be called before the
first frame, like `SetAssetPaths` - by the time the loader runs, the archive
has already streamed past. `GUI::FontSheetBytes(id)` is what one costs.

Nothing about this can make text disappear: `ResolveFont` falls back to
`Normal_00` when the named font was never requested or failed to parse, so a
style naming a font you forgot to ask for changes how the text looks, never
whether it appears. That is also why the presets the layouts set in NormalS
(`Name`, `Option`, `Button`, `System`, `Small`) are defined in Normal at the
layout's cap height, with the width taken from Normal's own 31:39 cell so the
text is not condensed - they render correctly with nothing requested.

Two of the five are worth knowing about beyond their size. **`Ancient_00`
maps the Sheikah script over plain ASCII** (U+0020-U+007B), so ordinary text
comes out in the glyphs the Shrines and the Slate are lettered in - same
string, same layout, just a different `FontId`. **`External_00` contains no
letters at all**: 49 button and control icons in the private-use range, for
setting inline with text. Its mapping, read out of the file's CMAP:

| Codepoint | Icon | | Codepoint | Icon |
| --- | --- | --- | --- | --- |
| U+E040 | A | | U+E047 | ZR |
| U+E041 | B | | U+E048 | power |
| U+E042 | X | | U+E049 | D-pad |
| U+E043 | Y | | U+E04A | home |
| U+E044 | L | | U+E04B | plus |
| U+E045 | R | | U+E04C | minus |
| U+E046 | ZL | | U+E050..U+E08F | stick, trigger and arrow variants |

The range is not contiguous: of the 80 codepoints the CMAP covers, 49 have
glyphs (the gaps are U+E04D-U+E04F, U+E05C and U+E068-U+E082) and the rest
resolve to nothing and draw nothing. The mapping is CMAP method 1, a direct
index table, which `bffnt.hpp` handles along with methods 0 and 2.

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
   Yaz0 SARC - `Normal_00.bffnt` plus any other face `GUI::RequestFont` asked
   for. All six are found in one pass over the archive.
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
payload's heap is small and this module never frees anything.

How small, precisely, because the number that used to be written here was
wrong. `deploy.py` used to emit `.origin = codecave + 0x600000` under a comment
claiming to reserve 6 MB; it reserved nothing, and that line has since been
removed. Cemu gives a patch group only
the bytes it emits - the log line
`Applying patch group 'BotW_GUITest_V208' (Codecave: 01804600-01846f60)` is
272 KB, exactly the payload plus its relocation table. The heap then runs off
the end of that allocation, through whatever is left of Cemu's 4 MB code-cave
area (`MEMORY_CODECAVEAREA_ADDR` 0x01800000, `..._SIZE` 0x00400000), and into
the gap after it, which no Cemu region claims. The wall is **0x02000000**,
`MEMORY_CODEAREA_ADDR`, where the game's own code starts - about 7.9 MB from a
typical cave base, shared with any other WiiXLaunch payload that is loaded.

**Correction, measured.** The wall is not 0x02000000 but **0x01C00000**, the
end of Cemu's code-cave AREA. The gap above it is unclaimed by any Cemu region
and is not mapped: a payload allocating past it dies silently on the first
write. With one font the heap peaked around 2.4 MB and never reached the
boundary; loading four walked straight through it, which is what the
"font-related" crash actually was. Unclaimed address space is not memory.
`Arena::AllocHost` enforces 0x01C00000 and returns null past it, and `AllocMEM1`
logs the first refusal. (This was `Backend::AllocCemuHeap` until the arena
became the single memory owner; the backend heap API was deleted rather than
wrapped, because every caller of it did its own bookkeeping against the same
distance-to-the-wall and each believed it had all of it.)

That leaves the payload about **3.7 MB**, which four faces plus a vertex ring,
blur targets and loader scratch do not fit in - measured at 3807/3807 KB with
External_00 refused.

**The fix is to allocate from the game instead**, and it needs no game-specific
address. `WiiXLaunch::Mem::UseCoreinitHeap()` (base framework,
`wiixlaunch/mem.hpp`) reaches `MEMGetBaseHeapHandle` and
`MEMAllocFromExpHeapEx` through `import.coreinit.<Name>` shims - the same
mechanism `OSGetTime` and the FS calls already used - takes the MEM2 base heap
(**~68 MB free on v208**) and installs it with `Arena::SetHostProvider`. Call it
from a `GX2::OnInitialized` callback, before anything large is allocated; the
base heaps do not exist at module entry. With it, all six faces fit and the cave
stays at ~176 KB.

That provider moves **host** allocations only - this module is compiled into the
payload, so its font sheets are the host's memory. A loaded `.wxlm` mod's grant
is never redirected: it holds relocated code that gets executed, and the code
cave is the only region established as executable.

Two things that do NOT work, both measured rather than assumed: the game's own
MEM1 wrapper at 0x0309BB68 (`BotW::Heap::UseGameHeap`) returns null, because
BotW carves MEM1 up during boot and the base heap has nothing left; and
`MEMAllocFromDefaultHeapEx` cannot be used through a shim at all, being a
coreinit DATA export - a function pointer - so branching to the symbol would
jump to the pointer's storage rather than through it.

A frame is capped at 2048 quads (the record) and the vertex ring holds about
2180 per frame, so the record is what runs out first and it says so in the
log. Both matter: when the ring was the smaller of the two it ran out
silently, and because flushes are dropped whole rather than trimmed, a long
run of text would vanish while the two-quad arrows drawn right after it still
fitted and drew. Missing labels with their surrounding art intact is the shape
of that failure.

Kept afterwards: 1 MB of glyph sheets for `Normal_00` (two 1024x1024 BC4
sheets) plus whatever else was requested - up to 2.9 MB if all six are, see the
table below - ~250 KB of UI art, ~20 KB of font tables, 160 KB of loader
scratch, plus the 512 KB vertex ring in `gx2.hpp`.
`GX2::DrawMesh`'s private depth buffer (3.6 MB) is allocated only if a mod
uses it - a mod using both is close to the heap limit.

## What was measured, and how

All of it against the Wii U **v208** update files (Cemu's
`mlc01/usr/title/0005000e/101c9400/content`), decoded offline with the
`tools/preview_ui_assets.py` de-tiler (an addrlib port) and BFLYT dumper.
Nothing here is taken from format documentation without checking it against
the real files.

### Fonts (`Font/Font_US.sbfarc`)

All six, measured from the v208 US archive. Every one starts its sheet data at
0x2000 and is sheet format 12 (BC4) except `NormalS_00`, which is 8 (A8), so
they all load through the same path. `Heap` is sheet size x sheet count - what
`GUI::RequestFont` spends.

| File | `FontId` | Cell | Sheets | Fmt | Heap | Coverage |
| --- | --- | --- | --- | --- | --- | --- |
| `Normal_00.bffnt` | `Normal` | 31x39 | 2 x 1024x1024 | 12 | 1024 KB | ASCII, Latin-1, Cyrillic U+0401-U+0451, kana U+3000-U+30FC. The dialogue / menu font, and the only one loaded by default. |
| `NormalS_00.bffnt` | `NormalSmall` | 24x30 | 1 x 512x1024 | 8 | 512 KB | Same coverage, black outline baked in. HUD, names, option rows. |
| `Caption_00.bffnt` | `Caption` | 18x22 | 1 x 512x1024 | 12 | 256 KB | Latin plus CJK/kana U+3041-U+FF5E. The subtitle face. |
| `Ancient_00.bffnt` | `Ancient` | 13x14 | 1 x 32x1024 | 12 | 64 KB | U+0020-U+007B only - the Sheikah script mapped over ASCII. |
| `Special_00.bffnt` | `Special` | 91x104 | 2 x 1024x1024 | 12 | 1024 KB | ASCII and Latin-1 at over 3x Normal's size. A display face. |
| `External_00.bffnt` | `External` | 42x39 | 1 x 128x1024 | 12 | 64 KB | No letters: 49 button/control icons in U+E040-U+E08F. |

`Ancient_00` is the one case where a sheet's declared `sheetSize` is larger
than width x height x bpp: 65536 for a 32x1024 BC4 surface whose pixels are
16384 bytes. That is the tiled, padded size - a 32 px wide BC4 surface is
8 blocks across and pads to the macro-tile granularity - and it matches because
the loader sizes its surface with the game's own `CalcSurfaceSizeAndAlignment`,
the same addrlib that produced the file.

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

## Sound

The menu plays the game's own sound events: `mc_Key_On` when the cursor moves,
`mc_Key_Decide` on A. `GUI::SetUiSounds(false)` turns them off,
`GUI::SetUiSoundEvents(cursor, decide)` swaps them for any other event, and
they can only fire when focusable widgets exist - a HUD-only mod stays silent
without asking.

`Sound::Play("name")` (`game/sound.hpp`) is the general form. It goes through
`0x0359BD24`, the single entry point everything from the menus to the keyboard
uses, and the argument is a sead string whose **character pointer comes first
and vtable second** - `0x0359A950` reads the text as `*param` and calls a
virtual through `param[1]`, so it cannot be the other way round.

**Do not guess event names from strings in the binary.** That cost five rounds
of testing here. The `.rodata` is full of plausible ones - `mc_CursorMove`,
`mc_Decide`, `mc_List_FocusMove` - that do not resolve, and even `mc_CallHorse`
fails despite the game passing that exact string to that exact function from
`0x02D11534`. The lookup at `0x03BA1E08` is a binary search over ONE loaded
resource, and the manager at `*(0x1047C390)+0x20` holds a small system-scoped
set of **51 events** (the same on the title screen and in game): Amiibo, map
markers, the title cursor, the software keyboard, heart and stamina upgrades,
priest voices, screen changes. Nothing else is findable through it.

`Sound::DumpEvents()` walks that table and prints what is actually there,
which is how the working names were found. Use it rather than reading strings:

```
container 0x663d0b68 selector=0 (only this slot is searched)
  slot 0: entry 0x663d0b9c ready=1, 51 events
    AmiiboError
    ...
```

There is no general menu-cursor event in that set, which is why the keyboard's
sounds are the default - they are the only crisp UI sounds in it.

## Layout

Widget rectangles come out of a `Stack` rather than a running `y` the caller
has to remember to advance:

```cpp
GUI::Stack rows{panel.Inset(40, 0), 2.0f};   // 2px between rows
rows.Skip(54);                                // clear of the title
c.Selector(rows.Row(40), "Dialogue", dialogue, kDialogues, 3);
c.Toggle  (rows.Row(40), "Fade panel", fade);

rows.Skip(12);
GUI::Stack footer{rows.Row(44)};
c.PlateButton(footer.Column(170), "OK");     // fixed width, left of the row
```

`Row`/`RowBottom` take off the top and bottom, `Column`/`ColumnRight` off the
left and right, and each consumes what it returns plus the gap. `Rest()` is
whatever is left, `Fits(h)` asks before committing, and `Skip` inserts space
with no gap of its own. Running out returns a zero-height rect at the current
position rather than one overlapping what came before, so overflow shows as
nothing drawn instead of a pile-up.

`Grid` covers indexed cells - icon tables, swatches. `Grid::Fit(rect, cols,
rows, gapX, gapY)` sizes cells to divide the space; `Cell(i)` is pure, so
cells can be visited in any order; `CellsThatFit()` says how many the area
holds. `Rect` also gained `Offset`, `WithWidth`/`WithHeight`, `CenteredIn`,
`Contains` and `Empty`.

All of it is in `gui_types.hpp`, which is not platform-guarded, so layout code
compiles on Switch too even though the GUI there is a stub.

## A mod's own art

The `Sprite` overloads index a fixed table of the GAME's textures, measured at
build time. For a mod's own art there is a second set of entry points that take
a `Backend::TextureHandle`:

```cpp
static GUI::Backend::TextureHandle icon = 0;   // load once, not per frame
if (!icon) icon = GUI::LoadTexture("Layout/MyIcon.bflim");

c.Image(icon, {100, 100, 64, 64});                     // stretched to the rect
c.ImageUV(icon, {200, 100, 64, 64}, 0, 0, 0.5f, 0.5f); // one corner of it
c.ImageAt(icon, 300, 100);                             // native pixel size
```

They go through the same path as the game's own art - layout-pixel mapping, the
group-alpha stack, blend states, orientation, pixel snapping, batching - so a
mod's texture behaves like any sprite and batches with them.

`.bflim` is the format, the same one the game's art is in, which means
`tools/preview_ui_assets.py` decodes it and `tools/pack_texture_gx2.py` builds
it. `GUI::LoadTexture` reads through the game's content mount and is only
meaningful once the pipeline is up, so call it from an initialisation callback
or on first use, never per frame. Nothing in the GUI owns or frees the handle,
and it must outlive the frames it is drawn in.

`Canvas::TextureSize(handle, w, h)` reports the native size, and `ImageAt`
returns false rather than drawing if the size is unknown.

## Porting the renderer

The GUI reaches a graphics API through `GUI::Backend` and nothing else.
`gui_backend.hpp` is the only file in `gui/` that names GX2 - the other five
have zero references, checked by grep and by the compiler - so a second backend
means providing that namespace, not editing the GUI.

The whole surface is **20 names**:

| Group | Names |
| --- | --- |
| Types | `TextureHandle`, `BlendState`, `SurfaceDesc`, `TextureVertex`, `CommandBuffer` |
| Frame | `Init`, `RegisterDrawCallback`, `OnInitialized` |
| Drawing | `BeginBatch`, `BatchQuad`, `EndBatch` |
| Textures | `AllocTextureSurface`, `CreateTextureFromSurface`, `CreateTexture`, `FinalizeTexture` |
| Memory | `AllocMEM1` |
| Backdrop | `BackdropReady`, `BackdropTexture`, `BlurBackdrop` |

plus three constants describing the game's own art
(`kCompMapShapeFromG`, `kTileModeTiled2DThin1`, `kSurfaceFormatUnormR8G8B8A8`).
`gui_backend.hpp` carries the semantics of each in full.

What makes the surface this small: the GUI submits exactly one kind of
geometry - a textured quad, four vertices in strip order, alpha-blended, no
depth, no culling, no scissor - and everything else is layout, table lookups
and text shaping. `gui_text.hpp` never mentions a graphics API at all.

Two things a backend may decline. The backdrop trio can return `false`/`0`
forever; the GUI then draws without frosting and nothing else changes.
`AllocMEM1` and the three constants are the only entries currently behind
`#if WIIXL_CEMU || WIIXL_WIIU`, because gx2.hpp's Switch stub has no
`AllocMEM1` and every GUI file needing them is behind that same guard.

For NVN specifically: add the implementation to `gui_backend.hpp` behind
`#if WIIXL_SWITCH`, then widen the guards at the top of `gui.hpp`,
`gui_render.hpp`, `gui_assets.hpp` and `gui_text.hpp` to include it, and delete
the stub `Canvas` at the bottom of `gui.hpp`. If anything beyond that needs
changing, the seam is in the wrong place and is worth moving rather than
working around.

## What is approximate

* **Backdrop blur.** See Transparency above. The speaker name in `MessageBox`
  now has its real backing: the game uses `P_Sh_00`, a blurred capture of the
  framebuffer (`FBLayout_00^r`) at alpha 180, which is what `BlurBehind`
  provides - so this is the mechanism rather than a stand-in. It is sized to
  the measured text rather than to the pane, so a short name gets a short
  backing, and `BlurBehindFaded` fades its two ends out, because a hard-edged
  rectangle of blurred scene was exactly the failure of the earlier stand-in
  (`DialogShadow_00`, one quarter of a radial gradient, which drew as a hard
  dark box and was worse than nothing). With blur off or not yet ready it draws
  nothing and the name's own hard text shadow carries it, as before.
* **The plate** (`Canvas::Plate`). `BtnBasic_08T/B` carry only the rim and
  shadow. Reading `BtnDialog_00.bflyt`'s texture list settled what the fill
  actually is: `ProjectTex_01^o` (256x256 BC1, a soft satin sheen) projected
  through `BtnBasic_08TI/BI+t`, which are BC5 NORMAL MAPS, in the material's
  TEV stages.

  That rules out the obvious approximation. Sampled down the stretched column,
  those normal maps are flat 128/127 for their whole height - there is no baked
  gradient to copy, because the shading IS the projection. A vertical gradient
  would have been inventing shading the game does not have.

  What the GUI draws instead is the projected texture itself, stretched over
  the plate and multiplied: the same soft diagonal bands without the normal-map
  lighting. It cannot be loud - the texture is 216-255, so at most 15% darkening
  and usually under 6% - which is why it needs no strength control. Three quads,
  not one, because the fill is rounded and a square quad's corners would fall
  outside it and multiply the scene behind the plate instead; a middle band plus
  top and bottom strips covers 95% of the fill with 0 pixels outside it,
  measured. Skipped while a group alpha is pushed, since `Blend::Multiply`
  takes its alpha factors from the destination and so cannot fade with the
  panel around it.

  At 32 KB, `ProjectTex_01^o` is also the first sprite the old flat 32 KB
  staging cap would have refused.
* **Cursor shimmer.** `Window_00` scrolls `Kumo64_00^r` - a 64x64 BC4
  luminance cloud, near-seamless (opposite edges differ by 6/255) - through
  the option cursor's material to make it drift. `CursorCorners` reproduces
  the MOTION but not the mechanism: `impl::CloudField` evaluates two drifting
  sine octaves per vertex and modulates the frame's own colour, rather than
  drawing the cloud as a second pass.

  That choice is forced, not lazy. The renderer draws one texture per quad and
  cannot mask one texture by another; the game confines its cloud with the
  frame art's alpha in a TEV stage. As a separate additive pass the cloud would
  show as a square patch at each 48x48 corner, which is worse than no shimmer
  at all. Modulating the frame's colour is masked exactly by the art, needs no
  second texture and adds no draw calls. The texture itself loads fine at 4 KB
  and is available if the renderer ever gains a second sampler.

  Measured over 60 s on a 460x40 row: brightness swings 0.55x to 1.44x around
  unchanged, moves at most 0.5% of that swing per frame at 60fps (so it drifts
  rather than flickers), and differs by up to 0.81 between an edge's two ends,
  which is what makes it travel instead of pulsing as one. Corner and edge
  sample points sit 24 px apart, so the pieces stay continuous.
  `Metrics::kCursorShimmer` sets the amplitude; `CursorCorners(..., 0.0f)`
  turns it off. The other glow animations are still not reproduced.
* **Alpha test.** A few materials (the selection frame among them) enable
  lyt's alpha compare. Everything here is blended instead, which for UI art
  with soft edges is what you want anyway.
* **Per-material separate alpha blending.** A handful of layouts carry a
  separate alpha blend block; the GUI always uses `ONE, INV_SRC_ALPHA` for
  alpha, which is right for every colour mode it exposes.
* **Sprite size cap.** The staging buffer is now sized to the largest texture
  the sprite table actually asks for, allocated once the SARC node table has
  been read and the sizes are known. `kSpriteStagingLimit` (1 MB) is a ceiling
  on any single `.bflim`, not a budget. The old flat 32 KB buffer skipped 179
  of the layout archive's 917 textures - everything from
  `Nt_DialogWindowBase_00^t` (393 KB) up - so adding larger art to the table
  no longer means raising a constant. Nothing currently in the table is near
  either figure, so the memory cost is unchanged.

## Runtime status

Compiled (devkitPPC 15.1 / devkitA64) for the Cemu and Wii U targets and the
Switch stub, with no dynamic static initialisers in the object - checked with
`powerpc-eabi-nm` for `__cxa_guard` and `_GLOBAL__sub_I`, because the Cemu
payload has no C runtime.

**Run in Cemu 2.6 against v208 (2026-09-02)**, TV at 1440p through the
Graphics pack with FPS++ at 60, using the `BreathOfTheWild_GUITest` mod. The
loader reports `4 font(s), 27/27 sprites, 34533412 bytes decompressed` about
0.7 s after the first injected frame. Confirmed on screen: the dialogue box
with its cap art, ornaments and cream Normal text; the widget panel with option
rows, selector arrows, plate button, D-pad focus movement; the cursor and
selection-frame composites; the blend-mode strip; frosted blur behind the
panel; the blurred backing under the speaker name; the scrolling list with its
track; and the status line reading `buf 1280x720 @1.00x 60.0fps 16:9` - the
frame rate matching Cemu's own counter, and the aspect detected rather than
configured.

Wii U hardware: still untested. Everything above is Cemu.

### What the last few sessions found in the game

Screenshots and logs from actual runs found things offline verification had
not, which is the argument for testing rather than reasoning:

* The option cursor was four corners at alpha 230 instead of a frame at 128,
  the boxed cursor's arrows were quarter-turned instead of rotated 45 degrees,
  the selection frame's edges sampled an empty texel, and the plate had no
  stretched centre. All four fixed against the decoded art offline.
* Corner-art padding produced notches on every rounded box and ticks on every
  outline; edge quads spanning a coordinate range instead of repeating one
  column produced a left-to-right transparency ramp; `SelectFrame`'s red
  channel was routed into RGB. All found from 1440p screenshots.
* `SARC::ParseHeader` read the node count from the SFAT header-size field, so
  every lookup failed. First run, first bug.
* A silent crash that looked like a font bug was the heap: the payload's
  code-cave heap ends at 0x01C00000 and the allocator had been given a wall
  4 MB too high, so a fourth font walked into unmapped memory. The font code
  was innocent. See the Cemu heap notes above.
* The speaker name's blurred backing took two attempts, and neither fault
  would have appeared offline. Fading only its left and right ends left hard
  horizontal edges, so it read as a slab; and tinting it white handed back the
  scene at face value, which over a bright sky is pale grey, making white text
  on it HARDER to read than no backing at all. `P_Sh_00` is a shadow pane and
  has to be tinted down to behave like one. Both faults depend on what happens
  to be behind the box, which no offline check exercises.
* The frame-rate readout said ~180 while Cemu's own counter said 60. Two
  separate errors, one on top of the other: the clock updates once per PAD
  READ and BotW reads the pad about three times a frame, and the draw hook
  fires once per SCAN BUFFER, of which BotW presents two (TV and GamePad) -
  measured at exactly 120 calls in 20.0 s of wall clock against a reported 60.
  `UpdateFrameRate` now latches one buffer and times only that; the readout
  reads 60.0 against Cemu's own 60, confirmed in game. The clock itself was never wrong: it reads real ticks, so
  animation was correct at any call rate - only the reported figure was off.

### Verified without the game

Where a check could be made offline it was, because it is faster and repeatable:

* Frosted-box corner geometry: 0 rects outside the shape, 99.4-100% coverage.
* `Display::GetAspectTerms` against all twelve aspect/resolution combinations
  the Cemu pack can produce - each reports the category the user selected.
* `Canvas::List` windowing over 15,930 count/height/index combinations: the
  focused row is never off-screen, the window never leaves range, it never
  draws past the end, and the scrollbar thumb never overflows its track.
* Cursor shimmer: swing 0.55x-1.44x, at most 0.5% of that per frame at 60fps,
  up to 0.81 difference between an edge's two ends.
* The six fonts' headers, sheet formats, tail offsets and CMAP coverage, read
  straight out of `Font_US.sbfarc`.
* Every emitted graphic pack: exactly two `.origin` directives, the code cave
  and the entry hook.
