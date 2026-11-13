/* rd_no_volume.c: a shadow window with no volume keeps SCENE's pass open
 * (rd_no_volume.h; docs/port/RENDER_POST.md "A shadow window with no
 * volume").
 *
 * Shadow.c resets the stencil (RDC_SHADOW_RESET), draws the volumes
 * (RDC_SHADOW_STRIP), resolves the count into the count target
 * (RDC_SHADOW_RESOLVE), blurs it through SHADOW0, 1 and 2 (three level
 * sprites) and composites the levels into SCENE (three sprites).  The
 * replay ends SCENE's pass at the resolve (rd_shadow_one.c ends any open
 * pass before the count) and begins it again at the first composite: the
 * scene's colour, depth and stencil are stored and loaded again (about 44 MB
 * at 2364x1080 on a tile-based GPU) and four small passes run.  Many title
 * frames hold a window with no strip at all (the corpus boot frames
 * rd-00400 and rd-00600: 3:6 the reset, 3:13 the resolve).
 *
 * Exact.  With no strip between the reset and the resolve, the stencil's
 * bits 0-5 are what the reset left, 0, so every count texel is (0, 0, 0, 0)
 * on every path (the one draw's average of +0.0 terms, the boxed one draw's
 * CLEAR, the seven draws' CLEAR load with no texel passing their stencil
 * tests, the reduction of a zero count).  A level (rd_shadow_box.c's form:
 * one sprite covering its whole target at scale 1, no blend, FBA 0, every
 * channel, MODULATE or DECAL with TCC 1, no alpha test, DATE or Z test, here
 * also no depth written) whose every tap reads 0 writes (0, 0, 0, 0) on every
 * texel of its target: (0 x Cv) >> 7 and 0 under MODULATE, 0 under DECAL.
 * Its vertices at replay are the payload's (rd__layer_keep_verts acts on a
 * keep frame's list 11 composite only, extendFullFrame on frame-sized
 * targets only).  A composite (an alpha test that fails on As 0, AFAIL KEEP,
 * no Z write) whose taps read alpha 0 writes nothing.  So the chain's whole
 * effect is: the count (at scale 1 the count target, above it the reduced
 * count rd_replay.c's s_shadowRed holds) and each level target hold (0, 0,
 * 0, 0), SCENE and its depth untouched.  The walk records exactly that: the
 * reset and the resolve record nothing, the levels and composites are not
 * drawn (rd__no_volume_skip), and each target the chain would have written
 * is owed a zero, recorded at the next end of a pass the walk makes anyway
 * (rd__no_volume_flush from rd_replay.c endPass, after the pending clears,
 * and at the walk's end) as a pass with a CLEAR (0, 0, 0, 0) load and no
 * draw, each target then put in SHADER_READ as the chain's last reader left
 * it.  The scan proves nothing else touches those targets between the
 * resolve and the walk's end (no draw on them, no texture of them, no clear,
 * copy, exact blend or post record naming them, no command it does not
 * know), so a clear recorded later leaves the same bytes for every reader:
 * the pass dumps, the readbacks (rd__shadow_count_read), the next replay.
 * The stencil's bits 0-5 are read only by a window's volumes and resolve
 * (rd_shadow_merge.c), and the next window resets them first: skipping the
 * reset leaves nothing any later command reads (DATE's set-ups and AA1's
 * pairs write bit 6 alone).  The write serial moves at the resolve, each
 * level and each composite as the drawn ones' may, so the next DATE draw
 * takes its own snapshot or stencil set-up from the alpha as it is (a
 * retake reads the same alpha; nothing it would have reused was written).
 *
 * The break mode (v0.11.6, RD_NO_VOLUME_BREAK_DEFAULT 0, ICO_RD_NO_VOLUME_BREAK
 * for the tests): the same window, but the reset is recorded as before (the
 * merged pass's stencil store armed as before: rd__no_volume_owns says no)
 * and the resolve ends the open pass as the count's draw ends it
 * (rd_shadow_one.c rd__replay_end_pass: the reset's pending stencil clear
 * recorded as before) and closes the shadow window as doShadowResolve does
 * after the count; the levels and composites are skipped and the zeros owed
 * as above.  So SCENE's pass ends where it ends today and is begun again by
 * its next draw with LOAD, as the first composite began it; what is left out
 * is the count's pass, the levels' passes and the composites' DATE set-ups.
 * A target that holds zero already is owed no clear: the zero clears the
 * replay before recorded and the targets it found holding zero (s_zero) are
 * read by the next replay alone (the replay counter one more: no own frame
 * or other replay between), for a target no command before its window's
 * resolve names (zeroTouch), whose texture is the same and in SHADER_READ as
 * the clear's pass left it (heldZero).  ICO_RD_NO_VOLUME_POISON=1 (tests):
 * the owed clears magenta and none left out, so a reader shows.
 *
 * Which device classes (the gate, rd_nv_gate): off by default on every
 * device in this release (RD_NO_VOLUME_DEFAULT 0 in classOn: rc14 and rc15
 * on the owner's PowerVR phone flickered black over the right part of the
 * picture at the title and on the pause menu, where windows with no volume
 * are taken; the cause is not found: lavapipe gives the same bytes on the
 * pause menu's F12 frame, with the store poison, the stencil cut and the
 * validation layer's sync checks too).  With the constant 1 it would be on
 * where a pass ends and begins at a cost, the tile-based classes under
 * Vulkan whose composites take DATE through the stencil bit: the PowerVR
 * class and MoltenVK; off on the Mali and Adreno classes (issues 123, 179,
 * 211), on D3D, OpenGL and the desktop.  Refused where a composite's DATE would take the R8 snapshot
 * (DATE's passes without the depth, rd_date_cut.c, predict their runs from
 * those composites), under MSAA (the count in the epoch), the tool's pass
 * window, the exact blend and GS state as shader data.  ICO_RD_NO_VOLUME=0|1
 * (tests only): off, or on whatever the class.  No new kind of GPU work: a
 * colour pass with a CLEAR load and no draw is what rd_replay.c flushClear
 * and the boxed empty count already record on every device, and the rest is
 * passes not begun, so no start-up self-test: what it adds is fewer passes
 * of the kinds the device draws today.
 *
 * The lines:
 *   rd: shadows: no volume: on|off (<why>) ...           (once a renderer)
 *   rd: shadows: no volume: frame F, the window at command N: ... (once)
 *   rd: shadows: no volume: a window not taken in frame F: <why> (the first
 *       8, then every 100th)
 *   window: per replay ...; no volume: K windows a replay without a volume
 *       kept SCENE's pass open (O with it open, up to M MB not stored and
 *       loaded again), V with volumes, R not taken (why n, ...)
 *   the replay tool: --stats "no volume on|off: K kept (...), V with
 *       volumes, R not taken (...)" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_date_one.h"
#include "rd_no_volume.h"
#include "rd_replay.h"
#include "rd_shadow_box.h"
#include "rd_stencil_selftest.h" /* rd__profile_powervr */

/* ------------------------------------------------------------- the scan */

static bool chainHas(const RdNvScan *s, uint32_t id)
{
    for (unsigned i = 0; id && i < s->nChain; i++) {
        if (s->chain[i] == id) {
            return true;
        }
    }
    return false;
}

static bool clearedHas(const RdNvScan *s, uint32_t id)
{
    if (s->clearedFull) {
        return true;
    }
    for (unsigned i = 0; i < s->nCleared; i++) {
        if (s->cleared[i] == id) {
            return true;
        }
    }
    return false;
}

static void decide(RdNvScan *s, uint8_t result, uint8_t why)
{
    s->phase = 3;
    s->result = result;
    s->why = why;
}

/* a command after the resolve touching the chain other than through src */
static bool touchesChain(const RdNvScan *s, const RdNvCmd *c, uint32_t src)
{
    if (chainHas(s, c->color) || chainHas(s, c->depth)) {
        return true;
    }
    for (int k = 0; k < RD_NV_NAMES; k++) {
        if (c->names[k] && c->names[k] != src && chainHas(s, c->names[k])) {
            return true;
        }
    }
    return false;
}

static bool skipAdd(RdNvScan *s, uint32_t ord, uint8_t kind)
{
    if (s->nSkip >= RD_NV_SKIPS) {
        return false;
    }
    s->skip[s->nSkip].ord = ord;
    s->skip[s->nSkip].kind = kind;
    s->nSkip++;
    return true;
}

void rd_nv_scan_begin(RdNvScan *s)
{
    memset(s, 0, sizeof(*s));
}

void rd_nv_scan_step(RdNvScan *s, const RdNvCmd *c)
{
    if (s->phase == 3 || c->kind == RD_NV_NONE) {
        return;
    }
    if (c->clears && s->phase < 2 && c->names[0] && !clearedHas(s, c->names[0])) {
        if (s->nCleared < RD_NV_CLEARS) {
            s->cleared[s->nCleared++] = c->names[0];
        } else {
            s->clearedFull = true;
        }
    }
    if (s->phase == 0) {
        if (c->kind == RD_NV_RESET) {
            s->phase = 1;
            s->resetOrd = c->ord;
            s->count = c->color;
            s->depth = c->depth;
            s->strip = false;
        } else if (c->kind == RD_NV_STRIP || c->kind == RD_NV_RESOLVE) {
            decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_FORM); /* a volume or a resolve unreset */
        }
        return;
    }
    if (s->phase == 1) {
        if (c->kind == RD_NV_STRIP) {
            s->strip = true; /* a volume: the window keeps today's passes */
            return;
        }
        if (c->kind != RD_NV_RESOLVE || c->color != s->count || c->depth != s->depth) {
            /* a draw inside the window, a reset again, a resolve elsewhere */
            decide(s, s->strip ? RD_NV_RES_VOLUME : RD_NV_RES_REFUSED, RD_NV_WHY_FORM);
            return;
        }
        if (s->strip) {
            decide(s, RD_NV_RES_VOLUME, 0);
            return;
        }
        if (clearedHas(s, s->count)) {
            decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_CHAIN);
            return;
        }
        s->phase = 2;
        s->resolveOrd = c->ord;
        s->chain[0] = s->count;
        s->nChain = 1;
        return;
    }
    /* after the resolve */
    switch (c->kind) {
    case RD_NV_RESET:
    case RD_NV_STRIP:
    case RD_NV_RESOLVE:
        decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_FORM); /* a second window */
        return;
    case RD_NV_UNKNOWN:
        decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_UNKNOWN);
        return;
    case RD_NV_LEVEL:
        if (chainHas(s, c->src)) {
            if (!c->formOk) {
                decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_FORM);
                return;
            }
            if (!c->color || touchesChain(s, c, c->src) || clearedHas(s, c->color) ||
                s->nChain >= RD_NV_CHAIN || !skipAdd(s, c->ord, RD_NV_LEVEL)) {
                decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_CHAIN);
                return;
            }
            s->chain[s->nChain++] = c->color; /* a source from here */
            return;
        }
        break;
    case RD_NV_COMPOSITE:
        if (chainHas(s, c->src)) {
            if (!c->formOk) {
                decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_DATE);
                return;
            }
            if (touchesChain(s, c, c->src) || !skipAdd(s, c->ord, RD_NV_COMPOSITE)) {
                decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_CHAIN);
            }
            return;
        }
        break;
    default:
        break;
    }
    if (touchesChain(s, c, 0)) {
        decide(s, RD_NV_RES_REFUSED, RD_NV_WHY_CHAIN);
    }
}

int rd_nv_scan_end(RdNvScan *s)
{
    if (s->phase == 0) {
        decide(s, RD_NV_RES_NONE, 0);
    } else if (s->phase == 1) {
        decide(s, s->strip ? RD_NV_RES_VOLUME : RD_NV_RES_REFUSED, RD_NV_WHY_FORM);
    } else if (s->phase == 2) {
        decide(s, RD_NV_RES_EMPTY, 0);
    }
    return s->result;
}

int rd_nv_skip_kind(const RdNvScan *s, uint32_t ord)
{
    if (s->result != RD_NV_RES_EMPTY) {
        return RD_NV_NONE;
    }
    for (unsigned i = 0; i < s->nSkip; i++) {
        if (s->skip[i].ord == ord) {
            return s->skip[i].kind;
        }
    }
    return RD_NV_NONE;
}

int rd_nv_gate(int env, bool classOn, bool msaa, bool passWindow, bool exactBlend, bool uber,
               bool dateStencil)
{
    if (env == 0) {
        return RD_NV_WHY_OFF;
    }
    if (env != 1 && !classOn) {
        return RD_NV_WHY_CLASS;
    }
    if (msaa || passWindow || exactBlend || uber) {
        return RD_NV_WHY_SETTING;
    }
    if (!dateStencil) {
        return RD_NV_WHY_DATE;
    }
    return RD_NV_WHY_COUNT;
}

const char *rd_nv_why_words(int why)
{
    switch (why) {
    case RD_NV_WHY_OFF:
        return "ICO_RD_NO_VOLUME=0";
    case RD_NV_WHY_CLASS:
        return "the default in this release: the PowerVR phone flickered black with it on, cause not "
               "found";
    case RD_NV_WHY_DATE:
        return "the composites' DATE by the R8 snapshot or the shader's read";
    case RD_NV_WHY_SETTING:
        return "MSAA, the pass window, the exact blend or GS state as shader data";
    case RD_NV_WHY_FORM:
        return "the window's commands (a draw inside it, a second window, a level writing a "
               "depth)";
    case RD_NV_WHY_CHAIN:
        return "the count target or a level read or written by another command";
    case RD_NV_WHY_UNKNOWN:
        return "a command the scan does not know after the resolve";
    default:
        return "?";
    }
}

/* ------------------------------------------------- the replay's state */

#define NV_OWED RD_NV_CHAIN

_Static_assert(NV_OWED >= RD_NV_CHAIN, "a zero owed to every target of the chain");

static struct {
    int env; /* ICO_RD_NO_VOLUME: -1 unset, 0, 1 */
    bool envRead;
    RdNvScan scan;
    uint32_t replay;   /* the replay scanned (g_rd.replayCounter) */
    int result, gate;  /* the scan's RD_NV_RES_*, the gate's RD_NV_WHY_* (COUNT: none) */
    bool owns;         /* the window is taken this replay */
    bool active;       /* its reset was met by the walk */
    bool resolved;     /* its resolve was taken */
    uint32_t frame;    /* the frame scanned (the lines) */
    uint32_t countFor; /* the count target (the passes' names) */

    /* the zeros owed: a target's colour (target != 0) or the reduced count
     * (red), their size */
    struct {
        uint32_t target;
        RhiTexture *red;
        RhiState *redState;
        uint32_t w, h;
    } owed[NV_OWED];

    unsigned nOwed;
    bool flushing;
    /* the break mode (rd_no_volume.h): the window taken with SCENE's pass
     * ended at the resolve and the reset and the merged stencil store as
     * they are without it (gateBreak its gate, brk taken so this replay) */
    int envBreak;
    bool envBreakRead;
    int gateBreak;
    bool brk;
    /* the lines */
    bool saidGate, saidKept;
    int saidGateWhy;
    uint32_t refusals;
} s_nv = {.env = -1, .envBreak = -1, .saidGateWhy = -2};

/* The targets that hold (0, 0, 0, 0): the zero clears the last replay
 * recorded (rd__no_volume_flush) and the ones it found holding zero, each
 * a target's colour (target) or the reduced count (red), its texture and
 * size.  Read by the next replay alone (zeroPrev: the replay counter one
 * more), and by it only for a target no command before its window's resolve
 * names (the scan marks those: dirty), whose texture is the same and still
 * in SHADER_READ as the clear's pass left it: the break mode then owes it no
 * clear (it holds the zero the clear would write) */
typedef struct NvZero {
    uint32_t target;
    RhiTexture *red;
    uint32_t tex, w, h;
    bool dirty;
} NvZero;

static struct {
    uint32_t replay;
    unsigned n;
    NvZero z[RD_NV_CHAIN];
} s_zero, s_zeroPrev;

static void zeroAdd(uint32_t target, RhiTexture *red, uint32_t tex, uint32_t w, uint32_t h)
{
    for (unsigned i = 0; i < s_zero.n; i++) {
        if (s_zero.z[i].target == target && s_zero.z[i].red == red) {
            s_zero.z[i].tex = tex;
            s_zero.z[i].w = w;
            s_zero.z[i].h = h;
            return;
        }
    }
    if (s_zero.n < RD_NV_CHAIN) {
        s_zero.z[s_zero.n++] = (NvZero){target, red, tex, w, h, false};
    }
}

/* the scan before the window's resolve: a command naming a target that held
 * zero (its colour, its depth, a texture, a clear, a copy, a post record's) */
static void zeroTouch(const RdNvCmd *c)
{
    for (unsigned i = 0; i < s_zeroPrev.n; i++) {
        const uint32_t t = s_zeroPrev.z[i].target;
        if (!t) {
            continue; /* the reduced count: only a resolve writes it, and the window's is the
                         first of the replay (the scan refuses one before it) */
        }
        bool named = c->color == t || c->depth == t;
        for (int k = 0; k < RD_NV_NAMES && !named; k++) {
            named = c->names[k] == t;
        }
        s_zeroPrev.z[i].dirty = s_zeroPrev.z[i].dirty || named;
    }
}

/* ICO_RD_NO_VOLUME_POISON=1 (the tests only): the owed clears write magenta,
 * and none is left out for a target that held zero, so any reader of the
 * count or a level the walk does not draw shows in the picture */
static bool poisonOn(void)
{
    static int s_env = -1;
    if (s_env < 0) {
        const char *e = getenv("ICO_RD_NO_VOLUME_POISON");
        s_env = e && strcmp(e, "1") == 0;
    }
    return s_env == 1;
}

/* the break mode only: the target (or the reduced count) holds zero from the
 * replay before, nothing has named it since, and its texture is the one the
 * zero was written into, in the state the clear's pass left it */
static bool heldZero(uint32_t target, RhiTexture *red, const RhiState *redState, uint32_t w,
                     uint32_t h)
{
    if (!s_nv.brk || poisonOn()) {
        return false;
    }
    for (unsigned i = 0; i < s_zeroPrev.n; i++) {
        const NvZero *z = &s_zeroPrev.z[i];
        if (z->dirty || z->target != target || z->red != red || z->w != w || z->h != h) {
            continue;
        }
        if (red) {
            return red->id && red->id == z->tex && redState && *redState == RHI_STATE_SHADER_READ;
        }
        const RdTargetRec *t = rd__target_rec(target);
        return t && t->color.id && t->color.id == z->tex &&
               t->colorState == RHI_STATE_SHADER_READ && t->tw == w && t->th == h;
    }
    return false;
}

static int envOf(void)
{
    if (!s_nv.envRead) {
        const char *e = getenv("ICO_RD_NO_VOLUME");
        s_nv.env = e && strcmp(e, "0") == 0 ? 0 : e && strcmp(e, "1") == 0 ? 1 : -1;
        s_nv.envRead = true;
    }
    return s_nv.env;
}

/* off by default in this release (rc14 and rc15 on the PowerVR phone: the right part of
   the picture flickered black at the title and on the pause menu, the pictures where a
   window has no volume; cause not found; lavapipe shows the same bytes). The classes it
   was on for: PowerVR and MoltenVK under Vulkan. ICO_RD_NO_VOLUME=1 turns it on (tests) */
#define RD_NO_VOLUME_DEFAULT 0

/* The break mode (v0.11.6): the same window taken with SCENE's pass ended at
 * the resolve as the count's draw ends it, the reset and the merged stencil
 * store as without it; off by default until a phone rc shows the PowerVR
 * phone's title and pause menu clean with it (a relative of the open mode,
 * which flickered there): the user decides.  Its classes are the open
 * mode's.  ICO_RD_NO_VOLUME_BREAK=1 turns it on (tests) */
#define RD_NO_VOLUME_BREAK_DEFAULT 0

/* the classes the window's passes end and begin at a cost: the tile-based
 * classes under Vulkan whose composites take DATE through the stencil bit */
static bool classBase(void)
{
    const RhiLimits *lim = g_rd.hasDevice ? rhi_limits() : NULL;
    return lim && rhi_backend() == RHI_BACKEND_VULKAN && (lim->moltenvk || rd__profile_powervr());
}

static bool classOn(void)
{
    return RD_NO_VOLUME_DEFAULT && classBase();
}

static bool breakClassOn(void)
{
    return RD_NO_VOLUME_BREAK_DEFAULT && classBase();
}

static int breakEnvOf(void)
{
    if (!s_nv.envBreakRead) {
        const char *e = getenv("ICO_RD_NO_VOLUME_BREAK");
        s_nv.envBreak = e && strcmp(e, "0") == 0 ? 0 : e && strcmp(e, "1") == 0 ? 1 : -1;
        s_nv.envBreakRead = true;
    }
    return s_nv.envBreak;
}

static int gateOf(int env, bool cls)
{
    return rd_nv_gate(env, cls, rd_aa_samples(rd__aa_mode()) > 1u, g_rdWin.first != 0,
                      g_rd.dstMode != RD_BLEND_EXACT_OFF, rd__uber_on(), g_rd.profile.dateStencil);
}

/* the window's gate: the break mode's when it holds (it goes first), else
 * the open mode's (its why when neither holds) */
static int gateNow(void)
{
    s_nv.gateBreak = gateOf(breakEnvOf(), breakClassOn());
    const int open = gateOf(envOf(), classOn());
    return s_nv.gateBreak == RD_NV_WHY_COUNT ? RD_NV_WHY_COUNT : open;
}

static bool here(void)
{
    return s_nv.replay == g_rd.replayCounter;
}

void rd__no_volume_scan_begin(const RdFrame *f, int keep)
{
    (void)keep;
    rd_nv_scan_begin(&s_nv.scan);
    s_nv.replay = g_rd.replayCounter;
    s_nv.frame = f ? f->number : 0;
    s_nv.result = RD_NV_RES_NONE;
    s_nv.owns = s_nv.active = s_nv.resolved = s_nv.brk = false;
    s_nv.nOwed = 0; /* every zero owed was recorded at the last replay's end */
    /* the zeros the replay before left: this replay's to read only if no
     * other replay (an own frame, a self-test's) came between */
    if (s_zero.replay + 1u == g_rd.replayCounter && s_zero.replay != 0) {
        s_zeroPrev = s_zero;
    } else {
        s_zeroPrev.n = 0;
    }
    s_zero.n = 0;
    s_zero.replay = g_rd.replayCounter;
}

/* the target a state's texture is a view of, else 0; enabledOnly: a
 * screen-prim command, which samples it only with TME on (rd_replay.c
 * resolveTexture), every other command's named whether on or not */
static uint32_t texTarget(const RdStateBlock *st, bool enabledOnly)
{
    if (enabledOnly && !st->ds.texEnabled) {
        return 0;
    }
    const RdTexRec *t = rd__tex_rec(st->tex);
    return t && t->kind == RD_TEXKIND_TARGET ? t->target : 0u;
}

/* a composite's DATE goes through the stencil bit where it is on: the
 * conditions rd_replay.c dateStencilFor reads from the targets (the profile,
 * MSAA and the exact blend are the gate's) */
static bool dateStencilTarget(const RdStateBlock *st)
{
    if (st->ds.test.date == RD_DATE_OFF) {
        return true;
    }
    const RdTargetRec *tc = rd__target_rec(st->color), *td = rd__target_rec(st->depth);
    return tc && td && td->withDepth && (tc->dstUsage & RD_DST_USAGE_LOCAL_DATE) != 0 &&
           tc->format == RHI_FMT_RGBA8_UNORM && td->tw == tc->tw && td->th == tc->th;
}

void rd__no_volume_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                            uint32_t ord)
{
    if (state || s_nv.scan.phase == 3) {
        return;
    }
    RdNvCmd e;
    memset(&e, 0, sizeof(e));
    e.ord = ord;
    e.color = st->color;
    e.depth = st->depth;
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return;
    case RDC_SHADOW_RESET:
        e.kind = RD_NV_RESET;
        break;
    case RDC_SHADOW_STRIP:
        e.kind = RD_NV_STRIP;
        break;
    case RDC_SHADOW_RESOLVE:
        e.kind = RD_NV_RESOLVE;
        break;
    case RDC_SCREEN: {
        uint32_t src = 0;
        const int role = rd__shadow_box_role(ord, &src);
        e.names[0] = texTarget(st, true);
        if (role == 2 || role == 3) { /* rd_shadow_box.c ROLE_LEVEL, ROLE_COMPOSITE */
            e.kind = role == 2 ? RD_NV_LEVEL : RD_NV_COMPOSITE;
            e.src = src;
            if (role == 2) {
                const RdTargetRec *td = rd__target_rec(st->depth);
                e.formOk = st->ds.zwrite == RD_ZWRITE_OFF || !td || !td->withDepth || !td->depth.id;
            } else {
                e.formOk = dateStencilTarget(st);
            }
        } else {
            e.kind = RD_NV_DRAW;
        }
        break;
    }
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_WORLD_PRIMS:
        e.kind = RD_NV_DRAW;
        e.names[0] = texTarget(st, false);
        break;
    case RDC_CLEAR:
        e.kind = RD_NV_DRAW;
        e.color = e.depth = 0;
        e.names[0] = c->u[0];
        e.clears = true;
        break;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
    case RDC_UPSCALE_POINT:
        e.kind = RD_NV_DRAW;
        e.names[0] = c->u[0];
        e.names[1] = c->u[1];
        e.names[2] = texTarget(st, false);
        break;
    case RDC_POST_STUB:
        e.kind = RD_NV_DRAW;
        e.names[0] = texTarget(st, false);
        if ((uint64_t)c->u[1] + sizeof(RdPostRec) <= f->payloadSize) {
            RdPostRec p;
            memcpy(&p, f->payload + c->u[1], sizeof(p));
            e.names[1] = p.src;
            e.names[2] = p.dst;
        } else {
            e.kind = RD_NV_UNKNOWN;
        }
        break;
    default:
        e.kind = RD_NV_UNKNOWN;
        break;
    }
    if (s_nv.scan.phase < 2) {
        zeroTouch(&e); /* before the window's resolve (after it the scan refuses any touch) */
    }
    rd_nv_scan_step(&s_nv.scan, &e);
}

static void whyCount(int why)
{
    if (why >= 0 && why < RD_NV_WHY_COUNT && g_rdPerf.noVolumeWhy[why] < UINT16_MAX) {
        g_rdPerf.noVolumeWhy[why]++;
    }
}

void rd__no_volume_scan_end(void)
{
    s_nv.result = rd_nv_scan_end(&s_nv.scan);
    s_nv.gate = gateNow();
    if (s_nv.result == RD_NV_RES_NONE) {
        return;
    }
    const bool brk = s_nv.gate == RD_NV_WHY_COUNT && s_nv.gateBreak == RD_NV_WHY_COUNT;
    const int said = s_nv.gate + (brk ? 100 : 0);
    if (!s_nv.saidGate || s_nv.saidGateWhy != said) {
        /* once a renderer, and again when the decision changes */
        s_nv.saidGate = true;
        s_nv.saidGateWhy = said;
        if (brk) {
            rd__log("shadows: no volume: on with SCENE's pass ended at the resolve (%s): a shadow "
                    "window without a volume draws no count, levels or composites, the count "
                    "and levels written as zero clears (none where they hold zero already), "
                    "the reset and the stencil's store as before; the same bytes",
                    breakEnvOf() == 1 ? "ICO_RD_NO_VOLUME_BREAK=1"
                                      : "the PowerVR or MoltenVK class, "
                                        "RD_NO_VOLUME_BREAK_DEFAULT 1");
        } else {
            rd__log("shadows: no volume: %s (%s): a shadow window without a volume keeps "
                    "SCENE's pass open, its count and levels written as zero clears outside "
                    "it; the same bytes",
                    s_nv.gate == RD_NV_WHY_COUNT ? "on" : "off",
                    s_nv.gate == RD_NV_WHY_COUNT
                        ? (envOf() == 1 ? "ICO_RD_NO_VOLUME=1"
                                        : "the PowerVR or MoltenVK class, RD_NO_VOLUME_DEFAULT 1")
                        : rd_nv_why_words(s_nv.gate));
        }
    }
    if (s_nv.result == RD_NV_RES_VOLUME) {
        g_rdPerf.noVolumeWith++;
        return;
    }
    const int why = s_nv.result == RD_NV_RES_REFUSED ? s_nv.scan.why
                    : s_nv.gate != RD_NV_WHY_COUNT   ? s_nv.gate
                                                     : -1;
    if (why < 0) {
        s_nv.owns = true;
        s_nv.brk = brk;
        return;
    }
    whyCount(why);
    if (why == RD_NV_WHY_OFF || why == RD_NV_WHY_CLASS) {
        return;
    }
    s_nv.refusals++;
    if (s_nv.refusals <= 8u || s_nv.refusals % 100u == 0u) {
        rd__log("shadows: no volume: a window not taken in frame %u (reset at command %u): %s "
                "(%u so far, the first 8 then every 100th)",
                s_nv.frame, s_nv.scan.resetOrd, rd_nv_why_words(why), s_nv.refusals);
    }
}

bool rd__no_volume_owns(uint32_t ord)
{
    /* the break mode keeps the reset and its stencil store */
    return s_nv.owns && !s_nv.brk && here() && ord == s_nv.scan.resetOrd;
}

/* ------------------------------------------------------------- the walk */

bool rd__no_volume_reset(Replay *r)
{
    (void)r;
    if (!s_nv.owns || !here() || g_rdStoreOrd != s_nv.scan.resetOrd) {
        return false;
    }
    s_nv.active = true;
    return !s_nv.brk; /* the break mode: the reset recorded as before */
}

bool rd__no_volume_active(void)
{
    return s_nv.active && here() && !s_nv.resolved;
}

static void owe(uint32_t target, RhiTexture *red, RhiState *redState, uint32_t w, uint32_t h)
{
    if (heldZero(target, red, redState, w, h)) {
        /* the break mode: it holds the zero a clear would write */
        zeroAdd(target, red, red ? red->id : rd__target_rec(target)->color.id, w, h);
        g_rdPerf.noVolumeHeld++;
        return;
    }
    for (unsigned i = 0; i < s_nv.nOwed; i++) {
        if (s_nv.owed[i].target == target && s_nv.owed[i].red == red) {
            return; /* owed already: one clear */
        }
    }
    if (s_nv.nOwed >= NV_OWED) {
        return; /* never: the owed are the scan's chain, at most RD_NV_CHAIN targets */
    }
    s_nv.owed[s_nv.nOwed].target = target;
    s_nv.owed[s_nv.nOwed].red = red;
    s_nv.owed[s_nv.nOwed].redState = redState;
    s_nv.owed[s_nv.nOwed].w = w;
    s_nv.owed[s_nv.nOwed].h = h;
    s_nv.nOwed++;
    g_rdPerf.noVolumeClears++;
}

void rd__no_volume_resolve(Replay *r, RdTargetRec *tc, RhiTexture *red, RhiState *redState)
{
    s_nv.resolved = true;
    s_nv.countFor = r->st.color;
    if (s_nv.brk) {
        /* the pass ends where the count's draw ends it (rd_shadow_one.c
         * rd__replay_end_pass: the reset's stencil clear and the merged
         * pass's stencil store recorded as before), and the shadow window
         * closes as doShadowResolve closes it after the count */
        rd__replay_end_pass(r);
        const RdTargetRec *td = rd__target_rec(r->st.depth);
        if (td && td->withDepth && td->depth.id) {
            rd__stencil_close(rd__replay_stencil(), td->depth.id);
        }
        g_rdPerf.noVolumeBreak++;
    }
    r->writeSerial++; /* as the count's draw moved it */
    if (red) {
        owe(0, red, redState, tc->w, tc->h); /* the reduced count at the GS size */
    } else {
        owe(r->st.color, NULL, NULL, tc->tw, tc->th);
    }
    if (s_nv.brk) {
        if (!s_nv.saidKept) {
            s_nv.saidKept = true;
            rd__log("shadows: no volume: frame %u, the window at commands %u..%u has no volume: "
                    "SCENE's pass ended at the resolve, its count%s and levels owed as zero "
                    "clears or holding zero already (the same bytes; reported once)",
                    s_nv.frame, s_nv.scan.resetOrd, s_nv.scan.resolveOrd,
                    red ? " (the reduced count)" : "");
        }
        return;
    }
    g_rdPerf.noVolumeKept++;
    const RdTargetRec *pc = r->passOpen ? rd__target_rec(r->passColor) : NULL;
    if (pc) {
        g_rdPerf.noVolumeOpen++;
        g_rdPerf.noVolumePassW = (uint16_t)pc->tw;
        g_rdPerf.noVolumePassH = (uint16_t)pc->th;
        g_rdPerf.noVolumePassDepth = (uint8_t)(r->passDepth != 0);
    }
    if (!s_nv.saidKept) {
        s_nv.saidKept = true;
        rd__log("shadows: no volume: frame %u, the window at commands %u..%u has no volume: its "
                "count%s and levels owed as zero clears, %s (the same bytes; reported once)",
                s_nv.frame, s_nv.scan.resetOrd, s_nv.scan.resolveOrd,
                red ? " (the reduced count)" : "",
                pc ? "SCENE's pass kept open" : "no pass was open at the resolve");
    }
}

bool rd__no_volume_skip(Replay *r)
{
    const int kind =
        s_nv.resolved && here() ? rd_nv_skip_kind(&s_nv.scan, g_rdStoreOrd) : RD_NV_NONE;
    if (kind == RD_NV_NONE) {
        /* a composite whose level the volumes' box leaves empty draws
         * nothing (rd_shadow_box.c): skipped with its DATE snapshot or
         * set-up; the write serial moves, so the next DATE draw takes its own
         * set-up from the alpha as it is, as after the drawn one */
        if (!rd__shadow_box_empty_skip(&r->st)) {
            return false;
        }
        rd__replay_flush_run(r);
        r->writeSerial++;
        rd__date_one_passed(r->writeSerial); /* the group's set-up still holds */
        return true;
    }
    rd__replay_flush_run(r); /* the run's draws keep their place */
    r->writeSerial++;        /* as a drawn level's or composite's may */
    if (kind == RD_NV_LEVEL) {
        const RdTargetRec *t = rd__target_rec(r->st.color);
        if (t) {
            owe(r->st.color, NULL, NULL, t->tw, t->th);
            g_rdPerf.noVolumeLevelTexels += t->tw * t->th;
        }
    }
    g_rdPerf.noVolumeSkipped++;
    return true;
}

void rd__no_volume_flush(void)
{
    if (s_nv.nOwed == 0 || s_nv.flushing) {
        return;
    }
    s_nv.flushing = true;
    const unsigned n = s_nv.nOwed;
    s_nv.nOwed = 0;
    RhiCommandList cl = rd__replay_cl();
    for (unsigned i = 0; i < n; i++) {
        RhiTexture tex = {0};
        RhiState *state = NULL;
        uint32_t id = s_nv.owed[i].target, w = s_nv.owed[i].w, h = s_nv.owed[i].h;
        if (s_nv.owed[i].red) {
            tex = *s_nv.owed[i].red;
            state = s_nv.owed[i].redState;
        } else {
            RdTargetRec *t = rd__target_rec(id);
            if (t) {
                tex = t->color;
                state = &t->colorState;
                w = t->tw;
                h = t->th;
            }
        }
        if (!tex.id || !state) {
            continue;
        }
        rd__transition(cl, tex, state, RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = tex;
        p.color[0].load = RHI_LOAD_CLEAR; /* clear[] zeroed: (0, 0, 0, 0) */
        p.color[0].store = RHI_STORE_STORE;
        if (poisonOn()) {
            static const float kMagenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
            memcpy(p.color[0].clear, kMagenta, sizeof(kMagenta)); /* a reader would show */
        } else {
            zeroAdd(s_nv.owed[i].red ? 0u : id, s_nv.owed[i].red, tex.id, s_nv.owed[i].w,
                    s_nv.owed[i].h);
        }
        p.colorCount = 1;
        p.width = w;
        p.height = h;
        /* "shadow count WxH" or "SHADOW0 WxH" in the pass list, 0 draws */
        const uint32_t name = s_nv.owed[i].red ? RD_PERF_ID_SHADOW_COUNT : id;
        rd__perf_pass(cl, RD_PERF_PASS_RENDER, name, w, h, -1);
        rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS,
                                                    s_nv.owed[i].red ? s_nv.countFor : id, w, h, 0,
                                                    0, 0, 0, -1));
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_end_render_pass(cl);
        rd__transition(cl, tex, state, RHI_STATE_SHADER_READ); /* as its last reader left it */
    }
    s_nv.flushing = false;
}

/* ------------------------------------------------------------ the lines */

static struct {
    uint64_t kept, open, with, why[RD_NO_VOLUME_WHYS], bytes, brk, clears, held;
    uint32_t replays;
} s_sum;

/* the bytes a kept pass did not store and load again: its colour (4 a
 * texel) and its depth and stencil (5), each way; an upper bound (the
 * store plan or the stencil cut may have dropped some of them) */
static uint64_t keptBytes(const RdPerfRecord *rec)
{
    const uint64_t texels = (uint64_t)rec->noVolumePassW * rec->noVolumePassH;
    return rec->noVolumeOpen * texels * (4u + (rec->noVolumePassDepth ? 5u : 0u)) * 2u;
}

void rd__no_volume_pop(const RdPerfRecord *rec)
{
    s_sum.kept += rec->noVolumeKept;
    s_sum.open += rec->noVolumeOpen;
    s_sum.with += rec->noVolumeWith;
    for (int k = 0; k < RD_NO_VOLUME_WHYS; k++) {
        s_sum.why[k] += rec->noVolumeWhy[k];
    }
    s_sum.bytes += keptBytes(rec);
    s_sum.brk += rec->noVolumeBreak;
    s_sum.clears += rec->noVolumeBreak ? rec->noVolumeClears : 0u;
    s_sum.held += rec->noVolumeHeld;
    s_sum.replays++;
}

/* "class 1.0, chain 0.5" (n the replays; perReplay) or "class 1" */
static void whyText(char *out, size_t size, const uint64_t *why, double n, bool perReplay)
{
    static const char *const kShort[RD_NV_WHY_COUNT] = {"off",  "class", "DATE",   "setting",
                                                        "form", "chain", "unknown"};
    size_t at = 0;
    out[0] = '\0';
    for (int k = 0; k < RD_NV_WHY_COUNT && at < size; k++) {
        if (!why[k]) {
            continue;
        }
        const int w = perReplay ? snprintf(out + at, size - at, "%s%s %.1f", at ? ", " : "",
                                           kShort[k], (double)why[k] / n)
                                : snprintf(out + at, size - at, "%s%s %llu", at ? ", " : "",
                                           kShort[k], (unsigned long long)why[k]);
        at += w > 0 && (size_t)w < size - at ? (size_t)w : 0;
    }
}

void rd__no_volume_text(char *out, size_t size, size_t *at)
{
    uint64_t k = 0;
    for (int i = 0; i < RD_NO_VOLUME_WHYS; i++) {
        k += s_sum.why[i];
    }
    if (s_sum.replays == 0 || (s_sum.kept == 0 && s_sum.with == 0 && k == 0 && s_sum.brk == 0) ||
        *at >= size) {
        memset(&s_sum, 0, sizeof(s_sum));
        return;
    }
    const double n = (double)s_sum.replays;
    char why[160];
    whyText(why, sizeof why, s_sum.why, n, true);
    const int w = snprintf(out + *at, size - *at,
                           "; no volume: %.1f windows a replay without a volume kept SCENE's "
                           "pass open (%.1f with it open, up to %.1f MB not stored and loaded "
                           "again), %.1f with volumes, %.1f not taken%s%s%s",
                           (double)s_sum.kept / n, (double)s_sum.open / n,
                           (double)s_sum.bytes / n / 1048576.0, (double)s_sum.with / n,
                           (double)k / n, why[0] ? " (" : "", why, why[0] ? ")" : "");
    *at += w > 0 && (size_t)w < size - *at ? (size_t)w : 0;
    if (s_sum.brk && *at < size) {
        /* the break mode's windows (RD_NO_VOLUME_BREAK_DEFAULT, ICO_RD_NO_VOLUME_BREAK) */
        const int b =
            snprintf(out + *at, size - *at,
                     ", %.1f with SCENE's pass ended at the resolve (%.1f zero clears, "
                     "%.1f targets holding zero already)",
                     (double)s_sum.brk / n, (double)s_sum.clears / n, (double)s_sum.held / n);
        *at += b > 0 && (size_t)b < size - *at ? (size_t)b : 0;
    }
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__no_volume_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    uint64_t why[RD_NO_VOLUME_WHYS], k = 0;
    for (int i = 0; i < RD_NO_VOLUME_WHYS; i++) {
        why[i] = rec->noVolumeWhy[i];
        k += why[i];
    }
    char words[160];
    whyText(words, sizeof words, why, 1.0, false);
    int w = snprintf(out, size,
                     "no volume %s: %u kept (%u with the pass open over %ux%u, depth %s; %u zero "
                     "clears, %u level texels; %u draws not drawn), %u with volumes, %llu not "
                     "taken%s%s%s",
                     s_nv.gate == RD_NV_WHY_COUNT ? "on" : "off", rec->noVolumeKept,
                     rec->noVolumeOpen, rec->noVolumePassW, rec->noVolumePassH,
                     rec->noVolumePassDepth ? "yes" : "no", rec->noVolumeClears,
                     rec->noVolumeLevelTexels, rec->noVolumeSkipped, rec->noVolumeWith,
                     (unsigned long long)k, words[0] ? " (" : "", words, words[0] ? ")" : "");
    size_t at = w > 0 && (size_t)w < size ? (size_t)w : (size ? size - 1 : 0);
    /* the shadow window's other cuts, on the same line: the break mode, the
     * empty composites (rd_shadow_box.c), one DATE set-up (rd_date_one.c) */
    if (at < size) {
        const int m = rd__shadow_box_empty_mode();
        w = snprintf(out + at, size - at,
                     "; break %s: %u taken (%u zero clears, %u targets holding zero); empty "
                     "composites %s: %u skipped (%u with DATE); ",
                     s_nv.gateBreak == RD_NV_WHY_COUNT ? "on" : "off", rec->noVolumeBreak,
                     rec->noVolumeBreak ? rec->noVolumeClears : 0u, rec->noVolumeHeld,
                     m ? "on" : "off", rec->emptySkipped, rec->emptySkippedDate);
        at += w > 0 && (size_t)w < size - at ? (size_t)w : 0;
    }
    if (at < size) {
        rd__date_one_stats(rec, out + at, size - at);
    }
}
