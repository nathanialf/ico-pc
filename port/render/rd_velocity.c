/*
 * port/render/rd_velocity.c
 *
 * The scene's motion vectors (docs/port/RENDER.md, "Velocity";
 * rd_velocity.h): the source rule on the CPU (rd__velocity_attach: each VU
 * draw's VuPrevCB, the transform its object had in the picture drawn before)
 * and the GPU side's resources (the VELOCITY texture, the velocity pass's
 * pipeline, the read back).  The pre-pass's second draw of each command and
 * the velocity pass itself are rd_replay_vu.c's (vuVelPrepass,
 * rd__vu_velocity_pass).
 *
 * The picture before is kept as a list of its VU draws with the fixed part
 * of each one's payload (s_snap: two buffers, the one being made and the
 * one read, swapped after each frame): a frame's draws are paired against
 * it as rd_interp.c pairs a draw with the tick before's (matchOf,
 * placeSlot), then it is replaced by this frame's.  Every frame given here
 * is the picture of one present, so with the interpolation the picture
 * before is the last present's blended frame (the motion between two
 * presents, however many ticks apart), without it the tick before's.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_camera_blend.h"
#include "rd_thread.h"
#include "rd_velocity.h"

/* ------------------------------------------------------------ the switch */

static int s_on = -1; /* -1: ICO_RD_VELOCITY not read yet */
static int s_signRead;
static float s_sign = 1.0f;
static bool s_reset = true;

static bool envTrue(const char *e)
{
    return e && (strcmp(e, "1") == 0 || strcmp(e, "true") == 0 || strcmp(e, "on") == 0);
}

bool rd__velocity_on(void)
{
    if (s_on < 0) {
        s_on = envTrue(getenv("ICO_RD_VELOCITY")) ? 1 : 0;
        if (s_on) {
            rd__log("velocity: on ([dev] velocity_test, ICO_RD_VELOCITY=1): every VU draw "
                    "carries its previous picture's transform and the replay draws the "
                    "scene's motion vectors (a developer switch; nothing reads them yet)");
        }
    }
    return s_on == 1;
}

void rd__velocity_set(bool on)
{
    const int want = on ? 1 : 0;
    if (s_on != want) {
        s_on = want;
        rd__velocity_forget();
    }
}

float rd__velocity_sign(void)
{
    if (!s_signRead) {
        s_signRead = 1;
        const char *e = getenv("ICO_RD_VELOCITY_SIGN");
        if (e && strcmp(e, "-1") == 0) {
            s_sign = -1.0f;
            rd__log("velocity: [dev] velocity_sign = -1: the upscaler takes the motion vectors "
                    "negated (previous to current)");
        }
    }
    return s_sign;
}

bool rd__velocity_reset(void)
{
    return s_reset;
}

/* ------------------------------------------------------- the stats, log */

static RdVelocityStats s_st;

/* the block's sums (the line every 10 s while on) */
static struct {
    double at;
    uint64_t frames, draws, paired, moved, unpaired, shape, jump, resets;
    uint64_t drawn, noRecord, passes;
} s_blk;

const RdVelocityStats *rd__velocity_stats(void)
{
    return &s_st;
}

static void blockLog(void)
{
    const double now = rd__now_ms();
    if (s_blk.at == 0.0) {
        s_blk.at = now;
        return;
    }
    if (now - s_blk.at < 10000.0) {
        return;
    }
    s_blk.at = now;
    if (s_blk.frames == 0) {
        return;
    }
    rd__log("velocity: %llu pictures, VU draws %llu: with their previous transform %llu (moved "
            "%llu), without %llu (no pair %llu, another shape %llu, a jump %llu), new histories "
            "%llu; replays drew %llu into VELOCITY, %llu of list 0 left at 0 (no record or "
            "multisampled), %llu passes",
            (unsigned long long)s_blk.frames, (unsigned long long)s_blk.draws,
            (unsigned long long)s_blk.paired, (unsigned long long)s_blk.moved,
            (unsigned long long)(s_blk.unpaired + s_blk.shape + s_blk.jump),
            (unsigned long long)s_blk.unpaired, (unsigned long long)s_blk.shape,
            (unsigned long long)s_blk.jump, (unsigned long long)s_blk.resets,
            (unsigned long long)s_blk.drawn, (unsigned long long)s_blk.noRecord,
            (unsigned long long)s_blk.passes);
    const double keep = s_blk.at;
    memset(&s_blk, 0, sizeof(s_blk));
    s_blk.at = keep;
}

void rd__velocity_note_replay(uint32_t drawn, uint32_t noRecord)
{
    s_st.drawn = drawn;
    s_st.noRecord = noRecord;
    s_st.passes = 1;
    s_blk.drawn += drawn;
    s_blk.noRecord += noRecord;
    s_blk.passes++;
}

/* ------------------------------------------------------ the draws' shape */

static bool isVelDraw(const RdCmd *c)
{
    return (c->type == RDC_MESH || c->type == RDC_SKINNED || c->type == RDC_GRID) &&
           (c->keyLo != 0 || c->keyHi != 0);
}

static bool isNormalProg(uint8_t prog)
{
    return prog == RD_PROG_PRELIT || prog == RD_PROG_LIT || prog == RD_PROG_LIT_SPEC ||
           prog == RD_PROG_REFLECT;
}

/* draw c's header when its payload holds its fixed part */
static bool headerOf(const RdFrame *f, const RdCmd *c, RdVuPayload *h)
{
    const uint64_t off = c->u[1], size = c->u[2];
    if (size < sizeof(RdVuPayload) + sizeof(RdVuBlock) || off + size > f->payloadSize) {
        return false;
    }
    memcpy(h, f->payload + off, sizeof(*h));
    return rd__velocity_prev_size(h) <= size;
}

const uint8_t *rd__velocity_prev(const RdFrame *f, const RdCmd *c)
{
    if (!f || !c || !isVelDraw(c) || (c->u[3] & RD_VU_PREV) == 0u) {
        return NULL;
    }
    RdVuPayload h, p;
    if (!headerOf(f, c, &h)) {
        return NULL;
    }
    const uint64_t off = c->u[3] & RD_VU_PREV_OFF, size = rd__velocity_prev_size(&h);
    if (off + size > f->payloadSize) {
        return NULL;
    }
    memcpy(&p, f->payload + off, sizeof(p));
    if (p.boneQw != h.boneQw || p.streamQw != h.streamQw || p.firstBatch != h.firstBatch ||
        p.batchCount != h.batchCount || p.vertsPerBatch != h.vertsPerBatch ||
        p.qwPerVertex != h.qwPerVertex) {
        return NULL;
    }
    return f->payload + off;
}

/* the model origin in the world (rd_interp.c vuWorldOrigin: the world to
 * screen matrix, qw 4..7, inverted, applied to the model to screen
 * translation, qw 19) */
static bool worldOrigin(const float (*m)[4], float out[3])
{
    double a[16], inv[16], w[4] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < 16; i++) {
        a[i] = m[4 + i / 4][i % 4];
    }
    if (!rdcb_invert4d(a, inv)) {
        return false;
    }
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            w[r] += inv[k * 4 + r] * m[19][k];
        }
    }
    if (!(fabs(w[3]) > 1e-12)) {
        return false;
    }
    for (int j = 0; j < 3; j++) {
        out[j] = (float)(w[j] / w[3]);
    }
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

static float dist3(const float a[3], const float b[3])
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* rd_interp.c sameOrigin: one still object's origins through two cameras */
static bool sameOrigin(const float a[3], const float b[3])
{
    const float m = fmaxf(fmaxf(fabsf(a[0]), fabsf(a[1])), fmaxf(fabsf(a[2]), 1.0f));
    return dist3(a, b) <= 0.01f + 2e-5f * m;
}

/* -------------------------------------------------- the picture before */

typedef struct Ent {
    uint32_t keyLo, keyHi;
    uint8_t type, list, org, taken;
    uint8_t prog, pad[3];
    uint32_t mesh; /* u[0] */
    uint32_t seq;  /* its place in the frame */
    uint32_t off;  /* its fixed part in the buffer's bytes (cur's side: the payload offset) */
    uint32_t idx;  /* cur's side: the command's index in its list */
    int32_t pick;  /* cur's side: the entry of the picture before it pairs with, -1 none */
    float at[3];
} Ent;

typedef struct Snap {
    Ent *e;
    uint32_t n, cap;
    uint8_t *b;
    uint32_t bn, bcap;
} Snap;

static Snap s_snap[2];
static int s_read;  /* s_snap[s_read]: the picture before */
static bool s_have; /* it holds one */
static Snap s_cur;  /* this frame's draws (Ent.off its payload offsets) */
static uint32_t *s_ord[2];
static uint32_t s_ordCap[2];

void rd__velocity_forget(void)
{
    s_have = false;
}

static bool grow(void **p, uint32_t *cap, uint32_t n, size_t size)
{
    if (n <= *cap) {
        return true;
    }
    uint32_t c = *cap ? *cap : 256u;
    while (c < n) {
        c *= 2u;
    }
    void *q = realloc(*p, (size_t)c * size);
    if (!q) {
        return false;
    }
    *p = q;
    *cap = c;
    return true;
}

static int entCmp(const Ent *x, const Ent *y)
{
    if (x->list != y->list) {
        return x->list < y->list ? -1 : 1;
    }
    if (x->type != y->type) {
        return x->type < y->type ? -1 : 1;
    }
    if (x->keyHi != y->keyHi) {
        return x->keyHi < y->keyHi ? -1 : 1;
    }
    if (x->keyLo != y->keyLo) {
        return x->keyLo < y->keyLo ? -1 : 1;
    }
    return 0;
}

static const Ent *s_sortBase;

static int ordCmp(const void *a, const void *b)
{
    const Ent *x = &s_sortBase[*(const uint32_t *)a], *y = &s_sortBase[*(const uint32_t *)b];
    const int k = entCmp(x, y);
    if (k != 0) {
        return k;
    }
    return x->seq < y->seq ? -1 : (x->seq > y->seq ? 1 : 0);
}

/* the entries of s in key order (then their order in the frame) */
static bool sortOf(const Snap *s, int which)
{
    if (!grow((void **)&s_ord[which], &s_ordCap[which], s->n ? s->n : 1u, sizeof(uint32_t))) {
        return false;
    }
    for (uint32_t i = 0; i < s->n; i++) {
        s_ord[which][i] = i;
    }
    if (s->n > 1u) {
        s_sortBase = s->e;
        qsort(s_ord[which], s->n, sizeof(uint32_t), ordCmp);
    }
    return true;
}

/* the frame's keyed mesh, skinned and grid draws, their u[3] cleared of an
 * older VuPrevCB; false on out of memory */
static bool collect(RdFrame *f, int keep)
{
    s_cur.n = 0;
    uint32_t seq = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            RdCmd *c = &f->lists[l].cmds[i];
            if (c->type == RDC_MESH || c->type == RDC_SKINNED || c->type == RDC_GRID) {
                c->u[3] &= RD_VU_TICK_CAMERAS;
            }
            RdVuPayload h;
            if (!isVelDraw(c) || !headerOf(f, c, &h)) {
                continue;
            }
            if (!grow((void **)&s_cur.e, &s_cur.cap, s_cur.n + 1u, sizeof(Ent))) {
                return false;
            }
            Ent *e = &s_cur.e[s_cur.n++];
            memset(e, 0, sizeof(*e));
            e->keyLo = c->keyLo;
            e->keyHi = c->keyHi;
            e->type = c->type;
            e->list = (uint8_t)l;
            e->prog = c->b[0];
            e->mesh = c->u[0];
            e->seq = seq++;
            e->off = c->u[1];
            e->idx = i;
            e->pick = -1;
            float m[36][4];
            memcpy(m, f->payload + c->u[1] + sizeof(RdVuPayload), sizeof(m));
            e->org = worldOrigin((const float (*)[4])m, e->at) ? 1u : 0u;
        }
    }
    return true;
}

/* rd_interp.c canPlace: the meshes and grids of a key drawn several times
 * pair by place (MicroCode.c keys particles by emitter; skinned by ordinal) */
static bool canPlace(uint8_t type)
{
    return type == RDC_MESH || type == RDC_GRID;
}

/* pairs the cur group g[0..ng) with the picture before's group p[0..np)
 * (indices into s_cur.e and the snapshot's entries, each in frame order) */
static void pairGroup(const uint32_t *g, uint32_t ng, const uint32_t *p, uint32_t np, Ent *pe)
{
    Ent *ce = s_cur.e;
    if (!(canPlace(ce[g[0]].type) && np >= 2u)) {
        /* the ordinal: the n-th with the n-th */
        for (uint32_t k = 0; k < ng && k < np; k++) {
            ce[g[k]].pick = (int32_t)p[k];
        }
        return;
    }
    /* place first: the draws at the same origin (the still instances) */
    uint32_t same = 0;
    for (uint32_t k = 0; k < ng; k++) {
        Ent *c = &ce[g[k]];
        if (!c->org) {
            continue;
        }
        for (uint32_t j = 0; j < np; j++) {
            Ent *q = &pe[p[j]];
            if (!q->taken && q->org && sameOrigin(q->at, c->at)) {
                q->taken = 1;
                c->pick = (int32_t)p[j];
                same++;
                break;
            }
        }
    }
    if (same == 0 && ng == np) {
        for (uint32_t k = 0; k < ng; k++) {
            ce[g[k]].pick = (int32_t)p[k]; /* none still, their number held: the ordinal */
        }
        return;
    }
    /* the leftovers: two that are each other's nearest are one instance
     * that moved; any other is a new one (rd_interp.c pairLeftovers) */
    for (uint32_t k = 0; k < ng; k++) {
        Ent *c = &ce[g[k]];
        if (c->pick >= 0 || !c->org) {
            continue;
        }
        int32_t best = -1;
        float bd = 0.0f;
        for (uint32_t j = 0; j < np; j++) {
            const Ent *q = &pe[p[j]];
            if (!q->taken && q->org && (best < 0 || dist3(q->at, c->at) < bd)) {
                best = (int32_t)j;
                bd = dist3(q->at, c->at);
            }
        }
        if (best < 0) {
            continue;
        }
        const Ent *q = &pe[p[best]];
        bool mutual = true;
        for (uint32_t i = 0; i < ng && mutual; i++) {
            const Ent *o = &ce[g[i]];
            mutual = o == c || o->pick >= 0 || !o->org || dist3(q->at, o->at) >= bd;
        }
        if (mutual) {
            pe[p[best]].taken = 1;
            c->pick = (int32_t)p[best];
        }
    }
}

/* the shape the two draws must share (rd_interp.c blendVu's header test,
 * sameMesh) */
static bool sameShape(const Ent *c, const RdVuPayload *hc, const Ent *q, const RdVuPayload *hq)
{
    if (c->prog != q->prog || hc->firstBatch != hq->firstBatch ||
        hc->batchCount != hq->batchCount || hc->boneQw != hq->boneQw ||
        hc->streamQw != hq->streamQw || hc->vertsPerBatch != hq->vertsPerBatch ||
        hc->qwPerVertex != hq->qwPerVertex) {
        return false;
    }
    if (c->type == RDC_GRID || c->mesh == q->mesh) {
        return true;
    }
    const RdMeshRec *x = rd__mesh_rec(c->mesh), *y = rd__mesh_rec(q->mesh);
    return x && y && x->vu && y->vu && x->vertexCount == y->vertexCount &&
           x->qwPerVertex == y->qwPerVertex && x->batchCount == y->batchCount &&
           x->indexCount == y->indexCount;
}

/* the object jumped in the interval (rd_interp.c's jump tests): its
 * velocity would be a streak across the screen, so it has none */
static bool jumped(const Ent *c, const uint8_t *cp, const Ent *q, const uint8_t *qp,
                   const RdVuPayload *h)
{
    if (c->type == RDC_SKINNED) {
        if (h->boneQw < 4u) {
            return false;
        }
        const size_t at = sizeof(RdVuPayload) + sizeof(RdVuBlock) + 3u * 16u;
        float a[4], b[4];
        memcpy(a, cp + at, sizeof(a));
        memcpy(b, qp + at, sizeof(b));
        return fabsf(a[3] - 1.0f) < 1e-3f && fabsf(b[3] - 1.0f) < 1e-3f &&
               dist3(a, b) > RD_INTERP_JUMP_WORLD;
    }
    if (c->type == RDC_MESH && !isNormalProg(c->prog)) {
        return false;
    }
    return c->org && q->org && dist3(c->at, q->at) > RD_INTERP_JUMP_WORLD;
}

/* n bytes appended to f's payload, 8-aligned; false on out of memory */
static bool payloadAppend(RdFrame *f, const void *data, uint32_t n, uint32_t *off)
{
    const uint64_t at = ((uint64_t)f->payloadSize + 7u) & ~(uint64_t)7u;
    const uint64_t end = at + n;
    if (end > 0x80000000ull) {
        return false;
    }
    if (end > f->payloadCap) {
        uint64_t cap = f->payloadCap ? f->payloadCap : 64u * 1024u;
        while (cap < end) {
            cap *= 2u;
        }
        uint8_t *p = realloc(f->payload, (size_t)cap);
        if (!p) {
            return false;
        }
        f->payload = p;
        f->payloadCap = (uint32_t)cap;
    }
    if (at > f->payloadSize) {
        memset(f->payload + f->payloadSize, 0, (size_t)(at - f->payloadSize));
    }
    memcpy(f->payload + at, data, n);
    f->payloadSize = (uint32_t)end;
    *off = (uint32_t)at;
    return true;
}

/* this frame's draws into the other buffer, which becomes the picture
 * before; false on out of memory (the next frame then starts a history) */
static bool keepFrame(const RdFrame *f)
{
    Snap *s = &s_snap[s_read ^ 1];
    s->n = 0;
    s->bn = 0;
    for (uint32_t i = 0; i < s_cur.n; i++) {
        const Ent *c = &s_cur.e[i];
        RdVuPayload h;
        memcpy(&h, f->payload + c->off, sizeof(h));
        const uint32_t size = (uint32_t)rd__velocity_prev_size(&h);
        if (!grow((void **)&s->e, &s->cap, s->n + 1u, sizeof(Ent)) ||
            !grow((void **)&s->b, &s->bcap, s->bn + size, 1u)) {
            return false;
        }
        Ent *e = &s->e[s->n++];
        *e = *c;
        e->off = s->bn;
        e->taken = 0;
        e->pick = -1;
        memcpy(s->b + s->bn, f->payload + c->off, size);
        s->bn += (size + 7u) & ~7u; /* the next grow covers the padding */
    }
    s_read ^= 1;
    return true;
}

void rd__velocity_attach(RdFrame *f, int keep)
{
    memset(&s_st, 0, sizeof(s_st));
    if (!f || !rd__velocity_on()) {
        return;
    }
    if (keep) {
        /* lists 11 and 12 over the kept picture: no scene, nothing to pair,
           the picture before kept for the next scene */
        return;
    }
    const bool cut = f->hasCamera && f->camera.cut != 0;
    s_reset = !s_have || cut;
    s_st.reset = s_reset ? 1u : 0u;
    if (!collect(f, keep)) {
        s_have = false;
        static uint32_t lines; /* the first 8 */
        if (lines++ < 8u) {
            rd__log("velocity: out of memory: no motion vectors this frame");
        }
        return;
    }
    s_st.vuDraws = s_cur.n;
    Snap *pv = &s_snap[s_read];
    if (!s_reset && s_cur.n && pv->n && sortOf(&s_cur, 0) && sortOf(pv, 1)) {
        for (uint32_t i = 0; i < pv->n; i++) {
            pv->e[i].taken = 0;
        }
        /* walk both in key order, a group at a time */
        uint32_t a = 0, b = 0;
        while (a < s_cur.n) {
            const Ent *ka = &s_cur.e[s_ord[0][a]];
            uint32_t ae = a + 1u;
            while (ae < s_cur.n && entCmp(&s_cur.e[s_ord[0][ae]], ka) == 0) {
                ae++;
            }
            while (b < pv->n && entCmp(&pv->e[s_ord[1][b]], ka) < 0) {
                b++;
            }
            uint32_t be = b;
            while (be < pv->n && entCmp(&pv->e[s_ord[1][be]], ka) == 0) {
                be++;
            }
            if (be > b) {
                pairGroup(&s_ord[0][a], ae - a, &s_ord[1][b], be - b, pv->e);
            }
            a = ae;
            b = be;
        }
    }
    for (uint32_t i = 0; i < s_cur.n; i++) {
        const Ent *c = &s_cur.e[i];
        if (s_reset) {
            continue;
        }
        if (c->pick < 0) {
            s_st.unpaired++;
            continue;
        }
        const Ent *q = &pv->e[c->pick];
        RdVuPayload hc, hq;
        memcpy(&hc, f->payload + c->off, sizeof(hc));
        memcpy(&hq, pv->b + q->off, sizeof(hq));
        if (!sameShape(c, &hc, q, &hq)) {
            s_st.shape++;
            continue;
        }
        if (jumped(c, f->payload + c->off, q, pv->b + q->off, &hc)) {
            s_st.jump++;
            continue;
        }
        const uint32_t size = (uint32_t)rd__velocity_prev_size(&hq);
        uint32_t off = 0;
        if (!payloadAppend(f, pv->b + q->off, size, &off)) {
            s_st.unpaired++;
            continue;
        }
        /* the moved count: the transform differs from the draw's own */
        s_st.moved += memcmp(f->payload + off, f->payload + c->off, size) != 0;
        RdCmd *cmd = &f->lists[c->list].cmds[c->idx];
        cmd->u[3] = (cmd->u[3] & RD_VU_TICK_CAMERAS) | RD_VU_PREV | (off & RD_VU_PREV_OFF);
        s_st.paired++;
    }
    s_have = keepFrame(f);
    s_blk.frames++;
    s_blk.draws += s_st.vuDraws;
    s_blk.paired += s_st.paired;
    s_blk.moved += s_st.moved;
    s_blk.unpaired += s_st.unpaired;
    s_blk.shape += s_st.shape;
    s_blk.jump += s_st.jump;
    s_blk.resets += s_st.reset;
    blockLog();
}

/* ------------------------------------------------------- the GPU side */

static struct {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
    RhiPipeline pipe;
    RhiShader vs, fs;
    bool tried, failed;
} s_gpu;

uint32_t rd__velocity_vp_extent(uint32_t n)
{
    const uint32_t lim = g_rd.hasDevice ? rhi_limits()->maxRenderTargetSize : 16384u;
    uint32_t p = 16384u;
    while (p > lim) {
        p >>= 1;
    }
    return p >= n ? p : n;
}

bool rd__velocity_target(RhiTexture *tex, RhiState **state, uint32_t *w, uint32_t *h)
{
    if (!g_rd.hasDevice || !rd__velocity_on()) {
        return false;
    }
    const RdTargetRec *sc = &g_rd.targets[RD_TARGET_SCENE];
    if (!sc->live || !sc->color.id || !sc->tw || !sc->th) {
        return false;
    }
    if (!s_gpu.tex.id || s_gpu.w != sc->tw || s_gpu.h != sc->th) {
        if (s_gpu.tex.id) {
            rd__retire_texture(s_gpu.tex);
            s_gpu.tex = (RhiTexture){0};
        }
        s_gpu.tex = rhi_create_texture(&(RhiTextureDesc){
            sc->tw, sc->th, 1, RHI_FMT_RGBA16F,
            RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC, "rd VELOCITY", 0});
        s_gpu.state = RHI_STATE_UNDEFINED;
        s_gpu.w = sc->tw;
        s_gpu.h = sc->th;
        if (!s_gpu.tex.id) {
            static uint32_t lines; /* the first 8 */
            if (lines++ < 8u) {
                rd__log("velocity: VELOCITY (%ux%u RGBA16F) could not be made: no motion "
                        "vectors this replay",
                        sc->tw, sc->th);
            }
            return false;
        }
        rd__log("velocity: VELOCITY made, %ux%u RGBA16F (%u KB), SCENE's texture size", sc->tw,
                sc->th, (unsigned)((uint64_t)sc->tw * sc->th * 8u / 1024u));
    }
    *tex = s_gpu.tex;
    *state = &s_gpu.state;
    *w = s_gpu.w;
    *h = s_gpu.h;
    return true;
}

RhiPipeline rd__velocity_pipeline(void)
{
    if (s_gpu.pipe.id || s_gpu.failed) {
        return s_gpu.pipe;
    }
    if (!s_gpu.vs.id) {
        s_gpu.vs = rd__make_shader("vu_velocity_main_vs");
    }
    if (!s_gpu.fs.id) {
        s_gpu.fs = rd__make_shader("velocity_ps");
    }
    if (!s_gpu.vs.id || !s_gpu.fs.id || !g_rd.layoutFrame.id || !g_rd.layoutVu.id) {
        s_gpu.failed = true;
        rd__log("velocity: the velocity pass's shaders are missing (vu_velocity_main_vs, "
                "velocity_ps): every motion vector stays 0");
        return s_gpu.pipe;
    }
    RhiBindGroupLayout layouts[2] = {g_rd.layoutFrame, g_rd.layoutVu};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = s_gpu.vs;
    d.fragment = s_gpu.fs;
    d.layouts = layouts;
    d.layoutCount = 2;
    d.cullNone = true;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.colorCount = 1;
    d.colorFormats[0] = RHI_FMT_RGBA16F;
    d.blend[0].srcColor = RHI_BF_ONE;
    d.blend[0].dstColor = RHI_BF_ZERO;
    d.blend[0].colorOp = RHI_BO_ADD;
    d.blend[0].srcAlpha = RHI_BF_ONE;
    d.blend[0].dstAlpha = RHI_BF_ZERO;
    d.blend[0].alphaOp = RHI_BO_ADD;
    d.blend[0].writeMask = 0xF;
    /* the surface the scene kept: its own depth, exactly (the same
       vu_main_vs position), read-only */
    d.depthFormat = RHI_FMT_D32F_S8;
    d.depthStencil.depthTest = true;
    d.depthStencil.depthWrite = false;
    d.depthStencil.depthCompare = RHI_CMP_EQUAL;
    d.debugName = "rd velocity";
    rd__pipeline_note_begin(d.debugName);
    s_gpu.pipe = rhi_create_pipeline(&d);
    rd__pipeline_note_end();
    if (s_gpu.pipe.id) {
        rd__stats_here()->pipelineCreates++;
        rd__log("velocity: the velocity pass's pipeline made (vu_velocity_main_vs, "
                "velocity_ps, RGBA16F, depth EQUAL read-only)");
    } else {
        s_gpu.failed = true;
        rd__log("velocity: the velocity pass's pipeline could not be made: every motion vector "
                "stays 0");
    }
    return s_gpu.pipe;
}

/* IEEE half to float */
static float halfToFloat(uint16_t h)
{
    const uint32_t s = (uint32_t)(h >> 15) & 1u, e = (uint32_t)(h >> 10) & 31u, m = h & 1023u;
    float v;
    if (e == 0u) {
        v = ldexpf((float)m, -24);
    } else if (e == 31u) {
        v = m ? NAN : INFINITY;
    } else {
        v = ldexpf((float)(m | 1024u), (int)e - 25);
    }
    return s ? -v : v;
}

bool rd__velocity_read(float *dst, size_t floats, uint32_t *w, uint32_t *h)
{
    if (!s_gpu.tex.id || !dst) {
        return false;
    }
    const size_t n = (size_t)s_gpu.w * s_gpu.h * 4u;
    if (floats < n) {
        return false;
    }
    uint16_t *raw = malloc(n * sizeof(uint16_t));
    if (!raw) {
        return false;
    }
    /* 8 bytes a texel: read as twice as many 4-byte texels */
    const bool ok = rd__read_rhi_texture(s_gpu.tex, &s_gpu.state, s_gpu.w * 2u, s_gpu.h, raw,
                                         n * sizeof(uint16_t));
    if (ok) {
        for (size_t i = 0; i < n; i++) {
            dst[i] = halfToFloat(raw[i]);
        }
        *w = s_gpu.w;
        *h = s_gpu.h;
    }
    free(raw);
    return ok;
}

void rd__velocity_shutdown(void)
{
    if (s_gpu.tex.id) {
        rhi_destroy_texture(s_gpu.tex);
    }
    if (s_gpu.pipe.id) {
        rhi_destroy_pipeline(s_gpu.pipe);
    }
    if (s_gpu.vs.id) {
        rhi_destroy_shader(s_gpu.vs);
    }
    if (s_gpu.fs.id) {
        rhi_destroy_shader(s_gpu.fs);
    }
    memset(&s_gpu, 0, sizeof(s_gpu));
    s_have = false;
}
