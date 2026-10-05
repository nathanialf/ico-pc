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
| Z | D32F (D32F_S8 on SCENE). The shaders map GS Z to depth `1 − gsZ / 2^24` (`gs_z_to_depth`), so a larger GS Z is a smaller depth: GS ZTST GEQUAL becomes `RHI_CMP_LEQUAL` and GREATER becomes `LESS`; a clear to GS Z `z` clears depth to `1 − z / 2^24` (`rd_pipeline.c`, `rd_replay.c`). GS Z integers below 2^24 are exact in float. (Wave 0 wrote this row as "reversed-Z with GEQUAL", which contradicts the shader mapping; the `rhi.h` comment on `depthCompare` has the same slip.) Wave 2 (R2c): the scale is per depth target, 2^-32 for the game's PSMZ32 (section 12). |
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
- No dithering: the flip's draw environment writes DTHE 0 every frame
  (`sceGsSetDefDrawEnv` with a PSMCT32 frame, `sce/libgraph/graph006.c`),
  and nothing else writes it (section 12).
- Simulation and presentation at 25/30 Hz; each frame shown for two
  refreshes; interpolation off. Vsync on.
- Field parity: the half-line XYOFFSET offset `sceGsSetHalfOffset` puts in
  the flip's draw environment, reproduced from `GS_CSR.FIELD` as the PS2
  read it (section 12): constant while the frame step stays 2, changing
  parity after an odd-length stall.

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
4. Field parity: settled in wave 2 (R2c), section 12.
5. DTHE: settled in wave 2 (R2c): the flip writes DTHE 0 every frame
   (section 12), so the image was not dithered.
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

**GsBase hooks** (marked `R2a`; taken over and replaced by package R2c,
section 12; the paragraph below is the R2a state): the
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

Open questions for R2b and R2c (R2c's, 3 to 6, are answered in section 12):

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

## 11. Textures (wave 2, R2b)

`port/render/rd_tex.h`, `rd_tex.c` (the cache) and `ico2/seki/src/Texture.c`
under `ICO_RD` (the game side). The headless build keeps the original
packet code; only the pointer-width fixes below apply to it.

**Decode.** At load (`tex_initTextureSub`) and on any later cache miss,
from the copies of the TIM2 images and CLUT that `Texture.c` already keeps
per record (`TexData.lv[].addr`, `TexData.clut.addr`, each behind a 32-byte
DMA header). No conversion at extraction time. Formats: PSMCT32, PSMCT24,
PSMCT16, PSMCT16S, PSMT8, PSMT4 (low nibble first) from TIM2, and in the
decoder also PSMT8H, PSMT4HL, PSMT4HH and the PSMZ formats (read as the
colour format of the same size). CLUTs: PSMCT32 or PSMCT16 entries (24-bit
TIM2 CLUTs, which the GS cannot use, as RGB24), read in CSM1 memory order
(a 256-entry CLUT swaps entries 8-15 and 16-23 of every 32; a 16-entry CLUT
is straight). 16-bit texels expand as `c << 3` (the GS does not replicate
the high bits). The texture is padded with zero texels to 2^TW x 2^TH, the
size the GS addresses, so STQ coordinates and REPEAT wrap where they do on
the GS.

**TEXA: applied at replay, not baked.** The cache creates PSMCT16/24 and
16-bit-CLUT textures with `rd_CreateTextureSrc` (alpha byte = the A bit, or
unused for RGB24) and `sprite_ps` applies the TEXA in force at replay
(`gs_texa_alpha`). Reasons: TEXA leaks between lists like every other
register (lists 1 and 2 default to 7F/81+AEM, the others to 80/80, and the
2D layer writes 80/80+AEM in the middle of a list), so a baked texture
would need one copy per mode and a guess at replay time about which one
was in force; one copy per texture is also a third of the memory. The
cache key keeps the TEXA mode (`RDTEX_TEXA_REPLAY` for these entries), and
`rdtex_Store` with an `RdTexA` bakes a variant (`rdtex_ApplyTexa`, the same
rule on the CPU) for a caller that needs one.

**Cache.** Key (texture id, content generation, TEXA mode). `Texture.c`
uses the table index as the id and `serial * 8 + TexExt.level` as the
generation; the serial is new at every load into a slot, at a CLUT scroll
that changed the CLUT (`tex_textureAnimation`, `tex_SetClutAnimation`;
compared byte for byte before and after) and at a `tex_Tool` CLUT reset.
A new generation of the same size and source format is re-expanded into
the same `RdTex` (`rd_UpdateTexture`); otherwise a new texture is created
and the old one retired, destroyed two `rdtex_FrameTick` calls (frames)
later so no recorded frame loses a texture it draws. `tex_FreeTexture`
drops the entry the same way.

`rd_UpdateTexture` replaces the pixels for every draw of the frame being
recorded. That is also the PS2's behaviour for the CLUT scroll:
`tex_ResetVram` runs the animation just before `dl_Swap`, and the DMA chain
the swap kicks reads the CLUT from memory at that point, so the frame
being closed already shows the new CLUT.

**Mips.** Original: one level per texture, `TexExt.level` (0 unless the
texture tool changes it), decoded at its own size; `tex_UpdateMipMapLevel`
only rewrites TEX1. MIPTBP1/2 are not written on rd. Enhanced hook:
`rdtex_SetEnhancedMips(1)` keeps a CPU box-filtered chain per entry
(`rdtex_BuildMipChain`) for the RHI to upload once it has mipmapped
textures; not in the settings yet.

**Binding.** `tex_TransTexture(id, pri)` keeps its PS2 logic (the per-list
`transDone` and `lastTex` checks, the VRAM bump allocator as bookkeeping,
the same return values). Where the PS2 chained the record's own TEX1/TEST
packet by DMA reference (`tex_transRegister`), the host writes TEX1 and
TEST into `tex_setTexReg`'s packet ahead of TEX0, so `GifPacket.c`'s
decoder turns them into `rd_SamplerFilter` and `rd_TestGs` (ATE GREATER,
AREF 96 or the TIM2 value, AFAIL FB_ONLY or the TIM2 value, Z GEQUAL) in
packet order, only when the PS2 sent them (`lastTex != id`). TEX1's filter
comes from the ICO block's SMPMAG/SMPMIN, or MMAG linear and MMIN
`GlobalStageSetting.texSampleMode` without it; the ICO block has no wrap
mode, so CLAMP stays with the materials and raw writes. The texture
image transfers (BITBLTBUF, TRXPOS/REG/DIR, TEXFLUSH packet) are not
written on rd.

**Resolver** (R2a's open questions 1 and 2). `tex_Init` registers
`texHostResolve` with `gif_HostSetTex0Resolver`. `tex_setTexReg` notes the
TEX0 it wrote per list (TBP, TBW, PSM, TW, TH, CBP, CPSM; 16 notes per
list, most recent first), since the bump allocator hands out TBPs per list.
The resolver looks the TEX0 up in its list's notes, then in the other
lists' (a list that never wrote TEX0 inherits another's), and returns the
cached texture at its current generation. Anything else returns 0, so the
decoder falls through to the named render targets (TBP 0, 0x800, 0x2800,
0x2840, 0x2C00, 0x3000, 0x3F00) and then to R2a's checker placeholder; the
resolver logs each such TBP once (up to 16). A texture placed by the bump
allocator at 0x2800 (its start when no head TBP is locked) wins over the
work buffer in the list that bound it, as the VRAM would until the work
buffer is drawn.

**UV scroll.** `t->uv` (uOfs/vOfs) stays a VU1 packet: the PS2 applies it
in the mesh microprograms only, never to GIF sprites, and the 2D code that
wants it reads it itself (`tex_printTexture`). `rd_UVOffset` offsets screen
primitives at replay, so calling it from `tex_TransTexture` would scroll
2D sprites the PS2 does not scroll. The mesh path (wave 3) takes the
offset from the record into `RdMaterial.uvOffset`.

**Render-target aliases in Texture.c.** `tex_TransTextureDefocus` (no
caller in the game) draws a texture at 1/2^lv into a bump-allocated block
and binds the block; on rd the block is an `rd_TempTarget`, noted as the
texture of that TEX0. The other `tex_AllocVramAuto` callers (`puddle.c`,
`pool.c`, `queen_barrier_disp.c`, `ZFog.c`) and the raw TEX0 writers of
`staticBlur.c`, `darkVolume.c`, `puddle.c`, `pool.c` and
`queen_barrier_disp.c` belong to the 3D waves; their TBPs reach the
placeholder until those files are converted.

**Host fixes in Texture.c** (both host builds): `tex_loadImage` takes the
image address as an `ICO_WORD` (was `(unsigned int)addr`, `Texture.c:382`);
the three `%s` prints of a record pass its name instead of `(int)rec`
(`:1662`, `:1873`, `:1878`); `tex_convertClutCSM2ToCSM1` (TIM2 ClutType bit
7: CLUT stored in index order) rearranges by the CLUT's own entry size and
count, where the PS2 code moves 256 words whatever the CLUT holds (right
only for 256 32-bit entries; past the end of the file image otherwise);
`tex_convertImage` (the `sceGsExecLoadImage`/`StoreImage` round trip,
unreachable: `tex_makeCopyImage` is only called with convert 0) copies the
image unconverted and logs once. `Tim2Picture` and `Tim2Mipmap` are frozen
overlay structs in `ico2/seki/include/Tim2.h` (`config/struct_classes.txt`,
`port/test/layout_asserts.c`).

Test: `rd_tex` (`port/render/test/rd_tex_test.c`).

## 12. Frame lifecycle, camera and post passes (wave 2, R2c)

`ico2/seki/src/GsBase.c` under `ICO_RD` (the window build; the headless
build compiles the original code), `port/render/rd_frame.c` (new),
`rd_post.c`, `rd_present.c`, small hooks in `rd_core.c` and `rd_replay.c`.
Test: `rd_gsbase` (`port/render/test/rd_gsbase_test.c`), which compiles
`GsBase.c`, `GifPacket.c`, `DisplayList.c` and `DmaPacket.c` as the window
build does and drives the real `gsb_InitGSSystem`, `gsb_SyncGSSystem` and
`gsb_UpdateGSSystem`.

**What a flip does on the PS2.** `scheduler()` (`common/src/main.c`) calls,
every `systemStatus[1]` vsyncs (2: 25 Hz PAL), `gsb_SyncGSSystem`
(`gsb_PostEffect` appends the post passes to the frame Main recorded) and
then `gsb_UpdateGSSystem(0)`, which in order:

1. reads `odd_even` from `GS_CSR` bit 13 (FIELD);
2. `gsb_Reduction`: a stack packet on DMA channel 2 reduces SCENE, as the
   lists kicked at the previous flip left it, into DISPLAY, with the tint
   the previous call computed (the packet is built before the tint update);
3. `sceGsSwapDBuff(&db, buffer_ID)` (`sce/libgraph/graph010.c`): the display
   environment, then `sceGsPutDrawEnv` sends `db.draw[buffer_ID]` and its
   clear packet on GIF path 3 (`graph008.c`): FRAME FBP 0x40, ZBUF 0xC0
   PSMZ32 write on (`gsb_SetFrame`), XYOFFSET, SCISSOR, PRMODECONT 1,
   COLCLAMP 1, DTHE 0, TEST 0x50000 (`graph006.c`, ztst 2), then TEST
   0x30000, PRIM 6, RGBAQ, a full-scene sprite at Z 0, TEST 0x50000
   (`graph007.c`); the RGBAQ is the word `gsb_SetBGColor` writes at
   `db+0x100` / `db+0x1F0` (`clear0.rgbaq`, `clear1.rgbaq`), alpha 0x80;
4. `sceGsSetHalfOffset(draw[buffer_ID], ..., odd_even == 0)` rewrites the
   XYOFFSET of the environment just sent (`graph021.c`: OFY + 8, half a
   line), so it takes effect when that buffer is sent again, two flips
   later;
5. `dl_Swap` kicks the 13 lists recorded since the previous flip (from
   list 11 with `fbKeep`); path 3 is ahead of them.

**Clear colour (R2a question 3).** A frame's lists therefore draw over a
clear to the BG colour current at the flip that kicks them, not at the flip
that opened them. `rd_FrameHead` (rd.h) records the draw environment and
the clear when the frame opens (`gsbHostFrameHead`, after `dl_Swap` /
`dl_Clear`), and `rd_FrameFlip` rewrites the clear colour and the half
offset in place at the flip (`gsbHostFlip`, between `sceGsSwapDBuff` and
`sceGsSetHalfOffset`), so the commands keep their position at the head of
the list. The head is recorded twice, at the head of list 0 and of list 11;
`rd_EndFrame` keeps the copy in the first list it replays (11 for a keep
frame, 0 otherwise) and turns the other into `RDC_NOP`s. A keep frame is
therefore cleared as on the PS2 (R2a's hook skipped the clear: list 0 is not
replayed), and a full frame's list 11 runs in the state list 10 left, not
in a re-applied scene environment. The head is recorded after
`rd_BeginFrame`'s list defaults; the GS has it before them, but the head
ends on TEST 0x50000 and Z write on, the normal defaults, so the state at
every later command is the same. Test: `bg` (the colour changed mid-tick),
`keep`, `clear` (pixels).

**Present boundary.** `dl_Swap` calls `rd_EndFrame(fbKeep)`, which replays
and presents; `gsb_UpdateGSSystem(1)` (the movie path) and the
`gsSystemReady == 0` path drop the open frame (`dl_Clear`,
`rd_DiscardFrame`). The PS2 still flips there (reduction, clear); nothing
of it is visible because the next frame's head clears again. The host shows
a frame one flip earlier than the PS2, which reduces it at the next flip
(R2a, section 9).

**`fbKeep`.** `fbKeep` is read at `dl_Swap` (replay 11..12 over the retained
DISPLAY) and by `gsb_Reduction` (tint 128 while set) and `gsb_PostEffect`
(`gsb_KeepFrameBuffer` draws DISPLAY back into SCENE at 112/128 in list 11,
after whatever the game drew there). A keep frame thus darkens the kept
image by 112/128 per frame unless something else redraws it, exactly as the
PS2 would.

**Field parity (open item 4).** The PS2 samples FIELD at every
`gsb_UpdateGSSystem`, which runs every second vsync. FIELD changes every
vsync (the host loop models it that way, `port/platform/host_loop.c`; the
game calls `sceGsResetGraph(0, systemStatus[1] == 1, ...)`, so `inter` is 0
at step 2; that FIELD also toggles every vsync in that mode is the host
model's assumption, not checked against hardware), so in steady state
every flip sees the same FIELD and both draw
environments carry the same half offset: constant parity. A skipped flip
(`gsb_SyncGSSystem` returning 1 leaves `frameStepCount` running, the
update lands one vsync later) or any other odd-length gap flips the parity
from then on; at frame step 1 the parity alternates per buffer. The
Original preset reproduces exactly this: `gsbHostField` reads the host's
`GS_CSR` (not `odd_even`, which stays 0 on the host so game state is
unchanged), `gsbHostHalf[2]` mirrors the bit in each draw environment of
`db`, and the flip sends the value of the buffer it flips to, i.e. the one
decided two flips earlier. The half offset is `RD_TARGET_HALF_Y` in the
head's `rd_SetTarget` (XYOFFSET.y + 0.5, origin y + 0.5 at replay); any
later `gif_SetDrawEnviroment(0x800, ...)` (fade, letterbox, anti-alias, the
2D layer) writes XYOFFSET without it, as on the GS. On the host no flip is
ever skipped, so the parity is fixed for a session segment by which vsync
the scheduler's frame step lands on after boot or a stage load. Test:
`parity` (constant field: no offset on the first two flips, then on every
one; field alternating per flip: one parity per buffer), `half` (pixels:
rows averaged).

**DTHE (open item 5).** Every flip writes DTHE 0 (the frame PSM is
PSMCT32, `graph006.c` clears DTHE when `psm & 2` is 0), and no game code
writes DTHE: no dithering.

**Camera (`rd_SetCamera`).** `gsb_MakeCommonMatrix` is where the view
(`matrixptr+0x80`, written by `camera-root.c` after `gsb_SetVSMatrix`) and
the screen matrix (`+0xC0`) are both final, so it fills `RdCamera`: `view`
= `+0x80`, `proj43` = `+0xC0` (view to GS window X/Y and GS Z after the
divide by w; 4:3), `zoom` = `vsParam[0]`, `aspect43` = 4/3, `nearZ`/`farZ`
= `vsParam[7]`/`[8]` (2 and 262144; the screen matrix maps them to GS Z
536870880 and 1, about 2^29: the scene's Z is 32-bit). `gsb_SetVSMatrix`
itself does not call `rd_SetCamera`: `puddle.c` and `pool.c` call it
mid-frame for their render-to-texture views and restore the matrices by
copying, so the frame camera would end up theirs. `cut` is 0 (no cut
detection yet). Replay fills FrameCB from the frame's camera (the previous
one when a frame has none): `g_view`, `g_proj` = `proj43`, `g_viewProj` =
`proj43` x `view` (the product the game keeps at `+0x100`), `g_cameraPos`
= the eye from the inverse view, `g_clip` = near, far, zoom, aspect. The
gameplay matrices are untouched. Test: `vu` (recording) and `camera`
(`rd__CameraProbe`: `camera_probe_ps` returns the three products as float
bits; within 1e-5 relative of the C products, and GS X/Y within 1/16 pixel
of `sceVu0RotTransPers` through `+0x100`).

**Widescreen hook.** `gsbHostWideX()` (1 in Original) and
`gsbHostWidenCull` divide `projHalf[0]` (`+0x240`, the projection of the
visible screen that `+0x280` and `RegistPacket.c`'s per-object `+0x300`
are built from and `gsb_ClipBox` culls against) by the output's widening;
called after `gsb_SetVSMatrixSub` under `ICO_RD` and a no-op at 1. The
renderer projection widens in `rd__FillCameraCB` (`rd_frame.c`, a comment
marks the place); `+0x80`/`+0xC0` stay 4:3.

**VU parameter block** (`rd_SetVuCommon`, `RdVuCommon` in rd.h). The
packet `gsb_MakeCommonMatrix` builds (when `game_pause` is non-zero) is a
DMA `cnt` of 17 qwords: FLUSHA, UNPACK V4-32 of 16 qwords to VU1 data
memory 0, then:

| VU1 qw | content | source |
|---|---|---|
| 0 | 0, 0, 0, 1 | `commonMatrixHead.row[0]` |
| 1 | 4095, 4095, 0, 16777215 | `row[1]` (clip extents) |
| 2 | 0, 0, 0, 0 | `row[2]` |
| 3 | GIF tag 0x8000, 0x302EC000, 0x512, 0: EOP, PRE, PRIM 0x5D (fan, IIP, TME, ABE), PACKED, NREG 3 (ST, RGBAQ, XYZ2) | `commonMatrixHead.tag` |
| 4..7 | world to GS screen: screen (`+0xC0`) x view (`+0x80`) | `+0x100` |
| 8..11 | viewport | `+0x340` |
| 12..15 | inverse view | `+0x380` |

Every one of the 13 lists gets a DMA reference to it at its current
position each time it is built (at the frame head through
`gsb_SetGsDefault`, and again when `camera-root.c` sets the camera), so a
draw sees the block current at its position in its list. rd keeps the last
block of the open frame (`RdFrame.vu`, not dumped); wave 3 needs the
per-list position (a state command in the lists) if a list draws before and
after a camera change in one tick. No lights: `light_ResetLight` is empty
and lights are per object (`light_MakeLightMatrix`). `gsb_SetGsDefault`'s
list-0 head is VIF BASE 0x100 / OFFSET 0x180 (VU1 double buffering), which
fixes where the mesh programs' input buffers start: wave 3.

**Post passes.** `gsb_KeepFrameBuffer`, `gsb_fade`, `gsb_scissorOnDemo`,
`gsb_controlBrightness`, `gsb_antiAlias` and `gsb_filmNoise` call `rd_Post`
under `ICO_RD` (`dl_SetDLPriority` as their packet start did; game logic,
fade and letterbox state machines unchanged). Each kind records the
original's register writes and sprites in order as rd state (rd_post.c
lists them), so the state they leak is the state the GS kept:

| kind | original | CPU reference (rd_gsbase) | tolerance, measured on llvmpipe |
|---|---|---|---|
| `RD_POST_KEEP` | DISPLAY as PSMCT24 at 112/128, TEXA 80/80, PRIM 0x116, the filter in force | bilinear at the GS sample point of the stretched sprite, then the modulate | 1 LSB, 1 |
| `RD_POST_FADE` | scene environment, TEST 0x30000, Z write off, PABE 0, ALPHA 0x44, PRIM 0x446 | GS LERP with As | 1, 1 |
| `RD_POST_LETTERBOX` | two 58-line bars, ALPHA 0x64 with FIX = the level, Z write on | GS LERP with FIX (level 25 after 10 ticks) | 1, 1 |
| `RD_POST_BRIGHTNESS` | white, alpha = the step, mode 7, PRIM 0x446 | GS LERP with As | 1, 1 |
| `RD_POST_AA_DOWNSAMPLE` | SCENE to AA0 (256²) and with `lines` 2 AA0 to AA1 (128²), PABE 1, ALPHA 0x64 FIX 0x80, no ABE | exact copy of every second texel (nearest leaking from the scene draw) | 0, 0 |
| `RD_POST_AA_COMPOSITE` | AA1 at `rgba[1]`, then AA0 at `rgba[0]`, LERP FIX into SCENE 512² (the original's literal size), then Z write on, TEST 0x50000, the scene environment | GS LERP of the nearest texel | 1 per level alone (1, 1); 2 with both (measured 1) |
| `RD_POST_FILM_NOISE` | CLAMP 0 (REPEAT), Z write off, TEST 0x30000, PABE 0, ALPHA 0x44, PRIM 0x56 (STQ), RGBAQ 128 grey with the grain alpha, ST 0..grain scale over window 0x7000..0x9000 | bilinear REPEAT on a periodic texture, modulate, GS LERP with As | 1, 1 |

`gsb_filmNoise` keeps `tex_TransTexture(n, 0xA)` and binds `sandstorm_spr`
through the decoder with one PRIM write (the TEX0 that call wrote goes
through R2b's resolver), as `DisplayFont.c` does; the pass then draws with
the bound texture. The film noise lands in whatever list is current
(`dl_GetPri()`), as on the PS2.

Two findings from the register values: `gsb_controlBrightness` passes
`gif_MakeSpriteNoTexture` corners that are already absolute, and the
helper adds the 0x8000 window origin again, so the GS receives (3840.0,
3840.0) and, from the carries of the 17-bit far corner, (256.0, 256.0625)
with Z 0xFFFFFFFF; the rectangle between them covers the whole scene, and
rd records exactly those vertices. `gsb_antiAlias` restores the scene
environment as 512 x 512 even at 448 lines (NTSC), which offsets XYOFFSET
by 32 lines there; recorded as written.

What these passes do not update is the decoder's per-list register shadow
(PRIM, TEX0, FRAME, XYOFFSET, SCISSOR in `GifPacket.c`): a later packet in
the same list that relies on a PRIM or TEX0 it did not write would see the
decoder's older value where the GS had the pass's. Every game path writes
both before drawing; it is a gap only for code that relied on the leak.

**Depth scale (R2a question 4).** Every ZBUF the game writes has PSM nibble
0 (PSMZ32: `gsb_SetFrame`, `gif_SetZWrite`, the post passes), and the scene
Z reaches 2^29, so FrameCB `g_z.x` is now the scale of the bound depth
target: `RdTargetRec.zFormat` (`rd_SetTargetZFormat`, default PSMZ32 =
2^-32; PSMZ24 2^-24 and PSMZ16 2^-16 for completeness), used by
`doScreen` through `rd__FrameGroupZ` and by clears through `rd__GsDepth`.
`gs_z_to_depth` computes `(zmax - z + 1) * scale` instead of
`1 - z * scale`: the same value, exact for every Z at 2^-24, and for the
large Z values at 2^-32, so the UI's 0xFFFFFF9B and 0xFFFFFFFF compare as on
the GS. Z of the far end (small Z) rounds like any float near 1: about 256
Z units per depth step around 2^29 (open question 3 below). Test:
`zscale` (CPU) and `depth` (a GEQUAL sprite at 0xFFFFFF9B over 0xFFFFFFFF
fails, the reverse passes, on SCENE).

**Presets and interpolation (hooks).** `RdPresentPreset` (rd_present.c)
carries the Enhanced fields `interpolate`, `aspectFromSettings`, `mirror`,
`fullHeight`, all at their Original values in both entries and read by
nothing yet, and `presentAlpha()` (1: each frame presented once, whole) is
where wave 7 blends the retained frames.

**rd.h additions (R2c).** `RdFrameHead`, `rd_FrameHead`, `rd_FrameFlip`;
`RD_TARGET_OFFSET`, `RD_TARGET_HALF_Y` (bit 1 of `rd_SetTarget`'s
`useOffset`); `RdZFormat`, `rd_SetTargetZFormat`, `rd_TargetZScale`;
`RdVuCommon`, `rd_SetVuCommon`, `rd_GetVuCommon`; the `rd_SetCamera`
comment now says what the fields hold. Internal: `RdFrame` head and VU
fields, `RdTargetRec.zFormat`, `rd__FrameGroupZ`, `rd__GsDepth`,
`rd__TargetZScale`, `rd__FillCameraCB`, `rd__SetReplayCamera`,
`rd__CameraProbe`, `RD_FS_CAMERA_PROBE` (`camera_probe_ps`, tests only).
The dump format is unchanged (the head lives in ordinary commands; the VU
block and Z format are not dumped).

Open questions for wave 3:

1. The VU block is per list position on the PS2; rd keeps one per frame.
   A list that draws meshes before and after `camera-root.c`'s
   `gsb_MakeCommonMatrix` in one tick needs a recorded command.
2. `g_proj` is the GS screen matrix, not a clip matrix: the mesh vertex
   shaders divide by w, apply the GS window to NDC conversion of the bound
   target (`gs_xy_to_ndc`, which also gives puddle/pool targets their own
   origin) and `gs_depth`; the VU1 `ftoi4` snapping of X/Y to 1/16 pixel is
   theirs to reproduce or not.
3. Depth precision: D32F with `(zmax - z + 1) * 2^-32` resolves about 256
   GS Z units near the scene's 2^29 range; coplanar decals that rely on
   GEQUAL ties at closer Z need a check (or a mapping with scale 2^-29 for
   SCENE).
4. `fog_lut.hlsl` derives Z as `(1 - d) * 2^24`; with SCENE at 2^-32 the
   fog package must use the target's scale (open item 3, the Z byte).
5. `cut` is never set: the interpolation package needs a camera-cut signal
   (camera-root.c's mode changes) before it trusts `RdCamera`.
6. Brightness: whether the GS rasterises a sprite whose second vertex lies
   above and left of the first (the corners `gsb_controlBrightness` ends up
   sending) is not settled by any source the port uses; rd draws it, which
   is the visible-overlay reading. If the GS drew nothing, the brightness
   step never showed on the PS2 and the Original preset should skip it.
