/* rd_fixture_make.c: a fixture from a frame dump, and the window check of a
 * fixture against the dump it was made from (rd_fixture.h,
 * docs/port/TESTING_FIXTURES.md).
 *
 * The maker reads the dump's structure only: the head, the commands, the
 * payload ranges the commands name as structure (rd_fx_payload_spans), the
 * texture and target headers, the meshes' counts, index lists and batch
 * records, the tick cameras and the facing tables' sizes.  It never reads a
 * texel, a vertex stream, a generated payload range or a facing bit: those
 * are skipped by their sizes.  The textures' patterns come from the rules
 * (below) on the state of the draws that use them; the meshes' size from the
 * placement matrix of the first draw of each (so the generated geometry
 * reaches the screen where the object was), never from the mesh.
 *
 * The placement rules (rd_fixture.h; opt-in lines of a rule file): @lamps
 * place generates each mesh a non-skinned draw puts into WORK2 about a point
 * that lands on WORK2 (placeAt below: from that draw's placement matrix, the
 * state's scissor and target size and the mesh's ordinal among them, nothing
 * else); @fans off generates the CPU-projected prims drawn into WORK2 beside
 * the target.  Neither reads a vertex. */
#include "rd_fixture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_velocity.h"

/* ------------------------------------------------------------ the rules */

const char rd_fx_default_rules[] =
    "# pattern  conditions (all must hold; !c: must not); the first match wins\n"
    "glyphs     sheet\n"
    "ramp       r8\n"
    "date_split date\n"
    "alpha_ramp ate\n"
    "bright     additive\n"
    "gradient   abe\n"
    "checker    sprite\n"
    "grey       unused\n"
    "checker\n";

enum {
    C_ATE = 1u << 0,      /* drawn with the alpha test on (not ALWAYS) */
    C_DATE = 1u << 1,     /* drawn under DATE */
    C_ADD = 1u << 2,      /* drawn with an additive blend (Cs added to Cd) */
    C_ABE = 1u << 3,      /* drawn with blending on */
    C_SCREEN = 1u << 4,   /* by a CPU-projected screen prim */
    C_SPRITE = 1u << 5,   /* by a screen rectangle (HUD, effects) */
    C_TEXT = 1u << 6,     /* by glyph quads */
    C_VU = 1u << 7,       /* by a VU draw */
    C_USED = 1u << 8,     /* by any draw */
    C_R8 = 1u << 9,       /* format R8 */
    C_SHEET = 1u << 10,   /* format SHEET (a font page) */
    C_RGBA8 = 1u << 11,   /* format RGBA8 */
    C_MIP = 1u << 12,     /* with GS levels */
    C_FBA = 1u << 13,     /* drawn with FBA (alpha's top bit forced) */
    C_TCC_RGB = 1u << 14, /* drawn with TCC RGB (the texture's alpha unused) */
};

static const struct {
    const char *name;
    uint32_t bit;
} s_conds[] = {{"ate", C_ATE},       {"date", C_DATE},     {"additive", C_ADD}, {"abe", C_ABE},
               {"screen", C_SCREEN}, {"sprite", C_SPRITE}, {"text", C_TEXT},    {"vu", C_VU},
               {"used", C_USED},     {"r8", C_R8},         {"sheet", C_SHEET},  {"rgba8", C_RGBA8},
               {"mip", C_MIP},       {"fba", C_FBA},       {"rgb", C_TCC_RGB}};

#define RULES_MAX 64

typedef struct Rule {
    char pattern[32];
    uint32_t must, mustNot;
} Rule;

/* the next blank-separated word of *s (NUL-terminated in place), NULL at the end */
static char *word(char **s)
{
    char *p = *s;
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    if (*p == '\0') {
        *s = p;
        return NULL;
    }
    char *w = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r') {
        p++;
    }
    if (*p) {
        *p++ = '\0';
    }
    *s = p;
    return w;
}

enum { PLACE_LAMPS = 1u << 0, PLACE_FANS_OFF = 1u << 1, PLACE_SCENE = 1u << 2 };

static bool parseRules(const char *text, Rule *out, uint32_t *n, uint32_t *place, char *why,
                       size_t whySize)
{
    *n = 0;
    *place = 0;
    const char *p = text;
    int line = 0;
    while (*p) {
        const char *e = strchr(p, '\n');
        const size_t len = e ? (size_t)(e - p) : strlen(p);
        char buf[256];
        line++;
        if (len >= sizeof(buf)) {
            snprintf(why, whySize, "rule line %d is too long", line);
            return false;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        p = e ? e + 1 : p + len;
        char *hash = strchr(buf, '#');
        if (hash) {
            *hash = '\0';
        }
        char *save = buf;
        char *tok = word(&save);
        if (!tok) {
            continue;
        }
        if (tok[0] == '@') {
            /* a placement rule: "@lamps place", "@fans off", "@scene place" */
            const char *arg = word(&save);
            if (strcmp(tok, "@lamps") == 0 && arg && strcmp(arg, "place") == 0 && !word(&save)) {
                *place |= PLACE_LAMPS;
            } else if (strcmp(tok, "@scene") == 0 && arg && strcmp(arg, "place") == 0 &&
                       !word(&save)) {
                *place |= PLACE_SCENE;
            } else if (strcmp(tok, "@fans") == 0 && arg && strcmp(arg, "off") == 0 &&
                       !word(&save)) {
                *place |= PLACE_FANS_OFF;
            } else {
                snprintf(why, whySize, "rule line %d: no placement rule '%s %s'", line, tok,
                         arg ? arg : "");
                return false;
            }
            continue;
        }
        if (*n >= RULES_MAX) {
            snprintf(why, whySize, "more than %d rules", RULES_MAX);
            return false;
        }
        Rule *r = &out[(*n)++];
        memset(r, 0, sizeof(*r));
        if (!rd_fx_pattern_known(tok) || strlen(tok) >= sizeof(r->pattern)) {
            snprintf(why, whySize, "rule line %d: no pattern '%s'", line, tok);
            return false;
        }
        snprintf(r->pattern, sizeof(r->pattern), "%s", tok);
        while ((tok = word(&save)) != NULL) {
            const bool neg = tok[0] == '!';
            const char *name = neg ? tok + 1 : tok;
            uint32_t bit = 0;
            if (strcmp(name, "unused") == 0) {
                bit = C_USED;
                r->mustNot |= neg ? 0u : bit;
                r->must |= neg ? bit : 0u;
                continue;
            }
            for (size_t k = 0; k < sizeof(s_conds) / sizeof(s_conds[0]); k++) {
                if (strcmp(s_conds[k].name, name) == 0) {
                    bit = s_conds[k].bit;
                }
            }
            if (!bit) {
                snprintf(why, whySize, "rule line %d: no condition '%s'", line, name);
                return false;
            }
            if (neg) {
                r->mustNot |= bit;
            } else {
                r->must |= bit;
            }
        }
    }
    if (*n == 0) {
        snprintf(why, whySize, "no rule");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------ the dump's map */

typedef struct DTex {
    uint32_t hdr[8];
    size_t texOff, texLen, chainOff, chainLen;
    uint32_t levels;
    uint32_t flags;
} DTex;

typedef struct DMesh {
    uint32_t id, nv, qpv, ni, nb;
    size_t streamOff, idxOff, batOff;
    int32_t sizeExp;
    bool sized;
    uint32_t bones;
    bool placed;       /* @lamps / @scene place: generated about centre, not the origin */
    int32_t centre[3]; /* in lattice steps 2^(sizeExp - 3) */
    uint32_t form;     /* @scene place: RD_FX_FORM_CUBE, or a sheet (RD_FX_FORM_SHEET + axis) */
} DMesh;

typedef struct DFacing {
    uint32_t id, nv;
} DFacing;

typedef struct DMap {
    const uint8_t *p;
    size_t n;
    uint32_t ver;
    size_t headBytes;
    size_t listOff[RD_LIST_COUNT];
    uint32_t listN[RD_LIST_COUNT];
    size_t payOff;
    uint32_t psz;
    DTex *tex;
    uint32_t nt;
    size_t tempsOff;
    uint32_t nr;
    DMesh *mesh;
    uint32_t nm;
    size_t tickOff;
    DFacing *fac;
    uint32_t nf;
} DMap;

static void mapFree(DMap *m)
{
    free(m->tex);
    free(m->mesh);
    free(m->fac);
    memset(m, 0, sizeof(*m));
}

typedef struct Cur {
    const uint8_t *p;
    size_t n, o;
    bool bad;
} Cur;

static uint32_t c32(Cur *c)
{
    if (c->bad || c->o + 4 > c->n) {
        c->bad = true;
        return 0;
    }
    const uint32_t v = rd_fx_get32(c->p + c->o);
    c->o += 4;
    return v;
}

static size_t cskip(Cur *c, uint64_t n)
{
    if (c->bad || n > c->n - c->o) {
        c->bad = true;
        return c->o;
    }
    const size_t at = c->o;
    c->o += (size_t)n;
    return at;
}

static uint64_t chainBytes(uint32_t w, uint32_t h, uint32_t levels)
{
    uint64_t n = 0;
    for (uint32_t l = 1; l < levels; l++) {
        w = w > 1 ? w / 2u : 1u;
        h = h > 1 ? h / 2u : 1u;
        n += (uint64_t)w * h * 4u;
    }
    return n;
}

/* the sections of a dump (version RD_FIXTURE_DUMP_MIN .. RD_DUMP_VERSION) */
static bool mapDump(const uint8_t *p, size_t n, DMap *m, char *why, size_t whySize)
{
    memset(m, 0, sizeof(*m));
    m->p = p;
    m->n = n;
    if (n < 32 || memcmp(p, RD_DUMP_MAGIC, 8) != 0) {
        snprintf(why, whySize, "not a frame dump");
        return false;
    }
    m->ver = rd_fx_get32(p + 8);
    if (m->ver < RD_FIXTURE_DUMP_MIN || m->ver > RD_DUMP_VERSION ||
        rd_fx_get32(p + 12) != sizeof(RdCmd) || rd_fx_get32(p + 16) != rd_fx_state_bytes(m->ver) ||
        rd_fx_get32(p + 20) != sizeof(RdScreenVtx)) {
        snprintf(why, whySize, "dump version %u: a fixture is made from versions %u to %u", m->ver,
                 RD_FIXTURE_DUMP_MIN, RD_DUMP_VERSION);
        return false;
    }
    Cur c = {p, n, 0, false};
    m->headBytes = rd_fx_head_bytes(m->ver);
    cskip(&c, m->headBytes);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        m->listN[l] = c32(&c);
        if (m->listN[l] >= (1u << 24)) {
            c.bad = true;
        }
        m->listOff[l] = cskip(&c, (uint64_t)m->listN[l] * sizeof(RdCmd));
    }
    m->psz = c32(&c);
    m->payOff = cskip(&c, m->psz);
    m->nt = c32(&c);
    if (m->nt > 4096) {
        c.bad = true;
    }
    m->tex = calloc(m->nt ? m->nt : 1, sizeof(DTex));
    for (uint32_t i = 0; m->tex && !c.bad && i < m->nt; i++) {
        DTex *t = &m->tex[i];
        for (int k = 0; k < 8; k++) {
            t->hdr[k] = c32(&c);
        }
        if (t->hdr[1] != RD_TEXKIND_IMAGE) {
            continue;
        }
        const uint8_t fmt = (uint8_t)(t->hdr[7] & 0xFFu);
        const uint32_t w = t->hdr[4], h = t->hdr[5];
        if (w == 0 || h == 0 || w > 8192 || h > 8192) {
            c.bad = true;
            break;
        }
        const uint32_t bpp = fmt == RD_TEXEL_R8 || fmt == RD_TEXEL_SHEET ? 1u : 4u;
        t->texLen = (size_t)w * h * bpp;
        t->texOff = cskip(&c, t->texLen);
        t->levels = m->ver >= 9u ? c32(&c) : 1u; /* version 9: the GS levels */
        if (t->levels < 1 || t->levels > 16) {
            c.bad = true;
            break;
        }
        if (t->levels > 1) {
            t->chainLen = (size_t)chainBytes(w, h, t->levels);
            t->chainOff = cskip(&c, t->chainLen);
        }
        t->flags = fmt == RD_TEXEL_R8 ? C_R8 : fmt == RD_TEXEL_SHEET ? C_SHEET : C_RGBA8;
        t->flags |= t->levels > 1 ? C_MIP : 0u;
    }
    m->nr = c32(&c);
    m->tempsOff = cskip(&c, (uint64_t)m->nr * 20u);
    m->nm = c32(&c);
    if (m->nm > 4096) {
        c.bad = true;
    }
    m->mesh = calloc(m->nm ? m->nm : 1, sizeof(DMesh));
    for (uint32_t i = 0; m->mesh && !c.bad && i < m->nm; i++) {
        DMesh *d = &m->mesh[i];
        d->id = c32(&c);
        d->nv = c32(&c);
        d->qpv = c32(&c);
        d->ni = c32(&c);
        d->nb = c32(&c);
        if (d->nv >= (1u << 22) || d->qpv > 8 || d->ni >= (1u << 24) || d->nb >= (1u << 20)) {
            c.bad = true;
            break;
        }
        cskip(&c, 24); /* the name: not kept */
        d->streamOff = cskip(&c, (uint64_t)d->nv * d->qpv * 16u);
        d->idxOff = cskip(&c, (uint64_t)d->ni * 4u);
        d->batOff = cskip(&c, (uint64_t)d->nb * sizeof(RdVuBatchRec));
    }
    if (m->ver >= 14u) {
        m->tickOff = cskip(&c, 4 + 128);
    }
    if (m->ver >= 15u) {
        m->nf = c32(&c);
        if (m->nf > 4096) {
            c.bad = true;
        }
        m->fac = calloc(m->nf ? m->nf : 1, sizeof(DFacing));
        for (uint32_t i = 0; m->fac && !c.bad && i < m->nf; i++) {
            m->fac[i].id = c32(&c);
            m->fac[i].nv = c32(&c);
            if (m->fac[i].nv >= (1u << 22)) {
                c.bad = true;
                break;
            }
            cskip(&c, (m->fac[i].nv + 7u) / 8u);
        }
    }
    if (c.bad || !m->tex || !m->mesh || (m->ver >= 15u && !m->fac) || c.o != n) {
        snprintf(why, whySize, "%s",
                 c.o != n && !c.bad ? "bytes after the dump's end"
                                    : "the dump is truncated or corrupt");
        mapFree(m);
        return false;
    }
    return true;
}

static RdCmd cmdAt(const DMap *m, int l, uint32_t i)
{
    RdCmd c;
    memcpy(&c, m->p + m->listOff[l] + (size_t)i * sizeof(RdCmd), sizeof(c));
    return c;
}

/* ------------------------------------------------------------ the sizes */

/* the 4 qwords at p as a matrix's columns */
static void cols(const uint8_t *p, double m[4][4])
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            float f;
            memcpy(&f, p + 16 * c + 4 * r, 4);
            m[c][r] = isfinite(f) ? (double)f : 0.0;
        }
    }
}

static void xf(const double m[4][4], const double v[4], double o[4])
{
    for (int r = 0; r < 4; r++) {
        o[r] = m[0][r] * v[0] + m[1][r] * v[1] + m[2][r] * v[2] + m[3][r] * v[3];
    }
}

/* the exponent k (a cube of half side 2^k about the model's origin) whose
 * corners reach at least 24 GS pixels from the origin's place on screen
 * through the draw's matrix (and the bone's for a skinned draw); 0 when the
 * origin is behind the eye or no k reaches */
static int32_t sizeAt(const double M[4][4], const double (*B)[4], const double centre[3])
{
    const double zero[4] = {centre[0], centre[1], centre[2], 1};
    double t[4], o[4];
    if (B) {
        xf(B, zero, t);
        xf(M, t, o);
    } else {
        xf(M, zero, o);
    }
    if (!(o[3] > 1e-9)) {
        return 0;
    }
    for (int32_t k = -12; k <= 24; k++) {
        const double s = ldexp(1.0, k);
        double ext = 0.0;
        for (int a = 0; a < 3; a++) {
            double v[4] = {centre[0], centre[1], centre[2], 1}, p[4];
            v[a] += s;
            if (B) {
                xf(B, v, t);
                xf(M, t, p);
            } else {
                xf(M, v, p);
            }
            if (!(p[3] > 1e-9)) {
                continue;
            }
            const double dx = fabs(p[0] / p[3] - o[0] / o[3]), dy = fabs(p[1] / p[3] - o[1] / o[3]);
            ext = dx > ext ? dx : ext;
            ext = dy > ext ? dy : ext;
        }
        if (isfinite(ext) && ext >= 24.0) {
            return k;
        }
    }
    return 0;
}

static int32_t sizeOf(const double M[4][4], const double (*B)[4])
{
    const double origin[3] = {0, 0, 0};
    return sizeAt(M, B, origin);
}

/* A v = b by Cramer's rule; false when A is singular or v is not finite */
static bool solve3(double A[3][3], const double b[3], double v[3])
{
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) -
                       A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                       A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (!isfinite(det) || fabs(det) < 1e-30) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        double T[3][3];
        memcpy(T, A, sizeof(T));
        for (int r = 0; r < 3; r++) {
            T[r][k] = b[r];
        }
        v[k] = (T[0][0] * (T[1][1] * T[2][2] - T[1][2] * T[2][1]) -
                T[0][1] * (T[1][0] * T[2][2] - T[1][2] * T[2][0]) +
                T[0][2] * (T[1][0] * T[2][1] - T[1][1] * T[2][0])) /
               det;
        if (!isfinite(v[k])) {
            return false;
        }
    }
    return true;
}

/* the model-space point v that M sends to (X, Y) at w = W: the three linear
 * equations (Mx - X Mw) . (v, 1) = 0, (My - Y Mw) . (v, 1) = 0,
 * Mw . (v, 1) = W; for a matrix whose w does not depend on the position the
 * third is instead the plane through the origin across the line the first
 * two give.  False when singular or v is not in front of the eye */
static bool solveAt(const double M[4][4], double X, double Y, double W, double v[3])
{
    double A[3][3], b[3];
    for (int k = 0; k < 3; k++) {
        A[0][k] = M[k][0] - X * M[k][3];
        A[1][k] = M[k][1] - Y * M[k][3];
        A[2][k] = M[k][3];
    }
    b[0] = -(M[3][0] - X * M[3][3]);
    b[1] = -(M[3][1] - Y * M[3][3]);
    b[2] = W - M[3][3];
    if (!solve3(A, b, v)) {
        /* w does not depend on the position (no perspective in M): the
         * point of the line through (X, Y) nearest the model's origin */
        A[2][0] = A[0][1] * A[1][2] - A[0][2] * A[1][1];
        A[2][1] = A[0][2] * A[1][0] - A[0][0] * A[1][2];
        A[2][2] = A[0][0] * A[1][1] - A[0][1] * A[1][0];
        b[2] = 0.0;
        if (!solve3(A, b, v)) {
            return false;
        }
    }
    const double wv = M[0][3] * v[0] + M[1][3] * v[1] + M[2][3] * v[2] + M[3][3];
    return wv > 1e-9;
}

/* v rounded to the lattice step 2^(k - 3), in steps; false past
 * RD_FX_PLACE_MAX steps */
static bool roundCentre(const double v[3], int32_t k, int32_t centre[3])
{
    const double step = ldexp(1.0, k - 3);
    for (int a = 0; a < 3; a++) {
        const double q = floor(v[a] / step + 0.5);
        if (!(fabs(q) <= (double)RD_FX_PLACE_MAX)) {
            return false;
        }
        centre[a] = (int32_t)q;
    }
    return true;
}

/* the scissor's box (GS pixels of the target, inclusive, clamped to it) and
 * its place in GS space: false when empty */
static bool scissorBox(const RdStateBlock *st, double *left, double *top, int32_t box[4])
{
    if (st->gsW == 0 || st->gsH == 0) {
        return false;
    }
    int32_t x0 = st->scissor[0], y0 = st->scissor[1], x1 = st->scissor[2], y1 = st->scissor[3];
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= (int32_t)st->gsW ? (int32_t)st->gsW - 1 : x1;
    y1 = y1 >= (int32_t)st->gsH ? (int32_t)st->gsH - 1 : y1;
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    box[0] = x0;
    box[1] = y0;
    box[2] = x1;
    box[3] = y1;
    *left = 2048.0 - (double)(st->gsW / 2u);
    *top = 2048.0 - (double)(st->gsH / 2u);
    return true;
}

/* @lamps place: where mesh number n (0, 1, ...: the meshes drawn into WORK2,
 * in the order of their first such draw) is generated.  Read: the draw's
 * placement matrix M (VU mem 16..19: model to GS pixels, x / w and y / w
 * about 2048), the state's scissor (GS pixels of the target, inclusive) and
 * the target's GS size.  The place on the target: the scissor's box cut in
 * four each way, the meshes on its three inner columns and rows in turn,
 *   X = 2048 - gsW / 2 + x0 + (x1 - x0 + 1) (1 + n mod 3) / 4
 *   Y = 2048 - gsH / 2 + y0 + (y1 - y0 + 1) (1 + (n / 3) mod 3) / 4.
 * The centre v is the model-space point that M sends to (X, Y) at the w of
 * the model's origin (the object keeps its distance from the eye and moves
 * across the picture; an origin behind the eye: at |w|, as far in front):
 * solveAt.  The size k is sizeAt's about v; the centre is then rounded to the
 * cube's lattice step 2^(k - 3) (so every vertex stays an exact float), which
 * moves it by at most half a step, a sixteenth of the cube's half side.  False
 * (the mesh keeps its origin) when the origin's w is 0, the equations are
 * singular, the centre is not in front of the eye, or it is past
 * RD_FX_PLACE_MAX steps. */
static bool placeAt(const uint8_t *pay, const RdCmd *c, const RdStateBlock *st, uint32_t n,
                    int32_t *sizeExp, int32_t centre[3])
{
    double M[4][4];
    cols(pay + c->u[1] + sizeof(RdVuPayload) + 16 * 16, M);
    /* the origin's w; an origin behind the eye: as far in front of it */
    const double w0 = fabs(M[3][3]);
    double left, top;
    int32_t box[4];
    if (!(w0 > 1e-9) || !scissorBox(st, &left, &top, box)) {
        return false;
    }
    const double X = left + box[0] + (double)(box[2] - box[0] + 1) * (1 + n % 3u) / 4.0;
    const double Y = top + box[1] + (double)(box[3] - box[1] + 1) * (1 + (n / 3u) % 3u) / 4.0;
    double v[3];
    if (!solveAt((const double (*)[4])M, X, Y, w0, v)) {
        return false;
    }
    const int32_t k = sizeAt((const double (*)[4])M, NULL, v);
    if (!roundCentre(v, k, centre)) {
        return false;
    }
    *sizeExp = k;
    return true;
}

/* ------------------------------------------------------------ @scene place */

/* M (p, 1) and its GS place: x / w, y / w, Z = 16 z / w (rd_vis_hiz.c's), w */
static void gsOf(const double M[4][4], const double p[3], double g[4])
{
    const double v[4] = {p[0], p[1], p[2], 1.0};
    double h[4];
    xf(M, v, h);
    g[0] = h[0] / h[3];
    g[1] = h[1] / h[3];
    g[2] = 16.0 * h[2] / h[3];
    g[3] = h[3];
}

/* a GS place an exact vertex may have (rd_vis_hiz.c: 0 < x, y < 4094), with
 * 16 pixels to spare, in front of the eye at more than wMin */
static bool gsUsable(const double g[4], double wMin)
{
    return g[3] > wMin && g[0] > 16.0 && g[0] < 4078.0 && g[1] > 16.0 && g[1] < 4078.0 &&
           isfinite(g[2]);
}

/* the last sheet placed (its draw's matrix, its centre in model units, its
 * half side, its two axes) */
typedef struct Sheet {
    bool have;
    double M[4][4];
    double c[3];
    double h;
    int a1, a2;
} Sheet;

typedef struct Scene {
    uint32_t n; /* the meshes the rule has seen */
    uint32_t sheets, behind, left, right, grid, kept;
    Sheet last;
} Scene;

/* sheet corner i (0 A: -,-; 1 B: -,+; 2 C: +,-; 3 D: +,+ along a1, a2: the
 * order rd_fixture.c's sheet strip visits them) at its GS place */
static void sheetCorner(const Sheet *s, int i, double g[4])
{
    double p[3] = {s->c[0], s->c[1], s->c[2]};
    p[s->a1] += i & 2 ? s->h : -s->h;
    p[s->a2] += i & 1 ? s->h : -s->h;
    gsOf((const double (*)[4])s->M, p, g);
}

/* A sheet about (X, Y) at w = W: a square of half side 2^k across two of the
 * model's axes, flat along the third (rd_fixture.c sheetPos).  The flat axis:
 * of the three, in order of the area the other two span on screen at the
 * centre (the matrix's derivatives there), the first for which a k exists
 * whose square reaches R GS pixels along both axes with its four corners
 * usable (gsUsable, w above half the centre's).  The centre is rounded to
 * the step 2^(k - 3) and the corners checked again about it. */
static bool sheetAt(const double M[4][4], double X, double Y, double W, double R, int32_t *sizeExp,
                    int32_t centre[3], uint32_t *form, Sheet *out)
{
    double v[3], g[4];
    if (!solveAt(M, X, Y, W, v)) {
        return false;
    }
    gsOf(M, v, g);
    double J[3][2];
    for (int a = 0; a < 3; a++) {
        J[a][0] = (M[a][0] - g[0] * M[a][3]) / g[3];
        J[a][1] = (M[a][1] - g[1] * M[a][3]) / g[3];
    }
    double score[3];
    for (int f = 0; f < 3; f++) {
        const int a1 = (f + 1) % 3, a2 = (f + 2) % 3;
        score[f] = fabs(J[a1][0] * J[a2][1] - J[a1][1] * J[a2][0]);
    }
    bool tried[3] = {false, false, false};
    for (int t = 0; t < 3; t++) {
        int f = -1;
        for (int k = 0; k < 3; k++) {
            if (!tried[k] && (f < 0 || score[k] > score[f])) {
                f = k;
            }
        }
        tried[f] = true;
        if (!(score[f] > 0.0) || !isfinite(score[f])) {
            continue;
        }
        Sheet s;
        memset(&s, 0, sizeof(s));
        memcpy(s.M, M, sizeof(s.M));
        s.a1 = (f + 1) % 3;
        s.a2 = (f + 2) % 3;
        int32_t kFound = 99;
        for (int32_t k = -12; k <= 24 && kFound == 99; k++) {
            memcpy(s.c, v, sizeof(s.c));
            s.h = ldexp(1.0, k);
            bool usable = true;
            for (int i = 0; i < 4 && usable; i++) {
                double gc[4];
                sheetCorner(&s, i, gc);
                usable = gsUsable(gc, 0.5 * g[3]);
            }
            if (!usable) {
                break; /* a larger square only reaches further */
            }
            double reach[2];
            for (int e = 0; e < 2; e++) {
                double p[3] = {v[0], v[1], v[2]}, ge[4];
                p[e ? s.a2 : s.a1] += s.h;
                gsOf(M, p, ge);
                const double dx = fabs(ge[0] - g[0]), dy = fabs(ge[1] - g[1]);
                reach[e] = dx > dy ? dx : dy;
            }
            if (reach[0] >= R && reach[1] >= R) {
                kFound = k;
            }
        }
        if (kFound == 99 || !roundCentre(v, kFound, centre)) {
            continue;
        }
        const double step = ldexp(1.0, kFound - 3);
        for (int a = 0; a < 3; a++) {
            s.c[a] = centre[a] * step;
        }
        bool usable = true;
        for (int i = 0; i < 4 && usable; i++) {
            double gc[4];
            sheetCorner(&s, i, gc);
            usable = gsUsable(gc, 0.0);
        }
        if (!usable) {
            continue;
        }
        s.have = true;
        *out = s;
        *sizeExp = kFound;
        *form = RD_FX_FORM_SHEET + (uint32_t)f;
        return true;
    }
    return false;
}

/* the signed distance of p from the line a -> b, positive on the side of c */
static double sideOf(const double a[4], const double b[4], const double c[4], double px, double py)
{
    const double ex = b[0] - a[0], ey = b[1] - a[1];
    const double len = sqrt(ex * ex + ey * ey);
    if (!(len > 0.0)) {
        return -1.0;
    }
    const double d = (ex * (py - a[1]) - ey * (px - a[0])) / len;
    const double dc = ex * (c[1] - a[1]) - ey * (c[0] - a[0]);
    return dc < 0.0 ? -d : d;
}

/* A cube behind the last sheet, in quadrant q of it (0: -,-; 1: +,+; 2:
 * +,-; 3: -,+ along its axes: inside its triangle ABC, BCD, CDA or DAB, the
 * order a strip draws them, so a strip of three vertices has the first).
 * The quadrant's centre, half a half side from the sheet's along both axes,
 * at its GS place (X, Y); the cube about the point of this draw's matrix at
 * (X, Y) at w = 2 wFar, 4 wFar, ... (wFar the sheet's farthest corner), the
 * first of six whose cube (its 8 corners, sizeAt's k about the rounded
 * centre, the lattice's reach of 8 steps each way) is wholly farther than
 * 1.25 wFar, whose GS box lies 8 pixels inside the triangle, and whose
 * largest Z is below the triangle's least by twice rd_vis_hiz.c's margin
 * (4096 + Z / 2^17) and 64 more.  So the occlusion test can find it hidden
 * once the sheet is drawn (when the sheet's draw is an occluder). */
static bool behindAt(const Sheet *s, uint32_t q, const double M[4][4], int32_t *sizeExp,
                     int32_t centre[3])
{
    static const int qu[4] = {-1, 1, 1, -1}, qv[4] = {-1, 1, -1, 1};
    static const int tri[4][3] = {{0, 1, 2}, {1, 2, 3}, {2, 3, 0}, {3, 0, 1}};
    double corner[4][4], wFar = 0.0;
    for (int i = 0; i < 4; i++) {
        sheetCorner(s, i, corner[i]);
        wFar = corner[i][3] > wFar ? corner[i][3] : wFar;
    }
    const double *T[3] = {corner[tri[q][0]], corner[tri[q][1]], corner[tri[q][2]]};
    const double zTri = fmin(T[0][2], fmin(T[1][2], T[2][2]));
    double p[3] = {s->c[0], s->c[1], s->c[2]}, gq[4];
    p[s->a1] += qu[q] * 0.5 * s->h;
    p[s->a2] += qv[q] * 0.5 * s->h;
    gsOf((const double (*)[4])s->M, p, gq);
    for (int i = 0; i < 6; i++) {
        double v[3];
        if (!solveAt(M, gq[0], gq[1], ldexp(2.0 * wFar, i), v)) {
            return false;
        }
        const int32_t k = sizeAt(M, NULL, v);
        int32_t cr[3];
        if (!roundCentre(v, k, cr)) {
            return false;
        }
        const double step = ldexp(1.0, k - 3);
        double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0, zMax = 0.0;
        bool ok = true;
        for (int j = 0; j < 8 && ok; j++) {
            const double pc[3] = {(cr[0] + (j & 1 ? 8 : -8)) * step,
                                  (cr[1] + (j & 2 ? 8 : -8)) * step,
                                  (cr[2] + (j & 4 ? 8 : -8)) * step};
            double gc[4];
            gsOf(M, pc, gc);
            ok = gsUsable(gc, 1.25 * wFar);
            x0 = j == 0 || gc[0] < x0 ? gc[0] : x0;
            x1 = j == 0 || gc[0] > x1 ? gc[0] : x1;
            y0 = j == 0 || gc[1] < y0 ? gc[1] : y0;
            y1 = j == 0 || gc[1] > y1 ? gc[1] : y1;
            zMax = j == 0 || gc[2] > zMax ? gc[2] : zMax;
        }
        for (int j = 0; j < 4 && ok; j++) {
            const double px = j & 1 ? x1 : x0, py = j & 2 ? y1 : y0;
            for (int e = 0; e < 3 && ok; e++) {
                ok = sideOf(T[e], T[(e + 1) % 3], T[(e + 2) % 3], px, py) >= 8.0;
            }
        }
        const double margin = 4096.0 + fabs(zMax) / 131072.0;
        if (ok && zMax + 2.0 * margin + 64.0 < zTri) {
            memcpy(centre, cr, sizeof(cr));
            *sizeExp = k;
            return true;
        }
    }
    return false;
}

/* @scene place: mesh number n (0, 1, ...: the meshes whose first draw is a
 * non-skinned draw of list 0, in that order) is generated by its role,
 * n mod 8, at grid point g = (n / 8) mod 9 (the scissor's box cut in four
 * each way, its three inner columns and rows in turn, as @lamps place).
 * Read: that draw's placement matrix M (VU mem 16..19), the state's
 * scissor and the target's GS size, n, and for role 1 to 4 the last sheet
 * this rule placed (its draw's matrix, its centre, size and axes).
 *   0     a sheet about g at the origin's w (|w|): sheetAt, R a third of the
 *         target's smaller side (its lattice triangles then span far more
 *         than the 64th of the frame an occluder's must, rd_vis_hiz.c);
 *   1..4  a cube behind the last sheet, in its quadrant n mod 8 - 1:
 *         behindAt;
 *   5     a cube about the scissor's left edge at g's row (partly outside);
 *   6     a cube 192 GS pixels past the scissor's right edge at g's row
 *         (wholly outside: a cube reaches under 48 pixels, sizeAt);
 *   7     a cube about g.
 * A role that cannot be taken (no sheet yet, no fit, singular equations)
 * falls to a cube about g; the cubes are at the origin's w, sized by sizeAt
 * about their centres.  False (the mesh keeps its origin) as placeAt. */
static bool scenePlace(const uint8_t *pay, const RdCmd *c, const RdStateBlock *st, Scene *sc,
                       int32_t *sizeExp, int32_t centre[3], uint32_t *form)
{
    const uint32_t n = sc->n++;
    double M[4][4];
    cols(pay + c->u[1] + sizeof(RdVuPayload) + 16 * 16, M);
    const double w0 = fabs(M[3][3]);
    double left, top;
    int32_t box[4];
    *form = RD_FX_FORM_CUBE;
    if (!(w0 > 1e-9) || !scissorBox(st, &left, &top, box)) {
        sc->kept++;
        return false;
    }
    const uint32_t role = n % 8u, g = (n / 8u) % 9u;
    double X = left + box[0] + (double)(box[2] - box[0] + 1) * (1 + g % 3u) / 4.0;
    const double Y = top + box[1] + (double)(box[3] - box[1] + 1) * (1 + g / 3u) / 4.0;
    const double side = (double)(st->gsW < st->gsH ? st->gsW : st->gsH);
    if (role == 0 &&
        sheetAt((const double (*)[4])M, X, Y, w0, side / 3.0, sizeExp, centre, form, &sc->last)) {
        sc->sheets++;
        return true;
    }
    if (role >= 1 && role <= 4 && sc->last.have &&
        behindAt(&sc->last, role - 1u, (const double (*)[4])M, sizeExp, centre)) {
        sc->behind++;
        return true;
    }
    if (role == 5) {
        X = left + box[0];
    } else if (role == 6) {
        X = left + box[2] + 1 + 192;
    }
    double v[3];
    if (!solveAt((const double (*)[4])M, X, Y, w0, v)) {
        sc->kept++;
        return false;
    }
    const int32_t k = sizeAt((const double (*)[4])M, NULL, v);
    if (!roundCentre(v, k, centre)) {
        sc->kept++;
        return false;
    }
    *sizeExp = k;
    if (role == 5) {
        sc->left++;
    } else if (role == 6) {
        sc->right++;
    } else {
        sc->grid++;
    }
    return true;
}

/* a VU draw's size from its block: the model matrix (mem 16..19), or for a
 * skinned draw its first bone (the bones after the block) then the common
 * block's world to screen (mem 4..7) */
static int32_t drawSize(const uint8_t *pay, const RdCmd *c)
{
    const uint8_t *blk = pay + c->u[1] + sizeof(RdVuPayload);
    double M[4][4];
    if (c->type == RDC_SKINNED) {
        RdVuPayload h;
        memcpy(&h, pay + c->u[1], sizeof(h));
        if (h.boneQw >= 4) {
            double B[4][4];
            cols(blk + sizeof(RdVuBlock), B);
            cols(blk + 4 * 16, M);
            return sizeOf((const double (*)[4])M, (const double (*)[4])B);
        }
    }
    cols(blk + 16 * 16, M);
    return sizeOf((const double (*)[4])M, NULL);
}

/* ------------------------------------------------------------ making */

typedef struct MakeCtx {
    const uint8_t *src; /* the dump's payload */
    uint8_t *out;       /* the fixture's */
    uint8_t *kept, *gen;
    uint32_t psz;
    uint32_t spans[RD_FX_GEN_COUNT];
    uint32_t textItems;
} MakeCtx;

static bool makeSpan(void *user, const RdCmd *c, const RdFxSpan *s)
{
    (void)c;
    MakeCtx *k = user;
    if (s->role == RD_FX_KEEP) {
        memcpy(k->out + s->off, k->src + s->off, s->len);
        memset(k->kept + s->off, 1, s->len);
    } else if (s->role == RD_FX_TEXT) {
        uint32_t n = 0;
        while (n < s->len && k->src[s->off + n] != '\0') {
            n++;
        }
        memset(k->out + s->off, 'x', n);
        memset(k->kept + s->off, 1, s->len);
        k->textItems++;
    } else {
        memset(k->gen + s->off, 1, s->len);
        k->spans[s->gen]++;
    }
    return true;
}

static int texIndex(const DMap *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->nt; i++) {
        if (m->tex[i].hdr[0] == id) {
            return (int)i;
        }
    }
    return -1;
}

static int meshIndex(const DMap *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->nm; i++) {
        if (m->mesh[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool isAdditive(uint8_t blend)
{
    return blend == RD_BLEND_CS_FIX_ADD_CD || blend == RD_BLEND_CD_FIX_ADD_CS ||
           blend == RD_BLEND_CS_AS_ADD_CD || blend == RD_BLEND_CS_AD_ADD_CD;
}

/* the texture flags from every draw's state, the meshes' sizes and bones,
 * the grid and particle draws' sizes */
static bool walk(DMap *m, RdFxBuf *exps, uint32_t *nexp, uint32_t place, RdFxBuf *offs,
                 uint32_t *placed, uint32_t *notPlaced, uint32_t *noff, Scene *sc)
{
    const uint32_t work2 = (uint32_t)RD_TARGET_WORK2 + 1u; /* rd_target's handle */
    RdStateBlock st;
    memset(&st, 0, sizeof(st)); /* an older block: the fields after it 0, as loaded */
    memcpy(&st, m->p + m->headBytes - 2 * (size_t)rd_fx_state_bytes(m->ver),
           rd_fx_state_bytes(m->ver));
    const uint8_t *pay = m->p + m->payOff;
    uint32_t *seen = NULL, nseen = 0;
    bool ok = true;
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; ok && i < m->listN[l]; i++) {
            const RdCmd c = cmdAt(m, l, i);
            if (rd__cmd_is_state(c.type)) {
                (void)rd__apply_state(&st, &c);
                continue;
            }
            uint32_t how = 0;
            if (c.type == RDC_SCREEN) {
                how = c.b[3] ? C_TEXT : c.b[0] == RD_PRIM_SPRITES ? C_SPRITE : C_SCREEN;
            } else if (c.type >= RDC_MESH && c.type <= RDC_PARTICLES) {
                how = C_VU;
            }
            if (!how) {
                continue;
            }
            if (!rd__dump_cmd_valid(pay, m->psz, &c)) {
                ok = false;
                break;
            }
            const int t = st.ds.texEnabled && st.tex ? texIndex(m, st.tex) : -1;
            if (t >= 0) {
                const RdDrawState *d = &st.ds;
                uint32_t f = how | C_USED;
                f |= d->test.ate && d->test.atst != RD_ATST_ALWAYS ? C_ATE : 0u;
                f |= d->test.date != RD_DATE_OFF ? C_DATE : 0u;
                f |= d->abe ? C_ABE : 0u;
                f |= d->abe && isAdditive(d->blend) ? C_ADD : 0u;
                f |= d->fba ? C_FBA : 0u;
                f |= d->tcc == 0 ? C_TCC_RGB : 0u;
                m->tex[t].flags |= f;
            }
            if ((place & PLACE_FANS_OFF) && how == C_SCREEN && st.color == work2) {
                rd_fx_put32(offs, c.u[0]); /* the sun's fans: beside the target */
                (*noff)++;
            }
            if (c.type == RDC_MESH || c.type == RDC_SKINNED) {
                const int k = meshIndex(m, c.u[0]);
                if (k >= 0) {
                    DMesh *d = &m->mesh[k];
                    if ((place & PLACE_LAMPS) && c.type == RDC_MESH && st.color == work2 &&
                        !d->placed) {
                        /* a lamp: about a point on WORK2 (this draw's
                         * matrix, over the size an earlier draw gave) */
                        if (placeAt(pay, &c, &st, *placed, &d->sizeExp, d->centre)) {
                            d->placed = d->sized = true;
                            (*placed)++;
                        } else {
                            (*notPlaced)++;
                        }
                    }
                    if ((place & PLACE_SCENE) && l == 0 && c.type == RDC_MESH && !d->sized) {
                        /* the scene: about a point this draw's state gives
                         * (its role by its ordinal; scenePlace) */
                        if (scenePlace(pay, &c, &st, sc, &d->sizeExp, d->centre, &d->form)) {
                            d->placed = d->sized = true;
                        }
                    }
                    if (!d->sized) {
                        d->sizeExp = drawSize(pay, &c);
                        d->sized = true;
                    }
                    if (c.type == RDC_SKINNED) {
                        RdVuPayload h;
                        memcpy(&h, pay + c.u[1], sizeof(h));
                        const uint32_t nb = h.boneQw / 4u ? h.boneQw / 4u : 1u;
                        d->bones = d->bones == 0 || nb < d->bones ? nb : d->bones;
                    }
                }
            } else if (c.type == RDC_GRID || c.type == RDC_PARTICLES) {
                bool dup = false;
                for (uint32_t s = 0; s < nseen; s++) {
                    dup = dup || seen[s] == c.u[1];
                }
                if (!dup) {
                    uint32_t *q = realloc(seen, (nseen + 1u) * sizeof(*seen));
                    if (!q) {
                        ok = false;
                        break;
                    }
                    seen = q;
                    seen[nseen++] = c.u[1];
                    rd_fx_put32(exps, c.u[1]);
                    rd_fx_put32(exps, (uint32_t)drawSize(pay, &c));
                    (*nexp)++;
                }
            }
        }
    }
    free(seen);
    return ok && !exps->bad;
}

static void putName(RdFxBuf *b, const char *s)
{
    const uint8_t n = (uint8_t)strlen(s);
    rd_fx_put(b, &n, 1);
    rd_fx_put(b, s, n);
}

bool rd_fixture_make(const uint8_t *dump, size_t n, const char *rules, RdFixture *fx, char *report,
                     size_t reportSize, char *why, size_t whySize)
{
    memset(fx, 0, sizeof(*fx));
    static Rule rl[RULES_MAX];
    uint32_t nrules = 0, place = 0;
    if (!parseRules(rules ? rules : rd_fx_default_rules, rl, &nrules, &place, why, whySize)) {
        return false;
    }
    DMap m;
    if (!mapDump(dump, n, &m, why, whySize)) {
        return false;
    }
    RdFxBuf exps = {0}, offs = {0};
    uint32_t nexp = 0, placed = 0, notPlaced = 0, noff = 0;
    Scene sc;
    memset(&sc, 0, sizeof(sc));
    if (!walk(&m, &exps, &nexp, place, &offs, &placed, &notPlaced, &noff, &sc) || offs.bad) {
        snprintf(why, whySize, "a command's payload range does not hold");
        rd_fx_free(&exps);
        rd_fx_free(&offs);
        mapFree(&m);
        return false;
    }
    const uint32_t scenePlaced = sc.sheets + sc.behind + sc.left + sc.right + sc.grid;
    if (((place & PLACE_LAMPS) && placed == 0) || ((place & PLACE_FANS_OFF) && noff == 0) ||
        ((place & PLACE_SCENE) && scenePlaced == 0)) {
        snprintf(why, whySize,
                 "a placement rule matched nothing (@lamps place: %u meshes placed, %u not; "
                 "@fans off: %u prims; @scene place: %u meshes placed, %u not)",
                 placed, notPlaced, noff, scenePlaced, sc.kept);
        rd_fx_free(&exps);
        rd_fx_free(&offs);
        mapFree(&m);
        return false;
    }
    /* the payload: structure only */
    MakeCtx k;
    memset(&k, 0, sizeof(k));
    k.src = dump + m.payOff;
    k.psz = m.psz;
    k.out = calloc(m.psz ? m.psz : 1u, 1);
    k.kept = calloc(m.psz ? m.psz : 1u, 1);
    k.gen = calloc(m.psz ? m.psz : 1u, 1);
    RdFxBuf lists = {0};
    bool ok = k.out && k.kept && k.gen;
    uint32_t cardsDropped = 0;
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        rd_fx_put32(&lists, m.listN[l]);
        for (uint32_t i = 0; ok && i < m.listN[l]; i++) {
            RdCmd c = cmdAt(&m, l, i);
            uint32_t vuSize = 0;
            ok = rd_fx_payload_spans(k.src, m.psz, &c, m.ver, makeSpan, &k, &vuSize);
            if (ok && vuSize && vuSize < c.u[2] && c.type != RDC_PARTICLES) {
                c.u[2] = vuSize; /* the card verdicts: made again at load */
                cardsDropped++;
            }
            rd_fx_put(&lists, &c, sizeof(c));
        }
    }
    uint32_t clash = 0;
    for (uint32_t i = 0; ok && i < m.psz; i++) {
        clash += k.kept[i] && k.gen[i];
    }
    if (ok && clash) {
        snprintf(why, whySize, "%u payload bytes are both structure and generated", clash);
        ok = false;
    } else if (!ok) {
        snprintf(why, whySize, "a command's payload range does not hold");
    }
    RdFxBuf sk = {0};
    uint32_t perPattern[16] = {0};
    if (ok) {
        rd_fx_put(&sk, dump, m.headBytes);
        rd_fx_put(&sk, lists.p, lists.n);
        rd_fx_put32(&sk, m.psz);
        rd_fx_put(&sk, k.out, m.psz);
        rd_fx_put32(&sk, m.nt);
        for (uint32_t i = 0; i < m.nt; i++) {
            const DTex *t = &m.tex[i];
            for (int w = 0; w < 8; w++) {
                rd_fx_put32(&sk, t->hdr[w]);
            }
            if (t->hdr[1] != RD_TEXKIND_IMAGE) {
                continue;
            }
            const char *pat = rl[nrules - 1].pattern;
            for (uint32_t r = 0; r < nrules; r++) {
                if ((t->flags & rl[r].must) == rl[r].must && (t->flags & rl[r].mustNot) == 0) {
                    pat = rl[r].pattern;
                    break;
                }
            }
            for (int q = 0; rd_fx_pattern_name(q); q++) {
                if (q < 16 && strcmp(rd_fx_pattern_name(q), pat) == 0) {
                    perPattern[q]++;
                }
            }
            putName(&sk, pat);
            rd_fx_put32(&sk, t->levels);
        }
        rd_fx_put32(&sk, m.nr);
        rd_fx_put(&sk, dump + m.tempsOff, (size_t)m.nr * 20u);
        rd_fx_put32(&sk, m.nm);
        for (uint32_t i = 0; i < m.nm; i++) {
            const DMesh *d = &m.mesh[i];
            rd_fx_put32(&sk, d->id);
            rd_fx_put32(&sk, d->nv);
            rd_fx_put32(&sk, d->qpv);
            rd_fx_put32(&sk, d->ni);
            rd_fx_put32(&sk, d->nb);
            putName(&sk, RD_FX_MESH_GEN);
            rd_fx_put32(&sk, (uint32_t)d->sizeExp);
            rd_fx_put32(&sk, d->bones);
            rd_fx_put(&sk, dump + d->idxOff, (size_t)d->ni * 4u);
            rd_fx_put(&sk, dump + d->batOff, (size_t)d->nb * sizeof(RdVuBatchRec));
        }
        if (m.ver >= 14u) {
            rd_fx_put(&sk, dump + m.tickOff, 4 + 128);
        }
        if (m.ver >= 15u) {
            rd_fx_put32(&sk, m.nf);
            for (uint32_t i = 0; i < m.nf; i++) {
                rd_fx_put32(&sk, m.fac[i].id);
                rd_fx_put32(&sk, m.fac[i].nv);
            }
        }
        rd_fx_put32(&sk, nexp);
        rd_fx_put(&sk, exps.p, exps.n);
        if (placed || noff || scenePlaced) {
            /* the placement section (rd_fixture.h): RD_FX_PLACE_TAG as it
             * always was; with @scene place RD_FX_PLACE_TAG2, each mesh's
             * form after its centre */
            const bool scene = (place & PLACE_SCENE) != 0;
            rd_fx_put32(&sk, scene ? RD_FX_PLACE_TAG2 : RD_FX_PLACE_TAG);
            rd_fx_put32(&sk, placed + scenePlaced);
            for (uint32_t i = 0; i < m.nm; i++) {
                const DMesh *d = &m.mesh[i];
                if (d->placed) {
                    rd_fx_put32(&sk, d->id);
                    for (int a = 0; a < 3; a++) {
                        rd_fx_put32(&sk, (uint32_t)d->centre[a]);
                    }
                    if (scene) {
                        rd_fx_put32(&sk, d->form);
                    }
                }
            }
            rd_fx_put32(&sk, noff);
            rd_fx_put(&sk, offs.p, offs.n);
        }
        ok = !sk.bad;
        if (!ok) {
            snprintf(why, whySize, "out of memory");
        }
    }
    if (ok) {
        fx->format = RD_FIXTURE_FORMAT;
        fx->dumpVersion = m.ver;
        fx->skel = sk.p;
        fx->skelBytes = sk.n;
        sk.p = NULL;
        (void)rd_fixture_add_note(fx, "maker", "rd_fixture_tool make (fixture format 1)");
        const char *rt = rules ? rules : rd_fx_default_rules;
        for (const char *p = rt; ok && *p;) {
            const char *e = strchr(p, '\n');
            const size_t len = e ? (size_t)(e - p) : strlen(p);
            char line[200];
            if (len && len < sizeof(line) && p[0] != '#') {
                memcpy(line, p, len);
                line[len] = '\0';
                (void)rd_fixture_add_note(fx, "rule", line);
            }
            p = e ? e + 1 : p + len;
        }
        uint8_t *x = NULL;
        size_t xn = 0;
        ok = rd_fixture_expand(fx, false, &x, &xn, why, whySize);
        if (ok) {
            rd_fx_sha256(x, xn, fx->sha);
            fx->expandedBytes = xn;
        }
        free(x);
    }
    if (ok && report && reportSize) {
        int32_t lo = 0, hi = 0;
        for (uint32_t i = 0; i < m.nm; i++) {
            lo = i == 0 || m.mesh[i].sizeExp < lo ? m.mesh[i].sizeExp : lo;
            hi = i == 0 || m.mesh[i].sizeExp > hi ? m.mesh[i].sizeExp : hi;
        }
        int at = snprintf(report, reportSize,
                          "make: dump version %u; %u textures; meshes %u (cube half sides 2^%d .. "
                          "2^%d); card verdicts dropped on %u draws; %u text items as 'x'; "
                          "generated spans: %u screen prims, %u glyph quads, %u shadow triangle "
                          "sets, %u shadow strips, %u grid strips, %u particles; patterns:",
                          m.ver, m.nt, m.nm, lo, hi, cardsDropped, k.textItems,
                          k.spans[RD_FX_GEN_SCREEN], k.spans[RD_FX_GEN_TEXTQ],
                          k.spans[RD_FX_GEN_SHTRIS], k.spans[RD_FX_GEN_SHSTRIP],
                          k.spans[RD_FX_GEN_GRIDSTRIP], k.spans[RD_FX_GEN_PARTICLE]);
        for (int q = 0; q < 16 && rd_fx_pattern_name(q) && at > 0 && (size_t)at < reportSize; q++) {
            if (perPattern[q]) {
                at += snprintf(report + at, reportSize - (size_t)at, " %s %u",
                               rd_fx_pattern_name(q), perPattern[q]);
            }
        }
        if ((place & (PLACE_LAMPS | PLACE_FANS_OFF)) && at > 0 && (size_t)at < reportSize) {
            at += snprintf(report + at, reportSize - (size_t)at,
                           "; placement: %u meshes drawn into WORK2 generated on it (%u kept their "
                           "origin), %u prims drawn into WORK2 generated beside it",
                           placed, notPlaced, noff);
        }
        if ((place & PLACE_SCENE) && at > 0 && (size_t)at < reportSize) {
            snprintf(report + at, reportSize - (size_t)at,
                     "; scene: %u meshes of list 0 placed (%u sheets, %u behind a sheet, %u "
                     "across the left edge, %u past the right edge, %u on the picture), %u kept "
                     "their origin",
                     scenePlaced, sc.sheets, sc.behind, sc.left, sc.right, sc.grid, sc.kept);
        }
    }
    if (!ok) {
        rd_fixture_free(fx);
    }
    rd_fx_free(&sk);
    rd_fx_free(&lists);
    rd_fx_free(&exps);
    rd_fx_free(&offs);
    free(k.out);
    free(k.kept);
    free(k.gen);
    mapFree(&m);
    return ok;
}

/* ------------------------------------------------------------ the window check */

typedef struct WinSet {
    uint64_t *slot;
    size_t cap, n;
} WinSet;

static uint64_t winHash(const uint8_t *p)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 32; i++) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h ? h : 1u;
}

static bool informative(const uint8_t *p)
{
    uint32_t w[8];
    int distinct = 0;
    for (int i = 0; i < 8; i++) {
        w[i] = rd_fx_get32(p + 4 * i);
        bool seen = false;
        for (int j = 0; j < i; j++) {
            seen = seen || w[j] == w[i];
        }
        distinct += !seen;
    }
    return distinct >= 3;
}

static bool winAdd(WinSet *s, uint64_t h)
{
    if ((s->n + 1) * 2 > s->cap) {
        const size_t cap = s->cap ? s->cap * 2 : 1u << 20;
        uint64_t *q = calloc(cap, sizeof(uint64_t));
        if (!q) {
            return false;
        }
        for (size_t i = 0; i < s->cap; i++) {
            if (s->slot[i]) {
                size_t k = (size_t)(s->slot[i] & (cap - 1));
                while (q[k]) {
                    k = (k + 1) & (cap - 1);
                }
                q[k] = s->slot[i];
            }
        }
        free(s->slot);
        s->slot = q;
        s->cap = cap;
    }
    size_t k = (size_t)(h & (s->cap - 1));
    while (s->slot[k] && s->slot[k] != h) {
        k = (k + 1) & (s->cap - 1);
    }
    if (!s->slot[k]) {
        s->slot[k] = h;
        s->n++;
    }
    return true;
}

static bool winHas(const WinSet *s, uint64_t h)
{
    if (!s->cap) {
        return false;
    }
    size_t k = (size_t)(h & (s->cap - 1));
    while (s->slot[k]) {
        if (s->slot[k] == h) {
            return true;
        }
        k = (k + 1) & (s->cap - 1);
    }
    return false;
}

typedef struct Ranges {
    RdFxBuf b; /* pairs of u64 (offset in the file, length) */
} Ranges;

static void addRange(Ranges *r, size_t off, size_t len)
{
    if (len >= 32) {
        const uint64_t v[2] = {off, len};
        rd_fx_put(&r->b, v, sizeof(v));
    }
}

typedef struct ArtCtx {
    Ranges *r;
    size_t payOff;
} ArtCtx;

static bool artSpan(void *user, const RdCmd *c, const RdFxSpan *s)
{
    (void)c;
    ArtCtx *a = user;
    if (s->role == RD_FX_GEN) {
        addRange(a->r, a->payOff + s->off, s->len);
    }
    return true;
}

/* the art ranges of a mapped dump: texels, GS levels, streams, generated
 * payload spans and (cards) the card verdicts past each VU draw's materials */
static bool artRanges(const DMap *m, Ranges *r, bool cards)
{
    for (uint32_t i = 0; i < m->nt; i++) {
        addRange(r, m->tex[i].texOff, m->tex[i].texLen);
        addRange(r, m->tex[i].chainOff, m->tex[i].chainLen);
    }
    for (uint32_t i = 0; i < m->nm; i++) {
        addRange(r, m->mesh[i].streamOff, (size_t)m->mesh[i].nv * m->mesh[i].qpv * 16u);
    }
    const uint8_t *pay = m->p + m->payOff;
    ArtCtx a = {r, m->payOff};
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < m->listN[l]; i++) {
            const RdCmd c = cmdAt(m, l, i);
            uint32_t vuSize = 0;
            if (!rd_fx_payload_spans(pay, m->psz, &c, m->ver, artSpan, &a, &vuSize)) {
                return false;
            }
            if (cards && vuSize && vuSize < c.u[2]) {
                addRange(r, m->payOff + c.u[1] + vuSize, c.u[2] - vuSize);
            }
        }
    }
    return !r->b.bad;
}

bool rd_fixture_selfcheck(const uint8_t *source, size_t sourceBytes, const uint8_t *expanded,
                          size_t expandedBytes, uint64_t *windows, uint64_t *hits, char *why,
                          size_t whySize)
{
    *windows = *hits = 0;
    DMap ms, me;
    if (!mapDump(source, sourceBytes, &ms, why, whySize)) {
        return false;
    }
    if (!mapDump(expanded, expandedBytes, &me, why, whySize)) {
        mapFree(&ms);
        return false;
    }
    Ranges rs = {{0}}, re = {{0}};
    WinSet set = {0};
    bool ok = artRanges(&ms, &rs, true) && artRanges(&me, &re, false);
    for (size_t i = 0; ok && i < rs.b.n; i += 16) {
        uint64_t v[2];
        memcpy(v, rs.b.p + i, sizeof(v));
        for (uint64_t o = 0; ok && o + 32 <= v[1]; o += 4) {
            const uint8_t *p = source + v[0] + o;
            if (informative(p)) {
                ok = winAdd(&set, winHash(p));
            }
        }
    }
    for (size_t i = 0; ok && i < re.b.n; i += 16) {
        uint64_t v[2];
        memcpy(v, re.b.p + i, sizeof(v));
        for (uint64_t o = 0; o + 32 <= v[1]; o += 4) {
            const uint8_t *p = expanded + v[0] + o;
            if (informative(p)) {
                (*windows)++;
                *hits += winHas(&set, winHash(p));
            }
        }
    }
    if (!ok) {
        snprintf(why, whySize, "out of memory or a corrupt command");
    }
    free(set.slot);
    rd_fx_free(&rs.b);
    rd_fx_free(&re.b);
    mapFree(&ms);
    mapFree(&me);
    return ok;
}
