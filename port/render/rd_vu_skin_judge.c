/* rd_vu_skin_judge.c: the skinned draws' bone-block check's judgement
 * (rd_vu_skin_check.h; docs/port/VU_SKIN_CHECK.md): the CPU's positions of
 * a skinned draw's vertices through vu1_ref's cluster program, a probe's
 * positions against them, the bone address whose zero matrix explains a
 * wrong vertex, the byte windows, and the decision table's words.  No
 * device: the tests run these alone (rd_vu_skin_check_test.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_vu_skin_check.h"
#include "vu1_ref.h"

/* the reference's state and one batch's output (VuBatchOut is large: kept
 * once; the judgement runs on one thread at a time, the replay's) */
static Vu1Ref *s_ref;
static VuBatchOut *s_out;
static float (*s_in)[4];

static bool refAlloc(void)
{
    if (!s_ref) {
        s_ref = malloc(sizeof(*s_ref));
        s_out = malloc(sizeof(*s_out));
        s_in = malloc(sizeof(float[4]) * (1u + VU_BATCH_MAX * 5u));
    }
    return s_ref && s_out && s_in;
}

/* ftoi4's saturated values (vu1_ref's vu_ftoi4, the VU's) */
static bool saturated(int32_t v)
{
    return v == INT32_MAX || v == INT32_MIN;
}

bool rd__skin_reference(const RdSkinCase *k, uint32_t zeroAddr, RdSkinPos *out)
{
    if (!k || !out || k->count == 0 || k->qpv != 5u || !refAlloc()) {
        return false;
    }
    memset(out, 0, sizeof(*out) * k->count);
    vu1ref_init(s_ref);
    vu1ref_load_common(s_ref, (const float (*)[4])k->mem);
    /* SET_CLUSTER_MATRIX: all 240 qwords from qw[1] to VU memory 16..255
     * (the zeros above the bones the payload holds are VuBoneCB's own);
     * qw[0].w the fade to mem[2].w (rd_stream_audit_test.c's form) */
    static float pk[242][4];
    memset(pk, 0, sizeof(pk));
    const int32_t n = 240;
    memcpy(&pk[0][0], &n, 4);
    pk[0][3] = k->mem[2][3];
    memcpy(pk[1], k->bones, 240u * 16u);
    if (zeroAddr >= 16u && zeroAddr <= 252u) {
        /* the shader's index (vu_skin.hlsl vu_bone_mat: min(addr - 16, 236)) */
        const uint32_t q = zeroAddr - 16u > 236u ? 236u : zeroAddr - 16u;
        memset(pk[1 + q], 0, 4u * 16u);
    }
    vu1ref_cluster_set_matrix(s_ref, (const float (*)[4])pk);
    vu1ref_cluster_set_light(s_ref, (const float (*)[4])(k->mem + 28));
    for (uint32_t b = 0; b < k->batchCount; b++) {
        const RdSkinBatch *br = &k->batches[b];
        if (br->count == 0) {
            continue;
        }
        if (br->count > VU_BATCH_MAX || (uint64_t)br->first + br->count > k->count) {
            return false;
        }
        /* the batch at TOP: the common tag (VU memory 3) with NLOOP the
         * vertices, then the vertices */
        memcpy(s_in[0], k->mem[3], 16);
        uint32_t t0;
        memcpy(&t0, &s_in[0][0], 4);
        t0 = (t0 & ~0x7FFFu) | (br->count & 0x7FFFu);
        memcpy(&s_in[0][0], &t0, 4);
        memcpy(s_in[1], k->stream[(size_t)br->first * k->qpv], (size_t)br->count * k->qpv * 16u);
        memset(s_out, 0, sizeof(*s_out));
        vu1ref_cluster(s_ref, k->code, (const float (*)[4])s_in, s_out);
        for (int i = 0; i < s_out->count && (uint32_t)i < br->count; i++) {
            const VuGsVertex *v = &s_out->v[i];
            RdSkinPos *o = &out[br->first + (uint32_t)i];
            o->x = v->xyz[0];
            o->y = v->xyz[1];
            o->z = v->xyz[2];
            o->inBatch = 1;
            /* in front of the eye and nothing saturated: the shader's and
               the reference's divides agree there (vu1_test.c's bounds) */
            o->compared = v->clipW > 1e-3f && v->clipW == v->clipW && !saturated(v->xyz[0]) &&
                          !saturated(v->xyz[1]) && !saturated(v->xyz[2]);
        }
    }
    return true;
}

bool rd__skin_pos_near(const int32_t got[3], const RdSkinPos *want)
{
    const int64_t dx = (int64_t)got[0] - want->x, dy = (int64_t)got[1] - want->y;
    const double dz = fabs((double)got[2] - (double)want->z);
    double zt = fabs((double)want->z) * RD_SKIN_Z_REL;
    zt = zt < RD_SKIN_XY_TOL ? RD_SKIN_XY_TOL : zt;
    return llabs(dx) <= RD_SKIN_XY_TOL && llabs(dy) <= RD_SKIN_XY_TOL && dz <= zt;
}

/* the candidate addresses: the weight lanes (x and z, integers) of the
 * wrong vertices, each once, the most frequent first */
#define CAND_MAX 8

typedef struct Cand {
    uint32_t addr, votes;
} Cand;

static void candAdd(Cand *c, uint32_t *n, uint32_t addr)
{
    if (addr < 16u || addr > 252u) {
        return;
    }
    for (uint32_t i = 0; i < *n; i++) {
        if (c[i].addr == addr) {
            c[i].votes++;
            return;
        }
    }
    if (*n < CAND_MAX) {
        c[*n].addr = addr;
        c[*n].votes = 1;
        (*n)++;
    }
}

void rd__skin_judge(const RdSkinCase *k, const RdSkinPos *ref, const int32_t (*got)[3],
                    RdSkinVerdict *v)
{
    memset(v, 0, sizeof(*v));
    v->firstWrong = UINT32_MAX;
    Cand cand[CAND_MAX];
    uint32_t nc = 0;
    for (uint32_t i = 0; i < k->count; i++) {
        if (!ref[i].compared) {
            continue;
        }
        v->compared++;
        if (rd__skin_pos_near(got[i], &ref[i])) {
            const int32_t dx = abs(got[i][0] - ref[i].x), dy = abs(got[i][1] - ref[i].y);
            const int32_t d = dx > dy ? dx : dy;
            v->maxRightXY = d > v->maxRightXY ? d : v->maxRightXY;
            continue;
        }
        uint32_t lane[4];
        memcpy(lane, k->stream[(size_t)i * k->qpv + 2u], sizeof(lane));
        if (v->wrong++ == 0) {
            v->firstWrong = i;
            memcpy(v->got, got[i], sizeof(v->got));
            v->want = ref[i];
            v->lanes[0] = lane[0];
            v->lanes[1] = lane[2];
        }
        candAdd(cand, &nc, lane[0]);
        candAdd(cand, &nc, lane[2]);
    }
    if (v->wrong == 0 || nc == 0) {
        return;
    }
    /* the most frequent first (a stable order: the votes, then the address) */
    for (uint32_t i = 1; i < nc; i++) {
        for (uint32_t j = i;
             j > 0 && (cand[j].votes > cand[j - 1].votes ||
                       (cand[j].votes == cand[j - 1].votes && cand[j].addr < cand[j - 1].addr));
             j--) {
            const Cand t = cand[j];
            cand[j] = cand[j - 1];
            cand[j - 1] = t;
        }
    }
    RdSkinPos *zero = malloc(sizeof(*zero) * k->count);
    if (!zero) {
        return;
    }
    for (uint32_t c = 0; c < nc; c++) {
        if (!rd__skin_reference(k, cand[c].addr, zero)) {
            break;
        }
        uint32_t gives = 0;
        bool first = false;
        for (uint32_t i = 0; i < k->count; i++) {
            if (!ref[i].compared || rd__skin_pos_near(got[i], &ref[i])) {
                continue;
            }
            /* a zero matrix can move the point behind the eye or saturate
             * it: compared there all the same, by its X, Y and Z */
            if (rd__skin_pos_near(got[i], &zero[i])) {
                gives++;
                first = first || i == v->firstWrong;
            }
        }
        if (gives > v->explained) {
            v->explained = gives;
            v->addr = cand[c].addr;
            v->explains = first;
        }
    }
    free(zero);
}

bool rd__skin_bytes_differ(const void *a, const void *b, uint32_t bytes, uint32_t *q0, uint32_t *q1)
{
    const uint8_t *x = a, *y = b;
    bool any = false;
    *q0 = *q1 = 0;
    for (uint32_t i = 0; i < bytes; i++) {
        if (x[i] != y[i]) {
            if (!any) {
                *q0 = i / 16u;
            }
            *q1 = i / 16u;
            any = true;
        }
    }
    return any;
}

void rd__skin_bytes_words(bool differ, uint32_t q0, uint32_t q1, char *out, size_t size)
{
    if (!differ) {
        snprintf(out, size, "equal");
    } else if (q0 == q1) {
        snprintf(out, size, "differs at qword %u", q0);
    } else {
        snprintf(out, size, "differs at qwords %u..%u", q0, q1);
    }
}

const char *rd__skin_decision(bool copyDiffers, bool rereadDiffers, bool probeWrong, int c1, int c2)
{
    if (rereadDiffers) {
        return "H2, a port race: the ring's bytes changed after the draw was recorded (the ring "
               "written while the GPU used it)";
    }
    if (copyDiffers) {
        return "H2, the memory: the GPU read other bytes than the CPU wrote and still holds";
    }
    if (!probeWrong) {
        return "the probe drew right: a wrong picture would be the scene pass's execution, not "
               "the bone read (see the box check)";
    }
    if (c1 == RD_SKIN_VAR_RIGHT && c2 == RD_SKIN_VAR_RIGHT) {
        return "H1, address-dependent: the driver's uniform read at that offset (right at a "
               "fresh offset, and right without uniform robustness)";
    }
    if (c1 == RD_SKIN_VAR_RIGHT) {
        return "H1, address-dependent: the driver's uniform read at that offset";
    }
    if (c2 == RD_SKIN_VAR_RIGHT) {
        return "H1 under robust access only: the driver's uniform read with robustness on";
    }
    if (c1 == RD_SKIN_VAR_WRONG && c2 == RD_SKIN_VAR_WRONG) {
        return "H1, the driver's uniform read of this draw: wrong at a fresh offset too, and "
               "not robust access alone";
    }
    if (c1 == RD_SKIN_VAR_WRONG) {
        return "H1, the driver's uniform read of this draw: wrong at a fresh offset too "
               "(robustness off not tried)";
    }
    return "H1 or the robust access: the bytes are right and the read is wrong (no variant "
           "ran)";
}

int rd__skin_check_env(const char *value)
{
    if (!value || !value[0]) {
        return -1;
    }
    if (strcmp(value, "2") == 0) {
        return 2;
    }
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0 ||
           strcmp(value, "yes") == 0;
}

int rd__skin_check_default(bool windowed, bool adreno, int env)
{
    if (env >= 0) {
        return env;
    }
    /* issue 163's devices only: on any other the check's GPU work is not
     * spent (issue 179's Mali-G52 is GPU-bound already) */
    return windowed && adreno ? 1 : 0;
}
