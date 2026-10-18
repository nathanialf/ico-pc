/* rd_vu_frac_test.c: the pixel side at a fractional scale
 * (port/render/rd_pixel_plan.c, rd_screen_planes.c rd__screen_planes_want),
 * CPU only.
 *
 *   rule    the truth table of rd__vu_pixel_fs, rd__planes_dropped and
 *           rd__screen_planes_want over the profile flag, the preset, the
 *           exact blend, the target's scale on each axis (whole,
 *           fractional), the two filters, the texture (none, without GS
 *           levels, with them), the clip plane and the entry (vu_ps,
 *           vu_texa_ps; sprite_ps, sprite_stq_ps), against the rule written
 *           out here: vu_np_ps only for vu_ps under the flag, the Enhanced
 *           preset, no exact blend, a scale not whole on an axis, not both
 *           filters nearest on a texture, no GS levels, no clip plane; the
 *           plain sprite build where the planes drop, but for STQ
 *   enum    rd__vu_pixel_fs_enum adds the vu_np_ps twin of a vu_ps key at a
 *           fractional scene scale (either axis) and nothing at a whole
 *           one, for vu_texa_ps, with the flag off, at Original or under an
 *           exact blend; the start-up set (rd__enumerate_reachable) at
 *           2.8125 x 2.41071 is the set at 2x and one twin per vu_ps key
 *   cb      rd__fill_draw_cb_extra leaves the DrawCB as fillDrawCB made it
 *           and counts the passes drawn through vu_np_ps
 *           (RdPerfRecord.vuNoPlanes)
 *   prefix  in the embedded SPIR-V: every input of vu_np_ps, vu_texa_np_ps
 *           and their _nodual builds is at location 0, 1 or 2, with the
 *           type and interpolation of that location in vu_ps's input and in
 *           the output of every VU vertex shader that draws (the record
 *           path's vu_main_vs and vu_particle_main_vs, the programs' own
 *           entries and their plain builds): the twins read a prefix of
 *           VuVSOut, 8 located input components against vu_ps's 36 (with
 *           the position, 12 against 40; skipped when the table was built
 *           without shaders) */
#include <stdlib.h>

#include "rd_replay.h"
#include "shaders_gen.h"
#include "../../test/ico_check.h"

/* ------------------------------------------------------------ the rule */

typedef struct Scale {
    float sx, sy;
    int whole;
} Scale;

static const Scale kScales[] = {
    {1.0f, 1.0f, 1}, {2.0f, 2.0f, 1}, {3.0f, 3.0f, 1}, {4.0f, 2.0f, 1},     {2.8125f, 2.41071f, 0},
    {2.0f, 2.5f, 0}, {2.5f, 2.0f, 0}, {1.5f, 1.5f, 0}, {3.75f, 2.8125f, 0},
};

/* a texture record with GS levels at id 1 (rd__tex_rec's slot 0) */
static RdTexRec *s_tex;

static RdTexRec *s_texKept;

static void gsLevelTexture(void)
{
    s_texKept = g_rd.textures;
    s_tex = calloc(RD_MAX_TEXTURES, sizeof(RdTexRec));
    if (!s_tex) {
        return;
    }
    s_tex[0].live = 1;
    s_tex[0].gen = 0;
    s_tex[0].kind = RD_TEXKIND_IMAGE;
    s_tex[0].gsLevels = 3;
    g_rd.textures = s_tex;
}

typedef struct Cell {
    int flag, preset, exact, filt, tex, plane, texa;
    const Scale *sc;
} Cell;

static void ruleCell(const Cell *c, uint32_t *twins, uint32_t *plain)
{
    g_rd.profile.vuPlanesFrac = c->flag != 0;
    g_rd.settings.preset = c->preset ? RD_PRESET_ENHANCED : RD_PRESET_ORIGINAL;
    g_rd.dstMode = (uint8_t)(c->exact ? RD_BLEND_EXACT_STORAGE : RD_BLEND_EXACT_OFF);
    RdTargetRec tc;
    memset(&tc, 0, sizeof(tc));
    tc.w = 688;
    tc.h = 448;
    tc.sx = c->sc->sx;
    tc.sy = c->sc->sy;
    tc.tw = (uint32_t)((float)tc.w * tc.sx + 0.5f);
    tc.th = (uint32_t)((float)tc.h * tc.sy + 0.5f);
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.magFilter = (uint8_t)((c->filt & 1) ? RD_FILTER_LINEAR : RD_FILTER_NEAREST);
    s.ds.minFilter = (uint8_t)((c->filt & 2) ? RD_FILTER_LINEAR : RD_FILTER_NEAREST);
    /* 0 untextured, 1 a texture without GS levels (no record), 2 the
       GS-level record */
    s.ds.texEnabled = (uint8_t)(c->tex != 0);
    s.tex = c->tex == 2 ? 1u : 0u;
    RdDrawPass p;
    memset(&p, 0, sizeof(p));
    p.flags = c->plane ? ICO_DF_PLANE : 0u;
    const bool nearest = c->tex != 0 && c->filt == 0;
    const bool dropped = c->flag && c->preset && !c->sc->whole && !nearest && c->tex != 2;
    const RdFsId today = c->texa ? RD_FS_VU_TEXA : RD_FS_VU;
    const RdFsId want = !c->texa && !c->exact && !c->plane && dropped ? RD_FS_VU_NP : today;
    const RdFsId got = rd__vu_pixel_fs(today, &s, &tc, &p);
    CHECK(got == want,
          "flag %d preset %d exact %d scale %gx%g filters %d tex %d plane %d texa %d: fs %d, "
          "want %d",
          c->flag, c->preset, c->exact, (double)tc.sx, (double)tc.sy, c->filt, c->tex, c->plane,
          c->texa, (int)got, (int)want);
    CHECK(rd__planes_dropped(&s, &tc) == dropped,
          "rd__planes_dropped: flag %d preset %d scale %gx%g filters %d tex %d", c->flag, c->preset,
          (double)tc.sx, (double)tc.sy, c->filt, c->tex);
    /* the screen prims: the plain build where the planes drop, the STQ
       passes keep theirs (exact blend or not: the plain sprite entries have
       their destination-read twins) */
    RdDrawPass sp = p;
    sp.key.fs = c->texa ? RD_FS_SPRITE_STQ : RD_FS_SPRITE;
    const int keep = rd__screen_planes_want(&s, &tc, &sp);
    CHECK(keep == (c->texa || !dropped), "screen planes: fs %u scale %gx%g filters %d tex %d: %d",
          sp.key.fs, (double)tc.sx, (double)tc.sy, c->filt, c->tex, keep);
    *twins += got == RD_FS_VU_NP;
    *plain += !keep;
}

static void testRule(void)
{
    gsLevelTexture();
    CHECK(rd__tex_rec(1) && rd__tex_rec(1)->gsLevels == 3, "the GS-level texture record");
    uint32_t cells = 0, twins = 0, plain = 0;
    Cell c;
    memset(&c, 0, sizeof(c));
    for (c.flag = 0; c.flag < 2; c.flag++) {
        for (c.preset = 0; c.preset < 2; c.preset++) {
            for (c.exact = 0; c.exact < 2; c.exact++) {
                for (size_t sc = 0; sc < sizeof(kScales) / sizeof(kScales[0]); sc++) {
                    c.sc = &kScales[sc];
                    for (c.filt = 0; c.filt < 4; c.filt++) {
                        for (c.tex = 0; c.tex < 3; c.tex++) {
                            for (c.plane = 0; c.plane < 2; c.plane++) {
                                for (c.texa = 0; c.texa < 2; c.texa++) {
                                    ruleCell(&c, &twins, &plain);
                                    cells++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    printf("  rule: %u cells, %u through vu_np_ps, %u screen commands without the planes\n", cells,
           twins, plain);
    g_rd.textures = s_texKept;
    free(s_tex);
    s_tex = NULL;
}

/* ------------------------------------------------------- the start-up set */

static RdPipeKeyInt s_keys[2][RD_PIPELINE_REACHABLE_MAX];

static void testEnum(void)
{
    g_rd.profile.vuPlanesFrac = true;
    g_rd.settings.preset = RD_PRESET_ENHANCED;
    g_rd.dstMode = RD_BLEND_EXACT_OFF;
    RdPipeKeyInt k;
    memset(&k, 0, sizeof(k));
    k.fs = RD_FS_VU;
    RdPipeKeyInt out[4];
    memset(out, 0, sizeof(out));
    out[0] = k;
    g_rd.sceneSx = 2.8125f;
    g_rd.sceneSy = 2.41071f;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 1, &k) == 2 && out[1].fs == RD_FS_VU_NP,
          "no twin of a vu_ps key at a fractional scale");
    CHECK(rd__vu_pixel_fs_enum(out, 4, 2, &k) == 2, "the twin twice");
    RdPipeKeyInt kt = k;
    kt.fs = RD_FS_VU_TEXA;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 2, &kt) == 2, "a vu_texa_ps twin");
    g_rd.sceneSx = g_rd.sceneSy = 2.0f;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 1, &k) == 1, "a twin at 2x");
    g_rd.sceneSy = 2.5f;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 1, &k) == 2, "no twin with one axis fractional");
    g_rd.profile.vuPlanesFrac = false;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 1, &k) == 1, "a twin with the flag off");
    g_rd.profile.vuPlanesFrac = true;
    g_rd.dstMode = RD_BLEND_EXACT_ATTACHMENT;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 1, &k) == 1, "a twin under an exact blend");
    g_rd.dstMode = RD_BLEND_EXACT_OFF;
    g_rd.settings.preset = RD_PRESET_ORIGINAL;
    CHECK(rd__vu_pixel_fs_enum(out, 4, 1, &k) == 1, "a twin at Original");

    /* the whole set: at 2x, then at the phone's 2.8125 x 2.41071 */
    g_rd.settings.preset = RD_PRESET_ENHANCED;
    g_rd.sceneSx = g_rd.sceneSy = 2.0f;
    const uint32_t n0 = rd__enumerate_reachable(s_keys[0], RD_PIPELINE_REACHABLE_MAX);
    g_rd.sceneSx = 2.8125f;
    g_rd.sceneSy = 2.41071f;
    const uint32_t n1 = rd__enumerate_reachable(s_keys[1], RD_PIPELINE_REACHABLE_MAX);
    CHECK(n0 < RD_PIPELINE_REACHABLE_MAX && n1 < RD_PIPELINE_REACHABLE_MAX, "set sizes %u, %u", n0,
          n1);
    uint32_t vu0 = 0, np0 = 0, np1 = 0, base = 0, others = 0;
    for (uint32_t i = 0; i < n0 && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        vu0 += s_keys[0][i].fs == RD_FS_VU;
        np0 += s_keys[0][i].fs == RD_FS_VU_NP;
    }
    for (uint32_t i = 0; i < n1 && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        if (s_keys[1][i].fs != RD_FS_VU_NP) {
            /* every other key is one of the 2x set's */
            bool found = false;
            for (uint32_t j = 0; j < n0 && j < RD_PIPELINE_REACHABLE_MAX && !found; j++) {
                found = rd__pipe_key_equal(&s_keys[1][i], &s_keys[0][j]);
            }
            others += !found;
            continue;
        }
        np1++;
        RdPipeKeyInt b = s_keys[1][i];
        b.fs = RD_FS_VU;
        for (uint32_t j = 0; j < n1 && j < RD_PIPELINE_REACHABLE_MAX; j++) {
            if (rd__pipe_key_equal(&b, &s_keys[1][j])) {
                base++;
                break;
            }
        }
    }
    printf("  start-up set: %u keys at 2x (%u vu_ps), %u at 2.8125x2.41071 (%u vu_np_ps)\n", n0,
           vu0, n1, np1);
    CHECK(np0 == 0, "%u vu_np_ps keys at 2x", np0);
    CHECK(np1 == vu0 && n1 == n0 + vu0, "the fractional set: %u twins of %u vu_ps keys, %u keys",
          np1, vu0, n1);
    CHECK(base == np1 && others == 0, "every twin's vu_ps key in the set (%u of %u), %u keys new",
          base, np1, others);
    g_rd.sceneSx = g_rd.sceneSy = 1.0f;
    g_rd.settings.preset = RD_PRESET_ORIGINAL;
}

/* ------------------------------------------------------------- the DrawCB */

static void testDrawCb(void)
{
    IcoDrawCB cb, before;
    memset(&cb, 0x5A, sizeof(cb));
    memcpy(&before, &cb, sizeof(cb));
    RdDrawPass p;
    memset(&p, 0, sizeof(p));
    p.key.fs = RD_FS_VU;
    const uint32_t n = g_rdPerf.vuNoPlanes;
    rd__fill_draw_cb_extra(NULL, &p, NULL, &cb);
    CHECK(g_rdPerf.vuNoPlanes == n, "a vu_ps pass counted");
    p.key.fs = RD_FS_VU_NP;
    rd__fill_draw_cb_extra(NULL, &p, NULL, &cb);
    CHECK(g_rdPerf.vuNoPlanes == n + 1, "a vu_np_ps pass not counted");
    CHECK(memcmp(&cb, &before, sizeof(cb)) == 0, "the DrawCB changed");
}

/* ------------------------------------------- the prefix, in the SPIR-V */

#define MAX_IO 32

/* a located interface variable: kind 1 float, 2 int; interp bits 1 Flat,
 * 2 NoPerspective, 4 Centroid or Sample */
typedef struct Io {
    uint32_t loc, comps, kind, interp;
} Io;

typedef struct Spv {
    const uint8_t *b; /* the embedded words (no alignment promised) */
    uint32_t n;
} Spv;

static uint32_t W(Spv m, uint32_t i)
{
    uint32_t w;
    memcpy(&w, m.b + (size_t)i * 4u, 4);
    return w;
}

/* type id's component kind and count (a scalar or a vector) */
static void typeOf(Spv m, uint32_t id, uint32_t *kind, uint32_t *comps)
{
    *kind = 0;
    *comps = 0;
    for (uint32_t i = 5; i < m.n;) {
        const uint32_t op = W(m, i) & 0xFFFFu, wc = W(m, i) >> 16;
        if (wc == 0 || i + wc > m.n) {
            return;
        }
        if ((op == 22 || op == 21) && W(m, i + 1) == id) { /* OpTypeFloat, OpTypeInt */
            *kind = op == 22 ? 1u : 2u;
            *comps = 1;
            return;
        }
        if (op == 23 && W(m, i + 1) == id) { /* OpTypeVector */
            uint32_t c;
            typeOf(m, W(m, i + 2), kind, &c);
            *comps = W(m, i + 3);
            return;
        }
        i += wc;
    }
}

/* the pointee of pointer type id */
static uint32_t pointeeOf(Spv m, uint32_t id)
{
    for (uint32_t i = 5; i < m.n;) {
        const uint32_t op = W(m, i) & 0xFFFFu, wc = W(m, i) >> 16;
        if (wc == 0 || i + wc > m.n) {
            break;
        }
        if (op == 32 && W(m, i + 1) == id) { /* OpTypePointer */
            return W(m, i + 3);
        }
        i += wc;
    }
    return 0;
}

/* variable var's location and interpolation (OpDecorate); loc ~0u for none
 * or a dual-source output's second (Index 1) */
static void decorOf(Spv m, uint32_t var, Io *v)
{
    v->loc = ~0u;
    v->interp = 0;
    bool second = false;
    for (uint32_t i = 5; i < m.n;) {
        const uint32_t op = W(m, i) & 0xFFFFu, wc = W(m, i) >> 16;
        if (wc == 0 || i + wc > m.n) {
            break;
        }
        if (op == 71 && wc >= 3 && W(m, i + 1) == var) { /* OpDecorate */
            const uint32_t dec = W(m, i + 2);
            if (dec == 30 && wc >= 4) {
                v->loc = W(m, i + 3); /* Location */
            } else if (dec == 14) {
                v->interp |= 1u; /* Flat */
            } else if (dec == 13) {
                v->interp |= 2u; /* NoPerspective */
            } else if (dec == 16 || dec == 17) {
                v->interp |= 4u; /* Centroid, Sample */
            } else if (dec == 32 && wc >= 4 && W(m, i + 3) != 0) {
                second = true; /* Index 1 */
            }
        }
        i += wc;
    }
    if (second) {
        v->loc = ~0u;
    }
}

/* the located Input (sc 1) or Output (sc 3) variables of module m */
static uint32_t ioOf(Spv m, uint32_t sc, Io *io)
{
    uint32_t n = 0;
    for (uint32_t i = 5; i < m.n;) {
        const uint32_t op = W(m, i) & 0xFFFFu, wc = W(m, i) >> 16;
        if (wc == 0 || i + wc > m.n) {
            break;
        }
        if (op == 59 && wc >= 4 && W(m, i + 3) == sc && n < MAX_IO) { /* OpVariable */
            Io v;
            memset(&v, 0, sizeof(v));
            decorOf(m, W(m, i + 2), &v);
            if (v.loc != ~0u) {
                typeOf(m, pointeeOf(m, W(m, i + 1)), &v.kind, &v.comps);
                io[n++] = v;
            }
        }
        i += wc;
    }
    return n;
}

static bool spvOf(const char *name, Spv *m)
{
    const IcoShaderBlob *b = ico_find_shader(name);
    if (!b || !b->spirv || b->spirv_len < 20 || (b->spirv_len & 3u)) {
        return false;
    }
    m->b = b->spirv;
    m->n = (uint32_t)(b->spirv_len / 4);
    return W(*m, 0) == 0x07230203u;
}

static const Io *ioAt(const Io *io, uint32_t n, uint32_t loc)
{
    for (uint32_t i = 0; i < n; i++) {
        if (io[i].loc == loc) {
            return &io[i];
        }
    }
    return NULL;
}

static bool ioSame(const Io *a, const Io *b)
{
    return a && b && a->kind == b->kind && a->comps == b->comps && a->interp == b->interp;
}

static void testPrefix(void)
{
    static const char *const kTwins[] = {"vu_np_ps", "vu_np_ps_nodual", "vu_texa_np_ps",
                                         "vu_texa_np_ps_nodual"};
    /* every VU vertex shader that draws (not the pre-pass's or the record
       stages', which write the record) */
    static const char *const kVs[] = {"vu_main_vs",
                                      "vu_particle_main_vs",
                                      "vu_prelit_vs",
                                      "vu_lit_vs",
                                      "vu_lit_spec_vs",
                                      "vu_reflect_vs",
                                      "vu_skin_vs",
                                      "vu_skin_spec_vs",
                                      "vu_skin_debug_vs",
                                      "vu_grid_vs",
                                      "vu_grid_lit_vs",
                                      "vu_grid_spec_vs",
                                      "vu_particle_vs",
                                      "vu_prelit_plain_vs",
                                      "vu_lit_plain_vs",
                                      "vu_lit_spec_plain_vs",
                                      "vu_reflect_plain_vs",
                                      "vu_skin_plain_vs",
                                      "vu_skin_spec_plain_vs",
                                      "vu_skin_debug_plain_vs",
                                      "vu_grid_plain_vs",
                                      "vu_grid_lit_plain_vs",
                                      "vu_grid_spec_plain_vs"};
    Spv base;
    if (!spvOf("vu_ps", &base)) {
        printf("  prefix: skipped (no embedded SPIR-V)\n");
        return;
    }
    Io bi[MAX_IO];
    const uint32_t nb = ioOf(base, 1, bi);
    uint32_t baseComps = 0;
    for (uint32_t i = 0; i < nb; i++) {
        baseComps += bi[i].comps;
    }
    CHECK(nb == 10 && baseComps == 36, "vu_ps: %u inputs, %u components (want 10, 36)", nb,
          baseComps);
    /* every vertex shader's outputs at vu_ps's input locations */
    uint32_t nvs = 0;
    for (size_t v = 0; v < sizeof(kVs) / sizeof(kVs[0]); v++) {
        Spv vs;
        if (!spvOf(kVs[v], &vs)) {
            CHECK(0, "%s is not in the shader table", kVs[v]);
            continue;
        }
        Io o[MAX_IO];
        const uint32_t no = ioOf(vs, 3, o);
        for (uint32_t i = 0; i < nb; i++) {
            CHECK(ioSame(ioAt(o, no, bi[i].loc), &bi[i]), "%s: location %u is not vu_ps's input",
                  kVs[v], bi[i].loc);
        }
        nvs++;
    }
    for (size_t t = 0; t < sizeof(kTwins) / sizeof(kTwins[0]); t++) {
        Spv m;
        if (!spvOf(kTwins[t], &m)) {
            CHECK(0, "%s is not in the shader table", kTwins[t]);
            continue;
        }
        Io in[MAX_IO];
        const uint32_t ni = ioOf(m, 1, in);
        uint32_t comps = 0;
        for (uint32_t i = 0; i < ni; i++) {
            comps += in[i].comps;
            CHECK(in[i].loc <= 2, "%s: an input at location %u", kTwins[t], in[i].loc);
            CHECK(ioSame(ioAt(bi, nb, in[i].loc), &in[i]), "%s: location %u is not vu_ps's",
                  kTwins[t], in[i].loc);
        }
        CHECK(ni == 3 && comps == 8, "%s: %u inputs, %u components (want 3, 8)", kTwins[t], ni,
              comps);
        if (t == 0) {
            printf("  prefix: vu_np_ps reads %u inputs, %u components and the position (vu_ps "
                   "%u, %u); vu_ps's inputs match the outputs of %u vertex shaders\n",
                   ni, comps, nb, baseComps, nvs);
        }
    }
}

int main(void)
{
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    testRule();
    testEnum();
    testDrawCb();
    testPrefix();
    rd_shutdown();
    return ico_check_summary("rd_vu_frac_test");
}
