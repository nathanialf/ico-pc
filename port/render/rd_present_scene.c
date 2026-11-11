/* rd_present_scene.c: the present from SCENE (rd_present_scene.h;
 * docs/port/RENDER_POST.md "The present from SCENE (off by default until
 * measured)").
 *
 * The reduction (rd_post.c postReduction: list 12's black sprite and the
 * textured sprite reading SCENE into DISPLAY) is, above 1x, a hardware
 * sprite (rd_blur.c rd__blur_screen_fallback) whose sample lies on a SCENE
 * texel's centre wherever its offset is snapped to a whole texel
 * (rd__blur_snap_verts: a scale that is not whole) and DISPLAY has SCENE's
 * texels per GS pixel (full height: one SCENE row a DISPLAY row).  There
 * DISPLAY(x, y) = g(SCENE(x + dx, y + dy)) exactly: g the reduction's
 * per-texel arithmetic (the fades fold's steps, sprite.hlsl sprite_fold
 * without bars, then its texture function with the tint), measured on the
 * Pixel's dump at 2364 x 1080 (dx 1, dy 0, every texel, the edges clamped
 * as the sprite's CLAMP holds them).  The present's box blit then samples
 * DISPLAY with the hardware filter.  Here the reduction is not drawn (its
 * pass on DISPLAY, 9.7 MB stored at 2364 x 1080, gone) and the box blit
 * samples SCENE at the same place moved by (dx, dy), then applies g
 * (blit.hlsl blit_scene_ps).  The order changes: g's rounding comes after
 * the filter instead of before it, so the picture is within a level of the
 * reduction's where the sampler weighs SCENE's taps as it weighed
 * DISPLAY's (the derivation in RENDER_POST.md), and is not byte for byte.
 *
 * The conditions (each with the site that decides it; any one keeps the
 * reduction, counted with its reason, RD_PS_WHY_*):
 *   - the switch (RD_PRESENT_SCENE_DEFAULT below, ICO_RD_PRESENT_SCENE);
 *   - a device and blit_scene_ps in the shader table (g_rd.fs);
 *   - no pass window (g_rdWin.first: the tool's bisect reads mid-frame);
 *   - the reduction's hardware sprite (rd__blur_screen_fallback: a scaled
 *     target; the GS sprite model at scale 1, the Original preset and 1x,
 *     is not taken);
 *   - no MSAA (rd_aa_samples), no GS state as shader data (rd__uber_on);
 *   - no upscaler set (RdSettings.upscale: FSR 1.0 reads DISPLAY at the
 *     present, a temporal one takes SCENE at the reduction:
 *     rd__upscale_point_on) and no display layer in force (rd__layer_active:
 *     SCENE's record lent to the composite this replay; rd__layer_on is only
 *     its [dev] switch, on everywhere, the layer itself needing the split);
 *   - the present reads DISPLAY in its box blit alone
 *     (rd__present_scene_present_why, rd_present.c: no CRT filter, no
 *     Downsampling, no doubled lines (full height), no effects depth, no
 *     capture armed or pending, no window reduce);
 *   - the border open (rd__replay_border_open: the scissor the whole target,
 *     CLAMP on both axes) without the exact width's repeated columns
 *     (rd_blur_border.c drawStrips, the strips argument);
 *   - the pair's form (planPair): the black sprite, then only state
 *     commands, NOPs and deferred text, then the textured sprite into
 *     DISPLAY reading SCENE's colour (PSMCT32) with MODULATE or DECAL, a
 *     flat tint of 0x80 (the texture function the identity), no blend, no
 *     DATE, no alpha test, no FBA, the colour mask whole, no Z test or
 *     write, no field offset or UV offset, covering every GS pixel
 *     (extendFullFrame leaves it alone) and every DISPLAY texel;
 *   - the map (rd__present_scene_axis): one SCENE texel a DISPLAY texel on
 *     both axes, each sample on a SCENE texel's centre at a whole offset;
 *   - the fades fold (rd__fades_fold_present) without bars, planned at the
 *     textured sprite if at all;
 *   - nothing after the textured sprite in the frame touches DISPLAY or
 *     SCENE (a draw into or from either, a copy, a clear, a command not
 *     known), the deferred text's quads aside.
 * When in doubt the reduction is drawn.
 *
 * DISPLAY is then owed (s_ps.owed, the plan's targets, their textures and
 * generations): every later reader gets it drawn first from the same SCENE
 * by the same arithmetic (blit_scene_ps reading one texel a pixel by Load),
 * the bytes the reduction would have stored:
 *   - the next replay (rd__present_scene_begin, from replayFrame after the
 *     store plan's scan, before anything is recorded on SCENE): its lists
 *     read with their state (rd__present_scene_settle); a read of DISPLAY
 *     before DISPLAY is written whole (a keep frame's KEEP sprite, the
 *     motion blur's DISPLAY, rd_interp.c's held copy at the head, the
 *     water's block of FBP 0) draws it in the replay's list, as does a
 *     frame that draws on SCENE without writing DISPLAY again; the
 *     reduction's black sprite over the whole of DISPLAY or a clear of it
 *     first drops the debt (DISPLAY written again before anything reads
 *     it); a frame touching neither (the still re-present's empty keep
 *     frame, rd_reshow.c) keeps it: its present samples SCENE again, which
 *     no replay changed;
 *   - a read outside a replay (rd__present_scene_readback: rd_replay.c
 *     rd__read_target, so the F12 dump, a save state's picture, the tests
 *     and the tool's PNGs; rd_display_probe), in a frame of its own;
 *   - a present that cannot read SCENE after all (rd__present_scene_draw
 *     from rd_present.c: a capture armed since the replay, a setting
 *     changed), in the present's list before its first pass.
 * SCENE is that frame's then: nothing but a replay draws on it, and every
 * replay settles the debt before its first command; its texture and
 * generation are checked (a SCENE made again with DISPLAY kept loses the
 * picture: said, and the switch is off from then on).
 *
 * Counters: RdPerfRecord.presentScene* (rd.h); rd_replay_tool --stats
 * "present from SCENE"; the 10 s line here, while on. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_present_scene.h"
#include "rd_layer.h"
#include "rd_replay.h"
#include "rd_upscale.h"

/* The default: off in v0.11.5 until the owner has seen the measurement
 * (build-host's present_scene_measure.sh).  The one place to flip. */
#define RD_PRESENT_SCENE_DEFAULT 1

static const char *const kWhy[RD_PS_WHY_COUNT] = {
    "taken",
    "the switch off",
    "no device or no blit_scene_ps",
    "a pass window",
    "the reduction's GS sprite model (scale 1)",
    "MSAA",
    "GS state as shader data",
    "an upscaler",
    "the display layer",
    "the CRT filter",
    "Downsampling",
    "the present doubles DISPLAY's lines",
    "the effects depth",
    "a capture armed",
    "the present's reduce of DISPLAY",
    "the border closed",
    "the open border's repeated columns",
    "the reduction's sprites not in the form",
    "not one SCENE texel a DISPLAY texel at a whole offset",
    "the fades fold's bars",
    "DISPLAY or SCENE touched after the reduction",
    "a replay reads DISPLAY first",
    "a replay draws on SCENE first",
    "a read of DISPLAY outside a replay",
};

const char *rd__present_scene_why_text(int why)
{
    return why >= 0 && why < RD_PS_WHY_COUNT ? kWhy[why] : "?";
}

/* ---------------------------------------------------------------- switch */

static int s_force = -1;
static bool s_lost; /* a debt lost (SCENE made again): off from then on */

bool rd__present_scene_on(void)
{
    static int env = -2;
    if (env == -2) {
        const char *e = getenv("ICO_RD_PRESENT_SCENE");
        env = e && (e[0] == '0' || e[0] == '1') && e[1] == '\0' ? e[0] - '0' : -1;
        const bool on = env >= 0 ? env != 0 : RD_PRESENT_SCENE_DEFAULT != 0;
        if (on) {
            rd__log("present from SCENE: on%s: where only the present reads DISPLAY the "
                    "reduction is not drawn and the present samples SCENE through the "
                    "reduction's steps (within a level of the reduction's picture where the "
                    "filter weighs the taps alike; docs/port/RENDER_POST.md)",
                    env >= 0 ? " (ICO_RD_PRESENT_SCENE=1)" : "");
        } else {
            rd__log("present from SCENE: off%s (the default until it is measured): the "
                    "reduction is drawn into DISPLAY and the present reads DISPLAY",
                    env == 0 ? " (ICO_RD_PRESENT_SCENE=0)" : "");
        }
    }
    if (s_lost) {
        return false;
    }
    if (s_force >= 0) {
        return s_force != 0;
    }
    return env >= 0 ? env != 0 : RD_PRESENT_SCENE_DEFAULT != 0;
}

void rd__present_scene_force(int mode)
{
    s_force = mode < 0 ? -1 : (mode != 0);
    s_lost = false;
}

/* ------------------------------------------------------------ predicate */

int rd__present_scene_why(const RdPresentSceneIn *in)
{
    if (!in->on) {
        return RD_PS_WHY_OFF;
    }
    if (!in->device) {
        return RD_PS_WHY_DEVICE;
    }
    if (in->window) {
        return RD_PS_WHY_WINDOW;
    }
    if (!in->scaled) {
        return RD_PS_WHY_SCALE1;
    }
    if (in->msaa) {
        return RD_PS_WHY_MSAA;
    }
    if (in->uber) {
        return RD_PS_WHY_UBER;
    }
    if (in->upscaler) {
        return RD_PS_WHY_UPSCALER;
    }
    if (in->layer) {
        return RD_PS_WHY_LAYER;
    }
    if (in->presentWhy) {
        return in->presentWhy;
    }
    if (!in->borderOpen) {
        return RD_PS_WHY_BORDER;
    }
    if (in->strips) {
        return RD_PS_WHY_STRIPS;
    }
    if (!in->formOk) {
        return RD_PS_WHY_FORM;
    }
    if (!in->mapOk) {
        return RD_PS_WHY_RESAMPLE;
    }
    if (in->bars) {
        return RD_PS_WHY_BARS;
    }
    if (in->after) {
        return RD_PS_WHY_AFTER;
    }
    return RD_PS_WHY_NONE;
}

/* the 12.4 coordinate rounded up to a whole GS pixel (rd_replay.c snapAxis) */
static int64_t ceil16(int64_t a)
{
    return (a + 15) & ~(int64_t)15;
}

bool rd__present_scene_axis(int32_t pa, int32_t pb, double ua, double ub, int32_t origin16,
                            double sd, double od, double ss, double os, uint32_t n, int32_t *shift)
{
    if (pa == pb || n == 0 || !(sd > 0.0) || !(ss > 0.0)) {
        return false;
    }
    /* GS texels a GS pixel; the sample shift setUvShift gives the bound
     * target's axis, (s - 1) / (2 s) GS pixels of it, taken off the UVs
     * (snapAxis); the corners' snap moves the UVs along their own line */
    const double dudp = (ub - ua) / ((double)pb - (double)pa);
    const double uvShift = sd > 1.0 ? (sd - 1.0) / (2.0 * sd) : 0.0;
    if (fabs(dudp * ss / sd - 1.0) * (double)n > 1.0 / 1024.0) {
        return false; /* not one SCENE texel a DISPLAY texel */
    }
    const int64_t lo = ceil16(pa < pb ? pa : pb), hi = ceil16(pa < pb ? pb : pa);
    double first = 0.0;
    for (int k = 0; k < 2; k++) {
        const double x = k == 0 ? 0.0 : (double)(n - 1u);
        /* texel x's centre is GS coordinate (x + od) / sd: the exact
         * viewport puts GS coordinate g at texel g sd - od + 1/2 (FrameCB's
         * half texel, rd_replay.c rd__frame_group_ex) */
        const double g16 = (double)origin16 + 16.0 * (x + od) / sd;
        if (!(g16 >= (double)lo && g16 < (double)hi)) {
            return false; /* a texel the sprite leaves (the rasteriser's top-left rule) */
        }
        const double u16 = ua + (g16 - (double)pa) * dudp - 16.0 * uvShift * dudp;
        /* the sampler's coordinate in SCENE's texels: u ss - os (common.hlsli
         * gs_uv_norm), centres at k + 1/2 */
        const double idx = u16 / 16.0 * ss - os - 0.5 - x;
        const double r = floor(idx + 0.5);
        /* on the centre but for the float error of the corners' UVs (about
         * 2e-4 texel at 2364 texels): a neighbour's weight under 1/1024
         * moves a sample less than a quarter level, so the filter returns
         * the texel itself, which Load reads */
        if (fabs(idx - r) > 1.0 / 1024.0 || fabs(r) > 64.0) {
            return false; /* between two texels: the filter blends them */
        }
        if (k == 0) {
            first = r;
        } else if (r != first) {
            return false;
        }
    }
    *shift = (int32_t)first;
    return true;
}

/* whether command c under st touches target id: a draw into it or on its
 * depth, its texture (enabled or not), a copy or exact blend naming it, a
 * clear of it, a command not known (rd_pass_cut.c touches, the clear a
 * touch here) */
static bool touches(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    if (c->type == RDC_SCREEN && g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS) {
        return false; /* drawn on the output (rd_replay.c doScreen) */
    }
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return false;
    case RDC_CLEAR:
        return c->u[0] == id;
    case RDC_COPY:
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
    case RDC_WORLD_PRIMS: {
        const RdTexRec *t = rd__tex_rec(st->tex);
        return st->color == id || st->depth == id ||
               (t && t->kind == RD_TEXKIND_TARGET && t->target == id);
    }
    default:
        return true;
    }
}

/* the reduction's black sprite (untextured) into disp over the whole of it:
 * the scissor the target's (it writes every GS pixel the textured sprite
 * would have) */
static bool blackWhole(const RdCmd *c, const RdStateBlock *st, uint32_t disp)
{
    if (c->type != RDC_POST_STUB || c->b[0] != RD_POST_REDUCTION || st->ds.texEnabled ||
        st->color != disp) {
        return false;
    }
    const RdTargetRec *t = rd__target_rec(disp);
    const int32_t w = t && t->w ? (int32_t)t->w : (int32_t)st->gsW;
    const int32_t h = t && t->h ? (int32_t)t->h : (int32_t)st->gsH;
    return w > 0 && h > 0 && st->scissor[0] <= 0 && st->scissor[1] <= 0 &&
           st->scissor[2] >= w - 1 && st->scissor[3] >= h - 1;
}

int rd__present_scene_settle(const RdFrame *f, int keep, uint32_t disp, uint32_t scene, int *why)
{
    *why = RD_PS_WHY_NONE;
    if (!f) {
        return RD_PS_SETTLE_KEEP;
    }
    RdStateBlock st = f->startState;
    bool sceneTouched = false;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            if (rd__cmd_is_state(c->type)) {
                (void)rd__apply_state(&st, c);
                continue;
            }
            if (blackWhole(c, &st, disp) || (c->type == RDC_CLEAR && c->u[0] == disp)) {
                return RD_PS_SETTLE_DROP;
            }
            if (touches(c, &st, disp)) {
                *why = RD_PS_WHY_READ;
                return RD_PS_SETTLE_DRAW;
            }
            sceneTouched = sceneTouched || touches(c, &st, scene);
        }
    }
    if (sceneTouched) {
        *why = RD_PS_WHY_SCENE;
        return RD_PS_SETTLE_DRAW;
    }
    return RD_PS_SETTLE_KEEP;
}

/* ----------------------------------------------------------------- state */

typedef struct PsPlan {
    uint32_t disp, scene;            /* the targets' ids */
    uint32_t dispGen, sceneGen;      /* RdTargetRec.gen */
    uint32_t dispTex, sceneTex;      /* their colour textures' ids */
    int32_t dx, dy;                  /* SCENE texel x + dx, y + dy for DISPLAY's x, y */
    uint32_t col[4];                 /* the tint (0x80 = 1.0) */
    uint32_t mode;                   /* ICO_DF_TEXTURED | DECAL | TCC_RGBA | FOLD */
    uint32_t steps[4], meta, alphas; /* the fades fold's words (sprite.hlsl sprite_fold) */
    uint32_t frame;                  /* the frame's number (the log) */
} PsPlan;

static struct {
    bool owed;
    PsPlan debt;
    /* the textured sprite planned at the black one */
    bool armed;
    uint32_t armedReplay, armedOrd;
    /* the 10 s line */
    double since;
    uint32_t replays, skipped, shown, kept, drawn;
    uint32_t keptWhy[RD_PS_WHY_COUNT], drawWhy[RD_PS_WHY_COUNT];
    uint32_t keptReplay; /* the replay a kept reduction was counted for */
} s_ps;

static bool debtTargets(const RdTargetRec **d, const RdTargetRec **sc)
{
    *d = rd__target_rec(s_ps.debt.disp);
    *sc = rd__target_rec(s_ps.debt.scene);
    return *d && (*d)->gen == s_ps.debt.dispGen && (*d)->color.id == s_ps.debt.dispTex;
}

static bool sceneSame(const RdTargetRec *sc)
{
    return sc && sc->gen == s_ps.debt.sceneGen && sc->color.id == s_ps.debt.sceneTex;
}

void rd__present_scene_init(void)
{
    s_ps.owed = false;
    s_ps.armed = false;
    s_ps.keptReplay = 0;
}

bool rd__present_scene_owed(void)
{
    const RdTargetRec *d, *sc;
    return s_ps.owed && debtTargets(&d, &sc) && sceneSame(sc);
}

/* the 10 s line, at a replay's start while on */
static void tenSeconds(void)
{
    const double now = rd__now_ms();
    if (s_ps.since == 0.0) {
        s_ps.since = now;
        return;
    }
    if (now - s_ps.since < 10000.0 || s_ps.replays == 0) {
        return;
    }
    char kept[512] = "", drawn[512] = "";
    for (int pass = 0; pass < 2; pass++) {
        char *out = pass ? drawn : kept;
        const uint32_t *n = pass ? s_ps.drawWhy : s_ps.keptWhy;
        size_t at = 0;
        for (int k = 0; k < RD_PS_WHY_COUNT; k++) {
            if (!n[k]) {
                continue;
            }
            const int m =
                snprintf(out + at, sizeof(kept) - at, "%s%u %s", at ? ", " : "", n[k], kWhy[k]);
            if (m < 0 || (size_t)m >= sizeof(kept) - at) {
                break;
            }
            at += (size_t)m;
        }
    }
    const double r = (double)s_ps.replays;
    rd__log("present from SCENE: %.2f a replay (%.2f presents from SCENE), %.2f through DISPLAY "
            "(%s); %u drawn on demand (%s) (%u replays, %.0f s)",
            s_ps.skipped / r, s_ps.shown / r, s_ps.kept / r, kept[0] ? kept : "none", s_ps.drawn,
            drawn[0] ? drawn : "none", s_ps.replays, (now - s_ps.since) / 1000.0);
    s_ps.since = now;
    s_ps.replays = s_ps.skipped = s_ps.shown = s_ps.kept = s_ps.drawn = 0;
    memset(s_ps.keptWhy, 0, sizeof(s_ps.keptWhy));
    memset(s_ps.drawWhy, 0, sizeof(s_ps.drawWhy));
}

/* the reduction drawn with the switch on: counted once a replay, its reason
 * logged the first time */
static void noteKept(int why, uint32_t frame)
{
    if (s_ps.keptReplay == g_rd.replayCounter) {
        return;
    }
    s_ps.keptReplay = g_rd.replayCounter;
    s_ps.kept++;
    s_ps.keptWhy[why]++;
    g_rdPerf.presentSceneKept = 1;
    g_rdPerf.presentSceneWhy = (uint32_t)why;
    static bool said[RD_PS_WHY_COUNT];
    if (!said[why]) {
        said[why] = true;
        rd__log("present from SCENE: the reduction drawn into DISPLAY: %s (frame %u; reported "
                "once for this reason)",
                kWhy[why], frame);
    }
}

/* -------------------------------------------------------- the pair's plan */

/* the textured sprite's form and payload (planPair), its pair of vertices
 * after the snap in v */
static bool texturedForm(const RdStateBlock *st, const RdFrame *f, const RdCmd *c, uint32_t disp,
                         uint32_t scene, RdScreenVtx v[2], bool *scaled)
{
    *scaled = false;
    if (c->u[1] > f->payloadSize || f->payloadSize - c->u[1] < sizeof(RdPostRec)) {
        return false;
    }
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const uint32_t vo = rd__blur_screen_fallback(RD_POST_REDUCTION, &p, st);
    *scaled = vo != ~0u;
    if (!*scaled || vo > f->payloadSize || f->payloadSize - vo < 2u * sizeof(RdScreenVtx)) {
        return false;
    }
    memcpy(v, f->payload + vo, 2u * sizeof(RdScreenVtx));
    rd__blur_snap_verts(st, v);
    const RdDrawState *d = &st->ds;
    const RdTexRec *t = rd__tex_rec(st->tex);
    return st->color == disp && d->texEnabled && t && t->kind == RD_TEXKIND_TARGET &&
           t->view == RD_VIEW_RGBA && t->target == scene && t->src == ICO_TEXFMT_RGBA32 &&
           (d->texFn == RD_TEXFN_MODULATE || d->texFn == RD_TEXFN_DECAL) && !d->abe &&
           d->test.date == RD_DATE_OFF && !(d->test.ate && d->test.atst != RD_ATST_ALWAYS) &&
           !d->fba && d->colorMask == 0xF && d->fbmsk == 0 && !st->aa1 &&
           d->zwrite != RD_ZWRITE_ON && !(d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) &&
           st->useOffset == 0 && st->uvOffset[0] == 0.0f && st->uvOffset[1] == 0.0f &&
           memcmp(v[0].rgba, v[1].rgba, 4) == 0 && v[0].rgba[0] == 0x80 && v[0].rgba[1] == 0x80 &&
           v[0].rgba[2] == 0x80 && v[0].rgba[3] == 0x80 && rd__a1x_gs_keep();
}

/* every GS pixel of the target covered (rd_replay.c extendFullFrame then
 * leaves the pair as it is, so its UVs keep their line) */
static bool coversPixels(const RdScreenVtx v[2], const RdStateBlock *st, const RdTargetRec *d)
{
    const int32_t ox = 2048 - (int32_t)(st->gsW >> 1), oy = 2048 - (int32_t)(st->gsH >> 1);
    const int32_t x0 = v[0].x < v[1].x ? v[0].x : v[1].x, x1 = v[0].x < v[1].x ? v[1].x : v[0].x;
    const int32_t y0 = v[0].y < v[1].y ? v[0].y : v[1].y, y1 = v[0].y < v[1].y ? v[1].y : v[0].y;
    return (int32_t)(ceil16(x0) / 16) - ox <= 0 &&
           (int32_t)(ceil16(x1) / 16) - 1 - ox >= (int32_t)d->w - 1 &&
           (int32_t)(ceil16(y0) / 16) - oy <= 0 &&
           (int32_t)(ceil16(y1) / 16) - 1 - oy >= (int32_t)d->h - 1;
}

/* c the black sprite (untextured RD_POST_REDUCTION) the walk is at: the
 * plan of the pair, RD_PS_WHY_NONE when the reduction may go undrawn;
 * *texOrd the textured sprite's ordinal */
static int planPair(Replay *r, const RdFrame *f, const RdCmd *c, bool strips, PsPlan *out,
                    uint32_t *texOrd)
{
    RdPresentSceneIn in;
    memset(&in, 0, sizeof(in));
    in.on = true;
    in.device = g_rd.hasDevice && g_rd.fs[RD_FS_BLIT_SCENE].id != 0;
    in.window = g_rdWin.first != 0;
    in.msaa = rd_aa_samples(rd__aa_mode()) > 1u;
    in.uber = rd__uber_on();
    in.upscaler = g_rd.settings.upscale != RD_UPSCALE_OFF || rd__upscale_point_on();
    /* the layer in force (SCENE lent to the composite), not its switch:
     * rd__layer_on is on on every device, the layer only runs under a
     * temporal upscaler (the phone's rc6: kept every replay for the switch) */
    in.layer = rd__layer_active();
    in.borderOpen = rd__replay_border_open();
    in.strips = strips;
    in.presentWhy = rd__present_scene_present_why();
    memset(out, 0, sizeof(*out));
    const uint32_t disp = r->st.color, scene = rd_target(RD_TARGET_SCENE).id;
    /* the command's place in the frame */
    int l = -1;
    uint32_t i = 0;
    for (int k = 0; k < RD_LIST_COUNT && l < 0; k++) {
        const RdCmdList *list = &f->lists[k];
        if (list->count && c >= list->cmds && c < list->cmds + list->count) {
            l = k;
            i = (uint32_t)(c - list->cmds);
        }
    }
    const RdCmd *tc = NULL;
    RdStateBlock st = r->st;
    uint32_t j = i + 1u;
    if (l >= 0) {
        const RdCmdList *list = &f->lists[l];
        for (; j < list->count; j++) {
            const RdCmd *e = &list->cmds[j];
            if (rd__cmd_is_state(e->type)) {
                (void)rd__apply_state(&st, e);
                continue;
            }
            if (e->type == RDC_NOP || e->type == RDC_OVERLAY_TEXT) {
                continue;
            }
            if (e->type == RDC_POST_STUB && e->b[0] == RD_POST_REDUCTION && st.ds.texEnabled) {
                tc = e;
            }
            break;
        }
    }
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    const RdTargetRec *d = rd__target_rec(disp);
    const RdTargetRec *sc = rd__target_rec(scene);
    in.scaled = true; /* unless the textured sprite says otherwise */
    if (tc && d && sc && d->color.id && sc->color.id && d->format == RHI_FMT_RGBA8_UNORM &&
        sc->format == RHI_FMT_RGBA8_UNORM) {
        bool scaled = false;
        in.formOk = texturedForm(&st, f, tc, disp, scene, v, &scaled) && coversPixels(v, &st, d);
        in.scaled = scaled;
    }
    if (in.formOk) {
        const int32_t ox16 = 16 * (2048 - (int32_t)(st.gsW >> 1));
        const int32_t oy16 = 16 * (2048 - (int32_t)(st.gsH >> 1));
        in.mapOk = rd__present_scene_axis(v[0].x, v[1].x, v[0].s, v[1].s, ox16, d->sx, d->ox,
                                          sc->sx, sc->ox, d->tw, &out->dx) &&
                   rd__present_scene_axis(v[0].y, v[1].y, v[0].t, v[1].t, oy16, d->sy, 0.0, sc->sy,
                                          0.0, d->th, &out->dy);
    }
    *texOrd = g_rdStoreOrd + (j - i);
    RdFadesFold fold;
    memset(&fold, 0, sizeof(fold));
    const bool folded = in.formOk && rd__fades_fold_present(*texOrd, false, &fold);
    /* the fold planned for the hardware sprite (the steps after the
     * filter), at most RD_FADES_FOLD_STEPS of them, none on the bars alone */
    in.formOk = in.formOk && (!folded || (fold.scaled && (fold.meta & 7u) <= 4u));
    in.bars = folded && (fold.meta & 0xF0u) != 0u;
    /* nothing after the textured sprite touches DISPLAY or SCENE */
    if (in.formOk && l >= 0) {
        RdStateBlock at = st;
        for (int k = l; k < RD_LIST_COUNT && !in.after; k++) {
            const RdCmdList *list = &f->lists[k];
            for (uint32_t e = k == l ? j + 1u : 0u; e < list->count; e++) {
                const RdCmd *q = &list->cmds[e];
                if (rd__cmd_is_state(q->type)) {
                    (void)rd__apply_state(&at, q);
                    continue;
                }
                if (touches(q, &at, disp) || touches(q, &at, scene)) {
                    in.after = true;
                    break;
                }
            }
        }
    }
    const int why = rd__present_scene_why(&in);
    if (why != RD_PS_WHY_NONE) {
        return why;
    }
    out->disp = disp;
    out->scene = scene;
    out->dispGen = d->gen;
    out->sceneGen = sc->gen;
    out->dispTex = d->color.id;
    out->sceneTex = sc->color.id;
    for (int k = 0; k < 4; k++) {
        out->col[k] = v[0].rgba[k];
    }
    out->mode = ICO_DF_TEXTURED | (st.ds.texFn == RD_TEXFN_DECAL ? ICO_DF_DECAL : 0u) |
                (st.ds.tcc == RD_TCC_RGBA ? ICO_DF_TCC_RGBA : 0u);
    if (folded) {
        out->mode |= ICO_DF_FOLD;
        memcpy(out->steps, fold.steps, sizeof(out->steps));
        out->meta = fold.meta;
        out->alphas = fold.alphas;
    }
    out->frame = f->number;
    return RD_PS_WHY_NONE;
}

bool rd__present_scene_skip(struct Replay *rp, const RdFrame *f, const RdCmd *c, bool strips)
{
    if (c->type != RDC_POST_STUB || c->b[0] != RD_POST_REDUCTION) {
        return false;
    }
    Replay *r = rp;
    if (s_ps.armed) {
        s_ps.armed = false;
        if (s_ps.armedReplay == g_rd.replayCounter && s_ps.armedOrd == g_rdStoreOrd) {
            return true; /* the textured sprite planned with the black one */
        }
    }
    if (r->st.ds.texEnabled || !rd__present_scene_on()) {
        return false; /* a textured sprite without its black one: drawn */
    }
    PsPlan plan;
    uint32_t texOrd = 0;
    const int why = planPair(r, f, c, strips, &plan, &texOrd);
    if (why != RD_PS_WHY_NONE) {
        noteKept(why, f->number);
        return false;
    }
    if ((plan.mode & ICO_DF_FOLD) != 0u) {
        (void)rd__fades_fold_present(texOrd, true, NULL); /* the fold taken */
    }
    s_ps.debt = plan;
    s_ps.owed = true;
    s_ps.armed = true;
    s_ps.armedReplay = g_rd.replayCounter;
    s_ps.armedOrd = texOrd;
    s_ps.skipped++;
    g_rdPerf.presentSceneSkipped = 1;
    static bool said;
    if (!said) {
        said = true;
        const RdTargetRec *sc = rd__target_rec(plan.scene);
        rd__log("present from SCENE: the reduction not drawn (frame %u): DISPLAY is SCENE "
                "through the reduction's steps at a whole offset (%d, %d) texels (SCENE %ux%u "
                "at %.4gx%.4g, %u fold steps); the present samples SCENE, every other reader "
                "gets DISPLAY drawn on demand (reported once)",
                plan.frame, plan.dx, plan.dy, sc ? sc->tw : 0, sc ? sc->th : 0,
                sc ? (double)sc->sx : 0.0, sc ? (double)sc->sy : 0.0, plan.meta & 7u);
    }
    return true;
}

/* -------------------------------------------------------------- the draw */

/* blit_scene_ps's DrawCB: the plan's g, the offset, load (DISPLAY drawn one
 * texel a pixel) or the present's filter, the colour adjustment */
static void fillCb(IcoDrawCB *cb, bool load, bool tone)
{
    memcpy(cb->col, s_ps.debt.col, sizeof(cb->col));
    cb->mode[0] = s_ps.debt.mode;
    cb->mode[3] = tone ? 1u : 0u;
    memcpy(cb->aux0, s_ps.debt.steps, sizeof(cb->aux0));
    cb->aux1[0] = s_ps.debt.meta;
    cb->aux1[1] = s_ps.debt.alphas;
    cb->scale[0] = (float)s_ps.debt.dx;
    cb->scale[1] = (float)s_ps.debt.dy;
    cb->scale[2] = load ? 1.0f : 0.0f;
}

static void countDrawn(int why)
{
    s_ps.drawn++;
    s_ps.drawWhy[why]++;
    g_rdPerf.presentSceneDrawn++;
    g_rdPerf.presentSceneDrawWhy = (uint32_t)why;
    static uint32_t seen;
    seen++;
    if (seen <= 8u || seen % 100u == 0u) {
        rd__log("present from SCENE: DISPLAY drawn on demand from SCENE (frame %u's): %s (the "
                "first 8, then every 100th: %u so far)",
                s_ps.debt.frame, kWhy[why], seen);
    }
}

void rd__present_scene_draw(RhiCommandList cl, int why)
{
    if (!s_ps.owed) {
        return;
    }
    s_ps.owed = false;
    s_ps.armed = false;
    const RdTargetRec *dc, *scc;
    if (!debtTargets(&dc, &scc)) {
        return; /* DISPLAY made again: it holds nothing of that frame either way */
    }
    RdTargetRec *d = rd__target_rec(s_ps.debt.disp);
    RdTargetRec *sc = rd__target_rec(s_ps.debt.scene);
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT_SCENE, d->format);
    const RhiPipeline pipe = g_rd.fs[RD_FS_BLIT_SCENE].id ? rd__get_pipeline(&k) : (RhiPipeline){0};
    if (!sceneSame(scc) || !pipe.id) {
        s_lost = true;
        rd__log("present from SCENE: DISPLAY could not be drawn from SCENE (%s; frame %u): it "
                "holds an older picture; the present from SCENE is off from now on",
                pipe.id ? "SCENE made again" : "no blit_scene_ps pipeline", s_ps.debt.frame);
        return;
    }
    /* a plain pass, as the present's own (its pipeline is the box blit's
     * kind: rd__post_key), on whatever DISPLAY's passes are begun as */
    rd__transition(cl, sc->color, &sc->colorState, RHI_STATE_SHADER_READ);
    rd__transition(cl, d->color, &d->colorState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = d->color;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel written */
    p.colorCount = 1;
    p.width = d->tw;
    p.height = d->th;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)d->tw, (float)d->th, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, d->tw, d->th};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    fillCb(&cb, true, false);
    cb.uvRect[2] = (float)d->tw;
    cb.uvRect[3] = (float)d->th;
    cb.tex[0] = (float)d->tw;
    cb.tex[1] = (float)d->th;
    cb.tex[2] = 1.0f / (float)d->tw;
    cb.tex[3] = 1.0f / (float)d->th;
    rd__set_pipeline(cl, pipe, &k);
    rd__bind_uniform(cl, 0, rd__frame_group(d->tw, d->th, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(
        cl, 2,
        rd__tex_group(sc->color, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                             RD_WRAP_CLAMP)));
    rhi_cmd_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    countDrawn(why >= 0 && why < RD_PS_WHY_COUNT ? why : RD_PS_WHY_READBACK);
}

void rd__present_scene_begin(const RdFrame *f, int keep)
{
    s_ps.armed = false;
    if (rd__present_scene_on()) {
        s_ps.replays++;
        tenSeconds();
    }
    if (!s_ps.owed) {
        return;
    }
    const RdTargetRec *d, *sc;
    if (!debtTargets(&d, &sc)) {
        s_ps.owed = false; /* DISPLAY made again (cleared before the walk) */
        return;
    }
    if (d->clearPending) {
        s_ps.owed = false; /* DISPLAY cleared before the walk (rd__clear_new_targets) */
        return;
    }
    int why = RD_PS_WHY_NONE;
    int v = rd__present_scene_settle(f, keep, s_ps.debt.disp, s_ps.debt.scene, &why);
    if (sc && sc->clearPending && v == RD_PS_SETTLE_KEEP) {
        /* SCENE is cleared before the walk (a self-test's "the next replay
         * starts SCENE from its clear", rd_fog_path.c, rd_fx_plan.c,
         * rd_shadow_tile.c, rd_msaa_shadow.c) without a new texture: a frame
         * touching neither target would present the cleared SCENE, so
         * DISPLAY is drawn now, before the clear */
        v = RD_PS_SETTLE_DRAW;
        why = RD_PS_WHY_SCENE;
    }
    if (v == RD_PS_SETTLE_KEEP) {
        return; /* nothing touches either: the present reads SCENE again */
    }
    if (v == RD_PS_SETTLE_DROP && !g_rdWin.first) {
        /* DISPLAY written whole before anything reads it (a pass window
         * may stop the walk before that: drawn then) */
        s_ps.owed = false;
        return;
    }
    if (v == RD_PS_SETTLE_DROP) {
        why = RD_PS_WHY_SCENE;
    }
    /* in the replay's list before its first pass, with the plain FrameCB
     * mapping (the exact viewport is the lists' own: rd_replay.c
     * setViewport) */
    rd__replay_vp_exact(false);
    rd__present_scene_draw(rd__replay_cl(), why);
    rd__replay_vp_exact(true);
}

void rd__present_scene_readback(uint32_t id)
{
    if (!s_ps.owed || id != s_ps.debt.disp || !g_rd.hasDevice) {
        return;
    }
    /* a frame of the renderer's own (the ring's slot for the two uniforms;
     * rd_video.c's way) */
    if (!rd__begin_own_frame(64u * 1024u)) {
        return;
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return;
    }
    rd__present_scene_draw(cl, RD_PS_WHY_READBACK);
    rhi_end_commands(cl);
    rhi_submit(cl);
}

/* ----------------------------------------------------------- the present */

bool rd__present_scene_ready(RhiFormat outFmt)
{
    if (!g_rd.fs[RD_FS_BLIT_SCENE].id) {
        return false;
    }
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT_SCENE, outFmt);
    return rd__get_pipeline(&k).id != 0;
}

RhiTexture rd__present_scene_texture(RhiCommandList cl)
{
    RdTargetRec *sc = rd__target_rec(s_ps.debt.scene);
    rd__transition(cl, sc->color, &sc->colorState, RHI_STATE_SHADER_READ);
    return sc->color;
}

void rd__present_scene_cb(struct IcoDrawCB *cb, bool tone)
{
    fillCb(cb, false, tone);
}

void rd__present_scene_shown(void)
{
    s_ps.shown++;
    g_rdPerf.presentSceneShown = 1;
}
