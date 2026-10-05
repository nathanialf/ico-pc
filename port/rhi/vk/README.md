# port/rhi/vk: the Vulkan backend

Implements [`port/rhi/rhi.h`](../rhi.h) on Vulkan 1.2 or later. Library
`ico_rhi_vk` (`port/rhi/CMakeLists.txt`, option `ICO_RHI_VULKAN`, default ON).
Dependencies come from `tools/fetch_deps.sh` (versions and licences in
[`docs/port/THIRD_PARTY.md`](../../../docs/port/THIRD_PARTY.md)).

| file | contents |
| --- | --- |
| `vk_internal.h` | backend state (`g_vkr`), object records, handle pools |
| `vk_enums.h` | every `rhi.h` enumeration mapped to Vulkan, as tables indexed by the RHI value |
| `vk_device.c` | loader, instance, validation layer, device selection, limits, `rhi_Init`/`rhi_Shutdown` |
| `vk_resource.c` | buffers, textures, samplers, shaders, deferred destruction |
| `vk_pipeline.c` | bind group layouts, transient bind groups, pipelines |
| `vk_cmd.c` | frames, command lists, submission, barriers, passes, draws, copies, readback |
| `vk_swapchain.c` | SDL3 surface swapchain, acquire, present, resize |
| `rhi_vk.h` | test-only extras (`rhi_vk_ValidationErrorCount`); `port/render` never includes it |

## Device

- Loader: volk. Headless, `volkInitialize()` opens `libvulkan.so.1` or
  `vulkan-1.dll`; with a window, the backend takes SDL's
  `vkGetInstanceProcAddr` (`SDL_Vulkan_GetVkGetInstanceProcAddr`) so SDL and
  the backend share one loader. Nothing links against a Vulkan import
  library, so the mingw builds need no `vulkan-1.a`.
- Instance: API 1.3 requested; SDL's surface extensions when there is a
  window; `VK_LAYER_KHRONOS_validation` and `VK_EXT_debug_utils` when
  validation is on and present. Validation is on when
  `RhiDeviceDesc.debugLayers` is set, always in `CMAKE_BUILD_TYPE=Debug`
  builds, and `ICO_VK_VALIDATION=0/1` overrides both. Errors are counted
  (`rhi_vk_ValidationErrorCount`) and printed.
- Device selection: requirements are Vulkan 1.2, dynamic rendering (core 1.3
  or `VK_KHR_dynamic_rendering`), `dualSrcBlend`, timeline semaphores, and
  optimal-tiling support for RGBA8 (blendable), RGBA8_UINT, R8, D32F and
  D32F_S8; with a window also present support and `VK_KHR_swapchain`.
  Scores: discrete 1000, integrated 500, virtual 200, CPU (lavapipe) 100,
  +10 for 1.3. `ICO_VK_DEVICE=<index or name substring>` forces a device.
- Limits: `uniformAlign` = max(min uniform, min storage offset alignment,
  16); `copyRowPitchAlign` 1, `copyOffsetAlign` 4.

## Handles

Every handle id is `(generation << 20) | (index + 1)` in a fixed-capacity
pool per type (4096 buffers, 8192 textures, 256 samplers, 512 shaders, 64
layouts, 1024 pipelines). Freeing bumps the slot's generation, so a stale
handle fails the lookup and the call is a no-op. Command lists and bind
groups are per-frame: their id carries the frame index (low 12 bits) in
place of the generation and is rejected in any other frame.

## Memory

One `VkDeviceMemory` per buffer and per texture. With the game's numbers
(about 15 targets, a few ring buffers, a few hundred to low thousands of
textures) this stays under `maxMemoryAllocationCount` (4096 on most
drivers); a sub-allocator is the change to make if the texture cache grows
past that.

- `RHI_MEM_DEVICE`: device-local; always gets `TRANSFER_DST` usage, since
  `rhi_CmdCopyBuffer` is the only way to fill it.
- `RHI_MEM_UPLOAD`: host-visible and host-coherent (device-local too when
  the driver offers it: resizable BAR, UMA), persistently mapped at
  creation. Writes need no flush.
- `RHI_MEM_READBACK`: host-visible, cached preferred; persistently mapped;
  `rhi_MapBuffer` invalidates when the type is not coherent.

Textures are optimal-tiling images with one view over all aspects (used for
attachments and colour sampling) and, for D32F_S8 textures that are
sampled, a depth-only view (`RHI_ASPECT_DEPTH` bindings). Copy-only textures
have no view. Every texture gets `TRANSFER_SRC` so `rhi_ReadbackTexture`
works on any of them.

## Descriptor scheme

A bind group layout is a `VkDescriptorSetLayout`; group *n* is descriptor
set *n* in the pipeline layout built from `RhiPipelineDesc.layouts`.

HLSL's b, t and s registers share a number in D3D12 but Vulkan has one
binding space per set, so the binding number is the RHI slot plus a shift
per register class (`vkr_bindTypeMap`):

| RHI bind type | HLSL register | Vulkan descriptor | binding |
| --- | --- | --- | --- |
| `RHI_BIND_UNIFORM_BUFFER` | `bN` | `UNIFORM_BUFFER` | N |
| `RHI_BIND_STORAGE_BUFFER` | `tN` | `STORAGE_BUFFER` | 16 + N |
| `RHI_BIND_SAMPLED_TEXTURE` | `tN` | `SAMPLED_IMAGE` | 16 + N |
| `RHI_BIND_SAMPLER` | `sN` | `SAMPLER` | 32 + N |

The shader toolchain must compile SPIR-V with

    dxc -spirv -fspv-target-env=vulkan1.2 \
        -fvk-b-shift 0 all -fvk-t-shift 16 all -fvk-s-shift 32 all -fvk-u-shift 48 all

and declare every resource with its group as the register space
(`register(b0, space0)`, `register(b1, space1)`, `register(t0, space1)`,
`register(t1, space2)`, `register(s1, space2)`), since DXC maps the space
to the descriptor set. Dual-source outputs need
`[[vk::location(0), vk::index(1)]]` on `SV_Target1`
(`port/rhi/test/shaders/rhi_test.hlsl` shows all of this). Separate
textures and samplers, no combined image samplers, so the HLSL is the same
for DXIL.

Bind groups are transient (rhi.h: valid for the current frame). Each frame
slot owns a chain of descriptor pools (8192 sets, 8192 uniform buffers,
2048 storage buffers, 8192 sampled images, 8192 samplers each; up to 16
pools); `rhi_CreateBindGroup` allocates from the current pool, opens the
next on exhaustion, and writes the descriptors at once. The whole chain is
reset when the slot is reused. No push descriptors, no descriptor indexing,
no dynamic offsets: one set per group per draw, written once.

`rhi_CmdSetBindGroup` records the set; the bind happens at the next draw
against the bound pipeline's layout, and a pipeline with a different layout
rebinds every group.

## Pipelines

Graphics pipelines with dynamic rendering (`VkPipelineRenderingCreateInfo`;
no `VkRenderPass` objects). Dynamic state: viewport, scissor, stencil
reference, blend constants. Cull mode none. Blending on an integer target
(RGBA8_UINT and the other UINT formats) is dropped with a log line: Vulkan
forbids it and the exact-blend path does the arithmetic in the shader. A
pipeline with depth format D32F_S8 also declares it as the stencil format.
No pipeline cache yet (`rd_core` keeps under a hundred pipelines).

## States and barriers

`vkr_stateMap` maps each `RhiState` to a layout, stage mask and access mask;
`rhi_CmdBarrier` emits one image barrier from the `before` row to the
`after` row over all mips and aspects.

| RhiState | layout |
| --- | --- |
| UNDEFINED | `UNDEFINED` (contents discarded) |
| RENDER_TARGET | `COLOR_ATTACHMENT_OPTIMAL` |
| DEPTH_WRITE | `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` |
| DEPTH_READ | `DEPTH_STENCIL_READ_ONLY_OPTIMAL` |
| SHADER_READ | `SHADER_READ_ONLY_OPTIMAL`; depth formats `DEPTH_STENCIL_READ_ONLY_OPTIMAL` |
| COPY_SRC / COPY_DST | `TRANSFER_SRC_OPTIMAL` / `TRANSFER_DST_OPTIMAL` |
| PRESENT | `PRESENT_SRC_KHR` |

Depth textures use one read-only layout for both SHADER_READ and
DEPTH_READ, so a bind group's descriptor layout does not depend on which
state the caller chose. DEPTH_READ makes stencil read-only too; the
shadow-volume pass (depth tested, not written, stencil counted) uses
DEPTH_WRITE with `depthWrite = false`. A render pass's depth attachment is
in DEPTH_READ when `readOnlyDepth` is set, DEPTH_WRITE otherwise; colour
attachments are in RENDER_TARGET.

The backend does not track states, but it orders writes within one state,
which the caller cannot express with a transition (rhi.h, "commands"): a
global memory barrier precedes every copy (transfer writes) and every
render pass (attachment writes), and `rhi_CmdCopyBuffer` is followed by a
barrier that makes the range visible to vertex, index, uniform, storage and
copy reads. Every command list ends with a transfer-to-host barrier for
READBACK buffers.

The viewport is set with a negative height (core since 1.1), so clip space
is D3D's (y up) and the same HLSL runs on both backends without
`-fvk-invert-y`. Clip-space z is 0..1 on both.

Integer colour targets are cleared with the clear colour's values taken as
integers (`clear[0] = 7.0f` writes 7, rounded to nearest, negatives to 0).
D3D12's `ClearRenderTargetView` on a UINT format is expected to behave the
same; the D3D12 backend should confirm it with the same test.

## Frame lifecycle

    rhi_WaitFrame                 frame N begins: slot N % 2 is waited and recycled
      write the rings (UPLOAD buffers) for frame N
      rhi_CreateBindGroup ...     transient, from slot N % 2's pools
      rhi_AcquireBackbuffer       optional; signals the slot's acquire semaphore
      rhi_BeginCommands ... rhi_Submit (one or more lists)
      rhi_Present                 optional
    rhi_WaitFrame                 frame N+1 ...

- One timeline semaphore orders everything. Each `vkQueueSubmit` signals
  the next value; the frame slot remembers the last value of its frame.
- `rhi_WaitFrame` advances the frame index, waits for the timeline value
  of the frame that last used the slot (`RHI_FRAMES_IN_FLIGHT` = 2 frames
  ago), then destroys the slot's deferred garbage, resets its command pool
  (8 command lists per frame) and its descriptor pools.
- `rhi_Destroy*` releases the handle at once and queues the Vulkan objects
  on the current slot; they are destroyed when the slot is next recycled,
  after the GPU finished every frame that could reference them.
  `rhi_WaitIdle` destroys the other slots' garbage (the current frame's
  stays: a list recorded but not submitted may still use it).
- Swapchain: the first submit after an acquire waits on the acquire
  semaphore (all stages). `rhi_Present` submits an empty batch that waits
  for the latest timeline value and signals the image's render-done binary
  semaphore (one per swapchain image), then presents. `OUT_OF_DATE` on
  acquire returns a null texture; `OUT_OF_DATE`/`SUBOPTIMAL` on present
  recreates the swapchain at the window's pixel size. Present mode: FIFO
  with vsync, else MAILBOX, IMMEDIATE, FIFO in that order. Format: BGRA8
  UNORM sRGB-nonlinear, else RGBA8 UNORM (reported by
  `rhi_SwapchainFormat`). Images have colour-attachment, transfer-dst and
  transfer-src usage.
- `rhi_ReadbackTexture` records a one-shot copy into a temporary READBACK
  buffer, submits it after all earlier work, and waits for its timeline
  value.

## Tests (`port/rhi/test/`)

| test | what |
| --- | --- |
| `rhi_vk_enum` | every RHI enumerator has a Vulkan mapping (static asserts on table sizes against the `*_COUNT` sentinels; a run over the `set` flags; spot checks of the GS-critical values) |
| `rhi_vk` | headless rendering checked texel by texel: dual-source blend (SRC1_COLOR, SRC1_ALPHA), stencil DECR_WRAP and INCR_WRAP, reversed-Z GEQUAL with exact depth readback, colour masks, an RGBA8_UINT target (integer clear and output), texture upload and sampling (indexed draw), R8 texture copies, a device buffer filled by `rhi_CmdCopyBuffer`; three frames; fails on any validation error |
| `rhi_vk_swapchain` | the window path through SDL's offscreen video driver (`VK_EXT_headless_surface`): six frames of acquire, clear, readback, present, with a resize |

In the container they run on Mesa lavapipe with the Khronos validation layer
and synchronisation validation (`VK_ADD_LAYER_PATH` points at the copy
`tools/fetch_deps.sh` unpacks). Exit 77 (skipped) without a Vulkan device.
The test shaders are `test/shaders/rhi_test.hlsl`, compiled by
`test/shaders/gen_shaders.sh` into the committed `rhi_test_spv.h`.

## What the D3D12 and Metal backends must mirror

- The `rhi.h` contracts, including the ones this backend made explicit:
  `rhi_WaitFrame` is the frame boundary; transient bind groups and command
  lists die at the next `rhi_WaitFrame` that reuses their slot; same-state
  writes are ordered by the backend; buffer copies are visible to all later
  reads without a barrier; `rhi_ReadbackTexture` takes a texture already in
  COPY_SRC and returns raw texels, mip 0, tightly packed; an acquired
  backbuffer starts UNDEFINED and the caller moves it to PRESENT;
  integer-target clears take the clear values as integers; DEPTH_READ makes
  stencil read-only.
- Clip space: D3D convention (y up, z 0..1). Vulkan flips the viewport;
  D3D12 needs nothing; Metal's NDC is also y up.
- Bind groups: group *n* and slot *s* address the same resource as HLSL
  `register(<class>s, space<n>)`. D3D12 maps each group to a descriptor
  table (or root descriptors for the uniform buffers) in one root signature;
  Metal maps through SPIRV-Cross's resource binding remapping.
- Exact results the tests assert: the eight cells of `rhi_vk_test.c`
  should be replicated as each backend's own test with the same expected
  values, so frame dumps from two backends can be compared bit for bit on
  the integer paths.
- Limits: `uniformAlign` 256 on D3D12; `copyRowPitchAlign` 256 and
  `copyOffsetAlign` 512 (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`,
  `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`).
