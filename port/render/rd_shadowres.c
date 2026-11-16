/* rd_shadowres.c: the shadow's levels at the scene's scale above 1x
 * (rd_shadowres.h; docs/port/RENDER_POST.md "The shadow's levels above 1x";
 * docs/port/DIVERGENCES.md VD-34).  Not an option: how the port draws the
 * shadow whenever the scene's scale is above 1 on an axis, in every preset
 * and at every aspect; at scale 1 on both axes nothing here is made or run.
 *
 * The game's shadow (Shadow.c shadow_Draw) is a count of the volumes, three
 * blur levels drawn one from the other by a 2:1 bilinear sprite (SHADOW0..2:
 * W/2 x 256, W/4 x 128, W/8 x 64 GS pixels) and three composites that
 * darken SCENE by each level magnified back with one bilinear tap.  The
 * levels keep the PS2's sizes at every scale, so above 1x a level texel is
 * 6, 12 and 24 SCENE texels at 3x: where an edge falls against the levels'
 * grid changes the penumbra (its 10-90 width 11.3 to 24.3 SCENE texels over
 * the grid's phase, its middle wandering 3.8 texels: the staircase along a
 * slanted edge).
 *
 * The count at the GS size (rd_replay.c shadowRedMake, shadowReduce,
 * shadowCountOne; moved here from rd_replay.c).  The first level reads the
 * count with a 2:1 bilinear sprite, which averages a 2x2 block of the
 * PS2's 512x512 count exactly; on a count at s times the resolution the
 * same sprite takes 2x2 taps of a 2s x 2s footprint, and the shadow's
 * integral varies with the volume's sub-pixel position (2.6 % at 4x against
 * 2.0 %).  With the count target scaled, doShadowResolve box-averages it
 * down to its GS size (box_reduce_ps, or the one draw's box) into the
 * reduced count and resolveTexture hands that to whatever samples the
 * count as a texture, so the level reads what the PS2's did.  Unscaled
 * targets never come there.  The named levels are still drawn from it.
 *
 * Above 1x (the count target's scale above 1 on an axis; each axis on its
 * own: 2.8125 x 2.41 under Resolution window; at 1 x 2 the levels keep the
 * PS2's width, the step and tent the identity across):
 *   1. the count is also kept at its full size: the seven draws and the
 *      MSAA resolve (the covered-majority resolve to one sample at the
 *      scene's size, rd_msaa_shadow.c) leave it there already; after the
 *      one draw (rd_shadow_one.c) the first level's step reads it from the
 *      stencil ("The count from the stencil" below), or a pass draws it a
 *      second time into the count target at its own texels (the box of one
 *      texel: the stencil's table texel itself);
 *   2. at each of the game's three level sprites (rd_shadow_box.c
 *      rd__shadow_box_screen, before the sprite's own pass), the level is
 *      drawn again into a texture of its own at S texels a GS pixel on each
 *      axis, S the smallest odd whole number at or above the scene's scale
 *      there (rd_sr_level_scale: 3 at 2x, 2.41x, 2.8125x and 3x, 5 at 4x
 *      and at 2160 rows; an axis at scale 1 keeps S = 1) (shadowres_step_ps):
 *      texel m stands for the GS position m / S, its taps the GS's two
 *      (0.75 / 0.25, one PS2 texel apart), the first level's each the box
 *      of count texels its GS pixel covers (partial texels by their share
 *      at a count scale that is not S), the later levels' the level before
 *      at 2 m and 2 m + S, whole texels; the sprite's colour MODULATEs it as
 *      on the GS;
 *   3. after the third, each level is blurred in two passes (across into a
 *      temporary of its size, down back into it: shadowres_tent_ps) by the
 *      tent its bilinear magnification supplied: weights S - |t - c|,
 *      c = S / 2 - 1/2 (whole at an odd S), over t = n - m.  The composite
 *      reads the level where the PS2 read the GS texel at u, fine texel
 *      S u - 1/2; with the tent there its one bilinear tap gives exactly the
 *      mean of the PS2's picture over the level grid's S x S placements:
 *      the same width and darkness, no staircase.  (At an even S the tent's
 *      centre would be half a texel off and the tap would soften it further:
 *      10 % wider at 2x in the edge model; hence the odd S);
 *   4. the composites are the game's, drawn as before, reading the fine
 *      level (resolveTexture, rd__shadow_fine) instead of the named one.
 * The game's own level sprites still draw the named levels as before (a
 * chain the rule could not take, a refusal, a draw into a level that is not
 * the chain's, all read them: the PS2's size).  All of it sits between the
 * count and the composites, where SCENE's pass is ended already (the DATE
 * snapshot's and the levels' own passes): no SCENE pass more on a tiler.
 * Every backend draws it (two plain pixel shaders, Load only: SPIR-V for
 * Vulkan, GLSL for OpenGL, DXIL for Direct3D 12, the HLSL compiled at run
 * time for Direct3D 11); a backend whose shaders or pipelines cannot be
 * made keeps the PS2's size for the run, logged.
 *
 * Interactions: the volumes' box and the empty composites (rd_shadow_box.c)
 * are the named levels' and are kept (a fine level's reach is the named
 * level's plus one level texel of the step (a fine texel between two named
 * ones reads both their footprints), one of the tent and 1 / S of one of the
 * composite's tap: inside the composites' RD_SB_TEX_MARGIN of 3, where the
 * named level needs one); the one DATE set-up holds (every fine texel is a
 * mean of values the named levels hold, MODULATEd alike); the shadow tiles
 * (rd_shadow_tile.c) step aside for a composite that reads a fine level
 * (their mask is the named level's); [dev] shadow_bspline does nothing to a
 * fine level (rd_fx_plan.c); a window without volumes (rd_no_volume.c)
 * skips the levels, so no chain is made.
 *
 * The cuts (v0.11.7 round 5; ICO_RD_SHADOW_FINE_CUTS=0 the tests' switch to
 * the nine-pass form; each the same bytes, the CPU test proving the pure
 * parts and rd_shadow_fine_ab the pictures):
 *   - the box: the count is 0 outside the volumes' box (rd_shadow_box.c
 *     rd__shadow_box_count_area; the one draw clears outside it, the stencil
 *     is 0 there for the seven draws); a level's GS pixel x reads its source's
 *     pixels 2 x + whole .. 2 x + whole + 3 at most, so its box follows
 *     (rd_sr_step_box: the whole axis where a REPEAT tap can wrap the source's
 *     far side in, the edge where a held tap reads it), and the tent spreads
 *     it by c - L + 1 .. c + L - 1 (rd_sr_tent_box).  Each pass clears its
 *     target (CLEAR: free on a tiler) and draws its box alone, so outside the
 *     box every texel is the 0 it would hold anyway, every frame, whatever
 *     the box did the frame before;
 *   - the step's empty areas: shadowres_step_ps skips a first-level texel
 *     whose count texels all lie in GS pixels the reduced count (rd_replay.c,
 *     rd__shadow_red_of) holds at alpha 0 (a nonzero count texel's alpha
 *     0x80 overlaps one of its GS pixels by a quarter texel or more, so its
 *     GS pixel reads at least 0x20 / (S.x S.y), nonzero below a scale product
 *     of 64); not where a tap leaves the count's texels (rd_sr_mask_empty);
 *   - the tent in one pass (shadowres_tent2_ps): the two passes' bytes in
 *     integers (an odd L makes every weight whole and S / L^2 never a half),
 *     so 6 passes a shadow instead of 9 (7 and 10 with the one draw's
 *     full-size count), the temporaries the final levels;
 *   - an empty count: no step or tent; each final level cleared once and kept
 *     while it holds 0.
 *
 * The count from the stencil (v0.11.8, issue 123: the full-size count pass
 * cost up to 10.69 ms of the tablet's 3x picture; rd_shadow_count.h): the
 * one draw's full-size pass (rd_shadow_one.c fullPass) writes at count
 * texel (x, y) inside its box shadow_count_ps at a box of one texel, which
 * is table[m] * (1 * 1) / (1 * 1) for the stencil's bits 0-5 m there (+0.0
 * plus one product, divided by 1: the table texel's float, blit.hlsl), and
 * 0 outside the box (its clear); stored to RGBA8 that is the table texel's
 * bytes, and the step loads it as an RGBA8_UNORM texel of those bytes, as
 * it loads the table's.  So shadowres_step_stencil_ps (the step built with
 * SHADOWRES_FROM_STENCIL) loads, where the step loads the count texel, the
 * table texel of the stencil there (0 outside the pass's box: the texel box
 * in DrawCB.aux0, the view's channel in g_mode.w) and is otherwise the same
 * text: the same bytes, no pass.  Its group: t1 the stencil view, t2 the
 * table, t3 the empty-area mask (a layout of its own, layoutSt).  Where it
 * stands (rd_shadow_one.c's guard: the depth and the count of one size,
 * nothing touched either since the count, else the pass after all) the
 * first level's step takes it (rd__shadow_count_take); the later levels
 * read the level before as ever.  Which form a device draws (the step from
 * the stencil, the full-size pass, the seven draws) its own timing decides
 * (rd_shadow_count.c).
 *
 * Tests only (docs/port/TESTING.md): ICO_RD_SHADOW_FINE=0 keeps the PS2's
 * sizes above 1x (v0.11.6's picture: the A/B's other half);
 * ICO_RD_SHADOW_FINE=force draws the fine chain at 1x too (S = 1 on both
 * axes: the GS's own step and an identity tent).
 *
 * The lines (rd__log):
 *   shadow levels: at the scene's scale above 1x (the smallest odd whole
 *     one at or above it on each axis: the count at its full size, the
 *     game's step at every texel, the tent its magnification supplied); at
 *     1x the PS2's sizes
 *   shadow levels: at the scene's scale: SHADOW0 864x768, SHADOW1 432x384,
 *     SHADOW2 216x192 (3x3 texels a GS pixel) from the count's 3x3 texels a
 *     GS pixel, 10 passes a shadow, 7.1 MB, on vulkan (reported at each
 *     change)
 *   shadow levels: the PS2's sizes: the scene at 1x1 texels a GS pixel
 *     (reported at each change)
 *   shadow levels: a shadow above 1x drawn at the PS2's size: <why>
 *     (reported once a reason)
 *   the 10 s clause "; shadow levels: 1.0 shadows a replay at the scene's
 *     scale (10.0 passes), 0.0 at the PS2's size" and --stats' "shadow
 *     levels: 1 shadows at the scene's scale, 10 passes, 0 at the PS2's
 *     size". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_clause.h"
#include "rd_internal.h"
#include "rd_replay.h"
#include "attempt_ledger.h"
#include "rd_shadow_box.h"   /* rd__shadow_box_count_area */
#include "rd_shadow_count.h" /* the full-size count's form above 1x */
#include "rd_shadowres.h"

/* ------------------------------------------------------------ the replay */

static struct {
    struct Replay *r; /* the screen command's replay (rd__no_volume_skip) */
    uint32_t chain;   /* the counts begun (rd__shadow_fine_reset) */
    uint32_t fullFor; /* the count target holding its full size this chain */
    uint32_t fullChain;
    /* the count target whose full size stands in the stencil this chain
     * (rd__shadow_fine_count_stencil; 0 none), and where */
    uint32_t stencilFor, stencilChain;
    RdSrStencil stencil;
    uint32_t made[RD_SR_LEVELS]; /* chain + 1 of the fine level drawn, 0 none */
    bool ready;                  /* the three drawn and blurred, this chain */
    uint32_t readyChain, readyReplay;
    /* the fine levels and their temporaries (across), with their states */
    RhiTexture lvl[RD_SR_LEVELS], tmp[RD_SR_LEVELS];
    RhiState lvlState[RD_SR_LEVELS], tmpState[RD_SR_LEVELS];
    uint32_t w[RD_SR_LEVELS], h[RD_SR_LEVELS];
    /* this chain's scales: the count's (cx, cy, its left-out cox) and each
     * level's texels per GS pixel */
    float cx, cy, cox, sx[RD_SR_LEVELS], sy[RD_SR_LEVELS];
    uint32_t chainPasses;
    RhiShader fsStep, fsTent, fsTent2;
    RhiPipeline pipeStep, pipeTent, pipeTent2;
    /* the first level's step from the stencil (shadowres_step_stencil_ps)
     * and its group's layout (t1 the stencil, s1, t2 the table, t3 the
     * mask); stRefused: they could not be made (the full-size pass then) */
    RhiShader fsStepSt;
    RhiPipeline pipeStepSt;
    RhiBindGroupLayout layoutSt;
    bool stRefused;
    /* the cuts (cutsOn): this chain's count box known (boxed) and empty, each
     * level's GS box (glo, ghi per axis) and its fine pre-tent and final
     * boxes; the final level is tmp[k] under the cuts (the fused tent writes
     * it), lvl[k] without */
    bool boxed, empty;
    int glo[RD_SR_LEVELS][2], ghi[RD_SR_LEVELS][2];
    RhiRect pre[RD_SR_LEVELS], fin[RD_SR_LEVELS];
    bool cuts;
    /* each texture's texels that may be nonzero (rd_shadowres.c "The render
     * area"): known false until a pass covered the whole texture */
    RdSrTrack trLvl[RD_SR_LEVELS], trTmp[RD_SR_LEVELS];
    bool refused;          /* the pipelines failed: the PS2's sizes for the run */
    uint32_t whyEvents;    /* the refusals so far (logged: 8, then every 100th) */
    uint32_t fineSince;    /* the shadows at the scene's scale since the last refusal */
    uint32_t saidW, saidH; /* the size line's last SHADOW0 */
    /* level 0's size whose textures could not be made: not tried again until
     * a chain at another size (review 1 m2) */
    uint32_t failW0, failH0;
    /* the tests' readback (rd__shadow_fine_probe): each step as it was drawn
     * (the reference's inputs), its source's texels, the count's target */
    RdSrStep step[RD_SR_LEVELS];
    uint32_t stepSw[RD_SR_LEVELS], stepSh[RD_SR_LEVELS];
    uint32_t stepCount;
    bool stepDone; /* the three steps and their tents drawn (the latest chain) */
} s_sr;

/* a shadow above 1x drawn at the PS2's size: counted by reason, a line the
 * first 8 times and every 100th after (detail: the field that failed) */
static void sayDetail(int why, const char *detail)
{
    g_rdPerf.shadowFineRefused++;
    if (why >= 0 && why < RD_SR_WHY_N && g_rdPerf.shadowFineWhy[why] < 0xFFFFu) {
        g_rdPerf.shadowFineWhy[why]++;
    }
    s_sr.whyEvents++;
    /* the first refusal after shadows at the scene's scale (a new scene) is
     * logged whatever the count */
    const uint32_t since = s_sr.fineSince;
    s_sr.fineSince = 0;
    if (s_sr.whyEvents > 8u && s_sr.whyEvents % 100u != 0u && since == 0u) {
        return;
    }
    rd__log("shadow levels: a shadow above 1x drawn at the PS2's size: %s%s%s (replay %u, command "
            "%u; refusal %u, after %u shadows at the scene's scale; logged: the first 8, the "
            "first after shadows at the scene's scale, then every 100th)",
            rd_sr_why_text(why), detail && detail[0] ? ": " : "", detail ? detail : "",
            (unsigned)g_rd.replayCounter, (unsigned)g_rdStoreOrd, (unsigned)s_sr.whyEvents,
            (unsigned)since);
}

static void say(int why)
{
    sayDetail(why, NULL);
}

/* ICO_RD_SHADOW_FINE (tests only): 0 the PS2's sizes above 1x, "force" the
 * fine chain at 1x too, anything else (or none) the rule */
enum { FINE_OFF = 0, FINE_RULE = 1, FINE_FORCE = 2 };

static int fineMode(void)
{
    static int s_mode = -1;
    if (s_mode < 0) {
        const char *e = getenv("ICO_RD_SHADOW_FINE");
        s_mode = e && strcmp(e, "0") == 0       ? FINE_OFF
                 : e && strcmp(e, "force") == 0 ? FINE_FORCE
                                                : FINE_RULE;
    }
    return s_mode;
}

/* ICO_RD_SHADOW_FINE_CUTS=0 (tests only): the chain as v0.11.7's first
 * form drew it (nine passes over every texel); else the cuts: each level's
 * passes over the box its count can reach, the step's empty areas skipped,
 * the tent in one pass, an empty count's levels cleared once (rd_shadowres.c
 * "The cuts"; every one the same bytes) */
static bool cutsOn(void)
{
    static int s_on = -1;
    if (s_on < 0) {
        const char *e = getenv("ICO_RD_SHADOW_FINE_CUTS");
        s_on = !(e && strcmp(e, "0") == 0);
    }
    return s_on == 1;
}

bool rd__shadow_fine_cuts(void)
{
    return cutsOn();
}

static const char *backendName(void)
{
    switch (rhi_backend()) {
    case RHI_BACKEND_VULKAN:
        return "vulkan";
    case RHI_BACKEND_D3D12:
        return "d3d12";
    case RHI_BACKEND_OPENGL:
        return "opengl";
    case RHI_BACKEND_D3D11:
        return "d3d11";
    default:
        return "another backend";
    }
}

void rd__shadow_fine_log(void)
{
    const int m = fineMode();
    if (m == FINE_OFF) {
        rd__log("shadow levels: the PS2's sizes at every scale (ICO_RD_SHADOW_FINE=0, the "
                "tests' switch)");
        return;
    }
    rd__log("shadow levels: at the scene's scale above 1x (the smallest odd whole one at or "
            "above it on each axis: the count at its full size, the game's step at every texel, "
            "the tent its magnification supplied) up to %u texels a GS pixel, above it and at 1x "
            "the PS2's sizes%s",
            RD_SR_LEVEL_CAP,
            m == FINE_FORCE ? "; at 1x too (ICO_RD_SHADOW_FINE=force, the tests' switch)" : "");
    rd__shadow_count_log(); /* the full-size count's form (rd_shadow_count.c) */
}

static void destroyAll(void)
{
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        if (s_sr.lvl[k].id) {
            rhi_destroy_texture(s_sr.lvl[k]);
        }
        if (s_sr.tmp[k].id) {
            rhi_destroy_texture(s_sr.tmp[k]);
        }
        s_sr.lvl[k] = s_sr.tmp[k] = (RhiTexture){0};
        s_sr.w[k] = s_sr.h[k] = 0;
        s_sr.made[k] = 0;
        s_sr.trLvl[k].known = s_sr.trTmp[k].known = false;
    }
    s_sr.ready = false;
}

static double heldMb(void)
{
    double b = 0.0;
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        b += 8.0 * (double)s_sr.w[k] * (double)s_sr.h[k]; /* the level and its temporary */
    }
    return b / (1024.0 * 1024.0);
}

void rd__shadow_fine_shutdown(void)
{
    if (g_rd.hasDevice) {
        destroyAll();
        if (s_sr.pipeStep.id) {
            rhi_destroy_pipeline(s_sr.pipeStep);
        }
        if (s_sr.pipeTent.id) {
            rhi_destroy_pipeline(s_sr.pipeTent);
        }
        if (s_sr.pipeTent2.id) {
            rhi_destroy_pipeline(s_sr.pipeTent2);
        }
        if (s_sr.fsTent2.id) {
            rhi_destroy_shader(s_sr.fsTent2);
        }
        if (s_sr.fsStep.id) {
            rhi_destroy_shader(s_sr.fsStep);
        }
        if (s_sr.fsTent.id) {
            rhi_destroy_shader(s_sr.fsTent);
        }
        if (s_sr.pipeStepSt.id) {
            rhi_destroy_pipeline(s_sr.pipeStepSt);
        }
        if (s_sr.fsStepSt.id) {
            rhi_destroy_shader(s_sr.fsStepSt);
        }
        if (s_sr.layoutSt.id) {
            rhi_destroy_bind_group_layout(s_sr.layoutSt);
        }
    }
    memset(&s_sr, 0, sizeof(s_sr));
}

void rd__shadow_fine_replay(struct Replay *r)
{
    s_sr.r = r;
}

void rd__shadow_fine_reset(uint32_t color)
{
    (void)color;
    s_sr.chain++;
    s_sr.fullFor = 0;
    s_sr.stencilFor = 0;
    s_sr.ready = false;
}

/* the count target's scales on both axes; false when neither is above 1
 * (1x: no path of its own) unless the tests force it */
static bool countScales(const RdTargetRec *tc, float *cx, float *cy)
{
    *cx = rd_sr_count_scale(tc->w, tc->tw, tc->sx, tc->ox);
    *cy = rd_sr_count_scale(tc->h, tc->th, tc->sy, 0.0f);
    return *cx > 1.0f || *cy > 1.0f || fineMode() == FINE_FORCE;
}

bool rd__shadow_fine_count_wanted(const RdTargetRec *tc)
{
    float cx, cy;
    return fineMode() != FINE_OFF && g_rd.hasDevice && !s_sr.refused && tc &&
           countScales(tc, &cx, &cy) && rd_sr_within_cap(cx, cy);
}

void rd__shadow_fine_count_full(uint32_t color)
{
    if (fineMode() == FINE_OFF) {
        return;
    }
    s_sr.fullFor = color;
    s_sr.fullChain = s_sr.chain;
    s_sr.stencilFor = 0; /* drawn: the step reads the count target */
}

void rd__shadow_fine_count_stencil(uint32_t color, const RdSrStencil *src)
{
    if (fineMode() == FINE_OFF) {
        return;
    }
    s_sr.fullFor = color;
    s_sr.fullChain = s_sr.chain;
    s_sr.stencilFor = color;
    s_sr.stencilChain = s_sr.chain;
    s_sr.stencil = *src;
}

/* blit_vs and one of the entries, RGBA8, no blend, every state static;
 * group 2's layout tex (g_rd.layoutTex, or the stencil step's) */
static RhiPipeline makePipeIn(RhiShader fs, const char *name, RhiBindGroupLayout tex)
{
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, tex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = g_rd.vs[RD_VS_BLIT];
    d.fragment = fs;
    d.layouts = layouts;
    d.layoutCount = 3;
    d.cullNone = true;
    d.colorCount = 1;
    d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    d.blend[0].srcColor = RHI_BF_ONE;
    d.blend[0].dstColor = RHI_BF_ZERO;
    d.blend[0].colorOp = RHI_BO_ADD;
    d.blend[0].srcAlpha = RHI_BF_ONE;
    d.blend[0].dstAlpha = RHI_BF_ZERO;
    d.blend[0].alphaOp = RHI_BO_ADD;
    d.blend[0].writeMask = 0xF;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.debugName = name;
    rd__pipeline_note_begin(d.debugName);
    const RhiPipeline p = rhi_create_pipeline(&d);
    rd__pipeline_note_end();
    if (p.id) {
        rd__stats_here()->pipelineCreates++;
    }
    return p;
}

static RhiPipeline makePipe(RhiShader fs, const char *name)
{
    return makePipeIn(fs, name, g_rd.layoutTex);
}

/* the first level's step from the stencil: its layout, shader and
 * pipeline, made at the first step that asks; false (logged once, the
 * full-size pass from then on) when one cannot be made */
static bool pipesStencil(void)
{
    if (s_sr.pipeStepSt.id) {
        return true;
    }
    if (s_sr.stRefused || !g_rd.vs[RD_VS_BLIT].id) {
        return false;
    }

    enum { FS = 1u << RHI_STAGE_FRAGMENT };

    static const RhiBindSlot k_slots[4] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                           {1, RHI_BIND_SAMPLER, FS},
                                           {2, RHI_BIND_SAMPLED_TEXTURE, FS},
                                           {3, RHI_BIND_SAMPLED_TEXTURE, FS}};
    if (!s_sr.layoutSt.id) {
        s_sr.layoutSt = rhi_create_bind_group_layout(
            &(RhiBindGroupLayoutDesc){k_slots, 4, "rd shadow levels step from the stencil"});
    }
    if (s_sr.layoutSt.id && !s_sr.fsStepSt.id) {
        s_sr.fsStepSt = rd__make_shader("shadowres_step_stencil_ps");
    }
    if (s_sr.fsStepSt.id) {
        s_sr.pipeStepSt =
            makePipeIn(s_sr.fsStepSt, "rd shadow levels step from the stencil", s_sr.layoutSt);
    }
    if (!s_sr.pipeStepSt.id) {
        rd__log("shadow levels: the step from the stencil could not be made on %s (layout %s, "
                "shader %s): the full-size count pass for this run",
                backendName(), s_sr.layoutSt.id ? "made" : "missing",
                s_sr.fsStepSt.id ? "made" : "missing");
        s_sr.stRefused = true;
        return false;
    }
    return true;
}

static bool pipes(void)
{
    if (s_sr.pipeStep.id && s_sr.pipeTent.id && (s_sr.pipeTent2.id || !cutsOn())) {
        return true;
    }
    if (s_sr.refused || !g_rd.vs[RD_VS_BLIT].id) {
        return false;
    }
    if (!s_sr.fsStep.id) {
        s_sr.fsStep = rd__make_shader("shadowres_step_ps");
    }
    if (!s_sr.fsTent.id) {
        s_sr.fsTent = rd__make_shader("shadowres_tent_ps");
    }
    if (s_sr.fsStep.id && !s_sr.pipeStep.id) {
        s_sr.pipeStep = makePipe(s_sr.fsStep, "rd shadow levels step");
    }
    if (s_sr.fsTent.id && !s_sr.pipeTent.id) {
        s_sr.pipeTent = makePipe(s_sr.fsTent, "rd shadow levels tent");
    }
    if (cutsOn() && !s_sr.fsTent2.id) {
        s_sr.fsTent2 = rd__make_shader("shadowres_tent2_ps");
    }
    if (s_sr.fsTent2.id && !s_sr.pipeTent2.id) {
        s_sr.pipeTent2 = makePipe(s_sr.fsTent2, "rd shadow levels tent in one pass");
    }
    if (!s_sr.pipeStep.id || !s_sr.pipeTent.id || (cutsOn() && !s_sr.pipeTent2.id)) {
        rd__log("shadow levels: the shaders or pipelines of the levels at the scene's scale could "
                "not be made on %s (step %s, tent %s): the PS2's sizes for this run",
                backendName(), s_sr.pipeStep.id ? "made" : "missing",
                s_sr.pipeTent.id ? "made" : "missing");
        s_sr.refused = true;
        return false;
    }
    return true;
}

/* level k's texture and its temporary at w x h */
static bool texturesFor(int k, uint32_t w, uint32_t h)
{
    if (s_sr.lvl[k].id && s_sr.tmp[k].id && s_sr.w[k] == w && s_sr.h[k] == h) {
        return true;
    }
    static const char *const kNames[RD_SR_LEVELS][2] = {
        {"rd shadow fine level 0", "rd shadow fine level 0 across"},
        {"rd shadow fine level 1", "rd shadow fine level 1 across"},
        {"rd shadow fine level 2", "rd shadow fine level 2 across"}};
    if (s_sr.lvl[k].id) {
        rhi_destroy_texture(s_sr.lvl[k]);
    }
    if (s_sr.tmp[k].id) {
        rhi_destroy_texture(s_sr.tmp[k]);
    }
    const uint32_t use = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC;
    s_sr.lvl[k] =
        rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM, use, kNames[k][0], 0});
    s_sr.tmp[k] =
        rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM, use, kNames[k][1], 0});
    s_sr.lvlState[k] = s_sr.tmpState[k] = RHI_STATE_UNDEFINED;
    s_sr.w[k] = w;
    s_sr.h[k] = h;
    s_sr.made[k] = 0;
    s_sr.trLvl[k].known = s_sr.trTmp[k].known = false;
    return s_sr.lvl[k].id && s_sr.tmp[k].id;
}

/* one pass of one triangle over dst (w x h) reading src (and mask, the
 * step's empty-area mask, when it has an id) through pipe.  Without box:
 * every texel drawn (DONT_CARE).  With box: the texels outside it are 0
 * after the pass (nothing outside it can be other than 0: "The cuts"), and
 * the pass touches only what it must: its render area is the union of box
 * and the texels the texture may still hold nonzero from before (tr), a
 * CLEAR load clearing that area, so a tile-based GPU loads, shades and
 * stores those tiles alone ("The render area"); a union of no texels (the
 * texture is 0 and the box empty) needs no pass.  A box of no texels: the
 * clear alone (no pipe needed) */
static void drawPassIn(struct Replay *r, RhiPipeline pipe, RhiTexture dst, RhiState *dstState,
                       RhiTexture src, RhiState *srcState, uint32_t w, uint32_t h, uint32_t name,
                       const IcoDrawCB *cb, const RhiRect *box, RhiTexture mask, RdSrTrack *tr,
                       RhiBindGroup group)
{
    const RhiRect all = {0, 0, w, h};
    RhiRect area;
    if (!rd_sr_area(&tr->last, &tr->known, s_sr.cuts ? box : NULL, w, h, &area)) {
        return; /* 0 already, nothing to draw */
    }
    RhiCommandList cl = rd__replay_cl();
    if (src.id) {
        rd__transition(cl, src, srcState, RHI_STATE_SHADER_READ);
    }
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, name, w, h, r->perfPost);
    rhi_cmd_checkpoint(cl,
                       rd__replay_pass_push(RD_PASSK_PASS, name, w, h, 0, 0, 0, 0, r->perfPost));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    /* every texel is written (DONT_CARE), or the area outside the box is 0
     * (CLEAR: clear[] zeroed) */
    p.color[0].load = box ? RHI_LOAD_CLEAR : RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    if (area.w != w || area.h != h) {
        p.area = area; /* the backends without tiles take the whole: the same bytes */
    }
    rhi_cmd_begin_render_pass(cl, &p);
    const bool draw = !box || (box->w && box->h);
    if (draw) {
        rd__replay_viewport(w, h);
        rhi_cmd_set_scissor(cl, box ? box : &all);
        rd__set_pipeline(cl, pipe, NULL); /* makePipe: every state static */
        rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(cb));
        const RhiSampler smp =
            rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rhi_cmd_set_bind_group(cl, 2,
                               group.id  ? group
                               : mask.id ? rd__tex_group_date(src, smp, mask)
                                         : rd__tex_group(src, smp));
        rd__replay_draw(cl, 3, 0, 1);
        rd__stats_here()->draws++;
        g_rdPerf.shadowFineTexels += box ? box->w * box->h : w * h;
    }
    g_rdPerf.shadowFineArea += w * h;
    g_rdPerf.shadowFineRendered += area.w * area.h;
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst, dstState, RHI_STATE_SHADER_READ);
    g_rdPerf.shadowFinePasses++;
    s_sr.chainPasses++;
}

static void drawPass(struct Replay *r, RhiPipeline pipe, RhiTexture dst, RhiState *dstState,
                     RhiTexture src, RhiState *srcState, uint32_t w, uint32_t h, uint32_t name,
                     const IcoDrawCB *cb, const RhiRect *box, RhiTexture mask, RdSrTrack *tr)
{
    drawPassIn(r, pipe, dst, dstState, src, srcState, w, h, name, cb, box, mask, tr,
               (RhiBindGroup){0});
}

/* the level index of a named level target, -1 for any other */
static int levelOf(uint32_t target)
{
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        if (target == rd_target((RdTargetId)(RD_TARGET_SHADOW0 + k)).id) {
            return k;
        }
    }
    return -1;
}

/* the sprite is the chain's (rd_sr_recognise, rd_shadowres_math.c): the
 * draw's state and vertices handed over as plain values */
static bool chainSprite(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v, uint32_t n,
                        const RdTargetRec *dst, const RdTargetRec *src, int *why, char *detail,
                        size_t dsize, int *ox, int *oy, float *fx, float *fy)
{
    const RdDrawState *d = &st->ds;
    RdSrSprite sp;
    memset(&sp, 0, sizeof(sp));
    sp.prim = c->b[0];
    sp.space = c->b[1];
    sp.uvFixed = c->b[2];
    sp.frameCam = c->b[4];
    sp.stShift = c->b[5];
    sp.aa1 = (uint8_t)(st->aa1 != 0);
    sp.n = n;
    sp.tcc = (uint8_t)d->tcc;
    sp.texFn = (uint8_t)d->texFn;
    sp.abe = (uint8_t)(d->abe != 0);
    sp.fba = (uint8_t)(d->fba != 0);
    sp.colorMask = (uint8_t)d->colorMask;
    sp.ate = (uint8_t)(d->test.ate != 0);
    sp.atst = (uint8_t)d->test.atst;
    sp.date = (uint8_t)d->test.date;
    sp.zte = (uint8_t)(d->test.zte != 0);
    sp.ztst = (uint8_t)d->test.ztst;
    sp.magF = (uint8_t)d->magFilter;
    sp.minF = (uint8_t)d->minFilter;
    for (uint32_t i = 0; i < 2 && i < n; i++) {
        memcpy(sp.rgba[i], v[i].rgba, 4);
        sp.x16[i] = (int32_t)v[i].x;
        sp.y16[i] = (int32_t)v[i].y;
        sp.s16[i] = v[i].s;
        sp.t16[i] = v[i].t;
    }
    sp.uOff = st->uvOffset[0] * (float)src->w;
    sp.vOff = st->uvOffset[1] * (float)src->h;
    sp.gsW = st->gsW;
    sp.gsH = st->gsH;
    sp.dstW = dst->w;
    sp.dstH = dst->h;
    sp.srcW = src->w;
    sp.srcH = src->h;
    *why = rd_sr_recognise(&sp, ox, oy, fx, fy, detail, dsize);
    return *why < 0;
}

/* the chain in force, at each change: the fine levels' sizes (saidW, saidH)
 * or the PS2's at a scale of 1 (saidW 1, saidH 0) */
static void sizeLine(void)
{
    if (s_sr.saidW == s_sr.w[0] && s_sr.saidH == s_sr.h[0]) {
        return;
    }
    s_sr.saidW = s_sr.w[0];
    s_sr.saidH = s_sr.h[0];
    const uint32_t ms = g_rdPerf.msaaSamples;
    rd__log("shadow levels: at the scene's scale: SHADOW0 %ux%u, SHADOW1 %ux%u, SHADOW2 %ux%u "
            "(%.4gx%.4g texels a GS pixel) from the count's %.4gx%.4g texels a GS pixel, %u "
            "passes a shadow, %.1f MB, on %s%s (reported at each change)",
            s_sr.w[0], s_sr.h[0], s_sr.w[1], s_sr.h[1], s_sr.w[2], s_sr.h[2], (double)s_sr.sx[0],
            (double)s_sr.sy[0], (double)s_sr.cx, (double)s_sr.cy, s_sr.chainPasses, heldMb(),
            backendName(),
            ms > 1u ? ", the count from the MSAA epoch's covered-majority resolve" : "");
}

static void ps2Line(float cx, float cy)
{
    if (s_sr.saidW == 1u && s_sr.saidH == 0u) {
        return;
    }
    s_sr.saidW = 1u;
    s_sr.saidH = 0u;
    rd__log("shadow levels: the PS2's sizes: the scene at %.4gx%.4g texels a GS pixel (reported "
            "at each change)",
            (double)cx, (double)cy);
}

/* the cap in force, at each change (saidW 2, saidH 0) */
static void capLine(float cx, float cy)
{
    if (s_sr.saidW == 2u && s_sr.saidH == 0u) {
        return;
    }
    s_sr.saidW = 2u;
    s_sr.saidH = 0u;
    rd__log("shadow levels: the PS2's sizes: the scene at %.4gx%.4g texels a GS pixel, above the "
            "cap of %u (the largest scale the levels were measured at; reported at each change)",
            (double)cx, (double)cy, RD_SR_LEVEL_CAP);
}

/* the tent across (level -> temporary) and down (temporary -> level) */
static void tent(struct Replay *r, int k, uint32_t name)
{
    for (int down = 0; down < 2; down++) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.mode[0] = down ? 1u : 0u;
        cb.param[0] = down ? s_sr.sy[k] : s_sr.sx[k];
        cb.tex[0] = (float)s_sr.w[k];
        cb.tex[1] = (float)s_sr.h[k];
        cb.tex[2] = 1.0f / (float)s_sr.w[k];
        cb.tex[3] = 1.0f / (float)s_sr.h[k];
        if (down) {
            drawPass(r, s_sr.pipeTent, s_sr.lvl[k], &s_sr.lvlState[k], s_sr.tmp[k],
                     &s_sr.tmpState[k], s_sr.w[k], s_sr.h[k], name, &cb, NULL, (RhiTexture){0},
                     &s_sr.trLvl[k]);
        } else {
            drawPass(r, s_sr.pipeTent, s_sr.tmp[k], &s_sr.tmpState[k], s_sr.lvl[k],
                     &s_sr.lvlState[k], s_sr.w[k], s_sr.h[k], name, &cb, NULL, (RhiTexture){0},
                     &s_sr.trTmp[k]);
        }
    }
}

/* the cuts' tent: one pass level -> temporary over the final box
 * (shadowres_tent2_ps, the two passes' bytes) */
static void tent2(struct Replay *r, int k, uint32_t name)
{
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.param[0] = s_sr.sx[k];
    cb.param[1] = s_sr.sy[k];
    cb.tex[0] = (float)s_sr.w[k];
    cb.tex[1] = (float)s_sr.h[k];
    cb.tex[2] = 1.0f / (float)s_sr.w[k];
    cb.tex[3] = 1.0f / (float)s_sr.h[k];
    drawPass(r, s_sr.pipeTent2, s_sr.tmp[k], &s_sr.tmpState[k], s_sr.lvl[k], &s_sr.lvlState[k],
             s_sr.w[k], s_sr.h[k], name, &cb, s_sr.boxed ? &s_sr.fin[k] : NULL, (RhiTexture){0},
             &s_sr.trTmp[k]);
}

/* level k's boxes from its source's GS box (the count's, or level k - 1's):
 * the GS box, the fine pre-tent box (the step's scissor) and the final box
 * (the tent's); false when the source holds nothing (the level is 0) */
static void levelBoxes(int k, const RdTargetRec *dst, int srcLo[2], int srcHi[2],
                       const int srcSize[2], int ofx, int ofy, int repS, int repT)
{
    const int Lx = (int)rd_sr_level_scale(s_sr.cx), Ly = (int)rd_sr_level_scale(s_sr.cy);
    int lo[2] = {0, 0}, hi[2] = {-1, -1};
    const bool any =
        rd_sr_step_box(srcLo[0], srcHi[0], srcSize[0], ofx, (int)dst->w, repS, &lo[0], &hi[0]) &&
        rd_sr_step_box(srcLo[1], srcHi[1], srcSize[1], ofy, (int)dst->h, repT, &lo[1], &hi[1]);
    if (!any) {
        lo[0] = lo[1] = 0;
        hi[0] = hi[1] = -1;
        s_sr.pre[k] = s_sr.fin[k] = (RhiRect){0, 0, 0, 0};
    } else {
        s_sr.pre[k] = (RhiRect){lo[0] * Lx, lo[1] * Ly, (uint32_t)((hi[0] - lo[0] + 1) * Lx),
                                (uint32_t)((hi[1] - lo[1] + 1) * Ly)};
        int fx0, fx1, fy0, fy1;
        rd_sr_tent_box(lo[0] * Lx, (hi[0] + 1) * Lx - 1, Lx, (int)s_sr.w[k], &fx0, &fx1);
        rd_sr_tent_box(lo[1] * Ly, (hi[1] + 1) * Ly - 1, Ly, (int)s_sr.h[k], &fy0, &fy1);
        s_sr.fin[k] = (RhiRect){fx0, fy0, (uint32_t)(fx1 - fx0 + 1), (uint32_t)(fy1 - fy0 + 1)};
    }
    for (int a = 0; a < 2; a++) {
        s_sr.glo[k][a] = lo[a];
        s_sr.ghi[k][a] = hi[a];
    }
}

/* shadowres_step_stencil_ps's group and DrawCB fields: t1 the stencil
 * view, t2 the table, t3 the mask (or the dummy); the full-size pass's
 * texel box in aux0 (ICO_RD_SHADOW_COUNT_SABOTAGE: empty, every texel 0)
 * and the stencil's channel in mode.w */
static RhiBindGroup stencilGroup(const RdSrStencil *sc, RhiTexture mask, IcoDrawCB *cb)
{
    cb->mode[3] = sc->channel;
    if (!rd__shadow_count_sabotaged()) {
        cb->aux0[0] = (uint32_t)sc->box.x;
        cb->aux0[1] = (uint32_t)sc->box.y;
        cb->aux0[2] = (uint32_t)sc->box.x + sc->box.w;
        cb->aux0[3] = (uint32_t)sc->box.y + sc->box.h;
    }
    RhiBinding b[4];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = sc->depth;
    b[0].aspect = RHI_ASPECT_STENCIL; /* in RHI_STATE_DEPTH_READ since the count */
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = sc->table;
    b[3].slot = 3;
    b[3].type = RHI_BIND_SAMPLED_TEXTURE;
    b[3].texture = mask.id ? mask : g_rd.dummy;
    return rd__replay_bind_group(s_sr.layoutSt, b, 4);
}

static RhiTexture scratch(uint32_t w, uint32_t h, const char *name)
{
    return rhi_create_texture(
        &(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM,
                          RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC, name, 0});
}

/* The check (rd_shadow_count.h), on the check replay right after its first
 * level was drawn from the full-size pass's count into lvl[0] (cb, the
 * step's DrawCB): the same level from the stencil (shadowres_step_stencil_ps)
 * and from the seven draws' full-size count (cw x ch) into scratch
 * textures of its own, over the same box with the same mask, and the three
 * copied into one readback buffer (rows pitch bytes apart: lvl[0], the
 * stencil's, the seven draws'), compared at the slot's recycle.  Its passes
 * are not the chain's (chainPasses kept); the picture is lvl[0]'s */
static void checkDraw(struct Replay *r, uint32_t name, uint32_t count, uint32_t w, uint32_t h,
                      uint32_t cw, uint32_t ch, const IcoDrawCB *cb, RhiTexture mask)
{
    RdSrStencil sc;
    RdTargetRec *td = NULL;
    uint32_t step = 4;
    if (!rd__shadow_count_check_take(count, &sc, &td, &step)) {
        return;
    }
    if (!pipesStencil()) {
        rd__shadow_count_check_refuse("the stencil step's shader or pipeline could not be made");
        return;
    }
    const RhiLimits *lim = rhi_limits();
    const uint32_t pa = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1u;
    const uint32_t pitch = (w * 4u + pa - 1u) / pa * pa;
    RhiTexture tex[RD_SC_CHECK_TEXTURES] = {scratch(w, h, "rd shadow count check: stencil step"),
                                            scratch(cw, ch, "rd shadow count check: seven draws"),
                                            scratch(w, h, "rd shadow count check: seven's step")};
    RhiState ts[RD_SC_CHECK_TEXTURES] = {RHI_STATE_UNDEFINED, RHI_STATE_UNDEFINED,
                                         RHI_STATE_UNDEFINED};
    const RhiBuffer rb =
        rhi_create_buffer(&(RhiBufferDesc){3ull * pitch * h, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                           RHI_MEM_READBACK, "rd shadow count check readback"});
    bool ok = rb.id != 0;
    for (int i = 0; i < RD_SC_CHECK_TEXTURES; i++) {
        ok = ok && tex[i].id != 0;
    }
    const uint32_t passes = s_sr.chainPasses;
    const RhiRect *box = s_sr.cuts && s_sr.boxed ? &s_sr.pre[0] : NULL;
    if (ok) {
        RdSrTrack t0 = {{0, 0, 0, 0}, false}, t2 = {{0, 0, 0, 0}, false};
        IcoDrawCB cbs = *cb;
        const RhiBindGroup g = stencilGroup(&sc, mask, &cbs);
        drawPassIn(r, s_sr.pipeStepSt, tex[0], &ts[0], (RhiTexture){0}, NULL, w, h, name, &cbs, box,
                   mask, &t0, g);
        ok = rd__shadow_count_seven_into(tex[1], &ts[1], td, cw, ch, step);
        if (ok) {
            drawPass(r, s_sr.pipeStep, tex[2], &ts[2], tex[1], &ts[1], w, h, name, cb, box, mask,
                     &t2);
        }
    }
    s_sr.chainPasses = passes;
    if (!ok) {
        for (int i = 0; i < RD_SC_CHECK_TEXTURES; i++) {
            if (tex[i].id) {
                rhi_destroy_texture(tex[i]);
            }
        }
        if (rb.id) {
            rhi_destroy_buffer(rb);
        }
        rd__shadow_count_check_refuse("its textures, readback or seven draws could not be made");
        return;
    }
    RhiCommandList cl = rd__replay_cl();
    const RhiRect all = {0, 0, w, h};
    rd__transition(cl, s_sr.lvl[0], &s_sr.lvlState[0], RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, s_sr.lvl[0], RHI_ASPECT_COLOR, all, rb, 0, pitch);
    rd__transition(cl, tex[0], &ts[0], RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, tex[0], RHI_ASPECT_COLOR, all, rb, (uint64_t)pitch * h,
                                   pitch);
    rd__transition(cl, tex[2], &ts[2], RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, tex[2], RHI_ASPECT_COLOR, all, rb, 2ull * pitch * h, pitch);
    rd__transition(cl, s_sr.lvl[0], &s_sr.lvlState[0], RHI_STATE_SHADER_READ);
    rd__shadow_count_check_recorded(rb, tex, w, h, pitch);
}

void rd__shadow_fine_screen(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v,
                            uint32_t n)
{
    struct Replay *r = s_sr.r;
    if (fineMode() == FINE_OFF || !g_rd.hasDevice || !r || &r->st != st) {
        return;
    }
    const int k = levelOf(st->color);
    if (k < 0) {
        return;
    }
    /* a draw into a level: the fine levels stand for the named ones no
     * more until this chain's step k is drawn again */
    s_sr.ready = false;
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view != RD_VIEW_RGBA ||
        t->src != RD_TEXSRC_RGBA32) {
        return; /* not a level sprite: the named level as recorded */
    }
    RdTargetRec *src = rd__target_rec(t->target);
    RdTargetRec *dst = rd__target_rec(st->color);
    if (!src || !dst) {
        return;
    }
    if (k == 0) {
        float cx, cy;
        if (!countScales(src, &cx, &cy)) {
            ps2Line(cx, cy);
            return; /* 1x: the game's own, no path of its own */
        }
        if (!rd_sr_within_cap(cx, cy)) {
            capLine(cx, cy);
            say(RD_SR_WHY_SCALE); /* above the cap: the PS2's sizes, v0.11.6's shadow */
            return;
        }
        s_sr.cx = cx;
        s_sr.cy = cy;
        s_sr.cox = src->ox > 0.0f ? src->ox : 0.0f;
    } else if (levelOf(t->target) != k - 1 || s_sr.made[k - 1] != s_sr.chain + 1u) {
        /* not the chain's next step, or the step before was not taken (1x,
         * or its refusal already counted): the named level as recorded */
        return;
    }
    if (k == 0 && (s_sr.fullFor == 0u || s_sr.fullChain != s_sr.chain)) {
        return; /* no count resolved under the rule this chain (a self-test's frame) */
    }
    if (k == 0 && s_sr.fullFor != t->target) {
        say(RD_SR_WHY_COUNT); /* a level 0 from another target than the count */
        return;
    }
    int why = RD_SR_WHY_MAP, ofx = 0, ofy = 0;
    float wfx = 0.25f, wfy = 0.25f;
    char detail[192];
    if (!chainSprite(st, c, v, n, dst, src, &why, detail, sizeof detail, &ofx, &ofy, &wfx, &wfy)) {
        sayDetail(why, detail);
        return;
    }
    if (!pipes()) {
        say(RD_SR_WHY_PIPES);
        return;
    }
    const uint32_t w = dst->w * rd_sr_level_scale(s_sr.cx), h = dst->h * rd_sr_level_scale(s_sr.cy);
    if (k == 0 && w == s_sr.failW0 && h == s_sr.failH0) {
        say(RD_SR_WHY_TEXTURES); /* failed at this size before */
        return;
    }
    rd__replay_end_pass(r); /* the open pass (the level before's) */
    if (!texturesFor(k, w, h)) {
        s_sr.failW0 = k == 0 ? w : s_sr.w[0];
        s_sr.failH0 = k == 0 ? h : s_sr.h[0];
        rd__log("shadow levels: the levels' textures could not be made (level %d at %ux%u): the "
                "PS2's sizes until the levels' size changes",
                k, w, h);
        say(RD_SR_WHY_TEXTURES);
        destroyAll();
        return;
    }
    if (k == 0) {
        s_sr.failW0 = s_sr.failH0 = 0; /* made at another size: a failure forgotten */
    }
    s_sr.sx[k] = (float)w / (float)dst->w;
    s_sr.sy[k] = (float)h / (float)dst->h;
    if (k == 0) {
        s_sr.chainPasses = 0;
        s_sr.cuts = cutsOn();
        s_sr.boxed = false;
        s_sr.empty = false;
    }
    const int repS = st->ds.wrap.s == RD_WRAP_REPEAT, repT = st->ds.wrap.t == RD_WRAP_REPEAT;
    RhiTexture mask = (RhiTexture){0};
    if (s_sr.cuts) {
        if (k == 0) {
            /* the count's box (rd_shadow_box.c: the volumes' extent, the count 0
             * outside it): none known, the whole level */
            RhiRect gb;
            s_sr.boxed = rd__shadow_box_count_area(src, &gb);
            int slo[2] = {gb.x, gb.y}, shi[2] = {gb.x + (int)gb.w - 1, gb.y + (int)gb.h - 1};
            const int ssz[2] = {(int)src->w, (int)src->h};
            if (s_sr.boxed) {
                s_sr.empty = gb.w == 0 || gb.h == 0;
                levelBoxes(0, dst, slo, shi, ssz, ofx, ofy, repS, repT);
            }
            /* the step's empty-area mask: the count box-reduced, where a
             * nonzero count texel's GS pixel is sure to read nonzero */
            if (s_sr.cx * s_sr.cy <= 64.0f) {
                mask = rd__shadow_red_of(t->target);
            }
        } else if (s_sr.boxed) {
            int slo[2] = {s_sr.glo[k - 1][0], s_sr.glo[k - 1][1]},
                shi[2] = {s_sr.ghi[k - 1][0], s_sr.ghi[k - 1][1]};
            const int ssz[2] = {(int)src->w, (int)src->h};
            levelBoxes(k, dst, slo, shi, ssz, ofx, ofy, repS, repT);
        }
    }
    /* the count from the stencil (rd_shadow_one.c; "The count from the
     * stencil"): the step reads it where the full-size pass was not drawn;
     * the pass drawn now where the step's pipeline cannot be made, and none
     * where the guard drew it already (the count target then) */
    bool fromStencil = false;
    if (k == 0 && s_sr.stencilFor == t->target && s_sr.stencilChain == s_sr.chain &&
        !(s_sr.cuts && s_sr.empty)) {
        if (!pipesStencil()) {
            rd__shadow_count_flush(t->target, RD_SC_WHY_PIPE);
        } else {
            const int took = rd__shadow_count_take(t->target);
            if (took < 0) {
                sayDetail(RD_SR_WHY_COUNT, "the count's stencil touched before the step could read "
                                           "it (rd_shadow_count.c says why)");
                return;
            }
            fromStencil = took > 0;
        }
    }
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    float ssx, ssy, ox;
    uint32_t sw, sh;
    RhiTexture stex;
    RhiState *sstate;
    if (k == 0) {
        ssx = s_sr.cx;
        ssy = s_sr.cy;
        ox = s_sr.cox;
        sw = src->tw;
        sh = src->th;
        stex = src->color;
        sstate = &src->colorState;
    } else {
        ssx = s_sr.sx[k - 1];
        ssy = s_sr.sy[k - 1];
        ox = 0.0f;
        sw = s_sr.w[k - 1];
        sh = s_sr.h[k - 1];
        stex = s_sr.lvl[k - 1];
        sstate = &s_sr.lvlState[k - 1];
    }
    cb.mode[0] = k == 0 ? 0u : 1u;
    cb.mode[1] =
        (st->ds.wrap.s == RD_WRAP_REPEAT ? 1u : 0u) | (st->ds.wrap.t == RD_WRAP_REPEAT ? 2u : 0u);
    cb.param[0] = 2.0f * ssx / s_sr.sx[k];
    cb.param[1] = 2.0f * ssy / s_sr.sy[k];
    cb.param[2] = ssx;
    cb.param[3] = ssy;
    cb.scale[0] = (float)ofx * ssx - ox;
    cb.scale[1] = (float)ofy * ssy;
    cb.uvRect[0] = wfx;
    cb.uvRect[1] = wfy;
    cb.tex[0] = (float)sw;
    cb.tex[1] = (float)sh;
    cb.tex[2] = 1.0f / (float)sw;
    cb.tex[3] = 1.0f / (float)sh;
    for (int ch = 0; ch < 4; ch++) {
        cb.col[ch] = v[1].rgba[ch];
    }
    s_sr.step[k] = (RdSrStep){.first = k == 0,
                              .kx = cb.param[0],
                              .ky = cb.param[1],
                              .ax = cb.scale[0],
                              .ay = cb.scale[1],
                              .sx = ssx,
                              .sy = ssy,
                              .fx = wfx,
                              .fy = wfy,
                              .repS = repS,
                              .repT = repT};
    for (int ch = 0; ch < 4; ch++) {
        s_sr.step[k].col[ch] = v[1].rgba[ch];
    }
    s_sr.stepSw[k] = sw;
    s_sr.stepSh[k] = sh;
    if (k == 0) {
        s_sr.stepCount = t->target;
        s_sr.stepDone = false;
    }
    if (mask.id) {
        cb.mode[2] = 1u;
        cb.scale[2] = ox;
        cb.uvRect[2] = (float)src->w;
        cb.uvRect[3] = (float)src->h;
    }
    if (fromStencil) {
        IcoDrawCB cbs = cb;
        const RhiBindGroup g = stencilGroup(&s_sr.stencil, mask, &cbs);
        drawPassIn(r, s_sr.pipeStepSt, s_sr.lvl[k], &s_sr.lvlState[k], (RhiTexture){0}, NULL, w, h,
                   st->color, &cbs, s_sr.cuts && s_sr.boxed ? &s_sr.pre[k] : NULL, mask,
                   &s_sr.trLvl[k], g);
        if (g_rdPerf.shadowCountStencil < 0xFFFFu) {
            g_rdPerf.shadowCountStencil++;
        }
        ico_attempt_fire(ICO_ATT_SHADOW_COUNT, 1);
        if (rd__shadow_count_keep()) {
            rd__shadow_count_materialise(); /* the tests' readback of the count */
        }
    } else if (!(s_sr.cuts && s_sr.empty)) {
        drawPass(r, s_sr.pipeStep, s_sr.lvl[k], &s_sr.lvlState[k], stex, sstate, w, h, st->color,
                 &cb, s_sr.cuts && s_sr.boxed ? &s_sr.pre[k] : NULL, mask, &s_sr.trLvl[k]);
        if (k == 0) {
            checkDraw(r, st->color, t->target, w, h, src->tw, src->th, &cb, mask);
        }
    }
    s_sr.made[k] = s_sr.chain + 1u;
    if (k + 1 < RD_SR_LEVELS) {
        return;
    }
    for (int j = 0; j < RD_SR_LEVELS; j++) {
        const uint32_t name = rd_target((RdTargetId)(RD_TARGET_SHADOW0 + j)).id;
        if (!s_sr.cuts) {
            tent(r, j, name);
        } else if (!s_sr.empty) {
            tent2(r, j, name);
        } else {
            /* an empty count: every level 0; the final texture's nonzero area
             * cleared, no pass once it holds 0 */
            drawPass(r, (RhiPipeline){0}, s_sr.tmp[j], &s_sr.tmpState[j], (RhiTexture){0}, NULL,
                     s_sr.w[j], s_sr.h[j], name, NULL, &(RhiRect){0, 0, 0, 0}, (RhiTexture){0},
                     &s_sr.trTmp[j]);
        }
    }
    s_sr.ready = true;
    s_sr.stepDone = true;
    s_sr.readyChain = s_sr.chain;
    s_sr.readyReplay = g_rd.replayCounter;
    g_rdPerf.shadowFineChains++;
    s_sr.fineSince++;
    sizeLine();
}

RhiTexture rd__shadow_fine(uint32_t target, uint32_t drawTarget)
{
    if (!s_sr.ready || s_sr.readyChain != s_sr.chain || s_sr.readyReplay != g_rd.replayCounter ||
        fineMode() == FINE_OFF || levelOf(drawTarget) >= 0) {
        return (RhiTexture){0};
    }
    const int k = levelOf(target);
    if (k < 0) {
        return (RhiTexture){0};
    }
    const RhiTexture fin = s_sr.cuts ? s_sr.tmp[k] : s_sr.lvl[k];
    const RhiState fs = s_sr.cuts ? s_sr.tmpState[k] : s_sr.lvlState[k];
    if (!fin.id || fs != RHI_STATE_SHADER_READ) {
        return (RhiTexture){0};
    }
    return fin;
}

bool rd__shadow_fine_tex(uint32_t texId)
{
    for (int k = 0; texId && k < RD_SR_LEVELS; k++) {
        if (s_sr.lvl[k].id == texId || s_sr.tmp[k].id == texId) {
            return true;
        }
    }
    return false;
}

bool rd__shadow_fine_level(uint32_t target)
{
    return levelOf(target) >= 0 && rd__shadow_fine(target, 0).id != 0;
}

/* The tests' readback (rd_replay_tool --fine-check, after a replay): the
 * chain's level k as it was drawn this replay. */
bool rd__shadow_fine_probe(int k, RdSrProbe *out)
{
    if (k < 0 || k >= RD_SR_LEVELS || !s_sr.stepDone || !s_sr.cuts || s_sr.empty ||
        !s_sr.lvl[k].id || !s_sr.tmp[k].id) {
        return false; /* no chain this replay, or not the cuts' form (the step kept in lvl) */
    }
    out->step = s_sr.step[k];
    out->sw = s_sr.stepSw[k];
    out->sh = s_sr.stepSh[k];
    out->w = s_sr.w[k];
    out->h = s_sr.h[k];
    out->lx = (int)(s_sr.sx[k] + 0.5f);
    out->ly = (int)(s_sr.sy[k] + 0.5f);
    out->count = s_sr.stepCount;
    return true;
}

bool rd__shadow_fine_read(int k, bool final, void *dst, size_t size)
{
    if (k < 0 || k >= RD_SR_LEVELS || !s_sr.lvl[k].id || !s_sr.tmp[k].id) {
        return false;
    }
    /* every level's two textures to COPY_SRC in one command list: a list a
     * readback would pass the device's command lists a frame (8 on Vulkan)
     * before the third level is read */
    bool move = false;
    for (int j = 0; j < RD_SR_LEVELS; j++) {
        move = move || (s_sr.lvl[j].id && s_sr.lvlState[j] != RHI_STATE_COPY_SRC) ||
               (s_sr.tmp[j].id && s_sr.tmpState[j] != RHI_STATE_COPY_SRC);
    }
    if (move) {
        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            return false;
        }
        for (int j = 0; j < RD_SR_LEVELS; j++) {
            if (s_sr.lvl[j].id) {
                rd__transition(cl, s_sr.lvl[j], &s_sr.lvlState[j], RHI_STATE_COPY_SRC);
            }
            if (s_sr.tmp[j].id) {
                rd__transition(cl, s_sr.tmp[j], &s_sr.tmpState[j], RHI_STATE_COPY_SRC);
            }
        }
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    /* under the cuts the step's output stays in lvl and the tent's is tmp */
    return rd__read_rhi_texture(final ? s_sr.tmp[k] : s_sr.lvl[k],
                                final ? &s_sr.tmpState[k] : &s_sr.lvlState[k], s_sr.w[k], s_sr.h[k],
                                dst, size);
}
