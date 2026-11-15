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
 * reaches the screen where the object was), never from the mesh. */
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

static bool parseRules(const char *text, Rule *out, uint32_t *n, char *why, size_t whySize)
{
    *n = 0;
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
        rd_fx_get32(p + 12) != sizeof(RdCmd) || rd_fx_get32(p + 16) != sizeof(RdStateBlock) ||
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
        t->levels = c32(&c);
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
static int32_t sizeOf(const double M[4][4], const double (*B)[4])
{
    const double zero[4] = {0, 0, 0, 1};
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
            double v[4] = {0, 0, 0, 1}, p[4];
            v[a] = s;
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
static bool walk(DMap *m, RdFxBuf *exps, uint32_t *nexp)
{
    RdStateBlock st;
    memcpy(&st, m->p + m->headBytes - 2 * sizeof(RdStateBlock), sizeof(st));
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
            if (c.type == RDC_MESH || c.type == RDC_SKINNED) {
                const int k = meshIndex(m, c.u[0]);
                if (k >= 0) {
                    DMesh *d = &m->mesh[k];
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
    uint32_t nrules = 0;
    if (!parseRules(rules ? rules : rd_fx_default_rules, rl, &nrules, why, whySize)) {
        return false;
    }
    DMap m;
    if (!mapDump(dump, n, &m, why, whySize)) {
        return false;
    }
    RdFxBuf exps = {0};
    uint32_t nexp = 0;
    if (!walk(&m, &exps, &nexp)) {
        snprintf(why, whySize, "a command's payload range does not hold");
        rd_fx_free(&exps);
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
    }
    if (!ok) {
        rd_fixture_free(fx);
    }
    rd_fx_free(&sk);
    rd_fx_free(&lists);
    rd_fx_free(&exps);
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
