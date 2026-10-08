/* gltf_test.c: port/render/gltf.h (v0.5.0, M2), CPU only.
 *
 * Write -> read bit-exact on every float for a 2-primitive static mesh and
 * a 3-bone skinned mesh; the written JSON's accessors, views, nodes and
 * skin; the same skinned file as a hand-assembled GLB; a "Blender-style"
 * file (interleaved POSITION/NORMAL, normalized ushort COLOR_0 VEC4,
 * ushort JOINTS_0, normalized ubyte WEIGHTS_0 with 4 weights, ubyte and
 * ushort indices, joints listed out of bone order, a TRS mesh node under a
 * translated parent); joints by skin order without bone_NN names; every
 * rejection and its message; a truncated .bin.  Fixtures are written under
 * argv[1] (the build tree), never committed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gltf.h"
#include "host_fs.h"
#include "json.h"

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

static char dir[1024];

static const char *tmp(const char *name)
{
    static char buf[4][1200];
    static int k;
    k = (k + 1) & 3;
    snprintf(buf[k], sizeof(buf[k]), "%s/%s", dir, name);
    return buf[k];
}

static void write_bytes(const char *path, const void *d, size_t n)
{
    FILE *f = ico_fopen(path, "wb");
    if (!f || fwrite(d, 1, n, f) != n) {
        printf("FAIL cannot write %s\n", path);
        failures++;
    }
    if (f)
        fclose(f);
}

static char *read_all(const char *path, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *p = malloc((size_t)len + 1);
    if (p && fread(p, 1, (size_t)len, f) != (size_t)len) {
        free(p);
        p = NULL;
    }
    fclose(f);
    if (p) {
        p[len] = '\0';
        *n = (size_t)len;
    }
    return p;
}

static int same_floats(const float *a, const float *b, size_t n)
{
    if (!a || !b)
        return a == b;
    return memcmp(a, b, n * sizeof(float)) == 0;
}

/* every field of two documents, floats bit for bit */
static void check_equal(const char *what, const GltfDoc *a, const GltfDoc *b)
{
    CHECK(a->primCount == b->primCount, "%s: %u vs %u primitives", what, a->primCount,
          b->primCount);
    if (a->primCount != b->primCount)
        return;
    CHECK((!a->meshName && !b->meshName) ||
              (a->meshName && b->meshName && !strcmp(a->meshName, b->meshName)),
          "%s: mesh name", what);
    CHECK(memcmp(a->nodeMatrix, b->nodeMatrix, sizeof(a->nodeMatrix)) == 0, "%s: node matrix",
          what);
    CHECK((!a->extrasText && !b->extrasText) ||
              (a->extrasText && b->extrasText && !strcmp(a->extrasText, b->extrasText)),
          "%s: extras %s vs %s", what, a->extrasText ? a->extrasText : "NULL",
          b->extrasText ? b->extrasText : "NULL");
    for (uint32_t i = 0; i < a->primCount; i++) {
        const GltfPrim *p = &a->prims[i], *q = &b->prims[i];
        uint32_t n = p->vertexCount;
        CHECK(n == q->vertexCount && p->indexCount == q->indexCount, "%s: prim %u counts", what, i);
        if (n != q->vertexCount || p->indexCount != q->indexCount)
            continue;
        CHECK(same_floats(p->pos, q->pos, n * 3), "%s: prim %u POSITION", what, i);
        CHECK(same_floats(p->nrm, q->nrm, n * 3), "%s: prim %u NORMAL", what, i);
        CHECK(same_floats(p->uv, q->uv, n * 2), "%s: prim %u TEXCOORD_0", what, i);
        CHECK(same_floats(p->col, q->col, n * 3), "%s: prim %u COLOR_0", what, i);
        CHECK(same_floats(p->weights, q->weights, n * 4), "%s: prim %u WEIGHTS_0", what, i);
        CHECK((!p->joints && !q->joints) ||
                  (p->joints && q->joints && !memcmp(p->joints, q->joints, n * 4)),
              "%s: prim %u JOINTS_0", what, i);
        CHECK((!p->idx && !q->idx) ||
                  (p->idx && q->idx && !memcmp(p->idx, q->idx, p->indexCount * 4)),
              "%s: prim %u indices", what, i);
        CHECK((!p->name && !q->name) || (p->name && q->name && !strcmp(p->name, q->name)),
              "%s: prim %u name", what, i);
    }
    CHECK(a->skin.count == b->skin.count, "%s: %u vs %u bones", what, a->skin.count, b->skin.count);
    if (a->skin.count != b->skin.count)
        return;
    for (uint32_t k = 0; k < a->skin.count; k++) {
        CHECK(!memcmp(a->skin.invBind[k], b->skin.invBind[k], 64), "%s: bone %u invBind", what, k);
        CHECK(a->skin.parent[k] == b->skin.parent[k], "%s: bone %u parent %d vs %d", what, k,
              a->skin.parent[k], b->skin.parent[k]);
    }
}

/* a float pattern that %.9g must carry exactly */
static float odd(unsigned i)
{
    static const float v[] = {0.1f,     -0.0f,       1.0f / 3.0f, 2.0e38f,     1e-40f, -1e-30f,
                              123.456f, 16777217.0f, -2.5f,       0.70710678f, 1e-7f,  -65504.0f};
    return v[i % (sizeof(v) / sizeof(v[0]))] * (1.0f + (float)((i / 12) % 4) * 0.125f);
}

static void fill(float *f, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++)
        f[i] = odd((unsigned)(seed + i * 7));
}

static const char *const EXTRAS =
    "{\"ico\":{\"hash\":\"0123456789abcdef\",\"model\":\"ico\",\"part\":2,\"ordinal\":0,"
    "\"normalW\":-1.5,\"batches\":[{\"vertices\":4,\"prim\":12345,\"material\":-1}]}}";

/* --- the static mesh ------------------------------------------------------- */

static void test_static(void)
{
    float pos0[4 * 3], nrm0[4 * 3], uv0[4 * 2], col0[4 * 3], pos1[3 * 3], uv1[3 * 2];
    uint32_t idx0[6] = {0, 1, 2, 2, 1, 3};
    fill(pos0, 12, 0);
    fill(nrm0, 12, 3);
    fill(uv0, 8, 5);
    fill(col0, 12, 1);
    fill(pos1, 9, 11);
    fill(uv1, 6, 2);
    GltfPrim prims[2] = {
        {.pos = pos0,
         .nrm = nrm0,
         .uv = uv0,
         .col = col0,
         .idx = idx0,
         .vertexCount = 4,
         .indexCount = 6},
        {.pos = pos1, .uv = uv1, .vertexCount = 3, .name = "second \"batch\""},
    };
    GltfDoc doc;
    gltf_DocInit(&doc);
    doc.prims = prims;
    doc.primCount = 2;
    doc.meshName = "ico/part2/0";
    doc.extrasText = (char *)EXTRAS;
    fill(doc.nodeMatrix, 16, 4);
    char why[256] = "";
    int rc = gltf_Write(tmp("static"), &doc, why, sizeof(why));
    CHECK(rc == 0, "static write: %s", why);
    GltfDoc back;
    rc = gltf_Read(tmp("static.gltf"), &back, why, sizeof(why));
    CHECK(rc == 0, "static read: %s", why);
    if (rc == 0)
        check_equal("static", &doc, &back);
    CHECK(back.skin.count == 0, "static: no skin");
    gltf_Free(&back);

    /* the JSON itself */
    size_t n;
    char *text = read_all(tmp("static.gltf"), &n);
    IcoJson j;
    CHECK(text && ico_json_parse(text, n, &j) == 0, "static JSON parses");
    if (text) {
        const IcoJsonNode *r = j.root;
        CHECK(ico_json_count(ico_json_get(r, "accessors")) == 7, "static: 7 accessors, %zu",
              ico_json_count(ico_json_get(r, "accessors")));
        CHECK(ico_json_count(ico_json_get(r, "bufferViews")) == 5, "static: 5 views");
        CHECK(ico_json_count(ico_json_get(r, "nodes")) == 1, "static: 1 node");
        CHECK(!ico_json_get(r, "skins"), "static: no skins");
        const char *gen = ico_json_str(ico_json_get(ico_json_get(r, "asset"), "generator"));
        CHECK(gen && !strcmp(gen, "ico-pc"), "generator");
        const char *uri =
            ico_json_str(ico_json_get(ico_json_at(ico_json_get(r, "buffers"), 0), "uri"));
        CHECK(uri && !strcmp(uri, "static.bin"), "uri %s", uri ? uri : "NULL");
        const IcoJsonNode *a0 = ico_json_at(ico_json_get(r, "accessors"), 0);
        CHECK(ico_json_count(ico_json_get(a0, "min")) == 3 &&
                  ico_json_count(ico_json_get(a0, "max")) == 3,
              "POSITION min/max");
        double mx;
        ico_json_double(ico_json_at(ico_json_get(a0, "max"), 0), &mx);
        float want = pos0[0];
        for (int v = 1; v < 4; v++)
            want = pos0[v * 3] > want ? pos0[v * 3] : want;
        CHECK((float)mx == want, "POSITION max x %g vs %g", mx, (double)want);
        const IcoJsonNode *p0 =
            ico_json_at(ico_json_get(ico_json_at(ico_json_get(r, "meshes"), 0), "primitives"), 0);
        uint64_t mode = 0;
        ico_json_u64(ico_json_get(p0, "mode"), &mode);
        CHECK(mode == 4, "mode 4");
        CHECK(ico_json_count(ico_json_get(p0, "attributes")) == 4, "prim 0 has 4 attributes");
        ico_json_free(&j);
    }
    free(text);

    /* a .bin four bytes short is refused */
    rc = gltf_Write(tmp("trunc"), &doc, why, sizeof(why));
    CHECK(rc == 0, "trunc write: %s", why);
    char *bin = read_all(tmp("trunc.bin"), &n);
    if (bin) {
        write_bytes(tmp("trunc.bin"), bin, n - 4);
        free(bin);
    }
    rc = gltf_Read(tmp("trunc.gltf"), &back, why, sizeof(why));
    CHECK(rc != 0 && strstr(why, "truncated"), "truncated .bin: %s", why);
    CHECK(back.primCount == 0 && !back.prims, "failed read leaves the doc empty");
    gltf_Free(&back);

    /* writer refusals */
    GltfPrim bad = {.pos = pos1, .vertexCount = 2};
    doc.prims = &bad;
    doc.primCount = 1;
    CHECK(gltf_Write(tmp("bad"), &doc, why, sizeof(why)) != 0 && strstr(why, "multiple of 3"),
          "write non-multiple: %s", why);
    float nanpos[9] = {0, 0, 0, 1, 0, 0, 0, NAN, 0};
    GltfPrim badn = {.pos = nanpos, .vertexCount = 3};
    doc.prims = &badn;
    CHECK(gltf_Write(tmp("bad"), &doc, why, sizeof(why)) != 0 && strstr(why, "not finite"),
          "write NaN: %s", why);
    doc.prims = prims;
    doc.primCount = 2;
    doc.extrasText = (char *)"[1]";
    CHECK(gltf_Write(tmp("bad"), &doc, why, sizeof(why)) != 0 && strstr(why, "JSON object"),
          "write bad extras: %s", why);
}

/* --- the skinned mesh ------------------------------------------------------ */

static void trs(float m[16], float tx, float ty, float tz, float angle)
{
    gltf_Mat4Identity(m);
    m[0] = cosf(angle);
    m[1] = sinf(angle);
    m[4] = -sinf(angle);
    m[5] = cosf(angle);
    m[12] = tx;
    m[13] = ty;
    m[14] = tz;
}

static GltfDoc skinned_doc(float (*ib)[16], int *parent, GltfPrim *prim)
{
    /* bone locals, worlds = parent's world * local, inverse bind = inverse(world) */
    float local[3][16], world[3][16];
    trs(local[0], 0.5f, 1.0f, -2.0f, 0.3f);
    trs(local[1], 0.0f, 3.25f, 0.0f, -0.7f);
    trs(local[2], 1.0f, 0.0f, 0.1f, 1.1f);
    parent[0] = -1;
    parent[1] = 0;
    parent[2] = 1;
    memcpy(world[0], local[0], 64);
    gltf_Mat4Mul(world[1], world[0], local[1]);
    gltf_Mat4Mul(world[2], world[1], local[2]);
    for (int b = 0; b < 3; b++)
        CHECK(gltf_Mat4Invert(ib[b], world[b]) == 0, "invert bone %d", b);
    static float pos[6 * 3], nrm[6 * 3], uv[6 * 2], col[6 * 3], w[6 * 4];
    static uint8_t joints[6 * 4];
    static uint32_t idx[6] = {0, 1, 2, 3, 4, 5};
    fill(pos, 18, 9);
    fill(nrm, 18, 1);
    fill(uv, 12, 7);
    fill(col, 18, 8);
    for (int v = 0; v < 6; v++) {
        joints[v * 4 + 0] = (uint8_t)(v % 3);
        joints[v * 4 + 1] = (uint8_t)((v + 1) % 3);
        joints[v * 4 + 2] = 0;
        joints[v * 4 + 3] = 0;
        w[v * 4 + 0] = 0.75f - 0.01f * (float)v;
        w[v * 4 + 1] = 0.25f + 0.01f * (float)v;
        w[v * 4 + 2] = 0.0f;
        w[v * 4 + 3] = 0.0f;
    }
    *prim = (GltfPrim){.pos = pos,
                       .nrm = nrm,
                       .uv = uv,
                       .col = col,
                       .joints = joints,
                       .weights = w,
                       .idx = idx,
                       .vertexCount = 6,
                       .indexCount = 6};
    GltfDoc doc;
    gltf_DocInit(&doc);
    doc.prims = prim;
    doc.primCount = 1;
    doc.meshName = "boy/part0/1";
    doc.skin.count = 3;
    doc.skin.invBind = ib;
    doc.skin.parent = parent;
    doc.extrasText = (char *)"{\"ico\":{\"bones\":3}}";
    return doc;
}

static void test_skinned(void)
{
    float ib[3][16];
    int parent[3];
    GltfPrim prim;
    GltfDoc doc = skinned_doc(ib, parent, &prim);
    char why[256] = "";
    int rc = gltf_Write(tmp("skinned"), &doc, why, sizeof(why));
    CHECK(rc == 0, "skinned write: %s", why);
    GltfDoc back;
    rc = gltf_Read(tmp("skinned.gltf"), &back, why, sizeof(why));
    CHECK(rc == 0, "skinned read: %s", why);
    if (rc == 0) {
        check_equal("skinned", &doc, &back);
        for (int b = 0; b < 3 && back.skin.count == 3; b++) {
            char want[16];
            snprintf(want, sizeof(want), "bone_%02d", b);
            CHECK(!strcmp(back.skin.names[b], want), "bone %d name %s", b, back.skin.names[b]);
        }
    }
    gltf_Free(&back);

    size_t n;
    char *text = read_all(tmp("skinned.gltf"), &n);
    IcoJson j;
    CHECK(text && ico_json_parse(text, n, &j) == 0, "skinned JSON parses");
    if (text) {
        const IcoJsonNode *r = j.root;
        CHECK(ico_json_count(ico_json_get(r, "accessors")) == 8, "skinned: 8 accessors, %zu",
              ico_json_count(ico_json_get(r, "accessors")));
        CHECK(ico_json_count(ico_json_get(r, "bufferViews")) == 8, "skinned: 8 views");
        CHECK(ico_json_count(ico_json_get(r, "nodes")) == 4, "skinned: 4 nodes");
        const IcoJsonNode *sk = ico_json_at(ico_json_get(r, "skins"), 0);
        CHECK(ico_json_count(ico_json_get(sk, "joints")) == 3, "skin joints");
        CHECK(ico_json_count(ico_json_get(ico_json_at(ico_json_get(r, "scenes"), 0), "nodes")) == 2,
              "scene roots: the mesh node and bone_00");
        /* bone_01's node matrix is its bind-pose local transform */
        const IcoJsonNode *n2 = ico_json_at(ico_json_get(r, "nodes"), 2);
        const char *nm = ico_json_str(ico_json_get(n2, "name"));
        CHECK(nm && !strcmp(nm, "bone_01"), "node 2 is bone_01");
        float want[16];
        trs(want, 0.0f, 3.25f, 0.0f, -0.7f);
        const IcoJsonNode *m = ico_json_get(n2, "matrix");
        for (int k = 0; k < 16; k++) {
            double d = 0;
            ico_json_double(ico_json_at(m, k), &d);
            CHECK(fabs(d - want[k]) < 1e-5, "bone_01 matrix[%d] %g vs %g", k, d, (double)want[k]);
        }
        const IcoJsonNode *ch = ico_json_get(ico_json_at(ico_json_get(r, "nodes"), 1), "children");
        uint64_t c0 = 0;
        ico_json_u64(ico_json_at(ch, 0), &c0);
        CHECK(ico_json_count(ch) == 1 && c0 == 2, "bone_00's child is node 2");
        ico_json_free(&j);
    }

    /* the same file as a GLB: buffer 0 without uri, the bin as BIN chunk */
    size_t bn = 0;
    char *bin = read_all(tmp("skinned.bin"), &bn);
    if (text && bin) {
        const char *cut = "\"uri\":\"skinned.bin\",";
        char *at = strstr(text, cut);
        CHECK(at != NULL, "uri in the JSON");
        if (at)
            memmove(at, at + strlen(cut), strlen(at + strlen(cut)) + 1);
        size_t jl = strlen(text), jp = (jl + 3) & ~(size_t)3, bp = (bn + 3) & ~(size_t)3;
        size_t total = 12 + 8 + jp + 8 + bp;
        unsigned char *glb = calloc(1, total);
        uint32_t hdr[5] = {0x46546C67u, 2, (uint32_t)total, (uint32_t)jp, 0x4E4F534Au};
        memcpy(glb, hdr, 20); /* little-endian host, as the test machines are */
        memset(glb + 20, ' ', jp);
        memcpy(glb + 20, text, jl);
        uint32_t ch2[2] = {(uint32_t)bp, 0x004E4942u};
        memcpy(glb + 20 + jp, ch2, 8);
        memcpy(glb + 28 + jp, bin, bn);
        write_bytes(tmp("skinned_glb.glb"), glb, total);
        GltfDoc a, b;
        int ra = gltf_Read(tmp("skinned.gltf"), &a, why, sizeof(why));
        CHECK(ra == 0, "gltf read: %s", why);
        int rb = gltf_Read(tmp("skinned_glb.glb"), &b, why, sizeof(why));
        CHECK(rb == 0, "glb read: %s", why);
        if (ra == 0 && rb == 0)
            check_equal("glb", &a, &b);
        gltf_Free(&a);
        gltf_Free(&b);
        /* a GLB whose header claims more than the file has */
        uint32_t big = (uint32_t)total + 16;
        memcpy(glb + 8, &big, 4);
        write_bytes(tmp("short.glb"), glb, total);
        CHECK(gltf_Read(tmp("short.glb"), &b, why, sizeof(why)) != 0 && strstr(why, "truncated"),
              "short GLB: %s", why);
        gltf_Free(&b);
        /* the BIN chunk cut short */
        memcpy(glb + 8, &total, 4);
        write_bytes(tmp("shortbin.glb"), glb, total - bp + 8);
        uint32_t len2 = (uint32_t)(total - bp + 8);
        FILE *f = ico_fopen(tmp("shortbin.glb"), "r+b");
        if (f) {
            fseek(f, 8, SEEK_SET);
            fwrite(&len2, 4, 1, f);
            fclose(f);
        }
        CHECK(gltf_Read(tmp("shortbin.glb"), &b, why, sizeof(why)) != 0 && strstr(why, "truncated"),
              "short BIN chunk: %s", why);
        gltf_Free(&b);
        free(glb);
    }
    free(text);
    free(bin);
}

/* --- a Blender-style file -------------------------------------------------- */

static unsigned char bbin[432];

static void bf(size_t off, float f)
{
    memcpy(bbin + off, &f, 4);
}

static void b16(size_t off, unsigned v)
{
    bbin[off] = (unsigned char)v;
    bbin[off + 1] = (unsigned char)(v >> 8);
}

static const char *const BLENDER_JSON =
    "{\"asset\":{\"generator\":\"Khronos glTF Blender I/O v4.2\",\"version\":\"2.0\"},"
    "\"extensionsUsed\":[\"KHR_materials_specular\"],"
    "\"scene\":0,\"scenes\":[{\"name\":\"Scene\",\"nodes\":[0]}],"
    "\"nodes\":["
    "{\"name\":\"Armature\",\"translation\":[1,2,3],\"children\":[2,1]},"
    "{\"name\":\"Mesh\",\"mesh\":0,\"skin\":0,"
    "\"rotation\":[0,0,0.7071067811865476,0.7071067811865476],\"scale\":[2,2,2]},"
    "{\"name\":\"bone_00\",\"children\":[3],\"translation\":[0,1,0]},"
    "{\"name\":\"bone_01\",\"children\":[4]},"
    "{\"name\":\"bone_02\"}],"
    "\"skins\":[{\"name\":\"Armature\",\"joints\":[4,2,3],\"inverseBindMatrices\":8}],"
    "\"materials\":[{\"name\":\"m\"}],"
    "\"meshes\":[{\"name\":\"Mesh.001\",\"primitives\":["
    "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"COLOR_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":4,"
    "\"TANGENT\":1,\"TEXCOORD_1\":7},\"indices\":5,\"material\":0},"
    "{\"attributes\":{\"POSITION\":6,\"TEXCOORD_0\":7},\"indices\":9,\"mode\":4}]}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,1]},"
    "{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
    "{\"bufferView\":1,\"componentType\":5123,\"normalized\":true,\"count\":4,\"type\":\"VEC4\"},"
    "{\"bufferView\":2,\"componentType\":5123,\"count\":4,\"type\":\"VEC4\"},"
    "{\"bufferView\":3,\"componentType\":5121,\"normalized\":true,\"count\":4,\"type\":\"VEC4\"},"
    "{\"bufferView\":4,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
    "{\"bufferView\":6,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":7,\"componentType\":5123,\"normalized\":true,\"count\":3,\"type\":\"VEC2\"},"
    "{\"bufferView\":5,\"componentType\":5126,\"count\":3,\"type\":\"MAT4\"},"
    "{\"bufferView\":8,\"componentType\":5121,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":["
    "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":96,\"byteStride\":24,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":128,\"byteLength\":32,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":160,\"byteLength\":16,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":176,\"byteLength\":12,\"target\":34963},"
    "{\"buffer\":0,\"byteOffset\":188,\"byteLength\":192},"
    "{\"buffer\":0,\"byteOffset\":380,\"byteLength\":36},"
    "{\"buffer\":0,\"byteOffset\":416,\"byteLength\":12},"
    "{\"buffer\":0,\"byteOffset\":428,\"byteLength\":3}],"
    "\"buffers\":[{\"uri\":\"blender%20model.bin\",\"byteLength\":431}]}";

static void blender_bin(void)
{
    memset(bbin, 0, sizeof(bbin));
    for (int v = 0; v < 4; v++)
        for (int c = 0; c < 3; c++) {
            bf((size_t)(v * 24 + c * 4), (float)(v * 10 + c) + 0.5f);
            bf((size_t)(v * 24 + 12 + c * 4), c == 2 ? 1.0f : 0.0f);
        }
    static const unsigned col[4][4] = {
        {65535, 32768, 0, 65535}, {0, 65535, 1, 0}, {12345, 0, 65535, 100}, {1, 2, 3, 4}};
    static const unsigned jnt[4][4] = {{0, 1, 2, 0}, {1, 1, 0, 2}, {2, 0, 1, 1}, {0, 0, 0, 0}};
    static const unsigned char wgt[4][4] = {
        {128, 64, 32, 31}, {255, 0, 0, 0}, {100, 100, 50, 5}, {64, 64, 64, 63}};
    for (int v = 0; v < 4; v++)
        for (int c = 0; c < 4; c++) {
            b16((size_t)(96 + v * 8 + c * 2), col[v][c]);
            b16((size_t)(128 + v * 8 + c * 2), jnt[v][c]);
            bbin[160 + v * 4 + c] = wgt[v][c];
        }
    static const unsigned idx[6] = {0, 1, 2, 2, 3, 0};
    for (int i = 0; i < 6; i++)
        b16((size_t)(176 + i * 2), idx[i]);
    /* skin joint j's inverse bind: identity translated by 10 * (j + 1) */
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 16; k++)
            bf((size_t)(188 + j * 64 + k * 4),
               k == 12 ? 10.0f * (float)(j + 1) : (k % 5 == 0 ? 1.0f : 0.0f));
    for (int i = 0; i < 9; i++)
        bf((size_t)(380 + i * 4), -(float)i);
    for (int i = 0; i < 6; i++)
        b16((size_t)(416 + i * 2), (unsigned)(i * 13107));
    bbin[428] = 2;
    bbin[429] = 0;
    bbin[430] = 1;
    write_bytes(tmp("blender model.bin"), bbin, 431);
}

static void write_text(const char *path, const char *text)
{
    write_bytes(path, text, strlen(text));
}

/* text with one substring replaced, in a static buffer */
static const char *replaced(const char *text, const char *from, const char *to)
{
    static char bufs[2][16384];
    static int k;
    char *buf = bufs[k ^= 1];
    const char *at = strstr(text, from);
    if (!at || strlen(text) + strlen(to) >= sizeof(bufs[0])) {
        printf("FAIL replace %s\n", from);
        failures++;
        return text;
    }
    size_t pre = (size_t)(at - text);
    memcpy(buf, text, pre);
    strcpy(buf + pre, to);
    strcat(buf, at + strlen(from));
    return buf;
}

static void test_blender(void)
{
    blender_bin();
    write_text(tmp("blender.gltf"), BLENDER_JSON);
    GltfDoc d;
    char why[256] = "";
    int rc = gltf_Read(tmp("blender.gltf"), &d, why, sizeof(why));
    CHECK(rc == 0, "blender read: %s", why);
    if (rc == 0) {
        CHECK(d.primCount == 2, "2 primitives");
        CHECK(d.meshName && !strcmp(d.meshName, "Mesh.001"), "mesh name");
        CHECK(!d.extrasText, "no extras");
        /* Armature T(1,2,3) * Mesh R(z, 90) S(2) */
        static const float want[16] = {0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 1, 2, 3, 1};
        for (int k = 0; k < 16; k++)
            CHECK(fabsf(d.nodeMatrix[k] - want[k]) < 1e-6f, "node matrix[%d] %g vs %g", k,
                  (double)d.nodeMatrix[k], (double)want[k]);
        const GltfPrim *p = &d.prims[0];
        CHECK(p->vertexCount == 4 && p->indexCount == 6 && p->nrm && p->col && p->joints &&
                  p->weights && !p->uv,
              "prim 0's attributes");
        if (p->vertexCount == 4 && p->col && p->joints && p->weights && p->nrm) {
            for (int v = 0; v < 4; v++)
                for (int c = 0; c < 3; c++) {
                    CHECK(p->pos[v * 3 + c] == (float)(v * 10 + c) + 0.5f, "pos %d.%d", v, c);
                    CHECK(p->nrm[v * 3 + c] == (c == 2 ? 1.0f : 0.0f), "nrm %d.%d", v, c);
                }
            CHECK(p->col[0] == 1.0f && p->col[1] == 32768.0f / 65535.0f && p->col[2] == 0.0f,
                  "colour 0 %g %g %g", (double)p->col[0], (double)p->col[1], (double)p->col[2]);
            CHECK(p->col[6] == 12345.0f / 65535.0f && p->col[8] == 1.0f, "colour 2");
            /* skin order [bone_02, bone_00, bone_01]: joint 0 -> 2, 1 -> 0, 2 -> 1 */
            static const uint8_t wantJ[16] = {2, 0, 1, 2, 0, 0, 2, 1, 1, 2, 0, 0, 2, 2, 2, 2};
            CHECK(!memcmp(p->joints, wantJ, 16), "joints %u %u %u %u", p->joints[0], p->joints[1],
                  p->joints[2], p->joints[3]);
            CHECK(p->weights[0] == 128.0f / 255.0f && p->weights[1] == 64.0f / 255.0f &&
                      p->weights[2] == 32.0f / 255.0f && p->weights[3] == 31.0f / 255.0f,
                  "4 weights");
            CHECK(p->weights[4] == 1.0f && p->weights[15] == 63.0f / 255.0f, "weights 1, 3");
            static const uint32_t wantI[6] = {0, 1, 2, 2, 3, 0};
            CHECK(!memcmp(p->idx, wantI, sizeof(wantI)), "ushort indices");
        }
        const GltfPrim *q = &d.prims[1];
        CHECK(q->vertexCount == 3 && q->uv && !q->joints && q->idx && q->indexCount == 3,
              "prim 1's attributes");
        if (q->vertexCount == 3 && q->uv && q->idx) {
            CHECK(q->pos[4] == -4.0f, "prim 1 pos");
            CHECK(q->uv[1] == 13107.0f / 65535.0f && q->uv[5] == 65535.0f / 65535.0f, "ushort uv");
            CHECK(q->idx[0] == 2 && q->idx[1] == 0 && q->idx[2] == 1, "ubyte indices");
        }
        CHECK(d.skin.count == 3, "3 bones");
        if (d.skin.count == 3) {
            CHECK(d.skin.invBind[2][12] == 10.0f && d.skin.invBind[0][12] == 20.0f &&
                      d.skin.invBind[1][12] == 30.0f && d.skin.invBind[1][0] == 1.0f,
                  "inverse binds by name");
            CHECK(d.skin.parent[0] == -1 && d.skin.parent[1] == 0 && d.skin.parent[2] == 1,
                  "parents %d %d %d", d.skin.parent[0], d.skin.parent[1], d.skin.parent[2]);
            CHECK(!strcmp(d.skin.names[2], "bone_02"), "names");
        }
    }
    gltf_Free(&d);

    /* no bone_NN names: skin order */
    const char *t = replaced(BLENDER_JSON, "\"bone_00\"", "\"Hips\"");
    t = replaced(t, "\"bone_01\"", "\"Spine\"");
    t = replaced(t, "\"bone_02\"", "\"Head\"");
    write_text(tmp("blender_order.gltf"), t);
    rc = gltf_Read(tmp("blender_order.gltf"), &d, why, sizeof(why));
    CHECK(rc == 0, "order read: %s", why);
    if (rc == 0) {
        static const uint8_t wantJ[4] = {0, 1, 2, 0};
        CHECK(!memcmp(d.prims[0].joints, wantJ, 4), "joints in skin order");
        CHECK(d.skin.count == 3 && d.skin.invBind[0][12] == 10.0f &&
                  !strcmp(d.skin.names[0], "Head"),
              "inverse binds in skin order");
        /* skin order: Head (joint 0) under Spine (joint 2) under Hips (joint 1) */
        CHECK(d.skin.parent[0] == 2 && d.skin.parent[2] == 1 && d.skin.parent[1] == -1,
              "parents in skin order %d %d %d", d.skin.parent[0], d.skin.parent[1],
              d.skin.parent[2]);
    }
    gltf_Free(&d);

    /* some named, some not */
    write_text(tmp("blender_mixed.gltf"), replaced(BLENDER_JSON, "\"bone_01\"", "\"Bone.001\""));
    rc = gltf_Read(tmp("blender_mixed.gltf"), &d, why, sizeof(why));
    CHECK(rc != 0 && strstr(why, "name all of them"), "mixed names: %s", why);
    gltf_Free(&d);
    write_text(tmp("blender_dup.gltf"), replaced(BLENDER_JSON, "\"bone_01\"", "\"bone_00\""));
    rc = gltf_Read(tmp("blender_dup.gltf"), &d, why, sizeof(why));
    CHECK(rc != 0 && strstr(why, "two joints"), "duplicate names: %s", why);
    gltf_Free(&d);
    /* a joint index past the skin */
    write_text(tmp("blender_joint.gltf"),
               replaced(BLENDER_JSON, "\"joints\":[4,2,3]", "\"joints\":[4,2]"));
    rc = gltf_Read(tmp("blender_joint.gltf"), &d, why, sizeof(why));
    CHECK(rc != 0 && strstr(why, "uses joint 2 of 2"), "joint past the skin: %s", why);
    gltf_Free(&d);
    /* an index past the vertices */
    bbin[430] = 3;
    write_bytes(tmp("blender model.bin"), bbin, 431);
    rc = gltf_Read(tmp("blender.gltf"), &d, why, sizeof(why));
    CHECK(rc != 0 && strstr(why, "index 3 is past"), "index past the vertices: %s", why);
    gltf_Free(&d);
    bbin[430] = 1;
    write_bytes(tmp("blender model.bin"), bbin, 431);
}

/* --- rejections ------------------------------------------------------------ */

typedef struct Rej {
    const char *uri, *blen, *vlen, *ct, *count, *type, *aextra, *prims, *top;
} Rej;

static char *rej_json(const Rej *r)
{
    static const Rej def = {"\"rej.bin\"",
                            "36",
                            "36",
                            "5126",
                            "3",
                            "\"VEC3\"",
                            "",
                            "{\"attributes\":{\"POSITION\":0}}",
                            ""};
#define PICK(f) (r->f ? r->f : def.f)
    size_t n = 1024 + strlen(PICK(prims));
    char *s = malloc(n);
    snprintf(s, n,
             "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":%s,\"byteLength\":%s}],"
             "\"bufferViews\":[{\"buffer\":0,\"byteLength\":%s}],"
             "\"accessors\":[{\"bufferView\":0,\"componentType\":%s,\"count\":%s,\"type\":%s%s}],"
             "\"meshes\":[{\"primitives\":[%s]}],\"nodes\":[{\"mesh\":0}],"
             "\"scenes\":[{\"nodes\":[0]}]%s}",
             PICK(uri), PICK(blen), PICK(vlen), PICK(ct), PICK(count), PICK(type), PICK(aextra),
             PICK(prims), PICK(top));
#undef PICK
    return s;
}

static void expect_reject(const char *name, const Rej *r, const char *msg)
{
    char *s = rej_json(r);
    char file[64];
    snprintf(file, sizeof(file), "rej_%s.gltf", name);
    write_text(tmp(file), s);
    free(s);
    GltfDoc d;
    char why[512] = "";
    int rc = gltf_Read(tmp(file), &d, why, sizeof(why));
    CHECK(rc != 0, "%s: accepted", name);
    CHECK(rc == 0 || strstr(why, msg), "%s: \"%s\" lacks \"%s\"", name, why, msg);
    if (rc != 0)
        printf("  %-14s %s\n", name, why);
    gltf_Free(&d);
}

static void test_rejections(void)
{
    float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    write_bytes(tmp("rej.bin"), pos, sizeof(pos));
    /* the template itself reads */
    Rej ok = {0};
    char *s = rej_json(&ok);
    write_text(tmp("rej_ok.gltf"), s);
    free(s);
    GltfDoc d;
    char why[256] = "";
    CHECK(gltf_Read(tmp("rej_ok.gltf"), &d, why, sizeof(why)) == 0 && d.primCount == 1 &&
              d.prims[0].vertexCount == 3 && d.prims[0].pos[3] == 1.0f && !d.prims[0].idx,
          "the template reads: %s", why);
    gltf_Free(&d);

    expect_reject("data_uri", &(Rej){.uri = "\"data:application/octet-stream;base64,AAAA\""},
                  "data: URI");
    expect_reject("sparse",
                  &(Rej){.aextra = ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":0,"
                                   "\"componentType\":5125},\"values\":{\"bufferView\":0}}"},
                  "sparse accessors are not supported");
    expect_reject("mode", &(Rej){.prims = "{\"attributes\":{\"POSITION\":0},\"mode\":5}"},
                  "mode 5; only triangle lists");
    expect_reject("no_position", &(Rej){.prims = "{\"attributes\":{\"NORMAL\":0}}"}, "no POSITION");
    size_t one = strlen("{\"attributes\":{\"POSITION\":0}},");
    char *many = malloc(one * 4097 + 1);
    for (int i = 0; i < 4097; i++)
        memcpy(many + (size_t)i * one, "{\"attributes\":{\"POSITION\":0}},", one);
    many[one * 4097 - 1] = '\0';
    expect_reject("prims", &(Rej){.prims = many}, "4097 primitives (at most 4096)");
    free(many);
    expect_reject("vertices", &(Rej){.count = "1048577"}, "more than 1048576 vertices");
    expect_reject("type_vec2", &(Rej){.type = "\"VEC2\""}, "the accepted types are float VEC3");
    expect_reject("type_short", &(Rej){.ct = "5122"}, "is short VEC3; the accepted types");
    expect_reject("type_mat3", &(Rej){.type = "\"MAT3\"", .count = "1"}, "MAT2/MAT3");
    expect_reject("past_view", &(Rej){.vlen = "24"}, "needs 36 bytes but bufferView 0 has 24");
    expect_reject("past_buffer", &(Rej){.vlen = "40"}, "runs past the end of buffer 0");
    expect_reject("truncated", &(Rej){.blen = "40", .vlen = "40"}, "is truncated: 36 bytes");
    expect_reject("ext_required",
                  &(Rej){.top = ",\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]"},
                  "KHR_draco_mesh_compression");
    expect_reject("absolute", &(Rej){.uri = "\"/etc/passwd\""}, "not a relative file name");
    expect_reject("scheme", &(Rej){.uri = "\"http://example.com/a.bin\""},
                  "not a relative file name");
    expect_reject("dotdot", &(Rej){.uri = "\"sub/../../rej.bin\""}, "leaves the model's folder");
    expect_reject("missing_bin", &(Rej){.uri = "\"nothere.bin\""}, "cannot open");
    expect_reject("non_multiple", &(Rej){.count = "2"}, "multiple of 3");
    expect_reject("bad_accessor", &(Rej){.prims = "{\"attributes\":{\"POSITION\":7}}"},
                  "refers to an accessor that does not exist");
    expect_reject("no_count", &(Rej){.count = "0"}, "has no count");
    expect_reject("bad_json", &(Rej){.top = ",\"x\":1}"}, "bad JSON at byte");
    expect_reject("stride", &(Rej){.vlen = "36,\"byteStride\":8"}, "smaller than");
    write_text(tmp("rej_v1.gltf"), "{\"asset\":{\"version\":\"1.0\"}}");
    CHECK(gltf_Read(tmp("rej_v1.gltf"), &d, why, sizeof(why)) != 0 && strstr(why, "2.x"),
          "glTF 1: %s", why);
    gltf_Free(&d);
    CHECK(gltf_Read(tmp("does_not_exist.gltf"), &d, why, sizeof(why)) != 0 &&
              strstr(why, "cannot open"),
          "missing file: %s", why);
    gltf_Free(&d);
}

static void test_matrix(void)
{
    float a[16], inv[16], prod[16], id[16];
    trs(a, 3.0f, -4.0f, 5.0f, 0.9f);
    a[0] *= 2.0f;
    a[1] *= 2.0f;
    CHECK(gltf_Mat4Invert(inv, a) == 0, "invertible");
    gltf_Mat4Mul(prod, a, inv);
    gltf_Mat4Identity(id);
    for (int k = 0; k < 16; k++)
        CHECK(fabsf(prod[k] - id[k]) < 1e-6f, "a * inverse(a) [%d] = %g", k, (double)prod[k]);
    /* column-major: a * point puts the translation in elements 12..14 */
    float t[16], p[16];
    gltf_Mat4Identity(t);
    t[12] = 7.0f;
    gltf_Mat4Identity(p);
    p[0] = 2.0f;
    gltf_Mat4Mul(prod, t, p); /* translate after scaling */
    CHECK(prod[0] == 2.0f && prod[12] == 7.0f, "T * S");
    gltf_Mat4Mul(prod, p, t); /* scale after translating */
    CHECK(prod[12] == 14.0f, "S * T");
    float z[16] = {0};
    CHECK(gltf_Mat4Invert(inv, z) != 0 && inv[0] == 1.0f && inv[1] == 0.0f, "singular");
}

int main(int argc, char **argv)
{
    snprintf(dir, sizeof(dir), "%s/gltf_test_tmp", argc > 1 ? argv[1] : ".");
    ico_mkdir(dir);
    test_matrix();
    test_static();
    test_skinned();
    test_blender();
    test_rejections();
    if (failures) {
        printf("gltf_test: %d failures\n", failures);
        return 1;
    }
    printf("gltf_test: all passed\n");
    return 0;
}
