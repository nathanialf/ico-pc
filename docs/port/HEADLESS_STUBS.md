# Headless stubs (`ICO_HEADLESS`)

Since renderer wave 2 (package R2a) the default `ico_pc` is the window build
(`ICO_HEADLESS=OFF`, compile definition `ICO_RD`): `GifPacket.c`,
`DisplayList.c`, `DmaPacket.c`, `DisplayFont.c` and hooks in `GsBase.c` draw
through `port/render` (`docs/port/RENDER_API.md` section 9). Since wave 3
(package R3ab) the 3D world does too: `Packet.c`, `RegistPacket.c`,
`MicroCode.c`, `Primitive.c` and `Texture.c`'s UV offset packet read their
VU1 chains on the host (`mc_HostDma`) and draw the mesh packets, grids and
particle batches through `rd_mesh.h` (section 13); the window build no
longer depends on the microprogram address table. Since wave 5 (package
R5c) the raw packet builders outside seki join them in the window build:
`darkVolume.c`, `particleEffect.c` and `lightning.c` hand their finished
chains to `mc_HostDma` (VIF DIRECT and GIF PACKED/REGLIST packets read on
the host, section 18); `enemy.c`'s and `lineManager.c`'s packets went
through the decoder already. These host paths are `ICO_RD` only, add no
heap use and change nothing the simulation reads (lightning's out-of-range
blend mode is mapped to mode 0 only in what reaches rd), so the headless
build and its traces are unchanged. The headless
build stays a CMake option (`-DICO_HEADLESS=ON`; the Linux presets set it,
the Windows presets build the window) for the trace and test runs, with the
original packet code. Both builds link `port/null/gfx_null.c`: the window
build still has no libgraph (the vsync busy-wait and the path syncs are the
same stubs), the DMA kick is not called by `dl_Swap` there (rd replays the
lists instead; `p2o_TransMicroProgram` still calls the no-op
`sceDmaSend`). Since renderer wave 6 (package R6a) `common/src/debug.c` and
`debug_exception.c` are compiled in both builds (docs/port/DEVELOPER_MODE.md)
and `port/null/debug_null.c`, which stood in for them, is gone; libgcc's
`fptodp` moved to `port/null/libgcc_null.c`. There is one host source list:
every game source but `ICO_EE_ONLY_SOURCES` (`ito/mpeg`, the PS2's FMV
player; the host's is `port/fmv/movie.c` since Phase 4E, docs/port/FMV.md,
and the headless build decodes the pictures and drops them).

What the headless `ico_pc` (package 1D) links in place of hardware. The
renderer waves delete these as they land. `docs/port/BUILD_STATUS.md` has
the per-preset link status.

## What remains stubbed (checked in 7A)

`port/null/` holds two files and nothing else is a stand-in for hardware in
either build: `gfx_null.c` (libgraph's GS calls and libdma's `sceDmaReset`,
`sceDmaSend`, `sceDmaGetChan`) and `libgcc_null.c` (`fptodp`). Everything
else the first packages stubbed has a real host layer: the pad
(`port/input/pad_host.c`, an empty console when nothing feeds it), the memory
card (`port/save/mc_host.c`), sound (`port/audio/`), libscf
(`port/config/sysconf.c`), `debug.c` (compiled, R6a) and the movie player
(`port/fmv`). The headless build differs from the window build only in what
happens to the packets (built and not consumed), the sound and picture output
and the missing window.

## Design: stub the hardware, not the renderer layer

The headless build compiles the game's own renderer layer (all of
`seki/src`, and the sugipon and ito effect files) and stubs only what lies
under it. The game builds its GIF/VIF packets and display lists in its own
buffers (`DmaPacket.c`'s two 512 KB banks, `DisplayList.c`'s 13 lists)
exactly as on the PS2; the DMA kick and the GS are no-ops.

This differs from the first plan for 1D (no-op stubs for `gsb_*`, `gif_*`,
`tex_*`, `prim_*`, `dl_*`, `p2o_*`, `shadow_*`, `fog_*`), because reading
the callers showed the draw layer owns simulation state:

| file | state the simulation depends on |
| --- | --- |
| `seki/src/GsBase.c` | the fade state machine (`gsb_fade`: `fadeStatus`, `fadeColor`, which boot and stage changes wait on), `ScreenWidth`/`ScreenHeight`, the view matrices at `matrixptr` (`gsb_SetVSMatrix`, `gsb_MakeCommonMatrix`) that screen tests read, zoom easing, `gsb_InitGSSystem`'s `matrix_init` (sets `matrixptr`; NULL without it), `shadow_Init`, `reg_Init` |
| `sugipon/src/darkVolume.c` | `DispGameOverEffect` grows the game-over ring and sends mail 0x22 (game over) to the boy and enemies inside it |
| `ito/src/lightning.c` | `DrawLightningN` reseeds the VU0 R register (`ctc2 $vi20`), the RNG `commonact.c`, `enemy_act.c`, `queen.c` and others draw from |
| `sugipon/src/particleEffect.c` | particle state and R-register draws (`sugiRandom`) |
| `Texture.c`, `Primitive.c`, `Packet.c`, `Shadow.c`, `DmaPacket.c`, `DisplayList.c` | heap allocations (`iosMalloc`, `mallocseki`) made at model, texture and stage load, which shape the heap the simulation allocates from; pointers callers dereference (`tex_GetTextureData`, `prim_InitMesh3D`, `prim_InitParticle*`) |
| `GifPacket.c` | the `PacketBufferStruct` bookkeeping (`gif`, `end`, `tail`) that raw packet writers in `particleEffect.c`, `lightning.c`, `Primitive.c`, `Shadow.c` and `GsBase.c` write through |
| `Matrix.c`, `matrixDrive.c`, `quaternion.c`, `clothAnimation.c`, `Light.c`, `lineManager.c`, `DisplayFont.c`, `DisplayP2O.c` | plain C the simulation calls (matrix and quaternion stacks, cloth and chain physics, light lists, `font_GetHeight` for the staff roll, `p2o_MakePacket`'s `model->dobj` link) |

So Main's draw phase runs: `common/src/main.c` calls `iosOmCreateDL()` in
every build (the 1B `ico_null_create_dl()` seam is gone), and every object's
display-list callback runs against the real packet layer. The simulation
state headless is the state the renderer build will have, as far as the
renderer layer is unchanged; the renderer waves keep it so by changing what
the packets are turned into, not what the layer computes.

`tools/gen_sources.py` has no renderer list since R6a: these files, and
`common/src/debug.c` and `debug_exception.c`, are ordinary game sources of
their programmer's list in both builds; `ito/mpeg/*` is
`ICO_EE_ONLY_SOURCES` (Phase 4E: replaced by `port/fmv`, never compiled on
the host).

## Stubs: `port/null/gfx_null.c` (11)

| symbol | returns | why |
| --- | --- | --- |
| `sceGsSyncV` | GS_CSR field bit | 1B's: busy-waits for the next simulated vsync, as libgraph's; records itself as the thread's last kernel call for the diagnostics (`docs/port/BOOT_DIAG.md`) |
| `sceGsSyncPath` | 0 | paths always drained; `gsb_Init` loops until 0, `gsb_SyncGSSystem` skips the frame on nonzero |
| `sceGsResetGraph`, `sceGsResetPath` | - | no GS |
| `sceGsSetDefDBuff`, `sceGsSetDefDispEnv`, `sceGsSetHalfOffset` | - | register images nothing reads; `gsb_SetFrame` patches fields of the zeroed `db` |
| `sceGsSwapDBuff` | 0 | the flip; result unused |
| `sceDmaGetChan` | static channel block | `Basic.c` `dma_init` ORs CHCR.TIE into it |
| `sceDmaReset` | 0 | clears the channel blocks |
| `sceDmaSend` | - | `dl_Swap` and `p2o_TransMicroProgram` kick chains nobody consumes |

`seki/src/MicroCode.c`'s VU1 microprogram address table is all zero on the
host (`#ifdef ICO_HOST`): `ico2/vusrc` is assembled only by the PS2 build and
a function address does not fit its `int` slots on 64-bit hosts. Since wave
3 (R3ab) no host path reads it: `mc_TransMicroCode` chains address 0 and
`p2o_TransMicroProgram` kicks address 0 without converting an `int` to a
pointer (the values are the same as before). In the window build the VU1
programs are the `vu_*.hlsl` shaders (`docs/port/VU1_PROGRAMS.md`).

## debug.c on the host (`port/null/debug_null.c` is gone, package R6a)

Until R6a `common/src/debug.c` and `debug_exception.c` were left out (VU0
asm in the VU1 register dumps, the debug font's VU1 packets, the exception
screen) and `port/null/debug_null.c` stood in: 59 option variables with
debug.c's initialisers, a copy of `debug_VariableInit`, 27 no-op or
fixed-value functions. Both files are compiled now, with `ICO_HOST` bodies
where the EE code cannot run (docs/port/DEVELOPER_MODE.md has the detail):

| what | host |
| --- | --- |
| `debug_VariableInit` | debug.c's own (the copy is gone); the same retail values; in developer mode with `[dev] debug_option` it then loads the saved option table |
| `debug_Init` | debug.c's: clears the font window and the counters, starts EE timers 0 and 1 (`port/platform/clock.c` advances them), builds the glyph packets in a host buffer (not the stage partition) |
| `debug_SetDmaCallback` | no handler (no DMA interrupts) |
| `debug_TryToGetStartStage` | `[dev] start_stage` as before (`ICO_START_STAGE`) |
| `debug_GetTimerSec`, the bar time stamps | -1.0 and 0, as before (debug.c's `ICO_HOST` branches) |
| `debug_Printf`, `debug_PrintFontWindow`, `debug_FlushFont` | draw through the debug font (window build: `gif_HostWriteRegs`; headless: the packets are built and not consumed), as on the PS2; the retail values of the option table keep them silent |
| `debug_PrintfDummy` | empty, as retail; draws in developer mode |
| `debugSceOpen` | `cdrom0:` (nothing found), as before; `host0:` = `<pref>/dev/` in developer mode (`port/data/sifdev_host.c`) |
| `debug_SnapShot` | a PNG of rd's DISPLAY target (window build), nothing headless |
| `debug_DispVu1*Reg` | return at once (no VU1) |
| `gsResetFunc` | `gsb_Init(&db)`, 1, as the stub had it |
| `debug_assert`, `debug_assertMessage`, `debug_Assert` | `debug_exception.c`'s host bodies: print the location to the log, keep it as the last failure message and `abort()`, which the crash handler reports (docs/port/BOOT_DIAG.md), as the stub did; the PS2 hung on the exception screen |
| the exception screen, `debugExceptionInit` | not compiled; nothing installed (the host crash handler covers faults) |

`port/null/libgcc_null.c` keeps libgcc's `fptodp` (0; its callers hand it
only to debug printfs).

Measured: the headless 4000-tick boot run gives the same trace lines as
the build with the stub (docs/port/DEVELOPER_MODE.md, "Runs").

## Memory card (the null card is gone, package 4D)

`port/null/mc_null.c` (both slots empty: every request finished with -10) was
removed in 4D. Headless builds run `port/save/mc_host.c` like the window
build: a formatted card in a host folder (`saves=` in `ico-pc.ini`, else
`<exe dir>/memcard`), empty until the game saves (docs/port/SAVES.md). The
boot card check therefore finds a card with no save instead of no card, and
skips the "no memory card" sign; the language and 50/60 Hz screens still
show. The language sign's cursor starts on `[game] language` or the system
locale (`port/config/sysconf.c`, Phase 4F; `port/null/scf_null.c` is gone).

## Sound (the null driver is gone, package 4B)

`port/null/snd_null.c` (package 1E: a silent Sg API and an ADPCM read
offset advanced in simulated time) was removed in 4B. Headless builds run
the real sequencer (`port/audio/sg/sound.c`), the SNDN2DRV host and the
software SPU2 (docs/port/AUDIO.md); only the output is dropped, unless ini
`audio_dump=` writes it to a WAV. The game therefore sees what it would on
a PS2 with sound: voices report their envelopes (slots are released when
they finish), ADPCM streams advance by NAX through the one-DMA-per-tick
queue (key on in the third tick, read offsets one tick behind the DMA), and
the scripts that wait for a stream's close (`script/src/op.c:516` and the
`scpAdpcmPlayRequestNum` checks) are released by the real loop count. The
run is a function of the vsync count only, so it stays deterministic. The
1E model's tests moved to `port/audio/test/sndn2_test.c`.

## Known differences from a renderer build

- **FMV pictures:** since Phase 4E the headless build plays a film like the
  window build, for the same number of vsyncs (two per picture; the length
  depends only on the stream, `docs/port/FMV.md`, "Timing"), with its
  audio through the PCM path; the pictures are decoded
  (`ICO_FMV_DECODE=0` skips the decoding) and not shown. Until 4E the
  stubs returned at once and a film took no simulated time, so traces
  through a film differ from earlier packages' from that tick on.
- **Clip flag history:** `lightning.c`'s `clip_flags` keeps its own VU0
  clip-flag history; on the PS2 `gsb_ClipBox` shares the register. Only
  lightning's strip packets depend on it.
- **Stale vector words:** the host forms of `GifPacket.c`'s `rotTransPers`
  leave the output's 4th word unwritten (the PS2 stored a stale register
  word no caller reads).
- **Shadows (wave 4, R4b):** `Shadow.c`'s rd recording (`shadowHost*`) is
  `ICO_RD` only. The headless build compiles the file's packet code as
  before. The window build still builds the same packets, and its
  triangle buffer is a file static, not heap, so the heap and the
  simulation are the same in both builds.
- **Packets are never consumed**, so anything the game would read back from
  the GS (`Texture.c`'s store-image path) reads zeros. No simulation path
  found reads GS memory.

## Game sources changed for the host (`#ifdef ICO_HOST`, EE path unchanged)

| file | change |
| --- | --- |
| `seki/src/GsBase.c` | `gsb_ClipBox`: the two VU0 clip blocks as C (`gsb_clipCorner`) |
| `seki/src/GifPacket.c` | `rotTransPers` as C |
| `seki/src/MicroCode.c` | empty microprogram table (above); the upload chains address 0 directly (wave 3) |
| `seki/src/DisplayP2O.c` | `p2o_TransMicroProgram` kicks address 0 (wave 3) |
| `seki/src/RegistPacket.c` | `reg_transMicroCode` passes the list mask to `mc_TransMicroCode` (the PS2's one-argument K&R call left it in the second argument register; on the host the callee read garbage), with `MicroCode.h`'s prototypes (wave 3) |
| `sugipon/src/darkVolume.c` | `projectVertex`, `setScreenClamp`, `addScaledVectorXYZ` as C |
| `ito/src/lightning.c` | `clip_flags`, `apply_m34`, the R-register reseed (`ico_vu0_random_set`) as C |
| `ito/src/act_bird.c` | `Debug_WireString_Bird` uses a host `va_list` (clang has no `__builtin_next_arg`) |
| `common/src/layout_texture.c`, `kanban.c`, `layout_action.c`, `icoMisc.c`, `fumi/src/jimaku.c`, `seki/src/GsBase.c`, `sugipon/src/staticBlur.c` | the local `gif_*` externs take `GifPacket.c`'s parameter types (they shifted stack arguments on i386; `docs/port/BOOT_DIAG.md`, crash 1) |
| `fumi/ios/cdvd.c`, `fumi/isys/gobj.c` | pointer-wide ring copy; table end pointers wrap as on the EE (64-bit) |
| `common/src/main.c`, `fumi/ios/thread.c`, `common/src/kanbanBoot.c` | diagnostics hooks: milestones, thread functions, kanban steps |

Unconditional (as package 0E): the GNU nested functions of
`sugipon/src/clothAnimation.c` (8, two through context structs
`ChainStepCtx` and `Cloth4DProcCtx`), `lineManager.c` (7) and
`quaternion.c` (1) are file-scope statics, so clang compiles them.
`common/src/main.c` calls `iosOmCreateDL()` again in headless builds.
