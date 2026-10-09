/*
 * port/input/test/input_record_test.c
 *
 * The pad recording (port/input/input_record.c): its line
 * format, the round trip through the pad script reader (pad_script.c) for a
 * long varied session, the "only when it changes" rule, a second read in
 * the same tick, the poll's timing, and the sample point in scePadRead
 * (pad_host.c): a scripted session recorded through libpad and loaded back
 * gives the same value at every tick.
 *
 *   input_record_test DIR    (DIR: where the recordings are written)
 */
#include <libpad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_record.h"
#include "pad_script.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char dir[1024];

static int same(const IcoPadFrame *a, const IcoPadFrame *b)
{
    return a->buttons == b->buttons && a->lx == b->lx && a->ly == b->ly && a->rx == b->rx &&
           a->ry == b->ry;
}

static void path_in_dir(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", dir, name);
}

/* the file's lines that are not comments */
static int data_lines(const char *path, int *comments)
{
    FILE *f = fopen(path, "rb");
    char line[256];
    int n = 0;

    *comments = 0;
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#') {
            (*comments)++;
        } else if (line[0] != '\n') {
            n++;
        }
    }
    fclose(f);
    return n;
}

static void test_format(void)
{
    char line[64];
    const IcoPadFrame f = {ICO_PAD_LEFT | ICO_PAD_START | ICO_PAD_CROSS, 0, 0x40, 0xC0, 255};
    const IcoPadFrame r = {0, 128, 128, 128, 128};

    CHECK(ico_input_record_format(line, sizeof(line), 12, &f) == (int)strlen(line));
    CHECK(strcmp(line, "12 8840 0 64 192 255\n") == 0);
    ico_input_record_format(line, sizeof(line), 4000000000u, &r);
    CHECK(strcmp(line, "4000000000 0000 128 128 128 128\n") == 0);
}

/* a deterministic session: runs of held values, sticks sweeping */
static unsigned int rng = 12345u;

static unsigned int next(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFFu;
}

#define SESSION_TICKS 5000

static IcoPadFrame session[SESSION_TICKS];

static void make_session(void)
{
    IcoPadFrame cur = {0, 128, 128, 128, 128};
    int t;

    for (t = 0; t < SESSION_TICKS; t++) {
        unsigned int r = next();

        if (r % 7 == 0) {
            cur.buttons = next() & 0xFFFFu;
        }
        if (r % 5 == 0) {
            cur.lx = (unsigned char)(next() & 0xFF);
            cur.ly = (unsigned char)(next() & 0xFF);
        }
        if (r % 11 == 0) {
            cur.rx = (unsigned char)(next() & 0xFF);
            cur.ry = (unsigned char)(next() & 0xFF);
        }
        if (r % 13 == 0) {
            cur.buttons = 0;
            cur.lx = cur.ly = cur.rx = cur.ry = 128;
        }
        session[t] = cur;
    }
}

static void test_round_trip(void)
{
    char path[1100];
    IcoPadFrame f;
    int changes = 0;
    int comments = 0;
    int t;
    const IcoPadFrame released = {0, 128, 128, 128, 128};

    make_session();
    for (t = 0; t < SESSION_TICKS; t++) {
        if (!same(&session[t], t == 0 ? &released : &session[t - 1])) {
            changes++;
        }
    }
    path_in_dir(path, sizeof(path), "input-roundtrip.txt");
    CHECK(!ico_input_record_active());
    CHECK(ico_input_record_open(path, "# a header line\n# another\n") == 0);
    CHECK(ico_input_record_active());
    for (t = 0; t < SESSION_TICKS; t++) {
        /* two vsyncs a tick, as in PAL: the read in the first, polls in both */
        ico_input_record_sample((unsigned int)t, &session[t]);
        ico_input_record_poll((unsigned int)t);
        ico_input_record_poll((unsigned int)t + 1);
    }
    ico_input_record_close();
    CHECK(!ico_input_record_active());
    ico_input_record_close(); /* idempotent */
    CHECK(ico_input_record_lines() == (unsigned int)changes);
    CHECK(data_lines(path, &comments) == changes);
    CHECK(comments == 3);                             /* the header's two and the closing line */
    CHECK(changes > 1000 && changes < SESSION_TICKS); /* the session does vary, and repeats */

    CHECK(ico_pad_script_load(path) == 0);
    CHECK(ico_pad_script_count() == changes);
    for (t = 0; t < SESSION_TICKS; t++) {
        ico_pad_script_frame_at((unsigned int)t, &f);
        if (!same(&f, &session[t])) {
            fprintf(stderr, "tick %d differs\n", t);
            failures++;
            break;
        }
    }
    /* past the end the last value holds, as it did */
    ico_pad_script_frame_at(SESSION_TICKS + 100, &f);
    CHECK(same(&f, &session[SESSION_TICKS - 1]));
    ico_pad_script_clear();
}

static void test_rules(void)
{
    char path[1100];
    char text[512];
    FILE *fp;
    size_t n;
    const IcoPadFrame rel = {0, 128, 128, 128, 128};
    const IcoPadFrame cross = {ICO_PAD_CROSS, 128, 128, 128, 128};
    const IcoPadFrame start = {ICO_PAD_START, 128, 128, 128, 128};

    path_in_dir(path, sizeof(path), "input-rules.txt");
    /* without a recording: nothing happens */
    ico_input_record_sample(3, &cross);
    ico_input_record_poll(4);
    CHECK(ico_input_record_open(path, NULL) == 0);
    ico_input_record_sample(7, &rel); /* the default: no line */
    ico_input_record_poll(8);
    ico_input_record_sample(8, &cross);
    ico_input_record_poll(8); /* tick 8 still running: not written yet */
    CHECK(ico_input_record_lines() == 0);
    ico_input_record_poll(9);
    CHECK(ico_input_record_lines() == 1);
    ico_input_record_sample(9, &cross); /* unchanged: no line */
    ico_input_record_sample(10, &start);
    ico_input_record_sample(10, &cross); /* read twice: the last read is kept */
    ico_input_record_sample(10, &cross);
    ico_input_record_sample(5, &start); /* before the held tick: ignored */
    ico_input_record_sample(11, &rel);
    ico_input_record_close(); /* writes tick 11's line */
    CHECK(ico_input_record_lines() == 2);
    fp = fopen(path, "rb");
    CHECK(fp != NULL);
    if (fp != NULL) {
        n = fread(text, 1, sizeof(text) - 1, fp);
        text[n] = '\0';
        fclose(fp);
        CHECK(strcmp(text, "8 0040 128 128 128 128\n"
                           "11 0000 128 128 128 128\n"
                           "# end at tick 11: 2 lines, 1 ticks read twice with different "
                           "values\n") == 0);
    }
}

/* scePadRead's sample point: a scripted pad read through libpad as the game
   reads it once a tick (pad_host.c), recorded, then loaded as the script:
   the same value at every tick from the first read on. */
static void test_through_libpad(void)
{
    static const char script[] = "0 0800\n"
                                 "8 0040\n"
                                 "11 0\n"
                                 "12 8840 0 0x40 0xC0 255\n"
                                 "13 0\n"
                                 "40 1000 128 0 128 128\n"
                                 "41 1000 128 1 128 128\n"
                                 "60 0\n";
    char path[1100];
    unsigned char buf[32];
    IcoPadFrame want[64], got;
    int t;

    path_in_dir(path, sizeof(path), "input-libpad.txt");
    CHECK(ico_pad_script_parse(script, "libpad") == 0);
    CHECK(scePadInit(0) == 1);
    CHECK(scePadSetMainMode(0, 0, 1, 3) == 1);
    CHECK(ico_input_record_open(path, "# libpad\n") == 0);
    for (t = 0; t < 64; t++) {
        ico_pad_script_set_tick((unsigned int)t);
        ico_pad_script_frame(&want[t]);
        CHECK(scePadRead(0, 0, buf) == 32);
        ico_pad_script_set_tick((unsigned int)t + 1); /* Main's tick is done */
        ico_input_record_poll((unsigned int)t + 1);
    }
    ico_input_record_close();
    CHECK(ico_input_record_lines() == 8);
    CHECK(ico_pad_script_load(path) == 0);
    for (t = 0; t < 64; t++) {
        ico_pad_script_frame_at((unsigned int)t, &got);
        CHECK(same(&got, &want[t]));
    }
    ico_pad_script_clear();
}

int main(int argc, char **argv)
{
    snprintf(dir, sizeof(dir), "%s", argc > 1 ? argv[1] : ".");
    test_format();
    test_round_trip();
    test_rules();
    test_through_libpad();
    printf("input_record_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
