/* rd_stream_audit_test.c: the VU stream bounds audit (issue 95;
 * docs/port/RENDER.md, "The stream bounds audit").
 *
 * The VU vertex shaders (port/shaders/vu_*.hlsl) read the vertex stream at
 * t0 (vu_stream) through indices they compute: a triangle's three vertices
 * from its kick (vu_vertex_qw), a batch's header qword (a grid's colour),
 * the strip test's scans and walks (vu_strip_of, vu_*_strip_passes), a
 * particle's header and its two qwords.  A GPU that reads storage buffers
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
 * it); the tick cameras (vu_ticks[4k .. 4k + 3], k < 2) and VU memory
 * (vu_mem[0..35]) are fixed-size constant blocks, checked by the static
 * asserts below.
 *
 * The strip test is assumed to run for every kick (under VU_F_EXTENDED
 * it runs for a triangle the region rule sends to the card test, which
 * depends on the camera): the audit is the worst case over cameras.
 *
 * The strip bounds come from one function, stripBounds: today it runs
 * vu_strip_of's scans; a stream that carries its strips' bounds (a table
 * per kick) replaces that function with the table's reader and adds the
 * table's own qwords to the reads.
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
} AuditDraw;

typedef struct Audit {
    const char *where; /* the draw, for the message */
    uint64_t reads;
    uint32_t bad;
    uint32_t badKicks;
    uint32_t boneOutside;
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

#define VU_STRIP_SCAN 256u      /* vu_common.hlsli */
#define VU_STRIP_SPAN_MAX 1024u /* vu_common.hlsli */

/* The strip of kick k: vu_strip_of (x its first vertex, y one past its
 * last, *longStrip its z), the scans' reads checked.  The one function a
 * stream carrying its strips' bounds replaces. */
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

/* ------------------------------------------------------------ the dumps */

typedef struct Totals {
    uint32_t dumps, draws, meshDraws, gridDraws, particleDraws, bad, boneOutside;
    uint64_t reads;
    int shortenedDone;
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
            if (why) {
                failText(&a, why);
            } else {
                auditDraw(&d, &a);
            }
            draws++;
            t->draws++;
            t->reads += a.reads;
            t->boneOutside += a.boneOutside;
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
