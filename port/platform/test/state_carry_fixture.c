/*
 * port/platform/test/state_carry_fixture.c
 *
 * The program tools/state_carry_check.py's self-test links in each layout
 * (docs/port/TESTING.md, "state_carry_check"): an ELF shared library (read-
 * only data in its own segment, then inside the executable one, with GNU ld
 * and with lld as the NDK links libmain.so) and a Windows program.  Never
 * run: only its file is read.  It has what the guard looks at in the game:
 * a game data table of initialised pointers (the game's ico_gdata, or
 * .data$ico_m between game_data_range.c's brackets on Windows), the anchor
 * function the symbol files are placed by, and the targets issue 217's
 * class A holds: short texts, a long one, an EUC-JP one, a pointer into a
 * constant table and a function.
 *
 * The game table holds 7 pointers to 6 targets (the short text twice:
 * state_carry_check.py FIXTURE_TARGETS).  carry_first and carry_hidden come
 * first in the file and every compile keeps the file's order
 * (-fno-toplevel-reorder with gcc; clang keeps it): the self-test's
 * negative case drops carry_hidden from the symbol file and bounds
 * carry_first by its own size (an nm -S line; on Windows its own input
 * section, -ffunction-sections), so the table's pointer to carry_hidden lies
 * in code no unit holds, inside the code range however it is derived: the
 * one word a save cannot name.
 */
#if defined(_WIN32)
#define CARRY_EXPORT __declspec(dllexport)
#define CARRY_GAME __attribute__((used, section(".data$ico_m")))
/* game_data_range.c's brackets around the game's data (the self-test's
   program has no game_data_range.c: these are its own) */
__attribute__((used, section(".data$ico_a"))) char carry_bracket_a[1] = {1};
__attribute__((used, section(".data$ico_n"))) char carry_bracket_n[1] = {1};
__attribute__((used, section(".data$ico_z"))) char carry_bracket_z[1] = {1};
#else
#define CARRY_EXPORT __attribute__((visibility("default")))
#define CARRY_GAME __attribute__((used, section("ico_gdata")))
#endif

CARRY_EXPORT __attribute__((noinline)) int carry_first(int x)
{
    return x * 5 + 7;
}

CARRY_EXPORT __attribute__((noinline)) int carry_hidden(int x)
{
    return x * 3 + 1;
}

/* a constant table, pointed into (a local symbol at most: none in a GNU ld
   map) */
static const int carry_consts[8] = {11, 22, 33, 44, 55, 66, 77, 88};

CARRY_EXPORT const void *carry_game_table[7] CARRY_GAME = {
    "On",
    "On",
    "a text of sixty bytes: longer than the old record's 24 bytes",
    "\xa5\xb5\xa5\xa6\xa5\xf3\xa5\xc9\xa4\xce\xc0\xdf\xc4\xea",
    &carry_consts[2],
    (const void *)carry_hidden,
    "Off",
};

/* the symbol files' anchor and check (state_host.c sym_table) */
CARRY_EXPORT __attribute__((noinline)) int ico_diag_init(int x)
{
    return carry_first(x) + carry_hidden(x) + carry_consts[x & 7];
}

CARRY_EXPORT __attribute__((noinline)) int ico_diag_log(int x)
{
    return ico_diag_init(x) ^ 5;
}

/* the Windows program's entry (-nostartfiles: it is never run) */
CARRY_EXPORT int carry_entry(void)
{
    return ico_diag_log((int)(long long)carry_game_table[0]);
}
