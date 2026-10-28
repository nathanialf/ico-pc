# The four backends as the renderer sees them

The renderer (rd) draws through one RHI and branches on the backend only where a backend's
limits or its copies differ. This document lists those places for Vulkan, Direct3D 12, OpenGL and
Direct3D 11: what each backend reports in `RhiLimits`, the policy the renderer takes from it,
the device profile's rows for the OpenGL and Direct3D 11 classes, the window each backend needs,
the order the window tries them in, and what has not been run. How each backend works inside is
[RHI.md](RHI.md) ([Vulkan](RHI.md#vulkan), [Direct3D 12](RHI.md#direct3d-12),
[OpenGL](RHI.md#opengl), [Direct3D 11](RHI.md#direct3d-11)); the device profile of every class is
[RENDER.md](RENDER.md#the-device-profile).

## Where the renderer branches on the backend

`rhi_backend()` (`RhiBackendKind`, `port/rhi/rhi.h`) is read in these places outside `port/rhi`;
everything else follows `RhiLimits`.

| place | Vulkan | Direct3D 12 | OpenGL | Direct3D 11 |
| --- | --- | --- | --- | --- |
| `rd__make_shader` (`port/render/rd_replay.c`) | the SPIR-V | the DXIL | the GLSL text (deflated; an entry without one is missing) | the embedded HLSL, compiled by the backend |
| optional modules (`optionalBuilt`, `rd_replay.c`) | every one the table has | every one the table has | only those with GLSL | every one the table has |
| `classOf` (`port/render/rd_profile.c`) | by vendor, tiler and MoltenVK | `PC_D3D12` | `PC_OPENGL` | `PC_D3D11` |
| `rd__blend_exact_name` (`port/render/rd_dst_read.c`) | `exact (interlock)` | `exact (rov)` | `exact (interlock)` | `exact (rov)` |
| fog buffer path, probe and edge probe depth (`depthRegionCopies`, `port/render/rd_fog_path.c`) | yes | no (a depth only whole) | yes (`glReadPixels` of the depth aspect) | no (a texture only into a readback buffer) |
| fog local path (`rd__fog_local_offered`) | where local read is offered | no | no | no |
| pipeline failure text (`failReason`, `port/render/rd_pipeline.c`) | `VkResult <n> (<name>)` | `HRESULT 0x<8 digits>` | `GL error 0x<4 digits>` | `HRESULT 0x<8 digits>` |
| VU pre-pass off line (`rd__vu_prepass_init`, `port/render/rd_replay_vu.c`) | the tiler and Apple A9/A10 rules | on | on | off: `on Direct3D 11 (the backend holds the colour targets to 32 bytes a pixel, the record 48: its copies are not in that backend)` |
| window flags (`window_flags`, `port/platform/window_host.c`) | `SDL_WINDOW_VULKAN` | none (the HWND) | `SDL_WINDOW_OPENGL` after `window_gl_attributes` | none (the HWND) |
| robust access in the summary and the profile line | `RhiLimits.robustText` (`off (the default; ...)`) | `D3D12's own` | `off (OpenGL)` (`robustText`) | `D3D11's own` |

The swapchain format is read from `rhi_swapchain_format` wherever it matters (`rd_present.c`,
`rd_video.c`): Direct3D 12 and Direct3D 11 report BGRA8, OpenGL RGBA8, Vulkan the surface's
format it picked (`vk_swapchain.c`). The start-up set already warms the
output keys for both formats (`rd__enumerate_reachable`, `port/render/rd_pipeline.c`), so nothing
in the renderer assumes BGRA.

## Limits and the policies they decide

| `RhiLimits` field | Vulkan | Direct3D 12 | OpenGL | Direct3D 11 | the renderer's policy |
| --- | --- | --- | --- | --- | --- |
| `tiler` | by vendor | false | false | false | the tile-based profile rows; MSAA up to 4 samples |
| `localRead` | where offered | false | false | false | fog local, DATE stencil, fx local read |
| `fragmentInterlock` / `rov` | interlock where offered | ROV where offered | interlock with `ARB_fragment_shader_interlock` | neither | the exact storage blend (`[dev] blend = storage`) offered |
| `rasterOrderAttachment` | where offered | false | false | false | the exact attachment blend offered |
| `dualSourceBlend` | where offered | true | `GL_MAX_DUAL_SOURCE_DRAW_BUFFERS` >= 1 | true | the one-pass blend, else the `_nodual` twins |
| `uintView` | true | false | true (`glTextureView`) | false | `uintViewWork`, `uintViewScene` |
| `stencilView` (`stencilViewChannel`) | where the depth-stencil is sampled: a `VK_IMAGE_ASPECT_STENCIL_BIT` view (red) | true: an SRV of plane 1, `X32_TYPELESS_G8X24_UINT` (green) | true: a `glTextureView` in `GL_STENCIL_INDEX` mode (red) | true: an SRV of `X32_TYPELESS_G8X24_UINT` (green) | `shadowOne`: the shadow count in one draw |
| `dynamicDepth`, `dynamicBlend` | where offered | false | true | false | dynamic state (`[dev] dynamic_state`) |
| `colorBytesPerPixel` | 0 (MoltenVK: the GPU's) | 0 | 0 | 32 | the VU vertex pre-pass needs 48 or 0 (no bound) |
| `msaaCounts` | the device's | the device's | `glGetInternalformativ` of RGBA8, RGBA8UI, D32F_S8 | `CheckMultisampleQualityLevels` | the MSAA values offered, with `depth_resolve_ps` made |
| `sampleRateShading` | where offered | true | true | true | the per-sample fog (`fog_lut_ms_ps`) |
| `robustDefaultOff` | where robust access is off by default | false | false | false | the loss order's `robust` step |

`depth_resolve_ps` is GLSL 4.50 (`textureSamples`); an OpenGL 4.3 or 4.4 driver refuses it. The
start then goes on (`rd__gpu_init` does not count it as required), logs `rd: msaa:
depth_resolve_ps was not made on this device (the line above says why): no MSAA value is offered,
the supersampling ones are`, and `rd_aa_supported_mask` leaves out every MSAA value
(`port/render/rd_core.c`), so the MSAA epoch never opens and a saved MSAA setting draws as Off
(`rd__aa_requested`). The settings menu shows only the values in the mask.

On OpenGL the table has no GLSL for the input-attachment builds (`fog_lut_local_ps`,
`date_stencil_ps`, `fx_local_ps` and the `_dstatt` twins, [SHADERS.md](SHADERS.md)); the first
three are optional modules and are left out as if missing from the table, and the `_dstatt` twins
are made only for the attachment blend, which OpenGL does not offer.

Vulkan's dynamic blend on RADV (Mesa 25.2.8 and 25.3-devel) and RDNA3 (Radeon RX 7000): the driver takes a SRC1
factor in the dynamic blend equation as dual-source blending even on a draw with blending off, and
on these cards the pixel shader then pairs neighbouring pixels' outputs, so a one-output draw after
a dual-source one loses every other column (issues 146 and 154). The Vulkan backend therefore sends
the equation on every change, blending on or off, on every driver; `RhiStats.dualLeftovers` counts
the SRC1 equations replaced on draws without blending (one a run of such draws) ([RHI.md](RHI.md#dynamic-state)). OpenGL (radeonsi
was clean for reporter 154) already sent the blend function on every change; Direct3D 11 and 12
have static blend state only.

## The device profile on OpenGL and Direct3D 11

Both classes take the desktop column whatever the vendor; what differs from desktop Vulkan comes
from the limits above. [RENDER.md](RENDER.md#the-device-profile) has the other classes.

| flag | OpenGL | Direct3D 11 |
| --- | --- | --- |
| blend | fixed-function; `[dev] blend = storage` opts into the interlock where the driver has it | fixed-function (no ROV yet) |
| `fogLocal`, `dateStencil`, `fxDstLocal` | off | off |
| `storePlan` | off | off |
| `blurFold` | on | on |
| robust access | `off (OpenGL)` | `D3D11's own` |
| `localRead` | `no` | `n/a` |
| `uintViewWork`, `uintViewScene` | on | off (`uintView` false) |
| `carefulSet` | off | off |
| the record path (VU pre-pass) | on | off (`colorBytesPerPixel` 32) |
| dynamic state | on | none |
| `alphaPlan`, `fxPlan`, `vuPlanesFrac` | on | on |
| `crackFill` | on | on |
| `shadowOne` | on (`stencilView`) | on (`stencilView`) |

The class name carries the vendor where the backend knows it and the backend's name:

```
rd: profile: immediate-mode GPU (OpenGL): blend fixed-function; fog local off; DATE snapshot; depth stores kept; blur fold on; robust access off (OpenGL); local read no; uint views work and scene; alpha plan on; fx plan on; fx local read off; VU planes whole scales only; shadow count one draw; pipelines the start-up set
rd: profile: immediate-mode GPU (NVIDIA, D3D11): blend fixed-function; fog local off; DATE snapshot; depth stores kept; blur fold on; robust access D3D11's own; local read n/a; uint views off; alpha plan on; fx plan on; fx local read off; VU planes whole scales only; shadow count one draw; pipelines the start-up set
```

The stencil views of the four backends (`RHI_ASPECT_STENCIL`, [RHI.md](RHI.md)) are what the
shadow count's one draw reads ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-count-in-one-draw));
Vulkan's (on lavapipe) and OpenGL's (on llvmpipe, the `_gl` twins) run in the suite, Direct3D 11's
in the WARP job; Direct3D 12's has not run on a device. Every one is checked at start by the
count's self-test, which falls back to the seven draws on a wrong byte.

OpenGL's vendor comes from `GL_VENDOR` and `GL_RENDERER` (`gl_vendor_id`: NVIDIA, AMD, Intel;
llvmpipe names none), Direct3D 11's from the adapter. The `ico_pc: summary:` line names the
backend (`rhi_backend_kind_name`: `OpenGL`, `D3D11`) and carries the same class in its profile
words.

Pipeline cache: OpenGL keeps none (`rhi_pipeline_cache_loaded` false, size 0), so the crash
order's `cache` step never applies there. Direct3D 11 keeps its compiled shaders in a folder,
`pipelines-<build>.d3d11` beside where the Vulkan cache file would be, and reports it as loaded
when a start had hits ([RHI.md](RHI.md#direct3d-11)).

## What Direct3D 11 costs the CPU a picture

Direct3D 11 is the one backend whose GPU does not read the renderer's upload ring in place: each
submit copies the ranges its list reads from the ring's CPU copy into up to three D3D11 buffers
(vertex and index, constant, structured), and every bind group is set register by register on a
deferred context. On issue 126's GTX 750 Ti and two-core i3 the submit phase took about 6 ms of an
11 ms CPU picture (`window: N replays: CPU ... submit 6.1`). Three mechanisms keep that down without
changing a byte the GPU reads ([RHI.md](RHI.md#direct3d-11) has them in full):

- **The written mark.** The renderer's ring tells the backend where its writes end
  (`rhi_buffer_note_written`, called from `rd__ring_alloc`; a no-op on Vulkan, Direct3D 12 and
  OpenGL), and a submit writes no byte past it. Without the mark the VU draws' storage view over
  the whole ring makes every submit copy the ring's whole capacity (about 11 MB there) while a
  picture writes about 1.3 MB (the log's `1270 KB uploaded` a replay).
- **Two constant buffer strategies.** A device with partial constant buffer updates (issue 126's
  card, WARP) writes the uniform windows' span with `UpdateSubresource1` and a box. One without
  them maps a `DYNAMIC` constant buffer and writes the range read (`DISCARD` at a frame's first
  map, then `NO_OVERWRITE` where the device allows it on constant buffers) instead of writing the
  whole buffer at every submit; the device line names which.
- **Fewer binding calls.** An SRV or sampler register already holding the view or sampler is not
  set again, per stage, as with constant buffer windows; and a changed window on the same
  constant buffer is one call instead of a NULL bind plus the call, except where the runtime
  emulates command lists (Microsoft's workaround is for the emulation only).

The bytes a submit copies are in the window's `uploaded KB per replay` line on Direct3D 11
([DISPLAY.md](DISPLAY.md#the-stats-lines)). None of this has run on hardware; CI's WARP job with the
debug layer is the gate, and `rhi_d3d11_test` checks the mark's byte counts and runs the cells on
the mapped constant buffers (`ICO_D3D11_FAKE_NO_PARTIAL_CB=1`).

## What the bind diet cuts (OpenGL and Direct3D 11)

On issue 126's i3-2120 the OpenGL walk of the bind groups took about 8 ms a picture (`group
entries 8459 a timed replay, 5953 already bound`). Three cuts, none changing a byte the GPU reads:

- **The VU draws' DrawCB goes through the content cache.** It was copied into the ring for every
  planned pass; it now takes the offset of `rd__draw_cb_offset` (rd_replay.c, the same cache the 2D
  draws' `rd__draw_group` uses, keyed by the block's bytes), so equal blocks across draws and
  passes share one copy and b1's dynamic offset repeats, which every backend's "already bound"
  check skips. The pre-pass batch draws' DrawCBs (`aux0` carries the batch table index) keep their
  own copies. The window's `uniform blocks` KB drops by the repeats and nothing else changes.
- **OpenGL: one `glBindBuffersRange` for a run of uniform points** (`GL_ARB_multi_bind`, core in
  4.4; the limits line says `multi bind yes|no`): the group's uniforms that need a bind and sit at
  consecutive points (the VU group's b1..b3) are one call. Without it, one `glBindBufferRange`
  each as before.
- **OpenGL: a slot's group is not walked again** when it holds the same group id and offsets as the
  last walk of that slot (set to another group and back between two draws): the shadow is cleared
  with the per-point shadows (`gl_forget_units`, at every new list), and a slot's shadow is dropped
  when another slot's walk changes a point it holds. The skipped group's entries count in `group
  entries N` as already bound, so N is unchanged and M rises; N minus M is what is still bound.

Direct3D 11 needed nothing more: `d11_set_cb` skips a window already bound for the stage (both
stages are checked apart), so a repeating DrawCB offset costs no call, and the NULL-plus-set pair
happens only where the runtime emulates command lists (above).

## The window and the order the backends are tried in

`[video] backend` (`backend=` in `ico-pc.ini`, the Options > Graphics row) names `vulkan`,
`d3d12`, `opengl` or `d3d11` (`rhi_create_backend` also takes `vk` and `gl`); a name the build
does not link logs `window: renderer backend "<name>" is not in this build; using vulkan`. Empty
is Automatic. `window: Graphics API <name> (chosen in the settings or the config file|automatic)`
says which.

1. The chosen backend, or Vulkan for Automatic.
2. Automatic, the backend did not start (SDL refused the `SDL_WINDOW_VULKAN` window, as without a
   Vulkan loader, after `window: SDL_CreateWindow: <error>`; or `rd_init` failed after
   `window: no usable vulkan device ...`): the next backend the build links is tried, once each
   (`window_fallback_backend`): Direct3D 12 first where it is linked (Windows: the tested one),
   then OpenGL. `ico_pc: no <failed> device; using <next>`, the window destroyed and made again
   with the flags that backend needs (`SDL_WINDOW_OPENGL` for OpenGL),
   `window: Graphics API <next> (automatic, after <failed>)`, `rd_init` once more. When the last
   one does not start either, the start ends as any failed start does. A backend this start
   already tried is skipped (`window_fallback_tried`): after a restart whose Direct3D 12 did not
   start and went on with Vulkan, a Vulkan that fails too goes on to OpenGL.
3. A restart after a crash or a loss follows the start-up retry orders
   ([PLATFORM.md](PLATFORM.md#diagnostics)): their backend steps go between Vulkan and
   Direct3D 12 only; a run on OpenGL or Direct3D 11 takes the same-backend steps, and the
   `d3d12`/`backend` steps to Direct3D 12 where the build has it. A `d3d12` restart whose
   Direct3D 12 does not start goes on with Vulkan, and then with OpenGL as in 2.

The OpenGL window gets a double-buffered pixel format without depth, stencil or sRGB and the
4.3 core context attributes before `SDL_CreateWindow` (`window_gl_attributes`), since WGL and GLX
pick the pixel format with the window; `gl_device.c` sets the context attributes again (with its
debug flag) and refuses a window made without `SDL_WINDOW_OPENGL`. Android and iOS link Vulkan only.

4. During the run, Options > Graphics > Apply switches to the row's backend in the process: the
   renderer is suspended, the window made again with the new backend's flags (`window_create`,
   `window_attach`, the start's own) and the renderer resumed; a backend that does not start
   brings back the one that ran. A switch is not a fallback: nothing else is tried. The steps,
   refusals and log lines are in [PLATFORM.md](PLATFORM.md#the-renderer-switched-from-the-menu).

## Tests

| test | Vulkan | OpenGL | Direct3D |
| --- | --- | --- | --- |
| `rd_video_window` / `rd_video_window_gl` | the out-of-date and poll rebuilds counted | the same sizes, no counts (the present follows the window) | 77 |
| `rd_present` scene fallback checks | run (`vkr_test_fail_texels_above`) | skipped | skipped |
| `rd_fog` edge probe (`checkEdgeProbe`) | run | run | only on the buffer path (never) |
| `rd_msaa` | the mask with and without `depth_resolve_ps` | the same | not run (Windows) |
| `rd_vuprepass_selftest*` | the profile names `Vulkan` | names `OpenGL`, desktop column | not run (Windows) |

The OpenGL runs of the renderer's suite are the `_gl` tests ([TESTING.md](TESTING.md)).

## Not verified

- Every OpenGL path above on a real driver: only llvmpipe (Mesa, OpenGL 4.5 core, no fragment
  shader interlock) has run the backend tests. The fog buffer path and the probes' depth copies
  on OpenGL have not been run by the renderer's tests yet.
- The fill rule on an OpenGL driver other than Mesa's: OpenGL leaves the rule for an edge through
  pixel centres to the driver; the lower-left clip origin with the vertex y negated gives Mesa's
  drivers the top-left rule the renderer's sprites need ([RHI.md](RHI.md#opengl), "Clip space"),
  and the common RHI cells' fill-rule cell is the check to run on another driver.
- The OpenGL fallback in the window: not started in the container (the game is not run there).
- The switch from the menu (Apply): no switch between any two backends has run.
- An OpenGL driver below 4.5 (no MSAA values) has not been met.
- The equation fix on a Radeon RX 7000 card with Mesa 25 RADV: the cause is read from Mesa's
  source; lavapipe, NVIDIA and Intel have not shown the fault, so only the counters and
  the colours at an even and an odd column are checked here.
- Direct3D 11: nothing has run on Windows; the profile text, the pre-pass line and the robust
  words are from the limits `d3d11_device.c` reports.
