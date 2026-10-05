/*
 * port/platform/fpenv_test.c
 *
 * Checks that ico_fpenv_sim_enter() rounds toward zero and flushes denormals,
 * and that ico_fpenv_host_enter() puts round-to-nearest back. Exit status 0
 * on success. Built with ICO_FPTRAP, `fpenv_test trap` checks that a
 * division by zero in simulation mode traps.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fpenv.h"

static unsigned int bits(float f)
{
    unsigned int u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float from_bits(unsigned int u)
{
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static int fails;

static void expect(const char *what, unsigned int got, unsigned int want)
{
    if (got != want) {
        printf("FAIL %s: got 0x%08X, want 0x%08X\n", what, got, want);
        fails++;
    }
}

#ifdef ICO_FPTRAP

static void on_sigfpe(int sig)
{
    (void)sig;
    /* printf is not async-signal-safe; the status says it all. */
    _Exit(0);
}

#endif

int main(int argc, char **argv)
{
    volatile float one = 1.0f, three = 3.0f;
    volatile float tiny = from_bits(0x00800000u); /* FLT_MIN */
    volatile float denorm = from_bits(0x00000001u);
    volatile float half = 0.5f, zero = 0.0f;

#ifdef ICO_FPTRAP
    /* `fpenv_test trap`: a division by zero in simulation mode must raise
       SIGFPE, whose handler exits with status 0. */
    if (argc > 1 && strcmp(argv[1], "trap") == 0) {
        signal(SIGFPE, on_sigfpe);
        ico_fpenv_sim_enter();
        printf("fpenv: no trap, 1/0 = %g\n", (double)(one / zero));
        return 1;
    }
#else
    (void)argc;
    (void)argv;
#endif

    ico_fpenv_sim_enter();
    expect("sim 1/3 rounds toward zero", bits(one / three), 0x3EAAAAAAu);
    expect("sim FLT_MIN*0.5 flushes to zero", bits(tiny * half), 0x00000000u);
    expect("sim denormal input reads as zero", bits(denorm + zero), 0x00000000u);

    ico_fpenv_host_enter();
    expect("host 1/3 rounds to nearest", bits(one / three), 0x3EAAAAABu);
    expect("host FLT_MIN*0.5 is denormal", bits(tiny * half), 0x00400000u);
    expect("host denormal input kept", bits(denorm + zero), 0x00000001u);

    printf("fpenv: %s (raw 0x%llX)\n", fails ? "FAILED" : "ok", ico_fpenv_raw());
    return fails != 0;
}
