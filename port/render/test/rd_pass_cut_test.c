/* rd_pass_cut_test.c: the passes cut (rd_pass_cut.c; docs/port/WORK_BUFFERS.md
 * "Passes cut"), each cut on against off on frames built here.
 *
 *   rd_pass_cut_test
 *
 * On a Vulkan device (77 without one), a wide scaled scene as on the phone
 * (Enhanced, 16:9, scale 2, Full pixel), under ICO_RD_STORE_POISON=1 (a
 * depth load the store plan leaves undefined clears to garbage: lavapipe
 * keeps an attachment's contents whatever its store op says):
 *   1. the clear carry: list 0 draws on WORK2, list 1 clears WORK3 and
 *      draws on it a sprite textured from WORK2 (WORK2 made readable after
 *      the clear: the pass on WORK2 ended there).  Carry on, the clear is
 *      the load of WORK3's pass (clearsCarried 1, one walk pass fewer);
 *      WORK3's and WORK2's bytes the same as with the carry off;
 *   2. the clear skip, its decision and its lazy clear: AURA_WORK filled
 *      green by a replay; a frame's scan (list 8: AURA_WORK cleared, then
 *      WORK0's sprite reading it) with the constant read's note for that
 *      sprite: the flush of the clear is skipped (not with a depth, not
 *      with a later read of AURA_WORK, not with the cut off); a read of
 *      AURA_WORK then sees the clear's colour (recorded on the read); the
 *      skip again, then a replay drawing on AURA_WORK's left half first:
 *      its right half is the clear's colour (recorded at the replay's
 *      start); the skip again, then a replay whose first touch is a clear
 *      of another colour: that colour, the lazy clear forgotten;
 *   3. the last store: list 0 clears SCENE and draws Z-tested, list 4 draws
 *      on WORK3, then on SCENE Z-tested (the depth's last touch, the pass's
 *      first draw) and with the Z test ALWAYS: that pass's depth store
 *      dropped (storeLastDropped 1) with the cut on, none with it off,
 *      SCENE's bytes the same; the same frame with a copy or a draw on
 *      another target between the pass's begin and the last touch: that
 *      pass keeps its store, the pass begun at the touch drops it (1); with
 *      a DATE draw or a draw textured from a target between: none (0);
 *   4. no validation error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "vk/rhi_vk.h"

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

static const uint8_t kGrey[4] = {0x40, 0x40, 0x40, 0x80};
static const uint8_t kGreen[4] = {0x00, 0x80, 0x00, 0x80};

/* a sprite over GS pixels x0 .. x0 + w, 0 .. h of a target whose frame is
 * fw x fh (GS offset 2048 - fw / 2), colour rgba; uv: textured over the
 * texels 0 .. w, 0 .. h */
static bool pushSpriteAt(RdFrame *f, int list, uint32_t fw, uint32_t fh, int32_t x0, uint32_t w,
                         uint32_t h, const uint8_t rgba[4], bool uv)
{
    RdScreenVtx v[2];
    const int32_t ox = 2048 - (int32_t)(fw / 2), oy = 2048 - (int32_t)(fh / 2);
    memset(v, 0, sizeof(v));
    v[0].x = (ox + x0) * 16;
    v[0].y = oy * 16;
    v[1].x = (ox + x0 + (int32_t)w) * 16;
    v[1].y = (oy + (int32_t)h) * 16;
    for (int k = 0; k < 2; k++) {
        v[k].q = 1.0f;
        memcpy(v[k].rgba, rgba, 4);
    }
    if (uv) {
        v[1].s = (float)(w * 16u);
        v[1].t = (float)(h * 16u);
    }
    RdCmd *c = rd__st_push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->b[2] = uv ? 1 : 0;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)sizeof(v));
    c->u[1] = 2;
    return true;
}

static bool pushSprite(RdFrame *f, int list, uint32_t w, uint32_t h, const uint8_t rgba[4])
{
    return pushSpriteAt(f, list, w, h, 0, w, h, rgba, false);
}

/* untextured, unblended, Z write off, the test word */
static bool drawState(RdFrame *f, int list, uint64_t test)
{
    return rd__st_push(f, list, RDC_TEXTURE_OFF) && rd__st_push1(f, list, RDC_ABE, 0) &&
           rd__st_test(f, list, test) && rd__st_push1(f, list, RDC_ZWRITE, RD_ZWRITE_OFF);
}

static bool textured(RdFrame *f, int list, RdTargetId src)
{
    RdCmd *c = rd__st_push(f, list, RDC_TEXTURE);
    if (!c) {
        return false;
    }
    c->u[0] = rd_target_texture(rd_target(src), RD_VIEW_RGBA).id;
    c->b[0] = RD_TEXFN_MODULATE;
    c->b[1] = RD_TCC_RGBA;
    return true;
}

/* a target without a depth, frame w x h */
static bool work(RdFrame *f, int list, RdTargetId id, uint32_t w, uint32_t h)
{
    RdCmd *c = rd__st_push(f, list, RDC_TARGET);
    if (!c) {
        return false;
    }
    c->u[0] = rd_target(id).id;
    c->u[1] = 0;
    c->u[2] = w | (h << 16);
    c->b[0] = RD_TARGET_OFFSET;
    return true;
}

static bool clearOf(RdFrame *f, int list, uint32_t id, const uint8_t rgba[4], int depth)
{
    RdCmd *c = rd__st_push(f, list, RDC_CLEAR);
    if (!c) {
        return false;
    }
    c->u[0] = id;
    memcpy(c->b, rgba, 4);
    c->b[4] = (uint8_t)depth;
    c->u[1] = 0;
    return true;
}

static void frameBegin(RdFrame *f, uint32_t number, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->number = number;
    f->gsW = w;
    f->gsH = h;
}

/* a named target's bytes (malloc'd), its size */
static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    const RdTargetRec *t = rd__target_rec(rd_target(id).id);
    if (!t) {
        return NULL;
    }
    const size_t n = (size_t)t->tw * t->th * 4u;
    uint8_t *px = malloc(n);
    if (px && !rd__read_target(rd_target(id), px, n, w, h)) {
        free(px);
        px = NULL;
    }
    return px;
}

static bool sameTarget(RdTargetId id, const uint8_t *was, uint32_t w, uint32_t h)
{
    uint32_t w2 = 0, h2 = 0;
    uint8_t *now = readTarget(id, &w2, &h2);
    const bool same = now && was && w2 == w && h2 == h && memcmp(now, was, (size_t)w * h * 4u) == 0;
    free(now);
    return same;
}

/* -------------------------------------------------------- 1. the carry */

static bool buildCarry(RdFrame *f, uint32_t number)
{
    static const uint8_t kClear[4] = {0x11, 0x22, 0x33, 0x44};
    frameBegin(f, number, 640, 448);
    bool ok = work(f, 0, RD_TARGET_WORK2, 256, 128) && drawState(f, 0, RD_TEST_Z_ALWAYS) &&
              pushSprite(f, 0, 256, 128, kGreen);
    ok = ok && work(f, 1, RD_TARGET_WORK3, 256, 128) &&
         clearOf(f, 1, rd_target(RD_TARGET_WORK3).id, kClear, 0) &&
         drawState(f, 1, RD_TEST_Z_ALWAYS) && textured(f, 1, RD_TARGET_WORK2) &&
         pushSpriteAt(f, 1, 256, 128, 0, 128, 64, kGrey, true);
    return ok;
}

static void cellCarry(void)
{
    static RdFrame f;
    CHECK(buildCarry(&f, 10), "the carry's frame built");
    rd__pass_cut_force(RD_CUT_CARRY, 0);
    CHECK(rd__replay_frame(&f, 0, false), "the carry's frame replayed (off)");
    const uint32_t passesOff = g_rdPerf.storePasses, carriedOff = g_rdPerf.clearsCarried;
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    uint8_t *t3 = readTarget(RD_TARGET_WORK3, &w, &h);
    uint8_t *t2 = readTarget(RD_TARGET_WORK2, &w2, &h2);
    rd__pass_cut_force(RD_CUT_CARRY, 1);
    CHECK(rd__replay_frame(&f, 0, false), "the carry's frame replayed (on)");
    const uint32_t passesOn = g_rdPerf.storePasses, carriedOn = g_rdPerf.clearsCarried;
    printf("  carry: off %u walk passes, %u carried; on %u passes, %u carried\n", passesOff,
           carriedOff, passesOn, carriedOn);
    CHECK(carriedOff == 0 && carriedOn == 1, "clears carried %u off, %u on (0 and 1 expected)",
          carriedOff, carriedOn);
    CHECK(passesOn + 1u == passesOff, "walk passes %u on, %u off: one fewer expected", passesOn,
          passesOff);
    CHECK(t3 && sameTarget(RD_TARGET_WORK3, t3, w, h), "WORK3 differs with the carry");
    CHECK(t2 && sameTarget(RD_TARGET_WORK2, t2, w2, h2), "WORK2 differs with the carry");
    free(t3);
    free(t2);
    rd__frame_free(&f);
    rd__pass_cut_force(RD_CUT_CARRY, -1);
}

/* --------------------------------------------------------- 2. the skip */

static const uint8_t kLazy[4] = {0x10, 0x20, 0x30, 0x40};

/* list 8: AURA_WORK cleared to kLazy, then WORK0's sprite textured from
 * it; with `later`, a second sprite reading it after; *reader: the first
 * sprite's ordinal (as the store plan counts) */
static bool buildSkip(RdFrame *f, uint32_t number, uint32_t gw, uint32_t gh, bool later,
                      uint32_t *reader)
{
    frameBegin(f, number, gw, gh);
    const uint32_t aura = rd_target(RD_TARGET_AURA_WORK).id;
    bool ok = work(f, 8, RD_TARGET_AURA_WORK, gw, gh) && clearOf(f, 8, aura, kLazy, 0) &&
              work(f, 8, RD_TARGET_WORK0, 256, 128) && drawState(f, 8, RD_TEST_Z_ALWAYS) &&
              textured(f, 8, RD_TARGET_AURA_WORK);
    const uint32_t at = f->lists[8].count;
    ok = ok && pushSpriteAt(f, 8, 256, 128, 0, 256, 128, kGrey, true);
    if (later) {
        ok = ok && pushSpriteAt(f, 8, 256, 128, 0, 128, 64, kGrey, true);
    }
    uint32_t ord = 0;
    for (int l = 0; l < 8; l++) {
        ord += f->lists[l].count;
    }
    *reader = ord + at;
    return ok;
}

/* the clear's flush decided as flushClear asks, at the reader */
static bool skipAt(const RdFrame *f, uint32_t reader, bool depth)
{
    const uint32_t aura = rd_target(RD_TARGET_AURA_WORK).id;
    float col[4];
    for (int k = 0; k < 4; k++) {
        col[k] = (float)kLazy[k] / 255.0f;
    }
    rd__pass_cut_scan(f, 0);
    g_rdStoreOrd = reader;
    rd__clear_skip_served(aura);
    const bool skip = rd__clear_skip(aura, rd__target_rec(aura), col, depth);
    g_rdStoreOrd = 0;
    return skip;
}

static bool texelIs(const uint8_t *px, uint32_t w, uint32_t x, uint32_t y, const uint8_t want[4])
{
    return memcmp(px + ((size_t)y * w + x) * 4u, want, 4) == 0;
}

static void cellSkip(uint32_t gw, uint32_t gh)
{
    static RdFrame fill, f, g, half, again;
    rd__pass_cut_force(RD_CUT_SKIP, 1);
    /* AURA_WORK green */
    frameBegin(&fill, 20, gw, gh);
    CHECK(work(&fill, 8, RD_TARGET_AURA_WORK, gw, gh) && drawState(&fill, 8, RD_TEST_Z_ALWAYS) &&
              pushSprite(&fill, 8, gw, gh, kGreen) && rd__replay_frame(&fill, 0, false),
          "AURA_WORK filled");
    uint32_t reader = 0, readerG = 0;
    CHECK(buildSkip(&f, 21, gw, gh, false, &reader), "the skip's frame built");
    CHECK(buildSkip(&g, 22, gw, gh, true, &readerG), "the skip's frame with a later read built");
    /* the decision */
    CHECK(!skipAt(&g, readerG, false), "a clear read again later was skipped");
    CHECK(!skipAt(&f, reader, true), "a clear with a depth was skipped");
    rd__pass_cut_force(RD_CUT_SKIP, 0);
    CHECK(!skipAt(&f, reader, false), "a clear was skipped with the cut off");
    rd__pass_cut_force(RD_CUT_SKIP, 1);
    CHECK(skipAt(&f, reader, false), "the clear its reader took the colour of was not skipped");
    /* a read sees the clear */
    uint32_t w = 0, h = 0;
    uint8_t *px = readTarget(RD_TARGET_AURA_WORK, &w, &h);
    CHECK(px && w && h && texelIs(px, w, 0, 0, kLazy) && texelIs(px, w, w - 1, h - 1, kLazy),
          "a read of AURA_WORK after its skipped clear: %02x%02x%02x%02x, the clear's expected",
          px ? px[0] : 0, px ? px[1] : 0, px ? px[2] : 0, px ? px[3] : 0);
    free(px);
    /* a later replay drawing on it first: the clear recorded at its start */
    CHECK(skipAt(&f, reader, false), "the clear skipped again");
    frameBegin(&half, 23, gw, gh);
    CHECK(work(&half, 8, RD_TARGET_AURA_WORK, gw, gh) && drawState(&half, 8, RD_TEST_Z_ALWAYS) &&
              pushSpriteAt(&half, 8, gw, gh, 0, gw / 2, gh, kGreen, false) &&
              rd__replay_frame(&half, 0, false),
          "the half frame replayed");
    px = readTarget(RD_TARGET_AURA_WORK, &w, &h);
    CHECK(px && texelIs(px, w, w / 8, h / 2, kGreen) && texelIs(px, w, w - 1 - w / 8, h / 2, kLazy),
          "AURA_WORK after the half frame: left %02x%02x%02x%02x, right %02x%02x%02x%02x "
          "(green and the clear's expected)",
          px ? px[((size_t)(h / 2) * w + w / 8) * 4] : 0,
          px ? px[((size_t)(h / 2) * w + w / 8) * 4 + 1] : 0,
          px ? px[((size_t)(h / 2) * w + w / 8) * 4 + 2] : 0,
          px ? px[((size_t)(h / 2) * w + w / 8) * 4 + 3] : 0,
          px ? px[((size_t)(h / 2) * w + w - 1 - w / 8) * 4] : 0,
          px ? px[((size_t)(h / 2) * w + w - 1 - w / 8) * 4 + 1] : 0,
          px ? px[((size_t)(h / 2) * w + w - 1 - w / 8) * 4 + 2] : 0,
          px ? px[((size_t)(h / 2) * w + w - 1 - w / 8) * 4 + 3] : 0);
    free(px);
    /* a later replay clearing it first: that clear, the lazy one gone */
    CHECK(skipAt(&f, reader, false), "the clear skipped a third time");
    frameBegin(&again, 24, gw, gh);
    CHECK(work(&again, 8, RD_TARGET_AURA_WORK, gw, gh) &&
              clearOf(&again, 8, rd_target(RD_TARGET_AURA_WORK).id, kGrey, 0) &&
              rd__replay_frame(&again, 0, false),
          "the clearing frame replayed");
    px = readTarget(RD_TARGET_AURA_WORK, &w, &h);
    CHECK(px && texelIs(px, w, 0, 0, kGrey) && texelIs(px, w, w - 1, h - 1, kGrey),
          "AURA_WORK after a replay that clears it: not that clear");
    free(px);
    rd__frame_free(&fill);
    rd__frame_free(&f);
    rd__frame_free(&g);
    rd__frame_free(&half);
    rd__frame_free(&again);
    rd__pass_cut_force(RD_CUT_SKIP, -1);
}

/* ---------------------------------------------------- 3. the last store */

enum { BETWEEN_NONE, BETWEEN_COPY, BETWEEN_DATE, BETWEEN_TEX, BETWEEN_OTHER };

/* list 0: SCENE cleared, a Z GEQUAL sprite; list 4: WORK3, then SCENE: a
 * screen sprite Z ALWAYS (the pass's first draw), something between, a Z
 * GEQUAL sprite (the last touch), a Z ALWAYS sprite */
static bool buildLast(RdFrame *f, uint32_t number, uint32_t scene, uint32_t w, uint32_t h,
                      int between)
{
    frameBegin(f, number, w, h);
    bool ok = rd__st_target(f, 0, scene, w, h) && clearOf(f, 0, scene, kGrey, 1) &&
              drawState(f, 0, RD_TEST_Z_GEQUAL) && pushSprite(f, 0, w, h, kGrey);
    ok = ok && work(f, 4, RD_TARGET_WORK3, 256, 128) && drawState(f, 4, RD_TEST_Z_ALWAYS) &&
         pushSprite(f, 4, 256, 128, kGreen);
    ok = ok && rd__st_target(f, 4, scene, w, h) && drawState(f, 4, RD_TEST_Z_ALWAYS) &&
         pushSpriteAt(f, 4, w, h, 0, w / 2, h / 2, kGreen, false);
    switch (between) {
    case BETWEEN_COPY: {
        RdCmd *c = ok ? rd__st_push(f, 4, RDC_COPY) : NULL;
        RdCopyRec cr;
        memset(&cr, 0, sizeof(cr));
        cr.w = 16;
        cr.h = 16;
        if (c) {
            c->u[0] = rd_target(RD_TARGET_WORK3).id;
            c->u[1] = rd_target(RD_TARGET_WORK2).id;
            c->u[2] = rd__frame_payload(f, &cr, (uint32_t)sizeof(cr));
        }
        ok = ok && c;
        break;
    }
    case BETWEEN_DATE:
        ok = ok && rd__st_test(f, 4, RD_TEST_DATE0) &&
             pushSpriteAt(f, 4, w, h, 8, 16, 16, kGrey, false);
        break;
    case BETWEEN_TEX:
        ok = ok && textured(f, 4, RD_TARGET_WORK3) &&
             pushSpriteAt(f, 4, w, h, 8, 16, 16, kGrey, true) && rd__st_push(f, 4, RDC_TEXTURE_OFF);
        break;
    case BETWEEN_OTHER:
        ok = ok && work(f, 4, RD_TARGET_WORK3, 256, 128) && pushSprite(f, 4, 256, 128, kGrey) &&
             rd__st_target(f, 4, scene, w, h);
        break;
    default:
        break;
    }
    ok = ok && drawState(f, 4, RD_TEST_Z_GEQUAL) &&
         pushSpriteAt(f, 4, w, h, w / 4, w / 2, h, kGrey, false) &&
         drawState(f, 4, RD_TEST_Z_ALWAYS) && pushSpriteAt(f, 4, w, h, 0, 32, 32, kGrey, false);
    return ok;
}

static void cellLast(uint32_t scene, uint32_t w, uint32_t h)
{
    static RdFrame f;
    CHECK(buildLast(&f, 30, scene, w, h, BETWEEN_NONE), "the last store's frame built");
    rd__pass_cut_force(RD_CUT_LAST, 0);
    CHECK(rd__replay_frame(&f, 0, false), "the last store's frame replayed (off)");
    const uint32_t droppedOff = g_rdPerf.storeDepthDropped, lastOff = g_rdPerf.storeLastDropped;
    uint32_t tw = 0, th = 0;
    uint8_t *was = readTarget(RD_TARGET_SCENE, &tw, &th);
    rd__pass_cut_force(RD_CUT_LAST, 1);
    CHECK(rd__replay_frame(&f, 0, false), "the last store's frame replayed (on)");
    const uint32_t droppedOn = g_rdPerf.storeDepthDropped, lastOn = g_rdPerf.storeLastDropped;
    printf("  last store: off %u depth stores dropped (%u at the last touch); on %u (%u)\n",
           droppedOff, lastOff, droppedOn, lastOn);
    CHECK(lastOff == 0 && lastOn == 1, "stores dropped at the last touch: %u off, %u on (0, 1)",
          lastOff, lastOn);
    CHECK(droppedOn == droppedOff + 1u, "depth stores dropped %u on, %u off: one more expected",
          droppedOn, droppedOff);
    CHECK(was && sameTarget(RD_TARGET_SCENE, was, tw, th), "SCENE differs with the last store");
    free(was);
    rd__frame_free(&f);
    static const char *const kBetween[] = {
        "", "a copy", "a DATE draw", "a draw textured from a target", "a draw on another target"};
    /* the pass begun before what lies between keeps its store; a copy or a
     * draw on another target ends it, and the pass begun at the last touch
     * itself holds it (its store dropped: 1); DATE's and the textured
     * draw's own pass holds the touch after a command the cut does not
     * take (0) */
    static const uint32_t kWant[] = {1, 1, 0, 0, 1};
    for (int b = BETWEEN_COPY; b <= BETWEEN_OTHER; b++) {
        CHECK(buildLast(&f, 31u + (uint32_t)b, scene, w, h, b) && rd__replay_frame(&f, 0, false),
              "the frame with %s replayed", kBetween[b]);
        printf("  last store with %s between: %u dropped at the last touch\n", kBetween[b],
               g_rdPerf.storeLastDropped);
        CHECK(g_rdPerf.storeLastDropped == kWant[b],
              "%s before the last touch: %u stores dropped at it, %u expected", kBetween[b],
              g_rdPerf.storeLastDropped, kWant[b]);
        rd__frame_free(&f);
    }
    /* the guard did not turn the cut off: the first frame drops it again */
    CHECK(buildLast(&f, 40, scene, w, h, BETWEEN_NONE) && rd__replay_frame(&f, 0, false) &&
              g_rdPerf.storeLastDropped == 1,
          "the last store's frame again: %u dropped at the last touch", g_rdPerf.storeLastDropped);
    rd__frame_free(&f);
    rd__pass_cut_force(RD_CUT_LAST, -1);
}

int main(void)
{
    printf("rd_pass_cut_test\n");
#ifdef _WIN32
    _putenv_s("ICO_RD_STORE_POISON", "1");
#else
    setenv("ICO_RD_STORE_POISON", "1", 1);
#endif
    const uint32_t w = 640, h = 448;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 2.0f;
    st.fullPixel = 1;
    st.outputWidth = 1280;
    st.outputHeight = 720;
    if (!rd_init(w, h, &st, NULL)) {
        printf("rd_pass_cut_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    CHECK(g_rd.profile.storePlan, "the store plan in force");
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const RdTargetRec *t = rd__target_rec(scene);
    CHECK(t && t->withDepth && t->depth.id, "SCENE with a depth");
    if (!t || !t->withDepth || !t->depth.id) {
        rd_shutdown();
        return 1;
    }
    const uint32_t gw = w;
    cellCarry();
    cellSkip(gw, h);
    cellLast(scene, gw, h);
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_pass_cut_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_pass_cut_test: ok\n");
    return 0;
}
