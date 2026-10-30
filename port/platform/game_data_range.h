/*
 * port/platform/game_data_range.h
 *
 * Where the game's own globals (the .data and .bss of every object in the
 * game library ico_game: ico2/ and the data tables) lie in the running
 * program, for save states (port/platform/state.h: the GDATA and GBSS
 * sections).  The build gathers them into sections of their own
 * (cmake/IcoStateSections.cmake, docs/port/PLATFORM.md "Game data ranges"):
 *
 *   ELF (Linux, Android)   ico_gdata and ico_gbss, read through the
 *                          linker's __start_ / __stop_ symbols
 *   PE (Windows)           .data$ico_m (initialised) and .data$ico_p (the
 *                          game's zero-filled data, stored in the file),
 *                          between one-byte brackets in .data$ico_a,
 *                          .data$ico_n and .data$ico_z (game_data_range.c);
 *                          with clang both are in .data$ico_m and the bss
 *                          range is only the .data$ico_n bracket
 *   Mach-O (macOS, iOS)    __DATA,__ico_gdata and __DATA,__ico_gbss, read
 *                          through ld64's section$start$ / section$end$
 *
 * The port's own globals stay in the ordinary sections, outside both
 * ranges.  A build where the gathering did not happen (a toolchain that
 * left the sections alone, a test program without the game) reports no
 * ranges or fails the verify below, and save states turn themselves off.
 *
 * The build id (state_file.c) hashes the two ranges' sizes as returned by
 * ico_game_data_ranges (hi - lo of each, data then bss): they are link-time
 * constants of one program, so no separate hash function is exported.
 */
#ifndef ICO_PLATFORM_GAME_DATA_RANGE_H
#define ICO_PLATFORM_GAME_DATA_RANGE_H

#include <stddef.h>
#include <stdint.h>

/* [lo, hi): lo the first byte, hi one past the last. */
typedef struct IcoMemRange {
    uintptr_t lo;
    uintptr_t hi;
} IcoMemRange;

enum { ICO_GAME_DATA = 0, ICO_GAME_BSS = 1 };

/* out[ICO_GAME_DATA], out[ICO_GAME_BSS]: the game's initialised data and
   its zero-filled data.  0, or -1 (out zeroed) when this program has no
   such sections or they are inconsistent: an empty data range, a range
   whose end is below its start, or the two ranges overlapping.  The bss
   range may be empty (clang's Windows build stores it in the data range). */
int ico_game_data_ranges(IcoMemRange out[2]);

/* One object a verify checks: its name (for the failure text), address
   and size. */
typedef struct IcoGameDataProbe {
    const char *name;
    const void *p;
    size_t n;
} IcoGameDataProbe;

/* 0 when every `in` object lies wholly inside one of r's two ranges and
   every `out` object wholly outside both; else -1 with `why` (n bytes,
   NUL-terminated) naming the first offender, e.g. "systemStatus is outside
   the game's data" or "ico_iop_ram is inside the game's data". */
int ico_game_data_ranges_check(const IcoMemRange r[2], const IcoGameDataProbe *in, int n_in,
                               const IcoGameDataProbe *out, int n_out, char *why, size_t n);

/* The program's own check (game_data_verify.c, linked into ico_pc only):
   ico_game_data_ranges, then ten of the game's globals (systemStatus,
   stage_no, stagePreLoadBuff, gameSysMemoryFuncList, gameSysMainSaveBuff,
   frame_count, gFlagGameClear, two data tables from port/data/gen/
   table_defs.c, ico_scratchpad) must lie inside and five of the port's
   (the scheduler's tables, ico_iop_ram, ico_mv_active, a data and a bss
   variable of ico_pc's own) outside, so a bracket that misses the game or
   swallows the port is caught.  0, or -1 with `why` filled ("no game data
   sections in this program" or the check's text).  Run once at start-up
   by state_host.c, which logs the failure and turns save states off. */
int ico_game_data_ranges_verify(char *why, size_t n);

#endif /* ICO_PLATFORM_GAME_DATA_RANGE_H */
