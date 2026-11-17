/*
 * port/data/reserved/tables.c
 *
 * The room the table loader's statics took in the game's data sections
 * until they moved out (port/data/tables.c holds port state set once at
 * start-up, not the game's; docs/port/PLATFORM.md "Game data ranges"), so
 * every game variable keeps its offset in the game's ranges and the game
 * data layout hash (state_syms.h) stays the one the versions before it
 * saved under:
 *
 *   clang, ELF (Android, linux-x64-clang)  the loader's five statics under
 *       their names, sizes and offsets, which Android's symbol file lists
 *       and the hash covers: loaded_count (0x1ec bytes at 0), loaded_set (8
 *       at 0x1f0), loaded_rows (4 at 0x1f8), loaded_records (4 at 0x1fc),
 *       ico_tables_crc32.table (0x400 at 0x200), aligned to 8 on arm64 and
 *       16 on x86-64: written in assembly, so no compiler can reorder or drop
 *       them;
 *   GCC (Linux, Windows)  its statics as the loader declared them: GCC laid
 *       them out in 0x614 bytes aligned to 32 (a COFF object rounds the
 *       section to 0x620), and GNU ld's map, which the hash reads there,
 *       names the input section (tables.c) and its size only;
 *   clang, other formats  0x600 bytes aligned as above.
 *
 * Frozen: change nothing here (the hand-back of issue 217's round 6
 * compares these objects with the loader's, toolchain by toolchain).
 */
#include <stdint.h>

#if defined(__clang__) && defined(__ELF__)

#if defined(__x86_64__)
#define ICO_ROOM_P2ALIGN "4"
#else
#define ICO_ROOM_P2ALIGN "3"
#endif

__asm__(".pushsection ico_gbss,\"aw\",%nobits\n"
        ".p2align " ICO_ROOM_P2ALIGN "\n"
        "loaded_count:\n"
        ".zero 0x1ec\n"
        ".type loaded_count,%object\n"
        ".size loaded_count,0x1ec\n"
        ".p2align 3\n"
        "loaded_set:\n"
        ".zero 8\n"
        ".type loaded_set,%object\n"
        ".size loaded_set,8\n"
        "loaded_rows:\n"
        ".zero 4\n"
        ".type loaded_rows,%object\n"
        ".size loaded_rows,4\n"
        "loaded_records:\n"
        ".zero 4\n"
        ".type loaded_records,%object\n"
        ".size loaded_records,4\n"
        "ico_tables_crc32.table:\n"
        ".zero 0x400\n"
        ".type ico_tables_crc32.table,%object\n"
        ".size ico_tables_crc32.table,0x400\n"
        ".popsection\n");

#elif defined(__clang__)

#if defined(__x86_64__)
#define ICO_ROOM_ALIGN 16
#else
#define ICO_ROOM_ALIGN 8
#endif
__attribute__((used, aligned(ICO_ROOM_ALIGN))) static uint8_t ico_tables_room_bytes[0x600];

#else

/* the loader's ICO_TID_COUNT of that time */
#define ICO_ROOM_TABLE_IDS 123

__attribute__((used)) static uint32_t loaded_rows, loaded_records;
__attribute__((used)) static uint32_t loaded_count[ICO_ROOM_TABLE_IDS];
__attribute__((used)) static const void *loaded_set;

/* the CRC table was a static of the loader's CRC function */
__attribute__((used)) static uint32_t ico_tables_crc32_table(void)
{
    __attribute__((used)) static uint32_t table[256];

    return table[1];
}

#endif

/* An archive member only joins the link when something refers to it: the
   program refers to this (state_host.c) as it referred to the loader that
   stood here, so the linker takes the member at the place the loader's
   was taken.  It returns the room's address so that its section is
   referred to as well: --gc-sections keeps an input section only when a
   kept section refers to it, and the Android link (v0.11.9-rc4) dropped
   the room while keeping this function when it returned its own address
   (tools/android_syms_lines.py layout now fails such a package). */
const void *ico_tables_room(void);

const void *ico_tables_room(void)
{
#if defined(__clang__) && defined(__ELF__) && defined(__aarch64__)
    const void *p;

    __asm__("adrp %0, loaded_count\n\tadd %0, %0, :lo12:loaded_count" : "=r"(p));
    return p;
#elif defined(__clang__) && defined(__ELF__) && defined(__x86_64__)
    const void *p;

    __asm__("leaq loaded_count(%%rip), %0" : "=r"(p));
    return p;
#elif defined(__clang__) && defined(__ELF__)
#error "the room's address: add this architecture's instruction"
#elif defined(__clang__)
    return ico_tables_room_bytes;
#else
    return loaded_count;
#endif
}
