# The render hardware interface

The [RHI](ARCHITECTURE.md#terms) is the thin interface under [rd](ARCHITECTURE.md#terms) that the
Vulkan, Direct3D 12 and OpenGL backends implement; macOS and iOS run the Vulkan backend over
MoltenVK. This
document covers the interface, how several backends share one build, each backend's device,
swapchain and diagnostics switches, and the tests. The shaders are [SHADERS.md](SHADERS.md); how the
window paces and presents is [DISPLAY.md](DISPLAY.md).

## Files

| path | what it holds |
| --- | --- |
| `port/rhi/rhi.h` | the interface: handles, formats, resources, bind groups, pipelines, passes, commands, stats, timing, backend selection |
| `port/rhi/rhi_pipeline_pool.h` | the pipeline pool's size, which both backends and the renderer's cache read |
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
| `port/rhi/gl/rhi_gl.h` | OpenGL extras for tests only |
| `port/rhi/gl/gl_internal.h` | the OpenGL backend's shared state, its records and which file defines which entry point |
| `port/rhi/gl/gl_loader.h`, `gl_loader.c` | the OpenGL entry points, fetched through `SDL_GL_GetProcAddress` |
| `port/rhi/gl/gl_device.c` | the context, the version and extension checks, the debug output, limits, frames, timestamps, the stats entry points |
| `port/rhi/gl/gl_resource.c` | buffers, textures, samplers, separable programs, bind group layouts and bind groups, deferred deletes, readback |
| `port/rhi/gl/gl_swapchain.c` | the backbuffer texture, its blit to the window and the swap |
| `port/rhi/gl/gl_pipeline.c` | pipelines: program pipeline objects, vertex array objects, the fixed-function state; the enum maps |
| `port/rhi/gl/gl_cmd.c` | command lists, submission, barriers, render passes and the framebuffer cache, draws, copies, resolves |
| `port/rhi/gl/gl_glsl.h`, `gl_glsl.c` | the shader table's deflated GLSL inflated ([SHADERS.md](SHADERS.md)) |
| `port/rhi/d3d11/d3d11_shader.h`, `d3d11_shader.c` | the Direct3D 11 backend's run-time HLSL compile through `d3dcompiler_47.dll` and its DXBC cache file ([SHADERS.md](SHADERS.md#direct3d-11-run-time-compile)) |
| `port/rhi/d3d11/d3d11_internal.h`, `rhi_d3d11.h` | the Direct3D 11 backend's shared state and the contract between its files; its test extras |
| `port/rhi/d3d11/d3d11_device.c` | runtime loading, adapter pick, device, debug layer, limits, frame slots, submission, timestamps, the DXBC cache folder |
| `port/rhi/d3d11/d3d11_resource.c`, `d3d11_swapchain.c` | buffers with their upload shadows, textures, samplers, shaders, bind groups, readback; the DXGI flip-model swapchain |
| `port/rhi/d3d11/d3d11_pipeline.c` | the Direct3D 11 pipelines: shaders compiled on the workers, input layouts, the shared blend, depth-stencil and rasterizer state objects, the pool |
| `port/rhi/d3d11/d3d11_cmd.c` | the Direct3D 11 command path on the deferred contexts: passes, bindings, draws, copies, the upload ranges each list reads |
| `cmake/IcoMoltenVK.cmake` | the MoltenVK slice an Apple build links |
| `tools/fetch_moltenvk.sh` | MoltenVK and the Apple SDL3 build |
| `port/rhi/test/` | the backend tests and their shaders |

## The interface

`rhi.h` keeps the surface small: fourteen shader files, a reachable set of a few hundred to about
1150 pipelines (held under `RD_PIPELINE_REACHABLE_MAX`, 2048, `port/render/rd_state.h`), about twenty render
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

**Device and swapchain.** Each Vulkan swapchain creation that changes the mode, image count or size logs one `rhi_vk: swapchain` line (extent, images asked and got, present mode, surface format and colour space, composite alpha, pre-transform, the window's pixel size, SDL's video driver) and the first also a `rhi_vk: surface formats` line, so a driver-side mismatch reads from a log alone; the device report keeps the latest of the swapchain line. `rhi_init` takes an `SDL_Window` (NULL: headless, no swapchain),
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
Both backends fill the pipeline pool's fields ([The pipeline pool](#the-pipeline-pool)).
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

### The pipeline pool

Each backend keeps its pipelines in a handle pool of `RHI_PIPELINE_POOL_MAX` slots
(`port/rhi/rhi_pipeline_pool.h`; `port/rhi/vk/vk_device.c` and `port/rhi/d3d12/d3d12_device.c`
(`rhi_init`)), 4160: `RHI_PIPELINE_POOL_CACHE` (4096), the renderer's whole pipeline cache
(`port/render/rd_state.h` (`RD_PIPELINE_CACHE_MAX`) is that number), and
`RHI_PIPELINE_POOL_HEADROOM` (64) for what the renderer holds outside the cache at once: one batch
of the start-up set's or a replay's gathered keys (`RD_PIPE_BATCH`, 32: their ids are held until
the batch ends and its duplicates are destroyed), the COLCLAMP 0 pipelines (`RD_WRAP_PIPES`, 16)
and the movie's three (`port/render/rd_video.c`: two outputs, the field pipeline), 51 in all; a
static assert in `port/render/rd_pipeline.c` holds the sum under the headroom and the cache plus
the headroom to the pool. So no pipeline the renderer's cache can keep is refused by the pool. A
handle indexes `2^20 - 1` slots (`port/rhi/vk/vk_internal.h` (`VKR_INDEX_BITS`),
`port/rhi/d3d12/d3d12_plan.h` (`D3DP_INDEX_BITS`), 20 bits each, the 12 above them the
generation), far above the pool. A slot costs the pipeline's record and 7 bytes of bookkeeping (the
generation, the live flag, the free list's entry): 79 bytes on 64-bit Vulkan (`VkrPipeline`, 72),
95 on 64-bit D3D12 (`DxPipeline`, 88), so the pool is about 321 KB and 386 KB, allocated at
`rhi_init`.

A slot freed by `rhi_destroy_pipeline` is taken again before the pool's never-used part, so the
pool's first never-used index (`VkrPool.next`, `D3dpPool.next`) is also the most pipelines alive at
once. `RhiStats` reads the pool on both backends: `pipelinePoolSize`, `pipelinesLive` and
`pipelinesPeak` (gauges), `pipelinePoolFull` (creations the full pool refused, the pool's own
count), `pipelineFailures` (creations that failed otherwise: the driver's error, or a description
the backend refused with its own line) and `pipelineLastError` (the driver's error of the last such
failure that had one, a `VkResult` or an `HRESULT`; 0 when the last was the description's). The
renderer reads them around a creation to name the failure's reason in its line
([RENDER.md](RENDER.md#the-start-up-set), "The set against the pool"). A full pool's line is written the first 8 times
and then every 100th, with the running count (Vulkan `vkr_pool_alloc`, any pool; D3D12 at the
pipeline's allocation, `dx_pipe_finish`):

    rhi_vk: pipeline pool full (4160); 9 refused so far
    rhi_d3d12: pipeline pool full (4160); 100 refused so far

the 8th ending `; from here every 100th is written`. The window's 10 s `memory:` line ends with the
pool's numbers (`port/platform/window_host.c` (`perf_log`); [PLATFORM.md](PLATFORM.md#diagnostics)):
`; pipelines <live> alive (most <peak> of the pool's <size>), <f> creations failed (<p> for a
full pool)`, `<f>` counting both kinds. `rhi_vk_test`'s `poolCell` makes `RHI_PIPELINE_POOL_MAX`
pipelines on one device (lavapipe in the container), the first 1100 one at a time and the rest in
batches of 32, none refused, every id distinct, draws with the 1025th and the last and reads their
colours back, has one more refused and counted in `pipelinePoolFull`, and finds none alive after
they are destroyed ([Tests](#tests)).

### Dynamic state

A pipeline whose `RhiPipelineDesc.dynamic` has `RHI_DYN_DEPTH` leaves the depth test, write and
compare to the draw, one with `RHI_DYN_BLEND` the blend enable, the blend equation and the colour
write mask of its one colour target, where the limits say the device takes them
(`RhiLimits.dynamicDepth`, `dynamicBlend`; a bit the device lacks, or blend on a pipeline with other
than one colour target, is dropped and the description's value is static). The description's values
are the draw's until a set: `rhi_cmd_set_depth_state(cl, test, write, cmp)` and
`rhi_cmd_set_blend_state(cl, &blend)` after the bind give the next draws their own (a set on a
pipeline without that group does nothing). D3D12 has neither: its limits say false and the sets are
no-ops (`port/rhi/d3d12/d3d12_cmd.c`). On OpenGL both groups are plain state, so both limits are
true and `dynamicSets` counts what Vulkan would record ([OpenGL](#opengl)).

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

Each backend library is compiled with `RHI_BACKEND_PREFIX` set to its short name (`vk`, `d3d12`,
`gl`).
`rhi.h` then includes `rhi_backend_names.h`, whose macros rename every entry point the backend
defines to `rhi_<prefix>_*`, so the backend's sources keep the plain names. The backend registers
its functions once with `RHI_BACKEND_DEFINE` (`rhi_backend_vk` at the end of `vk_device.c`,
`rhi_backend_d3d12` at the end of `d3d12_device.c`, `rhi_backend_gl` at the end of `gl_device.c`); `RHI_BACKEND_FUNCS` lists every entry point in
table order and types each field from `rhi.h`'s declaration (`port/rhi/rhi_backend.h`, header
comment; `port/rhi/rhi_backend_names.h`). Callers never include `rhi_backend.h`.

`rhi_backend.c` defines the real `rhi_*` functions as forwarders to the selected table. The linked
backends are listed by `ICO_RHI_HAVE_VK`, `ICO_RHI_HAVE_D3D12` and `ICO_RHI_HAVE_GL`, Vulkan
first. `rhi_create_backend(name)` takes "vulkan" (or "vk"), "d3d12" or "opengl" (or "gl"),
case-insensitive; NULL or "" takes
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
(`ICO_RHI_D3D12`, on for 64-bit Windows), `ico_rhi_opengl` (`ICO_RHI_OPENGL`, on except for
Android and Apple targets, built where SDL3 is found; it compiles `d3d12/d3d12_plan.c` for the
handle pools and the layout rules and links `ico_miniz` for the GLSL) and `ico_rhi` (the
forwarders, linking the backends);
`ico_rhi_vk` is what `port/render` and the shader tests link. `ICO_RHI_MOLTENVK` (on for Apple
targets) links the Vulkan backend to MoltenVK instead of compiling volk (`cmake/IcoMoltenVK.cmake`).
Vulkan-Headers and volk come from `tools/fetch_deps.sh` (the backend loads the Vulkan library at run
time); without SDL3 the backends are headless-only. The D3D12 backend uses mingw-w64's own headers
and loads `d3d12.dll` and `dxgi.dll` at run time (`port/rhi/CMakeLists.txt`, header comment).

The Direct3D 11 backend (`ICO_RHI_D3D11`, on for 64-bit Windows; [Direct3D 11](#direct3d-11))
compiles its shaders at
run time: `RhiShaderDesc` brings it the HLSL file, the entry, the macros and the embedded sources
rather than a blob, and `d3d11_shader_compile` (`port/rhi/d3d11/d3d11_shader.c`) turns them into
DXBC with `d3dcompiler_47.dll`, cached on disk per shader. The route, the cache file, its key and
its log lines are in [SHADERS.md](SHADERS.md#direct3d-11-run-time-compile).

## Vulkan

**Device** (`port/rhi/vk/vk_device.c`). The loader must be Vulkan 1.1 or later; the instance asks
for the loader's version at most 1.3, and a device is used at its own version at most the
instance's (`vkr_effective_api`; an instance at 1.3 leaves a 1.4 device its 1.4 outside MoltenVK).
A device needs Vulkan 1.1 at that version, the swapchain extension on a window, a graphics queue
that can present, and RGBA8, RGBA8 UINT, R8, D32F and D32F_S8 or D24S8 formats; dynamic rendering
(core 1.3 or `VK_KHR_dynamic_rendering`) and timeline semaphores are not required (without them
`VkRenderPass` objects, [Render passes](#vulkan), and the fence ring,
[Bindings and synchronisation](#vulkan); `VKR_LEGACY_PASSES` 0 in `vk_internal.h` makes dynamic
rendering required again). Devices score discrete 1000,
integrated 500, virtual 200, CPU 100, plus 10 for core 1.3, 5 for dual-source blending, 20 for
dynamic rendering and 20 for timeline semaphores (`vkr_rate_device`). Below 1.2 the core 1.2 parts
are taken by their KHR names when offered: `VK_KHR_create_renderpass2` and
`VK_KHR_depth_stencil_resolve` (which `VK_KHR_dynamic_rendering` needs there),
`VK_KHR_timeline_semaphore`, `VK_KHR_driver_properties` (without it the driver is unnamed and its
`VkDriverId` 0) and `VK_KHR_image_format_list` (without it the UINT views' images are only mutable);
on a 1.1 device, or one without dynamic rendering, below 1.3 the first of
`VK_KHR_load_store_op_none`, `VK_EXT_load_store_op_none` and `VK_QCOM_render_pass_store_ops`
offered (`VkrState.storeOpNone`). Local read needs 1.3. The device line ends `; render passes
dynamic|legacy; sync timeline|fences` (and `; ICO_VK_FAKE_API=<profile>` under the switch). The
adapter line gives the PCI vendor and device ids (`device 10de:1b80`) and
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
`VkPhysicalDeviceVulkan14Features.dynamicRenderingLocalRead` (`vkr_effective_api`: under MoltenVK
the instance's at most, 1.3, so there that branch waits for the instance to ask for 1.4), and the
device line ends `; local read yes|no` (`yes (Vulkan 1.4)` for the core feature) (`port/rhi/vk/vk_device.c`
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
| `ICO_VK_FAKE_API` | `1.1` uses the device as a stock Vulkan 1.1 driver (Adreno 618 class): the instance and the device held to 1.1 (the KHR and EXT names of what is core later), and `VK_KHR_timeline_semaphore`, `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_driver_properties`, `VK_KHR_image_format_list`, `VK_KHR_load_store_op_none`, `VK_EXT_load_store_op_none` and `VK_QCOM_render_pass_store_ops` reported absent (`vkr_fake_hidden`): the fence ring ([Bindings and synchronisation](#vulkan)), no local read, the driver unnamed; `VK_KHR_dynamic_rendering` is hidden too (`VKR_LEGACY_PASSES`), so the passes are `VkRenderPass` objects ([Render passes](#vulkan)) and the device line says `render passes legacy`; the line `ICO_VK_FAKE_API=1.1: the device is used as a Vulkan 1.1 driver without timeline semaphores, dynamic rendering, local read, driver properties, image format list or store op none`. `1.1ext` holds the versions to 1.1 and hides nothing (a 1.1 driver with the extensions: the timeline through `VK_KHR_timeline_semaphore`, dynamic rendering through `VK_KHR_dynamic_rendering`), local read off (it needs 1.3). Both compose with `ICO_VK_FAKE_LIMITS=mali`; the device line ends `; ICO_VK_FAKE_API=<profile>`; another value is logged and hides nothing (`rhi_vk_vk11`, `rhi_vk_swapchain_vk11`, the render twins `rd_*_vk11` and `rd_pixel_vk11ext`, `rd_present_vk11ext`, label `vk11`: [TESTING.md](TESTING.md#the-render-suite-on-a-vulkan-11-driver)) |
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
recorded; every submit takes the next number of the port's counter (`VkrState.timelineValue`), which
`rhi_wait_frame` waits for; the first submit after an acquire waits on its semaphore and the present on
the image's `renderDone` (`port/rhi/vk/vk_pipeline.c`; `port/rhi/vk/vk_cmd.c`, header comment). The
submission sync behind the counter (`port/rhi/vk/vk_sync.c`, `VkrSyncKind`) is one timeline semaphore
whose value is the number (Vulkan 1.2), or on a device without timeline semaphores a ring of 8
fences, number n signalling the fence at slot n % 8: one queue signals in submission order, so the
newest fence found signalled gives the newest number finished exactly, and a submit whose slot still
holds an unfinished number waits for it first (`rhi_vk: submission sync: submission N waits for
submission M, the fence ring's 8 slots all in flight (logged once)`). The fence path starts with
`rhi_vk: submission sync: a ring of 8 fences (no timeline semaphores...)`; it leaves out the empty
present batch's wait for the previous number (the semaphore signal already covers every earlier
submission on the queue), and the timeline path's submits are as before, field for field. The
watchdog and the loss report read the counter on both paths (`vkr_sync_init`, `vkr_sync_submit`,
`vkr_sync_reached`, `vkr_sync_wait`; `rhi_vk_vk11`'s sync cell: 24 submissions three times around
the ring, the last clear read back, every one seen finished). Every buffer and
texture has its own `VkDeviceMemory` (`port/rhi/vk/vk_resource.c`, header comment). Each memory kind
tries an ordered list of memory types, and a type the driver refuses (a full heap) passes the
allocation to the next: uploads go to a device-local, host-visible type first, but one whose heap is
under 1 GB (the PCIe BAR window without resizable BAR) takes uploads of up to 16 MB only, and once it
refused an allocation it is tried last, so uploads go through system memory
(`port/rhi/vk/vk_memory.c`, `vkr_memory_candidates`).

**Render passes** (`port/rhi/vk/vk_renderpass.c`, header comment). `rhi_cmd_begin_render_pass`
describes the pass once (`VkrPassDesc`: per attachment the view, format, samples, the layout the
state tracking put it in, the load and store ops, the clear; the area and the framebuffer's size),
issues the hazard barriers from it and records it with `vkr_pass_begin`: with dynamic rendering the
`VkRenderingInfo` it always recorded, field for field; on a device without it
(`VkrState.legacyPasses`, the device line's `render passes legacy`) `vkCmdBeginRenderPass` with a
cached `VkRenderPass` (one subpass, no dependencies, every attachment's initial, subpass and final
layout the one it is already in, so the pass makes no transition and the barriers before it order
it; a pass without attachments a subpass without them) and a cached `VkFramebuffer`. A read-only
depth stores with `STORE_OP_NONE` at 1.3 or where `VK_KHR_load_store_op_none`,
`VK_EXT_load_store_op_none` or `VK_QCOM_render_pass_store_ops` gave it (`storeOpNone`), else with
`STORE` (dynamic rendering keeps its 1.3 check). A pipeline on that path names the render pass of
its compatibility class (formats and sample count, fixed ops and layouts; `vkr_renderpass_compat`,
looked up in `vkr_pipe_prepare` on the calling thread) instead of chaining
`VkPipelineRenderingCreateInfo`; the device chains no dynamic rendering feature and leaves
`cmdBeginRendering` unset. The caches: 128 render passes, never evicted (when full a pass is made
for one use and its destroy deferred: `rhi_vk: render pass cache full (128): further render passes
are made for one use`), and 256 framebuffers keyed by the compatible pass, the views and the size
(when full the least recently used is deferred: `rhi_vk: framebuffer cache full (256): ...`).
`rhi_destroy_texture` defers its view's framebuffers before the views (`VKR_GARBAGE_FRAMEBUFFER`;
a later view may reuse the handle), a swapchain image's release destroys them at once (the device
idle), and `vkr_release_all_objects` destroys both caches first. The first pass made logs `rhi_vk:
render passes: VkRenderPass objects (no dynamic rendering), the first with N colour target(s)...;
read-only depth stored with STORE_OP_NONE|STORE (no store op none)`; a pass whose objects could not be
made is not begun (`rhi_vk: render pass: no VkRenderPass or framebuffer for ...`).

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

## Direct3D 11

The Direct3D 11 backend (`port/rhi/d3d11/`, prefix `d3d11`, `rhi_backend_d3d11`) is for cards and
drivers that have neither Vulkan nor Direct3D 12. It compiles in the `win-x64` and `win-x64-clang`
cross builds. CI's `windows` job runs the renderer's tests on it through WARP, Microsoft's software
adapter, with the debug layer on ([TESTING.md](TESTING.md#the-render-suite-on-direct3d-11-warp));
all of them pass there with no debug-layer error, and the rules the debug layer named are in the
**Debug layer rules** paragraph below. No hardware device has been seen drawing with it (issue 126:
on a GTX 750 Ti at feature level 11_0 both start-up self-tests read back their cleared target and
the screen stayed black; the cause found on WARP, the uniform updates the runtime dropped, is fixed
but not confirmed on that card). The fog probe's log line reads `z=n/a idx=n/a` here: a depth
texture is copied only into a readback buffer, so the probe has no depth texels, and
`rd_fog_selftest.cmake` accepts that only when the log has `rhi_d3d11:` lines.
`port/rhi/d3d11/d3d11_internal.h` holds the shared state and states which file provides what.

**Device** (`port/rhi/d3d11/d3d11_device.c`). `d3d11.dll` and `dxgi.dll` are loaded at run time and
an injector beside the program is named as on D3D12. Adapters are enumerated discrete first; one is
suitable when `D3D11CreateDevice` takes it at feature level 11_1 or 11_0 (a list with 11_1 that the
runtime answers `E_INVALIDARG` is tried again without it); with none, or with
`ICO_D3D11_ADAPTER=warp`, the device is made on `D3D_DRIVER_TYPE_WARP` (`d11_pick_adapter`,
`d11_create_device`). The device needs the Direct3D 11.1 interfaces (`ID3D11Device1`,
`ID3D11DeviceContext1`: Windows 8 and later) and `ConstantBufferOffsetting`; without them `rhi_init`
fails with `rhi_d3d11: Direct3D 11.1 is needed (...)` (`d11_require_11_1`). Each adapter logs
`rhi_d3d11: adapter N: <name>` as on D3D12, and the device line is

`rhi_d3d11: <adapter> (D3D11, feature level 11_1|11_0[, WARP][; driver version a.b.c.d]); 11.1
interfaces yes, partial constant buffer updates yes|no, command lists by the driver|emulated by the
runtime; tearing yes|no|n/a; debug layer on|off|asked for, not installed; shader cache <dir>|none[;
headless]`

| variable | effect |
| --- | --- |
| `ICO_D3D11_DEBUG` | 1 the debug layer (`D3D11_CREATE_DEVICE_DEBUG`, its info queue drained into the log as `rhi_d3d11 debug <severity> (id N): <text>`) and the DXGI debug factory; 0 off |
| `ICO_D3D11_ADAPTER` | `warp`, an adapter index or a substring of its name |
| `ICO_DX_FAKE_NO_TIMESTAMPS` | 1 turns GPU timestamps off, as on D3D12 |
| `ICO_D3D11_FRAME_LATENCY` | the swapchain's frame latency, as `ICO_D3D12_FRAME_LATENCY` |

A failed call logs `rhi_d3d11: <call> failed (HRESULT 0x...) at <file>:<line>`; a lost device is
logged once with its removed reason and the last eight submissions (`d11_check`).

**Limits** (`d11_fill_limits`). As D3D12's except: `copyRowPitchAlign` 1 and `copyOffsetAlign` 4
(uploads to textures are `UpdateSubresource` from the CPU copy), `maxDynamicUniforms` 4, `rov`
false (Direct3D 11.3 has rasterizer-ordered views; they stay off, untested), and
`colorBytesPerPixel` 32, which keeps the VU vertex pre-pass off as on the Apple A9 and A10
(`port/render/rd_replay_vu.c`): its record goes back through a texture-to-buffer copy Direct3D 11
does not have. The sample counts come from `CheckMultisampleQualityLevels` over RGBA8 UNORM and UINT
and D32S8, logged as `rhi_d3d11: multisampling: sample counts 0x..`.

**Frames** (`d3d11_device.c`). Each frame slot has eight command lists, each a deferred context made
on first use and kept. `rhi_begin_commands` hands out the next one; `rhi_end_commands` calls
`FinishCommandList` (the deferred context's state is cleared); `rhi_submit` first writes the upload
ranges the list read (below), then `ExecuteCommandList` on the immediate context in submit order,
releases the executed `ID3D11CommandList` at once (a list kept alive still references the swapchain
buffer it drew into, and `ResizeBuffers` returns `DXGI_ERROR_INVALID_CALL` while one is; the
deferred context is kept), and marks the slot's `D3D11_QUERY_EVENT` pending. `rhi_wait_frame` ends
the event of the slot it leaves once, after all of that slot's work (a query is read before it is
ended again, rule 4 below), moves to the next slot and polls its event
(`GetData`, which also flushes; `Sleep(0)`, then `SwitchToThread`, between polls), then releases the
slot's unsubmitted command lists, garbage and bind groups (`d11_recycle_frame`). `rhi_wait_idle` ends an event
of its own and waits for it. A wait past 5 s logs
`rhi_d3d11: a wait for the GPU has taken over 5 s; still waiting (once)`.

**Upload buffers** (`d3d11_resource.c`). `rhi_map_buffer` of an `RHI_MEM_UPLOAD` buffer hands back
a CPU copy (the shadow) whose address never changes, as the renderer keeps the pointer. A buffer is
up to three D3D11 buffers, one per bind class its usage asks for (vertex and index; constant, which
Direct3D 11 binds alone; structured shader resource, which cannot carry the vertex or index flag),
all `D3D11_USAGE_DEFAULT`. The command path notes each range a list reads from an upload buffer,
per class (`d11_cmd_touch`: bind group uniforms and storage ranges, vertex and index ranges with
their draws' extents, copy sources); `rhi_submit` writes each noted range from the shadow into that
class's buffer with `UpdateSubresource1` before the list executes (`d11_buffer_flush`), so a list
sees what the CPU wrote before its submit. Constant buffers take a partial update with
`D3D11_COPY_NO_OVERWRITE` where the driver offers `ConstantBufferPartialUpdate`, else the whole
buffer (rule 1 below).

The cost of those writes: the renderer's ring is
VERTEX|INDEX|UNIFORM|STORAGE upload memory (`rd_upload.c` `newRing`), so it is three D3D11 buffers.
Each list's notes are one span per buffer and class, the lowest to the highest byte it read. The
VU draws' bind group (`rd_replay_vu.c` `vuGroup`, cached per buffer) binds the ring as storage from
byte 0 to its whole size (or `maxStorageRange`) and passes the stream's start in a uniform, so the
storage class's note is the whole bound range and every `rhi_submit` with a VU draw writes the
ring's full capacity (4 MB at first, grown and never shrunk; hundreds of MB after a stage change
with many pack replacements) into the structured buffer, plus the uniform and index spans. The
vertex class is written only for the sprite pipelines (the only ones with vertex bindings), from
each draw's first to last vertex; the indexed draws are the VU draws, which have no vertex
bindings, so their "binding offset to the buffer end" rule (`d11_flush_vertex_buffers`) does not
come into play today. No narrower range is known inside the RHI without the renderer saying which
bytes a frame wrote (a ring high-water mark per frame slot, or the VU group bound over the stream
only); that is the fix if the copies show up on the old cards this backend is for, and it is not
measured: Direct3D 11 has run only on WARP. Uniform bindings are windows into the
constant buffer (`VSSetConstantBuffers1` and `PSSetConstantBuffers1`, first constant = offset / 16,
whole 256-byte blocks); storage bindings are structured SRVs of 16-byte elements, eight kept per
buffer. `RHI_MEM_DEVICE` buffers are filled by copies; an `RHI_MEM_READBACK` buffer is a staging
buffer plus the shadow, refreshed at `rhi_map_buffer`, and a `rhi_cmd_copy_texture_to_buffer` into
one goes through a staging texture whose rows are written into the shadow at the map
(`d11_readback_texture_target`). Textures are typeless with typed views as on D3D12, each with its
RTV, writable and read-only DSV and SRV made at creation. Shaders are recorded at
`rhi_create_shader` and compiled the first time a pipeline needs them (`d11_shader_compiled`, on the
pipeline workers, once per shader).

**Pipelines** (`d3d11_pipeline.c`). A pipeline is the vertex and pixel shader, the input layout,
three state objects, the topology and the strides. `rhi_create_pipelines` works in the D3D12
backend's three steps: on the calling thread the checks and the state objects; on the Win32 workers
(`RHI_PIPELINE_THREADS_MAX`, `maxThreads` obeyed, `CreateThread` taken from kernel32 by address as
on D3D12) the shaders (`d11_shader_compiled`: `d3d11_shader_compile` through the DXBC cache, then
`CreateVertexShader` or `CreatePixelShader`, once per shader, a failure kept and not retried) and
`CreateInputLayout` from the vertex shader's DXBC, its RHI locations matched to the input
signature's elements by register order (`d3dp_location_element`, as on D3D12); on the calling thread
again the pool slot. The blend (independent per target, the dual-source `SRC1` factors on target 0,
`*_COLOR` factors in the alpha equation replaced by their alpha twins as on D3D12, blending off on
an integer target), depth-stencil (the caller's compare: reversed depth is `GEQUAL`; stencil only
on a format with stencil) and rasterizer (no culling, depth clip and scissor on, multisampling per
the pipeline's sample count) objects are shared: a table per kind keyed by the description's bytes
holds one reference each until `rhi_shutdown` (`d11_pipelines_release`); Direct3D 11 keeps at most
4096 of a kind, and past that a pipeline is not created
(`rhi_d3d11: pipeline: 4095 distinct <kind> states, the most Direct3D 11 keeps; ...`, once). The
pool is `RHI_PIPELINE_POOL_MAX` with the other backends' line
(`rhi_d3d11: pipeline pool full (4160); N refused so far`, the first 8 then every 100th); a
pipeline whose shader failed logs `rhi_d3d11: pipeline <name> not created: vertex|pixel shader
<name>: <reason> (N so far)` on the same schedule and counts in `pipelineFailures`
(`pipelineLastError` the `CreateInputLayout` HRESULT where that failed, else 0). A destroyed
pipeline's shader and layout references are released with its frame slot.

**Command path** (`d3d11_cmd.c`). Every `rhi_cmd_*` call records on the list's deferred context;
the deferred context starts each recording with nothing bound. Barriers record nothing (Direct3D 11
orders every read after every write itself). A render pass unbinds every SRV the list bound
(a texture must not be a shader resource and a target at once), sets the RTVs and the writable or
read-only DSV, clears (`ClearRenderTargetView`, integer targets with the integer value as on D3D12;
`ClearDepthStencilView` unscissored) or discards (`DiscardView` for a `DONT_CARE` load: a colour
target, or a depth target whose aspects are both `DONT_CARE`, only when the pass covers the whole
texture, since `DiscardView` drops the whole view), and sets the viewport and scissor to
the pass (clamped to the smallest attachment, logged once, as on D3D12); its end unbinds the
targets so a later pass can sample them. `rhi_cmd_set_pipeline` sets the input layout, topology,
shaders and the three state objects, the blend constant and stencil reference with them;
`rhi_cmd_set_stencil_ref` and `rhi_cmd_set_blend_constant` set the bound pipeline's object again
with the new value. At a draw each dirty bind group of the pipeline's layouts binds its entries to
their own registers in the stages the slot names (both when it names none): uniforms as constant
buffer windows (`VSSetConstantBuffers1`/`PSSetConstantBuffers1`, first constant = (offset +
dynamic offset) / 16, the window whole 256-byte blocks; `d11_set_cb`: a window equal to the one the
register holds is not set again, and the same buffer with another window is unbound first, the
workaround Microsoft documents in the `VSSetConstantBuffers1` remarks for a runtime that drops a
call naming the bound buffer with only its offset or size changed; every draw's uniforms are
windows of the one ring buffer), storage buffers and textures as SRVs,
samplers (the point/clamp default for an empty slot); the destination read's storage texture is
never bound (`rov` false). `rhi_cmd_set_depth_state` and `rhi_cmd_set_blend_state` log once and do
nothing (`dynamicDepth`/`dynamicBlend` false). Every range a list reads from an upload buffer is
noted with `d11_cmd_touch` as it is recorded: the uniform window and storage range of each bound
entry, the vertex range of each draw (`[first, first + count)` vertices of a per-vertex binding,
the instances of a per-instance one; an indexed draw's from the binding's offset to the buffer's
end, the indices being unknown there), the index range of each indexed draw, and the source of a
buffer copy. Copies: `rhi_cmd_copy_buffer` is `CopySubresourceRegion` into every part of a device
buffer (the structured part in whole 16-byte elements, rule 2 below), or into a readback buffer's
staging buffer; `rhi_cmd_copy_buffer_to_texture` (from an
upload buffer only, logged once otherwise) reads the shadow when it is recorded, with
`UpdateSubresource1` and a box (BC regions rounded up to whole blocks as on D3D12), except where the
driver leaves command lists to the runtime (`command lists emulated by the runtime` in the device
line), whose deferred `UpdateSubresource` applies a box's offset to the source too: there the rows
become a texture of their own (created with them as its contents) copied in on the GPU;
`rhi_cmd_copy_texture` is `CopySubresourceRegion` (depth formats whole only);
`rhi_cmd_resolve_color` is `ResolveSubresource`; `rhi_cmd_copy_texture_to_buffer` goes into a
readback buffer through its staging texture and is skipped otherwise
(`rhi_d3d11: rhi_cmd_copy_texture_to_buffer: only into a readback buffer on Direct3D 11; skipped
(logged once)`: its other callers are the vertex pre-pass (`rd_replay_vu.c`), off on Direct3D 11,
the fog paths (`rd_fog_path.c`: the buffer path's depth copy, the fog probe and the edge probe),
all gated off on Direct3D 11, and `rd_crack_fill.c`'s count copy, which goes into a readback
buffer and so is served).

A pass whose colour target and depth target differ in size logs `rhi_d3d11: render pass: colour
target WxH with a depth target WxH: Direct3D 11 binds a depth-stencil view only with targets of its
size (the first 8 reported)`: Vulkan and D3D12 draw where both are, while Direct3D 11's
`OMSetRenderTargets` refuses a depth-stencil view whose dimensions are not the targets' (the debug
layer names it), which would leave the pass with no target; the line is a diagnostic, nothing
changes. Each draw keeps a copy of its state (viewport, scissor, target and depth sizes and format,
topology, whether a pixel shader and an input layout are bound, the vertex buffers, the b0..b3
windows per stage, the pipeline's state objects), made into one line only when asked:
`RhiStats.lastDrawText` (a function, NULL on the other backends), which the blend and fog
self-tests append when they fail (`rd: blend: self-test failed on Direct3D 11; its last draw:
draw N (plain|indexed, V x I): viewport x,y wxh depth a..b; scissor l,t..r,b; target WxH format F,
depth WxH[ read-only]; topology T, pixel shader yes|none, input layout yes|none (vertex buffers
0x.. bound 0x..); blend on|off S/D op O, mask 0x.; depth on|off write W func F, stencil on|off ref
R; cull C, scissor test on|off, depth clip on|off; constants vertex b0 N@F ..., pixel ...; feature
level 11_1|11_0, constant buffer offsets yes, partial updates yes|no, command lists by the
driver|emulated by the runtime`, and the same after `rd: fog: self-test failed on Direct3D 11`).

**Swapchain** (`d3d11_swapchain.c`). As D3D12's (three BGRA8 buffers, the frame latency waitable
object, tearing without vsync, the frame statistics, the resize at a present after the window's
size changed, the same `rhi_present_mode_name` rule), with `FLIP_SEQUENTIAL` where `FLIP_DISCARD`
is refused (Windows 8 and 8.1), logged as `rhi_d3d11: FLIP_DISCARD refused (HRESULT 0x...):
FLIP_SEQUENTIAL`; where that is refused too while it asks for the frame latency waitable object
(a Windows 8.1 flag, so on Windows 8.0), the swapchain is made without it (`rhi_d3d11: FLIP_SEQUENTIAL with the frame
latency waitable object refused (HRESULT 0x...): without it`) and Present waits for the display. Under the flip model buffer 0 is always the one to draw, so it is the one RHI
texture; its RTV is made again after every `ResizeBuffers`. Creation logs
`rhi_d3d11: swapchain WxH, 3 buffers, flip discard|flip sequential, frame latency N (waitable)` or
`..., no waitable object`.

**The shader cache folder** (`d3d11_device.c`, `d11_cache_setup`). The window's pipeline cache path
`<pref>/pipelines-<build>.vkcache` becomes the folder `<pref>/pipelines-<build>.d3d11`, one DXBC
file per shader, keyed with `<build>` among the rest ([SHADERS.md](SHADERS.md#direct3d-11-run-time-compile));
another build's `pipelines-*.d3d11` folder is removed at `rhi_init` with
`rhi_d3d11: shader cache: dropped <name> (another version of the game)`. Any other path (a test's)
gets `<path>.d3d11`, nothing dropped. A folder that cannot be made logs
`rhi_d3d11: shader cache folder <dir> could not be made: the shaders are compiled at every start`.
`rhi_pipeline_cache_loaded` is true once a shader came from the folder, `rhi_pipeline_cache_save`
logs the counts (`rhi_d3d11: shader cache <dir>: N hits, M compiles, K failed`; also at the first
`rhi_wait_idle` after a compile, and at `rhi_shutdown`), `rhi_pipeline_cache_size` is the folder's
DXBC bytes.

**Timestamps**. Per frame slot a `TIMESTAMP_DISJOINT` query begun at the slot's first submit and
ended when `rhi_wait_frame` leaves it, around 256 `TIMESTAMP` queries the lists end; read when the
slot comes round again, every query the slot ended, also when the stamps are dropped (rule 4
below). A disjoint frame's stamps are dropped (`rhi_d3d11: GPU timestamps: a frame's clock was
disjoint ...`, once).

**Debug layer rules**. What the debug layer requires of this backend, each rule with the message
id it reports when broken (`rhi_d3d11 debug <severity> (id N)` in the log). Where the debug layer
reports an error, the runtime drops the call, with or without the debug layer, so a broken rule
draws less than it should and nothing else says so.

1. A partial update of a constant buffer (`UpdateSubresource1` with a box) passes
   `D3D11_COPY_NO_OVERWRITE` or `D3D11_COPY_DISCARD`, and only on a device that reports
   `ConstantBufferPartialUpdate` (error 288). Without the flag every uniform window the renderer
   writes is dropped: the shaders read zeros, nothing reaches the targets, and the start-up
   self-tests read back their clear colour. `d11_buffer_flush` passes `NO_OVERWRITE`, which is the
   upload buffers' contract (the CPU writes a range no submitted list still reads, as Vulkan's
   mapped memory requires); `DISCARD` would drop the rest of the buffer. The other parts take no
   flag.
2. A copy into a structured buffer covers whole elements of its stride, 16 bytes here (error 280,
   "the width of the source box is not a multiple of the destination resource structure stride").
   The mesh arena's copies (`rd_upload.c`) end at a mesh's last index, on any 4-byte boundary. The
   structured part takes the 16-byte-aligned range around the copy from a part of the same buffer
   the copy has just written (`d11_copy_into_structured`; the parts hold the same bytes, so the
   bytes around the range stay the buffer's own); a storage-only buffer takes the aligned range
   from the source and logs `rhi_d3d11: rhi_cmd_copy_buffer: N bytes into M of a storage-only
   buffer cover part of a 16-byte element: the whole elements are copied from the source (logged
   once)`.
3. A pixel shader that writes `SV_Target1` with one target bound (warning 3146081, "The Pixel Shader
   expects a Render Target View bound to slot 1, but none is bound") is valid: the second output is
   discarded. The renderer's dual-output shaders (`common.hlsli` `DualOut`) are used with blending
   that does not read `SRC1` as well (blending off, the premultiplied and destination-alpha forms);
   with the `SRC1` factors the blend state consumes the output and the warning is not reported
   (`rhi_d3d11_test`'s dual-source cells draw without it). The warning stays.
4. A query is read with `GetData` (an `S_OK` answer) before it is begun or ended again (warnings 408
   and 410; valid, the earlier results are abandoned). The slot's event is ended once per slot use,
   at `rhi_wait_frame`, and polled when the slot comes round; every timestamp query and the disjoint
   query a slot ended are read when it comes round, with `D3D11_ASYNC_GETDATA_DONOTFLUSH` for the
   stamps (the slot's event has passed, so nothing waits).

**Not implemented on Direct3D 11**: the destination read, the VU vertex pre-pass, the
`gpu_checkpoints` breadcrumbs, mailbox with vsync, the display's timing of presents, and the
memory gauges.

**Unverified**, all of it, and these first: the upload ranges each list notes (a range left out
draws stale data), the constant buffer windows' offsets, the order of the deferred lists against
the uploads at submit, `UpdateSubresource1` with a box on a deferred context of a driver that
records command lists itself and the texture-and-copy route where it does not,
`CopySubresourceRegion` into a constant buffer part of a device buffer, FXC's acceptance of every
game entry under `vs_5_0`/`ps_5_0`, the input layouts against FXC's input signatures, the
flip-model resize, the first `rhi_init` on a real card. CI's `windows` job (WARP, the debug layer
where the runner can add it) is the first evidence on any of these; its logs are the artifact
`d3d11-warp-logs`. On a Windows machine, `rhi_d3d11_test.exe` (below, [Tests](#tests)) is the first
thing to run, and the game with `ICO_D3D11_DEBUG=1` once the Graphics Tools optional feature is
installed (the debug layer's first error goes to the log).

**Test extras** (`port/rhi/d3d11/rhi_d3d11.h`): the debug layer's error count, whether it is active,
whether the device runs on WARP, and the feature level.

## OpenGL

OpenGL 4.3 core with `ARB_clip_control` and `ARB_buffer_storage` (both core in 4.5), on Windows and
Linux. The backend links no GL library: SDL3 makes the context and `SDL_GL_GetProcAddress` gives
every entry point (`port/rhi/gl/gl_loader.h` `GL_FUNCS`: 139, the 4.3 core ones required, clip
control, buffer storage and texture barrier optional), called as `glf.Name` (not `gl`, the
backend's `RHI_BACKEND_PREFIX`).

**Context** (`port/rhi/gl/gl_device.c` (`gl_create_context`)). With a window, the window must have
been made with `SDL_WINDOW_OPENGL`, else `rhi_init` fails with
`rhi_gl: the window was made without SDL_WINDOW_OPENGL; no OpenGL context on it`. Headless
(`sdlWindow` NULL), the backend starts SDL's video when nothing has (the `offscreen` driver as a
hint, so `SDL_VIDEO_DRIVER` still picks another; it makes its contexts through EGL without a
display) and makes a hidden 1x1 `SDL_WINDOW_OPENGL` window of its own: double-buffered, no depth or
stencil, no sRGB. Either way the context asks for 4.3 core (a driver gives its newest compatible
version) with the debug flag under `debugLayers`. A failure logs `rhi_gl: no OpenGL 4.3 core
context (<SDL's error>)`. The checks then refuse a driver below 4.3, or without clip control or
buffer storage, with `rhi_gl: OpenGL 4.3 with ARB_clip_control is needed; this driver offers
<GL_VERSION> (<GL_RENDERER>)` (or `... with ARB_buffer_storage ...`), and an entry point missing
from a 4.3 driver with `rhi_gl: the driver has no glName (OpenGL 4.3 core)`. The device line is
`rhi_gl: <renderer> (OpenGL M.m core|compatibility, <GL_VERSION>; GLSL <version>)[, headless];
index values up to N`, followed by `rhi_gl: limits: uniform align N, dual-source yes|no, MSAA
1|2|4|8, interlock yes|no (image units N), texture barrier yes|no, BC yes|no, anisotropy N, max
texture N, entry points 139 (N optional missing), debug output on|off, clip origin lower left (vertex
y negated)`.

**Clip space** (`gl_defaults`). `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` with every vertex
program's `gl_Position.y` negated after its `main` (`gl_resource.c` `gl_flip_vertex_y`: a
`#define main ico_gl_vertex_main` after the `#version` line and a `main` of the backend's own at the
end, so every return is covered) gives OpenGL the renderer's D3D-style conventions: depth 0 to 1
(reversed-Z GEQUAL as on the other backends), row 0 of every target and texture at the top,
`gl_FragCoord` from the top left, viewports and scissors in the same coordinates. The viewports,
scissors, uploads and readbacks pass through unflipped. `GL_UPPER_LEFT` would give the same rows
without the negation, but not the same fill rule: an edge through pixel centres is a tie (every GS
sprite and screen edge is one, the GS sampling rules in [RENDER.md](RENDER.md)), and Mesa's
drivers (llvmpipe, radeonsi, iris) flip their rule under `GL_UPPER_LEFT`, so a framebuffer object
takes the bottom edge's row and leaves the top edge's: every sprite one row low, its first row left
at what was there (`rd_tex_gl` "PSMT8 sprite: 16 pixels differ"). At `GL_LOWER_LEFT` a framebuffer
object keeps Direct3D's and Vulkan's top-left rule in memory rows, which the common cells'
fill-rule cell checks on every backend. OpenGL leaves the rule to the driver, so another vendor's
driver may still differ; the limits line ends `clip origin lower left (vertex y negated)`. Only the
window's framebuffer 0 has its row 0 at the bottom, so the swapchain is a texture blitted upside
down at present (below). The other fixed state: no dithering, no
`GL_FRAMEBUFFER_SRGB`, the vertex shader's point size, no culling, no depth clamp, no primitive
restart, the first vertex's flat attributes (`glProvokingVertex(GL_FIRST_VERTEX_CONVENTION)`, the
D3D and Vulkan rule), the scissor test on, pixel rows packed at 1 byte.

**Bindings** (`gl_internal.h` `gl_bind_point`; the GLSL's side in [SHADERS.md](SHADERS.md)): uniform
block `b` at binding `b`; storage buffer `t` at `16 + t`; sampled texture `t` at texture unit
`16 + t`; storage image `u` at image unit `u` (the translation step takes the SPIR-V's 48 off: OpenGL 4.3 guarantees only 8 image units). GLSL has combined samplers only, so a bind
group's sampler goes to the unit of each texture it samples: the group's sampler slot with the
texture's number, else its lowest one. Integer textures, the RGBA8_UINT view and multisampled
textures get no sampler object (OpenGL filters none of them; an integer texture under a filtering
sampler is incomplete and reads zeros): every texture carries point sampling, clamping and its
level range as its own state. A bind group is a run of entries in its frame slot's arena, each ready
for one bind (`GlBindEntry`); a uniform or storage slot without a usable binding gets a 64 KiB zeroed
buffer, as D3D12 binds a null view, after one line a binding type:
`rhi_gl: bind group: slot N (type T): <why> (reported once a type)` (`gl_resource.c`
(`gl_make_entry`)). Dynamic uniforms are `glBindBufferRange` at the base plus the bind-time offset;
`uniformAlign` is `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`, at least 16.

**Resources** (`port/rhi/gl/gl_resource.c`). Buffers are immutable storage: UPLOAD mapped for
writing and READBACK for reading, both persistent and coherent, once at creation (the stable
pointer of `rhi_map_buffer`); DEVICE buffers are written only by copies. Textures are immutable
storage in every RHI format (`gl_format`): D32F_S8 is `GL_DEPTH32F_STENCIL8` ("D32S8"), BC1, BC2 and
BC3 the S3TC formats and BC7 `GL_COMPRESSED_RGBA_BPTC_UNORM`, BGRA8 stored as RGBA8 and moved as
`GL_BGRA`; an `RHI_TEX_UINT_VIEW` texture gets its `GL_RGBA8UI` view (`glTextureView`) at creation.
A shader is a separable program of its stage (`glCreateShaderProgramv`), so a pipeline pairs two
programs in a program pipeline object without a link of its own; the GLSL is the table's deflated
text inflated (`rhi_gl_inflate_glsl`), or plain text starting `#version` with `textSize` 0 (the RHI
tests). A vertex program without its own `gl_PerVertex` block gets one (`gl_Position`,
`gl_PointSize`), which strict drivers want for separable programs. A failed compile logs
`rhi_gl: shader <name>: <the compiler's log>` (the first 8, then every 100th) and returns a null
handle. Destroys are deferred to the frame slot's recycle: OpenGL keeps an object alive for the
commands queued with it but may hand its name out again at once, and a bind group made earlier in
the frame holds names. `rhi_readback_texture` waits with `glFinish` and reads level 0 through a
scratch framebuffer with `glReadPixels` (depth as 32-bit floats), the rows from the top.

**Frames** (`gl_device.c`). Command lists execute while they are recorded; `rhi_submit` places the
slot's fence (`gl_frame_fence`: `glFenceSync`, then `glFlush`). `rhi_wait_frame` moves to the next
slot, waits for its fence (`glClientWaitSync`, counted into `fenceWaits` and `fenceWaitNs` when it
blocks; after 2 s it logs `rhi_gl: a frame's fence has not signalled after 2 s; still waiting`),
then runs the slot's deletes, resets its bind group arena and reads its timestamps. A
`glFenceSync` that fails (`rhi_gl: glFenceSync failed (N so far): the frame slot waits with
glFinish`) leaves the slot without a fence; unless a later submit of the slot places one, its
recycle waits with `glFinish` (`GlFrame.needFinish`). GPU timestamps
are `glQueryCounter(GL_TIMESTAMP)` on a query object per index and slot, made at its first use,
read once the fence has passed (`rhi_gl: GPU timestamps: on (glQueryCounter, 256 per frame slot;
in the replays timed pass by pass each stamp's issue time is read ...)`). `rhi_wait_idle` is
`glFinish`.

**The busy clock** (`rhi_read_timestamps_busy`, `gl_device.c` `gl_collect_busy`). A list executes
while it is recorded, so when the walk is slower than the GPU the GPU idles between the commands
and the span between two stamps counts that wait. A `GL_TIME_ELAPSED` query would read the same span (ARB_timer_query: its timer starts and stops
when the earlier commands have completed). A stamp written with `RHI_TIMESTAMP_ISSUE_TIME` also
reads `glGetInteger64v(GL_TIMESTAMP)`, the GL time once the stamp and everything before it were
handed over (the extension's own latency example); the slot's clock starts at its first stamp
and each stretch from stamp a to stamp b adds `max(0, ts[b] - max(ts[a], issued[b]))`, the span
less the part before the stretch's last command arrived. That is a lower bound of the GPU's own
time: work it ran while the CPU was still handing the stretch over does not count, so on a GPU
that keeps up with a slow walk the busy time reads low. The get may hold the CPU until a threaded
driver has taken the commands before it, so the renderer asks for it only in the replays it
times pass by pass (one every 250 ms, `rd_perf.c`); a slot with a stamp without the flag has no
clock. The renderer keeps the timed replay's busy time beside its GPU time
(`RdPerfRecord.gpuBusyMs`) and each piece's (`RdPerfPass.gpuBusyUs`); the window's 10 s line
`window: GPU top passes:` gives each entry's `N ms (busy N)` and ends `...; GPU busy N ms, span N
ms a timed replay)`, the pieces' busy time and the span of the same pieces. Vulkan, Direct3D 12
and Direct3D 11 read no clock (their lines are unchanged). `rhi_gl_test`'s busy cell checks the
clock against the timestamps, `rd_perf_gl` each timed record and piece.

**Pipelines** (`port/rhi/gl/gl_pipeline.c`). A pipeline is a program pipeline object holding the
two separable programs (`glUseProgramStages`), a vertex array object holding the vertex layout
(`glVertexAttribFormat`, `glVertexAttribIFormat` for the integer formats, `glVertexAttribBinding`,
`glVertexBindingDivisor` for per-instance bindings: `ARB_vertex_attrib_binding`, core in 4.3; the
attribute's location is the RHI's, which is the GLSL's `layout(location = N)`), the strides per
binding, and the fixed-function state kept in the RHI's terms for the draws. OpenGL compiles
nothing at this point (the programs are linked; a driver may still specialise at the first draw),
so `rhi_create_pipelines` makes its batch on the calling thread (`threads` 1), calling the wait
function about every `RHI_PIPELINE_WAIT_MS`. Mesa's llvmpipe compiles a shader variant for each
program and state at its first draw and keeps it in Mesa's disk cache (`MESA_SHADER_CACHE_DIR`), so
on a cold cache the first replays that meet new variants log `rd: slow walk N ms ... the rest N ms`
(a first run on this machine logged 22 to 30 s replays; `rd_gsbase_test` whole takes about
20 s with an empty cache and 2 to 6 s warm on a shared 4-core machine, Vulkan about 2 s). The walk
times of llvmpipe's OpenGL also hold its vertex shading and binning, which run inside the draw
calls (on lavapipe the same work lands in the frame's fence wait): `rd_perf_gl`'s steady replay is
about 22 ms of walk and no wait, Vulkan's about 0.4 ms of walk and 47 ms of wait. The refusals and
their lines are the other backends':
`rhi_gl: pipeline <name>: invalid description`, `... destination read N not available` (the
attachment read always; the storage image without `fragmentInterlock` or with a colour target),
`... no vertex shader`, and `... dual-source blend factors without dual-source blending`; a GL
error at creation gives `rhi_gl: pipeline <name> not created: GL error 0x....` with the error in
`pipelineLastError`; a full pool `rhi_gl: pipeline pool full (4160); N refused so far` (the first 8,
then every 100th). A vertex shader input that no attribute feeds (OpenGL would read (0, 0, 0, 1))
is logged, not refused: `rhi_gl: pipeline <name>: the vertex shader reads location N, which no
vertex attribute feeds (N so far)` (the first 8, then every 100th). As on Vulkan, `RHI_DYN_DEPTH`
stays only with a depth target and `RHI_DYN_BLEND` only with one colour target. `rhi_destroy_pipeline`
defers the deletes of both objects.

**Command lists** (`port/rhi/gl/gl_cmd.c`). A list executes while it is recorded: every
`rhi_cmd_*` call makes its OpenGL calls at once, which the RHI's rule that lists are recorded and
submitted in order allows; `rhi_submit` places the slot's fence. What a list sets (pipeline, bind
groups with their offsets, vertex and index buffers, stencil reference, dynamic depth and blend) is
kept in its `GlCmdList` and given to OpenGL at the next draw: the program pipeline and the vertex
array object when they change, the vertex buffers into that VAO (`glBindVertexBuffer` with the
pipeline's stride; the element array buffer is the VAO's too, bound after it), the bind groups
whose id or offsets changed (`glBindBufferRange` for uniform and storage buffers, the dynamic
uniforms at their base plus the bind-time offset; `glActiveTexture` + `glBindTexture` +
`glBindSampler` at the texture's unit; `glBindImageTexture` for the storage image), and the
fixed-function state. A group is dirty at almost every draw (the frame's groups are made per draw
and the dynamic offsets move) while most of its entries bind what the draw before bound, so the
binding points below 64 are shadowed (`gl_bind_groups`: the buffer, offset and size per uniform
and storage point, the texture with its target and the sampler per unit, the image per image
unit) and an entry the point already holds is skipped: `RhiStats.entryBinds` counts the entries,
`entryBindsSkipped` those skipped (0 on the other backends), and the `GPU top passes` line of a
timed replay adds `; group entries N a timed replay, N already bound`. The shadow is forgotten
(`gl_forget_units`) at every `rhi_begin_commands` and every deferred delete of a buffer, texture
or sampler (OpenGL unbinds a deleted object and may hand its name out again); nothing else binds
those points (creates, copies, readbacks and the present use texture unit 0 and the non-indexed
buffer targets). The fixed-function state is shadowed (`GlRaster`): a draw calls only what differs from what
OpenGL holds (`glEnablei`/`glDisablei(GL_BLEND)`, `glBlendFuncSeparatei`,
`glBlendEquationSeparatei`, `glColorMaski` per draw buffer, the depth test, `glDepthMask`,
`glDepthFunc`, the stencil test, `glStencilFuncSeparate` with the list's reference,
`glStencilOpSeparate`, `glStencilMaskSeparate`); a new list starts from nothing known. A read-only
depth pass keeps both depth and stencil writes off with the depth-stencil still attached. Dynamic
depth and blend are plain OpenGL state (`dynamicDepth` and `dynamicBlend` true): the sets keep the
draw's values, and `dynamicSets` counts the commands the Vulkan backend would record for them
([Dynamic state](#dynamic-state)). Draws are `glDrawArrays` or `glDrawElementsBaseVertex` (the
instanced forms above one instance), the index offset in bytes from the list's index buffer; a draw
outside a pass, in a pass without a framebuffer, without a pipeline or (indexed) without an index
buffer is skipped with `rhi_gl: a draw skipped: <why> (N so far)`. A command on another list than
the last one sets that list's bindings, viewport, scissor and framebuffer again.

**Render passes.** A pass binds a framebuffer object from a cache keyed by the attachments' texture
names (a pass without attachments, the storage destination read's, by its size, made with
`GL_FRAMEBUFFER_DEFAULT_WIDTH` and `_HEIGHT`); colour attachments in `glDrawBuffers` order, a
D32F_S8 target at `GL_DEPTH_STENCIL_ATTACHMENT`. The cache holds 256 and drops the least recently
used when full (`rhi_gl: framebuffer cache full (256): the least recently used one deleted (N so
far)`); a texture's deferred delete drops the framebuffers holding its name first
(`gl_fbo_forget_texture`), so a name OpenGL hands out again never meets a stale framebuffer. An
incomplete one logs `rhi_gl: render pass: framebuffer incomplete (status 0x...; N colour,
depth|no depth, WxH); the pass draws nothing (N so far)`. `RHI_LOAD_CLEAR` is
`glClearBufferfv` (`glClearBufferuiv` with the integer per channel for integer targets,
`glClearBufferfi` for depth and stencil together) with the scissor test off and the write masks
opened first, since OpenGL's clears honour both (as D3D12's clear, which covers the whole attachment);
`RHI_LOAD_DONT_CARE` is `glInvalidateFramebuffer`; the store ops are not acted on (OpenGL keeps
both aspects, which `RHI_STORE_DONT_CARE` allows). A read-only depth target is not cleared
(`rhi_gl: render pass: a read-only depth target cannot be cleared; clear ignored`). The pass sets
the viewport and scissor to its area, clamped to the smallest attachment as on the other backends
(`rhi_gl: render pass WxH is larger than an attachment ...`, once). Its end opens the colour write masks
again, so the blits between passes (a resolve, the present) never meet a closed one. Viewports are
`glViewportIndexedf` and `glDepthRangef`, scissors `glScissor`, unflipped (clip control).

**Barriers and copies.** OpenGL orders rendering, clears, copies and texture fetches within its
context itself, so `rhi_cmd_barrier` has no layouts to change; a texture leaving or entering
`RHI_STATE_STORAGE` (image loads and stores) takes one `glMemoryBarrier(GL_ALL_BARRIER_BITS)` per
call (the narrower bits do not name `glCopyImageSubData`, which `rhi_cmd_copy_texture` uses), counted
in `barriers`. No
`glTextureBarrier`: the RHI never samples a texture its pass draws into. `rhi_cmd_local_read_barrier`
does nothing (`localRead` is false). Copies run outside passes on the scratch binding points:
`rhi_cmd_copy_buffer` is `glCopyBufferSubData` (an upload buffer as the destination is refused);
`rhi_cmd_copy_buffer_to_texture` binds the buffer as `GL_PIXEL_UNPACK_BUFFER` and calls
`glTexSubImage2D` with `GL_UNPACK_ROW_LENGTH` from the row pitch (row by row for a pitch that is no
whole number of texels), BC formats `glCompressedTexSubImage2D` by whole block rows;
`rhi_cmd_copy_texture` is `glCopyImageSubData` (depth and stencil together); the texture to buffer
copy (the VU vertex pre-pass's record, the fog's depth words, dumps) reads through the texture's
own framebuffer from the render passes' cache (its colour at attachment 0, or its depth alone), so
no copy attaches or detaches anything, and calls `glReadPixels` into the buffer bound as
`GL_PIXEL_PACK_BUFFER` with `GL_PACK_ROW_LENGTH` (the depth aspect as
`GL_DEPTH_COMPONENT`/`GL_FLOAT`, integer formats as integers). No `glMemoryBarrier` follows, also
into a buffer the shaders read: `glReadPixels` is not a shader write, OpenGL orders a command's
buffer writes before the later commands' reads, and the barrier is for the writes of shaders (the
`glMemoryBarrier` reference page); a source the shaders wrote as a storage image took its barrier
at its `RHI_STATE_STORAGE` transition. `rhi_cmd_resolve_color` is `glBlitFramebuffer` from the
source's cached framebuffer to the destination's, `GL_NEAREST`, the scissor test off. Both leave
framebuffer 0 bound; an incomplete framebuffer logs `rhi_gl: <entry point>: framebuffer
incomplete (...); nothing copied|nothing resolved (N so far)`. The scratch framebuffers remain for
`rhi_readback_texture` and the present's blit. A refused copy logs `rhi_gl: <entry
point>: <why> (N so far)` (the first 8, then every 100th).

**Swapchain** (`port/rhi/gl/gl_swapchain.c`). The backbuffer is an RGBA8 texture at the window's
pixel size (so `rhi_swapchain_format` is `RHI_FMT_RGBA8_UNORM`); `rhi_present` blits it upside down
into framebuffer 0 (stretched with linear filtering if the window's size has changed since) and
leaves framebuffer 0 bound for read and draw, read buffer `GL_BACK`, through the swap (a capture
hook such as OBS's game capture reads the bound read framebuffer's `GL_BACK` at the swap; the last
pass's framebuffer, whose texture is top-down, came out upside down; every pass begin and copy binds
its own framebuffer), then `SDL_GL_SwapWindow`, timed into `presentNs`, and follows the window's size afterwards as the other
backends do. The swap interval is the vsync (1, or 0): `rhi_present_mode_name` says `fifo` or
`immediate`. `rhi_present_shown` is false for a minimised or hidden window. `rhi_release_surface`
drops the backbuffer and keeps the context; `rhi_recreate_surface` makes the same context current on
the window again, so nothing made on it is lost. Creation logs
`rhi_gl: swapchain WxH: an RGBA8 texture blitted to the window, swap interval N`.

**Limits**: dual-source blending where `GL_MAX_DUAL_SOURCE_DRAW_BUFFERS` is at least 1;
`msaaCounts` the counts `glGetInternalformativ` gives for RGBA8, RGBA8UI and `GL_DEPTH32F_STENCIL8`
together, within `GL_MAX_SAMPLES`, `GL_MAX_INTEGER_SAMPLES` and the texture sample limits;
`fragmentInterlock` with `ARB_fragment_shader_interlock` and the image unit the translated
shaders name (`u`, within the 8 OpenGL 4.3 guarantees; the extension is absent on llvmpipe); `bcTextures` with S3TC and BPTC; `uintView`,
`dynamicDepth` and `dynamicBlend` true (plain OpenGL state); `maxDynamicUniforms` 4; texture-to-buffer
copies at a row pitch of 16 and an offset of 4, so the VU vertex pre-pass stays on
(`colorBytesPerPixel` 0); `vendorId` from the vendor and renderer text; `driverText` the renderer
and the version; `robustText` "off (OpenGL)". OpenGL 4.3 lets a driver keep 8 storage buffer
bindings and no vertex shader storage blocks; below the shaders' needs (bindings up to 20 for storage
buffers and textures, storage blocks in the VU vertex shaders) `rhi_init` logs `rhi_gl: this
driver's limits are below the shaders' bindings: storage buffer bindings N (21 needed), vertex
shader storage blocks N (1 needed), texture units N (21 needed); the draws that use them will fail`.

| variable | effect |
| --- | --- |
| `ICO_GL_DEBUG` | 1 the debug output (a debug context, `KHR_debug`) whatever `debugLayers` says; 0 off |
| `ICO_GL_FAKE_NO_DUAL` | 1 reports no dual-source blending (also `rhi_gl_fake_no_dual`) |
| `ICO_GL_FAKE_NO_DST` | 1 reports no fragment interlock |
| `ICO_GL_FAKE_NO_MSAA` | 1 reports single sampling only |
| `ICO_GL_FAKE_NO_TIMESTAMPS` | 1 turns GPU timestamps off |

The debug output counts the driver's errors and undefined-behaviour messages
(`rhi_gl_debug_error_count`) and prints `rhi_gl: debug: <type> #N (id 0x...): <message>` for the
first 8 and every 100th; the other messages of low severity or above likewise, without the count;
notifications are off; `rhi_shutdown` logs `rhi_gl: debug output: N errors, M other messages`.
Labels are debug groups, only with the debug output.

**Not implemented on OpenGL**: mailbox presents, exclusive fullscreen control, the display's timing
of presents and frame statistics (OpenGL has none of them); the pipeline cache file (program
binaries are for a later version; the driver's own shader cache applies); device loss (no robust
context is asked for, so `rhi_device_lost` stays false); the memory gauges; the input-attachment
shaders (GLSL cannot express them: `localRead` and `rasterOrderAttachment` are false).

**Test extras** (`port/rhi/gl/rhi_gl.h`): `rhi_gl_fake_no_dual`, the debug output's error count and
whether it was on.

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
  counter read; a removed D3D12 device's fence, which reads `UINT64_MAX`); the Vulkan header line
  names the submission sync (`sync timeline` or `sync fences`). A Vulkan wait that fails first logs
  how long it waited, the submission it waited for, the last one submitted, the sync and the one the
  GPU reached; once the device is lost no wait is
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
submitting thread last saw it, without a call into the driver: the newest submission's number
(the counter's, on either sync path), when it was made and its `rhi_set_submit_tag` replay and frame, the newest value seen
finished (a wait that returned, a counter read before one, `rhi_wait_idle`), and the value a wait
in progress blocks on with how long it has blocked. The submitting thread stores them with
`__atomic` stores in `vkr_note_submit`, `vkr_wait_value` and `rhi_wait_idle`; a wait in progress
for a value above the newest seen finished is work the GPU has not finished. The watchdog reads it
to tell a GPU hang from a freeze ([PLATFORM.md](PLATFORM.md#diagnostics)). Three other
`vkDeviceWaitIdle` calls are not marked as waits, so a hang inside one stays a freeze: the surface's
release and the swapchain's resize (`port/rhi/vk/vk_swapchain.c` (`rhi_release_surface`,
`rhi_resize_swapchain`)) and a full deferred-destroy list (`port/rhi/vk/vk_resource.c`
(`vkr_defer_sized`)). D3D12 does not track it
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
| `rhi_test_common.c`, `rhi_test_common.h` | headless rendering on any backend, texel by texel: a 64x32 target in eight cells (dual-source blending, stencil wrap, reversed depth, colour mask, a copied texture drawn indexed), the fill rule (`fillRuleCell`: two quads with every edge through pixel centres, the top and left edges' pixels taken and the bottom and right ones left, the shared edge's row once), an RGBA8 UINT target, R8 copies and same-state ordering, a device-local buffer, depth readback, three frames; seven of the cells' pipelines come from one `rhi_create_pipelines` call with an uncreatable description among them (only that one fails, the ids are distinct, the finished count never goes back); the dynamic state cell (`dynamicCell`: sixteen cells of the renderer's depth tests, Z writes, blends and masks drawn with every state static, from one `rhi_create_pipelines` batch with `maxThreads` 1 on one thread, then with the device's dynamic groups set after each bind: texel-equal, three commands a group after each static bind, none for a repeat; skipped but for the static pass without dynamic state) |
| `rhi_vk_test.c` | the common cells on Vulkan, a hazard-tracking cell, the render-area check (a pass wider than its colour target, one taller than its depth: each clamped and counted, no validation error; a fitting pass untouched), and the pipeline cache file (written while up by `rhi_pipeline_cache_save` and at shutdown, read back, another device's or garbage ignored; the drop of other builds' cache files in a temporary folder: the current file, its `.tmp` and unrelated names stay), and the pipeline pool filled (`poolCell`, [The pipeline pool](#the-pipeline-pool); not under the fake switches); ctest `rhi_vk`, and `rhi_vk_nodual`, `rhi_vk_d24s8`, `rhi_vk_mali` under the fake switches |
| `rhi_vk_swapchain_test.c` | the window path on SDL's offscreen driver: frames, readback order, surface release and recreation, suboptimal and lost presents, resizes, the surface poll, submits per frame, the present-wait stand-in and a hanging wait; ctest `rhi_vk_swapchain` |
| `rhi_vk_present_mode_test.c` | `vkr_choose_present_mode` over the mode sets drivers offer, and `vkr_present_wait_wanted`; ctest `rhi_vk_present_mode` |
| `rhi_vk_enum_test.c` | every `rhi.h` enumeration has a Vulkan mapping; the layer classifiers and `rhi_layer_switched_on`; ctest `rhi_vk_enum` |
| `rhi_driver_version_test.c` | the driver version text of each vendor's encoding; ctest `rhi_driver_version` |
| `rhi_d3d12_plan_test.c` | `d3d12_plan.c` on every host; with the shader table, every vertex shader's DXIL inputs against its SPIR-V locations and every DXIL blob signed; ctest `rhi_d3d12_plan` |
| `rhi_d3d11_shader_plan_test.c` | `d3d11_shader.c`'s portable part on every host: the macros text, the include lookup, the flat-register rule over the game's sources and `rhi_test_hlsl.h` (checked against `rhi_test.hlsl`), the cache key over every table entry, the cache file's round trip and the corrupt files it refuses, the failure log's limit; ctest `rhi_d3d11_shader_plan` |
| `rhi_gl_test.c` | the common cells on OpenGL with the driver's debug output failing the run, the copies the cells do not make (a colour region into a vertex-read device buffer and on into a readback buffer, a D32F_S8 depth as floats, RGBA32_UINT words, each at a wider row pitch; a target made after one whose framebuffer was dropped), and six swapchain frames on SDL's offscreen driver with a resize; ctest `rhi_gl`, and `rhi_gl_nodual` under `ICO_GL_FAKE_NO_DUAL=1`, both on llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`, `SDL_VIDEO_DRIVER=offscreen`), skipped without a context |
| `rhi_d3d12_test.c` | Windows, double-clickable: the cells on WARP, on the hardware adapter, the swapchain on a hidden window, then Vulkan for comparison; writes `rhi_d3d12_test.log` and shows a message box (`--console` for ctest); ctest `rhi_d3d12` |
| `rhi_d3d11_test.c` | Windows, double-clickable, as `rhi_d3d12_test.c` on Direct3D 11 (the cells' shaders compiled at run time from `rhi_test_hlsl.h` through a one-source table; the destination-read cell skipped by the limits); writes `rhi_d3d11_test.log`; ctest `rhi_d3d11` on a native Windows build only; CI's `windows` job runs it with `--console` (WARP; the hardware and Vulkan sections skip on the runner) |
| `compare_backends.cmd`, `compare_png.ps1` | Windows: every dump in `dumps\` rendered by `rd_replay_tool` on both backends, DISPLAY and the presented picture, each pair compared texel by texel and passing within 1 LSB (DISPLAY is expected identical) |
| `shaders/rhi_test.hlsl`, `gen_shaders.sh` | the tests' shaders, committed as `rhi_test_spv.h` and `rhi_test_dxil.h` so the tests build without DXC, and as `rhi_test_hlsl.h` (the text as a C string, its registers flat under `ICO_D3D11`) for a run-time compile |

The Vulkan tests exit 77 (skipped) without a device; under the fetched validation layer `rhi_vk`
fails on any validation error (`port/rhi/CMakeLists.txt`; `port/rhi/test/*`, header comments).
`ctest --test-dir <build> -R '^rhi_'` runs them; [TESTING.md](TESTING.md#portrhitest) lists every
test of the tree.
