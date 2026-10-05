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
| 3 | 0x29 | Cd·FIX + Cs | disc data only (0x61 until wave 5, a slip: that register is (Cd−Cs)·FIX + Cd; section 18) |
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
| COLCLAMP 0 (shadow count) | Stencil INCR_WRAP/DECR_WRAP on the scene depth-stencil under write mask 0x3F (the count mod 64); `rd_ShadowResolve` writes the colour the wrapped sum would have left, 4 n mod 256, into a scene-sized count target, then the original 256/128/64 blur chain runs into SHADOW0..2 (wave 4, R4b: section 14; the wave-0 plan said "stencil ≠ 0", but the chain reads the count's value). |
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
snapshot. With the normalisation the screen and post programs reach 68
pipelines from the game's state set (`rd__EnumerateReachableScreen`,
asserted under 100 by `rd_state` and `rd_pixel`; the text said 67 until wave
3, the count was 68 already); `RD_PIPELINE_CACHE_MAX` is 256 and `rd_core`
asserts above it. Wave 3 (R3ab) adds the VU program families (section 13):
`rd__EnumerateReachable` is 156 keys, asserted under the cache maximum.

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

**Backend selection and D3D12 (wave 6, R6c).** A Windows build links both
backends; `rhi_CreateBackend("vulkan" | "d3d12")` (`rhi.h`, before
`rhi_Init`) picks the one the `rhi_*` calls go to (`port/rhi/rhi_backend.c`;
default: `ICO_RHI_BACKEND` in the environment, else Vulkan). The window
build takes it from `[video] backend` (`config.toml`) or `backend=`
(`ico-pc.ini`), `rd_replay_tool` from `--backend`. The D3D12 backend is
`port/rhi/d3d12` (design: its `README.md`). R1b's D3D12 questions, answered
there in full:

- *CopyTextureRegion between R8G8B8A8_UNORM and _UINT* (the exact-blend
  ping-pong): answered. Every RGBA8 texture is created `R8G8B8A8_TYPELESS`
  and viewed typed, so the copy is between identical resource formats, a bit
  copy.
- *A pipeline writing SV_Target1 with blending disabled and one RT*:
  answered. The second output has no target and is discarded; the debug
  layer is expected to call it a warning, not an error (to be confirmed by
  the first Windows run, `rhi_d3d12_test.log`).
- *Integer clears*: answered. The clear value is rounded to an integer the
  way the Vulkan backend rounds it before `ClearRenderTargetView`, so the
  float-to-integer conversion is exact; `rhi_test_common.c` checks 1, 2, 3, 4
  on an RGBA8_UINT target on both backends.
- *Replicate rd_pixel's coverage cells*: answered by not replicating them.
  The RHI test is backend-neutral (`port/rhi/test/rhi_test_common.c`, the
  same eight cells and expected values on both), and every `rd_*` GPU test,
  `rd_pixel` included, runs on D3D12 with `ICO_RHI_BACKEND=d3d12`
  (`docs/port/TESTING.md`). Not yet run on Windows.

## 5. Targets and buffers

| RdTargetId | GS location | GS size | notes |
|---|---|---|---|
| SCENE | FBP 0x40 (TBP 0x800) | 512×512 PAL, 512×448 NTSC | RGBA8 + D32F_S8 |
| DISPLAY | FBP 0 | 512×256 / 512×224 | the only displayed buffer; retained (motion blur history, keep) |
| SHADOW0..2 | FBP 0x1C2, 0x1E2, 0x1EA (TBP 0x3840, 0x3C40, 0x3D40): blur levels 1..3 | 256², 128², 64² | wave 4 (R4b); the count at FBP 0x142 is scene-sized and lives in a per-frame target (`rd_ShadowCountTarget`, section 14) |
| WORK0..3 | TBP 0x2800..0x3000 | 256×128, 256×256 | depth of field, flare, aura (wave 5: WORK0 0x2800 256×128, WORK1 0x2A00 256×256, WORK2 0x2E00 scene-sized, WORK3 0x3000 256×128; section 17) |
| AURA_WORK, AURA_TAP, WORK2_PAD | TBP 0x2A00 TBW 8, 0x2800 TBW 2, 0x2E00 + W·H/64 | scene, 128², 256×64 | wave 5 (R5a), section 17 |
| AA0, AA1 | TBP 0x2800/0x2C00 | 256², 128² | anti-alias chain (aliases WORK in VRAM; separate here) |
| FEED128 | TBP 0x3F00 | 128² | aura feedback, persistent |
| DATE_SNAPSHOT | n/a | scene size | R8 |

`rd_TempTarget` replaces `tex_AllocVramAuto` for puddle, pool and queen
barrier render-to-texture.

Buffer sizes in the Original preset are literal. In Enhanced the scene
target is output-sized; blur and work targets scale by `outH/448` capped
at 2× so blur radii stay a constant fraction of the screen. Wave 7 (R7a):
the GS sizes stay literal and the textures scale (section 19).

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
simulation ticks, mirror flip at present. Section 19 (wave 7, R7a) has
the first four.

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

1. MSCALF code 18 ("mode 3" in `mc_SetMicroCode`): resolved in wave 3
   (R3c, `VU1_PROGRAMS.md` section 2): it is particle's BEGIN_PARTICLE, a
   redundant entry before the particle batch's MSCNT; in every other
   program code 18 is the light upload. rd maps (particle, 18) to
   `RD_PROG_PARTICLE` (section 13).
2. `cluster.vsm:460` "front/back slot selection": resolved in wave 3 (R3c,
   `VU1_PROGRAMS.md` section 5): XYZ2 kick selection by the region test and
   the ADC counter, no facing test; `RD_PROG_SKIN` pipelines keep
   `cullNone` (section 13).
3. ZFog Z byte: which byte of the PSMZ32 depth the PSMT8H reinterpretation
   plus the PSMT4 block shuffles at `ZFog.c:236` selects. Derive with a
   small host test over the GS swizzle tables before writing the fog shader.
   Resolved in wave 4 (R4c, section 15): bits 16..23 of the 32-bit Z.
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
`GifPacket.c`; permanent since renderer wave 6, below). Every register
write of the 2D layer reaches one decoder in packet order: the `gif_*` helpers (their bodies are unchanged; `setGsReg`
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

**The decoder is the permanent register-level route** (renderer wave 6,
package R6a). The plan made `rd_gs_shim.c` temporary: deleted once the
raw-register files were hand-converted, with zero `gif_SetGsReg` uses as
the gate. That gate is dropped. The decoder decodes the game's own register
writes at the level of its functions (the `gif_*` helpers, `gif_SetGsReg`,
the A+D pairs written into the open packet, and the GIF packets
`mc_HostDma` and `gif_HostWriteRegs` hand it from the VU1 chains); it is not
a GS emulator (no GS memory, no image transfers, no context 2). Converting
its users to direct `rd_*` calls would duplicate it file by file:
`darkVolume.c`'s spheres and `lineManager.c` (section 18, open item 4) keep
`gif_SetGsReg`, and the debug font joined it in R6a (`debug.c`'s
`debugHostFontGlyph` runs the font's VU1 routine on the glyph packets and
writes the points it would kick; docs/port/DEVELOPER_MODE.md). A file that
needs what the decoder cannot know supplies it with an explicit host call
(darkVolume.c's three, section 18). What still logs: a register the decoder
does not decode, once per register by name, then counted
(`gif_HostUndecodedCount`, `gif_HostUndecodedTotal`); the FBP of an unknown
block, ALPHA outside the twelve modes, TEXA, TFX, PRIM.CTXT, FGE, PRMODE,
CLAMP, PRIM 7 and DTHE oddities once each (`gsOnce`); and `mc_HostDma`'s
VIF/GIF forms it does not read (`mc:` lines, section 18).

R6a game run (window build `build-host/r6a-linux-x64-win` on lavapipe,
`SDL_VIDEODRIVER=offscreen`, `pad-boot.txt`, `ticks=4000`,
`dump_every=500`, `[gameplay] developer_mode = true`, a private pref folder,
`timeout 600`): exit 0 after 435 s at 4000 Main ticks, 8007 vsyncs,
stage 3; every process gone. No `gif:` and no `mc:` line in the log or on
stderr: the undecoded-register counters stayed at zero (the first undecoded
write of a register logs its name). The trace lines equal the headless
4000-tick runs' (the build before R6a, and R6a with developer mode off and
on; docs/port/DEVELOPER_MODE.md): developer mode changes nothing until the
menu is opened, and the boot script never presses SELECT, which opens it.
The menu was therefore not drawn in this run; `rd_debug_test` covers its
drawing. All 7 dumps replay through `rd_replay_tool` with no command
skipped; frame 500 is the title fading in (the castle and the copyright
line), frame 2500 the boy in stage 3's courtyard, frame 1000 black
(stage 1, between the title and the switch to stage 41 at tick 623).

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
textures; not in the settings yet. Wave 7 (R7a): the upload builds the
chain from the rd texture instead (section 19).

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

**Widescreen hook.** (Implemented in wave 7, R7a: section 19.)
`gsbHostWideX()` (1 in Original) and
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
where wave 7 blends the retained frames. Since R7a the Enhanced entry has
`aspectFromSettings` and `fullHeight` (section 19). Since R7b `interpolate`
is set in the Enhanced entry and `presentAlpha()` is gone: the blend
happens before the replay, in `rd_interp.c` (section 20).

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
   Resolved in wave 4 (R4c, section 15): `fog_lut_ps` inverts
   `gs_z_to_depth` with the depth target's scale.
5. `cut` is never set: the interpolation package needs a camera-cut signal
   (camera-root.c's mode changes) before it trusts `RdCamera`. Answered in
   wave 7 (R7b, section 20): `rd_CameraCut` from the game's cut sites.
6. Brightness: whether the GS rasterises a sprite whose second vertex lies
   above and left of the first (the corners `gsb_controlBrightness` ends up
   sending) is not settled by any source the port uses; rd draws it, which
   is the visible-overlay reading. If the GS drew nothing, the brightness
   step never showed on the PS2 and the Original preset should skip it.

Wave 3 (R3ab) answers questions 1 and 2 in section 13: the VU block is kept
per list at record time with the rest of the VU state, and the mesh shaders
take what the VU took (`VuCB`), the GS window conversion of the bound target
and the `ftoi4` snapping of the programs (`VU1_PROGRAMS.md` section 8).

## 13. The mesh path (wave 3, R3ab)

`port/render/rd_mesh.c` (recording, the mesh registry, the per-list VU
state, the VU pipeline families), `rd_replay.c` (`doVu`), `rd_mesh.h` (the
interface R3c fixed, with the additions below); on the game side the host
paths (`ICO_RD`) of `Packet.c`, `RegistPacket.c`, `MicroCode.c`,
`Primitive.c` and `DisplayP2O.c`. Test: `rd_mesh`
(`port/render/test/rd_mesh_test.c`). The shaders and their CPU references
are R3c's (`docs/port/VU1_PROGRAMS.md`, `SHADERS.md` "VU1 programs");
nothing in them changed.

**Principle.** The PS2 builds, per list, DMA chains for VU1: the common
block, the program upload, per object its matrix and light packets, per
packet the texture packet (TEX0 on path 2/3 plus the VU UV offset), the
material register packet, the MSCALF code and the vertex batches. The host
reads the small packets as the VIF would, when they are chained, and keeps
the effect: `MicroCode.c`'s `mc_HostDma(id, addr, qwc)` walks the chain
(id 5: cnt tags whose upper doubleword carries two VIF words, up to ret;
id 2: qwc quadwords of VIF codes) and interprets UNPACK V4-32 to TOP, MSCAL
and MSCALF, and MSCNT:

| VIF | host |
|---|---|
| UNPACK V4-32, FLG | the quadwords into a TOP staging buffer |
| MSCAL/MSCALF 0 (SET_GSREGISTER) | the GIF tag at TOP and its NLOOP A+D pairs to the GS register decoder, `gif_HostWriteRegs` (GifPacket.c, section 9): material ALPHA/CLAMP/FBA, the specular pass's PABE/ALPHA, the reflection pass's CLAMP/PABE/ALPHA, the dissolve's PABE/ALPHA/ZBUF and its reset, the point and line objects' PRIM/RGBAQ/XYZ2 (`reg_dispPoint`, `reg_dispLine`), `prim_DispFan2D` |
| MSCAL/MSCALF other | `rd_VuCall(code, TOP)`: SET_UVOFFSET (2), SET_*_MATRIX (16), SET_*_LIGHT (18), the BEGIN codes, through the resident program of the list (`rd_VuProgram`, set by `mc_TransMicroCode` where it uploads) |
| MSCNT | a particle batch (`rd_DrawVuParticles`); other programs' batches never arrive here |

`RegistPacket.c` routes every `dl_OpenDma` of the file through
`mc_HostDma` (a file-local wrapper), `Texture.c`'s `tex_TransTexture` its UV
offset packet, `Primitive.c` its matrix, light, UV, fan and particle
chains, `mc_SetMicroCode` its MSCALF. The vertex batches of a model packet
(`pk->data`) are not interpreted but drawn: `regHostMesh` sends the
batches' GIF tag PRIM (strip, IIP, TME, ABE) to the decoder, then records
`rd_DrawVuMesh` of the packet's mesh with `rd_VuDrawFromState`. A
`Mesh3D` buffer is drawn whole (`rd_DrawVuGrid`, after its strips' PRIM).
Culling (`reg_clipPacketBoundingBox` against `+0x300`, `gsb_ClipBox`), the
list choices, the packets and the allocations are unchanged; the headless
build keeps the original code byte for byte except the two host fixes
below.

**GS state.** A mesh batch draws with the replay state block at its
command, as screen prims do: the material, texture, dissolve, specular and
reflection register packets were decoded into ordinary rd state commands in
order with the meshes, so they leak between draws and lists as on the GS.
`RdVuDraw.materials` is optional (the seki sites pass NULL); a given array
is recorded for a later Enhanced path and not applied.

**Meshes.** One `RdMesh` per `PacHeader` (rather than per `PObjPart`: the
reg_disp* loops draw packet by packet, and the morph copy has its own
data). `pac_makePacket` builds it from the packet's UNPACK payloads
(`pac_hostBuild`) and keeps the id in `PacHeader.pad9C` (padding on the
EE). The mesh keeps on the CPU the vertex stream without the GIF tags and
the index list `ICO_VU_INDEX(kick, corner)` for every vertex k >= 2 of a
batch whose ST.w and the previous vertex's are 1 (the strip-flag rule of
`vu1ref_StaticKicks`); replay copies both into the frame's upload ring once
per replayed frame (no device buffers: no allocation count to watch, and a
morph update is a CPU write). `reg_setShape` rewrites the packets' vertex
quadwords every tick; `pac_HostRefresh` re-reads them (`rd_UpdateVuMesh`,
new), which the frame being recorded sees for every draw, as the PS2's DMA
reads the packet at the kick. The registry holds 16384 meshes; when it is
full, meshes no frame drew for a while are evicted (`rd_VuMeshValid`, new),
and `pac_HostMesh` builds a mesh again from its packet on the next draw.

**VU state per list (section 12, question 1).** rd keeps one VU image per
list at record time (a `Vu1Ref`, `vu1_ref.h`: data memory and the VF
registers, plus the resident program and the last BEGIN code). The common
block (`rd_SetVuCommon`) loads all 13, as `gsb_MakeCommonMatrix` references
it from every list's current position; SET_* uploads and SET_UVOFFSET load
the current list's. So the UV offset in VU memory 2 carries over from draw
to draw until the next SET_UVOFFSET or common block, exactly as on the VU,
and a list that draws before and after a camera change in one tick draws
each mesh with the block current at its position. VU memory does persist
across lists on the PS2; every list's chain re-establishes what its draws
read (the common block at its head, each object's matrices in every list it
draws in), so per-list images give the same values. `rd_VuDrawFromState`
builds the draw's VuCB from the image: normal_c/normal_l memory 0..35;
cluster memory 0..15, VF13..VF20 (lights) at 28..35, memory 16..255 as
`VuBoneCB`; mesh 0..15 with VF01..VF04 at 16 and VF05..VF12 at 28; particle
0..15 with VF01..VF08 at 16 (`vu_common.hlsli`'s map).

**Program table** (rd_mesh.h): (normal_c, 32/34/36) `RD_PROG_PRELIT`
REGION/NONE/SCISSOR; (normal_l, 32/34/36/38) `LIT`, `LIT_SPEC`, `LIT`
SCISSOR, `REFLECT`; (cluster, 20/22/24) `SKIN`, `SKIN_SPEC` (24 with
`vu_skin_debug_vs`); (mesh, 20/22/24) `GRID`, `GRID_LIT` (24 with
`vu_grid_spec_vs`); (particle, 18) `PARTICLE`. A pair without a row is
logged once and the batch is not drawn (normal_c 38, normal_l 34 on a
light-0 material: `VU1_PROGRAMS.md` section 2).

**Replay** (`doVu`). Group 1 is `layoutVu` (t0 the stream, b1 DrawCB, b2
VuCB, b3 VuBoneCB; a zero bone block for the programs that do not read it).
The pipeline key's program is the RdProg, its vertex shader the VU entry,
its fragment shader `vu_ps`; the blend path, alpha test split, Z and colour
mask come from the state block through `rd__PlanScreenDraw`, as for screen
prims. Static and skinned meshes: one indexed draw over the batches'
index range (REGION, NONE); under SCISSOR two draws a batch, the triangles
SCISSOR_COMMON clips (`ICO_VU_CUT_ONLY`) with PRIM.ABE forced on (the fans'
PRIM is the common block's 0x5D) and then the strip's kicks
(`ICO_VU_KICK_ONLY`), which keeps the fans-first order and makes the fans'
blend exact (`VU1_PROGRAMS.md` findings 1, 2). Grids: the Mesh3D buffer as
it is (`vu_batch` = 3, 1), indices k = 2 .. stripLen - 1 per strip. Particles:
`rhi_CmdDraw(6 * count)`.

**Pipelines.** `rd__EnumerateReachableVu` adds, for each of the 11 VU
vertex shaders, TEST 0x50000, the split pair of 0x5140D (and Texture.c's
per-texture TEST, the same keys) and 0x5C000, Z write on and off, ALPHA off
and the material/dissolve/specular/particle equations (four hardware
paths), on SCENE's D32F_S8: 88 keys, 156 with the screen and post set
(asserted under `RD_PIPELINE_CACHE_MAX` by `rd_state`, `rd_pixel` and
`rd_mesh`; `rd_mesh` checks every pipeline it creates is enumerated).

**Render-to-texture (reflections).** `reg_RenderReflection` draws in list
4 into whatever FRAME `puddle.c`/`pool.c` set (a `tex_AllocVramAuto`
block). The decoder (GifPacket.c, a minimal R3ab change) now maps a
FRAME.FBP that names no fixed buffer to an `rd_TempTarget` of the size
XYOFFSET gives, with its own depth, for the rest of the frame, and a TEX0
whose TBP is that block's (FBP x 32) samples it; before, such draws went
into SCENE. The surfaces that read the reflection are wave 5's; the
reflection pass itself draws into the temporary target.

**Quirks.** The particle end-tag quirk (`VU1_PROGRAMS.md` finding 4) is not
reproduced: the cluster region test reads the common block's mem[0..1]
from the list image as the game intends. `rd_DrawVuParticles` runs
`vu1ref_Particle` on a copy of the list image; when a batch would have
clobbered the common block, a later skinned draw in that list logs once
("particle end-tag quirk ... not reproduced"). The scissor fans' ABE is
reproduced (above). SET_CLUSTER_MATRIX's extra quadword (finding 5) is
reproduced: the copy reads the TOP staging buffer's next quadword.

**Host fixes in these files** (both host builds): `MicroCode.c` no longer
passes the `int` microprogram address table as a DMA pointer
(`dl_OpenDma(5, 0, 0)` on the host, the table being zero there),
`DisplayP2O.c`'s `p2o_TransMicroProgram` likewise; `RegistPacket.c`'s
`reg_transMicroCode` calls `mc_TransMicroCode` with the mask the PS2 left
in the second argument register (the K&R one-argument call read garbage on
the host's stack ABI), with `MicroCode.h`'s prototypes.

**Frame dumps.** Version 3 adds the VU meshes a frame draws (stream, index
list, batches), so `rd_replay_tool` replays meshes. ico-pc.ini
`dump_every=N` (and `dump_dir=`, default `dumps` beside the ini) writes every
Nth frame (`rd-NNNNN.rddump`, the renderer's frame number, one per Main
tick); `host_config.c` hands the keys to `rd_Init` in the environment
(`ICO_RD_DUMP_EVERY`, `ICO_RD_DUMP_DIR`), since port/render does not link
the platform layer.

**rd.h and rd_mesh.h changes (R3ab).** rd.h: none in the declarations; the
wave-0 semantic calls `rd_DrawMesh`, `rd_DrawSkinned`, `rd_DrawGrid`,
`rd_DrawParticles` are kept declared for an Enhanced path but record nothing
(logged once); `rd_CreateMesh` keeps a geometry-less record. rd_mesh.h:
`rd_UpdateVuMesh`, `rd_DestroyVuMesh`, `rd_VuMeshValid`, `rd_VuProgram`,
`rd_VuCurrentProgram`, `rd_VuCall`, `rd_VuDrawFromState`; materials
optional. GifHost.h: `gif_HostWriteRegs`. MicroCode.h: `mc_HostDma`.
Packet.h: `pac_HostMesh`, `pac_HostRefresh`. Internal: `RdMeshRec`
(stream, indices, batches), `RdVuBatchRec`, `RD_VS_VU_*`, `RD_FS_VU`,
`layoutVu`, `rd__VuRow`, `rd__EnumerateReachableScreen`/`Vu`; dump version 3.

**Measured** (`rd_mesh` on lavapipe): prelit code 32 and 36, cluster 20,
grid 20 and a particle batch each identical to the CPU reference's
triangles (sprites) drawn through `sprite_world_vs`/`sprite_ps` in the same
GS state: maximum difference 0 on every pixel (the tolerance is 1).

**Game run** (window build on lavapipe, `SDL_VIDEODRIVER=offscreen`,
`pad-boot.txt`, 1300 ticks in 77 s, `dump_every=100`; dumps replayed with
`rd_replay_tool`, no command skipped). Frame 600 (title): the castle,
cliffs, bridges and sea fully textured, the title menu over it; 94 static
mesh draws and 10 skinned in list 0, 6 skinned and 5 grids in list 1, 37
static in list 2, 52 in list 5 (dissolve), 5 particle batches in list 6.
Frame 700 is the black of a stage change; frame 800 the white fade of
stage 41's opening. Frame 1200 (stage 3, fading in under the letterbox):
the boy lying on the textured stone floor, skinned and lit, hair, bandage
and tunic textured; 35 static and 10 skinned draws in list 0, 6 skinned
and 5 grids in list 1, 3 specular/reflection passes in list 4. Missing:
shadows (wave 4), fog, and the effects outside seki listed below. No
`rd`/`mc` warning was logged (no unknown program/code pair, no undecoded
register from the VU packets).

Open items for waves 4 and 5:

1. `Shadow.c` sends its register packets through SET_GSREGISTER
   (`Shadow.c:129`) and its volumes as VU batches; they are not yet read by
   `mc_HostDma` (wave 4 routes them). Resolved in wave 4 (R4b, section 14):
   only shadow_Reset's packet goes through SET_GSREGISTER; the volumes and
   shadow_Draw are DIRECT (PATH2) GIF packets. Shadow.c's host path records
   rd calls itself and `mc_HostDma` is unchanged.
2. `particleEffect.c`, `enemy.c`'s own particle packets and other VU users
   outside seki (`lightning.c`, `darkVolume.c`) chain their packets without
   the host reader; only `prim_DispParticle` batches draw today. Resolved in
   wave 5 (R5c, section 18): they chain through `mc_HostDma` (DIRECT
   added); enemy.c's own code is `gif_SetAlpha` and `prim_DispParticle`,
   already routed.
3. The reflection and other render-to-texture blocks are temporary targets
   per frame and per FBP; the surfaces that read them (`puddle.c`,
   `pool.c`, `queen_barrier_disp.c`, `staticBlur.c` work buffers) are wave
   5's and may want named or kept targets.
4. Mesh vertex data is re-uploaded every replayed frame; a device arena with
   dirty tracking is an optimisation for later.
5. Near-plane scissor cases colour-interpolate over the original triangle
   (`VU1_PROGRAMS.md` section 8); not seen as an issue in the run.

## 14. Shadows (wave 4, R4b)

`ico2/seki/src/Shadow.c` under `ICO_RD` (the window build; the headless
build compiles the original code, and the host path adds no heap use),
`port/render/rd_shadow.c` (recording), `rd_replay.c` (`doShadowReset`,
`doShadowStrip`, `doShadowResolve`), `rd_pipeline.c` (keys, stencil state).
Test: `rd_shadow` (`port/render/test/rd_shadow_test.c`), which compiles
Shadow.c with the 2D layer and Matrix.c as the window build does.

**What the PS2 does.** `shadow_Reset` (frame head, list 3) clears FBP 0x142,
a scene-sized PSMCT32 buffer, and leaves ZBUF 0xC0 with ZMSK, TEST 0x50000,
ALPHA 0x68 with FIX 0x80 and COLCLAMP 0. Each `shadow_RenderVolume`
(`RegistPacket.c`'s display paths, in Main) sends one DIRECT packet per
object: per silhouette triple a strip of ten positions over the six
projected prism vertices, PRIM 0x144 (strip, flat, ABE), each position's
RGBAQ 0x04 or 0xFC by its facing (`emitVolumeStrip`). With flat shading a
triangle adds its last vertex's colour; FIX 0x80 is a factor of 1 and
COLCLAMP 0 keeps the low 8 bits, so every channel of a pixel ends at
4 n mod 256, n = (0x04 faces) - (0xFC faces) that pass Z GEQUAL against the
scene. `shadow_Draw` (`gsb_PostEffect`, end of list 3) reads FBP 0x142 as
PSMCT24 with TEXA 0x80 AEM (A = 0 where RGB is 0, else 0x80), modulates it
by (128, 128, 128, shadowDepth) into 256², 128² and 64² levels (sprites,
TEX1 0x60 bilinear, no blending), then composites levels 3, 2, 1 into SCENE
with ALPHA 0x44, TEST 0x3400D (alpha GREATER 0, DATE with DATM 0, Z
ALWAYS) and the colour (shadowColR/G/B, shadowBlend[i]), and ends with ZBUF
write on, TEST 0x50000 and FRAME 0x40. The brief for this package described
each face as adding 0x80; the packet words say 0x04 / 0xFC, so the count
wraps at 64 net faces, not 2.

**On rd.** The volumes change the stencil of SCENE's D32F_S8 instead of
colour: `rd_ShadowTris` records the triangles with a sign each, replayed as
two triangle lists (increments, then decrements) through
`sprite_world_vs`/`sprite_ps` with colour mask 0, the state's Z test, no Z
write and stencil INCR_WRAP or DECR_WRAP on both faces under write mask
0x3F. `rd_ShadowReset` clears that stencil (a pass with stencil load CLEAR,
depth LOAD) where the GS clears FBP 0x142. `rd_ShadowResolve`, recorded at
the head of `shadow_Draw`, clears the count target and draws seven
fullscreen `blit_vs`/`blit_ps` passes with the SCENE depth-stencil bound:
pass k (0..5) tests stencil bit k (EQUAL, read mask 1 << k) and adds
4 << k to RGB (ONE + ONE, at most 252 in total, exact in UNORM8); pass 6
writes A = 0x80 where the count is not 0. The count target
(`rd_ShadowCountTarget`) is a per-frame `rd_TempTarget` of the scene size,
since the stencil it is resolved from is SCENE's.

**Equivalence.** The stencil holds n mod 64 exactly: with write mask 0x3F an
increment from 63 writes the low six bits of 64 (0) and a decrement from 0
those of 255 (63). The GS colour is 4 n mod 256 = 4 (n mod 64), and the
resolve writes 4 (s & 63). Both sums commute (addition mod 256 and mod 64)
and the Z test writes nothing, so the face order, and the split into
increment and decrement lists, change nothing. The one case where an 8-bit
stencil wrapping at 256 would differ is n a non-zero multiple of 64 with
|n| < 256 (64, 128, 192): the GS colour is 0 (no shadow there) and such a
stencil is not; the write mask removes it. `rd_shadow` checks n = 0..127,
-1..-127, 2, 64, 128, 256 and faces behind the receiver: all 262,144
pixels equal the wrapped colour sum. What stays different: the alpha the GS
stores in FBP 0x142 (As = 0x80 wherever a face was drawn, net count or
not) is not reproduced; nothing reads it, since the chain reads PSMCT24.
Coverage of the volume edges follows the GPU's rasteriser, not the GS's
(both use a top-left rule on the same 1/16-pixel coordinates).

**The PSMCT24 read.** The resolve writes the AEM expansion as alpha and the
chain's first step reads the count target's RGBA view. The shader expands
TEXA after the sampler filtered (`gs_texa_expand` on the filtered texel),
the GS before, so an RGB24 view would give alpha 0x80 to every filtered
texel next to a non-zero one; baking the expansion is exact because
`shadow_Draw` writes the TEXA it reads with (0x80, AEM) itself. The same
order applies to every RGB24 or RGBA16 texture sampled bilinearly through
rd (open item 2).

**State.** Every register write of the three packets is recorded as rd
state in packet order (`shadowHostReset`, `shadowHostDrawBegin`,
`shadowHostChain`, `shadowHostCompositeBegin`, `shadowHostComposite`,
`shadowHostDrawEnd` in Shadow.c), so what list 3 leaves (TEST 0x50000, Z
write on, ALPHA 0x44 FIX 0, COLCLAMP 1, TEXA 0x80 AEM, TEX1 linear, PRIM
0x156, the last level's TEX0, FRAME 0x40 without the half-line offset)
leaks into lists 4 onwards as on the GS. Not recorded: the band
`shadow_Reset` clears at FBP 0x140 (16 lines of the two pages in front of
0x142, which no shadow pass reads; its register writes are repeated by the
0x142 clear) and FRAME 0x142 itself, which becomes the count target. The
blur levels have no depth target on rd (the decoder's convention for
non-SCENE targets); ZBUF never changes ZBP on the GS, and the Z test is
ALWAYS with ZMSK there. CLAMP is never written by Shadow.c; the chain and
composites sample with the CLAMP in force, as the GS does (the composites'
last column and row reach one texel past the level).

**mc_HostDma (deliverable 2).** Left alone. shadow_Reset's packet is an
UNPACK to VU1 memory and MSCAL 0 (SET_GSREGISTER), which `mc_HostDma` could
decode, but through the decoder it would clear SHADOW0 as FBP 0x142 at the
wrong size; the volumes and shadow_Draw are DIRECT packets the reader does
not interpret. Shadow.c's host path records rd calls where it chains each
packet (one `rd_ShadowTris` per object, as one DMA per object); the packets
are still built, for the bookkeeping. The GS register decoder never sees
these writes, so its per-list FRAME/PRIM/TEX0 shadow lags behind list 3
(section 12, as for `rd_Post`).

**Shaders.** None added: `sprite_world_vs` takes the CPU-projected GS
window coordinates as they are, `sprite_ps` with colour mask 0 serves the
stencil-only draw, `blit_vs`/`blit_ps` with the tint is the resolve, and
the chain and composites are screen sprites (`sprite_*`). Pipelines: two
volume keys (`RD_PROG_SHADOW_VOLUME`, INCR/DECR) and seven resolve keys
(`RD_STENCIL_RESOLVE_BIT0 + k`, `RD_STENCIL_TEST_NONZERO`) in
`rd__EnumerateReachableShadow`: 165 reachable in all (156 before).

**Measured** (`rd_shadow` on lavapipe, validation layers on, no errors):

| check | result |
|---|---|
| (a) count vs 4 n mod 256, A 0x80 where non-zero | exact, 0 of 262,144 pixels differ |
| (b) levels 1, 2, 3 vs GS bilinear of the level before (as read back) | 0 LSB each |
| (c) one composite alone vs the GS LERP of the bilinear texel | level 1: 1 LSB; levels 2 and 3: 2 LSB |
| (c) the same vs the float LERP of the same operands | level 1: 1; levels 2, 3: 2 on 16 and 10 channels, else 1 |
| (c) levels 3, 2, 1 together | 3 LSB from the GS LERP, 2 from the float LERP |
| (d) receiver half with the alpha MSB set (FBA) | untouched by all three composites; the other half shadowed |
| (e) Shadow.c's volume (one caster triangle) | one strip, 3 triangles +1 and 5 -1, the packet's kicks exactly; count +1 (colour 4) where the caster projects along the shadow direction, 0 elsewhere |

The composites miss the brief's 1 LSB at levels 2 and 3. The GPU sits up
to 1 above the float LERP over most of the image, depending on Cd (the
same residual with every shadow colour channel set to 64), so it comes
from lavapipe's UNORM8 blender rounding the dual-source LERP. The GS
floors where a float blender rounds, which adds up to 1 more. The 26
channels at 2 from the float LERP sit on band edges. Exact composites would
need the integer blend (`blend_int`), which handles neither a textured
sprite with an alpha test nor DATE. A count of +1 gives RGB 4, so the
shadow colour is 4 x shadowCol / 128 (near black), faded by the levels'
alpha.

**Game run.** Not done. The one permitted run (`timeout 300`,
`ticks=1300`, `pad-boot.txt`) used an exe configured with
`ICO_LINK_EXE=ON`, whose cache defaulted to `ICO_HEADLESS=ON` (R3ab's had
it OFF). It reached stage 3 at tick 996 and ended at 1300 with no renderer,
so there is no dump and no PNG. `build-host/r4b-linux-x64-win` has since
been rebuilt with `-DICO_HEADLESS=OFF` and is ready for the run
(`dump_every=100`, the tick 1200 dump through `rd_replay_tool`).

**rd.h changes (R4b).** `rd_ShadowCountTarget`, `rd_ShadowReset`,
`rd_ShadowTris`, `rd_ShadowResolve`; `RD_STENCIL_RESOLVE_BIT0`; the SHADOW0..2
comments (levels 1..3, not FBP 0x142); `rd_ShadowStrip` is now replayed (its
float form: one strip, one sign). Internal: `RDC_SHADOW_RESET`,
`RDC_SHADOW_RESOLVE` (appended; the dump version is unchanged, an older
replay tool reports them as unknown), `RDC_SHADOW_STRIP` b[0]
`RD_SHADOW_TRIS` with u[3], `RD_SHADOW_STENCIL_MASK`,
`RD_SHADOW_RESOLVE_PASSES`, `rd__ShadowVolumeKey`, `rd__ShadowResolveKey`,
`rd__EnumerateReachableShadow`, `RD_ONCE_SHADOW`.

Open items:

1. The game run and its PNG (above): the boy's shadow on the stage-3 floor
   is unverified in the game. The shadow's receivers are pixels whose alpha
   MSB is 0 when the composites run (DATM 0): the scene clear (alpha 0x80)
   and every texel or vertex alpha of 0x80 and above do not receive.
   Surfaces drawn with TEXA 7F (lists 1 and 2) or alpha below 0x80 do; FBA
   only ever excludes. Whether the floor of stage 3 receives is for the run
   to show. R4c's run (section 15, "Game run") replayed the shadow commands
   without error but still does not show it: in the stage-3 dumps (ticks
   1150 to 1250, the opening cutscene) the volumes are recorded (8,280 and
   8,712 vertices at 1200 and 1250) and the resolve runs, but the count
   target is empty (1 non-zero pixel at 1150, the boy lying on the floor,
   faded; 0 at 1200 and 1250, close-ups of the standing boy with no floor
   in view), so SHADOW0..2 are black and the composites change nothing.
   That fits volumes that end in front of the visible walls (the depth-pass
   count nets to 0); it neither shows nor rules out a shadow on the floor.
   A run that reaches free play on a lit floor (later than tick 1300 with
   `pad-boot.txt`) is still needed.
2. RGB24 and RGBA16 textures are TEXA-expanded after bilinear filtering in
   `sprite_ps` and `vu_ps` (above); with AEM or two TA values, edges between
   texels of different alpha differ from the GS. The shadow chain avoids it;
   other bilinear RGB24 reads (motion blur reading DISPLAY) may not.
3. `GifPacket.c` still maps FBP/TBP 0x142 to SHADOW0, which since R4b is
   level 1 (256²); nothing but Shadow.c wrote 0x142, and Shadow.c no longer
   goes through the decoder. The mapping should go (or point at the count
   target) when GifPacket.c is next edited.
4. NTSC (448 lines): the chain's first step samples 512 rows of the
   count (TH 9); rows 448..511 are other VRAM on the PS2 (pages
   0x1B2..0x1C1) and the clamped or repeated count target on rd.
5. The count target is a per-frame temporary target (a 1 MB texture
   created and freed per frame); a kept target resized with the scene would
   avoid the churn.
6. A SCENE depth clear inside list 3 between `shadow_Reset` and
   `shadow_Draw` would clear the stencil count, where the GS keeps the
   colour; no such clear exists today (list 3 holds only Shadow.c's work).

## 15. Depth fog (wave 4, R4c)

`ico2/seki/src/ZFog.c` under `ICO_RD` (the window build; the headless build
compiles the original code), `port/render/rd_post.c` (`RD_POST_FOG`),
`rd_replay.c` (`doFog`), `rd_pipeline.c` (`rd__FogPlan`, the fog key),
`port/shaders/fog_lut.hlsl` (`fog_lut_ps`). Test: `rd_fog`
(`port/render/test/rd_fog_test.c`), which compiles ZFog.c with the 2D layer
as the window build does.

**What the PS2 does.** `gsb_PostEffect` calls `fog_DrawFog` right after
`shadow_Draw` (`GsBase.c:1047`). It returns unless `debug_fullscreen_effect`
and `GlobalStageSetting.fogOn` are set, takes a CLUT block with
`tex_AllocVramAuto(1, 4)` (in the list current then, 3 after `shadow_Draw`)
and calls `tex_ResetVramPri(4)`, whose `resetVramPri` selects list 4
(`dl_SetDLPriority(pri)`, `Texture.c:1804`) before resetting its VRAM
bookkeeping: the fog's packets go into list 4, as the planner had it, and
list 4 stays current for `MotionBlur` and what follows until they select
their own. Then, in packet order (`ZFog.c:212-267`):

1. BITBLTBUF DBP = the CLUT block, DBW 1, PSMCT32; TRXREG 16 x 16; TRXDIR 0;
   then `fogClutPacket` by DMA: the 256 CLUT words as a 16 x 16 PSMCT32 image.
2. TEXFLUSH; BITBLTBUF SBP 0x1800 (ZBP 0xC0), SBW W/64, PSMZ32 to DBP 0x2800,
   DBW W/64, PSMCT32; TRXREG W x H; TRXDIR 2: the Z buffer copied pixel for
   pixel into a colour-format buffer (which drops PSMZ32's block order).
3. For each 32-line band i < H/32 and j < 4: BITBLTBUF SBP = DBP =
   0x2800 + i W/2, BW 2, PSM 0x14 (PSMT4); TRXPOS (16 + 32 j, 0) to
   (24 + 32 j, 0); TRXREG 8 x 2W; TRXDIR 2.
4. FRAME 0x40 (SCENE) with SCISSOR/XYOFFSET centred, FBA 0, ALPHA 0x44 FIX
   0x80, TEX0 (TBP 0x2800, TBW W/64, PSMT8H, TW = TH = 9, TCC 1, MODULATE,
   CBP the CLUT block, CPSM PSMCT32, CSM1, CLD 1), ZBUF 0xC0 with ZMSK, TEST
   0x50000, TEX1 0 (nearest), the sprite: PRIM 0x156 (sprite, TME, ABE,
   FST, flat), RGBAQ (0x80, 0x80, 0x80, fogStrength), UV 0.5 .. W + 0.5 and
   0.5 .. H + 0.5 over the whole scene at Z 0xFFFFFF; TEX1 0x60.
5. With `fogOffsetA` > 0: TEST 0x30000, an untextured sprite (PRIM 0x446)
   of (fogColR, fogColG, fogColB, fogOffsetA) over the scene at Z
   0xFFFFFFFF, TEST 0x50000.
6. ZBUF write on; FRAME 0x40 with screenOffsetX/Y.

**The Z byte (section 8, open item 3).** The PSMT8H texel at (x, y) is the
top byte (bits 24..31) of the 32-bit word the PSMCT32 layout puts at
(x, y): PSMT8H, PSMT4HH and PSMT4HL share PSMCT32's page, block and column
structure and keep their index in the high bits (GS User's Manual, "Pixel
storage formats" and the memory arrangement figures). Step 2 is a
pixel-for-pixel copy, so before step 3 that word is Z(x, y) and the index
would be Z bits 24..31. Step 3 works in a PSMT4 view of the copy:

- a page is 8 KB in every format: 64 x 32 PSMCT32 pixels or 128 x 128
  PSMT4 pixels. BW 2 makes the PSMT4 buffer one page (128 pixels) wide, so
  a band (base 0x2800 + i W/2 blocks; W/2 = 256 blocks = the 8 PSMCT32
  pages of 32 lines at W = 512) is 8 PSMT4 pages stacked, 1024 = 2W rows:
  exactly the transfer height;
- a PSMT4 block is 32 x 16 pixels, four across a page, so x = 16 + 32 j ..
  +7 (j = 0..3) is pixels 16..23 of every block column, in every block row;
- a PSMT4 column is 32 x 4 pixels, 128 nibbles, the same 64 bytes (16
  words) as a PSMCT32 column. In the documented arrangement, in every row
  of both the even and the odd column, pixels 0..7 hold the low or high
  nibble of byte 0 of eight of the column's words, 8..15 byte 1, 16..23
  byte 2 and 24..31 byte 3 of the same eight words (row 0 of an even
  column: pixel x of 0..7 is nibble 0 of word {0,1,4,5,8,9,12,13}[x],
  pixel 8 + x nibble 2, 16 + x nibble 4, 24 + x nibble 6; rows 2 and 3
  carry the high nibbles, the odd column swaps the row pairs). Moving
  pixels 16..23 to 24..31 therefore copies byte 2 into byte 3 of every
  word of every block of every band.

So the PSMT8H index is **bits 16..23 of the 32-bit Z**, for every pixel.
The sprite passes Z GEQUAL against 0xFFFFFF under ZMSK, so only pixels with
Z <= 0xFFFFFF are fogged, and for them Z >> 16 spans 0..255 without
wrapping; nearer pixels (Z above 2^24) are left alone. With the scene's
screen matrix (section 12: GS Z 536870880 at view depth 2, 1 at 262144),
GS Z is about 2^30 / w - 4095, so fog starts at w = 64 (index 255) and the
index falls as about 16384 / w with distance. `fog_MakeFogClut` stores
f(i) at entry 255 - i, so i = 255 - index grows with distance: f = 0 up to
`fogNear`, linear to `fogColA` / 2 at `fogFar`, `fogColA` / 2 beyond. The
same family of tricks (a Z buffer reinterpreted in another format, channels
moved in 8-pixel strips by local transfers, an 8-bit CLUT for the fog
curve) is described in SCEE's "Using the Z Buffer for Visual and Special
Effects" (lukasz.dk/files/SpecialEffects.pdf), whose 16-bit variant takes
bits 8..15; ICO's PSMT4 variant takes bits 16..23.

`rd_fog` (s) checks this with a CPU model of the GS memory (the PSMCT32,
PSMZ32 and PSMT4 page, block and column tables, written in the test from
the manual's figures and cross-checked against T. Krinkle's public GS
swizzle visualiser): steps 2 and 3 over a 512 x 512 Z buffer of arbitrary
32-bit values, then the PSMT8H read: the index is Z bits 16..23 at all
262,144 pixels (and Z bits 24..31 before step 3); bytes 0..2 are untouched.

**CLUT order.** `fog_MakeFogClut` writes the table in CSM1 storage order
(entries 8..15 and 16..23 of every 32 traded). The GS's CSM1 lookup of an
8-bit index n reads the 16 x 16 CLUT image at x = n bits 0..2 and 4, y = n
bit 3 and bits 5..7, i.e. storage entry n with bits 3 and 4 swapped, which
undoes the trade: index n sees `clut[n]`, the logical table. The host path
passes that table (`lut[n]` = stored entry n with bits 3, 4 swapped; RGBA
from the word's low byte up). `rd_fog` (c) checks it against the formula and
against a CSM1 lookup of an independently swizzled copy.

**On rd.** ZFog.c's host path (`fogHostDraw`) replaces the two packets with
their effect, in packet order: `rd_SetTarget(SCENE, SCENE)` for FRAME 0x40,
`rd_FBA(0)`, `rd_BlendFunc(LERP_AS, 128)`, `rd_Texture` of
`rd_TargetTexture(SCENE, RD_VIEW_DEPTH)` (MODULATE, TCC RGBA) for TEX0,
`rd_ZWrite(0)`, `rd_TestGs(0x50000)`, TEX1 nearest, ABE on and flat for
PRIM 0x156, then `rd_Post(RD_POST_FOG)` with the sprite as the GS gets it
(corners, UVs, RGBAQ, Z) and the LUT; TEX1 linear; the fogOffsetA sprite as
`rd_ScreenPrims` between TEST 0x30000 and 0x50000 (TME off, flat, ABE);
`rd_ZWrite(1)`; FRAME 0x40 again. The transfers are not recorded: steps 1
to 3 are what the LUT and the index rule stand for. `tex_AllocVramAuto` and
`tex_ResetVramPri(4)` still run (the texture cache's per-list bookkeeping
reads them). The GS register decoder never sees these writes (as for
`rd_Post` and Shadow.c, section 12).

Replay (`doFog`, for an `RDC_POST_STUB` of kind `RD_POST_FOG`; the other
stub kinds still stop): the Z source is the target of the bound depth view
(SCENE), else the state's depth target. Its D32F_S8 is copied into a
sampleable texture of the same format (SCENE's depth is an attachment, and
the GS copies too), the LUT is uploaded into a 256 x 1 RGBA8 texture, and
the sprite is drawn through `sprite_ui_vs` and `fog_lut_ps` on the colour
target with no depth attachment. The pipeline comes from the state block
(`rd__FogPlan`: `rd__PlanScreenDraw` without depth, fragment shader
`RD_FS_FOG`), so ALPHA, ABE, PABE, FBA, the colour mask and an alpha test
are the state's; the Z test is done in the shader against the copy (the
same values the attachment holds; ZMSK means nothing is written).
`fog_lut_ps` loads the depth at the texel the UV addresses (TEX1 0:
nearest, texel = floor(UV)), reconstructs the GS Z as `(zmax + 1) - d /
scale` with the target's scale, discards unless the GEQUAL holds, caps a
passing pixel's Z at the sprite's, takes bits 16..23, and applies the LUT
texel with MODULATE (RGB x 0x80 >> 7, A = LUT.a x fogStrength >> 7). One
pipeline (`rd__EnumerateReachableFog`): 166 reachable in all (165 before).

**Precision.** For Z up to 0xFFFFFF the depth `(zmax - z + 1) * 2^-32`
lies in [1 - 2^-8, 1], where D32F steps by 2^-24: the stored depth carries
Z rounded to a multiple of 256 (section 12, open question 3). The index
uses only bits 16..23, so it can be one off only for Z within 128 of a
multiple of 65536 (0.4 % of Z values; a one-step change of the fog alpha,
at most `fogColA` / 2 / (fogFar - fogNear) per step), and the Z test can
pass Z up to 0xFFFFFF + 128. 0xFFFFFF itself stores as 2^24 and would read
index 0 (full fog) without the cap.

**State left behind.** As the GS: ZBUF write on, TEST 0x50000, ALPHA 0x44
FIX 0x80, FBA 0, TEX1 linear, FRAME 0x40; without fogOffsetA PRIM 0x156
(TME on, ABE, flat) and with it PRIM 0x446 (TME off). TEX0 stays the depth
view of SCENE: a later draw that relied on that leak would read the Z copy
as PSMT8H through the fog CLUT on the GS; on rd it logs once
(`RD_ONCE_DEPTH_VIEW`) and draws untextured. Every game path after the fog
writes its own TEX0.

**Measured** (`rd_fog` on lavapipe, validation and synchronisation
validation on, no errors):

| check | result |
|---|---|
| (s) PSMT8H index of the modelled Z copy vs Z bits 16..23 | 262,144 of 262,144 pixels |
| (c) the LUT ZFog.c passes vs the formula and a CSM1 lookup | equal, both cases |
| (r) recorded state, sprite, LUT, fogOffsetA sprite, restores, in list 4 from list 3 (the test's `tex_ResetVramPri` selects the list as Texture.c's does); fogOn 0 records nothing | as listed above |
| (p) 16 x 16 cells at known Z (indices 0..255, Z above 2^24, the 0xFFFFFF tie, the clear at Z 0), strength 0x80, vs the CPU reference (index, LUT, MODULATE, GS LERP, alpha As) | max 1 LSB |
| (p) strength 0xFF with fogOffsetA 0x30 (two LERPs) | max 2 LSB |
| (d) the fog frame dumped, loaded and replayed | 0 bytes differ |

**rd.h changes (R4c).** The `rd_Post` comment for `RD_POST_FOG` (fields:
`lut`, `rgba`, `z`, `rect` = the two corners in 12.4 window coordinates,
`uv` = the two UVs in 12.4 texels). Internal: `RD_FS_FOG`, `rd__FogPlan`,
`rd__EnumerateReachableFog`, `rd__FogShutdown`, `RD_ONCE_FOG`,
`RD_ONCE_DEPTH_VIEW`; `RDC_POST_STUB` of kind `RD_POST_FOG` is replayed;
a draw sampling a depth view logs once instead of `rd__NotImplemented`. The
dump format is unchanged (the LUT is in the payload, the depth view is an
ordinary target view).

**Game run** (R4c: the window build on lavapipe, `SDL_VIDEODRIVER=offscreen`,
`pad-boot.txt`, 1300 ticks, `dump_every=50`, `timeout 420`, exit 0; the
dumps replayed with `rd_replay_tool --target SCENE`, no command skipped,
and inspected with a throwaway reader of the dumps' fog records and
SCENE's depth). Every dumped frame from the title on records one
`RD_POST_FOG` in list 4 (RGBAQ 0x80 grey, strength 0x80, no fogOffsetA).

- Title (ticks 550, 600): fog colour (223, 219, 205), LUT alpha 110 at
  index 0 falling to 0 at index 216. Every pixel has Z <= 0xFFFFFF (the
  castle is far) with indices 0..47, so the whole scene is blended about
  78 % (mean As 99.7) toward the pale grey: a strong, even, pale haze over
  castle, cliffs and sea, the title text and logo unfogged (list 11).
  550 and 600 agree; no flicker. Compared with R3ab's unfogged frame 600
  the image is much flatter; this is what the registers ask for, but
  whether the PS2 title looks this washed out is unverified (the result
  hardly depends on the Z byte: indices 0..47 all sit near the LUT's
  maximum).
- Stage 3 (ticks 1200, 1250; 1000 and 1100 are black, 1150 the faded
  first shot): fog colour (57, 68, 74), LUT alpha 121 at index 0 to 0 at
  255; 55 % and 66 % of the pixels are fogged, with indices spread over
  64..239 (As about 91 down to 7), so the far walls and stairs fade
  toward a dark blue grey with depth while the boy in the foreground (Z
  above 2^24) is untouched. That spread across the LUT's ramp is what the
  stage's fogNear/fogFar were set for, which supports bits 16..23 (bits
  24..31 would give index 0, full fog, at every fogged pixel; bits 8..15
  would band every 256 Z units).
- Nothing missing or miscoloured beyond that in the frames looked at
  (550, 600, 650 black, 1000, 1100, 1150, 1200, 1250). `logs/ico-pc.log`
  has no `rd` warning (in particular no `RD_ONCE_FOG` or depth-view
  notice); its `gif:` HIGHLIGHT and `tex:` placeholder notices (TBP 0x3400,
  0x2A00, 0x2E00 in lists 7 and 8) belong to the effects of wave 5.

Open items:

1. The PSMT8H bit position and the PSMT4 column arrangement are taken from
   the documented layouts (manual figures, cross-checked against a public
   visualiser), not from a hardware capture; a PS2 frame of a fogged stage
   would settle it.
2. The index can be one off within 128 Z units of a multiple of 65536
   (D32F, above); a mapping with scale 2^-24 for the fog's range, or an R32
   copy of the integer Z, would make it exact.
3. Enhanced presets: `doFog` reads the depth at GS texel coordinates and
   sizes the copy by the GS size; a resolution scale needs both scaled.
   Done in wave 7 (R7a, section 19).
4. The fog's TEX0 leak is not reproduced (above).
5. The title's strong haze (game run above) should be compared with a PS2
   capture of the title screen.

## 16. Render-to-texture surfaces (wave 5, R5b)

`ico2/sugipon/src/puddle.c`, `pool.c`, `ito/src/queen_barrier_disp.c` under
`ICO_RD` (the window build; the headless build compiles the original code),
`port/render/rd_water.c` (new: block targets, target aliases, camera scopes,
the pipeline enumeration of these states), additive hooks in `rd_core.c`
(`rd_SetTarget`, `rd_ClearTarget`, `rd_Texture`, `rd__FrameReset`) and one
line in `rd_pipeline.c` (`rd__EnumerateReachable` calls
`rd__EnumerateReachableWater`). `waterDot.c` and `clothAnimation.c` needed
no change. Test: `rd_water` (`port/render/test/rd_water_test.c`), which
compiles the five files with the mesh path, the 2D layer and
`matrixDrive.c` as the window build does.

**What the PS2 does.**

- `PuddleDL` (`puddle.c:430`): `baseSetup` (list 4) clears SCENE's alpha
  with a black ALPHA 0x48 sprite (As 0 written, RGB unchanged) and draws the
  puddle object itself, which sets alpha 0x80 where the puddle is.
  `drawAreaSetup` takes two 0x400-block VRAM areas after
  `tex_ResetVramPri(4)` (`workVram` 0x2800, `work1Vram` 0x2C00), saves +0xC0,
  +0x1C0, +0x100, +0x200, +0x340, calls `gsb_SetVSMatrix(0xE6, 0xE6, ...)`
  (a 230 x 230 screen matrix on the same view) and rebuilds +0x100/+0x200,
  draws into the 256 x 256 frame at `workVram` with ZBUF `0x30000000 |
  work1Vram / 32` (PSMZ32, Z write on, the Z buffer at the second block), a
  grey (128, 128, 128, 128) sprite at Z 0 under TEST 0x30000 (clears colour
  and Z), then `reg_RenderReflection` of the reflected object (a mirrored
  model; its matrices come from the rebuilt +0x100). `drawAreaRestore` copies
  the matrices back and writes TEX0 `workVram | 0x20010000 | 0x600000000`
  (TBW 4, PSMCT32, 256 x 256, TCC RGBA, MODULATE), FRAME SCENE, TEX1 0x60.
  `leveldown` darkens the puddle (TEST 0x3F001: DATE DATM 1, ATST NEVER,
  AFAIL RGB_ONLY; ALPHA 0x64 FIX 0x10, black), `drawRipples` draws the
  ripple rings as STQ strips (PRIM 0xD4: strip, TME, ABE, AA1; TEST 0x3F000)
  whose STs are the screen position scaled by 0.9 / 512 plus 1.5 (the 230 of
  256 texels, with REPEAT), and `copy` lays the work over the screen (UV
  13.25 .. 243.625, ALPHA 0x48 FIX 0x60, or 0x44 in stage 34) where the
  alpha MSB is set.
- `PoolDL` (`pool.c:770`): `copyToWork` copies SCENE (TEX0
  `0x664000800 | W/64 << 14`: TBP 0x800, 512 x 512) into the 256 x 256 block
  at 0x2800 (UV 0.5 .. 512.5: every second texel); the refracting grid
  (`prim_DispMesh3D`, PRIM 0x1C, lit) samples it with STs from
  `updatePoolGeo` (`pool.c:553`: screen position / 512 + 0.5 + 30 h / w);
  `flushWork` takes the block again (and 0x2C00 as Z), clears it white with
  A 0x80 and Z 0, `gsb_SetVSMatrix(0xCC, ...)` (204 x 204), and renders the
  pool object (and `w->dobj`) as reflections; after the restore the
  reflecting grid (PRIM 0x5C) adds the block at ALPHA 0x68 FIX 0x40 with
  STs offset along the reflected eye ray (`pool.c:601`).
  `DispLimitedPoolReflactionMesh` (stage 22's script) is the copy and one
  refracting grid.
- `queen_barrier_disp_proc` (`queen_barrier_disp.c:185`, list 10): the
  block at 0x2800 (2048 blocks) as a 512 x 256 frame (FRAME FBW 8, SCISSOR
  511 x 255, XYOFFSET 1792/1920; TEST 0x30000, ZBUF 0x1300000C0 masked), a
  sprite from SCENE (PRIM 0x116, UV 0.5 .. 512.5 both ways: every second
  row), FRAME SCENE again, ZBUF write on, TEST 0x50000, TEX0
  `vram | 0x24020000 | 0x600000000` (TBW 8, 512 x 256), TEX1 0x60, PABE 1,
  ALPHA 0x44, then the barrier grid (PRIM 0x1C, unlit) with the STs
  `makeRefractST` computes: the vertex pushed along its normal, projected,
  `(X - 2048 + W/2) / W`.
- `DispWaterDot` (`waterDot.c:149`, list 11): raw PRIM 0x1C0 (point, ABE,
  AA1, FST), TEST 0x50000, Z write off, ALPHA 0x48; per dot RGBAQ (128,
  128, 128, life) and XYZ2 = `_FTOI4Vector` of the projected position, so
  the Z is 16 times the GS Z (saturating at 2^31 near the camera): the
  dots pass the Z test almost everywhere.
- `DispClothMesh`, `DispCloth4D` (`clothAnimation.c:1034`, `:1095`): a lit
  Mesh3D through `prim_DispMesh3D` in list 2 or 1, ALPHA 0x44 (mode 7) or
  0x48 with ABE, CLAMP 0: the mesh path of section 13.

**Where the generic decoder was not exact.** `tex_ResetVramPri` puts the
list's VRAM cursor at 0x2800 when no head TBP is locked, and only lists 3, 7
and 8 lock one (`Shadow.c`, `staticBlur.c`), so all three files draw into
FBP 0x140 and read TBP 0x2800. The decoder maps that block to the named AA0
target (section 9): a 256 x 256 target **without a depth buffer**, so the
puddle and pool reflections drew with no Z test and no Z write (their
models' faces in submission order), and the barrier's 512 x 256 copy was
squeezed into a 256-wide target. R3ab's alias path (a temporary target per
FBP) is never reached by these files. The `rd_water` case `decoder` records
the same register writes without the host binding: the block is drawn as
AA0 with no depth and sampled as AA0. Everything else was already exact
through the decoder and the mesh path: the DATE/AFAIL literals, the ZBUF
literals (PSM nibble 0, PSMZ32, the default Z format of temporary targets;
ZMSK to Z write), TEX1 0x60 (bilinear) and the CLAMP the list leaves (the
files do not write CLAMP except the cloth's 0), the sprite UVs, the STQ
grids (perspective-correct STQ interpolation in `vu_common.hlsli`, as the
GS does), the point positions.

**On rd.** The game files bind the block themselves, keeping every register
write:

- `rd_BlockTarget(tbp, w, h, depth)`: one per-frame target per VRAM block
  and size (256 x 256 with depth for puddle and pool; 512 x 256 without for
  the barrier). The same (tbp, size) gives the same target for the rest of
  the frame, as the VRAM is one block: puddle and pool in one frame share
  it in order, as on the GS.
- `rd_AliasTarget(rd_GsNamedBlock(tbp, w, h), block)`: from the allocation
  (`drawAreaSetup`, `copyToWork`, `flushWork`, after `tex_AllocVramAuto`) to
  the last draw that samples the block (`copy`, the end of `dispPool` and
  `DispLimitedPoolReflactionMesh`, after the barrier's `prim_DispMesh3D`),
  in that list only, the decoder's `rd_SetTarget(AA0, none)` records the
  block with its own depth and its `rd_Texture` of AA0 records the block's
  view. Recording only: the commands name the block target, so replay and
  dumps see an ordinary temporary target. `rd_GsNamedBlock` mirrors
  GifPacket.c's FRAME table (its TEX0 table agrees for TH 8 and 9 at
  0x2800); a block the decoder does not name is left to its alias path.
  `gif_HostFlush` runs first, so the decoder's pending writes land on the
  right side of the scope.
- `rd_PushCamera` / `rd_PopCamera` around the reflection draws (after the
  `gsb_SetVSMatrix` and `_MulMatrix` calls, after the matrices are copied
  back): view +0x80, proj43 the 230 or 204 screen matrix +0xC0, near 2, far
  262144, zoom 0 (not known to the game file; vsParam is GsBase.c's).
  `rd__CameraAt(frame, list, index)` gives a draw's camera. The frame camera
  stays `gsb_MakeCommonMatrix`'s (R2c). In Original nothing reads either:
  the reflection's VU draws take the matrices the per-object packets carry
  (+0x100 x node, built from the rebuilt +0x100), which is where the PS2's
  reflection camera lives; the scope is for the Enhanced projection and
  interpolation. Not dumped. Note that the reflection's VU common block
  (viewport +0x340 at qw 8..11) stays the frame's, on the PS2 too (only
  `gsb_MakeCommonMatrix` uploads it), which matters to the scissor programs
  only.

**Pipelines.** Five screen states of `puddle.c` (TEST 0x3F001 with ALPHA
modes 0, 2, 4; 0x3F000 with 0 and 4) and `waterDot.c`'s UI points with
TEST 0x50000 and mode 5 were outside the enumerated screen families;
`rd__EnumerateReachableWater` adds them: 175 reachable (the count includes
R5a's families; the screen-only count is unchanged).

**Measured** (`rd_water` on lavapipe, validation and synchronisation
validation on, no errors; every created pipeline enumerated):

| check | result |
|---|---|
| decoder alone: block 0x2800 | drawn as AA0 without depth, sampled as AA0 (the finding) |
| puddle: the reflection draw's VuCB +0x100 vs the 230 x 230 camera in double precision | 5.5e-8 relative |
| puddle: the block (clear, then the reflected quad) vs a CPU raster of the VU reference | 1,500 interior pixels, 0 LSB |
| puddle: reflected vertices vs the double-precision projection | 0.044 px (within 1/16) |
| puddle: the block vs the projected quad itself (1 px from the edges) | 1,502 inside, 62,002 outside, 0 differ |
| puddle: SCENE after baseSetup, leveldown and copy vs a CPU model (DATE mask, LERP FIX 0x10, GS bilinear of the block at the copy UVs, ADD FIX 0x60) | 255,686 pixels, 0 LSB |
| pool: refraction STs vs a double recomputation; barrier: makeRefractST likewise | 5.2e-7, 7.1e-7 |
| pool: the scene copy; barrier: the 512 x 256 copy | exact (65,536; 131,072 pixels) |
| pool: refracting grid over the copy vs a CPU raster (STQ, bilinear, MODULATE, top-left fill) | 53,357 pixels: 1 LSB from the centre sample, 0 outside the GS precision range |
| pool: block after the reflection pass | 0 LSB |
| pool: SCENE after refraction and the additive reflection | 52,752 pixels: 2 LSB from the centre sample on 185 blue values, 0 outside the range |
| barrier: refracted grid (Z GEQUAL with write, as recorded) vs a CPU raster | 98,094 pixels: 1 LSB from the centre, 0 outside the range |
| water dots: one pixel each, Cd + life | exact |
| cloth: the lit grid vs the VU reference as screen prims | 0 LSB |

The "GS precision range" is, per pixel, the results over texel
coordinates moved by -1/16, 0, +1/16 on each axis and the filtered texel
moved by -1, 0, +1 (the GS addresses texels in 1/16 and filters with 4-bit
weights, the GPU with 8-bit weights); MODULATE by a vertex colour above
0x80 scales that rounding, which is the 2 LSB of the pool's blue channel.
The STQ maths itself is exact: no pixel leaves the range.

**Game run** (window build on lavapipe, `SDL_VIDEODRIVER=offscreen`,
`[dev] start_stage=34` (st13a ELEVATOR, the stage `puddle.c` special-cases),
`use_iso=1`, a pad script walking with the left stick, `ticks=4000`,
`dump_every=50`, `timeout 600`, exit 0 after 4000 ticks in about 6 min).
The stage loads at tick 1; the walk crosses into stage 35 (st24a JETTY) and
back four times between ticks 748 and 1047. In every dumped frame of stage
34 from tick 150 on, list 4 holds one bind of the 256 x 256 block, 13 to
22 VU draws (the reflection model's parts drawn into the block, plus the
specular passes) and 5 screen draws (6 from tick 1950, when the boy's steps
start ripples); stage 35 frames have none. Every dump replays with no
command skipped; `logs/ico-pc.log` has no `rd`, `gif` or `tex` notice.
Replaying a dump with lists 5 to 12 cut (motion blur reads DISPLAY history)
and again with the puddle's leveldown, ripple and copy draws removed, the
difference is confined to the puddle surfaces: 2,607 pixels at tick 400
(the puddle on the stone floor in front of the doorway; SCENE alpha 0x80 on
2,660 pixels after the puddle object's draw, 0 elsewhere after baseSetup),
13,592 at 2200 and 46,303 at 3000 (the wet lower floor where the boy
walks), at most 19 to 48 levels. The block at those ticks shows the grey
clear and the reflected rock and wall parts, faint, as the reflection
model's materials draw them. Whether the strength of the effect matches
the PS2 needs a capture.

**rd.h changes (R5b).** `rd_GsNamedBlock`, `rd_BlockTarget`,
`rd_AliasTarget`, `rd_PushCamera`, `rd_PopCamera`. Internal:
`RdCameraScope`, `rd__WaterFrameReset`, `rd__AliasOf`, `rd__CameraAt`,
`rd__CameraScopes`, `rd__EnumerateReachableWater`. Dump format unchanged.
Developer key `[dev] start_stage` (docs/port/CONFIG.md).

Open items:

1. PRIM.AA1 (the ripple strips' 0xD4, the water dots' 0x1C0) is not
   decoded: no edge antialiasing (GS coverage as alpha) on the ripple rings;
   on points it has no documented effect.
2. The camera scope's `zoom` is 0 (GsBase.c's vsParam is file-local); an
   Enhanced projection of the reflection needs it or a widened +0xC0.
   `gsbHostWidenCull` also widens the reflection's cull frustum when the
   output is wide, which the reflection's 230/204 viewport may not want.
   Wave 7 (R7a, section 19): kept; it only adds objects to the reflection.
3. A texture Texture.c places at 0x2800 in list 4 or 10 with exactly the
   block's TEX0 (TBW 4, PSMCT32, 256 x 256, or the barrier's) would win over
   the block in the resolver (section 11); none was seen.
4. `stage_no == 34` puddles and stage 15/101 pools were found from the code
   (`puddle.c`, `pool.c`); which other stages place POOL/PUDDLE objects is
   in the disc layouts and was not surveyed. The pool, the barrier (stage
   37) and the waterfall's limited pool meshes (stage 22, `st02a.c`) are
   not yet seen in a game run.
5. The block target is a per-frame temporary target (created and freed per
   frame), like the shadow count.

## 17. Full-screen effects (wave 5, R5a)

`ico2/sugipon/src/staticBlur.c` under `ICO_RD` (the window build; the
headless build compiles the original code), `port/render/rd_blur.c`
(recording, the feedback hook, the work-buffer scale rule),
`rd_replay.c` (`doBlurSprite`), `rd_pipeline.c` (`rd__BlurKey`, six
keys), `rd_post.c` (the six kinds dispatched to `rd__PostBlur`),
`port/shaders/fx_sprite.hlsl` (`fx_rect_vs`, `fx_sprite_ps`). Test:
`rd_blur` (`port/render/test/rd_blur_test.c`), which compiles staticBlur.c
with the 2D layer and Matrix.c as the window build does. (Section 16 is
R5b's, written at the same time.)

**What the PS2 does.** `gsb_UpdateGSSystem` calls `FullScreenEffectBefore`
at the head of each frame, after `gsb_SetGsDefault` and `shadow_Reset`
(`GsBase.c:1290`); `gsb_PostEffect` calls `FullScreenEffectAfter` first,
then `shadow_Draw`, `fog_DrawFog` and `MotionBlur` (`GsBase.c:1043-1048`).
Before sets the work buffers (`workBase` = 0x2800, 0x2A00, 0x2E00, 0x3000;
the static initial values 0x2800, 0x2C00, 0x3000, 0x3400 are in force for
the first After of a run, which comes before any Before) and locks list
8's texture head at 0x3A00 (PAL) or 0x3800 (NTSC) with `tex_LockHeadTBP(...,
8)`: 0x2A00 + W·H/64, the end of the scene-sized aura buffer. That call
also selects list 8 (`resetVramPri`, `Texture.c:1804`). Placement,
checked against the code: the flare head, the flare tail, the depth of
field and the motion blur are in list 7 (`gif_StartPacketPri(7)`; the
planner had depth of field and motion blur there, confirmed), the aura
head and tail in list 8. The flare head leaves FRAME on the flare mask
(`makeMaskPatternToWork2`) with Z test and write on, so the "shine level
1" objects of list 7 draw into the mask, and the aura head leaves the aura
buffer bound for list 8's objects; both buffers are scene-sized and use
ZBUF 0xC0, the scene's Z buffer. The effects, with the stage's
`postEffect` (0 none, 1/3 flare "SBLUR" (+ depth of field), 2 depth of
field, 4/5 "GLOW", 6/7 "BLSBLUR"; 8 nothing) and `feedbackEffect` (1
"AURA", 2 "MIRAGE", the default in `sceneManager.c:229`, 3 "AURA V2"):

- **Motion blur** (`MotionBlur`, when `motionBlurAlpha` is non-zero and
  `currentScreenWidth`, which `gsb_UpdateGSSystem` sets to `GlobalTimer`,
  is 0): SCENE, TEX0 = DISPLAY as PSMCT24 (TBW W/64, 512²), TEXA
  0x8000000080, TEST 0x3000C, ZMSK, ALPHA LERP FIX = `motionBlurAlpha`,
  one sprite at Z 0xFFFFFFFF from (-W/2·16 - 4) over W x H with UV 0.25 ..
  W + 0.25, 0.25 .. H/2 + 0.25: DISPLAY's H/2 lines stretched over H, with
  the TEX1 in force (the flare's 0x60 when the flare runs). DISPLAY is the
  reduction of the previous frame's SCENE, so this is a feedback loop.
- **Depth of field** (`depthField`): `copyToWork` SCENE into WORK1 as
  256²  (2:1 both ways, TEST 0, TEX1 0x60), `copyToWork2` WORK1 into WORK0
  256x128 (TFX HIGHLIGHT), six passes WORK0 ↔ WORK1 (the 256x128 top half
  of the 256² buffer) shrinking the rectangle by `cur`/16 and the UV by
  `pre`/16 pixels, TFX HIGHLIGHT with alpha 0, TEX1 0x40 (MMAG nearest) on
  the growing first pass and 0x20 (MMAG linear) after; then four WORK0
  planes into SCENE at GS Z = the screen matrix applied to `depth + width
  i/4`, TEST 0x50000 (Z GEQUAL), ZMSK, LERP FIX 32, 64, 96 with ABE and the
  last at FIX 128 without ABE (an opaque copy behind `depth + width`).
- **Flare** (`makeFullScreenFlareBefore/After`, `pasteFullScreenFlare`):
  SCENE's alpha cleared, then 192 where the scene Z is at most 1 (the sky,
  a sprite at Z 1 under GEQUAL); the 256x64 band above WORK2 and WORK2 filled
  with (0 or, in GLOW, 128 grey, alpha 192); the sun's two fans into WORK2;
  SCENE copied into WORK2 as black with the scene's alpha where that alpha
  is below 0x81 (TEST 0x30815, AFAIL KEEP): the mask is the sky, the sun
  and list 7's shine objects. After: WORK2 reduced into WORK0 (2:1), ten
  `blur` steps WORK0 → WORK1 → WORK0 with TFX HIGHLIGHT (the second pass's
  colour 139/136/132 with alpha 0, or 4 in GLOW: HIGHLIGHT adds Af to RGB,
  40 over the chain, which is the glow), the eye blur, WORK0's alpha set
  to 128, and WORK3 added into SCENE (Cs·FIX + Cd, FIX 128).
- **Eye blur** (`eyeBlur`): WORK0 x 150/128 into WORK3; with the sun on
  screen four ghosts added at FIX alpha/(i + 1), WORK3 shrunk 0.9 toward
  the sun into WORK1, the tint added into WORK3 (Cs·As + Cd), and in
  BLSBLUR WORK1 subtracted from SCENE at FIX 96 where SCENE's alpha MSB is
  0 (TEST 0x34003: DATE, DATM 0; the backlight shadow).
- **Aura** (`auraInspireBefore/After`, list 8): AURA_WORK cleared at the
  head. AURA (1): AURA_TAP (128²) cleared and four taps of FEED128 at ±0.625
  px added at FIX 32, stretched over AURA_WORK at FIX blurCol.a (additive),
  AURA_WORK added into SCENE, and (not paused) AURA_WORK reduced into
  FEED128. MIRAGE (2): AURA_WORK's alpha reduced into WORK0, WORK0's alpha
  copied into FEED128 (Cs·As + Cd with RGB 0), FEED128 pasted into SCENE
  with LERP As (blurCol), then (not paused) SCENE reduced into FEED128 (the
  bottom 16 lines cleared first on NTSC). AURA V2 (3): AURA_WORK's alpha
  written into SCENE's (Cs·As + Cd with RGB 0), then as AURA with the add
  into SCENE only where that alpha's MSB is 0 (TEST 0x34000: DATE, DATM
  0).
  `GlobalTimer` non-zero clears FEED128 to (0, 0, 0, 128) afterwards.
  FEED128 is the only buffer that must survive between frames.

**The generic path (audit).** `rd_blur` started as a dump of what
GifPacket.c's decoder made of these packets (the R4c state). Wrong for
staticBlur.c:

1. TBP 0x2A00 is a 256x128 buffer (flare), the 256² buffer (depth of
   field) and the scene-sized aura buffer by turns. The decoder made a
   temporary target per (FBP, size) for FRAME but resolved TEX0 0x2A00 to
   the first one made in the frame, the aura's (recorded first, at the
   frame head): every flare and depth-of-field blur pass read the aura
   buffer instead of what the pass before had written.
2. 0x2E00 (the flare mask) and the aura buffer became temporary targets
   with their own depth buffers, never cleared, where the GS tests list 7's
   and list 8's objects against the scene's Z.
3. 0x3000 mapped to WORK2 (256²) and the 0x2800 aura taps into WORK0's top
   left (256x128): CLAMP clamped at the wrong edge.
4. TFX HIGHLIGHT was drawn as MODULATE (logged once): GLOW lost its +4 per
   blur pass and every blur pass wrote alpha 0 instead of At.
5. Sampling went through the hardware sampler, which picks the minifying
   filter where the GS's sprites use MMAG (TEX1.K = 0, LCM 0: LOD 0), so
   the depth of field's TEX1 0x40 (MMAG nearest) shrink passes were drawn
   linear; and bilinear weights and the blend rounded as the GPU does (the
   motion blur's feedback drift of section 7).
6. The first After of a run uses the initial `workBase` (0x2C00, 0x3000,
   0x3400 for W1..W3), blocks the decoder has no work buffer for.

The motion blur's DISPLAY read was right (RGB24 view, the stretch); with
TEXA 0x80 and no AEM the alpha is constant, so R4b's TEXA-after-filtering
caveat does not change it.

**On rd.** staticBlur.c's host path renames the eight `gif_*` calls it
makes (`gif_StartPacketPri`, `gif_EndPacket`, `gif_SetGsReg`,
`gif_SetAlpha`, `gif_SetDrawEnviroment`, `gif_SetZTest`, `gif_SetZWrite`,
`gif_SpriteSensitiveOrg`) to `sbHost*` functions in the same file; the game
code is unchanged (`SB_KIND(...)` lines name the effect at each entry
point). They record the same register writes, in the same order, as rd
state: FRAME/SCISSOR/XYOFFSET as `rd_SetTarget` with the target below,
FBMSK 0, PABE and ALPHA, TEST, ZBUF, CLAMP, TEX1, TEXA, FBA; TEX0 is kept
and bound with the next sprite's PRIM (`rd_Texture` of the target view,
or `rd_TextureOff`), ABE and IIP 0 from the PRIM the helper writes; every
sprite is `rd_Post` of the effect's kind with the vertices as the GS gets
them. Packets are still opened and closed through GifPacket.c (list
selection and the packet bookkeeping); the sun fans still go through the
decoder and draw into whatever target is bound (the flare mask).

| VRAM (FRAME / TEX0) | rd target | depth bound |
|---|---|---|
| `workBase[0]`, 256 wide / TBW 4 | WORK0 256x128 | none |
| `workBase[0]`, 128 wide / TBW 2 | AURA_TAP 128² | none |
| `workBase[1]`, 256 wide / TBW 4 | WORK1 256² (256x128 = its top half) | none |
| `workBase[1]`, W wide / TBW W/64 | AURA_WORK W x H | SCENE |
| `workBase[2]` | WORK2 W x H | SCENE |
| `workBase[3]` | WORK3 256x128 | none |
| `workBase[2]` + W·H/64 | WORK2_PAD 256x64 (written, never read) | none |
| 0x3F00 | FEED128 128² | none |
| 0x800, 0 | SCENE, DISPLAY (PSMCT24: the RGB24 view) | SCENE, none |

Buffers are matched by `workBase`, so the first After (initial values)
uses the same targets. The flare's 256x128 and the aura's 128² views of
0x2800, and the flare's 256x128 and the aura's scene-sized views of
0x2A00, are separate targets: each is written whole before it is read in
the frame, and the aura (list 8) replays after everything of list 7, so
their sharing VRAM changes nothing. WORK3 lies inside the PS2's WORK2 and
AURA_WORK regions; WORK2 is consumed before WORK3 is written and AURA_WORK
is written after `pasteFullScreenFlare` reads WORK3, so separate targets
are equivalent here too.

**The sprite** (rd_blur.c, fx_sprite.hlsl). Each record is drawn by
`doBlurSprite` in the GS integer arithmetic with the state block in force,
the destination read from a copy of the target taken just before (so a
sprite sampling its own target reads it as the GS does):

| step | model |
|---|---|
| coverage | pixels whose window coordinate X (12.4, XYOFFSET + pixel) has x0 <= X < x1, y likewise, inside SCISSOR |
| UV | U(X) = u0 + (X - x0)(u1 - u0) / (x1 - x0) in 12.4 integers, truncated |
| filter | TEX1.MMAG (the sprites' LOD is K = 0): nearest = texel (U >> 4, V >> 4); linear = the four texels at (U - 8) >> 4, (V - 8) >> 4 weighted by the 4-bit fractions, sum >> 8 |
| texel | CLAMP or REPEAT on the TEX0 size (2^TW x 2^TH), clamped to the backing target, then TEXA (RGB24, RGBA16) before filtering |
| TFX | MODULATE `(T·C) >> 7`, DECAL, HIGHLIGHT `(T·C) >> 7 + Af`, A = At + Af with TCC, HIGHLIGHT2 (A = At); clamped at 255 |
| tests | alpha test (KEEP discards, RGB_ONLY keeps the destination alpha, FB_ONLY writes colour, ZB_ONLY discards); DATE against the copy; the Z test in hardware against the bound depth (D32F, section 12) |
| blend | `gs_blend_int` (`((A - B)·C >> 7) + D`), PABE, COLCLAMP, FBA; written as k / 255 into the UNORM8 target, which stores k exactly |

The UV step and the bilinear rounding are a model: the GS manual does not
give the sprite DDA or the filter's rounding, no hardware capture was
available, and no emulator source was consulted. They are the form the
4-bit sub-texel precision of the TEX1 filter implies; `rd_blur`'s CPU
reference implements the same rules independently of the shader, so the 0
LSB below says the GPU computes this model exactly, not that the model is
the GS bit for bit.

**Per effect.**

| effect | kind | passes | exactness (`rd_blur` on lavapipe vs the CPU model) |
|---|---|---|---|
| motion blur | `RD_POST_MOTION_BLUR` | 1 sprite DISPLAY (RGB24) → SCENE | 0 LSB, 600 feedback frames (FIX 0x40 static, 0x40 noise, 0x70 cuts) |
| depth of field | `RD_POST_DOF` | 2 downsamples, 6 blur passes, 4 Z-tested planes | 0 LSB (post modes 2, 3, 5, 7) |
| flare | `RD_POST_FLARE` | head 4 sprites + mask, 1 reduce, 20 blur, paste | 0 LSB (post modes 1, 3, 6, 7) |
| glow | `RD_POST_BLOOM` | the same in mode 2 (grey fill, HIGHLIGHT +4) | 0 LSB (post modes 4, 5) |
| eye blur | `RD_POST_EYE_BLUR` | base, 4 ghosts, shrink, tint, BLSBLUR subtract with DATE | 0 LSB (sun on screen, modes 1, 3..7) |
| aura | `RD_POST_AURA` | head clear; mode 1: 5 tap sprites, stretch, add, feed; 2: 4; 3: 6 | 0 LSB, 600 feedback frames through FEED128 (modes 1, 2, 3; blurCol.a 0x20, 0x40) |

The feedback passes run the GS integer formula against the destination as
the GS has it, which is section 7's requirement: `blend_int`'s RGBA8_UINT
ping-pong is not needed, since UNORM8 stores k / 255 exactly and `Load`
returns k. The motion blur's loop also goes through the reduction
(`RD_POST_REDUCTION`, R2c), which is still a hardware-filtered sprite (1
LSB against its CPU reference); the motion blur pass itself is exact, the
loop is exact only once the reduction is (open item 1). The time-corrected
factor hook is `RdPostRec.scalar[2]` (frames this sprite stands for, 1 in
Original) through `rd__BlurFeedbackFix` at replay: for LERP_FIX the FIX
whose retention over dt frames equals FIX's over one, for the additive and
subtractive forms FIX·dt; dt = 1 returns FIX unchanged.

**State left behind.** As the GS's, register by register, with two
differences: TEX0 is recorded at each sprite's PRIM (the GS holds the last
TEX0 written, which only differs if a later draw relied on a TEX0 written
after staticBlur's last sprite, as none does), and HIGHLIGHT is recorded
in the state block as MODULATE (the record carries TFX; a later draw that
relied on the leaked TFX would modulate). GifPacket.c's per-list FRAME,
PRIM and TEX0 shadow does not see these writes (as for `rd_Post`,
Shadow.c and ZFog.c): a later decoder packet in list 7 or 8 that writes
XYOFFSET without FRAME would re-emit the decoder's older FRAME. The sun
fans are the only decoder draws between staticBlur's packets and they
write neither.

**Work-buffer scale.** `rd_WorkTargetScale(preset, outputHeight)`: 1 in
Original (the literal sizes); in Enhanced outputHeight / 448 clamped to
[1, 2], so blur radii stay a constant fraction of the screen.
`namedTargetDesc` (rd_core.c) applies it to the fixed-size work buffers
behind `RD_WORK_SCALE_APPLY`, which is 0: the replay sizes the GS window by
the target's texture, so scaled buffers need the replay's GS-to-pixel
scale first (wave 6). No setting selects it. Wave 7 (R7a): applied, to
the textures (section 19).

**Pipelines.** `rd__BlurKey`: program POST, `fx_rect_vs`/`fx_sprite_ps`,
no blending, the colour mask (FBMSK), and with a depth target the Z test
and Z write; `rd__EnumerateReachableBlur` adds six keys (172 reachable in
all; 166 before).

**Game run** (the window build on lavapipe, `SDL_VIDEODRIVER=offscreen`,
`pad-boot.txt`, `ticks=1300`, `dump_every=50`, `timeout 420`, exit 0; the
host path logs the effects whenever they change). The stages' settings
(disc data) select:

| frames (Before calls; the stage changes at ticks 623, 716, 996) | stage | postEffect | feedbackEffect | motion blur | sun |
|---|---|---|---|---|---|
| 1..24, 145..206, 622..677 | boot, 1, the change to 41 | 0 | 2 (mirage) | 32, drawn | off |
| 25..144, 207..621, 678..714 | 1 (title), 41 | 1 (flare) | 0 | 32 (skipped on `GlobalTimer` frames) | on from 117 |
| 715..995 | 42..45, 40 | 0 | 2 | 32, 36 | off |
| 996..1300 | 3 | 0 | 2 | 32 | off |

So neither the title nor stage 3's opening uses the depth of field or the
glow in this run: depthField's parameters change (1000/500 on the title,
100/500 elsewhere) but no stage selects post mode 2, 3, 4, 5 or 7. Dumps
1000..1250 and 600 replayed with `rd_replay_tool` (no command skipped):

- 600 (title): castle, bridges, sea, logo and menu under the fog's haze,
  as in R4c's frame. The sun's fans are drawn into WORK2 but centred at
  (-278, -465) from the screen centre, above the screen, so the mask is
  black apart from the sky's alpha and WORK3 is 0: the flare adds nothing
  to this frame. The motion blur runs.
- 1000, 1050, 1100: black (the stage load). 1150, 1200, 1250: the boy in
  the dark hall under the letterbox, fogged; AURA_WORK is empty (no list-8
  object), so the mirage pastes FEED128 with As = 0 and changes nothing;
  FEED128 holds the frame's SCENE reduced to 128².
- A dump holds one frame: DISPLAY and FEED128 do not carry the previous
  frame's content when a dump is replayed alone, so the motion blur blends
  32/128 of whatever the replay context's DISPLAY holds; the PNGs are not
  the live frames' exact colours.
- Log: no `gif:` or `tex:` notice from lists 7 and 8 any more (R4c's run
  logged HIGHLIGHT and placeholders at 0x3400, 0x2A00, 0x2E00). One defect:
  `staticBlur: FRAME at a block with no work buffer (0x2c00)` on the first
  frame, the first After before any Before (audit item 6), which the host
  path then drew into SCENE. Fixed after the run (buffers matched by
  `workBase`) and checked by `rd_blur`; the run was not repeated.

**rd.h changes (R5a).** `RD_POST_MOTION_BLUR`, `RD_POST_DOF`,
`RD_POST_FLARE`, `RD_POST_BLOOM`, `RD_POST_AURA`, `RD_POST_EYE_BLUR`
(appended after `RD_POST_PRESENT_BLIT`), their `rd_Post` comment;
`RD_TARGET_AURA_WORK`, `RD_TARGET_AURA_TAP`, `RD_TARGET_WORK2_PAD`
(appended after `RD_TARGET_DATE_SNAPSHOT`); the WORK0..3 comments (WORK2
is now scene-sized, WORK3 256x128); `rd_WorkTargetScale`. Internal:
`RD_VS_FX_RECT`, `RD_FS_FX_SPRITE`, `rd__BlurKey`,
`rd__EnumerateReachableBlur`, `rd__PostBlur`, `rd__IsBlurKind`,
`rd__BlurFeedbackFix`, `RD_FXF_*`, `RD_ONCE_BLUR`. The dump format is
unchanged (the sprites are `RdPostRec`s in `RDC_POST_STUB`s); a dump
recorded before R5a names temporary targets with ids that now collide
with the three new named targets (`rd_dump.c` tells them apart by
`RD_TARGET_COUNT`). `rd_replay_tool` knows the three new target names.
GifPacket.c is unchanged: staticBlur.c no longer goes through it; its
0x2C00 → WORK1 and 0x3000 → WORK2 mappings (wave 2) now point at
differently sized targets and have no user.

Open items:

1. The motion blur loop includes `gsb_Reduction` (`RD_POST_REDUCTION`,
   hardware bilinear, section 12): routing the reduction through the same
   exact sprite would make the loop exact end to end.
2. The sprite UV step and bilinear rounding are a model (above); a PS2
   capture of a flare or depth-of-field frame would settle them.
3. List 7's textures are not head-locked on the PS2 (only list 8 is): a
   shine object whose texture lands at 0x2800 .. 0x3C00 overwrites the
   flare mask between head and tail there. rd keeps the mask intact.
4. AFAIL FB_ONLY with Z write is drawn in one pass (failing fragments
   write Z); no staticBlur state has it (logged once if seen).
5. The depth of field and the glow were not exercised by the run; a stage
   with post mode 2..7 (or the debug menu's "Post Effect") would show them
   in the game.
6. `rd_WorkTargetScale` is defined and called, but its scaling is off
   until the replay supports scaled work buffers. Done in wave 7 (R7a,
   section 19).

## 18. Raw packet builders outside seki (wave 5, R5c)

`ico2/sugipon/src/darkVolume.c`, `particleEffect.c`, `ico2/ito/src/lightning.c`
under `ICO_RD` (the headless build compiles the original code, unchanged),
`ico2/seki/src/MicroCode.c` (`mc_HostDma`), `port/render/rd_replay.c` (the
COLCLAMP 0 wrap path), `port/shaders/raw_wrap.hlsl`. Test: `rd_raw`
(`port/render/test/rd_raw_test.c`), which compiles the four game files with
`MicroCode.c`, `Primitive.c`, `matrixDrive.c`, `Matrix.c` and the 2D layer as
the window build does.

**Route.** The packets stay as the game builds them; the host reads the
finished chain at the point the game chains it (`mc_HostDma` right after
`dl_OpenDma`/`dl_CloseDma`), so the GS register decoder (`GifPacket.c`,
section 9) receives the PS2's register and vertex stream in packet order.
No file needed an explicit `rd_*` replacement for its packets; darkVolume.c
supplies three facts the decoder cannot know (below).

| file | what it builds (raw sites) | route |
|---|---|---|
| `darkVolume.c` | five VU1 SET_GSREGISTER packets (`dvOpenPacket`: DMA cnt, VIF FLUSH, UNPACK V4-32 of a PACKED A+D GIF tag and its pairs to TOP, MSCALF 0, ret), the spheres as raw `gif_SetGsReg` (PRIM 0x144, RGBAQ, XYZ2/XYZ3) | `mc_HostDma` after each packet (generic); the spheres already reached the decoder; three host calls (below) |
| `particleEffect.c` | one SET_GSREGISTER packet: PABE 0, ALPHA 0x44/0x48/0x42 by `alphaMode` 0/1/2 | `mc_HostDma` (generic); the batch is `prim_DispParticle`'s, read since R3ab |
| `lightning.c` | a `gif_*` state packet (ALPHA mode `c`, ZBUF mask, CLAMP, PRIM 0x54), then a VIF DIRECT block (path 2) of GIF REGLIST packets: PRIM, then strips of RGBAQ/ST/XYZ2 | `mc_HostDma`'s new DIRECT reader (generic) |
| `enemy.c` (its own packet code, `:312-322`) | `gif_SetAlpha(1, 4, 128)` and `prim_DispParticle` | unchanged: both were routed (decoder, R3ab) |
| `lineManager.c` | raw `gif_SetGsReg` PRIM/RGBAQ/XYZ2 into the caller's open packet | unchanged: the decoder decodes them (section 9) |

**`mc_HostDma` (MicroCode.c).** New: VIF DIRECT and DIRECTHL (the GIF
packets of the next IMM quadwords) and a GIF reader (`mcHostGif`) behind it
and behind SET_GSREGISTER (R3ab read PACKED A+D tags only):

- PACKED: per register descriptor the 128-bit forms of the GS manual's
  PACKED table: PRIM, RGBAQ (the Q of the last PACKED ST), ST, UV, XYZF2 and
  XYZ2 (ADC selects XYZF3/XYZ3), FOG, A+D, NOP, TEX0/CLAMP (the low
  doubleword); PRE writes the tag's PRIM first.
- REGLIST: NLOOP x NREG raw doublewords, padded to a quadword; A+D and NOP
  descriptors write nothing.
- IMAGE: skipped, logged once.
- FRAME_1 with PSM PSMCT24 (`FRAME` bits 24..29 = 1) reaches the decoder
  with FBMSK's top byte set: the GS does not write a PSMCT24 frame's top
  byte, and the decoder reads only FBP, FBW and FBMSK. PSMCT16 frames are
  logged once (no game file writes one).

Not added: `ref`/`next`/`call` tags (none of these files chains them, and a
host DMA tag cannot hold a 64-bit address) and UNPACK formats other than
V4-32 (the particle data is V4-32, Primitive.c's).

**What darkVolume.c draws on the GS** (list 10, `DispGameOverEffect`,
`SetupDarkVolume` from `queen.c`'s ball and `DarkVolumeGeo`). Packet 1:
FRAME FBP 0x140 (TBP 0x2800) at the scene's size, ZBUF ZBP 0xC0 (the
scene's Z) with ZMSK, a clear of that block to (0, 0, 0, 0) under TEST
0x30000, then TEST 0x50000, ALPHA 0x68 FIX 0x80 (Cs + Cd) and COLCLAMP 0.
The spheres (`renderViewCoordZSphere`, eight bands of 34 vertices, split
at the near plane) are strips whose triangles `drawHT` sorts by their
screen winding: one side drawn with the colour, the other with its two's
complement (`-col`, alpha 0x80). Z-tested (GEQUAL) against the scene and
never writing Z, a face in front of the scene adds its colour and a face
behind it adds nothing, so each pixel of the block ends at (front - back)
x colour per sphere, modulo 256: 0 where a sphere lies wholly in front of
the scene, the colour where the scene cuts it. `darkVolume` nests three
spheres, the outer one positive (255, 255, 255), the middle one (127, 0,
98) and the inner one (127, 254, 156) negative, so a pixel inside all three
ends at (1, 1, 1), between the outer two at (128, 255, 157) and inside only
the outer at (255, 255, 255); the modular sum is what makes these exact
(255 + 1 wraps to 0 where a back face also passes). Packet 2 composites
the block into the scene: FRAME 0x1000040 (SCENE as PSMCT24: alpha kept),
TEX0 0x664122800 (TBP 0x2800, TBW 8, PSMCT24, 512 x 512, TCC, MODULATE)
with TEXA 0x80/AEM (A = 0x80 where the count is not 0), TEX1 linear, RGBAQ
(128, 128, 128, 80), ALPHA 0x44: SCENE moves 80/128 of the way to the count
colour inside the volume. Then ZBUF write on, TEST 0x50000. `sonic` (the
game-over shock ring) does the same with two spheres, then adds the count
times (0, 0, 0) with ALPHA 0x68, which leaves SCENE's RGB and writes its
alpha (0x80 inside the ring, 0 elsewhere in the inset rectangle), then
draws SCENE onto itself zoomed (UV 31.5 + 0.939 x) times (245, 255, 245)
under TEST 0x33001 (alpha test NEVER, AFAIL RGB_ONLY: RGB written, alpha
and Z not) with LERP As: the lens inside the ring.

**darkVolume.c's host facts** (`dvHostBlockBegin`, `dvHostSceneZ`,
`dvHostBlockEnd`):

1. FBP 0x140 at the scene's size is a scene-sized VRAM block, not the
   decoder's AA0 (256 x 256): `rd_BlockTarget(0x2800, W, H, 0)` (R5b,
   section 16) aliased over `rd_GsNamedBlock(0x2800, W, H)` for the
   effect's packets, so the clear, the spheres and the TEX0 read of 0x2800
   all use one per-frame target.
2. ZBUF ZBP 0xC0 is SCENE's Z buffer: after packet 1 the block is bound
   with SCENE's depth (`rd_SetTarget(block, SCENE)`); the decoder binds
   depth only with SCENE.
3. The PSMCT24 FBMSK ends with the composite (`rd_ColorMask(0)`), open
   item 3.

**COLCLAMP 0 (rd_replay.c `doScreenWrap`, `raw_wrap.hlsl`).** A screen-prim
command under COLCLAMP 0 with ABE and an equation that adds or subtracts a
term of the source alone (ALPHA modes 0, 5: Cs F + Cd; 1, 6: Cd - Cs F)
ends at Cd plus the sum of its fragments' terms modulo 256, in any order.
rd draws it in two passes instead of the clamping hardware blend:
`wrap_acc_ps` into an RGBA16F accumulator of the target's size (cleared to
0, the bound depth target and the state's Z test and Z write, blend ONE +
ONE on colour, ONE/ZERO on alpha), each fragment adding its GS term
(Cs F) >> 7 reduced to -128..127 and writing alpha As + 1; then
`wrap_resolve_ps` over the target: (Cd + acc) mod 256 per channel, A from
the accumulator where a fragment landed, Cd from a copy of the target taken
before the command, under the state's colour mask and scissor. Exact while
no pixel takes more than 16 fragments of one command (|sum| <= 2048, where
half floats hold every integer); darkVolume's take at most 9. DATE, PABE
and an AFAIL split are not modelled (logged once, drawn by the clamping
path; no game state has them with COLCLAMP 0). The pipelines are a private
cache in rd_replay.c (`RD_WRAP_PIPES` 16, `rd__WrapPipelineCount`), outside
`rd__GetPipeline`'s cache and the enumeration; darkVolume reaches two.
Shadow.c, the other COLCLAMP 0 user, keeps its stencil path (section 14).

**ALPHA mode 3.** `gif_SetAlpha`'s table entry 3 is {1, 2, 2, 0}: A Cd, B
0, C FIX, D Cs, Cd FIX + Cs, register **0x29**. Sections 2 and 3, the
decoder (`GifPacket.c` `gsAlphaRegs`) and `rd__AlphaRegister`
(`rd_pipeline.c`) had 0x61, which is (Cd - Cs) FIX + Cd; the decoder
therefore ignored mode 3 ("ALPHA_1 equation outside the twelve modes"),
and blend_int/fx_sprite would have evaluated the wrong equation for it.
Fixed in all three (a constant each). The other eleven registers match
the table. Mode 3 is reachable only through lightning's `c`.

**lightning.c's blend mode.** `c` is the BGA lightning record's short at
+0x2E (`BgAnimation.c` `BgaLightningDef`). `gif_SetAlpha` indexes its
twelve-entry table with it, past the end on the PS2 for c outside 0..11;
the host path passes mode 0 there and reports the value once
(`lightningHostMode`). Every mode in range reaches its pipeline through the
decoder (section 3's paths; `rd_raw` records all twelve registers); modes 8
to 11 keep section 3's limits (Ad at half strength, mode 11 leaves Cd).

**Measured** (`rd_raw` on lavapipe, validation and synchronisation
validation on, no errors):

| check | result |
|---|---|
| packets hand-decoded (particle SET_GSREGISTER, lightning DIRECT REGLIST, dark packet 1's 16 A+D pairs) | as in the test's header comment |
| lightning's recorded triangles vs the packet's strips | every vertex (X, Y, Z, S, T, Q, RGBA) identical |
| c = -1..12 | the table's register for 0..11, mode 0 for -1 and 12 |
| dark count (the block) vs a CPU raster of the recorded spheres (12.4 edge functions, flat colour, Z GEQUAL against the known scene depth, sum mod 256; 816 pixels on an edge or a depth tie masked) | 0 of 58,833 written pixels differ; 4,617 at (255, 255, 255), 7,913 at (128, 255, 157), 14,021 at (1, 1, 1) |
| dark composite vs the GS LERP of the bilinear count with TEXA after filtering (as rd samples RGB24) | max 1 LSB; SCENE alpha untouched everywhere (PSMCT24) |
| the same vs the GS order (TEXA before filtering) | max 52 on 504 edge pixels (open item 1) |
| sonic: SCENE alpha after the frame vs what packet 2 wrote | 0 of 258,708 differ (the RGB_ONLY pass and the PSMCT24 composite keep it) |
| sonic: the RGB_ONLY zoom pass where the sampled texels are uniform | 0 LSB on 255,819 pixels (3,997 inside the ring) |
| lightning c = 4 (LERP As), c = 5 (Cs As + Cd) vs a CPU raster with the GS blend | 0 LSB on 10,360 pixels each |
| particles (alphaMode 1, 2; enemy.c's mode 4) vs vu1_ref's sprites rasterised with the GS blend | 0, 0, 2 LSB (the 2 on a pixel of two layers; tolerance 1 a layer) |
| lines: flat 0x142, Gouraud 0x18A, the segment pair | every pixel drawn exact; one pixel per unit of the major axis |

**Game run** (R5c: the window build `build-host/r5c-linux-x64-win` on
lavapipe, `SDL_VIDEODRIVER=offscreen`, `pad-boot.txt`, `ticks=4000`,
`dump_every=50`, `timeout 600`: exit 0 after 418 s, every process gone).
The stages ran 1 (boot, title), 41 at tick 623, 42, 43, 45 at tick 852, 40,
3 at tick 996 to the end. `logs/ico-pc.log` shows the first particle effect
drawn at the title (`alphaMode` 1) and the first lightning bolt in stage 45
(the opening, mode 0); no `mc:`, `gif:` or `rd:` warning, nothing
undecoded, no dark volume (no `darkVolume:` line: the game-over effect and
the queen's ball are not reached by tick 4000). All 79 dumps replay through
`rd_replay_tool` with no command skipped. Frames 450 to 600 (the title)
hold three to six `prim_DispParticle` batches in list 6 under the effect's
own ALPHA (mode 5, Cs As + Cd, from the SET_GSREGISTER packet now read);
the title shows them as three soft white additive clouds in front of the
castle's arches, the same image R4c's frame 550 gives (its dump, recorded
before R5c, drew the batches in the ALPHA that leaked from earlier draws,
which was also additive there). Whether the PS2 title shows these clouds
this bright is not settled without a capture. Stage 45 lasted 64 ticks and
no dump fell on a frame with a bolt (no list-6 screen prims in any dump),
so the lightning has no PNG; the bolt path is covered by `rd_raw` only.
Stage 3 (frames 1150 onwards; frame 2000: the boy standing in the capsule room, textured and lit) draws no raw-builder effect.

**rd.h changes (R5c).** None. Internal (`rd_internal.h`): `RD_FS_WRAP_ACC`,
`RD_FS_WRAP_RESOLVE`, `RD_ONCE_WRAP`, `rd__WrapApplies`,
`rd__WrapShutdown`, `rd__WrapPipelineCount`, `RD_WRAP_PIPES`. Shaders:
`wrap_acc_ps`, `wrap_resolve_ps`. The dump format is unchanged (the wrap is
a replay decision from the state block).

Open items:

1. The composite samples the count's RGB24 view bilinearly and expands
   TEXA after filtering (section 14, open item 2): at the volume's edges
   the alpha, so the LERP weight, differs from the GS's (up to 52 LSB on a
   one-pixel rim). The decoder's sprites have no exact path; R5a's
   `fx_sprite_ps` model would make it exact if the decoder routed such
   sprites there.
2. Lightning's strips carry STQ with Q = 1/w; screen prims divide per
   vertex (`RD_ONCE_STQ`), so a bolt receding in depth maps its texture
   affinely where the GS is perspective-correct. The test's bolt is flat in
   depth. Fix: pass Q to `sprite_vs` and divide per pixel.
3. The PSMCT24 frame mask ends at darkVolume's composite: rd's FRAME
   equivalents (`rd_FrameHead`, `rd_Post`) record no FBMSK, so a mask left
   in the state would leak into the anti-alias pass, lists 11 and 12 and
   the next frame. On the PS2 it holds until the next FRAME write (the
   anti-alias pass's), so list-10 draws between the dark volume and the
   anti-alias pass write SCENE's alpha on rd and not on the PS2. Recording
   FBMSK 0 in `rd_FrameHead` and the post kinds would let the decoder keep
   the GS's scope.
4. The plan's wave-6 gate ("zero `gif_SetGsReg` uses") and this package's
   route disagree: darkVolume.c's spheres and lineManager.c still write
   `gif_SetGsReg`, which the decoder in GifPacket.c decodes. Settled in
   wave 6 (R6a): `gif_SetGsReg` and the decoder are the permanent
   register-level entry (section 9).
5. PRIM.AA1 on lineManager's 0x189/0x18A lines (and the particles') is not
   reproduced.
6. The texture resolver is asked first for darkVolume's TEX0 at 0x2800: a
   game texture noted in list 10 with the identical TEX0 (TBW 8, PSMCT24,
   512 x 512 at 0x2800) would be bound instead of the block; none is known.
7. The wrap path's limits (above): more than 16 fragments of one command on
   a pixel, DATE, PABE, AFAIL splits.
8. The game run reached neither the dark volume nor a dumped lightning
   frame (above): a run into the queen's stage (`[dev] start_stage` 37) or
   a game over, and one dumping every frame of stage 45, would show both.

## 19. Presets and display options (wave 7, R7a)

`port/game/video_options.{c,h}` (new: the `[video]` keys, docs/port/DISPLAY.md
for players, CONFIG.md), `port/platform/window_host.c` (applies them),
`port/render/rd_present.c` (`rd__ApplyDisplay`, `rd__PresentBox`, the
presenter), `rd_core.c` (target scales), `rd_replay.c` (scaled replay, the
wide x scale, the Enhanced samplers and mips), `rd_frame.c` (the wide
`g_proj`), `rd_tex.c` (`rdtex_KeepAlphaCoverage`), `fx_sprite.hlsl`,
`fog_lut.hlsl`, `common.hlsli` / `shader_consts.h` (`DrawCB.g_scale`,
`FrameCB.g_z.yz`), `ico2/seki/src/GsBase.c` (the wide hook),
`ico2/common/src/layout_texture.c` (the primary sprite's full-screen tag),
`rd_video.c` (the movie box), `tools/rd_replay_tool.c` (the options as
flags). Test: `rd_present` (`port/render/test/rd_present_test.c`).

**Settings.** `[video] preset` `"original"` (default) or `"enhanced"`;
`resolution` `"window"` (default), `"WxH"` or `"Nx"`; `aspect` `"4:3"`
(default), `"16:10"`, `"16:9"`, `"auto"` (the window's, clamped to
[4:3, 16:9]); `fullscreen` (false), `vsync` (true), `texture_filter`
`"original"` (default), `"trilinear"`, `"anisotropic"`; `full_height`
(false); `framerate` is R7b's. Original ignores everything but fullscreen
and vsync. `ico_video_get/set/save` (`ico_config_get_*` / `set_*` /
`ico_config_save`); `ico_video_set` bumps a serial that `window_host.c`'s
pump compares, so the Settings menu applies without a restart:
`rd_SetSettings` takes effect at the next `rd_BeginFrame`, where
`rd__ApplyDisplay` turns `RdSettings` into the scales below and recreates
the named targets when a scale changed (`rhi_WaitIdle` first; SCENE is
redrawn by the next frame, DISPLAY's motion-blur history restarts), and
the swapchain when vsync changed. A window resize does the same (aspect
`auto`, resolution `window`). Fullscreen is SDL's borderless desktop
fullscreen (`SDL_SetWindowFullscreen`, no mode change); Alt+Enter flips the
option in memory. `RdSettings` gained `sceneWidth`, `sceneHeight`,
`sceneScale`; `filterUpgrade` takes `RdFilterUpgrade`.

**The Original preset** is section 6, unchanged: every path below is
guarded so that at scale 1 and aspect 4:3 it executes the pre-R7a
arithmetic (`tw == w`, `g_origin.zw = 0.5`, `g_space` identity, no UV
shift, the integer 4:3 box, `fx_sprite_ps` and `fog_lut_ps` multiplying by
1.0). Proof: every frame dump the render tests write (151 dumps: rd_pixel's,
rd_gsbase's 55, rd_blur (61 of 1207), rd_fog, rd_shadow, rd_mesh, rd_raw,
rd_water, rd_layout, rd_tex) rendered by `rd_replay_tool` before and after
this package to DISPLAY, SCENE and a 960 x 720 present: 453 PNGs,
byte-identical on llvmpipe; `rd_present`'s `original` case checks
rd_pixel's frame against the pre-R7a hashes, and that the Enhanced preset
with every option neutral (1x, 4:3, no filter, half height) gives the same
bytes.

**Resolution.** A target record keeps its GS size (`w`, `h`) and gains its
texture's (`tw`, `th`) and scale (`sx`, `sy`, texels per GS pixel):
`rd__TargetScaleOf`. FrameCB keeps the GS size in `g_target`, so every
vertex lands where it did and only the viewport, the scissor (GS pixels x0
.. x1 cover texels floor(x0 s) .. ceil((x1 + 1) s) - 1), copies, snapshots
and readbacks change; `rd__ReadTarget` returns the texture's size. The
scales (`rd__ApplyDisplay`):

| targets | scale |
|---|---|
| SCENE, WORK2, AURA_WORK, DATE_SNAPSHOT, temporary targets of the scene's GS size (the shadow count) | the scene's: `resolution` `Nx`: sy = N, sx = N x aspect / (4/3); `WxH`: W / gsW, H / gsH; `window`: the presentation box in the window; at least 1, at most 3840 x 2160 |
| DISPLAY | the scene's, sy doubled with `full_height` |
| SHADOW0..2, WORK0, WORK1, WORK3, AA0, AA1, FEED128, AURA_TAP, WORK2_PAD | `rd_WorkTargetScale(Enhanced, sy x 448)`: sy clamped to [1, 2] (`RD_WORK_SCALE_APPLY` is 1 now), so blur radii, which are GS distances, stay the same fraction of the screen at finer sampling |
| other temporary targets (puddle and pool reflections, render-to-texture blocks) | 1 |

Sampling a target through `rd_TargetTexture` is normalised, so it needs
nothing. The texel-addressed paths:

- Rasterisation: on a scaled target `g_origin.zw = 0.5 / s`, which puts a
  GS integer coordinate on the left/top edge of its s x s block (at 1 it is
  the pixel centre, as before). Sprites (`expand`) have their corners
  snapped up to whole GS pixels, which is exactly the set of pixels the GS
  covers (letterbox bars sit a quarter pixel off), and their UVs moved by
  (s - 1) / (2s) GS pixels so a texel samples at its own position in the GS
  pixel: a nearest-sampled sprite fills each block with the texel the GS
  samples. Triangles and the meshes rasterise continuously.
- `fx_sprite_ps` (blur, flare, aura, eye blur): the coverage test on the
  texel's GS pixel (its block), the UV from the GS position of the texel's
  centre, t1 addressed in its own texels (`DrawCB.g_scale`: a scaled
  target's scale, 1 for images), REPEAT by modulo (equal to the mask at a
  power-of-two size). At 1: `px * 16`, `u >> 4`, the same sums.
- `fog_lut_ps` (R4c open item 3): the Z copy is the depth target's texture
  size, the texel `floor(uv x size x g_scale)`.
- DATE snapshot, `blend_int`, the COLCLAMP-0 wrap accumulator, the shadow
  count and resolve: the texture's size (they address target texels 1:1).
- `doCopy`: the rectangle in each texture's texels; between targets of
  different scales the copy cannot resample (`RD_ONCE_COPY_SCALE`; no
  game path does it).

`rd_present`'s `scale2` case: rd_pixel's frame (smooth scene) at 2x: every
2 x 2 block of SCENE uniform and equal to the 1x pixel (0 LSB), DISPLAY's
block averages within 1 LSB of 1x (checked at 2).

**Widescreen.** `aspect` = A gives the wide factor f = (4/3) / A (0.75 at
16:9). The game: `gsbHostWideX()` = `ico_video_wide_x()` = A / (4/3), and
`gsbHostWidenCull` divides `projHalf[0]` (+0x240) by it after
`gsb_SetVSMatrixSub`, so +0x280 and every per-object +0x300 cull a wider
frustum; this is now under `ICO_HOST` (the headless build too) so a
headless run exercises it. +0x80, +0xC0 and +0x100 are untouched
(`IsPointIsInScreen` and every screen test read them). puddle.c and
pool.c call `gsb_SetVSMatrix` for the reflection views, so their cull
widens by the same factor: it only adds objects to a reflection, whose own
projection stays 4:3 (R5b open item 2: consistent, a superset). The
renderer: f multiplies NDC x about the target's centre (`g_space[0]` and
`[1]` = (f, 1, 0, 0)) for draws into the wide targets (SCENE, WORK2,
AURA_WORK, the shadow count: `RdTargetRec.wide`): the meshes (`vu_ndc`,
`vu_homogeneous_position`), the shadow volumes and the screen prims, so
CPU-projected prims (sun fans, lightning) follow the 3D and the 2D layer is
a centred 4:3 box. Full-screen draws stretch (f = 1): `RD_SPACE_FULLSCREEN`
prims (`rd_post.c`'s fade, letterbox, brightness, keep, film noise,
anti-alias, the reduction; ZFog.c's sprite; `layout_texture.c`'s primary
sprite through `rd_SetSpaceOverride`), every sprite that spans the
target's whole GS width (clears, Shadow.c's resolve, the game's own fills;
`screenStretch`), and the fullscreen-triangle passes (`fx_rect_vs`, the
fog, the shadow resolve). `gsb_scissorOnDemo`'s bars are letterbox posts:
58 of 512 lines, full width, at any aspect and resolution. `rd__FillCameraCB`
gives `g_proj` / `g_viewProj` the same compression (X' = f X + (1 - f)
2048 W) and `g_clip.w` the aspect. The presenter boxes DISPLAY at A
(`rd__PresentBox`: pillarboxed or letterboxed in the window); the movies
keep the 4:3 box (`rd_video.c`). `rd_present`'s `wide` case checks the
matrices (+0x80, +0xC0, +0x100 byte-identical, +0x240 x scale / (4/3),
+0x280 following, the frame camera unchanged, `g_proj`'s x row) and
`wide169` the pixels (a UI sprite at GS 128..384 in texels 214..468 of the
683-wide SCENE, a full-width fill and the bars over the whole width, the
present filling a 16:9 output).

**Logic unaffected.** Headless `linux-x64`, `pad-boot.txt`, `ticks=3000`:
the default config and `preset = "enhanced"`, `aspect = "16:9"` (the log
shows the options read, which in the headless build happens only through
the cull hook) wrote byte-identical traces (430837 bytes, 3000 ticks, to
stage 3). The trace is the per-tick game state hash of `trace_host.c`
(stage, system status, game flags, save hash), not every object's state.

**Presentation.** Original: section 6 (DISPLAY into the 4:3 box, each line
doubled, bilinear horizontally). Enhanced: the box of the aspect option;
with `full_height` DISPLAY's texture already has every line
(`rd__TargetScaleOf` doubles its sy, the reduction rasterises at that
density) and step 1 is skipped; without it the line doubling runs at the
scaled size. The window's output is the swapchain at the window's pixel
size; fullscreen is the desktop's. Vsync: `rhi_ResizeSwapchain(.., vsync)`
(Vulkan FIFO, else MAILBOX, else IMMEDIATE; D3D12 sync interval 1, or 0
with tearing where DXGI allows it).

**Texture filter.** `texture_filter` trilinear/anisotropic (Enhanced, and
`RhiLimits.textureMips`, new, true on Vulkan and on D3D12 since R6c):
`uploadTextures` creates each power-of-two game texture with its full
chain (`RdTexRec.mipLevels`), level 0 as before, levels 1.. 2 x 2 box
filtered (`rdtex_BuildMipChain`) with alpha coverage kept
(`rdtex_KeepAlphaCoverage`: per level, the alpha scale in [1, 4], never
past level 0's largest alpha, that restores level 0's share of texels with
alpha > 64, the semi-transparent lists' test), uploaded per level
(`rhi_CmdCopyBufferToTexture`'s mip). A change of the option recreates the
textures. Draws whose TEX1 minifies linearly sample with the sampler sets
1 (mip linear) or 2 (plus `min(16, RhiLimits.maxAnisotropy)`); textures
authored nearest stay nearest. The RHI needed no new entry points
(mipLevels, per-level copies and the sampler's mip/LOD/anisotropy fields
were there); `RhiLimits.textureMips` and `maxAnisotropy` are the additions.
`rdtex_SetEnhancedMips`'s per-entry CPU chain (R2b) is not used by the
upload. `rd_present`'s `mips` case: a 64 x 64 one-texel checker minified
8:1 samples mid grey (0 LSB off on llvmpipe).

**Game run** (window build, lavapipe, `SDL_VIDEODRIVER=offscreen`,
`pad-boot.txt`, `ticks=700`, `dump_every=100`, `timeout 600`, 960 x 720
window; `preset = "enhanced"`, `resolution = "2x"`, `aspect = "16:9"`,
`full_height = true`, through a config.toml in `XDG_DATA_HOME`'s pref
folder), exit 0 in about 5 minutes, dumps 100..600 (the run ends at tick
700 before dump 700). The log names the options (`window: 960x720 pixels,
.. Enhanced preset (resolution 2x, aspect 16:9, texture filter original,
full height)`) and has no rd notice (no `RD_ONCE_*` line: no copy across
scales, no DATE or exact-blend size mismatch). Dumps 100..400 are the boot
(black in Original too). Dump 600, `rd_replay_tool --enhanced --aspect
16:9 --resolution 2x --full-height`: SCENE and DISPLAY 1365 x 1024; the
title at 16:9 shows the castle wider (the radio mast on the left and the
far tower on the right, both outside the 4:3 frame), the ICO logo and the
Vibration menu in the centred 4:3 box at the size and place they have in
Original's 960 x 720 present of the same dump, the fog's haze over the
whole width, the sea and bridges sharper at 2x; the reduction's 8-line
crop shows as the thin top and bottom bands, as in Original. The flare
adds nothing to this frame (the sun is above the screen, as in R5a's run).
In the 960 x 720 window the 16:9 picture is letterboxed.

**rd.h changes (R7a).** `RdSettings.sceneWidth/sceneHeight/sceneScale`,
`RdFilterUpgrade`, `rd_SetSpaceOverride`; `rd_WorkTargetScale` applied.
Internal: `RdTargetRec.tw/th/sx/sy/wide`, `RdTexRec.mipLevels`,
`RdContext` scales (`sceneSx/Sy`, `workScale`, `wideX`, `outAspect`,
`filterUpgrade`, `fullHeight`, `spaceOverride`, `vsyncApplied`),
`RD_SAMPLER_SETS`, `rd__ApplyDisplay`, `rd__PresentBox`,
`rd__TargetScaleOf`, `rd__FrameGroupEx`, `RD_ONCE_COPY_SCALE`. Shader
constants: `DrawCB.g_scale` (DrawCB is 112 bytes), `FrameCB.g_z.yz`. The
dump format is unchanged (a dump carries no display options:
`rd_replay_tool --enhanced --aspect --resolution --full-height --filter`).

Open items:

1. The screen-space scissor is not widened: a UI draw clipped by a
   scissor narrower than the screen keeps the 4:3 clip rectangle while its
   geometry is compressed (it clips less, never more). None was seen.
2. A world-projected prim that spans the whole screen width as a sprite is
   taken for a full-screen fill and stretched (`screenStretch`).
3. The reflections' render-to-texture targets keep 4:3 and scale 1; the
   water surface samples them with the game's 4:3 UVs, so at the sides of
   a 16:9 frame a puddle shows the reflection's clamped edge.
4. Popups and the port's own text (port/ui) draw in list 12 in UI space,
   so they sit in the 4:3 box at the scene's resolution; an overlay at the
   output's size is 6C's (no post-present hook yet).
5. Interpolation (R7b): `RdPresentPreset.interpolate` and
   `presentAlpha()` are still the hook; the scaled DISPLAY is what it
   blends. Done in R7b (section 20), differently: the presenter does not
   blend two DISPLAY pictures; it replays the current frame with the
   keyed draws' data blended, so DISPLAY is drawn once per present.
6. Mirror: `RdPresentPreset.mirror` is still unimplemented. Done in R7c
   (section 21).

## 20. Frame rate and interpolation (wave 7, R7b)

`port/render/rd_interp.c` (new: the blend, the snap rules, `rd_Present`),
`rd_core.c` (a third retained frame, `rd_EndFrame` without a replay when
interpolating, `rd_CameraCut`, `rd_FrameNumber`, the half-way dump),
`rd_post.c` (the fade and letterbox sprites keyed), `rd_mesh.c` (particle
batches keyed), `rd_water.c` (the ring), `rd_video.c` (an FMV picture
holds the output), `rd_present.c` (`presentAlpha()` removed),
`port/platform/window_host.c` (the presentation loop),
`port/game/video_options.{c,h}` (`[video] framerate`, the cut signal),
`ico2/omori/src/camera-root.c` and `ico2/common/src/StageManager.c` (the
cut hooks). Test: `rd_interp` (`port/render/test/rd_interp_test.c`).

**Setting.** `[video] framerate`: `"original"`, `"uncapped"` (the default)
or a number 30..1000 (`ico_video_parse_framerate`; docs/port/DISPLAY.md,
CONFIG.md). The Original preset is always `"original"`
(`ico_video_framerate()`); in Enhanced anything else sets
`RdSettings.interpolate`, which rd honours only in Enhanced
(`rd_InterpolationActive`). `"original"` is the R7a path unchanged:
`rd_EndFrame` replays and presents the frame once and the window sleeps to
the next vsync deadline, so the picture is held for the tick's two
refreshes (PAL frame step 2).

**The loop** (`ico_window_pace`). The simulation is untouched: `main_host.c`
still steps one simulated vsync (`ico_host_step`) and calls the pace once
per vsync, 20 ms PAL, 16.68 ms NTSC. With interpolation, `rd_EndFrame` only
closes the frame (and dumps it), and the pace, instead of sleeping to the
deadline, presents until it: each present is `rd_Present(alpha)` with alpha
= (now - the time the last frame closed) / (the time between the last two
closes), clamped to [0, 0.999]. A frame closes inside the step before the
pace (the scheduler's `gsb_UpdateGSSystem` -> `dl_Swap`), so its time is
the start of that vsync period, a simulated-time clock read through
`rd_FrameNumber()`. The present blocks on vsync (FIFO), so `"uncapped"`
with vsync on runs at the display's rate; a number N waits at least 1/N s
between presents; vsync off and `"uncapped"` presents back to back. Behind
the deadline, or when one present took longer than a vsync period (a
software driver), only a frame's first present is made, so presenting
never costs the simulation more than the one replay per frame of the
original path. A movie on the output (`rd_video.c` sets
`RdContext.videoShown` until the next frame closes) makes `rd_Present`
return false and the pace sleeps as before. The window logs presents and
game frames every 10 s (`window: N presents and M game frames in T s`).

**Latency and the pair.** alpha 0 is the previous frame's data in the
current frame's structure, alpha 1 the current frame: presentation is one
tick (40 ms PAL) behind the simulation. `rd_BeginFrame` records into the
ring slot after the last closed frame (`RD_FRAME_RING` = 3: recording,
current, previous), so the pair survives while the game records the next
frame, which `dl_Clear` opens right after `dl_Swap` closes one.

**What blends** (`rd__InterpFrame`). The output frame is a copy of the
current frame: its lists, state commands, textures, targets and unkeyed
draws exactly as recorded. A keyed draw (`RdKey`; RD_KEY at the call sites,
0 = never) takes its data blended from the draw of the same type, list and
key in the previous frame, the n-th occurrence matching the n-th (the call
ordinal). Per command:

| command | blended | kept from the current frame |
|---|---|---|
| `RDC_MESH`, `RDC_SKINNED` | VuCB qw 2 (the UV scroll SET_UVOFFSET left, Texture.c's wrap of uOfs/vOfs into (-1, 1] by 2 undone: a step over 1 is taken as the wrap; the cluster fade alpha in w), qw 4..15 (world to screen, viewport, inverse view), 16..27 (the model matrices), 28..35 (the light matrices); the bone quadwords | qw 0, 1, 3 (constants, the GIF tag), the mesh |
| `RDC_GRID` | the VU block; each vertex's position, and normal when lit | strip headers, colours, STs |
| `RDC_PARTICLES` | the VU block; each particle's (x, y, z, size) | header, UV, grey, alpha |
| `RDC_SCREEN` | XY, Z, RGBA of every vertex | STQ, prim, space |
| `RDC_SHADOW_STRIP` | XY and Z of every vertex | the triangle split |
| frame camera, VU common block | `RdCamera` view, proj43, zoom, near, far; `RdVuCommon` matrices | `cut` |

Blends are element-wise, `(1 - t) p + t c` (exact at both ends); a float
pair that is bit-identical, or not both finite, keeps the current value;
integers round to nearest. Matrices blended element by element shorten a
rotation's axes by cos(theta/2) half way (0.4 % at 10 degrees in a tick).

Keyed today: `RegistPacket.c`'s meshes (packet, list, MSCAL code), the
grids (`Primitive.c`: the Mesh3D, list), the shadow volumes (`Shadow.c`:
the object), the fade and letterbox sprites (`rd_post.c`), and the
particle batches (`rd_mesh.c`, list and code 18: MicroCode.c draws them
from the VU scratch with key 0). No 2D call site passes a key yet
(GifPacket.c, layout_texture.c, DisplayFont.c, port/ui): their quads are
the current frame's, which is also what keeps text and menus from sliding
between unrelated glyphs.

**What snaps.** A keyed draw is the current frame's when the previous frame
has no match; when the shape differs (mesh, program, code, clip, payload
size, batch range, bone, stream, vertex or particle count, prim or space;
for a shadow volume the triangle counts: a topology change); or when it
jumped: the model origin in the world moved more than
`RD_INTERP_JUMP_WORLD` (300, the game's centimetres) in the tick (normal
programs, grids, particles: the model to screen translation qw 19 through
the inverse of the common block's world to screen qw 4..7, so the camera
cancels; a skinned draw: its first bone's translation), or a screen prim's
or shadow vertex moved more than `RD_INTERP_JUMP_SCREEN` (256 GS pixels).
A particle that moved more than four times its size, or whose alpha is 0
in either frame (born or dying), keeps the current position. The whole
frame is the current one (`rd__InterpSnap`) without a closed previous
frame; when the frame numbers are not consecutive (a discarded frame);
when either frame is a keep frame; on a cut (`RdFrame.cut`, copied into
`RdCamera.cut` at `rd_EndFrame`); when the targets were recreated after the
previous frame (display options: `RdContext.interpFloor`); when the scene
size changed; at a fade edge (either frame's fade at 0x80 or more: a scene
can change behind black); and when the camera turned more than
`RD_INTERP_CAMERA_TURN` (30 degrees) or its eye moved more than
`RD_INTERP_CAMERA_MOVE` (300) in the tick, a cut the hooks missed.

**The cut signal.** The game files call `ico_video_camera_cut()` (a counter
in `video_options.c`, which the headless build links, read by no game code;
under `ICO_HOST`): camera-root.c `InitCamera` (a stage's first camera), the
camera mode change at the end of `SetCameraMatrix` (path camera in or out,
`monitorCameraInit`; where it sets `GlobalTimer`), the cut back to the game
camera (`gamecamCutBack` before `SetCameraMatrix_Ico2`), and
`InsertCamera_Exec` with cutType 0 (camera-ico2.c then calls
`initMonitorCamera(1)`); StageManager.c `start_stage_Load_thread` (a stage
change). The window compares the counter once per vsync after the step and
calls `rd_CameraCut()`, which marks the frame being recorded: the step
closes the previous frame in the scheduler before the game's threads run
the tick that cuts. Not hooked (camera-ico2.c is not this package's): the
cut camera-ico2.c makes itself when the camera group's kind changes
(`initMonitorCamera(1)` with `flag == 0`); the camera-jump test covers a
cut that moves the eye 3 m or turns 30 degrees.

**Held at the tick.** Everything unkeyed or carried by state: CLUT
animation (textures are updated in place, `rd_UpdateTexture`), the
rand-driven draws (the menu sparkle and lightning are screen and world
prims with key 0), the dissolve FIX (an ALPHA state command), film noise
(an unkeyed post sprite), the morph path (`rd_UpdateVuMesh` rewrites the
mesh, not the frame: see open item 2), shadow topology changes (above),
and the aura's feedback: its sprites whose target is FEED128 are dropped
from every present but a tick's first (`firstOfTick`), so FEED128 advances
once per tick as on the PS2 and the other presents paste the held buffer.

**Feedback per present.** The motion blur's sprite (`RD_POST_MOTION_BLUR`:
DISPLAY back into SCENE, LERP FIX) runs at every present, standing for dt
ticks (`RdPostRec.scalar[2]`, R5a's hook). On the PS2 DISPLAY keeps
a = (128 - FIX) / 128 of itself per tick; `rd__BlurFeedbackFix` gives the
FIX that keeps a^dt: FIX' = round(128 - 128 a^dt) (additive and subtractive
forms: FIX dt), so k presents of dt = 1/k keep a per tick at any rate. dt
is the alpha advanced since the previous present plus whole ticks when
frames closed in between, clamped to [1/256, 4]; dt = 1 returns FIX
unchanged. The FIX is an integer, so a short dt rounds: FIX 32 (a = 0.75)
at dt 1/6 wants 5.99 and gets 6.

**Determinism.** `rd_interp.c` reads the retained frames and writes only
its own copy; the game's only addition is the cut counter, which no game
state reads. Headless `linux-x64`, `pad-boot.txt`, `ticks=3000`: HEAD
before this package, this package with the default config, and with
`preset = "enhanced"`, `aspect = "16:9"`, `framerate = "uncapped"` wrote
byte-identical traces (430837 bytes, md5 644806ba35abf126b19ad44fe9b900fb,
the same size as R7a's). Original: the 151 golden dumps of section 19
(regenerated by the render tests of HEAD and of this package) rendered by
`rd_replay_tool` to DISPLAY, SCENE and a 960 x 720 present: 453 PNGs,
byte-identical to R7a's set, before and after this package, from either
package's dumps (`build-host/r7b-golden/{gen,render,cmp}.sh`).
`rd_present`'s Original hashes are unchanged.

**Test** (`rd_interp`): without a device, two synthetic frames: alpha 1 is
the current payload byte for byte, alpha 0 the previous frame's sprite; a
UI sprite translated 32 px lands at 16 px (within 1/16 px) with its colour
half way and its UVs the current; the same key twice matches in order; a
missing key, a vertex count change and a 300 px jump snap; a cut
(`rd_CameraCut`, `RdCamera.cut` set), a fully faded frame, a discarded
frame between, a 40 degree camera turn and a keep frame snap the frame,
while a 5 degree turn blends the camera to 2.5; a mesh's model and light
matrices half way, its UV scroll across the wrap the short way, its GIF
tag qword the current, a 10 m teleport snapping; a grid's every vertex half
way with STs, colours and tags the current; a particle half way and one
moved 100 sizes the current; a shadow volume half way, and the current one
after a triangle count change; the fade level half way; the feedback FIX
at dt 0.5 within half a FIX step of a^0.5 for FIX 8..120, additive FIX x
0.5, the motion blur sprite carrying dt, FEED128's aura sprite dropped on a
later present. On a device the same, then: replays of alpha 0 and alpha 1
equal the previous and current frames' replays (0 pixels differ, outside
the two unkeyed and unmatched sprites), the half-way sprite covers columns
16..47 exactly; `rd_Present` does nothing in Original or with interpolate
off, presents in Enhanced; a scale change snaps the pair across it
(`RD_SNAP_HISTORY`) and the next pair blends. 0 validation errors.

**Game run** (window build, lavapipe, `SDL_VIDEODRIVER=offscreen`,
`pad-boot.txt`, `ticks=1300`, `timeout 600`, 960 x 720 window,
`preset = "enhanced"`, `resolution = "1x"`, `framerate = "100"`, vsync on,
`dump_every=100`, `ICO_RD_DUMP_INTERP=1`): exit 0 in 305 s, frames
100..1200 dumped with their half-way pairs. The rates are lavapipe's: the
simulation runs well below real time (2 to 10 game frames a second), and
the log's 10 s windows show 2.0 to 3.6 presents per game frame (e.g. 4.3
presented fps at 2.2 game fps on the title, 25.2 at 10.1 during the stage
load, 49 at 13.5 during the black boot frames); the cap of 100 was never
reached. The `interp:` summaries (per 250 frames): 231, 210 and 186 frames
blended; snaps: keep 3, 2, 4 (the dumps at 100, 200, 800, 900 and 1000 are
keep frames), cut 2, 4, 7, fade 14, 34, 53, camera 0, gap 0, history 0;
keyed draws 89 % / 75 % / 70 % blended, 1312..1394 unmatched per 250
frames (about 6 a frame: draws present in one frame only), 0 / 0 / 62
mismatched, 0 / 3510 / 21 jumped (below). Pictures (`rd_replay_tool
--enhanced --resolution 1x`, current against half way): 1200 (stage 3's
opening, the boy close up under the letterbox) shows the railings and the
boy part way between the two ticks, 3 % of SCENE's pixels differ by more
than 8, the letterbox bars black and identical in both, no tear or
doubled UI; 500 and 600
(title) differ in 8 and 4 pixels (the camera barely moves), the copyright
line identical; 700 (the opening's fade-in) is half way darker. Measured
with two things since changed (not re-run): the loop then made a second
present per frame on this slow driver (the cost rule above came after it),
and the jump test used the model to view translation (qw 27), which list
5's dissolve draws do not upload (they carry another object's). In frame
600, 49 keyed draws kept the current data (43 jumped, 6 unmatched): 33
list 5 meshes whose qw 27 was one stale value, and 16 skinned draws (10 in
list 0, 6 in list 1) of one character whose bones form a coherent skeleton
about 1.5 m across. The common block inverse puts the list 5 meshes at the
camera's eye and the stage geometry at the world origin (checked on frame
600's dump); whether the 10 list 0 skinned draws really moved 3 m in that
tick is not known without the previous frame's dump. The 6 list 1 skinned
draws are unmatched in every dumped frame (500, 600, 700, 1200: about the
6 unmatched a frame above), which suggests their key changes every frame
(open item 7). Only frame 600 was examined draw by draw.

**rd.h changes (R7b).** `rd_InterpolationActive`, `rd_Present`,
`rd_FrameNumber`, `rd_CameraCut`; the `RdSettings.interpolate` comment.
Internal: `RD_FRAME_RING`, `RdFrame.cut` / `.fade`, `RdContext.interpFloor`
/ `.cutPending` / `.videoShown`, `RD_SNAP_*`, `RdInterpStats`,
`rd__InterpSnap`, `rd__InterpFrame`, `rd__InterpShutdown`,
`RD_INTERP_JUMP_WORLD` / `_SCREEN`, `RD_INTERP_CAMERA_MOVE` / `_TURN`;
`rd__PrevFrame` follows the ring. The dump format is unchanged (the keys
were always dumped; `cut` and `fade` are not). `ICO_RD_DUMP_INTERP=1`
writes `rd-NNNNN-i50.rddump` next to each frame dump.

Open items:

1. 2D call sites pass no key (GifPacket.c's decoder, layout_texture.c,
   DisplayFont.c, port/ui): menus that slide, subtitles that move and the
   popup stay at the tick. Keying them needs a stable object per item
   (layout row, popup); glyph quads should stay unkeyed.
2. The morph path rewrites a mesh's stream in place (`rd_UpdateVuMesh`),
   and the frame keeps only the mesh id: while the next frame records, its
   morphs are already in the mesh, so a morphing face is presented with
   the newer tick's shape (one tick early, not blended). Blending morphs
   needs the stream in the frame (or a per-frame copy for morphing meshes).
3. Particles are matched by list and order; an emitter that adds a batch
   ahead of another in the same list shifts the order and snaps both (the
   count differs) rather than mismatching.
4. The cut camera-ico2.c makes on a camera group change is not hooked
   (outside this package's files); the camera-jump test catches large ones.
5. Rotations blend element-wise; a fast spin (a turn of tens of degrees in
   one tick on a bone or object) would shrink half way. None seen; a
   quaternion blend of the model matrices would fix it.
6. The run's figures above were measured before two post-run fixes (the
   slow-driver present rule and the world origin from the common block);
   the next run with the window should confirm the jump counts drop and
   that a software driver presents once per frame.
7. `RegistPacket.c` keys a mesh by its PacHeader pointer, list and code. A
   pass that builds its packet each frame in a double-buffered packet area
   would get a new key every frame (the six list 1 skinned draws above are
   never matched, consistent with that, unverified); such a site needs a
   key from the object and part instead (RegistPacket.c is not this
   package's file).

Open items for mirror mode (R7c), which follows: the mirror flip belongs in
the presenter's step 2 (`RdPresentPreset.mirror`), so it applies to every
interpolated present as well; UI-tagged prims are pre-flipped at record
time, which leaves the interpolation unaffected (it blends XY, and both
frames are flipped alike); the camera-jump and world-origin tests read
matrices, not the flipped picture, so they need nothing; `rd_video.c`'s
own mirror flag is separate (FMV). Done in R7c (section 21), with the UI
flip at replay instead of record time.

## 21. Mirror mode (wave 7, R7c)

`port/render/rd_present.c` (step 2 flips), `rd_replay.c` (the UI flip,
`mirrorUi` / `mirrorDraw`, the scissor), `rd_core.c` (`rd_SetMirror`,
`rd_MirrorActive`), `rd_internal.h` (`RdContext.mirrorRun`,
`rd__MirrorOn`), `rd_video.c` / `port/fmv/rd_video.h` (the FMV switches),
`rd_water.c` (waterDot's pipeline in WORLD space), `ico2/seki/src/GifPacket.c`
(raw list-11 writes tagged WORLD), `tools/rd_replay_tool.c` (`--mirror`).
The game side: `port/game/options.c` (the run's value, its listener, the
per-slot flag), `port/ui/settings.c` (the New Game screen, the Gameplay
rows, the listener that calls `rd_SetMirror`),
`ico2/common/src/layout_action.c` (the hooks), `port/audio/audio_host.c`
(the pan). Tests: `rd_mirror` (`port/render/test/rd_mirror_test.c`),
`settings`, `options`, `audio_pan`; players: docs/port/SETTINGS.md
"Mirror mode"; the slot flag: docs/port/SAVES.md "Mirror mode".

**The flag.** `rd_SetMirror(on)` is the run's value (port/ui/settings.c
registers it as `ico_opt_set_mirror`'s listener, so the New Game choice, a
load and the title's reset reach it at once). The window rebuilds
`RdSettings` from the display options and leaves `mirror` 0; tests and the
replay tool set `RdSettings.mirror`. The mirror is on when either is
(`rd__MirrorOn`); it is read at replay and at present, so it applies from
the next frame replayed. `rd_SetMirror` logs each change ("rd: mirror mode
on (frame N)").

**The present.** Step 2 of the presenter (lines target, or DISPLAY with the
full-height scene, into the output box) samples its source right to left
when the mirror is on: `blit`'s `uvRect` x runs from `sw` to 0, so `blit_vs`
gives u = 1 - t. Both presets have `RdPresentPreset.mirror` set: mirror mode
is a gameplay option, not a display one. Every present goes through step 2,
so the Original present inside `rd_EndFrame`, the Enhanced one and every
interpolated `rd_Present(alpha)` (section 20) are flipped alike. With the
mirror off the blit is the same call as before (Original byte-identical:
R7a's golden set, 453 PNGs, rendered from R7b's dumps and from dumps the
R7c build's tests recorded, 0 differ). With it on the output is the exact
horizontal flip of the output with it off when the box's horizontal scale
is a power of two (the sample positions are dyadic and mirror exactly; a
1024 x 768 output in `rd_mirror_test`); at other scales the bilinear
weights of mirrored positions may round differently by 1 LSB.

**The UI flip.** The present flips everything, so the 2D that must read
normally is flipped back where it is drawn: a screen-prim command tagged
`RD_SPACE_UI` replayed into SCENE or DISPLAY (the targets the present
shows) has its vertices reflected about the target's horizontal centre,
after `expand` and before the draw (`mirrorDraw`); the scissor of that draw
is mirrored too (`scissorRect`: GS pixel p becomes w - 1 - p). Draws into
any other target (WORK*, AA*, the shadow and water targets) are never
flipped: what they hold is sampled by later passes in GS coordinates.
WORLD and FULLSCREEN prims, the meshes, the shadow volumes and resolve,
`rd_WorldPrims` (darkVolume, lightning, lineManager), the posts (fade,
letterbox, reduction, keep, film noise, the fog, the flare and the eye
blur's sun ghosts in `staticBlur.c`) are drawn as recorded and flip with
the present: they are the world. Nothing else changes: no winding,
culling (none is ever set: `RhiPipelineDesc.cullNone`), VU program,
shadow sign, DATE or feedback path is touched.

The brief put the pre-flip at record time; it is at replay instead, for
the same result: the interpolation blends the recorded XY of both frames
and the reflection is affine, so flipping after the blend equals blending
flipped frames; the reflection needs the bound target's width and scale,
which the replay knows; and a frame dump stays a record of what the game
drew, so `rd_replay_tool --mirror` shows any dump mirrored (as R7a's
display options are flags of the tool, not part of the dump).

Exactness. A pixel p of a target w pixels wide is shown at w - 1 - p. The
GS covers pixel p when x0 <= p < x1 and takes its attributes at p (the
replay puts GS integer coordinates on pixel centres, R2a). Reflected, pixel
q = w - 1 - p would need w - 1 - x1 < q <= w - 1 - x0: the open and the
closed end swap. On the GS's 1/16-pixel grid that is the same as
w - 1 - x1 + 1/16 <= q < w - 1 - x0 + 1/16, so triangles (sprites and
points are expanded to triangles) are reflected as
X' = C - 1 + 1/16 - X with C = 2 (ox + w / 2) (ox = 2048 - gsW / 2, as
`bindDraw` sets the origin; C = 4096 for the 512-wide SCENE and DISPLAY):
exactly the mirrored pixels for any 12.4 edge. The attributes taken at q
are then the original's at p + 1/16, so each triangle's UVs are moved back
by one sixteenth of a pixel of their x gradient (computed per triangle from
its three vertices): textured sprites and quads sample the same texel at
the mirrored pixel. Colours and Z are not moved (a sixteenth of a pixel's
step of a gradient; the layout's and the font's colours are flat). Lines
are reflected about the pixel centre (X' = C - 1 - X); the GPU's line rule
is not the GS's anyway (R2a). On a scaled target (Enhanced, R7a) "1" is
one texel, 1 / (sx * wide) GS pixels; the snapped sprite corners (R7a's
`snapAxis`) are whole GS pixels, which reflect onto whole blocks.

`rd_mirror_test` checks, on lavapipe: a WORLD-only frame in DISPLAY
presents as the exact flip of itself with the mirror off (Original and
Enhanced 1x, 86356 asymmetric pixels); UI prims in DISPLAY (1:1 and 2x
nearest textured sprites with the +8 nudge, an untextured sprite with
quarter-pixel edges, a textured two-triangle quad, three points, a sprite
under a scissor that cuts it) land on the same output pixels with the
mirror on and off, while WORLD content in the same frame flips; the same
UI drawn into SCENE is SCENE's exact flip; a UI sprite into WORK1 is not
moved; `rd_Present(0.5)` of a keyed sprite moving between two Enhanced
frames is flipped exactly. Validation clean.

Through the reduction. The UI is drawn into SCENE and reaches DISPLAY
through `gsb_Reduction`'s sprite (`rd_post.c` `postReduction`), which
samples SCENE at u = x + 0.75 (UV 0.5 .. W + 0.5 over corners at -0.25):
DISPLAY x = 0.75 SCENE x + 0.25 SCENE x+1 (with the vertical halving). That
bias is not mirror-symmetric: with the mirror on a UI edge pixel takes its
quarter from the neighbour on the other side. So in the game the UI's
pixels are where they were, and a glyph edge's 25 % blend sits on its
other side; the world, which is not pre-flipped, is the exact flip.

**Space tags (GifPacket.c).** Until R7c the decoder tagged raw register
writes (and the raw-coordinate helpers, `GIF_SP_AUTO`) UI in lists 11 and
12, WORLD elsewhere. UI and WORLD replayed alike (both `g_space` slots
carry the same wide x scale), so the tag only mattered for the mirror.
Since R7c list 11's raw writes are WORLD: what draws raw there is
world-projected (`waterDot.c`'s drops, `weapon.c`'s insect net,
`Light.c`'s light volumes, the debug lines of `fieldCollision.c`,
`camera-editor.c`, `motionManager2.c`, `spider.c`, `icoMisc.c`'s wind
lines) and would have been flipped away from their objects; list 12's raw
writers are 2D (`debug.c`'s font and bars, `icoMisc.c`'s memory bar) and
stay UI. The UI sources come through the UI helpers
(`gif_Sprite*`, `gif_Point*`, `gif_Line*`: `layout_texture.c`,
`layout_action.c`'s progress bar, `jimaku.c`'s subtitles, `kanban.c`'s
signs, `debug.c`) or `rd_ScreenPrims` with `RD_SPACE_UI` (`DisplayFont.c`,
which `staffroll.c` draws through, and the port's text and popups,
`port/ui/font.c`): no change there. `layout_texture.c`'s primary sprite is
FULLSCREEN (R7a) and untextured: flip-invariant. `rd_water.c`'s prewarm
lists waterDot's state in WORLD space.

**FMV.** `rd_video.c` draws the film's rectangle mirrored when the mirror
is on (`rd__MirrorOn`) and both switches are: the player's `[game]
mirror_fmv` (default true; `rd_VideoSetMirrorOption`, set from the config
when the Settings menu installs and by its Gameplay row) and `movie.c`'s
per-movie `rd_VideoSetMirror` (on unless the developer environment
variable `ICO_MIRROR_FMV` is `0`; `movie.c` is not this package's file, so
the variable stays as a developer override).

**Game run** (window build on lavapipe, `SDL_VIDEODRIVER=offscreen`,
`port/input/pad-boot-mirror.txt`, `ticks=1000`, `dump_every=50`,
`build-host/r7c-run-window`): the trace has gflag 382 at tick 621 and the
log "rd: mirror mode on (frame 622)"; the stages follow at 622 (41), 715,
795, 851, 915 (40) and 995 (3), the run ending at 1000 before a stage 3
dump. The dumps, replayed with `rd_replay_tool --present 960x720` (and
`--mirror` from 650 on): the boot signs and the title (50 to 550) are
unmirrored (the flag is off until the choice); 600 is the Mirror mode
screen over the title stage, the cursor on Off before the RIGHT at 608;
650 to 950 are the opening's fades and black frames (the letterbox bars
symmetric). The mirrored world was looked at on earlier dumps instead:
R7b's title frames (500, 600) and stage 3 (1200), 6C's stage 3 (1400):
the castle, the cell and Ico flipped, the copyright line and the
vibration screen's text reading normally, the ICO logo (a stage
animation, part of the world) flipped.

Open items:

1. The reduction's quarter-pixel bias (above): UI glyph edges blend with
   the neighbour on the other side with the mirror on.
2. Gouraud colours and Z of flipped UI triangles are not moved by the
   sixteenth of a pixel (above).
3. `debug.c`'s font drawn in list 11 (`debug_PrintFont`,
   `debug_FlushFontWindow`, `debug_brainBar`) and `Texture.c`'s CLUT and
   texture viewers are now WORLD: in developer mode with the mirror on
   they read mirrored. The list 12 debug font stays UI.
4. A subtitle and the staff roll were not seen mirrored in a game run (no
   dump has them); they are UI helpers and `DisplayFont.c` sprites, the
   kinds `rd_mirror_test` covers.
5. During a film the audio pan follows the mirror mode, not
   `mirror_fmv`: with the mirror on and `mirror_fmv` off the picture is
   unmirrored and its stereo swapped (the film's PCM goes through the
   SPU2 like the rest; `port/audio` has no view of the movie state).
