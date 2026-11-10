/*
 * port/game/test/knockdown_disc_test.c
 *
 * Registered by CORE (port/game/CMakeLists.txt) so the wave-1 package KNOCK
 * fills it without a CMake edit: the knock-down limit off each disc image:
 * mails 55/56 and the down act on the disc's interrupt tables. Until KNOCK
 * merges it is this program, which checks nothing and passes.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("knockdown_disc_test: ok (no checks yet: KNOCK's)\n");
    return 0;
}
