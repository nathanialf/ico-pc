/* rd_interp.c: presentation between simulation ticks (renderer wave 7, R7b;
 * docs/port/RENDER_API.md section 20).
 *
 * The game draws one frame per tick (25 Hz PAL, 30 Hz NTSC at the frame
 * step 2).  With RdSettings.interpolate in the Enhanced preset, rd_EndFrame
 * only closes the frame and the host calls rd_Present(alpha) as often as
 * the display allows: the last closed frame (cur) is replayed with the data
 * of every keyed draw blended from its match in the frame before (prev),
 * weight alpha on cur.  At alpha 0 the picture is prev's, so presentation
 * runs one tick behind the simulation; it never feeds back into it (this
 * file only reads the retained frames, rd_core.c's RD_FRAME_RING).
 *
 * Structure comes from cur: its lists, state commands, textures, targets
 * and unkeyed draws are replayed as recorded.  A draw blends when it has a
 * key (RdKey, RD_KEY at the call sites; 0 = never) and prev has a draw of
 * the same type in the same list with the same key; the n-th occurrence of
 * a key in cur matches the n-th in prev (the "call ordinal" for packets
 * drawn more than once).  What blends, per command:
 *
 *   RDC_MESH, RDC_SKINNED   the VU block (VuCB.mem): qw 2 (the UV scroll
 *                           SET_UVOFFSET left, with Texture.c's wrap of
 *                           uOfs/vOfs into (-1, 1] by 2 undone, and the
 *                           cluster fade alpha), qw 4..15 (the common
 *                           block's world to screen, viewport and inverse
 *                           view matrices), 16..27 (the model matrices),
 *                           28..35 (the light matrices); the bones; not qw
 *                           0, 1, 3 (constants and the GIF tag)
 *   RDC_GRID                the VU block and each vertex's position (and
 *                           normal when lit); the strip headers, colours
 *                           and STs are cur's
 *   RDC_PARTICLES           the VU block and each particle's position and
 *                           size; UV, grey and alpha are cur's
 *   RDC_SCREEN              XY, Z and colour of each vertex; STQ is cur's
 *   RDC_SHADOW_STRIP        XY and Z of each vertex
 * Matrices blend element by element (a rotation of more than a few degrees
 * per tick shortens slightly half way; nothing in the game turns fast enough
 * to show it at 25 Hz).  A float pair with different bits that is not both
 * finite keeps cur's.
 *
 * A keyed draw snaps (is cur's) when prev has no match, when the payload's
 * shape differs (mesh, program, batch range, bone, vertex or particle
 * count, prim type, the shadow's triangle counts: its topology), or when it
 * jumped: a model's origin in the world (the model to screen translation
 * through the inverse of the frame's world to screen; a skinned draw's first
 * bone) moved more than RD_INTERP_JUMP_WORLD in the tick, or a screen prim's
 * or shadow vertex more than RD_INTERP_JUMP_SCREEN.  A particle whose position moved more than
 * four times its size, or whose alpha is 0 in either frame, keeps cur's.
 *
 * The whole frame snaps (rd__InterpSnap, RD_SNAP_*) without a previous
 * frame, across a discarded frame, on a keep frame, on a camera cut
 * (rd_CameraCut: the game's hard cuts and stage changes; RdCamera.cut), when
 * the camera turned more than RD_INTERP_CAMERA_TURN or moved more than
 * RD_INTERP_CAMERA_MOVE in the tick (a cut the hooks missed), at a fade
 * edge (either frame fully faded: what is behind a black screen may change
 * all at once), after the display options recreated the targets, and when
 * the scene size changed.
 *
 * Held at the tick (cur's, unkeyed or state): CLUT animation (textures are
 * updated in place), the rand-driven draws (the menu sparkle, lightning:
 * world prims and particles' screen packets with key 0), the dissolve FIX
 * (an ALPHA state command), film noise (an unkeyed post sprite), and the
 * aura's feedback through FEED128: its sprites that write FEED128 run in
 * the first present of a tick only, so the buffer advances once per tick
 * as on the PS2 and the other presents paste the held buffer.
 *
 * Feedback per present: the motion blur's sprite (DISPLAY back into SCENE
 * with LERP FIX) runs at every present.  On the PS2 DISPLAY keeps a =
 * (128 - FIX) / 128 of itself per tick T; a present that stands for dt
 * ticks uses the FIX that keeps a^dt (rd__BlurFeedbackFix: FIX' = 128 -
 * 128 a^dt, rounded), so k presents of dt = 1/k keep a per tick, whatever
 * the present rate.  dt is the alpha advanced since the previous present
 * (plus whole ticks when frames were closed in between), at least 1/256,
 * at most 4.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"

/* ------------------------------------------------------------ the result */

static RdFrame s_out;

static void outFree(void)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(s_out.lists[l].cmds);
    }
    free(s_out.payload);
    memset(&s_out, 0, sizeof(s_out));
}

/* s_out = cur, its own copy of the lists and the payload; no temporary
 * targets of its own */
static bool copyFrame(const RdFrame *cur)
{
    RdCmdList lists[RD_LIST_COUNT];
    uint8_t *payload = s_out.payload;
    uint32_t payloadCap = s_out.payloadCap;
    memcpy(lists, s_out.lists, sizeof(lists));
    s_out = *cur;
    s_out.tempCount = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        RdCmdList *o = &lists[l];
        const RdCmdList *c = &cur->lists[l];
        if (c->count > o->cap) {
            RdCmd *p = realloc(o->cmds, (size_t)c->count * sizeof(RdCmd));
            if (!p) {
                memcpy(s_out.lists, lists, sizeof(lists));
                s_out.payload = payload;
                s_out.payloadCap = payloadCap;
                return false;
            }
            o->cmds = p;
            o->cap = c->count;
        }
        if (c->count) {
            memcpy(o->cmds, c->cmds, (size_t)c->count * sizeof(RdCmd));
        }
        o->count = c->count;
    }
    memcpy(s_out.lists, lists, sizeof(lists));
    if (cur->payloadSize > payloadCap) {
        uint8_t *p = realloc(payload, cur->payloadSize);
        if (!p) {
            s_out.payload = payload;
            s_out.payloadCap = payloadCap;
            s_out.payloadSize = 0;
            return false;
        }
        payload = p;
        payloadCap = cur->payloadSize;
    }
    if (cur->payloadSize) {
        memcpy(payload, cur->payload, cur->payloadSize);
    }
    s_out.payload = payload;
    s_out.payloadCap = payloadCap;
    s_out.payloadSize = cur->payloadSize;
    return true;
}

static void presentReset(void);

void rd__InterpShutdown(void)
{
    outFree();
    presentReset();
}

/* ---------------------------------------------------------------- blends */

static inline float lerpF(float p, float c, float t)
{
    /* exact at both ends: t = 0 gives p, t = 1 gives c */
    return (1.0f - t) * p + t * c;
}

static inline bool sameBits(float a, float b)
{
    uint32_t x, y;
    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return x == y;
}

/* o[i] holds cur's value c[i]; blend it from p[i] */
static void lerpFloats(float *o, const float *p, const float *c, uint32_t n, float t)
{
    for (uint32_t i = 0; i < n; i++) {
        if (sameBits(p[i], c[i]) || !isfinite(p[i]) || !isfinite(c[i])) {
            continue;
        }
        o[i] = lerpF(p[i], c[i], t);
    }
}

/* Texture.c's UV scroll keeps uOfs/vOfs in (-1, 1] by adding or taking 2
 * (tex_textureAnimation); a step of more than 1 is that wrap. */
static float lerpWrap(float p, float c, float t)
{
    if (c - p > 1.0f) {
        p += 2.0f;
    } else if (p - c > 1.0f) {
        p -= 2.0f;
    }
    return lerpF(p, c, t);
}

static int32_t lerpI(int32_t p, int32_t c, float t)
{
    if (p == c) {
        return c;
    }
    const double v = (1.0 - (double)t) * p + (double)t * c;
    return (int32_t)floor(v + 0.5);
}

static uint32_t lerpU(uint32_t p, uint32_t c, float t)
{
    if (p == c) {
        return c;
    }
    const double v = (1.0 - (double)t) * p + (double)t * c;
    return (uint32_t)floor(v + 0.5);
}

static uint8_t lerpB(uint8_t p, uint8_t c, float t)
{
    return (uint8_t)lerpU(p, c, t);
}

/* VuCB.mem: qw 2 xy the UV scroll (wrap undone), zw; qw 4..35 matrices and
 * lights.  qw 0, 1, 3 are constants and a GIF tag: cur's. */
static void lerpVuBlock(float (*o)[4], const float (*p)[4], const float (*c)[4], float t)
{
    for (int k = 0; k < 2; k++) {
        if (!sameBits(p[2][k], c[2][k]) && isfinite(p[2][k]) && isfinite(c[2][k])) {
            o[2][k] = lerpWrap(p[2][k], c[2][k], t);
        }
    }
    lerpFloats(&o[2][2], &p[2][2], &c[2][2], 2, t);
    lerpFloats(o[4], p[4], c[4], 32 * 4, t);
}

/* ----------------------------------------------------------- jump tests */

static float dist3(const float a[3], const float b[3])
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* The inverse of a column-major 4 x 4 (cofactors), false when singular */
static bool invert4(const float *m, double *o)
{
    double a[16], inv[16];
    for (int i = 0; i < 16; i++) {
        a[i] = m[i];
    }
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (!(fabs(det) > 1e-30) || !isfinite(det)) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        o[i] = inv[i] / det;
    }
    return true;
}

/* A draw's model origin in the world: the common block's world to GS
 * screen (qw 4..7) inverted, applied to the model to screen translation
 * (qw 19: normal programs' +0x140 x node, the grid's and the particles'
 * screen matrix).  qw 24..27 (model to view) would be simpler but is not
 * uploaded by every pass: list 5's dissolve draws carry another object's.
 * The camera's motion cancels, so this is the object's own move. */
static bool vuWorldOrigin(const float (*m)[4], float out[3])
{
    double inv[16], w[4] = {0.0, 0.0, 0.0, 0.0};
    if (!invert4(&m[4][0], inv)) {
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

/* The screen position of a model to GS screen matrix's origin (qw at..at+3,
 * columns): its translation column divided by w. */
static bool vuScreenOrigin(const float (*m)[4], int at, float out[2])
{
    const float *t = m[at + 3];
    if (!(t[3] > 0.0f) || !isfinite(t[0]) || !isfinite(t[1])) {
        return false;
    }
    out[0] = t[0] / t[3];
    out[1] = t[1] / t[3];
    return isfinite(out[0]) && isfinite(out[1]);
}

static bool screenJump(const float (*p)[4], const float (*c)[4], int at)
{
    float a[2], b[2];
    if (!vuScreenOrigin(p, at, a) || !vuScreenOrigin(c, at, b)) {
        return false;
    }
    return fabsf(a[0] - b[0]) > RD_INTERP_JUMP_SCREEN || fabsf(a[1] - b[1]) > RD_INTERP_JUMP_SCREEN;
}

/* the world test, else (a singular common block) the screen test */
static bool worldJump(const float (*p)[4], const float (*c)[4])
{
    float a[3], b[3];
    if (vuWorldOrigin(p, a) && vuWorldOrigin(c, b)) {
        return dist3(a, b) > RD_INTERP_JUMP_WORLD;
    }
    return screenJump(p, c, 16);
}

static bool isNormalProg(uint8_t prog)
{
    return prog == RD_PROG_PRELIT || prog == RD_PROG_LIT || prog == RD_PROG_LIT_SPEC ||
           prog == RD_PROG_REFLECT;
}

static bool vtxJump(const RdScreenVtx *p, const RdScreenVtx *c, uint32_t n)
{
    const int32_t lim = (int32_t)(RD_INTERP_JUMP_SCREEN * 16.0f);
    for (uint32_t i = 0; i < n; i++) {
        if (abs(p[i].x - c[i].x) > lim || abs(p[i].y - c[i].y) > lim) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------- matching */

typedef struct Slot {
    uint32_t lo, hi;
    uint8_t type, list, used, _pad;
    int32_t head, tail, cursor; /* prev's occurrences, in order (Node.next) */
} Slot;

typedef struct Node {
    uint32_t list, index;
    int32_t next;
} Node;

static Slot *s_slots;

static uint32_t s_slotCap;

static Node *s_nodes;

static uint32_t s_nodeCap, s_nodeCount;

static bool isKeyedDraw(const RdCmd *c)
{
    if (c->keyLo == 0 && c->keyHi == 0) {
        return false;
    }
    switch (c->type) {
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_SHADOW_STRIP:
        return true;
    default:
        return false;
    }
}

static uint32_t slotHash(uint32_t lo, uint32_t hi, uint8_t type, uint8_t list)
{
    uint32_t h = lo * 0x9E3779B1u;
    h ^= hi * 0x85EBCA77u + 0x165667B1u;
    h ^= ((uint32_t)type << 8 | list) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x27D4EB2Fu;
    h ^= h >> 13;
    return h;
}

static Slot *slotFind(const RdCmd *c, int list, bool insert)
{
    uint32_t i = slotHash(c->keyLo, c->keyHi, c->type, (uint8_t)list) & (s_slotCap - 1);
    for (;;) {
        Slot *s = &s_slots[i];
        if (!s->used) {
            if (!insert) {
                return NULL;
            }
            s->used = 1;
            s->lo = c->keyLo;
            s->hi = c->keyHi;
            s->type = c->type;
            s->list = (uint8_t)list;
            s->head = s->tail = s->cursor = -1;
            return s;
        }
        if (s->lo == c->keyLo && s->hi == c->keyHi && s->type == c->type && s->list == list) {
            return s;
        }
        i = (i + 1) & (s_slotCap - 1);
    }
}

/* the table of prev's keyed draws; false on out of memory */
static bool buildIndex(const RdFrame *prev)
{
    uint32_t n = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < prev->lists[l].count; i++) {
            n += isKeyedDraw(&prev->lists[l].cmds[i]);
        }
    }
    uint32_t cap = 64;
    while (cap < n * 2 + 16) {
        cap *= 2;
    }
    if (cap > s_slotCap) {
        Slot *p = realloc(s_slots, (size_t)cap * sizeof(Slot));
        if (!p) {
            return false;
        }
        s_slots = p;
        s_slotCap = cap;
    }
    memset(s_slots, 0, (size_t)s_slotCap * sizeof(Slot));
    if (n > s_nodeCap) {
        Node *p = realloc(s_nodes, (size_t)n * sizeof(Node));
        if (!p) {
            return false;
        }
        s_nodes = p;
        s_nodeCap = n;
    }
    s_nodeCount = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < prev->lists[l].count; i++) {
            const RdCmd *c = &prev->lists[l].cmds[i];
            if (!isKeyedDraw(c)) {
                continue;
            }
            Slot *s = slotFind(c, l, true);
            const int32_t k = (int32_t)s_nodeCount++;
            s_nodes[k].list = (uint32_t)l;
            s_nodes[k].index = i;
            s_nodes[k].next = -1;
            if (s->tail >= 0) {
                s_nodes[s->tail].next = k;
            } else {
                s->head = s->cursor = k;
            }
            s->tail = k;
        }
    }
    return true;
}

/* the next unmatched occurrence in prev of cur's draw c in list l */
static const RdCmd *matchOf(const RdFrame *prev, const RdCmd *c, int l)
{
    Slot *s = slotFind(c, l, false);
    if (!s || s->cursor < 0) {
        return NULL;
    }
    const Node *nd = &s_nodes[s->cursor];
    s->cursor = nd->next;
    return &prev->lists[nd->list].cmds[nd->index];
}

/* ------------------------------------------------------------ per draw */

enum { R_LERP = 0, R_MISMATCH, R_JUMP };

static const void *payloadAt(const RdFrame *f, uint32_t off, uint32_t size)
{
    return off <= f->payloadSize && size <= f->payloadSize - off ? f->payload + off : NULL;
}

static int blendVu(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    if (pc->u[2] != cc->u[2] || pc->u[0] != cc->u[0] || pc->b[0] != cc->b[0] ||
        pc->b[1] != cc->b[1] || pc->b[2] != cc->b[2]) {
        return R_MISMATCH;
    }
    const uint8_t *pp = payloadAt(prev, pc->u[1], pc->u[2]);
    if (!pp || cc->u[2] < sizeof(RdVuPayload) + sizeof(RdVuBlock)) {
        return R_MISMATCH;
    }
    RdVuPayload hp, hc;
    memcpy(&hp, pp, sizeof(hp));
    memcpy(&hc, op, sizeof(hc));
    if (hp.firstBatch != hc.firstBatch || hp.batchCount != hc.batchCount ||
        hp.boneQw != hc.boneQw || hp.streamQw != hc.streamQw ||
        hp.vertsPerBatch != hc.vertsPerBatch || hp.qwPerVertex != hc.qwPerVertex ||
        hp.materialCount != hc.materialCount) {
        return R_MISMATCH;
    }
    const float (*vp)[4] = (const float (*)[4])(const void *)(pp + sizeof(RdVuPayload));
    float (*vo)[4] = (float (*)[4])(void *)(op + sizeof(RdVuPayload));
    float vc[36][4];
    memcpy(vc, vo, sizeof(vc));
    const uint32_t after = (uint32_t)(sizeof(RdVuPayload) + sizeof(RdVuBlock));
    const float (*bp)[4] = (const float (*)[4])(const void *)(pp + after);
    float (*bo)[4] = (float (*)[4])(void *)(op + after);
    const float (*sp)[4] = bp + hp.boneQw;
    float (*so)[4] = bo + hc.boneQw;

    /* the jump tests, on cur's data before it is blended */
    switch (cc->type) {
    case RDC_MESH:
        if (isNormalProg(cc->b[0]) && worldJump(vp, (const float (*)[4])vc)) {
            return R_JUMP;
        }
        break;
    case RDC_SKINNED:
        if (hc.boneQw >= 4 && fabsf(bp[3][3] - 1.0f) < 1e-3f && fabsf(bo[3][3] - 1.0f) < 1e-3f &&
            dist3(bp[3], bo[3]) > RD_INTERP_JUMP_WORLD) {
            return R_JUMP;
        }
        break;
    case RDC_GRID:
    case RDC_PARTICLES:
        if (worldJump(vp, (const float (*)[4])vc)) {
            return R_JUMP;
        }
        break;
    default:
        break;
    }

    lerpVuBlock(vo, vp, (const float (*)[4])vc, t);
    if (hc.boneQw) {
        float tmp[4];
        for (uint32_t q = 0; q < hc.boneQw; q++) {
            memcpy(tmp, bo[q], sizeof(tmp));
            lerpFloats(bo[q], bp[q], tmp, 4, t);
        }
    }
    if (cc->type == RDC_GRID && hc.streamQw) {
        /* per strip: the VIF qword, tag, colour, stripLen vertices, MSCNT */
        const uint32_t qpv = hc.qwPerVertex, len = hc.vertsPerBatch;
        const uint32_t per = len * qpv + 4;
        const uint32_t lerpQw = qpv == RD_VU_QW_GRID_LIT ? 2u : 1u; /* pos (, normal) */
        for (uint32_t s = 0; s < hc.batchCount && (s + 1) * per <= hc.streamQw; s++) {
            for (uint32_t k = 0; k < len; k++) {
                const uint32_t at = s * per + 3 + k * qpv;
                float tmp[4];
                for (uint32_t q = 0; q < lerpQw; q++) {
                    memcpy(tmp, so[at + q], sizeof(tmp));
                    lerpFloats(so[at + q], sp[at + q], tmp, 4, t);
                }
            }
        }
    } else if (cc->type == RDC_PARTICLES && hc.streamQw >= 6) {
        /* 6 header qwords, then (x, y, z, size), (u, v, grey, alpha) each */
        const uint32_t n = (hc.streamQw - 6) / 2;
        for (uint32_t i = 0; i < n; i++) {
            const float *a = sp[6 + 2 * i], *ap = sp[7 + 2 * i];
            float *b = so[6 + 2 * i];
            const float *bq = so[7 + 2 * i];
            const float size = fmaxf(fabsf(a[3]), fabsf(b[3]));
            if (ap[3] == 0.0f || bq[3] == 0.0f || dist3(a, b) > 4.0f * size) {
                continue;
            }
            float tmp[4];
            memcpy(tmp, b, sizeof(tmp));
            lerpFloats(b, a, tmp, 4, t);
        }
    }
    return R_LERP;
}

static int blendScreen(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    if (pc->u[1] != cc->u[1] || pc->b[0] != cc->b[0] || pc->b[1] != cc->b[1] ||
        pc->b[2] != cc->b[2]) {
        return R_MISMATCH;
    }
    const uint32_t n = cc->u[1];
    const RdScreenVtx *p = payloadAt(prev, pc->u[0], n * (uint32_t)sizeof(RdScreenVtx));
    if (!p) {
        return R_MISMATCH;
    }
    RdScreenVtx *o = (RdScreenVtx *)(void *)op;
    if (vtxJump(p, o, n)) {
        return R_JUMP;
    }
    for (uint32_t i = 0; i < n; i++) {
        o[i].x = lerpI(p[i].x, o[i].x, t);
        o[i].y = lerpI(p[i].y, o[i].y, t);
        o[i].z = lerpU(p[i].z, o[i].z, t);
        for (int k = 0; k < 4; k++) {
            o[i].rgba[k] = lerpB(p[i].rgba[k], o[i].rgba[k], t);
        }
    }
    return R_LERP;
}

static int blendShadow(uint8_t *op, const RdFrame *prev, const RdCmd *pc, const RdCmd *cc, float t)
{
    if (pc->b[0] != cc->b[0] || pc->u[0] != cc->u[0] || pc->u[2] != cc->u[2] ||
        pc->u[3] != cc->u[3]) {
        return R_MISMATCH; /* the volume's topology changed */
    }
    if (cc->b[0] == RD_SHADOW_TRIS) {
        const uint32_t n = cc->u[0] + cc->u[3];
        const RdScreenVtx *p = payloadAt(prev, pc->u[1], n * (uint32_t)sizeof(RdScreenVtx));
        if (!p) {
            return R_MISMATCH;
        }
        RdScreenVtx *o = (RdScreenVtx *)(void *)op;
        if (vtxJump(p, o, n)) {
            return R_JUMP;
        }
        for (uint32_t i = 0; i < n; i++) {
            o[i].x = lerpI(p[i].x, o[i].x, t);
            o[i].y = lerpI(p[i].y, o[i].y, t);
            o[i].z = lerpU(p[i].z, o[i].z, t);
        }
        return R_LERP;
    }
    /* rd_ShadowStrip: float[4] (12.4 X, Y, Z, unused) */
    const uint32_t n = cc->u[0];
    const float (*p)[4] = payloadAt(prev, pc->u[1], n * 16u);
    if (!p) {
        return R_MISMATCH;
    }
    float (*o)[4] = (float (*)[4])(void *)op;
    for (uint32_t i = 0; i < n; i++) {
        if (fabsf(p[i][0] - o[i][0]) > RD_INTERP_JUMP_SCREEN * 16.0f ||
            fabsf(p[i][1] - o[i][1]) > RD_INTERP_JUMP_SCREEN * 16.0f) {
            return R_JUMP;
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        float tmp[4];
        memcpy(tmp, o[i], sizeof(tmp));
        lerpFloats(o[i], p[i], tmp, 3, t);
    }
    return R_LERP;
}

/* The payload of a keyed draw in s_out (cur's copy) */
static uint8_t *outPayload(const RdCmd *c)
{
    uint32_t off, size;
    switch (c->type) {
    case RDC_SCREEN:
        off = c->u[0];
        size = c->u[1] * (uint32_t)sizeof(RdScreenVtx);
        break;
    case RDC_SHADOW_STRIP:
        off = c->u[1];
        size = c->b[0] == RD_SHADOW_TRIS ? (c->u[0] + c->u[3]) * (uint32_t)sizeof(RdScreenVtx)
                                         : c->u[0] * 16u;
        break;
    default:
        off = c->u[1];
        size = c->u[2];
        break;
    }
    return (uint8_t *)payloadAt(&s_out, off, size);
}

/* ------------------------------------------------------------ the frame */

static float frob(const float *a, const float *b)
{
    float s = 0.0f;
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            s += a[c * 4 + r] * b[c * 4 + r];
        }
    }
    return s;
}

/* the eye of a column-major view matrix: -R^T t */
static void viewEye(const float *v, float eye[3])
{
    for (int j = 0; j < 3; j++) {
        eye[j] = -(v[j * 4 + 0] * v[12] + v[j * 4 + 1] * v[13] + v[j * 4 + 2] * v[14]);
    }
}

static bool cameraJump(const RdCamera *p, const RdCamera *c)
{
    const float fp = frob(p->view, p->view), fc = frob(c->view, c->view);
    if (!(fp > 0.0f) || !(fc > 0.0f)) {
        return false;
    }
    /* cos of the turn between the two rotations, scale removed */
    const float cosT = (3.0f * frob(p->view, c->view) / sqrtf(fp * fc) - 1.0f) * 0.5f;
    if (cosT < cosf(RD_INTERP_CAMERA_TURN * 3.14159265f / 180.0f)) {
        return true;
    }
    float a[3], b[3];
    viewEye(p->view, a);
    viewEye(c->view, b);
    return dist3(a, b) > RD_INTERP_CAMERA_MOVE;
}

int rd__InterpSnap(const RdFrame *prev, const RdFrame *cur)
{
    if (!cur || !prev || !prev->closed) {
        return RD_SNAP_NO_PREV;
    }
    if (prev->number + 1 != cur->number) {
        return RD_SNAP_GAP;
    }
    if (prev->keep || cur->keep) {
        return RD_SNAP_KEEP;
    }
    if (cur->cut || cur->camera.cut) {
        return RD_SNAP_CUT;
    }
    if (g_rd.interpFloor && prev->number < g_rd.interpFloor) {
        return RD_SNAP_HISTORY;
    }
    if (prev->gsW != cur->gsW || prev->gsH != cur->gsH) {
        return RD_SNAP_SIZE;
    }
    if (prev->fade > 0x80u || cur->fade > 0x80u) {
        return RD_SNAP_FADE; /* 1 + an alpha of 0x80 or more: black */
    }
    if (prev->hasCamera && cur->hasCamera && cameraJump(&prev->camera, &cur->camera)) {
        return RD_SNAP_CAMERA;
    }
    return RD_SNAP_NONE;
}

static void lerpCamera(RdCamera *o, const RdCamera *p, const RdCamera *c, float t)
{
    lerpFloats(o->view, p->view, c->view, 16, t);
    lerpFloats(o->proj43, p->proj43, c->proj43, 16, t);
    lerpFloats(&o->zoom, &p->zoom, &c->zoom, 1, t);
    lerpFloats(&o->nearZ, &p->nearZ, &c->nearZ, 1, t);
    lerpFloats(&o->farZ, &p->farZ, &c->farZ, 1, t);
}

/* The feedback passes for a present standing for dt ticks: the motion
 * blur's FIX, and the aura's FEED128 writes only in a tick's first present. */
static void feedback(float dt, int firstOfTick)
{
    const uint32_t feed = rd_Target(RD_TARGET_FEED128).id;
    RdStateBlock st = s_out.startState;
    for (int l = rd__FirstList((int)s_out.keep); l < RD_LIST_COUNT; l++) {
        RdCmdList *cl = &s_out.lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            RdCmd *c = &cl->cmds[i];
            if (rd__ApplyState(&st, c) || c->type != RDC_POST_STUB) {
                continue;
            }
            if (c->b[0] == RD_POST_MOTION_BLUR) {
                RdPostRec *r = (RdPostRec *)payloadAt(&s_out, c->u[1], sizeof(RdPostRec));
                if (r) {
                    r->scalar[2] = dt;
                }
            } else if (c->b[0] == RD_POST_AURA && !firstOfTick && st.color == feed) {
                c->type = RDC_NOP;
            }
        }
    }
}

const RdFrame *rd__InterpFrame(const RdFrame *prev, const RdFrame *cur, float alpha, float dt,
                               int firstOfTick, RdInterpStats *stats)
{
    RdInterpStats st;
    memset(&st, 0, sizeof(st));
    if (!cur || !copyFrame(cur)) {
        if (stats) {
            *stats = st;
        }
        return NULL;
    }
    const float t = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    st.snap = (uint32_t)rd__InterpSnap(prev, cur);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
            st.keyed += isKeyedDraw(&s_out.lists[l].cmds[i]);
        }
    }
    if (st.snap == RD_SNAP_NONE && t < 1.0f && buildIndex(prev)) {
        if (prev->hasCamera && cur->hasCamera) {
            lerpCamera(&s_out.camera, &prev->camera, &cur->camera, t);
        }
        if (prev->hasVu && cur->hasVu) {
            lerpFloats(s_out.vu.screenView, prev->vu.screenView, cur->vu.screenView, 16, t);
            lerpFloats(s_out.vu.viewport, prev->vu.viewport, cur->vu.viewport, 16, t);
            lerpFloats(s_out.vu.invView, prev->vu.invView, cur->vu.invView, 16, t);
        }
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t i = 0; i < s_out.lists[l].count; i++) {
                const RdCmd *c = &s_out.lists[l].cmds[i];
                if (!isKeyedDraw(c)) {
                    continue;
                }
                const RdCmd *pc = matchOf(prev, c, l);
                uint8_t *op = outPayload(c);
                if (!pc || !op) {
                    st.missing++;
                    continue;
                }
                int r;
                switch (c->type) {
                case RDC_SCREEN:
                    r = blendScreen(op, prev, pc, c, t);
                    break;
                case RDC_SHADOW_STRIP:
                    r = blendShadow(op, prev, pc, c, t);
                    break;
                default:
                    r = blendVu(op, prev, pc, c, t);
                    break;
                }
                st.lerped += r == R_LERP;
                st.mismatch += r == R_MISMATCH;
                st.jump += r == R_JUMP;
            }
        }
    }
    feedback(dt, firstOfTick);
    if (stats) {
        *stats = st;
    }
    return &s_out;
}

/* ------------------------------------------------------------- present */

static struct {
    uint32_t number; /* the frame of the last present */
    float alpha;     /* and its alpha */
    RdInterpStats stats;
    /* the log: per frame, at its first present */
    uint32_t frames, presents, snaps[RD_SNAP_COUNT];
    uint64_t keyed, lerped, missing, mismatch, jump;
} s_pres;

#define RD_INTERP_LOG_FRAMES 250

static void presentLog(void)
{
    const RdInterpStats *st = &s_pres.stats;
    s_pres.frames++;
    s_pres.snaps[st->snap < RD_SNAP_COUNT ? st->snap : 0]++;
    s_pres.keyed += st->keyed;
    s_pres.lerped += st->lerped;
    s_pres.missing += st->missing;
    s_pres.mismatch += st->mismatch;
    s_pres.jump += st->jump;
    if (s_pres.frames < RD_INTERP_LOG_FRAMES) {
        return;
    }
    const uint32_t *n = s_pres.snaps;
    rd__Log("interp: %u frames, %u presents: %u blended; snapped: %u no previous, %u gap, "
            "%u keep, %u cut, %u camera, %u fade, %u history, %u size; keyed draws %llu: "
            "%llu blended, %llu unmatched, %llu mismatched, %llu jumped",
            s_pres.frames, s_pres.presents, n[RD_SNAP_NONE], n[RD_SNAP_NO_PREV], n[RD_SNAP_GAP],
            n[RD_SNAP_KEEP], n[RD_SNAP_CUT], n[RD_SNAP_CAMERA], n[RD_SNAP_FADE], n[RD_SNAP_HISTORY],
            n[RD_SNAP_SIZE], (unsigned long long)s_pres.keyed, (unsigned long long)s_pres.lerped,
            (unsigned long long)s_pres.missing, (unsigned long long)s_pres.mismatch,
            (unsigned long long)s_pres.jump);
    const uint32_t number = s_pres.number;
    const float alpha = s_pres.alpha;
    memset(&s_pres, 0, sizeof(s_pres));
    s_pres.number = number;
    s_pres.alpha = alpha;
}

static void presentReset(void)
{
    memset(&s_pres, 0, sizeof(s_pres));
}

bool rd_InterpolationActive(void)
{
    return g_rd.inited && g_rd.settings.preset == RD_PRESET_ENHANCED &&
           g_rd.settings.interpolate != 0;
}

bool rd_Present(float alpha)
{
    if (!rd_InterpolationActive() || !g_rd.hasDevice || g_rd.videoShown) {
        return false;
    }
    const RdFrame *cur = rd__LastFrame();
    if (!cur || !cur->closed) {
        return false;
    }
    if (!(alpha >= 0.0f)) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    const int first = cur->number != s_pres.number;
    float dt;
    if (first) {
        const uint32_t k =
            s_pres.number && cur->number > s_pres.number ? cur->number - s_pres.number : 1u;
        dt = (float)k - s_pres.alpha + alpha;
    } else {
        dt = alpha - s_pres.alpha;
    }
    dt = dt < 1.0f / 256.0f ? 1.0f / 256.0f : (dt > 4.0f ? 4.0f : dt);
    s_pres.number = cur->number;
    s_pres.alpha = alpha;
    s_pres.presents++;
    if (cur->keep) {
        /* a keep frame draws lists 11..12 over DISPLAY: once; the other
         * presents show DISPLAY again (an empty keep frame) */
        if (first) {
            return rd__ReplayFrame(cur, 1, true);
        }
        static RdFrame empty;
        return rd__ReplayFrame(&empty, 1, true);
    }
    const RdFrame *f = rd__InterpFrame(rd__PrevFrame(), cur, alpha, dt, first, &s_pres.stats);
    if (first) {
        presentLog();
    }
    return f != NULL && rd__ReplayFrame(f, 0, true);
}
