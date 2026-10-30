/*
 * port/platform/game_data_sections.h
 *
 * Forced into every game source (-include) when the compiler is clang
 * (cmake/IcoStateSections.cmake): it names the sections the game's
 * writable globals go to, so save states find them
 * (port/platform/game_data_range.h).  GCC has no such pragma; its objects
 * are renamed after the archive is made (objcopy, same file).  Read-only
 * data keeps the compiler's sections (no rodata= or relro= here), so a
 * constant table with addresses in it stays read-only.
 *
 *   ELF       ico_gdata (initialised) and ico_gbss (zero-filled, NOBITS)
 *   Mach-O    __DATA,__ico_gdata and __DATA,__ico_gbss (zerofill)
 *   COFF      .data$ico_m for both: the compile adds
 *             -fno-zero-initialized-in-bss, since lld puts an uninitialised
 *             .data$ or .bss$ section into an output section of its own,
 *             away from the brackets (game_data_range.c)
 */
#ifndef ICO_PLATFORM_GAME_DATA_SECTIONS_H
#define ICO_PLATFORM_GAME_DATA_SECTIONS_H

#if defined(__clang__)
#if defined(_WIN32)
#pragma clang section data = ".data$ico_m"
#elif defined(__APPLE__)
#pragma clang section data = "__DATA,__ico_gdata" bss = "__DATA,__ico_gbss,zerofill"
#else
#pragma clang section data = "ico_gdata" bss = "ico_gbss"
#endif
#endif

#endif /* ICO_PLATFORM_GAME_DATA_SECTIONS_H */
