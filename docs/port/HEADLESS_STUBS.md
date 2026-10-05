# The headless build (`ICO_HEADLESS`)

`ico_pc` builds in two forms from one source list. The window build
(`-DICO_HEADLESS=OFF`, the CMake default, compile definition `ICO_RD`) opens
an SDL3 window and draws through `port/render`. The headless build
(`-DICO_HEADLESS=ON`, compile definition `ICO_HEADLESS`) has no window and no
renderer; it runs the same game, sound, card, pad and movie code, and exists
for trace runs, replays of pad recordings and the tests. In
`CMakePresets.json` every preset is headless except `win-x64`, which builds
the window; any preset takes `-DICO_HEADLESS=OFF` to build the window.

## Stub the hardware, not the renderer layer

The headless build compiles the game's own renderer layer (all of
`seki/src`, and the sugipon and ito effect files) and stubs only what lies
under it: the GS and the DMA kick. The game builds its GIF/VIF packets and
display lists in its own buffers (`DmaPacket.c`'s two 512 KB banks,
`DisplayList.c`'s 13 lists) exactly as on the PS2, and nothing consumes
them.

The reason is that the draw layer owns simulation state. Leaving it out
would change what the game computes:

| file | state the simulation depends on |
| --- | --- |
| `seki/src/GsBase.c` | the fade state machine (`gsb_fade`: `fadeStatus`, `fadeColor`, which boot and stage changes wait on), `ScreenWidth`/`ScreenHeight`, the view matrices at `matrixptr` (`gsb_SetVSMatrix`, `gsb_MakeCommonMatrix`) that screen tests read, zoom easing, `gsb_InitGSSystem`'s `matrix_init` (sets `matrixptr`), `shadow_Init`, `reg_Init` |
| `sugipon/src/darkVolume.c` | `DispGameOverEffect` grows the game-over ring and sends mail 0x22 (game over) to the boy and enemies inside it |
| `ito/src/lightning.c` | `DrawLightningN` reseeds the VU0 R register, the RNG `commonact.c`, `enemy_act.c`, `queen.c` and others draw from |
| `sugipon/src/particleEffect.c` | particle state and R-register draws (`sugiRandom`) |
| `Texture.c`, `Primitive.c`, `Packet.c`, `Shadow.c`, `DmaPacket.c`, `DisplayList.c` | heap allocations made at model, texture and stage load, which shape the heap the simulation allocates from; pointers callers dereference (`tex_GetTextureData`, `prim_InitMesh3D`, `prim_InitParticle*`) |
| `GifPacket.c` | the `PacketBufferStruct` bookkeeping (`gif`, `end`, `tail`) that raw packet writers in `particleEffect.c`, `lightning.c`, `Primitive.c`, `Shadow.c` and `GsBase.c` write through |
| `Matrix.c`, `matrixDrive.c`, `quaternion.c`, `clothAnimation.c`, `Light.c`, `lineManager.c`, `DisplayFont.c`, `DisplayP2O.c` | plain C the simulation calls (matrix and quaternion stacks, cloth and chain physics, light lists, `font_GetHeight` for the staff roll, `p2o_MakePacket`'s `model->dobj` link) |

So Main's draw phase runs in both builds: `common/src/main.c` calls
`iosOmCreateDL()`, and every object's display-list callback runs against
the real packet layer. The window build differs only in what the packets
are turned into: its `ICO_RD` paths (`mc_HostDma`, the `rd_*` recording in
`GifPacket.c`, `Packet.c`, `Shadow.c` and the effect files) read the
finished chains and draw them, add no game-heap use and change nothing the
simulation reads, so the two builds produce the same trace. Shadow.c's
triangle buffer for the renderer is a file static, not heap, for the same
reason.

## What `port/null/` stubs

`port/null/` holds the two stand-ins left, linked into both builds:

`gfx_null.c`, libgraph's GS calls and libdma's channel calls:

| symbol | returns | why |
| --- | --- | --- |
| `sceGsSyncV` | the `GS_CSR` field bit | busy-waits for the next simulated vsync, as libgraph's does; records itself as the thread's last kernel call for the crash report (BOOT_DIAG.md) |
| `sceGsSyncPath` | 0 | paths always drained; `gsb_Init` loops until 0, `gsb_SyncGSSystem` skips the frame on nonzero |
| `sceGsResetGraph`, `sceGsResetPath` | | no GS |
| `sceGsSetDefDBuff`, `sceGsSetDefDispEnv`, `sceGsSetHalfOffset` | | register images nothing reads; `gsb_SetFrame` patches fields of the zeroed `db` |
| `sceGsSwapDBuff` | 0 | the flip; result unused |
| `sceDmaGetChan` | a static channel block | `Basic.c` `dma_init` ORs CHCR.TIE into it |
| `sceDmaReset` | 0 | clears the channel blocks |
| `sceDmaSend` | | `dl_Swap` (headless) and `p2o_TransMicroProgram` kick chains nobody consumes; the window build replays the lists in `rd` instead |

`libgcc_null.c`: libgcc's `fptodp`, returning 0; its callers hand it only to
debug printfs.

Everything else that once had a null stand-in has a real host layer in both
builds: the pad (`port/input/pad_host.c`; with no live pad and no pad script
it answers as an empty console), the memory card (`port/save/mc_host.c`, a
card in a host folder), sound (`port/audio/`; headless drops the output
unless `audio_dump=` writes it to a WAV), libscf (`port/config/sysconf.c`),
`common/src/debug.c` and `debug_exception.c` (DEVELOPER_MODE.md) and the
movie player (`port/fmv`; headless decodes and drops the pictures, and
`ICO_FMV_DECODE=0` skips the decoding). A film takes the same number of
vsyncs in both builds. `ito/mpeg`, the PS2's movie player, is
`ICO_EE_ONLY_SOURCES` and never compiled on the host.

`seki/src/MicroCode.c`'s VU1 microprogram address table is all zero on the
host: `ico2/vusrc` is not assembled for the host, and a function address
does not fit its `int` slots. No host path reads it: `mc_TransMicroCode`
chains address 0 and `p2o_TransMicroProgram` kicks address 0. In the window
build the VU1 programs are the `vu_*.hlsl` shaders (VU1_PROGRAMS.md).

## Known differences from the PS2

- **Packets are never consumed by a GS**, so anything the game would read
  back from GS memory (`Texture.c`'s store-image path) reads zeros. No
  simulation path found reads GS memory.
- **Clip flag history:** `lightning.c`'s `clip_flags` keeps its own VU0
  clip-flag history; on the PS2 `gsb_ClipBox` shares the register. Only
  lightning's strip packets depend on it.
- **Stale vector words:** the host forms of `GifPacket.c`'s `rotTransPers`
  leave the output's fourth word unwritten (the PS2 stored a stale register
  word no caller reads).

## Game sources changed for the host

Each is `#ifdef ICO_HOST` with the EE path unchanged:

| file | change |
| --- | --- |
| `seki/src/GsBase.c` | `gsb_ClipBox`: the two VU0 clip blocks as C (`gsb_clipCorner`) |
| `seki/src/GifPacket.c` | `rotTransPers` as C |
| `seki/src/MicroCode.c` | the empty microprogram table; the upload chains address 0 directly |
| `seki/src/DisplayP2O.c` | `p2o_TransMicroProgram` kicks address 0 |
| `seki/src/RegistPacket.c` | `reg_transMicroCode` passes the list mask to `mc_TransMicroCode` (the PS2's one-argument K&R call left it in the second argument register), with `MicroCode.h`'s prototypes |
| `sugipon/src/darkVolume.c` | `projectVertex`, `setScreenClamp`, `addScaledVectorXYZ` as C |
| `ito/src/lightning.c` | `clip_flags`, `apply_m34`, the R-register reseed (`ico_vu0_random_set`) as C |
| `ito/src/act_bird.c` | `Debug_WireString_Bird` uses a host `va_list` (clang has no `__builtin_next_arg`) |
| `common/src/layout_texture.c`, `kanban.c`, `layout_action.c`, `icoMisc.c`, `fumi/src/jimaku.c`, `seki/src/GsBase.c`, `sugipon/src/staticBlur.c` | the local `gif_*` externs take `GifPacket.c`'s parameter types (`long long z`, where the callers declared `unsigned int`; harmless in EE registers, wrong wherever arguments go on a stack) |
| `fumi/ios/cdvd.c`, `fumi/isys/gobj.c` | pointer-wide ring copy in `iosCdStRead`; the three `&gobjTable[gobjMax - 1]` end pointers wrap as on the EE when `gobjMax` is 0 |
| `common/src/main.c`, `fumi/ios/thread.c`, `common/src/kanbanBoot.c` | diagnostics hooks: milestones, thread functions, kanban steps |
