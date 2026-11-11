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

#include "rd_replay.h"
#include "rd_upscale.h"

static int s_force = -1; /* rd__late_pass_force: -1 none, else the bits */

static struct {
    bool read;
    int bits; /* ICO_RD_LATE_PASS: -1 not given */
    char said[256];
} s_sw = {false, -1, ""};

/* the 10 s line's counts since its last call (the window's thread reads
 * and clears them; the replay adds) */
static struct {
    uint64_t held, flushed, why[RD_LATE_WHY_COUNT], joined, joinPasses;
} s_sum;

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
    [RD_LATE_WHY_PASTE] = "the aura's paste pass could not take it",
    [RD_LATE_WHY_SNAP] = "SCENE's snapshot taken again",
    [RD_LATE_WHY_BORDER] = "SCENE's border repeat",
    [RD_LATE_WHY_MSAA] = "the MSAA epoch",
    [RD_LATE_WHY_END] = "the walk's end",
};

const char *rd__late_pass_why_text(int why)
{
    return why >= 0 && why < RD_LATE_WHY_COUNT && kWhy[why] ? kWhy[why] : "unknown";
}

const char *rd__late_pass_bits_text(unsigned bits)
{
    bits &= RD_LATE_ALL;
    return bits == RD_LATE_ALL ? "on" : bits == 0 ? "off" : bits == RD_LATE_FILL ? "fill" : "join";
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
        snprintf(line, sizeof(line), "late pass: %s (%s):%s%s (the same bytes)",
                 bits == RD_LATE_ALL ? "on" : rd__late_pass_bits_text(bits), why,
                 bits & RD_LATE_FILL ? " the crack fill's draw held from its snapshot to the "
                                       "aura's paste pass in a frame whose glow draws nothing"
                                     : "",
                 bits & RD_LATE_JOIN ? (bits & RD_LATE_FILL ? "; list 11's draws that use no "
                                                              "depth in the open pass without it"
                                                            : " list 11's draws that use no "
                                                              "depth in the open pass without it")
                                     : "");
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
            s_sw.bits = strcmp(e, "0") == 0      ? 0
                        : strcmp(e, "1") == 0    ? (int)RD_LATE_ALL
                        : strcmp(e, "fill") == 0 ? (int)RD_LATE_FILL
                        : strcmp(e, "join") == 0 ? (int)RD_LATE_JOIN
                                                 : -1;
            if (s_sw.bits < 0) {
                rd__log("late pass: ICO_RD_LATE_PASS=%.16s is not 0, 1, fill or join: the "
                        "profile's",
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
        bits = RD_LATE_ALL;
        why = "a tile-based GPU's default";
    } else {
        bits = 0;
        why = g_rd.hasDevice && g_rd.profile.tiler ? "not under Vulkan" : "not a tile-based GPU";
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
        if (rd__late_pass_class(f, c, st, -1, &why) != RD_LATE_HOLD) {
            s->done = true; /* the held draw lands here, or is drawn before c */
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

/* the walk holds under the DARK plan (or the tests' any plan) */
static bool planHolds(void)
{
    return (s_force >= 0 && ((unsigned)s_force & RD_LATE_ANY_PLAN) != 0) ||
           rd__glow_light_plan() == RD_GLOW_LIGHT_DARK;
}

bool rd__late_pass_scan_end(const RdLateScan *s, uint32_t end, int endL, uint32_t *ord, int *l,
                            uint32_t *i)
{
    if (!s->held) {
        return false;
    }
    if (!planHolds()) {
        *ord = s->takeOrd; /* drawn where it is taken, as without the hold */
        *l = s->takeL;
        *i = s->takeI;
    } else if (s->done) {
        *ord = s->landOrd;
        *l = s->landL;
        *i = s->landI;
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
           rd__replay_msaa_want() <= 1u && g_rd.dstMode == RD_BLEND_EXACT_OFF && !g_rdWin.first;
}

void rd__late_pass_held_drawn(const RdTargetRec *scene)
{
    g_rdPerf.latePassHeld++;
    g_rdPerf.latePassTexels += scene ? scene->tw * scene->th : 0u;
    __atomic_add_fetch(&s_sum.held, 1, __ATOMIC_RELAXED);
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("late pass: the crack fill's draw held from its snapshot to the aura's paste "
                "pass (the glow drew nothing between), not in a pass of its own (reported "
                "once)");
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

/* ------------------------------------------------------------ the line */

void rd_late_pass_line(void)
{
    const uint64_t held = __atomic_exchange_n(&s_sum.held, 0, __ATOMIC_RELAXED);
    const uint64_t flushed = __atomic_exchange_n(&s_sum.flushed, 0, __ATOMIC_RELAXED);
    const uint64_t joined = __atomic_exchange_n(&s_sum.joined, 0, __ATOMIC_RELAXED);
    const uint64_t joinPasses = __atomic_exchange_n(&s_sum.joinPasses, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_LATE_WHY_COUNT];
    for (int k = 0; k < RD_LATE_WHY_COUNT; k++) {
        w[k] = __atomic_exchange_n(&s_sum.why[k], 0, __ATOMIC_RELAXED);
    }
    if (held + flushed + joined == 0) {
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
            "window: late pass: %llu crack fills held into the aura's paste pass, %llu drawn "
            "early in a pass of their own%s%s; %llu list 11 draws joined the open pass without "
            "SCENE's depth (%llu passes not begun)\n",
            (unsigned long long)held, (unsigned long long)flushed, why, at ? ")" : "",
            (unsigned long long)joined, (unsigned long long)joinPasses);
}
