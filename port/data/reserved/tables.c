/*
 * port/data/reserved/tables.c
 *
 * The room the table loader's statics took in the game's data sections
 * until they moved out (port/data/tables.c holds port state set once at
 * start-up, not the game's; docs/port/PLATFORM.md "Across versions"):
 * every game variable after it keeps its offset in the game's ranges, so
 * a save state of an earlier version still carries over (the Windows
 * program holds this object's bss in the middle of the game's).  The
 * object keeps the loader's file name, tables.c, which a GNU ld map names
 * its input section by, and the loader's footprint of that time, per
 * compiler: GCC laid its five statics out in 0x614 bytes aligned to 32 (a
 * COFF object rounds the section to 0x620), clang in 0x600 bytes aligned
 * to 16 on x86-64 and to 8 on arm64.  Frozen: change nothing here (the
 * round 5 hand-back of issue 217 compares the objects' sections).
 */
#include <stdint.h>

#if defined(__clang__)
#define ICO_ROOM_BYTES 0x600
#if defined(__x86_64__)
#define ICO_ROOM_ALIGN 16
#else
#define ICO_ROOM_ALIGN 8
#endif
#else
#define ICO_ROOM_BYTES 0x614
#define ICO_ROOM_ALIGN 32
#endif

__attribute__((used, aligned(ICO_ROOM_ALIGN))) static uint8_t ico_tables_room_bytes[ICO_ROOM_BYTES];

/* An archive member only joins the link when something refers to it: the
   program refers to this (state_host.c) as it referred to the loader that
   stood here, so the linker takes the member at the place the loader's
   was taken. */
const void *ico_tables_room(void);

const void *ico_tables_room(void)
{
    return ico_tables_room_bytes;
}
