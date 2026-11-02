/* rd_store_plan_test.c: the store plan's keep (rd_store_plan.c; docs/port/
 * WORK_BUFFERS.md "After the replay"): which replays keep a depth's stores
 * for the rest of the run, and that the drops stay in force otherwise.
 *
 *   rd_store_plan_test
 *
 * On a Vulkan device (77 without one), a wide scaled scene as on the phone
 * (Enhanced, 16:9, scale 2), with the plan on (every device class):
 *   1. the start's self-tests (rd_fog_selftest: DATE, the fx local read,
 *      the shadow count, the fog), as the window runs them, and again at a
 *      new scene size (rd_reset_scene: the shadow count and the fog again):
 *      SCENE's depth is not kept.  The shadow count's frame reads SCENE's
 *      depth in list 3 with no clear of its own; its second replay (the
 *      seven draws) read the depth its first left, and the plan kept
 *      SCENE's stores for the whole run (the v0.9.8 rc1 phone log, "frame
 *      0"): each of its replays now starts SCENE from its clear;
 *   2. a fresh SCENE (clearPending: rd__clear_new_targets clears it before
 *      the walk) read in list 3 before any clear of the frame's own: not a
 *      carried read, not kept;
 *   3. the next frame (list 0 clears SCENE's depth, a Z-tested draw, then
 *      list 7 draws on WORK3 and again on SCENE without touching its depth):
 *      the plan drops the depth store of list 7's SCENE pass, nothing kept;
 *   4. a frame that reads SCENE's depth in list 3 with no clear (a frame
 *      drawing on the depth the replay before left): kept from then on, as
 *      before; frame 3 again then drops nothing, and the replay's record
 *      names the keep (storeKept 1, SCENE's, since frame 4);
 *   5. no validation error. */
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

/* a full-frame sprite of a w x h frame (GS offset 2048 - w / 2, as
 * rd__st_target sets it) */
static bool pushSprite(RdFrame *f, int list, uint32_t w, uint32_t h)
{
    RdScreenVtx v[2];
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, sizeof(v));
    v[0].x = ox * 16;
    v[0].y = oy * 16;
    v[1].x = (ox + (int32_t)w) * 16;
    v[1].y = (oy + (int32_t)h) * 16;
    for (int k = 0; k < 2; k++) {
        v[k].q = 1.0f;
        memcpy(v[k].rgba, kGrey, 4);
    }
    RdCmd *c = rd__st_push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)sizeof(v));
    c->u[1] = 2;
    return true;
}

/* the draw state of the sprites: untextured, unblended, Z write off, the
 * test word (RD_TEST_Z_GEQUAL reads the depth, RD_TEST_Z_ALWAYS does not) */
static bool drawState(RdFrame *f, int list, uint64_t test)
{
    return rd__st_push(f, list, RDC_TEXTURE_OFF) && rd__st_push1(f, list, RDC_ABE, 0) &&
           rd__st_test(f, list, test) && rd__st_push1(f, list, RDC_ZWRITE, RD_ZWRITE_OFF);
}

static void frameBegin(RdFrame *f, uint32_t number, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->number = number;
    f->gsW = w;
    f->gsH = h;
}

/* list 3 reads SCENE's depth (a Z GEQUAL sprite), no clear in the frame */
static bool buildRead(RdFrame *f, uint32_t number, uint32_t scene, uint32_t w, uint32_t h)
{
    frameBegin(f, number, w, h);
    return rd__st_target(f, 3, scene, w, h) && drawState(f, 3, RD_TEST_Z_GEQUAL) &&
           pushSprite(f, 3, w, h);
}

/* list 0 clears SCENE (its depth too) and reads the depth; list 7 draws on
 * WORK3 (no depth), then on SCENE with the Z test ALWAYS and no Z write: a
 * pass on SCENE begun after its depth's last touch */
static bool buildCleared(RdFrame *f, uint32_t number, uint32_t scene, uint32_t work, uint32_t w,
                         uint32_t h)
{
    frameBegin(f, number, w, h);
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kGrey, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && drawState(f, 0, RD_TEST_Z_GEQUAL) && pushSprite(f, 0, w, h);
    RdCmd *wt = ok ? rd__st_push(f, 7, RDC_TARGET) : NULL;
    if (wt) {
        wt->u[0] = work;
        wt->u[1] = 0; /* no depth */
        wt->u[2] = 256u | (128u << 16);
        wt->b[0] = RD_TARGET_OFFSET;
    }
    ok = ok && wt && drawState(f, 7, RD_TEST_Z_ALWAYS) && pushSprite(f, 7, 256, 128);
    ok = ok && rd__st_target(f, 7, scene, w, h) && drawState(f, 7, RD_TEST_Z_ALWAYS) &&
         pushSprite(f, 7, w, h);
    return ok;
}

static bool replay(const RdFrame *f)
{
    const bool ok = rd__replay_frame(f, 0, false);
    printf("  frame %u: %u of %u depth stores dropped (%u passes), %u depths kept\n", f->number,
           g_rdPerf.storeDepthDropped, g_rdPerf.storeDepthPasses, g_rdPerf.storePasses,
           (unsigned)g_rdPerf.storeKept);
    return ok;
}

int main(void)
{
    printf("rd_store_plan_test\n");
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
        printf("rd_store_plan_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    CHECK(g_rd.profile.storePlan && !g_rd.profile.storeAll, "the store plan in force");

    /* 1. the start's self-tests, then again at a new scene size */
    (void)rd_fog_selftest();
    printf("  self-tests run (shadow count %s)\n",
           g_rd.profile.shadowOne ? "one draw: its two replays ran" : "seven draws");
    CHECK(!rd__store_plan_kept(rd_target(RD_TARGET_SCENE).id),
          "SCENE's depth kept after the start's self-tests");
    rd_reset_scene(w + 64, h);
    CHECK(!rd__store_plan_kept(rd_target(RD_TARGET_SCENE).id),
          "SCENE's depth kept after the self-tests at a new scene size");
    const uint32_t gw = w + 64;

    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const uint32_t work = rd_target(RD_TARGET_WORK3).id;
    RdTargetRec *t = rd__target_rec(scene);
    CHECK(t && t->withDepth && t->depth.id, "SCENE with a depth");
    if (!t || !t->withDepth || !t->depth.id) {
        rd_shutdown();
        return 1;
    }
    static RdFrame f;

    /* 2. a fresh SCENE read before any clear of the frame's own */
    t->clearPending = 1;
    CHECK(buildRead(&f, 1, scene, gw, h) && replay(&f), "the fresh frame replayed");
    rd__frame_free(&f);
    CHECK(!rd__store_plan_kept(scene), "a fresh SCENE read before the frame's clear: kept");
    CHECK(g_rdPerf.storeKept == 0, "the fresh frame's record names %u kept depths",
          (unsigned)g_rdPerf.storeKept);

    /* 3. the drops in force on the next frame */
    CHECK(buildCleared(&f, 2, scene, work, gw, h) && replay(&f), "the cleared frame replayed");
    const uint32_t dropped = g_rdPerf.storeDepthDropped;
    CHECK(dropped >= 1u, "the cleared frame dropped %u depth stores, at least 1 expected", dropped);
    CHECK(!rd__store_plan_kept(scene), "the cleared frame kept SCENE's depth");

    /* 4. a frame reading the depth the replay before left: kept from now on */
    static RdFrame g;
    CHECK(buildRead(&g, 4, scene, gw, h) && replay(&g), "the carried frame replayed");
    rd__frame_free(&g);
    CHECK(rd__store_plan_kept(scene), "a read before the clear on an old SCENE: not kept");
    f.number = 5;
    CHECK(replay(&f), "the cleared frame replayed again");
    CHECK(g_rdPerf.storeDepthDropped == 0, "%u depth stores dropped under the keep",
          g_rdPerf.storeDepthDropped);
    CHECK(g_rdPerf.storeKept == 1 && g_rdPerf.storeKeptTarget == RD_TARGET_SCENE &&
              g_rdPerf.storeKeptFrame == 4,
          "the record's keep: %u depths, target %u since frame %u (1, SCENE, 4 expected)",
          (unsigned)g_rdPerf.storeKept, (unsigned)g_rdPerf.storeKeptTarget,
          g_rdPerf.storeKeptFrame);
    rd__frame_free(&f);

    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_store_plan_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_store_plan_test: ok\n");
    return 0;
}
