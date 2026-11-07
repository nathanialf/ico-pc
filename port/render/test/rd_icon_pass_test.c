/* rd_icon_pass_test.c: the title's icon pass (rd.h rd_set_icon_pass) on
 * synthetic frames, recorded without a device (rd__init_record_only):
 *   - on: a screen prim in RD_SPACE_UI or RD_SPACE_FULLFRAME (also through
 *     a space override) and a deferred text item are not recorded; world
 *     draws (world-space screen prims, world prims) are;
 *   - on: the frame head's clear (both copies, lists 0 and 11) is the
 *     pass's colour at the head and after rd_frame_flip with the game's
 *     colour, and a head whose flip sends no clear records one;
 *   - off: a frame recorded after the pass was on and off is byte-identical
 *     (every list's commands, the payload) to one recorded with the pass
 *     never on, the head's clear back to the game's colour and absent
 *     again without fbClear.
 * Exit 0, 1 on a mismatch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char kObjA, kObjB, kObjC;
static const uint8_t kGame[4] = {0x20, 0x30, 0x40, 0x80}; /* the game's BG colour */
static const uint8_t kFlip[4] = {0x21, 0x31, 0x41, 0x80}; /* the BG colour at the flip */
static const uint8_t kBlack[4] = {0, 0, 0, 0x80};
static const uint8_t kWhite[4] = {0xFF, 0xFF, 0xFF, 0x80};

static void screen(RdSpace space, RdKey key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[1].x = v[1].y = 160;
    v[0].q = v[1].q = 1.0f;
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, space, 0, key);
}

static void worldPrims(RdKey key)
{
    RdWorldVtx v[2];
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memset(v, 0, sizeof(v));
    rd_world_prims(RD_PRIM_LINES, v, 2, m, key);
}

static void text(RdKey key)
{
    RdTextItem it;
    memset(&it, 0, sizeof(it));
    strcpy(it.utf8, "NEW GAME");
    it.size = 16.0f;
    it.rgba[0] = it.rgba[1] = it.rgba[2] = it.rgba[3] = 0x80;
    rd_deferred_text(&it, key);
}

/* the open frame's commands of one type with this key, in list l */
static int count(int l, uint8_t type, RdKey key)
{
    const RdFrame *f = rd__rec_frame();
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        const RdCmd *c = &f->lists[l].cmds[i];
        if (c->type == type && c->keyLo == (uint32_t)key && c->keyHi == (uint32_t)(key >> 32)) {
            n++;
        }
    }
    return n;
}

/* the colour of the open frame's head clear copy (0: list 0, 1: list 11);
   false when it records none */
static bool headClear(int copy, uint8_t rgba[4])
{
    const RdFrame *f = rd__rec_frame();
    const RdCmdList *cl = f ? &f->lists[copy ? 11 : 0] : NULL;
    if (!cl || f->headClear[copy] >= cl->count || cl->cmds[f->headClear[copy]].type != RDC_CLEAR) {
        return false;
    }
    memcpy(rgba, cl->cmds[f->headClear[copy]].b, 4);
    return true;
}

static bool bothClear(const uint8_t want[4])
{
    uint8_t c[4];
    return headClear(0, c) && memcmp(c, want, 4) == 0 && headClear(1, c) && memcmp(c, want, 4) == 0;
}

static void head(int clear)
{
    RdFrameHead h;
    memset(&h, 0, sizeof(h));
    memcpy(h.rgba, kGame, 4);
    h.gsW = 512;
    h.gsH = 448;
    h.clear = (uint8_t)(clear != 0);
    rd_frame_head(&h);
}

/* a copy of the open frame's lists and payload */
typedef struct Snap {
    uint32_t count[RD_LIST_COUNT];
    RdCmd *cmds[RD_LIST_COUNT];
    uint8_t *payload;
    uint32_t payloadSize;
} Snap;

static void snap(Snap *s)
{
    const RdFrame *f = rd__rec_frame();
    memset(s, 0, sizeof(*s));
    for (int l = 0; f && l < RD_LIST_COUNT; l++) {
        s->count[l] = f->lists[l].count;
        if (s->count[l] > 0) {
            s->cmds[l] = malloc(sizeof(RdCmd) * s->count[l]);
            memcpy(s->cmds[l], f->lists[l].cmds, sizeof(RdCmd) * s->count[l]);
        }
    }
    if (f && f->payloadSize > 0) {
        s->payloadSize = f->payloadSize;
        s->payload = malloc(f->payloadSize);
        memcpy(s->payload, f->payload, f->payloadSize);
    }
}

static bool snapSame(const Snap *a, const Snap *b)
{
    if (a->payloadSize != b->payloadSize ||
        (a->payloadSize > 0 && memcmp(a->payload, b->payload, a->payloadSize) != 0)) {
        return false;
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (a->count[l] != b->count[l] ||
            (a->count[l] > 0 && memcmp(a->cmds[l], b->cmds[l], sizeof(RdCmd) * a->count[l]) != 0)) {
            return false;
        }
    }
    return true;
}

static void snapFree(Snap *s)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(s->cmds[l]);
    }
    free(s->payload);
    memset(s, 0, sizeof(*s));
}

/* one title-like frame: the head, a world draw, the menu's sprite, a
   full-frame sprite and the menu's text, the flip; snapped before it
   closes */
static void titleFrame(int clear, Snap *s)
{
    const RdKey a = RD_KEY(&kObjA, 0, 0), b = RD_KEY(&kObjB, 0, 0), c = RD_KEY(&kObjC, 0, 0);
    rd_begin_frame();
    head(clear);
    rd_select_list(3);
    screen(RD_SPACE_WORLD, a);
    worldPrims(a);
    rd_select_list(11);
    screen(RD_SPACE_UI, b);
    screen(RD_SPACE_FULLFRAME, c);
    text(b);
    rd_frame_flip(kFlip, 0);
    if (s) {
        snap(s);
    }
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (!rd__init_record_only(512, 512)) {
        printf("rd_icon_pass_test: no context\n");
        return 1;
    }
    const RdKey a = RD_KEY(&kObjA, 0, 0), b = RD_KEY(&kObjB, 0, 0), c = RD_KEY(&kObjC, 0, 0);
    Snap before, after;

    /* never on: everything recorded, the flip's colour (after a first
       frame, so the one compared is not the renderer's first) */
    CHECK(!rd_icon_pass_on(), "off by default");
    titleFrame(1, NULL);
    rd_end_frame(0);
    titleFrame(1, &before);
    CHECK(count(3, RDC_SCREEN, a) == 1 && count(3, RDC_WORLD_PRIMS, a) == 1,
          "off: the world draws recorded");
    CHECK(count(11, RDC_SCREEN, b) == 1 && count(11, RDC_SCREEN, c) == 1 &&
              count(11, RDC_OVERLAY_TEXT, b) == 1,
          "off: the UI and full-frame prims and the text recorded");
    CHECK(bothClear(kFlip), "off: the head clears to the flip's colour");
    rd_end_frame(0);

    /* on over black */
    rd_set_icon_pass(kBlack);
    CHECK(rd_icon_pass_on(), "on");
    titleFrame(1, NULL);
    CHECK(count(3, RDC_SCREEN, a) == 1 && count(3, RDC_WORLD_PRIMS, a) == 1,
          "on: the world draws recorded");
    CHECK(count(11, RDC_SCREEN, b) == 0 && count(11, RDC_SCREEN, c) == 0,
          "on: the UI and full-frame prims left out");
    CHECK(count(11, RDC_OVERLAY_TEXT, b) == 0, "on: the deferred text left out");
    const int prev = rd_set_space_override(RD_SPACE_UI);
    rd_select_list(3);
    screen(RD_SPACE_WORLD, a);
    rd_set_space_override(prev);
    CHECK(count(3, RDC_SCREEN, a) == 1, "on: a world prim drawn as UI space left out");
    CHECK(bothClear(kBlack), "on: the head clears to black over the flip's colour");
    rd_end_frame(0);

    /* white; a frame whose flip sends no clear records one */
    rd_set_icon_pass(kWhite);
    titleFrame(0, NULL);
    CHECK(bothClear(kWhite), "on, no fbClear: the head clears to white");
    rd_end_frame(0);

    /* turned on inside an open frame: the flip writes the colour */
    rd_set_icon_pass(NULL);
    rd_begin_frame();
    head(1);
    rd_set_icon_pass(kBlack);
    rd_frame_flip(kFlip, 0);
    CHECK(bothClear(kBlack), "on mid-frame: the flip writes black");
    rd_end_frame(0);

    /* off again: byte-identical to the frame recorded before */
    rd_set_icon_pass(NULL);
    CHECK(!rd_icon_pass_on(), "off again");
    titleFrame(1, &after);
    CHECK(count(11, RDC_SCREEN, b) == 1 && count(11, RDC_OVERLAY_TEXT, b) == 1,
          "off again: the UI prims and the text recorded");
    CHECK(bothClear(kFlip), "off again: the flip's colour");
    CHECK(snapSame(&before, &after),
          "off again: the frame byte-identical to one recorded with the pass never on");
    rd_end_frame(0);
    snapFree(&before);
    snapFree(&after);

    titleFrame(0, NULL);
    uint8_t col[4];
    CHECK(!headClear(0, col) && !headClear(1, col), "off again, no fbClear: no clear recorded");
    rd_end_frame(0);

    rd_shutdown();
    if (failures == 0) {
        printf("rd_icon_pass_test: ok\n");
    }
    return failures ? 1 : 0;
}
