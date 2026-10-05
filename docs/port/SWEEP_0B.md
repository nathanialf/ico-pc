# Package 0B report

`tools/host_syntax_check.sh` (gcc 14, `-m32 -std=gnu11 -fsyntax-only -nostdinc -fsigned-char -fno-strict-aliasing -fgnu89-inline -DICO_HOST`, include order of `tools/compile_c.sh`) passes 223 of 223
files. gcc 14 hard errors that the K&R-era code trips (incompatible pointer
types, int-to-pointer conversion, implicit declarations) are demoted to
warnings, as the host build must do; `--strict` lists the 48 files that
would fail without that. A missing `typedef.h` for an `ICO_*` macro is
reported as a failure (it would otherwise be an implicit call). The PS2 build
is byte-identical to the base ELF after every change here (sections
`.text` to `.sdata`, ELF SHA-1 equal; checked with the old
`check_elf.py`).

## 1. Front end

- `mode(TI)`: `ICO_QW`/`ICO_UQW` in `common/include/typedef.h` (7 typedefs; EnemyInit.c now includes `typedef.h`).
- Cast-as-lvalue post-increment: `ICO_POSTINC(T, p)` (15 in `debug.c`, 4 in `Primitive.c`, 2 in `RegistPacket.c`); `(int)GOBJ_SUB(s) = ...` in `commonact.c`.
- Multi-line strings: 3 in `Packet.c`, 1 in `Texture.c` (raw LF now `\n`, same bytes).
- Shadowed parameter `dist` in `NakaBoss`: parameter renamed `distArg`.
- `memset` redeclared with an `int` count: guarded `#ifndef ICO_HOST` in `layout_texture.c`, `way_util.c`.
- The 37 "initializer not constant" errors were follow-on errors and are gone.

## 2a. `long`

106 occurrences became `long long` / `unsigned long long` (identical on EE;
64-bit on Win64): `darkVolume.c` 72, `camera-editor.c` 14, `mv_vibuf.c` 3,
`StageAnimation.c` 3, `commonact.c` 2, `mcard.c` 2 (plus `1ul`),
`thread.c`/`thread.h` 4, `gobj_process.[ch]` 3, `generator.c`, `kanban.c`,
`StageManager.c`, `act-game.c` 1 each. Candidates that are really 32-bit
and could be `int`: `stackSize` parameters (thread, gobj_process),
`StageManager.c` `flags`, `kanban.c` `button`. Left alone: `%lx` with
`sceGsGetIMR()` in `mv_main.c` (324, 354).

## 2b. Address masks

`ICO_ADDR`, `ICO_PHYS`, `ICO_UNCACHED`, `ICO_UNCACHED_ACCEL` (EE forms
take an int operand and expand to the original expression; pointers go through
`ICO_ADDR`). 55 sites: `Packet.c` 17, node frees in `chain.c`,
`StageAnimation.c`, `flag.h`, `worm.c`, `box.c`, `boy.c`, `rope.c`, `cage.c`,
`enemyParts.c`, `a_p_1.c` (33), `DisplayList.c` 5, `DmaPacket.c` 2, `ZFog.c`, `Texture.c`,
`GsBase.c`, `mv_defs.h` (both helpers), `debug_exception_screen.c.inc` 5.
`mv_defs.h` keeps its line count: `alloc_zeroed`'s `__LINE__` is data.
`Packet.c` `pac_openDmaTag` keeps its `register int mask` on the EE and uses
`ICO_PHYS` under `ICO_HOST`.

Not converted (not addresses, or unsure):
- `sugipon/src/motMan_rootUpdate.c.inc:1014` `mag & 0x0FFFFFFF`, `fumi/sound/s_init.c:984` serial: flag masks.
- `mv_vibuf.c` 239, 314, 330: CHCR bits. Unsure; they carry a tag ID in bits 28 to 30.
- `sugipon/src/puddle.c:164`, `pool.c:81`: `0x30000000 | (vram / 32)` is a GS register value (ZBUF), not an address.
- DMA tag IDs in `DisplayList.c` 236 to 257.
- `s_init.c` `owner == 0xFFFFFFFF`: an unsigned handle, not a pointer.

## 2c. Sentinels

`ICO_INVALID_PTR`: `act.c` 3, `layout_action.c` 2, `st04l.c` 2. `(T *)-1` forms (`adpcm_init.c`, `script.c`, `fightSound.c`, `act2.c`) are already all-ones on any width and untouched.

## 2d. Scratchpad

`ICO_SPR_ADDR(off)`; `ico_scratchpad[16384]` (16-byte aligned) is declared in `typedef.h` and defined at the end of `seki/src/Basic.c` under `ICO_HOST`. Sites: `Basic.c` `matrix_init`, `PEWORK`, `OpenVif1DirectPacket`.

## 2e. Hardware addresses

See `HW_ADDRESS_SITES.md`.

## 2f. EE-ABI reconstructions

Upstream branch `port-prep/abi-fixes` in `/primary/dev/ico` (not pushed), each commit gated byte-identical:

- 645f64c71 gamesys: 2-argument `memcpy` x2 become 3-argument (a2 still held `size`).
- 410063857 thread: `iosThreadName(th, name)` (tail call; a1 passed through).
- 9f19f6d74 s_init: `soundSeDefPitchSet(id, pitch)` and a `SgSetSePitchDirect` prototype (pitch arrived in a1; answers the 0C note on `s_init.c:1193`).
- e2af515c5 adpcm_init: `AdpcmInterStereoVolumeSet(stream, ch)`, unread `vol` dropped, header prototyped.
- 99511c860 staticBlur: `InitStaticBlur(unused, dir)`; the binary copies `sunDir` from a1. No caller exists.
- 0318bff78 keyInput: `InitKeyInput(int unused)`; ios.c passed 0.
- 730a0fe64 jimaku: `jimakuMgrEnd(int *)` prototyped and `jimakuEnd` passes `msg` (a0 pass-through; answers the 0C note on `jimaku.c:385`).

Host-only in this tree: `ACTGame_isWeaponCombustible` (`act-game.c`, `act-game.h`) calls `CheckWeaponKind()` with no argument; the binary reaches it with the caller's `self` still in a0. Writing the argument moves the schedule of the inlined body (`.text` differs at 0x14D29C), so the EE spelling stays and `ICO_HOST` passes `self`.

Left, with reason: `_InnerProduct` in `Matrix.c:524` is asm and has no `return` (package 1A). Unprototyped externs that are consistent with their calls and need no change: `SetDebugHandler`, `GetCameraPos`, `mc_SetMicroCode`, the VU microprogram symbols, `sceVif1Pk*` in `debug_exception_screen.c.inc`, `iosFree` and `memcpy` in `Basic.c`.

## Items from the 0C audit

- `commonact.c:2820`: `1ULL << 57` on `flags18.ll`, a 64-bit field and a 64-bit `stuck` (it was `unsigned long`, now `unsigned long long`). It is a doubleword shift on the EE, not a masked 32-bit shift; bit 57 of the doubleword. No change needed.
- `fumi/src/fieldCollision.c:1563,1565` (not omori): `n[4]` and `out[4]` store past 4-float arrays; the comment there says the fourth store lands past `n`. Preserved, not changed; callers' arrays must keep a spare word.
- `common/src/layout_action.c:1338-1358`: `gflagKeepState`/`gflagRestoreState` read 20 entries of `keepFlagNo[5]` (line 149), the key-config tables follow in `.data`. They must stay contiguous and in order; the declaration comment says so. Not reordered.
- `script/src/st13c.c:974` `se` (declared `volatile int se`, line 921, never written) and `omori/src/camera-ico2.c:457` `vDbg` (declared line 305, never written, copied to `monitorCamera.dbgA`): the EE value is stack garbage. Open for the orchestrator; the `dbgA` store only feeds a debug field, `soundSeDefStop(se)` with garbage is the real risk (suggest passing -1 on the host).
- Nested functions (`gcc -Wpedantic`, 143 in 31 files; the audit counted 158 in 32, the difference is probably include-expanded duplicates):
- common/src/debug.c: 7
- fumi/src/act-env.c: 2
- fumi/src/act-game.c: 6
- fumi/src/act-parallel-control.c: 1
- fumi/src/act-way.c: 1
- fumi/src/boyact.c: 3
- fumi/src/commonact.c: 15
- fumi/src/enemy_act.c: 4
- fumi/src/girl_act.c: 19
- fumi/src/way_sys.c: 2
- omori/src/camera-editor.c: 1
- omori/src/chain.c: 1
- script/src/op.c: 1
- script/src/st22a.c: 1
- seki/src/DisplayFont.c: 2
- seki/src/FileManager.c: 1
- seki/src/Primitive.c: 5
- seki/src/RegistPacket.c: 11
- seki/src/Texture.c: 3
- sugipon/src/box.c: 2
- sugipon/src/boy.c: 2
- sugipon/src/clothAnimation.c: 11
- sugipon/src/handManager.c: 1
- sugipon/src/item.c: 1
- sugipon/src/lineManager.c: 7
- sugipon/src/motionManager.c: 1
- sugipon/src/motionManager2.c: 1
- sugipon/src/motionOrientManager.c: 1
- sugipon/src/motionViewer.c: 2
- sugipon/src/quaternion.c: 1
- sugipon/src/staticBlur.c: 27

## Open questions

- Pointer-in-int storage (nodes, `phys_addr` callers, `D2_MADR` operands) is unchanged: identity macros are correct on the 32-bit host only.
- `--strict` list (48 files) is what a gnu23 or clang-default build will reject without the demoted diagnostics.
