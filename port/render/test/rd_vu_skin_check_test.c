/* rd_vu_skin_check_test.c: the skinned draws' bone-block check (issue 163;
 * rd_vu_skin_check.h, docs/port/VU_SKIN_CHECK.md).
 *
 *   rd_vu_skin_check_test <judge|clean|probe|ring|twin|capture|thin> [folder]
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
 * twin|capture (a device; issue 123): the same frame under the game's
 *   mode (1) with the twins on (rd__vu_skin_twins_set) and the scheduled
 *   probe off (ICO_RD_VU_SKIN_INTERVAL_MS=off: the targeted probe alone),
 *   ICO_RD_VU_SKIN_SABOTAGE=200,ring (or 200,nan) in the environment,
 *   replayed three times, each harvested at once
 *   (rd__vu_skin_check_replays):
 *   11. the first replay's twins differ (A read the sabotaged bone, B the
 *      kept bytes at a fresh offset), no check drawn there;
 *   12. the next replay's probe targeted at that command, wrong, address
 *      200 named (zero: by the zero matrix; nan: dropped, by the
 *      non-finite matrix);
 *   capture, with folder: 13. the capture asked once
 *      (rd_vu_skin_capture_due), its sidecar under folder: the header's
 *      magic, version, command, the flags (the check's data, the twins'
 *      counts, the twins differed, the check targeted), A != B, the CPU's
 *      bytes and the GPU's copy differing at bone 200 (qwords 184..187)
 *      and nowhere else.
 * thin (a device; issue 123 after v0.10.5): the twins' thinning
 *   (rd__vu_skin_thin_set 4 for RD_SKIN_THIN_REPLAYS) on a frame of three
 *   skinned commands, the game's mode with the twins on and the scheduled
 *   probe off:
 *   14. four clean replays: 3 twins each, none differing; then thinned;
 *   15. three more: one twin a replay;
 *   16. the ring's bone at 200 zeroed (rd__vu_skin_sabotage_set): the one
 *      twin differs, the twins re-armed; the next replay twins all three
 *      again;
 *   17. the sabotage off, four clean replays: thinned again, one a replay.
 * judge also runs the trace (rd_vu_skin_trace.c; issue 123 v0.11.3):
 *   18. the clear stamp's colour read back as its replay, an earlier
 *      replay's, none;
 *   19. the ring stamp's states and the stamp line's words;
 *   20. a differing copy searched: the slot's readback as before, zeros, a
 *      block nowhere, the other slot's ring at the draw's offset, the own
 *      ring at another offset, a block checked two replays before (the
 *      check's own block never the answer), the trace's 64 kept.
 * stale (a device): 21. the frame replayed four times under the game's
 *   mode with a probe each replay (ICO_RD_VU_SKIN_INTERVAL_MS=0) and each
 *   slot's readback written by its first check alone
 *   (rd__vu_skin_sabotage_copy; both written in 22. before): the four
 *   checks read as not written by their replay; then two more replays
 *   without the sabotage, both stamps right.
 *   The clean, probe and ring frames' stamps are their replay's; ring's
 *   differing copy is found in the check's own ring.
 *   22. (first) under the careful rule (rd_vu_skin_background with a
 *   reason: the start-up set stopped for memory on Qualcomm's own driver)
 *   the box check's pipeline is compiled in the background and the box
 *   check drawn (before v0.11.3 none was compiled).
 * The log lines are matched by test/rd_vu_skin_check.cmake. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_vu_skin_check.h"
#include "vk/rhi_vk.h"

static int failures;

static void sleepMs(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    const struct timespec ts = {0, (long)ms * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

/* the window's memory function (rd_vu_skin_background) as it answers above
 * the floor */
static int memoryFine(char *why, size_t size)
{
    if (why && size) {
        snprintf(why, size, "the test's memory: fine");
    }
    return 0;
}

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

/* 18.-20.: the trace (rd_vu_skin_trace.c): the stamps and the search */
static void traceTests(void)
{
    /* 18. the clear stamp */
    float cl[4];
    const uint32_t replay = 0x01123456u;
    rd__skin_clear_stamp(replay, cl);
    uint8_t tx[4];
    for (int c = 0; c < 4; c++) {
        tx[c] = (uint8_t)cl[c];
    }
    uint32_t seen = 0;
    CHECK(rd__skin_clear_stamp_state(tx, replay, &seen) == RD_SKIN_STAMP_OK && seen == 0x123456u,
          "the clear stamp of replay 0x%x read back as 0x%x", replay, seen);
    CHECK(rd__skin_clear_stamp_state(tx, replay + 2u, &seen) == RD_SKIN_STAMP_OLD &&
              seen == 0x123456u,
          "a later replay's harvest: not OLD");
    static const uint8_t none[4] = {0, 0, 0, 0};
    CHECK(rd__skin_clear_stamp_state(none, replay, &seen) == RD_SKIN_STAMP_NONE,
          "a texel without the tag read as a stamp");
    /* 19. the ring stamp and the words */
    const RdSkinRingStamp rs = {RD_SKIN_RING_STAMP_MAGIC, 785u, 1u, 7u};
    CHECK(rd__skin_ring_stamp_state(&rs, 785u, 1u, 7u, &seen) == RD_SKIN_STAMP_OK,
          "the ring stamp of its own replay");
    CHECK(rd__skin_ring_stamp_state(&rs, 787u, 1u, 7u, &seen) == RD_SKIN_STAMP_OLD && seen == 785u,
          "an earlier replay's ring stamp");
    CHECK(rd__skin_ring_stamp_state(&rs, 785u, 0u, 7u, &seen) == RD_SKIN_STAMP_OLD,
          "another slot's ring stamp");
    static const uint8_t zeros16[16];
    CHECK(rd__skin_ring_stamp_state(zeros16, 785u, 1u, 7u, &seen) == RD_SKIN_STAMP_NONE,
          "zeros read as a ring stamp");
    char w[512];
    rd__skin_stamp_words(RD_SKIN_STAMP_OK, 0, RD_SKIN_STAMP_OK, 0, w, sizeof(w));
    CHECK(w[0] == '\0', "both stamps right: \"%s\"", w);
    rd__skin_stamp_words(RD_SKIN_STAMP_OLD, 783u, RD_SKIN_STAMP_OLD, 783u, w, sizeof(w));
    printf("  19. %s\n", w);
    CHECK(strstr(w, "the readback was not written by this replay's copies") == w &&
              strstr(w, "clear stamp: replay 783's") != NULL,
          "\"%s\"", w);
    rd__skin_stamp_words(RD_SKIN_STAMP_OK, 0, RD_SKIN_STAMP_NONE, 0, w, sizeof(w));
    printf("  19. %s\n", w);
    CHECK(strstr(w, "the copies ran") == w && strstr(w, "no stamp") != NULL, "\"%s\"", w);
    /* 20. the search: the bones kept, another replay's block x (the bones
     * moved), each place it can come from */
    bones(s_bones);
    static uint8_t kept[3840], x[3840], gpu[3840], prev[3840];
    memcpy(kept, s_bones, sizeof(kept));
    static float moved[240][4];
    memcpy(moved, s_bones, sizeof(moved));
    for (uint32_t b = 0; b < NB; b++) {
        moved[4 * b + 3][0] += 0.25f; /* every bone moved a little: a later pose */
    }
    memcpy(x, moved, sizeof(x));
    memset(prev, 0x3C, sizeof(prev));
    RdSkinTrace tr;
    memset(&tr, 0, sizeof(tr));
    RdSkinCopyFind in;
    memset(&in, 0, sizeof(in));
    in.gpu = gpu;
    in.kept = kept;
    in.bytes = sizeof(gpu);
    in.replay = 787u;
    in.slot = 1u;
    in.off = 512u;
    in.prev = prev;
    in.prevReplay = 781u;
    in.trace = &tr;
    in.align = 256u;
    static uint8_t ring0[65536], ring1[65536];
    memset(ring0, 0x11, sizeof(ring0));
    memset(ring1, 0x22, sizeof(ring1));
    in.ring[0] = ring0;
    in.ring[1] = ring1;
    in.ringUsed[0] = in.ringUsed[1] = sizeof(ring0);
    in.ringReplay[0] = 788u;
    in.ringReplay[1] = 787u;
    RdSkinCopyFound fd;
    memcpy(gpu, prev, sizeof(gpu));
    rd__skin_copy_find(&in, &fd);
    rd__skin_copy_words(&fd, 3840u, w, sizeof(w));
    printf("  20. the slot's readback as before: %s\n", w);
    CHECK(fd.kind == RD_SKIN_FOUND_PREV && fd.replay == 781u, "kind %d replay %u", fd.kind,
          fd.replay);
    memset(gpu, 0, sizeof(gpu));
    rd__skin_copy_find(&in, &fd);
    CHECK(fd.kind == RD_SKIN_FOUND_ZERO && fd.zeroQw == 240u, "zeros: kind %d, %u zero qwords",
          fd.kind, fd.zeroQw);
    memcpy(gpu, x, sizeof(gpu));
    rd__skin_copy_find(&in, &fd);
    CHECK(fd.kind == RD_SKIN_FOUND_NONE && fd.scanned > 0u && fd.keptQw > 0u,
          "a block found nowhere: kind %d, %llu scanned, %u kept", fd.kind,
          (unsigned long long)fd.scanned, fd.keptQw);
    rd__skin_copy_words(&fd, 3840u, w, sizeof(w));
    printf("  20. nowhere: %s\n", w);
    CHECK(strstr(w, "no block the check kept or the rings hold") == w, "\"%s\"", w);
    /* in the other slot's ring at the draw's own offset */
    memcpy(ring0 + 512, x, sizeof(x));
    rd__skin_copy_find(&in, &fd);
    CHECK(fd.kind == RD_SKIN_FOUND_RING && fd.slot == 0u && fd.off == 512u && fd.sameOff &&
              fd.replay == 788u,
          "the other slot's block: kind %d slot %u offset %llu replay %u", fd.kind, fd.slot,
          (unsigned long long)fd.off, fd.replay);
    /* in the check's own ring at another offset (searched first) */
    memcpy(ring1 + 8192, x, sizeof(x));
    rd__skin_copy_find(&in, &fd);
    rd__skin_copy_words(&fd, 3840u, w, sizeof(w));
    printf("  20. in a ring: %s\n", w);
    CHECK(fd.kind == RD_SKIN_FOUND_RING && fd.slot == 1u && fd.off == 8192u && !fd.sameOff,
          "the own slot's block: kind %d slot %u offset %llu", fd.kind, fd.slot,
          (unsigned long long)fd.off);
    /* a block checked two replays before at the draw's offset (the trace
     * is asked before the rings) */
    RdSkinTraceEntry te;
    memset(&te, 0, sizeof(te));
    te.hash = rd__skin_block_hash(x, sizeof(x));
    te.off = 512u;
    te.replay = 785u;
    te.slot = 1u;
    te.meshId = 65622u;
    snprintf(te.mesh, sizeof(te.mesh), "boymodel");
    rd__skin_trace_note(&tr, &te);
    /* the check's own block (its bytes kept) is never the answer */
    te.hash = rd__skin_block_hash(kept, sizeof(kept));
    te.replay = 787u;
    rd__skin_trace_note(&tr, &te);
    rd__skin_copy_find(&in, &fd);
    rd__skin_copy_words(&fd, 3840u, w, sizeof(w));
    printf("  20. checked before: %s\n", w);
    CHECK(fd.kind == RD_SKIN_FOUND_TRACE && fd.replay == 785u && fd.sameOff &&
              strcmp(fd.mesh, "boymodel") == 0,
          "the trace: kind %d replay %u", fd.kind, fd.replay);
    CHECK(strstr(w, "the block checked in replay 785 (frame slot 1, ring offset 512, the draw's "
                    "own offset; mesh boymodel") != NULL,
          "\"%s\"", w);
    /* the trace keeps the last RD_SKIN_TRACE_KEEP */
    for (uint32_t i = 0; i < RD_SKIN_TRACE_KEEP; i++) {
        te.hash = i + 1u;
        rd__skin_trace_note(&tr, &te);
    }
    rd__skin_copy_find(&in, &fd);
    CHECK(tr.count == RD_SKIN_TRACE_KEEP && fd.kind == RD_SKIN_FOUND_RING,
          "the trace kept %u, kind %d", tr.count, fd.kind);
    printf("  18.-20. the trace's stamps and search\n");
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
    traceTests();
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

static bool buildFrame(RdFrame *f, RdMesh mesh, uint32_t copies)
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
    for (uint32_t i = 0; i < copies; i++) {
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
    }
    return true;
}

/* 14.-17.: the twins' thinning over replays of f (three skinned commands) */
static void thinTests(const RdFrame *f)
{
    RdSkinSummary s;
    rd__vu_skin_thin_set(4);
    int rc = rd__vu_skin_check_replays(f, 0, 4, &s);
    printf("  14. four clean replays: %u twins, %u differ; thinned %d\n", s.twins, s.twinsDiffer,
           (int)rd__vu_skin_thinned());
    CHECK(rc >= 0 && s.twins == 12 && s.twinsDiffer == 0 && rd__vu_skin_thinned(),
          "rc %d, %u twins, %u differ, thinned %d", rc, s.twins, s.twinsDiffer,
          (int)rd__vu_skin_thinned());
    rc = rd__vu_skin_check_replays(f, 0, 3, &s);
    printf("  15. thinned, three replays: %u twins, %u differ\n", s.twins, s.twinsDiffer);
    CHECK(rc >= 0 && s.twins == 3 && s.twinsDiffer == 0 && rd__vu_skin_thinned(),
          "rc %d, %u twins, %u differ", rc, s.twins, s.twinsDiffer);
    rd__vu_skin_sabotage_set(200);
    rc = rd__vu_skin_check_replays(f, 0, 1, &s);
    printf("  16. the bone at 200 zeroed: %u twins, %u differ; thinned %d\n", s.twins,
           s.twinsDiffer, (int)rd__vu_skin_thinned());
    CHECK(rc >= 0 && s.twins == 1 && s.twinsDiffer == 1 && !rd__vu_skin_thinned(),
          "rc %d, %u twins, %u differ, thinned %d", rc, s.twins, s.twinsDiffer,
          (int)rd__vu_skin_thinned());
    rc = rd__vu_skin_check_replays(f, 0, 1, &s);
    printf("  16. re-armed, the next replay: %u twins, %u differ\n", s.twins, s.twinsDiffer);
    CHECK(rc >= 0 && s.twins == 3 && s.twinsDiffer == 3, "rc %d, %u twins, %u differ", rc, s.twins,
          s.twinsDiffer);
    rd__vu_skin_sabotage_set(0);
    rc = rd__vu_skin_check_replays(f, 0, 4, &s);
    const bool thin = rd__vu_skin_thinned();
    RdSkinSummary t;
    const int rc2 = rd__vu_skin_check_replays(f, 0, 2, &t);
    printf("  17. the sabotage off: four replays %u twins, %u differ, thinned %d; then two "
           "replays %u twins\n",
           s.twins, s.twinsDiffer, (int)thin, t.twins);
    CHECK(rc >= 0 && rc2 >= 0 && s.twins == 12 && s.twinsDiffer == 0 && thin && t.twins == 2 &&
              t.twinsDiffer == 0,
          "rc %d/%d, %u twins, %u differ, thinned %d, then %u twins", rc, rc2, s.twins,
          s.twinsDiffer, (int)thin, t.twins);
    rd__vu_skin_thin_set(0);
}

/* 13.: the sidecar's header and sections (path from
 * rd_vu_skin_capture_due) */
static void captureCheck(const char *path)
{
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL, "the sidecar %s cannot be read", path);
    if (!fp) {
        return;
    }
    static uint8_t buf[1u << 20];
    const size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    RdSkinCaptureHeader h;
    CHECK(n >= sizeof(h), "the sidecar is %zu bytes", n);
    if (n < sizeof(h)) {
        return;
    }
    memcpy(&h, buf, sizeof(h));
    printf("  13. %s: %zu bytes, frame %u command %u:%u mesh %s #%u, flags 0x%x, twins %llu / "
           "%llu, %u wrong (%u dropped), address %u (non-finite %u); reads as: %s\n",
           path, n, h.frame, h.list, h.index, h.mesh, h.meshId, h.flags,
           (unsigned long long)h.twinA, (unsigned long long)h.twinB, h.wrong, h.dropped, h.addr,
           h.nanAddr, h.reads);
    CHECK(memcmp(h.magic, RD_SKIN_CAPTURE_MAGIC, 8) == 0 && h.version == RD_SKIN_CAPTURE_VERSION &&
              h.headerBytes == sizeof(h),
          "the header's magic, version or size");
    CHECK((h.flags & 0x3Bu) == 0x3Bu,
          "flags 0x%x: the check's data, the twins' counts, wrong, "
          "the twins differed, targeted expected",
          h.flags);
    CHECK(h.twinA != h.twinB && h.twinA != UINT64_MAX && h.twinB != UINT64_MAX, "twins %llu / %llu",
          (unsigned long long)h.twinA, (unsigned long long)h.twinB);
    CHECK(h.list == 0 && h.probeVertices == NV && h.cpuBytes == 3840u && h.gpuBytes == 3840u &&
              h.memBytes == 36u * 16u && h.streamBytes == NV * 80u && h.rowsBytes > 0,
          "list %u, %u vertices, sections cpu %u gpu %u mem %u stream %u rows %u", h.list,
          h.probeVertices, h.cpuBytes, h.gpuBytes, h.memBytes, h.streamBytes, h.rowsBytes);
    CHECK((uint64_t)h.rowsOff + h.rowsBytes <= n && (uint64_t)h.cpuOff + 3840u <= n &&
              (uint64_t)h.gpuOff + 3840u <= n,
          "a section past the file's end");
    if ((uint64_t)h.cpuOff + 3840u <= n && (uint64_t)h.gpuOff + 3840u <= n) {
        uint32_t q0 = 0, q1 = 0;
        const bool d = rd__skin_bytes_differ(buf + h.cpuOff, buf + h.gpuOff, 3840u, &q0, &q1);
        CHECK(d && q0 == 184u && q1 == 187u, "the CPU's bytes and the GPU's copy differ at %u..%u",
              q0, q1);
        CHECK(memcmp(buf + h.cpuOff, s_bones, 3840u) == 0, "the CPU's bytes are not the bones");
    }
}

static int deviceTests(const char *mode, const char *folder)
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
    const bool thin = strcmp(mode, "thin") == 0;
    const bool stale = strcmp(mode, "stale") == 0;
    const bool twins = strcmp(mode, "twin") == 0 || strcmp(mode, "capture") == 0 || thin;
    if (twins) {
        rd__vu_skin_twins_set(1);
        if (folder) {
            rd_set_vu_skin_capture_dir(folder);
        }
    }
    block(s_mem);
    bones(s_bones);
    stream(s_stream);
    /* 7. */
    CHECK(rd__vu_skin_selftest(), "the start-up probe is wrong on this device");
    /* 8. */
    RdMesh mesh = makeMesh();
    static RdFrame f;
    CHECK(mesh.id != 0, "the mesh");
    if (mesh.id && thin && buildFrame(&f, mesh, 3)) {
        thinTests(&f);
        rd__frame_free(&f);
    } else if (mesh.id && stale && buildFrame(&f, mesh, 1)) {
        /* 22. the careful rule (the window's after a start-up set stopped
         * for memory on Qualcomm's own driver): the box check's pipeline is
         * still compiled in the background, and the box check drawn */
        rd_vu_skin_background(memoryFine, "the test's careful rule");
        uint32_t box = 0;
        for (int i = 0; i < 200 && box == 0; i++) {
            RdSkinSummary sb;
            (void)rd__vu_skin_check_replays(&f, 0, 1, &sb);
            box += sb.boxChecks;
            if (box == 0) {
                sleepMs(10);
            }
        }
        printf("  22. the careful rule: %u box checks drawn\n", box);
        CHECK(box > 0, "no box check drawn under the careful rule");
        rd_vu_skin_background(NULL, NULL);
        /* 21. each slot's readback written by its first check alone (both
         * written in 22.): the four checks read as not written by their
         * replay */
        rd__vu_skin_sabotage_copy(true);
        RdSkinSummary s;
        const int rc = rd__vu_skin_check_replays(&f, 0, 4, &s);
        rd__vu_skin_sabotage_copy(false);
        printf("  21. stale: %u checks, %u wrong, %u not written by their replay, %u ring "
               "stamps old\n",
               s.checks, s.wrong, s.unwritten, s.ringOld);
        CHECK(rc >= 0 && s.checks == 4 && s.unwritten == 4 && s.ringOld == 0,
              "rc %d, %u checks, %u unwritten, %u ring stamps old", rc, s.checks, s.unwritten,
              s.ringOld);
        /* written again: both stamps right */
        const int rc2 = rd__vu_skin_check_replays(&f, 0, 2, &s);
        CHECK(rc2 >= 0 && s.checks == 2 && s.unwritten == 0 && s.ringOld == 0 && s.wrong == 0,
              "rc %d, %u checks, %u unwritten, %u ring stamps old, %u wrong", rc2, s.checks,
              s.unwritten, s.ringOld, s.wrong);
        rd__frame_free(&f);
    } else if (mesh.id && twins && !thin && buildFrame(&f, mesh, 1)) {
        /* 11., 12. */
        const char *sab = getenv("ICO_RD_VU_SKIN_SABOTAGE");
        const bool nan = sab && strstr(sab, "nan");
        RdSkinSummary s1, s2;
        const int rc1 = rd__vu_skin_check_replays(&f, 0, 1, &s1);
        printf("  11. %s, the first replay: %u twins, %u differ (%u through the draw's own "
               "pipeline), %u checks\n",
               mode, s1.twins, s1.twinsDiffer, s1.twinsOwn, s1.checks);
        CHECK(rc1 >= 0 && s1.twins == 1 && s1.twinsDiffer == 1 && s1.checks == 0,
              "rc %d, %u twins, %u differ, %u checks", rc1, s1.twins, s1.twinsDiffer, s1.checks);
        const int rc2 = rd__vu_skin_check_replays(&f, 0, 2, &s2);
        printf("  12. %s, the next replays: %u checks, %u targeted, %u of them wrong, address %u, "
               "%u dropped; reads as: %s\n",
               mode, s2.checks, s2.targeted, s2.targetedWrong, s2.lastAddr, s2.dropped,
               s2.decision[0] ? s2.decision : "-");
        CHECK(rc2 >= 0 && s2.targeted >= 1 && s2.targetedWrong >= 1 && s2.lastAddr == 200,
              "rc %d, %u targeted, %u wrong, address %u", rc2, s2.targeted, s2.targetedWrong,
              s2.lastAddr);
        CHECK(!nan || s2.dropped > 0, "a NaN bone: no wrong vertex dropped");
        char path[1200];
        const bool due = rd_vu_skin_capture_due(path, sizeof(path));
        CHECK(due, "no capture asked");
        if (due && folder) {
            captureCheck(path);
        }
        CHECK(!rd_vu_skin_capture_due(path, sizeof(path)), "a second capture asked");
        rd__frame_free(&f);
    } else if (mesh.id && !twins && buildFrame(&f, mesh, 1)) {
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
        /* the trace's stamps: the readback is this replay's */
        CHECK(s.unwritten == 0 && s.ringOld == 0, "%u readbacks not written, %u ring stamps old",
              s.unwritten, s.ringOld);
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
            /* the copy is the sabotaged ring's bytes at the draw's offset:
             * the search finds them in the check's own ring */
            CHECK(s.lastFound == RD_SKIN_FOUND_RING, "the differing copy found as %d", s.lastFound);
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
        printf("usage: rd_vu_skin_check_test <judge|clean|probe|ring|twin|capture|thin|stale> "
               "[folder]\n");
        return 2;
    }
    const int rc = strcmp(argv[1], "judge") == 0 ? judgeTests()
                                                 : deviceTests(argv[1], argc > 2 ? argv[2] : NULL);
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
