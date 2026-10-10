/* rd_mesh.c: rd_mesh.h's recording half.
 *
 *   the mesh registry   rd_create_vu_mesh builds a mesh once from the VIF
 *                       UNPACK payloads Packet.c packed: the vertex stream
 *                       without the batches' GIF tags and the static index
 *                       list (ICO_VU_INDEX(kick, corner), the strip rule
 *                       vu1ref_static_kicks encodes).  Replay uploads both
 *                       into the frame's ring once per replayed frame.
 *   the VU image        one per list, at record time: VU1 data memory and
 *                       the VF registers the SET_* routines load (a Vu1Ref,
 *                       vu1_ref.h), the resident program and the last BEGIN
 *                       code (rd_mesh.h, "VU1 state per list").
 *   the draws           rd_draw_vu_mesh / rd_draw_vu_grid / rd_draw_vu_particles
 *                       record RDC_MESH / RDC_SKINNED / RDC_GRID /
 *                       RDC_PARTICLES with the RdVuPayload layout of
 *                       rd_mesh.h; rd_replay.c draws them.
 *   the pipelines       the (program, code) table and the VU families of
 *                       the reachable pipeline set.
 *
 * The particle end-tag quirk: a particle batch
 * that stores no sprite writes its EOP tag through an earlier program's vi04,
 * which after a normal or mesh loop is VU memory 0, and the cluster region
 * test then rejects every skinned triangle of the list until the next common
 * block.  Not reproduced (the shaders take mem[0..1] from the list image,
 * which keeps the common block): it would make skinned characters vanish for
 * the rest of a list on the PS2 behind any fully culled particle batch drawn
 * before them in the same list, an effect no stage is known to rely on.
 * rd_draw_vu_particles runs the CPU reference on a copy of the list image to
 * see whether a batch would have clobbered the common block, and a skinned
 * draw later in that list then logs once. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"
#include "vu1_ref.h"
#include "xxh3.h"

/* ------------------------------------------------------------- registry */

RdMeshRec *rd__mesh_rec(uint32_t id)
{
    uint32_t slot = (id & 0xFFFF) - 1;
    if (id == 0 || slot >= RD_MAX_MESHES || !g_rd.meshes) {
        return NULL;
    }
    RdMeshRec *m = &g_rd.meshes[slot];
    return m->live && m->gen == (id >> 16) ? m : NULL;
}

/* ---------------------------------------------------- the device arena */

typedef struct ArenaSpan {
    uint64_t off, size;
} ArenaSpan;

typedef struct ArenaChunk {
    RhiBuffer buf;
    uint64_t size;
    ArenaSpan *free; /* sorted by offset, never adjacent */
    uint32_t freeCount, freeCap;
} ArenaChunk;

static ArenaChunk s_arena[RD_MESH_CHUNKS];

static bool spanInsert(ArenaChunk *c, uint32_t at, uint64_t off, uint64_t size)
{
    if (c->freeCount == c->freeCap) {
        const uint32_t cap = c->freeCap ? c->freeCap * 2 : 64;
        ArenaSpan *p = realloc(c->free, cap * sizeof(*p));
        if (!p) {
            return false;
        }
        c->free = p;
        c->freeCap = cap;
    }
    memmove(&c->free[at + 1], &c->free[at], (c->freeCount - at) * sizeof(ArenaSpan));
    c->free[at].off = off;
    c->free[at].size = size;
    c->freeCount++;
    return true;
}

static void spanRemove(ArenaChunk *c, uint32_t at)
{
    memmove(&c->free[at], &c->free[at + 1], (c->freeCount - at - 1) * sizeof(ArenaSpan));
    c->freeCount--;
}

/* first fit in c of size bytes at an offset aligned to align */
static bool chunkAlloc(ArenaChunk *c, uint64_t size, uint64_t align, uint64_t *out)
{
    for (uint32_t i = 0; i < c->freeCount; i++) {
        ArenaSpan *f = &c->free[i];
        const uint64_t a = (f->off + align - 1) / align * align;
        if (a + size > f->off + f->size) {
            continue;
        }
        const uint64_t head = a - f->off, tail = f->off + f->size - (a + size);
        if (head == 0 && tail == 0) {
            spanRemove(c, i);
        } else if (head == 0) {
            f->off = a + size;
            f->size = tail;
        } else {
            f->size = head;
            if (tail && !spanInsert(c, i + 1, a + size, tail)) {
                f->size = head + size + tail; /* undone: no memory for the span */
                return false;
            }
        }
        *out = a;
        return true;
    }
    return false;
}

static void chunkFree(ArenaChunk *c, uint64_t off, uint64_t size)
{
    uint32_t i = 0;
    while (i < c->freeCount && c->free[i].off < off) {
        i++;
    }
    const bool joinPrev = i > 0 && c->free[i - 1].off + c->free[i - 1].size == off;
    const bool joinNext = i < c->freeCount && off + size == c->free[i].off;
    if (joinPrev && joinNext) {
        c->free[i - 1].size += size + c->free[i].size;
        spanRemove(c, i);
    } else if (joinPrev) {
        c->free[i - 1].size += size;
    } else if (joinNext) {
        c->free[i].off = off;
        c->free[i].size += size;
    } else {
        spanInsert(c, i, off, size); /* lost on no memory: a leak of arena space only */
    }
}

static void meshGpuRelease(RdMeshRec *m)
{
    if (m->gpuChunk && m->gpuChunk <= RD_MESH_CHUNKS && s_arena[m->gpuChunk - 1].buf.id) {
        chunkFree(&s_arena[m->gpuChunk - 1], m->gpuOff, m->gpuSize);
    }
    m->gpuChunk = 0;
    m->gpuOff = m->gpuIndexOff = m->gpuSize = 0;
}

bool rd__mesh_gpu_reserve(RdMeshRec *m, uint64_t streamBytes, uint64_t indexBytes, uint64_t align)
{
    if (!g_rd.hasDevice) {
        return false;
    }
    const uint64_t indexAt = (streamBytes + 15) / 16 * 16;
    const uint64_t need = indexAt + (indexBytes ? indexBytes : 4);
    if (m->gpuChunk && m->gpuSize >= need) {
        m->gpuIndexOff = m->gpuOff + indexAt;
        return true;
    }
    meshGpuRelease(m);
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < RD_MESH_CHUNKS; i++) {
            ArenaChunk *c = &s_arena[i];
            if (!c->buf.id) {
                if (pass == 0) {
                    continue; /* existing chunks first */
                }
                const uint64_t size = need > RD_MESH_CHUNK ? need : RD_MESH_CHUNK;
                c->buf = rhi_create_buffer(
                    &(RhiBufferDesc){size, RHI_BUF_INDEX | RHI_BUF_STORAGE_READ | RHI_BUF_COPY_DST,
                                     RHI_MEM_DEVICE, "rd mesh arena"});
                if (!c->buf.id) {
                    return false;
                }
                c->size = size;
                c->freeCount = 0;
                if (!spanInsert(c, 0, 0, size)) {
                    return false;
                }
            }
            uint64_t off;
            if (chunkAlloc(c, need, align, &off)) {
                m->gpuChunk = (uint8_t)(i + 1);
                m->gpuOff = off;
                m->gpuSize = need;
                m->gpuIndexOff = off + indexAt;
                return true;
            }
        }
    }
    rd__log_once(RD_ONCE_MESH_ARENA,
                 "mesh arena full (%u chunks): meshes past it are drawn from "
                 "the upload ring",
                 RD_MESH_CHUNKS);
    return false;
}

RhiBuffer rd__mesh_gpu_buffer(uint32_t chunk)
{
    return chunk && chunk <= RD_MESH_CHUNKS ? s_arena[chunk - 1].buf : (RhiBuffer){0};
}

uint64_t rd__mesh_gpu_buffer_size(uint32_t chunk)
{
    return chunk && chunk <= RD_MESH_CHUNKS ? s_arena[chunk - 1].size : 0;
}

void rd__mesh_gpu_shutdown(void)
{
    for (uint32_t i = 0; i < RD_MESH_CHUNKS; i++) {
        if (s_arena[i].buf.id && g_rd.hasDevice) {
            rhi_destroy_buffer(s_arena[i].buf);
        }
        free(s_arena[i].free);
    }
    memset(s_arena, 0, sizeof(s_arena));
    if (g_rd.meshes) {
        for (uint32_t i = 0; i < RD_MAX_MESHES; i++) {
            RdMeshRec *m = &g_rd.meshes[i];
            m->gpuChunk = 0;
            m->gpuOff = m->gpuIndexOff = m->gpuSize = 0;
        }
    }
}

/* meshes rd_vu_mesh_retire marked stale and not yet freed */
static uint32_t s_staleCount;

static void meshFree(RdMeshRec *m)
{
    if (m->stale && s_staleCount) {
        s_staleCount--;
    }
    m->stale = 0;
    meshGpuRelease(m); /* its span of the device arena */
    for (int i = 0; i < 2; i++) {
        free(m->hist[i].stream);
        m->hist[i].stream = NULL;
    }
    free(m->stream);
    free(m->index);
    free(m->drawIndex);
    free(m->batches);
    m->stream = NULL;
    m->index = NULL;
    m->drawIndex = NULL;
    m->batches = NULL;
    m->live = 0;
    m->vu = 0;
}

/* Frees VU meshes no frame has drawn for a while when the registry runs
 * full: the seki side keeps the packets and builds a mesh again on its next
 * draw (rd_vu_mesh_valid).  Meshes drawn in the open frame or the two
 * retained ones are kept. */
static void evict(void)
{
    uint32_t freed = 0;
    for (uint32_t age = 600; age >= 3 && freed == 0; age /= 2) {
        for (uint32_t i = 0; i < RD_MAX_MESHES; i++) {
            RdMeshRec *m = &g_rd.meshes[i];
            if (m->live && m->vu && m->lastUsed + age < g_rd.frameCounter) {
                meshFree(m);
                freed++;
            }
        }
    }
    rd__log_once(RD_ONCE_VU_MESHES, "mesh registry full: %u meshes not drawn lately evicted",
                 freed);
}

static RdMeshRec *meshAlloc(uint32_t *id)
{
    if (!g_rd.meshes) {
        return NULL;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < RD_MAX_MESHES; i++) {
            RdMeshRec *m = &g_rd.meshes[i];
            if (m->live) {
                continue;
            }
            uint32_t gen = (m->gen + 1) & 0xFFFF;
            memset(m, 0, sizeof(*m));
            m->gen = gen ? gen : 1;
            m->live = 1;
            m->lastUsed = g_rd.frameCounter;
            *id = (m->gen << 16) | (i + 1);
            return m;
        }
        evict();
    }
    return NULL;
}

/* ------------------------------------------- coplanar overlaps (issue 25)
 *
 * The near railing's lattice (st13b_p3, one code-32 draw: ATE GREATER 0x60,
 * AFAIL FB_ONLY, Z GEQUAL with Z write, ABE) is double sided: each panel's
 * front strips and then its back strips, the same five corners split along
 * other diagonals, and a corner whose UV is off the panel's mapping by two
 * texels, so the two faces show the wires two texels apart.  In the model
 * the faces are one plane, and with exact depths the GS's GEQUAL would let
 * the later face pass over the earlier at every pixel.  ftoi4 rounds every
 * vertex's X, Y and Z on its own, though, so the two triangulations' depths
 * differ by up to (|dZ/dX| + |dZ/dY|) / 8 + 2 GS units either way, and which
 * face wins, region by region, flips with sub-pixel camera motion (the
 * user's dumps: from one frame to the next the earlier face won 0 then 42%
 * of the panel): the wires and the dark FB_ONLY fringes of the two copies
 * took turns, the shimmer.  A triangle that overlaps an earlier one of its
 * mesh in the same plane carries ICO_VU_INDEX_LATER in the index list the
 * device draws, and vu_later_out raises its depth by more than that bound:
 * the later face wins every frame, the picture of an exact GS.
 *
 * Candidates are the earlier triangles that share a corner position with
 * the triangle (double-sided faces share all their corners), tested in
 * double: every corner within 1/1000 of the longer triangle's longest edge
 * of the earlier triangle's plane, and the two overlapping in that plane by
 * more than that on every separating axis (triangles that only share an
 * edge or a corner do not).  Only the prelit and lit layouts, whose first
 * quadword is the position the static programs transform; the
 * interpolation's scratch meshes take their source's list
 * (rd__vu_mesh_copy_draw_index), and a morph (rd_update_vu_mesh) keeps the marks
 * of the creation stream. */

typedef struct OvlTri {
    double p[3][3];
} OvlTri;

static double ovlDot(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void ovlSub(const double *a, const double *b, double *o)
{
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

static double ovlLongest(const OvlTri *t)
{
    double e = 0.0, d[3];
    for (int i = 0; i < 3; i++) {
        ovlSub(t->p[(i + 1) % 3], t->p[i], d);
        const double l = ovlDot(d, d);
        e = l > e ? l : e;
    }
    return sqrt(e);
}

/* whether t overlaps s (earlier) in s's plane */
static bool ovlCoplanarOverlap(const OvlTri *s, const OvlTri *t)
{
    double e1[3], e2[3], n[3], d[3];
    ovlSub(s->p[1], s->p[0], e1);
    ovlSub(s->p[2], s->p[0], e2);
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    const double ls = ovlLongest(s), lt = ovlLongest(t);
    const double len = ls > lt ? ls : lt, nn = sqrt(ovlDot(n, n));
    if (!(len > 0.0) || !(nn > 1e-9 * ls * ls)) {
        return false; /* degenerate */
    }
    const double tol = 1e-3 * len;
    for (int k = 0; k < 3; k++) {
        ovlSub(t->p[k], s->p[0], d);
        if (fabs(ovlDot(n, d)) > tol * nn) {
            return false;
        }
    }
    /* the plane without its dominant axis; then the six edge normals */
    const int ax = fabs(n[0]) >= fabs(n[1]) && fabs(n[0]) >= fabs(n[2]) ? 0
                   : fabs(n[1]) >= fabs(n[2])                           ? 1
                                                                        : 2;
    const int u = ax == 0 ? 1 : 0, v = ax == 2 ? 1 : 2;
    double a[3][2], b[3][2];
    for (int k = 0; k < 3; k++) {
        a[k][0] = s->p[k][u];
        a[k][1] = s->p[k][v];
        b[k][0] = t->p[k][u];
        b[k][1] = t->p[k][v];
    }
    for (int tri = 0; tri < 2; tri++) {
        const double (*e)[2] = tri == 0 ? a : b;
        for (int i = 0; i < 3; i++) {
            const double nx = e[i][1] - e[(i + 1) % 3][1], ny = e[(i + 1) % 3][0] - e[i][0];
            const double nl = sqrt(nx * nx + ny * ny);
            if (!(nl > 0.0)) {
                continue;
            }
            double a0 = 1e300, a1 = -1e300, b0 = 1e300, b1 = -1e300;
            for (int k = 0; k < 3; k++) {
                const double pa = a[k][0] * nx + a[k][1] * ny, pb = b[k][0] * nx + b[k][1] * ny;
                a0 = pa < a0 ? pa : a0;
                a1 = pa > a1 ? pa : a1;
                b0 = pb < b0 ? pb : b0;
                b1 = pb > b1 ? pb : b1;
            }
            const double lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
            if (hi - lo <= tol * nl) {
                return false; /* separated, or only touching */
            }
        }
    }
    return true;
}

static uint32_t ovlHash(const float *p)
{
    uint32_t w[3];
    memcpy(w, p, sizeof(w));
    uint32_t h = w[0] * 0x9E3779B1u;
    h = (h ^ (h >> 15) ^ w[1]) * 0x85EBCA77u;
    h = (h ^ (h >> 13) ^ w[2]) * 0xC2B2AE3Du;
    return h ^ (h >> 16);
}

/* m->drawIndex for m's stream and index list (none when no triangle
 * overlaps an earlier one, or on no memory) */
static void markLaterOverlaps(RdMeshRec *m)
{
    free(m->drawIndex);
    m->drawIndex = NULL;
    const uint32_t nt = m->indexCount / 3, qpv = m->qwPerVertex;
    if (nt < 2 || (qpv != RD_VU_QW_PRELIT && qpv != RD_VU_QW_LIT) || m->vertexCount >= (1u << 27)) {
        return;
    }
    /* a corner position (its float bits) to the triangles at it so far */
    uint32_t cap = 16;
    while (cap < nt * 6u && cap < (1u << 28)) {
        cap <<= 1;
    }

    typedef struct Slot {
        float key[3];
        int32_t head; /* -1: empty slot */
    } Slot;

    typedef struct Link {
        uint32_t tri;
        int32_t next;
    } Link;

    Slot *slot = malloc((size_t)cap * sizeof(Slot));
    Link *link = malloc((size_t)nt * 3 * sizeof(Link));
    uint32_t *seen = malloc((size_t)nt * sizeof(uint32_t));
    uint32_t *out = NULL;
    if (!slot || !link || !seen) {
        goto done;
    }
    for (uint32_t i = 0; i < cap; i++) {
        slot[i].head = -1;
    }
    for (uint32_t i = 0; i < nt; i++) {
        seen[i] = UINT32_MAX;
    }
    uint32_t links = 0;
    for (uint32_t t = 0; t < nt; t++) {
        const uint32_t kick = (m->index[t * 3] & ICO_VU_INDEX_MASK) / 4u;
        if (kick < 2 || kick >= m->vertexCount) {
            continue;
        }
        OvlTri tt;
        const float *pos[3];
        for (int k = 0; k < 3; k++) {
            pos[k] = m->stream[(size_t)(kick - 2u + (uint32_t)k) * qpv];
            for (int c = 0; c < 3; c++) {
                tt.p[k][c] = (double)pos[k][c];
            }
        }
        bool later = false;
        uint32_t at[3];
        for (int k = 0; k < 3; k++) {
            uint32_t h = ovlHash(pos[k]) & (cap - 1);
            while (slot[h].head >= 0 && memcmp(slot[h].key, pos[k], sizeof(slot[h].key)) != 0) {
                h = (h + 1) & (cap - 1);
            }
            at[k] = h;
            for (int32_t l = slot[h].head; l >= 0 && !later; l = link[l].next) {
                const uint32_t s = link[l].tri;
                if (seen[s] == t) {
                    continue;
                }
                seen[s] = t;
                const uint32_t sk = (m->index[s * 3] & ICO_VU_INDEX_MASK) / 4u;
                OvlTri ss;
                for (int j = 0; j < 3; j++) {
                    const float *q = m->stream[(size_t)(sk - 2u + (uint32_t)j) * qpv];
                    for (int c = 0; c < 3; c++) {
                        ss.p[j][c] = (double)q[c];
                    }
                }
                later = ovlCoplanarOverlap(&ss, &tt);
            }
        }
        if (later) {
            if (!out) {
                out = malloc((size_t)m->indexCount * 4);
                if (!out) {
                    goto done;
                }
                memcpy(out, m->index, (size_t)m->indexCount * 4);
            }
            for (int c = 0; c < 3; c++) {
                out[t * 3 + (uint32_t)c] |= ICO_VU_INDEX_LATER;
            }
        }
        for (int k = 0; k < 3; k++) {
            if (slot[at[k]].head < 0) {
                memcpy(slot[at[k]].key, pos[k], sizeof(slot[at[k]].key));
            }
            link[links].tri = t;
            link[links].next = slot[at[k]].head;
            slot[at[k]].head = (int32_t)links++;
        }
    }
    m->drawIndex = out;
    out = NULL;
done:
    free(out);
    free(slot);
    free(link);
    free(seen);
}

/* ------------------------------------------ closing planes beside 4:3
 *
 * A stage is modelled for the PS2's 4:3 picture, and a few of its faces are
 * closing planes that picture never reaches: black faces (vertex colour
 * 0, 0, 0, which MODULATE turns black whatever the texel, and the fog then
 * a flat grey) that seal the model where the camera was not meant to look.
 * A wide picture shows more on either side, and one such plane showed
 * there as a flat grey block through the arch at the end of the long
 * walkway (st08a_p4): a box 846 units wide, its top a four-vertex mesh of
 * its own and its two sides the last eight vertices of the next one, with
 * the textured stonework drawn behind it.  The triangles of such a plane
 * carry ICO_VU_INDEX_WIDE, and rd_replay.c draws the mesh twice on a wide
 * target: everything inside the 4:3 picture, as before, and beside it
 * without them.
 *
 * Black vertex colour alone does not tell a closing plane: the stages also
 * use it for faces in full shade, the same flat fogged colour, that the
 * 4:3 picture shows as well.  In the player's 32:9 dumps opaque black
 * triangles reach past the 4:3 picture's edges in 37 of 46 frames (69 to
 * 369 a frame by the bench, st17a, 67 to 108 at the pool, st09a), and by
 * the bench they are the shaded faces of the statues' pedestals (dropping
 * that draw leaves the walkway seen through them), with the same faces of
 * the pillar beside them inside the 4:3 picture.  So the planes are listed by their
 * corners, the position bits of the stream (prelit and lit layouts), and a
 * triangle is marked when its three vertices are black and are corners of
 * one listed plane: a model pack's replacement or any other geometry never
 * matches by accident. */

typedef struct ClosingPlane {
    const char *model; /* where it is, for the reader */
    uint32_t count;
    uint32_t corner[8][3]; /* x, y, z float bits */
} ClosingPlane;

static const ClosingPlane kClosingPlanes[] = {
    {"st08a_p4: the box through the arch at the walkway's end",
     7,
     {{0xc518a521u, 0xc51a5cc4u, 0xc431a218u},   /* -2442.32 -2469.80  -710.53 */
      {0xc518a521u, 0xc5696b2bu, 0xc431a218u},   /* -2442.32 -3734.70  -710.53 */
      {0xc518a521u, 0xc51a5cc4u, 0xc4a092d2u},   /* -2442.32 -2469.80 -1284.59 */
      {0xc518a521u, 0xc5696b2bu, 0xc4a092d2u},   /* -2442.32 -3734.70 -1284.59 */
      {0xc4c78446u, 0xc51a5cc4u, 0xc42abb35u},   /* -1596.13 -2469.80  -682.93 */
      {0xc4c78446u, 0xc5696b2bu, 0xc42abb35u},   /* -1596.13 -3734.70  -682.93 */
      {0xc4c78446u, 0xc5696b2bu, 0xc45c93adu}}}, /* -1596.13 -3734.70  -882.31 */
};

/* the listed closing plane that vertex v of m is a black corner of (-1: none) */
static int closingPlaneOf(const RdMeshRec *m, uint32_t v)
{
    const float *q = m->stream[(size_t)v * m->qwPerVertex];
    const float *col = m->stream[(size_t)v * m->qwPerVertex + m->qwPerVertex - 1u];
    if (col[0] != 0.0f || col[1] != 0.0f || col[2] != 0.0f) {
        return -1;
    }
    uint32_t bits[3];
    memcpy(bits, q, sizeof(bits));
    for (int p = 0; p < (int)(sizeof(kClosingPlanes) / sizeof(kClosingPlanes[0])); p++) {
        for (uint32_t c = 0; c < kClosingPlanes[p].count; c++) {
            if (memcmp(bits, kClosingPlanes[p].corner[c], sizeof(bits)) == 0) {
                return p;
            }
        }
    }
    return -1;
}

/* ICO_VU_INDEX_WIDE into m->drawIndex (a copy of m->index made when
 * markLaterOverlaps left none) on every triangle of a listed closing plane;
 * m->wideHidden when there is one.  The colour is the layout's last
 * quadword (prelit 2, lit 3). */
static void markWideHidden(RdMeshRec *m)
{
    m->wideHidden = 0;
    const uint32_t nt = m->indexCount / 3, qpv = m->qwPerVertex;
    if (qpv != RD_VU_QW_PRELIT && qpv != RD_VU_QW_LIT) {
        return;
    }
    if (m->vertexCount >= (1u << 27)) {
        return;
    }
    for (uint32_t t = 0; t < nt; t++) {
        const uint32_t kick = (m->index[t * 3] & ICO_VU_INDEX_MASK) / 4u;
        if (kick < 2 || kick >= m->vertexCount) {
            continue;
        }
        const int p = closingPlaneOf(m, kick - 2u);
        if (p < 0 || closingPlaneOf(m, kick - 1u) != p || closingPlaneOf(m, kick) != p) {
            continue;
        }
        if (!m->drawIndex) {
            m->drawIndex = malloc((size_t)m->indexCount * 4);
            if (!m->drawIndex) {
                return;
            }
            memcpy(m->drawIndex, m->index, (size_t)m->indexCount * 4);
        }
        for (int c = 0; c < 3; c++) {
            m->drawIndex[t * 3 + (uint32_t)c] |= ICO_VU_INDEX_WIDE;
        }
        m->wideHidden = 1;
    }
}

bool rd__vu_mesh_copy_draw_index(RdMeshRec *dst, const RdMeshRec *src)
{
    free(dst->drawIndex);
    dst->drawIndex = NULL;
    dst->wideHidden = 0;
    if (!src->drawIndex || dst->indexCount != src->indexCount) {
        return !src->drawIndex;
    }
    dst->drawIndex = malloc((size_t)src->indexCount * 4);
    if (dst->drawIndex) {
        memcpy(dst->drawIndex, src->drawIndex, (size_t)src->indexCount * 4);
        dst->wideHidden = src->wideHidden;
    }
    return dst->drawIndex != NULL;
}

uint32_t rd__vu_mesh_create_raw(const float (*stream)[4], uint32_t vertexCount,
                                uint32_t qwPerVertex, const uint32_t *index, uint32_t indexCount,
                                const RdVuBatchRec *batches, uint32_t batchCount, const char *name)
{
    uint32_t id = 0;
    RdMeshRec *m = meshAlloc(&id);
    if (!m) {
        return 0;
    }
    const size_t sq = (size_t)vertexCount * qwPerVertex;
    m->vu = 1;
    m->vertexCount = vertexCount;
    m->stripCount = batchCount;
    m->qwPerVertex = qwPerVertex;
    m->batchCount = batchCount;
    m->indexCount = indexCount;
    m->stream = malloc(sq ? sq * 16 : 16);
    m->index = malloc(indexCount ? (size_t)indexCount * 4 : 4);
    m->batches = calloc(batchCount ? batchCount : 1, sizeof(RdVuBatchRec));
    if (!m->stream || !m->index || !m->batches) {
        meshFree(m);
        return 0;
    }
    if (sq) {
        memcpy(m->stream, stream, sq * 16);
    }
    if (indexCount) {
        memcpy(m->index, index, (size_t)indexCount * 4);
    }
    if (batchCount) {
        memcpy(m->batches, batches, (size_t)batchCount * sizeof(RdVuBatchRec));
    }
    snprintf(m->name, sizeof(m->name), "%s", name ? name : "vu mesh");
    markLaterOverlaps(m); /* issue 25 */
    markWideHidden(m);
    return id;
}

static uint32_t tagWord(const float *q)
{
    uint32_t w;
    memcpy(&w, q, 4);
    return w;
}

static uint32_t tagPrim(const float *q)
{
    uint32_t hi;
    memcpy(&hi, (const char *)q + 4, 4);
    return (hi >> 15) & 0x7FF; /* bits 47..57 of the tag's first doubleword */
}

/* The walk of a desc (rd_create_vu_mesh, rd_vu_mesh_desc_hash): false for a
 * desc rd_create_vu_mesh refuses; *nv the vertex count of all batches (each
 * batch's GIF tag holds its NLOOP). */
static bool descCount(const RdVuMeshDesc *d, uint32_t *nv)
{
    *nv = 0;
    if (!d || !d->qw || d->qwPerVertex == 0 || d->batchCount == 0 || !d->batches) {
        return false;
    }
    uint64_t total = 0;
    for (uint32_t b = 0; b < d->batchCount; b++) {
        const uint32_t at = d->batches[b].firstQw;
        if (at >= d->qwCount) {
            return false;
        }
        const uint32_t n = tagWord(d->qw[at]) & 0x7FFF;
        if (at + 1 + (uint64_t)n * d->qwPerVertex > d->qwCount) {
            rd__log("rd_create_vu_mesh(%s): batch %u runs past the stream",
                    d->debugName ? d->debugName : "?", b);
            return false;
        }
        total += n;
    }
    if (total >= (1u << 30)) {
        return false;
    }
    *nv = (uint32_t)total;
    return true;
}

/* The bytes rd_mesh.h's mesh identity hashes, in one buffer: the header
 * ("ICOMESH1", qwPerVertex, batchCount, the batches' vertex counts), then
 * the tagless stream at *streamAt.  NULL on no memory; free() it. */
static uint8_t *taglessBuild(const RdVuMeshDesc *d, uint32_t nv, size_t *streamAt, size_t *bytes)
{
    const size_t hdr = 16 + (size_t)d->batchCount * 4;
    const size_t sb = (size_t)nv * d->qwPerVertex * 16;
    uint8_t *buf = malloc(hdr + (sb ? sb : 16));
    if (!buf) {
        return NULL;
    }
    memcpy(buf, "ICOMESH1", 8);
    memcpy(buf + 8, &d->qwPerVertex, 4);
    memcpy(buf + 12, &d->batchCount, 4);
    size_t at = hdr;
    for (uint32_t b = 0; b < d->batchCount; b++) {
        const uint32_t q = d->batches[b].firstQw;
        const uint32_t n = tagWord(d->qw[q]) & 0x7FFF;
        memcpy(buf + 16 + (size_t)b * 4, &n, 4);
        memcpy(buf + at, d->qw[q + 1], (size_t)n * d->qwPerVertex * 16);
        at += (size_t)n * d->qwPerVertex * 16;
    }
    *streamAt = hdr;
    *bytes = hdr + sb;
    return buf;
}

/* the first vertex's normal.w for the lit and skinned layouts */
static float streamNormalW(const float (*stream)[4], uint32_t nv, uint32_t qpv)
{
    return nv && qpv >= 4 ? stream[1][3] : 0.0f;
}

/* The static kicks of one batch (n vertices from v in the mesh numbering):
 * vertex k >= 2 kicks unless k or k-1 carries the strip flag (ST.w < 1; ST
 * is the second-last quadword of a vertex in normal_c, normal_l and
 * cluster).  Appends the indices at index, returns how many. */
static uint32_t batchIndices(const float (*stream)[4], uint32_t qpv, uint32_t v, uint32_t n,
                             uint32_t *index)
{
    const uint32_t stAt = qpv >= 3 ? qpv - 2 : 0;
    uint32_t ni = 0;
    for (uint32_t k = 2; k < n; k++) {
        const float *st0 = stream[(size_t)(v + k) * qpv + stAt];
        const float *st1 = stream[(size_t)(v + k - 1) * qpv + stAt];
        if (st0[3] < 1.0f || st1[3] < 1.0f) {
            continue; /* vertex k or k-1 starts a strip */
        }
        for (uint32_t c = 0; c < 3; c++) {
            index[ni++] = ICO_VU_INDEX(v + k, c);
        }
    }
    return ni;
}

uint64_t rd_vu_mesh_desc_hash(const RdVuMeshDesc *d, uint32_t *vertexCount, float *normalW)
{
    uint32_t nv = 0;
    uint64_t h = 0;
    float nw = 0.0f;
    size_t streamAt, bytes;
    uint8_t *buf;
    if (descCount(d, &nv) && (buf = taglessBuild(d, nv, &streamAt, &bytes)) != NULL) {
        h = xxh3_64(buf, bytes);
        nw = streamNormalW((const float (*)[4])(void *)(buf + streamAt), nv, d->qwPerVertex);
        free(buf);
    } else {
        nv = 0;
    }
    if (vertexCount) {
        *vertexCount = nv;
    }
    if (normalW) {
        *normalW = nw;
    }
    return h;
}

RdMesh rd_create_vu_mesh(const RdVuMeshDesc *d)
{
    uint32_t nv;
    if (!descCount(d, &nv)) {
        return (RdMesh){0};
    }
    size_t streamAt, bytes;
    uint8_t *buf = taglessBuild(d, nv, &streamAt, &bytes);
    uint32_t *index = malloc((size_t)(nv ? nv : 1) * 3 * 4);
    RdVuBatchRec *br = calloc(d->batchCount, sizeof(RdVuBatchRec));
    if (!buf || !index || !br) {
        free(buf);
        free(index);
        free(br);
        return (RdMesh){0};
    }
    const float (*stream)[4] = (const float (*)[4])(void *)(buf + streamAt);
    uint32_t v = 0, ni = 0;
    for (uint32_t b = 0; b < d->batchCount; b++) {
        const uint32_t at = d->batches[b].firstQw;
        const uint32_t n = tagWord(d->qw[at]) & 0x7FFF;
        br[b].srcQw = at;
        br[b].prim = tagPrim(d->qw[at]);
        br[b].material = d->batches[b].material;
        br[b].group = d->batches[b].group;
        br[b].firstVertex = v;
        br[b].vertexCount = n;
        br[b].firstIndex = ni;
        ni += batchIndices(stream, d->qwPerVertex, v, n, index + ni);
        br[b].indexCount = ni - br[b].firstIndex;
        v += n;
    }
    uint32_t id = rd__vu_mesh_create_raw(stream, nv, d->qwPerVertex, index, ni, br, d->batchCount,
                                         d->debugName);
    RdMeshRec *m = rd__mesh_rec(id);
    if (m) {
        m->materialCount = d->materialCount;
        m->srcQw = d->qwCount;
        m->hash = xxh3_64(buf, bytes);
    }
    free(buf);
    free(index);
    free(br);
    return (RdMesh){id};
}

RdMesh rd_create_vu_mesh_replacement(const RdVuMeshDesc *orig, const RdVuReplacement *rep,
                                     const char *name)
{
    uint32_t onv;
    if (!rep || !descCount(orig, &onv) || rep->qwPerVertex != orig->qwPerVertex ||
        rep->batchCount > orig->batchCount || (rep->batchCount && !rep->batches) ||
        (rep->vertexCount && !rep->qw)) {
        return (RdMesh){0};
    }
    const uint32_t qpv = orig->qwPerVertex;
    uint64_t total = 0;
    for (uint32_t b = 0; b < rep->batchCount; b++) {
        const RdVuReplacementBatch *rb = &rep->batches[b];
        if ((uint64_t)rb->firstVertex + rb->vertexCount > rep->vertexCount) {
            return (RdMesh){0};
        }
        total += rb->vertexCount;
    }
    if (total >= (1u << 30)) {
        return (RdMesh){0};
    }
    const uint64_t hash = rd_vu_mesh_desc_hash(orig, NULL, NULL);
    const uint32_t nv = (uint32_t)total;
    float (*stream)[4] = malloc((size_t)(nv ? nv : 1) * qpv * 16);
    uint32_t *index = malloc((size_t)(nv ? nv : 1) * 3 * 4);
    RdVuBatchRec *br = calloc(orig->batchCount, sizeof(RdVuBatchRec));
    if (!stream || !index || !br) {
        free(stream);
        free(index);
        free(br);
        return (RdMesh){0};
    }
    uint32_t v = 0, ni = 0;
    for (uint32_t b = 0; b < orig->batchCount; b++) {
        const uint32_t at = orig->batches[b].firstQw;
        const uint32_t n = b < rep->batchCount ? rep->batches[b].vertexCount : 0;
        br[b].srcQw = at;
        br[b].prim = tagPrim(orig->qw[at]);
        br[b].material = orig->batches[b].material;
        br[b].group = orig->batches[b].group;
        br[b].firstVertex = v;
        br[b].vertexCount = n;
        br[b].firstIndex = ni;
        if (n) {
            memcpy(stream[(size_t)v * qpv], rep->qw[(size_t)rep->batches[b].firstVertex * qpv],
                   (size_t)n * qpv * 16);
            ni += batchIndices((const float (*)[4])stream, qpv, v, n, index + ni);
        }
        br[b].indexCount = ni - br[b].firstIndex;
        v += n;
    }
    uint32_t id = rd__vu_mesh_create_raw((const float (*)[4])stream, nv, qpv, index, ni, br,
                                         orig->batchCount, name ? name : orig->debugName);
    RdMeshRec *m = rd__mesh_rec(id);
    if (m) {
        m->materialCount = orig->materialCount;
        m->srcQw = orig->qwCount;
        m->hash = hash;
        m->replaced = 1;
    }
    free(stream);
    free(index);
    free(br);
    return (RdMesh){id};
}

/* A rewrite while frame `rec` records.  The presenter draws the two
 * frames closed last (rd_interp.c) after the game has recorded the next
 * one, so a stream either of them drew is kept before it is overwritten:
 * a morphing part (reg_setShape) then shows the shape of the tick it
 * belongs to, blended, instead of the newer tick's.  The game's own double
 * buffer (grp->packets and grp->morph in alternate frames) gives each frame
 * its own mesh; a mesh rewritten every frame keeps two versions. */
static void keepVersion(RdMeshRec *m)
{
    const uint32_t rec = rd__rec_frame() ? g_rd.frameCounter : g_rd.frameCounter + 1;
    if (rd_interpolation_active() && m->lastUsed >= m->verFrame && m->lastUsed < rec &&
        m->lastUsed + 2 >= rec) {
        const int k = m->hist[0].to <= m->hist[1].to ? 0 : 1;
        const size_t size = (size_t)m->vertexCount * m->qwPerVertex * 16;
        if (!m->hist[k].stream) {
            m->hist[k].stream = malloc(size ? size : 16);
        }
        if (m->hist[k].stream) {
            if (size) {
                memcpy(m->hist[k].stream, m->stream, size);
            }
            m->hist[k].from = m->verFrame;
            m->hist[k].to = rec;
        }
    }
    m->verFrame = rec;
}

const float (*rd__mesh_stream_at(const RdMeshRec *m, uint32_t frame))[4]
{
    if (!m) {
        return NULL;
    }
    if (frame >= m->verFrame) {
        return (const float (*)[4])m->stream;
    }
    for (int k = 0; k < 2; k++) {
        if (m->hist[k].stream && m->hist[k].from <= frame && frame < m->hist[k].to) {
            return (const float (*)[4])m->hist[k].stream;
        }
    }
    return NULL;
}

bool rd_update_vu_mesh(RdMesh mesh, const float (*qw)[4])
{
    RdMeshRec *m = rd__mesh_rec(mesh.id);
    if (!m || !m->vu || !qw || m->replaced) {
        return false; /* a replaced mesh: its stream is not the packet's layout */
    }
    keepVersion(m);
    for (uint32_t b = 0; b < m->batchCount; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        memcpy(m->stream[(size_t)br->firstVertex * m->qwPerVertex], qw[br->srcQw + 1],
               (size_t)br->vertexCount * m->qwPerVertex * 16);
    }
    m->replaySeen = 0; /* upload again at the next replay */
    m->gpuDirty = 1;   /* the device copy too */
    return true;
}

void rd_destroy_vu_mesh(RdMesh mesh)
{
    RdMeshRec *m = rd__mesh_rec(mesh.id);
    if (m) {
        meshFree(m);
    }
}

bool rd_vu_mesh_valid(RdMesh mesh)
{
    RdMeshRec *m = rd__mesh_rec(mesh.id);
    return m && m->vu && !m->stale;
}

uint64_t rd_vu_mesh_hash(RdMesh mesh)
{
    RdMeshRec *m = rd__mesh_rec(mesh.id);
    return m && m->vu ? m->hash : 0;
}

bool rd_vu_mesh_replaced(RdMesh mesh)
{
    RdMeshRec *m = rd__mesh_rec(mesh.id);
    return m && m->vu && m->replaced;
}

void rd_vu_mesh_retire(bool (*pred)(uint64_t hash, bool replaced, void *user), void *user)
{
    if (!g_rd.meshes) {
        return;
    }
    for (uint32_t i = 0; i < RD_MAX_MESHES; i++) {
        RdMeshRec *m = &g_rd.meshes[i];
        if (!m->live || !m->vu || m->stale || m->transient) {
            continue;
        }
        if (!pred || pred(m->hash, m->replaced != 0, user)) {
            m->stale = 1;
            s_staleCount++;
        }
    }
}

void rd__vu_mesh_sweep_stale(void)
{
    if (s_staleCount == 0 || !g_rd.meshes) {
        return;
    }
    for (uint32_t i = 0; i < RD_MAX_MESHES && s_staleCount; i++) {
        RdMeshRec *m = &g_rd.meshes[i];
        /* the frame recording now and the two retained ones are younger */
        if (m->live && m->stale && m->lastUsed + 3 < g_rd.frameCounter) {
            meshFree(m);
        }
    }
}

void rd__mesh_shutdown(void)
{
    if (!g_rd.meshes) {
        return;
    }
    for (uint32_t i = 0; i < RD_MAX_MESHES; i++) {
        if (g_rd.meshes[i].live) {
            meshFree(&g_rd.meshes[i]);
        }
    }
    s_staleCount = 0;
}

/* ------------------------------------------------------------ VU images */

/* VIF UNPACKs write the input buffer at TOP; one staging area per list is
 * enough because every MSCAL reads what the UNPACKs just before it wrote
 * (the double-buffered TOPS alternate, the data does not mix). */
typedef struct RdVuList {
    Vu1Ref ref;
    int program;    /* resident program id, 0 = none uploaded yet */
    int code;       /* the last BEGIN code */
    int endTagHit;  /* a particle batch of this list would have clobbered mem[0..1] */
    uint8_t scroll; /* RD_VU_SCROLL_* of the last SET_UVOFFSET */
} RdVuList;

static RdVuList *s_vu;

static Vu1Ref s_scratch;

void rd__vu_init(void)
{
    free(s_vu);
    s_vu = calloc(RD_LIST_COUNT, sizeof(RdVuList));
    for (int l = 0; s_vu && l < RD_LIST_COUNT; l++) {
        vu1ref_init(&s_vu[l].ref);
    }
}

void rd__vu_shutdown(void)
{
    free(s_vu);
    s_vu = NULL;
}

static RdVuList *curVu(void)
{
    int l = rd_current_list();
    return s_vu && l >= 0 && l < RD_LIST_COUNT ? &s_vu[l] : NULL;
}

void rd__vu_load_common(const RdVuCommon *block)
{
    float qw[16][4];
    _Static_assert(sizeof(RdVuCommon) == sizeof(qw), "RdVuCommon is 16 quadwords");
    memcpy(qw, block, sizeof(qw));
    for (int l = 0; s_vu && l < RD_LIST_COUNT; l++) {
        vu1ref_load_common(&s_vu[l].ref, (const float (*)[4])qw);
        s_vu[l].endTagHit = 0;
        s_vu[l].scroll = 0;
    }
}

void rd_vu_program(int id)
{
    RdVuList *v = curVu();
    if (v) {
        v->program = id;
    }
}

int rd_vu_current_program(void)
{
    RdVuList *v = curVu();
    return v ? v->program : 0;
}

void rd_vu_call(int code, const float (*top)[4], uint32_t qw)
{
    RdVuList *v = curVu();
    if (!v) {
        return;
    }
    /* the SET_* routines read fixed counts from TOP; a short unpack reads
     * whatever the buffer held before (zeros here) */
    static float buf[256][4];
    memset(buf, 0, sizeof(buf));
    if (top && qw) {
        memcpy(buf, top, (size_t)(qw > 256 ? 256 : qw) * 16);
    }
    const float (*in)[4] = (const float (*)[4])buf;
    Vu1Ref *r = &v->ref;
    switch (code) {
    case 2: { /* SET_UVOFFSET, every program (vu1_common.h:57) */
        vu1ref_set_uv_offset(r, in[0]);
        uint32_t zw[2];
        memcpy(zw, &in[0][2], sizeof(zw));
        v->scroll =
            (uint8_t)((zw[0] ? RD_VU_SCROLL_SINE_U : 0u) | (zw[1] ? RD_VU_SCROLL_SINE_V : 0u));
        return;
    }
    case 16:
        switch (v->program) {
        case 1:
        case 2:
            vu1ref_normal_set_matrix(r, in);
            return;
        case 3: {
            /* qw[0].x is the packet's copy count: at most the qwords after
             * qw[0] that buf holds (vu1ref_cluster_set_matrix reads qw[1..n]) */
            int32_t n;
            memcpy(&n, buf[0], sizeof n);
            if (n > 255) {
                n = 255;
                memcpy(buf[0], &n, sizeof n);
            }
            vu1ref_cluster_set_matrix(r, in);
            return;
        }
        case 4:
            vu1ref_mesh_set_matrix(r, in);
            return;
        case 5:
            vu1ref_particle_set_matrix(r, in);
            v->code = 18; /* falls through into BEGIN_PARTICLE (particle.vsm:43-61) */
            return;
        default:
            break;
        }
        break;
    case 18:
        switch (v->program) {
        case 1:
        case 2:
            vu1ref_normal_set_light(r, in);
            return;
        case 3:
            vu1ref_cluster_set_light(r, in);
            return;
        case 4:
            vu1ref_mesh_set_light(r, in);
            return;
        case 5:
            v->code = 18; /* BEGIN_PARTICLE */
            return;
        default:
            break;
        }
        break;
    case 20:
    case 22:
    case 24:
    case 32:
    case 34:
    case 36:
    case 38:
        v->code = code; /* a BEGIN entry: the batches that follow run its START loop */
        return;
    default:
        break;
    }
    rd__log_once(RD_ONCE_VU_CODE, "MSCAL code %d with program %d in list %d is not modelled", code,
                 v->program, rd_current_list());
}

/* ----------------------------------------------------------- the table */

typedef struct VuRow {
    uint8_t program, code, prog, vs, clip;
} VuRow;

static const VuRow kRows[] = {
    {1, 32, RD_PROG_PRELIT, RD_VS_VU_PRELIT, RD_VU_CLIP_REGION},
    {1, 34, RD_PROG_PRELIT, RD_VS_VU_PRELIT, RD_VU_CLIP_NONE},
    {1, 36, RD_PROG_PRELIT, RD_VS_VU_PRELIT, RD_VU_CLIP_SCISSOR},
    {2, 32, RD_PROG_LIT, RD_VS_VU_LIT, RD_VU_CLIP_REGION},
    {2, 34, RD_PROG_LIT_SPEC, RD_VS_VU_LIT_SPEC, RD_VU_CLIP_REGION},
    {2, 36, RD_PROG_LIT, RD_VS_VU_LIT, RD_VU_CLIP_SCISSOR},
    {2, 38, RD_PROG_REFLECT, RD_VS_VU_REFLECT, RD_VU_CLIP_REGION},
    {3, 20, RD_PROG_SKIN, RD_VS_VU_SKIN, RD_VU_CLIP_REGION},
    {3, 22, RD_PROG_SKIN_SPEC, RD_VS_VU_SKIN_SPEC, RD_VU_CLIP_REGION},
    {3, 24, RD_PROG_SKIN_SPEC, RD_VS_VU_SKIN_DEBUG, RD_VU_CLIP_REGION},
    {4, 20, RD_PROG_GRID, RD_VS_VU_GRID, RD_VU_CLIP_REGION},
    {4, 22, RD_PROG_GRID_LIT, RD_VS_VU_GRID_LIT, RD_VU_CLIP_REGION},
    {4, 24, RD_PROG_GRID_LIT, RD_VS_VU_GRID_SPEC, RD_VU_CLIP_REGION},
    {5, 18, RD_PROG_PARTICLE, RD_VS_VU_PARTICLE, RD_VU_CLIP_REGION},
};

bool rd__vu_row(int program, int code, uint8_t *prog, uint8_t *vs, uint8_t *clip)
{
    for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++) {
        if (kRows[i].program == program && kRows[i].code == code) {
            *prog = kRows[i].prog;
            *vs = kRows[i].vs;
            *clip = kRows[i].clip;
            return true;
        }
    }
    return false;
}

/* The VuCB image a program reads (vu_common.hlsli's slot map): normal_c and
 * normal_l read memory 0..35; cluster, mesh and particle keep their SET_*
 * uploads in VF registers, which go to the slots the normal programs use. */
static void vuImage(const RdVuList *v, RdVuBlock *out)
{
    const Vu1Ref *r = &v->ref;
    memset(out, 0, sizeof(*out));
    switch (v->program) {
    case 1:
    case 2:
        memcpy(out->mem, r->mem, 36 * 16);
        break;
    case 3:
        memcpy(out->mem, r->mem, 16 * 16);
        memcpy(out->mem[28], r->vf[13], 8 * 16);
        break;
    case 4:
        memcpy(out->mem, r->mem, 16 * 16);
        memcpy(out->mem[16], r->vf[1], 4 * 16);
        memcpy(out->mem[28], r->vf[5], 8 * 16);
        break;
    default:
        memcpy(out->mem, r->mem, 16 * 16);
        memcpy(out->mem[16], r->vf[1], 8 * 16);
        break;
    }
}

bool rd_vu_draw_from_state(RdVuDraw *d)
{
    RdVuList *v = curVu();
    if (!d || !v) {
        return false;
    }
    uint8_t prog, vs, clip;
    if (!rd__vu_row(v->program, v->code, &prog, &vs, &clip)) {
        rd__log_once(RD_ONCE_VU_ROW,
                     "no table row for program %d code %d (list %d): the batch is not drawn",
                     v->program, v->code, rd_current_list());
        return false;
    }
    memset(d, 0, sizeof(*d));
    d->prog = (RdProg)prog;
    d->code = (uint8_t)v->code;
    d->clip = clip;
    d->scroll = v->scroll;
    vuImage(v, &d->vu);
    if (v->program == 3) {
        d->bones = (const float (*)[4])v->ref.mem[16];
        d->boneQw = 240; /* VU memory 16..255, VuBoneCB */
        if (v->endTagHit) {
            rd__log_once(RD_ONCE_VU_ENDTAG,
                         "particle end-tag quirk: a culled particle batch earlier in list %d "
                         "would have hidden this skinned draw on the PS2 (not reproduced)",
                         rd_current_list());
        }
    }
    return true;
}

/* --------------------------------------------------------------- draws */

static RdCmd *pushVu(uint8_t type, RdKey key, const RdVuPayload *p, const RdVuBlock *vu,
                     const void *bones, const void *stream, const RdMaterial *mats)
{
    RdFrame *f = rd__rec_frame();
    if (!f) {
        rd__push(type); /* reports once */
        return NULL;
    }
    const uint32_t sizes[5] = {(uint32_t)sizeof(*p), (uint32_t)sizeof(*vu), p->boneQw * 16u,
                               p->streamQw * 16u, p->materialCount * (uint32_t)sizeof(RdMaterial)};
    const void *parts[5] = {p, vu, bones, stream, mats};
    uint32_t total = 0;
    for (int i = 0; i < 5; i++) {
        total += sizes[i];
    }
    const uint32_t off = rd__frame_payload(f, NULL, total);
    uint32_t at = off;
    for (int i = 0; i < 5; i++) {
        if (parts[i] && sizes[i]) {
            memcpy(f->payload + at, parts[i], sizes[i]);
        }
        at += sizes[i];
    }
    RdCmd *c = rd__push(type);
    c->b[0] = p->prog;
    c->b[1] = p->code;
    c->b[2] = p->clip;
    /* b[3]: the game draws this model as a full-screen item (RegistPacket.c
     * reg_DispObj under rd_set_space_override(RD_SPACE_FULLSCREEN): the
     * title's darkening plane), so the replay draws it across the whole
     * width of a wide target instead of the centred 4:3 part (rd_replay.c
     * doVu); 0 in every other draw and in older dumps */
    c->b[3] = (uint8_t)(g_rd.spaceOverride - 1 == RD_SPACE_FULLSCREEN);
    c->u[1] = off;
    c->u[2] = total;
    c->keyLo = (uint32_t)key;
    c->keyHi = (uint32_t)(key >> 32);
    g_rd.stats.draws++;
    return c;
}

void rd_draw_vu_mesh(RdMesh mesh, const RdVuDraw *d, RdKey key)
{
    RdMeshRec *m = rd__mesh_rec(mesh.id);
    if (!m || !m->vu || !d || !rd__draw_filter_pass(key)) {
        return;
    }
    uint32_t first = d->firstBatch, n = d->batchCount;
    if (first >= m->batchCount) {
        return;
    }
    if (n == 0 || first + n > m->batchCount) {
        n = m->batchCount - first;
    }
    m->lastUsed = g_rd.frameCounter;
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.code = d->code;
    p.clip = d->clip;
    p.prog = (uint8_t)d->prog;
    p.scroll = d->scroll;
    p.firstBatch = first;
    p.batchCount = n;
    p.boneQw = d->bones ? (d->boneQw > 240 ? 240 : d->boneQw) : 0;
    p.qwPerVertex = m->qwPerVertex;
    if (d->materials) {
        rd__log_once(RD_ONCE_VU_MATERIALS,
                     "RdVuDraw.materials is recorded, not applied: draws take the GS state");
        p.materialCount = m->materialCount;
    }
    RdCmd *c =
        pushVu(d->bones ? RDC_SKINNED : RDC_MESH, key, &p, &d->vu, d->bones, NULL, d->materials);
    if (c) {
        c->u[0] = mesh.id;
        /* b[4]: RD_VU_VIEW_*, how the model matrices follow the camera
         * (rd_interp.c's re-base); 0 in older dumps */
        c->b[4] = d->bones ? 0 : d->view;
    }
}

void rd_draw_vu_grid(const RdVuGridDraw *d, RdKey key)
{
    if (!d || !d->qw || d->strips == 0 || d->stripLen < 3 || !rd__draw_filter_pass(key)) {
        return;
    }
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.code = d->code;
    p.clip = RD_VU_CLIP_REGION;
    p.prog = d->code == 20 ? RD_PROG_GRID : RD_PROG_GRID_LIT;
    p.batchCount = d->strips;
    p.vertsPerBatch = d->stripLen;
    p.qwPerVertex = d->lit ? RD_VU_QW_GRID_LIT : RD_VU_QW_GRID;
    /* Mesh3D.qwc: per strip the VIF qword, tag, colour, the vertices, MSCNT */
    p.streamQw = d->strips * (d->stripLen * p.qwPerVertex + 4);
    uint8_t kind = 0;
    float sx = 0.0f, sy = 0.0f;
    rd__grid_screen_st_take(&kind, &sx, &sy);
    RdCmd *c = pushVu(RDC_GRID, key, &p, &d->vu, NULL, d->qw, NULL);
    if (c && kind) {
        /* b[5]: the RdGridSt formula of the STs (rd.h rd_grid_screen_st),
         * f[0], f[1] its sx and sy; 0 in other grids and in older dumps */
        c->b[5] = kind;
        c->f[0] = sx;
        c->f[1] = sy;
    }
}

/* A particle batch drawn with key 0 is keyed here by list and the
 * occurrence order rd_interp.c adds, and a batch interpolates only when its
 * count matches (per particle a jump or an alpha 0 end snaps).  MicroCode.c
 * keys prim_DispParticle's batches by their emitter (mc_HostParticleKey),
 * so a batch another emitter inserts ahead does not shift the match; key 0
 * is left for a batch chained elsewhere. */
static const char kParticleKeyTag;

void rd_draw_vu_particles(const RdVuParticleDraw *d, RdKey key)
{
    if (key == 0) {
        key = RD_KEY(&kParticleKeyTag, rd_current_list(), 18);
    }
    if (!d || !d->qw || d->count == 0 || !rd__draw_filter_pass(key)) {
        return;
    }
    const uint32_t count = d->count > 80 ? 80 : d->count;
    RdVuList *v = curVu();
    if (v) {
        /* the end-tag quirk check, on a copy (see the file comment) */
        VuParticleOut out;
        memcpy(&s_scratch, &v->ref, sizeof(s_scratch));
        vu1ref_particle(&s_scratch, d->qw, 6 + 2 * count, &out);
        if (out.count == 0 && memcmp(s_scratch.mem, v->ref.mem, 2 * 16) != 0) {
            v->endTagHit = 1;
        }
    }
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.code = 18;
    p.clip = RD_VU_CLIP_REGION;
    p.prog = RD_PROG_PARTICLE;
    p.vertsPerBatch = count;
    p.qwPerVertex = RD_VU_QW_PARTICLE;
    p.streamQw = 6 + 2 * count;
    pushVu(RDC_PARTICLES, key, &p, &d->vu, NULL, d->qw, NULL);
}

/* ------------------------------------------------------------ pipelines
 * The VU families: each VU vertex shader with vu_ps under the states the
 * mesh lists draw with (rd.h's list defaults and the game's register
 * packets): TEST Z GEQUAL with ATE off (lists 0, 6-9, 0x50000), the list 1/2
 * default 0x5140D and Texture.c's per-texture TEST (ATE GREATER, AFAIL
 * FB_ONLY: the split pair), the list 4 default 0x5C000 (DATE, normalised
 * out); Z write on and off; ALPHA off and the material, dissolve,
 * specular, reflection and particle equations 0x44, 0x48, 0x42, 0x68, 0x62
 * (four hardware paths); SCENE's D32F_S8 (render-to-texture targets have
 * depth too).  The scissor batches' cut pass forces ABE on: the same set. */
uint32_t rd__enumerate_reachable_vu(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    static const uint64_t kTests[] = {RD_TEST_Z_GEQUAL, RD_TEST_AT_GT64_FBONLY,
                                      RD_TEST_DATE1_Z_GEQUAL};
    static const int kBlends[] = {-1, RD_BLEND_LERP_AS, RD_BLEND_CS_AS_ADD_CD,
                                  RD_BLEND_CD_SUB_CS_AS};
    for (uint8_t vs = RD_VS_VU_FIRST; vs <= RD_VS_VU_LAST; vs++) {
        uint8_t prog = 0;
        for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++) {
            if (kRows[i].vs == vs) {
                prog = kRows[i].prog;
            }
        }
        for (size_t t = 0; t < sizeof(kTests) / sizeof(kTests[0]); t++) {
            for (int zw = 0; zw < 2; zw++) {
                for (size_t b = 0; b < sizeof(kBlends) / sizeof(kBlends[0]); b++) {
                    RdStateBlock s;
                    rd__reset_state_block(&s);
                    s.ds.test = rd_test_from_gs(kTests[t]);
                    s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                    s.ds.abe = kBlends[b] >= 0;
                    s.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
                    RdDrawPass dp[2];
                    int np = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD,
                                                  RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, dp);
                    /* vu_ps and, for a 24- or 16-bit texture
                     * under AEM, vu_texa_ps */
                    for (int i = 0; i < np * 2; i++) {
                        RdPipeKeyInt k = dp[i / 2].key;
                        k.gs.program = prog;
                        k.vs = vs;
                        k.fs = (i & 1) ? RD_FS_VU_TEXA : RD_FS_VU;
                        n = rd__add_pipe_key(out, max, n, &k);
                    }
                }
            }
        }
    }
    return n;
}
