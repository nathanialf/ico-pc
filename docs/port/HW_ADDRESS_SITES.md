# Hardware address sites (package 0B)

Sites in `ico2/` that touch EE hardware or fixed link addresses. `ICO_HOST`
is the host build's define. "Guarded" means the site is now `#ifdef ICO_HOST`
with a stub; "listed" means it is untouched and the owning package must
handle it (the macros come from `sce/libkernl/eeregs.h`, which the host
build replaces).

## Guarded

| site | what | host behaviour |
| --- | --- | --- |
| `seki/src/Packet.c` `pac_DispVu1Memory` | `0x1100C000`, VU1 data memory | body empty |
| `seki/src/GsBase.c` GS setup packet (near line 274) | `D2_QWC`, `D2_MADR`, `D2_CHCR` DMA kick | skipped |
| `seki/src/GsBase.c` `gsb_UpdateGSSystem`, two sites | `GS_CSR` field bit | `odd_even = 0` |
| `fumi/ios/message.c` `signal_handler` | `GS_CSR` field bit | `odd_even = 1` |
| `common/src/debug.c` timer-ratio and timer-count functions | `T1_MODE`, `T1_COUNT` | return -1.0f |
| `common/src/debug.c` `debug_CallbackGsFinish`, `debug_SetBar`, `debug_SetBar2` | `T0_COUNT` | 0 |
| `fumi/src/fieldCollision.c` `ResetCollisionPC`, `DispCollisionPC` | `T0_COUNT` (profile display) | 0 |
| `common/src/layout_texture.c`, `common/src/kanban.c` | `D_0030D014`, link alias of `&texProperty[0].texNo` | `#define` to that expression |
| `fumi/sound/s_init.c` | `D_005F5E60`, link alias of `&stageData[0].seEnvFirst` | `#define` to that expression |
| `seki/src/Basic.c`, `sugipon/src/particleEffect.c`, `common/src/debug_exception_screen.c.inc` | scratchpad `0x70000000` | `ICO_SPR_ADDR`, backed by `ico_scratchpad[]` |

The two `D_` symbols are not hardware: the PS2 link aliases them to table
members (`build/data/*.alias.ld`).

## Listed, not guarded

- `ito/mpeg/mv_main.c` (`D_CTRL`), `ito/mpeg/mv_vibuf.c` (`D3_*`, `D4_*`,
  `IPU_CTRL`, `IPU_BP`, DMA channel control words), `ito/mpeg/mv_disp.c`
  (`D2_TADR`, `D2_MADR`, GIF DMA): the whole movie device layer. Replace the
  module, do not patch the registers.
- `common/src/debug_exception.c`, `debug_exception_screen.c.inc`: SPR
  DMA `| 0x80000000` at `sceDmaSend(p, (self[0x4 / 4] & 0x3FF0) | 0x80000000)`
  (scratchpad-flag bit, not an address mask), `sceVif1Pk*` calls, handler
  registration (`SetDebugHandler`).
- DMA tag words built as `qwc | 0x10000000 ...` (`seki/src/DisplayList.c`,
  `seki/src/GifPacket.c`, `RegistPacket.c`, ...): tag ID bits, not addresses,
  and the VIF codes `0x11000000`, `0x13000000`, `0x15000000`. The renderer
  package owns the packet format.
- `ito/mpeg/mv_vibuf.c` `chcr = (chcr & 0x0FFFFFFF) | 0x30000000` and the
  `(id << 28)` forms: DMA CHCR/tag bits, left as is.
- Physical-address fields stored as `int` (`DlEntry.addr`, `PacWork.dmaTag`,
  `nodeMtx`, `nodeQuat`, `ito/mpeg` `phys_addr` callers): the macros are
  identity on the host, but the pointer-in-int storage needs the 64-bit
  pointer work.
