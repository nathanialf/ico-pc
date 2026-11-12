/* rd_late_pass.c: fewer late full-size passes on SCENE, the same bytes
 * (docs/port/RENDER_POST.md, "The late passes on SCENE"; rd_late_pass.h).
 *
 * The crack fill's draw held (RD_LATE_FILL).  Above 1x the crack fill
 * (rd_crack_fill.c) is drawn from list 7 on where rd__crack_fill_place
 * puts it: after the glow's cleanUpFB (two sprites of SCENE's alpha alone)
 * that is the first sprite into another target (the glow's WORK2_PAD
 * band), on the assumption that the glow's mask, which samples SCENE,
 * follows; there the fill takes its colour snapshot and draws in a pass of
 * its own (the PowerVR phone's cage rooms at Resolution Auto: 0.9 passes and
 * 1.8 ms a picture, "after the glow's alpha" in its 10 s line).  Under the
 * glow's DARK plan (rd_glow_light.c: the chain draws nothing, the mask and
 * the paste are not drawn) nothing after that place draws on SCENE or reads
 * its RGB until the aura's paste (pasteFeedBackAreaToFB, FEED128 over SCENE
 * by the local read), whose pass the fill takes as its first draw in a room
 * without the glow (rd__crack_fill_in_pass).  So the fill takes its snapshot
 * where it is taken without the hold (rd__crack_fill_place's TAKE: the bytes
 * it reads, the fill's detector, reach and arithmetic unchanged) and its
 * draw waits, each command classified by rd__late_pass_class, to the
 * aura's paste pass.  Exact, by the class: a command it waits past writes
 * no texel of SCENE's colour and no depth of SCENE, and reads SCENE's
 * colour only in the alpha-only form (the mask, which the light plan does
 * not draw anyway), whose bytes the fill (RGB alone after the glow's
 * alpha) does not change; anything else has the fill drawn before it in its
 * own pass, as it is drawn without the hold.  One write of SCENE does fall
 * between the snapshot and the draw: SCENE's border repeat (rd_blur_border.c
 * sceneRepeat, before the first sprite that samples SCENE: the mask, whose
 * draw the light plan skips but whose repeat it keeps), which copies the
 * source columns 2 and W - 3 over the outer two a side.  Without the hold
 * the repeat copied the filled columns; with it the fill is drawn over the
 * columns the repeat left, and over each block it copied with the test made
 * at the block's source texel (crack_fill_ps's g_param.w offset): a crack
 * there writes its source's fill, any other texel keeps the copy, the
 * source's colour (rd_crack_fill.c fillDrawHeld).  The snapshot is the
 * one taken before the repeat, as without the hold, so the fill reads the
 * outer columns as the fog and lists 4-6 left them.  Drawn in a pass of
 * its own after all (counted by reason, RD_LATE_WHY_*): before any other
 * command, before SCENE's snapshot is taken again or swapped (rd_replay.c
 * takeSnap, blurSwapTarget), before a repeat of more blocks than the hold
 * keeps, at an MSAA epoch, before an aura paste whose pass cannot take it,
 * at the walk's end.  Not held at all under the upscaler's split (the
 * upscale point and the display layer, which only it takes), with an MSAA epoch wanted, under the exact blend, under the
 * tool's pass window.  The store plan's scan (rd_store_plan.c) asks
 * rd__late_pass_scan where the fill reads SCENE's depth: it notes the
 * snapshot's place and where the held draw lands (waiting past every sprite
 * on SCENE the plan may skip, the plan not decided yet), and
 * rd__late_pass_scan_end, after the light plan's scan, gives the place the
 * walk takes (the landing under DARK, the snapshot's place otherwise), so
 * every pass before that read keeps the depth's store and no other pass
 * keeps more.
 *
 * Under the LAMPS and WHOLE plans the glow's paste (pasteFullScreenFlare,
 * WORK3 added over SCENE, the fx plan's premultiplied add: rd_fx_plan.c
 * fxDraw) is drawn, and is where the held draw lands (RD_LATE_LAND_GLOW,
 * rd_crack_fill.c glowWhyNot), the first draw of the paste's pass: the fill
 * before the paste, as without the hold.  Between, the lamps' meshes draw
 * into WORK2 under a Z test of SCENE's depth (the draws the class waits
 * past: no SCENE sampled, no depth of SCENE written, no DATE, no PRIM.AA1);
 * the depth they bind is sampled again by the fill (rd_crack_fill.c takes it
 * back to its sampled state outside any pass), and the store plan keeps the
 * lamps' pass's depth store for that read (the scan's place moves to the
 * paste).  The sun's fans write SCENE's depth: a fill held past them is
 * drawn before them (WHOLE with the fans on the picture gains nothing).
 *
 * The glow's paste deferred (RD_LATE_DEFER).  Under LAMPS and WHOLE the
 * glow's paste has a pass of its own on SCENE (its colour loaded and
 * stored), and the aura's paste after it another: between them only the
 * aura's sprites into its own buffers.  rd_fx_plan.c fxDraw copies the
 * paste's arguments and inputs instead of drawing it (fxDeferTake: its
 * source made readable first) and draws it in the aura's local-read paste
 * pass, after a held fill and before the aura's paste (rd__fx_defer_draw),
 * under its own state and command order: the frame's order, so the same
 * bytes.  While it is deferred, the commands it waits past are the fill's
 * class less draws (their passes may bind SCENE's depth, which a held fill
 * reads later), sprites with a Z test of SCENE's depth, any read of SCENE
 * (the paste wrote its RGB and its alpha) and anything on its source;
 * anything else, SCENE's snapshot or swap, an aura's pass that is not the
 * local read's or has an area or a pass on SCENE open before it, and the
 * walk's end draw it there (a held fill first).  The glow's time is then
 * counted in the aura's class.
 *
 * List 11's draws in the open pass (RD_LATE_JOIN).  A list 11 screen prim
 * binds SCENE's depth wherever SCENE has one (rd_replay.c prepareDrawEx),
 * so after the aura's paste, whose pass has no depth, the pause menu's first
 * draw began a pass of its own with the depth (a paused picture has no
 * feedback read of SCENE between the paste and list 11).  A draw that uses
 * no depth (rd__late_pass_join's checks) takes none when the pass open on
 * its target has none, and joins it: the mirror of rd_fx_plan.c
 * fxAttachDepth.  The same colour bytes (the key without the depth has the
 * state's Z ALWAYS and no write, rd_pipeline.c rd__plan_screen_draw_ex), no
 * depth read or written either way, the key the start-up set's no-depth
 * twin.  A draw after it that needs the depth begins its pass with it, as
 * before.
 *
 * The switch: on by default on a tile-based GPU (RdProfile.tiler) under
 * Vulkan (MoltenVK too), where a pass loads and stores the picture; off on
 * the other classes (a pass costs little there) and on OpenGL.
 * ICO_RD_LATE_PASS=0|1|fill (tests only: no config key) forces it off or on
 * anywhere, rd__late_pass_force the tests' own.
 *
 * The lines:
 *   rd: late pass: on (a tile-based GPU's default): ...   (start, a change)
 *   rd: late pass: the crack fill's draw held from list L to the aura's
 *       paste pass ... (reported once)
 *   window: late pass: N crack fills held into the aura's paste pass, M
 *       drawn early (K why, ...)                         (10 s, when any)
 *   rd_replay_tool --stats: "stats: late pass on|off (...): N fills held
 *       into the aura's paste pass, M drawn early (why)" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_lamps.h" /* rd__lamps_fill_hold_ok */
#include "rd_replay.h"
#include "rd_upscale.h"

static int s_force = -1; /* rd__late_pass_force: -1 none, else the bits */

static struct {
    bool read;
    int bits; /* ICO_RD_LATE_PASS: -1 not given */
    char said[512];
} s_sw = {false, -1, ""};

/* the 10 s line's counts since its last call (the window's thread reads
 * and clears them; the replay adds) */
static struct {
    uint64_t held, glowHeld, flushed, why[RD_LATE_WHY_COUNT], joined, joinPasses;
    uint64_t deferred, deferEarly, noLand;
} s_sum;

/* the scan's verdict for the walk (rd__late_pass_scan_end): whether a paste
 * in this replay takes the held fill; the replay it is for */
static struct {
    uint32_t replay;
    bool known, lands;
} s_land;

/* RD_LATE_DEFER: the aura's paste the deferred glow paste lands at (its
 * command order and replay) */
static struct {
    bool land;
    uint32_t ord, replay;
} s_def;

/* RD_LATE_JOIN: the command walked now (rd__late_pass_cmd), the pass the
 * last joined draw was drawn in (its serial and replay) */
static struct {
    uint8_t type;
    uint32_t serial, replay;
    bool any;
} s_join;

static const char *const kWhy[RD_LATE_WHY_COUNT] = {
    [RD_LATE_WHY_NONE] = "-",
    [RD_LATE_WHY_DRAW] = "a draw",
    [RD_LATE_WHY_SCENE] = "a command on SCENE",
    [RD_LATE_WHY_DEPTH] = "SCENE's depth written or DATE",
    [RD_LATE_WHY_POST] = "the reduction or another post command",
    [RD_LATE_WHY_UPSCALE] = "the upscale point",
    [RD_LATE_WHY_PASTE] = "the paste's pass could not take it",
    [RD_LATE_WHY_SNAP] = "SCENE's snapshot taken again",
    [RD_LATE_WHY_BORDER] = "SCENE's border repeat",
    [RD_LATE_WHY_MSAA] = "the MSAA epoch",
    [RD_LATE_WHY_END] = "the walk's end",
    [RD_LATE_WHY_WORK3] = "WORK3 or SCENE's depth used",
};

const char *rd__late_pass_why_text(int why)
{
    return why >= 0 && why < RD_LATE_WHY_COUNT && kWhy[why] ? kWhy[why] : "unknown";
}

const char *rd__late_pass_bits_text(unsigned bits)
{
    bits &= RD_LATE_ALL;
    return bits == RD_LATE_ALL     ? "on"
           : bits == 0             ? "off"
           : bits == RD_LATE_FILL  ? "fill"
           : bits == RD_LATE_DEFER ? "defer"
           : bits == RD_LATE_JOIN  ? "join"
                                   : "some";
}

void rd__late_pass_force(int bits)
{
    s_force = bits < 0 ? -1 : (int)((unsigned)bits & (RD_LATE_ALL | RD_LATE_ANY_PLAN));
}

/* the state line, when it changes (a device only) */
static void note(unsigned bits, const char *why)
{
    if (!g_rd.hasDevice) {
        return;
    }
    char line[sizeof(s_sw.said)];
    if (bits) {
        /* each part's words, joined by "; " */
        const char *part[3] = {
            bits & RD_LATE_FILL ? "the crack fill's draw held from its snapshot to the aura's "
                                  "paste pass in a frame whose glow draws nothing, to the glow's "
                                  "paste pass in one that draws the paste"
                                : NULL,
            bits & RD_LATE_DEFER ? "the glow's paste in the aura's paste pass" : NULL,
            bits & RD_LATE_JOIN ? "list 11's draws that use no depth in the open pass without it"
                                : NULL};
        int at =
            snprintf(line, sizeof(line), "late pass: %s (%s):", rd__late_pass_bits_text(bits), why);
        bool first = true;
        for (int k = 0; k < 3 && at >= 0 && (size_t)at < sizeof(line); k++) {
            if (part[k]) {
                at += snprintf(line + at, sizeof(line) - (size_t)at, "%s %s", first ? "" : ";",
                               part[k]);
                first = false;
            }
        }
        if (at >= 0 && (size_t)at < sizeof(line)) {
            snprintf(line + at, sizeof(line) - (size_t)at, "%s",
                     bits & RD_LATE_JOIN ? " (the same bytes but the join's: a looks-the-same "
                                           "divergence, DIVERGENCES.md VD-31)"
                                         : " (the same bytes)");
        }
    } else {
        snprintf(line, sizeof(line), "late pass: off (%s)", why);
    }
    if (strcmp(line, s_sw.said) != 0) {
        snprintf(s_sw.said, sizeof(s_sw.said), "%s", line);
        rd__log("%s", line);
    }
}

unsigned rd__late_pass_bits(void)
{
    if (!s_sw.read) {
        s_sw.read = true;
        const char *e = getenv("ICO_RD_LATE_PASS");
        if (e && e[0]) {
            s_sw.bits = strcmp(e, "0") == 0       ? 0
                        : strcmp(e, "1") == 0     ? (int)RD_LATE_DEFAULT
                        : strcmp(e, "all") == 0   ? (int)RD_LATE_ALL
                        : strcmp(e, "fill") == 0  ? (int)RD_LATE_FILL
                        : strcmp(e, "join") == 0  ? (int)RD_LATE_JOIN
                        : strcmp(e, "defer") == 0 ? (int)RD_LATE_DEFER
                                                  : -1;
            if (s_sw.bits < 0) {
                rd__log("late pass: ICO_RD_LATE_PASS=%.16s is not 0, 1, all, fill, defer or join: "
                        "the profile's",
                        e);
            }
        }
    }
    unsigned bits;
    const char *why;
    if (s_force >= 0) {
        bits = (unsigned)s_force & RD_LATE_ALL;
        why = "the tests' switch";
    } else if (s_sw.bits >= 0) {
        bits = (unsigned)s_sw.bits;
        why = "ICO_RD_LATE_PASS";
    } else if (g_rd.hasDevice && g_rd.profile.tiler && rhi_backend() == RHI_BACKEND_VULKAN) {
        bits = RD_LATE_DEFAULT;
        why = "a tile-based GPU's default";
    } else if (g_rd.hasDevice) {
        /* the join, an accepted divergence (DIVERGENCES.md VD-31), wherever
         * it applies (rd__late_pass_join: the fx plan's local read); the
         * fill's hold and the deferred paste save a load and a store an
         * immediate-mode GPU hardly pays for */
        bits = RD_LATE_JOIN;
        why = g_rd.profile.tiler ? "not under Vulkan: list 11's join alone, where it applies"
                                 : "not a tile-based GPU: list 11's join alone, where it applies";
    } else {
        bits = 0;
        why = "no device";
    }
    note(bits, why);
    return bits;
}

/* ------------------------------------------------------------- the class */

static bool postRec(const RdFrame *f, const RdCmd *c, RdPostRec *p)
{
    if (!f->payload || (uint64_t)c->u[1] + sizeof(RdPostRec) > f->payloadSize) {
        return false;
    }
    memcpy(p, f->payload + c->u[1], sizeof(*p));
    return true;
}

static int flush(int *why, int w)
{
    *why = w;
    return RD_LATE_FLUSH;
}

int rd__late_pass_class(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, int skipped,
                        int *why)
{
    *why = RD_LATE_WHY_NONE;
    const uint32_t scene = RD_TARGET_SCENE + 1u;
    const RdTargetRec *sc = rd__target_rec(scene);
    if (rd__cmd_is_state(c->type)) {
        /* the upscaler reads SCENE at its marker (rd_upscale.h) */
        return c->type == RDC_UPSCALE_POINT ? flush(why, RD_LATE_WHY_UPSCALE) : RD_LATE_HOLD;
    }
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT: /* collected by the present */
        return RD_LATE_HOLD;
    case RDC_CLEAR:
    case RDC_COPY: {
        /* another target's (rd_interp.c's reads and writes by target id);
         * a clear's depth is its own target's (rd_replay.c doClear) */
        const RdTargetRec *a = rd__target_rec(c->u[0]);
        const RdTargetRec *b = c->type == RDC_COPY ? rd__target_rec(c->u[1]) : a;
        if (!sc || !a || !b || a == sc || b == sc || rd__interp_writes(c, st, scene) ||
            rd__interp_reads(c, st, scene) ||
            (c->type == RDC_CLEAR && c->b[4] && a->depth.id && a->depth.id == sc->depth.id)) {
            return flush(why, RD_LATE_WHY_SCENE);
        }
        return RD_LATE_HOLD;
    }
    case RDC_POST_STUB:
        break;
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES: {
        /* a draw into another target (the lamps' meshes into WORK2) that
         * samples no SCENE and writes no depth of SCENE: the fill's texels
         * and inputs untouched (a Z test reads the depth the fill reads);
         * no DATE, no PRIM.AA1 (their stencil on the depth) */
        const RdDrawState *d = &st->ds;
        const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
        if (!sc || rd__target_rec(st->color) == sc || (d->texEnabled && !t) ||
            (t && t->kind == RD_TEXKIND_TARGET && rd__target_rec(t->target) == sc)) {
            return flush(why, RD_LATE_WHY_DRAW);
        }
        /* under the lamps' LOWRES above 1x a draw into WORK2 writes WORK2's
         * own depth, not SCENE's (rd_lamps.h RD_LAMPS_FILLHOLD) */
        if (d->test.date != RD_DATE_OFF || st->aa1 ||
            (rd__target_rec(st->depth) == sc && d->zwrite == RD_ZWRITE_ON &&
             !rd__lamps_fill_hold_ok(st->color, st->depth))) {
            return flush(why, RD_LATE_WHY_DEPTH);
        }
        return RD_LATE_HOLD;
    }
    default:
        return flush(why, RD_LATE_WHY_DRAW);
    }
    RdPostRec p;
    const uint8_t kind = c->b[0];
    if (kind == RD_POST_REDUCTION || !rd__is_blur_kind(kind) || !postRec(f, c, &p)) {
        return flush(why, RD_LATE_WHY_POST);
    }
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    if (!sc || (d->texEnabled && !t)) {
        return flush(why, RD_LATE_WHY_SCENE);
    }
    const bool fromScene = t && t->kind == RD_TEXKIND_TARGET && rd__target_rec(t->target) == sc;
    if (d->test.date != RD_DATE_OFF || (st->depth == scene && d->zwrite == RD_ZWRITE_ON)) {
        return flush(why, RD_LATE_WHY_DEPTH);
    }
    if (rd__target_rec(st->color) != sc) {
        /* another target: the fill's texels untouched; SCENE sampled only
         * for its alpha (the glow's mask), which the fill does not write */
        if (fromScene && (t->view == RD_VIEW_DEPTH || !rd__fx_alpha_only(d, &p))) {
            return flush(why, RD_LATE_WHY_SCENE);
        }
        return RD_LATE_HOLD;
    }
    if (fromScene) {
        return flush(why, RD_LATE_WHY_SCENE);
    }
    /* on SCENE: the aura's paste (rd__crack_fill_place's PASTE form) */
    if (kind == RD_POST_AURA && t && t->kind == RD_TEXKIND_TARGET && d->abe &&
        !(d->test.zte && d->test.ztst != RD_ZTST_ALWAYS)) {
        return RD_LATE_LAND_AURA;
    }
    /* a sprite the glow's light plan does not draw (the DARK plan's paste),
     * or in the scan, before the plan is decided, any it might not */
    if (skipped != 0) {
        return RD_LATE_HOLD;
    }
    /* the glow's paste the plan draws (pasteFullScreenFlare: WORK3 added,
     * no Z test): the fx plan's premultiplied add (rd_fx_plan.c), whose
     * pass the fill begins */
    if ((kind == RD_POST_FLARE || kind == RD_POST_BLOOM) && t && t->kind == RD_TEXKIND_TARGET &&
        d->abe && !d->pabe && d->colclamp &&
        (d->blend == RD_BLEND_CS_FIX_ADD_CD || d->blend == RD_BLEND_CS_AS_ADD_CD) &&
        !(d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) &&
        !(d->test.ate && d->test.afail == RD_AFAIL_RGB_ONLY)) {
        return RD_LATE_LAND_GLOW;
    }
    return flush(why, RD_LATE_WHY_SCENE);
}

/* --------------------------------------------------------------- the scan */

void rd__late_pass_scan_begin(RdLateScan *s)
{
    memset(s, 0, sizeof(*s));
    s->hold = (rd__late_pass_bits() & RD_LATE_FILL) != 0 && !rd__upscale_point_on();
}

bool rd__late_pass_scan(RdLateScan *s, const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                        uint32_t ord, int l, uint32_t i)
{
    if (s->done) {
        return false;
    }
    if (s->held) {
        int why;
        int k = s->first ? RD_LATE_HOLD : rd__late_pass_class(f, c, st, 0, &why);
        if (!s->first && k != RD_LATE_HOLD) {
            s->first = true; /* a plan that skips nothing on SCENE: here */
            s->firstLands = k == RD_LATE_LAND_AURA || k == RD_LATE_LAND_GLOW;
            s->firstOrd = ord;
            s->firstL = l;
            s->firstI = i;
        }
        k = rd__late_pass_class(f, c, st, -1, &why);
        if (k != RD_LATE_HOLD) {
            s->done = true; /* the held draw lands here, or is drawn before c */
            s->doneLands = k == RD_LATE_LAND_AURA || k == RD_LATE_LAND_GLOW;
            s->landOrd = ord;
            s->landL = l;
            s->landI = i;
        }
        return false;
    }
    const int at = rd__crack_fill_place(f, c, st, &s->alpha);
    if (at == RD_CRACK_PLACE_TAKE && s->hold) {
        s->held = true; /* the snapshot here; the draw where it lands */
        s->takeOrd = ord;
        s->takeL = l;
        s->takeI = i;
        return false;
    }
    return at != RD_CRACK_PLACE_HOLD;
}

/* the walk holds under the DARK plan (it skips the paste), LAMPS and WHOLE
 * (the glow's paste drawn: the fill lands in its pass), or the tests' any
 * plan (none skips a sprite on SCENE in their frames) */
static bool anyPlan(void)
{
    return s_force >= 0 && ((unsigned)s_force & RD_LATE_ANY_PLAN) != 0;
}

static bool planHolds(void)
{
    const int plan = rd__glow_light_plan();
    return anyPlan() || plan == RD_GLOW_LIGHT_DARK || plan == RD_GLOW_LIGHT_LAMPS ||
           plan == RD_GLOW_LIGHT_WHOLE;
}

/* a paste pass on this device can take a held fill: the aura's when its
 * paste reads its destination by the local read (rd_crack_fill.c
 * pasteWhyNot's device part), the glow's when the fx plan draws it by the
 * premultiplied add (glowWhyNot's); under DARK the aura's alone (the plan
 * skips the glow's paste).  Where none can, the hold would only draw the
 * fill later in a pass of its own (past SCENE's border repeat: a draw a
 * repeated block more), so the fill is drawn where it was due, as without
 * the late pass (the GL backend's form: its paste reads a snapshot).  The
 * tests' RD_LATE_ANY_PLAN ignores the device too (rd_late_pass_test's scan
 * runs record-only, without a device's profile) */
static bool landPossible(void)
{
    if (anyPlan()) {
        return true;
    }
    const bool aura = g_rd.profile.fxPlan && g_rd.profile.fxDstLocal &&
                      g_rd.fs[RD_FS_FX_LOCAL].id && g_rd.layoutLocal.id;
    if (aura) {
        return true;
    }
    const bool glow = g_rd.profile.fxPlan && g_rd.fs[RD_FS_FX_FRAME].id && !rd__uber_on();
    const bool ok = glow && rd__glow_light_plan() != RD_GLOW_LIGHT_DARK;
    static bool s_said;
    if (!ok && !s_said) {
        s_said = true;
        rd__log("late pass: the crack fill not held on this device: no paste pass here can take "
                "it (the aura's paste reads its destination through a snapshot%s): drawn where "
                "it is due (reported once)",
                glow ? ", and the glow's paste is skipped in this room"
                     : ", and the glow's is "
                       "not the premultiplied add");
    }
    return ok;
}

bool rd__late_pass_scan_end(const RdLateScan *s, uint32_t end, int endL, uint32_t *ord, int *l,
                            uint32_t *i)
{
    if (!s->held) {
        return false;
    }
    const bool dark = rd__glow_light_plan() == RD_GLOW_LIGHT_DARK && !anyPlan();
    /* where the place the walk would hold the fill to is no paste (a draw
     * the fill must go before, the walk's end: the opening's frames, which
     * have no aura), the hold gains no pass and keeps the snapshot's copy
     * (FILLSWAP refuses a held fill): the walk does not hold it.  The tests'
     * RD_LATE_ANY_PLAN holds as before */
    const bool lands = anyPlan() || (dark ? s->done && s->doneLands : s->first && s->firstLands);
    s_land.replay = g_rd.replayCounter;
    s_land.known = true;
    s_land.lands = lands;
    if (!planHolds() || !landPossible() || !lands) {
        *ord = s->takeOrd; /* drawn where it is taken, as without the hold */
        *l = s->takeL;
        *i = s->takeI;
    } else if (dark && s->done) {
        *ord = s->landOrd; /* past the plan's skipped paste */
        *l = s->landL;
        *i = s->landI;
    } else if (!dark && s->first) {
        *ord = s->firstOrd; /* the glow's paste (LAMPS, WHOLE) */
        *l = s->firstL;
        *i = s->firstI;
    } else {
        *ord = end; /* held to the walk's end (rd__crack_fill_late) */
        *l = endL;
        *i = 0;
    }
    return true;
}

/* --------------------------------------------------------------- the walk */

bool rd__late_pass_hold_wanted(void)
{
    return (rd__late_pass_bits() & RD_LATE_FILL) != 0 && planHolds() && !rd__upscale_point_on() &&
           rd__replay_msaa_want() <= 1u && g_rd.dstMode == RD_BLEND_EXACT_OFF && !g_rdWin.first &&
           landPossible() && !rd__late_pass_no_land();
}

bool rd__late_pass_no_land(void)
{
    return (rd__late_pass_bits() & RD_LATE_FILL) != 0 && s_land.known &&
           s_land.replay == g_rd.replayCounter && !s_land.lands;
}

void rd__late_pass_not_held(void)
{
    g_rdPerf.latePassNoLand++;
    __atomic_add_fetch(&s_sum.noLand, 1, __ATOMIC_RELAXED);
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("late pass: the crack fill not held in a frame where no paste takes it (no aura, "
                "a draw or the walk's end first): drawn where it is due, its snapshot by the swap "
                "where allowed (reported once)");
    }
}

void rd__late_pass_held_drawn(const RdTargetRec *scene, bool glow)
{
    g_rdPerf.latePassHeld++;
    g_rdPerf.latePassGlowHeld += glow;
    g_rdPerf.latePassTexels += scene ? scene->tw * scene->th : 0u;
    __atomic_add_fetch(glow ? &s_sum.glowHeld : &s_sum.held, 1, __ATOMIC_RELAXED);
    static bool s_said[2];
    if (!s_said[glow]) {
        s_said[glow] = true;
        rd__log("late pass: the crack fill's draw held from its snapshot to the %s paste pass "
                "(%s), not in a pass of its own (reported once)",
                glow ? "glow's" : "aura's",
                glow ? "the lamps' or the sun's draws into WORK2 between"
                     : "the glow drew nothing between");
    }
}

void rd__late_pass_flushed(int why)
{
    if (why <= RD_LATE_WHY_NONE || why >= RD_LATE_WHY_COUNT) {
        why = RD_LATE_WHY_END;
    }
    g_rdPerf.latePassFlushed++;
    g_rdPerf.latePassWhy = (uint32_t)why;
    __atomic_add_fetch(&s_sum.flushed, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_sum.why[why], 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------- the join */

void rd__late_pass_cmd(const RdCmd *c)
{
    s_join.type = c->type;
}

void rd__late_pass_join(Replay *r, DrawSetup *ds)
{
    if (!ds->tdId || s_join.type != RDC_SCREEN || rd__replay_list() != 11) {
        return;
    }
    /* the open pass on the draw's target, without a depth (the aura's
     * paste's, a post sprite's) */
    if (!r->passOpen || r->passColor != r->st.color || r->passDepth != 0 || r->passStorage ||
        g_rd.passSamples > 1u || rd__replay_msaa_want() > 1u ||
        g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__uber_on() ||
        !(rd__late_pass_bits() & RD_LATE_JOIN)) {
        return;
    }
    /* only where the fx plan reads destinations by the local read (the
     * profile its byte-for-byte A/B ran on: rd_late_pass_ab's phone and 4:3
     * settings); on OpenGL, whose destination reads go another way, the
     * joined passes' pictures differed (rd_late_pass_gl, 2026-11-11: issue
     * 102's and 164's frames), the cause not found */
    if (!g_rd.profile.fxDstLocal || !g_rd.profile.fxPlan) {
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("late pass: list 11's draws not joined to the open pass on this device (the "
                    "fx plan does not read destinations by the local read here) (reported once)");
        }
        return;
    }
    /* a draw that uses no depth: no Z test (or ALWAYS), no Z write, no
     * DATE (its stencil bit or snapshot), no PRIM.AA1 (its stencil
     * pairs), no depth view sampled, the same Z scale without the depth
     * (every target's zFormat is RD_ZFMT_32), no shadow window on it */
    const RdDrawState *d = &r->st.ds;
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(r->st.tex) : NULL;
    const RdTargetRec *td = rd__target_rec(ds->tdId);
    if ((d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || d->zwrite == RD_ZWRITE_ON ||
        d->test.date != RD_DATE_OFF || ds->dateStencil || r->dateStNow || r->dateDstNow ||
        r->st.aa1 || (t && t->kind == RD_TEXKIND_TARGET && t->view == RD_VIEW_DEPTH) || !td ||
        rd__stencil_live(rd__replay_stencil(), td->depth.id) ||
        rd__target_z_scale(ds->tdId) != rd__target_z_scale(0)) {
        return;
    }
    /* the key without the depth: rd_pipeline.c rd__plan_screen_draw_ex gives
     * it Z ALWAYS and no write, the state's own with the depth; its twin is
     * in the start-up set beside the key with the depth
     * (rd__enumerate_reachable_screen draws each state with and without
     * it), so no pipeline is made during play */
    ds->tdId = 0;
    ds->depthFmt = RHI_FMT_UNKNOWN;
    g_rdPerf.latePassJoined++;
    __atomic_add_fetch(&s_sum.joined, 1, __ATOMIC_RELAXED);
    if (!s_join.any || s_join.serial != r->passSerial || s_join.replay != g_rd.replayCounter) {
        /* the first joined draw of this pass: with the depth it began a
         * pass of its own (bindDrawEx: the pass's depth differs) */
        s_join.any = true;
        s_join.serial = r->passSerial;
        s_join.replay = g_rd.replayCounter;
        g_rdPerf.latePassJoinPasses++;
        g_rdPerf.latePassJoinTexels += ds->tc ? ds->tc->tw * ds->tc->th : 0u;
        __atomic_add_fetch(&s_sum.joinPasses, 1, __ATOMIC_RELAXED);
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("late pass: list 11's draws that use no depth drawn in the open pass on %s "
                    "without its depth (frame's list 11 after the aura's paste: the pause menu, "
                    "the title), not in a pass of their own with it (reported once)",
                    rd__target_name(r->st.color));
        }
    }
}

/* ------------------------------------------------------------ the defer */

bool rd__late_pass_defer_wanted(Replay *r, const RdFrame *f, const RdCmd *c)
{
    const int plan = rd__glow_light_plan();
    int why;
    return (rd__late_pass_bits() & RD_LATE_DEFER) != 0 &&
           (anyPlan() || plan == RD_GLOW_LIGHT_LAMPS || plan == RD_GLOW_LIGHT_WHOLE) &&
           rd__target_rec(r->st.color) == rd__target_rec(RD_TARGET_SCENE + 1u) &&
           g_rd.profile.fxPlan && g_rd.profile.fxDstLocal && g_rd.fs[RD_FS_FX_LOCAL].id &&
           g_rd.layoutLocal.id && rd__replay_msaa_want() <= 1u &&
           g_rd.dstMode == RD_BLEND_EXACT_OFF && !rd__uber_on() && !rd__upscale_point_on() &&
           !g_rdWin.first && rd__late_pass_class(f, c, &r->st, 0, &why) == RD_LATE_LAND_GLOW;
}

/* the deferred paste's class at command c: the fill's, and besides a draw
 * (its pass may bind SCENE's depth, which a held fill reads after it), a
 * sprite with a Z test of SCENE's depth, a sprite sampling SCENE (the paste
 * wrote its RGB and its alpha), and a command on WORK3 (the paste's source,
 * left readable) are drawn after it: it is drawn before them */
static int deferClass(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, int *why)
{
    const int k = rd__late_pass_class(f, c, st, 0, why);
    if (k != RD_LATE_HOLD) {
        return k == RD_LATE_LAND_AURA ? k : RD_LATE_FLUSH;
    }
    const RdTargetRec *sc = rd__target_rec(RD_TARGET_SCENE + 1u);
    const RdTargetRec *w3 = rd__fx_defer_src(); /* WORK3: the paste's source */
    if (c->type == RDC_CLEAR || c->type == RDC_COPY) {
        const bool touches = w3 && (rd__target_rec(c->u[0]) == w3 ||
                                    (c->type == RDC_COPY && rd__target_rec(c->u[1]) == w3));
        if (touches) {
            *why = RD_LATE_WHY_WORK3;
            return RD_LATE_FLUSH;
        }
        return RD_LATE_HOLD;
    }
    if (c->type != RDC_POST_STUB) {
        if (rd__cmd_is_state(c->type) || c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT) {
            return RD_LATE_HOLD;
        }
        *why = RD_LATE_WHY_WORK3; /* a draw: its pass may bind SCENE's depth */
        return RD_LATE_FLUSH;
    }
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    if ((t && t->kind == RD_TEXKIND_TARGET && rd__target_rec(t->target) == sc) ||
        (w3 && rd__target_rec(st->color) == w3) ||
        (rd__target_rec(st->depth) == sc && d->test.zte && d->test.ztst != RD_ZTST_ALWAYS)) {
        *why = RD_LATE_WHY_WORK3;
        return RD_LATE_FLUSH;
    }
    return RD_LATE_HOLD;
}

void rd__late_pass_defer_at(Replay *r, const RdFrame *f, const RdCmd *c)
{
    if (!rd__fx_defer_on()) {
        return;
    }
    if (s_def.land && s_def.replay == g_rd.replayCounter && g_rdStoreOrd > s_def.ord) {
        /* the aura's paste it was to land in was drawn without it (a path
         * past rd_fx_plan.c rd__fx_plan_draw: none known); drawn now, said */
        static uint32_t s_seen;
        if (++s_seen <= 8u || s_seen % 100u == 0u) {
            rd__log("late pass: the glow's paste deferred to the aura's paste at command %u was "
                    "not drawn there: drawn after it (the first 8, then every 100th: %u so far)",
                    s_def.ord, s_seen);
        }
        rd__fx_defer_draw(r, false);
        return;
    }
    int why = RD_LATE_WHY_NONE;
    int k = RD_LATE_FLUSH;
    if (rd__replay_msaa_want() > 1u) {
        why = RD_LATE_WHY_MSAA;
    } else {
        k = deferClass(f, c, &r->st, &why);
    }
    if (k == RD_LATE_HOLD) {
        return;
    }
    if (k == RD_LATE_LAND_AURA) {
        /* the aura's paste by the local read (rd_fx_plan.c fxLocalNext's
         * profile), its pass whole and not open on SCENE before it */
        RhiRect a;
        const RdTargetRec *sc = rd__target_rec(RD_TARGET_SCENE + 1u);
        if (sc && !(r->passOpen && r->passColor == RD_TARGET_SCENE + 1u) &&
            rd__pass_wants_readable(sc) &&
            !(rd__pass_area_on() &&
              rd__pass_area_rect(RD_TARGET_SCENE + 1u, sc, g_rdStoreOrd, &a, NULL))) {
            s_def.land = true;
            s_def.ord = g_rdStoreOrd;
            s_def.replay = g_rd.replayCounter;
            return;
        }
        why = RD_LATE_WHY_PASTE;
    }
    if (why <= RD_LATE_WHY_NONE || why >= RD_LATE_WHY_COUNT) {
        why = RD_LATE_WHY_SCENE;
    }
    /* a held fill first (the order of the frame), then the paste */
    rd__crack_fill_flush_held(r, why);
    rd__fx_defer_draw(r, false);
}

bool rd__late_pass_defer_lands(void)
{
    return s_def.land && s_def.ord == g_rdStoreOrd && s_def.replay == g_rd.replayCounter;
}

void rd__late_pass_defer_end(Replay *r)
{
    if (rd__fx_defer_on()) {
        rd__crack_fill_flush_held(r, RD_LATE_WHY_END);
        rd__fx_defer_draw(r, false);
    }
    s_def.land = false;
}

void rd__late_pass_deferred_taken(void)
{
    s_def.land = false;
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("late pass: the glow's paste deferred to the aura's paste pass (the aura's "
                "sprites into its own buffers between), not in a pass of its own (reported "
                "once)");
    }
}

void rd__late_pass_deferred_drawn(bool landed, bool drawn)
{
    s_def.land = false;
    if (landed && drawn) {
        const RdTargetRec *sc = rd__target_rec(RD_TARGET_SCENE + 1u);
        g_rdPerf.latePassDeferred++;
        g_rdPerf.latePassDeferTexels += sc ? sc->tw * sc->th : 0u;
        __atomic_add_fetch(&s_sum.deferred, 1, __ATOMIC_RELAXED);
    } else {
        g_rdPerf.latePassDeferEarly++;
        __atomic_add_fetch(&s_sum.deferEarly, 1, __ATOMIC_RELAXED);
    }
}

/* ------------------------------------------------------------ the line */

void rd_late_pass_line(void)
{
    const uint64_t held = __atomic_exchange_n(&s_sum.held, 0, __ATOMIC_RELAXED);
    const uint64_t glowHeld = __atomic_exchange_n(&s_sum.glowHeld, 0, __ATOMIC_RELAXED);
    const uint64_t flushed = __atomic_exchange_n(&s_sum.flushed, 0, __ATOMIC_RELAXED);
    const uint64_t joined = __atomic_exchange_n(&s_sum.joined, 0, __ATOMIC_RELAXED);
    const uint64_t joinPasses = __atomic_exchange_n(&s_sum.joinPasses, 0, __ATOMIC_RELAXED);
    const uint64_t deferred = __atomic_exchange_n(&s_sum.deferred, 0, __ATOMIC_RELAXED);
    const uint64_t deferEarly = __atomic_exchange_n(&s_sum.deferEarly, 0, __ATOMIC_RELAXED);
    const uint64_t noLand = __atomic_exchange_n(&s_sum.noLand, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_LATE_WHY_COUNT];
    for (int k = 0; k < RD_LATE_WHY_COUNT; k++) {
        w[k] = __atomic_exchange_n(&s_sum.why[k], 0, __ATOMIC_RELAXED);
    }
    if (held + glowHeld + flushed + joined + deferred + deferEarly + noLand == 0) {
        return;
    }
    char why[512] = "";
    size_t at = 0;
    for (int k = 1; k < RD_LATE_WHY_COUNT; k++) {
        if (!w[k]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)w[k], kWhy[k]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    fprintf(stderr,
            "window: late pass: %llu crack fills held into the aura's paste pass, %llu into the "
            "glow's, %llu drawn early in a pass of their own%s%s; %llu glow pastes in the "
            "aura's paste pass, %llu drawn early; %llu list 11 draws joined the open pass "
            "without SCENE's depth (%llu passes not begun); %llu fills not held (no paste in "
            "the frame takes them)\n",
            (unsigned long long)held, (unsigned long long)glowHeld, (unsigned long long)flushed,
            why, at ? ")" : "", (unsigned long long)deferred, (unsigned long long)deferEarly,
            (unsigned long long)joined, (unsigned long long)joinPasses, (unsigned long long)noLand);
}
