/* rd_glow_light.c: the glow drawn only where it can add light
 * (docs/port/RENDER_POST.md, "The glow where it adds light";
 * docs/port/WORK_BUFFERS.md, "The glow's buffers under the light plan";
 * DIVERGENCES.md VD-30).  Above 1x on every device class and backend, no
 * switch; ICO_RD_GLOW_LIGHT=0 draws the chain as before (the tests' A/B,
 * TESTING.md).
 *
 * List 7's glow (staticBlur.c makeFullScreenFlareBefore/After,
 * pasteFullScreenFlare) fills WORK2 (fillWork2: WORK2_PAD's band and WORK2
 * itself, RGBAQ (0, 0, 0, 192) in postEffect 1, 3, 6 and 7), draws the
 * sun's fans into it (dispSun, when the sun is on screen), the mask
 * (makeMaskPatternToWork2: SCENE's alpha, MODULATE by RGBAQ (0, 0, 0, 128),
 * RGB 0), the lamps' meshes (the issue 163 rooms), reduces WORK2 into WORK0,
 * blurs WORK0 through WORK1 ten times, copies WORK0 into WORK3 (eyeBlur,
 * with the sun's ghosts, shrink and tint when the sun is up), sets WORK0's
 * alpha (pasteWork0ToFB) and adds WORK3 to SCENE (Cs * FIX + Cd, FIX 128).
 * Every sprite after the fill is MODULATE or HIGHLIGHT with RGBAQ's alpha
 * 0 per channel, so with RGB 0 in WORK2 every buffer's RGB stays 0 and the
 * paste adds 0: on the PowerVR phone that chain is 11.6 ms of a 32 ms
 * picture (the mask's 2366 x 1080 pass 5.3, the paste 3.1, twelve small
 * passes 2.9) for no light.  Only the fans (drawn before the mask) and the
 * lamps (after it) put light into WORK2.  The chain also carries an alpha:
 * WORK2's is the mask's (SCENE's alpha, 192 on the background cleanUpFB
 * marked, 0 elsewhere), and it reaches WORK1, WORK3 and, through the
 * paste, SCENE.
 *
 * A frame scan (rd__glow_light_scan_*, in rd_store_plan.c's one walk of
 * the lists) reads the chain in its known form, every command from the
 * band's fill to the paste contiguous:
 *   [PAD] FILL FANS* MASK LAMPS* REDUCTION (PASS1 PASS2)+ EYE SUN* [HOLD]
 *   WORK0 PASTE
 * (SUN: the eye blur's ghosts and tint into WORK3 and its shrink into
 * WORK1; HOLD: the glow hold's copy of WORK3 into WORK3_HELD,
 * rd_glow_hold.c) and picks one plan for it:
 *   DARK   WORK2's fill RGB 0, no fans on the target, no lamps: none of the
 *          chain is drawn.  Each buffer is left with the RGB the chain
 *          leaves: WORK2 and WORK2_PAD their fills' colours and WORK3 0 as
 *          lazy clears (drawn only when something reads them: a later
 *          replay that touches them before covering them, a read of them
 *          outside the replay, rd_pass_cut.c), WORK3 a clear instead when
 *          the glow hold copies it; WORK1 0 over the first blur pass's rect
 *          and its glow edge strip (the union of every pass into it: their
 *          rects nest) and over the sun's shrink, those sprites drawn with
 *          their source's colour planned (rd_const_read.c: untextured, in
 *          their own coverage), the first pass not even that when a dark
 *          plan left its rect zero and nothing wrote WORK1 since (s_w1);
 *          WORK0 (0, 0, 0, A), pasteWork0ToFB's (the last blur pass covered
 *          WORK0), a lazy clear too, which the next sprite that touches
 *          WORK0 draws first (lazyTouch: a pending clear, the load op of
 *          that sprite's pass: the aura's in play).  A buffer a command
 *          other than a sprite touches later (the hold's copy of WORK3)
 *          gets its clear at the chain's place.  So the chain begins no
 *          pass in a steady dark room (RdPerfRecord.glowLightPasses).
 *          The fans whose box misses the target are not drawn
 *          (rd__glow_light_skip, rd_replay.c doScreen).  SCENE is as the
 *          paste leaves it in RGB; its alpha is cleanUpFB's (0, 192 on the
 *          marked background) where the paste wrote WORK3's;
 *   LAMPS  fill RGB 0, no fans on the target, the lamps drawn into WORK2
 *          after the mask without DATE or a blend on the destination's
 *          alpha: the fill drawn as (0, 0, 0, 0) and the mask not drawn
 *          (it writes RGB 0 over a fill of RGB 0); the lamps and the rest
 *          of the chain as before;
 *   WHOLE  the chain as before: a grey fill (postEffect 4 and 5, the
 *          bloom), any other form, the MSAA epoch, the sun's fans on the
 *          target (the sun's tint reads the alpha of WORK1 at the sun,
 *          which the mask decides over the blur's whole reach), the sun's
 *          branch with the lamps (the same), the sun's glare onto SCENE,
 *          a later command reading WORK1, WORK2 or WORK3, or SCENE's alpha
 *          (a texture of SCENE, DATE, a blend on Ad, a copy) before a
 *          sprite rewrites it whole, SCENE's alpha not rewritten in the
 *          frame.
 * So every target's RGB is the chain's, byte for byte; the alpha of WORK1,
 * WORK2, WORK3, WORK3_HELD and SCENE (until it is rewritten) differs only
 * where WORK2's alpha was not 0: on the background cleanUpFB marked (the
 * fill kept by the mask's alpha test) and its bilinear rim.  No command
 * reads those alphas before they are rewritten (the scan); a later frame
 * that reads WORK1, WORK2 or WORK3 before writing them turns the plan off
 * from then on (logged).  At 1x (the predicate rd_replay.c
 * blurAbove1xSteps takes: SCENE at scale 1) nothing changes: Original keeps
 * the PS2's bytes in every buffer.
 *
 * The replay: rd_blur_border.c blurPostSprite asks rd__glow_light_sprite
 * for every post sprite (after SCENE's border repeat, before the WORK2
 * fold, which stands down in a plan frame: rd_glow_fold.c); the walk's
 * command order (g_rdStoreOrd) and the command name the planned ones, as
 * the fold's.  Skipping a draw does not move the crack fill (placed by the
 * list loop from the recorded commands, rd_replay.c rd__crack_fill_at).
 *
 * Counted (RdPerfRecord.glowLight, glowLightWhy): the 10 s line
 * "window: glow: light: N dark, L lamps (mask not drawn), K whole (why)"
 * (rd_glow_light_line) and rd_replay_tool --stats; the log says each plan's
 * first frame and each reason once. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* what the replay does with a chain command */
enum {
    ACT_DRAW,  /* drawn as recorded */
    ACT_SKIP,  /* not drawn */
    ACT_LAZY,  /* not drawn, its target's colour a lazy clear */
    ACT_PLAN,  /* drawn with its source's colour planned (untextured) */
    ACT_CLEAR, /* drawn untextured without a blend in colour col: a clear */
    ACT_ALPHA0 /* drawn with RGBAQ's alpha 0 (the lamps' fill) */
};

/* the scan's element of the chain */
enum {
    E_PAD,
    E_FILL,
    E_FAN,
    E_MASK,
    E_LAMP,
    E_RED,
    E_P1,
    E_P2,
    E_EYE,
    E_GHOST, /* the sun's ghosts and tint: into WORK3 */
    E_SHRINK,
    E_HOLD,
    E_W0,
    E_PASTE
};

#define GL_MAX 128

typedef struct GlCmd {
    const RdCmd *c;
    uint32_t ord;
    uint8_t el, act;
    uint8_t cover;   /* E_PAD, E_P2: it covers its target */
    uint8_t col[4];  /* ACT_LAZY, ACT_CLEAR: the colour (RGBA, FBA's bit in) */
    uint32_t target; /* ACT_LAZY: the target; ACT_PLAN: the planned source */
    float rect[4];   /* E_P1: the rect */
    int32_t box[4];  /* E_FAN: the vertices' box (12.4), x0 y0 x1 y1 */
} GlCmd;

enum {
    PH_IDLE,
    PH_PAD,
    PH_FILL,
    PH_MASK,
    PH_BLUR,
    PH_P1,
    PH_EYE,
    PH_W0,
    PH_AFTER, /* after the paste: SCENE's alpha not rewritten yet */
    PH_DONE,  /* SCENE's alpha rewritten */
    PH_FAIL
};

/* the targets a lazy clear may hold (the band, WORK2, WORK3, WORK0) */
enum { LZ_PAD, LZ_W2, LZ_W3, LZ_W0, LZ_COUNT };

static uint32_t lazyId(int k);

/* the first touch of a lazy target in the frame: none, a write covering
 * it (a clear, a covering sprite), anything else */
enum { FT_NONE, FT_COVER, FT_OTHER };

static struct {
    /* the scan */
    bool scanning, scanned;
    uint32_t scanReplay;
    uint8_t phase, why;
    const char *detail;
    bool glow, readFirst, grey, fansOn, sun;
    bool pasteFix;    /* the paste's blend takes FIX (not As or Ad) as its C */
    uint32_t written; /* bit k: the scan saw a write of watched buffer k (WORK1, 2, 3) */
    uint32_t n, pairs, lamps;
    GlCmd cmd[GL_MAX];
    uint8_t first[LZ_COUNT];
    /* a command the replay does not hand to rd__glow_light_sprite (not a
     * post sprite: a copy, a screen draw, a mesh) touches lazy target k
     * after the chain began: its clear cannot wait for a sprite to draw it */
    bool unhooked[LZ_COUNT];
    bool w1Before, w1After; /* WORK1 written outside the chain before it, after it */
    bool w1Cleared;         /* cleared to 0 before the chain, nothing written since */
    bool chainSeen;         /* the chain's first command was walked */
    float p1Rect[4];        /* the first blur pass's rect (12.4), its target's GS width */
    uint32_t p1GsW;
    /* the replay: the pass serial at the chain's first command, the passes
     * begun from there to the paste (RdPerfRecord.glowLightPasses) */
    bool spanOpen;
    uint32_t spanFrom;
    uint32_t spanLast;  /* the pass serial at the last chain hook (or mark) */
    uint32_t preSerial; /* rd__glow_light_mark's: before SCENE's border repeat */
    uint32_t preCount;  /* the marks this replay (the hook's own one included) */
    uint8_t spanLastEl;
    bool inRepeat;         /* between rd__glow_light_mark and the hook */
    uint32_t glowLightOwn; /* the span's passes but SCENE's border repeat */
    /* the plan of this replay */
    uint8_t plan;
    uint32_t replay;
    uint32_t frameNo;
    /* the replay's planned source (ACT_PLAN): its target and colour */
    bool planned;
    uint32_t plannedSrc;
    uint8_t plannedCol[4];
} s_gl;

/* the lazy clears: target id, colour texture id, colour */
static struct {
    bool on;
    uint32_t id, tex;
    float col[4];
    uint8_t rgba[4];
} s_lazy[LZ_COUNT];

/* WORK1's region a dark plan zeroed, still zero: its texture, the first
 * blur pass's rect and the GS width it was drawn at (no replay wrote WORK1
 * since but the dark plans' own zero draws) */
static struct {
    bool valid;
    uint32_t tex, gsW;
    float rect[4];
} s_w1;

static bool s_off;       /* a frame read the alphas a plan frame left */
static bool s_stale;     /* the last replay with a glow applied a plan */
static int s_force = -1; /* rd__glow_light_force: -1 the environment's */

/* the 10 s line's counts since the last line, per plan and reason */
static uint64_t s_count[RD_GLOW_LIGHT_COUNT];
static uint64_t s_whyCount[RD_GLOW_LIGHT_WHY_COUNT];
static uint64_t s_darkPasses; /* the passes the dark plans' chains began */

/* the passes begun in the last chain's span, named (rd__glow_light_debug,
 * the tests print it when a bound fails): what came before each */
static char s_dbg[768];
static size_t s_dbgAt;

static const char *const kEl[] = {
    "band",        "fill",     "fan",        "mask",   "lamp",      "reduction",     "blur pass 1",
    "blur pass 2", "eye blur", "sun sprite", "shrink", "hold copy", "WORK0's alpha", "paste"};

const char *rd__glow_light_debug(void)
{
    return s_dbg;
}

void rd__glow_light_mark(const Replay *r)
{
    s_gl.preSerial = r->passSerial;
    s_gl.preCount++;
    s_gl.inRepeat = true; /* until the hook: SCENE's border repeat */
}

void rd__glow_light_pass(uint32_t cid, const RdTargetRec *t, int load)
{
    if (!s_gl.spanOpen || s_dbgAt >= sizeof(s_dbg) - 1) {
        return;
    }
    const uint8_t el = s_gl.spanLastEl;
    const int m =
        snprintf(s_dbg + s_dbgAt, sizeof(s_dbg) - s_dbgAt, "%s%s %ux%u (%s load) %s %s",
                 s_dbgAt ? "; " : "", rd__target_name(cid), t ? t->tw : 0u, t ? t->th : 0u,
                 load == RHI_LOAD_CLEAR  ? "clear"
                 : load == RHI_LOAD_LOAD ? "load"
                                         : "no",
                 s_gl.inRepeat ? "in SCENE's border repeat, after the" : "after the",
                 el < sizeof(kEl) / sizeof(kEl[0]) ? kEl[el] : "?");
    if (m > 0) {
        s_dbgAt += (size_t)m < sizeof(s_dbg) - s_dbgAt ? (size_t)m : sizeof(s_dbg) - 1 - s_dbgAt;
    }
}

static const char *const kPlan[RD_GLOW_LIGHT_COUNT] = {
    [RD_GLOW_LIGHT_NONE] = "no glow",
    [RD_GLOW_LIGHT_DARK] = "dark (the chain not drawn)",
    [RD_GLOW_LIGHT_LAMPS] = "lamps (the mask not drawn)",
    [RD_GLOW_LIGHT_WHOLE] = "whole (the chain drawn)",
    [RD_GLOW_LIGHT_OFF] = "off (1x, or switched off)",
};

static const char *const kWhy[RD_GLOW_LIGHT_WHY_COUNT] = {
    [RD_GLOW_LIGHT_WHY_NONE] = "-",
    [RD_GLOW_LIGHT_WHY_GREY] = "a grey fill (the bloom)",
    [RD_GLOW_LIGHT_WHY_FORM] = "the chain not in its known form",
    [RD_GLOW_LIGHT_WHY_MSAA] = "anti-aliasing (MSAA)",
    [RD_GLOW_LIGHT_WHY_FANS] = "the sun's fans on the picture",
    [RD_GLOW_LIGHT_WHY_SUN] = "the sun's tint or glare with light in the chain",
    [RD_GLOW_LIGHT_WHY_SCENE] = "SCENE's alpha read before it is rewritten",
    [RD_GLOW_LIGHT_WHY_WORK] = "WORK1, WORK2 or WORK3 read outside the chain",
    [RD_GLOW_LIGHT_WHY_OFF] = "off since a frame read a planned frame's alpha",
};

const char *rd_glow_light_text(uint32_t plan)
{
    return plan < RD_GLOW_LIGHT_COUNT && kPlan[plan] ? kPlan[plan] : "unknown";
}

const char *rd_glow_light_why_text(uint32_t why)
{
    return why < RD_GLOW_LIGHT_WHY_COUNT && kWhy[why] ? kWhy[why] : "unknown";
}

void rd__glow_light_force(int on)
{
    s_force = on;
    s_off = false;
    s_stale = false;
}

bool rd__glow_light_on(void)
{
    if (s_force >= 0) {
        return s_force != 0;
    }
    static int env = -1;
    if (env < 0) {
        /* the tests' A/B only (TESTING.md): no config key */
        const char *e = getenv("ICO_RD_GLOW_LIGHT");
        env = !(e && e[0] == '0');
    }
    return env != 0;
}

/* ------------------------------------------------------------- helpers */

static uint32_t idOf(int named)
{
    return rd_target((RdTargetId)named).id;
}

static bool postRec(const RdFrame *f, const RdCmd *c, RdPostRec *p)
{
    if (!f || !f->payload || (uint64_t)c->u[1] + sizeof(RdPostRec) > f->payloadSize) {
        return false;
    }
    memcpy(p, f->payload + c->u[1], sizeof(*p));
    return true;
}

/* the state's enabled texture as a target's colour (not its depth view):
 * its target, else 0 */
static uint32_t texTarget(const RdStateBlock *st)
{
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    return t && t->kind == RD_TEXKIND_TARGET && t->view != RD_VIEW_DEPTH ? t->target : 0u;
}

/* a texture of a target's RGBA32 colour (the planned reads') */
static bool texRgba32(const RdStateBlock *st)
{
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    return t && t->kind == RD_TEXKIND_TARGET && t->view == RD_VIEW_RGBA &&
           t->src == RD_TEXSRC_RGBA32;
}

/* a post sprite of the glow's kinds (not the display's reduction) */
static bool glowSprite(const RdCmd *c)
{
    return c->type == RDC_POST_STUB && rd__is_blur_kind(c->b[0]) && c->b[0] != RD_POST_REDUCTION;
}

static bool noDepth(const RdDrawState *d)
{
    return d->zwrite == RD_ZWRITE_OFF && (!d->test.zte || d->test.ztst == RD_ZTST_ALWAYS);
}

static bool ateNoFail(const RdDrawState *d)
{
    return !d->test.ate || d->test.atst == RD_ATST_ALWAYS;
}

/* the blend reads the destination's alpha (C = Ad) */
static bool blendReadsAd(const RdDrawState *d)
{
    const uint32_t reg = rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : 0);
    return d->abe && ((reg >> 4) & 3u) == 1u;
}

/* ((A - B) C >> 7) + D leaves Cd when Cs is 0, whatever C: A and B each Cs
 * or 0, D Cd (blend off writes Cs: not) */
static bool blendKeepsAtCs0(const RdDrawState *d)
{
    if (!d->abe) {
        return false;
    }
    const uint32_t reg = rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : 0);
    const uint32_t a = reg & 3u, b = (reg >> 2) & 3u, dd = (reg >> 6) & 3u;
    return a != 1u && b != 1u && dd == 1u;
}

/* fx_tfx's RGB of a texel of RGB 0 (any alpha) under RGBAQ col is 0 */
static bool tfxRgbZero(const RdDrawState *d, const RdPostRec *p)
{
    const uint32_t flags = rd__replay_blur_flags(d, p->lines, 1, 0);
    for (int a = 0; a < 256; a += 255) {
        const uint8_t t[4] = {0, 0, 0, (uint8_t)a};
        uint8_t o[4];
        rd__const_tfx(t, p->rgba, flags, o);
        if (o[0] || o[1] || o[2]) {
            return false;
        }
    }
    return true;
}

/* the XYOFFSET a post sprite under st draws its target with, in sixteenths
 * (rd_glow_fold.c origin16, without a screen copy's half) */
static void origin16(const RdStateBlock *st, int32_t *ox16, int32_t *oy16)
{
    *ox16 = 16 * (2048 - (int32_t)(st->gsW >> 1));
    *oy16 = 16 * (2048 - (int32_t)(st->gsH >> 1)) + ((st->useOffset & RD_TARGET_HALF_Y) ? 8 : 0);
}

/* every texel of t has its GS pixel inside rect (12.4) under the origin:
 * fx_sprite_ps's coverage (rd_glow_fold.c coversTarget) */
static bool coversTarget(const RdTargetRec *t, const float rect[4], int32_t ox16, int32_t oy16)
{
    if (!t || t->w == 0 || t->h == 0 || t->tw == 0 || t->th == 0 || !(t->sx > 0.0f) ||
        !(t->sy > 0.0f)) {
        return false;
    }
    for (int k = 0; k < 4; k++) {
        if (!(rect[k] >= -1048576.0f && rect[k] <= 1048576.0f)) {
            return false;
        }
    }
    if (((float)(t->tw - 1) + t->ox) / t->sx + 0.01f >= (float)t->w ||
        (float)(t->th - 1) / t->sy + 0.01f >= (float)t->h) {
        return false;
    }
    return (int32_t)rect[0] <= ox16 && (int32_t)rect[1] <= oy16 &&
           (int32_t)rect[2] > ox16 + 16 * ((int32_t)t->w - 1) &&
           (int32_t)rect[3] > oy16 + 16 * ((int32_t)t->h - 1);
}

static bool scissorWhole(const RdTargetRec *t, const RdStateBlock *st)
{
    RhiRect sc;
    return t && rd__scissor_texels(t, st, 0, &sc) && sc.x == 0 && sc.y == 0 && sc.w == t->tw &&
           sc.h == t->th;
}

/* a post sprite that writes every texel of its target with no read of it:
 * no blend, no DATE, an alpha test that cannot fail, the colour mask whole,
 * no Z, the scissor whole, its rect covering the target */
static bool coverWrite(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, RdPostRec *p)
{
    const RdDrawState *d = &st->ds;
    const RdTargetRec *t = rd__target_rec(st->color);
    if (!t || !postRec(f, c, p) || d->abe || d->test.date != RD_DATE_OFF || !ateNoFail(d) ||
        d->colorMask != 0xF || d->fbmsk != 0 || !noDepth(d) || texTarget(st) == st->color) {
        return false;
    }
    int32_t ox16, oy16;
    origin16(st, &ox16, &oy16);
    return scissorWhole(t, st) && coversTarget(t, p->rect, ox16, oy16);
}

/* the colour a covering untextured sprite leaves (FBA's bit in) */
static void spriteColour(const RdDrawState *d, const RdPostRec *p, uint8_t col[4])
{
    col[0] = p->rgba[0];
    col[1] = p->rgba[1];
    col[2] = p->rgba[2];
    col[3] = (uint8_t)(p->rgba[3] | (d->fba ? 0x80u : 0u));
}

/* whether command c under st reads target id's colour (its texture, a
 * copy or exact blend naming it, a command the scan does not know) */
static bool readsTarget(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
    case RDC_CLEAR:
        return false;
    case RDC_COPY: /* u[0] the source, u[1] the destination */
        return c->u[0] == id;
    case RDC_EXACT_BLEND:
        return c->u[0] == id || c->u[1] == id;
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_POST_STUB:
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
    case RDC_WORLD_PRIMS:
        return texTarget(st) == id ||
               (st->color == id && (st->ds.test.date != RD_DATE_OFF || blendReadsAd(&st->ds)));
    default:
        return true;
    }
}

/* whether command c under st writes target id */
static bool writesTarget(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return false;
    case RDC_CLEAR:
        return c->u[0] == id;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        return c->u[1] == id || c->u[0] == id;
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_POST_STUB:
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
    case RDC_WORLD_PRIMS:
        return st->color == id;
    default:
        return true;
    }
}

/* a draw that rewrites SCENE's alpha at every texel without reading it: a
 * post sprite or a screen sprite over the frame, its alpha written (the
 * colour mask's A, no FBMSK on it), no DATE, no blend on Ad, an alpha test
 * that cannot fail or fails to FB_ONLY, the Z test ALWAYS */
static bool rewritesSceneAlpha(const RdFrame *f, const RdCmd *c, const RdStateBlock *st)
{
    const RdDrawState *d = &st->ds;
    const RdTargetRec *t = rd__target_rec(st->color);
    if (!t || st->color != idOf(RD_TARGET_SCENE) || !(d->colorMask & 8u) ||
        (d->fbmsk & 0xFF000000u) != 0 || d->test.date != RD_DATE_OFF || blendReadsAd(d) ||
        !(ateNoFail(d) || d->test.afail == RD_AFAIL_FB_ONLY) ||
        (d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || !scissorWhole(t, st)) {
        return false;
    }
    int32_t ox16, oy16;
    origin16(st, &ox16, &oy16);
    if (glowSprite(c) || (c->type == RDC_POST_STUB && c->b[0] == RD_POST_AURA)) {
        RdPostRec p;
        return postRec(f, c, &p) && texTarget(st) != st->color &&
               coversTarget(t, p.rect, ox16, oy16);
    }
    if (c->type != RDC_SCREEN || c->b[0] != RD_PRIM_SPRITES || c->u[1] < 2u ||
        (uint64_t)c->u[0] + (uint64_t)c->u[1] * sizeof(RdScreenVtx) > f->payloadSize ||
        texTarget(st) == st->color) {
        return false;
    }
    /* any one sprite of the command covering the frame */
    for (uint32_t i = 0; i + 1u < c->u[1]; i += 2u) {
        RdScreenVtx v[2];
        memcpy(v, f->payload + c->u[0] + (uint64_t)i * sizeof(RdScreenVtx), sizeof(v));
        const float rect[4] = {
            (float)(v[0].x < v[1].x ? v[0].x : v[1].x), (float)(v[0].y < v[1].y ? v[0].y : v[1].y),
            (float)(v[0].x > v[1].x ? v[0].x : v[1].x), (float)(v[0].y > v[1].y ? v[0].y : v[1].y)};
        if (coversTarget(t, rect, ox16, oy16)) {
            return true;
        }
    }
    return false;
}

/* the screen command's vertices' box (12.4), false when unread */
static bool screenBox(const RdFrame *f, const RdCmd *c, int32_t box[4])
{
    const uint32_t n = c->u[1];
    if (n == 0 || (uint64_t)c->u[0] + (uint64_t)n * sizeof(RdScreenVtx) > f->payloadSize) {
        return false;
    }
    box[0] = box[1] = INT32_MAX;
    box[2] = box[3] = INT32_MIN;
    for (uint32_t i = 0; i < n; i++) {
        RdScreenVtx v;
        memcpy(&v, f->payload + c->u[0] + (uint64_t)i * sizeof(v), sizeof(v));
        box[0] = v.x < box[0] ? v.x : box[0];
        box[1] = v.y < box[1] ? v.y : box[1];
        box[2] = v.x > box[2] ? v.x : box[2];
        box[3] = v.y > box[3] ? v.y : box[3];
    }
    return true;
}

/* the box (12.4) misses target t under st's origin by more than the
 * screen area's pad (rd__screen_area: two GS pixels, three with AA1) */
static bool boxMisses(const RdTargetRec *t, const RdStateBlock *st, const int32_t box[4])
{
    int32_t ox16, oy16;
    origin16(st, &ox16, &oy16);
    const int32_t pad = 3 * 16;
    return !t || box[2] + pad < ox16 || box[3] + pad < oy16 ||
           box[0] - pad > ox16 + 16 * (int32_t)t->w || box[1] - pad > oy16 + 16 * (int32_t)t->h;
}

/* command c under st reads WORK1, WORK2 or WORK3 before the scan saw a
 * write of it (noted here) */
static bool readBeforeWrite(const RdCmd *c, const RdStateBlock *st)
{
    const uint32_t ids[3] = {idOf(RD_TARGET_WORK1), idOf(RD_TARGET_WORK2), idOf(RD_TARGET_WORK3)};
    bool read = false;
    for (int k = 0; k < 3; k++) {
        if (!(s_gl.written & (1u << k)) && (readsTarget(c, st, ids[k]) || st->depth == ids[k])) {
            read = true;
        }
    }
    for (int k = 0; k < 3; k++) {
        if (writesTarget(c, st, ids[k])) {
            s_gl.written |= 1u << k;
        }
    }
    return read;
}

/* ------------------------------------------------------------- the scan */

static void fail(uint8_t why, const char *detail)
{
    if (s_gl.phase != PH_FAIL) {
        s_gl.why = why;
        s_gl.detail = detail;
        s_gl.phase = PH_FAIL;
    }
}

static GlCmd *add(const RdCmd *c, uint32_t ord, uint8_t el)
{
    if (s_gl.n >= GL_MAX) {
        fail(RD_GLOW_LIGHT_WHY_FORM, "more chain commands than the scan holds");
        return NULL;
    }
    GlCmd *e = &s_gl.cmd[s_gl.n++];
    memset(e, 0, sizeof(*e));
    e->c = c;
    e->ord = ord;
    e->el = el;
    return e;
}

/* the first touch of the lazy targets (a clear or a covering write, or
 * anything else) */
static void noteFirst(const RdFrame *f, const RdCmd *c, const RdStateBlock *st)
{
    uint32_t ids[LZ_COUNT];
    for (int k = 0; k < LZ_COUNT; k++) {
        ids[k] = lazyId(k);
    }
    for (int k = 0; k < LZ_COUNT; k++) {
        if (s_gl.first[k] != FT_NONE || !ids[k]) {
            continue;
        }
        const bool reads = readsTarget(c, st, ids[k]) || st->depth == ids[k];
        const bool writes = writesTarget(c, st, ids[k]);
        if (!reads && !writes) {
            continue;
        }
        RdPostRec p;
        const bool cover = !reads && ((c->type == RDC_CLEAR && c->u[0] == ids[k]) ||
                                      (glowSprite(c) && coverWrite(f, c, st, &p)));
        s_gl.first[k] = cover ? FT_COVER : FT_OTHER;
    }
}

bool rd__glow_light_scan_begin(const RdFrame *f)
{
    s_gl.scanning = false;
    s_gl.scanned = false;
    s_gl.plan = RD_GLOW_LIGHT_NONE;
    s_gl.planned = false;
    s_gl.phase = PH_IDLE;
    s_gl.why = RD_GLOW_LIGHT_WHY_NONE;
    s_gl.detail = NULL;
    s_gl.glow = s_gl.readFirst = s_gl.grey = s_gl.fansOn = s_gl.sun = s_gl.pasteFix = false;
    s_gl.written = 0;
    s_gl.n = s_gl.pairs = s_gl.lamps = 0;
    for (int k = 0; k < LZ_COUNT; k++) {
        s_gl.first[k] = FT_NONE;
        s_gl.unhooked[k] = false;
    }
    s_gl.w1Before = s_gl.w1After = s_gl.w1Cleared = s_gl.chainSeen = s_gl.spanOpen = false;
    s_gl.preCount = 0;
    s_gl.p1GsW = 0;
    g_rdPerf.glowLightPasses = 0;
    g_rdPerf.glowLight = RD_GLOW_LIGHT_NONE;
    g_rdPerf.glowLightWhy = RD_GLOW_LIGHT_WHY_NONE;
    g_rdPerf.glowLightLazy = 0;
    if (!f || !g_rd.hasDevice) {
        return false;
    }
    s_gl.frameNo = f->number;
    s_gl.scanning = true;
    return true;
}

static void scanStep(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, uint32_t ord)
{
    const uint32_t w0 = idOf(RD_TARGET_WORK0), w1 = idOf(RD_TARGET_WORK1);
    const uint32_t w2 = idOf(RD_TARGET_WORK2), w3 = idOf(RD_TARGET_WORK3);
    const uint32_t pad = idOf(RD_TARGET_WORK2_PAD), scene = idOf(RD_TARGET_SCENE);
    const uint32_t held = idOf(RD_TARGET_WORK3_HELD);
    const bool post = glowSprite(c);
    const uint32_t tex = texTarget(st), tgt = st->color;
    const RdDrawState *d = &st->ds;
    RdPostRec p;
    GlCmd *e;
    switch (s_gl.phase) {
    case PH_IDLE:
        if (post && !d->texEnabled && (tgt == pad || tgt == w2)) {
            s_gl.glow = true;
            if (tgt == pad) {
                if ((e = add(c, ord, E_PAD)) == NULL) {
                    return;
                }
                if (coverWrite(f, c, st, &p)) {
                    e->cover = 1;
                    e->target = pad;
                    spriteColour(d, &p, e->col);
                }
                s_gl.phase = PH_PAD;
                return;
            }
            goto fill;
        }
        if (readBeforeWrite(c, st)) {
            s_gl.readFirst = true; /* what an earlier replay left there */
        }
        return;
    case PH_PAD:
        if (post && !d->texEnabled && tgt == w2) {
            goto fill;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "WORK2_PAD's band not followed by WORK2's fill");
        return;
    case PH_FILL:
        if ((c->type == RDC_SCREEN || (post && !tex)) && tgt == w2) {
            /* the sun's fans (dispSun: triangle fans; any draw of no target's
             * texture here is taken as one, the light it may add judged by
             * its box) */
            if ((e = add(c, ord, E_FAN)) == NULL) {
                return;
            }
            bool boxed = false;
            if (c->type == RDC_SCREEN) {
                boxed = screenBox(f, c, e->box);
            } else if (postRec(f, c, &p)) {
                for (int k = 0; k < 4; k++) {
                    e->box[k] = (int32_t)p.rect[k];
                }
                boxed = true;
            }
            if (!boxed || !boxMisses(rd__target_rec(w2), st, e->box)) {
                s_gl.fansOn = true;
            }
            return;
        }
        if (post && tgt == w2 && tex == scene) {
            if (!postRec(f, c, &p) || d->texFn != RD_TEXFN_MODULATE || (p.lines & 3u) != 0u ||
                p.rgba[0] || p.rgba[1] || p.rgba[2] || d->abe || d->test.date != RD_DATE_OFF) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "the mask is not MODULATE by RGB 0 without a blend");
                return;
            }
            if (add(c, ord, E_MASK)) {
                s_gl.phase = PH_MASK;
            }
            return;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "a command between WORK2's fill and the mask");
        return;
    case PH_MASK:
        if (tgt == w2 && ((post && !tex) || c->type == RDC_SCREEN ||
                          (c->type >= RDC_MESH && c->type <= RDC_PARTICLES))) {
            /* the lamps' meshes (issue 163), or any draw of no target's
             * texture into WORK2 after the mask */
            if (d->test.date != RD_DATE_OFF || blendReadsAd(d)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "a lamp reads WORK2's alpha (DATE or Ad)");
                return;
            }
            if (add(c, ord, E_LAMP)) {
                s_gl.lamps++;
            }
            return;
        }
        if (post && tgt == w0 && tex == w2) {
            if (!postRec(f, c, &p) || d->abe || !tfxRgbZero(d, &p)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "the reduction blends or adds RGB");
                return;
            }
            if (add(c, ord, E_RED)) {
                s_gl.phase = PH_BLUR;
            }
            return;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "a command between the mask and the reduction");
        return;
    case PH_BLUR:
        if (post && tgt == w1 && tex == w0) {
            if (!postRec(f, c, &p) || d->abe || !tfxRgbZero(d, &p) || !texRgba32(st)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "a blur pass blends or adds RGB");
                return;
            }
            if ((e = add(c, ord, E_P1)) == NULL) {
                return;
            }
            memcpy(e->rect, p.rect, sizeof(e->rect));
            if (!s_gl.p1GsW) {
                memcpy(s_gl.p1Rect, p.rect, sizeof(s_gl.p1Rect));
                s_gl.p1GsW = st->gsW;
            }
            s_gl.phase = PH_P1;
            return;
        }
        if (s_gl.pairs > 0 && c->type == RDC_POST_STUB && c->b[0] == RD_POST_EYE_BLUR &&
            tgt == w3 && tex == w0) {
            if (!coverWrite(f, c, st, &p) || !tfxRgbZero(d, &p)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "the eye blur does not cover WORK3 or adds RGB");
                return;
            }
            if (add(c, ord, E_EYE)) {
                s_gl.phase = PH_EYE;
            }
            return;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "the blur's pairs not followed by the eye blur");
        return;
    case PH_P1:
        if (post && tgt == w0 && tex == w1) {
            if (!postRec(f, c, &p) || d->abe || !tfxRgbZero(d, &p)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "a blur pass blends or adds RGB");
                return;
            }
            if ((e = add(c, ord, E_P2)) == NULL) {
                return;
            }
            /* the pair's second pass covering WORK0: what WORK0 holds after */
            e->cover = coverWrite(f, c, st, &p) ? 1u : 0u;
            s_gl.pairs++;
            s_gl.phase = PH_BLUR;
            return;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "a blur pass not followed by its second");
        return;
    case PH_EYE:
        if (c->type == RDC_POST_STUB && c->b[0] == RD_POST_EYE_BLUR) {
            s_gl.sun = true;
            if (tgt == scene) {
                fail(RD_GLOW_LIGHT_WHY_SUN, "the sun's glare onto SCENE reads WORK1");
                return;
            }
            if (!postRec(f, c, &p) || !tfxRgbZero(d, &p)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "a sun sprite adds RGB");
                return;
            }
            if (tgt == w3 && (tex == w0 || tex == w1) && (!d->abe || blendKeepsAtCs0(d))) {
                (void)add(c, ord, E_GHOST);
                return;
            }
            if (tgt == w1 && tex == w3 && !d->abe && texRgba32(st)) {
                (void)add(c, ord, E_SHRINK);
                return;
            }
            fail(RD_GLOW_LIGHT_WHY_FORM, "a sun sprite of another form");
            return;
        }
        if (c->type == RDC_COPY && c->u[0] == w3 && c->u[1] == held) {
            (void)add(c, ord, E_HOLD);
            return;
        }
        if (post && tgt == w0 && !tex) {
            uint8_t ident = 0, rgb[3];
            if (!postRec(f, c, &p)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "WORK0's alpha sprite of another form");
                return;
            }
            /* its blend keeps WORK0's RGB (rd_fx_plan.c rd__fx_fold), over
             * the whole of WORK0, writing As */
            RdDrawState dd = *d;
            int32_t ox16, oy16;
            origin16(st, &ox16, &oy16);
            if (!rd__fx_fold(&dd, p.rgba, &ident, rgb) || (ident & 7u) != 7u ||
                d->test.date != RD_DATE_OFF || !ateNoFail(d) || d->colorMask != 0xF ||
                d->fbmsk != 0 || !noDepth(d) || !scissorWhole(rd__target_rec(tgt), st) ||
                !coversTarget(rd__target_rec(tgt), p.rect, ox16, oy16)) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "WORK0's alpha sprite changes its RGB");
                return;
            }
            if ((e = add(c, ord, E_W0)) == NULL) {
                return;
            }
            spriteColour(d, &p, e->col);
            e->col[0] = e->col[1] = e->col[2] = 0;
            s_gl.phase = PH_W0;
            return;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "the eye blur not followed by WORK0's alpha");
        return;
    case PH_W0:
        if (post && tgt == scene && tex == w3) {
            if (!postRec(f, c, &p) || !tfxRgbZero(d, &p) || !blendKeepsAtCs0(d) ||
                d->test.date != RD_DATE_OFF) {
                fail(RD_GLOW_LIGHT_WHY_FORM, "the paste is not an additive of WORK3");
                return;
            }
            if (add(c, ord, E_PASTE)) {
                const uint32_t reg = rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : 0);
                s_gl.pasteFix = ((reg >> 4) & 3u) == 2u;
                s_gl.phase = PH_AFTER;
                s_gl.written = 0; /* from here a read before a write reads the chain's */
            }
            return;
        }
        fail(RD_GLOW_LIGHT_WHY_FORM, "WORK0's alpha not followed by the paste");
        return;
    case PH_AFTER:
    case PH_DONE:
        if (post && !d->texEnabled && (tgt == pad || tgt == w2)) {
            fail(RD_GLOW_LIGHT_WHY_FORM, "a second glow in the frame");
            return;
        }
        if (readBeforeWrite(c, st)) {
            fail(RD_GLOW_LIGHT_WHY_WORK, "WORK1, WORK2 or WORK3 read after the paste");
            return;
        }
        if (s_gl.phase == PH_AFTER) {
            if (readsTarget(c, st, scene)) {
                fail(RD_GLOW_LIGHT_WHY_SCENE, "SCENE read before a sprite rewrites its alpha");
                return;
            }
            if (rewritesSceneAlpha(f, c, st)) {
                s_gl.phase = PH_DONE;
            }
        }
        return;
    default:
        return;
    }
fill:
    if ((e = add(c, ord, E_FILL)) == NULL) {
        return;
    }
    if (!coverWrite(f, c, st, &p)) {
        fail(RD_GLOW_LIGHT_WHY_FORM, "WORK2's fill does not cover it");
        return;
    }
    if (p.rgba[0] || p.rgba[1] || p.rgba[2]) {
        /* the bloom (postEffect 4 and 5): light in the fill, and its blur
         * passes add RGBAQ's alpha: the chain is drawn whole, said as such
         * before any later form refuses it */
        s_gl.grey = true;
        fail(RD_GLOW_LIGHT_WHY_GREY, "WORK2's fill is grey");
        return;
    }
    e->target = w2;
    spriteColour(d, &p, e->col);
    s_gl.phase = PH_FILL;
}

void rd__glow_light_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                             uint32_t ord)
{
    if (!s_gl.scanning || state || c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT) {
        return;
    }
    noteFirst(f, c, st);
    const uint32_t n0 = s_gl.n;
    const bool before = !s_gl.chainSeen;
    scanStep(f, c, st, ord);
    if (s_gl.n > n0) {
        s_gl.chainSeen = true;
        return; /* one of the chain's commands */
    }
    if (writesTarget(c, st, idOf(RD_TARGET_WORK1))) {
        /* a clear of WORK1 to (0, 0, 0, 0) before the chain leaves the zero
         * region zero (the tests clear every target at a frame's head) */
        const bool zero = c->type == RDC_CLEAR && !c->b[0] && !c->b[1] && !c->b[2] && !c->b[3];
        if (before && zero) {
            s_gl.w1Before = false;
            s_gl.w1Cleared = true;
        } else if (before) {
            s_gl.w1Before = true;
            s_gl.w1Cleared = false;
        } else {
            s_gl.w1After = true;
        }
    }
    if (!s_gl.chainSeen || (c->type == RDC_POST_STUB && rd__is_blur_kind(c->b[0]))) {
        return; /* before the chain, or a sprite rd__glow_light_sprite sees */
    }
    for (int k = 0; k < LZ_COUNT; k++) {
        const uint32_t id = lazyId(k);
        if (readsTarget(c, st, id) || writesTarget(c, st, id) || st->depth == id) {
            s_gl.unhooked[k] = true;
        }
    }
}

/* WORK1's zero region still zero over this frame's first blur pass */
static bool w1Zero(void)
{
    const RdTargetRec *t = rd__target_rec(idOf(RD_TARGET_WORK1));
    if (s_gl.w1Cleared && !s_gl.w1Before) {
        return true; /* the frame's own clear zeroed all of WORK1 */
    }
    return s_w1.valid && t && t->color.id == s_w1.tex && !t->clearPending && !s_gl.w1Before &&
           s_w1.gsW == s_gl.p1GsW && memcmp(s_w1.rect, s_gl.p1Rect, sizeof(s_w1.rect)) == 0;
}

/* WORK1's state for the replays after this one */
static void w1Track(uint8_t plan)
{
    const RdTargetRec *t = rd__target_rec(idOf(RD_TARGET_WORK1));
    if (plan == RD_GLOW_LIGHT_DARK && !s_gl.w1After && t && t->color.id && s_gl.p1GsW) {
        s_w1.valid = true; /* zeroed now, or zero already */
        s_w1.tex = t->color.id;
        s_w1.gsW = s_gl.p1GsW;
        memcpy(s_w1.rect, s_gl.p1Rect, sizeof(s_w1.rect));
    } else if (plan != RD_GLOW_LIGHT_NONE || s_gl.w1Before || s_gl.w1After) {
        s_w1.valid = false; /* the chain or another draw wrote WORK1 */
    }
}

/* the planned acts of the elements under plan */
static void planActs(uint8_t plan)
{
    const uint32_t w0 = idOf(RD_TARGET_WORK0), w3 = idOf(RD_TARGET_WORK3);
    const uint32_t w2 = idOf(RD_TARGET_WORK2);
    bool hold = false, firstP1 = true;
    for (uint32_t i = 0; i < s_gl.n; i++) {
        hold = hold || s_gl.cmd[i].el == E_HOLD;
    }
    for (uint32_t i = 0; i < s_gl.n; i++) {
        GlCmd *e = &s_gl.cmd[i];
        e->act = ACT_DRAW;
        if (plan == RD_GLOW_LIGHT_LAMPS) {
            if (e->el == E_FILL) {
                e->act = ACT_ALPHA0;
            } else if (e->el == E_MASK) {
                e->act = ACT_SKIP;
            }
            continue;
        }
        switch (e->el) {
        case E_PAD:
            e->act = !e->cover ? ACT_DRAW : s_gl.unhooked[LZ_PAD] ? ACT_CLEAR : ACT_LAZY;
            break;
        case E_FILL:
            e->act = s_gl.unhooked[LZ_W2] ? ACT_CLEAR : ACT_LAZY;
            e->target = w2;
            e->col[3] = 0; /* the mask's alpha where no background is marked */
            break;
        case E_P1:
            /* the first pass zeroes WORK1 over the rect every pass into it
             * lies in: not drawn when a dark plan before left that region
             * zero and nothing wrote WORK1 since */
            e->act = firstP1 && !w1Zero() ? ACT_PLAN : ACT_SKIP;
            e->target = w0;
            firstP1 = false;
            break;
        case E_SHRINK:
            e->act = ACT_PLAN;
            e->target = w3;
            break;
        case E_EYE:
            e->act = hold || s_gl.unhooked[LZ_W3] ? ACT_CLEAR : ACT_LAZY;
            e->target = w3;
            memset(e->col, 0, sizeof(e->col));
            break;
        case E_W0:
            /* WORK0 (0, 0, 0, A) lazily: the first sprite after that touches
             * it draws the clear first, as the load of its own pass (the
             * aura's, in play), else a later replay or a read does */
            e->act = s_gl.unhooked[LZ_W0] ? ACT_CLEAR : ACT_LAZY;
            e->target = w0;
            break;
        case E_HOLD:
        case E_LAMP:
            e->act = ACT_DRAW;
            break;
        default: /* fans, mask, reduction, second passes, ghosts, paste */
            e->act = ACT_SKIP;
            break;
        }
    }
}

/* the DARK plan's own checks: the first pass's rect holds every later
 * pass's into WORK1, the last pair's second pass covers WORK0 */
static const char *darkForm(void)
{
    const GlCmd *p0 = NULL;
    const GlCmd *last2 = NULL;
    for (uint32_t i = 0; i < s_gl.n; i++) {
        const GlCmd *e = &s_gl.cmd[i];
        if (e->el == E_P1) {
            if (!p0) {
                p0 = e;
            } else if (e->rect[0] < p0->rect[0] || e->rect[1] < p0->rect[1] ||
                       e->rect[2] > p0->rect[2] || e->rect[3] > p0->rect[3]) {
                return "a blur pass into WORK1 outside the first one's rect";
            }
        } else if (e->el == E_P2) {
            last2 = e;
        }
    }
    if (!p0 || !last2 || !last2->cover) {
        return "the last blur pass does not cover WORK0";
    }
    return NULL;
}

static void logOnce(uint8_t plan, uint8_t why, const char *detail)
{
    static bool saidPlan[RD_GLOW_LIGHT_COUNT], saidWhy[RD_GLOW_LIGHT_WHY_COUNT];
    if (plan == RD_GLOW_LIGHT_WHOLE) {
        if (why < RD_GLOW_LIGHT_WHY_COUNT && !saidWhy[why]) {
            saidWhy[why] = true;
            rd__log("glow: light plan: the chain drawn whole: %s (%s; frame %u; reported once "
                    "for this reason)",
                    kWhy[why], detail && detail[0] ? detail : "-", s_gl.frameNo);
        }
        return;
    }
    if (plan >= RD_GLOW_LIGHT_COUNT || saidPlan[plan]) {
        return;
    }
    saidPlan[plan] = true;
    if (plan == RD_GLOW_LIGHT_DARK) {
        rd__log("glow: light plan: above 1x nothing in frame %u puts light into the glow (WORK2's "
                "fill RGB 0, no sun fans on the picture, no lamps): its chain not drawn (%u "
                "commands), its buffers left with the chain's colours (WORK0's alpha sprite as "
                "its clear, WORK1's first blur pass in its colour, WORK2, its band and WORK3 "
                "cleared where read); reported once",
                s_gl.frameNo, s_gl.n);
    } else if (plan == RD_GLOW_LIGHT_LAMPS) {
        rd__log("glow: light plan: above 1x frame %u's light in the glow is the lamps' (%u draws "
                "into WORK2 after the mask): the fill drawn as (0, 0, 0, 0), the mask not drawn, "
                "the rest of the chain as before; reported once",
                s_gl.frameNo, s_gl.lamps);
    }
}

void rd__glow_light_scan_end(void)
{
    if (!s_gl.scanning) {
        return;
    }
    s_gl.scanning = false;
    s_gl.scanned = true;
    s_gl.scanReplay = g_rd.replayCounter;
    if (!s_gl.glow) {
        w1Track(RD_GLOW_LIGHT_NONE);
        return; /* no glow: nothing planned or counted */
    }
    const RdTargetRec *sc = rd__target_rec(idOf(RD_TARGET_SCENE));
    const bool above1x = sc && (sc->sx != 1.0f || sc->sy != 1.0f);
    if (!above1x || !rd__glow_light_on() || g_rdWin.first) {
        g_rdPerf.glowLight = RD_GLOW_LIGHT_OFF;
        w1Track(RD_GLOW_LIGHT_OFF);
        return; /* Original at 1x: the PS2's bytes in every buffer */
    }
    if (s_gl.readFirst && s_stale && !s_off) {
        s_off = true;
        rd__log("glow: light plan: frame %u reads WORK1, WORK2 or WORK3 before the glow's chain "
                "draws them, after a frame whose chain the plan left undrawn: the plan is off "
                "from now on",
                s_gl.frameNo);
    }
    uint8_t plan = RD_GLOW_LIGHT_DARK, why = RD_GLOW_LIGHT_WHY_NONE;
    const char *detail = NULL;
    if (s_off) {
        why = RD_GLOW_LIGHT_WHY_OFF;
    } else if (s_gl.phase == PH_FAIL) {
        why = s_gl.why;
        detail = s_gl.detail;
    } else if (s_gl.phase == PH_AFTER) {
        why = RD_GLOW_LIGHT_WHY_SCENE;
        detail = "SCENE's alpha not rewritten in the frame";
    } else if (s_gl.phase != PH_DONE) {
        why = RD_GLOW_LIGHT_WHY_FORM;
        detail = "the chain not finished in the frame";
    } else if (s_gl.grey) {
        why = RD_GLOW_LIGHT_WHY_GREY;
    } else if (rd_aa_samples(rd__aa_mode()) > 1u) {
        why = RD_GLOW_LIGHT_WHY_MSAA;
    } else if (s_gl.fansOn) {
        why = RD_GLOW_LIGHT_WHY_FANS;
    } else if (s_gl.lamps && s_gl.sun) {
        why = RD_GLOW_LIGHT_WHY_SUN;
        detail = "the sun's tint reads the lamps' WORK1 at the alpha the mask decides";
    } else if (s_gl.lamps && !s_gl.pasteFix) {
        why = RD_GLOW_LIGHT_WHY_FORM;
        detail = "the paste's blend reads WORK3's alpha, which the lamps' plan changes";
    } else if (s_gl.lamps) {
        plan = RD_GLOW_LIGHT_LAMPS;
    } else if ((detail = darkForm()) != NULL) {
        why = RD_GLOW_LIGHT_WHY_FORM;
    }
    if (why != RD_GLOW_LIGHT_WHY_NONE) {
        plan = RD_GLOW_LIGHT_WHOLE;
    }
    s_gl.plan = plan;
    s_gl.replay = g_rd.replayCounter;
    g_rdPerf.glowLight = plan;
    g_rdPerf.glowLightWhy = why;
    s_count[plan]++;
    s_whyCount[why]++;
    s_stale = plan != RD_GLOW_LIGHT_WHOLE;
    if (plan != RD_GLOW_LIGHT_WHOLE) {
        planActs(plan);
    }
    w1Track(plan);
    logOnce(plan, why, detail);
}

bool rd__glow_light_planned(void)
{
    return s_gl.replay == g_rd.replayCounter &&
           (s_gl.plan == RD_GLOW_LIGHT_DARK || s_gl.plan == RD_GLOW_LIGHT_LAMPS);
}

/* --------------------------------------------------------- the replay's */

static GlCmd *find(const RdCmd *c)
{
    if (!rd__glow_light_planned()) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_gl.n; i++) {
        if (s_gl.cmd[i].c == c && s_gl.cmd[i].ord == g_rdStoreOrd) {
            return &s_gl.cmd[i];
        }
    }
    return NULL;
}

static uint32_t lazyId(int k)
{
    static const int kNamed[LZ_COUNT] = {RD_TARGET_WORK2_PAD, RD_TARGET_WORK2, RD_TARGET_WORK3,
                                         RD_TARGET_WORK0};
    return k >= 0 && k < LZ_COUNT ? idOf(kNamed[k]) : 0u;
}

static int lazySlot(uint32_t id)
{
    for (int k = 0; k < LZ_COUNT; k++) {
        if (id && lazyId(k) == id) {
            return k;
        }
    }
    return -1;
}

/* a lazy clear noted: target id holds col until it is read; false when it
 * cannot be (no texture yet): the caller draws the clear */
static bool lazyNote(uint32_t id, const uint8_t col[4])
{
    const int k = lazySlot(id);
    const RdTargetRec *t = rd__target_rec(id);
    if (k < 0 || !t || !t->color.id) {
        return false;
    }
    s_lazy[k].on = true;
    s_lazy[k].id = id;
    s_lazy[k].tex = t->color.id;
    for (int i = 0; i < 4; i++) {
        s_lazy[k].col[i] = (float)col[i] / 255.0f;
        s_lazy[k].rgba[i] = col[i];
    }
    return true;
}

/* lazy clear k recorded on cl as the pass a clear of its own begins */
static void lazyRecord(RhiCommandList cl, int k)
{
    RdTargetRec *t = rd__target_rec(s_lazy[k].id);
    if (t && t->color.id == s_lazy[k].tex && !t->clearPending) {
        const bool readable = rd__pass_wants_readable(t);
        rd__transition(cl, t->color, &t->colorState,
                       readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = t->color;
        p.color[0].load = RHI_LOAD_CLEAR;
        p.color[0].readable = readable;
        memcpy(p.color[0].clear, s_lazy[k].col, sizeof(p.color[0].clear));
        p.colorCount = 1;
        p.width = t->tw;
        p.height = t->th;
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_end_render_pass(cl);
        g_rdPerf.glowLightLazy++;
    }
    s_lazy[k].on = false;
}

/* the lazy target k this replay leaves lazy again (its plan's act on it) */
static bool planLazies(int k)
{
    if (!rd__glow_light_planned()) {
        return false;
    }
    const uint32_t id = lazyId(k);
    for (uint32_t i = 0; i < s_gl.n; i++) {
        if (s_gl.cmd[i].act == ACT_LAZY && s_gl.cmd[i].target == id) {
            return true;
        }
    }
    return false;
}

void rd__glow_light_lazy_begin(void)
{
    for (int k = 0; k < LZ_COUNT; k++) {
        if (!s_lazy[k].on) {
            continue;
        }
        const RdTargetRec *t = rd__target_rec(s_lazy[k].id);
        if (!t || t->color.id != s_lazy[k].tex || t->clearPending) {
            s_lazy[k].on = false; /* a new texture: cleared before the walk */
            continue;
        }
        const bool scanned = s_gl.scanned && s_gl.scanReplay == g_rd.replayCounter;
        if (scanned && s_gl.first[k] == FT_NONE) {
            continue; /* not touched by this replay */
        }
        if (scanned && s_gl.first[k] == FT_COVER) {
            if (!planLazies(k)) {
                s_lazy[k].on = false; /* covered before anything reads it */
            }
            continue;
        }
        lazyRecord(rd__replay_cl(), k);
    }
}

void rd__glow_light_readback(uint32_t id)
{
    const int k = lazySlot(id);
    if (k < 0 || !s_lazy[k].on || s_lazy[k].id != id) {
        return;
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return;
    }
    lazyRecord(cl, k);
    rhi_end_commands(cl);
    rhi_submit(cl);
}

void rd__glow_light_reset(void)
{
    memset(s_lazy, 0, sizeof(s_lazy));
    memset(&s_w1, 0, sizeof(s_w1));
    s_stale = false;
}

bool rd__glow_light_skip(uint32_t ord)
{
    if (rd__fades_fold_skip(ord)) {
        return true; /* the fades fold's (rd_fades_fold.c) */
    }
    if (!rd__glow_light_planned()) {
        return false;
    }
    for (uint32_t i = 0; i < s_gl.n; i++) {
        if (s_gl.cmd[i].ord == ord && s_gl.cmd[i].el == E_FAN && s_gl.cmd[i].act == ACT_SKIP) {
            return true;
        }
    }
    return false;
}

bool rd__glow_light_src(uint32_t srcId, uint8_t rgba[4])
{
    if (!s_gl.planned || s_gl.plannedSrc != srcId) {
        return false;
    }
    memcpy(rgba, s_gl.plannedCol, 4);
    return true;
}

/* command c drawn from a private record p (the colour changed) in state st */
static void drawAs(Replay *r, const RdFrame *f, const RdCmd *c, const RdPostRec *p,
                   const RdStateBlock *st)
{
    static RdPostRec s_rec;
    static RdFrame s_sf;
    s_rec = *p;
    s_sf = *f;
    s_sf.payload = (uint8_t *)&s_rec;
    s_sf.payloadSize = (uint32_t)sizeof(s_rec);
    RdCmd sc = *c;
    sc.u[1] = 0;
    sc.u[2] = (uint32_t)sizeof(RdPostRec);
    const RdStateBlock keep = r->st;
    r->st = *st;
    rd__replay_blur_sprite_draw(r, &s_sf, &sc);
    r->st = keep;
}

/* command c drawn untextured without a blend in e's colour: a clear of
 * its target (rd_replay.c blurSpriteClears takes a covering sprite so) */
static void drawClear(Replay *r, const RdFrame *f, const RdCmd *c, const GlCmd *e, RdPostRec *p)
{
    RdStateBlock st = r->st;
    st.ds.texEnabled = 0;
    st.ds.abe = 0;
    st.ds.fba = 0;
    memcpy(p->rgba, e->col, sizeof(p->rgba));
    drawAs(r, f, c, p, &st);
}

/* lazy clear k drawn now as a covering untextured sprite without a blend
 * (rd_replay.c blurSpriteClears makes it a pending clear: the load op of
 * the next pass on the target, the sprite's that touches it) */
static void realise(Replay *r, const RdFrame *f, const RdCmd *c, int k)
{
    RdTargetRec *t = rd__target_rec(s_lazy[k].id);
    s_lazy[k].on = false;
    if (!t || t->color.id != s_lazy[k].tex || t->clearPending) {
        return; /* a new texture: cleared before the walk */
    }
    RdStateBlock st = r->st;
    st.ds.texEnabled = 0;
    st.ds.abe = 0;
    st.ds.fba = 0;
    st.ds.test.ate = 0;
    st.ds.test.date = RD_DATE_OFF;
    st.ds.test.zte = 0;
    st.ds.zwrite = RD_ZWRITE_OFF;
    st.ds.colorMask = 0xF;
    st.ds.fbmsk = 0;
    st.tex = 0;
    st.color = s_lazy[k].id;
    st.depth = 0;
    st.gsW = t->w;
    st.gsH = t->h;
    st.useOffset = 0;
    st.scissor[0] = st.scissor[1] = 0;
    st.scissor[2] = (int32_t)t->w - 1;
    st.scissor[3] = (int32_t)t->h - 1;
    int32_t ox16, oy16;
    origin16(&st, &ox16, &oy16);
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)ox16;
    p.rect[1] = (float)oy16;
    p.rect[2] = (float)(ox16 + 16 * (int32_t)t->w);
    p.rect[3] = (float)(oy16 + 16 * (int32_t)t->h);
    memcpy(p.rgba, s_lazy[k].rgba, sizeof(p.rgba));
    p.scalar[0] = p.scalar[1] = 1024.0f;
    p.scalar[2] = 1.0f;
    p.lutOffset = ~0u;
    RdCmd sc = *c;
    sc.type = RDC_POST_STUB;
    sc.b[0] = RD_POST_FLARE;
    const int half = r->halfAdded;
    r->halfAdded = 0; /* a work buffer: no frame head's half pixel */
    drawAs(r, f, &sc, &p, &st);
    r->halfAdded = half;
    g_rdPerf.glowLightLazy++;
}

/* a sprite the plan draws as recorded (or not a chain's) that touches a
 * lazy target: a covering write that does not read it forgets the clear,
 * anything else has it drawn first */
static void lazyTouch(Replay *r, const RdFrame *f, const RdCmd *c)
{
    for (int k = 0; k < LZ_COUNT; k++) {
        if (!s_lazy[k].on) {
            continue;
        }
        const uint32_t id = s_lazy[k].id;
        const bool reads =
            texTarget(&r->st) == id || r->st.depth == id ||
            (r->st.color == id && (r->st.ds.test.date != RD_DATE_OFF || blendReadsAd(&r->st.ds)));
        if (!reads && r->st.color != id) {
            continue;
        }
        RdPostRec p;
        if (!reads && coverWrite(f, c, &r->st, &p)) {
            s_lazy[k].on = false; /* every texel written again, nothing read */
            continue;
        }
        realise(r, f, c, k);
    }
}

bool rd__glow_light_sprite(Replay *r, const RdFrame *f, const RdCmd *c)
{
    GlCmd *e = find(c);
    s_gl.inRepeat = false;
    if (e && e == &s_gl.cmd[0] && !s_gl.spanOpen) {
        s_gl.spanOpen = true; /* the chain's passes counted from here */
        s_gl.spanFrom = s_gl.spanLast = r->passSerial;
        s_gl.spanLastEl = e->el;
        s_gl.glowLightOwn = 0;
        s_dbgAt = 0;
        s_dbg[0] = '\0';
    } else if (e && s_gl.spanOpen) {
        /* the passes begun since the last chain command: between the two
         * (the list loop's own work), and in SCENE's border repeat just
         * before this sprite (rd_blur_border.c sceneRepeatDue: SCENE's, not
         * the glow's: not counted) */
        const uint32_t pre = s_gl.preSerial >= s_gl.spanLast ? s_gl.preSerial : s_gl.spanLast;
        s_gl.glowLightOwn += pre - s_gl.spanLast;
        s_gl.spanLast = r->passSerial;
        s_gl.spanLastEl = e->el;
    }
    if (e && e->el == E_PASTE && s_gl.spanOpen) {
        s_gl.spanOpen = false;
        g_rdPerf.glowLightPasses = s_gl.glowLightOwn;
        if (s_gl.plan == RD_GLOW_LIGHT_DARK) {
            s_darkPasses += g_rdPerf.glowLightPasses;
        }
    }
    if (!e || e->act == ACT_DRAW) {
        lazyTouch(r, f, c);
        return false;
    }
    RdPostRec p;
    if (!postRec(f, c, &p)) {
        return false;
    }
    switch (e->act) {
    case ACT_SKIP:
        r->writeSerial++; /* as the sprite's draw (DATE's snapshot reuse) */
        return true;
    case ACT_PLAN:
        s_gl.planned = true;
        s_gl.plannedSrc = e->target;
        memset(s_gl.plannedCol, 0, sizeof(s_gl.plannedCol));
        rd__blur_post_planned(r, f, c); /* the sprite and its glow edge strip */
        s_gl.planned = false;
        return true;
    case ACT_LAZY:
        if (lazyNote(e->target, e->col)) {
            r->writeSerial++;
            return true;
        }
        drawClear(r, f, c, e, &p); /* no texture to hold it: drawn as the clear */
        return true;
    case ACT_CLEAR:
        drawClear(r, f, c, e, &p);
        return true;
    case ACT_ALPHA0:
        p.rgba[3] = 0;
        drawAs(r, f, c, &p, &r->st);
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------ the lines */

void rd_glow_light_line(void)
{
    const uint64_t dark = s_count[RD_GLOW_LIGHT_DARK], lamps = s_count[RD_GLOW_LIGHT_LAMPS];
    const uint64_t whole = s_count[RD_GLOW_LIGHT_WHOLE];
    if (dark + lamps + whole == 0) {
        s_darkPasses = 0;
        memset(s_count, 0, sizeof(s_count));
        memset(s_whyCount, 0, sizeof(s_whyCount));
        return;
    }
    char why[512] = "";
    size_t at = 0;
    for (int k = 1; k < RD_GLOW_LIGHT_WHY_COUNT; k++) {
        if (!s_whyCount[k]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)s_whyCount[k], kWhy[k]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    fprintf(stderr,
            "window: glow: light: %llu dark (%llu passes in their chains), %llu lamps (mask not "
            "drawn), %llu whole%s%s\n",
            (unsigned long long)dark, (unsigned long long)s_darkPasses, (unsigned long long)lamps,
            (unsigned long long)whole, why, at ? ")" : "");
    s_darkPasses = 0;
    memset(s_count, 0, sizeof(s_count));
    memset(s_whyCount, 0, sizeof(s_whyCount));
}
