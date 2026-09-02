#pragma once

#include <wiixlaunch/platform.hpp>

#include "../graphics/gx2.hpp"

// GUI::Backend - the whole of what the GUI needs from a graphics API.
//
// Everything above this line is platform-neutral: layout, the sprite and font
// tables, BFFNT text shaping, the widgets, input, the asset loader's parsing.
// gui_text.hpp does not mention a graphics API at all. What is left is the
// short list below, and it is the entire porting surface - an NVN backend
// means providing these names in this namespace, not touching the GUI.
//
// The list is deliberately small and it is checked by the compiler: nothing in
// gui/*.hpp names GX2 directly any more, so anything missing from here is a
// build error rather than a surprise at runtime.
//
// ---------------------------------------------------------------------------
// THE CONTRACT
// ---------------------------------------------------------------------------
// Types
//   TextureHandle    Opaque, 0 = none. Copyable, outlives the frame.
//   BlendState       Source/destination factors and combine ops for colour and
//                    alpha separately. `Blend::` below names the ones used.
//   SurfaceDesc      width, height, format, tileMode, swizzle, compMap,
//                    linearFilter - what AllocTextureSurface needs to build a
//                    sampleable 2D texture.
//   TextureVertex    One vertex: position, texture coordinate, colour. Filled
//                    by FillVertex in gui_render.hpp; the layout is the
//                    backend's business.
//   CommandBuffer    Opaque; handed to the draw callback and to BeginBatch.
//
// Frame plumbing
//   Init()                     Install the present hook.
//   RegisterDrawCallback(fn)   fn(cmdBuf, dst, width, height) once per frame,
//                              with dst the colour buffer being presented.
//   OnInitialized(fn)          fn() once the pipeline can accept work; called
//                              immediately if it already can.
//
// Drawing - the hot path, and the only geometry the GUI ever submits
//   BeginBatch(dst)            Start recording against that colour buffer.
//   BatchQuad(tex, verts, blend)  Four vertices, triangle strip order
//                              (TL, TR, BL, BR), alpha-blended per `blend`.
//                              No depth, no culling, no scissor.
//   EndBatch()                 Flush.
//
// Textures
//   AllocTextureSurface(desc, &image, &imageSize)
//                              Allocate storage and return a handle; `image`
//                              is CPU-writable and `imageSize` must equal what
//                              the source data provides, which is how a wrong
//                              tiling guess is caught.
//   CreateTextureFromSurface(desc, data, size)   Allocate, copy, finalize.
//   CreateTexture(pixels, size, w, h, format)    Small untiled RGBA8 helper.
//   FinalizeTexture(handle)    Publish CPU-written texels to the GPU.
//   LoadTexture(path, maxSize) Read one texture file from the content mount.
//                              A mod's own art arrives this way.
//   GetTextureSize(handle, &w, &h)  Native pixel size, false if unknown.
//
// NOT here: general allocation. The GUI's own buffers - decompression
// scratch, parse tables, the quad record - are plain memory with no graphics
// requirement, and they go through BotW::Heap::Alloc (platform/heap.hpp). Only
// TEXTURE storage is the backend's business, and that is allocated inside
// AllocTextureSurface where the backend can apply its own pool and alignment
// rules. A backend therefore never sees a general allocation request.
//
// Backdrop blur - optional; a backend may return false/0 forever and the GUI
// degrades to no frosting with no other change.
//   BackdropReady()            Is a blurred capture available this frame?
//   BackdropTexture()          Handle to it, sampled in screen fractions.
//   BlurBackdrop(dst, downscale, passes)   Produce one for this frame.
//
// Constants the sprite and font tables need
//   kCompMapShapeFromG, kTileModeTiled2DThin1, kSurfaceFormatUnormR8G8B8A8
//   These describe the GAME's own art, which is why they are here rather than
//   hidden: a BC4 sheet whose shape lives in the green channel is a fact about
//   the file, and any backend has to express it somehow.

namespace WiiXLaunch::BotW::GUI::Backend {

// The GX2 implementation of the contract above: pure aliases, no wrappers, so
// this costs nothing and cannot drift from what gx2.hpp actually does.
//
// Unguarded, because gx2.hpp already carries a Switch stub for all of these
// and gui_types.hpp - the one GUI header that is not platform-guarded - needs
// BlendState and Blend:: on every target.
using TextureHandle = GX2::TextureHandle;
using BlendState    = GX2::BlendState;
using SurfaceDesc   = GX2::SurfaceDesc;
using TextureVertex = GX2::TextureVertex;
using CommandBuffer = GX2::CommandBuffer;

namespace Blend = GX2::Blend;

using GX2::Init;
using GX2::RegisterDrawCallback;
using GX2::OnInitialized;

using GX2::BeginBatch;
using GX2::BatchQuad;
using GX2::EndBatch;

using GX2::AllocTextureSurface;
using GX2::CreateTextureFromSurface;
using GX2::CreateTexture;
using GX2::FinalizeTexture;
using GX2::LoadTexture;
using GX2::GetTextureSize;

using GX2::BackdropReady;
using GX2::BackdropTexture;
using GX2::BlurBackdrop;

#if WIIXL_CEMU || WIIXL_WIIU
// The descriptors for the game's own art. Guarded because only the drawing
// targets define them, and every GUI file that needs them is behind the same
// guard. Every other name in this file has a Switch stub already.
constexpr uint32_t kCompMapShapeFromG           = GX2Types::kCompMapShapeFromG;
constexpr uint32_t kTileModeTiled2DThin1        = GX2Types::kTileModeTiled2DThin1;
constexpr uint32_t kSurfaceFormatUnormR8G8B8A8  = GX2Types::kSurfaceFormatUnormR8G8B8A8;
#endif

// On Switch there is no implementation yet and the GUI is stubbed out one
// level up, in gui.hpp. An NVN backend goes here, behind #if WIIXL_SWITCH,
// providing exactly the names above; the `#if` guards in gui.hpp,
// gui_render.hpp and gui_assets.hpp then widen to include it. Nothing else in
// the GUI should need to change - if it does, the seam was drawn in the wrong
// place and it is worth moving rather than working around.

} // namespace WiiXLaunch::BotW::GUI::Backend
