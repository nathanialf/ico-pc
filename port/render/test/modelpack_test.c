/* modelpack_test.c: model packs (v0.4.1, M3; modelpack.h).
 *
 * Packet.c, RegistPacket.c, MicroCode.c, DisplayP2O.c and Primitive.c with
 * the 2D layer and Matrix.c, as rd_mesh_test.c has them (test/mesh_env.h
 * stubs the rest of the game), and the pack module on files under the
 * build tree (argv[1]/modelpack_tmp).
 *
 * Cases (no device needed):
 *   index      a fake tree: the user and the program folder's standard
 *              layouts, the tolerant ones, a name- prefix, a .glb, a
 *              duplicate (the user folder's wins), names that are not model
 *              names, a broken file, other files passed over quietly, the
 *              dumps folder and folders below models/ not walked
 *   strips     a two-batch prelit stream with restarts dumped, read back
 *              and made a replacement: its triangles are the original's
 *              (vu1ref_StaticKicks); a triangle list joined into strips
 *              (shared edges, a lone triangle turned), the node transform,
 *              colours, the layout rules, more pieces than batches
 *   bones      four weights to the two largest renormalised, joints merged,
 *              one bone as (bone, 1) (bone 0, 0), joints by name, bone 60
 *              refused, the model's bone count
 *   cluster    makeModel's two-bone cluster packet dumped through the
 *              one-shot object dump, read back and drawn through
 *              reg_DispObj in the original's place: RDC_SKINNED with the
 *              original's counts, kicks and bones
 *   switch     modelpack_SetEnabled retiring, modelpack_Decline
 * Then on a Vulkan device (77 without one): the prelit and the cluster
 * model rendered from the original and from the round-tripped replacement
 * agree within 1 per channel. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gltf.h"
#include "host_fs.h"
#include "modelpack.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"
#include "vk/rhi_vk.h"
#include "vu1_ref.h"
#include "xxh3.h"

#include "typedef.h"
#include "DisplayList.h"
#include "DisplayP2O.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "Light.h"
#include "Matrix.h"
#include "MicroCode.h"
#include "Packet.h"
#include "Primitive.h"
#include "RegistPacket.h"
#include "eeword.h"

#include "vu_models.h"

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

#include "mesh_env.h"

/* ------------------------------------------------------------- files */

static char s_base[1024];

static const char *pathOf(const char *rel)
{
    static char buf[8][1200];
    static int k;
    char *b = buf[k = (k + 1) & 7];
    snprintf(b, sizeof(buf[0]), "%s/%s", s_base, rel);
    return b;
}

/* mkdir -p of s_base/rel */
static void mkdirs(const char *rel)
{
    char p[1200];
    snprintf(p, sizeof(p), "%s/%s", s_base, rel);
    for (char *c = p + strlen(s_base) + 1; *c; c++) {
        if (*c == '/') {
            *c = 0;
            (void)ico_mkdir(p);
            *c = '/';
        }
    }
    (void)ico_mkdir(p);
}

typedef struct Files {
    char (*p)[1200];
    int n, cap;
} Files;

static int collect(const char *path, const char *name, void *user)
{
    Files *f = user;
    (void)name;
    if (f->n == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 64;
        f->p = realloc(f->p, (size_t)f->cap * sizeof(*f->p));
    }
    snprintf(f->p[f->n++], sizeof(f->p[0]), "%s", path);
    return 0;
}

/* every file under s_base removed (the folders stay; they are empty) */
static void clearFiles(void)
{
    Files f = {0};
    ico_dir_walk(s_base, 16, collect, &f);
    for (int i = 0; i < f.n; i++) {
        ico_remove(f.p[i]);
    }
    free(f.p);
}

static void writeBytes(const char *path, const void *d, size_t n)
{
    FILE *f = ico_fopen(path, "wb");
    if (!f) {
        CHECK(0, "cannot write %s", path);
        return;
    }
    fwrite(d, 1, n, f);
    fclose(f);
}

static char *readAll(const char *path, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)sz + 1);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        sz = 0;
    }
    b[sz] = 0;
    fclose(f);
    *n = (size_t)sz;
    return b;
}

static void writeDoc(const char *relNoExt, const GltfDoc *d)
{
    char why[256] = "";
    int rc = gltf_Write(pathOf(relNoExt), d, why, sizeof(why));
    CHECK(rc == 0, "write %s: %s", relNoExt, why);
}

static const char *hex16(uint64_t h)
{
    static char b[4][17];
    static int k;
    char *o = b[k = (k + 1) & 3];
    snprintf(o, 17, "%016llx", (unsigned long long)h);
    return o;
}

/* relNoExt.gltf + .bin (named baseName.bin inside) as one .glb at relGlb */
static void makeGlb(const char *relNoExt, const char *baseName, const char *relGlb)
{
    char g[1200], bpath[1200], cut[256];
    snprintf(g, sizeof(g), "%s.gltf", pathOf(relNoExt));
    snprintf(bpath, sizeof(bpath), "%s.bin", pathOf(relNoExt));
    size_t tn = 0, bn = 0;
    char *text = readAll(g, &tn), *bin = readAll(bpath, &bn);
    snprintf(cut, sizeof(cut), "\"uri\":\"%s.bin\",", baseName);
    char *at = text ? strstr(text, cut) : NULL;
    CHECK(at && bin, "the written glTF for the GLB");
    if (at && bin) {
        memmove(at, at + strlen(cut), strlen(at + strlen(cut)) + 1);
        size_t jl = strlen(text), jp = (jl + 3) & ~(size_t)3, bp = (bn + 3) & ~(size_t)3;
        size_t total = 12 + 8 + jp + 8 + bp;
        unsigned char *glb = calloc(1, total);
        uint32_t hdr[5] = {0x46546C67u, 2, (uint32_t)total, (uint32_t)jp, 0x4E4F534Au};
        memcpy(glb, hdr, 20);
        memset(glb + 20, ' ', jp);
        memcpy(glb + 20, text, jl);
        uint32_t ch2[2] = {(uint32_t)bp, 0x004E4942u};
        memcpy(glb + 20 + jp, ch2, 8);
        memcpy(glb + 28 + jp, bin, bn);
        writeBytes(pathOf(relGlb), glb, total);
        free(glb);
    }
    free(text);
    free(bin);
    ico_remove(g);
    ico_remove(bpath);
}

/* the dump of hash moved into a replacements folder (rel), as fileName */
static void moveDump(const char *dumpsRel, uint64_t hash, const char *destRel, const char *fileName)
{
    char from[1200], to[1200];
    for (int k = 0; k < 2; k++) {
        const char *ext = k ? "bin" : "gltf";
        snprintf(from, sizeof(from), "%s/%s.%s", pathOf(dumpsRel), hex16(hash), ext);
        if (k == 0 && fileName) {
            snprintf(to, sizeof(to), "%s/%s", pathOf(destRel), fileName);
        } else {
            snprintf(to, sizeof(to), "%s/%s.%s", pathOf(destRel), hex16(hash), ext);
        }
        CHECK(ico_rename_replace(from, to) == 0, "move %s to %s", from, to);
    }
}

static uint32_t fBitsOf(float f)
{
    return fbitsU(f);
}

/* ------------------------------------------------- synthetic streams */

#define ORIG_QW 4096

typedef struct Orig {
    float qw[ORIG_QW][4];
    RdVuBatchDesc b[8];
    RdVuMeshDesc d;
} Orig;

/* nb batches of counts[i] vertices in the qpv layout; a strip starts at
 * each batch's vertex 0 and at the vertices restart[] lists (batch * 100 +
 * vertex) */
static void origMake(Orig *o, uint32_t qpv, uint32_t nb, const uint32_t *counts, float seed,
                     const uint32_t *restart, int nRestart)
{
    memset(o, 0, sizeof(*o));
    uint32_t at = 0;
    const uint32_t stAt = qpv - 2;
    for (uint32_t i = 0; i < nb; i++) {
        const uint32_t tag[4] = {counts[i] | 0x8000u, (1u << 14) | (0x0Cu << 15) | (3u << 28),
                                 0x512, 0};
        memcpy(o->qw[at], tag, 16);
        o->b[i].firstQw = at;
        o->b[i].material = (uint16_t)i;
        o->b[i].group = (uint16_t)i;
        at++;
        for (uint32_t k = 0; k < counts[i]; k++, at += qpv) {
            int start = k == 0;
            for (int r = 0; r < nRestart; r++) {
                start |= restart[r] == i * 100 + k;
            }
            float (*v)[4] = o->qw + at;
            qw4(v[0], (float)(k >> 1) + seed, (float)(k & 1) + 2.0f * (float)i, 2.0f, 1.0f);
            if (qpv >= RD_VU_QW_LIT) {
                qw4(v[1], 0.0f, 0.0f, 1.0f, 1.0f);
            }
            if (qpv == RD_VU_QW_SKIN) {
                qw4(v[2], ubitsF(16), 1.0f, ubitsF(16), 0.0f);
            }
            qw4(v[stAt], 0.125f * (float)k, 0.25f * (float)i, 1.0f, start ? 0.0f : 1.0f);
            /* colours are GS bytes: whole numbers, or the round trip rounds them */
            qw4(v[qpv - 1], (float)(10 + k), (float)(20 + i), 30.0f + floorf(seed), 127.0f);
        }
    }
    o->d.qw = (const float (*)[4])o->qw;
    o->d.qwCount = at;
    o->d.qwPerVertex = qpv;
    o->d.batchCount = nb;
    o->d.batches = o->b;
    o->d.materialCount = nb;
    o->d.debugName = "orig";
}

static uint64_t hashOf(const Orig *o)
{
    return rd_VuMeshDescHash(&o->d, NULL, NULL);
}

/* A triangle-list document: prims[i] from pos/idx arrays the caller owns. */
typedef struct Doc {
    GltfDoc d;
    GltfPrim p[4];
    float ib[64][16];
    int parent[64];
} Doc;

static void docInit(Doc *x, uint32_t prims)
{
    memset(x, 0, sizeof(*x));
    gltf_DocInit(&x->d);
    x->d.prims = x->p;
    x->d.primCount = prims;
}

static void docSkin(Doc *x, uint32_t bones)
{
    for (uint32_t i = 0; i < bones; i++) {
        gltf_Mat4Identity(x->ib[i]);
        x->parent[i] = -1;
    }
    x->d.skin.count = bones;
    x->d.skin.invBind = x->ib;
    x->d.skin.parent = x->parent;
}

/* one triangle at x offset dx (3 vertices) */
static float s_triPos[2][9] = {{0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 0, 0, 2, 0, 0, 0, 2, 0}};

static uint32_t s_quadIdx[2][6] = {{0, 1, 2, 2, 1, 3}, {0, 1, 2, 0, 2, 3}};

static float s_quadPos[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};

/* ------------------------------------------------ the triangle sets */

/* a vertex as a key: its bytes without the strip flag */
static uint64_t vertexKey(const float (*v)[4], uint32_t qpv)
{
    float t[5][4];
    memcpy(t, v, (size_t)qpv * 16);
    t[qpv - 2][3] = 0.0f;
    return xxh3_64(t, (size_t)qpv * 16);
}

typedef struct Tri {
    uint64_t k[3];
} Tri;

static int triCmp(const void *a, const void *b)
{
    const Tri *x = a, *y = b;
    for (int i = 0; i < 3; i++) {
        if (x->k[i] != y->k[i]) {
            return x->k[i] < y->k[i] ? -1 : 1;
        }
    }
    return 0;
}

static void triSort(Tri *t)
{
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            if (t->k[j] < t->k[i]) {
                uint64_t s = t->k[i];
                t->k[i] = t->k[j];
                t->k[j] = s;
            }
        }
    }
}

/* the original's triangles, batch b: vu1ref_StaticKicks over its strip
 * flags */
static int origTris(const RdVuMeshDesc *d, uint32_t b, Tri *out)
{
    static float stw[4096];
    static int first[4096], kicks[4096];
    const uint32_t qpv = d->qwPerVertex, at = d->batches[b].firstQw;
    uint32_t n;
    memcpy(&n, d->qw[at], 4);
    n &= 0x7FFF;
    const float (*v)[4] = d->qw + at + 1;
    for (uint32_t k = 0; k < n; k++) {
        stw[k] = v[k * qpv + qpv - 2][3];
        first[k] = 0;
    }
    int nk = vu1ref_StaticKicks(stw, first, (int)n, kicks);
    for (int i = 0; i < nk; i++) {
        for (int c = 0; c < 3; c++) {
            out[i].k[c] = vertexKey(v + (size_t)(kicks[i] - 2 + c) * qpv, qpv);
        }
        triSort(&out[i]);
    }
    qsort(out, (size_t)nk, sizeof(*out), triCmp);
    return nk;
}

/* the record's triangles of batch b from its index list */
static int recTris(const RdMeshRec *r, uint32_t b, Tri *out)
{
    const RdVuBatchRec *br = &r->batches[b];
    int n = 0;
    for (uint32_t i = 0; i < br->indexCount; i += 3) {
        for (int c = 0; c < 3; c++) {
            const uint32_t idx = r->index[br->firstIndex + i + (uint32_t)c];
            /* ICO_VU_INDEX(K, corner): the corner-th of vertices K-2, K-1, K */
            out[n].k[c] = vertexKey(r->stream + (size_t)(idx / 4 - 2 + idx % 4) * r->qwPerVertex,
                                    r->qwPerVertex);
        }
        triSort(&out[n]);
        n++;
    }
    qsort(out, (size_t)n, sizeof(*out), triCmp);
    return n;
}

/* ------------------------------------------------------------ index */

static Orig s_o[8];

static void indexChecks(void)
{
    printf("index\n");
    clearFiles();
    const uint32_t c6[1] = {6};
    for (int i = 0; i < 8; i++) {
        origMake(&s_o[i], RD_VU_QW_PRELIT, 1, c6, (float)i, NULL, 0);
    }
    uint64_t h[8];
    for (int i = 0; i < 8; i++) {
        h[i] = hashOf(&s_o[i]);
    }
    mkdirs("u/models/SCES-50760/replacements/sub");
    mkdirs("u/models/SCES-50760/dumps");
    mkdirs("u/models/sub");
    mkdirs("p/models/SCES-50760/replacements");
    mkdirs("p/models/replacements");
    Doc x;
    docInit(&x, 1);
    x.p[0].pos = s_triPos[0];
    x.p[0].vertexCount = 3;
    char rel[256];
    /* 0: the user folder's (one triangle) and the program folder's (two) */
    snprintf(rel, sizeof(rel), "u/models/SCES-50760/replacements/%s", hex16(h[0]));
    writeDoc(rel, &x.d);
    {
        Doc y;
        docInit(&y, 1);
        y.p[0].pos = s_quadPos;
        y.p[0].idx = s_quadIdx[0];
        y.p[0].vertexCount = 4;
        y.p[0].indexCount = 6;
        snprintf(rel, sizeof(rel), "p/models/SCES-50760/replacements/%s", hex16(h[0]));
        writeDoc(rel, &y.d);
    }
    /* 1: a name- prefix in a folder below */
    snprintf(rel, sizeof(rel), "u/models/SCES-50760/replacements/sub/hero arm-%s", hex16(h[1]));
    writeDoc(rel, &x.d);
    /* 2: a GLB in the program folder's tolerant layout */
    snprintf(rel, sizeof(rel), "p/models/replacements/tmp2");
    writeDoc(rel, &x.d);
    char glb[256];
    snprintf(glb, sizeof(glb), "p/models/replacements/%s.GLB", hex16(h[2]));
    makeGlb(rel, "tmp2", glb);
    /* 3: directly under models/ */
    snprintf(rel, sizeof(rel), "u/models/%s", hex16(h[3]));
    writeDoc(rel, &x.d);
    /* not walked: below models/ and the dumps */
    snprintf(rel, sizeof(rel), "u/models/sub/%s", hex16(h[4]));
    writeDoc(rel, &x.d);
    snprintf(rel, sizeof(rel), "u/models/SCES-50760/dumps/%s", hex16(h[5]));
    writeDoc(rel, &x.d);
    /* not model names (4), a broken file (1), other files (quiet) */
    writeDoc("u/models/SCES-50760/replacements/model", &x.d);
    writeDoc("u/models/SCES-50760/replacements/0123456789abcdeg", &x.d);
    writeDoc("u/models/SCES-50760/replacements/0123456789ABCDEF", &x.d);
    snprintf(rel, sizeof(rel), "u/models/SCES-50760/replacements/-%s", hex16(h[6]));
    writeDoc(rel, &x.d);
    snprintf(rel, sizeof(rel), "u/models/SCES-50760/replacements/%s.gltf", hex16(h[7]));
    writeBytes(pathOf(rel), "not a model", 11);
    writeBytes(pathOf("u/models/SCES-50760/replacements/readme.txt"), "hello", 5);
    writeBytes(pathOf("u/models/SCES-50760/replacements/preview.png"), "png", 3);

    ModelpackConfig cfg = {pathOf("u"), pathOf("p"), "SCES-50760", 1, 0};
    char ucopy[1200], pcopy[1200];
    snprintf(ucopy, sizeof(ucopy), "%s", pathOf("u"));
    snprintf(pcopy, sizeof(pcopy), "%s", pathOf("p"));
    cfg.userDir = ucopy;
    cfg.programDir = pcopy;
    int n = modelpack_Init(&cfg);
    ModelpackStats st;
    modelpack_GetStats(&st);
    printf("  %d indexed, %u files, %u duplicates, %u bad names, %u failed\n", n, st.files,
           st.duplicates, st.badNames, st.failed);
    CHECK(n == 4 && modelpack_Count() == 4 && st.indexed == 4, "4 replacements (%d)", n);
    CHECK(st.files == 10, "10 model files seen (%u)", st.files);
    CHECK(st.duplicates == 1, "one duplicate (%u)", st.duplicates);
    CHECK(st.badNames == 4, "four names that are not model names (%u)", st.badNames);
    CHECK(st.failed == 1, "one broken file (%u)", st.failed);
    for (int i = 0; i < 4; i++) {
        CHECK(modelpack_Lookup(h[i]) >= 0, "hash %d indexed", i);
    }
    for (int i = 4; i < 8; i++) {
        CHECK(modelpack_Lookup(h[i]) < 0, "hash %d not indexed", i);
    }
    /* the user folder's file won: one triangle */
    RdMesh m = modelpack_Create(modelpack_Lookup(h[0]), &s_o[0].d, "first", 0);
    const RdMeshRec *r = rd__MeshRec(m.id);
    CHECK(r && r->vertexCount == 3 && r->indexCount == 3 && r->replaced,
          "the user folder's file wins (%u vertices)", r ? r->vertexCount : 0);
    rd_DestroyVuMesh(m);
    m = modelpack_Create(modelpack_Lookup(h[2]), &s_o[2].d, NULL, 0);
    CHECK(m.id != 0, "the GLB makes a replacement");
    rd_DestroyVuMesh(m);
    CHECK(modelpack_Create(modelpack_Lookup(h[1]), &s_o[0].d, NULL, 0).id == 0 &&
              modelpack_Lookup(h[1]) >= 0,
          "another part's desc: no mesh, the entry kept");

    /* nothing installed */
    modelpack_Shutdown();
    mkdirs("e");
    ModelpackConfig none = {pathOf("e"), NULL, NULL, 0, 0};
    char ecopy[1200];
    snprintf(ecopy, sizeof(ecopy), "%s", pathOf("e"));
    none.userDir = ecopy;
    CHECK(modelpack_Init(&none) == 0 && modelpack_Count() == 0 && modelpack_Lookup(h[0]) < 0,
          "no pack");
    modelpack_Shutdown();
}

/* ------------------------------------------------------------ strips */

static void initAt(const char *userRel, int dump)
{
    static char u[1200];
    snprintf(u, sizeof(u), "%s", pathOf(userRel));
    ModelpackConfig cfg = {u, NULL, "SCES-50760", 0, dump};
    modelpack_Init(&cfg);
}

static Tri s_ta[4096], s_tb[4096];

static void stripChecks(void)
{
    printf("strips\n");
    clearFiles();
    /* two batches (7 and 9 vertices), restarts at batch 0 vertex 4 and
       batch 1 vertices 3 and 6 */
    static Orig o;
    const uint32_t counts[2] = {7, 9};
    const uint32_t restarts[3] = {4, 103, 106};
    origMake(&o, RD_VU_QW_PRELIT, 2, counts, 0.5f, restarts, 3);
    const uint64_t h = hashOf(&o);
    mkdirs("s/models/SCES-50760/replacements");
    initAt("s", 1);
    CHECK(modelpack_DumpWanted(h, NULL), "dumping on: a new part is wanted");
    ModelpackIdent id = {"synth", 2, 1, NULL};
    CHECK(modelpack_Dump(&o.d, &id, NULL) == 2, "the dump writes two files");
    CHECK(!modelpack_DumpWanted(h, NULL) && modelpack_Dump(&o.d, &id, NULL) == 0, "once per hash");
    CHECK(strstr(modelpack_DumpDir(), "models/SCES-50760/dumps") != NULL, "the dumps folder %s",
          modelpack_DumpDir());
    size_t n = 0;
    char *list = readAll(pathOf("s/models/SCES-50760/dumps/models.txt"), &n);
    char want[200];
    snprintf(want, sizeof(want), "%s\tsynth\t2\t1\tprelit\t16\t2\t0\n", hex16(h));
    CHECK(list && strstr(list, "hash\tmodel\tpart") == list && strstr(list, want), "models.txt: %s",
          list ? list : "(none)");
    free(list);
    /* the file as written */
    GltfDoc d;
    char why[256] = "", rel[300];
    snprintf(rel, sizeof(rel), "s/models/SCES-50760/dumps/%s.gltf", hex16(h));
    int rc = gltf_Read(pathOf(rel), &d, why, sizeof(why));
    CHECK(rc == 0, "the dump reads back: %s", why);
    if (rc == 0) {
        CHECK(d.primCount == 2 && d.prims[0].vertexCount == 7 && d.prims[1].vertexCount == 9,
              "a primitive per batch with its vertices");
        CHECK(d.prims[0].indexCount == 3 * 3 && d.prims[1].indexCount == 3 * 3,
              "the kicks as triangles (%u %u)", d.prims[0].indexCount, d.prims[1].indexCount);
        CHECK(d.meshName && strcmp(d.meshName, "synth/part2/1") == 0, "mesh name %s",
              d.meshName ? d.meshName : "-");
        CHECK(!d.prims[0].nrm && !d.prims[0].joints && d.skin.count == 0, "prelit: no normals");
        CHECK(fabsf(d.prims[0].col[0] - 10.0f / 255.0f) < 1e-7f, "colour / 255");
        snprintf(want, sizeof(want), "\"hash\":\"%s\"", hex16(h));
        CHECK(d.extrasText && strstr(d.extrasText, "\"colorScale\":128") &&
                  strstr(d.extrasText, want) && strstr(d.extrasText, "\"layout\":\"prelit\"") &&
                  strstr(d.extrasText, "\"qwPerVertex\":3") &&
                  strstr(d.extrasText, "{\"vertices\":9,\"prim\":12,\"material\":1}"),
              "extras.ico: %s", d.extrasText ? d.extrasText : "-");
    }
    gltf_Free(&d);
    /* back as a replacement */
    moveDump("s/models/SCES-50760/dumps", h, "s/models/SCES-50760/replacements", NULL);
    initAt("s", 0);
    int e = modelpack_Lookup(h);
    CHECK(e >= 0, "the dump indexed as a replacement");
    RdMesh m = modelpack_Create(e, &o.d, "synth rep", 0);
    const RdMeshRec *r = rd__MeshRec(m.id);
    CHECK(r && r->replaced && r->hash == h && r->batchCount == 2, "the replacement");
    if (r) {
        CHECK(r->vertexCount == 16 && r->indexCount == 18,
              "the strips rebuilt as they were (%u vertices, %u indices)", r->vertexCount,
              r->indexCount);
        for (uint32_t b = 0; b < 2; b++) {
            int na = origTris(&o.d, b, s_ta), nb = recTris(r, b, s_tb);
            CHECK(na == nb && memcmp(s_ta, s_tb, (size_t)na * sizeof(Tri)) == 0,
                  "batch %u: the same triangles (%d and %d)", b, na, nb);
        }
    }
    rd_DestroyVuMesh(m);

    /* a triangle list: a quad of two triangles sharing an edge (one strip of
       four), two triangles where the first must turn (four again); the node
       transform; colours; ST */
    static Orig t;
    const uint32_t c2[2] = {4, 4};
    origMake(&t, RD_VU_QW_PRELIT, 2, c2, 7.0f, NULL, 0);
    const uint64_t ht = hashOf(&t);
    Doc x;
    docInit(&x, 2);
    static float col[12] = {1.0f, 0.5f, 0.0f, 0.2f, 0.2f, 0.2f, 0, 0, 0, 1, 1, 1};
    static float uv[8] = {0.5f, 0.25f, 1, 0, 0, 1, 1, 1};
    for (int i = 0; i < 2; i++) {
        x.p[i].pos = s_quadPos;
        x.p[i].idx = s_quadIdx[i];
        x.p[i].vertexCount = 4;
        x.p[i].indexCount = 6;
    }
    x.p[0].col = col;
    x.p[0].uv = uv;
    x.d.nodeMatrix[12] = 1.0f;
    x.d.nodeMatrix[13] = 2.0f;
    x.d.nodeMatrix[14] = 3.0f;
    snprintf(rel, sizeof(rel), "s/models/SCES-50760/replacements/list-%s", hex16(ht));
    writeDoc(rel, &x.d);
    /* a lit file for a prelit part; a prelit file for a lit part; more
       pieces than batches */
    static Orig lp, ll, l1;
    const uint32_t c3[1] = {3};
    origMake(&lp, RD_VU_QW_PRELIT, 1, c3, 11.0f, NULL, 0);
    origMake(&ll, RD_VU_QW_LIT, 1, c3, 12.0f, NULL, 0);
    origMake(&l1, RD_VU_QW_PRELIT, 1, c3, 13.0f, NULL, 0);
    static float nrm[9] = {0, 0, 2, 0, 0, 2, 0, 0, 2};
    Doc y;
    docInit(&y, 1);
    y.p[0].pos = s_triPos[0];
    y.p[0].nrm = nrm;
    y.p[0].vertexCount = 3;
    snprintf(rel, sizeof(rel), "s/models/SCES-50760/replacements/%s", hex16(hashOf(&lp)));
    writeDoc(rel, &y.d);
    y.p[0].nrm = NULL;
    snprintf(rel, sizeof(rel), "s/models/SCES-50760/replacements/%s", hex16(hashOf(&ll)));
    writeDoc(rel, &y.d);
    docInit(&y, 2);
    y.p[0].pos = y.p[1].pos = s_triPos[0];
    y.p[0].vertexCount = y.p[1].vertexCount = 3;
    snprintf(rel, sizeof(rel), "s/models/SCES-50760/replacements/%s", hex16(hashOf(&l1)));
    writeDoc(rel, &y.d);
    initAt("s", 0);
    m = modelpack_Create(modelpack_Lookup(ht), &t.d, NULL, 0);
    r = rd__MeshRec(m.id);
    CHECK(r != NULL, "the triangle list's replacement");
    if (r) {
        CHECK(r->batches[0].vertexCount == 4 && r->batches[1].vertexCount == 4 &&
                  r->indexCount == 12,
              "two strips of four (%u %u, %u indices)", r->batches[0].vertexCount,
              r->batches[1].vertexCount, r->indexCount);
        const float (*v)[4] = r->stream;
        CHECK(v[0][0] == 1.0f && v[0][1] == 2.0f && v[0][2] == 3.0f && v[0][3] == 1.0f &&
                  v[3 * 3][0] == 2.0f && v[3 * 3][1] == 3.0f,
              "the node's translation (%g %g %g)", (double)v[0][0], (double)v[0][1],
              (double)v[0][2]);
        CHECK(v[1][0] == 0.5f && v[1][1] == 0.25f && v[1][2] == 1.0f && v[1][3] == 0.0f &&
                  v[4][3] == 1.0f && v[7][3] == 1.0f && v[10][3] == 1.0f,
              "ST (u, v, 1, flag): the strip's first vertex 0, the rest 1");
        CHECK(v[2][0] == 255.0f && v[2][1] == 128.0f && v[2][2] == 0.0f && v[2][3] == 127.0f &&
                  v[5][0] == 51.0f,
              "colour round(c * 255), alpha 127 (%g %g %g %g)", (double)v[2][0], (double)v[2][1],
              (double)v[2][2], (double)v[2][3]);
        /* batch 1 without COLOR_0: 128 grey; its first triangle turned: the
           strip starts at vertex 1 (1, 2, 0, 3) */
        const float (*w)[4] = v + 4 * 3;
        CHECK(w[2][0] == 128.0f && w[2][1] == 128.0f && w[2][2] == 128.0f,
              "no COLOR_0: (128, 128, 128)");
        CHECK(w[0][0] == 2.0f && w[0][1] == 2.0f && w[3][0] == 1.0f && w[3][1] == 3.0f &&
                  w[6][0] == 1.0f && w[6][1] == 2.0f && w[9][0] == 2.0f && w[9][1] == 3.0f,
              "the lone triangle turned to join the next");
    }
    rd_DestroyVuMesh(m);
    m = modelpack_Create(modelpack_Lookup(hashOf(&lp)), &lp.d, NULL, 0);
    r = rd__MeshRec(m.id);
    CHECK(r && r->qwPerVertex == RD_VU_QW_PRELIT && r->vertexCount == 3 &&
              r->stream[1][2] == 1.0f && r->stream[2][0] == 128.0f,
          "a lit file serves a prelit part, the normals dropped");
    rd_DestroyVuMesh(m);
    CHECK(modelpack_Create(modelpack_Lookup(hashOf(&ll)), &ll.d, NULL, 0).id == 0 &&
              modelpack_Lookup(hashOf(&ll)) < 0,
          "a prelit file for a lit part: declined");
    CHECK(modelpack_Create(modelpack_Lookup(hashOf(&l1)), &l1.d, NULL, 0).id == 0 &&
              modelpack_Lookup(hashOf(&l1)) < 0,
          "more pieces than batches: declined");
    ModelpackStats st;
    modelpack_GetStats(&st);
    CHECK(st.declined == 2 && st.created == 2, "2 declined, 2 made (%u %u)", st.declined,
          st.created);
    modelpack_Shutdown();
}

/* ------------------------------------------------------------- bones */

static void boneChecks(void)
{
    printf("bones\n");
    clearFiles();
    mkdirs("b/models/SCES-50760/replacements");
    static Orig a, b2, c, nm;
    const uint32_t c6[1] = {6};
    origMake(&a, RD_VU_QW_SKIN, 1, c6, 1.0f, NULL, 0);
    origMake(&b2, RD_VU_QW_SKIN, 1, c6, 2.0f, NULL, 0);
    origMake(&c, RD_VU_QW_SKIN, 1, c6, 3.0f, NULL, 0);
    origMake(&nm, RD_VU_QW_SKIN, 1, c6, 4.0f, NULL, 0);
    static float nrm[9] = {0, 0, 1, 0, 3, 0, 1, 0, 0};
    /* v0: (3, 1, 2, 0) (0.1, 0.4, 0.3, 0.2): bones 1 and 2, 4/7 and 3/7;
       v1: one bone 5; v2: bone 2 twice merged (0.6) over bone 4 (0.4) */
    static uint8_t joints[12] = {3, 1, 2, 0, 5, 0, 0, 0, 2, 2, 4, 0};
    static float weights[12] = {0.1f, 0.4f, 0.3f, 0.2f, 1, 0, 0, 0, 0.3f, 0.3f, 0.4f, 0};
    Doc x;
    docInit(&x, 1);
    x.p[0].pos = s_triPos[0];
    x.p[0].nrm = nrm;
    x.p[0].joints = joints;
    x.p[0].weights = weights;
    x.p[0].vertexCount = 3;
    docSkin(&x, 6);
    x.d.nodeMatrix[12] = 5.0f; /* ignored for a skinned mesh */
    char rel[300];
    snprintf(rel, sizeof(rel), "b/models/SCES-50760/replacements/%s", hex16(hashOf(&a)));
    writeDoc(rel, &x.d);
    snprintf(rel, sizeof(rel), "b/models/SCES-50760/replacements/%s", hex16(hashOf(&b2)));
    writeDoc(rel, &x.d);
    /* joints by name: skin order swapped, bone_01 first; joint 0 is then
       bone 1 */
    static uint8_t j0[12] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    static float w0[12] = {1, 0, 0, 0, 1, 0, 0, 0, 0.75f, 0.25f, 0, 0};
    Doc y;
    docInit(&y, 1);
    y.p[0].pos = s_triPos[1];
    y.p[0].nrm = nrm;
    y.p[0].joints = j0;
    y.p[0].weights = w0;
    y.p[0].vertexCount = 3;
    docSkin(&y, 2);
    snprintf(rel, sizeof(rel), "b/models/SCES-50760/replacements/%s", hex16(hashOf(&nm)));
    writeDoc(rel, &y.d);
    {
        char g[400];
        snprintf(g, sizeof(g), "%s.gltf", pathOf(rel));
        size_t n = 0;
        char *text = readAll(g, &n);
        char *at = text ? strstr(text, "\"joints\":[1,2]") : NULL;
        CHECK(at != NULL, "the skin's joints in the file");
        if (at) {
            memcpy(at, "\"joints\":[2,1]", 14);
            writeBytes(g, text, n);
        }
        free(text);
    }
    /* bone 60: refused */
    static uint8_t j60[12] = {60, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    y.p[0].joints = j60;
    y.p[0].weights = w0;
    docSkin(&y, 61);
    snprintf(rel, sizeof(rel), "b/models/SCES-50760/replacements/%s", hex16(hashOf(&c)));
    writeDoc(rel, &y.d);
    initAt("b", 0);
    ModelpackStats st;
    modelpack_GetStats(&st);
    CHECK(st.indexed == 3 && st.failed == 1, "bone 60 refuses its file (%u indexed, %u failed)",
          st.indexed, st.failed);
    CHECK(modelpack_Lookup(hashOf(&c)) < 0, "no entry for the bone 60 file");
    RdMesh m = modelpack_Create(modelpack_Lookup(hashOf(&a)), &a.d, NULL, 6);
    const RdMeshRec *r = rd__MeshRec(m.id);
    CHECK(r && r->vertexCount == 3 && r->qwPerVertex == RD_VU_QW_SKIN, "the skinned replacement");
    if (r) {
        const float (*v)[4] = r->stream;
        CHECK(fBitsOf(v[2][0]) == 20 && fBitsOf(v[2][2]) == 24 &&
                  fabsf(v[2][1] - 4.0f / 7.0f) < 1e-6f && fabsf(v[2][3] - 3.0f / 7.0f) < 1e-6f,
              "four weights: the two largest renormalised (%u %g %u %g)", fBitsOf(v[2][0]),
              (double)v[2][1], fBitsOf(v[2][2]), (double)v[2][3]);
        CHECK(fBitsOf(v[7][0]) == 36 && v[7][1] == 1.0f && fBitsOf(v[7][2]) == 16 &&
                  v[7][3] == 0.0f,
              "one bone: (5 * 4 + 16, 1, 16, 0)");
        CHECK(fBitsOf(v[12][0]) == 24 && fBitsOf(v[12][2]) == 32 &&
                  fabsf(v[12][1] - 0.6f) < 1e-6f && fabsf(v[12][3] - 0.4f) < 1e-6f,
              "a bone given twice merged");
        CHECK(v[0][0] == 0.0f && v[5][0] == 1.0f && v[6][1] == 1.0f && v[1][2] == 1.0f,
              "skinned: no node transform; normals normalised (%g)", (double)v[6][1]);
        CHECK(v[1][3] == 1.0f && v[6][3] == 1.0f, "normal.w the original's");
    }
    rd_DestroyVuMesh(m);
    CHECK(modelpack_Create(modelpack_Lookup(hashOf(&b2)), &b2.d, NULL, 5).id == 0 &&
              modelpack_Lookup(hashOf(&b2)) < 0,
          "bone 5 on a model of 5 bones: declined");
    m = modelpack_Create(modelpack_Lookup(hashOf(&nm)), &nm.d, NULL, 2);
    r = rd__MeshRec(m.id);
    CHECK(r != NULL, "the joints-by-name replacement");
    if (r) {
        const float (*v)[4] = r->stream;
        CHECK(fBitsOf(v[2][0]) == 20 && v[2][1] == 1.0f && fBitsOf(v[7][0]) == 16 &&
                  fBitsOf(v[12][0]) == 20 && fBitsOf(v[12][2]) == 16 && v[12][1] == 0.75f,
              "joints mapped by their names (%u %u %u)", fBitsOf(v[2][0]), fBitsOf(v[7][0]),
              fBitsOf(v[12][0]));
    }
    rd_DestroyVuMesh(m);
    modelpack_Shutdown();
}

/* ----------------------------------------------------------- cluster */

static Sub15C s_objA, s_objB;
static Model s_modelA, s_modelB;

static PacHeader *packetA(void)
{
    return (PacHeader *)s_modelA.mdl.groups->packets;
}

static PacHeader *packetB(void)
{
    return (PacHeader *)s_modelB.mdl.groups->packets;
}

static void recordObj(Sub15C *o)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    s_clipRet = 1;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    reg_DispObj(o);
    dl_Swap();
}

static void setPacketMesh(PacHeader *pk, uint32_t id)
{
    memcpy(pk->pad9C, &id, sizeof(id));
}

static void buildModels(void)
{
    s_rng = 1234567u;
    buildScene();
    makeModel(&s_modelA, &s_objA, 0, 1);
    makeModel(&s_modelB, &s_objB, 1, 1);
    /* unit normals, as the game's: the dump writes them normalised */
    for (int v = 0; v < NV; v++) {
        float *n = s_modelB.nrm[v];
        const double l = sqrt((double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2]);
        n[0] = (float)(n[0] / l);
        n[1] = (float)(n[1] / l);
        n[2] = (float)(n[2] / l);
    }
    p2o_MakePacket(&s_objA);
    p2o_MakePacket(&s_objB);
}

static PkDesc s_pdA, s_pdB;
static uint64_t s_hA, s_hB;

static void skeletonOf(const Model *m, ModelpackSkeleton *sk)
{
    memset(sk, 0, sizeof(*sk));
    sk->count = 2;
    memcpy(sk->invBind[0], m->clusterMtx[0], 64);
    memcpy(sk->invBind[1], m->clusterMtx[1], 64);
    sk->parent[0] = -1;
    sk->parent[1] = 0;
}

/* the two models dumped (the cluster one through the one-shot object
 * dump), moved to a replacements folder and indexed */
static void roundTrip(void)
{
    packetDesc(packetA(), RD_VU_QW_PRELIT, &s_pdA);
    packetDesc(packetB(), RD_VU_QW_SKIN, &s_pdB);
    s_hA = rd_VuMeshDescHash(&s_pdA.d, NULL, NULL);
    s_hB = rd_VuMeshDescHash(&s_pdB.d, NULL, NULL);
    mkdirs("c/models/SCES-50760/replacements");
    initAt("c", 0);
    CHECK(!modelpack_DumpWanted(s_hB, &s_objB), "dumping off: not wanted");
    CHECK(modelpack_DumpObjectStatus() == 0, "no shot yet");
    modelpack_DumpObjectOnce(&s_objB);
    CHECK(modelpack_DumpObjectStatus() == -1, "the shot armed");
    CHECK(!modelpack_DumpWanted(s_hA, &s_objA), "another object: not wanted");
    CHECK(modelpack_DumpWanted(s_hB, &s_objB), "the armed object's part: wanted");
    ModelpackSkeleton sk;
    skeletonOf(&s_modelB, &sk);
    ModelpackIdent idB = {"test_cluster", 0, 0, &s_objB};
    CHECK(modelpack_Dump(&s_pdB.d, &idB, &sk) == 2, "the shot writes the part");
    CHECK(!modelpack_DumpWanted(s_hB, &s_objB) && modelpack_Dump(&s_pdB.d, &idB, &sk) == 0,
          "once in the shot");
    CHECK(modelpack_DumpObjectStatus() == -1, "the shot's frame still open");
    dl_Clear();
    dl_Swap();
    dl_Clear();
    CHECK(modelpack_DumpObjectStatus() == 2, "the shot wrote 2 files (%d)",
          modelpack_DumpObjectStatus());
    CHECK(!modelpack_DumpWanted(s_hB, &s_objB), "the shot is over");
    dl_Swap();
    /* the prelit one through the dump switch */
    modelpack_SetDumpEnabled(true);
    ModelpackIdent idA = {"test_prelit", 0, 0, NULL};
    CHECK(modelpack_DumpWanted(s_hA, NULL) && modelpack_Dump(&s_pdA.d, &idA, NULL) == 2,
          "the prelit part dumped");
    ModelpackStats st;
    modelpack_GetStats(&st);
    CHECK(st.dumped == 2, "two parts dumped (%u)", st.dumped);
    /* the skinned file: the skin as given */
    GltfDoc d;
    char why[256] = "", rel[300];
    snprintf(rel, sizeof(rel), "c/models/SCES-50760/dumps/%s.gltf", hex16(s_hB));
    int rc = gltf_Read(pathOf(rel), &d, why, sizeof(why));
    CHECK(rc == 0, "the cluster dump reads: %s", why);
    if (rc == 0) {
        CHECK(d.skin.count == 2 && d.skin.parent[0] < 0 && d.skin.parent[1] == 0 &&
                  memcmp(d.skin.invBind[1], s_modelB.clusterMtx[1], 64) == 0 &&
                  strcmp(d.skin.names[1], "bone_01") == 0,
              "skin: bone_NN joints, parents, inverse binds verbatim");
        int ok = d.prims[0].joints != NULL;
        for (uint32_t v = 0; ok && v < d.prims[0].vertexCount; v++) {
            const uint8_t *j = d.prims[0].joints + v * 4;
            const float *w = d.prims[0].weights + v * 4;
            ok = j[0] != j[1] && j[2] == 0 && j[3] == 0 && fabsf(w[0] + w[1] - 1.0f) < 1e-6f &&
                 w[2] == 0.0f && w[3] == 0.0f;
        }
        CHECK(ok, "two bones a vertex, weights summing to 1");
        CHECK(strstr(d.extrasText, "\"layout\":\"skinned\"") &&
                  strstr(d.extrasText, "\"bones\":2") && strstr(d.extrasText, "\"normalW\":1"),
              "extras.ico of the cluster part: %s", d.extrasText);
    }
    gltf_Free(&d);
    moveDump("c/models/SCES-50760/dumps", s_hA, "c/models/SCES-50760/replacements", NULL);
    char named[64];
    snprintf(named, sizeof(named), "test_cluster-%s.gltf", hex16(s_hB));
    moveDump("c/models/SCES-50760/dumps", s_hB, "c/models/SCES-50760/replacements", named);
    initAt("c", 0);
    CHECK(modelpack_Count() == 2 && modelpack_Lookup(s_hA) >= 0 && modelpack_Lookup(s_hB) >= 0,
          "both round-tripped parts indexed");
}

static void clusterChecks(void)
{
    printf("cluster\n");
    clearFiles();
    roundTrip();
    PacHeader *pk = packetB();
    /* the original's draw (made here: with the pack indexed, the packet's
       own build would make the replacement, Packet.c pac_hostBuild) */
    RdMesh orig = rd_CreateVuMesh(&s_pdB.d);
    setPacketMesh(pk, orig.id);
    recordObj(&s_objB);
    const RdMeshRec *orc = rd__MeshRec(orig.id);
    Found fd;
    walkFrame(rd__LastFrame(), &fd);
    CHECK(orc && fd.n == 1 && fd.cmd[0]->type == RDC_SKINNED, "the original drawn");
    if (!orc || fd.n != 1) {
        return;
    }
    const float *mem;
    RdVuPayload po = payloadOf(rd__LastFrame(), fd.cmd[0], &mem);
    static float bonesO[240][4];
    memcpy(bonesO, mem + 36 * 4, sizeof(bonesO));
    /* the replacement in its place */
    RdMesh m = modelpack_Create(modelpack_Lookup(s_hB), &s_pdB.d, "test_cluster rep",
                                (uint32_t)s_objB.nodeNum);
    const RdMeshRec *r = rd__MeshRec(m.id);
    CHECK(r && r->replaced && r->hash == orc->hash, "the cluster replacement");
    if (!r) {
        return;
    }
    CHECK(r->vertexCount == orc->vertexCount && r->batchCount == orc->batchCount &&
              r->indexCount == orc->indexCount &&
              memcmp(r->index, orc->index, (size_t)r->indexCount * 4) == 0,
          "the original's counts and kicks (%u %u %u vs %u %u %u)", r->vertexCount, r->batchCount,
          r->indexCount, orc->vertexCount, orc->batchCount, orc->indexCount);
    /* the stream: positions, ST, colours exact; normals and weights within
       rounding (the bone pair may come in either order) */
    int exact = 1, close = 1;
    for (uint32_t v = 0; v < r->vertexCount && v < orc->vertexCount; v++) {
        const float (*a)[4] = orc->stream + (size_t)v * 5;
        const float (*b)[4] = r->stream + (size_t)v * 5;
        exact &= memcmp(a[0], b[0], 16) == 0 && memcmp(a[3], b[3], 16) == 0 &&
                 memcmp(a[4], b[4], 16) == 0 && a[1][3] == b[1][3];
        for (int i = 0; i < 3; i++) {
            close &= fabsf(a[1][i] - b[1][i]) < 1e-6f;
        }
        const int sw = fBitsOf(a[2][0]) != fBitsOf(b[2][0]);
        close &= fBitsOf(a[2][0]) == fBitsOf(b[2][sw ? 2 : 0]) &&
                 fBitsOf(a[2][2]) == fBitsOf(b[2][sw ? 0 : 2]) &&
                 fabsf(a[2][1] - b[2][sw ? 3 : 1]) < 1e-6f &&
                 fabsf(a[2][3] - b[2][sw ? 1 : 3]) < 1e-6f;
    }
    CHECK(exact, "positions, ST, colours and normal.w as the original's");
    CHECK(close, "normals and bone weights within rounding");
    setPacketMesh(pk, m.id);
    recordObj(&s_objB);
    walkFrame(rd__LastFrame(), &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->type == RDC_SKINNED && fd.cmd[0]->u[0] == m.id,
          "one RDC_SKINNED of the replacement");
    if (fd.n == 1) {
        RdVuPayload p = payloadOf(rd__LastFrame(), fd.cmd[0], &mem);
        CHECK(p.prog == po.prog && p.code == 20 && p.boneQw == 240 && p.boneQw == po.boneQw &&
                  p.firstBatch == 0 && p.batchCount == orc->batchCount &&
                  p.qwPerVertex == RD_VU_QW_SKIN,
              "cluster code 20 over all batches, 240 bone quadwords (%u %u)", p.code, p.boneQw);
        CHECK(memcmp(mem + 36 * 4, bonesO, sizeof(bonesO)) == 0, "the same bones");
    }
    /* the prelit one in the same way */
    RdMesh ma = modelpack_Create(modelpack_Lookup(s_hA), &s_pdA.d, NULL, 0);
    RdMesh oa = rd_CreateVuMesh(&s_pdA.d);
    const RdMeshRec *ra = rd__MeshRec(ma.id), *rao = rd__MeshRec(oa.id);
    CHECK(ra && rao && ra->vertexCount == rao->vertexCount && ra->indexCount == rao->indexCount &&
              memcmp(ra->stream, rao->stream, (size_t)ra->vertexCount * 3 * 16) == 0,
          "the prelit part round-trips bit for bit");

    /* the switch: off retires the replacements, on the originals */
    modelpack_SetEnabled(false);
    CHECK(!modelpack_Enabled() && !rd_VuMeshValid(m) && !rd_VuMeshValid(ma) && rd_VuMeshValid(oa) &&
              modelpack_Lookup(s_hB) < 0,
          "off: the replacements retired, lookups refused");
    modelpack_SetEnabled(true);
    CHECK(modelpack_Enabled() && !rd_VuMeshValid(oa) && modelpack_Lookup(s_hB) >= 0,
          "on: the originals retired, lookups again");
    modelpack_Decline(s_hB);
    CHECK(modelpack_Lookup(s_hB) < 0 && modelpack_Lookup(s_hA) >= 0, "a declined part");
    setPacketMesh(pk, 0);
    setPacketMesh(packetA(), 0);
}

/* ------------------------------------------------------------- device */

static uint8_t s_imgA[512 * 512 * 4], s_imgB[512 * 512 * 4];

static void compareImages(const char *what, int minCover)
{
    int bad = 0, maxd = 0, cover = 0;
    for (int i = 0; i < 512 * 512; i++) {
        int d = 0;
        for (int c = 0; c < 4; c++) {
            int e = abs((int)s_imgA[i * 4 + c] - (int)s_imgB[i * 4 + c]);
            d = e > d ? e : d;
        }
        maxd = d > maxd ? d : maxd;
        bad += d > 1;
        cover += s_imgA[i * 4] != 40 || s_imgA[i * 4 + 1] != 40 || s_imgA[i * 4 + 2] != 60;
    }
    printf("  %s: %d pixels drawn, max difference %d, %d over 1\n", what, cover, maxd, bad);
    CHECK(bad == 0, "%s: %d pixels differ by more than 1 (max %d)", what, bad, maxd);
    CHECK(cover >= minCover, "%s: only %d pixels drawn", what, cover);
}

static void deviceCase(const char *what, Sub15C *o, PacHeader *pk, const PkDesc *pd, uint64_t h)
{
    /* the original made here (the packet's own build would make the
       replacement) */
    setPacketMesh(pk, rd_CreateVuMesh(&pd->d).id);
    recordObj(o);
    if (!readScene(s_imgA)) {
        CHECK(0, "%s: readback", what);
        return;
    }
    RdMesh m = modelpack_Create(modelpack_Lookup(h), &pd->d, NULL, (uint32_t)o->nodeNum);
    CHECK(m.id != 0, "%s: the replacement", what);
    setPacketMesh(pk, m.id);
    recordObj(o);
    if (!readScene(s_imgB)) {
        CHECK(0, "%s: readback", what);
        return;
    }
    compareImages(what, 2000);
    setPacketMesh(pk, 0);
}

int main(int argc, char **argv)
{
    snprintf(s_base, sizeof(s_base), "%s/modelpack_tmp", argc > 1 ? argv[1] : ".");
    (void)ico_mkdir(s_base);
    if (!rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    gif_HostSetTex0Resolver(texResolve);
    makeTexture();
    buildModels();
    indexChecks();
    stripChecks();
    boneChecks();
    clusterChecks();
    rd_Shutdown();
    if (failures) {
        printf("modelpack_test: %d failures\n", failures);
        return 1;
    }
    printf("modelpack_test: CPU checks ok\n");

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("modelpack_test: SKIP the pixel checks: no usable Vulkan device\n");
        modelpack_Shutdown();
        clearFiles();
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    gif_HostSetTex0Resolver(texResolve);
    makeTexture();
    buildScene();
    modelpack_Decline(0); /* nothing: hash 0 has no entry */
    initAt("c", 0);       /* the cluster entry again (the switch case declined it) */
    deviceCase("prelit", &s_objA, packetA(), &s_pdA, s_hA);
    deviceCase("cluster", &s_objB, packetB(), &s_pdB, s_hB);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
    modelpack_Shutdown();
    clearFiles();
    if (failures) {
        printf("modelpack_test: %d failures\n", failures);
        return 1;
    }
    printf("modelpack_test: ok\n");
    return 0;
}
