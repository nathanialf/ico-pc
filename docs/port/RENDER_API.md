# Render API design

Headers: `port/render/rd.h` (draw API the game's `seki` layer calls),
`port/render/rd_state.h` (the finite GS state the game uses),
`port/rhi/rhi.h` (the backend interface). Prototype:
`port/test/gs_blend_test.c`.

This document fixes the contracts for renderer waves 1 to 8. Changes to
`rd.h`, `rd_state.h` or `rhi.h` after wave 1 starts need a note here and a
grep of every caller.

## 1. Model

The PS2 game assembles 13 priority-ordered DMA chains per frame
(`ico2/seki/src/DisplayList.c`) and kicks them in order at `dl_Swap`. The
GS is one state machine: a register written by list *n* is still in force
when list *n*+1 starts. `gsb_SetGsDefault` (`ico2/seki/src/GsBase.c:638`)
writes defaults only at the head of lists 0, 1, 2, 4, 6 and 7 to 12, so
lists 3 (shadows) and 5 (dissolve) run in whatever state the previous list
left. Submission order inside a list is draw order.

`rd` keeps all of this: a frame is 13 ordered command lists recorded by the
game fiber and replayed in order against one `RdDrawState`. There is no
sort-by-material pass. Replay of a `keep` frame (`fbKeep`, pause and memory
card screens) runs lists 11 and 12 only over the retained DISPLAY target.

The game's post passes are appended to specific lists: shadow composite to
3, depth of field and motion blur to 7, anti-alias to 10, fade, letterbox,
brightness and keep to 11. Lists 8 and 9 (shine levels 2 and 3) therefore
draw after depth of field and motion blur. `rd` does not reorder them.

Coordinates stay as the game produced them: GS 12.4 window coordinates
around the 2048,2048 XYOFFSET centre, GS Z, texel UVs or STQ. One
conversion in `rd_core` applies the GS sampling convention and the preset's
resolution scale.

## 2. GS state inventory

Counted with `grep -a` over `ico2/` (47 files hold EUC-JP bytes and plain
grep skips them). Register encodings are the raw values written.

### ALPHA (blend)

`gif_SetAlpha(abe, mode, fix)` indexes `alphaTable[12]`
(`GifPacket.c:680`). Modes seen as literals, by call count: 5 (52), 4 (21),
2 (28), 7 (14), 0 (12), 1 (2), 6 (1). Material packets
(`Packet.c pac_setMaterialPacket`) write 0x44, 0x48, 0x42 with FIX 0x80.

| mode | ALPHA | equation | use |
|---|---|---|---|
| 0 | 0x68 | Cs·FIX + Cd | shadow accumulate, dissolve in, flare |
| 1 | 0x62 | Cd − Cs·FIX | dissolve out, aura decay |
| 2 | 0x64 | (Cs−Cd)·FIX + Cd | letterbox, motion blur, anti-alias, debug bars |
| 3 | 0x61 | Cd·FIX + Cs | disc data only |
| 4, 7 | 0x44 | (Cs−Cd)·As + Cd | default material, 2D |
| 5 | 0x48 | Cs·As + Cd | additive, specular/reflection with PABE |
| 6 | 0x42 | Cd − Cs·As | subtractive material |
| 8 | 0x58 | Cs·Ad + Cd | disc data only |
| 9 | 0x52 | Cd − Cs·Ad | disc data only |
| 10 | 0x54 | (Cs−Cd)·Ad + Cd | disc data only |
| 11 | 0x49 | Cd·As + Cd | disc data only |

Variable-mode sites:

- `ito/src/lightning.c:283` passes `c`, which `DrawLightningN` receives from
  the stage's BGA lightning record (`BgaLightningDef.c`,
  `seki/src/BgAnimation.c:1509`). It is disc data, so any of the 12 modes is
  possible; `rd_Blend` range-checks it and all 12 pipelines are supported.
- `sugipon/src/enemyParts.c:296,318` pass `PointBlur.alpha`, initialised to
  5 from `pointBlurTemplate` (`enemyParts.c:94`) and never written again:
  mode 5.

FIX literals: 0, 0x10, 0x20, 0x40, 0x60, 0x70, 96, 0x80, 255; runtime
values from `motionBlurAlpha`, shadow blend levels, `blurCol`, and debug.

### TEST

Fourteen literal values (`rd_state.h` names each). Decomposed, the game
uses: alpha test NEVER/ALWAYS/LESS/GREATER with AREF 0, 0x40, 0x81; AFAIL
KEEP, FB_ONLY, RGB_ONLY; DATE off, DATM 0 and DATM 1; Z test ALWAYS and
GEQUAL. ZTE is always set except for one `0` write.

### Other registers

| register | values seen | notes |
|---|---|---|
| ZBUF | mask on / off | ZBP 0xC0 PSMZ32 for the scene; temp targets use their own ZBP |
| TEXA | 80/80, 7F/81+AEM, 80/80+AEM | three modes; bakes per texture for PSMCT16/24 |
| CLAMP | 0, 1, 4, 5 | materials map `wrap` 0..3 → 5, 4, 1, 0 (`Packet.c:853`) |
| TEX1 | 0x60 (28), 0x40 (3), 0x20 (1), 0 | nearest / linear per axis; mips fixed per texture |
| TEX0 TFX | MODULATE, DECAL | plus TCC RGB / RGBA |
| PABE | 0 (9), 1 (1) | specular/reflection passes |
| FBA | 0 (raw), per material | shadow receiver mask |
| COLCLAMP | 1 (4), 0 (3) | 0 only in `Shadow.c` accumulate |
| DTHE, DIMX | never written | no framebuffer dithering to reproduce (default to confirm) |
| FRAME psm | 0 (PSMCT32) everywhere | no 16-bit framebuffer |

Deviations from the planning survey: materials use three equations and
CLAMP 4 (confirmed); mode 3 and 8 to 11 are reachable only through BGA
data; the `enemyParts.c` mode is fixed at 5.

## 3. GS to pipeline mapping

| GS feature | Native implementation |
|---|---|
| Alpha scale 0x80 = 1.0 | Dual-source blend: output 0 carries colour and the stored GS alpha, output 1 the factor As/128 or FIX/128 (`RHI_BF_SRC1_*`), required on every backend (Vulkan `dualSrcBlend`, D3D12 and Metal always). This is exact only while the factor is at most 1.0: on UNORM targets the blender clamps fixed-point factors to 0..1 (measured on llvmpipe, `shaders_pixel` cell 8: `Cs*As + Cd` with As 0xFF gives 128 where the GS gives 191; the Vulkan spec clamps the same way), so a dual-source factor above 0x80 is **not** exact. |
| Factor above 0x80 in `Cs·F + Cd`, `Cd − Cs·F` (modes 0, 1, 5, 6) | `DF_PREMUL`: the shader writes the GS term `min((Cs·F) >> 7, 255)` and the pipeline adds it (`ONE`, `ONE`) or reverse-subtracts it, exact for F up to 0xFF. `rd_core` uses this path for these four modes always, not only when F can exceed 0x80, since As can always reach 0xFF. |
| Factor above 0x80 in a LERP (modes 2, 4, 7) | Not representable: the GS weight on Cd goes negative. FIX is clamped to 0x80 by `rd_core` (the draw writes Cs); As is clamped by the hardware (same result). Chosen over routing to `blend_int` because these are ordinary draws into SCENE, not fullscreen feedback passes; revisit if a site with As or FIX above 0x80 shows a visible difference. |
| `Cd·FIX + Cs` (mode 3, disc data only) | Dual-source destination factor (`ONE`, `SRC1_COLOR`); FIX above 0x80 is clamped to 0x80, i.e. `Cd + Cs`. Same reasoning as the LERPs. |
| Ad factors (modes 8 to 10, disc data only) | `RHI_BF_DST_ALPHA`, which reads Ad/255 rather than Ad/128: half strength. The 2x fold through the fragment colour planned here does not work (the colour output is clamped to 1.0 before blending). Untested; open item 6. Exact alternatives need a shader read of the destination (DATE-style snapshot) or `blend_int`. |
| `Cd·As + Cd` (mode 11, disc data only) | Needs a factor above 1.0 on Cd; not representable with the current shaders. The draw leaves Cd unchanged and is reported once. |
| AFAIL FB_ONLY (0x5140D) | Two draws: alpha > ref with depth write, then alpha ≤ ref with depth write off (the split pass is a DrawCB uniform; the pipelines differ by Z write). Self-overlap order within a strip can differ; accepted, see DIVERGENCES.md when observed. |
| AFAIL RGB_ONLY with ATST NEVER (0x3F001, 0x33001) | Colour mask RGB, depth write off, no alpha test. |
| DATE | Applied since wave 2 (R2a). A screen draw with TEST.DATE first takes an R8 snapshot of its target's alpha MSB into `RD_TARGET_DATE_SNAPSHOT` (`date_snap_ps`, pixel for pixel, `rd_replay.c dateSnapshot`), then `sprite_ps` loads it at t2 and discards where the MSB differs from DATM (`DF_DATE`, `DF_DATM`, uniforms: the pipeline key keeps date 0). The snapshot is retaken when the target changes or anything since may have written alpha (a clear, a copy, an exact blend, a draw whose colour mask includes A), so consecutive DATE draws see each other's writes as on the GS; overlapping primitives inside one draw see the snapshot (accepted). D3D12 cannot read the bound target and stencil export is not universal, so no feedback loops. |
| COLCLAMP 0 (shadow count) | Stencil increment/decrement wrap on the scene depth-stencil; `RD_POST_SHADOW_RESOLVE` writes stencil ≠ 0 into SHADOW0, then the original 256/128/64 blur chain runs. |
| FBA | Fragment shader forces alpha MSB; per material. |
| PABE | Fragment shader: blend factor 0 when As MSB clear (dual-source output 1 = 0 and output 0 alpha path unchanged). |
| Z | D32F (D32F_S8 on SCENE). The shaders map GS Z to depth `1 − gsZ / 2^24` (`gs_z_to_depth`), so a larger GS Z is a smaller depth: GS ZTST GEQUAL becomes `RHI_CMP_LEQUAL` and GREATER becomes `LESS`; a clear to GS Z `z` clears depth to `1 − z / 2^24` (`rd_pipeline.c`, `rd_replay.c`). GS Z integers below 2^24 are exact in float. (Wave 0 wrote this row as "reversed-Z with GEQUAL", which contradicts the shader mapping; the `rhi.h` comment on `depthCompare` has the same slip.) |
| ZTE = 0 | Treated as Z ALWAYS with write enabled (GS manual: prohibited setting; one site writes TEST 0). |
| Texture function | Fragment shader integer path in Original: `min((tex·col) >> 7, 255)`; float in Enhanced. |
| Vertex colour | Truncated as VU `ftoi` and clamped at 255. |
| Feedback passes (any mode, any factor) | Exact GS integer blend in `blend_int` (`RdPostParams.exactInt`, `RDC_EXACT_BLEND`): source and destination are copied into `RHI_FMT_RGBA8_UINT` textures (a raw copy between the size-compatible formats), the shader writes a third, and the result is copied back into the destination. Exact for every mode, including FIX/As above 0x80 and the Ad modes; source and destination must be the same size. See section 7. |

Pipeline key: `RdPipelineKey` (program, blend or none, alpha test, AFAIL
split pass, DATE, Z test, Z write, PABE, FBA, colour mask, stencil mode,
target format, topology). AREF, FIX, sampler state and UV offset are
uniforms or sampler objects, not pipeline state. The shaders also take the
alpha test, the AFAIL split pass, the texture function, TCC, TEXA, FBA and
PABE as uniforms, so `rd_core` normalises those fields of the key (ATST
ALWAYS, split 0, PABE 0, FBA 0; a split pass shows in the key only through
its Z write and colour mask) and stores one representative blend mode per
hardware path. DATE is 0 in the key until `sprite_ps` reads the DATE
snapshot. With the normalisation the screen and post programs reach 67
pipelines from the game's state set (`rd__EnumerateReachable`, asserted
under 100 by `rd_state` and `rd_pixel`); `RD_PIPELINE_CACHE_MAX` is 256 and
`rd_core` asserts above it.

Wave 1 additions to `rd.h` (R1b): `RdTexSrc` and `rd_CreateTextureSrc` (a
PSMCT24/16 texture expanded by the shader under the TEXA state at replay,
so TEXA leaks between lists as on the GS), and a note on `rd_SetTarget`
(XYOFFSET is always centred; `useOffset` adds the field offset; the call
resets the scissor; `depth` names the target whose depth buffer is bound).
No caller exists yet.

Wave 2 additions to `rd.h` (R2a), for the seki layer's register decoding
(each raw GS write sets one register, never a group): `rd_ABE` (PRIM.ABE
alone), `rd_BlendFunc` (ALPHA alone, ABE untouched; new command
`RDC_ALPHA`), `rd_Scissor`, `rd_SamplerFilter` (TEX1 alone),
`rd_SamplerWrap` (CLAMP alone), `rd_Gouraud` (PRIM.IIP; new command
`RDC_SHADE` and `RdStateBlock.gouraud`, default 1; flat triangles take the
last vertex's colour, flat lines the second's), `rd_DiscardFrame` and
`rd_FrameOpen` (dl_Clear without dl_Swap drops the open frame unreplayed),
`rd_ResizeOutput` (window resize). The dump format is version 2
(`RdStateBlock` is 80 bytes). Callers: `ico2/seki/src/GifPacket.c`,
`DisplayFont.c`, `DisplayList.c`, `GsBase.c` (R2a hooks),
`port/platform/window_host.c`.

## 4. Backend

Own thin RHI (`port/rhi/rhi.h`) with hand-written Vulkan and D3D12
backends; Metal later. Shaders are one HLSL source compiled at build time
by DXC to SPIR-V and DXIL and embedded as byte arrays. No runtime shader
compiler. SPIRV-Cross is needed only for Metal.

Bind model (fixed, see `rhi.h`): group 0 per-frame uniforms, group 1
per-draw uniforms and storage (bones, particles), group 2 textures and
samplers. Uniform ranges are aligned to `RhiLimits.uniformAlign` (256 on
D3D12).

The RHI has: buffers (device, upload ring, readback), textures (RGBA8
unorm and uint, R8, D32F, D32F+S8), samplers, shaders, bind group layouts,
transient bind groups, pipelines (dual-source blend, stencil wrap,
reversed-Z), render passes with load ops, copies, barriers, readback for
verification. It has no compute, no input attachments, no framebuffer
feedback loops, no push constants (per-draw uniforms live in the ring).

## 5. Targets and buffers

| RdTargetId | GS location | GS size | notes |
|---|---|---|---|
| SCENE | FBP 0x40 (TBP 0x800) | 512×512 PAL, 512×448 NTSC | RGBA8 + D32F_S8 |
| DISPLAY | FBP 0 | 512×256 / 512×224 | the only displayed buffer; retained (motion blur history, keep) |
| SHADOW0..2 | FBP 0x142 and blur levels | 256², 128², 64² | |
| WORK0..3 | TBP 0x2800..0x3000 | 256×128, 256×256 | depth of field, flare, aura |
| AA0, AA1 | TBP 0x2800/0x2C00 | 256², 128² | anti-alias chain (aliases WORK in VRAM; separate here) |
| FEED128 | TBP 0x3F00 | 128² | aura feedback, persistent |
| DATE_SNAPSHOT | n/a | scene size | R8 |

`rd_TempTarget` replaces `tex_AllocVramAuto` for puddle, pool and queen
barrier render-to-texture.

Buffer sizes in the Original preset are literal. In Enhanced the scene
target is output-sized; blur and work targets scale by `outH/448` capped
at 2× so blur radii stay a constant fraction of the screen.

## 6. "Original" preset

- Scene target 512×512 (PAL) or 512×448 (NTSC), RGBA8, D32F+S8.
- Per-texture filtering as the TIM2 ICO block specifies (`smpMag`,
  `smpMin`); mip level fixed per texture from `TexExt.level`, not per pixel.
- Integer texture function and truncated vertex colours in the shader.
- Work buffers at literal PS2 sizes; GS-exact integer blending in feedback
  passes.
- Reduction (`gsb_Reduction`, `GsBase.c:213`): SCENE drawn into DISPLAY at
  half height with bilinear filtering (TEX1 0x60), the per-stage tint, and
  the border crop (2 px left/right; 2 lines NTSC, 8 lines PAL top/bottom).
- Present: DISPLAY into a centred 4:3 rectangle, each line doubled
  vertically, bilinear horizontally. PAL pixel aspect is 4:3.
- No dithering (DTHE never written; default to confirm).
- Simulation and presentation at 25/30 Hz; each frame shown for two
  refreshes; interpolation off. Vsync on.
- Field parity: `odd_even` drives `sceGsSetHalfOffset` (`GsBase.c:968`).
  With frame step 2 the parity is believed constant; the preset fixes one
  parity until confirmed (open item).

Enhanced settings, each independent: output resolution up to 4K, aspect
4:3 to 16:9/16:10 (projection and cull frustum widen; gameplay screen
tests stay 4:3), full-height scene (skip the vertical halving),
trilinear/anisotropic with generated mips, interpolation between
simulation ticks, mirror flip at present.

## 7. Blend exactness under feedback

`port/test/gs_blend_test.c` (600 iterations, 256 lanes) compares the GS
integer blend against a float blender with round-to-nearest:

| case | max LSB | final LSB | frames differing |
|---|---|---|---|
| single LERP_AS pass (baseline) | 1 | 1 | 600/600 |
| motion blur FIX 0x40, static / cuts / noise | 1 / 1 / 2 | 1 / 1 / 2 | 600 / 365 / 600 |
| motion blur FIX 0x70, static / cuts | 4 / 4 | 4 / 4 | 600 / 600 |
| aura in 0x20 decay 0x10; in 0x40 decay 0x08 | 2 / 1 | 2 / 1 | 600 / 600 |
| shadow count wrap, COLCLAMP 0 | 0 | 0 | 0/600 |

The error does not grow without bound but it does not vanish either: the
GS shift truncates toward −∞, so a high-retention LERP stalls one LSB short
of its target where a float blender converges, and the two settle at
different fixed points. At FIX 0x70 that is a steady 4-LSB bias in every
motion-blurred frame, visible as a brightness offset. Decision: feedback
passes (reduction→motion blur, aura FEED128, dissolve, flare accumulation)
run the integer formula in the fragment shader on RGBA8_UINT ping-pong
targets. Single-pass draws use the hardware blender; a 1-LSB tolerance is
the comparison threshold for them.

The shadow wrap test is exact, which supports replacing the wrapped colour
count with stencil.

## 8. Open items to confirm before wave 3

1. MSCALF code 18 ("mode 3" in `mc_SetMicroCode`): which program and what
   it draws. Read `MicroCode.c` and the `.vsm` entry table.
2. `cluster.vsm:460` "front/back slot selection": facing test (needs
   winding handling in `RD_PROG_SKIN`) or XYZ2/XYZ3 kick selection (no
   renderer effect). The file's comments are machine-generated; read the
   instructions.
3. ZFog Z byte: which byte of the PSMZ32 depth the PSMT8H reinterpretation
   plus the PSMT4 block shuffles at `ZFog.c:236` selects. Derive with a
   small host test over the GS swizzle tables before writing the fog shader.
4. Field parity: whether `odd_even` alternates with frame step 2; affects
   the half-pixel offset in the Original preset.
5. DTHE default after `sceGsResetGraph`: libgraph's default decides
   whether the PS2 image was dithered. No emulator is available; derive from
   the libgraph reimplementation in `sce/libgraph` and public documentation.
6. Ad-factor blend modes (8 to 10): whether any shipped stage's BGA
   lightning record uses them; decides whether the DST_ALPHA mapping needs
   verification or stays untested.
7. `gsb_Reduction` and `mv_disp.c` bypass the 13 lists and write DMA
   registers directly; wave 2 routes them through `rd_Post(RD_POST_REDUCTION)`
   and the FMV blit at the same point in the frame.

## 9. The seki layer on rd (wave 2, R2a)

The window build (`ICO_HEADLESS=OFF`, the default; compile definition
`ICO_RD`) compiles `GifPacket.c`, `DisplayList.c`, `DmaPacket.c`,
`DisplayFont.c` and five hooks in `GsBase.c` onto `rd`; the headless build
keeps the original packet code byte for byte, so traces are unchanged.

**Lists and frames.** `dl_SetDLPriority` and `dl_PopPriority` select the rd
list (after flushing the decoder into the list being left).
`dl_Swap` closes the frame with `rd_EndFrame(fbKeep)` (replay and present;
nothing is DMA'd, `sceDmaSend` is not called), then `dl_Clear` discards a
frame still open (the `gsb_UpdateGSSystem(1)` path) and begins the next.
`PacketBufferStruct`, the 13 DMA lists and their tags are still built (the
`gif_*` helpers no longer write their A+D pairs into the packet; raw
writers still do), so heap use and the bookkeeping other code reads are
unchanged.

**GS register decoding** (the plan's `rd_gs_shim.c`, folded into
`GifPacket.c`). Every register write of the 2D layer reaches one decoder in
packet order: the `gif_*` helpers (their bodies are unchanged; `setGsReg`
feeds the decoder), `gif_SetGsReg`, and the A+D pairs other files write
straight into the open packet (`Texture.c`'s TEX0 packet, `GsBase.c`'s
macros), which are decoded at the next `gif_*` entry or at the end of the
packet, so the two stay in order. Decoded: PRIM (ABE, TME, IIP, FST, the
vertex queue), RGBAQ, ST, UV, XYZ2/XYZF2 (kick), XYZ3/XYZF3 (queue only,
as the strip helpers use it), TEX0 (texture seam), TEX1, CLAMP, ALPHA,
TEST, ZBUF (mask), FBA, PABE, TEXA (three modes), COLCLAMP, FRAME (FBP to a
named target, FBMSK to `rd_ColorMask`), XYOFFSET (gives the target size),
SCISSOR, TEXFLUSH and PRMODECONT 1 (nothing to do), DTHE 0. Any other
register is counted per register and logged once by name
(`gif_HostUndecodedCount`). PRIM, TEX0, FRAME, XYOFFSET and SCISSOR are
kept per list, since lists are recorded in any order but replayed 0..12.
Primitives of one kind, space and UV mode are batched into one
`rd_ScreenPrims`; strips and fans become triangle lists.

| FBP / TBP | rd target |
|---|---|
| 0 / 0 | DISPLAY (TEX0 PSMCT24: the RGB24 view) |
| 0x40 / 0x800 | SCENE (with its depth) |
| 0x140 / 0x2800 | AA0, or WORK0 when 128 lines high |
| 0x142 / 0x2840 | SHADOW0 |
| 0x160 / 0x2C00 | AA1, or WORK1 when 256 wide |
| 0x180 / 0x3000 | WORK2 |
| 0x1F8 / 0x3F00 | FEED128 |
| other FBP | SCENE, logged once |

**Space tags.** The 640 x 224 layout helpers (`gif_Sprite`,
`gif_SpriteSensitive`, `gif_SpriteOffset`, `gif_SpriteSensitiveOffset`,
`gif_Point*`, `gif_Line*`) draw `RD_SPACE_UI`; the CPU-projected strips
(`gif_DrawStrip*`, `gif_DrawPolyF4`) `RD_SPACE_WORLD`; raw writes and the
raw-coordinate helpers UI in lists 11 and 12, WORLD elsewhere. Layout,
subtitles (`jimaku.c`) and the font are therefore UI.

**Texture seam.** A TEX0 write binds, when PRIM.TME is on: the RdTex a
registered resolver returns (`gif_HostSetTex0Resolver`, `GifHost.h`; for
package R2b), else the named target of the table above, else a placeholder:
a 16 x 16 checker, one magenta-leaning colour per TBP, every other 4-texel
cell transparent. Until R2b lands every game texture, the font's included,
draws as its placeholder. The texture packets `Texture.c` chains by DMA
reference (`t->pkt`: TEX1 and an alpha-test TEST; `t->uv`: the VU1 UV
scroll; the image transfers) are not decoded: they reach the GS only
through DMA, which the host does not interpret.

**Font.** `font_Print` sets its state on rd directly (Z write on, TEST
0x30000, ALPHA 0x44, TEX1 0x60), lets the decoder bind the "font" TEX0 with
one PRIM write, and draws all glyphs as one `rd_ScreenPrims` of sprites in
UI space (`gif_HostScreenPrims`).

**GsBase hooks** (marked `R2a`, for package R2c to take over): the
reduction as `rd_Post(RD_POST_REDUCTION)` at the end of list 12 of the frame
`dl_Swap` closes, with the tint `gsb_Reduction` just computed (on the PS2
that is the tint of the reduction of the frame that `dl_Swap` kicks); the
flip's draw environment at the head of each frame (`rd_SetTarget(SCENE)`
and a clear to the `gsb_SetBGColor` colour, alpha 0x80, Z 0, in list 0;
`rd_SetTarget(SCENE)` again at the head of list 11 for keep frames, which
replay 11 and 12 only); `rd_ResetScene` when `gsb_Init` changes the frame
size (50/60 Hz).

**Window and pacing** (`port/platform/window_host.c`): an SDL3 window
"ICO" (960 x 720, resizable), `rd_Init` with the Original preset and vsync
on, `ico_host_step` once per real 20 ms (16.683 ms at 60 Hz) by
sleep-until with resynchronisation past 100 ms behind, presentation inside
`rd_EndFrame` after each `gsb_UpdateGSSystem`. Escape or closing the window
exits. ini key: none new; the headless build is the CMake option.

Open questions for R2b and R2c:

1. R2b: the resolver receives TEX0 and the list; Texture.c allocates VRAM
   per list (`tex_AllocVramAuto`), so the TBP alone is ambiguous across
   lists. Calling `rd_Texture` directly from `tex_TransTexture` would also
   do, with the decoder's TEX0 binding left for the work-buffer reads.
2. R2b: `t->pkt` sets TEX1 (per-texture filter) and TEST (ATE GREATER, AREF
   96 or the TIM2 value, AFAIL FB_ONLY) for the material; on rd these need
   `rd_SamplerFilter` and `rd_Test` from `tex_TransTexture` when the
   register packet would be chained (`vramPri[].lastTex != id`).
3. R2c: the SCENE clear at the frame head records the BG colour at the
   frame's start; the PS2 cleared with the colour current at the flip,
   one tick later. The difference shows only on the tick the stage changes
   it.
4. R2c: GS Z of the UI sprites (0xFFFFFF9B, 0xFFFFFFFF) is above 2^24, so
   with `g_z.x = 1/2^24` they all clamp to depth 0. Harmless for Z ALWAYS;
   the scene's ZBUF is PSMZ32, so depth-tested screen prims need the 2^32
   scale (FrameCB `g_z`) decided per target.
5. R2c: `gsb_KeepFrameBuffer`, `gsb_fade`, `gsb_scissorOnDemo`,
   `gsb_controlBrightness`, `gsb_antiAlias` and `gsb_filmNoise` go through
   the decoder today (their packets decode to the sprites they draw);
   moving them to `rd_Post` should keep the state they leak. Only
   `gsb_Reduction` (a stack packet kicked on DMA channel 2) is replaced, by
   the R2a hook.
6. R2c: `gsb_MakeCommonMatrix`, `gsb_SetGsDefault`'s list heads and the
   other DMA chains that are not GIF packets (VU1 data, microprogram
   uploads) are bookkeeping only on the host.

## 10. What the first pixels need

RHI (Vulkan), `rd_core` lists and state, `RD_PROG_SCREEN` and `RD_PROG_POST`
shaders, `GifPacket.c` reimplemented on `rd_*`, TIM2 decode to
`rd_CreateTexture`, and the frame lifecycle in `gsb_UpdateGSSystem`. That
is the title screen, menus and debug font without any mesh or VU shader
work.
