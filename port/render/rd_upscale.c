/* rd_upscale.c: the upscale point (rd_upscale.h; docs/port/RENDER.md "The
 * upscale point"): the switch, the scale split rd__apply_display applies,
 * the per-present jitter, the marker's replay (the edge guard, the
 * hook's inputs, UPSCALED) and the reduction's read of UPSCALED in SCENE's
 * place.  The hook itself is rd_upscale_hook.c. */
#include "rd_upscale.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_fsr3.h"
#include "rd_jitter.h"
#include "rd_replay.h"
#include "rd_thread.h"
#include "rd_upscale_temporal.h"
#include "rd_velocity.h"

/* the ratio [dev] upscale_test asks: FSR's Quality (1.5) */
#define RD_UPSCALE_TEST_RATIO 1.5f

static struct {
    bool asked;        /* set by a call, or the switch read */
    bool want;         /* rd__upscale_set's (the tools, the tests, [dev] upscale_test) */
    bool velocityOurs; /* the switch turned the motion vectors on */
    float ratio;
    /* the settings' (rd__upscale_follow: Upscaling = FSR 3.1 in force),
     * used while rd__upscale_set has not turned the switch on */
    bool fsr3;
    float fsr3Ratio;
    uint32_t phases;
    /* the split in force (rd__apply_display) */
    bool split;
    float dsx, dsy;
    /* the jitter: the next present's index, set by this file */
    uint32_t index;
    bool jitterOurs;
    uint32_t since; /* presents since the last dispatch */
    /* UPSCALED and what the replay made of it */
    RhiTexture out;
    RhiState outState;
    RdTargetRec geom;      /* its texel geometry (tw, th, sx, sy, ox) */
    bool storage;          /* made with RHI_TEX_STORAGE */
    uint32_t madeReplay;   /* g_rd.replayCounter of the replay that wrote it, 0 none */
    uint32_t pointReplay;  /* the replay whose marker dispatched (once a replay) */
    uint32_t lastW, lastH; /* the last dispatch's sizes (a new size resets) */
    /* the edge guard's strips (a column, a row) */
    RhiTexture col, row;
    RhiState colState, rowState;
    uint32_t colH, rowW;
    RdDepthCopy depth;
    RdUpscaleStats stats;
} s_up;

/* SCENE's record lent to UPSCALED for the reduction's sprite */
static RdTargetRec s_saved;
static bool s_swapped;

static float clampRatio(float ratio)
{
    return !(ratio >= 1.0f) ? 1.0f : (ratio > 3.0f ? 3.0f : ratio);
}

static void readSwitch(void)
{
    s_up.asked = true;
    const char *e = getenv("ICO_RD_UPSCALE_TEST");
    if (!e || strcmp(e, "1") != 0) {
        return;
    }
    rd__upscale_set(true, RD_UPSCALE_TEST_RATIO);
    rd__log("upscale point: [dev] upscale_test: the scene at the display scale over %.2f, "
            "upscaled at the reduction by %s (a developer test of the path)",
            (double)RD_UPSCALE_TEST_RATIO, rd__upscale_hook_name());
}

/* the motion vectors are the upscaler's input: on with it (either source),
 * and off again only when it was this file that turned them on */
static void syncVelocity(void)
{
    const bool on = s_up.want || s_up.fsr3;
    if (on && !rd__velocity_on()) {
        rd__velocity_set(true);
        s_up.velocityOurs = true;
    } else if (!on && s_up.velocityOurs) {
        rd__velocity_set(false);
        s_up.velocityOurs = false;
    }
}

void rd__upscale_set(bool on, float ratio)
{
    s_up.asked = true;
    s_up.want = on;
    s_up.ratio = on ? clampRatio(ratio) : 0.0f;
    syncVelocity();
}

void rd__upscale_follow(const RdSettings *st, bool crt, float displayScale)
{
    /* Upscaling = FSR 3.1 in force (rd_upscale_temporal.c: supported, no
     * CRT filter, no Downsampling) at the quality's ratio, Native AA under
     * the Original preset or at a display scale of 1 or less */
    s_up.fsr3 = rd__upscale_temporal_on(st, crt);
    s_up.fsr3Ratio = s_up.fsr3 ? clampRatio(rd__upscale_temporal_ratio(st, displayScale)) : 0.0f;
    syncVelocity();
}

bool rd__upscale_by_fsr3(void)
{
    if (!s_up.asked) {
        readSwitch();
    }
    return !s_up.want && s_up.fsr3;
}

bool rd__upscale_wanted(void)
{
    if (!s_up.asked) {
        readSwitch();
    }
    return (s_up.want || s_up.fsr3) && !rd__crt_on();
}

float rd__upscale_ratio(void)
{
    if (!s_up.asked) {
        readSwitch();
    }
    return s_up.want ? s_up.ratio : (s_up.fsr3 ? s_up.fsr3Ratio : 0.0f);
}

float rd_upscale_ratio_of(const RdSettings *st, bool crt, float displayScale)
{
    if (!s_up.asked) {
        readSwitch();
    }
    if (s_up.want) {
        return crt ? 0.0f : s_up.ratio;
    }
    return st && rd__upscale_temporal_on(st, crt)
               ? clampRatio(rd__upscale_temporal_ratio(st, displayScale))
               : 0.0f;
}

void rd__upscale_set_phases(uint32_t n)
{
    s_up.phases = n > 1024u ? 1024u : n;
    s_up.index = 0;
}

float rd_upscale_ratio(void)
{
    return rd__upscale_wanted() ? rd__upscale_ratio() : 0.0f;
}

uint32_t rd__upscale_phases(void)
{
    return s_up.phases;
}

/* ------------------------------------------------------- the scale split */

void rd__upscale_render_scale(float dsx, float dsy, float ratio, float cap, float *sx, float *sy)
{
    const float k = clampRatio(ratio);
    float x = dsx / k, y = dsy / k;
    /* Auto's step: the vertical scale at most the cap, both axes together
     * (the window's box keeps the PS2's pixel shape), and the ratio at most
     * 3 (FSR's Ultra Performance) */
    if (cap > 0.0f && y > cap) {
        float f = cap / y;
        if (y * f < dsy / 3.0f) {
            f = dsy / 3.0f / y;
        }
        x *= f;
        y *= f;
    }
    *sx = x < 1.0f ? 1.0f : x;
    *sy = y < 1.0f ? 1.0f : y;
}

bool rd__upscale_split_differs(bool on, float dsx, float dsy)
{
    return on != s_up.split || (on && (dsx != s_up.dsx || dsy != s_up.dsy));
}

void rd__upscale_split_set(bool on, float dsx, float dsy)
{
    s_up.split = on;
    s_up.dsx = on ? dsx : 0.0f;
    s_up.dsy = on ? dsy : 0.0f;
}

bool rd__upscale_point_on(void)
{
    return s_up.split;
}

void rd__display_scale(float *sx, float *sy)
{
    if (s_up.split) {
        *sx = s_up.dsx;
        *sy = s_up.dsy;
        return;
    }
    *sx = g_rd.sceneSx > 0.0f ? g_rd.sceneSx : 1.0f;
    *sy = g_rd.sceneSy > 0.0f ? g_rd.sceneSy : 1.0f;
}

/* --------------------------------------------------------------- jitter */

void rd__upscale_replay_begin(int keep, bool present)
{
    rd__fsr3_collect(); /* FSR 3.1's retired contexts the GPU is done with */
    if (!s_up.split || s_up.phases == 0) {
        if (s_up.jitterOurs) {
            rd__frame_jitter_clear(); /* the sequence's, not [dev] jitter_test's */
            s_up.jitterOurs = false;
        }
        return;
    }
    if (!present) {
        return; /* a replay that presents nothing keeps the jitter in force */
    }
    if (keep) {
        s_up.since++; /* the still re-present: no scene drawn, no dispatch */
        s_up.stats.reshown++;
        return;
    }
    float j[2];
    rd__jitter_halton(s_up.index % s_up.phases, j);
    s_up.index++;
    rd__frame_jitter_set(j[0], j[1]);
    s_up.jitterOurs = true;
    s_up.stats.jittered++;
}

/* --------------------------------------------------------------- the log */

static void refuse(const char *why, uint32_t frame)
{
    const uint32_t n = ++s_up.stats.refused;
    if (n <= 8u || n % 100u == 0u) {
        rd__log("upscale point: frame %u not upscaled (%s): the reduction reads SCENE at the "
                "render scale (%u times so far)",
                frame, why, n);
    }
}

/* -------------------------------------------------------------- UPSCALED */

static bool outEnsure(void)
{
    RdTargetRec g;
    memset(&g, 0, sizeof(g));
    g.w = g_rd.gsW;
    g.h = g_rd.gsH;
    rd__target_scale_of(&g, RD_UPSCALED_NAMED);
    if (s_up.out.id && s_up.geom.tw == g.tw && s_up.geom.th == g.th) {
        s_up.geom = g; /* the same texels; the geometry as now */
        return true;
    }
    if (s_up.out.id) {
        rd__retire_texture(s_up.out);
        s_up.out = (RhiTexture){0};
    }
    /* storage too, for an upscaler that writes it from a compute dispatch
     * (rhi.h RHI_TEX_STORAGE; a backend that refuses it gets the texture
     * without, which a pixel-shader hook needs no more than) */
    const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC;
    s_up.out = rhi_create_texture(&(RhiTextureDesc){g.tw, g.th, 1, RHI_FMT_RGBA8_UNORM,
                                                    usage | RHI_TEX_STORAGE, "rd upscaled", 0});
    s_up.storage = s_up.out.id != 0;
    if (!s_up.out.id) {
        s_up.out = rhi_create_texture(
            &(RhiTextureDesc){g.tw, g.th, 1, RHI_FMT_RGBA8_UNORM, usage, "rd upscaled", 0});
    }
    s_up.outState = RHI_STATE_UNDEFINED;
    s_up.geom = g;
    rd__log("upscale point: UPSCALED %ux%u (display scale %gx%g%s)%s", g.tw, g.th, (double)g.sx,
            (double)g.sy, g.ox > 0.0f ? ", the Full pixel window" : "",
            !s_up.out.id   ? ": not made, no upscaling"
            : s_up.storage ? ", a storage texture too"
                           : ", without storage (the backend refused it)");
    return s_up.out.id != 0;
}

/* ------------------------------------------------------------ edge guard */

static bool stripEnsure(RhiTexture *t, RhiState *st, uint32_t *have, uint32_t w, uint32_t h,
                        uint32_t n)
{
    if (t->id && *have == n) {
        return true;
    }
    if (t->id) {
        rd__retire_texture(*t);
    }
    *t = rhi_create_texture(&(RhiTextureDesc){
        w, h, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST, "rd upscale edge", 0});
    *st = RHI_STATE_UNDEFINED;
    *have = t->id ? n : 0u;
    return t->id != 0;
}

/* A jitter right (down) moves a full-frame GS sprite off texel column
 * (row) 0's centre: the column (row) would keep what the replay before
 * left.  It is made a copy of column (row) 1 instead, the edge clamped as
 * a sampler clamps it.  Nothing with no jitter, a jitter of 0 or one left
 * (up) on that axis, or a Full pixel window (its texel 0 lies inside the
 * frame, which every draw covers). */
static void edgeGuard(Replay *r, RhiCommandList cl, RdTargetRec *t)
{
    float j[2];
    if (!rd__frame_jitter_get(j) || t->ox > 0.0f || t->tw < 2u || t->th < 2u) {
        return;
    }
    const bool cols = j[0] > 0.0f, rows = j[1] > 0.0f;
    if (cols && stripEnsure(&s_up.col, &s_up.colState, &s_up.colH, 1u, t->th, t->th)) {
        rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
        rd__transition(cl, s_up.col, &s_up.colState, RHI_STATE_COPY_DST);
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){1, 0, 1, t->th}, s_up.col, 0, 0);
        rd__transition(cl, s_up.col, &s_up.colState, RHI_STATE_COPY_SRC);
        rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_DST);
        rhi_cmd_copy_texture(cl, s_up.col, (RhiRect){0, 0, 1, t->th}, t->color, 0, 0);
        s_up.stats.guardCols++;
        r->writeSerial++; /* SCENE's colour changed (DATE's snapshot reuse) */
    }
    if (rows && stripEnsure(&s_up.row, &s_up.rowState, &s_up.rowW, t->tw, 1u, t->tw)) {
        rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
        rd__transition(cl, s_up.row, &s_up.rowState, RHI_STATE_COPY_DST);
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){0, 1, t->tw, 1}, s_up.row, 0, 0);
        rd__transition(cl, s_up.row, &s_up.rowState, RHI_STATE_COPY_SRC);
        rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_DST);
        rhi_cmd_copy_texture(cl, s_up.row, (RhiRect){0, 0, t->tw, 1}, t->color, 0, 0);
        s_up.stats.guardRows++;
        r->writeSerial++;
    }
}

/* ----------------------------------------------------------- the marker */

/* FSR 3.1 dispatched while the VU vertex pre-pass is off ([dev] vu_prepass
 * = false, a failed self-test, a record that could not be made): no draw
 * gets its second record, so VELOCITY stays 0 and every moving object
 * trails.  One line each time that state begins. */
static void prepassNote(void)
{
    static bool told;
    const bool off = rd__upscale_by_fsr3() && !g_rd.vuPrepass;
    if (off && !told) {
        rd__log("FSR 3.1: the vertex pre-pass is off, so moving objects carry no motion: expect "
                "trails (%s)",
                g_rd.vuPrepassWhy[0] ? g_rd.vuPrepassWhy : "no reason recorded");
    }
    told = off;
}

void rd__upscale_point(Replay *r, const RdFrame *f, const RdCmd *c, bool msaa)
{
    if (!s_up.split) {
        return;
    }
    s_up.stats.points++;
    const uint32_t frame = f ? f->number : 0u;
    if (s_up.pointReplay == g_rd.replayCounter) {
        return; /* a second reduction of the frame: UPSCALED as the first left it */
    }
    s_up.pointReplay = g_rd.replayCounter;
    s_up.madeReplay = 0;
    const uint32_t sceneId = rd_target(RD_TARGET_SCENE).id;
    RdTargetRec *ts = rd__target_rec(sceneId);
    if (c->u[0] != sceneId) {
        refuse("the reduction reads another target", frame);
        return;
    }
    if (!ts || !ts->color.id) {
        refuse("no SCENE", frame);
        return;
    }
    if (msaa) {
        refuse("SCENE is multisampled", frame);
        return;
    }
    if (!outEnsure()) {
        refuse("no UPSCALED", frame);
        return;
    }
    const RhiCommandList cl = rd__replay_cl();
    edgeGuard(r, cl, ts);
    RdUpscaleIn in;
    memset(&in, 0, sizeof(in));
    in.color = ts->color;
    in.colorState = &ts->colorState;
    in.colorW = ts->tw;
    in.colorH = ts->th;
    if (ts->withDepth && ts->depth.id && ts->depthState != RHI_STATE_UNDEFINED) {
        in.depth = rd__sampled_depth(cl, ts, &s_up.depth, "rd upscale depth copy");
        in.depthW = in.depth.id ? ts->tw : 0u;
        in.depthH = in.depth.id ? ts->th : 0u;
    }
    RhiTexture vt;
    RhiState *vs = NULL;
    uint32_t vw = 0, vh = 0;
    if (rd__velocity_on() && rd__velocity_target(&vt, &vs, &vw, &vh)) {
        in.velocity = vt;
        in.velocityState = vs;
        in.velocityW = vw;
        in.velocityH = vh;
    }
    in.out = s_up.out;
    in.outState = &s_up.outState;
    in.outW = s_up.geom.tw;
    in.outH = s_up.geom.th;
    in.renderSx = ts->sx;
    in.renderSy = ts->sy;
    in.displaySx = s_up.geom.sx;
    in.displaySy = s_up.geom.sy;
    (void)rd__frame_jitter_get(in.jitter);
    const bool resized = in.outW != s_up.lastW || in.outH != s_up.lastH;
    /* a new history: decided from the frame replayed here (its cut, and the
     * attach's mark on its own marker: the first frame, a frame after the
     * motion vectors went on), never from a value the game thread's attach
     * of a later frame may already have changed (interpolation off) */
    const bool frameCut = f && (f->cut != 0 || (f->hasCamera && f->camera.cut != 0));
    in.reset = s_up.stats.dispatched == 0u || resized || frameCut ||
               (c->u[2] & RD_UPSCALE_NO_HISTORY) != 0u;
    in.presents = s_up.since + 1u;
    in.frame = frame;
    /* the frame camera (rd_frame.c rd__fill_camera_cb's): near, far, and
     * the vertical angle from the game's projection distance (zoom, in GS
     * pixels) over half the frame's height */
    if (f && f->hasCamera && f->camera.farZ > f->camera.nearZ && f->camera.nearZ > 0.0f &&
        f->camera.zoom > 0.0f) {
        in.camera = true;
        in.cameraNear = f->camera.nearZ;
        in.cameraFar = f->camera.farZ;
        const float half = 0.5f * (float)(f->gsH ? f->gsH : g_rd.gsH);
        in.fovY = 2.0f * atanf(half / f->camera.zoom);
    }
    if (!rd__upscale_dispatch(cl, &in)) {
        refuse("the hook wrote nothing", frame);
        return;
    }
    rd__transition(cl, s_up.out, &s_up.outState, RHI_STATE_SHADER_READ);
    prepassNote();
    s_up.madeReplay = g_rd.replayCounter;
    s_up.since = 0;
    s_up.lastW = in.outW;
    s_up.lastH = in.outH;
    s_up.stats.dispatched++;
    s_up.stats.resets += in.reset ? 1u : 0u;
    s_up.stats.outW = in.outW;
    s_up.stats.outH = in.outH;
    s_up.stats.colorW = in.colorW;
    s_up.stats.colorH = in.colorH;
    if (s_up.stats.dispatched == 1u || resized) {
        rd__log("upscale point: SCENE %ux%u (render scale %gx%g) -> UPSCALED %ux%u (display scale "
                "%gx%g) by %s at frame %u; depth %s, motion vectors %s, jitter %.4g, %.4g "
                "(phase count %u)",
                in.colorW, in.colorH, (double)in.renderSx, (double)in.renderSy, in.outW, in.outH,
                (double)in.displaySx, (double)in.displaySy, rd__upscale_hook_name(), frame,
                !in.depth.id                  ? "none"
                : in.depth.id == ts->depth.id ? "in place"
                                              : "a copy",
                in.velocity.id ? "VELOCITY" : "none", (double)in.jitter[0], (double)in.jitter[1],
                s_up.phases);
    }
}

/* ------------------------------------------------------- the reduction */

bool rd__upscale_remap_begin(const RdStateBlock *st)
{
    if (s_swapped || !s_up.split || s_up.madeReplay == 0u ||
        s_up.madeReplay != g_rd.replayCounter || !st->ds.texEnabled) {
        return false;
    }
    const RdTexRec *t = rd__tex_rec(st->tex);
    const uint32_t sceneId = rd_target(RD_TARGET_SCENE).id;
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view == RD_VIEW_DEPTH || t->target != sceneId) {
        return false;
    }
    RdTargetRec *sc = rd__target_rec(sceneId);
    if (!sc) {
        return false;
    }
    s_saved = *sc;
    sc->color = s_up.out;
    sc->colorState = s_up.outState;
    sc->tw = s_up.geom.tw;
    sc->th = s_up.geom.th;
    sc->sx = s_up.geom.sx;
    sc->sy = s_up.geom.sy;
    sc->ox = s_up.geom.ox;
    sc->uintView = 0;           /* UPSCALED has no integer view */
    sc->snap = (RhiTexture){0}; /* nor a snapshot (the reduction draws into DISPLAY) */
    sc->snapState = RHI_STATE_UNDEFINED;
    s_swapped = true;
    s_up.stats.remapped++;
    return true;
}

void rd__upscale_remap_end(void)
{
    if (!s_swapped) {
        return;
    }
    RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (sc) {
        s_up.outState = sc->colorState; /* where the sprite left UPSCALED */
        *sc = s_saved;
    }
    s_swapped = false;
}

/* ----------------------------------------------------------- the rest */

const RdUpscaleStats *rd__upscale_stats(void)
{
    return &s_up.stats;
}

bool rd__upscale_read(void *dst, size_t size, uint32_t *w, uint32_t *h)
{
    if (!s_up.out.id || !dst || size < (size_t)s_up.geom.tw * s_up.geom.th * 4u) {
        return false;
    }
    const bool ok =
        rd__read_rhi_texture(s_up.out, &s_up.outState, s_up.geom.tw, s_up.geom.th, dst, size);
    if (ok) {
        *w = s_up.geom.tw;
        *h = s_up.geom.th;
    }
    return ok;
}

void rd__upscale_free(void)
{
    if (s_up.out.id) {
        rd__retire_texture(s_up.out);
    }
    if (s_up.col.id) {
        rd__retire_texture(s_up.col);
    }
    if (s_up.row.id) {
        rd__retire_texture(s_up.row);
    }
    s_up.out = s_up.col = s_up.row = (RhiTexture){0};
    s_up.colH = s_up.rowW = 0;
    s_up.madeReplay = 0;
    s_up.lastW = s_up.lastH = 0;
    memset(&s_up.geom, 0, sizeof(s_up.geom));
    rd__depth_copy_free(&s_up.depth);
    /* FSR 3.1's context (rd_fsr3.c): kept while FSR 3.1 stays in force with
     * the split (the next dispatch uses it at the same sizes and flags),
     * else destroyed once the GPU is done with it.  rd__apply_display has
     * put the new state in force before the targets are made again */
    rd__fsr3_free(s_up.split && rd__upscale_by_fsr3());
}
