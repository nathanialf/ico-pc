/* rd_vu_skin_check_test.c: the skinned draws' bone-block check (issue 163;
 * rd_vu_skin_check.h, docs/port/VU_SKIN_CHECK.md).
 *
 *   rd_vu_skin_check_test <judge|clean|probe|ring>
 *
 * judge (no device): the judgement alone (rd_vu_skin_judge.c) on a skinned
 *   case of 60 bones and 120 vertices through vu1_ref:
 *   1. the CPU's own positions as the probe: no vertex wrong;
 *   2. one vertex on address 200 moved to where a zero matrix at 200 puts
 *      it: one vertex wrong, address 200 named, "gives it: yes";
 *   3. one vertex moved where no zero matrix puts it: wrong, no address;
 *   4. the bounds: 16/16 pixel right, 17/16 wrong;
 *   5. the byte windows: equal, one qword, qwords 184..187;
 *   6. the decision table's words for each row (H2 race, H2 memory, the
 *      probe right, H1 address-dependent, H1 robust only, H1 neither,
 *      robustness off not tried);
 *   10. the default (rd__skin_check_default, rd__profile_vendor_adreno,
 *      rd__skin_check_env): on only on an Adreno (0x5143) with a window,
 *      off on Mali, PowerVR, NVIDIA, lavapipe and through MoltenVK, off
 *      headless; ICO_RD_VU_SKIN_CHECK=1/true on anywhere, 0/false/off
 *      off, 2 every command.
 * clean|probe|ring (a device; 77 without one): rd_init at Original 4:3 1x,
 *   the check on (rd__vu_skin_check_set), then
 *   7. the start-up probe (rd__vu_skin_selftest: its "ok" line, matched by
 *      test/rd_vu_skin_check.cmake) is right;
 *   8. a frame of one skinned command (the same 60 bones and 120 vertices
 *      through code 20, a camera whose world origin lies in front of the
 *      eye) checked by rd__vu_skin_check_frame: clean, one check, nothing
 *      wrong, the bytes equal, its box check run with no sample outside;
 *      probe (ICO_RD_VU_SKIN_SABOTAGE=200: the probe reads the bone at 200
 *      as zeros, the draw's bytes right), the check wrong, address 200,
 *      the bytes equal, c1 right: "H1, address-dependent"; ring
 *      (ICO_RD_VU_SKIN_SABOTAGE=200,ring: the ring's bytes zeroed after
 *      they were kept), wrong, address 200, the GPU's copy and the CPU's
 *      re-read differing: "H2, a port race";
 *   9. no validation error.
 * The log lines are matched by test/rd_vu_skin_check.cmake. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_vu_skin_check.h"
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

#define NV 120u
#define NB 60u

static float fbits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void qw(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

/* VU memory 0..35: the region bounds, the fade, the common tag, a camera
 * whose world origin lies 5 in front of the eye (x' = 16 x + 2048 (z + 5),
 * y' likewise, z' = 3 * 2^20 (z + 4), w' = z + 5), the light matrices */
static void block(float (*m)[4])
{
    memset(m, 0, 36 * sizeof(m[0]));
    qw(m[0], 0, 0, 0, 1);
    qw(m[1], 4095, 4095, 0, 16777215);
    qw(m[2], 0, 0, 0, 1);
    qw(m[3], fbits(0x8000u), fbits(0x302EC000u), fbits(0x512u), 0);
    qw(m[4], 16, 0, 0, 0);
    qw(m[5], 0, 16, 0, 0);
    qw(m[6], 2048, 2048, 3145728, 1);
    qw(m[7], 10240, 10240, 12582912, 5);
    qw(m[28], 0, 1, 0, 0);
    qw(m[29], 0, 0, 1, 0);
    qw(m[30], 1, 0, 0, 0);
    qw(m[31], 0, 0, 0, 1);
    qw(m[32], 0.5f, 0.5f, 0.5f, 0);
    qw(m[33], 0.25f, 0, 0, 0);
    qw(m[34], 0, 0.25f, 0, 0);
    qw(m[35], 0.125f, 0.125f, 0.25f, 0);
}

static void bones(float (*q)[4])
{
    memset(q, 0, 240 * sizeof(q[0]));
    for (uint32_t b = 0; b < NB; b++) {
        const float c = 1.0f - 0.0012f * (float)(b * b % 41), sn = 0.03f * (float)(b % 11) - 0.15f;
        const float s = 0.9f + 0.004f * (float)b;
        qw(q[4 * b + 0], s * c, s * sn, 0.01f * (float)(b % 5), 0);
        qw(q[4 * b + 1], -s * sn, s * c, 0.015f * (float)(b % 3), 0);
        qw(q[4 * b + 2], 0.02f * (float)(b % 4), -0.01f * (float)(b % 6), 1.0f + 0.003f * (float)b,
           0);
        qw(q[4 * b + 3], 0.3f * (float)(b % 10) - 1.5f, 0.25f * (float)(b % 8) - 1.0f,
           0.05f * (float)(b % 9), 1);
    }
}

/* the stream, vertex v at q[v * 5]: lane x address 16 + 4 (v mod 60), lane
 * z 16 + 4 ((7 v + 13) mod 60); vertex 0 of each run of 12 restarts the
 * strip (ST.w 0) */
static void stream(float (*q)[4])
{
    for (uint32_t v = 0; v < NV; v++) {
        float (*o)[4] = q + v * 5u;
        const float x = -25.0f + 50.0f * (float)(v % 12u) / 11.0f;
        const float y = -20.0f + 40.0f * (float)(v / 12u) / 9.0f;
        qw(o[0], x, y, 2.0f + 0.15f * (float)(v % 7u), 1);
        qw(o[1], 0.3f, 0.5f, 0.8f, 1);
        const uint32_t a0 = 16u + 4u * (v % NB), a1 = 16u + 4u * ((7u * v + 13u) % NB);
        const float w0 = 0.2f + 0.15f * (float)(v % 5u);
        qw(o[2], fbits(a0), w0, fbits(a1), 1.0f - w0);
        qw(o[3], 0.1f * (float)(v % 12u), 0.2f, 1, v % 12u == 0 ? 0.0f : 1.0f);
        qw(o[4], 100, 100, 100, 128);
    }
}

static float s_mem[36][4], s_bones[240][4], s_stream[NV * 5u][4];
static const RdSkinBatch s_batch = {0, NV};

static RdSkinCase skinCase(void)
{
    return (RdSkinCase){20,
                        (const float (*)[4])s_mem,
                        (const float (*)[4])s_bones,
                        (const float (*)[4])s_stream,
                        5u,
                        &s_batch,
                        1u,
                        NV};
}

static int judgeTests(void)
{
    block(s_mem);
    bones(s_bones);
    stream(s_stream);
    const RdSkinCase k = skinCase();
    static RdSkinPos ref[NV], zero[NV];
    CHECK(rd__skin_reference(&k, 0, ref), "the reference");
    CHECK(rd__skin_reference(&k, 200, zero), "the reference with address 200 zeroed");
    uint32_t compared = 0;
    for (uint32_t i = 0; i < NV; i++) {
        compared += ref[i].compared;
    }
    CHECK(compared == NV, "%u of %u vertices compared", compared, NV);
    static int32_t got[NV][3];
    for (uint32_t i = 0; i < NV; i++) {
        got[i][0] = ref[i].x;
        got[i][1] = ref[i].y;
        got[i][2] = ref[i].z;
    }
    /* 1. */
    RdSkinVerdict v;
    rd__skin_judge(&k, ref, (const int32_t (*)[3])got, &v);
    printf("  1. the CPU's positions: %u compared, %u wrong\n", v.compared, v.wrong);
    CHECK(v.compared == NV && v.wrong == 0 && v.addr == 0, "%u compared, %u wrong, address %u",
          v.compared, v.wrong, v.addr);
    /* 2. a vertex on 200 (bone 46: v mod 60 = 46 in lane x) that moves */
    uint32_t at = UINT32_MAX;
    for (uint32_t i = 0; i < NV && at == UINT32_MAX; i++) {
        if (i % NB == 46u && !rd__skin_pos_near(got[i], &zero[i])) {
            at = i;
        }
    }
    CHECK(at != UINT32_MAX, "no vertex on address 200 moves under its zero matrix");
    if (at != UINT32_MAX) {
        got[at][0] = zero[at].x;
        got[at][1] = zero[at].y;
        got[at][2] = zero[at].z;
        rd__skin_judge(&k, ref, (const int32_t (*)[3])got, &v);
        printf("  2. vertex %u at the zero matrix's point: %u wrong, address %u, gives it %s\n", at,
               v.wrong, v.addr, v.explains ? "yes" : "no");
        CHECK(v.wrong == 1 && v.firstWrong == at && v.addr == 200 && v.explains && v.explained == 1,
              "%u wrong (first %u), address %u, explains %d, gives %u", v.wrong, v.firstWrong,
              v.addr, v.explains, v.explained);
        got[at][0] = ref[at].x;
        got[at][1] = ref[at].y;
        got[at][2] = ref[at].z;
    }
    /* 3. */
    got[7][0] += 50 * 16;
    rd__skin_judge(&k, ref, (const int32_t (*)[3])got, &v);
    printf("  3. vertex 7 moved 50 pixels: %u wrong, address %u\n", v.wrong, v.addr);
    CHECK(v.wrong == 1 && v.addr == 0 && !v.explains, "%u wrong, address %u, explains %d", v.wrong,
          v.addr, v.explains);
    got[7][0] = ref[7].x;
    /* 4. */
    int32_t p[3] = {ref[3].x + RD_SKIN_XY_TOL, ref[3].y - RD_SKIN_XY_TOL, ref[3].z};
    CHECK(rd__skin_pos_near(p, &ref[3]), "16/16 pixel off is right");
    p[0]++;
    CHECK(!rd__skin_pos_near(p, &ref[3]), "17/16 pixel off is wrong");
    /* 5. */
    uint8_t a[3840], b[3840];
    memset(a, 0x11, sizeof(a));
    memcpy(b, a, sizeof(b));
    uint32_t q0 = 9, q1 = 9;
    char w[64];
    CHECK(!rd__skin_bytes_differ(a, b, sizeof(a), &q0, &q1), "equal blocks differ");
    rd__skin_bytes_words(false, q0, q1, w, sizeof(w));
    CHECK(strcmp(w, "equal") == 0, "\"%s\"", w);
    b[5 * 16 + 3] ^= 1;
    CHECK(rd__skin_bytes_differ(a, b, sizeof(a), &q0, &q1) && q0 == 5 && q1 == 5, "qword %u..%u",
          q0, q1);
    rd__skin_bytes_words(true, q0, q1, w, sizeof(w));
    CHECK(strcmp(w, "differs at qword 5") == 0, "\"%s\"", w);
    memcpy(b, a, sizeof(b));
    memset(b + 184 * 16, 0, 64);
    CHECK(rd__skin_bytes_differ(a, b, sizeof(a), &q0, &q1) && q0 == 184 && q1 == 187,
          "qwords %u..%u", q0, q1);
    rd__skin_bytes_words(true, q0, q1, w, sizeof(w));
    printf("  5. bone 200 zeroed in a block: \"%s\"\n", w);
    CHECK(strcmp(w, "differs at qwords 184..187") == 0, "\"%s\"", w);

    /* 6. */
    static const struct {
        bool copy, reread, wrong;
        int c1, c2;
        const char *starts;
    } rows[] = {
        {false, true, true, 1, 1, "H2, a port race:"},
        {true, false, true, 1, 1, "H2, the memory:"},
        {false, false, false, -1, -1, "the probe drew right:"},
        {false, false, true, 1, -1,
         "H1, address-dependent: the driver's uniform read at that "
         "offset"},
        {false, false, true, 1, 1,
         "H1, address-dependent under robust access: right at a fresh "
         "offset and right without uniform robustness (either fix"},
        {false, false, true, 0, 1, "H1 under robust access only:"},
        {false, false, true, 0, 0,
         "H1, the driver's uniform read of this draw: wrong at a fresh "
         "offset too, and not robust access alone"},
        {false, false, true, 0, -1,
         "H1, the driver's uniform read of this draw: wrong at a fresh "
         "offset too (robustness off not tried)"},
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const char *d =
            rd__skin_decision(rows[i].copy, rows[i].reread, rows[i].wrong, rows[i].c1, rows[i].c2);
        printf("  6. row %zu: %s\n", i, d);
        CHECK(strncmp(d, rows[i].starts, strlen(rows[i].starts)) == 0, "row %zu: \"%s\"", i, d);
    }

    /* 10. the default: on only on an Adreno (vendor 0x5143, Qualcomm's
     * driver and Turnip alike) with a window; the setting over it */
    static const struct {
        uint32_t vendor;
        bool moltenvk, adreno;
    } vendors[] = {
        {0x5143u, false, true},   /* Adreno */
        {0x13B5u, false, false},  /* Mali (issue 179's G52) */
        {0x1010u, false, false},  /* PowerVR */
        {0x10DEu, false, false},  /* NVIDIA */
        {0x10005u, false, false}, /* lavapipe */
        {0x5143u, true, false},   /* through MoltenVK: the Apple class */
    };

    for (size_t i = 0; i < sizeof(vendors) / sizeof(vendors[0]); i++) {
        const bool a = rd__profile_vendor_adreno(vendors[i].vendor, vendors[i].moltenvk);
        CHECK(a == vendors[i].adreno, "vendor 0x%x moltenvk %d: adreno %d", vendors[i].vendor,
              vendors[i].moltenvk, a);
        const int on = rd__skin_check_default(true, a, -1);
        CHECK(on == (vendors[i].adreno ? 1 : 0), "vendor 0x%x with a window: %d", vendors[i].vendor,
              on);
        CHECK(rd__skin_check_default(false, a, -1) == 0, "vendor 0x%x headless: on",
              vendors[i].vendor);
        CHECK(rd__skin_check_default(false, a, 1) == 1 && rd__skin_check_default(true, a, 1) == 1,
              "vendor 0x%x: ICO_RD_VU_SKIN_CHECK=1 not on", vendors[i].vendor);
        CHECK(rd__skin_check_default(true, a, 0) == 0, "vendor 0x%x: ICO_RD_VU_SKIN_CHECK=0 on",
              vendors[i].vendor);
    }
    CHECK(rd__skin_check_default(true, true, 2) == 2, "=2 not every command");

    static const struct {
        const char *v;
        int want;
    } envs[] = {{NULL, -1}, {"", -1},    {"0", 0},  {"false", 0}, {"off", 0}, {"no", 0},
                {"1", 1},   {"true", 1}, {"on", 1}, {"yes", 1},   {"2", 2}};

    for (size_t i = 0; i < sizeof(envs) / sizeof(envs[0]); i++) {
        const int e = rd__skin_check_env(envs[i].v);
        CHECK(e == envs[i].want, "ICO_RD_VU_SKIN_CHECK=%s read as %d, not %d",
              envs[i].v ? envs[i].v : "(unset)", e, envs[i].want);
    }
    printf("  10. on by default only on an Adreno with a window; =false off, =1 on anywhere\n");
    return failures ? 1 : 0;
}

/* --------------------------------------------------------- on a device */

static RdMesh makeMesh(void)
{
    static float q[1 + NV * 5u][4];
    qw(q[0], fbits(0x8000u | NV), fbits(0x30064000u), fbits(0x512u), 0);
    memcpy(q[1], s_stream, sizeof(s_stream));
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            1u + NV * 5u,
                            RD_VU_QW_SKIN,
                            1,
                            &batch,
                            0,
                            "rd vu skin check test",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

static bool buildFrame(RdFrame *f, RdMesh mesh)
{
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = t->w;
    f->gsH = t->h;
    static const uint8_t clear[4] = {16, 32, 48, 0x80};
    bool ok = rd__st_target(f, 0, rd_target(RD_TARGET_SCENE).id, t->w, t->h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = rd_target(RD_TARGET_SCENE).id;
        memcpy(c->b, clear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    RdCmd *bl = ok && c ? rd__st_push(f, 0, RDC_BLEND) : NULL;
    if (bl) {
        bl->b[1] = 0x80; /* ABE off */
    }
    ok = ok && bl && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_FBA, 0) &&
         rd__st_push1(f, 0, RDC_PABE, 0) && rd__st_push1(f, 0, RDC_COLCLAMP, 1) &&
         rd__st_push1(f, 0, RDC_SHADE, 1);
    if (!ok) {
        return false;
    }
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.code = 20;
    p.clip = RD_VU_CLIP_REGION;
    p.prog = RD_PROG_SKIN;
    p.batchCount = 1;
    p.boneQw = 240;
    p.qwPerVertex = RD_VU_QW_SKIN;
    RdVuBlock vu;
    memset(&vu, 0, sizeof(vu));
    memcpy(vu.mem, s_mem, sizeof(vu.mem));
    static uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock) + sizeof(s_bones)];
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &vu, sizeof(vu));
    memcpy(buf + sizeof(p) + sizeof(vu), s_bones, sizeof(s_bones));
    const uint32_t off = rd__frame_payload(f, buf, (uint32_t)sizeof(buf));
    RdCmd *m = rd__st_push(f, 0, RDC_SKINNED);
    if (!m) {
        return false;
    }
    m->b[0] = p.prog;
    m->b[1] = p.code;
    m->b[2] = p.clip;
    m->u[0] = mesh.id;
    m->u[1] = off;
    m->u[2] = (uint32_t)sizeof(buf);
    m->keyLo = 1;
    return true;
}

static int deviceTests(const char *mode)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = 1.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    if (!rd_init(640, 448, &st, NULL)) {
        printf("rd_vu_skin_check_test: SKIP: no usable device\n");
        return 77;
    }
    rd__set_not_implemented_fatal(true);
    rd__vu_skin_check_set(1);
    block(s_mem);
    bones(s_bones);
    stream(s_stream);
    /* 7. */
    CHECK(rd__vu_skin_selftest(), "the start-up probe is wrong on this device");
    /* 8. */
    RdMesh mesh = makeMesh();
    static RdFrame f;
    CHECK(mesh.id != 0, "the mesh");
    if (mesh.id && buildFrame(&f, mesh)) {
        RdSkinSummary s;
        const int rc = rd__vu_skin_check_frame(&f, 0, &s);
        printf("  8. %s: %u commands, %u checks, %u wrong (address %u), %u blocks differ, %u "
               "re-read differ, c1 right %u, c2 right %u (c2 drawn %u), %u box checks, %u with "
               "samples outside; reads as: %s\n",
               mode, s.commands, s.checks, s.wrong, s.lastAddr, s.blocksDiffer, s.rereadDiffer,
               s.c1Right, s.c2Right, s.c2Run, s.boxChecks, s.boxOutside,
               s.decision[0] ? s.decision : "-");
        CHECK(rc >= 0 && s.commands == 1 && s.checks == 1, "rc %d, %u commands, %u checks", rc,
              s.commands, s.checks);
        /* c2 drawn wherever the device offers uniform robustness off
         * (robust access on, VK_EXT_pipeline_robustness: _robust,
         * _probe_robust) */
        const bool c2Offered = rhi_limits()->uniformRobustOff;
        CHECK(!c2Offered || s.c2Run == 1, "c2 offered but drawn in %u checks", s.c2Run);
        if (strcmp(mode, "clean") == 0) {
            CHECK(s.wrong == 0 && s.blocksDiffer == 0 && s.rereadDiffer == 0, "a clean check");
            CHECK(!rhi_occlusion_supported() || (s.boxChecks == 1 && s.boxOutside == 0),
                  "%u box checks, %u outside", s.boxChecks, s.boxOutside);
        } else if (strcmp(mode, "probe") == 0) {
            CHECK(s.wrong == 1 && s.lastAddr == 200 && s.blocksDiffer == 0 && s.c1Right == 1 &&
                      strncmp(s.decision, "H1, address-dependent", 21) == 0,
                  "the probe's zero matrix at 200 not named as H1");
            /* c2 reads the draw's own block (right) through the pipeline
             * with uniform robustness off */
            CHECK(!c2Offered || s.c2Right == 1, "c2 offered, drawn %u, right %u", s.c2Run,
                  s.c2Right);
        } else {
            CHECK(s.wrong == 1 && s.lastAddr == 200 && s.blocksDiffer == 1 && s.rereadDiffer == 1 &&
                      strncmp(s.decision, "H2, a port race", 15) == 0,
                  "the ring's zeros at 200 not named as H2");
        }
        rd__frame_free(&f);
    } else {
        CHECK(0, "the frame");
    }
    if (mesh.id) {
        rd_destroy_vu_mesh(mesh);
    }
    /* 9. */
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: rd_vu_skin_check_test <judge|clean|probe|ring>\n");
        return 2;
    }
    const int rc = strcmp(argv[1], "judge") == 0 ? judgeTests() : deviceTests(argv[1]);
    if (rc == 77) {
        return 77;
    }
    if (failures) {
        printf("rd_vu_skin_check_test %s: %d failures\n", argv[1], failures);
        return 1;
    }
    printf("rd_vu_skin_check_test %s: ok\n", argv[1]);
    return 0;
}
