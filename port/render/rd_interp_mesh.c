/* rd_interp_mesh.c: what the interpolation (rd_interp.c) reads from a
 * mesh's stream alone, kept while the stream is unchanged (rd_internal.h
 * RdMeshRec.streamSerial: a new serial at every write of a stream, its kept
 * versions with it).
 *
 * The pivots the rotations turn about.  A skinned draw's bone is the node
 * times the cluster (bind) inverse, so its translation is not the joint:
 * blended about it, a turning limb would leave its joint.  Each bone's
 * pivot is the weighted centroid of the mesh vertices bound to it (the
 * stream's weight qword: VU address bone * 4 + 16 and weight, twice); a
 * rigid mesh's is its vertices' centroid.  Both in the mesh's space, so the
 * part's middle follows the straight line between its two ticks, as an
 * element-wise blend has it, and the turn is blended about it.  A skinned
 * mesh's pivots were made again at every present (0.12 ms a present on
 * lavapipe on the rc10 phone dump, build-host/tmp/v098/evidence/rscan/):
 * they are kept per (mesh, serial, stream, vertex count) across presents
 * and ticks; a record with no serial (a test's own) is made again at every
 * present, as before.
 *
 * The same shape.  A mesh draw matched across the ticks whose two streams
 * (prev's at its frame, cur's at its, rd__mesh_stream_at) are other buffers
 * was compared whole at every present (rd_interp.c morphDraw: equal, the
 * draw keeps the live stream; unequal, a scratch mesh holds the blend).  A
 * stream version is written only through rd_update_vu_mesh (the live
 * stream, and the kept copy it makes first) or at its mesh's creation, each
 * giving the mesh a new serial, so the verdict is kept per (both meshes,
 * both serials, both buffers, the size): the later presents of a tick, and
 * the ticks whose two meshes did not change, compare nothing.  (The replay
 * tool loads each dump's meshes on their own, so every matched mesh of its
 * --interp pair is two buffers of the same bytes, compared whole: 0.28 ms a
 * present on lavapipe; the game's own frames share one mesh between the
 * ticks where it did not change, the same buffer, not compared.) */
#include <math.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"

/* ---------------------------------------------------------------- pivots */

#define PIVOT_MEMO 32

static struct {
    uint64_t serial;
    const void *stream;
    uint32_t mesh, vertices;
    double pivot[RD_INTERP_BONES][4]; /* x, y, z, weight */
} s_pivots[PIVOT_MEMO + 1];           /* the last: a record's with no serial */

/* the mesh of the last answer this present (0: none) */
static uint32_t s_pivotMesh;
static int s_pivotAt = PIVOT_MEMO;

void rd__interp_pivots_present(void)
{
    s_pivotMesh = 0;
}

const double (*rd__interp_skin_pivots(uint32_t meshId))[4]
{
    if (meshId == s_pivotMesh) {
        return (const double (*)[4])s_pivots[s_pivotAt].pivot;
    }
    s_pivotMesh = meshId;
    const RdMeshRec *m = rd__mesh_rec(meshId);
    const bool ok = m && m->vu && m->qwPerVertex == RD_VU_QW_SKIN && m->stream;
    const int at = ok && m->streamSerial ? (int)((meshId * 2654435761u) >> 27) : PIVOT_MEMO;
    double (*pivot)[4] = s_pivots[at].pivot;
    s_pivotAt = at;
    if (at < PIVOT_MEMO && s_pivots[at].mesh == meshId && s_pivots[at].serial == m->streamSerial &&
        s_pivots[at].stream == m->stream && s_pivots[at].vertices == m->vertexCount) {
        return (const double (*)[4])pivot; /* the same stream's */
    }
    memset(pivot, 0, sizeof(s_pivots[at].pivot));
    s_pivots[at].mesh = 0; /* made below, or none */
    if (!ok) {
        return (const double (*)[4])pivot;
    }
    for (uint32_t v = 0; v < m->vertexCount; v++) {
        const float *pos = m->stream[(size_t)v * RD_VU_QW_SKIN];
        const float *wt = m->stream[(size_t)v * RD_VU_QW_SKIN + 2];
        for (int k = 0; k < 2; k++) {
            uint32_t addr;
            memcpy(&addr, &wt[k * 2], 4);
            const float w = wt[k * 2 + 1];
            if (addr < 16u || ((addr - 16u) & 3u) != 0u || (addr - 16u) / 4u >= RD_INTERP_BONES ||
                !(w > 0.0f) || !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) {
                continue;
            }
            double *pv = pivot[(addr - 16u) / 4u];
            pv[0] += w * pos[0];
            pv[1] += w * pos[1];
            pv[2] += w * pos[2];
            pv[3] += w;
        }
    }
    for (int b = 0; b < RD_INTERP_BONES; b++) {
        if (pivot[b][3] > 0.0) {
            for (int k = 0; k < 3; k++) {
                pivot[b][k] /= pivot[b][3];
            }
        }
    }
    s_pivots[at].mesh = meshId;
    s_pivots[at].serial = m->streamSerial;
    s_pivots[at].stream = m->stream;
    s_pivots[at].vertices = m->vertexCount;
    return (const double (*)[4])pivot;
}

bool rd__interp_mesh_centroid(uint32_t meshId, double out[3])
{
    const RdMeshRec *m = rd__mesh_rec(meshId);
    if (!m || !m->vu || !m->stream || m->vertexCount == 0 || m->qwPerVertex == 0) {
        return false;
    }
    double s[3] = {0.0, 0.0, 0.0};
    uint32_t n = 0;
    for (uint32_t v = 0; v < m->vertexCount; v++) {
        const float *pos = m->stream[(size_t)v * m->qwPerVertex];
        if (isfinite(pos[0]) && isfinite(pos[1]) && isfinite(pos[2])) {
            s[0] += pos[0];
            s[1] += pos[1];
            s[2] += pos[2];
            n++;
        }
    }
    if (n == 0) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        out[k] = s[k] / n;
    }
    return true;
}

/* ------------------------------------------------------------ same shape */

#define SAME_MEMO 256

typedef struct SameMemo {
    uint64_t pSerial, cSerial;
    const void *ps, *cs;
    size_t size;
    uint32_t pMesh, cMesh;
    bool same;
} SameMemo;

static SameMemo s_same[SAME_MEMO];

static uint32_t s_sameCompared, s_sameKept; /* the tests' counts */

bool rd__interp_streams_same(uint32_t pMesh, const RdMeshRec *mp, const float (*ps)[4],
                             uint32_t cMesh, const RdMeshRec *mc, const float (*cs)[4], size_t size)
{
    if (ps == cs) {
        return true;
    }
    if (!mp || !mc || !mp->streamSerial || !mc->streamSerial) {
        s_sameCompared++;
        return memcmp(ps, cs, size) == 0; /* no serial: nothing to keep it by */
    }
    const uint32_t h = (pMesh * 2654435761u) ^ (cMesh * 40503u);
    SameMemo *e = &s_same[(h >> 8) % SAME_MEMO];
    if (e->pMesh == pMesh && e->cMesh == cMesh && e->pSerial == mp->streamSerial &&
        e->cSerial == mc->streamSerial && e->ps == (const void *)ps && e->cs == (const void *)cs &&
        e->size == size) {
        s_sameKept++;
        return e->same;
    }
    s_sameCompared++;
    e->same = memcmp(ps, cs, size) == 0;
    e->pMesh = pMesh;
    e->cMesh = cMesh;
    e->pSerial = mp->streamSerial;
    e->cSerial = mc->streamSerial;
    e->ps = ps;
    e->cs = cs;
    e->size = size;
    return e->same;
}

void rd__interp_streams_counts(uint32_t *compared, uint32_t *kept)
{
    if (compared) {
        *compared = s_sameCompared;
    }
    if (kept) {
        *kept = s_sameKept;
    }
}
