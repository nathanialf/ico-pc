/*
 * port/game/test/no_shadows_census_test.c
 *
 * Registered by CORE (port/game/CMakeLists.txt) so the wave-1 package NOSHADOW
 * fills it without a CMake edit: the No Shadows census: every script site that
 * checks a Shadow in a reviewed table with its class, an unclassified site
 * failing. Until NOSHADOW merges it is this program, which checks nothing and
 * passes.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("no_shadows_census_test: ok (no checks yet: NOSHADOW's)\n");
    return 0;
}
