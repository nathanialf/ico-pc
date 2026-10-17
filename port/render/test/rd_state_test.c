/* rd_state_test.c: the recording half of rd, without a GPU.
 *
 *   order      draws replay in list order 0..12, not recording order
 *   defaults   gsb_SetGsDefault's TEST/ZBUF/FBA/TEXA at the head of lists
 *              0, 1, 2, 4, 6..12 only
 *   leakage    list 3 inherits list 2's state; list 4's defaults reset
 *              TEST/ZBUF/FBA/TEXA but not ALPHA; list 5 inherits list 4;
 *              state left by list 12 reaches the next frame's list 0
 *   keep       rd_end_frame(1) walks lists 11 and 12 only
 *   retention  the closed frame and the one before it are kept
 *   stubs      VU, shadow-strip and post draws are recorded with payload
 *   region     rd__region_scale: exactly 1 at 4:3, 512 / W on a target of a
 *              W-wide frame's size, 1 on the others
 *   plans      AFAIL splits, blend paths, FIX clamps (rd__plan_screen_draw);
 *              the stair railings' state (TEST 0x5160D, ALPHA
 *              0x44 with ABE, Z write) splits into two passes that differ
 *              in Z write only (depth test GEQUAL in both, no stencil, no
 *              DATE), as the VU path and its edge-clipped fans (ABE forced
 *              on) draw them; the DATE snapshot and the shadow count write
 *              no Z; PABE flags (DF_C1_DST for Cd*FIX + Cs only)
 *   alpha merge the condition table (Z write x AFAIL x ATE x the alpha
 *              mask): one draw without the test where Z write is off and a
 *              failing fragment writes what a passing one writes, the pass
 *              and fail pair otherwise; the toggle restores the split
 *   dump       a frame with textures and a temp target survives dump/load;
 *              an image's GS levels and TEX1's mipmap fields (tex1Lod)
 *              round-trip; version 8 and older dumps (no tex1Lod, no level
 *              words) still load; RDC_AA1 and RdStateBlock.aa1 round-trip, and a
 *              version 3 dump (no aa1) still loads, with AA1 off; a version
 *              5 and a version 7 dump's temps at the handles FEED_HELD and
 *              DISPLAY_HELD (appended after them) are recreated
 *   dump v10   the native frame (aspect, wExact, cullW) after gsH round-trips
 *              and peeks (rd__dump_peek); a version 9 dump loads as the 4:3
 *              frame; a header the renderer cannot size is refused
 *   dump sheet a version 11 dump's sheets round-trip (1x the coverage
 *              alone, 2x over rd_sheet_rim's rim); one with a halo made on
 *              the CPU (w x 2h at every scale, a gain in bakedTexa bits
 *              16..23) loads as the image made again from its coverage
 *              (rd_dump.c sheetFromHalo)
 *   aa1 plans  PRIM.AA1 keys: the AA1 shaders, triangles, blending with ABE
 *              0 (DF_AA1_FULL), no Z write on lines; aa1 0 unchanged
 *   aura filter the model viewer's draw filter around the
 *              mirage's list 8: a GRID keyed by a second object inside the
 *              open window is kept (and after it), a third object's is not;
 *              list 8's targets, Z writes, clear and aura sprite survive
 *   pass ops   clear -> draw: the draw's pass takes the clear as its
 *              load op (colour and depth CLEAR), both stored; the passes that
 *              cannot take it keep their loads (rd__take_pending_clear)
 *   stencil ops clear -> draw -> shadow reset -> volumes -> resolve -> draw:
 *              outside the shadow window the stencil is not stored and
 *              loads DONT_CARE (a CLEAR stays CLEAR), inside it loads and
 *              stores; the reset is the volumes' pass's stencil CLEAR, not
 *              a pass of its own (rd__stencil_ops); two depths' windows
 *              interleaved keep their own counts (rd__stencil_open/Close)
 *   date area  the DATE snapshot of a screen-prim command covers its box
 *              (rd__screen_area: the vertices' box padded by two pixels, in
 *              the scissor), not the whole target; a sprite larger than the
 *              target clamps to it; rd_perf_test's DATE sprites still take
 *              one snapshot each (rd__date_retake)
 *   pipelines  the reachable screen and post set is under 400 keys (the
 *              sprite_texa_ps twins and the triangle planes included), with the VU program families
 *              under RD_PIPELINE_REACHABLE_MAX; font_sheet_ps has font_ps's
 *              10 keys
 *
 * argv[1]: a writable directory for the dump.  Exit 0 or 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- a walk that records the state at every screen draw, by key ---- */
typedef struct Seen {
    int n;
    uint32_t keys[64];
    int lists[64];
    RdStateBlock st[64];
} Seen;

static void onCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Seen *seen = user;
    (void)index;
    if (c->type == RDC_SCREEN && seen->n < 64) {
        seen->keys[seen->n] = c->keyLo;
        seen->lists[seen->n] = list;
        seen->st[seen->n] = *s;
        seen->n++;
    }
}

static void walk(const RdFrame *f, Seen *seen)
{
    memset(seen, 0, sizeof(*seen));
    RdStateBlock s = f->startState;
    rd__walk(f, (int)f->keep, &s, onCmd, seen);
}

static const RdStateBlock *stateFor(const Seen *s, uint32_t key)
{
    for (int i = 0; i < s->n; i++) {
        if (s->keys[i] == key) {
            return &s->st[i];
        }
    }
    return NULL;
}

static void draw(uint32_t key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[1].x = v[1].y = 16;
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, key);
}

static int testEq(const RdTestState *a, uint64_t gs)
{
    RdTestState b = rd_test_from_gs(gs);
    return a->ate == b.ate && a->atst == b.atst && a->aref == b.aref && a->afail == b.afail &&
           a->date == b.date && a->zte == b.zte && a->ztst == b.ztst;
}

static void testOrderAndLeakage(void)
{
    rd_begin_frame();
    rd_select_list(11);
    draw(11);
    rd_select_list(5);
    draw(5);
    rd_select_list(1);
    rd_fba(1);
    draw(1);
    rd_select_list(2);
    rd_blend(RD_BLEND_CS_AS_ADD_CD, 0x40, 1);
    rd_test_gs(RD_TEST_AT_LT129);
    rd_pabe(1);
    draw(2);
    rd_select_list(3);
    draw(3);
    rd_select_list(4);
    draw(4);
    rd_select_list(0);
    draw(0);
    rd_select_list(12);
    rd_blend(RD_BLEND_LERP_FIX, 0x33, 1);
    draw(12);
    rd_end_frame(0);

    const RdFrame *f = rd__last_frame();
    Seen s;
    walk(f, &s);
    static const uint32_t order[] = {0, 1, 2, 3, 4, 5, 11, 12};
    CHECK(s.n == 8, "8 draws walked, got %d", s.n);
    for (int i = 0; i < s.n && i < 8; i++) {
        CHECK(s.keys[i] == order[i], "replay order: draw %d is key %u, expected %u", i, s.keys[i],
              order[i]);
    }

    /* defaults at the head of the covered lists, nothing in 3 and 5 */
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        int covered = l != 3 && l != 5;
        int head = cl->count >= 4 && cl->cmds[0].type == RDC_TEST &&
                   cl->cmds[1].type == RDC_ZWRITE && cl->cmds[2].type == RDC_FBA &&
                   cl->cmds[3].type == RDC_TEXA;
        CHECK(covered == head, "list %d defaults %s", l, covered ? "missing" : "present");
    }

    const RdStateBlock *s1 = stateFor(&s, 1), *s2 = stateFor(&s, 2), *s3 = stateFor(&s, 3);
    const RdStateBlock *s4 = stateFor(&s, 4), *s5 = stateFor(&s, 5), *s0 = stateFor(&s, 0);
    const RdStateBlock *s11 = stateFor(&s, 11);
    if (!s0 || !s1 || !s2 || !s3 || !s4 || !s5 || !s11) {
        CHECK(0, "a keyed draw was not walked");
        return;
    }
    CHECK(testEq(&s0->ds.test, RD_TEST_Z_GEQUAL), "list 0 TEST default");
    CHECK(s1->ds.fba == 1 && testEq(&s1->ds.test, RD_TEST_AT_GT64_FBONLY) &&
              s1->ds.texa == RD_TEXA_7F_81_AEM,
          "list 1: semitrans defaults then FBA 1");
    CHECK(s2->ds.fba == 0, "list 2 default resets FBA");
    CHECK(s2->ds.blend == RD_BLEND_CS_AS_ADD_CD && s2->ds.blendFix == 0x40 && s2->ds.abe == 1 &&
              s2->ds.pabe == 1 && testEq(&s2->ds.test, RD_TEST_AT_LT129),
          "list 2 state");
    CHECK(memcmp(s3, s2, sizeof(*s3)) == 0, "list 3 inherits list 2's state exactly");
    CHECK(testEq(&s4->ds.test, RD_TEST_DATE1_Z_GEQUAL) && s4->ds.zwrite == RD_ZWRITE_OFF &&
              s4->ds.fba == 0 && s4->ds.texa == RD_TEXA_80_80,
          "list 4 defaults applied");
    CHECK(s4->ds.blend == RD_BLEND_CS_AS_ADD_CD && s4->ds.blendFix == 0x40 && s4->ds.pabe == 1,
          "list 4: ALPHA and PABE leak through the defaults");
    CHECK(memcmp(s5, s4, sizeof(*s5)) == 0, "list 5 inherits list 4's state exactly");
    CHECK(testEq(&s11->ds.test, RD_TEST_Z_GEQUAL) && s11->ds.zwrite == RD_ZWRITE_ON,
          "list 11 defaults");
    CHECK(f->endState.ds.blend == RD_BLEND_LERP_FIX && f->endState.ds.blendFix == 0x33,
          "end state is list 12's");

    /* next frame: list 0 sees list 12's ALPHA and PABE, its own defaults */
    rd_begin_frame();
    draw(100);
    rd_end_frame(0);
    walk(rd__last_frame(), &s);
    const RdStateBlock *n0 = stateFor(&s, 100);
    CHECK(n0 && n0->ds.blend == RD_BLEND_LERP_FIX && n0->ds.blendFix == 0x33 && n0->ds.pabe == 1 &&
              testEq(&n0->ds.test, RD_TEST_Z_GEQUAL),
          "state crosses the frame boundary except the list defaults");
    CHECK(rd__prev_frame() && rd__prev_frame()->number + 1 == rd__last_frame()->number,
          "previous frame retained");
}

static void testKeep(void)
{
    rd_begin_frame();
    rd_select_list(12);
    rd_blend(RD_BLEND_CD_SUB_CS_AS, 0x11, 1);
    rd_end_frame(0);
    rd_begin_frame();
    rd_select_list(0);
    rd_blend(RD_BLEND_CS_FIX_ADD_CD, 0x22, 1);
    draw(200);
    rd_select_list(11);
    draw(211);
    rd_end_frame(1);
    Seen s;
    walk(rd__last_frame(), &s);
    CHECK(s.n == 1 && s.keys[0] == 211, "keep frame walks list 11/12 only (%d draws)", s.n);
    const RdFrame *f = rd__last_frame();
    CHECK(f->keep == 1 && f->endState.ds.blend == RD_BLEND_CD_SUB_CS_AS &&
              f->endState.ds.blendFix == 0x11,
          "keep frame: list 0's ALPHA never applied");
}

static void testStubs(void)
{
    /* a VU mesh of one batch of three prelit vertices (a GIF
     * tag with NLOOP 3, then pos, ST, colour each) */
    static float qw[1 + 3 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f; /* strip flag */
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 10;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    md.materialCount = 2;
    RdMesh m = rd_create_vu_mesh(&md);
    const RdMeshRec *mr = rd__mesh_rec(m.id);
    CHECK(mr && mr->vertexCount == 3 && mr->indexCount == 3 && mr->index[0] == ICO_VU_INDEX(2, 0),
          "VU mesh: three vertices, one kick");
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_LIT;
    d.code = 32;
    d.vu.mem[2][0] = 0.5f;
    RdXform xf;
    static const uint8_t lut[256 * 4] = {1, 2, 3, 4};
    rd_begin_frame();
    rd_select_list(3);
    rd_draw_vu_mesh(m, &d, RD_KEY(&xf, 1, 2));
    float sv[4][4] = {{0}};
    rd_shadow_strip(sv, 4, -1.0f, 7);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.lut = lut;
    pp.scalar[0] = 0.5f;
    rd_post(RD_POST_FOG, &pp);
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    const RdCmdList *cl = &f->lists[3];
    CHECK(cl->count == 3, "three stub commands in list 3, got %u", cl->count);
    if (cl->count == 3) {
        RdKey k = RD_KEY(&xf, 1, 2);
        CHECK(cl->cmds[0].type == RDC_MESH && cl->cmds[0].u[0] == m.id &&
                  cl->cmds[0].keyLo == (uint32_t)k && cl->cmds[0].keyHi == (uint32_t)(k >> 32) &&
                  cl->cmds[0].u[2] == sizeof(RdVuPayload) + sizeof(RdVuBlock),
              "mesh draw recorded with key and payload");
        RdVuPayload p;
        memcpy(&p, f->payload + cl->cmds[0].u[1], sizeof(p));
        float m2;
        memcpy(&m2, f->payload + cl->cmds[0].u[1] + sizeof(p) + 2 * 16, 4);
        CHECK(p.prog == RD_PROG_LIT && p.code == 32 && p.batchCount == 1 && m2 == 0.5f,
              "the VU payload: program, code, batches, VuCB");
        CHECK(cl->cmds[1].type == RDC_SHADOW_STRIP && cl->cmds[1].u[0] == 4 &&
                  cl->cmds[1].f[0] == -1.0f,
              "shadow strip recorded");
        CHECK(cl->cmds[2].type == RDC_POST_STUB && cl->cmds[2].b[0] == RD_POST_FOG,
              "fog post recorded");
        RdPostRec r;
        memcpy(&r, f->payload + cl->cmds[2].u[1], sizeof(r));
        CHECK(r.scalar[0] == 0.5f && r.lutOffset != ~0u &&
                  memcmp(f->payload + r.lutOffset, lut, sizeof(lut)) == 0,
              "fog parameters and LUT in the payload");
    }
}

static void testPlans(void)
{
    RdStateBlock s;
    RdDrawPass dp[2];
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_AT_GT64_FBONLY);
    s.ds.zwrite = RD_ZWRITE_ON;
    int n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                 RHI_FMT_D32F_S8, dp);
    CHECK(n == 2 && dp[0].key.gs.zwrite == RD_ZWRITE_ON && dp[1].key.gs.zwrite == RD_ZWRITE_OFF &&
              dp[0].modeZ == (RD_ATST_GREATER | 1u << 8 | 1u << 16) &&
              dp[1].modeZ == (RD_ATST_GREATER | 1u << 8 | 2u << 16) && dp[0].aref == 0x40 &&
              dp[0].key.gs.ztst == RD_ZTST_GEQUAL,
          "AFAIL FB_ONLY: two passes, Z written by the passing one only");

    /* the railing (tesri.tm2's TEST 0x5160D: ATE GREATER 0x60,
     * AFAIL FB_ONLY, Z GEQUAL; ALPHA 0x44 with ABE; Z write on), with ABE
     * as the material packet sets it and as rd_replay_vu.c doVuDraw's scissor fans force it:
     * the failing pass is the passing one's pipeline with Z write off, so
     * a hole keeps the depth test and leaves the Z buffer to what is drawn
     * behind it later */
    for (int abe = 0; abe < 2; abe++) {
        rd__reset_state_block(&s);
        s.ds.test = rd_test_from_gs(0x5160D);
        s.ds.zwrite = RD_ZWRITE_ON;
        s.ds.abe = (uint8_t)abe;
        s.ds.blend = RD_BLEND_LERP_AS;
        n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                 RHI_FMT_D32F_S8, dp);
        RdPipeKeyInt k1 = dp[1].key;
        k1.gs.zwrite = RD_ZWRITE_ON;
        CHECK(n == 2 && s.ds.test.aref == 0x60 && s.ds.test.afail == RD_AFAIL_FB_ONLY &&
                  dp[0].key.gs.zwrite == RD_ZWRITE_ON && dp[1].key.gs.zwrite == RD_ZWRITE_OFF &&
                  rd__pipe_key_equal(&k1, &dp[0].key) && dp[1].key.gs.ztst == RD_ZTST_GEQUAL &&
                  dp[1].key.gs.stencil == RD_STENCIL_OFF && dp[1].key.gs.date == RD_DATE_OFF &&
                  dp[1].key.gs.colorMask == 0xF && dp[0].flags == dp[1].flags &&
                  !(dp[1].flags & ICO_DF_DATE) && dp[1].aref == 0x60 &&
                  dp[1].modeZ == (RD_ATST_GREATER | 1u << 8 | 2u << 16) &&
                  dp[1].key.gs.blend == (abe ? RD_BLEND_LERP_AS : RD_BLEND_COUNT),
              "railing TEST 0x5160D ABE %d: the failing pass differs in Z write only", abe);
    }
    {
        /* the passes that are not draws: no Z written, no split */
        const RdPipeKeyInt snap = rd__post_key(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
        CHECK(snap.gs.zwrite == RD_ZWRITE_OFF && snap.depthFmt == RHI_FMT_UNKNOWN,
              "the DATE snapshot writes no Z and binds no depth");
        rd__reset_state_block(&s);
        s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
        s.ds.zwrite = RD_ZWRITE_ON;
        for (int decr = 0; decr < 2; decr++) {
            const RdPipeKeyInt v = rd__shadow_volume_key(&s, RHI_FMT_RGBA8_UNORM, decr);
            CHECK(v.gs.zwrite == RD_ZWRITE_OFF && v.gs.colorMask == 0,
                  "the shadow count writes no Z (decr %d)", decr);
        }
    }
    /* PABE: a uniform; DF_C1_DST tells the shader the
     * unblended c1 is 0 for Cd*FIX + Cs only */
    rd__reset_state_block(&s);
    s.ds.abe = 1;
    s.ds.pabe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                         dp);
    CHECK((dp[0].flags & ICO_DF_PABE) && !(dp[0].flags & ICO_DF_C1_DST) && dp[0].key.gs.pabe == 0,
          "PABE on a lerp: DF_PABE, no DF_C1_DST, normalised out of the key");
    s.ds.blend = RD_BLEND_CD_FIX_ADD_CS;
    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                         dp);
    CHECK((dp[0].flags & (ICO_DF_PABE | ICO_DF_C1_DST)) == (ICO_DF_PABE | ICO_DF_C1_DST),
          "PABE on Cd*FIX + Cs: DF_C1_DST");

    rd__reset_state_block(&s);
    s.ds.zwrite = RD_ZWRITE_ON;
    s.ds.test = rd_test_from_gs(RD_TEST_NEVER_RGBONLY);
    n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].key.gs.colorMask == 0x7 && dp[0].key.gs.zwrite == RD_ZWRITE_OFF &&
              dp[0].modeZ == 0,
          "ATST NEVER + RGB_ONLY: one RGB-only pass without Z");

    rd__reset_state_block(&s);
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_CS_AS_ADD_CD;
    n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
    CHECK(n == 1 && (dp[0].flags & ICO_DF_PREMUL) && !(dp[0].flags & ICO_DF_FIX_FACTOR) &&
              rd__blend_path(dp[0].key.gs.blend) == RD_BP_PREMUL_ADD,
          "Cs*As + Cd goes through DF_PREMUL");
    s.ds.blend = RD_BLEND_CS_FIX_ADD_CD;
    s.ds.blendFix = 0xFF;
    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                         dp);
    CHECK((dp[0].flags & (ICO_DF_PREMUL | ICO_DF_FIX_FACTOR)) ==
                  (ICO_DF_PREMUL | ICO_DF_FIX_FACTOR) &&
              dp[0].fix == 0xFF,
          "Cs*FIX + Cd keeps FIX 0xFF exact through DF_PREMUL");
    s.ds.blend = RD_BLEND_LERP_FIX;
    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                         dp);
    CHECK(dp[0].fix == 0x80 && !(dp[0].flags & ICO_DF_PREMUL), "LERP_FIX with FIX 0xFF clamps");
    s.ds.blend = RD_BLEND_LERP_AS_ALT;
    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                         dp);
    CHECK(dp[0].key.gs.blend == RD_BLEND_LERP_AS, "mode 7 shares mode 4's pipeline");
    s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                         dp);
    CHECK(dp[0].key.gs.ztst == RD_ZTST_ALWAYS && dp[0].key.gs.zwrite == RD_ZWRITE_OFF,
          "no depth attachment: Z test and write normalised off");
}

/* The alpha test merged where it decides nothing (rd_pipeline.c
 * rd__plan_screen_draw_ex): Z write on/off x AFAIL 0..3 x ATE x the alpha
 * channel's mask, ATST GREATER with a depth attachment.  One draw without
 * the test where Z write is off and a failing fragment writes what a
 * passing one writes (FB_ONLY; RGB_ONLY with alpha masked), the pass and
 * fail pair otherwise; the merged draw is the plan of the same state
 * without the test (key, flags, FIX, AREF), DATE and FBA kept. */
static void testAlphaMerge(void)
{
    for (int zw = 0; zw < 2; zw++) {
        for (int afail = 0; afail < 4; afail++) {
            for (int ate = 0; ate < 2; ate++) {
                for (int am = 0; am < 2; am++) {
                    RdStateBlock s;
                    RdDrawPass dp[2], ref[2];
                    rd__reset_state_block(&s);
                    /* ATST GREATER, AREF 0x40, ZTE, Z GEQUAL, DATE (DATM 1) */
                    s.ds.test = rd_test_from_gs(0x5C40Cu | (uint64_t)ate | (uint64_t)afail << 12);
                    s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                    s.ds.colorMask = am ? 0x7 : 0xF;
                    s.ds.fba = 1;
                    s.ds.abe = 1;
                    s.ds.blend = RD_BLEND_LERP_AS;
                    const int n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD,
                                                       RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, dp);
                    RdStateBlock off = s;
                    off.ds.test.ate = 0;
                    CHECK(rd__plan_screen_draw(&off, RD_PRIM_TRIANGLES, RD_SPACE_WORLD,
                                               RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, ref) == 1,
                          "alpha merge: the plan without the test is one draw");
                    /* the merge adds no pipeline key: every key of the
                     * merged plan is a key of the split's (the merged draw
                     * is the passing draw's key, which the failing one
                     * shares where the merge applies), so the enumerated
                     * sets are the same either way */
                    {
                        RdDrawPass sp[2];
                        rd__set_alpha_merge(false);
                        const int ns =
                            rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD,
                                                 RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, sp);
                        rd__set_alpha_merge(true);
                        for (int i = 0; i < n; i++) {
                            int found = 0;
                            for (int j = 0; j < ns; j++) {
                                found |= rd__pipe_key_equal(&dp[i].key, &sp[j].key);
                            }
                            CHECK(found,
                                  "alpha merge: zwrite %d afail %d ate %d alpha %s: draw %d's "
                                  "key is not a key of the split plan",
                                  zw, afail, ate, am ? "masked" : "written", i);
                        }
                    }
                    int want;
                    const int merged =
                        ate && !zw &&
                        (afail == RD_AFAIL_FB_ONLY || (afail == RD_AFAIL_RGB_ONLY && am));
                    if (!ate || afail == RD_AFAIL_KEEP || merged) {
                        want = 1;
                    } else if (afail == RD_AFAIL_ZB_ONLY && !zw) {
                        want = 1; /* a failing fragment writes nothing: the passing draw alone */
                    } else {
                        want = 2;
                    }
                    CHECK(n == want,
                          "alpha merge: zwrite %d afail %d ate %d alpha %s: %d draws, "
                          "expected %d",
                          zw, afail, ate, am ? "masked" : "written", n, want);
                    if (n != want) {
                        continue;
                    }
                    if (merged || !ate) {
                        CHECK(dp[0].modeZ == 0 && rd__pipe_key_equal(&dp[0].key, &ref[0].key) &&
                                  dp[0].flags == ref[0].flags && dp[0].fix == ref[0].fix &&
                                  dp[0].dstWant == ref[0].dstWant &&
                                  (dp[0].flags & (ICO_DF_DATE | ICO_DF_DATM | ICO_DF_FBA)) ==
                                      (ICO_DF_DATE | ICO_DF_DATM | ICO_DF_FBA),
                              "alpha merge: zwrite %d afail %d ate %d alpha %s: the draw "
                              "without the test, DATE and FBA kept",
                              zw, afail, ate, am ? "masked" : "written");
                    } else if (want == 2) {
                        CHECK(dp[0].modeZ == (RD_ATST_GREATER | 1u << 8 | 1u << 16) &&
                                  dp[1].modeZ == (RD_ATST_GREATER | 1u << 8 | 2u << 16),
                              "alpha merge: zwrite %d afail %d alpha %s: the pass, then the fail",
                              zw, afail, am ? "masked" : "written");
                    }
                }
            }
        }
    }
    /* the game's FB_ONLY state (TEST 0x5140D) with Z write off, with and
     * without a depth target, every enumerated blend: the merged draw's key
     * is in the screen set (rd__enumerate_reachable_screen) */
    {
        static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
        const uint32_t nk = rd__enumerate_reachable_screen(keys, RD_PIPELINE_REACHABLE_MAX);
        static const int kBlends[] = {-1, 0, 1, 2, 4, 5, 6, 7};
        for (int dz = 0; dz < 2; dz++) {
            for (size_t b = 0; b < sizeof(kBlends) / sizeof(kBlends[0]); b++) {
                RdStateBlock s;
                RdDrawPass dp[2];
                rd__reset_state_block(&s);
                s.ds.test = rd_test_from_gs(RD_TEST_AT_GT64_FBONLY);
                s.ds.zwrite = RD_ZWRITE_OFF;
                s.ds.abe = kBlends[b] >= 0;
                s.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
                const int n =
                    rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                         dz ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN, dp);
                int found = 0;
                for (uint32_t j = 0; j < nk && j < RD_PIPELINE_REACHABLE_MAX && n == 1; j++) {
                    found |= rd__pipe_key_equal(&dp[0].key, &keys[j]);
                }
                CHECK(n == 1 && found,
                      "alpha merge: FB_ONLY without Z write (depth %d, blend %d): %d draws, "
                      "enumerated %d",
                      dz, kBlends[b], n, found);
            }
        }
    }
    /* the toggle: off, the split (two draws for FB_ONLY without Z
     * write); ATST NEVER is one draw either way */
    RdStateBlock s;
    RdDrawPass dp[2];
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(0x5140Du); /* ATE GREATER 0x40, FB_ONLY, Z GEQUAL */
    s.ds.zwrite = RD_ZWRITE_OFF;
    rd__set_alpha_merge(false);
    int n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                 RHI_FMT_D32F_S8, dp);
    CHECK(n == 2, "alpha merge off: FB_ONLY without Z write splits (%d draws)", n);
    rd__set_alpha_merge(true);
    n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].modeZ == 0, "alpha merge on: FB_ONLY without Z write, one draw");
    s.ds.test.atst = RD_ATST_NEVER;
    n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].modeZ == 0, "alpha merge: ATST NEVER, FB_ONLY without Z write: one draw");
    /* no depth attachment: Z write is off whatever the state says */
    s.ds.test.atst = RD_ATST_GREATER;
    s.ds.zwrite = RD_ZWRITE_ON;
    n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
    CHECK(n == 1 && dp[0].modeZ == 0, "alpha merge: no depth attachment, FB_ONLY: one draw");
}

/* A dump written before a fixed target was appended: version 5 (before
   RD_TARGET_FEED_HELD) had 16 fixed targets, so its first temporary slot
   has low half 17, FEED_HELD's handle now; versions 6 and 7 (before
   RD_TARGET_DISPLAY_HELD) had 17, so theirs is 18, DISPLAY_HELD's.
   The dump at path with its temp's handle moved to that slot and its
   version set loads with the temp recreated and remapped, not read as the
   fixed target. */
static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* n bytes at o taken out of buf (*size bytes long) */
static void cutBytes(uint8_t *buf, long *size, size_t o, size_t n)
{
    memmove(buf + o, buf + o + n, (size_t)*size - o - n);
    *size -= (long)n;
}

/* The version 10 frame fields' place: after the version word and the three
 * sizes (8 + 16 bytes) and gsW, gsH; aspect, wExact and cullW, 12 bytes. */
#define V10_FIELDS_AT (8 + 4 * 4 + 2 * 4)

/* A dump of this version in buf rewritten in place as a version 8 one:
 * without the version 10 frame fields, the state blocks without tex1Lod and
 * every image without its level word (and GS levels).  The version word is
 * left to the caller. */
static void dumpTo8(uint8_t *buf, long *size)
{
    cutBytes(buf, size, V10_FIELDS_AT, 12);
    const size_t st0 = 8 + 4 * 4 + 5 * 4 + sizeof(RdCamera);
    const size_t sb = sizeof(RdStateBlock), sb8 = RD_STATE_BLOCK_V8_SIZE;
    const uint32_t sz8 = RD_STATE_BLOCK_V8_SIZE;
    memcpy(buf + 16, &sz8, 4);
    cutBytes(buf, size, st0 + 2 * sb - (sb - sb8), sb - sb8); /* the end state's */
    cutBytes(buf, size, st0 + sb - (sb - sb8), sb - sb8);     /* the start state's */
    size_t o = st0 + 2 * sb8;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        o += 4 + (size_t)rd32(buf + o) * sizeof(RdCmd);
    }
    o += 4 + rd32(buf + o); /* the payload */
    const uint32_t nt = rd32(buf + o);
    o += 4;
    for (uint32_t i = 0; i < nt; i++) {
        const uint32_t kind = rd32(buf + o + 4), w = rd32(buf + o + 16), h = rd32(buf + o + 20),
                       view = rd32(buf + o + 28);
        o += 32;
        if (kind == RD_TEXKIND_IMAGE) {
            o += (size_t)w * h * rd__texel_bytes((uint8_t)(view & 0xFFu));
            const uint32_t nl = rd32(buf + o);
            cutBytes(buf, size, o, 4 + (nl > 1 ? rd__gs_chain_bytes(w, h, nl) : 0));
        }
    }
}

static void testDumpOldTemps(const RdFrame *f, const char *path, const char *dir, uint32_t ver,
                             RdTargetId fixed)
{
    const uint32_t slot = rd_target(fixed).id;
    uint32_t tid = 0;
    for (uint32_t i = 0; i < f->lists[7].count && !tid; i++) {
        if (f->lists[7].cmds[i].type == RDC_TARGET) {
            tid = f->lists[7].cmds[i].u[0];
        }
    }
    CHECK(slot == (ver <= 5u ? 17u : 18u) && (tid & 0xFFFF) > slot,
          "v%u: the fixed target's handle %u, the temp's slot past it (%#x)", ver, slot, tid);
    FILE *fp = fopen(path, "rb");
    long size = 0;
    uint8_t *buf = NULL;
    if (fp && fseek(fp, 0, SEEK_END) == 0 && (size = ftell(fp)) > 0 &&
        fseek(fp, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)size);
        if (buf && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
            free(buf);
            buf = NULL;
        }
    }
    if (fp) {
        fclose(fp);
    }
    if (!buf || !tid) {
        CHECK(0, "v%u: dump read back", ver);
        free(buf);
        return;
    }
    dumpTo8(buf, &size);
    /* every field holding the temp's handle (the commands, the state
       blocks, its view's texture header, the temp section) */
    const uint32_t old = (tid & 0xFFFF0000u) | slot;
    int moved = 0;
    for (long o = 0; o + 4 <= size; o += 4) {
        uint32_t v;
        memcpy(&v, buf + o, 4);
        if (v == tid) {
            memcpy(buf + o, &old, 4);
            moved++;
        }
    }
    memcpy(buf + 8, &ver, 4);
    char pathOld[1100];
    snprintf(pathOld, sizeof(pathOld), "%s/rd_state_test_v%u.rddump", dir, ver);
    fp = fopen(pathOld, "wb");
    CHECK(moved >= 4 && fp && fwrite(buf, 1, (size_t)size, fp) == (size_t)size,
          "v%u dump written (%d handles moved)", ver, moved);
    if (fp) {
        fclose(fp);
    }
    free(buf);
    RdFrame h;
    if (!rd__load_frame(pathOld, &h)) {
        CHECK(0, "version %u dump loaded", ver);
        remove(pathOld);
        return;
    }
    int targets = 0, views = 0;
    for (uint32_t i = 0; i < h.lists[7].count; i++) {
        const RdCmd *c = &h.lists[7].cmds[i];
        if (c->type == RDC_TARGET) {
            const RdTargetRec *t = rd__target_rec(c->u[0]);
            CHECK(c->u[0] != old && (c->u[0] & 0xFFFF) > RD_TARGET_COUNT && t && t->w == 64 &&
                      t->h == 32 && t->withDepth,
                  "version %u: the temp at slot %u recreated, not the fixed target (%#x)", ver,
                  slot, c->u[0]);
            targets++;
        } else if (c->type == RDC_TEXTURE) {
            const RdTexRec *tx = rd__tex_rec(c->u[0]);
            if (tx && tx->kind == RD_TEXKIND_TARGET) {
                CHECK(tx->target != old && (tx->target & 0xFFFF) > RD_TARGET_COUNT,
                      "version %u: the temp's view on the new temp (%#x)", ver, tx->target);
                views++;
            }
        }
    }
    CHECK(targets == 1 && views == 1, "version %u: one target, one view (%d, %d)", ver, targets,
          views);
    rd__frame_free(&h);
    remove(pathOld);
}

static void testDump(const char *dir)
{
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < (int)sizeof(px); i++) {
        px[i] = (uint8_t)(i * 7 + 1);
    }
    RdTex a = rd_create_texture(4, 4, px, RD_TEXA_7F_81_AEM, "a");
    RdTex b = rd_create_texture_src(2, 8, px, RD_TEXSRC_RGB24, "b");
    /* GS levels on a: 2x2 and 1x1 (rdtex_store_levels), and a TEX1 that
       mipmaps (MXL 2, LINEAR_MIPMAP_LINEAR, L 1, K -100) */
    uint8_t chain[2 * 2 * 4 + 4];
    for (int i = 0; i < (int)sizeof(chain); i++) {
        chain[i] = (uint8_t)(200 - i);
    }
    rd__tex_set_levels(a, chain, 3);
    const uint64_t tex1 =
        (2ull << 2) | (1ull << 5) | (5ull << 6) | (1ull << 19) | ((uint64_t)(-100 & 0xFFF) << 32);
    rd_begin_frame();
    RdTarget tt = rd_temp_target(64, 32, 1, 0);
    rd_select_list(7);
    rd_set_target(tt, tt, 64, 32, 0);
    rd_sampler_tex1(tex1);
    rd_texture(a, RD_TEXFN_DECAL, RD_TCC_RGB);
    draw(1);
    rd_texture(b, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_texture(rd_target_texture(tt, RD_VIEW_RGB24_TA0), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    draw(2);
    rd_aa1(1); /* PRIM.AA1 on */
    draw(3);
    rd_aa1(0);
    draw(4);
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    char path[1024];
    snprintf(path, sizeof(path), "%s/rd_state_test.rddump", dir);
    CHECK(rd__dump_frame(f, path), "dump written");
    RdFrame g;
    if (!rd__load_frame(path, &g)) {
        CHECK(0, "dump loaded");
        return;
    }
    CHECK(g.number == f->number && g.payloadSize == f->payloadSize &&
              memcmp(g.payload, f->payload, f->payloadSize) == 0,
          "payload round trip");
    int texSeen = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        CHECK(g.lists[l].count == f->lists[l].count, "list %d count", l);
        for (uint32_t i = 0; i < g.lists[l].count && i < f->lists[l].count; i++) {
            const RdCmd *x = &f->lists[l].cmds[i], *y = &g.lists[l].cmds[i];
            if (x->type == RDC_TEXTURE) {
                RdTexRec *tx = rd__tex_rec(x->u[0]), *ty = rd__tex_rec(y->u[0]);
                CHECK(tx && ty && x->u[0] != y->u[0] && tx->w == ty->w && tx->h == ty->h &&
                          tx->src == ty->src && tx->kind == ty->kind,
                      "texture remapped to an equal copy");
                if (tx && ty && tx->kind == RD_TEXKIND_IMAGE) {
                    CHECK(memcmp(tx->pixels, ty->pixels, (size_t)tx->w * tx->h * 4) == 0 &&
                              tx->bakedTexa == ty->bakedTexa,
                          "texture pixels round trip");
                    CHECK(tx->gsLevels == ty->gsLevels &&
                              (tx->gsLevels < 2 ||
                               (ty->gsChain &&
                                memcmp(tx->gsChain, ty->gsChain,
                                       rd__gs_chain_bytes(tx->w, tx->h, tx->gsLevels)) == 0)),
                          "GS levels round trip (%u, %u)", tx->gsLevels, ty->gsLevels);
                }
                texSeen++;
            } else if (x->type == RDC_TARGET) {
                RdTargetRec *a1 = rd__target_rec(y->u[0]);
                CHECK(y->u[0] != x->u[0] && y->u[1] == y->u[0] && a1 && a1->w == 64 &&
                          a1->h == 32 && a1->withDepth,
                      "temp target recreated and remapped");
            } else {
                CHECK(memcmp(x, y, sizeof(*x)) == 0, "command %d/%u round trip", l, i);
            }
        }
    }
    CHECK(texSeen == 3, "three texture binds compared, got %d", texSeen);
    CHECK(rd__tex_rec(a.id) && rd__tex_rec(a.id)->gsLevels == 3, "a has its 3 GS levels");
    {
        /* TEX1's mipmap fields: in the FILTER command and the state of every draw after it */
        const uint32_t want = rd_tex1_lod(tex1);
        CHECK(RD_TEX1_MMIN(want) == 5 && RD_TEX1_MXL(want) == 2 && RD_TEX1_LCM(want) == 0 &&
                  RD_TEX1_L(want) == 1 && RD_TEX1_K(want) == -100,
              "rd_tex1_lod packs MMIN 5 MXL 2 L 1 K -100 (%#x)", want);
        Seen sl;
        walk(&g, &sl);
        const RdStateBlock *s1 = stateFor(&sl, 1);
        CHECK(s1 && s1->tex1Lod == want && g.endState.tex1Lod == want,
              "tex1Lod at draw 1 and at the end after the round trip");
    }
    /* PRIM.AA1: the bit at each draw after the round trip */
    Seen seen;
    walk(&g, &seen);
    const RdStateBlock *s2 = stateFor(&seen, 2), *s3 = stateFor(&seen, 3), *s4 = stateFor(&seen, 4);
    CHECK(s2 && s3 && s4 && s2->aa1 == 0 && s3->aa1 == 1 && s4->aa1 == 0,
          "AA1 off, on, off at draws 2, 3, 4 after the round trip");
    CHECK(g.endState.aa1 == f->endState.aa1 && g.startState.aa1 == f->startState.aa1,
          "state blocks' aa1 round trip");
    rd__frame_free(&g);

    /* a version 3 dump: the same file with the version and state size of
       version 3 and the state blocks without their trailing aa1, read with
       AA1 off (every command but the two RDC_AA1, which version 3 lacks,
       left in place: the loader takes the commands as they are) */
    FILE *fp = fopen(path, "rb");
    long size = 0;
    uint8_t *buf = NULL;
    if (fp && fseek(fp, 0, SEEK_END) == 0 && (size = ftell(fp)) > 0 &&
        fseek(fp, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)size);
        if (buf && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
            free(buf);
            buf = NULL;
        }
    }
    if (fp) {
        fclose(fp);
    }
    CHECK(buf != NULL, "dump read back");
    if (buf) {
        const size_t st0 = 8 + 4 * 4 + 5 * 4 + sizeof(RdCamera);
        const size_t sb8 = RD_STATE_BLOCK_V8_SIZE;
        dumpTo8(buf, &size);
        uint32_t v3 = 3, sz3 = RD_STATE_BLOCK_V3_SIZE;
        memcpy(buf + 8, &v3, 4);
        memcpy(buf + 16, &sz3, 4);
        /* the end state's aa1, then the start state's */
        cutBytes(buf, &size, st0 + 2 * sb8 - 4, 4);
        cutBytes(buf, &size, st0 + sb8 - 4, 4);
        char path3[1100];
        snprintf(path3, sizeof(path3), "%s/rd_state_test_v3.rddump", dir);
        fp = fopen(path3, "wb");
        CHECK(fp && fwrite(buf, 1, (size_t)size, fp) == (size_t)size, "v3 dump written");
        if (fp) {
            fclose(fp);
        }
        free(buf);
        RdFrame h;
        if (rd__load_frame(path3, &h)) {
            CHECK(h.startState.aa1 == 0 && h.endState.aa1 == 0 &&
                      memcmp(&h.startState, &f->startState, RD_STATE_BLOCK_V3_SIZE) == 0 &&
                      h.payloadSize == f->payloadSize,
                  "version 3 dump: the state blocks without aa1, AA1 off");
            rd__frame_free(&h);
        } else {
            CHECK(0, "version 3 dump loaded");
        }
        remove(path3);
    }
    testDumpOldTemps(f, path, dir, 5u, RD_TARGET_FEED_HELD);
    testDumpOldTemps(f, path, dir, 7u, RD_TARGET_DISPLAY_HELD);
    remove(path);
}

/* the whole file at path in a malloc block (NULL when unreadable) */
static uint8_t *slurp(const char *path, long *size)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *buf = NULL;

    if (fp && fseek(fp, 0, SEEK_END) == 0 && (*size = ftell(fp)) > 0 &&
        fseek(fp, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)*size);
        if (buf && fread(buf, 1, (size_t)*size, fp) != (size_t)*size) {
            free(buf);
            buf = NULL;
        }
    }
    if (fp) {
        fclose(fp);
    }
    return buf;
}

static int spit(const char *path, const uint8_t *buf, long size)
{
    FILE *fp = fopen(path, "wb");
    const int ok = fp && fwrite(buf, 1, (size_t)size, fp) == (size_t)size;

    if (fp) {
        fclose(fp);
    }
    return ok;
}

/* Water reflections: a block rd_block_target made is marked
 * (RdTargetRec.block), a plain temporary target is not, and the mark goes
 * through a dump in the keep word's bit 1 with keepAcross unchanged, so a
 * replay with --water-sharp scales the same targets the game did */
static void testDumpBlock(const char *dir)
{
    rd_begin_frame();
    const RdTarget blk = rd_block_target(0x2800u, 256u, 256u, 1);
    const RdTarget tmp = rd_temp_target(256, 256, 1, 0);
    const RdTargetRec *rb = rd__target_rec(blk.id), *rt = rd__target_rec(tmp.id);
    CHECK(rb && rt && rb->block == 1 && rt->block == 0 && rb->withDepth == 1,
          "block: rd_block_target marks its target, rd_temp_target does not");
    CHECK(rd_block_target(0x2800u, 256u, 256u, 1).id == blk.id,
          "block: the same block for the rest of the frame");
    rd_select_list(4);
    rd_set_target(blk, blk, 256, 256, 0);
    draw(1);
    rd_set_target(tmp, tmp, 256, 256, 0);
    draw(2);
    rd_end_frame(0);
    char path[1100];
    snprintf(path, sizeof(path), "%s/rd_state_test_block.rddump", dir);
    CHECK(rd__dump_frame(rd__last_frame(), path), "block: dump written");
    RdFrame g;
    if (!rd__load_frame(path, &g)) {
        CHECK(0, "block: dump loaded");
        return;
    }
    int seen = 0;
    for (uint32_t i = 0; i < g.lists[4].count; i++) {
        const RdCmd *c = &g.lists[4].cmds[i];
        if (c->type != RDC_TARGET) {
            continue;
        }
        const RdTargetRec *t = rd__target_rec(c->u[0]);
        CHECK(t && t->block == (seen == 0) && t->keepAcross == 0 && t->withDepth == 1 &&
                  t->w == 256u && t->h == 256u,
              "block: target %d of the loaded dump %s", seen,
              seen == 0 ? "is a block" : "is not a block");
        seen++;
    }
    CHECK(seen == 2, "block: two targets bound in the loaded list (%d)", seen);
    rd__frame_free(&g);
    remove(path);
}

/* Dump version 10: the native frame after gsH (aspect, wExact, cullW) round
 * trips; rd__dump_peek reads it without a load; a version 9 dump (the same
 * file less the three fields and with the version word 9) loads and peeks as
 * the 4:3 frame (aspect 4/3, wExact 512, cullW 512) and leaves its content
 * alone; a header the renderer cannot size (an aspect under 4:3, a visible
 * width past the native one, a cull width that is not the frame's) is
 * refused. */
static void testDumpV10(const char *dir)
{
    CHECK(RD_DUMP_VERSION >= 10u, "the dump version is 10 or later (%u)", RD_DUMP_VERSION);
    RdFrameHead h;
    memset(&h, 0, sizeof(h));
    h.gsW = 1376; /* 32:9: 16 * ceil(32 * k), k = 32/9 * 3/4 */
    h.gsH = 448;
    h.aspect = 32.0f / 9.0f;
    h.wExact = 512.0f * (32.0f / 9.0f * 0.75f);
    rd_begin_frame();
    rd_frame_head(&h);
    rd_select_list(7);
    draw(1);
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    CHECK(f->aspect == h.aspect && f->wExact == h.wExact && f->cullW == 1376u,
          "rd_frame_head sets the frame's native fields (%g, %g, %u)", (double)f->aspect,
          (double)f->wExact, f->cullW);
    char path[1100];
    snprintf(path, sizeof(path), "%s/rd_state_test_v10.rddump", dir);
    CHECK(rd__dump_frame(f, path), "v10 dump written");
    RdDumpInfo info;
    CHECK(rd__dump_peek(path, &info) && info.version == RD_DUMP_VERSION && info.gsW == 512u &&
              info.gsH == 512u && info.aspect == h.aspect && info.wExact == h.wExact &&
              info.cullW == 1376u,
          "v10 header peeked");
    RdFrame g;
    if (rd__load_frame(path, &g)) {
        CHECK(g.gsW == 512u && g.aspect == h.aspect && g.wExact == h.wExact && g.cullW == 1376u &&
                  g.number == f->number && g.lists[7].count == f->lists[7].count,
              "v10 dump round trip (%u, %g, %g, %u)", g.gsW, (double)g.aspect, (double)g.wExact,
              g.cullW);
        rd__frame_free(&g);
    } else {
        CHECK(0, "v10 dump loaded");
    }
    long size = 0;
    uint8_t *buf = slurp(path, &size);
    CHECK(buf != NULL, "v10 dump read back");
    if (buf) {
        /* the same frame as version 9 */
        const long size10 = size;
        char path9[1100];
        snprintf(path9, sizeof(path9), "%s/rd_state_test_v9.rddump", dir);
        uint8_t *b9 = malloc((size_t)size);
        memcpy(b9, buf, (size_t)size);
        cutBytes(b9, &size, V10_FIELDS_AT, 12);
        const uint32_t v9 = 9;
        memcpy(b9 + 8, &v9, 4);
        CHECK(size == size10 - 12 && spit(path9, b9, size), "v9 dump written");
        CHECK(rd__dump_peek(path9, &info) && info.version == 9u && info.aspect == 4.0f / 3.0f &&
                  info.wExact == 512.0f && info.cullW == 512u,
              "v9 header peeked as the 4:3 frame");
        if (rd__load_frame(path9, &g)) {
            CHECK(g.aspect == 4.0f / 3.0f && g.wExact == 512.0f && g.cullW == 512u &&
                      g.number == f->number && g.lists[7].count == f->lists[7].count &&
                      g.payloadSize == f->payloadSize,
                  "v9 dump loads with the 4:3 defaults (%g, %g, %u)", (double)g.aspect,
                  (double)g.wExact, g.cullW);
            rd__frame_free(&g);
        } else {
            CHECK(0, "v9 dump loaded");
        }
        remove(path9);
        free(b9);
        /* refused: an aspect under 4:3, a cull width other than gsW, a
           visible width past the native one */
        const float lowAspect = 1.0f, wide = 2000.0f;
        const uint32_t badCull = 1136;
        char pathBad[1100];
        snprintf(pathBad, sizeof(pathBad), "%s/rd_state_test_bad.rddump", dir);
        for (int k = 0; k < 3; k++) {
            uint8_t *bb = malloc((size_t)size10);
            memcpy(bb, buf, (size_t)size10);
            memcpy(bb + V10_FIELDS_AT +
                       (k == 0   ? 0
                        : k == 1 ? 8
                                 : 4),
                   k == 0   ? (const void *)&lowAspect
                   : k == 1 ? (const void *)&badCull
                            : (const void *)&wide,
                   4);
            CHECK(spit(pathBad, bb, size10), "bad v10 dump %d written", k);
            CHECK(!rd__dump_peek(pathBad, &info) && !rd__load_frame(pathBad, &g),
                  "bad v10 header %d refused", k);
            free(bb);
        }
        remove(pathBad);
        free(buf);
    }
    remove(path);
}

/* A version 11 dump's sheets (rd_dump.c sheetFromHalo): written now, at 1x
 * the coverage alone (w x h), at 2x w x 2h with the coverage over
 * rd_sheet_rim's rim, bakedTexa bits 16..23 clear, and they load back the
 * same.  The same dump rewritten by hand as one with a halo made on the CPU
 * (each sheet w x 2h, its coverage over a halo, bits 16..23 a gain of 74)
 * loads as the image made again from the coverage: 1x the coverage alone,
 * 2x the coverage over rd_sheet_rim's rim (the dumped halo dropped), the
 * scale and the style kept. */
static void testDumpSheetV11(const char *dir)
{
    enum { W = 48, H = 20 };

    static uint8_t cov[W * H], img[W * H * 2];
    memset(cov, 0, sizeof(cov));
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const int a = x >= 3 && x < 18 && y >= 2 && y < 8;
            const int b = x >= 25 && x < 44 && y >= 2 && y < 8;
            const int c = x >= 3 && x < 30 && y >= 13 && y < 18;
            if (a || b || c) {
                cov[y * W + x] = (uint8_t)(((x * 37 + y * 11) & 0xFF) | 0x40);
            }
        }
    }
    static uint8_t rim2[W * H];
    memset(rim2, 0, sizeof(rim2));
    rd_sheet_rim(cov, W, H, 2, 0, 0, W, H, rim2);
    memcpy(img, cov, sizeof(cov));
    memcpy(img + sizeof(cov), rim2, sizeof(rim2));
    RdTex sh[2];
    for (int k = 0; k < 2; k++) {
        const RdSheetStyle st = {1, 24, 0xFF, 1, 0, (uint8_t)(k + 1)};
        sh[k] = rd_create_texture_sheet(W, k ? 2 * H : H, img, &st, k ? "sheet 2x" : "sheet 1x");
    }
    rd_begin_frame();
    rd_select_list(7);
    for (int k = 0; k < 2; k++) {
        rd_texture(sh[k], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        draw((uint32_t)k + 1u);
    }
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    char path[1100], pathHalo[1100];
    snprintf(path, sizeof(path), "%s/rd_state_test_sheet.rddump", dir);
    snprintf(pathHalo, sizeof(pathHalo), "%s/rd_state_test_sheet_halo.rddump", dir);
    CHECK(rd__dump_frame(f, path), "sheet dump written");
    long size = 0;
    uint8_t *buf = slurp(path, &size);
    /* the rewritten dump: at most one more W x H block per sheet */
    uint8_t *out = buf ? malloc((size_t)size + 2u * sizeof(cov)) : NULL;
    if (!buf || !out) {
        CHECK(0, "sheet dump read back");
        free(buf);
        free(out);
        remove(path);
        return;
    }
    /* the texture records: after the header (with the version 10 frame
       fields), the two state blocks, the lists and the payload */
    size_t o = V10_FIELDS_AT + 12 + 3 * 4 + sizeof(RdCamera) + 2 * sizeof(RdStateBlock);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        o += 4 + (size_t)rd32(buf + o) * sizeof(RdCmd);
    }
    o += 4 + rd32(buf + o);
    const uint32_t nt = rd32(buf + o);
    o += 4;
    memcpy(out, buf, o);
    size_t q = o;
    int sheets = 0, clear = 1;
    for (uint32_t i = 0; i < nt; i++) {
        const uint32_t kind = rd32(buf + o + 4), w = rd32(buf + o + 16), h = rd32(buf + o + 20);
        const uint32_t texa = rd32(buf + o + 12), view = rd32(buf + o + 28);
        const uint8_t fmt = (uint8_t)(view & 0xFFu);
        size_t body = 0;
        if (kind == RD_TEXKIND_IMAGE) {
            body = (size_t)w * h * rd__texel_bytes(fmt);
            const uint32_t nl = rd32(buf + o + 32 + body);
            body += 4 + (nl > 1 ? rd__gs_chain_bytes(w, h, nl) : 0);
        }
        const int up = (int)((texa >> 8) & 0xFFu);
        if (kind == RD_TEXKIND_IMAGE && fmt == RD_TEXEL_SHEET && w == W &&
            h == (up ? 2u * H : (uint32_t)H)) {
            clear &= (texa >> 16) == 0u;
            /* the record as a halo dump has it: w x 2h, a gain, the
               coverage over a halo the load drops */
            const uint32_t texaHalo = (texa & 0xFFFFu) | 74u << 16, h2 = 2u * H;
            memcpy(out + q, buf + o, 32);
            memcpy(out + q + 12, &texaHalo, 4);
            memcpy(out + q + 20, &h2, 4);
            memcpy(out + q + 32, cov, sizeof(cov));
            memset(out + q + 32 + sizeof(cov), 0x55, sizeof(cov));
            memcpy(out + q + 32 + 2 * sizeof(cov), buf + o + 32 + (size_t)w * h, 4); /* 1 level */
            q += 32 + 2 * sizeof(cov) + 4;
            sheets++;
        } else {
            memcpy(out + q, buf + o, 32 + body);
            q += 32 + body;
        }
        o += 32 + body;
    }
    memcpy(out + q, buf + o, (size_t)size - o);
    q += (size_t)size - o;
    CHECK(sheets == 2 && clear, "v11 sheets written with bakedTexa bits 16..23 clear (%d sheets)",
          sheets);
    CHECK(spit(pathHalo, out, (long)q), "v11 halo sheet dump written");
    free(buf);
    free(out);
    for (int pass = 0; pass < 2; pass++) {
        const char *pp = pass ? pathHalo : path;
        RdFrame g;
        if (!rd__load_frame(pp, &g)) {
            CHECK(0, "v11 sheet dump %d loaded", pass);
            continue;
        }
        int seen = 0;
        for (uint32_t i = 0; i < g.lists[7].count; i++) {
            const RdCmd *c = &g.lists[7].cmds[i];
            const RdTexRec *t = c->type == RDC_TEXTURE ? rd__tex_rec(c->u[0]) : NULL;
            if (!t || t->format != RD_TEXEL_SHEET || !t->pixels) {
                continue;
            }
            const uint32_t s = t->sheetScale;
            const uint32_t hh = s > 1 ? 2u * H : (uint32_t)H;
            const int top = t->h == hh && memcmp(t->pixels, cov, sizeof(cov)) == 0;
            const int rim =
                s == 1 || (t->h == hh && memcmp(t->pixels + sizeof(cov), rim2, sizeof(rim2)) == 0);
            CHECK(t->w == W && t->h == hh && (s == 1 || s == 2) && t->sheet[0] == 64 &&
                      t->sheet[1] == 24 && t->sheet[2] == 0xFF && t->sheet[3] == 1 && top && rim,
                  "%s v11 sheet at %ux: %ux%u, coverage %s, rim %s rd_sheet_rim's",
                  pass ? "halo" : "plain", s, t->w, t->h, top ? "kept" : "changed",
                  rim ? "is" : "is not");
            seen |= 1 << (s - 1);
        }
        CHECK(seen == 3, "v11 sheets at 1x and 2x loaded (%d, pass %d)", seen, pass);
        rd__frame_free(&g);
    }
    remove(path);
    remove(pathHalo);
    for (int k = 0; k < 2; k++) {
        rd_destroy_texture(sh[k]);
    }
}

static void testAa1Plans(void)
{
    RdStateBlock s;
    RdDrawPass dp[2], base[2];
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
    s.ds.zwrite = RD_ZWRITE_ON;
    s.ds.abe = 0;
    s.ds.blend = RD_BLEND_CS_AS_ADD_CD; /* the storm: mode 5 under PRIM ABE 0 */
    s.aa1 = 1;
    int n = rd__plan_screen_draw_ex(&s, RD_PRIM_LINES, 1, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                    RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].key.gs.aa1 == 1 && dp[0].key.gs.prim == RD_PRIM_TRIANGLES &&
              dp[0].key.vs == RD_VS_SPRITE_AA1_WORLD && dp[0].key.fs == RD_FS_SPRITE_AA1 &&
              dp[0].key.gs.zwrite == RD_ZWRITE_OFF &&
              rd__blend_path(dp[0].key.gs.blend) == RD_BP_PREMUL_ADD &&
              (dp[0].flags & ICO_DF_AA1_FULL) && (dp[0].flags & ICO_DF_PREMUL),
          "AA1 line with ABE 0: AA1 shaders, triangles, mode 5 blended, no Z write");
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    n = rd__plan_screen_draw_ex(&s, RD_PRIM_TRIANGLES, 1, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].key.gs.aa1 == 1 && dp[0].key.gs.zwrite == RD_ZWRITE_ON &&
              !(dp[0].flags & ICO_DF_AA1_FULL),
          "AA1 triangles with ABE 1: Z written (the interior), As kept");
    /* aa1 0: the plan rd__plan_screen_draw makes, whatever the state's bit */
    n = rd__plan_screen_draw_ex(&s, RD_PRIM_TRIANGLES, 0, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                RHI_FMT_D32F_S8, dp);
    s.aa1 = 0;
    const int nb = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                        RHI_FMT_D32F_S8, base);
    CHECK(n == nb && memcmp(&dp[0], &base[0], sizeof(dp[0])) == 0 && dp[0].key.gs.aa1 == 0,
          "aa1 0 plans as before AA1");
}

/* the VU keys of the set by variant (vuExt 0 plain, 1 extended) */
static void countVu(const RdPipeKeyInt *keys, uint32_t n, uint32_t *plain, uint32_t *ext)
{
    *plain = *ext = 0;
    for (uint32_t i = 0; i < n && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        if (keys[i].vs >= RD_VS_VU_FIRST && keys[i].vs <= RD_VS_VU_LAST) {
            *plain += keys[i].vuExt == 0;
            *ext += keys[i].vuExt != 0;
        }
    }
}

/* whether keys[0..n) holds k */
static bool keyIn(const RdPipeKeyInt *keys, uint32_t n, const RdPipeKeyInt *k)
{
    for (uint32_t i = 0; i < n && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        if (rd__pipe_key_equal(&keys[i], k)) {
            return true;
        }
    }
    return false;
}

/* the key a VU draw of vertex shader vs (variant ext) plans under TEST
 * test, Z write zw and blend (-1: ABE off) on SCENE's formats, as
 * rd_replay_vu.c vuDraw makes it */
static RdPipeKeyInt vuKey(uint8_t vs, uint8_t ext, uint64_t test, int zw, int blend)
{
    RdStateBlock st;
    rd__reset_state_block(&st);
    st.ds.test = rd_test_from_gs(test);
    st.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
    st.ds.abe = blend >= 0;
    st.ds.blend = (uint8_t)(blend >= 0 ? blend : 0);
    RdDrawPass dp[2];
    rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                         RHI_FMT_D32F_S8, dp);
    RdPipeKeyInt k = dp[0].key;
    k.gs.program = RD_PROG_PRELIT;
    k.vs = vs;
    k.fs = RD_FS_VU;
    k.vuExt = ext;
    return k;
}

/* The VU keys of the start-up set with the vertex pre-pass
 * (rd__set_vu_prepass; docs/port/RENDER.md, "The vertex pre-pass"), in the
 * Enhanced preset (the extended variant): the triangle stage's keys
 * (vu_tri_vs, RD_PROG_PRELIT) and the particles' under the VU states and
 * TEST Z ALWAYS too, so twice two of the eleven VU vertex shaders' keys
 * without it (perVariant, as enumerated without the pre-pass), and the ten
 * pre-pass keys (rd__vu_pre_key), each once; no program entry's key but the
 * particles'; every program entry's key enumerated without the pre-pass
 * has its vu_tri_vs twin (program RD_PROG_PRELIT) in the set, and every
 * particle key its own; the phone's first-use keys (rc2, PowerVR: program 0
 * blend 4 and 12 vs vu_tri_vs fs vu_ps fmt 1/8 ext 1, under Z ALWAYS) and
 * the 2D one (program 9 blend 4, sprite_ui_vs + sprite_ps, fmt 1/8, under
 * the list head's Z GEQUAL) are in it; the whole set under
 * RD_PIPELINE_REACHABLE_MAX.  The pre-pass is off again after. */
static void testEnumerationPrepass(RdPipeKeyInt *keys, uint32_t perVariant)
{
    static RdPipeKeyInt off[RD_PIPELINE_REACHABLE_MAX];
    const bool keepPrepass = g_rd.vuPrepass;
    const uint32_t nOff = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    memcpy(off, keys, sizeof(off));
    rd__set_vu_prepass(true);
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    uint32_t tri = 0, particle = 0, entries = 0, plain, ext;
    countVu(keys, n, &plain, &ext);
    for (uint32_t i = 0; i < n && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        tri += keys[i].vs == RD_VS_VU_TRI;
        particle += keys[i].vs == RD_VS_VU_PARTICLE;
        entries += keys[i].vs >= RD_VS_VU_PRELIT && keys[i].vs <= RD_VS_VU_GRID_SPEC;
        if (keys[i].vs == RD_VS_VU_TRI) {
            CHECK(keys[i].gs.program == RD_PROG_PRELIT,
                  "pre-pass: key %u of vu_tri_vs carries "
                  "program %u",
                  i, keys[i].gs.program);
        }
    }
    uint32_t pre = 0;
    for (uint8_t v = RD_VS_VU_PRE_FIRST; v <= RD_VS_VU_PRE_LAST; v++) {
        const RdPipeKeyInt k = rd__vu_pre_key(v);
        uint32_t found = 0;
        for (uint32_t i = 0; i < n && i < RD_PIPELINE_REACHABLE_MAX; i++) {
            found += rd__pipe_key_equal(&keys[i], &k);
        }
        CHECK(found == 1, "pre-pass: the key of %s enumerated %u times", rd__vs_name(v), found);
        pre += found == 1;
        CHECK(k.gs.atst == RD_ATST_ALWAYS && k.fs == RD_FS_VU_PRE &&
                  k.colorFmt == RHI_FMT_RGBA32_UINT && k.depthFmt == RHI_FMT_UNKNOWN,
              "pre-pass: the key of %s", rd__vs_name(v));
    }
    printf("  with the vertex pre-pass: %u keys (%u without), vu_tri_vs %u, vu_particle_vs %u, "
           "program entries %u, pre-pass %u\n",
           n, nOff, tri, particle, entries, pre);
    CHECK(entries == 0, "pre-pass: %u program entries' keys enumerated", entries);
    CHECK(tri > 0 && tri == particle && (tri + particle) * 11u == perVariant * 4u,
          "pre-pass: vu_tri_vs %u and vu_particle_vs %u keys, %u for the eleven shaders without",
          tri, particle, perVariant);
    /* every VU key without the pre-pass has its twin with it */
    uint32_t twins = 0, missing = 0;
    for (uint32_t i = 0; i < nOff && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        if (off[i].vs < RD_VS_VU_FIRST || off[i].vs > RD_VS_VU_LAST) {
            continue;
        }
        RdPipeKeyInt t = off[i];
        if (t.vs != RD_VS_VU_PARTICLE) {
            t.vs = RD_VS_VU_TRI;
            t.gs.program = RD_PROG_PRELIT;
        }
        twins++;
        if (!keyIn(keys, n, &t)) {
            if (missing++ < 4) {
                char w[160];
                rd__pipe_key_text(&off[i], w, sizeof(w));
                CHECK(0, "pre-pass: no vu_tri_vs twin of %s", w);
            }
        }
    }
    printf("  every program entry's key (%u) has its twin with the pre-pass: %u missing\n", twins,
           missing);
    CHECK(twins == perVariant && missing == 0, "pre-pass: %u of %u VU keys without a twin", missing,
          twins);
    /* the phone's first-use keys: vu_tri_vs under Z ALWAYS, ABE off (blend
       12) and LERP_AS (blend 4), Z write on and off; the 2D sprite under
       the list head's Z GEQUAL */
    for (int zw = 0; zw < 2; zw++) {
        for (int b = 0; b < 2; b++) {
            const RdPipeKeyInt k =
                vuKey(RD_VS_VU_TRI, 1, RD_TEST_Z_ALWAYS, zw, b ? RD_BLEND_LERP_AS : -1);
            CHECK(k.gs.blend == (b ? 4 : 12) && k.vs == RD_VS_VU_TRI && k.fs == RD_FS_VU &&
                      k.colorFmt == RHI_FMT_RGBA8_UNORM && k.depthFmt == RHI_FMT_D32F_S8,
                  "pre-pass: the first-use key's fields");
            CHECK(keyIn(keys, n, &k), "pre-pass: vu_tri_vs blend %u under Z ALWAYS, Z write %d",
                  k.gs.blend, zw);
            const RdPipeKeyInt p =
                vuKey(RD_VS_VU_PARTICLE, 1, RD_TEST_Z_ALWAYS, zw, b ? RD_BLEND_LERP_AS : -1);
            RdPipeKeyInt pk = p;
            pk.gs.program = RD_PROG_PARTICLE;
            CHECK(keyIn(keys, n, &pk), "pre-pass: vu_particle_vs blend %u under Z ALWAYS",
                  pk.gs.blend);
        }
    }
    {
        RdStateBlock st;
        rd__reset_state_block(&st);
        st.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
        st.ds.abe = 1;
        st.ds.blend = RD_BLEND_LERP_AS;
        RdDrawPass dp[2];
        rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_D32F_S8, dp);
        CHECK(dp[0].key.gs.program == RD_PROG_SCREEN && dp[0].key.gs.blend == 4 &&
                  dp[0].key.vs == RD_VS_SPRITE_UI && dp[0].key.fs == RD_FS_SPRITE,
              "the 2D first-use key's fields");
        CHECK(keyIn(keys, n, &dp[0].key), "the 2D sprite under Z GEQUAL with depth, blend 4");
    }
    CHECK(pre == RD_VS_VU_PRE_LAST - RD_VS_VU_PRE_FIRST + 1, "pre-pass: %u pre-pass keys", pre);
    CHECK(n == nOff - perVariant + tri + particle + pre && n < RD_PIPELINE_REACHABLE_MAX,
          "pre-pass: %u keys, %u without the pre-pass", n, nOff);
    rd__set_vu_prepass(keepPrepass);
}

/* The VU keys of the start-up set (rd__enumerate_reachable_vu): out of the
 * box (the Original preset, 4:3, 1x, the original framerate) the plain
 * variants only; in a state other than out of the box (rd__vu_extended:
 * here the Enhanced preset) every VU draw takes the extended variant
 * (rd__vu_draw_ext), so the plain ones are not enumerated; with the device
 * or the settings keeping the plain ones (vuPlainOnly) the plain ones only,
 * whatever the state. */
static void testEnumerationVu(RdPipeKeyInt *keys)
{
    const RdPreset keepPreset = g_rd.settings.preset;
    const uint8_t keepInterp = g_rd.settings.interpolate;
    const bool keepPlain = g_rd.vuPlainOnly;
    uint32_t n, p0, e0, p1, e1, p2, e2;
    g_rd.vuPlainOnly = false;
    g_rd.settings.interpolate = 0;
    g_rd.settings.preset = RD_PRESET_ORIGINAL;
    const bool box = !rd__vu_extended();
    n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    countVu(keys, n, &p0, &e0);
    g_rd.settings.preset = RD_PRESET_ENHANCED;
    n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    countVu(keys, n, &p1, &e1);
    g_rd.vuPlainOnly = true;
    n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    countVu(keys, n, &p2, &e2);
    printf("  VU keys plain/extended: out of the box %u/%u, Enhanced %u/%u, Enhanced plain "
           "only %u/%u\n",
           p0, e0, p1, e1, p2, e2);
    if (box) {
        CHECK(p0 > 0 && e0 == 0, "out of the box: the plain VU keys only (%u/%u)", p0, e0);
    }
    CHECK(p1 == 0 && e1 > 0, "Enhanced: the extended VU keys only (%u/%u)", p1, e1);
    CHECK(p2 > 0 && e2 == 0, "Enhanced, plain only: the plain VU keys only (%u/%u)", p2, e2);
    CHECK(!box || (p0 == p2 && p0 == e1), "the variants' key counts (%u, %u, %u)", p0, p2, e1);
    testEnumerationPrepass(keys, p1 + e1);
    g_rd.settings.preset = keepPreset;
    g_rd.settings.interpolate = keepInterp;
    g_rd.vuPlainOnly = keepPlain;
}

static void testEnumeration(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
    uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    const uint32_t ns = rd__enumerate_reachable_screen(keys, RD_PIPELINE_REACHABLE_MAX);
    n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    printf("  reachable pipelines: %u (screen and post %u, VU programs %u)\n", n, ns, n - ns);
    CHECK(ns > 0 && ns < 400, "reachable screen and post pipelines %u must stay under 400", ns);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipeline count %u must stay under %d", n,
          RD_PIPELINE_REACHABLE_MAX);
    /* the whole set (683 keys with the triangle passes' plane keys) */
    for (uint32_t i = 0; i < n && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        for (uint32_t j = i + 1; j < n && j < RD_PIPELINE_REACHABLE_MAX; j++) {
            CHECK(!rd__pipe_key_equal(&keys[i], &keys[j]), "duplicate key %u/%u", i, j);
        }
        CHECK(keys[i].gs.atst == RD_ATST_ALWAYS && keys[i].gs.pabe == 0 && keys[i].gs.fba == 0 &&
                  keys[i].gs.afailSplit == 0 && keys[i].gs.date == 0,
              "key %u normalised", i);
    }
    /* font_sheet_ps has font_ps's keys: the overlay's two
     * blends on the two outputs and the frame's text, two blends, with and
     * without depth, colour mask F and 7 (the overlay's RGBA8 keys are the
     * frame's depthless mask F ones: 10 in all) */
    uint32_t font = 0, sheet = 0;
    for (uint32_t i = 0; i < n && i < 512; i++) {
        font += keys[i].fs == RD_FS_FONT;
        sheet += keys[i].fs == RD_FS_FONT_SHEET;
    }
    printf("  font_ps keys %u, font_sheet_ps keys %u\n", font, sheet);
    CHECK(font == 10 && sheet == 10, "font_ps keys %u, font_sheet_ps keys %u (10 each)", font,
          sheet);
    testEnumerationVu(keys);
}

/* The native frame's region scale (rd__region_scale, FrameCB g_frame.x):
 * exactly 1 at 4:3 on every target (the region path untouched), 512 / W on a
 * target of a W-wide frame's size and 1 on the others; no scissor or
 * position is scaled. */
static void testRegionScale(void)
{
    const uint32_t keepW = g_rd.gsW, keepH = g_rd.gsH;
    RdTargetRec t;
    memset(&t, 0, sizeof(t));
    g_rd.gsW = 512;
    g_rd.gsH = 448;
    t.w = 512;
    t.h = 448;
    CHECK(rd__region_scale(&t) == 1.0f, "region scale: a 4:3 frame's SCENE is exactly 1 (%g)",
          (double)rd__region_scale(&t));
    g_rd.gsW = 688; /* 16:9 (ico_frame_width_for) */
    t.w = 688;
    CHECK(rd__region_scale(&t) == 512.0f / 688.0f,
          "region scale: a 688-wide frame's target is 512 / 688 (%g)",
          (double)rd__region_scale(&t));
    t.w = 256;
    t.h = 256;
    CHECK(rd__region_scale(&t) == 1.0f, "region scale: a work buffer is 1 (%g)",
          (double)rd__region_scale(&t));
    t.w = 688;
    t.h = 224;
    CHECK(rd__region_scale(&t) == 1.0f, "region scale: DISPLAY's half height is 1 (%g)",
          (double)rd__region_scale(&t));
    CHECK(rd__region_scale(NULL) == 1.0f, "region scale: no target is 1");
    g_rd.gsW = keepW;
    g_rd.gsH = keepH;
}

/* ------------------------------------------------------- the aura filter
 * The model viewer's draw filter (rd_set_draw_filter, open
 * while the viewed object's display list runs) and the mirage's list 8.
 * The Queen's dumps hold list 8's shine materials keyed by her and two
 * GRIDs keyed by other objects (her cloth), which are not in list 1.  A GRID
 * keyed by a second object inside the open window is kept and learned (and
 * kept once the window is closed); a third object's is not; and list 8's
 * state (the AURA_WORK target with SCENE's depth, the Z write), the clear
 * (rd_clear_target and auraInspireBefore's RD_POST_AURA sprite) and the
 * mesh draw keyed by the object survive the filter. */
static const char kQueen, kCloth, kOther;

static void filterGrid(RdKey key)
{
    static float qw[3 * RD_VU_QW_GRID + 4][4];
    RdVuGridDraw d;
    memset(&d, 0, sizeof(d));
    d.qw = (const float (*)[4])qw;
    d.strips = 1;
    d.stripLen = 3;
    d.code = 20;
    rd_draw_vu_grid(&d, key);
}

static void filterWorld(RdKey key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[1].x = v[1].y = 160;
    v[0].q = v[1].q = 1.0f;
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 0, key);
}

static int countIn(const RdFrame *f, int l, uint8_t type, RdKey key, int anyKey)
{
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        const RdCmd *c = &f->lists[l].cmds[i];
        n += c->type == type &&
             (anyKey || (c->keyLo == (uint32_t)key && c->keyHi == (uint32_t)(key >> 32)));
    }
    return n;
}

static void testAuraFilter(void)
{
    const RdKey queen = RD_KEY(&kQueen, 12, 0), cloth = RD_KEY(&kCloth, 8, 0),
                other = RD_KEY(&kOther, 8, 0);
    const void *own[1] = {&kQueen};
    static const uint8_t zero[4] = {0, 0, 0, 0};
    rd_set_draw_filter(true, own, 1);
    rd_begin_frame();
    /* the viewed object's display list: the filter open */
    rd_draw_filter_open(true);
    rd_select_list(8);
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_z_write(1);
    filterWorld(queen);
    filterGrid(cloth); /* drawn by the object, keyed by another */
    rd_draw_filter_open(false);
    /* after the window: staticBlur.c's list 8, an unknown object's grid, the
       learned cloth again */
    rd_select_list(8);
    rd_clear_target(rd_target(RD_TARGET_AURA_WORK), zero, 0, 0);
    {
        RdPostParams p;
        memset(&p, 0, sizeof(p));
        p.rect[0] = p.rect[1] = (float)(0x8000 - 256 * 16);
        p.rect[2] = p.rect[3] = (float)(0x8000 + 256 * 16);
        p.scalar[2] = 1.0f;
        p.exactInt = 1;
        rd_post(RD_POST_AURA, &p);
    }
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_z_write(0);
    filterGrid(other);
    filterGrid(cloth);
    filterWorld(queen);
    const RdFrame *f = rd__rec_frame();
    const int grids = countIn(f, 8, RDC_GRID, cloth, 0), lost = countIn(f, 8, RDC_GRID, other, 0);
    const int worlds = countIn(f, 8, RDC_SCREEN, queen, 0);
    const int targets = countIn(f, 8, RDC_TARGET, 0, 1), zw = countIn(f, 8, RDC_ZWRITE, 0, 1);
    const int clears = countIn(f, 8, RDC_CLEAR, 0, 1), posts = countIn(f, 8, RDC_POST_STUB, 0, 1);
    rd_end_frame(0);
    rd_set_draw_filter(false, NULL, 0);
    CHECK(grids == 2, "aura filter: the cloth's GRID kept in the window and after it (%d of 2)",
          grids);
    CHECK(lost == 0, "aura filter: an unknown object's GRID left out (%d)", lost);
    CHECK(worlds == 2, "aura filter: the object's own draws kept (%d of 2)", worlds);
    CHECK(
        targets == 2 && zw >= 2 && clears == 1 && posts == 1, /* and the list head's default */
        "aura filter: list 8's targets %d (2), Z writes %d (2 + the head's), clear %d (1), aura sprite %d (1)",
        targets, zw, clears, posts);
}

/* The render pass ops of a synthetic frame (clear -> draw ->
 * present).  The clear is not a pass of its own: the draw's pass takes it as
 * its load op (rd__take_pending_clear, as rd_replay.c beginPass), so the
 * scene's colour and depth open with CLEAR and are stored (a zeroed
 * RhiRenderPassDesc: RHI_STORE_STORE; the depth is kept, since a keep
 * replay of the frame and the dumps read it after the frame).  A pass that
 * cannot take the clear (another target, the cleared depth absent or not
 * loaded) leaves its loads as asked. */
typedef struct PassWalk {
    RdPendingClear pend;
    int draws, taken;
    RhiLoadOp colorLoad, depthLoad;
} PassWalk;

static void onPassCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    PassWalk *w = user;
    (void)list;
    (void)index;
    if (c->type == RDC_CLEAR) {
        memset(&w->pend, 0, sizeof(w->pend));
        w->pend.target = c->u[0];
        w->pend.depth = c->b[4] != 0;
    } else if (c->type == RDC_SCREEN) {
        RhiLoadOp cl = RHI_LOAD_LOAD, dl = RHI_LOAD_LOAD;
        if (rd__take_pending_clear(&w->pend, s->color, s->depth, &cl, &dl)) {
            w->taken++;
            w->pend.target = 0;
        }
        if (w->draws++ == 0) {
            w->colorLoad = cl;
            w->depthLoad = dl;
        }
    }
}

static void testPassOps(void)
{
    static const uint8_t bg[4] = {10, 20, 30, 0x80};
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, 512, 448, 0);
    rd_clear_target(scene, bg, 1, 0);
    draw(1);
    draw(2);
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    PassWalk w;
    memset(&w, 0, sizeof(w));
    RdStateBlock st = f->startState;
    rd__walk(f, (int)f->keep, &st, onPassCmd, &w);
    CHECK(w.draws == 2 && w.taken == 1, "pass ops: %d draws, %d took the clear", w.draws, w.taken);
    CHECK(w.colorLoad == RHI_LOAD_CLEAR && w.depthLoad == RHI_LOAD_CLEAR,
          "pass ops: the first draw's pass loads colour %d, depth %d (CLEAR expected)",
          (int)w.colorLoad, (int)w.depthLoad);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    CHECK(RHI_STORE_STORE == 0 && p.color[0].store == RHI_STORE_STORE &&
              p.depth.store == RHI_STORE_STORE && p.depth.stencilStore == RHI_STORE_STORE,
          "pass ops: a zeroed pass stores colour, depth and stencil");

    /* the cases that keep their loads */
    RdPendingClear pc;
    memset(&pc, 0, sizeof(pc));
    RhiLoadOp cl = RHI_LOAD_LOAD, dl = RHI_LOAD_LOAD;
    CHECK(!rd__take_pending_clear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: nothing pending");
    pc.target = 5;
    pc.depth = 1;
    CHECK(!rd__take_pending_clear(&pc, 6, 5, &cl, &dl) && cl == RHI_LOAD_LOAD &&
              dl == RHI_LOAD_LOAD,
          "pass ops: another colour target");
    CHECK(!rd__take_pending_clear(&pc, 5, 0, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: the cleared depth not bound");
    CHECK(!rd__take_pending_clear(&pc, 5, 6, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: another depth bound");
    dl = RHI_LOAD_DONT_CARE;
    CHECK(!rd__take_pending_clear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: the depth not loaded");
    dl = RHI_LOAD_LOAD;
    cl = RHI_LOAD_CLEAR;
    CHECK(!rd__take_pending_clear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_CLEAR &&
              dl == RHI_LOAD_LOAD,
          "pass ops: a pass clearing on its own");
    /* a colour-only clear: any depth (or none) loads as asked */
    pc.depth = 0;
    cl = RHI_LOAD_LOAD;
    CHECK(rd__take_pending_clear(&pc, 5, 6, &cl, &dl) && cl == RHI_LOAD_CLEAR &&
              dl == RHI_LOAD_LOAD,
          "pass ops: a colour-only clear with another depth");
    cl = RHI_LOAD_LOAD;
    CHECK(rd__take_pending_clear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_CLEAR &&
              dl == RHI_LOAD_LOAD,
          "pass ops: a colour-only clear keeps its own depth loaded");
}

/* The stencil's ops through a frame with the shadow count (clear -> draw ->
 * shadow reset -> volumes -> resolve -> draw), walked as rd_replay.c opens
 * its passes: beginPass's loads (rd__take_pending_clear, then rd__stencil_ops
 * on the pass's depth), a pending clear or shadow reset not taken recorded
 * as its own pass before the next one (endPass), the reset opening the
 * window and the end of the resolve's pass closing it.  Outside the window
 * the stencil is not stored and loads DONT_CARE, a CLEAR staying CLEAR;
 * inside it loads and stores.  The reset is no pass of its own:
 * the volumes' pass clears the stencil as its load op, one pass fewer than
 * with the reset recorded alone (fold 0). */
#define SW_MAX 16

typedef struct StencilWalk {
    int fold;
    RdStencilWindow w;
    RdPendingClear pend;
    int open;
    uint32_t passColor, passDepth;
    int passes, resets;
    RhiLoadOp load[SW_MAX];
    RhiStoreOp store[SW_MAX];
    char what[SW_MAX]; /* C clear, D draw, R reset, V volumes, S resolve */
} StencilWalk;

static void swRecord(StencilWalk *w, char what, RhiLoadOp load, RhiStoreOp store)
{
    if (w->passes < SW_MAX) {
        w->load[w->passes] = load;
        w->store[w->passes] = store;
        w->what[w->passes] = what;
    }
    w->passes++;
}

/* endPass: the pass closes, a clear or reset still pending is recorded */
static void swEnd(StencilWalk *w)
{
    w->open = 0;
    if (w->pend.target) {
        RhiLoadOp sl;
        RhiStoreOp ss;
        rd__stencil_ops(&w->w, w->pend.depth ? w->pend.target : 0, true, RHI_LOAD_CLEAR, &sl, &ss);
        w->pend.target = 0;
        swRecord(w, 'C', sl, ss);
    }
    if (w->w.clearFor) {
        swRecord(w, 'R', RHI_LOAD_CLEAR, RHI_STORE_STORE);
        w->w.clearFor = 0;
    }
}

static int swWhole(uint32_t cid, uint32_t did)
{
    const RdTargetRec *c = rd__target_rec(cid), *d = rd__target_rec(did);
    return c && d && c->tw == d->tw && c->th == d->th;
}

/* beginPass on colour cid and depth did (0: none) */
static void swBegin(StencilWalk *w, char what, uint32_t cid, uint32_t did, RhiLoadOp colorLoad,
                    RhiLoadOp depthLoad)
{
    if (rd__take_pending_clear(&w->pend, cid, did, &colorLoad, &depthLoad)) {
        w->pend.target = 0;
    }
    RhiLoadOp sl = RHI_LOAD_LOAD;
    RhiStoreOp ss = RHI_STORE_STORE;
    if (did) {
        rd__stencil_ops(&w->w, did, swWhole(cid, did), depthLoad, &sl, &ss);
    }
    swEnd(w);
    swRecord(w, what, sl, ss);
    w->open = 1;
    w->passColor = cid;
    w->passDepth = did;
}

static void onStencilCmd(void *user, int list, uint32_t index, const RdCmd *c,
                         const RdStateBlock *s)
{
    StencilWalk *w = user;
    (void)list;
    (void)index;
    const RdTargetRec *td = rd__target_rec(s->depth);
    const uint32_t did = td && td->withDepth ? s->depth : 0;
    switch (c->type) {
    case RDC_CLEAR:
        swEnd(w);
        memset(&w->pend, 0, sizeof(w->pend));
        w->pend.target = c->u[0];
        w->pend.depth = c->b[4] != 0;
        break;
    case RDC_SCREEN:
    case RDC_SHADOW_STRIP:
        if (!w->open || w->passColor != s->color || w->passDepth != did) {
            swBegin(w, c->type == RDC_SCREEN ? 'D' : 'V', s->color, did, RHI_LOAD_LOAD,
                    RHI_LOAD_LOAD);
        }
        break;
    case RDC_SHADOW_RESET:
        swEnd(w);
        w->resets++;
        rd__stencil_open(&w->w, did);
        if (w->fold) {
            w->w.clearFor = did;
        } else {
            swRecord(w, 'R', RHI_LOAD_CLEAR, RHI_STORE_STORE);
        }
        break;
    case RDC_SHADOW_RESOLVE:
        swBegin(w, 'S', s->color, did, RHI_LOAD_CLEAR, RHI_LOAD_LOAD);
        swEnd(w);
        rd__stencil_close(&w->w, did);
        break;
    default:
        break;
    }
}

static void testStencilOps(void)
{
    static const uint8_t bg[4] = {10, 20, 30, 0x80};
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    const RdTargetRec *sr = rd__target_rec(scene.id);
    if (!sr || !sr->withDepth) {
        CHECK(0, "stencil ops: SCENE without depth");
        return;
    }
    const uint32_t gw = sr->w, gh = sr->h;
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, gw, gh, 0);
    rd_clear_target(scene, bg, 1, 0);
    draw(1);
    rd_select_list(3);
    const RdTarget cnt = rd_shadow_count_target(gw, gh);
    rd_set_target(cnt, scene, gw, gh, 0);
    rd_z_write(0);
    rd_test_gs(0x30000);
    rd_texture_off();
    rd_shadow_reset();
    rd_test_gs(0x50000);
    {
        RdScreenVtx t[3];
        const int8_t sign[1] = {1};
        memset(t, 0, sizeof(t));
        t[1].x = 64 * 16;
        t[2].y = 64 * 16;
        rd_shadow_tris(t, sign, 1, RD_KEY(4, 0, 0));
    }
    rd_shadow_resolve();
    rd_set_target(scene, scene, gw, gh, 0);
    draw(2);
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    CHECK(swWhole(cnt.id, scene.id), "stencil ops: the count target has SCENE's size");

    StencilWalk w, before;
    memset(&w, 0, sizeof(w));
    w.fold = 1;
    RdStateBlock st = f->startState;
    rd__walk(f, (int)f->keep, &st, onStencilCmd, &w);
    swEnd(&w);
    memset(&before, 0, sizeof(before));
    st = f->startState;
    rd__walk(f, (int)f->keep, &st, onStencilCmd, &before);
    swEnd(&before);

    CHECK(w.resets == 1 && w.passes == 4 && before.passes == 5 &&
              before.passes - w.passes == w.resets,
          "stencil ops: %d passes, %d with the reset as its own pass, %d resets (4, 5, 1)",
          w.passes, before.passes, w.resets);
    static const char whats[] = "DVSD";
    static const RhiLoadOp loads[] = {RHI_LOAD_CLEAR, RHI_LOAD_CLEAR, RHI_LOAD_LOAD,
                                      RHI_LOAD_DONT_CARE};
    static const RhiStoreOp stores[] = {RHI_STORE_DONT_CARE, RHI_STORE_STORE, RHI_STORE_STORE,
                                        RHI_STORE_DONT_CARE};
    for (int i = 0; i < 4 && i < w.passes; i++) {
        CHECK(w.what[i] == whats[i] && w.load[i] == loads[i] && w.store[i] == stores[i],
              "stencil ops: pass %d is %c, stencil load %d store %d (%c, %d, %d expected)", i,
              w.what[i], (int)w.load[i], (int)w.store[i], whats[i], (int)loads[i], (int)stores[i]);
    }
    /* before: the reset's own pass clears and stores, the volumes load */
    CHECK(before.passes == 5 && before.what[1] == 'R' && before.load[1] == RHI_LOAD_CLEAR &&
              before.store[1] == RHI_STORE_STORE && before.what[2] == 'V' &&
              before.load[2] == RHI_LOAD_LOAD && before.store[2] == RHI_STORE_STORE,
          "stencil ops: with the reset as its own pass, reset CLEAR/STORE then volumes LOAD/STORE");

    /* a reset with no volumes (frames without a shadow volume): the
     * resolve's pass takes it and reads the cleared stencil */
    rd_begin_frame();
    rd_select_list(3);
    const RdTarget cnt2 = rd_shadow_count_target(gw, gh);
    rd_set_target(cnt2, scene, gw, gh, 0);
    rd_shadow_reset();
    rd_shadow_resolve();
    rd_end_frame(0);
    f = rd__last_frame();
    memset(&w, 0, sizeof(w));
    w.fold = 1;
    st = f->startState;
    rd__walk(f, (int)f->keep, &st, onStencilCmd, &w);
    swEnd(&w);
    CHECK(w.passes == 1 && w.what[0] == 'S' && w.load[0] == RHI_LOAD_CLEAR &&
              w.store[0] == RHI_STORE_STORE,
          "stencil ops: reset -> resolve is one pass, %d (1), the resolve's with stencil CLEAR "
          "and STORE",
          w.passes);

    /* the rule's other cases */
    RdStencilWindow sw;
    memset(&sw, 0, sizeof(sw));
    rd__stencil_open(&sw, 7);
    sw.clearFor = 7;
    RhiLoadOp sl;
    RhiStoreOp ss;
    CHECK(!rd__stencil_ops(&sw, 7, false, RHI_LOAD_LOAD, &sl, &ss) && sw.clearFor == 7 &&
              sl == RHI_LOAD_LOAD && ss == RHI_STORE_STORE,
          "stencil ops: a pass not covering the depth leaves the reset pending, loads and stores");
    CHECK(!rd__stencil_ops(&sw, 8, true, RHI_LOAD_LOAD, &sl, &ss) && sw.clearFor == 7 &&
              sl == RHI_LOAD_DONT_CARE && ss == RHI_STORE_DONT_CARE,
          "stencil ops: another depth neither loads nor stores, the reset stays pending");
    CHECK(!rd__stencil_ops(&sw, 8, true, RHI_LOAD_CLEAR, &sl, &ss) && sl == RHI_LOAD_CLEAR &&
              ss == RHI_STORE_DONT_CARE,
          "stencil ops: a depth clear outside the window still clears the stencil");
    CHECK(rd__stencil_ops(&sw, 7, true, RHI_LOAD_CLEAR, &sl, &ss) && sw.clearFor == 0 &&
              sl == RHI_LOAD_CLEAR && ss == RHI_STORE_STORE,
          "stencil ops: the live depth's pass takes the reset");

    /* two depths' windows interleaved (reset 7, reset 9, then 7's volumes
     * and resolve, then 9's): each depth keeps its own count, loaded and
     * stored until its own resolve */
    memset(&sw, 0, sizeof(sw));
    rd__stencil_open(&sw, 7);
    rd__stencil_open(&sw, 9);
    CHECK(!rd__stencil_ops(&sw, 7, true, RHI_LOAD_LOAD, &sl, &ss) && sl == RHI_LOAD_LOAD &&
              ss == RHI_STORE_STORE,
          "stencil ops: a reset on another depth leaves the first depth's count loaded and stored");
    rd__stencil_close(&sw, 7);
    CHECK(!rd__stencil_ops(&sw, 7, true, RHI_LOAD_LOAD, &sl, &ss) && sl == RHI_LOAD_DONT_CARE &&
              ss == RHI_STORE_DONT_CARE,
          "stencil ops: the first depth's window closes at its own resolve");
    CHECK(!rd__stencil_ops(&sw, 9, true, RHI_LOAD_LOAD, &sl, &ss) && sl == RHI_LOAD_LOAD &&
              ss == RHI_STORE_STORE,
          "stencil ops: the second depth stays live after the first one's resolve");
    rd__stencil_close(&sw, 9);
    CHECK(!rd__stencil_live(&sw, 7) && !rd__stencil_live(&sw, 9) && !rd__stencil_live(&sw, 0),
          "stencil ops: both windows closed");
    /* more live depths than the window holds: every depth counts as live */
    for (uint32_t d = 1; d <= RD_STENCIL_LIVE_MAX + 1; d++) {
        rd__stencil_open(&sw, d);
    }
    CHECK(sw.spill && rd__stencil_live(&sw, 100) && !rd__stencil_live(&sw, 0),
          "stencil ops: a spilled window treats every depth as live");
}

/* The DATE snapshot's area.  rd_perf_test's list 4 (four 40 x 60 DATE
 * sprites on SCENE, each writing alpha) and a sprite larger than the target,
 * walked as rd_replay.c replays them: each DATE draw's area from
 * rd__screen_area, the retake decision from rd__date_retake with the write
 * serial bumped after every draw (each writes alpha, as there).  A sprite's
 * area is its box padded by two GS pixels, not the target; the large one
 * is the target inside the scissor; four snapshots for the four sprites
 * (rd_perf_test's dateSnapshots is unchanged), one more for the large one. */
#define DA_W 512
#define DA_H 448

typedef struct DateWalk {
    const RdFrame *f;
    int draws, takes;
    uint32_t heldFor, heldSerial, serial;
    RhiRect held, area[8];
    int got[8];
} DateWalk;

static void onDateCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    DateWalk *w = user;
    (void)list;
    (void)index;
    if (c->type != RDC_SCREEN || s->ds.test.date == RD_DATE_OFF || w->draws >= 8) {
        return;
    }
    const RdTargetRec *tc = rd__target_rec(s->color);
    const RdScreenVtx *v = (const RdScreenVtx *)(w->f->payload + c->u[0]);
    const int i = w->draws++;
    w->got[i] = tc && rd__screen_area(tc, s, v, c->u[1], 0, 0, &w->area[i]);
    RhiRect take;
    if (tc && rd__date_retake(w->heldFor, w->heldSerial, &w->held, s->color, w->serial,
                              w->got[i] ? &w->area[i] : NULL, tc->tw, tc->th, &take)) {
        w->takes++;
        w->heldFor = s->color;
        w->heldSerial = w->serial;
        w->held = take;
    }
    w->serial++; /* the sprite writes alpha */
}

static RdScreenVtx daVtx(int x, int y)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - DA_W / 2 + x) * 16;
    v.y = (2048 - DA_H / 2 + y) * 16;
    v.z = 0x2000000u;
    v.q = 1.0f;
    v.rgba[0] = v.rgba[1] = v.rgba[2] = 200;
    v.rgba[3] = 0x80;
    return v;
}

static void testDateArea(void)
{
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(4);
    rd_set_target(scene, scene, DA_W, DA_H, RD_TARGET_OFFSET);
    rd_test_gs(0x5C000); /* DATE on, Z GEQUAL (rd_perf_test's list 4) */
    for (int i = 0; i < 4; i++) {
        RdScreenVtx q[2] = {daVtx(100 + i * 50, 100), daVtx(140 + i * 50, 160)};
        rd_texture_off();
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 0, 0);
    }
    {
        RdScreenVtx q[2] = {daVtx(-50, -50), daVtx(DA_W + 100, DA_H + 100)};
        rd_screen_prims(RD_PRIM_SPRITES, q, 2, RD_SPACE_WORLD, 0, 0);
    }
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    DateWalk w;
    memset(&w, 0, sizeof(w));
    w.f = f;
    RdStateBlock st = f->startState;
    rd__walk(f, (int)f->keep, &st, onDateCmd, &w);
    const RdTargetRec *tc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    CHECK(tc && tc->sx == 1.0f && tc->sy == 1.0f, "date area: SCENE at 1x in the record-only rd");
    CHECK(w.draws == 5, "date area: %d DATE draws walked (5)", w.draws);
    if (!tc || w.draws != 5) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        const RhiRect a = w.area[i];
        CHECK(w.got[i] && a.x == 98 + i * 50 && a.y == 98 && a.w == 45 && a.h == 65,
              "date area: sprite %d (%d,100)-(%d,160) takes (%d,%d) %ux%u, the padded box "
              "(%d,98) 45x65 expected",
              i, 100 + i * 50, 140 + i * 50, a.x, a.y, a.w, a.h, 98 + i * 50);
        CHECK(a.w < tc->tw && a.h < tc->th, "date area: sprite %d's area is not the %ux%u target",
              i, tc->tw, tc->th);
    }
    const uint32_t cw = tc->tw < DA_W ? tc->tw : DA_W, ch = tc->th < DA_H ? tc->th : DA_H;
    CHECK(w.got[4] && w.area[4].x == 0 && w.area[4].y == 0 && w.area[4].w == cw &&
              w.area[4].h == ch,
          "date area: the large sprite takes (%d,%d) %ux%u, the target in the scissor %ux%u",
          w.area[4].x, w.area[4].y, w.area[4].w, w.area[4].h, cw, ch);
    CHECK(w.takes == 5, "date area: %d snapshots for 4 alpha-writing sprites and the large one (5)",
          w.takes);

    /* the retake rule: the same target and serial reuse a snapshot that holds
     * the area, and retake the whole target for one it does not */
    const RhiRect held = {98, 98, 45, 65}, inner = {100, 100, 40, 60}, other = {148, 98, 45, 65};
    RhiRect take = {0, 0, 0, 0};
    CHECK(!rd__date_retake(7, 3, &held, 7, 3, &inner, 512, 448, &take),
          "date retake: an area inside the snapshot reuses it");
    CHECK(rd__date_retake(7, 3, &held, 7, 3, &other, 512, 448, &take) && take.x == 0 &&
              take.y == 0 && take.w == 512 && take.h == 448,
          "date retake: another area at the same serial takes the whole target");
    CHECK(rd__date_retake(7, 3, &held, 7, 4, &inner, 512, 448, &take) && take.x == 100 &&
              take.w == 40,
          "date retake: a new serial takes the draw's area");
    CHECK(rd__date_retake(7, 3, &held, 8, 3, NULL, 512, 448, &take) && take.w == 512 &&
              take.h == 448,
          "date retake: another target without an area takes it whole");

    /* AA1 pads one pixel more; the UI mirror flips the box about the centre */
    RdScreenVtx q[2] = {daVtx(100, 100), daVtx(140, 160)};
    RdStateBlock s = f->startState;
    s.gsW = DA_W;
    s.gsH = DA_H;
    s.scissor[0] = s.scissor[1] = 0;
    s.scissor[2] = DA_W - 1;
    s.scissor[3] = DA_H - 1;
    RhiRect a;
    CHECK(rd__screen_area(tc, &s, q, 2, 0, 1, &a) && a.x == 97 && a.w == 47,
          "date area: AA1 pads three pixels: (%d) %u wide, (97) 47 expected", a.x, a.w);
    CHECK(rd__screen_area(tc, &s, q, 2, 1, 0, &a) && a.x == (int32_t)tc->w - 1 - 142 && a.w == 45,
          "date area: mirrored to (%d) %u wide, (%d) 45 expected", a.x, a.w,
          (int32_t)tc->w - 1 - 142);
}

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    const char *dir = argc > 1 ? argv[1] : ".";
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    testOrderAndLeakage();
    testKeep();
    testStubs();
    testPlans();
    testAlphaMerge();
    testDump(dir);
    testDumpV10(dir);
    testDumpBlock(dir);
    testDumpSheetV11(dir);
    testAa1Plans();
    testEnumeration();
    testRegionScale();
    testAuraFilter();
    testPassOps();
    testStencilOps();
    testDateArea();
    rd_shutdown();
    if (failures) {
        printf("rd_state_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_state_test: ok\n");
    return 0;
}
