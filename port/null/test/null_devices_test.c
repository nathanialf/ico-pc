/*
 * port/null/test/null_devices_test.c
 *
 * The null pad, memory card and system configuration
 * (port/null/{pad,mc,scf}_null.c) answer as an empty console would.  The
 * sound driver's tests moved to port/audio/test/sndn2_test.c with the real
 * driver (Phase 4B).
 */
#include "null_devices.h"
#include <libmc.h>
#include <libpad.h>
#include <libscf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_pad(void)
{
    static unsigned char dma[256] __attribute__((aligned(64)));
    unsigned char data[32];

    /* fumi/ios/pad.c's iosPadDevInit asserts on these two */
    CHECK(scePadInit(0) == 1);
    CHECK(scePadPortOpen(0, 0, dma) == 1);
    CHECK(scePadPortOpen(1, 0, dma) == 1);
    /* controler_stable_check: state 0 keeps the port in its error state */
    CHECK(scePadGetState(0, 0) == 0);
    CHECK(scePadGetState(1, 0) == 0);
    CHECK(scePadInfoMode(0, 0, 1, 0) == 0);
    CHECK(scePadRead(0, 0, data) == 0);
    CHECK(data[0] == 0xFF && data[2] == 0xFF && data[3] == 0xFF);
    CHECK(scePadSetActDirect(0, 0, data) == 0);
}

static void test_mc(void)
{
    int type = 99, free = 99, format = 99;
    int cmd = 0, result = 0;

    CHECK(sceMcInit() == 0);
    CHECK(sceMcSync(1, &cmd, &result) == -1); /* nothing pending */
    CHECK(sceMcGetInfo(0, 0, &type, &free, &format) == 0);
    CHECK(type == 0 && free == 0 && format == 0);
    CHECK(sceMcSync(1, &cmd, &result) == 1);
    CHECK(cmd == 1 && result == ICO_MC_NULL_RESULT);
    CHECK(sceMcSync(1, &cmd, &result) == -1);
    CHECK(sceMcOpen(0, 0, "/BESCES-50760ico/game.0", 1) == 0);
    CHECK(sceMcSync(0, &cmd, &result) == 1 && cmd == 2 && result < 0);
    CHECK(sceMcChdir(0, 0, "/BESCES-50760ico", NULL) == 0);
    CHECK(sceMcSync(1, &cmd, &result) == 1 && cmd == 0x0C && result == ICO_MC_NULL_RESULT);
}

static void test_scf(void)
{
    CHECK(sceScfGetLanguage() == ICO_SCF_LANGUAGE_ENGLISH);
    ico_scf_language = ICO_SCF_LANGUAGE_GERMAN;
    CHECK(sceScfGetLanguage() == 4);
    ico_scf_language = ICO_SCF_LANGUAGE_ENGLISH;
}

int main(void)
{
    test_pad();
    test_mc();
    test_scf();
    printf("null_devices_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
