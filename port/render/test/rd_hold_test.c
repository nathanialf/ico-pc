/* rd_hold_test: the paused scene's hold, its CPU half (rd_hold_key.c), on
 * frames recorded without a device.
 *
 *   counters    a texture made, given the same texels (no change), new
 *               texels, a rectangle of the same and of new texels,
 *               destroyed; a mesh made, morphed to the same stream (no
 *               change) and to another, destroyed: each change moves its
 *               counter by one, an unchanged write by none
 *   lists key   two ticks with the same scene and another menu: one key;
 *               the menu's payload recorded before the scene's (every
 *               offset moved): the same key; one scene sprite's colour, one
 *               scene state command (the blend's FIX) and one command more:
 *               another key each
 *   state key   a setting changed and back
 *   scratch     a mesh draw: its live mesh written again keeps the key; as
 *               the interpolation's scratch mesh (transient) the key holds
 *               until its stream is rewritten, then changes
 *   scan        a plain frame: qualifies, list 11's draws counted; the aura
 *               into FEED128: FEED128 carried; the motion blur reading
 *               DISPLAY: the picture before; a list 11 sprite with a Z
 *               test on SCENE's depth, one reading WORK0, one drawn into
 *               WORK0: list 11 refused, its command named; a keep frame
 *   decision    rd__hold_why for every input in its order
 */
#include <stdio.h>
#include <string.h>

#include "rd_hold.h"
#include "rd_mesh.h"
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

static void sprite(int x0, int y0, int x1, int y1, const uint8_t c[4])
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
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 0, 0);
}

/* what a frame holds besides its plain scene and menu */
enum {
    F_MENU_FIRST = 1,  /* the menu's sprite recorded before the scene's */
    F_AURA = 2,        /* an aura post into FEED128 in list 8 */
    F_BLUR = 4,        /* a motion blur reading DISPLAY in list 7 */
    F_MENU_ZTEST = 8,  /* the menu sprite under a Z GEQUAL test on SCENE's depth */
    F_MENU_WORK0 = 16, /* the menu sprite reads WORK0 */
    F_MENU_INTO = 32,  /* the menu sprite drawn into WORK0 */
    F_FIX = 64,        /* the scene's blend FIX 0x40 instead of 0x80 */
    F_MORE = 128,      /* one scene sprite more */
};

static void scene(uint8_t blue, int flags)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    const uint8_t c[4] = {90, 90, blue, 0x80};
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), TW, TH, 0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, (flags & F_FIX) ? 0x40 : 0x80, 0);
    rd_texture_off();
    sprite(10, 10, 60, 40, c);
    if (flags & F_MORE) {
        sprite(70, 10, 90, 40, c);
    }
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x20;
    pp.blend = RD_BLEND_LERP_FIX;
    pp.scalar[2] = 1.0f;
    if (flags & F_BLUR) {
        rd_select_list(7);
        rd_blend_func(RD_BLEND_LERP_FIX, 0x20);
        rd_texture(rd_target_texture(rd_target(RD_TARGET_DISPLAY), RD_VIEW_RGB24_TA0),
                   RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_post(RD_POST_MOTION_BLUR, &pp);
        rd_texture_off();
    }
    if (flags & F_AURA) {
        rd_select_list(8);
        rd_set_target(rd_target(RD_TARGET_FEED128), (RdTarget){0}, 128, 128, 0);
        rd_post(RD_POST_AURA, &pp);
    }
}

static void menu(uint8_t red, int flags)
{
    const uint8_t c[4] = {red, 20, 20, 0x80};
    rd_select_list(11);
    if (flags & F_MENU_INTO) {
        rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, TW, TH, 0);
    } else {
        rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), TW, TH, 0);
    }
    rd_test_gs((flags & F_MENU_ZTEST) ? RD_TEST_Z_GEQUAL : RD_TEST_Z_ALWAYS);
    if (flags & F_MENU_WORK0) {
        rd_texture(rd_target_texture(rd_target(RD_TARGET_WORK0), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
                   RD_TCC_RGBA);
    } else {
        rd_texture_off();
    }
    sprite(100, 50, 120, 60, c);
    rd_texture_off();
}

static const RdFrame *frame(uint8_t blue, uint8_t red, int flags, int keep)
{
    rd_begin_frame();
    if (flags & F_MENU_FIRST) {
        menu(red, flags);
        scene(blue, flags);
    } else {
        scene(blue, flags);
        menu(red, flags);
    }
    rd_end_frame(keep);
    return rd__last_frame();
}

static uint64_t keyOf(uint8_t blue, uint8_t red, int flags)
{
    return rd__hold_lists_key(frame(blue, red, flags, 0));
}

static void testCounters(void)
{
    uint8_t px[16 * 16 * 4];
    memset(px, 7, sizeof(px));
    uint64_t t0 = rd__hold_tex_gen();
    const RdTex tex = rd_create_texture(16, 16, px, (RdTexA)0, "hold test");
    CHECK(tex.id != 0, "a texture");
    CHECK(rd__hold_tex_gen() == t0 + 1, "made: one change (%llu)",
          (unsigned long long)(rd__hold_tex_gen() - t0));
    t0 = rd__hold_tex_gen();
    rd_update_texture(tex, px);
    CHECK(rd__hold_tex_gen() == t0, "the same texels: no change");
    px[5] = 9;
    rd_update_texture(tex, px);
    CHECK(rd__hold_tex_gen() == t0 + 1, "new texels: one change");
    t0 = rd__hold_tex_gen();
    rd_update_texture_rect(tex, 0, 0, 2, 2, px);
    CHECK(rd__hold_tex_gen() == t0, "a rectangle of the same texels: no change");
    uint8_t four[2 * 2 * 4];
    memset(four, 200, sizeof(four));
    rd_update_texture_rect(tex, 0, 0, 2, 2, four);
    CHECK(rd__hold_tex_gen() == t0 + 1, "a rectangle of new texels: one change");
    t0 = rd__hold_tex_gen();
    rd_destroy_texture(tex);
    CHECK(rd__hold_tex_gen() == t0 + 1, "destroyed: one change");

    /* a mesh of 3 vertices of one quadword, one batch whose tag is
       quadword 0 of the update's stream (the vertices from quadword 1) */
    float stream[3][4], qw[4][4];
    memset(stream, 0, sizeof(stream));
    for (int i = 0; i < 3; i++) {
        stream[i][0] = (float)i;
    }
    const uint32_t index[3] = {0, 1, 2};
    RdVuBatchRec b;
    memset(&b, 0, sizeof(b));
    b.indexCount = 3;
    b.vertexCount = 3;
    uint64_t m0 = rd__hold_mesh_gen();
    const uint32_t id = rd__vu_mesh_create_raw(stream, 3, 1, index, 3, &b, 1, "hold test");
    CHECK(id != 0, "a mesh");
    CHECK(rd__hold_mesh_gen() == m0 + 1, "made: one change (%llu)",
          (unsigned long long)(rd__hold_mesh_gen() - m0));
    memset(qw, 0, sizeof(qw));
    memcpy(qw[1], stream, sizeof(stream));
    m0 = rd__hold_mesh_gen();
    CHECK(rd_update_vu_mesh((RdMesh){id}, (const float (*)[4])qw), "morphed");
    CHECK(rd__hold_mesh_gen() == m0, "morphed to the same stream: no change");
    qw[2][1] = 5.0f;
    CHECK(rd_update_vu_mesh((RdMesh){id}, (const float (*)[4])qw), "morphed again");
    CHECK(rd__hold_mesh_gen() == m0 + 1, "another stream: one change");
    m0 = rd__hold_mesh_gen();
    rd_destroy_vu_mesh((RdMesh){id});
    CHECK(rd__hold_mesh_gen() == m0 + 1, "destroyed: one change");
}

static void testKeys(void)
{
    /* the start state is the frame before's end: one frame first, so every
       frame compared starts from the same */
    (void)keyOf(90, 200, 0);
    const uint64_t base = keyOf(90, 200, 0);
    CHECK(keyOf(90, 200, 0) == base, "the same scene and menu, the next tick: one key");
    CHECK(keyOf(90, 10, 0) == base, "another menu: the same key");
    CHECK(keyOf(90, 10, F_MENU_FIRST) == base, "the menu recorded first: the same key");
    CHECK(keyOf(91, 200, 0) != base, "one colour word of the scene: another key");
    CHECK(keyOf(90, 200, F_FIX) != base, "one state command of the scene: another key");
    CHECK(keyOf(90, 200, F_MORE) != base, "one scene command more: another key");
    const uint64_t s0 = rd__hold_state_key();
    CHECK(rd__hold_state_key() == s0, "the state key again: the same");
    g_rd.settings.fullPixel ^= 1;
    CHECK(rd__hold_state_key() != s0, "a setting changed: another state key");
    g_rd.settings.fullPixel ^= 1;
    CHECK(rd__hold_state_key() == s0, "the setting back: the same state key");
}

/* a mesh draw's key and the interpolation's scratch meshes (rd_interp.c
   morphDraw: one id rewritten at every present, transient) */
static void testScratch(void)
{
    float stream[3][4];
    memset(stream, 0, sizeof(stream));
    const uint32_t index[3] = {0, 1, 2};
    RdVuBatchRec b;
    memset(&b, 0, sizeof(b));
    b.indexCount = 3;
    b.vertexCount = 3;
    const uint32_t id = rd__vu_mesh_create_raw(stream, 3, 1, index, 3, &b, 1, "hold scratch");
    RdMeshRec *m = rd__mesh_rec(id);
    CHECK(id != 0 && m != NULL, "a mesh");
    if (!m) {
        return;
    }
    static uint8_t payload[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    static RdCmd c;
    static RdFrame f;
    memset(&c, 0, sizeof(c));
    c.type = RDC_MESH;
    c.u[0] = id;
    c.u[1] = 0;
    c.u[2] = (uint32_t)sizeof(payload);
    f.lists[0].cmds = &c;
    f.lists[0].count = 1;
    f.payload = payload;
    f.payloadSize = (uint32_t)sizeof(payload);
    const uint64_t live = rd__hold_lists_key(&f);
    rd__mesh_stream_written(m);
    CHECK(rd__hold_lists_key(&f) == live,
          "a live mesh written again: the same key (its changes are the mesh counter's)");
    m->transient = 1;
    const uint64_t s0 = rd__hold_lists_key(&f);
    CHECK(rd__hold_lists_key(&f) == s0, "a scratch mesh not rewritten: the same key");
    rd__mesh_stream_written(m);
    CHECK(rd__hold_lists_key(&f) != s0, "a scratch mesh rewritten (the next present): another key");
    m->transient = 0;
    rd_destroy_vu_mesh((RdMesh){id});
}

static void testScan(void)
{
    RdHoldScan s;
    CHECK(rd__hold_scan(frame(90, 200, 0, 0), &s) == RD_HOLD_RULE_OK, "a plain frame qualifies");
    CHECK(s.crossN == 0 && s.menuDraws == 1, "nothing carried (%u), list 11's draw (%u)", s.crossN,
          s.menuDraws);
    CHECK(rd__hold_scan(frame(90, 200, F_AURA, 0), &s) == RD_HOLD_RULE_OK, "the aura qualifies");
    CHECK(s.crossN == 1 && s.cross[0] == rd_target(RD_TARGET_FEED128).id,
          "FEED128 carried (%u targets)", s.crossN);
    CHECK(rd__hold_scan(frame(90, 200, F_BLUR, 0), &s) == RD_HOLD_RULE_PICTURE,
          "the motion blur reads the picture before");
    CHECK(rd__hold_scan(frame(90, 200, F_MENU_ZTEST, 0), &s) == RD_HOLD_RULE_MENU &&
              s.badList == 11,
          "a Z test in list 11 refused (list %d)", s.badList);
    CHECK(rd__hold_scan(frame(90, 200, F_MENU_WORK0, 0), &s) == RD_HOLD_RULE_MENU,
          "list 11 reading WORK0 refused");
    CHECK(rd__hold_scan(frame(90, 200, F_MENU_INTO, 0), &s) == RD_HOLD_RULE_MENU,
          "list 11 drawing into WORK0 refused");
    CHECK(rd__hold_scan(frame(90, 200, 0, 1), &s) == RD_HOLD_RULE_KEEP, "a keep frame");
}

static RdHoldIn held(void)
{
    RdHoldIn in;
    memset(&in, 0, sizeof(in));
    in.paused = in.on = in.device = in.take = in.settled = true;
    in.rule = RD_HOLD_RULE_OK;
    in.lists = in.refLists = 11;
    in.state = in.refState = 12;
    in.meshGen = in.refMeshGen = 13;
    in.texGen = in.refTexGen = 14;
    return in;
}

static void testDecision(void)
{
    RdHoldIn in = held();
    CHECK(rd__hold_why(&in) == RD_HOLD_HELD, "every fact holds: held");
    in = held();
    in.paused = false;
    CHECK(rd__hold_why(&in) == RD_HOLD_NOT_PAUSED, "not paused");
    in = held();
    in.on = false;
    CHECK(rd__hold_why(&in) == RD_HOLD_OFF, "off");
    in = held();
    in.device = false;
    CHECK(rd__hold_why(&in) == RD_HOLD_DEVICE, "the device");
    in = held();
    in.rule = RD_HOLD_RULE_PICTURE;
    CHECK(rd__hold_why(&in) == RD_HOLD_PICTURE, "the picture before");
    in = held();
    in.rule = RD_HOLD_RULE_MENU;
    CHECK(rd__hold_why(&in) == RD_HOLD_MENU, "the menu's lists");
    in.rule = RD_HOLD_RULE_KEEP;
    CHECK(rd__hold_why(&in) == RD_HOLD_MENU, "a keep frame: the lists the hold cannot keep");
    in = held();
    in.take = false;
    CHECK(rd__hold_why(&in) == RD_HOLD_FIRST, "no copy yet: first");
    in = held();
    in.lists ^= 1;
    CHECK(rd__hold_why(&in) == RD_HOLD_LISTS, "lists differ");
    in = held();
    in.state ^= 1;
    CHECK(rd__hold_why(&in) == RD_HOLD_SETTINGS, "settings");
    in = held();
    in.meshGen++;
    CHECK(rd__hold_why(&in) == RD_HOLD_UPLOADS, "a mesh changed");
    in = held();
    in.texGen++;
    CHECK(rd__hold_why(&in) == RD_HOLD_UPLOADS, "a texture changed");
    in = held();
    in.texWaiting = true;
    CHECK(rd__hold_why(&in) == RD_HOLD_UPLOADS, "a texture waits for its upload");
    in = held();
    in.settled = false;
    CHECK(rd__hold_why(&in) == RD_HOLD_FEEDBACK, "the feedback not settled");
    /* the order: a list change outranks a texture change */
    in = held();
    in.lists ^= 1;
    in.texGen++;
    CHECK(rd__hold_why(&in) == RD_HOLD_LISTS, "lists first");
    for (int w = 0; w < RD_HOLD_COUNT; w++) {
        CHECK(strcmp(rd__hold_why_text(w), "unknown") != 0, "reason %d has words", w);
    }
}

int main(void)
{
    if (!rd__init_record_only(512, 512)) {
        printf("rd_hold_test: no context\n");
        return 1;
    }
    testCounters();
    testKeys();
    testScratch();
    testScan();
    testDecision();
    rd_shutdown();
    if (failures) {
        printf("rd_hold_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_hold_test: ok\n");
    return 0;
}
