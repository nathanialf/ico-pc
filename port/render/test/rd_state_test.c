/* rd_state_test.c: the recording half of rd, without a GPU.
 *
 *   order      draws replay in list order 0..12, not recording order
 *   defaults   gsb_SetGsDefault's TEST/ZBUF/FBA/TEXA at the head of lists
 *              0, 1, 2, 4, 6..12 only
 *   leakage    list 3 inherits list 2's state; list 4's defaults reset
 *              TEST/ZBUF/FBA/TEXA but not ALPHA; list 5 inherits list 4;
 *              state left by list 12 reaches the next frame's list 0
 *   keep       rd_EndFrame(1) walks lists 11 and 12 only
 *   retention  the closed frame and the one before it are kept
 *   stubs      later-wave draws and post kinds are recorded with payload
 *   scissor    (package RSMALL) rd__WideScissor: a UI scissor narrower than the
 *              target follows the wide x scale, outwards, edges stay
 *   plans      AFAIL splits, blend paths, FIX clamps (rd__PlanScreenDraw);
 *              package P8: the stair railings' state (TEST 0x5160D, ALPHA
 *              0x44 with ABE, Z write) splits into two passes that differ
 *              in Z write only (depth test GEQUAL in both, no stencil, no
 *              DATE), as the VU path and its edge-clipped fans (ABE forced
 *              on) draw them; the DATE snapshot and the shadow count write
 *              no Z; PABE flags (DF_C1_DST for Cd*FIX + Cs only)
 *   dump       a frame with textures and a temp target survives dump/load;
 *              package AA1: RDC_AA1 and RdStateBlock.aa1 round-trip, and a
 *              version 3 dump (no aa1) still loads, with AA1 off; a version
 *              5 and a version 7 dump's temps at the handles FEED_HELD and
 *              DISPLAY_HELD (appended after them) are recreated
 *   aa1 plans  PRIM.AA1 keys: the AA1 shaders, triangles, blending with ABE
 *              0 (DF_AA1_FULL), no Z write on lines; aa1 0 unchanged
 *   aura filter (package QUEEN) the model viewer's draw filter around the
 *              mirage's list 8: a GRID keyed by a second object inside the
 *              open window is kept (and after it), a third object's is not;
 *              list 8's targets, Z writes, clear and aura sprite survive
 *   pass ops   (v0.4.2 N2) clear -> draw: the draw's pass takes the clear as its
 *              load op (colour and depth CLEAR), both stored; the passes that
 *              cannot take it keep their loads (rd__TakePendingClear)
 *   pipelines  the reachable screen and post set is under 250 keys (150
 *              before package TEXA's sprite_texa_ps twins), with
 *              the VU program families (wave 3) under RD_PIPELINE_REACHABLE_MAX;
 *              font_sheet_ps (v0.4.2) has font_ps's 10 keys
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
    rd__Walk(f, (int)f->keep, &s, onCmd, seen);
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
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, key);
}

static int testEq(const RdTestState *a, uint64_t gs)
{
    RdTestState b = rd_TestFromGs(gs);
    return a->ate == b.ate && a->atst == b.atst && a->aref == b.aref && a->afail == b.afail &&
           a->date == b.date && a->zte == b.zte && a->ztst == b.ztst;
}

static void testOrderAndLeakage(void)
{
    rd_BeginFrame();
    rd_SelectList(11);
    draw(11);
    rd_SelectList(5);
    draw(5);
    rd_SelectList(1);
    rd_FBA(1);
    draw(1);
    rd_SelectList(2);
    rd_Blend(RD_BLEND_CS_AS_ADD_CD, 0x40, 1);
    rd_TestGs(RD_TEST_AT_LT129);
    rd_PABE(1);
    draw(2);
    rd_SelectList(3);
    draw(3);
    rd_SelectList(4);
    draw(4);
    rd_SelectList(0);
    draw(0);
    rd_SelectList(12);
    rd_Blend(RD_BLEND_LERP_FIX, 0x33, 1);
    draw(12);
    rd_EndFrame(0);

    const RdFrame *f = rd__LastFrame();
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
    rd_BeginFrame();
    draw(100);
    rd_EndFrame(0);
    walk(rd__LastFrame(), &s);
    const RdStateBlock *n0 = stateFor(&s, 100);
    CHECK(n0 && n0->ds.blend == RD_BLEND_LERP_FIX && n0->ds.blendFix == 0x33 && n0->ds.pabe == 1 &&
              testEq(&n0->ds.test, RD_TEST_Z_GEQUAL),
          "state crosses the frame boundary except the list defaults");
    CHECK(rd__PrevFrame() && rd__PrevFrame()->number + 1 == rd__LastFrame()->number,
          "previous frame retained");
}

static void testKeep(void)
{
    rd_BeginFrame();
    rd_SelectList(12);
    rd_Blend(RD_BLEND_CD_SUB_CS_AS, 0x11, 1);
    rd_EndFrame(0);
    rd_BeginFrame();
    rd_SelectList(0);
    rd_Blend(RD_BLEND_CS_FIX_ADD_CD, 0x22, 1);
    draw(200);
    rd_SelectList(11);
    draw(211);
    rd_EndFrame(1);
    Seen s;
    walk(rd__LastFrame(), &s);
    CHECK(s.n == 1 && s.keys[0] == 211, "keep frame walks list 11/12 only (%d draws)", s.n);
    const RdFrame *f = rd__LastFrame();
    CHECK(f->keep == 1 && f->endState.ds.blend == RD_BLEND_CD_SUB_CS_AS &&
              f->endState.ds.blendFix == 0x11,
          "keep frame: list 0's ALPHA never applied");
}

static void testStubs(void)
{
    /* wave 3 (R3ab): a VU mesh of one batch of three prelit vertices (a GIF
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
    RdMesh m = rd_CreateVuMesh(&md);
    const RdMeshRec *mr = rd__MeshRec(m.id);
    CHECK(mr && mr->vertexCount == 3 && mr->indexCount == 3 && mr->index[0] == ICO_VU_INDEX(2, 0),
          "VU mesh: three vertices, one kick");
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_LIT;
    d.code = 32;
    d.vu.mem[2][0] = 0.5f;
    RdXform xf;
    static const uint8_t lut[256 * 4] = {1, 2, 3, 4};
    rd_BeginFrame();
    rd_SelectList(3);
    rd_DrawVuMesh(m, &d, RD_KEY(&xf, 1, 2));
    float sv[4][4] = {{0}};
    rd_ShadowStrip(sv, 4, -1.0f, 7);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.lut = lut;
    pp.scalar[0] = 0.5f;
    rd_Post(RD_POST_FOG, &pp);
    rd_EndFrame(0);
    const RdFrame *f = rd__LastFrame();
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
    rd__ResetStateBlock(&s);
    s.ds.test = rd_TestFromGs(RD_TEST_AT_GT64_FBONLY);
    s.ds.zwrite = RD_ZWRITE_ON;
    int n = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                               RHI_FMT_D32F_S8, dp);
    CHECK(n == 2 && dp[0].key.gs.zwrite == RD_ZWRITE_ON && dp[1].key.gs.zwrite == RD_ZWRITE_OFF &&
              dp[0].modeZ == (RD_ATST_GREATER | 1u << 8 | 1u << 16) &&
              dp[1].modeZ == (RD_ATST_GREATER | 1u << 8 | 2u << 16) && dp[0].aref == 0x40 &&
              dp[0].key.gs.ztst == RD_ZTST_GEQUAL,
          "AFAIL FB_ONLY: two passes, Z written by the passing one only");

    /* package P8: the railing (tesri.tm2's TEST 0x5160D: ATE GREATER 0x60,
     * AFAIL FB_ONLY, Z GEQUAL; ALPHA 0x44 with ABE; Z write on), with ABE
     * as the material packet sets it and as doVu's scissor fans force it:
     * the failing pass is the passing one's pipeline with Z write off, so
     * a hole keeps the depth test and leaves the Z buffer to what is drawn
     * behind it later */
    for (int abe = 0; abe < 2; abe++) {
        rd__ResetStateBlock(&s);
        s.ds.test = rd_TestFromGs(0x5160D);
        s.ds.zwrite = RD_ZWRITE_ON;
        s.ds.abe = (uint8_t)abe;
        s.ds.blend = RD_BLEND_LERP_AS;
        n = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                               RHI_FMT_D32F_S8, dp);
        RdPipeKeyInt k1 = dp[1].key;
        k1.gs.zwrite = RD_ZWRITE_ON;
        CHECK(n == 2 && s.ds.test.aref == 0x60 && s.ds.test.afail == RD_AFAIL_FB_ONLY &&
                  dp[0].key.gs.zwrite == RD_ZWRITE_ON && dp[1].key.gs.zwrite == RD_ZWRITE_OFF &&
                  rd__PipeKeyEqual(&k1, &dp[0].key) && dp[1].key.gs.ztst == RD_ZTST_GEQUAL &&
                  dp[1].key.gs.stencil == RD_STENCIL_OFF && dp[1].key.gs.date == RD_DATE_OFF &&
                  dp[1].key.gs.colorMask == 0xF && dp[0].flags == dp[1].flags &&
                  !(dp[1].flags & ICO_DF_DATE) && dp[1].aref == 0x60 &&
                  dp[1].modeZ == (RD_ATST_GREATER | 1u << 8 | 2u << 16) &&
                  dp[1].key.gs.blend == (abe ? RD_BLEND_LERP_AS : RD_BLEND_COUNT),
              "railing TEST 0x5160D ABE %d: the failing pass differs in Z write only", abe);
    }
    {
        /* the passes that are not draws: no Z written, no split */
        const RdPipeKeyInt snap = rd__PostKey(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
        CHECK(snap.gs.zwrite == RD_ZWRITE_OFF && snap.depthFmt == RHI_FMT_UNKNOWN,
              "the DATE snapshot writes no Z and binds no depth");
        rd__ResetStateBlock(&s);
        s.ds.test = rd_TestFromGs(RD_TEST_Z_GEQUAL);
        s.ds.zwrite = RD_ZWRITE_ON;
        for (int decr = 0; decr < 2; decr++) {
            const RdPipeKeyInt v = rd__ShadowVolumeKey(&s, RHI_FMT_RGBA8_UNORM, decr);
            CHECK(v.gs.zwrite == RD_ZWRITE_OFF && v.gs.colorMask == 0,
                  "the shadow count writes no Z (decr %d)", decr);
        }
    }
    /* PABE (package P8): a uniform; DF_C1_DST tells the shader the
     * unblended c1 is 0 for Cd*FIX + Cs only */
    rd__ResetStateBlock(&s);
    s.ds.abe = 1;
    s.ds.pabe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                       dp);
    CHECK((dp[0].flags & ICO_DF_PABE) && !(dp[0].flags & ICO_DF_C1_DST) && dp[0].key.gs.pabe == 0,
          "PABE on a lerp: DF_PABE, no DF_C1_DST, normalised out of the key");
    s.ds.blend = RD_BLEND_CD_FIX_ADD_CS;
    rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                       dp);
    CHECK((dp[0].flags & (ICO_DF_PABE | ICO_DF_C1_DST)) == (ICO_DF_PABE | ICO_DF_C1_DST),
          "PABE on Cd*FIX + Cs: DF_C1_DST");

    rd__ResetStateBlock(&s);
    s.ds.zwrite = RD_ZWRITE_ON;
    s.ds.test = rd_TestFromGs(RD_TEST_NEVER_RGBONLY);
    n = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8,
                           dp);
    CHECK(n == 1 && dp[0].key.gs.colorMask == 0x7 && dp[0].key.gs.zwrite == RD_ZWRITE_OFF &&
              dp[0].modeZ == 0,
          "ATST NEVER + RGB_ONLY: one RGB-only pass without Z");

    rd__ResetStateBlock(&s);
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_CS_AS_ADD_CD;
    n = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                           dp);
    CHECK(n == 1 && (dp[0].flags & ICO_DF_PREMUL) && !(dp[0].flags & ICO_DF_FIX_FACTOR) &&
              rd__BlendPath(dp[0].key.gs.blend) == RD_BP_PREMUL_ADD,
          "Cs*As + Cd goes through DF_PREMUL");
    s.ds.blend = RD_BLEND_CS_FIX_ADD_CD;
    s.ds.blendFix = 0xFF;
    rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                       dp);
    CHECK((dp[0].flags & (ICO_DF_PREMUL | ICO_DF_FIX_FACTOR)) ==
                  (ICO_DF_PREMUL | ICO_DF_FIX_FACTOR) &&
              dp[0].fix == 0xFF,
          "Cs*FIX + Cd keeps FIX 0xFF exact through DF_PREMUL");
    s.ds.blend = RD_BLEND_LERP_FIX;
    rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                       dp);
    CHECK(dp[0].fix == 0x80 && !(dp[0].flags & ICO_DF_PREMUL), "LERP_FIX with FIX 0xFF clamps");
    s.ds.blend = RD_BLEND_LERP_AS_ALT;
    rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                       dp);
    CHECK(dp[0].key.gs.blend == RD_BLEND_LERP_AS, "mode 7 shares mode 4's pipeline");
    s.ds.test = rd_TestFromGs(RD_TEST_Z_GEQUAL);
    rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN,
                       dp);
    CHECK(dp[0].key.gs.ztst == RD_ZTST_ALWAYS && dp[0].key.gs.zwrite == RD_ZWRITE_OFF,
          "no depth attachment: Z test and write normalised off");
}

/* A dump written before a fixed target was appended: version 5 (before
   RD_TARGET_FEED_HELD) had 16 fixed targets, so its first temporary slot
   has low half 17, FEED_HELD's handle now; versions 6 and 7 (before
   RD_TARGET_DISPLAY_HELD, v0.4.3) had 17, so theirs is 18, DISPLAY_HELD's.
   The dump at path with its temp's handle moved to that slot and its
   version set loads with the temp recreated and remapped, not read as the
   fixed target. */
static void testDumpOldTemps(const RdFrame *f, const char *path, const char *dir, uint32_t ver,
                             RdTargetId fixed)
{
    const uint32_t slot = rd_Target(fixed).id;
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
    if (!rd__LoadFrame(pathOld, &h)) {
        CHECK(0, "version %u dump loaded", ver);
        remove(pathOld);
        return;
    }
    int targets = 0, views = 0;
    for (uint32_t i = 0; i < h.lists[7].count; i++) {
        const RdCmd *c = &h.lists[7].cmds[i];
        if (c->type == RDC_TARGET) {
            const RdTargetRec *t = rd__TargetRec(c->u[0]);
            CHECK(c->u[0] != old && (c->u[0] & 0xFFFF) > RD_TARGET_COUNT && t && t->w == 64 &&
                      t->h == 32 && t->withDepth,
                  "version %u: the temp at slot %u recreated, not the fixed target (%#x)", ver,
                  slot, c->u[0]);
            targets++;
        } else if (c->type == RDC_TEXTURE) {
            const RdTexRec *tx = rd__TexRec(c->u[0]);
            if (tx && tx->kind == RD_TEXKIND_TARGET) {
                CHECK(tx->target != old && (tx->target & 0xFFFF) > RD_TARGET_COUNT,
                      "version %u: the temp's view on the new temp (%#x)", ver, tx->target);
                views++;
            }
        }
    }
    CHECK(targets == 1 && views == 1, "version %u: one target, one view (%d, %d)", ver, targets,
          views);
    rd__FrameFree(&h);
    remove(pathOld);
}

static void testDump(const char *dir)
{
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < (int)sizeof(px); i++) {
        px[i] = (uint8_t)(i * 7 + 1);
    }
    RdTex a = rd_CreateTexture(4, 4, px, RD_TEXA_7F_81_AEM, "a");
    RdTex b = rd_CreateTextureSrc(2, 8, px, RD_TEXSRC_RGB24, "b");
    rd_BeginFrame();
    RdTarget tt = rd_TempTarget(64, 32, 1, 0);
    rd_SelectList(7);
    rd_SetTarget(tt, tt, 64, 32, 0);
    rd_Texture(a, RD_TEXFN_DECAL, RD_TCC_RGB);
    draw(1);
    rd_Texture(b, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_Texture(rd_TargetTexture(tt, RD_VIEW_RGB24_TA0), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    draw(2);
    rd_AA1(1); /* package AA1 */
    draw(3);
    rd_AA1(0);
    draw(4);
    rd_EndFrame(0);
    const RdFrame *f = rd__LastFrame();
    char path[1024];
    snprintf(path, sizeof(path), "%s/rd_state_test.rddump", dir);
    CHECK(rd__DumpFrame(f, path), "dump written");
    RdFrame g;
    if (!rd__LoadFrame(path, &g)) {
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
                RdTexRec *tx = rd__TexRec(x->u[0]), *ty = rd__TexRec(y->u[0]);
                CHECK(tx && ty && x->u[0] != y->u[0] && tx->w == ty->w && tx->h == ty->h &&
                          tx->src == ty->src && tx->kind == ty->kind,
                      "texture remapped to an equal copy");
                if (tx && ty && tx->kind == RD_TEXKIND_IMAGE) {
                    CHECK(memcmp(tx->pixels, ty->pixels, (size_t)tx->w * tx->h * 4) == 0 &&
                              tx->bakedTexa == ty->bakedTexa,
                          "texture pixels round trip");
                }
                texSeen++;
            } else if (x->type == RDC_TARGET) {
                RdTargetRec *a1 = rd__TargetRec(y->u[0]);
                CHECK(y->u[0] != x->u[0] && y->u[1] == y->u[0] && a1 && a1->w == 64 &&
                          a1->h == 32 && a1->withDepth,
                      "temp target recreated and remapped");
            } else {
                CHECK(memcmp(x, y, sizeof(*x)) == 0, "command %d/%u round trip", l, i);
            }
        }
    }
    CHECK(texSeen == 3, "three texture binds compared, got %d", texSeen);
    /* package AA1: the bit at each draw after the round trip */
    Seen seen;
    walk(&g, &seen);
    const RdStateBlock *s2 = stateFor(&seen, 2), *s3 = stateFor(&seen, 3), *s4 = stateFor(&seen, 4);
    CHECK(s2 && s3 && s4 && s2->aa1 == 0 && s3->aa1 == 1 && s4->aa1 == 0,
          "AA1 off, on, off at draws 2, 3, 4 after the round trip");
    CHECK(g.endState.aa1 == f->endState.aa1 && g.startState.aa1 == f->startState.aa1,
          "state blocks' aa1 round trip");
    rd__FrameFree(&g);

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
        uint32_t v3 = 3, sz3 = RD_STATE_BLOCK_V3_SIZE;
        memcpy(buf + 8, &v3, 4);
        memcpy(buf + 16, &sz3, 4);
        /* the end state's aa1, then the start state's */
        memmove(buf + st0 + 2 * sizeof(RdStateBlock) - 4, buf + st0 + 2 * sizeof(RdStateBlock),
                (size_t)size - (st0 + 2 * sizeof(RdStateBlock)));
        memmove(buf + st0 + sizeof(RdStateBlock) - 4, buf + st0 + sizeof(RdStateBlock),
                (size_t)size - 4 - (st0 + sizeof(RdStateBlock)));
        char path3[1100];
        snprintf(path3, sizeof(path3), "%s/rd_state_test_v3.rddump", dir);
        fp = fopen(path3, "wb");
        CHECK(fp && fwrite(buf, 1, (size_t)size - 8, fp) == (size_t)size - 8, "v3 dump written");
        if (fp) {
            fclose(fp);
        }
        free(buf);
        RdFrame h;
        if (rd__LoadFrame(path3, &h)) {
            CHECK(h.startState.aa1 == 0 && h.endState.aa1 == 0 &&
                      memcmp(&h.startState, &f->startState, RD_STATE_BLOCK_V3_SIZE) == 0 &&
                      h.payloadSize == f->payloadSize,
                  "version 3 dump: the state blocks without aa1, AA1 off");
            rd__FrameFree(&h);
        } else {
            CHECK(0, "version 3 dump loaded");
        }
        remove(path3);
    }
    testDumpOldTemps(f, path, dir, 5u, RD_TARGET_FEED_HELD);
    testDumpOldTemps(f, path, dir, 7u, RD_TARGET_DISPLAY_HELD); /* v0.4.3, issue 28 */
    remove(path);
}

static void testAa1Plans(void)
{
    RdStateBlock s;
    RdDrawPass dp[2], base[2];
    rd__ResetStateBlock(&s);
    s.ds.test = rd_TestFromGs(RD_TEST_Z_GEQUAL);
    s.ds.zwrite = RD_ZWRITE_ON;
    s.ds.abe = 0;
    s.ds.blend = RD_BLEND_CS_AS_ADD_CD; /* the storm: mode 5 under PRIM ABE 0 */
    s.aa1 = 1;
    int n = rd__PlanScreenDrawEx(&s, RD_PRIM_LINES, 1, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                 RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].key.gs.aa1 == 1 && dp[0].key.gs.prim == RD_PRIM_TRIANGLES &&
              dp[0].key.vs == RD_VS_SPRITE_AA1_WORLD && dp[0].key.fs == RD_FS_SPRITE_AA1 &&
              dp[0].key.gs.zwrite == RD_ZWRITE_OFF &&
              rd__BlendPath(dp[0].key.gs.blend) == RD_BP_PREMUL_ADD &&
              (dp[0].flags & ICO_DF_AA1_FULL) && (dp[0].flags & ICO_DF_PREMUL),
          "AA1 line with ABE 0: AA1 shaders, triangles, mode 5 blended, no Z write");
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    n = rd__PlanScreenDrawEx(&s, RD_PRIM_TRIANGLES, 1, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_D32F_S8, dp);
    CHECK(n == 1 && dp[0].key.gs.aa1 == 1 && dp[0].key.gs.zwrite == RD_ZWRITE_ON &&
              !(dp[0].flags & ICO_DF_AA1_FULL),
          "AA1 triangles with ABE 1: Z written (the interior), As kept");
    /* aa1 0: the plan rd__PlanScreenDraw makes, whatever the state's bit */
    n = rd__PlanScreenDrawEx(&s, RD_PRIM_TRIANGLES, 0, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_D32F_S8, dp);
    s.aa1 = 0;
    const int nb = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                      RHI_FMT_D32F_S8, base);
    CHECK(n == nb && memcmp(&dp[0], &base[0], sizeof(dp[0])) == 0 && dp[0].key.gs.aa1 == 0,
          "aa1 0 plans as before AA1");
}

static void testEnumeration(void)
{
    static RdPipeKeyInt keys[512];
    uint32_t n = rd__EnumerateReachable(keys, 512);
    const uint32_t ns = rd__EnumerateReachableScreen(keys, 512);
    n = rd__EnumerateReachable(keys, 512);
    printf("  reachable pipelines: %u (screen and post %u, VU programs %u)\n", n, ns, n - ns);
    CHECK(ns > 0 && ns < 250, "reachable screen and post pipelines %u must stay under 250", ns);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipeline count %u must stay under %d", n,
          RD_PIPELINE_REACHABLE_MAX);
    for (uint32_t i = 0; i < n && i < 512; i++) {
        for (uint32_t j = i + 1; j < n && j < 512; j++) {
            CHECK(!rd__PipeKeyEqual(&keys[i], &keys[j]), "duplicate key %u/%u", i, j);
        }
        CHECK(keys[i].gs.atst == RD_ATST_ALWAYS && keys[i].gs.pabe == 0 && keys[i].gs.fba == 0 &&
                  keys[i].gs.afailSplit == 0 && keys[i].gs.date == 0,
              "key %u normalised", i);
    }
    /* v0.4.2 (F-A): font_sheet_ps has font_ps's keys: the overlay's two
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
}

/* Package RSMALL: a UI scissor narrower than the target follows the wide x
 * scale about the target's centre, rounded outwards (it clips as much as the
 * draw does, no more); a side at the target's edge stays; f 1 changes nothing. */
static void testWideScissor(void)
{
    int32_t x0 = 100, x1 = 300;
    rd__WideScissor(&x0, &x1, 512, 0.75f);
    CHECK(x0 == 139 && x1 == 289, "wide scissor 100..300 of 512 at 0.75 is %d..%d (139..289)", x0,
          x1);
    /* the draw's pixels 100..300 land at 256 + 0.75 (p - 256): all inside */
    CHECK(256.0f + 0.75f * (100.0f - 256.0f) >= (float)x0 &&
              256.0f + 0.75f * (301.0f - 256.0f) <= (float)(x1 + 1),
          "the scissor covers the compressed draw");
    x0 = 0;
    x1 = 300;
    rd__WideScissor(&x0, &x1, 512, 0.75f);
    CHECK(x0 == 0 && x1 == 289, "a scissor from the left edge keeps it: %d..%d", x0, x1);
    x0 = 100;
    x1 = 511;
    rd__WideScissor(&x0, &x1, 512, 0.75f);
    CHECK(x0 == 139 && x1 == 511, "a scissor to the right edge keeps it: %d..%d", x0, x1);
    x0 = 0;
    x1 = 511;
    rd__WideScissor(&x0, &x1, 512, 0.75f);
    CHECK(x0 == 0 && x1 == 511, "the full-width scissor is untouched: %d..%d", x0, x1);
    x0 = 100;
    x1 = 300;
    rd__WideScissor(&x0, &x1, 512, 1.0f);
    CHECK(x0 == 100 && x1 == 300, "f 1 leaves the scissor alone: %d..%d", x0, x1);
    /* a one-pixel scissor stays one pixel or more and inside the target */
    x0 = x1 = 5;
    rd__WideScissor(&x0, &x1, 512, 0.75f);
    CHECK(x0 >= 0 && x1 >= x0 && x1 < 512, "a one-pixel scissor: %d..%d", x0, x1);
}

/* ------------------------------------------------------- the aura filter
 * Package QUEEN: the model viewer's draw filter (rd_SetDrawFilter, open
 * while the viewed object's display list runs) and the mirage's list 8.
 * The Queen's dumps hold list 8's shine materials keyed by her and two
 * GRIDs keyed by other objects (her cloth), which are not in list 1.  A GRID
 * keyed by a second object inside the open window is kept and learned (and
 * kept once the window is closed); a third object's is not; and list 8's
 * state (the AURA_WORK target with SCENE's depth, the Z write), the clear
 * (rd_ClearTarget and auraInspireBefore's RD_POST_AURA sprite) and the
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
    rd_DrawVuGrid(&d, key);
}

static void filterWorld(RdKey key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[1].x = v[1].y = 160;
    v[0].q = v[1].q = 1.0f;
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 0, key);
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
    rd_SetDrawFilter(true, own, 1);
    rd_BeginFrame();
    /* the viewed object's display list: the filter open */
    rd_DrawFilterOpen(true);
    rd_SelectList(8);
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), 512, 512, 0);
    rd_ZWrite(1);
    filterWorld(queen);
    filterGrid(cloth); /* drawn by the object, keyed by another */
    rd_DrawFilterOpen(false);
    /* after the window: staticBlur.c's list 8, an unknown object's grid, the
       learned cloth again */
    rd_SelectList(8);
    rd_ClearTarget(rd_Target(RD_TARGET_AURA_WORK), zero, 0, 0);
    {
        RdPostParams p;
        memset(&p, 0, sizeof(p));
        p.rect[0] = p.rect[1] = (float)(0x8000 - 256 * 16);
        p.rect[2] = p.rect[3] = (float)(0x8000 + 256 * 16);
        p.scalar[2] = 1.0f;
        p.exactInt = 1;
        rd_Post(RD_POST_AURA, &p);
    }
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), 512, 512, 0);
    rd_ZWrite(0);
    filterGrid(other);
    filterGrid(cloth);
    filterWorld(queen);
    const RdFrame *f = rd__RecFrame();
    const int grids = countIn(f, 8, RDC_GRID, cloth, 0), lost = countIn(f, 8, RDC_GRID, other, 0);
    const int worlds = countIn(f, 8, RDC_SCREEN, queen, 0);
    const int targets = countIn(f, 8, RDC_TARGET, 0, 1), zw = countIn(f, 8, RDC_ZWRITE, 0, 1);
    const int clears = countIn(f, 8, RDC_CLEAR, 0, 1), posts = countIn(f, 8, RDC_POST_STUB, 0, 1);
    rd_EndFrame(0);
    rd_SetDrawFilter(false, NULL, 0);
    CHECK(grids == 2, "aura filter: the cloth's GRID kept in the window and after it (%d of 2)",
          grids);
    CHECK(lost == 0, "aura filter: an unknown object's GRID left out (%d)", lost);
    CHECK(worlds == 2, "aura filter: the object's own draws kept (%d of 2)", worlds);
    CHECK(
        targets == 2 && zw >= 2 && clears == 1 && posts == 1, /* and the list head's default */
        "aura filter: list 8's targets %d (2), Z writes %d (2 + the head's), clear %d (1), aura sprite %d (1)",
        targets, zw, clears, posts);
}

/* v0.4.2 (N2): the render pass ops of a synthetic frame (clear -> draw ->
 * present).  The clear is not a pass of its own: the draw's pass takes it as
 * its load op (rd__TakePendingClear, as rd_replay.c beginPass), so the
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
        if (rd__TakePendingClear(&w->pend, s->color, s->depth, &cl, &dl)) {
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
    const RdTarget scene = rd_Target(RD_TARGET_SCENE);
    rd_BeginFrame();
    rd_SelectList(0);
    rd_SetTarget(scene, scene, 512, 448, 0);
    rd_ClearTarget(scene, bg, 1, 0);
    draw(1);
    draw(2);
    rd_EndFrame(0);
    const RdFrame *f = rd__LastFrame();
    PassWalk w;
    memset(&w, 0, sizeof(w));
    RdStateBlock st = f->startState;
    rd__Walk(f, (int)f->keep, &st, onPassCmd, &w);
    CHECK(w.draws == 2 && w.taken == 1, "pass ops: %d draws, %d took the clear", w.draws, w.taken);
    CHECK(w.colorLoad == RHI_LOAD_CLEAR && w.depthLoad == RHI_LOAD_CLEAR,
          "pass ops: the first draw's pass loads colour %d, depth %d (CLEAR expected)",
          (int)w.colorLoad, (int)w.depthLoad);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    CHECK(RHI_STORE_STORE == 0 && p.color[0].store == RHI_STORE_STORE &&
              p.depth.store == RHI_STORE_STORE,
          "pass ops: a zeroed pass stores colour and depth");

    /* the cases that keep their loads */
    RdPendingClear pc;
    memset(&pc, 0, sizeof(pc));
    RhiLoadOp cl = RHI_LOAD_LOAD, dl = RHI_LOAD_LOAD;
    CHECK(!rd__TakePendingClear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: nothing pending");
    pc.target = 5;
    pc.depth = 1;
    CHECK(!rd__TakePendingClear(&pc, 6, 5, &cl, &dl) && cl == RHI_LOAD_LOAD && dl == RHI_LOAD_LOAD,
          "pass ops: another colour target");
    CHECK(!rd__TakePendingClear(&pc, 5, 0, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: the cleared depth not bound");
    CHECK(!rd__TakePendingClear(&pc, 5, 6, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: another depth bound");
    dl = RHI_LOAD_DONT_CARE;
    CHECK(!rd__TakePendingClear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_LOAD,
          "pass ops: the depth not loaded");
    dl = RHI_LOAD_LOAD;
    cl = RHI_LOAD_CLEAR;
    CHECK(!rd__TakePendingClear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_CLEAR && dl == RHI_LOAD_LOAD,
          "pass ops: a pass clearing on its own");
    /* a colour-only clear: any depth (or none) loads as asked */
    pc.depth = 0;
    cl = RHI_LOAD_LOAD;
    CHECK(rd__TakePendingClear(&pc, 5, 6, &cl, &dl) && cl == RHI_LOAD_CLEAR && dl == RHI_LOAD_LOAD,
          "pass ops: a colour-only clear with another depth");
    cl = RHI_LOAD_LOAD;
    CHECK(rd__TakePendingClear(&pc, 5, 5, &cl, &dl) && cl == RHI_LOAD_CLEAR && dl == RHI_LOAD_LOAD,
          "pass ops: a colour-only clear keeps its own depth loaded");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    if (!rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    testOrderAndLeakage();
    testKeep();
    testStubs();
    testPlans();
    testDump(dir);
    testAa1Plans();
    testEnumeration();
    testWideScissor();
    testAuraFilter();
    testPassOps();
    rd_Shutdown();
    if (failures) {
        printf("rd_state_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_state_test: ok\n");
    return 0;
}
