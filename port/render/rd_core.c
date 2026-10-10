/* rd_core.c: the recording half of rd.h.
 *
 * A frame is 13 command lists plus a payload arena (rd_internal.h).  The
 * game fiber records into the list rd_select_list chose; state calls record
 * deltas, they do not change anything at once.  rd_end_frame closes the
 * frame, walks its state deltas in replay order (lists 0..12, or 11..12 for
 * a keep frame) from the state the previous frame left, and hands it to the
 * replayer, which applies the same deltas to the same persistent state
 * block in the same order.  So state leaks between lists, and from one frame
 * into the next, exactly as GS registers do (rd.h, "Defaults and leakage").
 *
 * The two most recent closed frames are retained (the current one and the
 * one before it) for the interpolation (rd_interp.c); the frame
 * being recorded takes a third slot (RD_FRAME_RING).
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../fmv/rd_video.h"
#include "rd_internal.h"
#include "modelpack.h" /* the model pack switch */
#include "rd_mesh.h"
#include "rd_tex.h"
#include "texpack.h"

RdContext g_rd;

static RdHostCall s_hostCall;

double rd__now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e3 / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
#endif
}

void rd_set_host_call(RdHostCall call)
{
    s_hostCall = call;
}

void rd__on_host(void (*fn)(void *arg), void *arg)
{
    if (s_hostCall) {
        s_hostCall(fn, arg);
    } else {
        fn(arg);
    }
}

/* false in the game: a stub command is logged once and skipped; the tests
 * set it (rd__set_not_implemented_fatal(true)) so a stub replayed stops them */
static bool s_notImplementedFatal = false;
static bool s_notImplementedLogged;

static uint32_t s_notImplementedCount;

/* ------------------------------------------------------------ diagnostics */

void rd__log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("rd: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void rd__log_once(int bit, const char *fmt, ...)
{
    if (g_rd.onceFlags & (1u << bit)) {
        return;
    }
    g_rd.onceFlags |= 1u << bit;
    va_list ap;
    va_start(ap, fmt);
    fputs("rd: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputs(" (reported once)\n", stderr);
    va_end(ap);
}

void rd__not_implemented(const char *what)
{
    s_notImplementedCount++;
    if (s_notImplementedFatal) {
        fprintf(stderr, "rd: replay of %s is not implemented yet\n", what);
        abort();
    }
    if (!s_notImplementedLogged) {
        s_notImplementedLogged = true;
        fprintf(stderr, "rd: replay of %s is not implemented yet: skipped (reported once)\n", what);
    }
}

void rd__set_not_implemented_fatal(bool fatal)
{
    s_notImplementedFatal = fatal;
}

uint32_t rd__not_implemented_count(void)
{
    return s_notImplementedCount;
}

/* ------------------------------------------------------------ state block */

void rd__reset_state_block(RdStateBlock *s)
{
    memset(s, 0, sizeof(*s));
    s->ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
    s->ds.blend = RD_BLEND_LERP_AS;
    s->ds.blendFix = 0x80;
    s->ds.zwrite = RD_ZWRITE_ON;
    s->ds.colclamp = 1;
    s->ds.texa = RD_TEXA_80_80;
    s->ds.texFn = RD_TEXFN_MODULATE;
    s->ds.tcc = RD_TCC_RGBA;
    s->ds.colorMask = 0xF;
    s->color = RD_TARGET_SCENE + 1;
    s->depth = RD_TARGET_SCENE + 1;
    s->gsW = g_rd.gsW ? g_rd.gsW : 512;
    s->gsH = g_rd.gsH ? g_rd.gsH : 512;
    s->useOffset = 1;
    s->scissor[2] = (int32_t)s->gsW - 1;
    s->scissor[3] = (int32_t)s->gsH - 1;
    s->gouraud = 1;
    s->aa1 = 0;
}

static uint8_t maskFromFbmsk(uint32_t fbmsk)
{
    uint8_t m = 0;
    for (int c = 0; c < 4; c++) {
        uint32_t bits = (fbmsk >> (8 * c)) & 0xFF;
        if (bits != 0 && bits != 0xFF) {
            rd__log_once(
                RD_ONCE_FBMSK,
                "FRAME.FBMSK 0x%08x masks part of a channel; the port masks whole channels", fbmsk);
        }
        if (bits != 0xFF) {
            m |= (uint8_t)(1u << c);
        }
    }
    return m;
}

bool rd__apply_state(RdStateBlock *s, const RdCmd *c)
{
    RdDrawState *d = &s->ds;
    switch (c->type) {
    case RDC_TEST:
        d->test.ate = c->b[0];
        d->test.atst = c->b[1];
        d->test.aref = c->b[2];
        d->test.afail = c->b[3];
        d->test.date = c->b[4];
        d->test.zte = c->b[5];
        d->test.ztst = c->b[6];
        return true;
    case RDC_BLEND:
        d->blend = c->b[0];
        d->blendFix = c->b[1];
        d->abe = c->b[2];
        return true;
    case RDC_ABE:
        d->abe = c->b[0];
        return true;
    case RDC_ZWRITE:
        d->zwrite = c->b[0];
        return true;
    case RDC_FBA:
        d->fba = c->b[0];
        return true;
    case RDC_PABE:
        d->pabe = c->b[0];
        return true;
    case RDC_COLCLAMP:
        d->colclamp = c->b[0];
        return true;
    case RDC_TEXA:
        d->texa = c->b[0];
        return true;
    case RDC_FILTER:
        d->magFilter = c->b[0];
        d->minFilter = c->b[1];
        return true;
    case RDC_WRAP:
        d->wrap.s = c->b[0];
        d->wrap.t = c->b[1];
        return true;
    case RDC_TEXTURE:
        s->tex = c->u[0];
        d->texFn = c->b[0];
        d->tcc = c->b[1];
        d->texEnabled = 1;
        return true;
    case RDC_TEXTURE_OFF:
        d->texEnabled = 0;
        return true;
    case RDC_UVOFFSET:
        s->uvOffset[0] = c->f[0];
        s->uvOffset[1] = c->f[1];
        return true;
    case RDC_COLORMASK:
        d->fbmsk = c->u[0];
        d->colorMask = maskFromFbmsk(c->u[0]);
        return true;
    case RDC_TARGET:
        s->color = c->u[0];
        s->depth = c->u[1];
        s->gsW = c->u[2] & 0xFFFF;
        s->gsH = c->u[2] >> 16;
        s->useOffset = c->b[0];
        s->scissor[0] = 0;
        s->scissor[1] = 0;
        s->scissor[2] = (int32_t)s->gsW - 1;
        s->scissor[3] = (int32_t)s->gsH - 1;
        return true;
    case RDC_SCISSOR:
        for (int i = 0; i < 4; i++) {
            s->scissor[i] = (int32_t)c->u[i];
        }
        return true;
    case RDC_ALPHA:
        d->blend = c->b[0];
        d->blendFix = c->b[1];
        return true;
    case RDC_SHADE:
        s->gouraud = c->b[0];
        return true;
    case RDC_AA1:
        s->aa1 = c->b[0];
        return true;
    default:
        return false;
    }
}

/* ----------------------------------------------------------------- frames */

void rd__frame_reset(RdFrame *f)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        f->lists[l].count = 0;
    }
    f->payloadSize = 0;
    for (uint32_t i = 0; i < f->tempCount; i++) {
        rd__temp_target_free(f->tempTargets[i]);
    }
    f->tempCount = 0;
    rd__water_frame_reset(f); /* block targets, aliases, camera scopes */
    f->hasCamera = 0;
    f->hasVu = 0;
    f->headValid = 0;
    f->cut = 0;
    f->fade = 0;
    f->textItems = 0;
    f->closed = 0;
    f->keep = 0;
    f->number = 0;
}

void rd__frame_free(RdFrame *f)
{
    rd__frame_reset(f);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(f->lists[l].cmds);
        f->lists[l].cmds = NULL;
        f->lists[l].cap = 0;
    }
    free(f->payload);
    f->payload = NULL;
    f->payloadCap = 0;
}

uint32_t rd__frame_payload(RdFrame *f, const void *data, uint32_t size)
{
    uint32_t off = (f->payloadSize + 7u) & ~7u;
    uint32_t end = off + size;
    if (end < off || end > 0x80000000u) {
        /* past 2 GiB the doubling below would wrap to 0 and never end */
        rd__log("payload arena: %u + %u bytes is past 2 GiB", off, size);
        abort();
    }
    if (end > f->payloadCap) {
        uint32_t cap = f->payloadCap ? f->payloadCap : 64 * 1024;
        while (cap < end) {
            cap *= 2;
        }
        uint8_t *p = realloc(f->payload, cap);
        if (!p) {
            rd__log("payload arena: out of memory (%u bytes)", cap);
            abort();
        }
        f->payload = p;
        f->payloadCap = cap;
    }
    if (data && size) {
        memcpy(f->payload + off, data, size);
    } else if (size) {
        memset(f->payload + off, 0, size);
    }
    f->payloadSize = end;
    return off;
}

void rd__walk(const RdFrame *f, int keep, RdStateBlock *state, RdWalkFn fn, void *user)
{
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            rd__apply_state(state, &cl->cmds[i]);
            if (fn) {
                fn(user, l, i, &cl->cmds[i], state);
            }
        }
    }
}

RdFrame *rd__rec_frame(void)
{
    return g_rd.recIndex >= 0 ? &g_rd.frames[g_rd.recIndex] : NULL;
}

RdCmd *rd__push(uint8_t type)
{
    RdFrame *f = rd__rec_frame();
    if (!f) {
        rd__log_once(RD_ONCE_OUTSIDE_FRAME,
                     "rd call outside rd_begin_frame/rd_end_frame dropped (command %u)",
                     (unsigned)type);
        return NULL;
    }
    RdCmdList *cl = &f->lists[g_rd.list];
    if (cl->count == cl->cap) {
        uint32_t cap = cl->cap ? cl->cap * 2 : 256;
        RdCmd *p = realloc(cl->cmds, (size_t)cap * sizeof(RdCmd));
        if (!p) {
            rd__log("command list: out of memory");
            abort();
        }
        cl->cmds = p;
        cl->cap = cap;
    }
    RdCmd *c = &cl->cmds[cl->count++];
    memset(c, 0, sizeof(*c));
    c->type = type;
    return c;
}

static void setKey(RdCmd *c, RdKey key)
{
    c->keyLo = (uint32_t)key;
    c->keyHi = (uint32_t)(key >> 32);
}

/* ---------------------------------------------------------------- targets */

static void namedTargetDesc(int id, uint32_t gsW, uint32_t gsH, uint32_t *w, uint32_t *h,
                            RhiFormat *fmt, uint8_t *depth)
{
    *fmt = RHI_FMT_RGBA8_UNORM;
    *depth = 0;
    switch (id) {
    case RD_TARGET_SCENE:
        *w = gsW;
        *h = gsH;
        *depth = 1;
        break;
    case RD_TARGET_DISPLAY:
    case RD_TARGET_DISPLAY_HELD: /* issue 28: DISPLAY's copy */
        *w = gsW;
        *h = gsH / 2;
        break;
    case RD_TARGET_SHADOW0:
        *w = *h = 256;
        break;
    case RD_TARGET_SHADOW1:
        *w = *h = 128;
        break;
    case RD_TARGET_SHADOW2:
        *w = *h = 64;
        break;
    case RD_TARGET_WORK0:
    case RD_TARGET_WORK3: /* TBP 0x3000, 256 x 128 */
        *w = 256;
        *h = 128;
        break;
    case RD_TARGET_WORK2: /* TBP 0x2E00, the scene-sized flare mask */
    case RD_TARGET_AURA_WORK:
        *w = gsW;
        *h = gsH;
        break;
    case RD_TARGET_WORK1:
    case RD_TARGET_AA0:
        *w = *h = 256;
        break;
    case RD_TARGET_AA1:
    case RD_TARGET_FEED128:
    case RD_TARGET_FEED_HELD:
    case RD_TARGET_AURA_TAP:
        *w = *h = 128;
        break;
    case RD_TARGET_WORK2_PAD:
        *w = 256;
        *h = 64;
        break;
    case RD_TARGET_DATE_SNAPSHOT:
        *w = gsW;
        *h = gsH;
        *fmt = RHI_FMT_R8_UNORM;
        break;
    default:
        *w = *h = 1;
        break;
    }
    /* the work buffers' resolution scale (rd.h rd_work_target_scale) applies
     * to the texture, not to the GS size: rd__target_scale_of */
}

/* The scene-class targets, which take the Enhanced scene
 * resolution and (SCENE, WORK2, AURA_WORK) the wide projection: the
 * scene-sized buffers and DISPLAY.  The other named targets are the fixed
 * work buffers, scaled by rd_work_target_scale. */
static int sceneClass(int id)
{
    return id == RD_TARGET_SCENE || id == RD_TARGET_DISPLAY || id == RD_TARGET_WORK2 ||
           id == RD_TARGET_AURA_WORK || id == RD_TARGET_DATE_SNAPSHOT ||
           id == RD_TARGET_DISPLAY_HELD;
}

/* DISPLAY and its held copy (issue 28), of one size and scale */
static int displayClass(int id)
{
    return id == RD_TARGET_DISPLAY || id == RD_TARGET_DISPLAY_HELD;
}

/* Shadow.c's blur levels (SHADOW0..2) keep the PS2 sizes at every scale.
 * The shadow's blur is not a GS distance there but the levels' resolution
 * itself (each level a bilinear half of the one before, 256, 128 and 64
 * texels over the screen, composited back with bilinear magnification), so
 * levels at the work scale would halve the penumbra: at 4x the softest level
 * would need a further Gaussian of about 4 GS pixels to match the Original
 * preset's. */
static int shadowLevel(int id)
{
    return id == RD_TARGET_SHADOW0 || id == RD_TARGET_SHADOW1 || id == RD_TARGET_SHADOW2;
}

static uint32_t scaled(uint32_t n, float k)
{
    uint32_t v = (uint32_t)((float)n * k + 0.5f);
    return v ? v : 1;
}

/* The texture size and scale of a target of GS size t->w x t->h: named is
 * the RdTargetId of a named target, -1 for a temporary one (scene-class when
 * it has the scene's GS size: the shadow count, rd_shadow.c).  At scale 1
 * (Original, and every Enhanced target the options leave alone) tw == w,
 * th == h. */
void rd__target_scale_of(RdTargetRec *t, int named)
{
    float sx = 1.0f, sy = 1.0f;
    int scene = named >= 0 ? sceneClass(named) : (t->w == g_rd.gsW && t->h == g_rd.gsH);
    if (scene) {
        sx = g_rd.sceneSx > 0.0f ? g_rd.sceneSx : 1.0f;
        sy = g_rd.sceneSy > 0.0f ? g_rd.sceneSy : 1.0f;
        if (displayClass(named) && g_rd.fullHeight) {
            sy *= 2.0f; /* the full-height scene: no vertical halving */
        }
    } else if (named >= 0 && g_rd.workScale > 1.0f && !shadowLevel(named)) {
        sx = sy = g_rd.workScale;
    }
    /* Widescreen reflections: a temporary target of another size with its
     * own depth buffer is a render-to-texture block that a 3D view is drawn
     * into through a screen matrix centred on it (puddle.c, pool.c; the
     * decoder's blocks, GifPacket.c gsAliasTarget).  Wider than 4:3 it gets
     * 1 / f times the texels across and the wide x scale, as SCENE does at
     * 1x, so the reflection covers what the wide scene shows instead of
     * ending at the 4:3 frame's edge.  f = 1 (4:3, Original): untouched. */
    const float f = g_rd.wideX;
    const int block = named < 0 && !scene && t->withDepth && f > 0.0f && f < 1.0f;
    if (block) {
        sx = 1.0f / f;
    }
    t->sx = sx;
    t->sy = sy;
    t->tw = sx == 1.0f ? t->w : scaled(t->w, sx);
    t->th = sy == 1.0f ? t->h : scaled(t->h, sy);
    t->wide = (scene && !displayClass(named) && named != RD_TARGET_DATE_SNAPSHOT) || block;
    t->wideBlock = (uint8_t)block;
}

float rd_work_target_scale(uint32_t sceneHeight)
{
    if (sceneHeight <= 448) {
        return 1.0f; /* the GS height (1x): the literal PS2 sizes */
    }
    float k = (float)sceneHeight / 448.0f;
    return k < 1.0f ? 1.0f : (k > 2.0f ? 2.0f : k);
}

static const char *const s_targetNames[RD_TARGET_COUNT] = {
    "SCENE",         "DISPLAY",   "SHADOW0",  "SHADOW1",   "SHADOW2",   "WORK0",
    "WORK1",         "WORK2",     "WORK3",    "AA0",       "AA1",       "FEED128",
    "DATE_SNAPSHOT", "AURA_WORK", "AURA_TAP", "WORK2_PAD", "FEED_HELD", "DISPLAY_HELD"};

RdTargetRec *rd__target_rec(uint32_t id)
{
    uint32_t slot = (id & 0xFFFF) - 1;
    if (id == 0 || slot >= RD_MAX_TARGETS) {
        return NULL;
    }
    RdTargetRec *t = &g_rd.targets[slot];
    if (!t->live || t->gen != (id >> 16)) {
        return NULL;
    }
    return t;
}

bool rd__target_create_gpu(RdTargetRec *t, const char *name)
{
    if (!g_rd.hasDevice) {
        return true;
    }
    uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST;
    t->color = rhi_create_texture(&(RhiTextureDesc){t->tw, t->th, 1, t->format, usage, name});
    t->colorState = RHI_STATE_UNDEFINED;
    if (t->withDepth) {
        /* every use the device allows, whichever way the fog reads it
         * (rd_fog_path.c): sampled in place, a copy source for its copy and
         * for the buffer path, so a change of path needs no new targets */
        const bool sampled = rhi_limits()->depthSampled;
        const uint32_t dsUsage =
            RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_SRC | (sampled ? (uint32_t)RHI_TEX_SAMPLED : 0u);
        t->depth =
            rhi_create_texture(&(RhiTextureDesc){t->tw, t->th, 1, RHI_FMT_D32F_S8, dsUsage, name});
        t->depthState = RHI_STATE_UNDEFINED;
        t->depthSampled = sampled;
    }
    return t->color.id != 0 && (!t->withDepth || t->depth.id != 0);
}

RhiTexture rd__sampled_depth(RhiCommandList cl, RdTargetRec *t, RdDepthCopy *copy, const char *name)
{
    if (!g_rd.depthCopy && t->depthSampled) {
        rd__transition(cl, t->depth, &t->depthState, RHI_STATE_DEPTH_READ);
        return t->depth;
    }
    /* the whole depth, both aspects, into a texture that is only ever
     * sampled: the read goes through a transfer, after the depth's last
     * pass has stored it, never through the attachment itself */
    if (!copy->tex.id || copy->w != t->tw || copy->h != t->th) {
        rd__depth_copy_free(copy);
        /* the depth-stencil usage: Vulkan's sampled depth layout
         * (DEPTH_STENCIL_READ_ONLY_OPTIMAL) requires it; a copy source for
         * the fog's probe (rd_fog_path.c) */
        copy->tex = rhi_create_texture(&(RhiTextureDesc){
            t->tw, t->th, 1, RHI_FMT_D32F_S8,
            RHI_TEX_SAMPLED | RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC, name});
        copy->state = RHI_STATE_UNDEFINED;
        copy->w = t->tw;
        copy->h = t->th;
    }
    if (!copy->tex.id) {
        rd__note_scene_pressure();
        return (RhiTexture){0};
    }
    rd__transition(cl, t->depth, &t->depthState, RHI_STATE_COPY_SRC);
    rd__transition(cl, copy->tex, &copy->state, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, t->depth, (RhiRect){0, 0, t->tw, t->th}, copy->tex, 0, 0);
    rd__transition(cl, copy->tex, &copy->state, RHI_STATE_SHADER_READ);
    return copy->tex;
}

void rd__depth_copy_free(RdDepthCopy *copy)
{
    if (copy->tex.id) {
        rhi_destroy_texture(copy->tex);
    }
    memset(copy, 0, sizeof(*copy));
}

void rd__force_depth_copy(void)
{
    rd__set_fog_path(RD_FOG_COPY);
}

void rd__target_destroy_gpu(RdTargetRec *t)
{
    if (!g_rd.hasDevice) {
        return;
    }
    if (t->color.id) {
        rhi_destroy_texture(t->color);
    }
    if (t->depth.id) {
        rhi_destroy_texture(t->depth);
    }
    if (t->snap.id) {
        rhi_destroy_texture(t->snap);
    }
    t->color = t->depth = t->snap = (RhiTexture){0};
}

/* One pass over the named targets at the scale in force.  True when a
 * scene-class target could not be created (the pass stops there: the caller
 * may retry smaller); any other failure is only logged, as it always was. */
static bool createNamedPass(bool stopOnSceneFail)
{
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (t->live) {
            rd__target_destroy_gpu(t);
        }
        uint32_t keepViews[3];
        memcpy(keepViews, t->viewTex, sizeof(keepViews));
        memset(t, 0, sizeof(*t));
        memcpy(t->viewTex, keepViews, sizeof(keepViews));
        t->live = 1;
        t->named = 1;
        namedTargetDesc(i, g_rd.gsW, g_rd.gsH, &t->w, &t->h, &t->format, &t->withDepth);
        rd__target_scale_of(t, i);
        if (!rd__target_create_gpu(t, s_targetNames[i])) {
            if (stopOnSceneFail && sceneClass(i)) {
                return true;
            }
            rd__log("could not create target %s", s_targetNames[i]);
        }
        /* cleared at the first replay (rd_replay.c clearNewTargets): a new
           texture holds whatever the driver's memory held before, and the
           first frames of a start sample DISPLAY (the game's kept frame
           buffer) before anything has drawn into it */
        t->clearPending = 1;
    }
    return false;
}

/* The named targets at the scene scale the options asked for.  A scene
 * scale the GPU cannot hold (16x of a wide picture is hundreds of megabytes
 * a target) would leave the scene targets without a texture and the picture
 * black, so the scale is halved, both axes together, until they fit or it
 * reaches 1x.  The scale in force then stays lowered until the options
 * change it (rd__apply_display), and rd_scene_scale_lowered reports it. */
static void createNamedTargets(void)
{
    if (g_rd.hasDevice) {
        /* what the last scale holds goes first, and really: a deferred free
           would still count against the allocations below */
        for (int i = 0; i < RD_TARGET_COUNT; i++) {
            rd__target_destroy_gpu(&g_rd.targets[i]);
        }
        rd__temp_target_pool_clear();
        rd__scene_caches_free();
        rd__effects_depth_free();
        rhi_collect_garbage_now();
    }
    for (;;) {
        const bool canLower = g_rd.hasDevice && (g_rd.sceneSx > 1.0f || g_rd.sceneSy > 1.0f);

        if (!createNamedPass(canLower)) {
            return;
        }
        /* give back what this pass made before asking for less */
        for (int i = 0; i < RD_TARGET_COUNT; i++) {
            rd__target_destroy_gpu(&g_rd.targets[i]);
        }
        rhi_collect_garbage_now(); /* the next, smaller try must find this memory free */
        const float nx = g_rd.sceneSx * 0.5f < 1.0f ? 1.0f : g_rd.sceneSx * 0.5f;
        const float ny = g_rd.sceneSy * 0.5f < 1.0f ? 1.0f : g_rd.sceneSy * 0.5f;

        rd__log("display: scene %gx%g does not fit this GPU, falling back to %gx%g",
                (double)g_rd.sceneSx, (double)g_rd.sceneSy, (double)nx, (double)ny);
        g_rd.sceneSx = nx;
        g_rd.sceneSy = ny;
        g_rd.sceneFellBack = true;
    }
}

void rd__note_scene_pressure(void)
{
    if (g_rd.hasDevice && (g_rd.sceneSx > 1.0f || g_rd.sceneSy > 1.0f)) {
        g_rd.scenePressure = true;
    }
}

void rd__note_target_pressure(const RdTargetRec *t)
{
    const int idx = (int)(t - g_rd.targets);
    const int scene = idx >= 0 && idx < RD_TARGET_COUNT ? sceneClass(idx)
                                                        : (t->w == g_rd.gsW && t->h == g_rd.gsH);
    if (scene) {
        rd__note_scene_pressure();
    }
}

/* Temporary targets come from a pool.  A freed record keeps
 * its GPU textures (parked: not live, so its id is dead) and the next
 * allocation of the same texture size, format and depth takes them over:
 * the per-frame targets (the shadow count, the block targets, the decoder's
 * render-to-texture blocks) are created once, not every frame.  A taken
 * texture is cleared to zero at the first replay after (clearPending, the
 * content a new texture has on the drivers the tests run on), so a frame
 * never sees an earlier frame's pixels.  The GPU work of the frame that
 * freed it is ordered before by the queue and the transitions (the state
 * goes with the textures). */
static bool parkedFits(const RdTargetRec *t, const RdTargetRec *want)
{
    return !t->live && t->parked && t->tw == want->tw && t->th == want->th &&
           t->format == want->format && t->withDepth == want->withDepth;
}

uint32_t rd__temp_target_alloc(uint32_t w, uint32_t h, int withDepth, int keepAcross)
{
    RdTargetRec want;
    memset(&want, 0, sizeof(want));
    want.w = w ? w : 1;
    want.h = h ? h : 1;
    want.format = RHI_FMT_RGBA8_UNORM;
    want.withDepth = withDepth ? 1 : 0;
    rd__target_scale_of(&want, -1);
    int pick = -1, empty = -1, other = -1;
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        const RdTargetRec *t = &g_rd.targets[i];
        if (t->live) {
            continue;
        }
        if (parkedFits(t, &want)) {
            pick = i;
            break;
        }
        if (!t->parked && empty < 0) {
            empty = i;
        } else if (t->parked && other < 0) {
            other = i;
        }
    }
    const bool reuse = pick >= 0;
    if (!reuse) {
        pick = empty >= 0 ? empty : other; /* a parked one of another size goes last */
    }
    if (pick < 0) {
        rd__log_once(RD_ONCE_TEMP_FULL, "out of temporary targets (%d)",
                     RD_MAX_TARGETS - RD_TARGET_COUNT);
        return 0;
    }
    RdTargetRec *t = &g_rd.targets[pick];
    RdTargetRec keep = *t;
    if (!reuse && t->parked) {
        rd__target_destroy_gpu(t);
    }
    uint32_t gen = (t->gen + 1) & 0xFFFF;
    *t = want;
    t->gen = gen ? gen : 1;
    t->live = 1;
    t->keepAcross = keepAcross ? 1 : 0;
    if (reuse) {
        t->color = keep.color;
        t->depth = keep.depth;
        t->colorState = keep.colorState;
        t->depthState = keep.depthState;
        t->snap = keep.snap;
        t->snapState = keep.snapState;
        g_rd.stats.tempReused++;
    } else if (!rd__target_create_gpu(t, "temp target")) {
        rd__note_target_pressure(t); /* the shadow count, a screen-size alias */
    }
    t->clearPending = 1;
    g_rd.stats.tempTargets++;
    return (t->gen << 16) | (uint32_t)(pick + 1);
}

void rd__temp_target_free(uint32_t id)
{
    RdTargetRec *t = rd__target_rec(id);
    if (!t || t->named) {
        return;
    }
    for (int v = 0; v < 3; v++) {
        if (t->viewTex[v]) {
            rd_destroy_texture((RdTex){t->viewTex[v]});
        }
    }
    /* parked with its textures for the next allocation of its size,
     * up to RD_TEMP_PARKED of them (more are sizes no frame asks for any
     * more: destroyed) */
    uint32_t parked = 0;
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        parked += g_rd.targets[i].parked;
    }
    if (parked >= RD_TEMP_PARKED) {
        rd__target_destroy_gpu(t);
    }
    RdTargetRec keep = *t;
    memset(t, 0, sizeof(*t));
    t->gen = keep.gen;
    t->parked = g_rd.hasDevice && keep.color.id != 0;
    if (t->parked) {
        t->w = keep.w;
        t->h = keep.h;
        t->tw = keep.tw;
        t->th = keep.th;
        t->format = keep.format;
        t->withDepth = keep.withDepth;
        t->color = keep.color;
        t->depth = keep.depth;
        t->colorState = keep.colorState;
        t->depthState = keep.depthState;
        t->snap = keep.snap;
        t->snapState = keep.snapState;
    }
    if (g_rd.stats.tempTargets) {
        g_rd.stats.tempTargets--;
    }
}

void rd__temp_target_pool_clear(void)
{
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (!t->live && t->parked) {
            rd__target_destroy_gpu(t);
            const uint32_t gen = t->gen;
            memset(t, 0, sizeof(*t));
            t->gen = gen;
        }
    }
}

RdTarget rd_target(RdTargetId id)
{
    if ((int)id < 0 || id >= RD_TARGET_COUNT) {
        return (RdTarget){0};
    }
    return (RdTarget){(uint32_t)id + 1};
}

RdTarget rd_temp_target(uint32_t gsW, uint32_t gsH, int withDepth, int keepAcrossFrames)
{
    uint32_t id = rd__temp_target_alloc(gsW, gsH, withDepth, keepAcrossFrames);
    RdFrame *f = rd__rec_frame();
    if (id && !keepAcrossFrames) {
        if (f && f->tempCount < RD_MAX_TEMP_PER_FRAME) {
            f->tempTargets[f->tempCount++] = id;
        } else {
            rd__log("rd_temp_target outside a frame or past %d per frame: it is never freed",
                    RD_MAX_TEMP_PER_FRAME);
        }
    }
    return (RdTarget){id};
}

void rd_set_target(RdTarget color, RdTarget depth, uint32_t gsW, uint32_t gsH, int useOffset)
{
    /* a target rd_alias_target stands in for, with its own depth buffer */
    uint32_t alias = rd__alias_of(color.id);
    if (alias != 0) {
        const RdTargetRec *a = rd__target_rec(alias);
        if (depth.id == 0 || depth.id == color.id) {
            depth.id = a != NULL && a->withDepth ? alias : 0;
        }
        color.id = alias;
    }
    RdCmd *c = rd__push(RDC_TARGET);
    if (!c) {
        return;
    }
    c->u[0] = color.id;
    c->u[1] = depth.id;
    c->u[2] = (gsW & 0xFFFF) | ((gsH & 0xFFFF) << 16);
    /* bit 0 RD_TARGET_OFFSET, bit 1 RD_TARGET_HALF_Y (the flip's half offset) */
    c->b[0] = (uint8_t)(useOffset & (RD_TARGET_OFFSET | RD_TARGET_HALF_Y));
}

void rd_clear_target(RdTarget t, const uint8_t rgba[4], int clearDepth, uint32_t z)
{
    if (rd__alias_of(t.id) != 0) { /* a target rd_alias_target stands in for */
        t.id = rd__alias_of(t.id);
    }
    RdCmd *c = rd__push(RDC_CLEAR);
    if (!c) {
        return;
    }
    c->u[0] = t.id;
    if (rgba) {
        memcpy(c->b, rgba, 4);
    }
    c->b[4] = clearDepth ? 1 : 0;
    c->u[1] = z;
}

/* --------------------------------------------------------------- textures */

RdTexRec *rd__tex_rec(uint32_t id)
{
    uint32_t slot = (id & 0xFFFF) - 1;
    if (id == 0 || slot >= RD_MAX_TEXTURES || !g_rd.textures) {
        return NULL;
    }
    RdTexRec *t = &g_rd.textures[slot];
    if (!t->live || t->gen != (id >> 16)) {
        return NULL;
    }
    return t;
}

static uint32_t texAlloc(void)
{
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        RdTexRec *t = &g_rd.textures[i];
        if (!t->live) {
            uint32_t gen = (t->gen + 1) & 0xFFFF;
            memset(t, 0, sizeof(*t));
            t->gen = gen ? gen : 1;
            t->live = 1;
            return (t->gen << 16) | (i + 1);
        }
    }
    rd__log("out of texture slots (%d)", RD_MAX_TEXTURES);
    return 0;
}

/* The whole texture changed (a create, rd_update_texture) */
static void texDirtyAll(RdTexRec *t)
{
    if (!t->dirty) {
        g_rd.texDirtyCount++;
    }
    t->dirty = 1;
    t->dirtyX0 = t->dirtyY0 = 0;
    t->dirtyX1 = t->w;
    t->dirtyY1 = t->h;
}

RdTex rd__create_texture_fmt(uint32_t w, uint32_t h, const void *px, uint8_t format, RdTexSrc src,
                             const char *debugName)
{
    if (!g_rd.inited || w == 0 || h == 0 || format >= RD_TEXEL_COUNT) {
        return (RdTex){0};
    }
    uint32_t id = texAlloc();
    RdTexRec *t = rd__tex_rec(id);
    if (!t) {
        return (RdTex){0};
    }
    const size_t bytes = (size_t)w * h * rd__texel_bytes(format);
    t->kind = RD_TEXKIND_IMAGE;
    t->src = (uint8_t)src;
    t->format = format;
    t->w = w;
    t->h = h;
    t->pixels = malloc(bytes);
    if (!t->pixels) {
        t->live = 0;
        return (RdTex){0};
    }
    if (px) {
        memcpy(t->pixels, px, bytes);
    } else {
        memset(t->pixels, 0, bytes);
    }
    texDirtyAll(t);
    snprintf(t->name, sizeof(t->name), "%s", debugName ? debugName : "texture");
    return (RdTex){id};
}

RdTex rd__create_texture_replacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
                                     const char *debugName)
{
    if (!g_rd.inited || !img || !img->blob || img->levels == 0 || img->w == 0 || img->h == 0 ||
        img->fmt >= RD_TEXEL_COUNT || img->fmt == RD_TEXEL_R8) {
        return (RdTex){0};
    }
    TexpackImage *p = malloc(sizeof(*p));
    if (!p) {
        return (RdTex){0};
    }
    uint32_t id = texAlloc();
    RdTexRec *t = rd__tex_rec(id);
    if (!t) {
        free(p);
        return (RdTex){0};
    }
    *p = *img;
    memset(img, 0, sizeof(*img));
    t->kind = RD_TEXKIND_IMAGE;
    t->src = RD_TEXSRC_RGBA32; /* the pack's alpha is raw GS alpha: no TEXA */
    t->format = p->fmt;
    t->w = p->w;
    t->h = p->h;
    t->replacement = 1;
    t->uvW = uvW;
    t->uvH = uvH;
    t->mipLevels = (uint8_t)p->levels;
    t->pending = p;
    texDirtyAll(t);
    snprintf(t->name, sizeof(t->name), "%s", debugName ? debugName : "replacement");
    return (RdTex){id};
}

void rd__free_pending(RdTexRec *t)
{
    if (t && t->pending) {
        texpack_free_image(t->pending);
        free(t->pending);
        t->pending = NULL;
    }
}

RdTex rd_create_texture_src(uint32_t w, uint32_t h, const void *rgba8, RdTexSrc src,
                            const char *debugName)
{
    return rd__create_texture_fmt(w, h, rgba8, RD_TEXEL_RGBA8, src, debugName);
}

RdTex rd_create_texture_r8(uint32_t w, uint32_t h, const uint8_t *cov, const char *debugName)
{
    return rd__create_texture_fmt(w, h, cov, RD_TEXEL_R8, RD_TEXSRC_RGBA32, debugName);
}

RdTex rd_create_texture(uint32_t w, uint32_t h, const void *rgba8, RdTexA texaMode,
                        const char *debugName)
{
    RdTex r = rd_create_texture_src(w, h, rgba8, RD_TEXSRC_RGBA32, debugName);
    RdTexRec *t = rd__tex_rec(r.id);
    if (t) {
        t->bakedTexa = (uint8_t)texaMode;
    }
    return r;
}

void rd_update_texture(RdTex tex, const void *rgba8)
{
    RdTexRec *t = rd__tex_rec(tex.id);
    if (!t || t->kind != RD_TEXKIND_IMAGE || !rgba8 || !t->pixels) {
        return; /* a pack replacement has no CPU texels to update */
    }
    const size_t bytes = (size_t)t->w * t->h * rd__texel_bytes(t->format);
    /* an update that changes nothing (a page or CLUT re-expanded to the
     * same texels) is not uploaded again */
    if (memcmp(t->pixels, rgba8, bytes) == 0) {
        return;
    }
    memcpy(t->pixels, rgba8, bytes);
    texDirtyAll(t);
    g_rd.texFullUpdates++;
}

void rd_update_texture_rect(RdTex tex, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            const void *px)
{
    RdTexRec *t = rd__tex_rec(tex.id);
    if (!t || t->kind != RD_TEXKIND_IMAGE || !px || !t->pixels || x >= t->w || y >= t->h || !w ||
        !h) {
        return;
    }
    const uint32_t bpp = rd__texel_bytes(t->format);
    const size_t srcPitch = (size_t)w * bpp;
    const uint32_t cw = w < t->w - x ? w : t->w - x, ch = h < t->h - y ? h : t->h - y;
    const size_t rowBytes = (size_t)cw * bpp;
    const uint8_t *src = px;
    int changed = 0;
    for (uint32_t r = 0; r < ch; r++) {
        uint8_t *dst = t->pixels + ((size_t)(y + r) * t->w + x) * bpp;
        if (memcmp(dst, src + r * srcPitch, rowBytes) != 0) {
            memcpy(dst, src + r * srcPitch, rowBytes);
            changed = 1;
        }
    }
    if (!changed) {
        return; /* nothing changed: nothing to upload */
    }
    g_rd.texRectUpdates++;
    if (!t->dirty) {
        g_rd.texDirtyCount++;
        t->dirty = 1;
        t->dirtyX0 = x;
        t->dirtyY0 = y;
        t->dirtyX1 = x + cw;
        t->dirtyY1 = y + ch;
        return;
    }
    t->dirtyX0 = x < t->dirtyX0 ? x : t->dirtyX0;
    t->dirtyY0 = y < t->dirtyY0 ? y : t->dirtyY0;
    t->dirtyX1 = x + cw > t->dirtyX1 ? x + cw : t->dirtyX1;
    t->dirtyY1 = y + ch > t->dirtyY1 ? y + ch : t->dirtyY1;
}

void rd_destroy_texture(RdTex tex)
{
    RdTexRec *t = rd__tex_rec(tex.id);
    if (!t) {
        return;
    }
    if (t->kind == RD_TEXKIND_TARGET) {
        RdTargetRec *tr = rd__target_rec(t->target);
        if (tr && t->view < 3 && tr->viewTex[t->view] == tex.id) {
            tr->viewTex[t->view] = 0;
        }
    }
    if (t->rhi.id && g_rd.hasDevice) {
        rhi_destroy_texture(t->rhi);
    }
    if (t->dirty && g_rd.texDirtyCount) {
        g_rd.texDirtyCount--;
    }
    free(t->pixels);
    rd__free_pending(t); /* a replacement destroyed before its upload */
    uint32_t gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->gen = gen;
}

RdTex rd_target_texture(RdTarget target, RdTexView view)
{
    RdTargetRec *tr = rd__target_rec(target.id);
    if (!tr || (unsigned)view > RD_VIEW_DEPTH) {
        return (RdTex){0};
    }
    if (tr->viewTex[view] && rd__tex_rec(tr->viewTex[view])) {
        return (RdTex){tr->viewTex[view]};
    }
    uint32_t id = texAlloc();
    RdTexRec *t = rd__tex_rec(id);
    if (!t) {
        return (RdTex){0};
    }
    t->kind = RD_TEXKIND_TARGET;
    t->target = target.id;
    t->view = (uint8_t)view;
    t->src = view == RD_VIEW_RGB24_TA0 ? RD_TEXSRC_RGB24 : RD_TEXSRC_RGBA32;
    t->w = tr->w;
    t->h = tr->h;
    tr->viewTex[view] = id;
    return (RdTex){id};
}

/* ------------------------------------------------------------- lifecycle */

static void initCommon(uint32_t gsW, uint32_t gsH, const RdSettings *settings)
{
    memset(&g_rd, 0, sizeof(g_rd));
    g_rd.gsW = gsW ? gsW : 512;
    g_rd.gsH = gsH ? gsH : 512;
    if (settings) {
        g_rd.settings = *settings;
    }
    g_rd.textures = calloc(RD_MAX_TEXTURES, sizeof(RdTexRec));
    g_rd.meshes = calloc(RD_MAX_MESHES, sizeof(RdMeshRec));
    g_rd.recIndex = -1;
    g_rd.lastIndex = -1;
    g_rd.texLevelsFilter = -1;
    rd__reset_state_block(&g_rd.persistent);
    rd__vu_init(); /* the per-list VU images */
    g_rd.inited = true;
}

bool rd__init_record_only(uint32_t gsW, uint32_t gsH)
{
    if (g_rd.inited) {
        rd_shutdown();
    }
    initCommon(gsW, gsH, NULL);
    if (!g_rd.textures || !g_rd.meshes) {
        return false;
    }
    rd__apply_display();
    createNamedTargets();
    return true;
}

/* Frame dumps every N replayed frames: ico-pc.ini dump_every= and
 * dump_dir=, handed over by port/platform/host_config.c in the environment
 * (ICO_RD_DUMP_EVERY, ICO_RD_DUMP_DIR), read at rd_init. */
static uint32_t s_dumpEvery;

static char s_dumpDir[512];

/* ICO_RD_DUMP_INTERP=1 (the hand-over of the config key [dev]
 * dump_interp, port/platform/host_config.c) also dumps, next to
 * each frame dump, the frame interpolated half way from the one before
 * (rd-NNNNN-i50.rddump; rd__interp_frame at alpha 0.5, the feedback passes as
 * a first present) */
static int s_dumpInterp;

/* ICO_RD_DUMP_FROM (config key [dev] dump_from): no frame numbered
 * below it is dumped */
static uint32_t s_dumpFrom;

static void readDumpConfig(void)
{
    const char *every = getenv("ICO_RD_DUMP_EVERY");
    const char *dir = getenv("ICO_RD_DUMP_DIR");
    s_dumpEvery = every ? (uint32_t)strtoul(every, NULL, 10) : 0;
    snprintf(s_dumpDir, sizeof(s_dumpDir), "%s", dir ? dir : ".");
    const char *interp = getenv("ICO_RD_DUMP_INTERP");
    s_dumpInterp = interp != NULL && interp[0] != '\0' && interp[0] != '0';
    const char *from = getenv("ICO_RD_DUMP_FROM");
    s_dumpFrom = from ? (uint32_t)strtoul(from, NULL, 10) : 0;
    if (s_dumpEvery) {
        rd__log("dumping every %u frames from frame %u into %s%s", s_dumpEvery, s_dumpFrom,
                s_dumpDir, s_dumpInterp ? ", with the frame interpolated half way" : "");
    }
}

bool rd_init(uint32_t gsWidth, uint32_t gsHeight, const RdSettings *settings, void *sdlWindow)
{
    if (g_rd.inited) {
        rd_shutdown();
    }
    initCommon(gsWidth, gsHeight, settings);
    if (!g_rd.textures || !g_rd.meshes) {
        return false;
    }
    if (!rd__gpu_init(sdlWindow)) {
        rd__gpu_shutdown();
        free(g_rd.textures);
        free(g_rd.meshes);
        memset(&g_rd, 0, sizeof(g_rd));
        return false;
    }
    g_rd.hasDevice = true;
    /* the two-pass blend without dual-source blending (a
     * device without dualSrcBlend; ICO_RD_NO_DUAL=1 forces it for tests) */
    const char *noDual = getenv("ICO_RD_NO_DUAL");
    g_rd.noDual = !rhi_limits()->dualSourceBlend || (noDual && noDual[0] && noDual[0] != '0');
    if (g_rd.noDual) {
        rd__log("blend: two-pass fallback (no dualSrcBlend)");
    }
    /* the fog's depth path (RdContext.fogPath, depthCopy) */
    rd__fog_path_init();
    rd__apply_display(); /* the scales the named targets take */
    createNamedTargets();
    readDumpConfig();
    return true;
}

void rd_shutdown(void)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.hasDevice) {
        rhi_wait_idle();
    }
    for (int i = 0; i < RD_FRAME_RING; i++) {
        rd__frame_free(&g_rd.frames[i]);
    }
    rd__interp_shutdown();
    for (int i = 0; i < RD_MAX_TARGETS; i++) {
        if (g_rd.targets[i].live || g_rd.targets[i].parked) {
            rd__target_destroy_gpu(&g_rd.targets[i]);
        }
    }
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        RdTexRec *t = &g_rd.textures[i];
        if (t->live) {
            if (t->rhi.id && g_rd.hasDevice) {
                rhi_destroy_texture(t->rhi);
            }
            free(t->pixels);
            rd__free_pending(t);
        }
    }
    if (g_rd.hasDevice) {
        rd_video_shutdown(); /* the FMV path's objects (rd_video.c), before the device */
        rd__present_shutdown();
        rd__gpu_shutdown();
    }
    rd__pipeline_cache_clear();
    rd__mesh_shutdown();
    rd__vu_shutdown();
    free(g_rd.textures);
    free(g_rd.meshes);
    memset(&g_rd, 0, sizeof(g_rd));
}

static void fogSceneRemade(void *arg)
{
    (void)arg;
    rd__fog_scene_remade();
}

void rd_reset_scene(uint32_t gsWidth, uint32_t gsHeight)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.hasDevice) {
        rhi_wait_idle();
    }
    g_rd.gsW = gsWidth;
    g_rd.gsH = gsHeight;
    rd__apply_display();
    createNamedTargets();
    rd__temp_target_pool_clear(); /* the parked ones have the old scene size */
    for (int v = 0; v < 3; v++) {
        for (int i = 0; i < RD_TARGET_COUNT; i++) {
            RdTexRec *t = rd__tex_rec(g_rd.targets[i].viewTex[v]);
            if (t) {
                t->w = g_rd.targets[i].w;
                t->h = g_rd.targets[i].h;
            }
        }
    }
    if (g_rd.hasDevice) {
        rd__on_host(fogSceneRemade, NULL); /* a new SCENE size: the fog's probe and self-test */
    }
}

void rd_set_settings(const RdSettings *settings)
{
    if (settings) {
        g_rd.pendingSettings = *settings;
        g_rd.settingsPending = true;
    }
}

const RdSettings *rd_get_settings(void)
{
    return &g_rd.settings;
}

void rd_get_scene_scale(float *sx, float *sy)
{
    if (sx) {
        *sx = g_rd.sceneSx > 0.0f ? g_rd.sceneSx : 1.0f;
    }
    if (sy) {
        *sy = g_rd.sceneSy > 0.0f ? g_rd.sceneSy : 1.0f;
    }
}

int rd_scene_scale_lowered(void)
{
    /* below what was asked, whether the GPU's size limit or the allocation
       fallback brought it down */
    if (g_rd.settings.sceneScale <= 0.0f || g_rd.sceneSy + 0.5f >= g_rd.settings.sceneScale) {
        return 0;
    }
    return g_rd.sceneSy < 1.0f ? 1 : (int)g_rd.sceneSy;
}

void rd_set_mirror(int on)
{
    if (g_rd.mirrorRun != (on != 0)) {
        rd__log("mirror mode %s (frame %u)", on ? "on" : "off", g_rd.frameCounter);
    }
    g_rd.mirrorRun = on != 0;
}

bool rd_mirror_active(void)
{
    return rd__mirror_on();
}

bool rd_set_no_dual(bool on)
{
    if (!on && g_rd.hasDevice && !rhi_limits()->dualSourceBlend) {
        return false; /* the device has no dual-source blending: the fallback stays */
    }
    if (g_rd.noDual != on) {
        rd__log("blend: two-pass fallback %s", on ? "on" : "off");
    }
    g_rd.noDual = on;
    return true;
}

bool rd_no_dual(void)
{
    return g_rd.noDual;
}

/* The per-list defaults of gsb_SetGsDefault (GsBase.c:638-688, rd.h). */
typedef struct RdListDefault {
    uint64_t test;
    int zwrite;
    RdTexA texa;
} RdListDefault;

static void recordDefaults(void)
{
    static const RdListDefault kNormal = {RD_TEST_Z_GEQUAL, 1, RD_TEXA_80_80};
    static const RdListDefault kSemi = {RD_TEST_AT_GT64_FBONLY, 1, RD_TEXA_7F_81_AEM};
    static const RdListDefault kSpec = {RD_TEST_DATE1_Z_GEQUAL, 0, RD_TEXA_80_80};
    static const RdListDefault kPart = {RD_TEST_Z_GEQUAL, 0, RD_TEXA_80_80};
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        const RdListDefault *d = NULL;
        if (l == 0 || l >= 7) {
            d = &kNormal;
        } else if (l == 1 || l == 2) {
            d = &kSemi;
        } else if (l == 4) {
            d = &kSpec;
        } else if (l == 6) {
            d = &kPart;
        }
        if (!d) {
            continue; /* lists 3 and 5 inherit */
        }
        rd_select_list(l);
        rd_test_gs(d->test);
        rd_z_write(d->zwrite);
        rd_fba(0);
        rd_tex_a(d->texa);
    }
    rd_select_list(0);
}

static void recreateTargets(void *arg)
{
    (void)arg;
    rhi_wait_idle();
    createNamedTargets();
    rd__temp_target_pool_clear(); /* the parked textures have the old scale */
    rd__fog_scene_remade();       /* a new SCENE size: the fog's probe and self-test */
}

static void fogProbeFinish(void *arg)
{
    (void)arg;
    rd__fog_probe_finish();
}

typedef struct ReplayCall {
    const RdFrame *f;
    int keep;
} ReplayCall;

static void replayOnHost(void *arg)
{
    const ReplayCall *c = (const ReplayCall *)arg;
    rd__note_frame_present(c->f); /* rd_last_present_info, as rd_present notes its own */
    rd__replay_frame(c->f, c->keep, true);
}

void rd_begin_frame(void)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.recIndex >= 0) {
        rd__log("rd_begin_frame without rd_end_frame: the open frame is discarded");
    }
    /* the fog probe a replay recorded since the last frame: read back and
     * logged (once per start and new scene size) */
    if (rd__fog_probe_pending()) {
        rd__on_host(fogProbeFinish, NULL);
    }
    bool recreated = false;
    if (g_rd.settingsPending) {
        /* the texture pack switched off: the originals back (once
         * per edge; rd_tex.h rdtex_revert_replacements) */
        const bool packOff = g_rd.settings.texturePack && !g_rd.pendingSettings.texturePack;
        /* the model pack's switch and its dump, on a change */
        const bool modelsChanged = g_rd.settings.modelPack != g_rd.pendingSettings.modelPack;
        const bool dumpChanged = g_rd.settings.dumpModels != g_rd.pendingSettings.dumpModels;
        g_rd.settings = g_rd.pendingSettings;
        g_rd.settingsPending = false;
        if (packOff) {
            rdtex_revert_replacements();
        }
        if (modelsChanged) {
            modelpack_set_enabled(g_rd.settings.modelPack != 0);
        }
        if (dumpChanged) {
            modelpack_set_dump_enabled(g_rd.settings.dumpModels != 0);
        }
        /* the Settings menu applies here; a change of the
         * targets' scales recreates them (their content is lost: the next
         * frame redraws SCENE; DISPLAY's motion-blur history restarts) */
        if (rd__apply_display() && g_rd.hasDevice) {
            rd__log("display: targets recreated at scene %gx%g, work %g", (double)g_rd.sceneSx,
                    (double)g_rd.sceneSy, (double)g_rd.workScale);
            rd__on_host(recreateTargets, NULL);
            /* the retained frames' history is dropped: the frame opened
             * now and the next are the first pair interpolated */
            g_rd.interpFloor = g_rd.frameCounter + 1;
            recreated = true;
        }
    }
    /* A scene-sized texture made since the last frame did not fit
     * (rd__note_scene_pressure): the scale comes down by half here, between
     * frames, and the targets are made again at it.  Targets just made at a
     * new request get their own try first. */
    if (g_rd.scenePressure) {
        g_rd.scenePressure = false;
        if (!recreated && g_rd.hasDevice && (g_rd.sceneSx > 1.0f || g_rd.sceneSy > 1.0f)) {
            const float nx = g_rd.sceneSx * 0.5f < 1.0f ? 1.0f : g_rd.sceneSx * 0.5f;
            const float ny = g_rd.sceneSy * 0.5f < 1.0f ? 1.0f : g_rd.sceneSy * 0.5f;
            rd__log("display: scene %gx%g does not fit this GPU, falling back to %gx%g",
                    (double)g_rd.sceneSx, (double)g_rd.sceneSy, (double)nx, (double)ny);
            g_rd.sceneSx = nx;
            g_rd.sceneSy = ny;
            g_rd.sceneFellBack = true;
            rd__on_host(recreateTargets, NULL);
            g_rd.interpFloor = g_rd.frameCounter + 1;
        }
    }
    /* the slot after the last closed frame, which is neither it nor
     * the one before it */
    int idx = g_rd.lastIndex < 0 ? 0 : (g_rd.lastIndex + 1) % RD_FRAME_RING;
    RdFrame *f = &g_rd.frames[idx];
    rd__frame_reset(f);
    f->number = ++g_rd.frameCounter;
    rd__vu_mesh_sweep_stale(); /* retired meshes no kept frame drew */
    f->startState = g_rd.persistent;
    f->gsW = g_rd.gsW;
    f->gsH = g_rd.gsH;
    g_rd.recIndex = idx;
    g_rd.stats.draws = 0;
    f->cut = g_rd.cutPending; /* rd_camera_cut between frames */
    g_rd.cutPending = 0;
    recordDefaults();
}

void rd_end_frame(int keep)
{
    RdFrame *f = rd__rec_frame();
    if (!f) {
        return;
    }
    f->keep = keep ? 1 : 0;
    if (f->cut) {
        f->camera.cut = 1;
    }
    rd__frame_head_resolve(f, f->keep); /* the flip's head in the first replayed list */
    f->closed = 1;
    RdStateBlock s = f->startState;
    rd__walk(f, f->keep, &s, NULL, NULL);
    f->endState = s;
    g_rd.persistent = s;
    g_rd.lastIndex = g_rd.recIndex;
    g_rd.recIndex = -1;
    g_rd.stats.bytesPayload = f->payloadSize;
    g_rd.videoShown = 0;
    /* with interpolation on (either preset) the host presents (rd_present);
     * with it off the frame is replayed and presented once here */
    if (g_rd.hasDevice && !rd_interpolation_active()) {
        ReplayCall c = {f, f->keep};
        rd__on_host(replayOnHost, &c);
    }
    if (s_dumpEvery && f->number % s_dumpEvery == 0 && f->number >= s_dumpFrom) {
        char path[600];
        snprintf(path, sizeof(path), "%s/rd-%05u.rddump", s_dumpDir, f->number);
        if (rd__dump_frame(f, path)) {
            rd__log("frame %u dumped to %s", f->number, path);
        }
        if (s_dumpInterp) {
            RdInterpStats st;
            /* the previous frame too (rd-NNNNN-prev.rddump), so a
             * half-way dump can be checked against both its ticks */
            const RdFrame *pv = rd__prev_frame();
            snprintf(path, sizeof(path), "%s/rd-%05u-prev.rddump", s_dumpDir, f->number);
            if (pv && pv->closed && pv->number + 1 == f->number) {
                (void)rd__dump_frame(pv, path);
            }
            const RdFrame *i = rd__interp_frame(pv, f, 0.5f, 1, &st);
            snprintf(path, sizeof(path), "%s/rd-%05u-i50.rddump", s_dumpDir, f->number);
            if (i && rd__dump_frame(i, path)) {
                rd__log("frame %u interpolated half way (snap %u, %u keyed draws: %u blended, "
                        "%u unmatched, %u mismatched, %u jumped; %u mesh streams kept or "
                        "blended; %u blended as rotations; %u VU draws through the blended "
                        "camera, %u of them the tick's) dumped to %s",
                        f->number, st.snap, st.keyed, st.lerped, st.missing, st.mismatch, st.jump,
                        st.morph, st.rotated, st.rebased, st.rebasedCur, path);
            }
        }
    }
}

void rd_discard_frame(void)
{
    RdFrame *f = rd__rec_frame();
    if (!f) {
        return;
    }
    /* the state the dropped lists would have left is not applied: nothing
     * of them reached the GS */
    rd__frame_reset(f);
    g_rd.recIndex = -1;
}

bool rd_frame_open(void)
{
    return rd__rec_frame() != NULL;
}

void rd_set_camera(const RdCamera *cam)
{
    RdFrame *f = rd__rec_frame();
    if (f && cam) {
        f->camera = *cam;
        f->hasCamera = 1;
    }
}

void rd_camera_cut(void)
{
    RdFrame *f = rd__rec_frame();
    if (f) {
        f->cut = 1;
    } else {
        g_rd.cutPending = 1;
    }
}

uint32_t rd_frame_number(void)
{
    const RdFrame *f = rd__last_frame();
    return f && f->closed ? f->number : 0;
}

const RdFrame *rd__last_frame(void)
{
    return g_rd.lastIndex >= 0 ? &g_rd.frames[g_rd.lastIndex] : NULL;
}

const RdFrame *rd__prev_frame(void)
{
    if (g_rd.lastIndex < 0) {
        return NULL;
    }
    /* the slot before the last closed one in the ring */
    const RdFrame *f = &g_rd.frames[(g_rd.lastIndex + RD_FRAME_RING - 1) % RD_FRAME_RING];
    const RdFrame *last = &g_rd.frames[g_rd.lastIndex];
    return f->closed && f->number < last->number ? f : NULL;
}

/* ------------------------------------------------------------------ lists */

void rd_select_list(int list)
{
    if (list < 0 || list >= RD_LIST_COUNT) {
        rd__log_once(RD_ONCE_LIST_RANGE, "rd_select_list(%d) out of range", list);
        return;
    }
    g_rd.list = list;
}

int rd_current_list(void)
{
    return g_rd.list;
}

/* ----------------------------------------------------------- state deltas */

static void push1(uint8_t type, int v)
{
    RdCmd *c = rd__push(type);
    if (c) {
        c->b[0] = (uint8_t)v;
    }
}

void rd_blend(RdBlend eq, uint8_t fix, int abe)
{
    if ((unsigned)eq >= RD_BLEND_COUNT) {
        rd__log_once(RD_ONCE_BLEND_RANGE, "rd_blend(%d) out of range: blending off", (int)eq);
        eq = RD_BLEND_LERP_AS;
        abe = 0;
    }
    RdCmd *c = rd__push(RDC_BLEND);
    if (c) {
        c->b[0] = (uint8_t)eq;
        c->b[1] = fix;
        c->b[2] = abe ? 1 : 0;
    }
}

void rd_test(const RdTestState *t)
{
    RdCmd *c = rd__push(RDC_TEST);
    if (c && t) {
        c->b[0] = t->ate;
        c->b[1] = t->atst;
        c->b[2] = t->aref;
        c->b[3] = t->afail;
        c->b[4] = t->date;
        c->b[5] = t->zte;
        c->b[6] = t->ztst;
    }
}

void rd_test_gs(uint64_t gsTestWord)
{
    RdTestState t = rd_test_from_gs(gsTestWord);
    rd_test(&t);
}

void rd_z_write(int on)
{
    push1(RDC_ZWRITE, on ? RD_ZWRITE_ON : RD_ZWRITE_OFF);
}

void rd_fba(int on)
{
    push1(RDC_FBA, on ? 1 : 0);
}

void rd_pabe(int on)
{
    push1(RDC_PABE, on ? 1 : 0);
}

void rd_col_clamp(int on)
{
    push1(RDC_COLCLAMP, on ? 1 : 0);
}

void rd_tex_a(RdTexA mode)
{
    push1(RDC_TEXA, (int)mode < RD_TEXA_COUNT ? (int)mode : RD_TEXA_80_80);
}

void rd__rec_filter(RdFilter mag, RdFilter min)
{
    RdCmd *c = rd__push(RDC_FILTER);
    if (c) {
        c->b[0] = (uint8_t)mag;
        c->b[1] = (uint8_t)min;
    }
}

void rd_sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t)
{
    rd_sampler_filter(mag, min);
    rd_sampler_wrap(s, t);
}

void rd_texture(RdTex tex, RdTexFn fn, RdTcc tcc)
{
    /* a view of a target rd_alias_target stands in for */
    const RdTexRec *tr = rd__tex_rec(tex.id);
    if (tr != NULL && tr->kind == RD_TEXKIND_TARGET && rd__alias_of(tr->target) != 0) {
        tex = rd_target_texture((RdTarget){rd__alias_of(tr->target)}, (RdTexView)tr->view);
    }
    RdCmd *c = rd__push(RDC_TEXTURE);
    if (c) {
        c->u[0] = tex.id;
        c->b[0] = (uint8_t)fn;
        c->b[1] = (uint8_t)tcc;
    }
}

void rd_texture_off(void)
{
    rd__push(RDC_TEXTURE_OFF);
}

void rd_uv_offset(float u, float v)
{
    RdCmd *c = rd__push(RDC_UVOFFSET);
    if (c) {
        c->f[0] = u;
        c->f[1] = v;
    }
}

void rd_color_mask(uint32_t fbmsk)
{
    RdCmd *c = rd__push(RDC_COLORMASK);
    if (c) {
        c->u[0] = fbmsk;
    }
}

void rd__rec_scissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    RdCmd *c = rd__push(RDC_SCISSOR);
    if (c) {
        c->u[0] = (uint32_t)x0;
        c->u[1] = (uint32_t)y0;
        c->u[2] = (uint32_t)x1;
        c->u[3] = (uint32_t)y1;
    }
}

void rd__rec_abe(int abe)
{
    push1(RDC_ABE, abe ? 1 : 0);
}

void rd_abe(int abe)
{
    rd__rec_abe(abe);
}

void rd_blend_func(RdBlend eq, uint8_t fix)
{
    if ((unsigned)eq >= RD_BLEND_COUNT) {
        rd__log_once(RD_ONCE_BLEND_RANGE, "rd_blend_func(%d) out of range: ignored", (int)eq);
        return;
    }
    RdCmd *c = rd__push(RDC_ALPHA);
    if (c) {
        c->b[0] = (uint8_t)eq;
        c->b[1] = fix;
    }
}

void rd_scissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    rd__rec_scissor(x0, y0, x1, y1);
}

void rd_sampler_filter(RdFilter mag, RdFilter min)
{
    rd__rec_filter(mag, min);
}

void rd_sampler_wrap(RdWrap s, RdWrap t)
{
    RdCmd *c = rd__push(RDC_WRAP);
    if (c) {
        c->b[0] = (uint8_t)s;
        c->b[1] = (uint8_t)t;
    }
}

void rd_gouraud(int iip)
{
    push1(RDC_SHADE, iip ? 1 : 0);
}

void rd_aa1(int aa1)
{
    push1(RDC_AA1, aa1 ? 1 : 0);
}

/* ------------------------------------------------------------------ draws */

/* ------------------------------------------------------- the draw filter
 * rd.h rd_set_draw_filter: a set of object words (RD_KEY's objptr, the key
 * shifted right by 16), searched linearly (a few dozen at most). */
static struct {
    bool on, open;
    uint32_t n;
    uint64_t obj[RD_DRAW_FILTER_MAX];
} s_filter;

static bool filterHas(uint64_t o)
{
    for (uint32_t i = 0; i < s_filter.n; i++) {
        if (s_filter.obj[i] == o) {
            return true;
        }
    }
    return false;
}

static void filterAdd(uint64_t o)
{
    if (!filterHas(o) && s_filter.n < RD_DRAW_FILTER_MAX) {
        s_filter.obj[s_filter.n++] = o;
    }
}

void rd_set_draw_filter(bool on, const void *const *objs, uint32_t n)
{
    memset(&s_filter, 0, sizeof(s_filter));
    s_filter.on = on;
    for (uint32_t i = 0; on && objs && i < n; i++) {
        filterAdd(RD_KEY(objs[i], 0, 0) >> 16);
    }
}

void rd_draw_filter_open(bool open)
{
    s_filter.open = s_filter.on && open;
}

bool rd_draw_filter_keeps(RdKey key)
{
    return !s_filter.on || s_filter.open || (key != 0 && filterHas(key >> 16));
}

bool rd__draw_filter_pass(RdKey key)
{
    if (!s_filter.on) {
        return true;
    }
    if (s_filter.open) {
        if (key != 0) {
            filterAdd(key >> 16);
        }
        return true;
    }
    return key != 0 && filterHas(key >> 16);
}

void rd_screen_prims(RdPrim type, const RdScreenVtx *v, uint32_t count, RdSpace space, int uvFixed,
                     RdKey key)
{
    if (!v || count == 0 || (unsigned)type > RD_PRIM_SPRITES) {
        return;
    }
    if ((g_rd.spaceOverride > 0 ? g_rd.spaceOverride - 1 : (int)space) == RD_SPACE_WORLD &&
        !rd__draw_filter_pass(key)) {
        return;
    }
    RdFrame *f = rd__rec_frame();
    if (!f) {
        rd__push(RDC_SCREEN); /* reports once */
        return;
    }
    uint32_t off = rd__frame_payload(f, v, count * (uint32_t)sizeof(RdScreenVtx));
    RdCmd *c = rd__push(RDC_SCREEN);
    c->b[0] = (uint8_t)type;
    c->b[1] = (uint8_t)(g_rd.spaceOverride > 0 ? g_rd.spaceOverride - 1 : (int)space);
    /* RD_UV_FIXED_CONTINUOUS is kept (port UI text, no GS-pixel snap) */
    c->b[2] = uvFixed == RD_UV_FIXED_CONTINUOUS ? RD_UV_FIXED_CONTINUOUS : (uvFixed ? 1 : 0);
    c->u[0] = off;
    c->u[1] = count;
    c->b[3] = g_rd.textQuads ? RD_SCREEN_TEXT_QUADS : 0; /* deferred text's quads */
    c->b[4] = rd__frame_projected() ? RD_SCREEN_FRAME_CAMERA : 0;
    setKey(c, key);
    g_rd.stats.draws++;
}

/* ---------------------------------------------------------- deferred text */

void rd_deferred_text(const RdTextItem *item, RdKey key)
{
    RdFrame *f = rd__rec_frame();
    if (!item || !f) {
        if (item) {
            rd__push(RDC_OVERLAY_TEXT); /* reports once */
        }
        return;
    }
    RdTextItem it = *item;
    it.utf8[RD_TEXT_BYTES - 1] = '\0';
    it.pad[0] = it.pad[1] = 0;
    const uint32_t off = rd__frame_payload(f, &it, (uint32_t)sizeof(it));
    RdCmd *c = rd__push(RDC_OVERLAY_TEXT);
    c->b[0] = RD_OTEXT_ITEM;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(it);
    setKey(c, key);
    f->textItems++;
}

void rd_deferred_text_quads(int on)
{
    g_rd.textQuads = on ? 1 : 0;
}

/* rd_post.c: a post pass after deferred text in this frame (RdTextOp) */
void rd__deferred_text_op(uint8_t kind, const RdTextOp *op, RdKey key)
{
    RdFrame *f = rd__rec_frame();
    if (!f || !f->textItems || !op) {
        return;
    }
    const uint32_t off = rd__frame_payload(f, op, (uint32_t)sizeof(*op));
    RdCmd *c = rd__push(RDC_OVERLAY_TEXT);
    c->b[0] = RD_OTEXT_OP;
    c->b[1] = kind;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(*op);
    setKey(c, key);
}

int rd_set_space_override(int space)
{
    const int prev = g_rd.spaceOverride - 1;
    g_rd.spaceOverride = space < 0 || space > RD_SPACE_FULLSCREEN ? 0 : space + 1;
    return prev;
}

/* Records a stubbed draw: the payload holds the parts concatenated. */
static RdCmd *pushStub(uint8_t type, RdKey key, const void *const *parts, const uint32_t *sizes,
                       int n)
{
    if (!rd__draw_filter_pass(key)) {
        return NULL; /* rd_world_prims, rd_shadow_strip: world draws */
    }
    RdFrame *f = rd__rec_frame();
    if (!f) {
        rd__push(type);
        return NULL;
    }
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        total += (sizes[i] + 7u) & ~7u;
    }
    uint32_t off = rd__frame_payload(f, NULL, total);
    uint32_t at = off;
    for (int i = 0; i < n; i++) {
        if (parts[i] && sizes[i]) {
            memcpy(f->payload + at, parts[i], sizes[i]);
        }
        at += (sizes[i] + 7u) & ~7u;
    }
    RdCmd *c = rd__push(type);
    c->u[1] = off;
    c->u[2] = total;
    setKey(c, key);
    g_rd.stats.draws++;
    return c;
}

void rd_world_prims(RdPrim type, const RdWorldVtx *v, uint32_t count, const float *mtx, RdKey key)
{
    const void *parts[2] = {v, mtx};
    uint32_t sizes[2] = {v ? count * (uint32_t)sizeof(RdWorldVtx) : 0, mtx ? 64u : 0};
    RdCmd *c = pushStub(RDC_WORLD_PRIMS, key, parts, sizes, 2);
    if (c) {
        c->b[0] = (uint8_t)type;
        c->u[0] = count;
    }
}

void rd_shadow_strip(const float (*v)[4], uint32_t count, float sign, RdKey key)
{
    const void *parts[1] = {v};
    uint32_t sizes[1] = {v ? count * 16u : 0};
    RdCmd *c = pushStub(RDC_SHADOW_STRIP, key, parts, sizes, 1);
    if (c) {
        c->u[0] = count;
        c->f[0] = sign;
    }
}

/* ------------------------------------------------------- verification */

bool rd_dump_frame(const char *path)
{
    const RdFrame *f = rd__last_frame();
    return f ? rd__dump_frame(f, path) : false;
}

bool rd_read_display(void *dst, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__target_rec(RD_TARGET_DISPLAY + 1);
    if (!t || !dst) {
        return false;
    }
    return rd__read_target(rd_target(RD_TARGET_DISPLAY), dst, (size_t)t->tw * t->th * 4, w, h);
}

const RdStats *rd_get_stats(void)
{
    g_rd.stats.pipelines = rd__pipeline_count();
    return &g_rd.stats;
}
