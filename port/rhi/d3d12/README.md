# port/rhi/d3d12: the Direct3D 12 backend

Implements [`port/rhi/rhi.h`](../rhi.h) on Direct3D 12, feature level 11_0,
with the operating system's D3D12 runtime (no Agility SDK). Written in C
through the COM C interfaces (`COBJMACROS`: `ID3D12Device_CreateX(dev, ...)`;
`WIDL_C_INLINE_WRAPPERS` for the methods that return structures). Library
`ico_rhi_d3d12` (`port/rhi/CMakeLists.txt`, option `ICO_RHI_D3D12`, default
ON for 64-bit Windows: the `win-x64` and `win-x64-clang` presets). Renderer
wave 6, package R6c. It mirrors the Vulkan backend
([`../vk/README.md`](../vk/README.md)) file for file.

| file | contents |
| --- | --- |
| `d3d12_internal.h` | backend state (`g_dx`), object records |
| `d3d12_enums.h` | every `rhi.h` enumeration mapped to D3D12/DXGI, as tables indexed by the RHI value; static asserts tie `d3d12_plan.h`'s state values to `d3d12.h` |
| `d3d12_plan.h`, `d3d12_plan.c` | the bookkeeping that needs no D3D12 header: state table and barrier plan, buffer state tracking, same-state copy ordering, descriptor rings, layouts and root parameters, CBV sizes, the DXBC input-signature reader, handle pools. Built and tested on every host (`test/rhi_d3d12_plan_test.c`) |
| `d3d12_device.c` | runtime loading, adapter selection, device, queue, fence, debug layer, descriptor heaps, limits, `rhi_Init`/`rhi_Shutdown`, the backend's `rhi_CreateBackend` entry |
| `d3d12_resource.c` | buffers, textures, samplers, shaders, deferred destruction |
| `d3d12_pipeline.c` | bind group layouts, root signatures, transient bind groups, pipeline state objects |
| `d3d12_cmd.c` | frames, command lists, submission, barriers, passes, draws, copies, readback |
| `d3d12_swapchain.c` | DXGI flip-model swapchain on the SDL window's HWND, acquire, present, resize |
| `rhi_d3d12.h` | test-only extras (`rhi_d3d12_DebugErrorCount`, `rhi_d3d12_DebugLayerActive`, `rhi_d3d12_IsWarp`); `port/render` never includes it |

## Backend selection

A Windows build links both backends. `port/rhi/rhi_backend.c` defines the
`rhi.h` functions as forwarders to the backend `rhi_CreateBackend(name)`
picked (`"vulkan"` or `"d3d12"`; default: `ICO_RHI_BACKEND` from the
environment if it names a linked backend, else Vulkan). Each backend library
is compiled with `RHI_BACKEND_PREFIX` (`vk`, `d3d12`), under which `rhi.h`
includes `rhi_backend_names.h` and every `rhi_X` the backend defines becomes
`rhi_<prefix>_X`; the backend registers its table with one
`RHI_BACKEND_DEFINE` line (`rhi_backend.h`). The backends' sources keep the
plain `rhi.h` names. The Linux build has the same dispatcher with the Vulkan
backend alone.

The window build reads `[video] backend = "vulkan" | "d3d12"` from
`config.toml` (or `backend=` in `ico-pc.ini`; `port/platform/host_config.c`,
`window_host.c`), default `"vulkan"`; the SDL window gets
`SDL_WINDOW_VULKAN` only for Vulkan. `rd_replay_tool --backend d3d12` renders
a frame dump on D3D12 (`docs/port/TESTING.md`).

## Device

- Runtime: `d3d12.dll` and `dxgi.dll` are loaded with `LoadLibrary` at
  `rhi_Init` (`D3D12CreateDevice`, `D3D12GetDebugInterface`,
  `D3D12SerializeRootSignature`, `CreateDXGIFactory2`), so the executable
  imports neither and the Vulkan path never depends on them. The IIDs come
  from the toolchain's `libdxguid.a` (static data).
- Headers: the mingw-w64 toolchain's own `d3d12.h`, `d3d12sdklayers.h`,
  `dxgi1_6.h` (mingw-w64 in `tools/toolchain/mingw-gcc`, WIDL 9.8 output;
  llvm-mingw's in `tools/toolchain/llvm-mingw`). Both have every interface
  used here; the DirectX-Headers repository is not needed. See
  `docs/port/THIRD_PARTY.md` for their licence.
- Adapter: with DXGI 1.6, `EnumAdapterByGpuPreference(HIGH_PERFORMANCE)`
  (discrete first), else `EnumAdapters1`; the first hardware adapter that
  creates a feature level 11_0 device wins. No hardware adapter: WARP
  (`EnumWarpAdapter`), the software rasteriser every Windows 10 and 11
  ships. `ICO_D3D12_ADAPTER=warp`, an index, or a name substring forces a
  pick (the test uses `warp`). Every adapter is listed in the log.
- Debug layer: enabled when `RhiDeviceDesc.debugLayers` is set, always in
  `CMAKE_BUILD_TYPE=Debug` builds, and `ICO_D3D12_DEBUG=0/1` overrides
  both. It exists only with the Windows optional feature "Graphics Tools";
  without it the backend logs that and continues. Messages (corruption,
  error, warning) are drained after every submit, readback, `rhi_WaitIdle`
  and pipeline creation, printed, and errors counted
  (`rhi_d3d12_DebugErrorCount`). Info messages are filtered out.
- Limits (renderer wave 7's `textureMips` and `maxAnisotropy` too:
  mipmapped textures with per-level uploads, 16x anisotropy; an anisotropic
  sampler filters linearly between levels whatever `RhiSamplerDesc.mip`
  says, since the feature level 11_0 runtime has no anisotropic filter with
  point mips): `uniformAlign` 256 (`D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT`),
  `copyRowPitchAlign` 256 (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`),
  `copyOffsetAlign` 512 (`D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`),
  `maxTextureSize` 16384, dual-source blend and stencil wrap always.

## Handles

As the Vulkan backend: `(generation << 20) | (index + 1)` in fixed pools
(4096 buffers, 8192 textures, 256 samplers, 512 shaders, 64 layouts, 1024
pipelines); command lists and bind groups carry the frame tag and die at
the next `rhi_WaitFrame` that reuses their slot.

## Memory

Every buffer and texture is a committed resource.

- `RHI_MEM_DEVICE`: default heap, created in COMMON.
- `RHI_MEM_UPLOAD`: upload heap, GENERIC_READ for its lifetime (D3D12
  requires it), persistently mapped.
- `RHI_MEM_READBACK`: readback heap, COPY_DEST for its lifetime,
  persistently mapped; GPU writes are visible after the fence.
- Buffer sizes round up to 256 bytes so a constant buffer view at the end of
  a buffer stays inside it.
- Textures are created **typeless** (`R8G8B8A8_TYPELESS`, `R8_TYPELESS`,
  `R32G8X24_TYPELESS`, ...) and viewed typed (UNORM or UINT SRV/RTV, the
  depth DSV), so RGBA8 UNORM and RGBA8 UINT are one resource format. Depth
  formats always get `ALLOW_DEPTH_STENCIL` (a depth copy target is sampled
  through a depth SRV, as on Vulkan), never `DENY_SHADER_RESOURCE`, and two
  DSVs: writable, and read-only for depth and stencil. Render targets get an
  RTV. RTV and DSV descriptors live in CPU heaps (8192 and 2048 slots,
  free-listed); D3D12 reads them when a command is recorded, so their slots
  are reused at once while the resource itself is released late.
- Swapchain buffers are the typed exception (`B8G8R8A8_UNORM`; flip model
  takes typed formats only).

## Descriptors

One root signature per distinct list of bind group layouts (cached, at most
64): group *g* is HLSL register space *g*, as up to two descriptor tables, a
resource table (the group's `b` and `t` registers: CBVs, SRVs) and a sampler
table (its `s` registers), in group order. Each slot is one descriptor at a
fixed offset in its table (`d3dp_LayoutBuild`, `d3dp_RootParams`). Table
visibility is vertex, pixel or all from the slots' stage masks. Root
signature version 1.0 (descriptors and data volatile). No root constants or
root descriptors: everything goes through tables, so a bind group is two
GPU handles.

Two shader-visible heaps, set once per command list:

- CBV/SRV/UAV: 131072 descriptors, split into one region per frame slot
  (65536 each). `rhi_CreateBindGroup` allocates the resource table
  linearly from the current frame's region and writes the views straight
  into it; the region is reset when the slot is recycled (`rhi_WaitFrame`).
- Sampler: 2048 descriptors (the shader-visible maximum). The first 256 are
  persistent, one per `RhiSampler` (its pool index): a table of one sampler,
  the only kind `rd_core` uses, points at it directly, so the per-draw bind
  groups cost no sampler descriptors. Descriptor 256 is a default
  point/clamp sampler for null tables. Tables of several samplers are
  written into the rest, split per frame slot (895 each). A destroyed
  sampler's slot is released when its frame slot is recycled.

Views: a uniform buffer is a CBV of its range rounded up to 256 bytes (at
most 64 KiB; offset 256-aligned, which `uniformAlign` guarantees). A storage
buffer is a structured-buffer SRV of 16-byte elements: the game's only
storage buffer is `StructuredBuffer<float4>` (`vu_common.hlsli`), so
offsets and sizes are multiples of 16 (a different element size needs a
stride in `RhiBinding`). A texture SRV is its typed view; a depth format is
always viewed as its depth plane (`R32_FLOAT`, `R32_FLOAT_X8X24_TYPELESS`).
A slot the bind group leaves out gets a null view. A group the caller has
not bound when a draw needs its root parameter gets a per-frame bind group
of null views (and the default sampler), so no root parameter is ever unset
at a draw.

## Pipelines

Graphics PSOs. Vertex attributes: D3D12 matches by semantic, the RHI by
location, so `rhi_CreateShader` reads the vertex shader's DXBC input
signature (`ISG1`) and RHI location *n* becomes the input with the *n*-th
lowest register among those without a system value; DXC assigns registers
in declaration order, as the SPIR-V build assigns locations
(`VK_LOC` in `common.hlsli`). `rhi_d3d12_plan_test` checks this on every
vertex shader of `port/shaders`: location *n* names the same semantic in
the DXIL and the SPIR-V (DXC's `in.var.<SEMANTIC>` names); 19 shaders, 12
inputs at the time of writing.

Blend: factors map one to one; in the alpha equation the `*_COLOR` factors
become the `*_ALPHA` ones (D3D12 rejects colour factors there, and for the
alpha channel the two are the same number), `CONSTANT` is `BLEND_FACTOR`.
Blending on an integer target is dropped with a log line (D3D12 forbids it,
as Vulkan does). Write masks: RHI bits 0..3 are D3D12's R, G, B, A bits.
Stencil `INCR_WRAP`/`DECR_WRAP` are `D3D12_STENCIL_OP_INCR`/`DECR`, the
clamping ones `INCR_SAT`/`DECR_SAT`. Depth compare as given (reversed-Z
`GEQUAL` is `GREATER_EQUAL`); a pipeline without a depth format has depth
and stencil off; stencil is off on D32F. Cull none, depth clip on, one
sample. Topology type from the RHI topology; the primitive topology is set
at draw time. Vertex strides live in the pipeline and go into the vertex
buffer views at the first draw after a change.

## States and barriers

`d3dp_TextureState`:

| RhiState | D3D12 state |
| --- | --- |
| UNDEFINED | none: the tracked state is used |
| RENDER_TARGET | `RENDER_TARGET` |
| DEPTH_WRITE | `DEPTH_WRITE` |
| DEPTH_READ | `DEPTH_READ \| PIXEL_SHADER_RESOURCE \| NON_PIXEL_SHADER_RESOURCE` |
| SHADER_READ | `PIXEL_SHADER_RESOURCE \| NON_PIXEL_SHADER_RESOURCE`; depth formats as DEPTH_READ |
| COPY_SRC / COPY_DST | `COPY_SOURCE` / `COPY_DEST` |
| PRESENT | `PRESENT` (= `COMMON`) |

D3D12 has no "discard" state and its barriers name the real before-state,
so the backend tracks each texture's D3D12 state in recording order (rd
records lists in submission order). A barrier from UNDEFINED transitions
from the tracked state; a barrier from any other state uses the caller's
(and logs when it differs from the tracked one). Mapped states that are
equal (DEPTH_READ and SHADER_READ on a depth texture) record nothing: the
debug layer rejects a transition between equal states. Transitions cover
all subresources (both planes of D32F_S8).

The same-state ordering rhi.h promises, per kind of write:

- **Render target and depth writes** in one state (a pass after a pass on
  one target, a clear after draws): the output merger writes in submission
  order within a queue (the same guarantee every D3D version gives blending);
  nothing is recorded.
- **Copies into one texture** (a copy after a copy, nothing in between):
  the legacy barrier model has no barrier for a write after a write in one
  state on a non-UAV resource, and nothing documents copies as ordered, so
  the second copy
  into a texture in one command list since its last barrier is preceded by
  `COPY_DEST -> COMMON -> COPY_DEST` (`d3dp_CopyNeedsSync`).
- **Buffers** (`rhi_CmdCopyBuffer`, copies into device buffers): D3D12
  buffers decay to COMMON at the end of every `ExecuteCommandLists`
  (Microsoft, "Using resource barriers to synchronize resource states in
  Direct3D 12", section "State decay to common":
  https://learn.microsoft.com/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12),
  so a
  buffer's state is known per command list (COMMON at its start). A copy
  into a device buffer moves it to `COPY_DEST`, and after the copy to
  `GENERIC_READ` (vertex, index, constant, shader resource and copy source at
  once): rhi.h's "visible to every later read" without a caller barrier; a
  second copy into it transitions back, which orders the writes. Upload and
  readback buffers never transition.

## Frame lifecycle

    rhi_WaitFrame              frame N: slot N % 2 waits for its fence value, then
                               releases its deferred objects, resets its 8 command
                               allocators and its descriptor regions
      rhi_CreateBindGroup ...  from slot N % 2's descriptor regions
      rhi_AcquireBackbuffer    GetCurrentBackBufferIndex; the buffer is in PRESENT
      rhi_BeginCommands ... rhi_Submit   ExecuteCommandLists, then Signal(++value)
      rhi_Present              IDXGISwapChain3::Present
    rhi_WaitFrame              frame N+1 ...

- One `ID3D12Fence`; every submit signals the next value and the frame slot
  remembers its last. Two frames in flight.
- Eight command lists per frame slot, each with its own allocator, so lists
  of one frame can be recorded at the same time.
- `rhi_Destroy*` releases the handle at once and queues the D3D12 object on
  the current slot; it is released when the slot is recycled.
  `rhi_WaitIdle` releases the other slots' objects.
- Swapchain: `CreateSwapChainForHwnd` on the queue, the HWND from SDL
  (`SDL_PROP_WINDOW_WIN32_HWND_POINTER`), `FLIP_DISCARD`, three
  `B8G8R8A8_UNORM` buffers, `ALLOW_TEARING` when DXGI 1.5 offers it (used
  when vsync is off), Alt+Enter left to SDL. Present interval 1 with vsync,
  else 0 with tearing. DXGI has no "out of date": after each present the
  backend compares the window's pixel size and resizes
  (`ResizeBuffers`) when it changed, as the Vulkan backend does on
  `VK_ERROR_OUT_OF_DATE_KHR`.
- A list whose `Close` fails is marked (`DxCmdList.closeFailed`) and
  `rhi_Submit` does not execute it (its commands are dropped, logged once
  per list); the fence is still signalled so the slot recycles.
- Device loss: a failure of `DXGI_ERROR_DEVICE_REMOVED`, `_RESET`, `_HUNG`
  or `DXGI_ERROR_DRIVER_INTERNAL_ERROR` (from `Present`, `Close`, `Signal`,
  any checked call), or any failure while
  `ID3D12Device::GetDeviceRemovedReason` is not `S_OK`, sets
  `g_dx.deviceLost` in `dx_Check` and logs the code and the removed reason
  once. After it, submits, presents and fence waits are skipped, further
  failures are counted (`lostFailures`) instead of logged, and
  `rhi_DeviceLost()` is true; the window loop (`ico_window_pump`) then
  shows one message box and quits through the normal exit path. The Vulkan
  backend reports `VK_ERROR_DEVICE_LOST` through the same entry point.
- `rhi_ReadbackTexture`: `GetCopyableFootprints` of mip 0 (plane 0 for
  depth), a copy into a temporary readback buffer on a one-shot list after
  all earlier work, a fence wait, and the rows repacked tightly.

## R1a's and R1b's questions, answered

From `../vk/README.md` ("What the D3D12 and Metal backends must mirror") and
`docs/port/RENDER_API.md`.

1. **Same-state copy barriers.** Needed for copies only. A second copy into
   a texture within one list since its last barrier gets
   `COPY_DEST -> COMMON -> COPY_DEST` first; render-target and depth writes
   are ordered by D3D12 itself (States and barriers, above). Tested by
   `rhi_test_common.c`'s R8 copies (a whole copy, then a 2x2 block over it).
2. **Buffer state tracking (COPY_DEST -> vertex/constant).** Per command
   list, from COMMON (buffers decay at every `ExecuteCommandLists`): COPY_DEST
   for the copy, then GENERIC_READ, which covers vertex, index, constant,
   shader and copy reads. Upload heap buffers stay GENERIC_READ, readback
   heap buffers COPY_DEST. Tested by the device vertex buffer of
   `rhi_test_common.c` (copied every frame, then drawn) and by
   `rhi_d3d12_plan_test`.
3. **Sampled depth (DEPTH_READ | PIXEL_SHADER_RESOURCE); stencil writes need
   DEPTH_WRITE.** DEPTH_READ and a depth texture's SHADER_READ are both
   `DEPTH_READ | PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE`, so a
   bind group never depends on which of the two the caller chose, and the
   transition between them records nothing. A `readOnlyDepth` pass binds the
   read-only DSV (`READ_ONLY_DEPTH | READ_ONLY_STENCIL`), which is what that
   state allows; the shadow-volume pass (stencil counted, depth tested, not
   written) is DEPTH_WRITE with `depthWrite = false`, as rhi.h says. A clear
   on a read-only depth pass is refused with a log line.
4. **Integer clears on R8G8B8A8_UINT.** `ClearRenderTargetView` takes
   floats and converts them to the integer format; the backend first rounds
   each clear value the way the Vulkan backend does (`v <= 0 ? 0 :
   floor(v + 0.5)`, `d3dp_IntClearValue`), so the float it passes is an
   exact integer and the conversion's rounding mode cannot matter. Tested by
   `rhi_test_common.c`'s UINT target (cleared to 1, 2, 3, 4).
5. **Backbuffer PRESENT/COMMON transitions.** `D3D12_RESOURCE_STATE_PRESENT`
   is `COMMON`; an acquired buffer's tracked state is set to PRESENT, so the
   caller's `UNDEFINED -> RENDER_TARGET` records `COMMON -> RENDER_TARGET`,
   and the caller's final `-> PRESENT` records `X -> COMMON` before
   `Present`.
6. **Bind groups as descriptor tables per register space 0..2 in one root
   signature, per-frame shader-visible heap ring.** Done that way, with one
   root signature per distinct layout list rather than a single global one
   (`rd_core` has a handful; a global one would need every group's table
   shape fixed in advance). Samplers of single-sampler tables are persistent
   descriptors so the 2048-sampler heap limit is never reached by per-draw
   groups (Descriptors, above).
7. **Limits.** `uniformAlign` 256, `copyRowPitchAlign` 256,
   `copyOffsetAlign` 512. `rhi_test_common.c` now places its texel rows and
   offsets by `rhi_Limits()` (it used pitch 8 and offset 16640 before, which
   D3D12 rejects).
8. **(R1b) CopyTextureRegion between R8G8B8A8_UNORM and _UINT.** Both are
   created as `R8G8B8A8_TYPELESS`, so the copy is between identical resource
   formats: a bit copy, which is what `RDC_EXACT_BLEND`'s ping-pong needs.
   Creating them typeless means the answer does not depend on whether the
   runtime accepts a copy between two different typed members of the family.
9. **(R1b) A pipeline writing SV_Target1 with blending disabled and one
   RT.** The PSO declares one RT format; the second output has no target
   and is discarded. The debug layer is expected to report this as a
   warning (`D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_RENDERTARGETVIEW_NOT_SET`),
   not an error; warnings are printed and not counted. Not yet observed on a
   device: if it turns out to be an error, the fix is a second pipeline key
   bit or a single-output pixel shader variant, and `rhi_d3d12_test.log`
   will say so. With dual-source blending on,
   SV_Target1 feeds the blender, one RT is bound and
   `IndependentBlendEnable` is off, as D3D12 requires.
10. **(R1b) Integer clears.** Question 4.
11. **(R1b) Replicate rd_pixel's coverage cells.** No copy is needed: the
    RHI tests are backend-neutral now (`test/rhi_test_common.c`), and every
    `rd_*` test runs on D3D12 by selecting the backend through the
    environment (`ICO_RHI_BACKEND=d3d12`, with `ICO_D3D12_ADAPTER=warp` for
    the software rasteriser). `docs/port/TESTING.md` gives the commands.
    Not yet run on Windows.
12. **Dual-source blending on D3D12 (rhi.h).** Always available at feature
    level 11_0; `RHI_BF_SRC1_*` map to `D3D12_BLEND_SRC1_*`. Same expected
    values as Vulkan in cells 0 and 1.
13. **DXIL signing (docs/port/SHADERS.md).** The blobs are signed at build
    time; this backend passes them to the runtime unchanged, with no
    experimental shader models. Whether the OS runtime accepts them is
    checked by the first run of `rhi_d3d12_test` (a pipeline that fails to
    create fails the test).

## Tests (`port/rhi/test/`)

| test | where | what |
| --- | --- | --- |
| `rhi_d3d12_plan` | every host (ctest) | the state table over every (before, after, tracked) triple, write states alone, the recorded sequences of the RHI test and the swapchain, buffer tracking, copy ordering, descriptor rings (random allocations stay in their region), rd_core's layouts and root parameters, CBV sizes, integer clears, the DXBC reader on the test shaders and on malformed input, pools; with DXC, every game vertex shader's DXIL inputs against its SPIR-V locations |
| `rhi_d3d12_test.exe` | Windows (built by `win-x64`, `win-x64-clang`) | `rhi_test_common.c`'s cells (the same eight cells and expected values as `rhi_vk`) on WARP, then on the hardware adapter; the swapchain on a hidden window (six frames, a resize, readback in BGRA); the Vulkan cells on the same machine for comparison. Double-click: writes `rhi_d3d12_test.log` beside itself and shows the verdict in a message box. Debug layer errors fail it when Graphics Tools is installed |

The test shaders are `test/shaders/rhi_test.hlsl`, compiled by
`test/shaders/gen_shaders.sh` into the committed `rhi_test_spv.h` and
`rhi_test_dxil.h` (signed DXIL).

What could not be checked in the container (no Windows, no Wine): any run of
this backend. Everything above compiles warning-free with mingw-w64 GCC 14
and llvm-mingw clang; `rhi_d3d12_plan` runs; the rest waits for
`rhi_d3d12_test.exe` on the user's machine (`docs/port/TESTING.md`).

## Open items

1. First run on Windows: `rhi_d3d12_test.exe` (WARP and hardware), then
   the window build with `[video] backend = "d3d12"`, then the frame-dump
   cross-check against Vulkan (`docs/port/TESTING.md`).
2. Line and point rasterisation rules differ between APIs (D3D12's
   diamond-exit lines, Vulkan's implementation-defined non-strict lines);
   the game's line strips (`rd_ScreenPrims`) may differ at endpoints. The
   dump cross-check will show it.
3. Storage buffers are assumed to be `StructuredBuffer<float4>` (16-byte
   elements).
4. No pipeline cache (`ID3D12PipelineLibrary`); `rd_core` keeps a few
   hundred pipelines at most (`RD_PIPELINE_REACHABLE_MAX`).
5. The `rd_*` GPU tests call `rhi_vk_ValidationErrorCount`, which stays 0
   on D3D12; run on D3D12 they count no debug-layer errors (the log shows
   them).
