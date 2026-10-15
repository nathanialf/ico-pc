/* rd_stream_audit_test.c: the VU stream bounds audit (issue 95;
 * docs/port/RENDER.md, "The stream bounds audit").
 *
 * The VU vertex shaders (port/shaders/vu_*.hlsl) read the vertex stream at
 * t0 (vu_stream) through indices they compute: a triangle's three vertices
 * from its kick (vu_vertex_qw; the clip plane's distance reads one of them),
 * a batch's header qword (a grid's colour), a particle's header and its two
 * qwords.  The shipped shaders scan no strip: the card test's verdicts come
 * from the CPU as bits in VuBoneCB (vu_strip_card, its index clamped to
 * 239), not from the stream.  The audit still runs v0.7.6's strip scans and
 * walks (vu_strip_of, vu_*_strip_passes) over every kick, a superset of
 * today's reads, so it stays conservative: a stream it passes is safe for
 * the shaders of either version.  A GPU that reads storage buffers
 * through plain pointers (Mali) faults on an index past the end where
 * others read zero, so every VU draw of the dumps is walked here on the
 * CPU, every stream index those shaders can read for it computed as the
 * shader computes it (the same unsigned arithmetic, the strip scans run on
 * the stream's own strip flags), and each checked against the draw's own
 * stream: the qwords the replay uploads for it (rd_replay.c doVuDraw:
 * the mesh's vertices, or the command's copied stream).  The bound range
 * holds the draw's stream and possibly other meshes' after it, so a read
 * past the draw's own stream is a read of something else or past the
 * buffer: either fails.  Also checked: every kick at least 2 (k - 2 reads
 * a vertex), the draw's index range inside the mesh's index list (the
 * index buffer is read too).  Reported, not failed: a skinned vertex's
 * bone address outside the bones its draw uploaded (VuBoneCB is a fixed
 * 240 qwords and vu_bone_mat clamps into it, so it reads zeros, not past
 * it), and the skinned draws' weight addresses and those whose bones fill
 * VuBoneCB (bone 59 or an address below 16: no room for the card bits,
 * docs/port/VU1_PROGRAMS.md "The card test"); the tick cameras
 * (vu_ticks[4k .. 4k + 3], k < 2) and VU memory
 * (vu_mem[0..35]) are fixed-size constant blocks, checked by the static
 * asserts below.
 *
 * The (v0.7.6) strip test is taken to run for every kick (it ran for a
 * triangle the region rule sent to the card test, which depends on the
 * camera): the audit is the worst case over cameras.
 *
 * The strip bounds come from one function, stripBounds, which runs
 * v0.7.6's vu_strip_of scans; the shaders no longer read them, so they only
 * add reads the shaders do not make.
 *
 * Self-test (always runs): a synthetic prelit draw passes; the same draw
 * with its stream one vertex short fails, as do a kick below 2 and an
 * index range past the list; on the first real mesh draw of the dumps, the
 * same draw with its stream shortened by its last vertex fails.
 *
 * Arguments: dumps (.rddump, or a F12 / loss .zip: its first .rddump) and
 * dump lists (--list FILE: tools/native_frame_dumps.txt's format, one path
 * a line, '#' comments, paths relative to the file, '*' globs).  Missing
 * files are named and skipped; with none read, the dump half is skipped
 * (exit 77 when the self-test passed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <glob.h>
#endif
#include "dump_zip.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"
#include "vu1_ref.h"

_Static_assert(sizeof(((IcoVuCB *)0)->ticks) == 8 * 16,
               "vu_ticks[4k + 3] for k < 2 must stay inside the block");
_Static_assert(sizeof(((IcoVuCB *)0)->mem) == 36 * 16, "vu_mem[0..35]");
_Static_assert(sizeof(IcoVuBoneCB) == 240 * 16, "vu_bone_mat clamps to 236 + 3");

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: ");                                                                      \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* One VU draw as the shaders see it: the VuCB words rd_replay.c doVuDraw
 * sets (vu_draw.x 0: the stream's own first qword), the stream and the
 * index values it draws. */
typedef struct AuditDraw {
    uint8_t prog;              /* RdProg */
    const float (*stream)[4];  /* streamQw qwords */
    uint32_t streamQw;         /* the draw's own stream */
    uint32_t qpv;              /* vu_draw.y */
    uint32_t vpb;              /* vu_draw.w */
    uint32_t batchHead;        /* vu_batch.x */
    uint32_t batchTail;        /* vu_batch.y */
    uint32_t stripLo, stripHi; /* vu_strip.xy */
    const uint32_t *index;     /* the index values drawn (NULL: particles) */
    uint32_t indexCount;
    uint32_t particles; /* RD_PROG_PARTICLE: the count */
    uint32_t boneQw;    /* skinned: the bone qwords uploaded */
    /* the marks as the replay hands them over: OR-ed into every index
     * value (valueMark: a grid before v0.8.0), a mark byte a kick
     * (kickMark, markCount bytes: rd_mesh.c's), or the draw's own
     * (drawMark: a grid's vu_mark.y) */
    uint32_t valueMark;
    const uint8_t *kickMark;
    uint32_t markCount;
    uint32_t drawMark;
} AuditDraw;

typedef struct Audit {
    const char *where; /* the draw, for the message */
    uint64_t reads;
    uint32_t bad;
    uint32_t badKicks;
    uint32_t boneOutside;
    /* skinned: the weights' addresses (lowest, highest) and the bone
     * qwords they reach (rd_mesh.c cardStrips' boneQwUsed: 240 leaves no
     * room for the card bits) */
    uint32_t addrLo, addrHi, boneUsed;
    char first[320];
} Audit;

/* a failed read or a bad draw; the first one's text is kept */
static void failText(Audit *a, const char *what)
{
    if (a->bad++ == 0) {
        snprintf(a->first, sizeof(a->first), "%s: %s", a->where, what);
    }
}

/* fmt takes two unsigned values */
static void fail(Audit *a, const char *fmt, uint32_t x, uint32_t y)
{
    char what[160];
    snprintf(what, sizeof(what), fmt, (unsigned)x, (unsigned)y);
    failText(a, what);
}

/* one stream read at qword qw (vu_stream[qw]) */
static void readQw(const AuditDraw *d, Audit *a, uint32_t qw, uint32_t v)
{
    a->reads++;
    if (qw >= d->streamQw) {
        fail(a, "qword %u read for vertex %u is past the stream", qw, v);
    }
}

/* vu_common.hlsli vu_batch_qw / vu_vertex_qw, in the shader's uint */
static uint32_t batchQw(const AuditDraw *d, uint32_t v)
{
    const uint32_t b = d->vpb ? v / d->vpb : 0u;
    return 0u + b * (d->batchHead + d->vpb * d->qpv + d->batchTail);
}

static uint32_t vertexQw(const AuditDraw *d, uint32_t v)
{
    const uint32_t k = d->vpb ? v % d->vpb : v;
    return batchQw(d, v) + d->batchHead + k * d->qpv;
}

static int flagged(uint8_t prog)
{
    return prog != RD_PROG_GRID && prog != RD_PROG_GRID_LIT;
}

/* the qwords a program's vertex function reads at vertex v (full: the
 * attributes too, else position and tests only; the audit takes full
 * everywhere, the worst case) */
static void readVertex(const AuditDraw *d, Audit *a, uint32_t v)
{
    const uint32_t at = vertexQw(d, v);
    uint32_t n = 0;
    switch (d->prog) {
    case RD_PROG_PRELIT:
        n = 3; /* pos, ST, colour */
        break;
    case RD_PROG_LIT:
    case RD_PROG_LIT_SPEC:
    case RD_PROG_REFLECT:
        n = 4; /* pos, normal, ST, colour */
        break;
    case RD_PROG_SKIN:
    case RD_PROG_SKIN_SPEC:
        n = 5; /* pos, normal, weights, ST, colour */
        break;
    case RD_PROG_GRID:
        n = 2; /* pos, ST */
        break;
    case RD_PROG_GRID_LIT:
        n = 3; /* pos, normal, ST */
        break;
    default:
        break;
    }
    for (uint32_t i = 0; i < n; i++) {
        readQw(d, a, at + i, v);
    }
    if (d->prog == RD_PROG_GRID || d->prog == RD_PROG_GRID_LIT) {
        /* the batch colour, the header's last qword */
        readQw(d, a, batchQw(d, v) + d->batchHead - 1u, v);
    }
    if ((d->prog == RD_PROG_SKIN || d->prog == RD_PROG_SKIN_SPEC) && at + 2u < d->streamQw) {
        /* vu_bone_mat: min(addr - 16, 236) .. + 3 of VuBoneCB; the bones
           past the uploaded ones read zeros */
        uint32_t addr[2];
        memcpy(&addr[0], &d->stream[at + 2u][0], 4);
        memcpy(&addr[1], &d->stream[at + 2u][2], 4);
        for (int k = 0; k < 2; k++) {
            uint32_t i = addr[k] - 16u;
            i = i < 236u ? i : 236u;
            if (i + 4u > d->boneQw) {
                a->boneOutside++;
            }
            a->addrLo = addr[k] < a->addrLo ? addr[k] : a->addrLo;
            a->addrHi = addr[k] > a->addrHi ? addr[k] : a->addrHi;
            a->boneUsed = i + 4u > a->boneUsed ? i + 4u : a->boneUsed;
        }
    }
}

/* vu_strip_start: the strip flag, ST.w < 1 at qword qpv - 2 of v; a read
 * past the stream is counted and taken as zero (a strip start) */
static int stripStart(const AuditDraw *d, Audit *a, uint32_t v)
{
    if (!flagged(d->prog)) {
        return 0;
    }
    const uint32_t qw = vertexQw(d, v) + d->qpv - 2u;
    readQw(d, a, qw, v);
    return qw >= d->streamQw || d->stream[qw][3] < 1.0f;
}

#define VU_STRIP_SCAN 256u      /* v0.7.6's vu_common.hlsli */
#define VU_STRIP_SPAN_MAX 1024u /* v0.7.6's vu_common.hlsli */

/* The strip of kick k: vu_strip_of (x its first vertex, y one past its
 * last, *longStrip its z), the scans' reads checked.  The one function a
 * stream carrying its strips' bounds would replace.  v0.7.6's scans: the
 * shipped shaders make none of these reads (the audit's conservative
 * superset). */
static void stripBounds(const AuditDraw *d, Audit *a, uint32_t k, uint32_t *x, uint32_t *y,
                        int *longStrip)
{
    uint32_t lo = d->stripLo, hi = d->stripHi;
    if (d->vpb != 0u) {
        const uint32_t b0 = (k / d->vpb) * d->vpb;
        lo = lo > b0 ? lo : b0;
        hi = hi < b0 + d->vpb ? hi : b0 + d->vpb;
    }
    *longStrip = 0;
    if (!flagged(d->prog)) {
        *x = lo;
        *y = hi;
        return;
    }
    /* the shader's && short-circuits (DXC, HLSL 2021): the flag is read
       only inside the bounds */
    uint32_t s = k - 2u, n = 0;
    while (s > lo && !stripStart(d, a, s) && n < VU_STRIP_SCAN) {
        s--;
        n++;
    }
    uint32_t e = k + 1u, m = 0;
    while (e < hi && !stripStart(d, a, e) && m < VU_STRIP_SCAN) {
        e++;
        m++;
    }
    *x = s;
    *y = e;
    *longStrip = n >= VU_STRIP_SCAN || m >= VU_STRIP_SCAN;
}

/* vu_*_strip_passes: x, x + 1, then x + 2 .. vu_strip_end - 1 (a long
 * flagged strip returns before reading) */
static void stripWalk(const AuditDraw *d, Audit *a, uint32_t k)
{
    uint32_t x, y;
    int longStrip;
    stripBounds(d, a, k, &x, &y, &longStrip);
    if (longStrip && flagged(d->prog)) {
        return;
    }
    readVertex(d, a, x);
    readVertex(d, a, x + 1u);
    const uint32_t end = y < x + VU_STRIP_SPAN_MAX ? y : x + VU_STRIP_SPAN_MAX;
    for (uint32_t v = x + 2u; v < end; v++) {
        readVertex(d, a, v);
    }
}

/* every read of one draw */
static void auditDraw(const AuditDraw *d, Audit *a)
{
    if (d->prog == RD_PROG_PARTICLE) {
        /* vu_particle(vid / 6): base + 3 .. 5, base + 6 + 2i, base + 7 + 2i */
        for (uint32_t i = 0; i < d->particles; i++) {
            readQw(d, a, 3u, i);
            readQw(d, a, 4u, i);
            readQw(d, a, 5u, i);
            readQw(d, a, 6u + 2u * i, i);
            readQw(d, a, 7u + 2u * i, i);
        }
        return;
    }
    if (d->qpv < 2u && flagged(d->prog)) {
        fail(a, "%u qwords a vertex: the strip flag (qword qpv - 2) is before the vertex (%u)",
             d->qpv, 0);
    }
    /* the strip walk is the same for every kick of a strip: once each */
    uint32_t prev = UINT32_MAX;
    for (uint32_t i = 0; i < d->indexCount; i++) {
        const uint32_t kick = (d->index[i] & ICO_VU_INDEX_MASK) >> 2;
        if (kick < 2u) {
            a->badKicks++;
            fail(a, "index %u: kick %u below 2 (k - 2 reads before vertex 0)", i, kick);
        }
        if (kick == prev) {
            continue; /* the triangle's other corners read the same */
        }
        prev = kick;
        /* the three vertices (and vu_plane_distance's qword 0 of each) */
        readVertex(d, a, kick - 2u);
        readVertex(d, a, kick - 1u);
        readVertex(d, a, kick);
        if (d->stripHi != 0u) {
            stripWalk(d, a, kick);
        }
    }
}

/* ------------------------------------------------------ the index span
 * Issue 95: the index values the device draws (rd_replay.c doVuDraw: the
 * mesh's list, or the grid's made at replay) are vertex numbers to the
 * driver, whatever the shaders make of them.  Vulkan holds them to
 * VkPhysicalDeviceLimits::maxDrawIndexedIndexValue, which is 2^24 - 1 on
 * a device without fullDrawIndexUint32, and a GPU that shades by index
 * range (Mali) sizes a draw's vertex storage by max - min.  Reported per
 * frame: the widest draw, the draws past 2^24 - 1, the values carrying
 * one of the old mark bits (bits 28..30, the marks before v0.8.0); and,
 * failed, a kick listed twice in one draw with different marks (a mark a
 * kick, rd_mesh.c kickMark, is exact only when each kick is one
 * triangle). */
#define AUDIT_SPAN_LIMIT ((1u << 24) - 1u)
/* 1: a span past AUDIT_SPAN_LIMIT fails (the marks live in the stream) */
#ifndef AUDIT_SPAN_GATE
#define AUDIT_SPAN_GATE 0
#endif
#define AUDIT_OLD_LATER 0x40000000u
#define AUDIT_OLD_HIDE 0x20000000u
#define AUDIT_OLD_JOINED 0x10000000u

typedef struct Span {
    uint32_t lo, hi, n;
    uint32_t later, hide, joined; /* values carrying each old mark bit */
    uint32_t marked;              /* triangles with a mark of any kind (marks or bits) */
    uint32_t dupKicks;            /* kicks listed twice with different marks */
} Span;

/* the span of n values (each OR-ed with orMark, as the grid's are) */
static void spanOf(const AuditDraw *d, Span *s)
{
    memset(s, 0, sizeof(*s));
    s->lo = UINT32_MAX;
    uint32_t maxKick = 0;
    for (uint32_t i = 0; i < d->indexCount; i++) {
        const uint32_t v = d->index[i] | d->valueMark;
        s->lo = v < s->lo ? v : s->lo;
        s->hi = v > s->hi ? v : s->hi;
        s->later += (v & AUDIT_OLD_LATER) != 0;
        s->hide += (v & AUDIT_OLD_HIDE) != 0;
        s->joined += (v & AUDIT_OLD_JOINED) != 0;
        const uint32_t k = (v & ICO_VU_INDEX_MASK) >> 2;
        maxKick = k > maxKick ? k : maxKick;
    }
    s->n = d->indexCount;
    /* the mark of each triangle: the old bits, or the draw's mark bytes */
    uint8_t *seen = d->indexCount ? malloc((size_t)maxKick + 1u) : NULL;
    if (seen) {
        memset(seen, 0xFF, (size_t)maxKick + 1u);
    }
    for (uint32_t i = 0; seen && i + 2 < d->indexCount; i += 3) {
        const uint32_t v = d->index[i] | d->valueMark;
        const uint32_t k = (v & ICO_VU_INDEX_MASK) >> 2;
        uint32_t mark = (v >> 28) & 7u;
        if (d->kickMark && k < d->markCount) {
            mark |= d->kickMark[k];
        }
        mark |= d->drawMark;
        s->marked += mark != 0;
        if (seen[k] != 0xFF && seen[k] != mark) {
            s->dupKicks++;
        }
        seen[k] = (uint8_t)mark;
    }
    free(seen);
    if (s->lo == UINT32_MAX) {
        s->lo = 0;
    }
}

/* ---------------------------------------------------- the CPU mirror
 * The programs' CPU references (port/render/vu1_ref) run on each batch of
 * every prelit, lit and skinned mesh draw at the draw's own VU memory
 * (RdVuBlock) and bones: what the PS2's VU would have sent.  Counted per
 * frame, not checked against the GPU: clip W not finite or within 1e-6 of
 * 0, GS X or Y past 16 bits (the GS wraps them), the scissor programs'
 * fans (and the largest), NaN in the bones the draw uploaded or in its VU
 * memory.  Failed: a NaN clip W or a NaN bone (values the program reads);
 * a NaN in VU memory is only counted (a slot the program does not read
 * may hold one). */
typedef struct Mirror {
    uint32_t draws, batches, skipped; /* skipped: a batch past VU_BATCH_MAX */
    uint64_t vertices, wNonFinite, wNan, wTiny, xyWrap;
    uint32_t fans, fanMax;
    uint32_t nanBones, nanMem;
} Mirror;

static bool qwNan(const float *q)
{
    return q[0] != q[0] || q[1] != q[1] || q[2] != q[2] || q[3] != q[3];
}

static void mirrorDraw(const RdFrame *f, const RdCmd *c, Mirror *mt, const char *where)
{
    RdVuPayload p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    if (c->type != RDC_MESH && c->type != RDC_SKINNED) {
        return;
    }
    const int skin = p.prog == RD_PROG_SKIN || p.prog == RD_PROG_SKIN_SPEC;
    const int lit =
        p.prog == RD_PROG_LIT || p.prog == RD_PROG_LIT_SPEC || p.prog == RD_PROG_REFLECT;
    if (!skin && !lit && p.prog != RD_PROG_PRELIT) {
        return;
    }
    const RdMeshRec *m = rd__mesh_rec(c->u[0]);
    if (!m || !m->vu || p.firstBatch >= m->batchCount) {
        return;
    }
    RdVuBlock blk;
    memcpy(&blk, f->payload + c->u[1] + sizeof(p), sizeof(blk));
    const float (*bones)[4] =
        (const float (*)[4])(f->payload + c->u[1] + sizeof(p) + sizeof(RdVuBlock));
    const uint32_t boneQw = p.boneQw > 240u ? 240u : p.boneQw;
    for (int q = 0; q < 36; q++) {
        /* counted, not failed: a slot the program does not read may hold
           anything (the title logo's prelit draws leave NaN in 24..27, the
           model-to-view matrix normal_c does not use) */
        if (qwNan(blk.mem[q])) {
            mt->nanMem++;
            break;
        }
    }
    static Vu1Ref r;
    vu1ref_init(&r);
    vu1ref_load_common(&r, (const float (*)[4])blk.mem);
    if (skin) {
        /* SET_CLUSTER_MATRIX: n + 1 qwords from qw[1] to mem[16..] */
        static float pk[242][4];
        memset(pk, 0, sizeof(pk));
        const uint32_t n1 = boneQw ? boneQw : 1u;
        memcpy(&pk[0][0], &n1, 4);
        pk[0][3] = blk.mem[2][3];
        for (uint32_t b = 0; b < boneQw; b++) {
            memcpy(pk[1 + b], bones[b], 16);
            if (qwNan(bones[b])) {
                mt->nanBones++;
            }
        }
        CHECK(mt->nanBones == 0, "%s: a bone qword is NaN", where);
        vu1ref_cluster_set_matrix(&r, (const float (*)[4])pk);
        vu1ref_cluster_set_light(&r, (const float (*)[4])(blk.mem + 28));
    } else {
        vu1ref_normal_set_matrix(&r, (const float (*)[4])(blk.mem + 16));
        vu1ref_normal_set_light(&r, (const float (*)[4])(blk.mem + 28));
    }
    uint32_t last = p.firstBatch + p.batchCount;
    if (last > m->batchCount || p.batchCount == 0) {
        last = m->batchCount;
    }
    mt->draws++;
    static float in[1 + VU_BATCH_MAX * 5][4];
    static VuBatchOut out;
    const uint32_t qpv = m->qwPerVertex;
    for (uint32_t b = p.firstBatch; b < last; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        const uint32_t nv = br->vertexCount;
        if (nv == 0 || (uint64_t)br->firstVertex + nv > m->vertexCount || nv > VU_BATCH_MAX ||
            qpv > 5u) {
            mt->skipped++;
            continue;
        }
        /* the batch at TOP: the common tag with NLOOP = the vertices */
        memcpy(in[0], blk.mem[3], 16);
        uint32_t t0;
        memcpy(&t0, &in[0][0], 4);
        t0 = (t0 & ~0x7FFFu) | (nv & 0x7FFFu);
        memcpy(&in[0][0], &t0, 4);
        memcpy(in[1], m->stream[(size_t)br->firstVertex * qpv], (size_t)nv * qpv * 16);
        memset(&out, 0, sizeof(out));
        if (skin) {
            vu1ref_cluster(&r, p.code, (const float (*)[4])in, &out);
        } else if (lit) {
            vu1ref_normal_l(&r, p.code, (const float (*)[4])in, &out);
        } else {
            vu1ref_normal_c(&r, p.code, (const float (*)[4])in, &out);
        }
        mt->batches++;
        for (int i = 0; i < out.count; i++) {
            const VuGsVertex *v = &out.v[i];
            const float w = v->clipW;
            mt->vertices++;
            if (w != w) {
                mt->wNan++;
                CHECK(0, "%s: batch %u vertex %d: the reference's clip w is NaN", where,
                      (unsigned)b, i);
            }
            if (w != w || w - w != 0.0f) {
                mt->wNonFinite++;
            } else if (w > -1e-6f && w < 1e-6f) {
                mt->wTiny++;
            }
            mt->xyWrap +=
                v->xyz[0] < 0 || v->xyz[0] > 0xFFFF || v->xyz[1] < 0 || v->xyz[1] > 0xFFFF;
        }
        mt->fans += (uint32_t)out.fanCount;
        for (int i = 0; i < out.fanCount; i++) {
            mt->fanMax =
                (uint32_t)out.fans[i].n > mt->fanMax ? (uint32_t)out.fans[i].n : mt->fanMax;
        }
    }
}

/* ---------------------------------------------------- list 0's textures
 * Every texture a list-0 command binds (RDC_TEXTURE), once: its id, size,
 * texel format, mip levels and whether the CPU copy is there. */
static void listTextures(const RdFrame *f)
{
    uint32_t seen[256];
    uint32_t n = 0;
    const RdCmdList *cl = &f->lists[0];
    for (uint32_t i = 0; i < cl->count; i++) {
        const RdCmd *c = &cl->cmds[i];
        if (c->type != RDC_TEXTURE) {
            continue;
        }
        bool dup = false;
        for (uint32_t k = 0; k < n && !dup; k++) {
            dup = seen[k] == c->u[0];
        }
        if (dup || n == 256) {
            continue;
        }
        seen[n++] = c->u[0];
        const RdTexRec *t = rd__tex_rec(c->u[0]);
        printf("rd_stream_audit:   list 0 texture %u \"%.24s\" %ux%u format %u mips %u pixels %s\n",
               c->u[0], t ? t->name : "", t ? t->w : 0u, t ? t->h : 0u, t ? t->format : 0u,
               t ? t->mipLevels : 0u, t && t->pixels ? "present" : "absent");
    }
    printf("rd_stream_audit:   list 0 binds %u textures\n", n);
}

/* ------------------------------------------------------------ the dumps */

typedef struct Totals {
    uint32_t dumps, draws, meshDraws, gridDraws, particleDraws, bad, boneOutside;
    /* skinned draws, those whose bones fill VuBoneCB (no card bits), and
     * the weights' addresses seen (lowest, highest) */
    uint32_t skinDraws, skinFull, addrLo, addrHi;
    uint64_t reads;
    int shortenedDone;
    /* the index span (spanOf), over every frame */
    uint32_t spanMax, spanOver, spanMarkedValues, dupKicks;
    char spanWhere[240];
    Mirror mirror;
} Totals;

/* doVuDraw's words for command c of frame f; false for a draw the replay
 * skips (no mesh, a batch past the mesh's), with *why set when the command
 * itself is out of range */
static bool drawOf(const RdFrame *f, const RdCmd *c, AuditDraw *d, uint32_t **gridIndex,
                   const char **why, char *name, size_t nameSize)
{
    RdVuPayload p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const uint8_t *stream =
        f->payload + c->u[1] + sizeof(p) + sizeof(RdVuBlock) + (size_t)p.boneQw * 16;
    memset(d, 0, sizeof(*d));
    *why = NULL;
    d->prog = p.prog;
    d->boneQw = p.boneQw > 240u ? 240u : p.boneQw;
    snprintf(name, nameSize, "%s", "stream");
    if (c->type == RDC_MESH || c->type == RDC_SKINNED) {
        const RdMeshRec *m = rd__mesh_rec(c->u[0]);
        if (!m || !m->vu || p.firstBatch >= m->batchCount) {
            return false;
        }
        snprintf(name, nameSize, "%.24s", m->name);
        uint32_t last = p.firstBatch + p.batchCount;
        if (last > m->batchCount || p.batchCount == 0) {
            last = m->batchCount;
        }
        const RdVuBatchRec *b0 = &m->batches[p.firstBatch], *b1 = &m->batches[last - 1];
        d->stream = (const float (*)[4])m->stream;
        d->streamQw = m->vertexCount * m->qwPerVertex;
        d->qpv = m->qwPerVertex;
        /* the strip bounds as the replay sets them, the widest (the
           content table's tiles drawn: rd__vu_sheet_end extended) */
        d->stripLo = b0->firstVertex;
        d->stripHi = b1->firstVertex + b1->vertexCount;
        const uint32_t first = b0->firstIndex, end = b1->firstIndex + b1->indexCount;
        if (end < first || end > m->indexCount) {
            *why = "the draw's index range is past the mesh's index list";
            return true;
        }
        d->index = rd__mesh_draw_index(m) + first;
        d->indexCount = end - first;
        return true;
    }
    d->stream = (const float (*)[4])stream;
    d->streamQw = p.streamQw;
    d->qpv = p.qwPerVertex;
    if (c->type == RDC_PARTICLES) {
        d->prog = RD_PROG_PARTICLE;
        d->particles = p.vertsPerBatch;
        return true;
    }
    /* RDC_GRID: the Mesh3D buffer as it is, its indices made here as the
       replay makes them */
    d->vpb = p.vertsPerBatch;
    d->batchHead = 3;
    d->batchTail = 1;
    d->stripLo = 0;
    d->stripHi = p.batchCount * p.vertsPerBatch;
    const uint32_t ni = p.batchCount * (p.vertsPerBatch - 2u) * 3u;
    uint32_t *idx = malloc((size_t)(ni ? ni : 1u) * 4u);
    if (!idx) {
        return false;
    }
    for (uint32_t b = 0, k = 0; b < p.batchCount; b++) {
        for (uint32_t v = 2; v < p.vertsPerBatch; v++) {
            for (uint32_t cc = 0; cc < 3; cc++) {
                idx[k++] = ICO_VU_INDEX(b * p.vertsPerBatch + v, cc);
            }
        }
    }
    *gridIndex = idx;
    /* a grid of two strips or more: every value carries the joined mark
       (rd_replay.c doVuDraw) */
    d->valueMark = p.batchCount >= 2 ? AUDIT_OLD_JOINED : 0u;
    d->index = idx;
    d->indexCount = ni;
    return true;
}

static const char *kindName(uint8_t t)
{
    return t == RDC_MESH      ? "mesh"
           : t == RDC_SKINNED ? "skinned"
           : t == RDC_GRID    ? "grid"
                              : "particles";
}

static void auditFrame(const char *path, const RdFrame *f, Totals *t)
{
    uint32_t draws = 0, bad = 0;
    /* the frame's index spans */
    uint32_t fMax = 0, fOver = 0, fMarkedDraws = 0, fLater = 0, fHide = 0, fJoined = 0;
    uint32_t fOverList[RD_LIST_COUNT] = {0}, fDrawsList[RD_LIST_COUNT] = {0};
    char fWhere[240] = "";
    const Mirror m0 = t->mirror;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *c = &f->lists[l].cmds[i];
            if (c->type != RDC_MESH && c->type != RDC_SKINNED && c->type != RDC_GRID &&
                c->type != RDC_PARTICLES) {
                continue;
            }
            AuditDraw d;
            uint32_t *gridIndex = NULL;
            const char *why = NULL;
            char name[32], where[200];
            if (!drawOf(f, c, &d, &gridIndex, &why, name, sizeof(name))) {
                continue;
            }
            snprintf(where, sizeof(where), "%s list %d command %u (%s %s, prog %u)", path, l, i,
                     kindName(c->type), name, (unsigned)d.prog);
            Audit a;
            memset(&a, 0, sizeof(a));
            a.where = where;
            a.addrLo = UINT32_MAX;
            if (why) {
                failText(&a, why);
            } else {
                auditDraw(&d, &a);
            }
            if (!why && d.index && d.indexCount) {
                Span sp;
                spanOf(&d, &sp);
                const uint32_t span = sp.hi - sp.lo;
                if (span >= fMax) {
                    fMax = span;
                    snprintf(fWhere, sizeof(fWhere), "list %d command %u (%s %s, prog %u)", l, i,
                             kindName(c->type), name, (unsigned)d.prog);
                }
                fOver += span > AUDIT_SPAN_LIMIT;
                fOverList[l] += span > AUDIT_SPAN_LIMIT;
                fDrawsList[l]++;
                if (span > AUDIT_SPAN_LIMIT && getenv("ICO_AUDIT_SPANS")) {
                    /* ICO_AUDIT_SPANS=1: each such draw */
                    printf("rd_stream_audit:     %s: values %u..%u, old mark bits later %u hide %u "
                           "joined %u\n",
                           where, sp.lo, sp.hi, sp.later, sp.hide, sp.joined);
                }
                fMarkedDraws += sp.marked != 0;
                fLater += sp.later;
                fHide += sp.hide;
                fJoined += sp.joined;
                t->spanMarkedValues += sp.later + sp.hide + sp.joined;
                t->dupKicks += sp.dupKicks;
                CHECK(sp.dupKicks == 0, "%s: %u kicks listed twice with different marks", where,
                      sp.dupKicks);
#if AUDIT_SPAN_GATE
                CHECK(span <= AUDIT_SPAN_LIMIT,
                      "%s: index values %u..%u, a span past 2^24 - 1 (Vulkan's least "
                      "maxDrawIndexedIndexValue)",
                      where, sp.lo, sp.hi);
#endif
                if (span > t->spanMax) {
                    t->spanMax = span;
                    snprintf(t->spanWhere, sizeof(t->spanWhere), "%s", where);
                }
            }
            if (!why) {
                mirrorDraw(f, c, &t->mirror, where);
            }
            draws++;
            t->draws++;
            t->reads += a.reads;
            t->boneOutside += a.boneOutside;
            if (a.boneUsed) {
                t->skinDraws++;
                t->skinFull += a.boneUsed >= 240u;
                t->addrLo = a.addrLo < t->addrLo ? a.addrLo : t->addrLo;
                t->addrHi = a.addrHi > t->addrHi ? a.addrHi : t->addrHi;
            }
            t->meshDraws += c->type == RDC_MESH || c->type == RDC_SKINNED;
            t->gridDraws += c->type == RDC_GRID;
            t->particleDraws += c->type == RDC_PARTICLES;
            if (a.bad) {
                bad++;
                t->bad++;
                printf("FAIL: %s (%u reads past the end)\n", a.first, a.bad);
                failures++;
            }
            /* the self-test on real data: the first mesh draw with a
               complete stream, its stream one vertex short, must fail */
            if (!t->shortenedDone && !why && !a.bad && c->type == RDC_MESH && d.indexCount &&
                d.qpv) {
                AuditDraw s = d;
                Audit b;
                memset(&b, 0, sizeof(b));
                b.where = where;
                uint32_t maxKick = 0;
                for (uint32_t k = 0; k < d.indexCount; k++) {
                    const uint32_t kk = (d.index[k] & ICO_VU_INDEX_MASK) >> 2;
                    maxKick = kk > maxKick ? kk : maxKick;
                }
                s.streamQw = maxKick * d.qpv; /* the drawn last vertex cut off */
                auditDraw(&s, &b);
                CHECK(b.bad > 0,
                      "%s: the draw with its last drawn vertex cut from the stream "
                      "passed the audit",
                      where);
                if (b.bad > 0) {
                    printf("rd_stream_audit: self-test on %s: the shortened stream fails (%s)\n",
                           where, b.first);
                }
                t->shortenedDone = 1;
            }
            free(gridIndex);
        }
    }
    printf("rd_stream_audit: %s: frame %u, %u VU draws, %u with reads past the end\n", path,
           f->number, draws, bad);
    t->spanOver += fOver;
    printf("rd_stream_audit:   index span: widest %u (%s), %u draws past 2^24 - 1; %u draws "
           "with marked triangles; values with the old mark bits: later %u, hide %u, joined "
           "%u\n",
           fMax, fWhere[0] ? fWhere : "none", fOver, fMarkedDraws, fLater, fHide, fJoined);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (fDrawsList[l]) {
            printf("rd_stream_audit:   list %d: %u indexed VU draws, %u past 2^24 - 1\n", l,
                   fDrawsList[l], fOverList[l]);
        }
    }
    const Mirror *m1 = &t->mirror;
    printf("rd_stream_audit:   CPU mirror: %u draws, %u batches (%u past VU_BATCH_MAX), %llu "
           "vertices: clip w not finite %llu, |w| < 1e-6 %llu, GS X/Y past 16 bits %llu; %u "
           "fans (largest %u); NaN bone qwords %u, NaN VU memory %u\n",
           m1->draws - m0.draws, m1->batches - m0.batches, m1->skipped - m0.skipped,
           (unsigned long long)(m1->vertices - m0.vertices),
           (unsigned long long)(m1->wNonFinite - m0.wNonFinite),
           (unsigned long long)(m1->wTiny - m0.wTiny), (unsigned long long)(m1->xyWrap - m0.xyWrap),
           m1->fans - m0.fans, m1->fanMax, m1->nanBones - m0.nanBones, m1->nanMem - m0.nanMem);
    listTextures(f);
}

static bool endsWith(const char *s, const char *suffix)
{
    const size_t n = strlen(s), m = strlen(suffix);
    return n >= m && (strcmp(s + n - m, suffix) == 0);
}

static void auditPath(const char *path, Totals *t)
{
    char tmp[1100];
    const char *load = path;
    tmp[0] = '\0';
    FILE *probe = fopen(path, "rb");
    if (!probe) {
        printf("rd_stream_audit: %s: missing, skipped\n", path);
        return;
    }
    fclose(probe);
    if (endsWith(path, ".zip") || endsWith(path, ".ZIP")) {
        size_t size = 0;
        void *data = ico_dump_zip_extract(path, &size, NULL, 0);
        if (!data) {
            printf("rd_stream_audit: %s: no .rddump in this zip, skipped\n", path);
            return;
        }
        snprintf(tmp, sizeof(tmp), "rd_stream_audit_%u.rddump", (unsigned)t->dumps);
        FILE *fp = fopen(tmp, "wb");
        const bool ok = fp && fwrite(data, 1, size, fp) == size;
        if (fp) {
            fclose(fp);
        }
        free(data);
        if (!ok) {
            printf("rd_stream_audit: %s: cannot write %s, skipped\n", path, tmp);
            remove(tmp);
            return;
        }
        load = tmp;
    }
    RdFrame f;
    if (!rd__load_frame(load, &f)) {
        CHECK(0, "%s: not a dump this version reads", path);
    } else {
        t->dumps++;
        auditFrame(path, &f, t);
        rd__frame_free(&f);
    }
    if (tmp[0]) {
        remove(tmp);
    }
}

/* a dump list: one path a line, '#' comments, relative to the list's
 * folder, '*' globs (tools/native_frame_dumps.txt) */
static void auditList(const char *list, Totals *t)
{
    FILE *fp = fopen(list, "r");
    if (!fp) {
        printf("rd_stream_audit: list %s: missing, skipped\n", list);
        return;
    }
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", list);
    char *slash = strrchr(dir, '/');
    if (slash) {
        slash[1] = '\0';
    } else {
        dir[0] = '\0';
    }
    char line[1100];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0' || *s == '#') {
            continue;
        }
        char path[2200];
        snprintf(path, sizeof(path), "%s%s", *s == '/' ? "" : dir, s);
        if (strchr(path, '*')) {
#if !defined(_WIN32)
            glob_t g;
            if (glob(path, 0, NULL, &g) == 0) {
                for (size_t k = 0; k < g.gl_pathc; k++) {
                    auditPath(g.gl_pathv[k], t);
                }
                globfree(&g);
            } else {
                printf("rd_stream_audit: %s: nothing matches, skipped\n", path);
            }
#else
            printf("rd_stream_audit: %s: no globs here, skipped\n", path);
#endif
            continue;
        }
        auditPath(path, t);
    }
    fclose(fp);
}

/* ------------------------------------------------------------ self-test */

/* a prelit strip of n vertices (3 qwords each), its first vertex flagged,
 * kicks 2 .. n - 1 */
static void selfTest(void)
{
    enum { N = 6, QPV = 3 };

    static float stream[N * QPV][4];
    static uint32_t index[(N - 2) * 3];
    memset(stream, 0, sizeof(stream));
    for (uint32_t v = 0; v < N; v++) {
        stream[v * QPV + 1][3] = v == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
    }
    for (uint32_t k = 2, i = 0; k < N; k++) {
        for (uint32_t c = 0; c < 3; c++) {
            index[i++] = ICO_VU_INDEX(k, c);
        }
    }
    AuditDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.stream = (const float (*)[4])stream;
    d.streamQw = N * QPV;
    d.qpv = QPV;
    d.stripLo = 0;
    d.stripHi = N;
    d.index = index;
    d.indexCount = (N - 2) * 3;
    Audit a;
    memset(&a, 0, sizeof(a));
    a.where = "self-test";
    auditDraw(&d, &a);
    CHECK(a.bad == 0 && a.reads > 0, "self-test: the whole strip read %u qwords past the end (%s)",
          a.bad, a.first);
    /* one vertex short: the last kick reads past the end */
    AuditDraw s = d;
    s.streamQw = (N - 1) * QPV;
    memset(&a, 0, sizeof(a));
    a.where = "self-test (short)";
    auditDraw(&s, &a);
    CHECK(a.bad > 0, "self-test: a stream one vertex short passed");
    /* the strip bounds one past the stream: the scan stays inside them,
       so the walk reads the vertex past the end */
    s = d;
    s.streamQw = (N - 1) * QPV;
    s.index = index;
    s.indexCount = (N - 3) * 3; /* the kicks inside the stream only */
    memset(&a, 0, sizeof(a));
    a.where = "self-test (bounds)";
    auditDraw(&s, &a);
    CHECK(a.bad > 0, "self-test: strip bounds past the stream passed");
    /* a kick below 2 */
    static const uint32_t low[3] = {ICO_VU_INDEX(1, 0), ICO_VU_INDEX(1, 1), ICO_VU_INDEX(1, 2)};
    s = d;
    s.index = low;
    s.indexCount = 3;
    memset(&a, 0, sizeof(a));
    a.where = "self-test (kick)";
    auditDraw(&s, &a);
    CHECK(a.badKicks == 3 && a.bad > 0, "self-test: a kick below 2 passed");
    /* a particle batch one particle short */
    AuditDraw p;
    memset(&p, 0, sizeof(p));
    p.prog = RD_PROG_PARTICLE;
    p.stream = (const float (*)[4])stream;
    p.particles = 4;
    p.streamQw = 6 + 2 * 4;
    memset(&a, 0, sizeof(a));
    a.where = "self-test (particles)";
    auditDraw(&p, &a);
    CHECK(a.bad == 0, "self-test: four particles in their 14 qwords failed");
    p.streamQw = 6 + 2 * 3;
    memset(&a, 0, sizeof(a));
    auditDraw(&p, &a);
    CHECK(a.bad > 0, "self-test: four particles in 12 qwords passed");
}

int main(int argc, char **argv)
{
    selfTest();
    if (failures) {
        printf("rd_stream_audit_test: the self-test FAILED\n");
        return 1;
    }
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    Totals t;
    memset(&t, 0, sizeof(t));
    t.addrLo = UINT32_MAX;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc) {
            auditList(argv[++i], &t);
        } else {
            auditPath(argv[i], &t);
        }
    }
    rd_shutdown();
    printf("rd_stream_audit: %u dumps, %u VU draws (%u meshes, %u grids, %u particle batches), "
           "%llu stream reads, %u draws with reads past the end; %u bone addresses past the "
           "uploaded bones (read as zero, not past the block)\n",
           t.dumps, t.draws, t.meshDraws, t.gridDraws, t.particleDraws, (unsigned long long)t.reads,
           t.bad, t.boneOutside);
    /* reported: a skinned draw whose weights reach bone 59 (address 252) or
       wrap below address 16 has no room in VuBoneCB for the card bits
       (rd_replay.c doVuDraw draws it with no card) */
    if (t.skinDraws) {
        printf("rd_stream_audit: %u skinned draws, weight addresses %u..%u (bone %d..%d); %u "
               "with their bones filling VuBoneCB (no card bits)\n",
               t.skinDraws, t.addrLo, t.addrHi, ((int)t.addrLo - 16) / 4, ((int)t.addrHi - 16) / 4,
               t.skinFull);
    }
    printf("rd_stream_audit: index span: widest %u (%s), %u draws past 2^24 - 1 (%s), %u values "
           "with the old mark bits, %u kicks listed twice with different marks\n",
           t.spanMax, t.spanWhere[0] ? t.spanWhere : "none", t.spanOver,
           AUDIT_SPAN_GATE ? "failed" : "reported", t.spanMarkedValues, t.dupKicks);
    printf("rd_stream_audit: CPU mirror: %u draws, %llu vertices, clip w not finite %llu, "
           "|w| < 1e-6 %llu, GS X/Y past 16 bits %llu, %u fans (largest %u), NaN bones %u\n",
           t.mirror.draws, (unsigned long long)t.mirror.vertices,
           (unsigned long long)t.mirror.wNonFinite, (unsigned long long)t.mirror.wTiny,
           (unsigned long long)t.mirror.xyWrap, t.mirror.fans, t.mirror.fanMax, t.mirror.nanBones);
    if (failures) {
        printf("rd_stream_audit_test: FAILED\n");
        return 1;
    }
    if (t.dumps == 0) {
        printf("rd_stream_audit_test: no dump read; the self-test passed (skipped)\n");
        return 77;
    }
    CHECK(t.shortenedDone, "no mesh draw to shorten in the dumps");
    printf("rd_stream_audit_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
