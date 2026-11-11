/* rd_late_pass_test.c: the late passes' predicate on made-up commands, no
 * device (port/render/rd_late_pass.c; docs/port/RENDER_POST.md, "The late
 * passes on SCENE").
 *
 *   rd_late_pass_test
 *
 * In a record-only renderer (rd__init_record_only), with SCENE, WORK0,
 * WORK2, WORK3, WORK3_HELD and FEED128 as the glow and the aura use them:
 *   c1  rd__late_pass_class on each kind of command: state, a NOP and
 *       overlay text wait; the upscale point does not; a clear or a copy of
 *       another target waits, of SCENE (either side) not; a staticBlur sprite
 *       into another target waits (untextured, from another target, from
 *       SCENE in the alpha-only form: the glow's mask), not from SCENE's
 *       colour otherwise, from SCENE's depth view, with DATE, or writing
 *       SCENE's depth; the aura's paste into SCENE lands; the glow's paste
 *       the light plan skips waits, drawn it lands (not under PABE or as a
 *       lerp), in the scan it waits; a draw into WORK2 under a Z test of
 *       SCENE's depth waits (a lamp), writing it, under DATE, PRIM.AA1 or
 *       sampling SCENE not; the reduction, the fog, a screen prim, a grid
 *       on SCENE and a shadow command do not; each refusal's reason
 *   c2  rd__late_pass_scan and rd__late_pass_scan_end over a frame in a glow
 *       room's order (cleanUpFB's two sprites of SCENE's alpha, the band,
 *       the mask, a lamp's mesh into WORK2, a blur pass, the glow's paste,
 *       the aura's sprites, its paste, a screen prim): held (the plan
 *       ignored, RD_LATE_ANY_PLAN: nothing on SCENE skipped) the fill's read
 *       lies at the glow's paste, without the glow's paste at the aura's;
 *       under no plan that holds and switched off at the band
 *       (rd__crack_fill_place's place, as before the hold); held with no
 *       paste in the frame at the walk's end; with a screen prim on SCENE
 *       before the aura's paste at that prim
 *   c3  rd__fx_alpha_only: MODULATE by RGBAQ RGB 0 without a blend only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_late_pass.h"
#include "rd_replay.h"

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

/* the frame the commands' payloads live in */
#define MAX_RECS 32
static RdPostRec s_recs[MAX_RECS];
static uint32_t s_nRec;
static RdFrame s_f;

static uint32_t idOf(RdTargetId t)
{
    return rd_target(t).id;
}

static uint32_t texOf(RdTargetId t, RdTexView v)
{
    return rd_target_texture(rd_target(t), v).id;
}

/* a post sprite of kind with RGBAQ rgba (its record in the frame's payload) */
static RdCmd post(uint8_t kind, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    RdCmd c;
    memset(&c, 0, sizeof(c));
    c.type = RDC_POST_STUB;
    c.b[0] = kind;
    if (s_nRec < MAX_RECS) {
        RdPostRec *p = &s_recs[s_nRec];
        memset(p, 0, sizeof(*p));
        p->rgba[0] = r;
        p->rgba[1] = g;
        p->rgba[2] = b;
        p->rgba[3] = a;
        p->lutOffset = ~0u;
        c.u[1] = s_nRec * (uint32_t)sizeof(RdPostRec);
        s_nRec++;
    }
    return c;
}

static RdCmd ofType(uint8_t type, uint32_t u0, uint32_t u1)
{
    RdCmd c;
    memset(&c, 0, sizeof(c));
    c.type = type;
    c.u[0] = u0;
    c.u[1] = u1;
    return c;
}

/* a state on colour c and depth d, untextured (tex 0) or textured from tex,
 * Z ALWAYS, Z write off, no blend */
static RdStateBlock state(uint32_t c, uint32_t d, uint32_t tex)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.color = c;
    s.depth = d;
    s.gsW = 512;
    s.gsH = 448;
    s.tex = tex;
    s.ds.texEnabled = tex != 0;
    s.ds.texFn = RD_TEXFN_MODULATE;
    s.ds.abe = 0;
    s.ds.zwrite = RD_ZWRITE_OFF;
    s.ds.test.zte = 1;
    s.ds.test.ztst = RD_ZTST_ALWAYS;
    s.ds.test.date = RD_DATE_OFF;
    s.ds.colorMask = 0xF;
    s.ds.colclamp = 1;
    s.ds.pabe = 0;
    s.aa1 = 0;
    return s;
}

static int classOf(const RdCmd *c, const RdStateBlock *st, int skipped, int *why)
{
    return rd__late_pass_class(&s_f, c, st, skipped, why);
}

static void expect(const char *name, const RdCmd *c, const RdStateBlock *st, int skipped,
                   int wantClass, int wantWhy)
{
    int why = -1;
    const int k = classOf(c, st, skipped, &why);
    printf("  %-44s class %d why %d (%s)\n", name, k, why, rd__late_pass_why_text(why));
    CHECK(k == wantClass, "%s: class %d, %d expected", name, k, wantClass);
    CHECK(why == wantWhy, "%s: why %d (%s), %d (%s) expected", name, why,
          rd__late_pass_why_text(why), wantWhy, rd__late_pass_why_text(wantWhy));
}

static void testClass(void)
{
    const uint32_t scene = idOf(RD_TARGET_SCENE), w0 = idOf(RD_TARGET_WORK0);
    const uint32_t w2 = idOf(RD_TARGET_WORK2), w3 = idOf(RD_TARGET_WORK3);
    const uint32_t held = idOf(RD_TARGET_WORK3_HELD), feed = idOf(RD_TARGET_FEED128);
    const uint32_t tScene = texOf(RD_TARGET_SCENE, RD_VIEW_RGBA);
    const uint32_t tDepth = texOf(RD_TARGET_SCENE, RD_VIEW_DEPTH);
    const uint32_t tW3 = texOf(RD_TARGET_WORK3, RD_VIEW_RGBA);
    const uint32_t tFeed = texOf(RD_TARGET_FEED128, RD_VIEW_RGBA);
    CHECK(scene && w0 && w2 && w3 && held && feed && tScene && tDepth && tW3 && tFeed,
          "the named targets and their textures (record-only)");
    CHECK(rd__tex_rec(tScene) != NULL && rd__tex_rec(tFeed) != NULL, "the texture records");
    const RdStateBlock onScene = state(scene, scene, 0), onW2 = state(w2, scene, 0);
    /* state, NOP, text, the upscale point */
    RdCmd c = ofType(RDC_TARGET, 0, 0);
    expect("a state command", &c, &onScene, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    c = ofType(RDC_UPSCALE_POINT, scene, idOf(RD_TARGET_DISPLAY));
    expect("the upscale point", &c, &onScene, 0, RD_LATE_FLUSH, RD_LATE_WHY_UPSCALE);
    c = ofType(RDC_NOP, 0, 0);
    expect("a NOP", &c, &onScene, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    c = ofType(RDC_OVERLAY_TEXT, 0, 0);
    expect("overlay text", &c, &onScene, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    /* clears and copies */
    c = ofType(RDC_CLEAR, w0, 0);
    expect("a clear of WORK0", &c, &onW2, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    c = ofType(RDC_CLEAR, scene, 0);
    expect("a clear of SCENE", &c, &onW2, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    c = ofType(RDC_COPY, w3, held);
    expect("the glow hold's copy WORK3 -> WORK3_HELD", &c, &onW2, 0, RD_LATE_HOLD,
           RD_LATE_WHY_NONE);
    c = ofType(RDC_COPY, scene, w3);
    expect("a copy of SCENE", &c, &onW2, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    c = ofType(RDC_COPY, w3, scene);
    expect("a copy into SCENE", &c, &onW2, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    /* sprites into another target */
    c = post(RD_POST_FLARE, 0, 0, 0, 192);
    expect("the band's fill into WORK2 (untextured)", &c, &onW2, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    RdStateBlock mask = state(w2, scene, tScene);
    c = post(RD_POST_FLARE, 0, 0, 0, 128);
    expect("the mask: SCENE's alpha into WORK2", &c, &mask, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    c = post(RD_POST_FLARE, 128, 128, 128, 128);
    expect("SCENE's colour into WORK2", &c, &mask, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    RdStateBlock decal = mask;
    decal.ds.texFn = RD_TEXFN_DECAL;
    c = post(RD_POST_FLARE, 0, 0, 0, 128);
    expect("SCENE into WORK2 under DECAL", &c, &decal, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    RdStateBlock blended = mask;
    blended.ds.abe = 1;
    blended.ds.blend = RD_BLEND_CS_AS_ADD_CD;
    expect("SCENE into WORK2 with a blend", &c, &blended, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    RdStateBlock depthView = state(w2, scene, tDepth);
    expect("SCENE's depth view into WORK2", &c, &depthView, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    RdStateBlock zw = onW2;
    zw.ds.zwrite = RD_ZWRITE_ON;
    c = post(RD_POST_FLARE, 0, 0, 0, 0);
    expect("a sprite writing SCENE's depth", &c, &zw, 0, RD_LATE_FLUSH, RD_LATE_WHY_DEPTH);
    RdStateBlock date = onW2;
    date.ds.test.date = RD_DATE_DEST_ALPHA_1;
    expect("a sprite under DATE", &c, &date, 0, RD_LATE_FLUSH, RD_LATE_WHY_DEPTH);
    RdStateBlock fromW3 = state(w0, 0, tW3);
    c = post(RD_POST_AURA, 128, 128, 128, 128);
    expect("an aura sprite WORK3 -> WORK0", &c, &fromW3, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    /* on SCENE */
    RdStateBlock paste = state(scene, scene, tFeed);
    paste.ds.abe = 1;
    paste.ds.blend = RD_BLEND_LERP_AS_ALT;
    c = post(RD_POST_AURA, 64, 64, 64, 128);
    expect("the aura's paste FEED128 -> SCENE", &c, &paste, 0, RD_LATE_LAND_AURA, RD_LATE_WHY_NONE);
    RdStateBlock ztest = paste;
    ztest.ds.test.ztst = RD_ZTST_GEQUAL;
    expect("the aura's sprite into SCENE under a Z test", &c, &ztest, 0, RD_LATE_FLUSH,
           RD_LATE_WHY_SCENE);
    RdStateBlock glowPaste = state(scene, scene, tW3);
    glowPaste.ds.abe = 1;
    glowPaste.ds.blend = RD_BLEND_CS_FIX_ADD_CD;
    glowPaste.ds.blendFix = 128;
    c = post(RD_POST_FLARE, 128, 128, 128, 128);
    expect("the glow's paste, skipped by the plan", &c, &glowPaste, 1, RD_LATE_HOLD,
           RD_LATE_WHY_NONE);
    expect("the glow's paste, drawn: its pass", &c, &glowPaste, 0, RD_LATE_LAND_GLOW,
           RD_LATE_WHY_NONE);
    RdStateBlock glowPabe = glowPaste;
    glowPabe.ds.pabe = 1;
    expect("the glow's paste under PABE", &c, &glowPabe, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    RdStateBlock glowLerp = glowPaste;
    glowLerp.ds.blend = RD_BLEND_LERP_FIX;
    expect("a lerp of WORK3 over SCENE", &c, &glowLerp, 0, RD_LATE_FLUSH, RD_LATE_WHY_SCENE);
    expect("the glow's paste in the scan", &c, &glowPaste, -1, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    RdStateBlock fromScene = state(scene, scene, tScene);
    c = post(RD_POST_MOTION_BLUR, 128, 128, 128, 128);
    expect("a sprite on SCENE sampling SCENE (scan)", &c, &fromScene, -1, RD_LATE_FLUSH,
           RD_LATE_WHY_SCENE);
    /* the rest */
    c = post(RD_POST_REDUCTION, 128, 128, 128, 128);
    expect("the reduction", &c, &fromScene, 0, RD_LATE_FLUSH, RD_LATE_WHY_POST);
    c = post(RD_POST_FOG, 128, 128, 128, 128);
    expect("the fog", &c, &onScene, 0, RD_LATE_FLUSH, RD_LATE_WHY_POST);
    c = ofType(RDC_SCREEN, 0, 0);
    expect("a screen prim", &c, &onScene, 0, RD_LATE_FLUSH, RD_LATE_WHY_DRAW);
    /* draws: into another target (the lamps' meshes into WORK2 under a Z
     * test of SCENE's depth) wait; on SCENE, from SCENE, writing SCENE's
     * depth, DATE or PRIM.AA1 do not */
    RdStateBlock lamp = onW2;
    lamp.ds.test.ztst = RD_ZTST_GEQUAL;
    c = ofType(RDC_MESH, 0, 0);
    expect("a lamp's mesh into WORK2, Z tested", &c, &lamp, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    c = ofType(RDC_SCREEN, 0, 0);
    expect("a screen prim into WORK2, Z tested", &c, &lamp, 0, RD_LATE_HOLD, RD_LATE_WHY_NONE);
    RdStateBlock fan = lamp;
    fan.ds.zwrite = RD_ZWRITE_ON;
    expect("a sun's fan into WORK2 writing SCENE's depth", &c, &fan, 0, RD_LATE_FLUSH,
           RD_LATE_WHY_DEPTH);
    RdStateBlock lampDate = lamp;
    lampDate.ds.test.date = RD_DATE_DEST_ALPHA_0;
    expect("a draw into WORK2 under DATE", &c, &lampDate, 0, RD_LATE_FLUSH, RD_LATE_WHY_DEPTH);
    RdStateBlock lampAa1 = lamp;
    lampAa1.aa1 = 1;
    expect("a draw into WORK2 with PRIM.AA1", &c, &lampAa1, 0, RD_LATE_FLUSH, RD_LATE_WHY_DEPTH);
    expect("a screen prim into WORK2 from SCENE", &c, &mask, 0, RD_LATE_FLUSH, RD_LATE_WHY_DRAW);
    c = ofType(RDC_GRID, 0, 0);
    expect("a grid on SCENE", &c, &onScene, 0, RD_LATE_FLUSH, RD_LATE_WHY_DRAW);
    c = ofType(RDC_SHADOW_STRIP, 0, 0);
    expect("a shadow strip", &c, &onScene, 0, RD_LATE_FLUSH, RD_LATE_WHY_DRAW);
}

/* the scan over the DARK room's order: the index the read lands at (-1 none) */
typedef struct Step {
    RdCmd c;
    RdStateBlock st;
} Step;

static int scanAt(const Step *s, int n)
{
    RdLateScan ls;
    rd__late_pass_scan_begin(&ls);
    for (int i = 0; i < n; i++) {
        if (rd__late_pass_scan(&ls, &s_f, &s[i].c, &s[i].st, (uint32_t)i, 7, (uint32_t)i)) {
            return i;
        }
    }
    uint32_t ord = 0, at = 0;
    int l = 0;
    if (rd__late_pass_scan_end(&ls, (uint32_t)n, 12, &ord, &l, &at)) {
        return (int)ord;
    }
    return -1;
}

/* the DARK room's order up to the aura's paste (glow: with the glow's paste
 * the plan would skip), a screen prim on SCENE before the paste (screen) */
static int order(Step *s, bool glow, bool screen, int *band, int *glowAt, int *auraAt)
{
    const uint32_t scene = idOf(RD_TARGET_SCENE), w0 = idOf(RD_TARGET_WORK0);
    const uint32_t w1 = idOf(RD_TARGET_WORK1), w2 = idOf(RD_TARGET_WORK2);
    const uint32_t pad = idOf(RD_TARGET_WORK2_PAD);
    const uint32_t feed = idOf(RD_TARGET_FEED128), aura = idOf(RD_TARGET_AURA_WORK);
    int n = 0;
    /* cleanUpFB: Cs * As + Cd with RGBAQ 0, 0, 0, 0 (RGB kept, alpha 0),
     * then the background's mark at Z GEQUAL */
    s[n].st = state(scene, scene, 0);
    s[n].st.ds.abe = 1;
    s[n].st.ds.blend = RD_BLEND_CS_AS_ADD_CD;
    s[n++].c = post(RD_POST_FLARE, 0, 0, 0, 0);
    s[n].st = s[n - 1].st;
    s[n].st.ds.test.ztst = RD_ZTST_GEQUAL;
    s[n++].c = post(RD_POST_FLARE, 0, 0, 0, 192);
    *band = n;
    s[n].st = state(pad, 0, 0);
    s[n++].c = post(RD_POST_FLARE, 0, 0, 0, 192);
    s[n].st = state(w2, scene, texOf(RD_TARGET_SCENE, RD_VIEW_RGBA));
    s[n++].c = post(RD_POST_FLARE, 0, 0, 0, 128); /* the mask */
    s[n].st = state(w2, scene, 0);
    s[n].st.ds.test.ztst = RD_ZTST_GEQUAL;
    s[n++].c = ofType(RDC_MESH, 0, 0); /* a lamp into WORK2 */
    s[n].st = state(w1, 0, texOf(RD_TARGET_WORK0, RD_VIEW_RGBA));
    s[n++].c = post(RD_POST_FLARE, 128, 128, 128, 0); /* a blur pass */
    *glowAt = -1;
    if (glow) {
        *glowAt = n;
        s[n].st = state(scene, scene, texOf(RD_TARGET_WORK3, RD_VIEW_RGBA));
        s[n].st.ds.abe = 1;
        s[n].st.ds.blend = RD_BLEND_CS_FIX_ADD_CD;
        s[n++].c = post(RD_POST_FLARE, 128, 128, 128, 128); /* the glow's paste */
    }
    s[n].st = state(aura, scene, 0);
    s[n++].c = post(RD_POST_AURA, 0, 0, 0, 0);
    s[n].st = state(w0, 0, texOf(RD_TARGET_AURA_WORK, RD_VIEW_RGBA));
    s[n++].c = post(RD_POST_AURA, 0, 0, 0, 128);
    s[n].st = state(feed, 0, texOf(RD_TARGET_WORK0, RD_VIEW_RGBA));
    s[n++].c = post(RD_POST_AURA, 0, 0, 0, 128);
    if (screen) {
        s[n].st = state(scene, scene, 0);
        s[n++].c = ofType(RDC_SCREEN, 0, 0);
    }
    *auraAt = n;
    s[n].st = state(scene, scene, texOf(RD_TARGET_FEED128, RD_VIEW_RGBA));
    s[n].st.ds.abe = 1;
    s[n].st.ds.blend = RD_BLEND_LERP_AS_ALT;
    s[n++].c = post(RD_POST_AURA, 64, 64, 64, 128);
    s[n].st = state(scene, scene, 0);
    s[n++].c = ofType(RDC_SCREEN, 0, 0);
    return n;
}

static void testScan(void)
{
    Step s[20];
    int band, glowAt, auraAt;
    /* the plan ignored (the tests' frames hold no chain the light plan
     * reads, so nothing on SCENE is skipped): held, the read where the
     * draw lands, the glow's paste (LAMPS, WHOLE) or without it the aura's */
    int n = order(s, true, false, &band, &glowAt, &auraAt);
    rd__late_pass_force((int)(RD_LATE_FILL | RD_LATE_ANY_PLAN));
    const int onGlow = scanAt(s, n);
    /* no plan that holds (none here): the walk draws at the band */
    rd__late_pass_force((int)RD_LATE_FILL);
    const int none = scanAt(s, n);
    rd__late_pass_force(0);
    const int off = scanAt(s, n);
    printf("  scan: the fill's read at command %d held (the glow's paste %d), %d under no plan "
           "that holds and %d off (the band %d)\n",
           onGlow, glowAt, none, off, band);
    CHECK(onGlow == glowAt, "held, the read at %d, the glow's paste %d expected", onGlow, glowAt);
    CHECK(none == band, "no plan that holds, the read at %d, the band %d expected", none, band);
    CHECK(off == band, "off, the read at %d, the band %d expected", off, band);
    n = order(s, false, false, &band, &glowAt, &auraAt);
    rd__late_pass_force((int)(RD_LATE_FILL | RD_LATE_ANY_PLAN));
    const int onAura = scanAt(s, n);
    /* held to the walk's end: no landing in the frame */
    const int end = scanAt(s, auraAt);
    printf("  scan: without the glow's paste %d (the aura's paste %d); %d with no paste (the "
           "walk's end %d)\n",
           onAura, auraAt, end, auraAt);
    CHECK(onAura == auraAt, "held, the read at %d, the aura's paste %d expected", onAura, auraAt);
    CHECK(end == auraAt, "held with no paste, the read at %d, the end %d expected", end, auraAt);
    /* a screen prim on SCENE before the aura's paste: the read there */
    n = order(s, false, true, &band, &glowAt, &auraAt);
    const int early = scanAt(s, n);
    rd__late_pass_force(-1);
    CHECK(early == auraAt - 1, "a screen prim before the paste: the read at %d, %d expected", early,
          auraAt - 1);
}

static void testAlphaOnly(void)
{
    RdDrawState d;
    memset(&d, 0, sizeof(d));
    d.texEnabled = 1;
    d.texFn = RD_TEXFN_MODULATE;
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    p.rgba[3] = 128;
    CHECK(rd__fx_alpha_only(&d, &p), "MODULATE by RGB 0: the alpha alone");
    p.lines = 2; /* a TFX in the record: HIGHLIGHT */
    CHECK(!rd__fx_alpha_only(&d, &p), "HIGHLIGHT is not the alpha alone");
    p.lines = 0;
    p.rgba[1] = 1;
    CHECK(!rd__fx_alpha_only(&d, &p), "RGBAQ's G 1 is not the alpha alone");
    p.rgba[1] = 0;
    d.abe = 1;
    CHECK(!rd__fx_alpha_only(&d, &p), "a blend is not the alpha alone");
    d.abe = 0;
    d.texEnabled = 0;
    CHECK(!rd__fx_alpha_only(&d, &p), "untextured is not a read");
}

int main(void)
{
    printf("rd_late_pass_test\n");
    if (!rd__init_record_only(512, 448)) {
        printf("rd_late_pass_test: rd__init_record_only failed\n");
        return 1;
    }
    memset(&s_f, 0, sizeof(s_f));
    s_f.payload = (uint8_t *)s_recs;
    s_f.payloadSize = (uint32_t)sizeof(s_recs);
    testClass();
    testScan();
    testAlphaOnly();
    rd_shutdown();
    if (failures) {
        printf("rd_late_pass_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_late_pass_test: ok\n");
    return 0;
}
