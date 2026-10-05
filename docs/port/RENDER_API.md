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
| Alpha scale 0x80 = 1.0, alpha up to 0xFF | Dual-source blend. Output 0 carries colour and the stored GS alpha; output 1 carries the blend factor (As/128 or FIX/128). `RHI_BF_SRC1_*`. Required on every backend (Vulkan `dualSrcBlend`, D3D12 and Metal always). |
| Blend equations with Cd − Cs·X | `RHI_BO_REVERSE_SUBTRACT` with the dual-source factor. |
| Ad factors (modes 8 to 10) | `RHI_BF_DST_ALPHA` scaled: the shader cannot see Ad, so the dst alpha is stored as GS alpha and the pipeline uses DST_ALPHA with a 2× constant folded into the fragment's colour (Ad/128 = 2·Ad_unorm). Verified in the lightning scene when a stage uses it. |
| AFAIL FB_ONLY (0x5140D) | Two draws: alpha > ref with depth write, then alpha ≤ ref with depth write off (`RdPipelineKey.afailSplit`). Self-overlap order within a strip can differ; accepted, see DIVERGENCES.md when observed. |
| AFAIL RGB_ONLY with ATST NEVER (0x3F001, 0x33001) | Colour mask RGB, depth write off, no alpha test. |
| DATE | R8 snapshot of the scene alpha MSB (`RD_TARGET_DATE_SNAPSHOT`) taken when a DATE-consuming list starts (list 4 head, shadow composite, aura); fragment shader discards on the test. D3D12 cannot read the bound target and stencil export is not universal, so no feedback loops. Fullscreen consumers ping-pong. |
| COLCLAMP 0 (shadow count) | Stencil increment/decrement wrap on the scene depth-stencil; `RD_POST_SHADOW_RESOLVE` writes stencil ≠ 0 into SHADOW0, then the original 256/128/64 blur chain runs. |
| FBA | Fragment shader forces alpha MSB; per material. |
| PABE | Fragment shader: blend factor 0 when As MSB clear (dual-source output 1 = 0 and output 0 alpha path unchanged). |
| Z | Reversed-Z D32F with GEQUAL. GS Z after the game's projection is affine in 1/w (`gsb_SetVSMatrixSub`), so GS Z maps linearly: `z_ndc = 1 − gsZ / 2^24` for PSMZ24-scaled values (the game's far 0xFFFFFF becomes 0). One function converts sprite Z constants (DoF planes, fog far plane). |
| ZTE = 0 | Treated as Z ALWAYS with write enabled (GS manual: prohibited setting; one site writes TEST 0). |
| Texture function | Fragment shader integer path in Original: `min((tex·col) >> 7, 255)`; float in Enhanced. |
| Vertex colour | Truncated as VU `ftoi` and clamped at 255. |
| Feedback passes | Exact GS integer blend in the shader on `RHI_FMT_RGBA8_UINT` ping-pong targets (`RdPostParams.exactInt`); see section 7. |

Pipeline key: `RdPipelineKey` (program, blend or none, alpha test, AFAIL
split pass, DATE, Z test, Z write, PABE, FBA, colour mask, stencil mode,
target format, topology). AREF, FIX, sampler state and UV offset are
uniforms or sampler objects, not pipeline state. Expect under 100 keys;
`RD_PIPELINE_CACHE_MAX` is 256 and `rd_core` asserts above it.

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

## 9. What the first pixels need

RHI (Vulkan), `rd_core` lists and state, `RD_PROG_SCREEN` and `RD_PROG_POST`
shaders, `GifPacket.c` reimplemented on `rd_*`, TIM2 decode to
`rd_CreateTexture`, and the frame lifecycle in `gsb_UpdateGSSystem`. That
is the title screen, menus and debug font without any mesh or VU shader
work.
