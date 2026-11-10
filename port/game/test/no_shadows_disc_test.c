/*
 * port/game/test/no_shadows_disc_test.c
 *
 * Registered by CORE (port/game/CMakeLists.txt) so the wave-1 package NOSHADOW
 * fills it without a CMake edit: No Shadows off each disc image: each disc's
 * placed Shadows take the hidden branch, every kidnap-timer stage is covered
 * by the backStage guard, the Queen's stage has no Shadow rows. Until NOSHADOW
 * merges it is this program, which checks nothing and passes.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("no_shadows_disc_test: ok (no checks yet: NOSHADOW's)\n");
    return 0;
}
