/* rd_ssaa_shadow_test.c: the shadow count under supersampling at a scene
 * scale the render-target limit made fractional (issue 209's log :902-:923:
 * Custom 2x, 16:9, Full height, Full pixel, 4x supersampling on a tile-based
 * GPU, whose 4096 limit turns 688 x 448 x 8 into 4096 x 2667 at 5.95349 a
 * GS pixel; "shadow command without a depth-stencil target of the colour
 * target's size: skipped").
 *
 * On a Vulkan device (77 without one): a frame at GS 688 x 448 whose list 0
 * draws a ground into SCENE and whose list 3 draws Shadow.c's count (its
 * count target, rd_shadow_count_target, with SCENE's depth: a reset, a
 * volume over the ground, the resolve), replayed at each setting below; the
 * count (rd__shadow_count_read, at its GS size) must be drawn (the resolve
 * counted in RdPerfRecord.shadowOne + shadowSeven) and hold the volume's
 * count at its centre, the count target's texels must be SCENE's, and no
 * shadow command may be skipped:
 *   2x 16:9 Full pixel                  the whole scale (the reference)
 *   2x 16:9 Full pixel, 4x supersampling the reporter's: on the tile-based
 *                                        class (ICO_VK_FAKE_LIMITS=moltenvk,
 *                                        ctest rd_ssaa_shadow_tiler) the 4096
 *                                        limit makes the scale fractional
 *   3x 16:9 Full pixel, 2x supersampling another clamped scale
 *   2x 4:3, 4x supersampling             no crop
 * then a live switch to 4x supersampling with the renderer's own thread and
 * across between-tick presents: every replay after it draws its count; and
 * the shadow commands' size rule (rd__shadow_size_agree, rd_msaa_shadow.c):
 * a scene-sized temporary made at 2x, held across the switch, is made again
 * at SCENE's texels (4096 x 2667 at 5.95349) and agrees, where the rule
 * before skipped every shadow command on it; a named target of another size
 * does not agree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_replay.h" /* rd__shadow_size_agree */
#include "rd_thread.h"
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

#define GW 688
#define GH 448

static const uint8_t kGround[4] = {0x60, 0x50, 0x40, 0x80}, kClear[4] = {30, 60, 90, 0};
static uint8_t s_cnt[GW * GH * 4];

static RdScreenVtx vtx(int32_t x, int32_t y, uint32_t z, const uint8_t c[4])
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - GW / 2) * 16 + x;
    v.y = (2048 - GH / 2) * 16 + y;
    v.z = z;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

static void settings(float scale, float aspect, int aa)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = 1920;
    s.outputHeight = 1080;
    s.aspect = aspect;
    s.vsync = 1;
    s.sceneScale = scale;
    s.fullHeightScene = 1;
    s.fullPixel = aspect > 1.5f ? 1 : 0;
    s.antiAlias = (uint8_t)aa;
    rd_set_settings(&s);
}

static RdTarget s_cntTarget;

/* the frame; with midAlpha >= 0 a present of the frames before at that
 * alpha while it is recorded (after rd_begin_frame applied the settings),
 * as the window presents between ticks on the render thread, the present's
 * counts into *midCounted (-1: no present made) */
static void frameEx(float midAlpha, int *midCounted)
{
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    if (midAlpha >= 0.0f) {
        const uint32_t before = g_rdPerf.replay;
        const bool shown = rd_present(midAlpha);
        *midCounted = shown && g_rdPerf.replay != before && !g_rdPerf.reshown
                          ? (int)(g_rdPerf.shadowOne + g_rdPerf.shadowSeven)
                          : -1;
    }
    rd_select_list(0);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_clear_target(scene, kClear, 1, 0);
    rd_abe(0);
    rd_fba(0);
    rd_gouraud(0);
    rd_texture_off();
    rd_z_write(1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    RdScreenVtx g[6] = {
        vtx(0, 0, 0x40000000u, kGround),       vtx(GW * 16, 0, 0x40000000u, kGround),
        vtx(0, GH * 16, 0x40000000u, kGround), vtx(0, GH * 16, 0x40000000u, kGround),
        vtx(GW * 16, 0, 0x40000000u, kGround), vtx(GW * 16, GH * 16, 0x40000000u, kGround)};
    rd_screen_prims(RD_PRIM_TRIANGLES, g, 6, RD_SPACE_WORLD, 1, 0);
    rd_select_list(3);
    const RdTarget cnt = rd_shadow_count_target(GW, GH);
    s_cntTarget = cnt;
    rd_set_target(cnt, scene, GW, GH, 0);
    rd_z_write(0);
    rd_test_gs(0x30000);
    rd_abe(0);
    rd_texture_off();
    rd_shadow_reset();
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_blend_func(RD_BLEND_CS_FIX_ADD_CD, 0x80);
    rd_col_clamp(0);
    /* a volume's front face in front of the ground, over the middle */
    RdScreenVtx t[6] = {vtx(200 * 16 + 3, 100 * 16 + 5, 0x60000000u, kGround),
                        vtx(480 * 16 + 9, 100 * 16 + 5, 0x60000000u, kGround),
                        vtx(200 * 16 + 3, 340 * 16 + 7, 0x60000000u, kGround),
                        vtx(200 * 16 + 3, 340 * 16 + 7, 0x60000000u, kGround),
                        vtx(480 * 16 + 9, 100 * 16 + 5, 0x60000000u, kGround),
                        vtx(480 * 16 + 9, 340 * 16 + 7, 0x60000000u, kGround)};
    const int8_t sign[2] = {1, 1};
    rd_shadow_tris(t, sign, 2, 0);
    rd_shadow_resolve();
    rd_col_clamp(1);
    rd_end_frame(0);
}

static void frame(void)
{
    frameEx(-1.0f, NULL);
}

/* the between-tick presents across the switch (Frame rate above Original):
 * two ticks at 2x, then 4x supersampling asked; the first frame recorded
 * after it is presented at alpha 0.5 while it is recorded (the pair of the
 * ticks before, recorded at 2x, whose count targets have 2x's texels), then
 * the next frames' presents: every present draws its count */
static void interpSwitch(void)
{
    RdSettings s;
    settings(2.0f, 16.0f / 9.0f, RD_AA_OFF);
    s = *rd_get_settings();
    s.interpolate = 1;
    rd_set_settings(&s);
    frame();
    frame();
    int mid = -1;
    CHECK(rd_present(0.5f), "interp: the present at 2x");
    const int at2x = (int)(g_rdPerf.shadowOne + g_rdPerf.shadowSeven);
    s.antiAlias = RD_AA_SSAA4;
    rd_set_settings(&s);
    frameEx(0.5f, &mid);
    const RdTargetRec *ts = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    int after = 0, presents = 0;
    for (int i = 0; i < 3; i++) {
        frame();
        if (rd_present(0.5f)) {
            presents++;
            after += (int)(g_rdPerf.shadowOne + g_rdPerf.shadowSeven) > 0;
        }
    }
    printf("  interp switch: a present at 2x drew %d counts; across the switch to 4x "
           "supersampling (SCENE %ux%u) the present while the first frame records drew %d "
           "(-1: no replay: shown again or none); %d of the %d presents after drew theirs\n",
           at2x, ts ? ts->tw : 0, ts ? ts->th : 0, mid, after, presents);
    CHECK(at2x == 1, "interp: the present at 2x drew %d counts", at2x);
    CHECK(mid != 0, "interp: the present across the switch drew no shadow count");
    CHECK(presents > 0 && after == presents, "interp: %d of %d presents after drew the count",
          after, presents);
    s.interpolate = 0;
    rd_set_settings(&s);
    frame();
}

typedef struct Run {
    int ok, drawn, centre;
    uint32_t sceneTw, sceneTh, cntTw, cntTh;
    float sx;
} Run;

static Run run(const char *name, float scale, float aspect, int aa)
{
    Run o;
    memset(&o, 0, sizeof(o));
    settings(scale, aspect, aa);
    frame(); /* the settings' first replay */
    frame();
    o.drawn = (int)(g_rdPerf.shadowOne + g_rdPerf.shadowSeven);
    const RdTargetRec *ts = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const RdTargetRec *tc = rd__target_rec(s_cntTarget.id);
    o.sceneTw = ts ? ts->tw : 0;
    o.sceneTh = ts ? ts->th : 0;
    o.cntTw = tc ? tc->tw : 0;
    o.cntTh = tc ? tc->th : 0;
    o.sx = ts ? ts->sx : 0.0f;
    rhi_wait_idle();
    uint32_t w = 0, h = 0;
    o.ok =
        rd__shadow_count_read(s_cntTarget.id, s_cnt, sizeof(s_cnt), &w, &h) && w == GW && h == GH;
    uint8_t want[4];
    rd__shadow_table_texel(1, rd__shadow_disc_step(), want);
    o.centre = o.ok && memcmp(&s_cnt[((size_t)220 * GW + 340) * 4], want, 4) == 0;
    printf("  %s: SCENE %ux%u at %.5f, count target %ux%u, %d counts drawn, read %d, centre %s\n",
           name, o.sceneTw, o.sceneTh, (double)o.sx, o.cntTw, o.cntTh, o.drawn, o.ok,
           o.centre ? "right" : "wrong");
    CHECK(o.drawn == 1, "%s: %d counts drawn (one expected)", name, o.drawn);
    CHECK(o.ok, "%s: the count not read back", name);
    CHECK(o.centre, "%s: the volume's centre does not hold the count 1", name);
    CHECK(o.cntTw == o.sceneTw && o.cntTh == o.sceneTh, "%s: the count target %ux%u, SCENE %ux%u",
          name, o.cntTw, o.cntTh, o.sceneTw, o.sceneTh);
    return o;
}

/* the records after replay `after`: how many, how many drew their count */
static void drain(uint32_t after, uint32_t *n, uint32_t *counted, uint32_t *firstMissing)
{
    RdPerfRecord r;
    *n = *counted = 0;
    *firstMissing = 0;
    while (rd_perf_pop(&r)) {
        if (r.replay <= after) {
            continue;
        }
        (*n)++;
        if (r.shadowOne + r.shadowSeven) {
            (*counted)++;
        } else if (!*firstMissing) {
            *firstMissing = r.replay;
        }
    }
}

/* the live switch with the renderer's own thread (as the window runs it):
 * frames at 2x, then 4x supersampling asked while the game runs; every
 * replay after the switch draws its count */
static void liveSwitch(void)
{
    uint32_t n = 0, counted = 0, missing = 0;
    settings(2.0f, 16.0f / 9.0f, RD_AA_OFF);
    bool thread = rd_thread_local_start(RD_THREAD_ON);
    CHECK(thread, "live: the renderer's own thread did not start");
    for (int i = 0; i < 4; i++) {
        frame();
    }
    if (thread) {
        rd_thread_local_stop();
    }
    rhi_wait_idle();
    drain(0, &n, &counted, &missing);
    const uint32_t last = g_rdPerf.replay;
    printf("  live 2x: %u replays, %u drew the count\n", n, counted);
    CHECK(n > 0 && counted == n, "live 2x: %u of %u replays drew the count", counted, n);
    thread = rd_thread_local_start(RD_THREAD_ON);
    settings(2.0f, 16.0f / 9.0f, RD_AA_SSAA4);
    for (int i = 0; i < 8; i++) {
        frame();
    }
    if (thread) {
        rd_thread_local_stop();
    }
    rhi_wait_idle();
    drain(last, &n, &counted, &missing);
    const RdTargetRec *ts = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    printf("  live switch to 4x supersampling: SCENE %ux%u, %u replays, %u drew the count "
           "(the first without: replay %u)\n",
           ts ? ts->tw : 0, ts ? ts->th : 0, n, counted, missing);
    CHECK(n >= 2 && counted == n,
          "live switch: %u of %u replays after it drew the count, the first without replay %u",
          counted, n, missing);
}

static void sizeAgree(void)
{
    settings(2.0f, 16.0f / 9.0f, RD_AA_OFF);
    frame();
    const uint32_t id = rd__temp_target_alloc(GW, GH, 0, 1); /* kept across frames */
    settings(2.0f, 16.0f / 9.0f, RD_AA_SSAA4);
    frame(); /* the settings applied, SCENE made again */
    RdTargetRec *tc = rd__target_rec(id);
    RdTargetRec *ts = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const uint32_t tw0 = tc ? tc->tw : 0, th0 = tc ? tc->th : 0;
    const bool agree = tc && ts && rd__shadow_size_agree(tc, ts);
    printf("  size rule: a temporary made at 2x (%ux%u) against SCENE %ux%u: %s, now %ux%u\n", tw0,
           th0, ts ? ts->tw : 0, ts ? ts->th : 0, agree ? "made again" : "skipped", tc ? tc->tw : 0,
           tc ? tc->th : 0);
    CHECK(tc && ts && tw0 != ts->tw, "size rule: the temporary already has SCENE's texels");
    CHECK(agree && tc->tw == ts->tw && tc->th == ts->th && tc->color.id,
          "size rule: the held temporary was not made again at SCENE's texels");
    RdTargetRec *shadow0 = rd__target_rec(rd_target(RD_TARGET_SHADOW0).id);
    CHECK(shadow0 && !rd__shadow_size_agree(shadow0, ts),
          "size rule: a named target of another size agrees");
    rd__temp_target_free(id);
}

int main(void)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = 1920;
    s.outputHeight = 1080;
    s.aspect = 16.0f / 9.0f;
    s.vsync = 1;
    s.sceneScale = 2.0f;
    if (!rd_init(GW, GH, &s, NULL)) {
        printf("rd_ssaa_shadow_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    printf("  device %s: tile-based %d, largest render target %u\n", rhi_adapter_name(),
           (int)rhi_limits()->tiler, rhi_limits()->maxRenderTargetSize);
    (void)run("2x 16:9", 2.0f, 16.0f / 9.0f, RD_AA_OFF);
    (void)run("2x 16:9 4x supersampling", 2.0f, 16.0f / 9.0f, RD_AA_SSAA4);
    (void)run("3x 16:9 2x supersampling", 3.0f, 16.0f / 9.0f, RD_AA_SSAA2);
    (void)run("2x 4:3 4x supersampling", 2.0f, 4.0f / 3.0f, RD_AA_SSAA4);
    liveSwitch();
    interpSwitch();
    sizeAgree();
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_ssaa_shadow_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_ssaa_shadow_test: ok\n");
    return 0;
}
