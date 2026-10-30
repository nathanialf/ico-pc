/*
 * port/platform/game_data_range.c: the game's .data/.bss ranges in the
 * running program (game_data_range.h; the sections are made by
 * cmake/IcoStateSections.cmake).
 */
#include "game_data_range.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
/* PE: the linkers merge every .data$<suffix> input section into .data,
   sorted by the suffix (GNU ld's default script: SORT_BY_NAME(.data$*);
   lld-link: the same for every $ group), so these three one-byte objects
   bracket the game's .data$ico_m and .data$ico_p wherever the linker puts
   .data.  GNU ld has no .bss$* rule (an orphan .bss$ section lands in a
   separate output section), which is why the game's zero-filled data is
   renamed into .data too.  The bracket bytes lie inside the ranges; a
   restore writes back the value they already hold. */
__attribute__((used, section(".data$ico_a"))) static char gdata_bracket_a[1] = {1};
__attribute__((used, section(".data$ico_n"))) static char gdata_bracket_n[1] = {1};
__attribute__((used, section(".data$ico_z"))) static char gdata_bracket_z[1] = {1};

static void bounds(uintptr_t b[4])
{
    b[0] = (uintptr_t)gdata_bracket_a;
    b[1] = (uintptr_t)gdata_bracket_n;
    b[2] = (uintptr_t)gdata_bracket_n;
    b[3] = (uintptr_t)gdata_bracket_z;
}
#elif defined(__APPLE__)
/* Mach-O: ld64 defines section$start$<seg>$<sect> and section$end$ for
   any section a program references. */
extern char gdata_lo[] __asm("section$start$__DATA$__ico_gdata");
extern char gdata_hi[] __asm("section$end$__DATA$__ico_gdata");
extern char gbss_lo[] __asm("section$start$__DATA$__ico_gbss");
extern char gbss_hi[] __asm("section$end$__DATA$__ico_gbss");

static void bounds(uintptr_t b[4])
{
    b[0] = (uintptr_t)gdata_lo;
    b[1] = (uintptr_t)gdata_hi;
    b[2] = (uintptr_t)gbss_lo;
    b[3] = (uintptr_t)gbss_hi;
}
#else
/* ELF: GNU ld and lld define __start_<name> and __stop_<name> for a
   section whose name is a C identifier when the program references them.
   Weak: a program without the sections links, and they read as 0.  Hidden:
   never looked up through the dynamic symbol table (libmain.so). */
#define ICO_GDR_WEAK __attribute__((weak, visibility("hidden")))
extern char __start_ico_gdata[] ICO_GDR_WEAK;
extern char __stop_ico_gdata[] ICO_GDR_WEAK;
extern char __start_ico_gbss[] ICO_GDR_WEAK;
extern char __stop_ico_gbss[] ICO_GDR_WEAK;

static void bounds(uintptr_t b[4])
{
    b[0] = (uintptr_t)__start_ico_gdata;
    b[1] = (uintptr_t)__stop_ico_gdata;
    b[2] = (uintptr_t)__start_ico_gbss;
    b[3] = (uintptr_t)__stop_ico_gbss;
}
#endif

int ico_game_data_ranges(IcoMemRange out[2])
{
    uintptr_t b[4];
    bounds(b);
    memset(out, 0, 2 * sizeof(*out));
    if (b[0] == 0 || b[1] <= b[0])
        return -1; /* no game data at all */
    if (b[2] == 0 && b[3] == 0) {
        b[2] = b[1]; /* no zero-filled section: an empty range */
        b[3] = b[1];
    }
    if (b[3] < b[2])
        return -1;
    if (b[2] < b[3] && b[0] < b[3] && b[2] < b[1])
        return -1; /* overlapping */
    out[ICO_GAME_DATA].lo = b[0];
    out[ICO_GAME_DATA].hi = b[1];
    out[ICO_GAME_BSS].lo = b[2];
    out[ICO_GAME_BSS].hi = b[3];
    return 0;
}

static int inside(const IcoMemRange *r, uintptr_t p, size_t n)
{
    return p >= r->lo && p <= r->hi && n <= r->hi - p;
}

static int touches(const IcoMemRange *r, uintptr_t p, size_t n)
{
    if (r->lo == r->hi)
        return 0;
    return p < r->hi && (n == 0 ? p >= r->lo : p + n > r->lo);
}

int ico_game_data_ranges_check(const IcoMemRange r[2], const IcoGameDataProbe *in, int n_in,
                               const IcoGameDataProbe *out, int n_out, char *why, size_t n)
{
    int i;
    if (why != NULL && n > 0)
        why[0] = '\0';
    for (i = 0; i < n_in; i++) {
        uintptr_t p = (uintptr_t)in[i].p;
        if (!inside(&r[ICO_GAME_DATA], p, in[i].n) && !inside(&r[ICO_GAME_BSS], p, in[i].n)) {
            if (why != NULL && n > 0)
                snprintf(why, n, "%s is outside the game's data", in[i].name);
            return -1;
        }
    }
    for (i = 0; i < n_out; i++) {
        uintptr_t p = (uintptr_t)out[i].p;
        if (touches(&r[ICO_GAME_DATA], p, out[i].n) || touches(&r[ICO_GAME_BSS], p, out[i].n)) {
            if (why != NULL && n > 0)
                snprintf(why, n, "%s is inside the game's data", out[i].name);
            return -1;
        }
    }
    return 0;
}
