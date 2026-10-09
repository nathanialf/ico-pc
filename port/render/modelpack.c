/* modelpack.c: model packs (modelpack.h): the folder index, the glTF to VU
 * conversion (strips, colours, bones), the replacement meshes and the dump.
 * The files go through gltf.c, the meshes through rd_mesh.c
 * (rd_CreateVuMeshReplacement, rd_VuMeshRetire).
 */
#include "modelpack.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gltf.h"
#include "host_fs.h"
#include "rd_internal.h"
#include "xxh3.h"

/* how deep under a replacements folder the walk goes (as texpack.c) */
#define MODELPACK_WALK_DEPTH 16

typedef struct PackMesh {
    uint64_t hash;
    char *path;
    float (*qw)[4]; /* vertexCount * qwPerVertex quadwords (normal.w set by Create) */
    uint32_t vertexCount;
    uint32_t qwPerVertex; /* RD_VU_QW_PRELIT / _LIT / _SKIN */
    RdVuReplacementBatch *batches;
    uint32_t batchCount;
    uint32_t bones;   /* skinned: the highest bone a vertex weights + 1 */
    float (*ibm)[16]; /* skinned: the file's own inverse bind matrices */
    uint32_t ibmCount;
    uint8_t ibmChecked;
    uint8_t buildFailLogged;
    uint8_t declined;
    uint64_t bytes;
} PackMesh;

typedef struct HashList {
    uint64_t *h;
    uint32_t n, cap;
} HashList;

static struct {
    int inited;
    int enabled;
    int developer;
    int dumpEnabled;
    char serial[32];
    char userDir[1024];
    PackMesh *e;
    int n, cap;
    int *table; /* open addressing: entry + 1, 0 empty */
    uint32_t tableMask;
    ModelpackStats stats;
    /* dumps */
    int dumpDirReady; /* 0 not tried, 1 made, -1 failed */
    char dumpDir[1100];
    uint64_t *seen; /* open addressing set of dumped hashes (0 empty; hash 0 kept apart) */
    uint32_t seenMask, seenCount;
    int seenZero;
    /* the one-shot object dump */
    const void *shotObj;
    int shotArmed;
    int shotDrawn; /* the object was drawn, in shotFrame */
    uint32_t shotFrame;
    int shotFiles;
    int lastShotFiles;
    HashList shotDone;
} s_mp;

/* ------------------------------------------------------------ helpers */

static uint32_t mixHash(uint64_t h)
{
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return (uint32_t)h;
}

static float bitsF(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t fBits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static const char *layoutName(uint32_t qpv)
{
    return qpv == RD_VU_QW_SKIN ? "skinned" : qpv == RD_VU_QW_LIT ? "lit" : "prelit";
}

/* ------------------------------------------------------------- index */

static int findHash(uint64_t hash)
{
    if (!s_mp.table) {
        return -1;
    }
    for (uint32_t i = mixHash(hash) & s_mp.tableMask;; i = (i + 1) & s_mp.tableMask) {
        int v = s_mp.table[i];
        if (v == 0) {
            return -1;
        }
        if (s_mp.e[v - 1].hash == hash) {
            return v - 1;
        }
    }
}

static int growTable(void)
{
    uint32_t size = 64;
    while (size < (uint32_t)s_mp.n * 2 + 2) {
        size *= 2;
    }
    if (s_mp.table && size <= s_mp.tableMask + 1) {
        return 0;
    }
    int *t = calloc(size, sizeof(*t));
    if (!t) {
        return -1;
    }
    free(s_mp.table);
    s_mp.table = t;
    s_mp.tableMask = size - 1;
    for (int k = 0; k < s_mp.n; k++) {
        uint32_t i = mixHash(s_mp.e[k].hash) & s_mp.tableMask;
        while (t[i] != 0) {
            i = (i + 1) & s_mp.tableMask;
        }
        t[i] = k + 1;
    }
    return 0;
}

/* 1 for ".gltf" / ".glb" in any case after the last '.', else 0 */
static int isModelFile(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }
    char ext[6] = {0};
    size_t n = strlen(dot + 1);
    if (n < 3 || n > 4) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        char c = dot[1 + i];
        ext[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return strcmp(ext, "gltf") == 0 || strcmp(ext, "glb") == 0;
}

/* The hash in a model file's name: the 16 lower-case hex digits before the
 * extension, the whole base name or after a '-'.  0 and *hash, or -1. */
static int parseName(const char *name, uint64_t *hash)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot - name < 16) {
        return -1;
    }
    const char *h = dot - 16;
    if (h != name && h[-1] != '-') {
        return -1;
    }
    if (h != name && h - 1 == name) {
        return -1; /* "-<hash>": an empty name before the dash */
    }
    uint64_t v = 0;
    for (int i = 0; i < 16; i++) {
        char c = h[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) {
            return -1;
        }
        v = v << 4 | (uint64_t)d;
    }
    *hash = v;
    return 0;
}

/* ------------------------------------------------------- conversion */

typedef struct Conv {
    uint32_t qpv;
    float node[16];   /* static: the node's world transform */
    float nmat[9];    /* static: its normal matrix (row-major 3 x 3) */
    int transform;    /* static and the node is not the identity */
    uint32_t maxBone; /* highest weighted bone + 1 */
    char *why;
    size_t whyLen;
} Conv;

static int convFail(Conv *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static int convFail(Conv *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->why, c->whyLen, fmt, ap);
    va_end(ap);
    return -1;
}

static void normalise3(float *n)
{
    const double l = sqrt((double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2]);
    if (l > 0.0 && isfinite(l)) {
        n[0] = (float)(n[0] / l);
        n[1] = (float)(n[1] / l);
        n[2] = (float)(n[2] / l);
    } else {
        n[0] = n[1] = 0.0f;
        n[2] = 1.0f;
    }
}

static int isIdentity(const float m[16])
{
    for (int i = 0; i < 16; i++) {
        if (m[i] != (i % 5 == 0 ? 1.0f : 0.0f)) {
            return 0;
        }
    }
    return 1;
}

static uint8_t colourByte(float c)
{
    float v = roundf(c * 255.0f);
    return (uint8_t)(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
}

/* The two largest of four weights, joints merged by bone first,
 * renormalised.  b[1] 0 and w[1] 0 for one bone (the game's single-bone
 * vertex).  -1 when a weighted bone is 60 or more. */
static int packBones(Conv *c, const uint8_t j[4], const float wt[4], uint32_t b[2], float w[2])
{
    uint32_t bone[4];
    float wsum[4];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        float x = wt[i];
        if (!(x > 0.0f)) {
            continue;
        }
        int k = 0;
        while (k < n && bone[k] != j[i]) {
            k++;
        }
        if (k == n) {
            bone[n] = j[i];
            wsum[n++] = x;
        } else {
            wsum[k] += x;
        }
    }
    if (n == 0) { /* no weight at all: the first joint carries the vertex */
        bone[0] = j[0];
        wsum[0] = 1.0f;
        n = 1;
    }
    /* the two largest, the first of equals kept first */
    for (int a = 0; a < 2 && a < n; a++) {
        for (int k = a + 1; k < n; k++) {
            if (wsum[k] > wsum[a]) {
                uint32_t tb = bone[a];
                float tw = wsum[a];
                bone[a] = bone[k];
                wsum[a] = wsum[k];
                bone[k] = tb;
                wsum[k] = tw;
            }
        }
    }
    for (int k = 0; k < n && k < 2; k++) {
        if (bone[k] >= MODELPACK_MAX_BONES) {
            return convFail(c, "it uses bone %u (the game has at most %u)", bone[k],
                            MODELPACK_MAX_BONES);
        }
        if (bone[k] + 1 > c->maxBone) {
            c->maxBone = bone[k] + 1;
        }
    }
    if (n == 1) {
        b[0] = bone[0];
        w[0] = 1.0f;
        b[1] = 0;
        w[1] = 0.0f;
    } else {
        const float s = wsum[0] + wsum[1];
        b[0] = bone[0];
        b[1] = bone[1];
        w[0] = wsum[0] / s;
        w[1] = wsum[1] / s;
    }
    return 0;
}

/* glTF vertex v of p as a VU vertex (c->qpv quadwords, the strip flag 1,
 * normal.w 0 for now) */
static int vuVertex(Conv *c, const GltfPrim *p, uint32_t v, float (*o)[4])
{
    float pos[3] = {p->pos[v * 3], p->pos[v * 3 + 1], p->pos[v * 3 + 2]};
    float nrm[3] = {0.0f, 0.0f, 1.0f};
    if (p->nrm) {
        nrm[0] = p->nrm[v * 3];
        nrm[1] = p->nrm[v * 3 + 1];
        nrm[2] = p->nrm[v * 3 + 2];
    }
    if (c->transform) {
        const float *m = c->node;
        float x = pos[0], y = pos[1], z = pos[2];
        pos[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
        pos[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
        pos[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
        const float *q = c->nmat;
        x = nrm[0], y = nrm[1], z = nrm[2];
        nrm[0] = q[0] * x + q[1] * y + q[2] * z;
        nrm[1] = q[3] * x + q[4] * y + q[5] * z;
        nrm[2] = q[6] * x + q[7] * y + q[8] * z;
    }
    normalise3(nrm);
    const float u = p->uv ? p->uv[v * 2] : 0.0f, t = p->uv ? p->uv[v * 2 + 1] : 0.0f;
    float col[3] = {128.0f, 128.0f, 128.0f};
    if (p->col) {
        for (int i = 0; i < 3; i++) {
            col[i] = (float)colourByte(p->col[v * 3 + i]);
        }
    }
    int q = 0;
    o[q][0] = pos[0], o[q][1] = pos[1], o[q][2] = pos[2], o[q][3] = 1.0f;
    q++;
    if (c->qpv >= RD_VU_QW_LIT) {
        o[q][0] = nrm[0], o[q][1] = nrm[1], o[q][2] = nrm[2], o[q][3] = 0.0f;
        q++;
    }
    if (c->qpv == RD_VU_QW_SKIN) {
        uint32_t b[2];
        float w[2];
        if (packBones(c, p->joints + v * 4, p->weights + v * 4, b, w) != 0) {
            return -1;
        }
        o[q][0] = bitsF(b[0] * 4 + 16), o[q][1] = w[0];
        o[q][2] = bitsF(b[1] * 4 + 16), o[q][3] = w[1];
        q++;
    }
    o[q][0] = u, o[q][1] = t, o[q][2] = 1.0f, o[q][3] = 1.0f;
    q++;
    o[q][0] = col[0], o[q][1] = col[1], o[q][2] = col[2], o[q][3] = 127.0f;
    return 0;
}

/* A growing array of welded-vertex ids with their strip flags. */
typedef struct Strip {
    uint32_t *id;
    uint8_t *start;
    uint32_t n, cap;
} Strip;

static int stripPush(Strip *s, uint32_t id, int start)
{
    if (s->n == s->cap) {
        uint32_t nc = s->cap ? s->cap * 2 : 256;
        uint32_t *a = realloc(s->id, (size_t)nc * sizeof(*a));
        if (!a) {
            return -1;
        }
        s->id = a;
        uint8_t *b = realloc(s->start, nc);
        if (!b) {
            return -1;
        }
        s->start = b;
        s->cap = nc;
    }
    s->id[s->n] = id;
    s->start[s->n] = (uint8_t)start;
    s->n++;
    return 0;
}

/* t's vertex other than a and b, or -1 when t lacks either */
static int64_t thirdOf(const uint32_t t[3], uint32_t a, uint32_t b)
{
    int ha = 0, hb = 0;
    int64_t r = -1;
    for (int i = 0; i < 3; i++) {
        if (t[i] == a) {
            ha = 1;
        } else if (t[i] == b) {
            hb = 1;
        } else {
            r = t[i];
        }
    }
    return ha && hb ? r : -1;
}

/* Greedy strips over triangles tri[n][3] (welded ids, no vertex twice in
 * one): appends to s. */
static int stripify(const uint32_t (*tri)[3], uint32_t n, Strip *s)
{
    uint32_t at = 0;   /* the current strip's first entry in s */
    uint32_t tris = 0; /* triangles in the current strip */
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t *t = tri[i];
        if (tris > 0) {
            int64_t r = thirdOf(t, s->id[s->n - 2], s->id[s->n - 1]);
            if (r < 0 && tris == 1) {
                /* a lone triangle may turn to give its other edges a try */
                uint32_t a = s->id[at], b = s->id[at + 1], c = s->id[at + 2];
                const uint32_t rot[2][3] = {{b, c, a}, {c, a, b}};
                for (int k = 0; k < 2 && r < 0; k++) {
                    r = thirdOf(t, rot[k][1], rot[k][2]);
                    if (r >= 0) {
                        memcpy(&s->id[at], rot[k], sizeof(rot[k]));
                    }
                }
            }
            if (r >= 0) {
                if (stripPush(s, (uint32_t)r, 0) != 0) {
                    return -1;
                }
                tris++;
                continue;
            }
        }
        at = s->n;
        if (stripPush(s, t[0], 1) != 0 || stripPush(s, t[1], 0) != 0 ||
            stripPush(s, t[2], 0) != 0) {
            return -1;
        }
        tris = 1;
    }
    return 0;
}

/* Vertices equal in every byte share one id: an open-addressing table over
 * the converted vertices. */
static int weld(const float (*vtx)[4], uint32_t nv, uint32_t qpv, uint32_t *map, uint32_t *canon,
                uint32_t *nCanon)
{
    uint32_t size = 64;
    while (size < nv * 2u + 2u) {
        size *= 2;
    }
    uint32_t *t = malloc((size_t)size * sizeof(*t));
    if (!t) {
        return -1;
    }
    memset(t, 0xFF, (size_t)size * sizeof(*t));
    const size_t bytes = (size_t)qpv * 16;
    uint32_t nc = 0;
    for (uint32_t v = 0; v < nv; v++) {
        const float (*x)[4] = vtx + (size_t)v * qpv;
        uint32_t i = (uint32_t)xxh3_64(x, bytes) & (size - 1);
        for (;; i = (i + 1) & (size - 1)) {
            if (t[i] == UINT32_MAX) {
                t[i] = nc;
                canon[nc] = v;
                map[v] = nc++;
                break;
            }
            if (memcmp(vtx + (size_t)canon[t[i]] * qpv, x, bytes) == 0) {
                map[v] = t[i];
                break;
            }
        }
    }
    free(t);
    *nCanon = nc;
    return 0;
}

/* The document as a pack mesh (m->hash and m->path the caller's). */
static int convertDoc(const GltfDoc *d, PackMesh *m, char *why, size_t whyLen)
{
    Conv c;
    memset(&c, 0, sizeof(c));
    c.why = why;
    c.whyLen = whyLen;
    if (d->primCount > MODELPACK_MAX_BATCHES) {
        return convFail(&c, "it has %u pieces (at most %u)", d->primCount, MODELPACK_MAX_BATCHES);
    }
    int skin = 0, allNormals = 1;
    uint64_t total = 0;
    for (uint32_t i = 0; i < d->primCount; i++) {
        const GltfPrim *gp = &d->prims[i];
        const size_t nv = gp->vertexCount;
        for (size_t k = 0; k < nv * 3; k++) {
            if (!isfinite(gp->pos[k]) || (gp->nrm && !isfinite(gp->nrm[k]))) {
                return convFail(&c, "piece %u has a position or normal that is not a number", i);
            }
        }
        for (size_t k = 0; gp->joints && k < nv * 4; k++) {
            if (!isfinite(gp->weights[k])) {
                return convFail(&c, "piece %u has a bone weight that is not a number", i);
            }
        }
        skin |= d->prims[i].joints != NULL;
        allNormals &= d->prims[i].nrm != NULL;
        total += d->prims[i].vertexCount;
    }
    if (skin) {
        for (uint32_t i = 0; i < d->primCount; i++) {
            if (!d->prims[i].joints) {
                return convFail(&c, "piece %u has no bone weights and others have", i);
            }
            if (!d->prims[i].nrm) {
                return convFail(&c, "piece %u has bone weights but no normals", i);
            }
        }
    }
    c.qpv = skin ? RD_VU_QW_SKIN : allNormals ? RD_VU_QW_LIT : RD_VU_QW_PRELIT;
    if (!skin && !isIdentity(d->nodeMatrix)) {
        memcpy(c.node, d->nodeMatrix, sizeof(c.node));
        float inv[16];
        if (gltf_Mat4Invert(inv, c.node) != 0) {
            return convFail(&c, "its transform cannot be undone (a scale of 0)");
        }
        /* the normal matrix: the transpose of the inverse's 3 x 3 (row r,
           column k of the inverse is inv[k * 4 + r]) */
        for (int r = 0; r < 3; r++) {
            for (int k = 0; k < 3; k++) {
                c.nmat[r * 3 + k] = inv[r * 4 + k];
            }
        }
        c.transform = 1;
    }
    const uint32_t qpv = c.qpv;
    float (*vtx)[4] = malloc((size_t)(total ? total : 1) * qpv * 16);
    uint32_t *map = malloc((size_t)(total ? total : 1) * sizeof(*map));
    uint32_t *canon = malloc((size_t)(total ? total : 1) * sizeof(*canon));
    RdVuReplacementBatch *batches = calloc(d->primCount ? d->primCount : 1, sizeof(*batches));
    Strip s = {0};
    uint32_t (*tri)[3] = NULL;
    int rc = -1;
    if (!vtx || !map || !canon || !batches) {
        convFail(&c, "out of memory");
        goto done;
    }
    /* every primitive's strips, appended as one welded-id list with the
       batch boundaries */
    uint32_t base = 0;
    for (uint32_t i = 0; i < d->primCount; i++) {
        const GltfPrim *p = &d->prims[i];
        for (uint32_t v = 0; v < p->vertexCount; v++) {
            if (vuVertex(&c, p, v, vtx + (size_t)(base + v) * qpv) != 0) {
                goto done;
            }
        }
        base += p->vertexCount;
    }
    uint32_t nc = 0;
    if (weld((const float (*)[4])vtx, (uint32_t)total, qpv, map, canon, &nc) != 0) {
        convFail(&c, "out of memory");
        goto done;
    }
    base = 0;
    for (uint32_t i = 0; i < d->primCount; i++) {
        const GltfPrim *p = &d->prims[i];
        const uint32_t ni = p->idx ? p->indexCount : p->vertexCount;
        uint32_t (*nt)[3] = realloc(tri, (size_t)(ni / 3 + 1) * sizeof(*tri));
        if (!nt) {
            convFail(&c, "out of memory");
            goto done;
        }
        tri = nt;
        uint32_t n = 0;
        for (uint32_t k = 0; k + 2 < ni; k += 3) {
            uint32_t a, b, e;
            if (p->idx) {
                a = p->idx[k], b = p->idx[k + 1], e = p->idx[k + 2];
            } else {
                a = k, b = k + 1, e = k + 2;
            }
            a = map[base + a], b = map[base + b], e = map[base + e];
            if (a == b || b == e || a == e) {
                continue; /* draws nothing */
            }
            tri[n][0] = a, tri[n][1] = b, tri[n][2] = e;
            n++;
        }
        batches[i].firstVertex = s.n;
        if (stripify((const uint32_t (*)[3])tri, n, &s) != 0) {
            convFail(&c, "out of memory");
            goto done;
        }
        batches[i].vertexCount = s.n - batches[i].firstVertex;
        if (s.n > MODELPACK_MAX_VERTICES) {
            convFail(&c, "it needs more than %u vertices as strips", MODELPACK_MAX_VERTICES);
            goto done;
        }
        base += p->vertexCount;
    }
    m->qw = malloc((size_t)(s.n ? s.n : 1) * qpv * 16);
    if (!m->qw) {
        convFail(&c, "out of memory");
        goto done;
    }
    const uint32_t stAt = qpv - 2;
    for (uint32_t k = 0; k < s.n; k++) {
        float (*o)[4] = m->qw + (size_t)k * qpv;
        memcpy(o, vtx + (size_t)canon[s.id[k]] * qpv, (size_t)qpv * 16);
        o[stAt][3] = s.start[k] ? 0.0f : 1.0f;
    }
    m->vertexCount = s.n;
    m->qwPerVertex = qpv;
    m->batches = batches;
    m->batchCount = d->primCount;
    m->bones = skin ? c.maxBone : 0;
    if (skin && d->skin.count > 0 && d->skin.count <= MODELPACK_MAX_BONES && d->skin.invBind) {
        m->ibm = malloc((size_t)d->skin.count * sizeof(*m->ibm));
        if (m->ibm) {
            memcpy(m->ibm, d->skin.invBind, (size_t)d->skin.count * sizeof(*m->ibm));
            m->ibmCount = d->skin.count;
        }
    }
    m->bytes = (uint64_t)s.n * qpv * 16 + (uint64_t)d->primCount * sizeof(*batches);
    batches = NULL;
    rc = 0;
done:
    free(vtx);
    free(map);
    free(canon);
    free(batches);
    free(tri);
    free(s.id);
    free(s.start);
    return rc;
}

/* ------------------------------------------------------------- walk */

typedef struct WalkCtx {
    uint32_t added;
} WalkCtx;

static int addFile(const char *path, const char *name, void *user)
{
    WalkCtx *w = (WalkCtx *)user;
    uint64_t hash;
    if (!isModelFile(name)) {
        return 0; /* the .bin beside a .gltf, a readme, a picture: not ours */
    }
    s_mp.stats.files++;
    if (parseName(name, &hash) != 0) {
        s_mp.stats.badNames++;
        return 0;
    }
    if (findHash(hash) >= 0) {
        s_mp.stats.duplicates++;
        return 0;
    }
    GltfDoc doc;
    char why[256] = "";
    PackMesh m;
    memset(&m, 0, sizeof(m));
    m.hash = hash;
    if (gltf_Read(path, &doc, why, sizeof(why)) != 0 ||
        convertDoc(&doc, &m, why, sizeof(why)) != 0) {
        gltf_Free(&doc);
        fprintf(stderr, "models: %s skipped: %s\n", path, why);
        s_mp.stats.failed++;
        return 0;
    }
    gltf_Free(&doc);
    if (s_mp.stats.bytes + m.bytes > MODELPACK_MAX_BYTES) {
        fprintf(stderr, "models: %s skipped: the model pack is over %llu MB\n", path,
                (unsigned long long)(MODELPACK_MAX_BYTES >> 20));
        free(m.qw);
        free(m.batches);
        free(m.ibm);
        s_mp.stats.failed++;
        return 0;
    }
    if (s_mp.n == s_mp.cap) {
        int ncap = s_mp.cap ? s_mp.cap * 2 : 64;
        PackMesh *g = realloc(s_mp.e, (size_t)ncap * sizeof(*g));
        if (!g) {
            free(m.qw);
            free(m.batches);
            free(m.ibm);
            return 1;
        }
        s_mp.e = g;
        s_mp.cap = ncap;
    }
    m.path = malloc(strlen(path) + 1);
    if (!m.path) {
        free(m.qw);
        free(m.batches);
        free(m.ibm);
        return 1;
    }
    strcpy(m.path, path);
    s_mp.e[s_mp.n++] = m;
    if (growTable() != 0) {
        s_mp.n--;
        free(m.qw);
        free(m.batches);
        free(m.ibm);
        free(m.path);
        return 1;
    }
    if (findHash(hash) < 0) {
        uint32_t i = mixHash(hash) & s_mp.tableMask;
        while (s_mp.table[i] != 0) {
            i = (i + 1) & s_mp.tableMask;
        }
        s_mp.table[i] = s_mp.n;
    }
    s_mp.stats.bytes += m.bytes;
    w->added++;
    return 0;
}

static void walkFolder(const char *dir, int depth)
{
    WalkCtx w = {0};
    if (ico_dir_walk(dir, depth, addFile, &w) < 0) {
        return;
    }
    if (w.added > 0) {
        fprintf(stderr, "models: %u replacements from %s\n", w.added, dir);
    }
}

/* ------------------------------------------------------------ public */

int modelpack_Init(const ModelpackConfig *cfg)
{
    char dirs[2][1100];
    char std[2][1100];
    int nStd = 0;

    modelpack_Shutdown();
    memset(&s_mp, 0, sizeof(s_mp));
    s_mp.enabled = 1;
    if (!cfg) {
        return 0;
    }
    snprintf(s_mp.serial, sizeof(s_mp.serial), "%s", cfg->serial ? cfg->serial : "SCES-50760");
    snprintf(s_mp.userDir, sizeof(s_mp.userDir), "%s", cfg->userDir ? cfg->userDir : "");
    s_mp.developer = cfg->developer != 0;
    s_mp.dumpEnabled = cfg->dumpEnabled != 0;
    s_mp.inited = 1;

    /* the folders in texpack_Init's order: the standard layout of the user
       folder, then of the program's folder, then the tolerant layouts of
       both; a root given twice (portable mode) once */
    const char *roots[2] = {cfg->userDir, cfg->programDir};
    int nRoots = 0;
    const char *uniq[2];
    for (int i = 0; i < 2; i++) {
        if (roots[i] && roots[i][0] && !(nRoots == 1 && strcmp(uniq[0], roots[i]) == 0)) {
            uniq[nRoots++] = roots[i];
        }
    }
    char tail[64];
    snprintf(tail, sizeof(tail), "models/%s/replacements", s_mp.serial);
    for (int i = 0; i < nRoots; i++) {
        if (rd__JoinPath(std[nStd], sizeof(std[0]), uniq[i], tail) == 0) {
            walkFolder(std[nStd], MODELPACK_WALK_DEPTH);
            nStd++;
        }
    }
    for (int i = 0; i < nRoots; i++) {
        if (rd__JoinPath(dirs[0], sizeof(dirs[0]), uniq[i], "models/replacements") == 0) {
            walkFolder(dirs[0], MODELPACK_WALK_DEPTH);
        }
        /* files directly under models/ only: below it are the serial
           folders (walked above) and the dumps */
        if (rd__JoinPath(dirs[1], sizeof(dirs[1]), uniq[i], "models") == 0) {
            walkFolder(dirs[1], 0);
        }
    }
    s_mp.stats.indexed = (uint32_t)s_mp.n;
    if (s_mp.n == 0) {
        fprintf(stderr, "models: no model pack (looked in %s%s%s)\n",
                nStd > 0 ? std[0] : "no folder", nStd > 1 ? ", " : "", nStd > 1 ? std[1] : "");
    }
    if (s_mp.stats.duplicates || s_mp.stats.badNames) {
        fprintf(stderr,
                "models: skipped %u files named like another, %u files whose names are not "
                "model names\n",
                s_mp.stats.duplicates, s_mp.stats.badNames);
    }
    return s_mp.n;
}

int modelpack_Count(void)
{
    return s_mp.n;
}

bool modelpack_HashWanted(void)
{
    return s_mp.n > 0 || s_mp.dumpEnabled || s_mp.shotArmed;
}

void modelpack_NoteSkeleton(int entry, const ModelpackSkeleton *skel)
{
    if (entry < 0 || entry >= s_mp.n || !skel) {
        return;
    }
    PackMesh *m = &s_mp.e[entry];
    if (m->ibmChecked || !m->ibm) {
        return;
    }
    m->ibmChecked = 1;
    const uint32_t n = m->ibmCount < skel->count ? m->ibmCount : skel->count;
    for (uint32_t b = 0; b < n; b++) {
        for (int k = 0; k < 16; k++) {
            const float d = fabsf(m->ibm[b][k] - skel->invBind[b][k]);
            if (!(d <= 1e-3f * (1.0f + fabsf(skel->invBind[b][k])))) {
                fprintf(stderr,
                        "models: %s has its own inverse bind matrices, which differ from the "
                        "game's; the game's are used\n",
                        m->path);
                return;
            }
        }
    }
}

void modelpack_GetStats(ModelpackStats *out)
{
    *out = s_mp.stats;
    out->indexed = (uint32_t)s_mp.n;
}

void modelpack_Shutdown(void)
{
    for (int i = 0; i < s_mp.n; i++) {
        free(s_mp.e[i].path);
        free(s_mp.e[i].qw);
        free(s_mp.e[i].batches);
        free(s_mp.e[i].ibm);
    }
    free(s_mp.e);
    free(s_mp.table);
    free(s_mp.seen);
    free(s_mp.shotDone.h);
    memset(&s_mp, 0, sizeof(s_mp));
    s_mp.enabled = 1;
}

int modelpack_Lookup(uint64_t hash)
{
    if (s_mp.n == 0 || !s_mp.enabled) {
        return -1;
    }
    int e = findHash(hash);
    return e >= 0 && !s_mp.e[e].declined ? e : -1;
}

static void decline(PackMesh *m, const char *part, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void decline(PackMesh *m, const char *part, const char *fmt, ...)
{
    char why[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof(why), fmt, ap);
    va_end(ap);
    if (!m->declined) {
        fprintf(stderr, "models: %s not used for %s: %s\n", m->path, part ? part : "a part", why);
        s_mp.stats.declined++;
    }
    m->declined = 1;
}

RdMesh modelpack_Create(int entry, const RdVuMeshDesc *orig, const char *name, uint32_t boneCount)
{
    if (entry < 0 || entry >= s_mp.n || !orig) {
        return (RdMesh){0};
    }
    PackMesh *m = &s_mp.e[entry];
    if (m->declined) {
        return (RdMesh){0};
    }
    const char *part = name ? name : orig->debugName;
    float normalW = 0.0f;
    if (rd_VuMeshDescHash(orig, NULL, &normalW) != m->hash) {
        return (RdMesh){0}; /* not the part this entry is for: nothing to decline */
    }
    const uint32_t oq = orig->qwPerVertex;
    if (oq != m->qwPerVertex && !(oq == RD_VU_QW_PRELIT && m->qwPerVertex == RD_VU_QW_LIT)) {
        if (oq == RD_VU_QW_SKIN) {
            decline(m, part, "the original is skinned (it moves with bones) and this file is not");
        } else if (m->qwPerVertex == RD_VU_QW_SKIN) {
            decline(m, part,
                    "this file has bone weights and the original does not move with "
                    "bones");
        } else if (oq == RD_VU_QW_LIT) {
            decline(m, part, "the original is lit and needs normals, which this file lacks");
        } else {
            decline(m, part, "the original is a %s part and this file a %s one", layoutName(oq),
                    layoutName(m->qwPerVertex));
        }
        return (RdMesh){0};
    }
    if (m->batchCount > orig->batchCount) {
        decline(m, part, "it has %u pieces and the original %u", m->batchCount, orig->batchCount);
        return (RdMesh){0};
    }
    if (oq == RD_VU_QW_SKIN) {
        const uint32_t cap = boneCount < MODELPACK_MAX_BONES ? boneCount : MODELPACK_MAX_BONES;
        if (m->bones > cap) {
            decline(m, part, "it uses bone %u and the model has %u", m->bones - 1, cap);
            return (RdMesh){0};
        }
    }
    float (*tmp)[4] = NULL;
    RdVuReplacement rep;
    memset(&rep, 0, sizeof(rep));
    rep.vertexCount = m->vertexCount;
    rep.qwPerVertex = oq;
    rep.batches = m->batches;
    rep.batchCount = m->batchCount;
    if (oq == m->qwPerVertex) {
        if (oq >= RD_VU_QW_LIT) {
            for (uint32_t v = 0; v < m->vertexCount; v++) {
                m->qw[(size_t)v * oq + 1][3] = normalW;
            }
        }
        rep.qw = (const float (*)[4])m->qw;
    } else {
        /* a lit file for a prelit part: the normals dropped */
        tmp = malloc((size_t)(m->vertexCount ? m->vertexCount : 1) * oq * 16);
        if (!tmp) {
            return (RdMesh){0};
        }
        for (uint32_t v = 0; v < m->vertexCount; v++) {
            const float (*s)[4] = m->qw + (size_t)v * RD_VU_QW_LIT;
            memcpy(tmp[(size_t)v * oq], s[0], 16);
            memcpy(tmp[(size_t)v * oq + 1], s[2], 32);
        }
        rep.qw = (const float (*)[4])tmp;
    }
    for (uint32_t b = 0; b < m->batchCount; b++) {
        if ((uint64_t)m->batches[b].firstVertex + m->batches[b].vertexCount > m->vertexCount) {
            free(tmp);
            decline(m, part, "its pieces do not fit its vertices");
            return (RdMesh){0};
        }
    }
    RdMesh mesh = rd_CreateVuMeshReplacement(orig, &rep, name);
    free(tmp);
    if (mesh.id == 0) {
        /* not a mismatch the file could be blamed for (it was checked
           above): a failed allocation, say.  The entry stays on; the next
           build tries again */
        if (!m->buildFailLogged) {
            m->buildFailLogged = 1;
            fprintf(stderr,
                    "models: %s: the game could not build %s just now; trying again later\n",
                    m->path, part ? part : "a part");
        }
        return mesh;
    }
    s_mp.stats.created++;
    if (s_mp.developer) {
        fprintf(stderr, "models: %s replaced by %s\n", part ? part : "a part", m->path);
    }
    return mesh;
}

void modelpack_Decline(uint64_t hash)
{
    int e = findHash(hash);
    if (e >= 0 && !s_mp.e[e].declined) {
        s_mp.e[e].declined = 1;
        s_mp.stats.declined++;
    }
}

static bool retireReplaced(uint64_t hash, bool replaced, void *user)
{
    (void)hash, (void)user;
    return replaced;
}

static bool retireOriginal(uint64_t hash, bool replaced, void *user)
{
    (void)user;
    return !replaced && modelpack_Lookup(hash) >= 0;
}

void modelpack_SetEnabled(bool on)
{
    if ((s_mp.enabled != 0) == on) {
        return;
    }
    s_mp.enabled = on;
    if (s_mp.n == 0) {
        return;
    }
    rd_VuMeshRetire(on ? retireOriginal : retireReplaced, NULL);
}

bool modelpack_Enabled(void)
{
    return s_mp.enabled != 0;
}

/* ------------------------------------------------------------- dumps */

static int seenHas(uint64_t h)
{
    if (h == 0) {
        return s_mp.seenZero;
    }
    if (!s_mp.seen) {
        return 0;
    }
    for (uint32_t i = mixHash(h) & s_mp.seenMask;; i = (i + 1) & s_mp.seenMask) {
        if (s_mp.seen[i] == 0) {
            return 0;
        }
        if (s_mp.seen[i] == h) {
            return 1;
        }
    }
}

static void seenAdd(uint64_t h)
{
    if (h == 0) {
        s_mp.seenZero = 1;
        return;
    }
    if (seenHas(h)) {
        return;
    }
    if (!s_mp.seen || (s_mp.seenCount + 1) * 2 > s_mp.seenMask + 1) {
        uint32_t size = s_mp.seen ? (s_mp.seenMask + 1) * 2 : 256;
        uint64_t *t = calloc(size, sizeof(*t));
        if (!t) {
            return;
        }
        for (uint32_t k = 0; s_mp.seen && k <= s_mp.seenMask; k++) {
            if (s_mp.seen[k]) {
                uint32_t i = mixHash(s_mp.seen[k]) & (size - 1);
                while (t[i]) {
                    i = (i + 1) & (size - 1);
                }
                t[i] = s_mp.seen[k];
            }
        }
        free(s_mp.seen);
        s_mp.seen = t;
        s_mp.seenMask = size - 1;
    }
    uint32_t i = mixHash(h) & s_mp.seenMask;
    while (s_mp.seen[i]) {
        i = (i + 1) & s_mp.seenMask;
    }
    s_mp.seen[i] = h;
    s_mp.seenCount++;
}

static int listHas(const HashList *l, uint64_t h)
{
    for (uint32_t i = 0; i < l->n; i++) {
        if (l->h[i] == h) {
            return 1;
        }
    }
    return 0;
}

static void listAdd(HashList *l, uint64_t h)
{
    if (l->n == l->cap) {
        uint32_t nc = l->cap ? l->cap * 2 : 32;
        uint64_t *a = realloc(l->h, (size_t)nc * sizeof(*a));
        if (!a) {
            return;
        }
        l->h = a;
        l->cap = nc;
    }
    l->h[l->n++] = h;
}

/* the shot ends when a frame after the one that drew its object opens */
static void shotTick(void)
{
    if (s_mp.shotArmed && s_mp.shotDrawn && g_rd.frameCounter != s_mp.shotFrame) {
        s_mp.shotArmed = 0;
        s_mp.lastShotFiles = s_mp.shotFiles;
        s_mp.shotDone.n = 0;
    }
}

static int shotActive(const void *obj)
{
    shotTick();
    return obj && s_mp.shotArmed && obj == s_mp.shotObj;
}

bool modelpack_DumpWanted(uint64_t hash, const void *obj)
{
    if (shotActive(obj)) {
        if (!s_mp.shotDrawn) {
            s_mp.shotDrawn = 1;
            s_mp.shotFrame = g_rd.frameCounter;
        }
        return !listHas(&s_mp.shotDone, hash);
    }
    return s_mp.dumpEnabled && !seenHas(hash);
}

void modelpack_SetDumpEnabled(bool on)
{
    s_mp.dumpEnabled = on;
}

void modelpack_DumpObjectOnce(const void *obj)
{
    s_mp.shotObj = obj;
    s_mp.shotArmed = obj != NULL;
    s_mp.shotDrawn = 0;
    s_mp.shotFrame = 0;
    s_mp.shotFiles = 0;
    s_mp.shotDone.n = 0;
}

int modelpack_DumpObjectStatus(void)
{
    shotTick();
    return s_mp.shotArmed ? -1 : s_mp.lastShotFiles;
}

const char *modelpack_DumpDir(void)
{
    return s_mp.dumpDirReady > 0 ? s_mp.dumpDir : "";
}

static int ensureDumpDir(void)
{
    if (s_mp.dumpDirReady) {
        return s_mp.dumpDirReady > 0 ? 0 : -1;
    }
    char p[1100], q[1100];
    s_mp.dumpDirReady = -1;
    if (!s_mp.inited || s_mp.userDir[0] == '\0' ||
        rd__JoinPath(p, sizeof(p), s_mp.userDir, "models") != 0 ||
        rd__JoinPath(q, sizeof(q), p, s_mp.serial) != 0 ||
        rd__JoinPath(s_mp.dumpDir, sizeof(s_mp.dumpDir), q, "dumps") != 0) {
        s_mp.dumpDir[0] = '\0';
        return -1;
    }
    (void)ico_mkdir(p);
    (void)ico_mkdir(q);
    (void)ico_mkdir(s_mp.dumpDir);
    if (ico_path_kind(s_mp.dumpDir, NULL, NULL) != 1) {
        fprintf(stderr, "models: cannot make %s; no model dumps\n", s_mp.dumpDir);
        return -1;
    }
    fprintf(stderr, "models: writing model dumps to %s\n", s_mp.dumpDir);
    s_mp.dumpDirReady = 1;
    return 0;
}

static uint32_t tagNloop(const float *q)
{
    uint32_t w;
    memcpy(&w, q, 4);
    return w & 0x7FFF;
}

static uint32_t tagPrimBits(const float *q)
{
    uint32_t hi;
    memcpy(&hi, (const char *)q + 4, 4);
    return (hi >> 15) & 0x7FF;
}

/* text appended to a growing buffer */
typedef struct Text {
    char *p;
    size_t n, cap;
    int bad;
} Text;

static void tput(Text *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void tput(Text *t, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(t->p ? t->p + t->n : NULL, t->p ? t->cap - t->n : 0, fmt, ap);
        va_end(ap);
        if (k < 0) {
            t->bad = 1;
            return;
        }
        if (t->p && t->n + (size_t)k < t->cap) {
            t->n += (size_t)k;
            return;
        }
        size_t nc = t->cap ? t->cap * 2 : 1024;
        while (nc < t->n + (size_t)k + 1) {
            nc *= 2;
        }
        char *g = realloc(t->p, nc);
        if (!g) {
            t->bad = 1;
            return;
        }
        t->p = g;
        t->cap = nc;
    }
}

/* s as a JSON string's contents (control characters and quotes escaped) */
static void tjson(Text *t, const char *s)
{
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            tput(t, "\\%c", c);
        } else if (c < 0x20) {
            tput(t, "\\u%04x", c);
        } else {
            tput(t, "%c", c);
        }
    }
}

/* The stream's per-vertex arrays for one batch (n vertices at at). */
typedef struct DumpPrim {
    float *pos, *nrm, *uv, *col, *wt;
    uint8_t *jt;
    uint32_t *idx;
} DumpPrim;

int modelpack_Dump(const RdVuMeshDesc *orig, const ModelpackIdent *id,
                   const ModelpackSkeleton *skel)
{
    const void *obj = id ? id->obj : NULL;
    const int shot = shotActive(obj);
    uint32_t nv = 0;
    float normalW = 0.0f;
    if (!orig || (!shot && !s_mp.dumpEnabled)) {
        return 0;
    }
    const uint64_t hash = rd_VuMeshDescHash(orig, &nv, &normalW);
    const uint32_t qpv = orig->qwPerVertex;
    if ((hash == 0 && nv == 0) ||
        (qpv != RD_VU_QW_PRELIT && qpv != RD_VU_QW_LIT && qpv != RD_VU_QW_SKIN)) {
        return 0;
    }
    const uint64_t built = id ? id->buildHash : 0;
    if (shot ? listHas(&s_mp.shotDone, hash) : seenHas(hash)) {
        return 0;
    }
    /* a part that changes shape: the hash it was built with stands for it */
    if (built != 0 && (shot ? listHas(&s_mp.shotDone, built) : seenHas(built))) {
        return 0;
    }
    if (shot) {
        if (!s_mp.shotDrawn) {
            s_mp.shotDrawn = 1;
            s_mp.shotFrame = g_rd.frameCounter;
        }
        listAdd(&s_mp.shotDone, hash);
        if (built != 0) {
            listAdd(&s_mp.shotDone, built);
        }
    }
    seenAdd(hash);
    if (built != 0) {
        seenAdd(built);
    }
    if (ensureDumpDir() != 0) {
        return 0;
    }
    char base[1200], path[1210], hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
    if (rd__JoinPath(base, sizeof(base), s_mp.dumpDir, hex) != 0) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s.gltf", base);
    const int existed = ico_path_kind(path, NULL, NULL) >= 0;
    if (existed && !shot) {
        return 0; /* dumped in an earlier session: kept */
    }

    const uint32_t nb = orig->batchCount;
    const int skinned = qpv == RD_VU_QW_SKIN, lit = qpv >= RD_VU_QW_LIT;
    const uint32_t stAt = qpv - 2, colAt = qpv - 1;
    GltfDoc doc;
    gltf_DocInit(&doc);
    GltfPrim *prims = calloc(nb, sizeof(*prims));
    DumpPrim *dp = calloc(nb, sizeof(*dp));
    float (*ib)[16] = NULL;
    int *parent = NULL;
    Text ex = {0};
    char why[256] = "";
    char meshName[200];
    int written = 0;
    float bmin[3] = {INFINITY, INFINITY, INFINITY}, bmax[3] = {-INFINITY, -INFINITY, -INFINITY};
    uint32_t maxBone = 0;
    if (!prims || !dp) {
        goto done;
    }
    for (uint32_t b = 0; b < nb; b++) {
        const uint32_t at = orig->batches[b].firstQw;
        const uint32_t n = tagNloop(orig->qw[at]);
        const float (*v0)[4] = orig->qw + at + 1;
        const uint32_t pv = n ? n : 1; /* an empty batch: one vertex, one empty triangle */
        DumpPrim *d = &dp[b];
        d->pos = calloc((size_t)pv * 3, sizeof(float));
        d->uv = calloc((size_t)pv * 2, sizeof(float));
        d->col = calloc((size_t)pv * 3, sizeof(float));
        d->nrm = lit ? calloc((size_t)pv * 3, sizeof(float)) : NULL;
        d->wt = skinned ? calloc((size_t)pv * 4, sizeof(float)) : NULL;
        d->jt = skinned ? calloc((size_t)pv * 4, 1) : NULL;
        d->idx = malloc((size_t)(n > 2 ? (n - 2) * 3 : 3) * sizeof(uint32_t));
        if (!d->pos || !d->uv || !d->col || (lit && !d->nrm) || (skinned && (!d->wt || !d->jt)) ||
            !d->idx) {
            goto done;
        }
        if (n == 0 && d->nrm) {
            d->nrm[2] = 1.0f;
        }
        if (n == 0 && d->wt) {
            d->wt[0] = 1.0f;
        }
        for (uint32_t k = 0; k < n; k++) {
            const float (*v)[4] = v0 + (size_t)k * qpv;
            for (int i = 0; i < 3; i++) {
                d->pos[k * 3 + i] = v[0][i];
                bmin[i] = v[0][i] < bmin[i] ? v[0][i] : bmin[i];
                bmax[i] = v[0][i] > bmax[i] ? v[0][i] : bmax[i];
                d->col[k * 3 + i] = v[colAt][i] / 255.0f;
            }
            d->uv[k * 2] = v[stAt][0];
            d->uv[k * 2 + 1] = v[stAt][1];
            if (lit) {
                memcpy(&d->nrm[k * 3], v[1], 12);
                normalise3(&d->nrm[k * 3]);
            }
            if (skinned) {
                /* (addr, w, addr, w): bone = (addr - 16) / 4; one bone when
                   the other has no weight or is the same bone */
                uint32_t b0 = (fBits(v[2][0]) - 16) / 4, b1 = (fBits(v[2][2]) - 16) / 4;
                float w0 = v[2][1], w1 = v[2][3];
                if (b0 >= MODELPACK_MAX_BONES) {
                    b0 = 0;
                }
                if (b1 >= MODELPACK_MAX_BONES) {
                    b1 = 0;
                }
                if (b1 == b0) {
                    w0 += w1;
                    w1 = 0.0f;
                }
                if (!(w0 > 0.0f) && w1 > 0.0f) {
                    b0 = b1;
                    w0 = w1;
                    w1 = 0.0f;
                }
                uint8_t *j = &d->jt[k * 4];
                float *w = &d->wt[k * 4];
                if (!(w1 > 0.0f) || !(w0 > 0.0f)) {
                    j[0] = (uint8_t)b0;
                    w[0] = 1.0f;
                    maxBone = b0 + 1 > maxBone ? b0 + 1 : maxBone;
                } else {
                    const float s = w0 + w1;
                    j[0] = (uint8_t)b0;
                    j[1] = (uint8_t)b1;
                    w[0] = w0 / s;
                    w[1] = w1 / s;
                    maxBone = b0 + 1 > maxBone ? b0 + 1 : maxBone;
                    maxBone = b1 + 1 > maxBone ? b1 + 1 : maxBone;
                }
            }
        }
        /* the kicks (rd_CreateVuMesh's rule): vertex k >= 2 draws (k-2,
           k-1, k) unless k or k-1 starts a strip; every other triangle of a
           strip turned back so the faces agree */
        uint32_t ni = 0, start = 0;
        for (uint32_t k = 0; k < n; k++) {
            if (v0[(size_t)k * qpv + stAt][3] < 1.0f) {
                start = k;
            }
            if (k < 2 || v0[(size_t)k * qpv + stAt][3] < 1.0f ||
                v0[(size_t)(k - 1) * qpv + stAt][3] < 1.0f) {
                continue;
            }
            const int odd = (k - start) % 2 == 1;
            d->idx[ni++] = odd ? k - 1 : k - 2;
            d->idx[ni++] = odd ? k - 2 : k - 1;
            d->idx[ni++] = k;
        }
        if (ni == 0) {
            d->idx[0] = d->idx[1] = d->idx[2] = 0; /* no triangle: one that draws nothing */
            ni = 3;
        }
        GltfPrim *p = &prims[b];
        p->pos = d->pos;
        p->nrm = d->nrm;
        p->uv = d->uv;
        p->col = d->col;
        p->joints = d->jt;
        p->weights = d->wt;
        p->idx = d->idx;
        p->vertexCount = pv;
        p->indexCount = ni;
    }
    if (nv == 0) {
        memset(bmin, 0, sizeof(bmin));
        memset(bmax, 0, sizeof(bmax));
    }
    uint32_t bones = 0;
    if (skinned) {
        bones = skel && skel->count > maxBone ? skel->count : maxBone;
        if (bones > MODELPACK_MAX_BONES) {
            bones = MODELPACK_MAX_BONES;
        }
        if (bones == 0) {
            bones = 1;
        }
        ib = calloc(bones, sizeof(*ib));
        parent = calloc(bones, sizeof(*parent));
        if (!ib || !parent) {
            goto done;
        }
        for (uint32_t i = 0; i < bones; i++) {
            const int have = skel && i < skel->count;
            if (have) {
                memcpy(ib[i], skel->invBind[i], 64);
            } else {
                gltf_Mat4Identity(ib[i]);
            }
            int pr = have ? skel->parent[i] : -1;
            /* a parent that is not an earlier bone would make a loop or
               point past the skin: a root then */
            parent[i] = pr >= 0 && (uint32_t)pr < bones && (uint32_t)pr != i ? pr : -1;
        }
        /* loops (a parent chain longer than the bones) are cut at the bone */
        for (uint32_t i = 0; i < bones; i++) {
            uint32_t steps = 0;
            for (int q = parent[i]; q >= 0; q = parent[q]) {
                if (++steps > bones) {
                    parent[i] = -1;
                    break;
                }
            }
        }
        doc.skin.count = bones;
        doc.skin.invBind = ib;
        doc.skin.parent = parent;
    }
    snprintf(meshName, sizeof(meshName), "%s/part%d/%d", id && id->model ? id->model : "model",
             id ? id->part : 0, id ? id->ordinal : 0);
    doc.prims = prims;
    doc.primCount = nb;
    doc.meshName = meshName;
    /* asset.extras.ico */
    tput(&ex, "{\"ico\":{\"hash\":\"%s\",\"model\":\"", hex);
    tjson(&ex, id && id->model ? id->model : "");
    tput(&ex, "\",\"part\":%d,\"ordinal\":%d,\"layout\":\"%s\",\"qwPerVertex\":%u,\"batches\":[",
         id ? id->part : 0, id ? id->ordinal : 0, layoutName(qpv), qpv);
    for (uint32_t b = 0; b < nb; b++) {
        const uint32_t at = orig->batches[b].firstQw;
        tput(&ex, "%s{\"vertices\":%u,\"prim\":%u,\"material\":%u}", b ? "," : "",
             tagNloop(orig->qw[at]), tagPrimBits(orig->qw[at]), orig->batches[b].material);
    }
    tput(&ex,
         "],\"normalW\":%.9g,\"bones\":%u,\"box\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],"
         "\"colorScale\":128}}",
         (double)normalW, bones, (double)bmin[0], (double)bmin[1], (double)bmin[2], (double)bmax[0],
         (double)bmax[1], (double)bmax[2]);
    if (ex.bad) {
        goto done;
    }
    doc.extrasText = ex.p;
    if (gltf_Write(base, &doc, why, sizeof(why)) != 0) {
        fprintf(stderr, "models: cannot write the dump of %s: %s\n", meshName, why);
        goto done;
    }
    written = 2;
    s_mp.stats.dumped++;
    if (!existed) {
        char list[1210];
        if (rd__JoinPath(list, sizeof(list), s_mp.dumpDir, "models.txt") == 0) {
            const int fresh = ico_path_kind(list, NULL, NULL) < 0;
            FILE *f = ico_fopen(list, "ab");
            if (f) {
                if (fresh) {
                    fprintf(f, "hash\tmodel\tpart\tordinal\tlayout\tvertices\tbatches\tbones\n");
                }
                fprintf(f, "%s\t%s\t%d\t%d\t%s\t%u\t%u\t%u\n", hex,
                        id && id->model ? id->model : "", id ? id->part : 0, id ? id->ordinal : 0,
                        layoutName(qpv), nv, nb, bones);
                fclose(f);
            }
        }
    }
    if (shot) {
        s_mp.shotFiles += written;
    }
done:
    for (uint32_t b = 0; dp && b < nb; b++) {
        free(dp[b].pos);
        free(dp[b].nrm);
        free(dp[b].uv);
        free(dp[b].col);
        free(dp[b].wt);
        free(dp[b].jt);
        free(dp[b].idx);
    }
    free(dp);
    free(prims);
    free(ib);
    free(parent);
    free(ex.p);
    return written;
}
