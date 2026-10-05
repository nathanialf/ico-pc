/*
 * port/null/test/null_devices_test.c
 *
 * The null pad, memory card, sound driver and system configuration
 * (port/null/{pad,mc,snd,scf}_null.c) answer as an empty console would.
 */
#include "null_devices.h"
#include "sif_host.h"

#include <libmc.h>
#include <libpad.h>
#include <libscf.h>
#include <sifrpc.h>
#include <sound.h>

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

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

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

static void test_snd(void)
{
    static unsigned char page[ICO_SND_REPLY_SIZE];
    static unsigned char pk[3 * 16];
    sceSifRpcClientData cd;
    int i;
    int nonzero;

    ico_sif_host_reset();
    /* the EE side: bind, init, tick */
    CHECK(SgSndn2RemoteInit() == 0);
    CHECK(SgSndn2RemoteSync() == 0);
    SgInit();
    SgCalledTickProc();
    CHECK(ico_snd_null_last_reply() != NULL);
    CHECK(SgGetDmaTransferStatus(1) == 1);
    CHECK(SgGetSlotStatus(1, 0) == 0);
    CHECK(SgSePlay(0, 0, 0) >= 0);

    /* the IOP side through a client of its own: a tick page with a key-on,
       an upload (counter 5) and a download (counter 6) */
    memset(&cd, 0, sizeof(cd));
    CHECK(sceSifBindRpc(&cd, ICO_SND_SERVER_ID, 0) == 0 && cd.serve != 0);
    memset(pk, 0, sizeof(pk));
    put32(pk + 0, 0x01);
    put32(pk + 16, 0x20);
    put32(pk + 20, 5u << 8 | 0x12);
    put32(pk + 32, 0x21);
    put32(pk + 36, 6u << 8 | 0x34);
    memset(page, 0xAA, sizeof(page));
    CHECK(sceSifCallRpc(&cd, 0x64, 1, pk, (int)sizeof(pk), page, ICO_SND_REPLY_SIZE, 0, 0) == 0);
    CHECK(sceSifCheckStatRpc(&cd) == 0);
    CHECK(rd32(page + 0x1C0) == 6);
    nonzero = 0;
    for (i = 0; i < ICO_SND_REPLY_SIZE; i++) {
        if (i < 0x1C0 || i >= 0x1C4) {
            nonzero |= page[i];
        }
    }
    CHECK(nonzero == 0);
    /* the next page (the other half of the double buffer) keeps the counter */
    memset(page, 0xAA, sizeof(page));
    CHECK(sceSifCallRpc(&cd, 0x64, 1, pk, 0, page, ICO_SND_REPLY_SIZE, 0, 0) == 0);
    CHECK(rd32(page + 0x1C0) == 6 && page[0] == 0);

    /* an unregistered server never answers the bind */
    memset(&cd, 0, sizeof(cd));
    CHECK(sceSifBindRpc(&cd, 0x12345678u, 0) == 0 && cd.serve == 0);
    CHECK(sceSifCallRpc(&cd, 0, 0, NULL, 0, NULL, 0, 0, 0) < 0);
    ico_sif_host_reset();
}

int main(void)
{
    test_pad();
    test_mc();
    test_scf();
    test_snd();
    printf("null_devices_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
