/* gltf.c: glTF 2.0 writer and reader for model packs (gltf.h). */
#include "gltf.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ico_endian.h"
#include "host_fs.h"
#include "json.h"

#define CT_BYTE 5120
#define CT_UBYTE 5121
#define CT_SHORT 5122
#define CT_USHORT 5123
#define CT_UINT 5125
#define CT_FLOAT 5126

#define GLB_MAGIC 0x46546C67u
#define GLB_JSON 0x4E4F534Au
#define GLB_BIN 0x004E4942u

#define GLTF_MAX_NODES 65536u

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static int fail(char *why, size_t whyLen, const char *fmt, ...)
{
    if (why && whyLen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(why, whyLen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* --- matrices (column-major, column vectors) ------------------------------ */

static void m4d_identity(double m[16])
{
    memset(m, 0, 16 * sizeof(double));
    m[0] = m[5] = m[10] = m[15] = 1.0;
}

static void m4d_mul(double out[16], const double a[16], const double b[16])
{
    double t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            double s = 0.0;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    memcpy(out, t, sizeof(t));
}

/* cofactor inverse; -1 when singular or not finite */
static int m4d_invert(double inv[16], const double m[16])
{
    double t[16];
    t[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    t[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    t[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    t[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    t[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    t[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    t[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    t[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    t[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    t[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    t[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    t[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    t[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    t[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    t[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    t[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = m[0] * t[0] + m[1] * t[4] + m[2] * t[8] + m[3] * t[12];
    if (!(det != 0.0) || !isfinite(det)) {
        m4d_identity(inv);
        return -1;
    }
    for (int i = 0; i < 16; i++) {
        inv[i] = t[i] / det;
        if (!isfinite(inv[i])) {
            m4d_identity(inv);
            return -1;
        }
    }
    return 0;
}

static void m4_to_d(double d[16], const float f[16])
{
    for (int i = 0; i < 16; i++)
        d[i] = (double)f[i];
}

static void m4_from_d(float f[16], const double d[16])
{
    for (int i = 0; i < 16; i++)
        f[i] = (float)d[i];
}

void gltf_Mat4Identity(float out[16])
{
    memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void gltf_Mat4Mul(float out[16], const float a[16], const float b[16])
{
    double da[16], db[16], dr[16];
    m4_to_d(da, a);
    m4_to_d(db, b);
    m4d_mul(dr, da, db);
    m4_from_d(out, dr);
}

int gltf_Mat4Invert(float out[16], const float m[16])
{
    double dm[16], di[16];
    m4_to_d(dm, m);
    int rc = m4d_invert(di, dm);
    m4_from_d(out, di);
    return rc;
}

void gltf_DocInit(GltfDoc *doc)
{
    memset(doc, 0, sizeof(*doc));
    gltf_Mat4Identity(doc->nodeMatrix);
}

/* --- string builder --------------------------------------------------------- */

typedef struct Sb {
    char *p;
    size_t n, cap;
    int oom;
} Sb;

static void sb_put(Sb *s, const char *d, size_t n)
{
    if (s->oom)
        return;
    if (s->n + n + 1 > s->cap) {
        size_t cap = s->cap ? s->cap : 4096;
        while (cap < s->n + n + 1)
            cap *= 2;
        char *p = realloc(s->p, cap);
        if (!p) {
            s->oom = 1;
            return;
        }
        s->p = p;
        s->cap = cap;
    }
    memcpy(s->p + s->n, d, n);
    s->n += n;
    s->p[s->n] = '\0';
}

static void sb_str(Sb *s, const char *str)
{
    sb_put(s, str, strlen(str));
}

static void sb_printf(Sb *s, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        s->oom = 1;
        return;
    }
    sb_put(s, buf, (size_t)n);
}

/* a number printed by the C library: a ',' decimal point (another locale)
 * becomes '.' */
static void sb_num(Sb *s, const char *fmt, double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, v);
    for (char *c = buf; *c; c++)
        if (*c == ',')
            *c = '.';
    sb_str(s, buf);
}

static void sb_float(Sb *s, float f)
{
    sb_num(s, "%.9g", (double)f);
}

static void sb_jstr(Sb *s, const char *str, size_t n)
{
    sb_put(s, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', (char)c};
            sb_put(s, e, 2);
        } else if (c < 0x20) {
            sb_printf(s, "\\u%04x", c);
        } else {
            sb_put(s, (const char *)&c, 1);
        }
    }
    sb_put(s, "\"", 1);
}

/* a parsed value back to compact JSON text */
static void sb_jvalue(Sb *s, const IcoJsonNode *v)
{
    switch (v->type) {
    case ICO_JSON_NULL:
        sb_str(s, "null");
        break;
    case ICO_JSON_FALSE:
        sb_str(s, "false");
        break;
    case ICO_JSON_TRUE:
        sb_str(s, "true");
        break;
    case ICO_JSON_NUM:
        if (v->integral)
            sb_printf(s, "%s%llu", v->neg ? "-" : "", (unsigned long long)v->u);
        else if (!isfinite(v->d))
            sb_str(s, v->d < 0 ? "-1e999" : "1e999");
        else
            sb_num(s, "%.17g", v->d);
        break;
    case ICO_JSON_STR:
        sb_jstr(s, v->str, v->len);
        break;
    case ICO_JSON_ARR:
    case ICO_JSON_OBJ:
        sb_put(s, v->type == ICO_JSON_ARR ? "[" : "{", 1);
        for (const IcoJsonNode *m = v->child; m; m = m->next) {
            if (m != v->child)
                sb_put(s, ",", 1);
            if (v->type == ICO_JSON_OBJ) {
                sb_jstr(s, m->key, m->keyLen);
                sb_put(s, ":", 1);
            }
            sb_jvalue(s, m);
        }
        sb_put(s, v->type == ICO_JSON_ARR ? "]" : "}", 1);
        break;
    }
}

/* --- little-endian bytes ---------------------------------------------------- */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    put_u32(p, v);
}

static float get_f32(const uint8_t *p)
{
    uint32_t v = ico_le32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* --- writer ----------------------------------------------------------------- */

enum { K_POS, K_NRM, K_UV, K_COL, K_JNT, K_WGT, K_IDX, K_IBM, K_COUNT };

static const uint32_t k_elem[K_COUNT] = {12, 12, 8, 12, 4, 16, 4, 64};
static const char *const k_attr[K_COUNT] = {"POSITION", "NORMAL",    "TEXCOORD_0", "COLOR_0",
                                            "JOINTS_0", "WEIGHTS_0", NULL,         NULL};
static const char *const k_type[K_COUNT] = {"VEC3", "VEC3", "VEC2",   "VEC3",
                                            "VEC4", "VEC4", "SCALAR", "MAT4"};
static const int k_ct[K_COUNT] = {CT_FLOAT, CT_FLOAT, CT_FLOAT, CT_FLOAT,
                                  CT_UBYTE, CT_FLOAT, CT_UINT,  CT_FLOAT};

static const void *prim_data(const GltfPrim *p, int k)
{
    switch (k) {
    case K_POS:
        return p->pos;
    case K_NRM:
        return p->nrm;
    case K_UV:
        return p->uv;
    case K_COL:
        return p->col;
    case K_JNT:
        return p->joints;
    case K_WGT:
        return p->weights;
    case K_IDX:
        return p->idx;
    default:
        return NULL;
    }
}

static int floats_finite(const float *f, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!isfinite(f[i]))
            return 0;
    return 1;
}

static int is_zero16(const float m[16])
{
    for (int i = 0; i < 16; i++)
        if (m[i] != 0.0f)
            return 0;
    return 1;
}

static int is_identity16(const float m[16])
{
    float id[16];
    gltf_Mat4Identity(id);
    for (int i = 0; i < 16; i++)
        if (m[i] != id[i])
            return 0;
    return 1;
}

static int write_validate(const GltfDoc *doc, char *why, size_t whyLen)
{
    if (!doc->prims || doc->primCount == 0)
        return fail(why, whyLen, "the model has no primitives");
    if (doc->primCount > GLTF_MAX_PRIMS)
        return fail(why, whyLen, "the model has %u primitives (at most %u)", doc->primCount,
                    GLTF_MAX_PRIMS);
    const GltfSkin *sk = &doc->skin;
    if (sk->count > GLTF_MAX_JOINTS)
        return fail(why, whyLen, "the skin has %u bones (at most %u)", sk->count, GLTF_MAX_JOINTS);
    if (sk->count && (!sk->invBind || !sk->parent))
        return fail(why, whyLen, "the skin has no inverse bind matrices or parents");
    for (uint32_t i = 0; i < sk->count; i++) {
        if (!floats_finite(sk->invBind[i], 16))
            return fail(why, whyLen, "bone %u's inverse bind matrix is not finite", i);
        int p = sk->parent[i];
        if (p >= (int)sk->count || p == (int)i)
            return fail(why, whyLen, "bone %u's parent %d is not another bone", i, p);
        /* no cycles: a chain longer than the bone count loops */
        uint32_t steps = 0;
        for (int q = p; q >= 0; q = sk->parent[q])
            if (++steps > sk->count)
                return fail(why, whyLen, "bone %u's parents form a loop", i);
    }
    if (!floats_finite(doc->nodeMatrix, 16))
        return fail(why, whyLen, "the node matrix is not finite");
    uint64_t verts = 0, idxs = 0;
    for (uint32_t i = 0; i < doc->primCount; i++) {
        const GltfPrim *p = &doc->prims[i];
        uint32_t n = p->vertexCount;
        if (!p->pos || n == 0)
            return fail(why, whyLen, "primitive %u has no vertices", i);
        verts += n;
        if (verts > GLTF_MAX_VERTICES)
            return fail(why, whyLen, "the model has more than %u vertices", GLTF_MAX_VERTICES);
        if (!floats_finite(p->pos, (size_t)n * 3) ||
            (p->nrm && !floats_finite(p->nrm, (size_t)n * 3)) ||
            (p->uv && !floats_finite(p->uv, (size_t)n * 2)) ||
            (p->col && !floats_finite(p->col, (size_t)n * 3)) ||
            (p->weights && !floats_finite(p->weights, (size_t)n * 4)))
            return fail(why, whyLen, "primitive %u has a value that is not finite", i);
        if (!p->joints != !p->weights)
            return fail(why, whyLen,
                        "primitive %u has joints without weights or weights without joints", i);
        if (p->joints) {
            if (sk->count == 0)
                return fail(why, whyLen, "primitive %u has joints but the model has no skin", i);
            for (size_t v = 0; v < (size_t)n * 4; v++)
                if (p->joints[v] >= sk->count)
                    return fail(why, whyLen, "primitive %u: joint %u is not a bone (%u bones)", i,
                                p->joints[v], sk->count);
        }
        if (p->idx) {
            if (p->indexCount == 0 || p->indexCount % 3)
                return fail(why, whyLen, "primitive %u has %u indices (a non-zero multiple of 3)",
                            i, p->indexCount);
            idxs += p->indexCount;
            if (idxs > GLTF_MAX_INDICES)
                return fail(why, whyLen, "the model has more than %u indices", GLTF_MAX_INDICES);
            for (uint32_t k = 0; k < p->indexCount; k++)
                if (p->idx[k] >= n)
                    return fail(why, whyLen, "primitive %u: index %u is past its %u vertices", i,
                                p->idx[k], n);
        } else if (n % 3) {
            return fail(why, whyLen,
                        "primitive %u has no indices and %u vertices (a multiple of 3)", i, n);
        }
    }
    if (doc->extrasText) {
        IcoJson j;
        int bad = ico_json_parse(doc->extrasText, strlen(doc->extrasText), &j) != 0 ||
                  j.root->type != ICO_JSON_OBJ;
        ico_json_free(&j);
        if (bad)
            return fail(why, whyLen, "the extras text is not a JSON object");
    }
    return 0;
}

static void sb_floats(Sb *s, const float *f, int n)
{
    sb_put(s, "[", 1);
    for (int i = 0; i < n; i++) {
        if (i)
            sb_put(s, ",", 1);
        sb_float(s, f[i]);
    }
    sb_put(s, "]", 1);
}

static int write_file(const char *path, const void *data, size_t n)
{
    FILE *f = ico_fopen(path, "wb");
    if (!f)
        return -1;
    size_t w = n ? fwrite(data, 1, n, f) : 0;
    int rc = fclose(f);
    return (w == n && rc == 0) ? 0 : -1;
}

/* percent-encodes everything outside RFC 3986's unreserved set */
static void sb_uri(Sb *s, const char *name)
{
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
            *c == '-' || *c == '.' || *c == '_' || *c == '~')
            sb_put(s, (const char *)c, 1);
        else
            sb_printf(s, "%%%02X", *c);
    }
}

int gltf_Write(const char *path, const GltfDoc *doc, char *why, size_t whyLen)
{
    if (!path || !doc)
        return fail(why, whyLen, "no path or document");
    if (write_validate(doc, why, whyLen) != 0)
        return -1;

    /* the bin: one view per kind, primitives in order */
    uint64_t kindBytes[K_COUNT] = {0};
    for (uint32_t i = 0; i < doc->primCount; i++) {
        const GltfPrim *p = &doc->prims[i];
        for (int k = 0; k < K_IDX; k++)
            if (prim_data(p, k))
                kindBytes[k] += (uint64_t)p->vertexCount * k_elem[k];
        if (p->idx)
            kindBytes[K_IDX] += (uint64_t)p->indexCount * 4;
    }
    kindBytes[K_IBM] = (uint64_t)doc->skin.count * 64;
    uint64_t viewOff[K_COUNT], total = 0;
    int viewIdx[K_COUNT], views = 0;
    for (int k = 0; k < K_COUNT; k++) {
        viewOff[k] = total;
        total += kindBytes[k]; /* every element size is a multiple of 4 */
        viewIdx[k] = kindBytes[k] ? views++ : -1;
    }
    uint8_t *bin = malloc(total ? (size_t)total : 1);
    uint64_t (*accOff)[K_COUNT] = calloc(doc->primCount, sizeof(*accOff));
    Sb s = {0};
    char *gltfPath = NULL, *binPath = NULL;
    int rc = -1;
    if (!bin || !accOff) {
        fail(why, whyLen, "out of memory");
        goto done;
    }
    uint64_t cur[K_COUNT];
    memcpy(cur, viewOff, sizeof(cur));
    for (uint32_t i = 0; i < doc->primCount; i++) {
        const GltfPrim *p = &doc->prims[i];
        uint32_t n = p->vertexCount;
        for (int k = 0; k <= K_IDX; k++) {
            const void *d = prim_data(p, k);
            if (!d)
                continue;
            accOff[i][k] = cur[k] - viewOff[k];
            uint8_t *o = bin + cur[k];
            if (k == K_JNT) {
                memcpy(o, p->joints, (size_t)n * 4);
                cur[k] += (uint64_t)n * 4;
            } else if (k == K_IDX) {
                for (uint32_t x = 0; x < p->indexCount; x++)
                    put_u32(o + 4 * x, p->idx[x]);
                cur[k] += (uint64_t)p->indexCount * 4;
            } else {
                size_t nf = (size_t)n * (k_elem[k] / 4);
                const float *f = d;
                for (size_t x = 0; x < nf; x++)
                    put_f32(o + 4 * x, f[x]);
                cur[k] += (uint64_t)nf * 4;
            }
        }
    }
    for (uint32_t b = 0; b < doc->skin.count; b++)
        for (int x = 0; x < 16; x++)
            put_f32(bin + viewOff[K_IBM] + b * 64 + x * 4, doc->skin.invBind[b][x]);

    const char *base = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\')
            base = c + 1;
    size_t plen = strlen(path);
    gltfPath = malloc(plen + 6);
    binPath = malloc(plen + 5);
    if (!gltfPath || !binPath) {
        fail(why, whyLen, "out of memory");
        goto done;
    }
    snprintf(gltfPath, plen + 6, "%s.gltf", path);
    snprintf(binPath, plen + 5, "%s.bin", path);

    /* the JSON */
    sb_str(&s, "{\n\"asset\":{\"version\":\"2.0\",\"generator\":\"ico-pc\"");
    if (doc->extrasText) {
        sb_str(&s, ",\"extras\":");
        sb_str(&s, doc->extrasText);
    }
    sb_str(&s, "},\n\"scene\":0,\n\"scenes\":[{\"nodes\":[0");
    for (uint32_t b = 0; b < doc->skin.count; b++)
        if (doc->skin.parent[b] < 0)
            sb_printf(&s, ",%u", b + 1);
    sb_str(&s, "]}],\n\"nodes\":[\n{");
    if (doc->meshName) {
        sb_str(&s, "\"name\":");
        sb_jstr(&s, doc->meshName, strlen(doc->meshName));
        sb_str(&s, ",");
    }
    sb_str(&s, "\"mesh\":0");
    if (doc->skin.count)
        sb_str(&s, ",\"skin\":0");
    if (!is_zero16(doc->nodeMatrix) && !is_identity16(doc->nodeMatrix)) {
        sb_str(&s, ",\"matrix\":");
        sb_floats(&s, doc->nodeMatrix, 16);
    }
    sb_str(&s, "}");
    for (uint32_t b = 0; b < doc->skin.count; b++) {
        /* local bind = parent's inverse bind * inverse(own inverse bind) */
        double ib[16], g[16], local[16];
        m4_to_d(ib, doc->skin.invBind[b]);
        m4d_invert(g, ib);
        int par = doc->skin.parent[b];
        if (par >= 0) {
            double pib[16];
            m4_to_d(pib, doc->skin.invBind[par]);
            m4d_mul(local, pib, g);
        } else {
            memcpy(local, g, sizeof(g));
        }
        float lf[16];
        m4_from_d(lf, local);
        if (!floats_finite(lf, 16))
            gltf_Mat4Identity(lf);
        sb_printf(&s, ",\n{\"name\":\"bone_%02u\"", b);
        if (!is_identity16(lf)) { /* the default is left out */
            sb_str(&s, ",\"matrix\":");
            sb_floats(&s, lf, 16);
        }
        int first = 1;
        for (uint32_t c = 0; c < doc->skin.count; c++)
            if (doc->skin.parent[c] == (int)b) {
                sb_str(&s, first ? ",\"children\":[" : ",");
                sb_printf(&s, "%u", c + 1);
                first = 0;
            }
        if (!first)
            sb_str(&s, "]");
        sb_str(&s, "}");
    }
    sb_str(&s, "\n],\n\"meshes\":[{");
    if (doc->meshName) {
        sb_str(&s, "\"name\":");
        sb_jstr(&s, doc->meshName, strlen(doc->meshName));
        sb_str(&s, ",");
    }
    sb_str(&s, "\"primitives\":[");
    uint32_t acc = 0;
    for (uint32_t i = 0; i < doc->primCount; i++) {
        const GltfPrim *p = &doc->prims[i];
        sb_str(&s, i ? ",\n{\"attributes\":{" : "\n{\"attributes\":{");
        int first = 1;
        for (int k = 0; k < K_IDX; k++)
            if (prim_data(p, k)) {
                sb_printf(&s, "%s\"%s\":%u", first ? "" : ",", k_attr[k], acc++);
                first = 0;
            }
        sb_str(&s, "}");
        if (p->idx)
            sb_printf(&s, ",\"indices\":%u", acc++);
        sb_str(&s, ",\"mode\":4");
        if (p->name) {
            sb_str(&s, ",\"extras\":{\"name\":");
            sb_jstr(&s, p->name, strlen(p->name));
            sb_str(&s, "}");
        }
        sb_str(&s, "}");
    }
    sb_str(&s, "\n]}],\n");
    if (doc->skin.count) {
        sb_str(&s, "\"skins\":[{\"joints\":[");
        for (uint32_t b = 0; b < doc->skin.count; b++)
            sb_printf(&s, "%s%u", b ? "," : "", b + 1);
        sb_printf(&s, "],\"inverseBindMatrices\":%u}],\n", acc);
    }
    sb_str(&s, "\"accessors\":[");
    int firstAcc = 1;
    for (uint32_t i = 0; i <= doc->primCount; i++) {
        for (int k = 0; k < K_COUNT; k++) {
            uint32_t count;
            uint64_t off;
            float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
            if (i < doc->primCount) {
                const GltfPrim *p = &doc->prims[i];
                if (k == K_IBM || !prim_data(p, k))
                    continue;
                count = k == K_IDX ? p->indexCount : p->vertexCount;
                off = accOff[i][k];
                if (k == K_POS) {
                    for (int c = 0; c < 3; c++)
                        mn[c] = mx[c] = p->pos[c];
                    for (uint32_t v = 1; v < p->vertexCount; v++)
                        for (int c = 0; c < 3; c++) {
                            float f = p->pos[v * 3 + c];
                            mn[c] = f < mn[c] ? f : mn[c];
                            mx[c] = f > mx[c] ? f : mx[c];
                        }
                }
            } else {
                if (k != K_IBM || !doc->skin.count)
                    continue;
                count = doc->skin.count;
                off = 0;
            }
            sb_str(&s, firstAcc ? "\n{" : ",\n{");
            firstAcc = 0;
            sb_printf(&s, "\"bufferView\":%d,", viewIdx[k]);
            if (off)
                sb_printf(&s, "\"byteOffset\":%llu,", (unsigned long long)off);
            sb_printf(&s, "\"componentType\":%d,\"count\":%u,\"type\":\"%s\"", k_ct[k], count,
                      k_type[k]);
            if (k == K_POS) {
                sb_str(&s, ",\"min\":");
                sb_floats(&s, mn, 3);
                sb_str(&s, ",\"max\":");
                sb_floats(&s, mx, 3);
            }
            sb_str(&s, "}");
        }
    }
    sb_str(&s, "\n],\n\"bufferViews\":[");
    for (int k = 0, first = 1; k < K_COUNT; k++) {
        if (viewIdx[k] < 0)
            continue;
        sb_printf(&s, "%s\n{\"buffer\":0,\"byteOffset\":%llu,\"byteLength\":%llu", first ? "" : ",",
                  (unsigned long long)viewOff[k], (unsigned long long)kindBytes[k]);
        if (k < K_IDX)
            sb_printf(&s, ",\"byteStride\":%u,\"target\":34962", k_elem[k]);
        else if (k == K_IDX)
            sb_str(&s, ",\"target\":34963");
        sb_str(&s, "}");
        first = 0;
    }
    sb_str(&s, "\n],\n\"buffers\":[{\"uri\":\"");
    sb_uri(&s, base);
    sb_printf(&s, ".bin\",\"byteLength\":%llu}]\n}\n", (unsigned long long)total);
    if (s.oom) {
        fail(why, whyLen, "out of memory");
        goto done;
    }
    if (total > GLTF_MAX_FILE_BYTES) {
        fail(why, whyLen, "the model's data is %llu bytes (at most %u)", (unsigned long long)total,
             GLTF_MAX_FILE_BYTES);
        goto done;
    }
    if (write_file(binPath, bin, (size_t)total) != 0) {
        fail(why, whyLen, "cannot write %s", binPath);
        ico_remove(binPath);
        goto done;
    }
    if (write_file(gltfPath, s.p, s.n) != 0) {
        fail(why, whyLen, "cannot write %s", gltfPath);
        ico_remove(gltfPath);
        ico_remove(binPath);
        goto done;
    }
    rc = 0;
done:
    free(s.p);
    free(bin);
    free(accOff);
    free(gltfPath);
    free(binPath);
    return rc;
}

/* --- reader ----------------------------------------------------------------- */

typedef struct RdBuf {
    const uint8_t *p;
    uint64_t len; /* the declared byteLength (the bytes there are checked) */
    uint8_t *own;
} RdBuf;

typedef struct Rd {
    IcoJson j;
    const IcoJsonNode *root;
    const char *path;
    uint8_t *file;
    size_t fileLen;
    const uint8_t *glbBin;
    uint64_t glbBinLen;
    RdBuf *bufs;
    size_t bufCount;
    int *parentOf; /* per node */
    size_t nodeCount;
    char *why;
    size_t whyLen;
} Rd;

typedef struct Acc {
    const uint8_t *base;
    uint64_t count, stride;
    int ct, nc, norm;
    uint64_t index;
} Acc;

static int read_file(const char *path, uint8_t **data, size_t *len, unsigned long limit, char *why,
                     size_t whyLen)
{
    *data = NULL;
    *len = 0;
    FILE *f = ico_fopen(path, "rb");
    if (!f)
        return fail(why, whyLen, "cannot open %s", path);
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail(why, whyLen, "cannot read %s", path);
    }
    if ((unsigned long)n > limit) {
        fclose(f);
        return fail(why, whyLen, "%s is %ld bytes (at most %lu here)", path, n, limit);
    }
    uint8_t *p = malloc((size_t)n + 1);
    if (!p) {
        fclose(f);
        return fail(why, whyLen, "out of memory reading %s", path);
    }
    size_t got = n ? fread(p, 1, (size_t)n, f) : 0;
    fclose(f);
    if (got != (size_t)n) {
        free(p);
        return fail(why, whyLen, "cannot read %s", path);
    }
    *data = p;
    *len = (size_t)n;
    return 0;
}

/* a non-negative integer member: def when absent, -1 when not one */
static int get_uint(const IcoJsonNode *obj, const char *key, uint64_t def, uint64_t *out)
{
    const IcoJsonNode *n = ico_json_get(obj, key);
    if (!n) {
        *out = def;
        return 0;
    }
    return ico_json_u64(n, out);
}

static int uri_decode(Rd *r, uint64_t bi, const char *uri, char *out, size_t outLen)
{
    if (strncmp(uri, "data:", 5) == 0)
        return fail(r->why, r->whyLen,
                    "buffer %llu is a data: URI; keep the data in a .bin file next to the .gltf "
                    "(or export a .glb)",
                    (unsigned long long)bi);
    for (const char *c = uri; *c && *c != '/'; c++)
        if (*c == ':')
            return fail(r->why, r->whyLen, "buffer %llu's uri \"%s\" is not a relative file name",
                        (unsigned long long)bi, uri);
    if (uri[0] == '/' || uri[0] == '\\' || uri[0] == '\0')
        return fail(r->why, r->whyLen, "buffer %llu's uri \"%s\" is not a relative file name",
                    (unsigned long long)bi, uri);
    size_t n = 0;
    for (const char *c = uri; *c; c++) {
        char ch = *c;
        if (ch == '%') {
            int hi = ico_json_hexval(c[1]), lo = hi >= 0 ? ico_json_hexval(c[2]) : -1;
            if (hi < 0 || lo < 0)
                return fail(r->why, r->whyLen, "buffer %llu's uri \"%s\" has a bad %% escape",
                            (unsigned long long)bi, uri);
            ch = (char)(hi * 16 + lo);
            c += 2;
            if (ch == '\0')
                return fail(r->why, r->whyLen, "buffer %llu's uri has a NUL",
                            (unsigned long long)bi);
        }
        if (n + 1 >= outLen)
            return fail(r->why, r->whyLen, "buffer %llu's uri is too long", (unsigned long long)bi);
        out[n++] = ch;
    }
    out[n] = '\0';
    /* no ".." segment: the file stays in the .gltf's folder or below it */
    for (const char *seg = out; seg;) {
        const char *end = seg + strcspn(seg, "/\\");
        if (end - seg == 2 && seg[0] == '.' && seg[1] == '.')
            return fail(r->why, r->whyLen, "buffer %llu's uri \"%s\" leaves the model's folder",
                        (unsigned long long)bi, uri);
        seg = *end ? end + 1 : NULL;
    }
    return 0;
}

static int load_buffers(Rd *r)
{
    const IcoJsonNode *bufs = ico_json_get(r->root, "buffers");
    const size_t count = ico_json_count(bufs);
    if (bufs && bufs->type != ICO_JSON_ARR)
        return fail(r->why, r->whyLen, "buffers is not an array");
    if (count == 0)
        return 0;
    if (count > GLTF_MAX_BUFFERS)
        return fail(r->why, r->whyLen, "the file lists %zu buffers (at most %u)", count,
                    (unsigned)GLTF_MAX_BUFFERS);
    uint64_t totalBytes = 0;
    r->bufs = calloc(count, sizeof(RdBuf));
    if (!r->bufs)
        return fail(r->why, r->whyLen, "out of memory");
    r->bufCount = count; /* set only once bufs exists: the cleanup walks bufCount entries */
    size_t i = 0;
    for (const IcoJsonNode *b = bufs->child; b; b = b->next, i++) {
        uint64_t len;
        if (totalBytes > GLTF_MAX_BUFFER_BYTES)
            return fail(r->why, r->whyLen, "the buffers together are over %u bytes",
                        (unsigned)GLTF_MAX_BUFFER_BYTES);
        if (get_uint(b, "byteLength", UINT64_MAX, &len) != 0 || len == UINT64_MAX)
            return fail(r->why, r->whyLen, "buffer %zu has no byteLength", i);
        const IcoJsonNode *u = ico_json_get(b, "uri");
        if (!u) {
            if (i != 0 || !r->glbBin)
                return fail(r->why, r->whyLen, "buffer %zu has no uri and no GLB BIN chunk", i);
            if (r->glbBinLen < len)
                return fail(r->why, r->whyLen,
                            "the GLB's BIN chunk is truncated: %llu bytes, buffer 0 needs %llu",
                            (unsigned long long)r->glbBinLen, (unsigned long long)len);
            r->bufs[i].p = r->glbBin;
            r->bufs[i].len = len;
            totalBytes += r->glbBinLen;
            continue;
        }
        const char *uri = ico_json_str(u);
        if (!uri)
            return fail(r->why, r->whyLen, "buffer %zu's uri is not a string", i);
        char name[1024];
        if (uri_decode(r, i, uri, name, sizeof(name)) != 0)
            return -1;
        size_t dirLen = 0;
        for (size_t c = 0; r->path[c]; c++)
            if (r->path[c] == '/' || r->path[c] == '\\')
                dirLen = c + 1;
        size_t full = dirLen + strlen(name) + 1;
        char *fp = malloc(full);
        if (!fp)
            return fail(r->why, r->whyLen, "out of memory");
        memcpy(fp, r->path, dirLen);
        strcpy(fp + dirLen, name);
        uint8_t *data;
        size_t n;
        const uint64_t room =
            totalBytes < GLTF_MAX_BUFFER_BYTES ? GLTF_MAX_BUFFER_BYTES - totalBytes : 0;
        int rc = read_file(fp, &data, &n, (unsigned long)room, r->why, r->whyLen);
        if (rc == 0)
            totalBytes += n;
        if (rc == 0 && n < len)
            rc = fail(r->why, r->whyLen, "%s is truncated: %zu bytes, buffer %zu needs %llu", fp, n,
                      i, (unsigned long long)len);
        free(fp);
        if (rc != 0) {
            free(data);
            return -1;
        }
        r->bufs[i].own = data;
        r->bufs[i].p = data;
        r->bufs[i].len = len;
    }
    return 0;
}

static int type_comps(const char *t)
{
    if (!t)
        return 0;
    if (!strcmp(t, "SCALAR"))
        return 1;
    if (!strcmp(t, "VEC2"))
        return 2;
    if (!strcmp(t, "VEC3"))
        return 3;
    if (!strcmp(t, "VEC4"))
        return 4;
    if (!strcmp(t, "MAT4"))
        return 16;
    return -1; /* MAT2, MAT3 and anything else: not accepted */
}

static int ct_size(int ct)
{
    switch (ct) {
    case CT_BYTE:
    case CT_UBYTE:
        return 1;
    case CT_SHORT:
    case CT_USHORT:
        return 2;
    case CT_UINT:
    case CT_FLOAT:
        return 4;
    default:
        return 0;
    }
}

static const IcoJsonNode *acc_node(Rd *r, const IcoJsonNode *ref, const char *what, uint64_t *index)
{
    const IcoJsonNode *accs = ico_json_get(r->root, "accessors");
    if (ico_json_u64(ref, index) != 0 || *index >= ico_json_count(accs)) {
        fail(r->why, r->whyLen, "%s refers to an accessor that does not exist", what);
        return NULL;
    }
    return ico_json_at(accs, (size_t)*index);
}

/* the accessor's count alone (the caps come before the bounds) */
static int acc_count(Rd *r, const IcoJsonNode *ref, const char *what, uint64_t *count)
{
    uint64_t index;
    const IcoJsonNode *a = acc_node(r, ref, what, &index);
    if (!a)
        return -1;
    if (get_uint(a, "count", 0, count) != 0 || *count == 0)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has no count", (unsigned long long)index,
                    what);
    return 0;
}

static int acc_open(Rd *r, const IcoJsonNode *ref, const char *what, Acc *out)
{
    memset(out, 0, sizeof(*out));
    uint64_t ai;
    const IcoJsonNode *a = acc_node(r, ref, what, &ai);
    if (!a)
        return -1;
    unsigned long long ail = (unsigned long long)ai;
    out->index = ai;
    if (ico_json_get(a, "sparse"))
        return fail(r->why, r->whyLen,
                    "accessor %llu (%s) is sparse; sparse accessors are not supported", ail, what);
    uint64_t ct, count, off, vi;
    if (get_uint(a, "componentType", 0, &ct) != 0 || ct_size((int)ct) == 0 || ct > 6000)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has a bad componentType", ail, what);
    out->ct = (int)ct;
    out->nc = type_comps(ico_json_str(ico_json_get(a, "type")));
    if (out->nc == 0)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has no type", ail, what);
    if (get_uint(a, "count", 0, &count) != 0 || count == 0)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has no count", ail, what);
    if (count > GLTF_MAX_INDICES)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has %llu entries (at most %u)", ail,
                    what, (unsigned long long)count, GLTF_MAX_INDICES);
    out->count = count;
    out->norm = ico_json_bool(ico_json_get(a, "normalized")) == 1;
    if (get_uint(a, "byteOffset", 0, &off) != 0)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has a bad byteOffset", ail, what);
    const IcoJsonNode *vref = ico_json_get(a, "bufferView");
    if (!vref)
        return fail(r->why, r->whyLen, "accessor %llu (%s) has no bufferView", ail, what);
    const IcoJsonNode *views = ico_json_get(r->root, "bufferViews");
    if (ico_json_u64(vref, &vi) != 0 || vi >= ico_json_count(views))
        return fail(r->why, r->whyLen,
                    "accessor %llu (%s) refers to a bufferView that does not exist", ail, what);
    const IcoJsonNode *v = ico_json_at(views, (size_t)vi);
    unsigned long long vil = (unsigned long long)vi;
    uint64_t bi, voff, vlen, vstride;
    if (get_uint(v, "buffer", UINT64_MAX, &bi) != 0 || bi >= r->bufCount)
        return fail(r->why, r->whyLen, "bufferView %llu refers to a buffer that does not exist",
                    vil);
    if (get_uint(v, "byteOffset", 0, &voff) != 0 ||
        get_uint(v, "byteLength", UINT64_MAX, &vlen) != 0 || vlen == UINT64_MAX ||
        get_uint(v, "byteStride", 0, &vstride) != 0 || vstride > 252)
        return fail(r->why, r->whyLen,
                    "bufferView %llu has a bad byteOffset, byteLength or byteStride", vil);
    const RdBuf *b = &r->bufs[bi];
    if (voff > b->len || vlen > b->len - voff)
        return fail(
            r->why, r->whyLen,
            "bufferView %llu (bytes %llu..%llu) runs past the end of buffer %llu (%llu bytes)", vil,
            (unsigned long long)voff, (unsigned long long)(voff + vlen), (unsigned long long)bi,
            (unsigned long long)b->len);
    uint64_t elem = (uint64_t)ct_size(out->ct) * (uint64_t)(out->nc > 0 ? out->nc : 1);
    uint64_t stride = vstride ? vstride : elem;
    if (vstride && vstride < elem)
        return fail(r->why, r->whyLen,
                    "bufferView %llu's byteStride %llu is smaller than accessor %llu's elements",
                    vil, (unsigned long long)vstride, ail);
    /* count <= GLTF_MAX_INDICES and stride <= 252: no overflow */
    uint64_t need = off + stride * (count - 1) + elem;
    if (off > vlen || need > vlen)
        return fail(r->why, r->whyLen,
                    "accessor %llu (%s) needs %llu bytes but bufferView %llu has %llu", ail, what,
                    (unsigned long long)need, vil, (unsigned long long)vlen);
    out->base = b->p + voff + off;
    out->stride = stride;
    return 0;
}

static const char *ct_name(int ct)
{
    switch (ct) {
    case CT_BYTE:
        return "byte";
    case CT_UBYTE:
        return "ubyte";
    case CT_SHORT:
        return "short";
    case CT_USHORT:
        return "ushort";
    case CT_UINT:
        return "uint";
    default:
        return "float";
    }
}

static int acc_reject(Rd *r, const Acc *a, const char *what, const char *want)
{
    static const char *const tn[17] = {"?", "SCALAR", "VEC2", "VEC3", "VEC4", "?", "?", "?",   "?",
                                       "?", "?",      "?",    "?",    "?",    "?", "?", "MAT4"};
    return fail(r->why, r->whyLen, "accessor %llu (%s) is %s%s %s; the accepted types are %s",
                (unsigned long long)a->index, what, a->norm ? "normalized " : "", ct_name(a->ct),
                a->nc > 0 ? tn[a->nc] : "MAT2/MAT3", want);
}

/* float, or normalized ubyte/ushort */
static int acc_is_floatish(const Acc *a)
{
    return a->ct == CT_FLOAT || (a->norm && (a->ct == CT_UBYTE || a->ct == CT_USHORT));
}

static float acc_f(const Acc *a, uint64_t i, int c)
{
    const uint8_t *p = a->base + i * a->stride;
    switch (a->ct) {
    case CT_UBYTE:
        return (float)p[c] / 255.0f;
    case CT_USHORT:
        return (float)ico_le16(p + 2 * c) / 65535.0f;
    default:
        return get_f32(p + 4 * c);
    }
}

static uint32_t acc_u(const Acc *a, uint64_t i, int c)
{
    const uint8_t *p = a->base + i * a->stride;
    switch (a->ct) {
    case CT_UBYTE:
        return p[c];
    case CT_USHORT:
        return ico_le16(p + 2 * c);
    default:
        return ico_le32(p + 4 * c);
    }
}

/* nc floats per entry into a new array (ncOut wide; extra components dropped) */
static float *acc_floats(Rd *r, const Acc *a, int ncOut)
{
    float *f = malloc((size_t)a->count * (size_t)ncOut * sizeof(float));
    if (!f) {
        fail(r->why, r->whyLen, "out of memory");
        return NULL;
    }
    for (uint64_t i = 0; i < a->count; i++)
        for (int c = 0; c < ncOut; c++)
            f[i * ncOut + c] = acc_f(a, i, c);
    return f;
}

/* a node's local transform */
static int node_local(Rd *r, size_t ni, double m[16])
{
    const IcoJsonNode *n = ico_json_at(ico_json_get(r->root, "nodes"), ni);
    const IcoJsonNode *mat = ico_json_get(n, "matrix");
    m4d_identity(m);
    if (mat) {
        if (ico_json_count(mat) != 16 || mat->type != ICO_JSON_ARR)
            return fail(r->why, r->whyLen, "node %zu's matrix is not 16 numbers", ni);
        int i = 0;
        for (const IcoJsonNode *e = mat->child; e; e = e->next, i++)
            if (ico_json_double(e, &m[i]) != 0)
                return fail(r->why, r->whyLen, "node %zu's matrix is not 16 numbers", ni);
        return 0;
    }
    double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};

    struct {
        const char *key;
        double *v;
        size_t n;
    } parts[3] = {{"translation", t, 3}, {"rotation", q, 4}, {"scale", s, 3}};

    for (int p = 0; p < 3; p++) {
        const IcoJsonNode *a = ico_json_get(n, parts[p].key);
        if (!a)
            continue;
        if (a->type != ICO_JSON_ARR || ico_json_count(a) != parts[p].n)
            return fail(r->why, r->whyLen, "node %zu's %s is not %zu numbers", ni, parts[p].key,
                        parts[p].n);
        size_t i = 0;
        for (const IcoJsonNode *e = a->child; e; e = e->next, i++)
            if (ico_json_double(e, &parts[p].v[i]) != 0)
                return fail(r->why, r->whyLen, "node %zu's %s is not %zu numbers", ni, parts[p].key,
                            parts[p].n);
    }
    double len = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 0)
        for (int i = 0; i < 4; i++)
            q[i] /= len;
    double x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m[1] = (2 * (x * y + z * w)) * s[0];
    m[2] = (2 * (x * z - y * w)) * s[0];
    m[4] = (2 * (x * y - z * w)) * s[1];
    m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6] = (2 * (y * z + x * w)) * s[1];
    m[8] = (2 * (x * z + y * w)) * s[2];
    m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return 0;
}

static int node_world(Rd *r, size_t ni, double m[16])
{
    if (node_local(r, ni, m) != 0)
        return -1;
    for (int p = r->parentOf[ni]; p >= 0; p = r->parentOf[p]) {
        double l[16];
        if (node_local(r, (size_t)p, l) != 0)
            return -1;
        m4d_mul(m, l, m);
    }
    return 0;
}

static int build_hierarchy(Rd *r)
{
    const IcoJsonNode *nodes = ico_json_get(r->root, "nodes");
    if (nodes && nodes->type != ICO_JSON_ARR)
        return fail(r->why, r->whyLen, "nodes is not an array");
    r->nodeCount = ico_json_count(nodes);
    if (r->nodeCount > GLTF_MAX_NODES)
        return fail(r->why, r->whyLen, "the file has %zu nodes (at most %u)", r->nodeCount,
                    GLTF_MAX_NODES);
    r->parentOf = malloc((r->nodeCount ? r->nodeCount : 1) * sizeof(int));
    if (!r->parentOf)
        return fail(r->why, r->whyLen, "out of memory");
    for (size_t i = 0; i < r->nodeCount; i++)
        r->parentOf[i] = -1;
    size_t i = 0;
    for (const IcoJsonNode *n = nodes ? nodes->child : NULL; n; n = n->next, i++) {
        const IcoJsonNode *ch = ico_json_get(n, "children");
        for (const IcoJsonNode *c = ch ? ch->child : NULL; c; c = c->next) {
            uint64_t ci;
            if (ico_json_u64(c, &ci) != 0 || ci >= r->nodeCount || ci == i)
                return fail(r->why, r->whyLen, "node %zu has a child that does not exist", i);
            if (r->parentOf[ci] >= 0)
                return fail(r->why, r->whyLen, "node %llu has two parents", (unsigned long long)ci);
            r->parentOf[ci] = (int)i;
        }
    }
    for (i = 0; i < r->nodeCount; i++) {
        size_t steps = 0;
        for (int p = r->parentOf[i]; p >= 0; p = r->parentOf[p])
            if (++steps > r->nodeCount)
                return fail(r->why, r->whyLen, "the nodes' children form a loop");
    }
    return 0;
}

/* the first node with a mesh, depth-first from the scene's roots; -1 none */
static int find_mesh_node(Rd *r, int64_t *found)
{
    *found = -1;
    if (r->nodeCount == 0)
        return 0;
    const IcoJsonNode *nodes = ico_json_get(r->root, "nodes");
    size_t *stack = malloc(r->nodeCount * sizeof(size_t));
    uint8_t *seen = calloc(r->nodeCount, 1);
    int rc = 0;
    if (!stack || !seen) {
        rc = fail(r->why, r->whyLen, "out of memory");
        goto done;
    }
    const IcoJsonNode *scenes = ico_json_get(r->root, "scenes");
    const IcoJsonNode *roots = NULL;
    if (ico_json_count(scenes)) {
        uint64_t si;
        if (get_uint(r->root, "scene", 0, &si) != 0 || si >= ico_json_count(scenes)) {
            rc = fail(r->why, r->whyLen, "the default scene does not exist");
            goto done;
        }
        roots = ico_json_get(ico_json_at(scenes, (size_t)si), "nodes");
    }
    size_t rootCount = roots ? ico_json_count(roots) : r->nodeCount;
    const IcoJsonNode *rn = roots ? roots->child : NULL;
    for (size_t ri = 0; ri < rootCount && *found < 0; ri++) {
        size_t start;
        if (roots) {
            uint64_t v;
            if (ico_json_u64(rn, &v) != 0 || v >= r->nodeCount) {
                rc = fail(r->why, r->whyLen, "the scene has a node that does not exist");
                goto done;
            }
            start = (size_t)v;
            rn = rn->next;
        } else {
            if (r->parentOf[ri] >= 0)
                continue;
            start = ri;
        }
        size_t sp = 0;
        if (!seen[start]) {
            stack[sp++] = start;
            seen[start] = 1;
        }
        while (sp && *found < 0) {
            size_t ni = stack[--sp];
            const IcoJsonNode *n = ico_json_at(nodes, ni);
            if (ico_json_get(n, "mesh")) {
                *found = (int64_t)ni;
                break;
            }
            /* children pushed in reverse so the first is visited first */
            const IcoJsonNode *ch = ico_json_get(n, "children");
            size_t cc = ico_json_count(ch);
            for (size_t k = cc; k-- > 0;) {
                uint64_t ci;
                ico_json_u64(ico_json_at(ch, k), &ci); /* checked in build_hierarchy */
                if (!seen[ci]) {
                    seen[ci] = 1;
                    stack[sp++] = (size_t)ci;
                }
            }
        }
    }
done:
    free(stack);
    free(seen);
    return rc;
}

/* "bone_NN": NN, else -1 */
static int bone_number(const char *name)
{
    if (!name || strncmp(name, "bone_", 5) != 0)
        return -1;
    const char *d = name + 5;
    int v = 0, n = 0;
    for (; *d; d++, n++) {
        if (*d < '0' || *d > '9' || n >= 3)
            return -1;
        v = v * 10 + (*d - '0');
    }
    if (n == 0 || v >= (int)GLTF_MAX_JOINTS)
        return -1;
    return v;
}

static int read_skin(Rd *r, uint64_t si, GltfDoc *out, int **jointToBone, size_t *jointCount)
{
    const IcoJsonNode *skins = ico_json_get(r->root, "skins");
    if (si >= ico_json_count(skins))
        return fail(r->why, r->whyLen, "the mesh node's skin does not exist");
    const IcoJsonNode *skin = ico_json_at(skins, (size_t)si);
    const IcoJsonNode *joints = ico_json_get(skin, "joints");
    size_t jc = ico_json_count(joints);
    if (!joints || joints->type != ICO_JSON_ARR || jc == 0)
        return fail(r->why, r->whyLen, "skin %llu has no joints", (unsigned long long)si);
    if (jc > GLTF_MAX_JOINTS)
        return fail(r->why, r->whyLen, "skin %llu has %zu joints (at most %u)",
                    (unsigned long long)si, jc, GLTF_MAX_JOINTS);
    const IcoJsonNode *nodes = ico_json_get(r->root, "nodes");
    size_t *jn = malloc(jc * sizeof(size_t));
    int *bone = malloc(jc * sizeof(int));
    int *nodeJoint = malloc(r->nodeCount * sizeof(int));
    float *ibm = NULL;
    int rc = -1;
    *jointToBone = bone;
    if (!jn || !bone || !nodeJoint) {
        fail(r->why, r->whyLen, "out of memory");
        goto done;
    }
    for (size_t n = 0; n < r->nodeCount; n++)
        nodeJoint[n] = -1;
    size_t named = 0, j = 0;
    for (const IcoJsonNode *e = joints->child; e; e = e->next, j++) {
        uint64_t ni;
        if (ico_json_u64(e, &ni) != 0 || ni >= r->nodeCount || nodeJoint[ni] >= 0) {
            fail(r->why, r->whyLen, "skin %llu's joint %zu is not a distinct node",
                 (unsigned long long)si, j);
            goto done;
        }
        jn[j] = (size_t)ni;
        nodeJoint[ni] = (int)j;
        bone[j] = bone_number(ico_json_str(ico_json_get(ico_json_at(nodes, (size_t)ni), "name")));
        if (bone[j] >= 0)
            named++;
    }
    uint32_t count;
    if (named == jc) {
        int maxBone = -1;
        for (j = 0; j < jc; j++) {
            for (size_t k = 0; k < j; k++)
                if (bone[k] == bone[j]) {
                    fail(r->why, r->whyLen, "two joints are named bone_%02d", bone[j]);
                    goto done;
                }
            maxBone = bone[j] > maxBone ? bone[j] : maxBone;
        }
        count = (uint32_t)maxBone + 1;
    } else if (named == 0) {
        for (j = 0; j < jc; j++)
            bone[j] = (int)j;
        count = (uint32_t)jc;
    } else {
        fail(r->why, r->whyLen,
             "%zu of the skin's %zu joints are named bone_NN; name all of them so or none", named,
             jc);
        goto done;
    }
    const IcoJsonNode *ibmRef = ico_json_get(skin, "inverseBindMatrices");
    if (ibmRef) {
        Acc a;
        if (acc_open(r, ibmRef, "inverseBindMatrices", &a) != 0)
            goto done;
        if (a.ct != CT_FLOAT || a.nc != 16) {
            acc_reject(r, &a, "inverseBindMatrices", "float MAT4");
            goto done;
        }
        if (a.count < jc) {
            fail(r->why, r->whyLen, "inverseBindMatrices has %llu matrices for %zu joints",
                 (unsigned long long)a.count, jc);
            goto done;
        }
        if (!(ibm = acc_floats(r, &a, 16)))
            goto done;
    }
    GltfSkin *sk = &out->skin;
    sk->invBind = malloc(count * sizeof(*sk->invBind));
    sk->parent = malloc(count * sizeof(int));
    sk->names = calloc(count, sizeof(*sk->names));
    if (!sk->invBind || !sk->parent || !sk->names) {
        fail(r->why, r->whyLen, "out of memory");
        goto done;
    }
    sk->count = count;
    for (uint32_t b = 0; b < count; b++) {
        gltf_Mat4Identity(sk->invBind[b]);
        sk->parent[b] = -1;
    }
    for (j = 0; j < jc; j++) {
        int b = bone[j];
        if (ibm)
            memcpy(sk->invBind[b], ibm + j * 16, 16 * sizeof(float));
        const char *nm = ico_json_str(ico_json_get(ico_json_at(nodes, jn[j]), "name"));
        if (nm)
            snprintf(sk->names[b], sizeof(sk->names[b]), "%s", nm);
        for (int p = r->parentOf[jn[j]]; p >= 0; p = r->parentOf[p])
            if (nodeJoint[p] >= 0) {
                sk->parent[b] = bone[nodeJoint[p]];
                break;
            }
    }
    *jointCount = jc;
    rc = 0;
done:
    free(jn);
    free(nodeJoint);
    free(ibm);
    return rc;
}

static int read_prims(Rd *r, const IcoJsonNode *mesh, GltfDoc *out, const int *jointToBone,
                      size_t jointCount)
{
    const IcoJsonNode *prims = ico_json_get(mesh, "primitives");
    size_t pc = ico_json_count(prims);
    if (!prims || prims->type != ICO_JSON_ARR || pc == 0)
        return fail(r->why, r->whyLen, "the mesh has no primitives");
    if (pc > GLTF_MAX_PRIMS)
        return fail(r->why, r->whyLen, "the mesh has %zu primitives (at most %u)", pc,
                    GLTF_MAX_PRIMS);
    /* first pass: modes, POSITION, the caps */
    uint64_t verts = 0, idxs = 0;
    size_t i = 0;
    char what[64];
    for (const IcoJsonNode *p = prims->child; p; p = p->next, i++) {
        uint64_t mode, n;
        if (get_uint(p, "mode", 4, &mode) != 0)
            return fail(r->why, r->whyLen, "primitive %zu's mode is not a number", i);
        if (mode != 4)
            return fail(r->why, r->whyLen,
                        "primitive %zu has mode %llu; only triangle lists (mode 4) are supported",
                        i, (unsigned long long)mode);
        const IcoJsonNode *attrs = ico_json_get(p, "attributes");
        const IcoJsonNode *pos = ico_json_get(attrs, "POSITION");
        if (!pos)
            return fail(r->why, r->whyLen, "primitive %zu has no POSITION", i);
        snprintf(what, sizeof(what), "POSITION of primitive %zu", i);
        if (acc_count(r, pos, what, &n) != 0)
            return -1;
        verts += n;
        if (verts > GLTF_MAX_VERTICES)
            return fail(r->why, r->whyLen, "the mesh has more than %u vertices", GLTF_MAX_VERTICES);
        const IcoJsonNode *ind = ico_json_get(p, "indices");
        if (ind) {
            snprintf(what, sizeof(what), "indices of primitive %zu", i);
            if (acc_count(r, ind, what, &n) != 0)
                return -1;
            idxs += n;
            if (idxs > GLTF_MAX_INDICES)
                return fail(r->why, r->whyLen, "the mesh has more than %u indices",
                            GLTF_MAX_INDICES);
        }
    }
    out->prims = calloc(pc, sizeof(GltfPrim));
    if (!out->prims)
        return fail(r->why, r->whyLen, "out of memory");
    out->primCount = (uint32_t)pc;
    i = 0;
    for (const IcoJsonNode *p = prims->child; p; p = p->next, i++) {
        GltfPrim *g = &out->prims[i];
        const IcoJsonNode *attrs = ico_json_get(p, "attributes");
        Acc a;
        snprintf(what, sizeof(what), "POSITION of primitive %zu", i);
        if (acc_open(r, ico_json_get(attrs, "POSITION"), what, &a) != 0)
            return -1;
        if (a.ct != CT_FLOAT || a.nc != 3)
            return acc_reject(r, &a, what, "float VEC3");
        g->vertexCount = (uint32_t)a.count;
        if (!(g->pos = acc_floats(r, &a, 3)))
            return -1;

        static const struct {
            const char *name;
            int nc, nc2, floatish;
            size_t off;
            const char *want;
        } at[] = {
            {"NORMAL", 3, 3, 0, offsetof(GltfPrim, nrm), "float VEC3"},
            {"TEXCOORD_0", 2, 2, 1, offsetof(GltfPrim, uv),
             "VEC2 float, normalized ubyte or normalized ushort"},
            {"COLOR_0", 3, 4, 1, offsetof(GltfPrim, col),
             "VEC3 or VEC4 float, normalized ubyte or normalized ushort"},
            {"WEIGHTS_0", 4, 4, 1, offsetof(GltfPrim, weights),
             "VEC4 float, normalized ubyte or normalized ushort"},
        };

        for (size_t k = 0; k < sizeof(at) / sizeof(at[0]); k++) {
            const IcoJsonNode *ref = ico_json_get(attrs, at[k].name);
            if (!ref)
                continue;
            snprintf(what, sizeof(what), "%s of primitive %zu", at[k].name, i);
            if (acc_open(r, ref, what, &a) != 0)
                return -1;
            int okType = at[k].floatish ? acc_is_floatish(&a) : a.ct == CT_FLOAT;
            if (!okType || (a.nc != at[k].nc && a.nc != at[k].nc2))
                return acc_reject(r, &a, what, at[k].want);
            if (a.count != g->vertexCount)
                return fail(r->why, r->whyLen, "%s has %llu entries for %u vertices", what,
                            (unsigned long long)a.count, g->vertexCount);
            float *f = acc_floats(r, &a, at[k].nc);
            if (!f)
                return -1;
            memcpy((char *)g + at[k].off, &f, sizeof(f));
        }
        const IcoJsonNode *jref = ico_json_get(attrs, "JOINTS_0");
        if (!jref != !g->weights)
            return fail(r->why, r->whyLen, "primitive %zu has %s without %s", i,
                        jref ? "JOINTS_0" : "WEIGHTS_0", jref ? "WEIGHTS_0" : "JOINTS_0");
        if (jref) {
            if (!jointToBone)
                return fail(r->why, r->whyLen,
                            "primitive %zu has JOINTS_0 but the mesh node has no skin", i);
            snprintf(what, sizeof(what), "JOINTS_0 of primitive %zu", i);
            if (acc_open(r, jref, what, &a) != 0)
                return -1;
            if ((a.ct != CT_UBYTE && a.ct != CT_USHORT) || a.nc != 4)
                return acc_reject(r, &a, what, "VEC4 ubyte or ushort");
            if (a.count != g->vertexCount)
                return fail(r->why, r->whyLen, "%s has %llu entries for %u vertices", what,
                            (unsigned long long)a.count, g->vertexCount);
            g->joints = malloc((size_t)a.count * 4);
            if (!g->joints)
                return fail(r->why, r->whyLen, "out of memory");
            for (uint64_t v = 0; v < a.count; v++)
                for (int c = 0; c < 4; c++) {
                    uint32_t jv = acc_u(&a, v, c);
                    if (jv >= jointCount)
                        return fail(r->why, r->whyLen, "%s: vertex %llu uses joint %u of %zu", what,
                                    (unsigned long long)v, jv, jointCount);
                    g->joints[v * 4 + c] = (uint8_t)jointToBone[jv];
                }
        }
        const IcoJsonNode *iref = ico_json_get(p, "indices");
        if (iref) {
            snprintf(what, sizeof(what), "indices of primitive %zu", i);
            if (acc_open(r, iref, what, &a) != 0)
                return -1;
            if ((a.ct != CT_UBYTE && a.ct != CT_USHORT && a.ct != CT_UINT) || a.nc != 1)
                return acc_reject(r, &a, what, "SCALAR ubyte, ushort or uint");
            if (a.count % 3)
                return fail(r->why, r->whyLen, "primitive %zu has %llu indices (a multiple of 3)",
                            i, (unsigned long long)a.count);
            g->indexCount = (uint32_t)a.count;
            g->idx = malloc((size_t)a.count * sizeof(uint32_t));
            if (!g->idx)
                return fail(r->why, r->whyLen, "out of memory");
            for (uint64_t v = 0; v < a.count; v++) {
                g->idx[v] = acc_u(&a, v, 0);
                if (g->idx[v] >= g->vertexCount)
                    return fail(r->why, r->whyLen,
                                "primitive %zu: index %u is past its %u vertices", i, g->idx[v],
                                g->vertexCount);
            }
        } else if (g->vertexCount % 3) {
            return fail(r->why, r->whyLen,
                        "primitive %zu has no indices and %u vertices (a multiple of 3)", i,
                        g->vertexCount);
        }
        const char *nm = ico_json_str(ico_json_get(ico_json_get(p, "extras"), "name"));
        if (nm && !(g->name = dup_str(nm)))
            return fail(r->why, r->whyLen, "out of memory");
    }
    return 0;
}

static int read_doc(Rd *r, GltfDoc *out)
{
    /* the container */
    const char *jsonText = (const char *)r->file;
    size_t jsonLen = r->fileLen;
    if (r->fileLen >= 4 && ico_le32(r->file) == GLB_MAGIC) {
        if (r->fileLen < 20)
            return fail(r->why, r->whyLen, "the GLB is truncated (%zu bytes)", r->fileLen);
        uint32_t version = ico_le32(r->file + 4), length = ico_le32(r->file + 8);
        if (version != 2)
            return fail(r->why, r->whyLen, "the GLB is version %u (2 is supported)", version);
        if (length > r->fileLen || length < 20)
            return fail(r->why, r->whyLen,
                        "the GLB is truncated: its header says %u bytes, the file has %zu", length,
                        r->fileLen);
        uint32_t clen = ico_le32(r->file + 12), ctype = ico_le32(r->file + 16);
        if (ctype != GLB_JSON)
            return fail(r->why, r->whyLen, "the GLB's first chunk is not JSON");
        if (clen > length - 20)
            return fail(r->why, r->whyLen, "the GLB's JSON chunk is truncated");
        jsonText = (const char *)r->file + 20;
        jsonLen = clen;
        /* the JSON chunk may end in padding spaces; trailing NULs are not
         * JSON, trim them as some writers pad with them */
        while (jsonLen && jsonText[jsonLen - 1] == '\0')
            jsonLen--;
        uint64_t at = 20 + (uint64_t)clen;
        if (at + 8 <= length) {
            uint32_t blen = ico_le32(r->file + at), btype = ico_le32(r->file + at + 4);
            if (btype == GLB_BIN) {
                if (blen > length - at - 8)
                    return fail(r->why, r->whyLen, "the GLB's BIN chunk is truncated");
                r->glbBin = r->file + at + 8;
                r->glbBinLen = blen;
            }
        }
    }
    if (ico_json_parse(jsonText, jsonLen, &r->j) != 0)
        return fail(r->why, r->whyLen, "bad JSON at byte %zu: %s", r->j.errorAt, r->j.error);
    r->root = r->j.root;
    if (r->root->type != ICO_JSON_OBJ)
        return fail(r->why, r->whyLen, "the JSON is not an object");
    const IcoJsonNode *asset = ico_json_get(r->root, "asset");
    const char *ver = ico_json_str(ico_json_get(asset, "version"));
    if (!ver)
        return fail(r->why, r->whyLen, "no asset.version: not a glTF file");
    if (strncmp(ver, "2.", 2) != 0)
        return fail(r->why, r->whyLen, "glTF version %s (2.x is supported)", ver);
    const IcoJsonNode *req = ico_json_get(r->root, "extensionsRequired");
    if (ico_json_count(req)) {
        const char *e = ico_json_str(ico_json_at(req, 0));
        return fail(r->why, r->whyLen, "the file needs the extension %s, which is not supported",
                    e ? e : "?");
    }
    if (load_buffers(r) != 0 || build_hierarchy(r) != 0)
        return -1;
    /* the mesh node */
    int64_t mn;
    if (find_mesh_node(r, &mn) != 0)
        return -1;
    const IcoJsonNode *meshes = ico_json_get(r->root, "meshes");
    uint64_t mi = 0;
    const IcoJsonNode *skinRef = NULL;
    if (mn >= 0) {
        const IcoJsonNode *node = ico_json_at(ico_json_get(r->root, "nodes"), (size_t)mn);
        if (ico_json_u64(ico_json_get(node, "mesh"), &mi) != 0)
            return fail(r->why, r->whyLen, "node %lld has a bad mesh", (long long)mn);
        skinRef = ico_json_get(node, "skin");
        double w[16];
        if (node_world(r, (size_t)mn, w) != 0)
            return -1;
        m4_from_d(out->nodeMatrix, w);
    }
    if (mi >= ico_json_count(meshes))
        return fail(r->why, r->whyLen, "the file has no mesh");
    const IcoJsonNode *mesh = ico_json_at(meshes, (size_t)mi);
    const char *name = ico_json_str(ico_json_get(mesh, "name"));
    if (name && !(out->meshName = dup_str(name)))
        return fail(r->why, r->whyLen, "out of memory");
    int *jointToBone = NULL;
    size_t jointCount = 0;
    int rc = 0;
    if (skinRef) {
        uint64_t si;
        if (ico_json_u64(skinRef, &si) != 0)
            rc = fail(r->why, r->whyLen, "the mesh node's skin is not an index");
        else
            rc = read_skin(r, si, out, &jointToBone, &jointCount);
    }
    if (rc == 0)
        rc = read_prims(r, mesh, out, jointToBone, jointCount);
    free(jointToBone);
    if (rc != 0)
        return -1;
    const IcoJsonNode *extras = ico_json_get(asset, "extras");
    if (extras) {
        Sb s = {0};
        sb_jvalue(&s, extras);
        if (s.oom) {
            free(s.p);
            return fail(r->why, r->whyLen, "out of memory");
        }
        out->extrasText = s.p;
    }
    return 0;
}

int gltf_Read(const char *path, GltfDoc *out, char *why, size_t whyLen)
{
    if (!out)
        return fail(why, whyLen, "no document");
    gltf_DocInit(out);
    if (!path)
        return fail(why, whyLen, "no path");
    Rd r;
    memset(&r, 0, sizeof(r));
    r.path = path;
    r.why = why;
    r.whyLen = whyLen;
    int rc = read_file(path, &r.file, &r.fileLen, GLTF_MAX_FILE_BYTES, why, whyLen);
    if (rc == 0)
        rc = read_doc(&r, out);
    ico_json_free(&r.j);
    for (size_t i = 0; i < r.bufCount; i++)
        free(r.bufs[i].own);
    free(r.bufs);
    free(r.parentOf);
    free(r.file);
    if (rc != 0) {
        gltf_Free(out);
        return -1;
    }
    return 0;
}

void gltf_Free(GltfDoc *doc)
{
    if (!doc)
        return;
    for (uint32_t i = 0; doc->prims && i < doc->primCount; i++) {
        GltfPrim *p = &doc->prims[i];
        free(p->pos);
        free(p->nrm);
        free(p->uv);
        free(p->col);
        free(p->joints);
        free(p->weights);
        free(p->idx);
        free((void *)(uintptr_t)p->name);
    }
    free(doc->prims);
    free((void *)(uintptr_t)doc->meshName);
    free(doc->skin.invBind);
    free(doc->skin.parent);
    free(doc->skin.names);
    free(doc->extrasText);
    gltf_DocInit(doc);
}
