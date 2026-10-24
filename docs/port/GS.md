# The GS layer on the host

The game's own render layer (`ico2/seki/src`) runs as on the PS2 and builds its packets and display
lists in its own buffers. This document says what the port adds under it: where its register writes
become [rd](ARCHITECTURE.md#terms) state, what is decoded and what is only logged, and where the
game hands work to `port/render`. rd itself is [RENDER.md](RENDER.md).

## Files

| path | what it holds |
| --- | --- |
| `ico2/seki/src/GsBase.c` | the frame latch, the per-list GS defaults, the flip on rd, the cull against the blended cameras |
| `ico2/seki/include/GifHost.h` | what the decoder offers the rest of seki: the TEX0 resolver, flush, keys, A+D writes from the VU path |
| `ico2/seki/include/GifPacket.h` | the frame width and extended-state setters, the full-frame 2D scope |
| `ico2/seki/src/GifPacket.c` | the 2D helpers, the CPU strips' one-camera rule, the GS register decoder (`#ifdef ICO_RD`) |
| `ico2/seki/src/DisplayList.c` | the 13 list buffers, their host pads, `dl_Swap` as the frame's kick |
| `ico2/seki/src/DmaPacket.c` | the two packet banks, their host pad, fill and room checks |
| `ico2/seki/src/Texture.c` | the textures on rd's cache: `texHostTexture`, the TEX0 resolver |
| `ico2/seki/src/Packet.c` | one rd mesh per model packet; the title logo's strip order |
| `ico2/seki/src/RegistPacket.c` | the VU1 chains through the host VIF reader; mesh draws; packet cull |
| `ico2/seki/src/MicroCode.c` | the empty microprogram table; the host VIF and GIF reader `mc_HostDma` |
| `ico2/seki/src/Primitive.c` | grids and particles on rd's VU path; particle emitter keys |
| `ico2/seki/src/Shadow.c` | the shadow count recorded on rd |
| `port/null/gfx_null.c` | libgraph and libdma stubs under both builds |
| `port/render/rd.h` | the recording API the layer calls |

## The layer in both builds

Both the [headless build](ARCHITECTURE.md#terms) and the [window build](ARCHITECTURE.md#terms)
compile the whole layer, so the state the draw phase changes (fades, view matrices, heap allocations
made by the packet builders) is the same with or without a renderer; the headless build writes its
packets as on the PS2, and the window build sends the register writes to the decoder instead
([below](#the-register-decoder)). Under it, Sony's libgraph and libdma are stubs: a
[DMA](ARCHITECTURE.md#terms) kick does nothing, the [GS](ARCHITECTURE.md#terms) is always idle, and
`sceGsSyncV` keeps the [EE](ARCHITECTURE.md#terms)'s busy wait for the next
[vsync](ARCHITECTURE.md#terms) through the scheduler (`port/null/gfx_null.c` (`sceGsSyncV`,
`sceGsSyncPath`, `sceDmaSend`)). The DMA channel register blocks are plain writable memory because
`dma_init` sets `CHCR.TIE` in them (`port/null/gfx_null.c` (`sceDmaGetChan`)).

The window build defines `ICO_RD`. Under it the same functions also record into rd: register writes
go through the decoder in `GifPacket.c`, [VU1](ARCHITECTURE.md#terms) chains through the
[VIF](ARCHITECTURE.md#terms) reader in `MicroCode.c`, model packets become rd meshes, and the post
passes become `rd_post` calls (`ico2/seki/include/GifHost.h`). The frame latch, the cull and the CPU
strips' rule below are in every build, because they are game logic: a headless run culls and kicks
as a window run does (`ico2/seki/src/GsBase.c` (`gsbHostCullMargin`); `ico2/seki/src/GifPacket.c`
(`gifHostFrameW`)).

One frame in the window build: `dl_Clear` drops a frame never kicked and opens the next
(`rd_discard_frame`, `rd_begin_frame`); the game's `gif_*` helpers feed `gsWrite`, which turns
register writes into rd state and primitives in the list `dl_SetDLPriority` selected
(`rd_select_list`); model packets record `rd_draw_vu_mesh` through `RegistPacket.c`; the VU1 chains
go through `mc_HostDma`; `dl_Swap` flushes the decoder (`gif_HostFlush`) and ends the frame with
`rd_end_frame`, after which rd [replays](ARCHITECTURE.md#terms) lists 0..12 onto the
[RHI](ARCHITECTURE.md#terms) ([RENDER.md](RENDER.md#the-replay)) (`ico2/seki/src/DisplayList.c`
(`dl_Clear`, `dl_Swap`)).

## The native frame and the frame latch

`ScreenWidth` stays the PS2's 0x200 at every aspect. Each read of it is tagged in a comment as
one of SCALE (the projection's scale), UI (the 2D layer's 640 grid), ADDRESS (a VRAM address or
size), and every extent of the picture reads `gsFrame.w` instead, the
[native frame](ARCHITECTURE.md#terms)'s width, which equals `ScreenWidth` at 4:3
(`ico2/seki/src/GsBase.c` (`ScreenWidth`, `gsFrame`)). The width is 512 at 4:3 and at most
`ICO_FRAME_W_MAX` (2560) (`port/include/ico_frame.h`).

`gsbHostFrameLatch` sets `gsFrame` from the display options at `gsb_Init` and at each flip,
before the next frame opens, so one [tick](ARCHITECTURE.md#terms)'s matrices, cull, frame extents
and recorded frame share one width. It hands the width to `gif_HostSetFrameWidth`, the extended
state (`!ico_video_original()`) to `gif_HostSetExtended`, and in the window build calls
`rd_reset_scene` when the width or height changed, which remakes rd's scene-sized targets as the
50/60 Hz switch does (`ico2/seki/src/GsBase.c` (`gsbHostFrameLatch`)). `gif_HostExtended` is true
outside [out of the box](ARCHITECTURE.md#terms): when the latch said so or the frame is wider than
4:3 (`ico2/seki/src/GifPacket.c` (`gif_HostExtended`)).

## The GS defaults per list

`gsb_SetGsDefault` opens the frame's first [list](ARCHITECTURE.md#terms) and writes TEST, ZBUF, FBA
and [TEXA](ARCHITECTURE.md#terms) at the head of lists 0, 1, 2, 4, 6 to 12; lists 3 and 5 inherit
whatever the list before left (`ico2/seki/src/GsBase.c` (`gsb_SetGsDefault`, `gsb_setNormalReg`,
`gsb_setSemitransReg`, `gsb_setSpecularReg`, `gsb_setParticleReg`)):

| lists | TEST | Z write | FBA | TEXA |
| --- | --- | --- | --- | --- |
| 0, 7 to 12 (normal) | 0x50000 | on | 0 | 0x80 / 0x80 |
| 1, 2 (semitransparent) | 0x5140D | on | 0 | 0x7F / 0x81, AEM |
| 4 (specular) | 0x5C000 | off | 0 | 0x80 / 0x80 |
| 6 (particle) | 0x50000 | off | 0 | 0x80 / 0x80 |

rd applies the same values at `rd_begin_frame` through its ordinary state calls and nothing else,
so the leak into lists 3 and 5 is the GS's (`port/render/rd_core.c` (`recordDefaults`)). The
[list](ARCHITECTURE.md#terms) table and the leakage rules are in rd.h's header comment
(`port/render/rd.h`).

`gsb_MakeCommonMatrix` builds the VU1 parameter block (16 quadwords: constants, the
[GIF](ARCHITECTURE.md#terms) tag, world to GS screen, viewport, inverse view) and the frame camera,
and the window build hands both to rd: `rd_set_vu_common` and `rd_set_camera`
(`ico2/seki/src/GsBase.c` (`gsbHostCommon`)).

## The flip on rd

On the PS2 `gsb_UpdateGSSystem` runs the reduction of the previous frame, `sceGsSwapDBuff` (the
draw environment and its clear packet on GIF path 3), `sceGsSetHalfOffset`, then `dl_Swap` kicks
the lists. A frame's lists therefore draw over a clear to the BG colour current at the flip that
kicks them. The host records the head when the frame opens (`gsbHostFrameHead` → `rd_frame_head`:
the draw environment and the clear, at the head of lists 0 and 11) and patches the colour and the
half offset at the flip (`gsbHostFlip` → `rd_frame_flip`) (`ico2/seki/src/GsBase.c`
(`gsbHostFrameHead`, `gsbHostFlip`)). The field that decides the half offset is always 1: no half
pixel on any flip, as a progressive display shows it (`ico2/seki/src/GsBase.c` (`gsbHostField`)).

The reduction (SCENE into the half-height DISPLAY, tinted, border-cropped) is recorded as
`rd_post(RD_POST_REDUCTION)` at the end of list 12 of the frame `dl_Swap` is about to close
(`ico2/seki/src/GsBase.c` (`gsbHostReduction`)). The other full-screen passes keep their game code
and replace only the packet with an `rd_post` of the same writes: the fade in list 11
(`RD_POST_FADE`), the letterbox (`RD_POST_LETTERBOX`), the brightness step (`RD_POST_BRIGHTNESS`),
the screen softening in list 10 (`RD_POST_AA_DOWNSAMPLE` then `RD_POST_AA_COMPOSITE`) and the film
grain (`RD_POST_FILM_NOISE`, after one PRIM write that lets the decoder bind `sandstorm_spr`'s
[TEX0](ARCHITECTURE.md#terms)) (`ico2/seki/src/GsBase.c` (`gsb_fade`, `gsb_scissorOnDemo`,
`gsb_controlBrightness`, `gsb_antiAlias`, `gsb_filmNoise`)). How rd records them is
`port/render/rd_post.c`.

## The cull against the blended cameras

The cull's projections are built from the native frame's half-width, so parts at the sides of a
wide picture are registered; the screen matrix `+0xC0` and the gameplay matrices `+0x80` never
change with the width (`ico2/seki/src/GsBase.c` (`gsb_SetVSMatrixSub`)). With
[interpolation](ARCHITECTURE.md#terms) on, the presenter draws pictures between two ticks through a
blended camera, so `gsbHostCullMargin` widens the culled frustum by `RD_CULL_MARGIN` (0.12) and
`gsb_ClipBox` culls a box only when it is outside three cameras: this tick's, the tick before's
and the half-way one, each also swept along the eye's step (`ico2/seki/src/GsBase.c`
(`gsbHostCullMargin`, `gsbHostCullViews`, `gsbHostClipBoxViews`)). The blend functions are the
presenter's own (`port/render/rd_camera_blend.h`). With the Original framerate the matrices are
left exactly as computed and one camera culls.

`gsbHostCullViews` falls back to one camera for the first tick of a stage, after a cut, for a
still or singular camera and for a step past the presenter's limits. For a step of more than half
those limits it culls nothing for the tick, until the packet bank is past half the EE's size
(`dpk_HostFill`), logged once a stage (`ico2/seki/src/GsBase.c` (`gsbHostCullViews`,
`gsbHostKeepAll`)). The camera kept for the next tick is taken at the flip
(`gsbHostCullFlip`) and forgotten at `gsb_InitGSSystem` and `gsb_ResetGSSystem`
(`gsbHostCullForget`). Presentation itself is [DISPLAY.md](DISPLAY.md).

## The register decoder

In the window build `setGsReg` feeds `gsWrite` instead of the packet, so every `gif_*` helper
keeps its body. `GIF_ENTER` marks each entry point with the space its vertices are in and first
decodes the raw A+D pairs callers wrote straight into the open packet since the last entry, so the
two stay in order (`ico2/seki/src/GifPacket.c` (`GIF_ENTER`, `gifEnter`, `gsRawFlush`)). A+D
pairs that reach the GS by another path (a VU `SET_GSREGISTER`, a mesh batch tag's PRIM) arrive
through `gif_HostWriteRegs` and are decoded in order after what the open packet holds
(`ico2/seki/src/GifPacket.c` (`gif_HostWriteRegs`)).

| register | what the decoder does |
| --- | --- |
| PRIM | ABE, TME, IIP to rd (`rd_abe`, `rd_texture`/`rd_texture_off`, `rd_gouraud`); PRIM.AA1 on lines and triangles to `rd_aa1`, back to 0 when the packet ends; resets the vertex queue |
| RGBAQ, ST, UV | the current vertex attributes |
| XYZ2, XYZF2 | a vertex with a drawing kick; XYZ3 and XYZF3 advance the queue without one |
| [TEX0](ARCHITECTURE.md#terms)_1 | the texture seam: bound when PRIM.TME is on |
| TEX1_1, CLAMP_1 | `rd_sampler_tex1`, `rd_sampler_wrap` |
| ALPHA_1, TEST_1, ZBUF_1, FBA_1, PABE, TEXA, COLCLAMP | `rd_blend_func`, `rd_test_gs`, `rd_z_write`, `rd_fba`, `rd_pabe`, `rd_tex_a`, `rd_col_clamp` |
| FRAME_1, XYOFFSET_1, SCISSOR_1 | `rd_set_target` and `rd_scissor`, emitted before the next primitive or at the packet's end; FRAME.FBMSK to `rd_color_mask` |
| TEXFLUSH | nothing |
| 0x7E XYZ2_HI | host only: the high 16 bits of the next vertex's X and Y |

(`ico2/seki/src/GifPacket.c` (`gsWrite`, `gsApplyPrim`, `gsVertex`, `gsSyncEnv`)).

PRIM, TEX0, FRAME, XYOFFSET and SCISSOR are kept per list: lists are recorded in any order but
[replayed](ARCHITECTURE.md#terms) 0..12, and a list that skips a write (Texture.c skips TEX0 when
its last texture is the same) means the value its own list wrote. A list that never wrote TEX0
inherits the last one of any list (`ico2/seki/src/GifPacket.c` (`GsShim`, `gsTex0`)).

Consecutive primitives of one kind, space, UV mode and projection mark are batched into one
`rd_screen_prims` call, and any state change ends the batch first, so rd sees the writes in GS
order. Strips and fans become triangle lists here, which also gives XYZ3 its GS meaning
(`ico2/seki/src/GifPacket.c` (`gsEmit`, `gsFlushBatch`)). Raw writes and the raw-coordinate helpers
are tagged UI in list 12 and WORLD elsewhere; the 640-grid helpers are UI, the CPU-projected
strips WORLD, the full-frame scope FULLFRAME (`ico2/seki/src/GifPacket.c` (`gsSpace`)).

### Targets

FRAME.FBP selects a named rd target (`ico2/seki/src/GifPacket.c` (`gsTargetOfFbp`)):

| FBP | TBP | rd target |
| --- | --- | --- |
| 0x000 | 0x0000 | DISPLAY |
| 0x040 | 0x0800 | SCENE (with its depth) |
| 0x140 | 0x2800 | WORK0 when at most 128 high, else AA0 |
| 0x160 | 0x2C00 | AA1 when at most a quarter of the frame width wide, else WORK1 |
| 0x180 | 0x3000 | WORK2 |
| 0x1F8 | 0x3F00 | FEED128 |

Any other FBP is a render-to-texture block: it draws into a temporary target of the size XYOFFSET
gives, with its own depth, for the rest of the frame, and a TEX0 with that block's TBP samples it
(`ico2/seki/src/GifPacket.c` (`gsAliasTarget`, `gsSyncEnv`)). A TEX0 resolves through the
registered resolver first, then the named targets (TBP 0x2840 is SHADOW0; PSMCT24 reads take the
RGB24 view), then a frame's alias blocks, then a placeholder checker with one hue per TBP
(`ico2/seki/src/GifPacket.c` (`gsResolveTex0`, `gif_HostPlaceholder`)).

### Field widths

At 4:3 the decoder reads the GS's own field widths, so a stray high bit reads as on the GS. On a
frame wider than 4:3 SCISSOR's fields are read as 16 bits (the GS has 11) and UV's as 16 bits (the
GS has 14), and a strip vertex past XYZ2's 16 bits is sent as XYZ2_HI followed by the low halves.
The game-side writers that mask UV (`MicroCode.c`'s PACKED UV, `staticBlur.c`) take the same width
from `gif_HostFrameWidth` (`ico2/seki/src/GifPacket.c` (`gifInScreenXyz`, `gsSyncEnv`, `gsWrite`);
`ico2/seki/src/MicroCode.c` (`mcHostPacked`)).

### What is reported once

These are logged once with the value and otherwise handled as the second column says
(`ico2/seki/src/GifPacket.c` (`gsOnce`)):

| condition | handled as |
| --- | --- |
| FRAME.FBP that names no target and is not a block | drawn into SCENE |
| ALPHA_1 outside the twelve modes of `alphaTable` | ignored |
| TEXA outside the three modes | the nearest mode |
| TEX0.TFX HIGHLIGHT or HIGHLIGHT2 | MODULATE |
| PRIM.CTXT 1 (context 2) | context 1 |
| PRIM.FGE | no fog |
| PRMODECONT 0 | not decoded |
| CLAMP region modes | CLAMP |
| PRIM type 7 | ignored |
| DTHE 1 | not reproduced: no dithering |

Every other register written through the packet layer (the image transfers BITBLTBUF, TRXPOS,
TRXREG, TRXDIR; MIPTBP; TEX2; FOG; FOGCOL) is counted per register, its first write logged with the
list (`ico2/seki/src/GifPacket.c` (`gsUndecoded`, `gif_HostUndecodedCount`)). The effects that
need those registers record on rd directly (below).

### The decoder's host API

`gif_HostFlush` emits what the decoder still holds; `dl_SetDLPriority`, `dl_PopPriority` and
`dl_Swap` call it before the list changes. `gif_HostScreenPrims` puts primitives in order with the
decoder's own (DisplayFont.c's glyphs). `gif_HostDrawKey` and `gif_HostDrawKeyText` set the RdKey
the next 2D primitives carry, so the presenter can pair a layout row or a string across ticks.
`gif_HostFrameProjected` marks primitives projected on the CPU through the frame camera;
`gif_HostFrameProjectedSt` gives them STs that follow the screen, moved with each re-projected
vertex ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-ripples-between-ticks)).
`gif_HostFrameReset` forgets the frame's alias blocks from `gif_Init`, which `dl_Clear` calls
(`ico2/seki/include/GifHost.h`; `ico2/seki/src/GifPacket.c` (`gif_Init`)).

## CPU strips and the one-camera rule

`isInScreen` is the PS2's compare at every frame width: Z in 0..0x0FFFFFF0, X and Y in 0..0xFFF0,
the GS coordinate range, not the picture. A CPU-projected strip (`gif_DrawStripF`, `FST`, `G`,
`gif_Draw2DStripG`, `gif_Draw2DUVStripG`) kicks a triangle only when its three vertices pass, and
`gif_DrawPolyF4` draws its quad only when all four do (`ico2/seki/src/GifPacket.c` (`isInScreen`,
`gifStripKick`, `gif_DrawPolyF4`)).

`gifHostBeside` adds the port's triangles. With every Z in range: on a frame wider than 4:3, a
triangle wholly beside the camera's picture (X at or left of GS 1792, at or right of 2304) is drawn,
since the PS2 showed nothing there; outside out of the box, a triangle that reaches the picture is
drawn too, clipped by the GPU, unless its strip is a card (no triangle of the strip has all three
vertices passing, `gifStripPasses`). At 4:3 in the Original [preset](ARCHITECTURE.md#terms) the rule
is not applied and every packet is the PS2's (`ico2/seki/src/GifPacket.c` (`gifHostBeside`,
`gifStripPasses`, `gifStripXyz`)). The VU programs' form of the same rule is [the region
rule](ARCHITECTURE.md#terms), in [VU1_PROGRAMS.md](VU1_PROGRAMS.md).

## The 2D layer's scopes

The 640-grid helpers (`gif_Sprite*`, `gif_Point*`, `gif_Line*`) map x to the centred 4:3 box: grid x
times `ScreenWidth / 640`. Between `gif_FullFrameBegin` and `gif_FullFrameEnd` they map it to the
whole native frame, and the window build tags those primitives `RD_SPACE_FULLFRAME`. Only an item
that covers the frame by its layout data enters the scope (`ico2/common/src/layout_texture.c`);
scopes nest (`ico2/seki/include/GifPacket.h` (`gif_FullFrameBegin`); `ico2/seki/src/GifPacket.c`
(`gifUiW`)).

## Display lists and packet banks

Each of the 13 list buffers gets `DL_HOST_PAD` (8 KB) past the EE's size. Nothing checks the fill
on the EE; the host keeps tags that overflow in the pad, logs each new 1 KB past the EE's size,
and drops a tag (and its draw) only when the pad is full too, keeping room for `dl_Swap`'s chain
tag (`ico2/seki/src/DisplayList.c` (`dlHostFull`, `dl_Init`)).

`dl_Clear` is the frame boundary: it drops an open frame that was never kicked
(`rd_discard_frame`) and opens the next (`rd_begin_frame`). `dl_Swap` is the kick:
`rd_end_frame(fbKeep)` replays lists 0..12, or 11 and 12 when `fbKeep` is set (pause and memory
card screens, which start the PS2's DMA at list 11), and nothing is DMA'd
(`ico2/seki/src/DisplayList.c` (`dl_Clear`, `dl_Swap`)). `dl_SetDLPriority` and `dl_PopPriority`
select the rd list with `rd_select_list` (`ico2/seki/src/DisplayList.c` (`dl_SetDLPriority`)).

The two packet banks (`PacketBufferStruct`) get `DPK_HOST_PAD` (512 KB) past the EE's 512 KB each,
from the common [partition](ARCHITECTURE.md#terms)'s [host extra](ARCHITECTURE.md#terms). A wide
picture and the cull against the blended cameras register more parts than the PS2 did; at each
frame's end the bank's fill is logged once it passes the EE's size and again per 16 KB of new peak.
`dpk_HostRoom` refuses further objects once the fill is within `DPK_HOST_RESERVE` of the pad's end,
which `RegistPacket.c` asks before writing an object (`ico2/seki/src/DmaPacket.c`
(`dpkHostHighWater`, `dpk_HostFill`, `dpk_HostRoom`)).

## Textures

The VRAM bump allocator keeps running as bookkeeping (the TBPs `tex_setTexReg` writes), but no
texel reaches a VRAM: `tex_loadImage` decodes the record's images once into rd's texture cache,
keyed on (table index, content generation, `RDTEX_TEXA_REPLAY`). The generation is a per-slot load
serial times 8 plus the level drawn; a CLUT scroll that changes the CLUT bumps the serial
(`ico2/seki/src/Texture.c` (`texHostTexture`, `texHostClutCheck`)). A TEX1 that asks for
mipmapping decodes the TIM2's further levels too (`rdtex_store_levels`), and the characters'
colours decode from a recoloured CLUT copy (`ico_appearance_recolour`)
(`ico2/seki/src/Texture.c` (`texHostTexture`)).

`tex_Init` registers `texHostResolve` with `gif_HostSetTex0Resolver`. `tex_setTexReg` notes
(list, TEX0) → table index; the resolver looks the TEX0 up in its own list's notes first, then in
the other lists', and logs a TBP that names neither a texture nor a named target once
(`ico2/seki/src/Texture.c` (`tex_Init`, `texHostBind`, `texHostResolve`)). The UV scroll stays a
VU1 packet, read by the mesh path from the record (`ico2/seki/src/Texture.c`). Texture packs hook
in at `texHostPack` ([PACKS.md](PACKS.md)).

## Model packets

Each model packet is one rd mesh: `pac_hostBuild` collects the VIF UNPACK payloads of the packet
back to back (GIF tag, then vertices, per batch) and calls `rd_create_vu_mesh`, or a model pack's
replacement for the mesh hash, and stores the mesh id in the header's pad word `pad9C`
(`ico2/seki/src/Packet.c` (`pac_hostStream`, `pac_hostBuild`)). A morph part rewrites its stream
each tick through `pac_HostRefreshFor` (`ico2/seki/src/Packet.c`).

`pac_hostStripEntry` takes five strips of the title logo's letters in a fixed order of their own
entries; the disc's order folds them across their own outline, which the logo's additive blend
brightened. A strip is matched by model name, entry count and the exact index sequence; anything
else keeps the disc's order (`ico2/seki/src/Packet.c` (`pacHostStrips`, `pac_HostStripOrder`,
`pac_hostStripEntry`)). This runs in every build.

`RegistPacket.c` wraps `dl_OpenDma` so every DMA it chains for VU1 (matrix, light, material,
texture, dissolve, specular and reflection packets) also goes through `mc_HostDma`, in list order.
A model's vertex batches are drawn instead: `regHostMesh` records `rd_draw_vu_mesh` with the list's
VU state at that point, keyed by object, part, packet ordinal and pass
(`ico2/seki/src/RegistPacket.c` (`regHostDma`, `regKeyPart`)). Game logic, culling and the list
choices are unchanged. A packet of a part that passed its cull is never dropped on its own box: a
box that fails selects the clipping program (code 36) instead (`ico2/seki/src/RegistPacket.c`
(`reg_clipPacketBoundingBox`)).

## The VU1 side of the chains

`MicroCodeAddress[]` is all zeros on the host: nothing in this tree assembles `ico2/vusrc`, and the
renderer replaces the [microprograms](ARCHITECTURE.md#terms) with shaders. `mc_TransMicroCode` still
writes the tags and tells rd which program is resident in each list (`rd_vu_program`)
(`ico2/seki/src/MicroCode.c` (`MicroCodeAddress`, `mc_TransMicroCode`)).

`mc_HostDma` reads the chains the seki layer builds for path 1 (id 5: a call into `cnt` tags ending
in `ret`; id 2: a reference to VIF codes) and walks their VIF codes (`ico2/seki/src/MicroCode.c`
(`mc_HostDma`, `mcHostVif`)):

| VIF code | on the host |
| --- | --- |
| UNPACK V4-32 with FLG | the quadwords into the TOP staging buffer |
| MSCAL/MSCALF 0 | `SET_GSREGISTER`: the GIF packet at TOP to the decoder (`gif_HostWriteRegs`) |
| MSCAL/MSCALF other | `rd_vu_call`: `SET_UVOFFSET`, `SET_*_MATRIX`, `SET_*_LIGHT`, the BEGIN codes |
| MSCNT | a particle batch draws (`rd_draw_vu_particles`) |
| DIRECT, DIRECTHL | path 2: the GIF packets to the decoder (lightning.c) |

`mcHostGif` unpacks GIF PACKED and REGLIST tags into A+D pairs; a FRAME_1 with PSMCT24 passes with
`FBMSK |= 0xFF000000` (the GS keeps the alpha byte). Reported once: IMAGE data on path 2, a REGLIST
tag with PRE, a `SET_GSREGISTER` tag without EOP, an UNPACK other than V4-32 to TOP, a DIRECT past
its chain's end, a DMA tag other than `cnt`/`ret`/`end`, a FRAME_1 with a 16-bit PSM, and a VU batch
of another program chained as a small packet (`ico2/seki/src/MicroCode.c` (`mcHostGif`,
`mcHostAdPush`, `mcHostBatch`, `mcHostOnceLog`)).

## Primitives and shadows

`Primitive.c`'s chains also go through `mc_HostDma`: `prim_DispFan2D`'s fan reaches the decoder,
the grid and particle matrix packets the list's VU state, and a particle batch's MSCNT draws it. A
`Mesh3D` packet buffer is drawn whole by `primHostGrid` → `rd_draw_vu_grid`
(`ico2/seki/src/Primitive.c` (`primHostGrid`)). Each particle emitter's slot carries a count of the
inits at its address, which goes into the key's ordinal byte, so a new effect at a freed emitter's
address does not pair with the dead one's sprites (`ico2/seki/src/Primitive.c`
(`prim_HostParticleStamp`)). The particle program's x/y window stays the PS2's 1024..3071 at every
width (`ico2/seki/src/Primitive.c` (`prim_InitParticleByPartition`)).

`Shadow.c` still builds its packets, but nothing on the host reads them. `shadow_Reset` records
`rd_shadow_reset` on the frame's count target, the volumes record `rd_shadow_tris` (each triangle
counted +1 or -1 by its last vertex's colour, one call per object), and `shadow_Draw` records
`rd_shadow_resolve` and then the blur chain's writes and sprites into SHADOW0..2 and SCENE, so
their state leaks into the rest of list 3 as on the GS (`ico2/seki/src/Shadow.c`
(`shadowHostFlush`)). The stencil count is [RENDER.md](RENDER.md#shadows).

## Effects recorded on rd directly

Some files record their register writes as rd state themselves, because the decoder's generic
mapping cannot be right for them. These writes never pass the decoder, so they leave its per-list
PRIM, TEX0 and FRAME shadow behind (`ico2/seki/src/ZFog.c`,
`ico2/seki/src/Shadow.c`, header comments).

- **Fog**: `fog_DrawFog`'s two DIRECT packets become their register writes as rd state and the
  fog sprite as `rd_post(RD_POST_FOG)`; the Z copy and its PSMT4 byte copy fold into the fog
  index (`ico2/seki/src/ZFog.c`).
- **Blur, glow, depth of field, motion blur, aura, eye blur**: `staticBlur.c` renames its `gif_*`
  calls to `sbHost*`, which write the same registers as rd state and send every sprite as `rd_post`
  of the effect's kind, drawn in the GS integer arithmetic (`ico2/sugipon/src/staticBlur.c`;
  `port/render/rd_blur.c`).
- **Dark volumes**: `darkVolume.c`'s packets reach the decoder through `mc_HostDma`, and the
  scene-sized block at FBP 0x140 is bound as an `rd_block_target` aliased over AA0
  (`ico2/sugipon/src/darkVolume.c` (`dvHostBlockBegin`)).
- **Puddles, pools, the queen's barrier**: they draw into the VRAM block `tex_AllocVramAuto` hands
  out (TBP 0x2800) and sample it. `rd_gs_named_block`, `rd_block_target` and `rd_alias_target`
  give the block a target of its own size and depth, and `rd_push_camera` records the reflection
  camera of a mid-frame `gsb_SetVSMatrix` (`port/render/rd_water.c`).

`port/render/rd_post.c` records the GsBase post passes as the register writes and sprites the
original issues, so they leak state into the rest of their list exactly as the packets do. Its
header comment lists each kind's writes.
