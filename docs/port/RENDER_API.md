# Render API design

The renderer replaces the PS2's Graphics Synthesizer (GS) and the VU1 vertex
path with a native draw API, `rd`, over a thin hardware interface with
Vulkan and Direct3D 12 backends. It is not a GS emulator: there is no GS
memory, no image transfer and no context 2. The game's own graphics code in
`ico2/seki/` (and a handful of effect files elsewhere) records `rd` calls in
place of DMA packets, and `rd` reproduces the GS rules the game relies on.

Headers:

| header | what it is |
|---|---|
| `port/render/rd.h` | the draw API the game's seki layer and the port call |
| `port/render/rd_state.h` | the finite GS state the game uses, named |
| `port/render/rd_mesh.h` | the VU mesh interface (meshes, VU state, VU draws) |
| `port/render/rd_tex.h` | the texture cache |
| `port/render/rd_internal.h` | the renderer's internal records, constants and log-once ids |
| `port/rhi/rhi.h` | the backend interface |

`port/render/README.md` lists the source files. The shaders are described
in `docs/port/SHADERS.md`, the five VU1 programs in
`docs/port/VU1_PROGRAMS.md`, and the display options as a player sees them
in `docs/port/DISPLAY.md`. A change to `rd.h`, `rd_state.h` or `rhi.h` needs
a grep of every caller and a note here.

The window build (`ICO_HEADLESS=OFF`, the default) defines `ICO_RD` and
compiles the seki layer's host paths onto `rd`. The headless build
(`ICO_HEADLESS=ON`) compiles the original packet code and links no
renderer; its traces are the reference that the window build's game logic
must match.

## 1. Model: 13 lists, replayed in order

The PS2 game assembles 13 priority-ordered DMA chains per frame
(`ico2/seki/src/DisplayList.c`) and kicks them in order at `dl_Swap`. The
GS is one state machine: a register written by list *n* is still in force
when list *n*+1 starts. `gsb_SetGsDefault` (`GsBase.c`) writes defaults only
at the head of lists 0, 1, 2, 4, 6 and 7 to 12, so lists 3 (shadows) and 5
(dissolve) run in whatever state the previous list left. Submission order
inside a list is draw order.

`rd` keeps all of this. A frame is 13 ordered command lists recorded by the
game fiber and replayed in order against one draw state (`RdDrawState`).
There is no sort-by-material pass, because the game depends on state
leaking between lists and on draw order for its blending; a sorted renderer
would draw a different picture. Replay of a keep frame (`fbKeep`: the pause
and memory card screens) runs lists 11 and 12 only, over the retained
DISPLAY target.

The game's post passes are appended to specific lists: the shadow
composite to list 3, the fog to 4, depth of field and motion blur to 7,
the anti-alias pass to 10, fade, letterbox, brightness and keep to 11.
Lists 8 and 9 (shine levels 2 and 3) therefore draw after depth of field
and motion blur. `rd` does not reorder them.

Coordinates stay as the game produced them: GS 12.4 window coordinates
around the 2048,2048 XYOFFSET centre, GS Z, texel UVs or STQ. One conversion
in the shaders applies the GS sampling convention, the target's resolution
scale and the widescreen factor (`gs_xy_to_ndc`, SHADERS.md).

## 2. Backends

`port/rhi/rhi.h` is a small hardware interface with hand-written Vulkan
(`port/rhi/vk/`) and Direct3D 12 (`port/rhi/d3d12/`) backends. Shaders are
one HLSL source compiled at build time by DXC to SPIR-V and DXIL and
embedded as byte arrays; there is no runtime shader compiler.

The bind model is fixed: group 0 per-frame uniforms, group 1 per-draw
uniforms and storage (bones, the VU vertex stream), group 2 textures and
samplers. Uniform ranges are aligned to `RhiLimits.uniformAlign` (256 on
D3D12). The RHI has buffers (device, upload ring, readback), textures
(RGBA8 unorm and uint, R8, RGBA16F, D32F, D32F with stencil), samplers,
shaders, bind group layouts, transient bind groups, dynamic uniform offsets
(`RHI_BIND_UNIFORM_BUFFER_DYNAMIC`, `rhi_CmdSetBindGroupOffsets`: Vulkan
`UNIFORM_BUFFER_DYNAMIC`, D3D12 root CBVs; section 18), pipelines (dual-source
blend, stencil wrap), render passes with load operations, copies, barriers,
timestamps and readback. It has no compute, no input attachments, no
framebuffer feedback loops and no push constants: per-draw uniforms live in
the upload ring and are bound by offset. Anything that needs to read the target it writes (DATE,
exact blends, the effect sprites) copies the target first.

**Selection.** A Windows build links both backends;
`rhi_CreateBackend("vulkan" | "d3d12")` (`port/rhi/rhi_backend.c`, before
`rhi_Init`) picks the one the `rhi_*` calls go to. The window build takes
the name from `[video] backend` in `config.toml` (or `backend=` in
`ico-pc.ini`), else from the `ICO_RHI_BACKEND` environment variable, else
Vulkan. `rd_replay_tool` takes `--backend`. Linux builds have Vulkan only.

**Device selection and validation.** `ICO_VK_DEVICE` (an index or a
substring of the device name) and `ICO_D3D12_ADAPTER` (`warp`, an index or
a name substring) force an adapter. `ICO_VK_VALIDATION=1` and
`ICO_D3D12_DEBUG=1` turn on the validation layer and the D3D12 debug layer
(on by default in debug builds, `0` turns them off).

**D3D12.** The backend's design is in `port/rhi/d3d12/README.md`. Every
RGBA8 texture is created typeless and viewed typed, so the copy between
the UNORM and UINT views of the exact-blend path is a bit copy; integer
clears are rounded the way the Vulkan backend rounds them; a pipeline that
writes `SV_Target1` with blending off has its second output discarded. The
backend-neutral RHI test (`port/rhi/test/rhi_test_common.c`) runs the same
cells with the same expected values on both backends, and every `rd_*` GPU
test runs on D3D12 with `ICO_RHI_BACKEND=d3d12`. The D3D12 backend has no
performance counters, no timestamps and no mailbox present mode
(`rhi_GetStats` reads zero, `rhi_TimestampsSupported` is false): the
renderer's performance records then carry the CPU phases and its own counts
only. It has not yet been run on Windows hardware (docs/TODO.md).

**Present modes.** With vsync on, the Vulkan backend presents FIFO, or
MAILBOX when the window asked for it (`rhi_PreferMailbox`, which it does
with a frame rate other than `"original"`) and the surface offers it. With
vsync off it takes MAILBOX, else IMMEDIATE, else FIFO
(`vkr_PickPresentMode`). D3D12 uses sync interval 1, or 0 with tearing where
DXGI allows it. `window: present mode` in the log says which mode is in
use.

## 3. GS state the game uses

Counted with `grep -a` over `ico2/` (47 files hold EUC-JP bytes, which plain
grep treats as binary and skips). Register encodings are the raw values
written.

### ALPHA (blend)

`gif_SetAlpha(abe, mode, fix)` indexes the twelve-entry `alphaTable`
(`GifPacket.c`). Modes seen as literals, by call count: 5 (52), 4 (21),
2 (28), 7 (14), 0 (12), 1 (2), 6 (1). Material packets
(`pac_setMaterialPacket`) write 0x44, 0x48 and 0x42 with FIX 0x80.

| mode | ALPHA | equation | use |
|---|---|---|---|
| 0 | 0x68 | Cs·FIX + Cd | shadow accumulate, dissolve in, flare |
| 1 | 0x62 | Cd − Cs·FIX | dissolve out, aura decay |
| 2 | 0x64 | (Cs−Cd)·FIX + Cd | letterbox, motion blur, anti-alias, debug bars |
| 3 | 0x29 | Cd·FIX + Cs | disc data only |
| 4, 7 | 0x44 | (Cs−Cd)·As + Cd | default material, 2D |
| 5 | 0x48 | Cs·As + Cd | additive, specular and reflection with PABE |
| 6 | 0x42 | Cd − Cs·As | subtractive material |
| 8 | 0x58 | Cs·Ad + Cd | disc data only |
| 9 | 0x52 | Cd − Cs·Ad | disc data only |
| 10 | 0x54 | (Cs−Cd)·Ad + Cd | disc data only |
| 11 | 0x49 | Cd·As + Cd | disc data only |

Mode 3's table entry is {1, 2, 2, 0} (A Cd, B 0, C FIX, D Cs), register
0x29; the decoder (`gsAlphaRegs`) and `rd_pipeline.c` both use that value.

Two sites pass a variable mode. `lightning.c` passes `c`, the short at +0x2E
of the stage's BGA lightning record (`BgAnimation.c`): disc data, so any of
the twelve modes is possible, and all twelve pipelines exist. On the PS2 a
value outside 0..11 indexes past the table; the host path draws mode 0 there
and reports the value once (`lightningHostMode`). `enemyParts.c` passes
`PointBlur.alpha`, initialised to 5 from `pointBlurTemplate` and never
written again.

FIX literals: 0, 0x10, 0x20, 0x40, 0x60, 0x70, 96, 0x80, 255; runtime
values come from `motionBlurAlpha`, the shadow blend levels, `blurCol` and
debug code.

### TEST and the other registers

Fourteen literal TEST values (`rd_state.h` names each). Decomposed, the game
uses alpha test NEVER, ALWAYS, LESS and GREATER with AREF 0, 0x40 and 0x81;
AFAIL KEEP, FB_ONLY and RGB_ONLY; DATE off, DATM 0 and 1; Z test ALWAYS and
GEQUAL. ZTE is always set except for one `0` write, which `rd` treats as Z
ALWAYS with write enabled (the GS manual calls ZTE 0 a prohibited setting).

| register | values seen | notes |
|---|---|---|
| ZBUF | mask on / off | ZBP 0xC0 PSMZ32 for the scene; temporary targets use their own ZBP |
| TEXA | 80/80, 7F/81+AEM, 80/80+AEM | three modes, applied at replay |
| CLAMP | 0, 1, 4, 5 | materials map `wrap` 0..3 to 5, 4, 1, 0 |
| TEX1 | 0x60 (28), 0x40 (3), 0x20 (1), 0 | nearest or linear per axis; mip level fixed per texture |
| TEX0 TFX | MODULATE, DECAL (HIGHLIGHT in `staticBlur.c`) | plus TCC RGB or RGBA |
| PABE | 0 (9), 1 (1) | specular and reflection passes |
| FBA | 0 (raw), per material | the shadow receiver mask |
| COLCLAMP | 1 (4), 0 (3) | 0 in `Shadow.c` and `darkVolume.c` |
| DTHE, DIMX | never written by the game | the flip writes DTHE 0: no dithering |
| FRAME psm | PSMCT32, PSMCT24 in `darkVolume.c` | no 16-bit framebuffer |
| PRIM.AA1 | `lineManager.c`'s 0x189 and 0x18A (lines, ABE 0: the storm's streaks, `stormTest.c`), `gif_DrawStripFST`'s 0xD4 (strips, ABE 1: the puddle's ripples, `puddle.c`), the particles' GIF tag 0xD6 (sprites) | decoded since package AA1 ("PRIM.AA1" below); no effect on sprites |

## 4. GS to pipeline mapping

| GS feature | native implementation |
|---|---|
| Alpha scale 0x80 = 1.0 | Dual-source blend: output 0 carries the colour and the stored GS alpha, output 1 the factor As/128 or FIX/128. Required on every backend (Vulkan `dualSrcBlend`). Exact only while the factor is at most 1.0: UNORM blenders clamp fixed-point factors to 0..1 (measured on llvmpipe: `Cs·As + Cd` with As 0xFF gives 128 where the GS gives 191; the Vulkan specification clamps the same way). |
| Factor above 0x80 in `Cs·F + Cd`, `Cd − Cs·F` (modes 0, 1, 5, 6) | `DF_PREMUL`: the shader writes the GS term `min((Cs·F) >> 7, 255)` and the pipeline adds it (ONE, ONE) or reverse-subtracts it. Exact for F up to 0xFF. Used for these four modes always, since As can always reach 0xFF. |
| Factor above 0x80 in a LERP (modes 2, 4, 7) | Not representable (the GS weight on Cd goes negative). FIX is clamped to 0x80 (the draw writes Cs); the hardware clamps As the same way. These are ordinary draws into SCENE, not feedback passes. |
| `Cd·FIX + Cs` (mode 3) | Destination factor `SRC1_COLOR`; FIX above 0x80 clamped to 0x80. |
| Ad factors (modes 8 to 10) | `RHI_BF_DST_ALPHA`, which reads Ad/255 rather than Ad/128: half strength. Disc data only and not known to be used. |
| `Cd·As + Cd` (mode 11) | Needs a factor above 1.0 on Cd; the draw leaves Cd unchanged and logs once. |
| AFAIL FB_ONLY (0x5140D) | Two draws: alpha above the reference with depth write, then the rest with depth write off. Self-overlap inside one strip can order differently. |
| AFAIL RGB_ONLY with ATST NEVER | Colour mask RGB, depth write off, no alpha test. |
| DATE | A screen draw with TEST.DATE first takes an R8 snapshot of its target's alpha MSB (`date_snap_ps`, `dateSnapshot` in `rd_replay.c`); `sprite_ps` reads it and discards where the MSB differs from DATM. The snapshot is retaken when the target changes or anything since may have written alpha, so consecutive DATE draws see each other's writes as on the GS; overlapping primitives inside one draw see the snapshot. |
| COLCLAMP 0 | Shadow volumes: a stencil count (section 10). Other draws: the wrap path (section 14). |
| FBA | The fragment shader forces the alpha MSB. |
| PRIM.AA1 (lines and triangles) | The AA1 key bit: `sprite_aa1_*_vs` / `sprite_aa1_ps` over the edge geometry `rd_replay.c` adds, the coverage as the fragment's alpha ("PRIM.AA1" below). |
| STQ (Q != 1) | A textured triangle command (list, strip or fan, not `uvFixed`) whose vertices all have Q above 0 and some Q other than 1 (the lightning's strips: Q = 1 / w) draws with `sprite_stq_*_vs` / `sprite_stq_ps` over `IcoSpriteStqVertex` (24 bytes: the sprite vertex and Q; S and T are not divided). The vertex shader passes (s, t, q) without perspective and the pixel shader divides, as the GS interpolates S, T and Q linearly on the screen and divides per pixel. Colour and Z stay as for every prim. Every other command (UV mode, sprites, lines, an R8 texture, PRIM.AA1, a vertex with Q at or below 0) keeps `sprite_ps` and the per-vertex divide; `sprite_ps` is unchanged. A bolt that recedes in depth now maps its texture perspective-correctly (DIVERGENCES.md V-STQ). |
| PABE | The fragment shader sets the blend factor to 0 where the As MSB is clear. |
| Z | D32F (D32F with stencil on SCENE). The shaders map GS Z to depth `(zmax − z + 1) · scale` (`gs_z_to_depth`), the same value as `1 − z · scale` computed without cancellation, so a larger GS Z is a smaller depth: GS GEQUAL becomes `RHI_CMP_LEQUAL`, GREATER becomes `LESS`. The scale is per depth target: 2^-32 for the game's PSMZ32 (every ZBUF it writes), 2^-24 and 2^-16 for the other formats. The UI's Z values 0xFFFFFF9B and 0xFFFFFFFF stay distinct. |
| Texture function | Integer `min((tex·col) >> 7, 255)` in the shader. |
| Vertex colour | Truncated as the VU's `ftoi` and clamped at 255. |
| Feedback passes | Evaluated in the GS's integer arithmetic in the shader against a copy of the destination (`fx_sprite_ps`, section 13). A general exact path (`blend_int`, `RDC_EXACT_BLEND`: source and destination copied into RGBA8_UINT textures, the result copied back) exists for `rd_Post` passes with `exactInt`; the game records none. |

**Pipeline keys.** `RdPipelineKey` holds the program, the blend path, the
AFAIL split's Z write and colour mask, DATE, the Z test and write, the
colour mask, the stencil mode, the target format and the topology. AREF,
FIX, the texture function, TCC, TEXA, FBA, PABE and the alpha test are
uniforms, and the sampler state is a sampler object, so the key normalises
those fields and stores one representative blend mode per hardware path.
The internal key (`RdPipeKeyInt`) adds the vertex and fragment shaders and
the attachment formats; a screen-prim draw whose bound texture is R8
(section 8) has `RD_FS_FONT` (`font_ps`) where the others have
`RD_FS_SPRITE`, chosen by `rd__PlanScreenDraw` from the texture record's
format, so text and sprites under the same state are two keys. The
reachable set has the font's keys for the text in the frame (TEST
0x30000, no Z write, ALPHA 0x44 or 0x48, with and without the depth
target) and on the overlay. Under the dark volume's FBMSK the 2D draws of
list 11 on a frame without the anti-alias pass (and of any later list-11
pass, until the next FRAME write) draw with colour mask 7: the UI keys and
the font keys are enumerated with mask 7 as well as 0xF (`rd_pixel` checks
them), as are the STQ keys of the WORLD triangle passes.
`rd__EnumerateReachable` lists every key the game's state set can reach
(the screen and post programs, the VU program families, shadows, fog,
water and the effect sprites); the tests hold it under
`RD_PIPELINE_REACHABLE_MAX` (256) and check that every pipeline they create
is enumerated. `rd_PrecreatePipelines`, called after `rd_Init`, compiles the
whole set at start-up so that no replay compiles a pipeline (on a GPU
driver without a warm cache a compile takes tens of milliseconds, a
visible hitch).

The runtime cache holds `RD_PIPELINE_CACHE_MAX` (1024) keys and is looked
up by hash. There is no assert on it (the build never defines `NDEBUG`, so
an assert would abort a release build): a key past it is logged once and
its draws are skipped. A key whose `rhi_CreatePipeline` failed is
remembered (`RD_PIPELINE_FAIL_MAX`, 64) and not retried. Both reset with
the cache.

### PRIM.AA1

The GS's edge antialiasing. `GifPacket.c` records PRIM bit 7 as `rd_AA1`
(`RDC_AA1`, `RdStateBlock.aa1`) for the line and triangle types (1 to 5)
and returns it to 0 at the end of the packet, so the bit never leaks into
another list; points and sprites record nothing. The model is the one
PCSX2's software renderer implements (`pcsx2/GS/Renderers/SW/GSRasterizer.cpp`
`DrawEdgeLine`, `DrawEdgeTriangle`; `GSDrawScanline.cpp` `CDrawEdge` and the
`sel.aa1` block; `GSState.cpp` `IsCoverageAlpha`, read from PCSX2's master
branch in October 2026), whose comments cite hardware tests:

| | PCSX2's software renderer | rd |
|---|---|---|
| which primitives | lines and triangles (`IsCoverageAlpha`); points and sprites draw as without AA1 | the same (`aa1Prim` in `rd_replay.c`; the decoder records nothing for sprites) |
| line | all edge: per step along the major axis the two pixels nearest the line on the minor axis, coverage 1 − d and d (d the minor-axis distance of the nearer one) | the line widened by one GS pixel on each side of its minor axis, coverage interpolated from 1 on the line to 0 at the sides: 1 − d at every sample point |
| triangle | its interior as without AA1; per edge, one pixel per major-axis step just outside it, coverage 1 − d, inside the other two edges' half-planes | the interior as without AA1; per edge a band one GS pixel deep outside it along its minor axis, over the edge's major-axis extent, coverage 1 − d |
| strips and fans | every triangle on its own: shared edges get fringes too | the same (each triangle of the expanded list) |
| edge pixel attributes | the edge's interpolated colour, texture coordinates and Z | the edge's: the band's far vertices copy its near ones |
| alpha | a = coverage (16 bits) >> 9 (0..0x7F) on an edge pixel, 0x80 inside; with ABE 0 a replaces the alpha of every pixel, with ABE 1 only an alpha of exactly 0x80; before the alpha test; that alpha is the blend's As and the alpha written | the same (`sprite_aa1_ps`, `ICO_DF_AA1_FULL` for ABE 0) |
| blending | on with AA1 whatever ABE says (`sel.abe \|\| sel.aa1`), the ALPHA register's equation | the same (`rd__PlanScreenDrawEx`) |
| Z | edge pixels write none (`CDrawEdge` clears `zwrite`); so a line writes none | lines: Z write off in the key; triangles: the interiors drawn with the state's Z write, then the fringes without (two draws, so a strip's fringes land after all its interiors when Z is written; in one draw, in the GS's order, when it is not) |

Not settled, and kept simple: the ends of a line (PCSX2 applies the
diamond-exit rule to the first and last pixel; rd's band ends on the
end points' minor-axis lines), a fringe pixel at a triangle's corner (PCSX2
keeps it inside the other two edges' half-planes; rd's band is a
parallelogram over the edge's extent, so at a sharp corner it can reach
past them or leave a gap), a sample point exactly on an edge (PCSX2 picks
the pixel by the top-left rule with coverage 0 or 1; rd's rasteriser gives
it to the triangle or the band by its fill rule), the order of fringes and
interiors noted above, and everything only a PS2 capture can settle (the
GS's own coverage precision). A FIX blend (modes 0 to 3) does not read As,
so the ripples of every stage but 0x22 (34; the others use mode 0, FIX 0x60) change only by
their fringes; the storm's lines (mode 5, Cs·As + Cd, with ABE 0) went from
opaque to additive. DIVERGENCES.md V-AA1 records the change.

Not implemented: AA1 under COLCLAMP 0 (the wrap path, section 14) draws
without coverage and is reported once; no game draw combines them.

## 5. Blend exactness under feedback

`port/test/gs_blend_test.c` (600 iterations, 256 lanes) compares the GS
integer blend against a float blender that rounds to nearest:

| case | max LSB | final LSB | frames differing |
|---|---|---|---|
| single LERP_AS pass | 1 | 1 | 600/600 |
| motion blur FIX 0x40, static / cuts / noise | 1 / 1 / 2 | 1 / 1 / 2 | 600 / 365 / 600 |
| motion blur FIX 0x70, static / cuts | 4 / 4 | 4 / 4 | 600 / 600 |
| aura in 0x20 decay 0x10; in 0x40 decay 0x08 | 2 / 1 | 2 / 1 | 600 / 600 |
| shadow count wrap, COLCLAMP 0 | 0 | 0 | 0/600 |

The error does not grow without bound but it does not vanish either: the GS
shift truncates toward −∞, so a high-retention LERP stalls one LSB short of
its target where a float blender converges, and the two settle at different
fixed points. At FIX 0x70 that is a steady 4-LSB bias in every
motion-blurred frame, a visible brightness offset. This is why the feedback
passes (motion blur, aura, flare and depth-of-field chains) are evaluated
in the GS's integer arithmetic, and why single-pass draws, which use the
hardware blender, are compared with a 1-LSB tolerance. The exact shadow row
supports replacing the wrapped colour count with a stencil count.

## 6. Targets

| `RdTargetId` | GS location | GS size | notes |
|---|---|---|---|
| SCENE | FBP 0x40 (TBP 0x800) | 512×512 PAL, 512×448 NTSC | RGBA8 and D32F with stencil |
| DISPLAY | FBP 0 | 512×256 / 512×224 | the displayed buffer; retained (motion blur history, keep frames) |
| SHADOW0..2 | TBP 0x3840, 0x3C40, 0x3D40 | 256², 128², 64² | the shadow blur levels 1 to 3 |
| WORK0 | TBP 0x2800 | 256×128 | flare, depth of field |
| WORK1 | TBP 0x2A00 | 256² | depth of field (its top half is the 256×128 view) |
| WORK2 | TBP 0x2E00 | scene-sized | the flare mask |
| WORK3 | TBP 0x3000 | 256×128 | the eye blur |
| AURA_WORK, AURA_TAP, WORK2_PAD | TBP 0x2A00 (TBW 8), 0x2800 (TBW 2), after WORK2 | scene, 128², 256×64 | the aura buffers; WORK2_PAD is written and never read |
| AA0, AA1 | TBP 0x2800, 0x2C00 | 256², 128² | the anti-alias chain |
| FEED128 | TBP 0x3F00 | 128² | the aura feedback, kept between frames |
| DATE_SNAPSHOT | none | scene size | R8 |

On the PS2 several of these share VRAM. They are separate targets on `rd`
because each is written whole before it is read in the frame, so sharing
changes nothing visible (checked per effect: the flare's and the aura's
views of 0x2800 and 0x2A00, WORK3 inside WORK2's and AURA_WORK's regions).

`rd_TempTarget` provides per-frame targets of any size (the shadow count,
the water reflections, render-to-texture blocks). They come from a pool
(`rd__TempTargetAlloc`): a freed target is parked with its textures, up to
`RD_TEMP_PARKED` (24), and the next request of the same texture size,
format and depth takes it over; a target taken from the pool is cleared to
zero at the next replay, which is what a new texture holds, so pixels do
not depend on the pool. The pool exists because a fresh scene-sized target
every frame cost a 16 MB device allocation and free per frame at 4x full
height, which on Windows goes through the video memory manager.

## 7. The seki layer on rd

**Lists and frames.** `dl_SetDLPriority` and `dl_PopPriority` select the rd
list (after flushing the decoder into the list being left). `dl_Swap`
closes the frame with `rd_EndFrame(fbKeep)`, then `dl_Clear` opens the next
(`rd_BeginFrame`); a `dl_Clear` without a `dl_Swap` (the
`gsb_UpdateGSSystem(1)` movie path) drops the open frame unreplayed
(`rd_DiscardFrame`). `PacketBufferStruct`, the 13 DMA lists and their tags
are still built, so heap use and the bookkeeping other code reads are
unchanged; nothing is DMA'd.

**The GS register decoder is permanent.** Every register write of the 2D
layer reaches one decoder in `GifPacket.c`'s host path, in packet order:
the `gif_*` helpers (their bodies are unchanged; `setGsReg` feeds the
decoder), `gif_SetGsReg`, the A+D pairs other files write straight into
the open packet (`Texture.c`'s TEX0 packet, `GsBase.c`'s macros, decoded at
the next `gif_*` entry or at the end of the packet), and the GIF packets
`mc_HostDma` and `gif_HostWriteRegs` hand it from the VU1 chains. It
decodes PRIM (ABE, TME, IIP, FST, the vertex queue), RGBAQ, ST, UV,
XYZ2/XYZF2 (kick), XYZ3/XYZF3, TEX0, TEX1, CLAMP, ALPHA, TEST, ZBUF, FBA,
PABE, TEXA, COLCLAMP, FRAME (FBP to a named target, FBMSK to
`rd_ColorMask`, at the FRAME write itself), XYOFFSET (which gives the target size), SCISSOR, and the
no-ops TEXFLUSH, PRMODECONT 1 and DTHE 0. PRIM, TEX0, FRAME, XYOFFSET and
SCISSOR are kept per list, since lists are recorded in any order but
replayed 0 to 12. Primitives of one kind, space and UV mode are batched
into one `rd_ScreenPrims`; strips and fans become triangle lists.

It works at the level of the game's own functions, so it is not a GS
emulator, and converting its users to direct `rd_*` calls would duplicate
it file by file. `darkVolume.c`'s spheres, `lineManager.c` and the debug
font (`debug.c`'s `debugHostFontGlyph` runs the font's VU1 routine on the
glyph packets and writes the points it would kick) reach `rd` through it. A
file that needs what the decoder cannot know supplies it with an explicit
host call (`darkVolume.c`, section 14). A register the decoder does not
decode is logged once by name and counted (`gif_HostUndecodedCount`,
`gif_HostUndecodedTotal`); in game runs the counters stay at zero. Unusual
values (an unknown FBP, ALPHA outside the twelve modes, PRIM.CTXT, FGE and
similar) are logged once each.

| FBP / TBP | rd target |
|---|---|
| 0 / 0 | DISPLAY (TEX0 PSMCT24: the RGB24 view) |
| 0x40 / 0x800 | SCENE (with its depth) |
| 0x140 / 0x2800 | WORK0 when 128 lines high, else AA0 |
| 0x160 / 0x2C00 | AA1 when 128 wide, else WORK1 |
| 0x180 / 0x3000 | WORK2 |
| 0x1F8 / 0x3F00 | FEED128 |
| other FBP | an `rd_TempTarget` of the size XYOFFSET gives, with its own depth, for the rest of the frame; a TEX0 whose TBP is that block's samples it. This includes 0x142, the shadow count's buffer, which never reaches the decoder: `Shadow.c` records its passes on `rd` directly (section 11) |

What the decoder does not see is state recorded by direct `rd` calls
(`rd_Post`, `Shadow.c`, `ZFog.c`, `staticBlur.c`): its per-list register
shadow then lags behind the GS's. Every game path writes PRIM and TEX0
before drawing, so this only matters for code that relied on a leak.

**Space tags.** The 640×224 layout helpers (`gif_Sprite`,
`gif_SpriteSensitive`, the `Offset` forms, `gif_Point*`, `gif_Line*`) draw
`RD_SPACE_UI`; the CPU-projected strips (`gif_DrawStrip*`,
`gif_DrawPolyF4`) draw `RD_SPACE_WORLD`. Raw register writes and the
raw-coordinate helpers are UI in list 12 and WORLD elsewhere: list 11's raw
writers are world-projected (water drops, the insect net, light volumes,
debug lines), list 12's are 2D (the debug font and bars, the memory bar).
`RD_SPACE_FULLSCREEN` marks draws that cover the whole screen. The tags
matter for widescreen (section 15) and the mirror (section 17).

**Font.** `font_Print` sets its state on rd directly, lets the decoder bind
the font's TEX0 with one PRIM write, and draws all glyphs as one
`rd_ScreenPrims` of sprites in UI space.

## 8. Textures

`port/render/rd_tex.c` is the cache; `ico2/seki/src/Texture.c` is the game
side.

**Decode.** At load (`tex_initTextureSub`) and on any later cache miss, from
the copies of the TIM2 images and CLUTs that `Texture.c` already keeps per
record. Nothing is converted at extraction time. Formats: PSMCT32, PSMCT24,
PSMCT16, PSMCT16S, PSMT8 and PSMT4 (low nibble first) from TIM2, and in the
decoder also PSMT8H, PSMT4HL, PSMT4HH and the PSMZ formats. CLUTs are
PSMCT32 or PSMCT16 (24-bit TIM2 CLUTs as RGB24), read in CSM1 order (a
256-entry CLUT swaps entries 8 to 15 and 16 to 23 of every 32). 16-bit
texels expand as `c << 3` (the GS does not replicate the high bits). The
texture is padded with zero texels to 2^TW × 2^TH, the size the GS
addresses, so STQ coordinates and REPEAT wrap where they do on the GS.

**TEXA is applied at replay, not baked.** PSMCT16/24 and 16-bit-CLUT
textures are created with `rd_CreateTextureSrc` and `sprite_ps` applies the
TEXA in force when the draw replays (`gs_texa_alpha`). TEXA leaks between
lists like every other register (lists 1 and 2 default to 7F/81+AEM, the
others to 80/80, and the 2D layer writes 80/80+AEM mid-list), so a baked
texture would need one copy per mode and a guess at which was in force. The
shader expands TEXA after the sampler has filtered, where the GS expands
before; with AEM or two TA values, bilinear edges between texels of
different alpha can differ (docs/TODO.md). The shadow chain avoids this by
baking (section 10).

**Cache.** The key is (texture id, content generation, TEXA mode).
`Texture.c` uses the table index as the id and `serial * 8 + TexExt.level`
as the generation; the serial changes at every load into a slot, at a CLUT
scroll that changed the CLUT, and at a `tex_Tool` CLUT reset. A new
generation of the same size and format is re-expanded into the same texture
(`rd_UpdateTexture`, which does nothing if the texels are unchanged);
otherwise a new texture is created and the old one destroyed two frames
later, so no recorded frame loses a texture it draws. Replacing the pixels
for every draw of the frame being recorded matches the PS2 for the CLUT
scroll: `tex_ResetVram` runs the animation just before `dl_Swap`, and the
DMA chain the swap kicks reads the CLUT at that point.

**Binding.** `tex_TransTexture(id, pri)` keeps its PS2 logic (the per-list
`transDone` and `lastTex` checks, the VRAM bump allocator as bookkeeping).
Where the PS2 chained the record's own TEX1/TEST packet, the host writes
TEX1 and TEST into `tex_setTexReg`'s packet ahead of TEX0, so the decoder
turns them into `rd_SamplerFilter` and `rd_TestGs` in packet order, only
when the PS2 sent them. `tex_Init` registers a TEX0 resolver
(`gif_HostSetTex0Resolver`): `tex_setTexReg` notes the TEX0 it wrote per
list (16 notes, most recent first), since the bump allocator hands out TBPs
per list, and the resolver looks a TEX0 up in its list's notes and then in
the other lists'. Anything else falls through to the named targets and then
to a placeholder (a 16×16 magenta-leaning checker), each such TBP logged
once.

**UV scroll.** `t->uv` stays a VU1 packet: the PS2 applies it in the mesh
programs only, never to GIF sprites.

**Mips.** Original: one level per texture (`TexExt.level`), decoded at its
own size. With the Enhanced trilinear or anisotropic filter, section 15.

**R8 textures and rectangle updates** (package R8). Besides the RGBA8
images above, `rd_CreateTextureR8(w, h, cov, name)` makes a one-channel
texture of `w × h` bytes, the port's font atlas pages (UI.md "Coordinates
and metrics"). A byte is the coverage in GS alpha units (0x80 full) and
stands for a white texel with that alpha: screen prims and overlay prims
that sample it are drawn by `font_ps`, whose texture function, TCC, alpha
test, DATE and output are `sprite_ps`'s (SHADERS.md), so the pixels are
those of the same texels as RGBA8 `(255, 255, 255, cov)`, filtered or not
(the sampler filters the same UNORM8 values), at a quarter of the memory.
An R8 texture never gets a mip chain. The texture record (`RdTexRec`)
holds the format (`RD_TEXEL_RGBA8`, `RD_TEXEL_R8`) and a CPU copy of
`w × h` texels of it.

`rd_UpdateTextureRect(t, x, y, w, h, px)` replaces a rectangle of an image
texture (rows of `w` texels in the texture's format, clipped to the
texture); an update that changes nothing is dropped, as `rd_UpdateTexture`'s
is. Each record keeps the union of the rectangles changed since its last
upload, and `uploadTextures` (`rd_replay.c`) copies that rectangle alone
through the ring with `rhi_CmdCopyBufferToTexture`'s region, in the
record's format; a create, `rd_UpdateTexture`, a new RHI texture (the
filter option changed) or a mip chain uploads the whole texture. The font
creates a page whole when it is first drawn and after that uploads each
new glyph's cell alone.

## 9. Frame lifecycle, camera and the post passes

`ico2/seki/src/GsBase.c` (host path), `port/render/rd_frame.c`, `rd_post.c`,
`rd_present.c`.

**What a flip does on the PS2.** `scheduler()` (`common/src/main.c`) calls,
every `systemStatus[1]` vsyncs (2 on PAL: 25 Hz), `gsb_SyncGSSystem`
(`gsb_PostEffect` appends the post passes) and then
`gsb_UpdateGSSystem(0)`, which reads FIELD from `GS_CSR`; reduces SCENE, as
the lists kicked at the previous flip left it, into DISPLAY
(`gsb_Reduction`); swaps buffers, sending the draw environment and its
clear (FRAME 0x40, ZBUF 0xC0 PSMZ32, COLCLAMP 1, DTHE 0, then a full-scene
sprite in the background colour at Z 0); sets the half-line offset of that
environment for the flip after next (`sceGsSetHalfOffset`); and finally
kicks the 13 lists recorded since the previous flip (`dl_Swap`).

**On rd.** `rd_FrameHead` records the draw environment (FRAME with FBMSK 0
first) and the clear when the frame opens; `rd_FrameFlip` rewrites the clear colour and the half
offset in place at the flip. A frame's lists therefore draw over a clear to
the background colour current at the flip that kicks them, as on the PS2.
The head is recorded at the head of list 0 and of list 11; replay keeps the
copy in the first list it replays (11 for a keep frame) and turns the other
into no-ops, so a keep frame is cleared and a full frame's list 11 runs in
the state list 10 left. `rd_EndFrame` replays and presents; the host shows
a frame one flip earlier than the PS2, which reduces it at the next flip.

**Keep frames.** `fbKeep` is read at `dl_Swap` (replay lists 11 and 12 over
the retained DISPLAY), by `gsb_Reduction` (tint 128) and by
`gsb_PostEffect` (`gsb_KeepFrameBuffer` draws DISPLAY back into SCENE at
112/128). A kept image therefore darkens by 112/128 per frame unless
something redraws it, as on the PS2.

**Field parity.** The PS2 samples FIELD every second vsync. FIELD toggles
every vsync in the host model (`port/platform/host_loop.c`; the game calls
`sceGsResetGraph` non-interlaced at frame step 2), so in steady state every
flip sees the same FIELD and both draw environments carry the same half
offset. An odd-length gap (a skipped flip) changes the parity from then on.
`gsbHostField` reads the host's `GS_CSR` (the game's `odd_even` stays 0, so
game state is unchanged), the two draw environments each keep their half
offset, and the flip sends the one decided two flips earlier. The offset is
`RD_TARGET_HALF_Y` in the head's `rd_SetTarget` (half a line); later
`gif_SetDrawEnviroment` calls write XYOFFSET without it, as on the GS. The
host never skips a flip, so the parity is fixed for a session segment by
the vsync the scheduler's step lands on after boot or a stage load.

**Dithering.** Every flip writes DTHE 0 (the frame PSM is PSMCT32) and no
game code writes DTHE: the PS2 picture was not dithered, and neither is
this one.

**Camera.** `gsb_MakeCommonMatrix` is where the view (`matrixptr+0x80`,
written by `camera-root.c`) and the screen matrix (`+0xC0`) are both
final, so it fills `RdCamera` (`rd_SetCamera`): `view`, `proj43` (view to GS
window X/Y and GS Z after the divide by w, 4:3), `zoom`, `aspect43`,
`nearZ` and `farZ` (2 and 262144; the screen matrix maps them to GS Z
536870880 and 1, so the scene's Z is 32-bit). `gsb_SetVSMatrix` itself does
not call `rd_SetCamera`, because `puddle.c` and `pool.c` call it mid-frame
for their reflection views. Replay fills FrameCB from the frame's camera
(SHADERS.md). Gameplay matrices are never touched by the renderer.

**VU parameter block.** The packet `gsb_MakeCommonMatrix` builds is 16
quadwords for VU1 data memory 0 (`RdVuCommon`, `rd_SetVuCommon`):

| VU1 qw | content | source |
|---|---|---|
| 0 | 0, 0, 0, 1 | `commonMatrixHead.row[0]` |
| 1 | 4095, 4095, 0, 16777215 | clip extents |
| 2 | 0, 0, 0, 0 | the UV offset slot |
| 3 | GIF tag: EOP, PRE, PRIM 0x5D (fan, IIP, TME, ABE), PACKED, NREG 3 | `commonMatrixHead.tag` |
| 4..7 | world to GS screen: screen × view | `+0x100` |
| 8..11 | viewport | `+0x340` |
| 12..15 | inverse view | `+0x380` |

Every list gets a reference to it at its current position each time it is
built, so a draw sees the block current at its position in its list. `rd`
keeps one VU image per list (section 10), which reproduces that.

**Post passes.** `gsb_KeepFrameBuffer`, `gsb_fade`, `gsb_scissorOnDemo`,
`gsb_controlBrightness`, `gsb_antiAlias`, `gsb_filmNoise` and the reduction
call `rd_Post`. Each kind records the original's register writes and
sprites in order as rd state (`rd_post.c`), so the state they leak is the
state the GS kept. Measured on llvmpipe against CPU references of the GS
arithmetic:

| kind | original | tolerance |
|---|---|---|
| `RD_POST_KEEP` | DISPLAY as PSMCT24 at 112/128 | 1 LSB |
| `RD_POST_FADE` | ALPHA 0x44, Z write off | 1 |
| `RD_POST_LETTERBOX` | two 58-line bars, LERP with FIX = the level | 1 |
| `RD_POST_BRIGHTNESS` | white, alpha = the step, mode 7 | 1 |
| `RD_POST_AA_DOWNSAMPLE` | SCENE to AA0 (256²), and AA0 to AA1 (128²) | 0 |
| `RD_POST_AA_COMPOSITE` | AA1 then AA0 LERPed into SCENE at 512×512 | 1 per level, 2 with both |
| `RD_POST_FILM_NOISE` | REPEAT, STQ, grey with the grain alpha | 1 |
| `RD_POST_REDUCTION` | SCENE to DISPLAY at half height, bilinear, tint, border crop | 0 |

**FBMSK.** FRAME.FBMSK is part of the FRAME register, so on the GS a mask
holds until the next FRAME write, whatever draws in between. `rd` records it
the same way: the decoder records `rd_ColorMask` at each FRAME write it
decodes, and every post pass whose original writes FRAME (the reduction,
the fade, the letterbox, both halves of the anti-alias pass, and the keep
and brightness passes when given a target) and the frame head record FBMSK
0 with their `rd_SetTarget`. The only mask the game sets is the dark
volume's PSMCT24 composite (section 14); it now lasts, as on the PS2, to
the anti-alias pass's FRAME write in list 10, or, on a stage without
anti-aliasing, to the next FRAME write after list 10 (a list-11 pass or
2D packet, at the latest the reduction in list 12). Draws in between keep
SCENE's alpha. `rd_gsbase` and `rd_raw` check the extent.

**The reduction** is two sprites drawn through the GS sprite model of
section 14 (`fx_sprite_ps`, recorded as `RdPostRec`s of kind
`RD_POST_REDUCTION`): the black clear of DISPLAY, then SCENE at
u = x + 0.75, v = 2y + 1 with the GS's 4-bit bilinear (texels x and x + 1
weighted 12 and 4, rows 2y and 2y + 1 weighted 8 and 8, the sum shifted
down by 8), modulated by the tint, inside the border-crop scissor. The
Original output is that integer result exactly (`rd_pixel`), so the motion
blur loop through it is exact end to end. Enhanced on targets of scale 1
(1x at 4:3) takes the same model and gives the same bytes. On a scaled
target (Enhanced above 1x, or 1x at a wider aspect) the textured sprite is
drawn as the hardware-filtered screen sprite it was before (its vertices
are kept with the record, `rd__BlurScreenFallback`): the model steps the GS
position in 1/16 pixel and weighs in 1/16 texel, which at a scale that is
not an integer moves neighbouring samples up to a tenth of a texel and
shows as edge jitter of up to 33 LSB at 16:9 1080p, where the hardware
filter is continuous in both. Enhanced thus keeps its pictures as they
were, and DISPLAY's 2x block averages stay within 2 LSB of the 1x pixel
(`rd_present`). With the mirror on the sampling is mirrored (section 17).

Two quirks are recorded as written. `gsb_controlBrightness` passes corners
that are already absolute to a helper that adds the window origin again,
so the GS receives (3840, 3840) and, from the carries of the 17-bit far
corner, (256, 256.0625): the rectangle covers the whole scene and `rd`
draws it. Whether the GS rasterises a sprite whose second vertex lies above
and left of the first is not settled by any source the port uses
(docs/TODO.md). `gsb_antiAlias` restores the scene environment as 512×512
even at 448 lines (NTSC), which offsets XYOFFSET by 32 lines there.

## 10. The mesh path

`port/render/rd_mesh.c` (recording, the mesh registry, the per-list VU
state), `rd_replay.c` (`doVu`); on the game side the host paths of
`Packet.c`, `RegistPacket.c`, `MicroCode.c`, `Primitive.c` and
`DisplayP2O.c`.

**Principle.** The PS2 builds, per list, DMA chains for VU1: the common
block, the program upload, per object its matrix and light packets, per
packet the texture packet, the material register packet, the MSCALF code
and the vertex batches. The host reads the small packets as the VIF would,
when they are chained, and keeps the effect. `MicroCode.c`'s
`mc_HostDma(id, addr, qwc)` walks the chain (id 5: cnt tags carrying two
VIF words, up to ret; id 2: quadwords of VIF codes) and interprets:

| VIF | host |
|---|---|
| UNPACK V4-32, FLG | the quadwords into a TOP staging buffer |
| MSCAL/MSCALF 0 (SET_GSREGISTER) | the GIF packet at TOP to the decoder (`gif_HostWriteRegs`): material ALPHA, CLAMP and FBA, the specular and reflection passes' PABE and ALPHA, the dissolve, point and line objects |
| MSCAL/MSCALF other | `rd_VuCall(code, TOP)`: SET_UVOFFSET, SET_*_MATRIX, SET_*_LIGHT, the BEGIN codes, through the list's resident program (`rd_VuProgram`) |
| MSCNT | a particle batch (`rd_DrawVuParticles`) |
| DIRECT, DIRECTHL | path 2 GIF packets to the decoder (PACKED and REGLIST; IMAGE skipped and logged) |

Unpack formats other than V4-32 and `ref`/`next`/`call` tags are not read:
none of the chained files uses them, and a host DMA tag cannot hold a
64-bit address.

The vertex batches of a model packet are not interpreted but drawn:
`RegistPacket.c` sends the batches' GIF tag PRIM to the decoder and records
the packet's mesh with `rd_VuDrawFromState`. A `Mesh3D` buffer
(`Primitive.c`) is drawn whole. Culling, the list choices, the packets and
the allocations are unchanged.

**GS state.** A mesh batch draws with the replay state in force at its
command, as screen prims do: the material, texture, dissolve, specular and
reflection packets were decoded into ordinary state commands in order with
the meshes, so they leak between draws and lists as on the GS.

**Meshes.** One `RdMesh` per `PacHeader` (`pac_HostMesh`; the id lives in
the header's padding). The mesh holds the vertex stream without the GIF
tags and the index list `ICO_VU_INDEX(kick, corner)` for every vertex k ≥ 2
of a batch whose strip flag allows it. `reg_setShape` rewrites the packets'
vertices every tick for morphs; `pac_HostRefresh` re-reads them
(`rd_UpdateVuMesh`). Mesh data lives in a device arena (32 MB chunks, first
fit): a mesh is copied once and again only after an update, because copying
every mesh into the upload ring on every replay was 2 MB a frame of bus
traffic. The registry holds 16384 meshes; when it is full, meshes no recent
frame drew are evicted (`rd_VuMeshValid`) and rebuilt from their packet on
the next draw.

**VU state per list.** `rd` keeps one VU image per list at record time (a
`Vu1Ref`, `vu1_ref.h`: data memory, the VF registers, the resident program
and the last BEGIN code). The common block loads all 13; SET_* uploads and
SET_UVOFFSET load the current list's. The UV offset therefore carries over
from draw to draw until the next SET_UVOFFSET or common block, exactly as on
the VU, and a list that draws before and after a camera change in one tick
draws each mesh with the block current at its position.
`rd_VuDrawFromState` builds the draw's VuCB from the image (SHADERS.md).

**Programs.** (normal_c, 32/34/36) `RD_PROG_PRELIT` region, none and
scissor; (normal_l, 32/34/36/38) `LIT`, `LIT_SPEC`, `LIT` scissor,
`REFLECT`; (cluster, 20/22/24) `SKIN`, `SKIN_SPEC`; (mesh, 20/22/24) `GRID`,
`GRID_LIT`; (particle, 18) `PARTICLE`. A pair without a row is logged once
and not drawn (VU1_PROGRAMS.md section 2).

**Replay.** One indexed draw over a batch range for the region and no-clip
modes. Under the scissor mode, two draws per batch: the triangles the VU's
SCISSOR_COMMON clips, with PRIM.ABE forced on (their PRIM is the common
block's 0x5D), then the strip's own kicks. That keeps the fans-first order
and makes the fans' blend exact (VU1_PROGRAMS.md findings 1 and 2).

**Quirks.** The particle end-tag quirk (VU1_PROGRAMS.md finding 4: an
all-culled particle batch clobbers VU memory 0, and skinned draws later in
the list vanish) is not reproduced; when a batch would have done it, a
later skinned draw in that list logs once. The extra quadword
SET_CLUSTER_MATRIX copies is reproduced.

**The title's TM.** The ICO logo is three VU meshes in list 5 (additive,
no Z write). The M of its TM is a 12-vertex strip that the disc stores in
an order zig-zagging across the letter's concave outline, so four of its
ten triangles fill the notch and the additive blend shows the overlaps;
the PS2 draws the same triangles. `Packet.c`'s host path corrects that one
strip's order as a data fix (DIVERGENCES.md).

## 11. Shadows

`ico2/seki/src/Shadow.c` (host path), `port/render/rd_shadow.c`,
`rd_replay.c` (`doShadowReset`, `doShadowStrip`, `doShadowResolve`).

**What the PS2 does.** `shadow_Reset` (frame head, list 3) clears a
scene-sized PSMCT32 buffer at FBP 0x142 and leaves ZMSK, TEST 0x50000, ALPHA
0x68 with FIX 0x80 and COLCLAMP 0. Each `shadow_RenderVolume` sends one
DIRECT packet per object: per silhouette triangle a strip of ten positions
over the six vertices of a projected prism, flat shaded, each position's
colour 0x04 or 0xFC by its facing. With FIX 1 and COLCLAMP 0 every channel
of a pixel ends at 4n mod 256, n the net count of faces that pass Z GEQUAL
against the scene. `shadow_Draw` (end of list 3) reads the buffer as
PSMCT24 with TEXA AEM, modulates it into 256², 128² and 64² levels with
bilinear sprites, then composites levels 3, 2 and 1 into SCENE with ALPHA
0x44 and TEST 0x3400D (alpha GREATER 0, DATE with DATM 0): pixels whose
alpha MSB is clear receive the shadow.

**On rd.** The volumes count in the stencil of SCENE's depth target
instead of in colour: `rd_ShadowTris` records the triangles with a sign
each, replayed as two triangle lists (increments, then decrements) with
colour mask 0, the state's Z test, no Z write, and stencil INCR_WRAP or
DECR_WRAP under write mask 0x3F. `rd_ShadowReset` clears the stencil.
`rd_ShadowResolve` clears a scene-sized count target and draws seven
fullscreen passes: pass k (0 to 5) tests stencil bit k and adds 4 << k to RGB,
pass 6 writes alpha 0x80 where the count is not 0. The chain and composites
are ordinary screen sprites.

**Why it is exact.** The stencil holds n mod 64: with write mask 0x3F an
increment from 63 writes 0 and a decrement from 0 writes 63. The GS colour
is 4n mod 256 = 4(n mod 64), which the resolve writes. Both sums commute and
the Z test writes nothing, so face order and the split into increments and
decrements change nothing. An 8-bit stencil wrapping at 256 would differ for
n = 64, 128, 192, where the GS colour is 0; the write mask removes that.
`rd_shadow` checks every count over a 512×512 target: all pixels equal the
wrapped colour sum. The resolve writes the AEM expansion as alpha directly,
because the shader expands TEXA after filtering and an RGB24 view would give
alpha 0x80 to every filtered texel next to a non-zero one; baking is exact
because `shadow_Draw` writes the TEXA it reads with itself.

**Precision of the composites.** The chain levels match the GS bilinear of
the level before with 0 LSB. One composite alone is within 1 LSB (level 1)
or 2 (levels 2 and 3) of the GS LERP, all three together within 3: the GPU's
UNORM blender rounds where the GS floors. An exact composite would need the
integer sprite path, which handles neither a textured sprite with an alpha
test nor DATE through the decoder.

**State.** Every register write of the three packets is recorded in packet
order, so what list 3 leaves (TEST 0x50000, Z write on, ALPHA 0x44,
COLCLAMP 1, TEXA AEM, TEX1 linear, the last level's TEX0, FRAME 0x40)
leaks into list 4 onwards as on the GS.

**Resolution.** The count is resolved at the scene's resolution, but the
blur levels keep the PS2 sizes at every scale: the shadow's softness is the
levels' resolution, not a GS distance, and at scale 2 work-sized levels made
the penumbra half as wide. The first level then samples a 4x count with
2×2 bilinear taps of a 4×4 footprint, so the shadow's integral varied by
2.6 % frame to frame instead of 2.0 %. Now a scaled count is box-reduced to
its GS size first: `doShadowResolve` runs `box_reduce_ps` (an exact
area-weighted average of the count's texels under each GS pixel, any scale)
into an RGBA8 texture of the GS size, and `resolveTexture` gives that texture
to whatever samples the count as a texture, so level 1 reads what the PS2's
read and its integral is the area's however the volume sits against the
grid (`rd_shadow`, scale 4: a rectangle moved by quarter pixels, level 1's
sum within 0.005 %; before the fix 3.9 %). Original (scale 1) takes no
extra pass and is byte for byte what it was (DIVERGENCES.md V-SHL1).

## 12. Depth fog

`ico2/seki/src/ZFog.c` (host path `fogHostDraw`), `rd_post.c`
(`RD_POST_FOG`), `rd_replay.c` (`doFog`), `port/shaders/fog_lut.hlsl`.

**What the PS2 does.** `fog_DrawFog` runs right after `shadow_Draw` when the
stage has fog. Its packets go into list 4 (`tex_ResetVramPri(4)` selects
it). It uploads a 256-entry CLUT, copies the PSMZ32 Z buffer pixel for pixel
into a colour buffer, shuffles 8-pixel strips of that copy in a PSMT4 view
with local transfers, then draws one full-scene sprite that reads the copy as
PSMT8H through the CLUT, under Z GEQUAL against 0xFFFFFF with ZMSK, blended
LERP As into SCENE. An optional untextured sprite of the fog colour
(`fogOffsetA`) follows.

**The Z byte.** The PSMT8H texel at (x, y) is the top byte of the 32-bit
word the PSMCT32 layout puts there. In the PSMT4 view a page is 128 pixels
wide; BW 2 makes each 32-line band of the copy eight PSMT4 pages stacked,
and moving pixels 16 to 23 of every block column to 24 to 31 copies byte 2
of every word into byte 3. So the CLUT index is **bits 16 to 23 of the
32-bit Z**. With the scene's screen matrix GS Z is about 2^30 / w − 4095,
so fog starts at w = 64 (index 255) and the index falls as about 16384 / w.
`rd_fog` checks this with a CPU model of the GS memory (the PSMCT32, PSMZ32
and PSMT4 page, block and column tables from the GS manual's figures,
cross-checked against a public swizzle visualiser): the index is Z bits 16
to 23 at every pixel. The same family of tricks is described in SCEE's
"Using the Z Buffer for Visual and Special Effects", whose 16-bit variant
takes bits 8 to 15. The layouts come from documentation, not from a
hardware capture (docs/TODO.md).

**CLUT order.** `fog_MakeFogClut` writes the table in CSM1 storage order,
and the GS's CSM1 lookup undoes the trade, so index n sees the logical
entry n. The host path passes the logical table.

**On rd.** The transfers are not recorded; the LUT and the index rule stand
for them. `fogHostDraw` records the register state the packets set, then
`rd_Post(RD_POST_FOG)` with the sprite as the GS gets it and the LUT. Replay
copies SCENE's depth into a sampleable texture, uploads the LUT as a 256×1
texture and draws `fog_lut_ps`, which reconstructs the GS Z from the depth,
does the GEQUAL test in the shader, caps a passing pixel's Z at the
sprite's, takes bits 16 to 23 and applies the LUT texel with MODULATE. The
blend, PABE, FBA, colour mask and alpha test come from the state.

**Precision.** For Z up to 0xFFFFFF the D32F depth lies in [1 − 2^-8, 1],
where a float steps by 2^-24, so the stored depth carries Z rounded to a
multiple of 256. The index can therefore be one off only within 128 of a
multiple of 65536 (0.4 % of Z values; one step of the fog alpha). Without
the cap, 0xFFFFFF would store as 2^24 and read index 0.

**State left behind.** As the GS's, except that TEX0 stays the depth view
of SCENE on rd; a later draw relying on that leak would read the Z copy
through the fog CLUT on the GS and logs once (`RD_ONCE_DEPTH_VIEW`) on rd.
Every game path after the fog writes its own TEX0.

## 13. Render-to-texture surfaces

`ico2/sugipon/src/puddle.c`, `pool.c`, `ito/src/queen_barrier_disp.c` (host
paths), `port/render/rd_water.c`.

**What the PS2 does.** The puddle clears SCENE's alpha, draws the puddle
object (alpha 0x80 where it is), renders the reflected model into a 256×256
block at TBP 0x2800 with its own Z buffer at 0x2C00 through a 230×230 screen
matrix, darkens the puddle (DATE), draws ripple rings as STQ strips and lays
the block over the screen where the alpha MSB is set. The pool copies SCENE
into the 256×256 block, draws a refracting grid sampling it, renders the
reflection through a 204×204 matrix and adds the block through a second
grid. The queen's barrier copies SCENE into a 512×256 block and draws its
grid with refraction STs. Water drops (`waterDot.c`) are raw points in list
11; cloth (`clothAnimation.c`) is an ordinary lit Mesh3D.

**Why the decoder was not enough.** The bump allocator puts these blocks at
TBP 0x2800, which the decoder maps to AA0: a 256×256 target without a depth
buffer, so the reflections would draw with no Z test and the barrier's copy
would be squeezed into 256 columns.

**On rd.** The game files bind the block themselves and keep every register
write:

- `rd_BlockTarget(tbp, w, h, depth)`: one per-frame target per VRAM block
  and size, so puddle and pool in one frame share it in order, as on the GS.
- `rd_AliasTarget(rd_GsNamedBlock(tbp, w, h), block)`: from the allocation
  to the last draw that samples the block, in that list only, the decoder's
  binding of AA0 records the block instead. Recording only: replay and
  dumps see an ordinary temporary target.
- `rd_PushCamera` / `rd_PopCamera` around the reflection draws, for the
  Enhanced projection and the interpolation. In Original nothing reads them:
  the reflection's VU draws take the matrices their packets carry.

Everything else was already exact through the decoder and the mesh path,
including the STQ grids (perspective-correct STQ interpolation, as the GS
does). Measured against CPU rasters: the puddle block and composite 0 LSB;
the pool's grids within the GS's 1/16-texel addressing and 4-bit filter
weights (1 LSB from the centre sample, 0 outside that range).

The ripple strips carry PRIM.AA1 (0xD4): their triangles get the edge
fringes of section 4, "PRIM.AA1".

## 14. Full-screen effects and the raw packet builders

### staticBlur.c: motion blur, depth of field, flare, glow, eye blur, aura

`ico2/sugipon/src/staticBlur.c` (host path), `port/render/rd_blur.c`,
`rd_replay.c` (`doBlurSprite`), `port/shaders/fx_sprite.hlsl`.

`FullScreenEffectBefore` runs at each frame's head and
`FullScreenEffectAfter` first in `gsb_PostEffect`. The stage's
`postEffect` selects the flare ("SBLUR", 1 and 3, the latter with depth of
field), depth of field (2), glow (4, 5) or "BLSBLUR" (6, 7), and
`feedbackEffect` the aura (1), mirage (2, the default) or aura v2 (3). The
flare, depth of field and motion blur draw in list 7, the aura in list 8:

- **Motion blur**: DISPLAY (the reduction of the previous frame) stretched
  back over SCENE with LERP FIX `motionBlurAlpha`: a feedback loop.
- **Depth of field**: SCENE downsampled into WORK1 and WORK0, six blur
  passes between them with TFX HIGHLIGHT, then four planes composited back
  at increasing GS Z under Z GEQUAL.
- **Flare**: a mask of the sky, the sun's fans and list 7's shine objects
  in WORK2, reduced and blurred ten times (HIGHLIGHT adds Af to RGB, which is
  the glow), plus the eye blur's ghosts, added into SCENE.
- **Aura and mirage**: list 8's objects drawn into a scene-sized buffer,
  tapped into FEED128, which persists between frames, and pasted back.

`staticBlur.c`'s host path renames the eight `gif_*` calls it makes to
`sbHost*` functions in the same file, which record the same register
writes in the same order as rd state and record every sprite as `rd_Post`
of the effect's kind (`RD_POST_MOTION_BLUR`, `RD_POST_DOF`,
`RD_POST_FLARE`, `RD_POST_BLOOM`, `RD_POST_EYE_BLUR`, `RD_POST_AURA`). The
buffers are matched by `workBase` (so the first After of a run, before any
Before, uses the same targets):

| VRAM (FRAME / TEX0) | rd target | depth bound |
|---|---|---|
| `workBase[0]`, 256 wide | WORK0 256×128 | none |
| `workBase[0]`, 128 wide | AURA_TAP 128² | none |
| `workBase[1]`, 256 wide | WORK1 256² | none |
| `workBase[1]`, scene wide | AURA_WORK | SCENE |
| `workBase[2]` | WORK2 | SCENE |
| `workBase[3]` | WORK3 256×128 | none |
| `workBase[2]` + W·H/64 | WORK2_PAD 256×64 | none |
| 0x3F00 | FEED128 | none |
| 0x800, 0 | SCENE, DISPLAY (RGB24 view) | SCENE, none |

The effects have their own path because the generic decoder cannot know
these sizes (the GS reuses 0x2A00 for three buffers of different sizes),
would give the buffers depth of their own where the GS tests against the
scene's Z, and draws through the hardware sampler, which picks a minifying
filter where the GS's sprites use MMAG.

**The sprite model.** Each record is drawn by `fx_sprite_ps` in the GS's
integer arithmetic, with the destination read from a copy of the target
taken just before (so a sprite sampling its own target reads it as the GS
does):

| step | model |
|---|---|
| coverage | pixels whose 12.4 window coordinate X has x0 ≤ X < x1, likewise Y, inside SCISSOR |
| UV | U(X) = u0 + (X − x0)(u1 − u0) / (x1 − x0) in 12.4 integers, truncated |
| filter | TEX1.MMAG (sprites have LOD 0): nearest = texel (U >> 4, V >> 4); linear = the four texels at (U − 8) >> 4, (V − 8) >> 4 weighted by the 4-bit fractions, sum >> 8 |
| texel | CLAMP or REPEAT on the TEX0 size, then TEXA before filtering |
| TFX | MODULATE, DECAL, HIGHLIGHT `(T·C) >> 7 + Af` with A = At + Af, HIGHLIGHT2; clamped at 255 |
| tests | alpha test with AFAIL, DATE against the copy, Z test in hardware |
| blend | `gs_blend_int` (`((A − B)·C >> 7) + D`), PABE, COLCLAMP, FBA; written as k / 255, which UNORM8 stores exactly |

The UV step and bilinear rounding are a model: the GS manual does not give
the sprite DDA or the filter's rounding, and no hardware capture was
available. `rd_blur`'s CPU reference implements the same rules
independently, and the GPU matches it with 0 LSB for every effect,
including 600 feedback frames of motion blur and aura. The motion blur loop
also runs through the reduction, which is drawn by the same model (section
9), so the loop is exact end to end: `rd_blur` runs it with the real
`rd_Post(RD_POST_REDUCTION)` and a tint, and checks the reduction against
the GS formula written out as well.

**Time-corrected feedback.** `RdPostRec.scalar[2]` carries the number of
ticks a sprite stands for (1 in Original); `rd__BlurFeedbackFix` turns FIX
into the value whose retention over dt ticks equals FIX's over one (section
16).

### Raw packet builders outside seki

`darkVolume.c`, `particleEffect.c` and `lightning.c` build their own
packets. They stay as the game builds them; the host reads the finished
chain at the point the game chains it (`mc_HostDma` after `dl_CloseDma`),
so the decoder receives the PS2's register and vertex stream in packet
order. `enemy.c` and `lineManager.c` already went through routed helpers.

**The dark volume** (`DispGameOverEffect`, the queen's ball) draws nested
spheres into a scene-sized block with Z GEQUAL against the scene, no Z
write and COLCLAMP 0, each face adding its colour or its two's complement by
its screen winding, so each pixel ends at (front − back) × colour per
sphere, modulo 256; then composites the block into SCENE as PSMCT24 (alpha
kept). Two facts the decoder cannot know are supplied by explicit host
calls (`dvHostBlockBegin`, `dvHostSceneZ`, `dvHostBlockEnd`): the block is
scene-sized (`rd_BlockTarget`) and it uses SCENE's Z buffer. The PSMCT24
frame mask is left in force after the composite and ends at the next FRAME
write, as on the GS (section 9, "FBMSK").

**COLCLAMP 0 wrap** (`doScreenWrap`, `raw_wrap.hlsl`). A screen-prim
command under COLCLAMP 0 with an equation that adds or subtracts a source
term alone (modes 0, 5, 1, 6) ends at Cd plus the sum of its fragments'
terms modulo 256, in any order. `rd` draws it in two passes: each fragment
adds its GS term into an RGBA16F accumulator (`wrap_acc_ps`), then
`wrap_resolve_ps` writes (Cd + acc) mod 256. Exact while no pixel takes more
than 16 fragments of one command (half floats hold every integer up to
2048); the dark volume takes at most 9. DATE, PABE and an AFAIL split are
not modelled under COLCLAMP 0 (logged once; no game state has them).
Shadows keep their stencil path.

## 15. Presets and display options

`port/game/video_options.c` (the `[video]` keys), `port/platform/window_host.c`
(applies them), `rd_present.c` (`rd__ApplyDisplay`, `rd__PresentBox`, the
presenter), `rd_core.c` (target scales), `rd_replay.c`, `rd_frame.c`.

**Settings.** `[video] preset`, `resolution`, `aspect`, `fullscreen`,
`vsync`, `texture_filter`, `full_height` and `framerate` (DISPLAY.md has
the values; CONFIG.md the file). The Original preset ignores everything but
`fullscreen`, `vsync` and `framerate`. `ico_video_set` bumps a serial the
window's pump compares, so the Settings menu applies without a restart:
`rd_SetSettings` takes effect at the next `rd_BeginFrame`, where
`rd__ApplyDisplay` turns `RdSettings` into target scales and recreates the
named targets when a scale changed (after `rhi_WaitIdle`; DISPLAY's
motion-blur history restarts), and the swapchain when vsync changed.
Fullscreen is SDL's borderless desktop fullscreen; Alt+Enter flips it.

**The Original preset.** The scene target is 512×512 (PAL) or 512×448
(NTSC). Textures are filtered as the TIM2 ICO block specifies, with the mip
level fixed per texture. The texture function and vertex colours are
integer. Work buffers have their literal PS2 sizes. The reduction draws
SCENE into DISPLAY at half height with bilinear filtering, the per-stage
tint and the border crop (2 pixels left and right; 8 lines top and bottom on
PAL, 2 on NTSC). The presenter draws DISPLAY into a centred 4:3 box, each
line doubled vertically and bilinear horizontally. Every path below is
guarded so that at scale 1 and aspect 4:3 it executes the same arithmetic as
Original, and the render tests' frame dumps rendered by `rd_replay_tool` are
byte-identical between the two in that case.

**Resolution.** A target keeps its GS size and gains a texture size and a
scale (texels per GS pixel, `rd__TargetScaleOf`). FrameCB keeps the GS size
in `g_target`, so every vertex lands where it did and only the viewport,
scissor, copies, snapshots and readbacks change.

| targets | scale |
|---|---|
| SCENE, WORK2, AURA_WORK, DATE_SNAPSHOT, temporary targets of the scene's GS size | the scene's: `Nx` gives sy = N and sx = N × aspect / (4/3); `WxH` gives W / gsW, H / gsH; `window` fits the presentation box; at least 1, at most 3840×2160 |
| DISPLAY | the scene's, sy doubled with `full_height` |
| WORK0, WORK1, WORK3, AA0, AA1, FEED128, AURA_TAP, WORK2_PAD | `rd_WorkTargetScale`: sy clamped to [1, 2], so blur radii, which are GS distances, stay the same fraction of the screen at finer sampling |
| SHADOW0..2 | the PS2 sizes at every scale (section 11) |
| other temporary targets (reflections, render-to-texture blocks) | 1 |

On a scaled target `g_origin.zw = 0.5 / s`, which puts a GS integer
coordinate on the top-left edge of its s×s block. Sprites have their
corners snapped to whole GS pixels, exactly the set the GS covers, and
their UVs moved so a nearest-sampled sprite fills each block with the texel
the GS samples. Triangles and meshes rasterise continuously, and so do
sprites recorded with `RD_UV_FIXED_CONTINUOUS` (the port's own text, whose
glyph quads have sub-pixel edges). `fx_sprite_ps` and `fog_lut_ps` address
their source in its own texels (`DrawCB.g_scale`). A copy between targets of
different scales cannot resample and logs once (`RD_ONCE_COPY_SCALE`); no
game path does it.

**Vertex quantisation.** With the Enhanced preset on a target whose scale
exceeds 1 (`FrameCB.g_z.w`), the VU programs rasterise the divided position
instead of its 12.4 value. At 4x a 12.4 step is 0.25 output pixels
vertically and 0.33 horizontally at 16:9: sub-pixel stepping of slowly
moving edges. The no-clip mode, which wraps X and Y to 16 bits, and the
particles keep the 12.4 value. Original keeps the grid.

**Widescreen.** An aspect A gives the factor f = (4/3) / A (0.75 at 16:9).
The game: `gsbHostWideX()` returns A / (4/3) and `gsbHostWidenCull` divides
`projHalf[0]` (`+0x240`) by it after `gsb_SetVSMatrixSub`, so the per-object
cull frustum is wider. This is compiled into the headless build too, so a
headless run exercises it, and traces with and without 16:9 are
byte-identical. `+0x80`, `+0xC0` and `+0x100` are untouched, so every
screen test the game makes (`IsPointIsInScreen` and the rest) still uses the
4:3 frame. The renderer multiplies NDC x by f about the target's centre for
draws into the wide targets (SCENE, WORK2, AURA_WORK, the shadow count):
meshes, shadow volumes and screen prims, so CPU-projected prims follow the
3D and the 2D layer sits in a centred 4:3 box. Full-screen draws stretch
(f = 1): `RD_SPACE_FULLSCREEN` prims (the post passes, the fog sprite, the
layout's primary sprite through `rd_SetSpaceOverride`), every sprite that
covers the target's whole width to within one pixel at each edge
(`screenStretch` in `rd_replay.c`: the covered pixels reach pixel 1 or less
and w − 2 or more), and the fullscreen-triangle passes. `rd__FillCameraCB`
compresses `g_proj` and `g_viewProj` the same way. The presenter boxes
DISPLAY at A; movies keep the 4:3 box.

A UI-space screen prim's scissor follows the wide scale too: a scissor that
does not reach the target's left or right edge moves with the draw about the
target's centre, rounded outwards (`rd__WideScissor`, `rd_state`), so it
clips as much as the draw does; a side at the edge stays there. (It used
to keep its 4:3 position, which clips less, never more; none was seen.)

The widescreen audit, each full-frame draw with the 512×512 scene (GS
origin 1792):

| draw | source | GS extent | at 16:9 |
| --- | --- | --- | --- |
| demo letterbox | `gsb_scissorOnDemo` → `postLetterbox` | x 0..512, 58 lines each | FULLSCREEN: stretched |
| fade | `gsb_fade` → `postFade` | 0..512 × 0..512 | FULLSCREEN: stretched |
| reduction border and tint | `gsb_Reduction` → `postReduction` | DISPLAY 0..512, scissor 2..509 × 8..247 | FULLSCREEN: stretched |
| keep (paused picture) | `gsb_KeepFrameBuffer` → `postKeep` | −0.75..513.25 | FULLSCREEN: stretched |
| brightness | `gsb_controlBrightness` → `postBrightness` | 256..3840 (wrapped corners) | FULLSCREEN: stretched |
| anti-alias | `gsb_antiAlias` → `rd_post.c` | AA0/AA1, then −0.25..511.75 into SCENE | FULLSCREEN: stretched |
| film noise | `gsb_filmNoise` → `postFilmNoise` | 0x7000..0x9000 (0..512) | FULLSCREEN: stretched |
| fog | `ZFog.c` | full scene | FULLSCREEN: stretched |
| menu dimming (pause, Options, game over red) | `lt_draw_primary_sprite` (`layout_texture.c`) | 0..512 | `rd_SetSpaceOverride`: stretched |
| menu black bands | `display_texture` (`layout_texture.c`) | 0.25..511.44 | UI sprite covering the width: stretched |
| boot signs' black | `kanban.c` | 0..512 | UI sprite covering the width: stretched |
| boot and card-check black | the clears; the opaque backdrops of layouts 0 to 4, 7, 8 | 0..512 | stretched |
| movies | `rd_video.c` | the presenter's 4:3 box | boxed by design |
| loading bar | `progressive_bar` (`layout_action.c`) | 160 px | UI: boxed |
| debug text backgrounds, memory bars | `debug.c`; `icoMisc.c` | text-sized; ScreenWidth − 200 | UI: boxed |

The menu black bands are the pair of `texProperty` rows every banded layout
draws (dispX 0, dispW 640; one texel of texture file 11). The GS gets them
at x 1792.25 to 2303.44, pixels 1 to 511 under the top-left rule, which is
why the full-width test allows a pixel at each edge. The layouts with the
band pair are 17 to 19, 22, 23, 25 to 27, 31 to 33, 37 to 40, 42 to 47 (memory
card messages, save, format, Resume Game / End Game), 56 and 57 (pause), 58
(Options), 59 (button configuration), 60 (Brightness), 61 (End Game
question), 62 and 64 (game over "Continue?").

What does not widen: the reflections' render-to-texture targets keep 4:3,
so at the sides of a 16:9 frame a puddle shows its reflection's clamped
edge; a world-projected prim
drawn as a sprite spanning the whole width is taken for a fill and
stretched. The port's menu text draws in list 11 in UI space, so it sits in
the 4:3 box at the scene's resolution in the Original preset; in Enhanced it
is drawn deferred (below), at the output's resolution in the same 4:3
picture, as the popups on the presentation overlay are.

**Presentation.** Original: DISPLAY into the 4:3 box, each line doubled,
bilinear horizontally. Enhanced: the box of the aspect option; with
`full_height` DISPLAY already has every line (its sy doubled, the reduction
rasterising at that density) and the line doubling is skipped. The
swapchain is the window's pixel size.

**Texture filter.** With `texture_filter` trilinear or anisotropic (Enhanced,
and `RhiLimits.textureMips`), each power-of-two game texture is uploaded
with its full mip chain (`rdtex_BuildMipChain`, 2×2 box filtered, into
`rdtex_MipChainBytes(w, h)` bytes) with alpha coverage kept
(`rdtex_KeepAlphaCoverage`: per level an alpha scale in [1, 4], never past
level 0's largest alpha, that restores level 0's share of texels with alpha
above 64, the threshold the semi-transparent lists test), so fences and
leaves keep their thickness in the distance. Draws whose TEX1 minifies
linearly sample with mip-linear or anisotropic samplers (up to 16);
textures authored nearest stay nearest. A change of the option recreates
the textures.

### The presentation overlay

`rd.h` `rd_SetPresentOverlay`, `rd_OverlayPrims`, `rd_ReadPresented`;
`rd_present.c` (`rd__OverlayCollect`, `overlayRecord`), `rd_replay.c`
(`rd__OverlayDraw`), `rd_pipeline.c` (`rd__OverlayState`). The port's own
UI that does not belong to the game's picture, today the popups
(UI.md "Popups"), is drawn on the output after the box blit, at the
output's resolution.

**Contract.** One callback is registered at a time (`port/ui/ui_host.c`
registers the popups' at start-up; the registration survives `rd_Shutdown`
and `rd_Init`). It is called once per present that reaches an output:
`rd_EndFrame`'s in Original, every `rd_Present`, the replay tool's
`--present`; not for the movie picture (`rd_video.c`). It gets an
`RdOverlayCtx`: the output's size, the box DISPLAY was blitted into,
`boxScale` = box height / 448 (output pixels per line of the 448-line
frame) and whether the blit was mirrored. Inside it, and only there,
`rd_OverlayPrims(prim, v, n, tex, blend)` adds prims:

- XY in 12.4 output pixels from the output's top-left corner, an integer
  on a pixel's top-left edge (pixel (x, y) covers [x, x + 1) × [y, y + 1),
  not the GS convention), so a texture drawn 1:1 samples its texel
  centres; at most 4095 pixels each way (the sprite vertex's u16 12.4);
- s, t in 12.4 texels; tex 0 untextured, else MODULATE with TCC RGBA,
  bilinear, clamped; colours and the texture's alpha in GS units (0x80 =
  1.0; an R8 texture's byte is that alpha);
- `blend` the GS equation with ABE on (the popups use `RD_BLEND_LERP_AS`,
  the glow `RD_BLEND_CS_AS_ADD_CD`); no depth, no alpha test, no DATE,
  COLCLAMP on; no keys, so nothing is interpolated; never mirrored (the
  mirror mode flips only the box blit); no scissor but the output.

The callback runs before the frame's replay (`replayFrame` calls
`rd__OverlayCollect` before it sizes the upload ring), so textures it
creates or updates (the font's atlas pages) are uploaded with the frame and
the ring has room for its vertices; the prims are kept and drawn later in
`rd__PresentRecord`. The output and box it is given come from `RdSettings`,
which do not change inside a replay, so they are the ones the present
uses.

**Drawing.** After the box blit `overlayRecord` opens one load-preserving
pass on the output (the headless `presentOut` or the swapchain image),
viewport and scissor the whole output, FrameCB `rd__FrameGroup(outW, outH,
0.5, 0.5)` (`sprite_ui_vs` then maps x / 16 to the pixel position x: the
origin's half pixel cancels `g_origin.zw`), and draws each batch through
the screen-prim path (`expand`, no sprite snap, no UV shift, no mirror) with
`sprite_ps` (`font_ps` for an R8 texture, section 8) and the pipeline
`rd__PlanScreenDraw` plans for `rd__OverlayState`'s block with the batch's
texture bound. The overlay's pipelines (LERP and additive, RGBA8 and BGRA8,
no depth, both fragment shaders) are in `rd__EnumerateReachableScreen`, so
`rd_PrecreatePipelines` makes them at start-up.

**Ordering.** In `rd__PresentRecord`: DISPLAY to the line-doubled target
and the box blit, or in their place the CRT filter ("The CRT pass" below),
then the deferred text (`textRecord`, below), then the presentation passes
of later packages (a marked insertion point in `rd_present.c` says where),
then the overlay, last, then the window's transition to PRESENT. The
filter is the picture's; the deferred text and the overlay are drawn
above it, unfiltered. A pass inserted there draws on `out` (in
RENDER_TARGET at that point) with `rd__FrameGroup(s_outW, s_outH, ...)` as
`blit()` does; the overlay stays above it.

**What it fixes.** The popups were recorded into list 12 of the game's
frame: drawn at the scene's resolution and halved by the reduction,
pre-flipped with the UI, and part of DISPLAY, so a keep frame drew the last
full frame's popup under the live one. On the overlay none of that can
happen: the overlay is never in DISPLAY. Since it is not part of the
frame, a frame dump does not contain it; `rd_replay_tool --overlay-test`
draws a test pattern on the overlay instead.

**Unchanged without it.** With no callback registered nothing is collected
and `overlayRecord` returns at once: the Original present's bytes are the
recorded ones (`rd_present`'s hash).

**Reading the output.** `rd_ReadPresented` reads the last presented output
(RGBA8, outputWidth × outputHeight), overlay included, from the headless
output. The window build presents the swapchain image and keeps no copy,
so there it returns false; a photo mode wants a copy of `out` taken at the
end of `rd__PresentRecord` (the swapchain is created with transfer-source
usage), which is where one would go.

### The deferred text pass

`rd.h` `rd_DeferredText`, `rd_DeferredTextQuads`, `rd_SetDeferredTextFn`,
`rd_DeferredTextActive`; `rd_core.c` (recording, `rd__DeferredTextOp`),
`rd_post.c` (`textOp`), `rd_present.c` (`textCollect`, `textRecord`),
`rd_replay.c` (`doScreen`), `rd_interp.c` (`blendText`); `port/ui/font.c`
(`ui_DrawTextDeferred`, the renderer); UI.md "Menu text". Package DEF.

The game's menu rows are text the port draws (UI.md "Menu text"). Drawn as
glyph quads into list 11 they are rasterised at the scene's resolution,
halved by the reduction and scaled by the box blit, which in Enhanced
leaves three pixels between background and ink on a glyph's edge at 1080p.
Drawn on the output after the box blit they are rasterised at the shown
size and composited a texel a pixel, but the frame's later passes, which
changed the quads' pixels, must then be applied to them by other means.

**Recording.** A text line is recorded twice, in place in its list: first
an `RDC_OVERLAY_TEXT` item (`RD_OTEXT_ITEM`, an `RdTextItem` in the
payload: the UTF-8 string up to 255 bytes, the layout-grid anchor, the size
in grid y units, port/ui's flags (alignment, halo, `UI_ADDITIVE`), the
glow's stretch (`xf`, `hasXf`), the colour after the row's fade and
dimming, and `additive`; keyed like the quads), then the glyph quads, each
`RDC_SCREEN` marked `RD_SCREEN_TEXT_QUADS` in `b[3]` (between
`rd_DeferredTextQuads(1)` and `(0)`). The state commands between are the
quad path's, so the state the frame leaks is the same either way. Once a
frame has an item, `rd_Post` adds an `RD_OTEXT_OP` (`RdTextOp`) after the
sprites of a FADE, LETTERBOX, BRIGHTNESS, KEEP or REDUCTION pass, keyed
as the fade and letterbox sprites are; a frame without text records none, so the post
passes' command streams are unchanged.

**The gate is the replay's.** A replay that presents to an output in the
Enhanced preset with a renderer registered (`rd_SetDeferredTextFn`;
`port/ui/ui_host.c`, `rd_replay_tool` unless `--quad-text`, the tests)
draws the items deferred: `rd__OverlayCollect` sets `g_rd.deferText`,
`doScreen` skips every marked quad, and the items are drawn on the output.
Every other replay (Original, no renderer, no output, a replay that does
not present, such as the replay tool's DISPLAY or SCENE PNGs) ignores the
items and draws the quads, so its bytes are those of a frame recorded
without items (`font_edge`: the Original present of a frame with a
deferred row and a popup is byte-identical to the plain quads'). Classic
menu text (`[game] classic_menu_text`) is decided at recording: port/ui
records no item then. Deciding at replay lets a preset change apply at the
next present, and lets one dump render both presets: the corpus renders
each dump in Original and Enhanced.

**Collection.** Before the replay (as the overlay's callback, so the font's
atlas pages are created and uploaded with the frame and the ring has room),
`textCollect` walks the replayed lists (11 and 12 of a keep frame) with the
state they replay under. An item takes the scissor in force and the size
of the target it is drawn into. An op changes the items before it as its
pass changed the pixels they were drawn into:

| pass | what it does to the scene | the items before it |
| --- | --- | --- |
| FADE | lerp to the fade colour by its alpha / 128 | a lerp item's shown colour (the GS colour times 255 / 128, the atlas being white) lerped the same way, its alpha kept; an additive item's colour times (1 − k) |
| BRIGHTNESS | lerp to white by the step / 128 (`LERP_AS_ALT`) | the same, toward white |
| LETTERBOX | lerp the two `lines`-line bands to black by FIX / 128 | cut by scene line into segments; the segments inside a band folded toward black |
| KEEP | DISPLAY drawn over the whole scene without blending | dropped (their quads would be drawn over) |
| REDUCTION | SCENE into DISPLAY times the stage's tint / 128 | either kind's colour times the tint |

Each of the first three is d' = d (1 − k) + C k, affine in the destination,
so text blended over the scene and then lerped equals the lerped scene with
the text blended over it in the lerped colour: the fold is exact up to the
8-bit rounding of the stored colour. The reduction's tint is linear too,
but it can exceed 1.0 (149 / 128 on the title) and the GS clamps the
tinted pixel: where the scene behind a partly covered text pixel (an edge,
the halo, a dimmed row) is bright enough to clamp, the deferred pixel is
the clamped scene blended with the tinted text, a little darker than the
quad path's. Fully covered pixels are exact. Then for each item and segment the
renderer is called with the item as folded and a region: the item's
scissor mapped onto the output (a side at the target's edge to the box's
edge, as `rd__WideScissor` keeps it; the others through the 4:3 picture the
UI is in), the reduction's border crop (2 of 512 columns, 8 of 256 lines;
2 below a 512-line scene) and the segment's lines, in whole output pixels.
port/ui's renderer lays the item out through font.c's overlay mode, so the
prims are rasterised at the box's scale with each glyph's corner on a whole
output pixel; `rd_OverlayPrims` gives them the region as their batch's
scissor.

**Drawing.** `textRecord` draws the items' batches in one load-preserving
pass on the output after the box blit, before any later presentation pass
and the overlay, through the overlay's path (`rd__OverlayDraw`, the same
pipelines, so `rd__EnumerateReachableScreen` needs no new key), setting
the scissor when a batch's region differs from the last.

**Ordering it cannot keep.** An item is drawn after the whole picture: a
draw recorded after it in lists 11 and 12 that overlaps it (film noise, the
boot signs' cursor sparkle, the loading bar, the developer overlay) is now
under it, and DISPLAY no longer holds the text, so the motion blur's
feedback, a keep frame's retained picture and an F12 DISPLAY screenshot
show no menu text. UI.md "Menu text" gives the cases and why they are
accepted.

**Mirror.** Nothing is flipped: the quads are pre-flipped at replay and the
present flips them back, so on the output they read normally at their
recorded place, which is where the unflipped item is drawn (`font_edge`:
the mirrored present equals the unmirrored one). Mirroring the anchor and
swapping the alignment, as the plan proposed, would have put the text on
the other side.

**Interpolation.** Items and ops are keyed draws (section 16): an item
blends its anchor, stretch and colour, and snaps when its string, size or
flags differ or its anchor moved more than `RD_INTERP_JUMP_SCREEN` grid
units; an op blends its colour and level, so a fading row and the fade it
is under move together. An item of one tick only fades in or out with its
alpha, cur's in place and prev's inserted (I1's `unmatchedPass`), as the
quads it stands for do.

### The CRT pass

`port/render/rd_crt.c` (`rd__CrtRecord`, `rd__CrtResolve`, the modes'
table), `port/shaders/crt.hlsl`, `rd.h` `RdSettings.crtMode` ...
`crtCurvature` and `rd_CrtSettings`; `port/game/video_options.c` (the
`[video] crt*` keys), `port/ui/settings.c` (the two rows). DISPLAY.md "CRT
filter" is the user's description and holds the parameter table. Package
CRT.

**Where.** With `RdSettings.crtMode` not `RD_CRT_OFF` and `crtStrength`
above 0 (`rd__CrtOn`), `rd__PresentRecord` calls `rd__CrtRecord` with
DISPLAY (SHADER_READ), the output (RENDER_TARGET) and the box, instead of
the line doubling and the box blit; if it cannot draw (a pipeline missing)
it returns false and the blit runs. Off, no command is recorded that was
not before: the present is byte-identical (`rd_crt`: rd_present's hash),
and so is a mode at strength 0. Either preset; nothing in SCENE or DISPLAY
changes, so dumps, `rd_ReadDisplay`, the replay tool's target PNGs and the
deferred text's collection do not depend on it.

**The virtual source.** The filter's grid is the PS2's: DISPLAY's GS width
divided by `g_rd.wideX` (512 at 4:3, 683 at 16:9) by its GS height, doubled
with the full-height scene. DISPLAY's own lines are the scanlines: the line
doubling is skipped. When DISPLAY's texture is larger (an Enhanced scene
scale), `box_reduce_ps` (the shadow family's key) averages it to the
virtual size into "rd crt source" first; in Original DISPLAY is used as it
is.

**Passes.** (1) `crt_bloom_ps` into "rd crt glow A", RGBA16F, half the
virtual size rounded up (256 x 128 in Original): linear light (`gamma in`),
a 9-tap Gaussian across, taps 2 source pixels apart, each a bilinear
sample of 2 x 2 source pixels. (2) `crt_blur_ps` into "glow B": the same
vertically. (3) `crt_ps` into the box of the output, cleared black around
it (the pass's clear, as the blit's): the box-relative position is warped
(x by 1 + y^2 cx, y by 1 + x^2 cy, crt-lottes' warp), a rounded-box signed
distance (the corner radius, antialiased over a pixel) cuts the corners and
what the warp pushed outside; the mirror flips the warped x; the two
nearest source lines are each a Gaussian of 4 pixels across (sigma half
the horizontal blur), and each line's beam a Gaussian across the lines of
unit area whose full width at half maximum goes from beam min to beam max
with the channel's value, mixed with the plain linear interpolation by the
scanline strength; the mask (lit channels 1.5, dark 0.5, lerped toward 1
by its strength) by output pixel; halation (glow B at the point and 4
glow texels around, a fifth of each) and bloom (glow B weighted by
smoothstep(0.2, 1, luma)) added; the vignette (16 x y (1 - x)(1 - y))^v;
gamma out; finally a lerp by the strength against the plain picture (DISPLAY
bilinear across, nearest down, as the blit shows it), alpha DISPLAY's.

**Constants.** `IcoCrtCB` (`shader_consts.h`) is DrawCB's size and is bound
in its slot with the draw layout (`rd__CrtGroup`), so no layout or dynamic
group is added; group 0 gets a FrameCB (unused by the shader) and group 2
`rd__TexGroupDate` with t2 the glow (the dummy for the glow passes). The
mask strength is multiplied by `rd__CrtMaskFade(box h)`: 1 from 1080, 0 at
720 and below (DISPLAY.md says why).

**Pipelines.** Four keys, `rd__EnumerateReachableCrt`: the two RGBA16F
glow passes and `crt_ps` on RGBA8 (headless) and BGRA8 (the swapchain), so
`rd_PrecreatePipelines` makes them at start-up.

**Ordering.** The deferred text and the overlay draw after it, unfiltered:
the Enhanced preset's menu text stays sharp and is not curved (in a curved
mode it sits where the flat picture has it). The Original preset's menu
text is in DISPLAY and is filtered with the picture.

## 16. Frame rate and interpolation

`port/render/rd_interp.c`, `rd_core.c`, `port/platform/window_host.c`,
`port/game/video_options.c`; the cut hooks in `camera-root.c`,
`camera-ico2.c` and `StageManager.c`; the draw keys in `RegistPacket.c`,
`GifPacket.c`, `DisplayFont.c`, `layout_texture.c`, `jimaku.c` and
`port/ui/`.

**Why a fixed tick and an interpolated picture.** The simulation keeps the
PS2's cadence exactly: `main_host.c` steps one simulated vsync
(`ico_host_step`) at a time, 20 ms PAL or 16.68 ms NTSC, and the game
updates every second vsync. Game logic, timing and recordings are therefore
identical at any display rate. Smooth motion comes from presenting pictures
between two closed frames, blended from the recorded draw data. The game
is never asked to produce an in-between state.

**Setting.** `[video] framerate`: `"original"`, `"uncapped"` (the default)
or a number from 30 to 1000 (`ico_video_parse_framerate`). It applies in
both presets; anything other than `"original"` sets
`RdSettings.interpolate`. In the Original preset each tick's picture is
still the PS2-exact one and only the presents between ticks blend.
`"original"` replays and presents each frame once inside `rd_EndFrame`, and
the window sleeps to the next vsync deadline, so the picture is held for
the tick's two refreshes.

**The loop** (`ico_window_pace`). With interpolation, `rd_EndFrame` only
closes the frame, and the pace presents until the next vsync deadline
instead of sleeping. Each present is `rd_Present(alpha)`, alpha the
position of the present clock between the last two closes, clamped to
[0, 0.999]. The present clock (`rd_PresentClockAlpha`) advances by the
running mean of the intervals between presents and moves its phase toward
the measured time by an eighth of the error per present, so alpha advances
in even steps instead of carrying the jitter of the step and the sleeps
(0.0099 rms error per step against 0.0719 from the raw time in the test).
`"uncapped"` presents at most twice a refresh in mailbox and once a refresh
under FIFO; a number N waits at least 1/N s between presents. Behind the
deadline, or when the last present's cost would end past the next step's
deadline, only a frame's first present is made, so presenting never costs
the simulation time: a slow driver gets one present per game frame, as with
`"original"`. A movie on the output makes `rd_Present` return false and the
pace sleeps.

**Latency.** Alpha 0 is the previous frame's data in the current frame's
structure, alpha 1 the current frame: presentation is one tick (40 ms PAL)
behind the simulation. Frames live in a ring of three (`RD_FRAME_RING`:
recording, current, previous), so the pair survives while the game records
the next frame.

**What blends** (`rd__InterpFrame`). The output frame is a copy of the
current frame: its lists, state commands, textures, targets and unkeyed
draws exactly as recorded. A keyed draw (`RdKey`, 0 meaning never) takes its
data blended from the draw of the same type, list and key in the previous
frame, the n-th occurrence matching the n-th:

| command | blended | kept from the current frame |
|---|---|---|
| `RDC_MESH`, `RDC_SKINNED` | VuCB qw 2 (the UV scroll, unwrapped the short way; the cluster fade alpha), qw 4..15, the model matrices qw 16..27, the light matrices qw 28..35 (below), the bones | qw 0, 1, 3, the mesh |
| `RDC_GRID` | the VU block; each vertex's position, and normal when lit | strip headers, colours, STs |
| `RDC_PARTICLES` | the VU block; each particle's position and size | header, UV, grey, alpha |
| `RDC_SCREEN` | XY, Z and RGBA of every vertex; unmatched, the alpha (below) | STQ, prim, space |
| `RDC_SHADOW_STRIP` | the volume's prisms (below); unmatched, drawn on the nearer tick's side | |
| `RDC_OVERLAY_TEXT` | an item's anchor, glow stretch and colour; an op's colour and level | the string, size and flags (which must match) |
| frame camera, VU common block | one rigid camera for the frame (below) | `cut` |

Blends are `(1 − t) p + t c`, exact at both ends; a bit-identical pair, or
one that is not finite, keeps the current value; integers round to nearest.

**Rotations.** Matrices blended element by element shorten a rotation's
axes by cos(θ/2) half way: a bone turning 77 degrees in a tick, seen in
stage 3, was drawn 22 % short, which looked like a wobble. So a normal
program's model matrices and a skinned draw's bones are blended as
rotations: the model-to-world part is polar-decomposed (Higham's
iteration) into a rotation, slerped, and a stretch, lerped. Each matrix
turns about a pivot whose image follows the straight line between its two
ticks' images: for a bone the weighted centroid of the vertices bound to it
(a bone is the node times the bind inverse, so its translation is not the
joint, and turning about it detached limbs), for a rigid mesh its vertices'
centroid. A turn over `RD_INTERP_TURN_SNAP` (120 degrees a tick) is taken as
a flip and that matrix is the tick's. Matrices that are not affine, are
singular or change handedness keep the element-wise blend.

**Light matrices.** A lit normal program (`normal_l`, `RD_PROG_LIT`,
`RD_PROG_LIT_SPEC`) computes `l = max0(L1 n)` from the model-space normal and
`c = max0(L2 l)` (VU1_PROGRAMS.md "normal_l"). L1 (qw 28..31) has the three
lights' directions as rows 0..2 and (0, 0, 0, 1) as row 3, which carries
`n.w` into `l.w`; L2 (qw 32..35) has the three lights' colours as columns
0..2 and the ambient colour as column 3. `RegistPacket.c` builds L1 as
Light.c's normal light matrix Ln (`_MakeNormalLightMatrix`: the negated unit
directions, an unused light a zero row) times the node's 3 x 3, which is the
model to world W's. Element by element a turning object's rows shorten by
cos(θ/2) half way, so its lighting dimmed (29 % at 90 degrees a tick). Now
the world-space part Ln = L1 W⁻¹ blends element-wise (between ticks the
lights change only as the nearest lights and their strengths do) and is
multiplied by the blended W(t) of the model matrices, so the lights turn
with the object at full strength (a 90-degree turn keeps the luminance
within 1 % at t = 0.5, 1.000000 in `rd_interp_test`, against 0.707
element-wise). Row 3,
column 3 and L2 (colours and ambient) blend element-wise; a flip (over
`RD_INTERP_TURN_SNAP`) takes the tick's L1 with its model matrices. The
cluster and grid programs' lights are in world space (the bones and the
grid carry the motion) and blend element-wise.

**The camera.** The whole frame is drawn through one rigid camera: the two
ticks' inverse views blended with the eye lerped and the rotation slerped
(`camSetup`), and the projection lerped, since the game eases its zoom
(about 2 % a tick in stage 3) and the projection is part of what moves.
Every VU block through the frame's camera (its inverse view times the
tick's view is the identity to 2e-3, and its world-to-screen is proj43 times
the view: `camOf`) is re-based onto it (`camRebase`), including draws that
are the current tick's (unmatched, mismatched, jumped, unkeyed), which then
appear through the half-way camera instead of standing at the tick. Without
this, a packet entering the screen (culled the tick before) was drawn up to
13 px from where its neighbours put it. With the same view and projection in
both ticks nothing is touched, bit for bit. Draws through another camera (a
reflection's scope) keep the element-wise blend. CPU-projected draws
(`RDC_SCREEN`, `RDC_SHADOW_STRIP`) hold GS positions: matched ones blend in
screen space, which for static geometry lands within 0.26 GS pixels of the
rigid camera's projection; unmatched ones are faded or switched (below).

**Unmatched screen prims and shadow volumes.** A CPU-projected draw with no
partner in the other tick (a packet culled in one tick, a string that
changed, a caster that came or went) cannot be re-based. Standing at the
tick, a draw of the current tick only appeared a tick early and one of the
previous tick only vanished a tick early. Now a screen prim whose blend
fades with its vertex alpha (GS ALPHA with C = As and D = Cd: `LERP_AS`,
`CS_AS_ADD_CD`, `CD_SUB_CS_AS`, `CD_AS_ADD_CD`; ABE on, PABE off, since with
PABE a pixel whose alpha drops under 0x80 is written unblended; and the
vertex alpha reaching As: untextured, TCC RGB or MODULATE) is drawn with its
alpha times t (the current tick's) or 1 − t (the previous tick's). Any other
screen prim, and every shadow volume, is drawn whole on the nearer tick's
side of t = 0.5. A volume only counts the stencil and `RDC_SHADOW_RESOLVE`
darkens every pixel whose count is not 0 by one shadow colour for all the
volumes, so there is no alpha to fade and a partial volume would unbalance
the count; the half-way switch is the rule the prism blend already applies
to a prism of one tick only. A draw of the previous tick is inserted after
the match of the keyed draw before it in its list (else before the match of
the one after it), bracketed by state commands that set its own tick's
state and restore the current one; a volume only beside a volume or before
the list's `RDC_SHADOW_RESOLVE`, and only when the colour and depth targets
agree, otherwise it is not drawn, as before. Alpha 0 then shows the previous
tick's unmatched draws and none of the current's; alpha 1 is the tick,
byte for byte (nothing blends at 1).

**Shadow volumes.** A volume is one closed prism per caster triangle.
`rd_ShadowTris` tags every vertex with its triangle's place in the call
(`RdScreenVtx.rgba`, which the volume draw never reads), and `blendPrisms`
regroups each tick's prisms, aligns the two sequences by dynamic
programming (a pair costs the mean square distance of its caster triangles
once the volume's median shift is taken out; an unmatched prism costs
`RD_INTERP_PRISM_GAP`, 32 GS pixels, squared), lerps paired prisms vertex
by vertex and gives each triangle the sign `emitVolumeStrip` would give the
blended geometry. A prism of one tick only is drawn on the nearer tick's
side of t = 0.5, moved by the volume's shift. Blending triangles in sorted
order instead joined faces of different prisms and leaked shadow; against
a ground truth (ticks N − 1 and N + 1 blended at 0.5 against tick N) this
halves the mean error. Untagged volumes (older dumps) move by the vertex
median shift; a shift over `RD_INTERP_JUMP_SCREEN` jumps.

**Morphs.** A morphing part draws `grp->packets` in odd frames and
`grp->morph` in even ones, each its own mesh, and `reg_setShape` rewrites a
stream while the next frame records. `rd_mesh.c` keeps the two last
replaced streams with the frames that drew them (`rd__MeshStreamAt`), and
`rd__InterpFrame` gives a morphing draw a scratch mesh (up to
`RD_INTERP_SCRATCH`, 256 a present; past it the live stream) holding the
current stream with each vertex's position and normal blended from the
previous one. A morph is therefore shown with its own tick's shape, half
way between the ticks. A scratch mesh holds a copy of its mesh's stream,
indices and batches; the log's third `interp:` line gives the most scratch
meshes a present used and their bytes. A mesh rewritten more than once
while a frame records (two `reg_setShape` calls, or a twin rewritten twice
before its frame) keeps the version a retained frame drew, since the first
rewrite keeps it and the later ones replace a stream no frame drew; the
frame then draws the last version. So a double rewrite blends from the
previous tick's shape to the last one (`rd_interp_test`). A mesh drawn twice
in one frame with a rewrite between its draws gives both draws the last
version, in the tick's own replay too.

**Keys.** The call sites build keys that are stable from frame to frame:

| path | key | where |
|---|---|---|
| VU meshes | `RD_KEY(object, part, packet × 4 + pass)`: the object, the part index, the packet's place in the part's chain (the same in `grp->packets` and `grp->morph`), the pass (material, specular, reflection) | `RegistPacket.c` (`regKeyPart`, `regKeyOrdinal`, `regHostMesh`) |
| grids, shadow volumes | the Mesh3D and list; the object | `Primitive.c`, `Shadow.c` |
| particle batches | the emitter (`prim_DispParticle`'s `PrimParticle`, `mc_HostParticleKey`) and code 18, the n-th batch of an emitter matching the n-th; a batch drawn outside it: the list and code 18, matched by order | `Primitive.c`, `MicroCode.c`, `rd_mesh.c` |
| fade, letterbox | the post kind | `rd_post.c` |
| the decoder's 2D | `gif_HostDrawKey(obj, part, ordinal)` keys every primitive decoded after it | `GifPacket.c`, `GifHost.h` |
| layout rows | the row's `texProperty` entry; part 0 the sprite, part 1 its glow | `layout_texture.c` |
| subtitles | the subtitle's block, a part per row | `jimaku.c` |
| `font_Print` | FNV-1a of the string (`gif_HostDrawKeyText`) | `DisplayFont.c` |
| the port's text and rects | FNV-1a of the string, the alignment, the atlas page and the owner (`ui_SetDrawKey`); rects only under an owner | `port/ui/font.c`, `layout_ext.c` |
| deferred text items, their ops | the quads' key with a page no atlas has; the post kind | `port/ui/font.c` `ui_DrawTextDeferred`, `rd_post.c` |

Glyphs match within a draw by order, so a string that moves or fades blends
glyph for glyph, and a changed string is a new key. The menu sparkle,
lightning, the debug font and anything drawn outside these sites stay
unkeyed and show the current frame.

**What snaps.** A keyed draw is the current frame's when the previous frame
has no match; when its shape differs (mesh layout, program, payload size,
batch range, vertex or particle count, prim or space; a mesh's clip code
and mode may differ, since only the clipping changes); or when it jumped:
the model origin moved more than `RD_INTERP_JUMP_WORLD` (300, the game's
centimetres) in the tick, or a screen vertex more than
`RD_INTERP_JUMP_SCREEN` (256 GS pixels). A particle that moved more than
four times its size, or whose alpha is 0 in either frame, keeps the current
position. The whole frame is the current one (`rd__InterpSnap`) without a
closed previous frame, when the frame numbers are not consecutive, when
either frame is a keep frame, on a camera cut, after the targets were
recreated, when the scene size changed, at a fade edge (either frame's fade
at 0x80 or more: the scene can change behind black), and when the camera
turned more than `RD_INTERP_CAMERA_TURN` (30 degrees) or moved more than
`RD_INTERP_CAMERA_MOVE` (300) in the tick.

**The cut signal.** Game code calls `ico_video_camera_cut()` (a counter read
by no game code) at a stage's first camera (`InitCamera`), a camera mode
change at the end of `SetCameraMatrix`, the cut back to the game camera, an
`InsertCamera_Exec` with cut type 0, a stage load
(`start_stage_Load_thread`) and `camera-ico2.c`'s re-initialisation when
the camera group changes. The window compares the counter once per vsync
and calls `rd_CameraCut()`, which marks the frame being recorded. The
camera thresholds above catch a cut no hook names.

**Held at the tick.** Everything unkeyed or carried by state: CLUT
animation, the dissolve FIX, film noise, flickering effects, and the aura's
feedback: sprites whose target is FEED128 are dropped from every present
but a tick's first, so FEED128 advances once per tick as on the PS2.

**Feedback per present.** The motion blur sprite runs at every present,
standing for dt ticks. On the PS2 DISPLAY keeps a = (128 − FIX) / 128 of
itself per tick; `rd__BlurFeedbackFix` gives FIX' = round(128 − 128 a^dt)
(additive and subtractive forms: FIX × dt), so k presents of dt = 1/k keep
a per tick at any rate and the trail has the same length. dt is clamped to
[1/256, 4]; a short dt rounds (FIX 32 at dt 1/6 wants 5.99 and gets 6).

**Determinism.** `rd_interp.c` reads the retained frames and writes only its
own copy; the only game-side addition is the cut counter, which no game
state reads. Headless traces with and without interpolation are
byte-identical.

**Diagnostics.** Every 250 frames the log has `interp:` lines: frames
blended and snapped by reason, keyed draws blended, unmatched, mismatched
(by reason) and jumped, rotation blends, shadow volumes moved, VU draws
re-based onto the blended camera, unmatched screen prims and shadow volumes
faded or switched at half way and those not placed, lit draws whose lights
turned with them, the scratch meshes' most and bytes, and up to six
`interp: flapping draw` lines naming draws whose outcome changed four or
more times.
`ICO_RD_S2_LEGACY=1` in the environment turns off the rotation blend, the
rigid camera, the prism blend, the unquantised positions and the present
clock, for A/B comparison.

## 17. Mirror mode

`rd_present.c` (the flip), `rd_replay.c` (`mirrorDraw`, the scissor),
`rd_core.c` (`rd_SetMirror`, `rd_MirrorActive`), `rd_video.c`. The option is
a gameplay one (SETTINGS.md, SAVES.md); `port/ui/settings.c` registers
`rd_SetMirror` as the listener of `ico_opt_set_mirror`, so the New Game
choice, a load and the title's reset reach the renderer at once. The flag
is read at replay and at present, and each change is logged (`rd: mirror
mode on (frame N)`).

**The present.** The presenter samples its source right to left when the
mirror is on. Every present goes through that step, so Original, Enhanced
and every interpolated present flip alike. With the mirror off the blit is
unchanged. With it on the output is the exact horizontal flip when the
box's horizontal scale is a power of two; at other scales mirrored bilinear
weights may round differently by 1 LSB.

**The UI flip.** The 2D that must read normally is flipped back where it is
drawn: a screen-prim command tagged `RD_SPACE_UI` replayed into SCENE or
DISPLAY has its vertices reflected about the target's centre, and its
scissor mirrored. Draws into any other target are never flipped, since later
passes sample them in GS coordinates. WORLD and FULLSCREEN prims, meshes,
shadows, the decoder's world prims and the post passes flip with the
present: they are the world. No winding, culling (`cullNone` everywhere),
VU program, shadow sign, DATE or feedback path changes.

**The developer overlay.** `debug.c`'s text is UI like the rest: the glyph
points are raw register writes in list 12 (UI by the list rule of
`GifPacket.c` `gsSpace`), the backdrops, bars and the font window are
`gif_Sprite`/`gif_Line` calls (UI helpers), so all of it is flipped where it
is drawn and the present flips it back. `rd_debug` draws "F7" (asymmetric),
the font window and the developer menu with the mirror off and on, into
SCENE and into DISPLAY, in Original and at scene scale 2, and requires the
mirrored target to be the exact horizontal flip of the unmirrored one, every
byte. (`rd_replay_tool --mirror` has no present to flip back, so it shows
all UI, the overlay included, mirrored; the TODO entry that said the
developer font reads mirrored did not reproduce in the game's path.)

The flip is done at replay rather than at record time: the interpolation
blends the recorded XY and the reflection is affine, so flipping after the
blend equals blending flipped frames; the reflection needs the bound
target's width and scale, which the replay knows; and a frame dump stays a
record of what the game drew (`rd_replay_tool --mirror` shows any dump
mirrored).

**Exactness.** The GS covers pixel p when x0 ≤ p < x1. Reflected, the open
and closed ends swap; on the 1/16-pixel grid that is the same as reflecting
X' = C − 1 + 1/16 − X with C = 2(ox + w/2), which gives exactly the
mirrored pixels for any 12.4 edge. Each triangle's UVs are moved back by a
sixteenth of a pixel of their x gradient, so textured sprites sample the
same texel at the mirrored pixel. Colours and Z are not moved (the layout's
and the font's colours are flat).

**The reduction.** The GS samples SCENE at u = x + 0.75 for DISPLAY pixel
x (texels x and x + 1 at 12/16 and 4/16), a bias that is not
mirror-symmetric: with the UI drawn flipped and the present flipping it
back, a glyph edge's 25 % blend would sit on its other side. With the
mirror on, the replay moves the reduction's U back by half a texel
(`rd__BlurUvRect`, `rd_blur.c`), so it samples at u = x + 0.25 (texels
x − 1 and x at 4/16 and 12/16), the mirror image of the unmirrored sample
points; the crop scissor and the clear are symmetric already and V is
unchanged. DISPLAY with the mirror on is then the exact flip of DISPLAY
with it off for the UI, so the presented UI is the same pixels either way
(`rd_mirror`, Original and Enhanced at 1x and 2x; on a scaled target the
hardware sprite's U moves the same half texel, so its samples are
x ± s/4 in the target's texels), and
the world, which the present flips, is reduced as the PS2 would reduce the
flipped picture. With the mirror off nothing changes: the GS's 0.75 stays.

**The overlay.** The presentation overlay (section 15) is drawn after the
flip and is never flipped: the popups read normally without a pre-flip and
stay at the right of the picture. The deferred text is not flipped either:
it is drawn where the pre-flipped and present-flipped quads would show
(section 15, "The deferred text pass").

**FMV.** `rd_video.c` draws the film mirrored exactly when the mirror is on;
the audio pan follows the mirror mode too (AUDIO.md, FMV.md).

## 18. Performance

`rd_perf.c`, `rd_replay.c`, `rd_core.c`, `rd_mesh.c`, `rd_pipeline.c`;
`port/rhi/` (counters, timestamps, the mailbox mode);
`port/platform/window_host.c`, `host_loop.c`, `host_config.c`.

**Instrumentation.** Every replay fills an `RdPerfRecord` (`rd.h`):
`interpMs` (the blended copy), `waitMs` (`rhi_WaitFrame`: the GPU finishing
the frame `RHI_FRAMES_IN_FLIGHT` replays ago), `uploadMs`, `acquireMs`,
`walkMs` (the command lists), `bindMs` (bind group creation), `submitMs`,
`presentMs`, `readbackMs`, `fenceWaitMs`, the `RhiStats` deltas (resources
created and destroyed, bind groups, binds, draws, passes, barriers, copies,
waits, readbacks), rd's own counts (texture and mesh uploads, target clears,
DATE snapshots, exact blends, pipelines created, ring bytes, uniform and
texture bind groups), and GPU
timestamps at the start, after the uploads, after each of the 13 lists and
after the present, read `RHI_FRAMES_IN_FLIGHT` replays later without
waiting. `rd_PerfPop` hands the records out. Every 10 s the window logs a
`window:` presents line, the phase and GPU averages, the simulation step,
and the counts. `[dev] perf_log = true` writes every record to
`logs/ico-pc-perf.csv` (including `start_ms`, `alpha` and `first_of_tick`).
`rd_perf_test --dump FILE [--enhanced] [--resolution N] [--full-height]
[--precreate] [--repeat N]` times a dump.

A step longer than `[dev] slow_step_ms` (8 ms by default, 0 off) logs a
`window: slow step` line with the step's phases (`ico_host_step_profile`:
vsync callbacks, audio, the game threads and their fiber switches, the
achievements poll), the pump, the disc reads, texture decodes, pipelines
created and the memory card's state, at most five a block. F11 switches the
stats lines to every second for 30 s.

**What the frame path avoids, and why.**

- No pipeline compiles in a replay: `rd_PrecreatePipelines` after
  `rd_Init` (section 4).
- No device allocation per frame: the temporary target pool (section 6).
- No mesh data in the ring: the device arena (section 10). The ring holds
  the VU blocks and bones (per-draw uniforms) and screen vertices.
- No reads of the ring: on NVIDIA with resizable BAR the ring is
  write-combined device memory, where reads are uncached, so vertices are
  built in local scratch and copied in whole.
- One uniform bind group per layout and replay (package PA). FrameCB,
  DrawCB, VuCB and VuBoneCB are `RHI_BIND_UNIFORM_BUFFER_DYNAMIC` slots
  (Vulkan `UNIFORM_BUFFER_DYNAMIC`, D3D12 root CBVs): each block is written
  into the ring at a `uniformAlign` offset, as before, and the draw binds its
  layout's one group with that offset (`rd__BindUniform`,
  `rhi_CmdSetBindGroupOffsets`); a DrawCB repeated within a replay is still
  written once (cached by content). The VU group also binds the stream's
  whole buffer at t0 and the draw finds its stream through `vu_draw.x` (its
  first qword in that buffer), so it is one group per stream buffer: the
  mesh arena chunk, and the ring for the streams drawn from it (particles,
  grids, a mesh with no device copy). A stream past `maxStorageRange` falls
  back to a group of its own at the stream's offset. Texture groups stay
  cached per (texture, sampler, DATE snapshot). The Vulkan backend skips
  re-binding a set already bound with the same offsets; the pipeline lookup
  is a hash. `RdPerfRecord.uniformGroups` and `textureGroups` count the two
  kinds; `rd_perf` checks that its synthetic frame (frame, draw and VU
  layouts, meshes in one arena chunk) creates exactly 3 uniform groups a
  replay, and `rd_replay_tool --stats` prints a dump's counts.

  Bind groups created per replay on the dump corpus (`rd_perf_test --dump`,
  Original preset, before on main at 1e2ebd4a, after with package PA; the
  renders of all 75 corpus PNGs are byte-identical between the two):

  | dump | before | after (uniform + texture) |
  | --- | --- | --- |
  | boot 200, 400, 600 | 589, 373, 818 | 70, 65, 80 (4 + 66, 61, 76) |
  | lightning 200, 300 | 2264, 2193 | 85, 82 (4 + 81, 78) |
  | plain 200, 300 | 1396, 1521 | 72, 98 (4 + 68, 94) |
  | puddle 200, 300 | 597, 595 | 81, 81 (4 + 77) |
  | queen 200, 300 | 1662, 1661 | 53, 53 (4 + 49) |
  | the load frames (100) | 15 | 7 (2 + 5) |

  Gameplay frames make 4 uniform groups: frame, draw and two VU groups (the
  arena chunk and the ring); the load frames only frame and draw. On
  lavapipe the bind phase of lightning 200 went from 7.9 ms to 0.37 ms a
  replay (mean of 30). Bind group binds are unchanged (a draw still binds
  its group with its own offsets).
- The texture upload walk runs only while a texture is dirty or the filter
  changed (`RdContext.texDirtyCount`). A texture uploaded on 60 replays in a
  row is named in the log (in the game, only the title's sea, a CLUT
  animation).
- No `vkDeviceWaitIdle`, queue wait or readback in the frame path:
  `rhi_WaitIdle` runs at start-up, resize, option changes and shutdown;
  readbacks only for dumps and tests. DATE snapshots and the stencil resolve
  are GPU passes.
- `rhi_CmdCopyBuffer` waits for earlier reads of its destination, so an
  arena range freed while the GPU may still read it can be reused at once.
- Barriers only where a resource has a hazard (package PB). The Vulkan
  backend used to put a global memory barrier before every render pass and
  copy to order same-state writes; it now records, per texture and buffer,
  what touched it since its last barrier and emits an image (buffer) barrier
  only for a pass's targets or a copy's resources that were written since
  (or, for a write, read as read-only depth), in the layout the use needs.
  Layout changes stay the renderer's `rd__Transition` calls. Each command
  list opens with one global barrier, so nothing is tracked across
  submissions or frames in flight. `ICO_VK_GLOBAL_BARRIERS=1` restores the
  old path; `port/rhi/vk/README.md` ("Hazard tracking") has the model.
  `rd_replay_tool --stats` prints the barrier and copy counts, and `rd_perf`
  checks its synthetic frame's: 36 a steady replay and 39 a recorded frame
  (44 and 48 on the global path).

  Pipeline barriers of a steady replay (`rd_perf_test --dump`, Original
  preset; main at 81318c6d, the global path with
  `ICO_VK_GLOBAL_BARRIERS=1`, which records the same counts, and the
  tracked path; the 75 corpus PNGs are byte-identical to main's):

  | dump | passes, copies | main | tracked |
  | --- | --- | --- | --- |
  | boot 200, 400, 600 | 58 + 13, 51 + 8, 53 + 9 | 196, 161, 171 | 140, 116, 123 |
  | lightning 200, 300 | 29 + 6 | 94 | 71 |
  | plain 200, 300 | 28 + 6 | 89 | 68 |
  | puddle 200, 300 | 60 + 11 | 201 | 147 |
  | queen 200, 300 | 29 + 6 | 94 | 71 |
  | the load frames (100) | 6 + 0 | 12 | 9 |

  The rest are the renderer's transitions (main's count less one per pass
  and copy: 125 on boot 200, 55 on plain 200), the list's opening barrier,
  and the hazard barriers proper (boot 200: 14, plain 200: 12; on plain 200
  every one is a pass on a target the previous pass on it wrote with no
  transition between). The first replay of a dump, with its texture
  uploads (two transitions a texture), goes from 537 to 438 on boot 200
  (`--stats`). On lavapipe the replay time did not change measurably: the
  mean steady replays (`rd_perf_test --dump --repeat 10`) of main, the
  global path and the tracked path differ by up to 7 % on the gameplay
  dumps and more on the 20 ms load frames, in both directions, with other
  work on the machine. No GPU driver has been measured.

  The Khronos validation layer's synchronisation validation (1.4.309, the
  copy `tools/fetch_deps.sh` unpacks) reports nothing on the `rd_*` and
  `rhi_*` tests. It does report a copy written twice without a barrier,
  but it reported nothing either when the attachment hazard barriers were
  deliberately dropped (a local experiment on plain 200), so it does not
  check same-target render passes under dynamic rendering in this version:
  for those the barrier counts above and `rd_perf`'s are the check.

**Logging cost.** On Windows stdout and stderr are fully buffered and the
host loop calls `ico_host_log_flush` once per vsync, after the step. The
mingw build's msvcrt has no line buffering, and an unbuffered stream to a
file writes each character with its own `_write` (a seek to the end first
in append mode): on a network drive that was 0.26 ms a character, and the
10 s stats lines (about 870 characters) cost a 225 ms stall every block.
POSIX keeps the streams unbuffered (glibc formats a whole call before one
`write`). A milestone on the main thread flushes stdio before its own line,
and the crash, abort and watchdog reports flush stdio from a helper thread
they wait on for at most 500 ms.

## 19. Frame dumps and tools

**Frame dumps.** A dump (`rd_dump.c`, `.rddump`) holds one recorded frame:
its 13 lists, state, textures, the VU meshes it draws, and the targets'
contents it needs. An image texture's header word for the view (which only
target views have) holds its texel format, 0 (RGBA8) in every dump written
before R8 textures, so the version did not change and older dumps load as
before. It carries no display options; those are flags of the
replay tool. Keys (`ico-pc.ini`, or `[dev]` in `config.toml`; CONFIG.md):

| key | effect |
|---|---|
| `dump_every=N` | every Nth frame as `rd-NNNNN.rddump` (the renderer's frame number) |
| `dump_dir=` | where (default `dumps` beside the ini) |
| `dump_from=N` | no frame numbered below N |
| `dump_interp=1` | also the previous frame (`rd-NNNNN-prev.rddump`) and the half-way frame (`rd-NNNNN-i50.rddump`) |

`host_config.c` hands them to the renderer as `ICO_RD_DUMP_EVERY`,
`ICO_RD_DUMP_DIR`, `ICO_RD_DUMP_FROM` and `ICO_RD_DUMP_INTERP`, since
`port/render` does not link the platform layer. F12 (`rd_DumpOnDemand`)
writes the last closed frame and a PNG of DISPLAY into `<pref>/dumps` and
flushes the pad recording; TESTING.md "Reporting a visual bug" says what to
send.

**`rd_replay_tool`** (`port/render/tools/rd_replay_tool.c`) replays a dump
headlessly and writes PNGs: `--target NAME` (SCENE, DISPLAY, any named
target), `--present WxH`, `--list` (every command with its list and index),
`--nop L:I` (skip a command), `--mesh` and `--dump-textures` (inspect
inputs; an R8 texture is written as a grey image), `--enhanced`, `--aspect`, `--resolution`, `--full-height`,
`--filter`, `--mirror` (the display options), `--backend`, and
`--overlay-test` (with `--present`: a test pattern on the presentation
overlay; without it the tool registers no overlay), `--no-aa1` (every
`RDC_AA1` dropped: the frame as `rd` drew it before PRIM.AA1 was decoded,
for a before/after pair from one dump), and `--crt MODE` with
`--present` (the CRT filter in `scanlines`, `consumer`, `trinitron` or
`pvm` at full strength; `--crt-strength K` after it sets the strength).

Dumps carry `RD_DUMP_VERSION` (`rd_internal.h`), 5 since package DEF
(`RDC_OVERLAY_TEXT` items and ops with their `RdTextItem` and `RdTextOp`
payloads, and `RDC_SCREEN`'s `RD_SCREEN_TEXT_QUADS`; no new section), 4
before (package AA1: `RDC_AA1` and `RdStateBlock.aa1`); `rd__LoadFrame`
also reads version 4 dumps (no items: they replay as before in every
preset) and version 3 dumps, with AA1 off. A dump holds the items, not the
font: the replay tool links `port/ui/font.c` and installs its renderer, so
an Enhanced `--present` draws them deferred (`--quad-text` draws the quads
instead, a before/after pair from one dump), and `--list` prints each item
and op and marks the quads `text-quads`.

## 20. Tests

| ctest | what |
|---|---|
| `rd_state` | pipeline keys, the reachable set (under 150 screen and post keys), normalisation; the wide scissor; the AA1 bit through a dump, a version 3 dump, the AA1 plans |
| `rd_pixel` | screen prims, blends, DATE, AFAIL against CPU references; a receding STQ strip (Q 1 to 0.25) perspective-correct against the analytic U, a strip with Q 1 affine; the enumeration holding the colour mask 7 and STQ keys; an R8 atlas through `font_ps` byte-identical to the same texels as RGBA8, 1:1 and magnified, and through a dump; an AA1 line and triangle edge against a CPU coverage reference (0 LSB of As on lavapipe) |
| `rd_tex` | TIM2 decode, CLUTs, TEXA, the cache; R8 textures and rectangle updates (the union uploaded alone, read back from the GPU) |
| `rd_mip` | the mip chain size and alpha coverage |
| `rd_gsbase` | `GsBase.c`, `GifPacket.c`, `DisplayList.c`, `DmaPacket.c` compiled as the window build does: the frame head, keep, parity, camera, depth scale, post passes, FBMSK's extent |
| `rd_layout` | the layout's draws and keys |
| `rd_mesh` | VU programs drawn against the CPU reference (0 LSB) |
| `rd_shadow` | the stencil count, levels, composites, tags; at scale 4 the level 1 integral independent of the volume's sub-pixel position |
| `rd_fog` | the Z byte model, the CLUT, the fog pixels |
| `rd_water` | puddle, pool, barrier, water drops, cloth |
| `rd_blur` | every staticBlur effect against the sprite model, feedback over 600 frames through the real reduction |
| `rd_raw` | dark volume, lightning, particles, lines, the wrap path, FBMSK's extent; which lines the decoder records with AA1 |
| `rd_debug` | the debug font and menu; the developer overlay (text, font window, menu) under the mirror mode is the exact flip of the unmirrored one in SCENE and in DISPLAY, Original and 2x |
| `rd_crt` | the CRT filter: the `[video] crt*` options and their save; the modes and overrides; on a device the rich frame's present with the filter off is rd_present's hash and a mode at strength 0 the same bytes, each mode's hash at 960×720 and 1920×1440 (llvmpipe), black outside the box at 1280×720, the Scanlines mode's mean luminance within 20 %, a white frame's mask period equal to the pitch at 1920×1440 (Trinitron 3, PVM 2) and no mask in a 720-line box |
| `rd_present` | presets, scales, widescreen, mips; Original byte-identical; the presentation overlay at 960×720 and 1920×1080 (rects at their pixels, a glyph texel for pixel, unflipped under the mirror, nothing else touched) |
| `font_edge` (port/ui) | the deferred text: edges at 1080p and 2160p, the mirror, the Original present unchanged, the fold of fade, letterbox and keep (UI.md "Tests") |
| `rd_interp` | the blend, snaps, keys, rotations, camera, prisms, feedback; deferred text items and ops |
| `rd_mirror` | the present flip, the UI flip, the mirrored reduction |
| `rd_perf` | nothing created or uploaded in the steady state; DISPLAY unchanged over 200 replays |
| `rd_replay_tool` | the tool on a test dump |
| `rhi_vk`, `rhi_vk_enum`, `rhi_vk_swapchain`, `rhi_d3d12`, `rhi_d3d12_plan` | the backends |

GPU tests exit 77 (skipped) without a device and fail on validation errors.

## 21. Known differences and open work

Differences from the PS2 that are accepted are listed in DIVERGENCES.md.
Renderer work still open, and the facts that need a PS2 capture to settle,
are in docs/TODO.md.

Approximations kept in the code and described with their feature:
PRIM.AA1 (section 4, "PRIM.AA1": PCSX2's software model, with the line
ends, the fringe corners, on-edge sample points and the fringe order of a
Z-writing strip left approximate; sprites and points unaffected).
