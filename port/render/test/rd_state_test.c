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
 *   plans      AFAIL splits, blend paths, FIX clamps (rd__PlanScreenDraw)
 *   dump       a frame with textures and a temp target survives dump/load
 *   pipelines  the reachable screen and post set is under 100 keys, with
 *              the VU program families (wave 3) under RD_PIPELINE_REACHABLE_MAX
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
    rd__FrameFree(&g);
    remove(path);
}

static void testEnumeration(void)
{
    static RdPipeKeyInt keys[512];
    uint32_t n = rd__EnumerateReachable(keys, 512);
    const uint32_t ns = rd__EnumerateReachableScreen(keys, 512);
    n = rd__EnumerateReachable(keys, 512);
    printf("  reachable pipelines: %u (screen and post %u, VU programs %u)\n", n, ns, n - ns);
    CHECK(ns > 0 && ns < 100, "reachable screen and post pipelines %u must stay under 100", ns);
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
    testEnumeration();
    rd_Shutdown();
    if (failures) {
        printf("rd_state_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_state_test: ok\n");
    return 0;
}
