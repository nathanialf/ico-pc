# The render hardware interface

The [RHI](ARCHITECTURE.md#terms) is the thin interface under [rd](ARCHITECTURE.md#terms) that the
Vulkan and Direct3D 12 backends implement; macOS and iOS run the Vulkan backend over MoltenVK. This
document covers the interface, how several backends share one build, each backend's device,
swapchain and diagnostics switches, and the tests. The shaders are [SHADERS.md](SHADERS.md); how the
window paces and presents is [DISPLAY.md](DISPLAY.md).

## Files

| path | what it holds |
| --- | --- |
| `port/rhi/rhi.h` | the interface: handles, formats, resources, bind groups, pipelines, passes, commands, stats, timing, backend selection |
| `port/rhi/rhi_backend.h` | the backend table (`RHI_BACKEND_FUNCS`, `RhiBackendTable`, `RHI_BACKEND_DEFINE`) |
| `port/rhi/rhi_backend_names.h` | the renaming of every entry point to `rhi_<prefix>_*` inside a backend |
| `port/rhi/rhi_backend.c` | the real `rhi_*` functions, forwarding to the selected table; the layer classifiers |
| `port/rhi/rhi_driver_version.h`, `rhi_driver_version.c` | a Vulkan `driverVersion` decoded the way its vendor writes it, for the adapter log line |
| `port/rhi/CMakeLists.txt` | the libraries, SDL3's prefix per target, the tests |
| `port/rhi/vk/rhi_vk.h` | Vulkan extras for tests and tools only |
| `port/rhi/vk/vk_device.c` | instance, device pick, features, limits, validation, the `ICO_VK_*` switches, handle pools |
| `port/rhi/vk/vk_present_mode.c` | the present mode and the present-wait decision, pure |
| `port/rhi/vk/vk_swapchain.c` | the swapchain on SDL3's surface, the present-wait thread, the surface poll |
| `port/rhi/vk/vk_pipeline.c` | bind group layouts, bind groups, pipelines, the pipeline cache file |
| `port/rhi/vk/vk_cmd.c` | frames, command lists, submission, barriers, passes, draws, copies, readback |
| `port/rhi/vk/vk_resource.c` | buffers, textures, samplers, shaders, deferred destruction |
| `port/rhi/vk/vk_enums.h`, `vk_internal.h` | the enum tables; the backend's shared state |
| `port/rhi/vk/vk_surface_android.c` | the window surface on Android |
| `port/rhi/d3d12/rhi_d3d12.h` | D3D12 extras for tests only |
| `port/rhi/d3d12/d3d12_device.c` | runtime loading, adapter pick, device, debug layer, heaps, limits, the stats entry points |
| `port/rhi/d3d12/d3d12_plan.h`, `d3d12_plan.c` | the bookkeeping that needs no D3D12 header |
| `port/rhi/d3d12/d3d12_swapchain.c` | the DXGI flip-model swapchain, the waitable object, frame statistics |
| `port/rhi/d3d12/d3d12_cmd.c`, `d3d12_pipeline.c`, `d3d12_resource.c` | commands and fences; root signatures and pipeline states; committed resources |
| `port/rhi/d3d12/d3d12_enums.h`, `d3d12_internal.h` | the enum tables; the backend's shared state |
| `cmake/IcoMoltenVK.cmake` | the MoltenVK slice an Apple build links |
| `tools/fetch_moltenvk.sh` | MoltenVK and the Apple SDL3 build |
| `port/rhi/test/` | the backend tests and their shaders |

## The interface

`rhi.h` keeps the surface small: fourteen shader files, a reachable set of a few hundred pipelines
(held under `RD_PIPELINE_REACHABLE_MAX`, 1024, `port/render/rd_state.h`), about twenty render
targets, ring buffers for per-frame data and no compute. Its rules for additions: nothing
Vulkan-only (no input attachments, no framebuffer feedback loops, no push descriptors; anything
added must be expressible in D3D12 and Metal); explicit state transitions (`rhi_cmd_barrier`: the
caller knows the frame graph and records every texture transition; a backend adds none of its own
for textures, though D3D12 keeps a record of each texture's state to fill in a transition from
UNDEFINED, and orders buffer copies and same-state copies itself); opaque 32-bit handles with a
generation, 0 being null; byte layouts fixed by rd and shared with the shaders through
`port/shaders/shader_consts.h` (`port/rhi/rhi.h`, header comment). The destination read's input
attachment (`RHI_BIND_INPUT_ATTACHMENT`, Vulkan only) sits behind its own limits bit (below).

**Handles and frames.** `RhiBuffer`, `RhiTexture`, `RhiSampler`, `RhiShader`, `RhiPipeline`,
`RhiBindGroupLayout`, `RhiBindGroup`, `RhiCommandList`, `RhiFence`. `RHI_FRAMES_IN_FLIGHT` is 2;
`rhi_wait_frame` blocks until the frame submitted two frames ago is done and is the frame boundary
(command lists and transient bind groups of that slot recycled); `rhi_frame_slot` names the slot for
per-frame resources outside the RHI. `rhi_wait_idle` idles the GPU; `rhi_collect_garbage_now` also
carries out every deferred destroy (`port/rhi/rhi.h`, commands).

**Formats** (`RhiFormat`): RGBA8 UNORM (alpha holds raw [GS](ARCHITECTURE.md#terms) alpha, 0x80 =
1.0), RGBA8 UINT (the exact integer blend's targets), R8 UNORM and UINT, R16 UINT, RGBA16F, D32F,
D32F_S8 (the scene's depth-stencil; the Vulkan backend may back it with D24S8,
`RhiLimits.depthStencilFormatName`), BGRA8 (the swapchain), BC1/2/3/7 (texture packs, only with
`RhiLimits.bcTextures`, copy-destination and sampled only), R32 UINT (the scene depth's bits for the
fog), RGBA32 UINT (the VU vertex pre-pass's record planes, a colour target and copy source: a
colour attachment and sampled on every Vulkan device by the specification's required format
support, a render target at Direct3D 12 feature level 11_0; D3D12 makes it typeless, viewed UINT,
[RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)). Vertex formats are F32x1..4, U8x4 UNORM and UINT, U16x2 UINT, U32x1 (`port/rhi/rhi.h`,
formats).

**Resources.** Buffers have a usage mask and a memory kind: DEVICE (static meshes), UPLOAD (the
per-frame ring, persistently mapped) or READBACK. Textures take a size, mip levels, a format, a
usage mask and a sample count (2, 4 or 8 from `RhiLimits.msaaCounts`: multisampled targets are never
copied or read back, only resolved by `rhi_cmd_resolve_color` or read by a shader). STORAGE and
INPUT_ATTACHMENT usages exist for the destination read and the [local read](#local-read). Create
and destroy are not frame-safe:
destruction is deferred until the frames that may use the handle have retired (`port/rhi/rhi.h`,
resources).

**States** (`RhiState`): UNDEFINED, RENDER_TARGET, DEPTH_WRITE, DEPTH_READ (test without write, also
sampleable; a stencil-only writer uses DEPTH_WRITE with `depthWrite` false), SHADER_READ, COPY_SRC,
COPY_DST, PRESENT, STORAGE and RENDER_TARGET_READ. A barrier is needed only when a texture changes
state: two writes in the same state are ordered by the backend. Buffers have no barriers: a buffer
copy is made visible to every later read and waits for every earlier read of its destination, and
so is a texture-to-buffer copy into a device buffer a shader reads (one made for vertex, index,
uniform or storage reads: the pre-pass's record buffer). The Vulkan backend folds that copy's wait
into the copy's own hazard barrier and holds its visibility back as a memory barrier the next pass,
copy or command issues with its own (`VkrCmdList.pendMemDst`); D3D12 moves the buffer to
COPY_DEST and back to GENERIC_READ around the copy (`port/rhi/rhi.h`, commands
(`rhi_cmd_copy_texture_to_buffer`); `port/rhi/vk/vk_cmd.c` (`vkr_hz_copy`, `vkr_flush_barriers`)).

**Bind groups.** One fixed slot model for every program: group 0 the per-frame uniforms (b0), group
1 the per-draw uniforms (b1; the VU programs' b2, b3), the VU vertex stream (t0) and the vertex
pre-pass's record (t4), group 2 the
textures t1..t4 and samplers s1..s4. Backends map these onto descriptor sets or root parameters; rd
never sees the mapping. Binding types: uniform buffer, storage buffer, sampled texture, sampler, a
dynamic uniform buffer whose offset is given at bind time (`rhi_cmd_set_bind_group_offsets`, at most
`RHI_MAX_DYNAMIC_OFFSETS` (4) a layout, so one group serves every draw reading a block of one size
from one buffer), storage texture and input attachment. Bind groups are transient: valid for the
current frame only (`port/rhi/rhi.h`, bind groups).

**Pipelines and passes.** `RhiPipelineDesc` carries the two shaders, the layouts, vertex bindings
and attributes, topology (triangle list or strip, line list, point list), up to
`RHI_MAX_COLOR_TARGETS` (3: the VU vertex pre-pass draws its record's three planes; every other
pass one target) blend states with dual-source factors (`RHI_BF_SRC1_*`, only with
`RhiLimits.dualSourceBlend`), the depth-stencil state (depth grows with GS Z: GS GEQUAL is GEQUAL),
colour and depth formats, a sample count and `dstRead`: NONE, STORAGE (the target bound as a
rasterizer-ordered storage texture, no colour target) or ATTACHMENT (colour attachment 0 read as an
input attachment in rasterization order, Vulkan only). Culling is always off: the GS has none. A
render pass names its colour and depth attachments with load ops (LOAD, CLEAR, DONT_CARE) and store
ops (STORE, or DONT_CARE so a tile-based GPU can skip the write-back) (`port/rhi/rhi.h`, pipelines,
passes).

**Several pipelines at once.** `rhi_create_pipelines(descs, n, out, batch)` creates a set of
pipelines with the driver's compiles on worker threads: the logical cores less one, at least one, at
most `RHI_PIPELINE_THREADS_MAX` (8), each with an 8 MB stack. What the backend keeps in its own
tables (the pool slots, Vulkan's pipeline layouts, D3D12's root signatures) is made on the calling
thread before the workers start and filled in after they finish, so the workers call nothing but
`vkCreateGraphicsPipelines`, through the one `VkPipelineCache` (which the Vulkan specification makes
internally synchronised), or `ID3D12Device::CreateGraphicsPipelineState` (the device is free
threaded). Vulkan's workers are SDL threads (a backend built without SDL compiles on the calling
thread); D3D12's are Win32 threads. `out[i]` is `descs[i]`'s pipeline or 0, each failure logged
as `rhi_create_pipeline` logs it, and the call returns the count made. The optional
`RhiPipelineBatch` takes a wait function, which the calling thread calls about every
`RHI_PIPELINE_WAIT_MS` (50) with the number finished; it may use the RHI meanwhile (the renderer's
start-up screen presents frames from it), since the workers touch nothing but the driver. It hands
back each pipeline's compile time as the worker measured it around the driver call, and the threads
used; its `maxThreads` (0: the count above) caps the workers on both backends, the renderer's
careful start-up set taking 1 ([RENDER.md](RENDER.md#the-start-up-set)). `rhi_create_pipeline` stays the single creation, the same three steps on the calling thread
(`port/rhi/rhi.h` (`rhi_create_pipelines`); `port/rhi/vk/vk_pipeline.c`,
`port/rhi/d3d12/d3d12_pipeline.c`).

**Commands.** Begin, end and submit a list; barriers; begin and end a render pass; viewport,
scissor, pipeline, bind groups, vertex and index buffers, stencil reference, blend constant; draw
and draw indexed; buffer, buffer-to-texture, texture and texture-to-buffer copies (outside passes,
the textures in COPY_SRC or COPY_DST); colour resolve; debug labels; checkpoint markers and the
submit tag of the [device-loss report](#device-loss). `rhi_readback_texture` reads a
whole texture synchronously for dumps and tests (`port/rhi/rhi.h`, commands, verification helpers).

**Limits** (`RhiLimits`): the uniform alignment (256 on D3D12), the largest texture and render
target, dual-source blending (optional: without it rd blends in two passes), depth readback and
sampling of the depth-stencil format, copy pitch and offset alignment (D3D12 256 and 512, Vulkan 1
and 4), mips and anisotropy, dynamic uniforms and the storage range, BC textures, `tiler` (a
tile-based phone GPU: Qualcomm, ARM, Imagination, Samsung, Apple, Broadcom or any Android device;
false on D3D12), the MSAA counts every renderer target supports and sample-rate shading, and the
destination read's three bits: `fragmentInterlock` (Vulkan), `rov` (D3D12) and
`rasterOrderAttachment` (Vulkan), and `rasterOrderOff`, why the attachment read is off on a device
that has it (`attachment read off: Qualcomm driver <version>`, NULL otherwise), and
`colorBytesPerPixel`, the bytes one pixel of a pass's colour targets may take together: 0 (no
bound) on D3D12 and Vulkan, under MoltenVK the Apple GPU's Metal implicit image block (below),
`dynamicDepth` and `dynamicBlend` ([Dynamic state](#dynamic-state)), and the device table's two
reasons (Vulkan, `kVuPlainOnly` in `port/rhi/vk/vk_device.c`; NULL elsewhere and on D3D12):
`vuFallbackPlain`, why the renderer's VU fallback takes the plain program entries on this device,
and `carefulPipelines`, why its start-up set is made under the careful policy
([RENDER.md](RENDER.md#the-fallback-entries), [RENDER.md](RENDER.md#the-start-up-set))
(`port/rhi/rhi.h` (`RhiLimits`)).

**Device and swapchain.** `rhi_init` takes an `SDL_Window` (NULL: headless, no swapchain),
[vsync](ARCHITECTURE.md#terms), debug layers and the app name. `rhi_device_lost` turns true once the
device is gone; the backend logs the reason and its [loss report](#device-loss) once and the window
ends the session.
`rhi_resize_swapchain`, `rhi_swapchain_size` (the backend rebuilds on its own at the surface's size,
and the renderer follows it at each acquire), `rhi_acquire_backbuffer` (id 0: rebuild needed),
`rhi_present`, `rhi_present_shown` (false after a [present](ARCHITECTURE.md#terms) that did nothing
or a D3D12 present the OS reported occluded), and the Android lifecycle's `rhi_release_surface` and
`rhi_recreate_surface` (`port/rhi/rhi.h`, device).

**Stats** (`RhiStats`) are cumulative since `rhi_init`; the caller takes differences. The `*Ns`
fields are CPU time blocked in the backend: `fenceWaitNs` on GPU completion (`rhi_wait_frame`,
`rhi_wait_idle`, readbacks), `acquireNs` in the swapchain acquire, `presentNs` in the present call.
`swapchains` counts swapchains made, so the window can tell the first present on a new one. On
Vulkan `acquireNs` is `vkAcquireNextImageKHR` and `presentNs` `vkQueuePresentKHR`; on D3D12
`acquireNs` is the wait on the frame latency object, `presentNs` is `Present` and `fenceWaitNs` is
`dx_wait_fence`. D3D12 keeps its counts in `DxState.counts`: `draws` per draw call, `renderPasses`
per pass begun, `pipelineBinds` per pipeline change, `barriers` per `ResourceBarrier` call, `copies`
per copy, resolve or texture-to-buffer call of the RHI, `submits` per executed command list of a
frame (a readback's one-shot list is not counted, as Vulkan does not count its readback's submit:
`port/rhi/vk/vk_cmd.c` (`rhi_readback_texture`), `port/rhi/d3d12/d3d12_cmd.c`
(`rhi_readback_texture`)), `waitIdles`, `readbacks`, and the buffers and textures made and
destroyed, at the same points as Vulkan. The bind groups count differently: `bindGroups` counts
every group made, which on D3D12 includes the null group it makes for a slot a pipeline reads and
the caller left unbound (`port/rhi/d3d12/d3d12_pipeline.c` (`dx_null_bind_group`)), and
`bindGroupBinds` counts each slot `dx_flush` sets at a draw: every slot marked dirty, all of the
root signature's slots after a root-signature change (D3D12 resets the root arguments with it),
null groups included, where Vulkan counts only the slots it binds a caller's group to
(`port/rhi/vk/vk_cmd.c` (`vkr_flush_bind_groups`)). So for the same draws D3D12's two bind group
figures read higher than Vulkan's.
The Vulkan-only fields read 0 on D3D12: the memory gauges (live and peak allocations, their bytes,
the device's limit), `memoryAllocs`, `memoryFrees`, `memoryFallbacks`, `orderBarriers`
(`port/rhi/rhi.h` (`RhiStats`); `port/rhi/vk/vk_swapchain.c` (`rhi_acquire_backbuffer`,
`rhi_present`); `port/rhi/d3d12/d3d12_device.c` (`rhi_get_stats`); `port/rhi/d3d12/d3d12_cmd.c`
(`dx_wait_fence`, the draw, pass, bind and copy calls)). `createNs` is the CPU time in
`rhi_create_buffer` and `rhi_create_texture`, the memory allocation and the object's creation, on
both backends (`port/rhi/vk/vk_resource.c`, `port/rhi/d3d12/d3d12_resource.c`); the renderer's
slow-walk line reads it ([RENDER.md](RENDER.md#the-start-up-set)). `window: per replay N draws, ...` therefore
reads real numbers on both backends.

**Timestamps.** `RHI_MAX_TIMESTAMPS` (256) a [frame slot](ARCHITECTURE.md#terms): the
renderer's list and effect stamps take the first 32 and a replay timed pass by pass one more per
pass ([RENDER.md](RENDER.md#the-per-pass-timing)); a pool or heap of 256 is 2 KB a slot. The
indices written are bit sets of `RHI_TIMESTAMP_WORDS` words (`rhi_ts_has`, `rhi_ts_add` in
`port/rhi/rhi.h`), and Vulkan reads a slot's results only up to the highest index written
(`port/rhi/vk/vk_cmd.c` (`vkr_collect_timestamps`)).
`rhi_read_timestamps` hands back the slot `rhi_wait_frame` last recycled, so reading never waits.
`RHI_TIMESTAMP_KEEP_BARRIERS` keeps the held-back image barriers held (D3D12 holds none back and
ignores it). Vulkan uses query pools. D3D12 has a timestamp query heap of `RHI_MAX_TIMESTAMPS`
entries and a readback buffer per frame slot (`port/rhi/d3d12/d3d12_cmd.c`): `rhi_cmd_write_timestamp`
is an `EndQuery`, the list that wrote the stamps resolves them (`ResolveQueryData`) into the slot's
readback buffer as it closes, and `rhi_wait_frame` reads them once the slot's fence has passed (the
slot is recycled, so the GPU is not writing it), converting ticks with
`ID3D12CommandQueue::GetTimestampFrequency`. A list that is not executed (a failed `Close`, a lost
device) leaves its stamps unread. `rhi_timestamps_supported` is true when the queue reports a
frequency and every slot got its heap and buffer; otherwise the device log says why once
(`rhi_d3d12: GPU timestamps: off (...)`) and the calls do nothing. `rhi_init` logs
`rhi_d3d12: GPU timestamps: on (<Hz> Hz, 256 per frame slot)`. `ICO_DX_FAKE_NO_TIMESTAMPS=1` turns
them off for a test of the fallback.

**Present mode and timing.** `rhi_prefer_mailbox` asks for mailbox with vsync from the next
swapchain (D3D12: not offered); `rhi_present_mailbox` and `rhi_present_mode_name` say what the
swapchain has. `rhi_present_timing`, `rhi_present_timing_use`, `rhi_present_last_id` and
`rhi_present_timing_next` are the display's timing of presents (Vulkan's present wait, below; D3D12:
false, nothing, 0, -1).
`rhi_present_statistics(&presentCount, &presentRefresh, &syncRefresh, &syncNs)` is the swapchain's
own count of the pictures it showed, read after each present: D3D12's `GetFrameStatistics`, with the
last vblank's QPC time moved onto `SDL_GetTicksNS`'s clock; false on Vulkan, before the first
picture, without a window or while DXGI calls the counts disjoint (`port/rhi/rhi.h`;
`port/rhi/d3d12/d3d12_swapchain.c` (`dx_frame_stats`)).

**Other entry points.** `rhi_set_pipeline_cache_path` (Vulkan's cache file, below; D3D12 ignores
it), `rhi_set_vulkan_loader` (a `vkGetInstanceProcAddr` the Android build loaded itself; MoltenVK
builds reject another loader), `rhi_injector_name` ("ReShade" or "vkBasalt") and `rhi_overlay_name`
("Steam overlay" or "Mesa overlay"), with the pure classifiers `rhi_injector_from_layer_name`,
`rhi_overlay_from_layer_name` and `rhi_layer_switched_on`, which reads a layer's enable and disable
variables as the loader does (`port/rhi/rhi.h`; `port/rhi/rhi_backend.c`).

### Dynamic state

A pipeline whose `RhiPipelineDesc.dynamic` has `RHI_DYN_DEPTH` leaves the depth test, write and
compare to the draw, one with `RHI_DYN_BLEND` the blend enable, the blend equation and the colour
write mask of its one colour target, where the limits say the device takes them
(`RhiLimits.dynamicDepth`, `dynamicBlend`; a bit the device lacks, or blend on a pipeline with other
than one colour target, is dropped and the description's value is static). The description's values
are the draw's until a set: `rhi_cmd_set_depth_state(cl, test, write, cmp)` and
`rhi_cmd_set_blend_state(cl, &blend)` after the bind give the next draws their own (a set on a
pipeline without that group does nothing). D3D12 has neither: its limits say false and the sets are
no-ops (`port/rhi/d3d12/d3d12_cmd.c`).

Vulkan (`port/rhi/vk/vk_device.c` (`vkr_create_device`), `vk_pipeline.c` (`vkr_pipe_prepare`),
`vk_cmd.c` (`vkr_flush_dynamic`)): depth is Vulkan 1.3's core states, or on a 1.2 device
`VK_EXT_extended_dynamic_state` with `extendedDynamicState`; blend is
`VK_EXT_extended_dynamic_state3` with `extendedDynamicState3ColorBlendEnable`,
`extendedDynamicState3ColorBlendEquation` and `extendedDynamicState3ColorWriteMask`, only those
enabled. A pipeline's dynamic states are the four always dynamic (viewport, scissor, stencil
reference, blend constants) and `VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE`, `DEPTH_WRITE_ENABLE`,
`DEPTH_COMPARE_OP` and `COLOR_BLEND_ENABLE_EXT`, `COLOR_BLEND_EQUATION_EXT`,
`COLOR_WRITE_MASK_EXT` for its groups; the commands are found through `vkGetDeviceProcAddr` (the
core names on 1.3, the EXT ones otherwise). The sets keep the values the next draw needs; the draw
records only what differs from the values the command buffer holds, and every command of a group
after the bind of a pipeline with that group static (that bind replaced them). So binds of two
dynamic pipelines with the same values record nothing more, and a draw gets exactly the values a
static pipeline of its state would have given it. `RhiStats.dynamicSets` counts the commands
recorded. A draw on a dynamic pipeline whose sets were not made since its bind takes the
pipeline's own values and is logged once (`rhi_vk: dynamic state: a draw with its pipeline's depth
left unset since the bind ...`). The device's line: `rhi_vk: dynamic state: depth test, write and
compare yes (Vulkan 1.3); blend enable, equation and write mask yes
(VK_EXT_extended_dynamic_state3)` (or `no`, with why). `ICO_VK_FAKE_NO_DYNAMIC=1` reports both
false, `=blend` the blend alone, for an A/B of the same frames; `[dev] dynamic_state = false` or
`depth` exports them ([CONFIG.md](CONFIG.md#dev)), and the line then says `no ([dev] dynamic_state =
false, ICO_VK_FAKE_NO_DYNAMIC=1)` or `no ([dev] dynamic_state = depth, ICO_VK_FAKE_NO_DYNAMIC=blend)` (`rhi_vk_test`'s dynamic cell draws
the renderer's depth tests, blends and masks static and dynamic, texel-equal; [Tests](#tests)).

### Local read

Where `RhiLimits.localRead` is true a pass's fragment shaders can read its own attachments as
input attachments (`RHI_BIND_INPUT_ATTACHMENT`; Vulkan `VK_KHR_dynamic_rendering_local_read`; D3D12
has none and its limits say false). The texture is made with `RHI_TEX_INPUT_ATTACHMENT` (a colour
target or a depth-stencil; asked of a device without local read it is ignored) and the pass begins
it readable: `RhiColorAttachment.readable` or `RhiDepthAttachment.readable`, the texture in
`RHI_STATE_RENDER_TARGET_READ` (Vulkan `RENDERING_LOCAL_READ`; for a depth-stencil texture the
barrier's scope is the depth tests' stages and accesses with the fragment shader's input read,
`vk_cmd.c` (`vkr_state_scope`)). A readable depth is never read-only. The binding names the
texture: a colour target's view, or a depth-stencil's depth-only view (`RHI_ASPECT_DEPTH`; the
image gets that view when made with either use). The shader declares a colour attachment with
`[[vk::input_attachment_index(0)]]` and the depth without an index, the mapping Vulkan gives
without `VkRenderingInputAttachmentIndexInfoKHR`; the binding is the t register's slot + 16, as
every texture's.

Between the pass's writes and a read, and between the read and the next writes of the same pixel,
`rhi_cmd_local_read_barrier(cl, mask)` records one by-region memory barrier inside the pass
(`vkCmdPipelineBarrier` with `VK_DEPENDENCY_BY_REGION_BIT`, counted in `RhiStats.barriers`):
`RHI_LOCAL_READ_COLOR` colour attachment output and its writes to the fragment shader's input
attachment reads, `RHI_LOCAL_READ_DEPTH` the early and late fragment tests and their depth-stencil
writes to the same reads, `RHI_LOCAL_READ_BACK` those reads to the next draws' depth-stencil and
colour writes. Outside a pass, or without local read, it records nothing. The exact blend's
attachment read (`RHI_DST_READ_ATTACHMENT`) reads the colour in rasterization order instead and
needs no barrier (`port/rhi/rhi.h` (`rhi_cmd_local_read_barrier`); `port/rhi/vk/vk_cmd.c`
(`rhi_cmd_begin_render_pass`, `rhi_cmd_local_read_barrier`); `port/rhi/vk/vk_pipeline.c`
(`rhi_create_bind_group`); `port/rhi/vk/vk_resource.c` (`createTexture`)). The renderer reads the
fog's depth and DATE's alpha this way ([RENDER.md](RENDER.md#the-replay)).

## Backends in one build

Each backend library is compiled with `RHI_BACKEND_PREFIX` set to its short name (`vk`, `d3d12`).
`rhi.h` then includes `rhi_backend_names.h`, whose macros rename every entry point the backend
defines to `rhi_<prefix>_*`, so the backend's sources keep the plain names. The backend registers
its functions once with `RHI_BACKEND_DEFINE` (`rhi_backend_vk` at the end of `vk_device.c`,
`rhi_backend_d3d12` at the end of `d3d12_device.c`); `RHI_BACKEND_FUNCS` lists every entry point in
table order and types each field from `rhi.h`'s declaration (`port/rhi/rhi_backend.h`, header
comment; `port/rhi/rhi_backend_names.h`). Callers never include `rhi_backend.h`.

`rhi_backend.c` defines the real `rhi_*` functions as forwarders to the selected table. The linked
backends are listed by `ICO_RHI_HAVE_VK` and `ICO_RHI_HAVE_D3D12`, Vulkan first.
`rhi_create_backend(name)` takes "vulkan" (or "vk") or "d3d12", case-insensitive; NULL or "" takes
`ICO_RHI_BACKEND` when it names a linked backend, else the first linked one. It refuses a backend
not in the build and any change while a device is up; `rhi_backend_name(i)` lists the linked ones
(`port/rhi/rhi_backend.c` (`find`, `defaultBackend`, `rhi_create_backend`)). The window passes
`[video] backend` (default "vulkan"); after a graphics driver crash at start the relaunch's retry
step may name "d3d12" instead when the player chose none (`STARTUP_RETRY_ENV`,
`startup_retry_parse`); a name the build lacks logs
`window: renderer backend "<name>" is not in this build; using vulkan` and takes Vulkan.
`ICO_RHI_BACKEND` reaches only callers that pass no name (`port/platform/window_host.c`
(`ico_window_open`); [DISPLAY.md](DISPLAY.md#the-window-host)).

`port/rhi/CMakeLists.txt` builds `ico_rhi_vulkan` (option `ICO_RHI_VULKAN`, on), `ico_rhi_d3d12`
(`ICO_RHI_D3D12`, on for 64-bit Windows) and `ico_rhi` (the forwarders, linking the backends);
`ico_rhi_vk` is what `port/render` and the shader tests link. `ICO_RHI_MOLTENVK` (on for Apple
targets) links the Vulkan backend to MoltenVK instead of compiling volk (`cmake/IcoMoltenVK.cmake`).
Vulkan-Headers and volk come from `tools/fetch_deps.sh` (the backend loads the Vulkan library at run
time); without SDL3 the backends are headless-only. The D3D12 backend uses mingw-w64's own headers
and loads `d3d12.dll` and `dxgi.dll` at run time (`port/rhi/CMakeLists.txt`, header comment).

## Vulkan

**Device** (`port/rhi/vk/vk_device.c`). A device needs Vulkan 1.2, dynamic rendering (core 1.3 or
the extension), timeline semaphores, the swapchain extension on a window, a graphics queue that can
present, and RGBA8, RGBA8 UINT, R8, D32F and D32F_S8 or D24S8 formats. Devices score discrete 1000,
integrated 500, virtual 200, CPU 100, plus 10 for core 1.3 and 5 for dual-source blending
(`vkr_rate_device`). The adapter line gives the PCI vendor and device ids (`device 10de:1b80`) and
the driver's name and version, decoded by
`rhi_driver_version_text`: NVIDIA's own driver as major 10 bits, minor 8, a third part 8 and a
fourth 6, Intel's Windows driver as major 18 bits and minor 14, every other driver in the Vulkan
version packing; `VkDriverId` decides, and the vendor id alone when it is 0
(`port/rhi/rhi_driver_version.h`; `port/rhi/vk/vk_device.c` (`vkr_driver_text`)). Only the features
used are enabled; the destination read takes `VK_EXT_fragment_shader_interlock` or
rasterization-order attachment access with `VK_KHR_dynamic_rendering_local_read`. Local read
itself (`RhiLimits.localRead`: a pass's own attachments read as input attachments after
`rhi_cmd_local_read_barrier`) is enabled wherever the device offers it, whatever the destination
read: `VK_KHR_dynamic_rendering_local_read`, or on a Vulkan 1.4 device without the extension
`VkPhysicalDeviceVulkan14Features.dynamicRenderingLocalRead` (the effective version is the
instance's at most, 1.3, so that branch waits for the instance to ask for 1.4), and the device line
ends `; local read yes|no` (`yes (Vulkan 1.4)` for the core feature) (`port/rhi/vk/vk_device.c`
(`vkr_create_device`)). `ICO_VK_FAKE_NO_DST` leaves it off too. D3D12 reports none. The attachment
read is not offered on Qualcomm's own Adreno driver (vendor 0x5143, `VkDriverId`
`QUALCOMM_PROPRIETARY` or unknown; not Mesa's Turnip): its reads return wrong colours (issue 89,
Adreno 830 on driver 512.800.72), and the device line says `rasterization-order attachment read off
on this driver (Qualcomm driver <version>: wrong results on Adreno; ICO_VK_ATTACHMENT_READ=1 tries
it)` (`port/rhi/rhi_driver_version.h` (`rhi_driver_is_qualcomm_proprietary`);
[RENDER.md](RENDER.md#destination-reads-and-blend-paths)). The scene's
depth-stencil is D32_SFLOAT_S8_UINT, or D24_UNORM_S8_UINT when only that has the sampled and
transfer features (`vkr_choose_depth_stencil`).

| variable | effect |
| --- | --- |
| `ICO_VK_VALIDATION` | 1 adds `VK_LAYER_KHRONOS_validation` and debug utils; 0 keeps them off |
| `ICO_VK_DEVICE` | an index or a substring of the device name forces the pick |
| `ICO_VK_FAKE_LIMITS` | `mali` clamps the limits to a Mali-G68's (4 descriptor sets, 128 bytes of push constants, 16 sampled images a stage, 4096 allocations, ...); `min` holds the alignment, texture, render target, dynamic uniform, storage range and anisotropy limits to 256, 4096, 4096, 8, 2^27 and 16 |
| `ICO_VK_FAKE_LIMITS=moltenvk` | no limit clamped: the device is reported as MoltenVK (`RhiLimits.moltenvk`) without the attachment read (`rd_blend_selftest_moltenvk_storage`) |
| `ICO_VK_FAKE_D24S8` | 1 takes D24S8 where the device has it |
| `ICO_VK_FAKE_NO_DUAL` | 1 leaves dual-source blending off |
| `ICO_VK_FAKE_NO_DYNAMIC` | 1 leaves the dynamic depth and blend state off, `blend` the blend alone ([Dynamic state](#dynamic-state)) |
| `ICO_VK_FAKE_NO_MSAA` | 1 reports single sampling only |
| `ICO_VK_FAKE_NO_DST` | 1 reports no destination read and no local read |
| `ICO_VK_FAKE_ADRENO` | 1 takes the Qualcomm driver's branch: the attachment read not offered |
| `ICO_VK_ATTACHMENT_READ` | 1 offers the attachment read on Qualcomm's driver too; ico-pc.ini `attachment_read` |
| `ICO_VK_STORAGE_BARRIERS` | 1 puts the by-region barrier before a destination-read draw that only the validation layer needs |
| `ICO_VK_GLOBAL_BARRIERS` | 1 records a global barrier before every render pass and copy (`vk_cmd.c`) |
| `ICO_VK_PRESENT_WAIT` | 1 uses `VK_KHR_present_id` and `VK_KHR_present_wait` where offered (off otherwise) |
| `ICO_VK_FIFO_LATEST_READY` | 1 uses `VK_PRESENT_MODE_FIFO_LATEST_READY_EXT` where FIFO would be taken, when offered |
| `ICO_VK_POLL_SURFACE` | 1 polls the surface size after presents, as Android always does (`vk_swapchain.c`) |
| `ICO_GPU_ROBUST` | 1 enables `robustBufferAccess`, and `robustImageAccess2` and `nullDescriptor` where `VK_EXT_robustness2` offers them; 0 or unset: off, on every platform ([Device loss](#device-loss)); ico-pc.ini `gpu_robust`; the loss order's `robust` step sets 1 for its run |
| `ICO_GPU_CHECKPOINTS` | 1 enables `VK_NV_device_diagnostic_checkpoints` and `VK_EXT_device_address_binding_report` where offered ([Device loss](#device-loss)); ico-pc.ini `gpu_checkpoints` |

(`port/rhi/vk/vk_device.c` (`vkr_fake_limits`, `vkr_choose_depth_stencil`, `vkr_pick_device`);
`port/rhi/vk/vk_cmd.c`; `port/rhi/vk/vk_swapchain.c` (`vkr_poll_surface_on`)).

**Present mode** (`port/rhi/vk/vk_present_mode.c`, pure so its test compiles it alone). With vsync:
mailbox when preferred and offered; FIFO latest ready when asked for and offered; else FIFO. Without
vsync: immediate wherever offered (the picture tears where the compositor lets it), then mailbox,
then FIFO (`vkr_choose_present_mode`). `vkr_present_wait_wanted` is true only when
`ICO_VK_PRESENT_WAIT` is set to anything but "" or "0", the device offers both extensions and SDL
can run the thread.

**Swapchain** (`port/rhi/vk/vk_swapchain.c`). BGRA8 UNORM (else RGBA8) in sRGB-nonlinear colour
space, so the game's colours are written as they are; `minImageCount + 1` images, held to the
surface's maximum; the identity transform where offered (the compositor rotates). A rebuild keeps
the old swapchain until the first present on the new one succeeds, so the screen keeps its last
picture (`vkr_swapchain_create`). An out-of-date acquire returns id 0 and the presenter rebuilds; a
lost surface makes a new surface and swapchain. On Android (or with `ICO_VK_POLL_SURFACE=1`) the
surface's size is polled at every present for 15 s after a swapchain is made, then once a second,
since the driver can go on returning success while the surface changes size (`vkr_poll_surface`).

**The present-wait thread.** With `ICO_VK_PRESENT_WAIT=1` a thread waits on each present's id in
turn (`vkWaitForPresentKHR`, 4 ms a wait) and notes when it returned on `SDL_GetTicksNS`'s clock in
a 64-note ring. Ids go only to presents made while the window uses the timing (under its vsync
lock). A wait that returned at once found the present on the screen already: its time is the note
before when that was under half a millisecond earlier, else not known. Before the swapchain is made
again or destroyed the thread is held out of its waits for at most 100 ms (`vkr_pw_pause`); a thread
still in a wait then stops the timing and the swapchain goes on; at shutdown a thread that does not
end is left to end on its own (`port/rhi/vk/vk_swapchain.c`, the display's timing).

**Pipeline cache.** The window sets `pipelines-<build>.vkcache` in the [user folder](ARCHITECTURE.md#terms)
(Android: the app's cache folder), where `<build>` is `ICO_BUILD_COMMIT` (`git describe`: tag, commits since, short
hash, `-dirty`) with every character but letters, digits, `.` and `-` made `_`
(`port/platform/window_host.c` (`ico_window_open`)); a build without git names it `unknown`. `rhi_init`
first removes every other `pipelines*.vkcache` and `pipelines*.vkcache.tmp` in that folder (the current
file and its `.tmp` stay, files of other names stay, a folder that cannot be listed logs
`pipeline cache: could not list <dir>; no other cache dropped` and goes on, a missing one nothing), one
line each: `pipeline cache: dropped <name> (another version of the game); this start prepares the graphics
from nothing` (`port/rhi/vk/vk_pipeline.c` (`vkr_cache_drop_others`); not done for a path whose file name does
not start `pipelines` and end `.vkcache`). It then loads the file when the file is 32 bytes to 64 MB and its
header (`VkPipelineCacheHeaderVersionOne`: header size, version one, vendor id, device id,
`pipelineCacheUUID`) names this device; anything else logs "is for another device or driver" and
starts empty, as does data the driver refuses. Every pipeline is created through it, and
`rhi_shutdown` writes it back to `<path>.tmp` and renames it over the file, not synced to the disk
(`pipeline cache: N bytes written to <path> at exit in T ms`), and not at all when its data's size
is the one this run loaded or last wrote (`pipeline cache unchanged, not written (N bytes in
<path>)`: a run that made no new pipeline; a 21 MB cache took seconds on a USB stick).
`rhi_pipeline_cache_save` writes it the same way while the device is up, flushed to the disk
before the rename, keeping the cache, and logs
`pipeline cache: N bytes written to <path> in T ms` (data of the size the file already holds is not
written again); the window calls it after the start-up set, so the file holds the set when a run
never reaches `rhi_shutdown` (a phone's system ends the app, or the device is lost). Nothing is
written after a device loss. D3D12 writes nothing (`port/rhi/vk/vk_pipeline.c` (`vkr_cache_load`,
`vkr_pipeline_cache_init`, `vkr_cache_write`, `rhi_pipeline_cache_save`,
`vkr_pipeline_cache_shutdown`)). `rhi_pipeline_cache_loaded` says whether a file was loaded; the
window passes it to the crash diagnostics, whose start-up retry deletes the file first
(`port/platform/window_host.c` (`ico_window_open`)).

**Bindings and synchronisation.** A layout slot is a descriptor binding at the slot plus its type's
shift (`port/rhi/vk/vk_enums.h` (`vkr_bindTypeMap`): b 0, t 16, s 32, u 48, the shifts DXC compiles
with). Each frame slot owns a command pool, descriptor pools and the objects destroyed while it
recorded; every submit signals the next value of one timeline semaphore that `rhi_wait_frame` waits
on; the first submit after an acquire waits on its semaphore and the present on the image's
`renderDone` (`port/rhi/vk/vk_pipeline.c`; `port/rhi/vk/vk_cmd.c`, header comment). Every buffer and
texture has its own `VkDeviceMemory` (`port/rhi/vk/vk_resource.c`, header comment). Each memory kind
tries an ordered list of memory types, and a type the driver refuses (a full heap) passes the
allocation to the next: uploads go to a device-local, host-visible type first, but one whose heap is
under 1 GB (the PCIe BAR window without resizable BAR) takes uploads of up to 16 MB only, and once it
refused an allocation it is tried last, so uploads go through system memory
(`port/rhi/vk/vk_memory.c`, `vkr_memory_candidates`).

**Android surface.** With a driver the app loaded itself the instance belongs to another loader, so
`vk_surface_android.c` makes the surface through the `vkGetInstanceProcAddr` given to volk instead
of SDL's; the surface is NULL while the app is in the background and the caller tries again
(`port/rhi/vk/vk_surface_android.c`, header comment).

**Render area.** A render pass larger than one of its attachments would draw past the image's end:
`rhi_cmd_begin_render_pass` logs the first such pass with the sizes, asserts in a Debug build,
counts it and clamps the area, viewport and scissor to the smallest attachment, and every scissor set
while the pass is open is cut to that area, so the renderer's own full-target scissor cannot reach
past it; a pass that fits is untouched (`port/rhi/vk/vk_cmd.c` (`vkr_pass_area_fits`,
`rhi_cmd_set_scissor`)). The D3D12 backend clamps its default
viewport and scissor the same way (`port/rhi/d3d12/d3d12_cmd.c` (`dx_pass_area_check`)).

**Test hooks** (`port/rhi/vk/rhi_vk.h`): the validation error count, a forced present result
(suboptimal, surface lost, out of date), swapchain creation and retirement counts, a stand-in for
`vkWaitForPresentKHR` (and one that hangs), the last limit a creation reached, a texel ceiling
that makes texture creation fail as out of memory, and the count of render passes clamped to their
attachments (with a switch that keeps the Debug build's assert quiet).

## Direct3D 12

**Device** (`port/rhi/d3d12/d3d12_device.c`). `d3d12.dll` and `dxgi.dll` are loaded at run time, so
the Vulkan path of the same executable never depends on them. A module loaded from outside the
system directory (ReShade's `dxgi.dll` beside the program) makes `rhi_injector_name` "ReShade".
Adapters are enumerated discrete first (`EnumAdapterByGpuPreference`); a software adapter or one
without feature level 11_0 is skipped; with none the device runs on WARP (`dx_pick_adapter`). Each
enumerated adapter logs `rhi_d3d12: adapter N: <name>`, followed by
`(unsuitable: a software adapter, or no feature level 11_0)` for one it skips, and the device line
is `rhi_d3d12: <adapter> (D3D12, feature level 11_0[, WARP][; driver version a.b.c.d])[, headless]`,
the user-mode driver's version in four 16-bit parts (`dx_driver_text`, `rhi_init`).

| variable | effect |
| --- | --- |
| `ICO_D3D12_DEBUG` | 1 the debug layer and the DXGI debug factory; 0 off |
| `ICO_D3D12_ADAPTER` | `warp`, an adapter index or a substring of its name |
| `ICO_DX_FAKE_NO_DST` | 1 reports no destination read (no ROVs) |
| `ICO_DX_FAKE_NO_TIMESTAMPS` | 1 turns GPU timestamps off (`rhi_timestamps_supported` false) |
| `ICO_D3D12_FRAME_LATENCY` | the swapchain's frame latency (below) |

The debug layer's info queue keeps corruption, errors and warnings and drops the clear-value
performance notes (`port/rhi/d3d12/d3d12_device.c` (`dx_setup_info_queue`)). A failed call logs
`rhi_d3d12: <call> failed (HRESULT 0x...) at <file>:<line>`; a lost device (removed, reset, hung,
driver internal error) is logged once with the removed reason; later failures are counted, not
logged (`port/rhi/d3d12/d3d12_device.c` (`dx_check`)).

**Frames and fences** (`port/rhi/d3d12/d3d12_cmd.c`). One `ID3D12Fence` on the direct queue: every
`rhi_submit` executes its list and stores the next signalled value in the current frame slot
(`dx_signal`); `rhi_wait_frame` moves to the next slot, waits for that slot's value and then resets
its command allocators, descriptor rings and deferred destroys (`dx_recycle_frame`,
`dx_wait_fence`); `rhi_wait_idle` signals and waits at once. Each wait is counted into `fenceWaits`
and `fenceWaitNs`.

**Bookkeeping** (`port/rhi/d3d12/d3d12_plan.h`), header-free so it is tested on every host: the
`RhiState` to `D3D12_RESOURCE_STATES` table (depth SHADER_READ and DEPTH_READ are one state, PRESENT
is COMMON) and the barrier plan (UNDEFINED takes the tracked state); buffer tracking for copies
(COMMON at a list's start, COPY_DEST, then GENERIC_READ); same-state copy ordering (a second copy
into a texture in one list goes through COPY_DEST, COMMON, COPY_DEST); the descriptor rings (one
shader-visible heap per type, split into a region per frame slot, the sampler heap keeping one
persistent descriptor per sampler); bind group layouts as up to two descriptor tables in register
space `<group>` with each dynamic uniform a root CBV; constant buffer view sizes; the DXBC input
signature reader; the handle pools. Every resource is committed (`d3d12_resource.c`); textures are
created typeless and viewed typed (`d3d12_enums.h`).

**Swapchain** (`port/rhi/d3d12/d3d12_swapchain.c`). `DXGI_SWAP_EFFECT_FLIP_DISCARD` on the SDL
window's HWND, `DX_SWAP_BUFFERS` (3) BGRA8 buffers, stretch scaling, DXGI's Alt+Enter turned off.
`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` is set when DXGI 1.5 offers it, and
`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` when a frame latency is wanted; the same flags
go to `ResizeBuffers`, since DXGI refuses a change of the waitable flag (`dx_swap_flags`).
`ICO_D3D12_FRAME_LATENCY` "0" makes no waitable object (Present blocks on a full queue), "1" to "3"
that latency (1 serialises the CPU and the GPU), anything else 2, which logs
`ICO_D3D12_FRAME_LATENCY=<v> is not 0..3: frame latency 2` (`dx_frame_latency_wanted`). After
creation `SetMaximumFrameLatency` and `GetFrameLatencyWaitableObject`: a failed
`SetMaximumFrameLatency` logs `frame latency N not set: no waitable object` after the `DX_CHECK`
line, a missing object `no frame latency waitable object: Present waits for the display`; either way
the swapchain goes on without the wait (`dx_frame_latency_open`). Creation logs
`swapchain WxH, 3 buffers, frame latency N (waitable)` or `..., no waitable object`
(`dx_swapchain_create`). While the object exists, `rhi_acquire_backbuffer` waits on it before each
acquire, skipped after an occluded Present; the wait comes before the
[replay](ARCHITECTURE.md#terms) records and is timed into `acquireNs`, so `Present` returns at once,
the picture is drawn after the wait rather than before it, and the wait for the display lands in the
replay's "acquire" phase; the first acquire waits too. `rhi_present` calls `Present(1)` with vsync
and `Present(0)` without (`DXGI_PRESENT_ALLOW_TEARING` where allowed), times it into `presentNs`,
treats `DXGI_STATUS_OCCLUDED` as not shown, reads the frame statistics (`PresentCount`,
`PresentRefreshCount`, `SyncRefreshCount`, `SyncQPCTime`) after a shown present, and resizes the
buffers when the window's pixel size changed, as DXGI has no out-of-date result.
`rhi_resize_swapchain` calls `rhi_wait_idle` before `ResizeBuffers` and returns false for a zero
size (minimised), which the caller retries at the next resize. `rhi_present_mode_name` reports
`fifo` with vsync, `immediate` without vsync where tearing is allowed, else `mailbox`. The surface
calls do nothing: a DXGI swapchain stays on its window (`port/rhi/d3d12/d3d12_swapchain.c`).

**Not implemented on D3D12**: mailbox with vsync, the display's timing of presents, the pipeline
cache file, and the Vulkan-only stats (the memory gauges and counts above; `port/rhi/d3d12/d3d12_device.c`). On Windows
the vsync lock takes the display's vblanks from the window's own clock thread on
`IDXGIOutput::WaitForVBlank`, outside the RHI and for both backends
(`port/platform/display_clock_win.c`; [DISPLAY.md](DISPLAY.md#the-vblank-clock)).

**Test extras** (`port/rhi/d3d12/rhi_d3d12.h`): the debug layer's error count, whether it is active,
and whether the device runs on WARP.

## Device loss

When the driver gives up on the device (Vulkan `VK_ERROR_DEVICE_LOST`; D3D12 a removed, reset or hung
device) the backend prints one report, at the first failing call, and the window prints the
renderer's [pass ring](RENDER.md#the-pass-ring) after it, before the message box
([PLATFORM.md](PLATFORM.md#diagnostics)). `rhi_wait_idle` is such a call too: Vulkan checks
`vkDeviceWaitIdle`'s result and D3D12 the device's removed reason after the wait, so the
[loss bisect](RENDER.md#the-loss-bisect), which waits there after each replay, sees the loss
(`port/rhi/vk/vk_cmd.c`, `port/rhi/d3d12/d3d12_cmd.c` (`rhi_wait_idle`)). The report's parts:

- **The last submissions.** Each backend keeps its last 8 submissions with the timeline (fence) value
  each signals, the replay and frame numbers `rhi_set_submit_tag` set before it, and on Vulkan its
  command lists and whether it signalled a present; the report marks each finished or not against
  the value the GPU reached, or "state unknown" when that value cannot be read (a failed Vulkan
  counter read; a removed D3D12 device's fence, which reads `UINT64_MAX`). A Vulkan wait that fails first logs how long it waited, the value it
  waited for, the last one submitted and the one the GPU reached; once the device is lost no wait is
  made again (`port/rhi/vk/vk_cmd.c` (`vkr_wait_value`, `vkr_note_submit`); `port/rhi/vk/vk_device.c`
  (`vkr_lost_report`, `vkr_report_submits`); `port/rhi/d3d12/d3d12_device.c` (`dx_lost_report`)).
- **The driver's fault report** (Vulkan). `VK_EXT_device_fault` is enabled whenever the driver offers
  it; `vkGetDeviceFaultInfoEXT` gives a description, the faulting addresses (each with its kind:
  invalid read, write or execute, instruction pointer; and its precision) and the vendor's entries.
  Each address is matched to a resource through the allocation table: every `VkDeviceMemory` alive
  with the buffer or image bound to it, its size and debug name, kept by `vkr_allocate` and
  `vkr_free_memory`, and with `gpu_checkpoints` the object's GPU address range from
  `VK_EXT_device_address_binding_report` (a debug-utils messenger of its own, no validation layer;
  a report naming the buffer or image, or the memory itself for a driver that reports at
  allocation, which comes before the table has the entry and is kept until `vkr_alloc_note` adds
  it);
  the report names the resource holding the address, or the nearest below it
  (`port/rhi/vk/vk_resource.c` (`vkr_alloc_note`, `vkr_alloc_bound`, `vkr_alloc_nearest`);
  `port/rhi/vk/vk_device.c` (`vkr_report_fault`, `vkr_bind_callback`)).
- **Checkpoints** (`gpu_checkpoints`). `rhi_cmd_checkpoint(cl, marker)` records
  `vkCmdSetCheckpointNV` where `VK_NV_device_diagnostic_checkpoints` is offered; the renderer gives
  every render pass, copy, snapshot, exact blend, upload batch and present a marker, the pass ring's
  sequence number. On the loss `vkGetQueueCheckpointDataNV` gives the last marker the GPU started
  (top of pipe) and finished (bottom of pipe): "pass #N" in the ring printed next. On D3D12 the same
  switch turns DRED on before the device is made (auto-breadcrumbs, breadcrumb contexts, page faults,
  through `ID3D12DeviceRemovedExtendedDataSettings1`, else the first settings interface), the marker
  is a `SetMarker` string ("pass #N") DRED keeps as a breadcrumb context, objects are named for its
  page-fault report, and the report prints every command list not finished (its operations from the
  one it stopped at, and the last marker before it) and the faulting address with the allocations
  alive or lately freed there (`port/rhi/vk/vk_cmd.c` (`rhi_cmd_checkpoint`);
  `port/rhi/vk/vk_device.c` (`vkr_report_checkpoints`); `port/rhi/d3d12/d3d12_device.c`
  (`dx_dred_enable`, `dx_report_breadcrumbs`); `port/rhi/d3d12/d3d12_cmd.c` (`rhi_cmd_checkpoint`)).

**A device that hangs without a loss.** A driver may also never return from a wait and never
report the device lost. `rhi_gpu_progress` gives another thread the GPU's progress as the
submitting thread last saw it, without a call into the driver: the newest submission's timeline
value, when it was made and its `rhi_set_submit_tag` replay and frame, the newest value seen
finished (a wait that returned, a counter read before one, `rhi_wait_idle`), and the value a wait
in progress blocks on with how long it has blocked. The submitting thread stores them with
`__atomic` stores in `vkr_note_submit`, `vkr_wait_value` and `rhi_wait_idle`; a wait in progress
for a value above the newest seen finished is work the GPU has not finished. The watchdog reads it
to tell a GPU hang from a freeze ([PLATFORM.md](PLATFORM.md#diagnostics)). D3D12 does not track it
(`rhi_gpu_progress` returns false): its driver reports a hung device as `DXGI_ERROR_DEVICE_HUNG`, a
loss (`port/rhi/rhi.h` (`RhiGpuProgress`); `port/rhi/vk/vk_cmd.c` (`vkr_progress_wait`,
`vkr_progress_finished`); `port/rhi/vk/vk_device.c` (`rhi_gpu_progress`);
`port/rhi/d3d12/d3d12_cmd.c` (`rhi_gpu_progress`)).

The checkpoints cost one GPU write per pass (about 430 a replay) and the binding report a callback
per resource, so they stay off unless asked for; the fault report costs nothing until a loss.
**Robust access** (`gpu_robust`, Vulkan): `robustBufferAccess`, plus `robustBufferAccess2` and
`robustImageAccess2` where `VK_EXT_robustness2` offers them, so an access out of a buffer's or
image's range reads zero and writes nothing instead of reaching other memory; an A/B switch for a
loss that an out-of-range access could explain. It is off by default on every platform: a Mali GPU
reads storage buffers through plain pointers, so an out-of-range read there loses the device
instead of reading zero (issue 95), but the VU path's reads that went out of range are gone since
its index values are dense (marks in the stream), and the bounds checks cost every
StructuredBuffer read of that path. `gpu_robust = true` turns it on with `nullDescriptor`; the
device-loss order's `robust` step (`port/platform/startup_retry.h`) exports `ICO_GPU_ROBUST=1` for
its run, which only a platform that can start the game again reaches, and only a run whose caller
sets `StartupRetryRun.robustOffDefault`. One start-up line says what is on:
`rhi_vk: loss diagnostics: fault report on|not offered; checkpoints ...; resource addresses ...;
robust access ...` (`off (the default; gpu_robust = true turns it on)`, `off (gpu_robust)`, or `on,
buffers (robustness2) and images (robustness2) (gpu_robust)`, `(the restart after the graphics card
stopped responding)` at the robust step) (`port/rhi/vk/vk_device.c` (`vkr_create_device`)). Both switches come from the
environment (`ICO_GPU_CHECKPOINTS`, `ICO_GPU_ROBUST`) or ico-pc.ini's `[dev]` keys
([CONFIG.md](CONFIG.md#dev)).

## MoltenVK

Apple builds link MoltenVK's static XCFramework instead of loading a Vulkan driver (iOS apps cannot
load one): `cmake/IcoMoltenVK.cmake` picks the `ios-arm64` or `macos-arm64_x86_64` slice under
`tools/toolchain/deps/moltenvk/<platform>/MoltenVK`, links Metal, Foundation, QuartzCore,
CoreGraphics, IOSurface and UIKit or AppKit and IOKit, and exports `vkGetInstanceProcAddr` so SDL
finds it with `dlsym`; the iOS package has the device slice only (`cmake/IcoMoltenVK.cmake`).
`tools/fetch_moltenvk.sh [ios|macos]` (on macOS with Xcode) fetches MoltenVK v1.4.2 against a pinned
SHA-256, the licences of the libraries compiled into it, and builds a static SDL3 for the target
(iOS 15.0, macOS 12.0 minimum), then runs `tools/fetch_deps.sh --game-only`
(`tools/fetch_moltenvk.sh`). `rhi_init` rejects a different Vulkan loader on these builds
(`port/rhi/rhi.h` (`rhi_set_vulkan_loader`)).

`RhiLimits.moltenvk` is true on MoltenVK (`rhi_driver_is_moltenvk`: `VK_DRIVER_ID_MOLTENVK`, or
vendor 0x106B with the id unknown under `ICO_RHI_MOLTENVK`) and is logged as `; MoltenVK` on the
`rhi_vk:` device line. The renderer's device profile reads it for the Apple / MoltenVK class
([RENDER.md](RENDER.md#the-device-profile)); the blend is the fixed-function one there as
everywhere ([RENDER.md](RENDER.md#destination-reads-and-blend-paths)).

`RhiLimits.colorBytesPerPixel` under MoltenVK is the Metal family's "Maximum implicit image block
size, per pixel, per sample, when using multiple color render targets" (Metal Feature Set Tables)
for the Apple GPU the device name gives (`rhi_metal_color_bytes`: `Apple A9 GPU` and `Apple A10 GPU`
32 bytes, A11 to A13 64, A14 and later and the M-series 128; an Intel or AMD GPU of a Mac 0, no
bound), so the renderer leaves the VU vertex pre-pass (48 bytes a pixel) off on an A9 or A10
([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes); `port/rhi/rhi_driver_version.c`;
`port/rhi/vk/vk_device.c` (`vkr_fill_limits`)).

## Tests

| test | checks |
| --- | --- |
| `rhi_test_common.c`, `rhi_test_common.h` | headless rendering on any backend, texel by texel: a 64x32 target in eight cells (dual-source blending, stencil wrap, reversed depth, colour mask, a copied texture drawn indexed), an RGBA8 UINT target, R8 copies and same-state ordering, a device-local buffer, depth readback, three frames; seven of the cells' pipelines come from one `rhi_create_pipelines` call with an uncreatable description among them (only that one fails, the ids are distinct, the finished count never goes back); the dynamic state cell (`dynamicCell`: sixteen cells of the renderer's depth tests, Z writes, blends and masks drawn with every state static, from one `rhi_create_pipelines` batch with `maxThreads` 1 on one thread, then with the device's dynamic groups set after each bind: texel-equal, three commands a group after each static bind, none for a repeat; skipped but for the static pass without dynamic state) |
| `rhi_vk_test.c` | the common cells on Vulkan, a hazard-tracking cell, the render-area check (a pass wider than its colour target, one taller than its depth: each clamped and counted, no validation error; a fitting pass untouched), and the pipeline cache file (written while up by `rhi_pipeline_cache_save` and at shutdown, read back, another device's or garbage ignored; the drop of other builds' cache files in a temporary folder: the current file, its `.tmp` and unrelated names stay); ctest `rhi_vk`, and `rhi_vk_nodual`, `rhi_vk_d24s8`, `rhi_vk_mali` under the fake switches |
| `rhi_vk_swapchain_test.c` | the window path on SDL's offscreen driver: frames, readback order, surface release and recreation, suboptimal and lost presents, resizes, the surface poll, submits per frame, the present-wait stand-in and a hanging wait; ctest `rhi_vk_swapchain` |
| `rhi_vk_present_mode_test.c` | `vkr_choose_present_mode` over the mode sets drivers offer, and `vkr_present_wait_wanted`; ctest `rhi_vk_present_mode` |
| `rhi_vk_enum_test.c` | every `rhi.h` enumeration has a Vulkan mapping; the layer classifiers and `rhi_layer_switched_on`; ctest `rhi_vk_enum` |
| `rhi_driver_version_test.c` | the driver version text of each vendor's encoding; ctest `rhi_driver_version` |
| `rhi_d3d12_plan_test.c` | `d3d12_plan.c` on every host; with the shader table, every vertex shader's DXIL inputs against its SPIR-V locations and every DXIL blob signed; ctest `rhi_d3d12_plan` |
| `rhi_d3d12_test.c` | Windows, double-clickable: the cells on WARP, on the hardware adapter, the swapchain on a hidden window, then Vulkan for comparison; writes `rhi_d3d12_test.log` and shows a message box (`--console` for ctest); ctest `rhi_d3d12` |
| `compare_backends.cmd`, `compare_png.ps1` | Windows: every dump in `dumps\` rendered by `rd_replay_tool` on both backends, DISPLAY and the presented picture, each pair compared texel by texel and passing within 1 LSB (DISPLAY is expected identical) |
| `shaders/rhi_test.hlsl`, `gen_shaders.sh` | the tests' shaders, committed as `rhi_test_spv.h` and `rhi_test_dxil.h` so the tests build without DXC |

The Vulkan tests exit 77 (skipped) without a device; under the fetched validation layer `rhi_vk`
fails on any validation error (`port/rhi/CMakeLists.txt`; `port/rhi/test/*`, header comments).
`ctest --test-dir <build> -R '^rhi_'` runs them; [TESTING.md](TESTING.md#portrhitest) lists every
test of the tree.
