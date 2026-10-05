/*
 * port/input/test/pad_script_test.c
 *
 * The --pad-script reader (port/input/pad_script.c) and what the null pad
 * (port/input/pad_host.c) then shows fumi/ios/pad.c.  pad.c itself is game
 * code and is not linked: pad_dev_tick below repeats the libpad calls of
 * its controler_stable_check (pad.c:80-270) and iosPadDevReadFunc
 * (pad.c:307-353) in their order, with the same branch conditions, and
 * iosPadRead's button arithmetic (pad.c:369-386).
 */
#include "pad_script.h"
#include <libpad.h>
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

/* --- parsing ------------------------------------------------------------- */

static void test_parse(void)
{
    static const char good[] = "# boot\n"
                               "\n"
                               "  10 0040   # cross\n"
                               "14 0\r\n"
                               "20 0x8000 0 255 0x10 128\n"
                               "21 ffff\n";
    IcoPadFrame f;

    ico_pad_script_clear();
    CHECK(!ico_pad_script_active());
    CHECK(ico_pad_script_parse(good, "good") == 0);
    CHECK(ico_pad_script_active());
    CHECK(ico_pad_script_count() == 4);

    ico_pad_script_frame_at(0, &f);
    CHECK(f.buttons == 0 && f.lx == 0x80 && f.ly == 0x80 && f.rx == 0x80 && f.ry == 0x80);
    ico_pad_script_frame_at(9, &f);
    CHECK(f.buttons == 0);
    ico_pad_script_frame_at(10, &f);
    CHECK(f.buttons == ICO_PAD_CROSS && f.lx == 0x80);
    ico_pad_script_frame_at(13, &f);
    CHECK(f.buttons == ICO_PAD_CROSS);
    ico_pad_script_frame_at(14, &f);
    CHECK(f.buttons == 0);
    ico_pad_script_frame_at(20, &f);
    CHECK(f.buttons == ICO_PAD_LEFT && f.lx == 0 && f.ly == 255 && f.rx == 0x10 && f.ry == 128);
    ico_pad_script_frame_at(21, &f);
    CHECK(f.buttons == 0xFFFF && f.lx == 0x80 && f.ry == 0x80);
    ico_pad_script_frame_at(0xFFFFFFFFu, &f);
    CHECK(f.buttons == 0xFFFF);

    ico_pad_script_set_tick(12);
    CHECK(ico_pad_script_tick() == 12);
    ico_pad_script_frame(&f);
    CHECK(f.buttons == ICO_PAD_CROSS);
    ico_pad_script_set_tick(0);

    /* an empty script still plugs the pad in */
    CHECK(ico_pad_script_parse("# nothing\n\n", "empty") == 0);
    CHECK(ico_pad_script_active() && ico_pad_script_count() == 0);
    CHECK(ico_pad_script_parse("", "empty2") == 0);
    CHECK(ico_pad_script_active());
}

static void test_parse_errors(void)
{
    static const char *const bad[] = {
        "5\n",              /* one field */
        "5 40 1 2\n",       /* three or four sticks fields */
        "5 40 1 2 3 4 5\n", /* too many */
        "x 40\n",           /* tick not a number */
        "-1 40\n",          /* negative tick */
        "5 10000\n",        /* buttons over 16 bits */
        "5 zz\n",           /* buttons not hex */
        "5 40 256 0 0 0\n", /* stick over 255 */
        "5 40 -1 0 0 0\n",  /* negative stick */
        "10 40\n10 0\n",    /* equal ticks */
        "10 40\n9 0\n",     /* decreasing ticks */
        "0x10 40\n",        /* tick is decimal only */
    };
    size_t i;

    /* a failed parse keeps the previous script */
    CHECK(ico_pad_script_parse("3 40\n", "keep") == 0);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (ico_pad_script_parse(bad[i], "bad") != -1) {
            fprintf(stderr, "accepted bad script %u: %s", (unsigned)i, bad[i]);
            failures++;
        }
    }
    CHECK(ico_pad_script_active() && ico_pad_script_count() == 1);
}

static void test_load(void)
{
    const char *path = "pad_script_test.txt";
    FILE *f = fopen(path, "wb");
    IcoPadFrame fr;

    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("# file\n100 0800\n104 0\n", f);
    fclose(f);
    CHECK(ico_pad_script_load(path) == 0);
    CHECK(ico_pad_script_count() == 2);
    ico_pad_script_frame_at(103, &fr);
    CHECK(fr.buttons == ICO_PAD_START);
    remove(path);
    CHECK(ico_pad_script_load("no/such/pad_script.txt") == -1);
    CHECK(ico_pad_script_count() == 2);
}

/* --- what pad.c sees --------------------------------------------------- */

typedef struct Dev {
    int port;
    int state;
    int phase;
    int error;
    unsigned long long flags;
    int idx;
    unsigned char buf[2][32];
} Dev;

/* controler_stable_check's libpad calls (pad.c:94-242). */
static void stable_check(Dev *d)
{
    int state = scePadGetState(d->port, 0);
    int phase = d->phase;
    int mode;
    int orig;
    int exid;
    char act[6] = {0, 1, (char)255, (char)255, (char)255, (char)255};

    if (state == 0) {
        phase = 0;
    }
    switch (phase) {
    case 0:
        d->flags &= ~0x40000ull;
        phase++;
        /* fallthrough */
    case 1:
        if (state != 6 && state != 2) {
            d->error = 1;
            break;
        }
        mode = scePadInfoMode(d->port, 0, 1, 0);
        orig = mode;
        if (mode == 0) {
            break;
        }
        exid = scePadInfoMode(d->port, 0, 2, 0);
        if (exid >= 1) {
            mode = exid;
        }
        if (mode == 4) {
            phase = 40;
            if (orig != mode) {
                phase = 30;
            } else {
                d->flags |= 0x40000ull;
            }
        } else if (mode == 7) {
            phase = 70;
            if (((d->flags >> 18) & 1) == 0) {
                phase = 0;
            }
        } else {
            phase = 99;
        }
        break;
    case 40:
        if (scePadInfoMode(d->port, 0, 4, -1) == 0) {
            phase = 99;
            break;
        }
        phase++;
        /* fallthrough */
    case 41:
        if (scePadSetMainMode(d->port, 0, 1, 3) == 1) {
            phase++;
        }
        break;
    case 42:
        if (scePadGetReqState(d->port, 0) == 1) {
            phase--;
        }
        if (scePadGetReqState(d->port, 0) != 0) {
            break;
        }
        phase = 1;
        break;
    case 70:
        phase = scePadInfoPressMode(d->port, 0) == 1 ? 71 : 75;
        break;
    case 75:
        if (scePadInfoAct(d->port, 0, -1, 0) == 0) {
            phase = 99;
        }
        if (scePadSetActAlign(d->port, 0, act) != 0) {
            phase++;
        }
        break;
    case 76:
        if (scePadGetState(d->port, 0) != 5) {
            phase = 99;
        }
        break;
    default:
        break;
    }
    d->state = state;
    d->phase = phase;
}

/* iosPadDevReadFunc for one port (pad.c:316-340). Returns 1 if it read. */
static int pad_dev_tick(Dev *d)
{
    d->idx ^= 1;
    if (d->phase == 99) {
        d->error = 0;
    }
    if (d->error != 0) {
        stable_check(d);
        return 0;
    }
    if (scePadRead(d->port, 0, d->buf[d->idx]) == 0) {
        if (scePadGetState(d->port, 0) == 0) {
            d->phase = 0;
            d->error = 1;
        }
        return 0;
    }
    return 1;
}

/* iosPadRead's now and trg (pad.c:366-386, identity bit table). */
static void pad_words(const Dev *d, unsigned int *now, unsigned int *trg)
{
    const unsigned char *prev = d->buf[d->idx ^ 1];
    const unsigned char *cur = d->buf[d->idx];
    unsigned int p = ((unsigned int)(prev[2] << 8) | prev[3]) ^ 0xFFFFu;
    unsigned int c = ((unsigned int)(cur[2] << 8) | cur[3]) ^ 0xFFFFu;

    *now = d->error ? 0 : c;
    *trg = d->error ? 0 : (c ^ p) & c;
}

static void dev_init(Dev *d, int port)
{
    memset(d, 0, sizeof(*d));
    d->port = port;
    d->error = -1;
    d->state = 0xFFFF;
}

static void test_no_controller(void)
{
    static unsigned char dma[256] __attribute__((aligned(64)));
    Dev d;
    unsigned char data[32];
    int t;

    ico_pad_script_clear();
    CHECK(scePadInit(0) == 1);
    CHECK(scePadPortOpen(0, 0, dma) == 1);
    dev_init(&d, 0);
    for (t = 0; t < 50; t++) {
        CHECK(pad_dev_tick(&d) == 0);
    }
    CHECK(d.error != 0 && d.phase == 1 && d.state == 0);
    CHECK(scePadGetState(0, 0) == 0);
    CHECK(scePadInfoMode(0, 0, 1, 0) == 0);
    CHECK(scePadSetMainMode(0, 0, 1, 3) == 0);
    CHECK(scePadRead(0, 0, data) == 0);
    CHECK(data[0] == 0xFF && data[1] == 0x41 && data[2] == 0xFF && data[3] == 0xFF);
}

static void test_scripted_pad(void)
{
    static const char script[] = "0 0800\n" /* held until tick 7, never a trigger */
                                 "8 0040\n" /* CROSS */
                                 "11 0\n"
                                 "12 8840 0 0x40 0xC0 255\n"
                                 "13 0\n";
    Dev d;
    Dev d1;
    unsigned int now;
    unsigned int trg;
    int t;
    int first_read = -1;

    CHECK(ico_pad_script_parse(script, "scripted") == 0);
    CHECK(scePadInit(0) == 1);
    dev_init(&d, 0);
    dev_init(&d1, 1);
    for (t = 0; t < 14; t++) {
        int read;

        ico_pad_script_set_tick((unsigned int)t);
        read = pad_dev_tick(&d);
        CHECK(pad_dev_tick(&d1) == 0); /* port 1 stays empty */
        pad_words(&d, &now, &trg);
        if (read && first_read < 0) {
            first_read = t;
            /* START, held since tick 0, shows; the never-filled other
               buffer reads as everything held, so nothing triggers */
            CHECK(now == ICO_PAD_START && trg == 0);
        }
        if (t == 8) {
            CHECK(now == ICO_PAD_CROSS && trg == ICO_PAD_CROSS);
        }
        if (t == 9 || t == 10) {
            CHECK(now == ICO_PAD_CROSS && trg == 0); /* held: one trigger only */
        }
        if (t == 11) {
            CHECK(now == 0 && trg == 0);
        }
        if (t == 12) {
            CHECK(now == (ICO_PAD_LEFT | ICO_PAD_START | ICO_PAD_CROSS));
            CHECK(trg == now);
            /* bytes 4-7 are rx ry lx ly (fumi/include/pad.h:21-24) */
            CHECK(d.buf[d.idx][6] == 0 && d.buf[d.idx][7] == 0x40);
            CHECK(d.buf[d.idx][4] == 0xC0 && d.buf[d.idx][5] == 0xFF);
        }
    }
    /* the walk above: stable, digital, analog requested, analog, actuators,
       phase 99 after 7 checks, first read on tick 7 */
    CHECK(first_read == 7);
    CHECK(d.phase == 99 && d.error == 0);
    CHECK(d1.error != 0);
    CHECK(d.buf[d.idx][0] == 0x00 && d.buf[d.idx][1] == 0x73);
    /* (word >> 12) & 0xF of bytes 0-3 is the id's upper nibble: 7 keeps the
       sticks (pad.c:335-339) */
    CHECK(((d.buf[d.idx][1] >> 4) & 0xF) == 7);
    CHECK(scePadSetActDirect(0, 0, d.buf[0]) == 1);
    CHECK(scePadSetActDirect(1, 0, d.buf[0]) == 0);
    ico_pad_script_clear();
}

int main(void)
{
    test_parse();
    test_parse_errors();
    test_load();
    test_no_controller();
    test_scripted_pad();
    printf("pad_script_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
