# Hardware address sites

Places in `ico2/` that touch EE hardware or fixed link addresses, and what
the host build does at each. `ICO_HOST` is the host build's define. The
register macros come from `port/compat/eeregs.h` on the host (the EE build
uses `sce/libkernl/eeregs.h`); the register pages are plain memory
(`port/platform/hwregs.c`), and the EE timer counters in them advance in
simulated time (`port/platform/clock.c`; PLATFORM.md, "EE timers").

## Guarded under `ICO_HOST`

| site | what | host behaviour |
| --- | --- | --- |
| `seki/src/Packet.c` `pac_DispVu1Memory` | `0x1100C000`, VU1 data memory | body empty |
| `seki/src/GsBase.c` GS setup packet | `D2_QWC`, `D2_MADR`, `D2_CHCR` DMA kick | skipped |
| `seki/src/GsBase.c` `gsb_UpdateGSSystem`, two sites | `GS_CSR` field bit | `odd_even = 0` |
| `fumi/ios/message.c` `signal_handler` | `GS_CSR` field bit | the host sets `GS_CSR.FIELD` in `ico_vsync` before raising the vblank, and the handler reads it |
| `common/src/layout_texture.c`, `common/src/kanban.c` | `D_0030D014`, link alias of `&texProperty[0].texNo` | `#define` to that expression (and the rows written by field, OFFSET_AUDIT.md) |
| `fumi/sound/s_init.c` | `D_005F5E60`, link alias of `&stageData[0].seEnvFirst` | `#define` to that expression |
| `seki/src/Basic.c`, `sugipon/src/particleEffect.c`, `common/src/debug_exception_screen.c.inc` | scratchpad `0x70000000` | `ICO_SPR_ADDR(off)`, backed by `ico_scratchpad[16384]` (16-byte aligned, declared in `typedef.h`, defined in `seki/src/Basic.c`) |
| `common/src/debug.c` `debug_GetTimerSec` and the bar time stamps | EE timer 1 | -1.0f and 0 (profiler displays only) |
| address masks (`Packet.c`, `DisplayList.c`, `DmaPacket.c`, node frees in the actor files, ...) | `& 0x0FFFFFFF`, `| 0x20000000`, `| 0x30000000` physical and uncached aliases | `ICO_ADDR`, `ICO_PHYS`, `ICO_UNCACHED`, `ICO_UNCACHED_ACCEL` (`typedef.h`): the original expression on the EE, the identity on the host |
| sentinels | `(T *)0xFFFFFFFF` | `ICO_INVALID_PTR`; the `(T *)-1` forms are all-ones on any width and untouched |

The two `D_` symbols are not hardware: the PS2 link aliases them to table
members.

## Not guarded, and why

- `ito/mpeg/` (`D_CTRL`, `D3_*`, `D4_*`, `IPU_CTRL`, `IPU_BP`, `D2_TADR`,
  GIF DMA): the PS2 movie player. It is `ICO_EE_ONLY_SOURCES` and never
  compiled on the host; `port/fmv` replaces the whole module (FMV.md).
- `common/src/debug_exception.c`, `debug_exception_screen.c.inc`: the
  exception screen's SPR DMA (`| 0x80000000` is the scratchpad flag bit,
  not an address mask), `sceVif1Pk*` calls and handler registration are not
  compiled on the host (DEVELOPER_MODE.md); the host's crash handler covers
  faults (BOOT_DIAG.md).
- DMA tag words built as `qwc | 0x10000000 ...` (`seki/src/DisplayList.c`,
  `GifPacket.c`, `RegistPacket.c`, ...) and the VIF codes `0x11000000`,
  `0x13000000`, `0x15000000`: tag and command bits, not addresses. The
  renderer reads them (`mc_HostDma`, RENDER_API.md).
- Flag masks that look like address masks but are not:
  `motMan_rootUpdate.c.inc`'s `mag & 0x0FFFFFFF`, `s_init.c`'s serial,
  `puddle.c` and `pool.c`'s `0x30000000 | (vram / 32)` (a GS ZBUF value),
  `s_init.c`'s `owner == 0xFFFFFFFF` (an unsigned handle).
