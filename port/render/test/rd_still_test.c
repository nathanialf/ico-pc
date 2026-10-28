/* rd_still_test: the still re-present's decision (rd_reshow.c) and the hash
 * it rests on (rd_interp.c outHash), on synthetic frames recorded without a
 * device.
 *
 *   equal       two ticks with the same keyed and unkeyed sprites, the camera
 *               still: the frame built at alpha 0.25, 0.5, 0.75 and 0.999 hashes
 *               the same, as a tick's first present and as a later one (the
 *               head copies of the feedback passes are not in it), and
 *               rd__reshow_why says shown again
 *   one word    the second tick's unkeyed sprite differs in one colour word:
 *               the hash differs from the unchanged frame's at the same
 *               alpha; a keyed sprite that moves differs between alphas
 *   camera      the camera moving: no hash (RD_RESHOW_MOVING)
 *   smooth      Motion blur: Smooth on: no hash
 *   decision    rd__reshow_why for every input: the switch off, Smooth, a
 *               video, no hash, a tick's first present, no reference, another
 *               tick's, another replay since, a differing hash, a changed
 *               state, and the one case that shows the picture again
 */
#include <math.h>
#include <stdio.h>
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

#define TW 256
#define TH 128
#define OX ((2048 - TW / 2) * 16)
#define OY ((2048 - TH / 2) * 16)

static const char kObjA;

static void sprite(int x0, int y0, int x1, int y1, const uint8_t c[4], RdKey key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = OX + x0 * 16;
    v[0].y = OY + y0 * 16;
    v[1].x = OX + x1 * 16;
    v[1].y = OY + y1 * 16;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, c, 4);
    memcpy(v[1].rgba, c, 4);
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 0, key);
}

static void cameraAt(RdCamera *cam, float deg)
{
    memset(cam, 0, sizeof(*cam));
    const float r = deg * 3.14159265f / 180.0f;
    cam->view[0] = cosf(r);
    cam->view[2] = -sinf(r);
    cam->view[5] = 1.0f;
    cam->view[8] = sinf(r);
    cam->view[10] = cosf(r);
    cam->view[15] = 1.0f;
    cam->zoom = 1024.0f;
}

/* a tick: a keyed sprite at kx, an unkeyed one of colour `tweak`, the camera
 * at camX, and the feedback passes (an aura into FEED128 and a motion blur
 * reading DISPLAY) so that a tick's first present and the later ones differ
 * in the head copies */
static void tick(int kx, uint8_t tweak, float camDeg)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    const uint8_t keyed[4] = {200, 40, 0, 0x80}, plain[4] = {90, 90, tweak, 0x80};
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, TW, TH, 0);
    rd_clear_target(rd_target(RD_TARGET_WORK0), black, 0, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
    RdCamera cam;
    cameraAt(&cam, camDeg);
    rd_set_camera(&cam);
    sprite(kx, 8, kx + 32, 40, keyed, RD_KEY(&kObjA, 0, 0));
    sprite(100, 50, 120, 60, plain, 0);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x20;
    pp.blend = RD_BLEND_LERP_FIX;
    pp.scalar[2] = 1.0f;
    rd_select_list(7);
    rd_blend_func(RD_BLEND_LERP_FIX, 0x20);
    rd_texture(rd_target_texture(rd_target(RD_TARGET_DISPLAY), RD_VIEW_RGB24_TA0),
               RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_post(RD_POST_MOTION_BLUR, &pp);
    rd_texture_off();
    rd_select_list(8);
    rd_set_target(rd_target(RD_TARGET_FEED128), (RdTarget){0}, 128, 128, 0);
    rd_post(RD_POST_AURA, &pp);
    rd_end_frame(0);
}

static uint64_t hashAt(float alpha, int first, bool *ok)
{
    const RdFrame *f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), alpha, first, NULL);
    CHECK(f != NULL, "a frame at alpha %.2f", (double)alpha);
    return rd__interp_frame_hash(ok);
}

static RdReshowIn shown(void)
{
    RdReshowIn in;
    memset(&in, 0, sizeof(in));
    in.enabled = in.hashOk = in.haveRef = in.stateSame = true;
    in.number = in.refNumber = 7;
    in.hash = in.refHash = 0x1234;
    in.replay = in.refReplay = 9;
    return in;
}

static void testDecision(void)
{
    RdReshowIn in = shown();
    CHECK(rd__reshow_why(&in) == RD_RESHOW_SHOWN, "equal hash, same tick: shown again");
    in = shown();
    in.enabled = false;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_OFF, "the switch off");
    in = shown();
    in.smooth = true;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_OFF, "Smooth on");
    in = shown();
    in.video = true;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_OFF, "a video");
    in = shown();
    in.hashOk = false;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_MOVING, "no hash");
    in = shown();
    in.first = true;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_FIRST, "a tick's first present");
    in = shown();
    in.haveRef = false;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_STALE, "no reference picture");
    in = shown();
    in.refNumber = 6;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_STALE, "the reference is another tick's");
    in = shown();
    in.replay = 10;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_STALE, "another replay since the reference");
    in = shown();
    in.hash ^= 1;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_DIFF, "a hash one bit apart");
    in = shown();
    in.stateSame = false;
    CHECK(rd__reshow_why(&in) == RD_RESHOW_STATE, "settings, targets or textures changed");
}

int main(void)
{
    if (!rd__init_record_only(512, 512)) {
        printf("rd_still_test: no context\n");
        return 1;
    }
    bool ok = false, ok2 = false;
    /* equal: two ticks with the same content, the camera still */
    tick(0, 90, 0.0f);
    tick(0, 90, 0.0f);
    const uint64_t h25 = hashAt(0.25f, 1, &ok);
    CHECK(ok, "a still camera gives a hash");
    CHECK(hashAt(0.5f, 0, &ok2) == h25 && ok2, "alpha 0.5 as a later present: the same hash");
    CHECK(hashAt(0.75f, 0, &ok2) == h25, "alpha 0.75: the same hash");
    /* the presenter never presents at alpha 1 (window_host.c clamps the top to
       0.999): the blend at exactly 1 renormalises the camera's rotation to
       other bits, so that hash is not asserted */
    CHECK(hashAt(0.999f, 0, &ok2) == h25, "alpha 0.999 (the presenter's top): the same hash");
    CHECK(hashAt(0.25f, 0, &ok2) == h25, "the head copies are not in the hash");
    /* one word: the unkeyed sprite's blue differs in the second tick */
    tick(0, 90, 0.0f);
    tick(0, 91, 0.0f);
    const uint64_t hw = hashAt(0.5f, 0, &ok2);
    CHECK(ok2 && hw != h25, "one colour word differs: another hash");
    /* a keyed sprite that moves differs between alphas */
    tick(0, 90, 0.0f);
    tick(40, 90, 0.0f);
    const uint64_t m25 = hashAt(0.25f, 0, &ok2), m50 = hashAt(0.5f, 0, &ok2);
    CHECK(ok2 && m25 != m50, "a moving sprite: the hash follows the alpha");
    CHECK(hashAt(0.25f, 0, &ok2) == m25, "the same alpha again: the same hash");
    /* camera: moving gives no hash, still does */
    tick(0, 90, 0.0f);
    tick(0, 90, 3.0f);
    (void)hashAt(0.5f, 0, &ok2);
    CHECK(!ok2, "a moving camera: no hash");
    /* smooth: no hash */
    tick(0, 90, 0.0f);
    tick(0, 90, 0.0f);
    g_rd.settings.motionBlurSmooth = 1;
    (void)hashAt(0.5f, 0, &ok2);
    CHECK(!ok2, "Motion blur: Smooth on: no hash");
    g_rd.settings.motionBlurSmooth = 0;
    (void)hashAt(0.5f, 0, &ok2);
    CHECK(ok2, "Smooth off again: a hash");
    testDecision();
    rd_shutdown();
    if (failures) {
        printf("rd_still_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_still_test: ok\n");
    return 0;
}
