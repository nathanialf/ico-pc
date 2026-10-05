# Headless stubs (`ICO_HEADLESS`)

Since renderer wave 2 (package R2a) the default `ico_pc` is the window build
(`ICO_HEADLESS=OFF`, compile definition `ICO_RD`): `GifPacket.c`,
`DisplayList.c`, `DmaPacket.c`, `DisplayFont.c` and hooks in `GsBase.c` draw
through `port/render` (`docs/port/RENDER_API.md` section 9). The headless
build stays a CMake option (`-DICO_HEADLESS=ON`; the Linux presets set it,
the Windows presets build the window) for the trace and test runs, with the
original packet code. Both builds link `port/null/gfx_null.c` and
`debug_null.c`: the window build still has no libgraph (the vsync busy-wait
and the path syncs are the same stubs), the DMA kick is not called by
`dl_Swap` there (rd replays the lists instead; `p2o_TransMicroProgram`
still calls the no-op `sceDmaSend`), and FMV and `debug.c` are later
packages'. `ICO_RENDERER_SOURCES` (`debug.c`, `debug_exception.c`,
`ito/mpeg`) are compiled in neither build until their packages port them.

What the headless `ico_pc` (package 1D) links in place of hardware and of the
two renderer-owned sources it still leaves out. The renderer waves delete
these as they land. `docs/port/BUILD_STATUS.md` has the per-preset link
status.

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

`tools/gen_sources.py` keeps these files renderer-owned (`HEADLESS_SIM`):
the renderer waves still own and rewrite them; the headless build compiles
them in both modes. `ICO_RENDERER_SOURCES` now holds only `common/src/debug.c`,
`common/src/debug_exception.c` and `ito/mpeg/*`.

## Stubs: `port/null/gfx_null.c` (13)

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
| `movie_init` | 0 | the FMV player (`ito/mpeg`, Phase 4) |
| `movie_proc` | 0 | the film "plays to the end" at once (1 would mean skipped); on the PS2 Main is held for the film's length, headless it is not |

`seki/src/MicroCode.c`'s VU1 microprogram address table is all zero on the
host (`#ifdef ICO_HOST`): `ico2/vusrc` is assembled only by the PS2 build and
a function address does not fit its `int` slots on 64-bit hosts. The
addresses reach DMA tags only.

## Stubs: `port/null/debug_null.c` (27 functions, 59 variables)

`common/src/debug.c` and `debug_exception.c` stay out (24 VU0 asm blocks of
font, bar and exception-screen drawing; renderer wave 6, then the plan's
developer mode).

- **Variables (59):** the 54 `debug_*` option variables simulation or
  renderer-layer code reads, `debug_bar_flag`, `LoadFileType` (1), and
  `Texture.c`'s statistics `texregs`, `textures`, `texturetranssize`, all
  with `debug.c`'s initialisers.
- **`debug_VariableInit`:** a copy of `debug.c:953` for those variables, in
  its order, plus `game_pause = 0` and `ChangeFieldCollisionDebugMode(0)`.
  This matters: it sets the retail values of `debug_zoom_per` (100, the
  projection scale), `debug_chain_cycle_speed`/`slow_speed` (4),
  `debug_hair_tight_level` (20), `debug_hair_bend_angle` (256),
  `debug_enemy_battle_type` (3), `debug_girl_detour_flag`,
  `debug_hand_camera`, `debug_enemy_kidnap_timer`,
  `debug_use_new_queen_battle`, `debug_stick_simulate`, `debug_mot_slope_interp`
  (5), `debug_snapshot_num` (100) and others. Assignments to variables only
  `debug.c` reads are left out. If `debug.c` changes, this copy must follow.
- **No-ops (drawing, profiling):** `debug_Init`, `debug_SetDmaCallback`,
  `debug_openLog`, `debug_closeLog`, `debug_BeginTimer`, `debug_ResetBar`,
  `debug_Printf`, `debug_PrintfDummy`, `debug_StdPrintfDummy`,
  `debug_PrintFontWindow`, `debug_FlushFont`, `debug_SESlotDisp`,
  `debug_DispQW`, `debugCdvdLoadInfoSegInit`, `debugCdvdLoadInfoSegAdd`.
- **Values:** `debug_GetTimerSec` -1.0 (as `debug.c`'s host branch),
  `debug_TryToGetStartStage` -1 (retail), `debugSceOpen`/`debugSceClose`
  through the host `sceOpen`/`sceClose` (no host files: -1),
  `gsResetFunc` calls `gsb_Init(&db)` and returns 1 as `debug.c:2364`,
  `debug_SelectCsvWindow`/`Val` -1 (cancel: only the compiled-out debug
  menu's tools call them).
- **Failures:** `debug_assert`, `debug_assertMessage`, `debug_Assert` hang
  on the PS2 (exception screen); headless they print the location to the
  log, keep it as the last failure message and `abort()`, which the crash
  handler reports (`docs/port/BOOT_DIAG.md`).
- **`fptodp`** (libgcc soft-float): 0; only passed to debug printfs.

## The null sound driver's ADPCM streams (`port/null/snd_null.c`, package 1E)

The Sg API is otherwise silent: requests are accepted and nothing ever
sounds. ADPCM streams are the exception, because the game reads their
progress back.

- **Open.** A stream opens through the cdvd background reader
  (`fumi/sound/adpcm_init.c` `AdpcmOpen` → `adpcmOpenProc` reads the first
  368 KB into IOP RAM; `AdpcmOpenSync` returns -1 until it has). The
  opening demo's skip waits on that open (`script/src/op.c:161`,
  `titleSubAdpcm`), not on the driver. It completes headless: the 1E
  `ref-m32` run reached the title and set flag 382 at tick 661.
- **Progress.** `adpcmTickProc` refills the IOP ring behind
  `SgStAdpcmIopReadAddr`. `adpcmTickProc2` counts loops from it and closes a
  stream once it has played `loopNum` times. Scripts wait for that close
  (`op.c:516` `while (adpcm_conte01_sea != 0)` after a play-once
  `scpAdpcmPlayRequestFunc`; the `scpAdpcmPlayRequestNum() != 0` checks in
  `st01b.c`, `st02a.c`, `st07a.c`, `st17a.c`, `st18a.c`, `st24a.c`). A read
  offset that never moved would hold them forever. So `snd_null.c` advances
  each playing stream as SNDN2DRV does (`docs/research/sndn2drv.md`, "ADPCM
  streams"):
  - `SgStAdpcmOpen` records the slot's channel count (`attr >> 16`), IOP
    ring size and SPU ring size (the request's last field, 0x4000);
  - `SgStAdpcmChannelPitch` sets its sample rate in Hz;
  - `SgStAdpcmPlay` counts the first half fill (read offset += SPU ring/2 ×
    channels);
  - every `SgCalledTickProc` (the sound thread runs once per vsync; PAL,
    50 Hz assumed) adds the samples played. Each time half the SPU ring
    (8 KB, 14336 samples) has played, the offset moves on by another half
    fill, modulo the IOP ring;
  - a rate of 0 (`adpcmTickProc2` while paused or the disc is not ready)
    holds the stream; `SgStAdpcmStop` and `SgStAdpcmClose` clear the
    offset.

  So a stream lasts its real length in simulated time, and the background
  reader refills its ring from the disc as on the PS2. Phase 4's
  `sndn2_host.c` replaces this with the real mixer. The model is tested in
  `port/null/test/null_devices_test.c` (`test_adpcm_stream`).
- **Not modelled.** The one-DMA-per-tick queue, so key-on comes at once, not
  on the third tick; NAX-based half tracking; PCM streams (FMV audio, Phase
  4).

## Known differences from a renderer build

- **FMV time:** films take no simulated time (above).
- **Clip flag history:** `lightning.c`'s `clip_flags` keeps its own VU0
  clip-flag history; on the PS2 `gsb_ClipBox` shares the register. Only
  lightning's strip packets depend on it.
- **Stale vector words:** the host forms of `GifPacket.c`'s `rotTransPers`
  leave the output's 4th word unwritten (the PS2 stored a stale register
  word no caller reads).
- **Packets are never consumed**, so anything the game would read back from
  the GS (`Texture.c`'s store-image path) reads zeros. No simulation path
  found reads GS memory.

## Game sources changed for the host (`#ifdef ICO_HOST`, EE path unchanged)

| file | change |
| --- | --- |
| `seki/src/GsBase.c` | `gsb_ClipBox`: the two VU0 clip blocks as C (`gsb_clipCorner`) |
| `seki/src/GifPacket.c` | `rotTransPers` as C |
| `seki/src/MicroCode.c` | empty microprogram table (above) |
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
