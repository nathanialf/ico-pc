/*
 * port/platform/assert_host.c
 *
 * newlib's __assert as the game calls it (port/compat/assert.h): the same
 * message on stderr, then abort().
 */
#include <stdio.h>
#include <stdlib.h>

void ico_assert(const char *file, int line, const char *failedexpr);

void ico_assert(const char *file, int line, const char *failedexpr)
{
    fprintf(stderr, "assertion \"%s\" failed: file \"%s\", line %d\n", failedexpr, file, line);
    fflush(stderr);
    abort();
}
