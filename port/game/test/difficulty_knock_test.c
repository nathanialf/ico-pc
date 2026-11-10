/*
 * port/game/test/difficulty_knock_test.c
 *
 * Registered by CORE (port/game/CMakeLists.txt) so the wave-1 package KNOCK
 * fills it without a CMake edit: the knock-down limit's game side: the record
 * driven through gamestate.c's synthetic sampler, the game over event, the in-
 * fall latch from the snapshot, and the hooks in commonact.c, StageManager.c,
 * host_loop.c and gamestate.c. Until KNOCK merges it is this program, which
 * checks nothing and passes.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("difficulty_knock_test: ok (no checks yet: KNOCK's)\n");
    return 0;
}
