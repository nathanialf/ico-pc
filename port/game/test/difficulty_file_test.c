/*
 * port/game/test/difficulty_file_test.c
 *
 * Registered by CORE (port/game/CMakeLists.txt) so the wave-1 package SAVE
 * fills it without a CMake edit: the difficulty sidecar
 * (port/game/difficulty_file.c, difficulty.toml): the entries per disc, card
 * and slot, the look-up by sum, the one-time carry-over, the finished list,
 * the Continue list's preview match and the hooks in layout_action.c. Until
 * SAVE merges it is this program, which checks nothing and passes.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("difficulty_file_test: ok (no checks yet: SAVE's)\n");
    return 0;
}
