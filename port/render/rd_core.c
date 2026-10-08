/* rd_core.c: the recording half of rd.h.
 *
 * A frame is 13 command lists plus a payload arena (rd_internal.h).  The
 * game fiber records into the list rd_SelectList chose; state calls record
 * deltas, they do not change anything at once.  rd_EndFrame closes the
 * frame, walks its state deltas in replay order (lists 0..12, or 11..12 for
 * a keep frame) from the state the previous frame left, and hands it to the
 * replayer, which applies the same deltas to the same persistent state
 * block in the same order.  So state leaks between lists, and from one frame
 * into the next, exactly as GS registers do (rd.h, "Defaults and leakage").
 *
 * The two most recent closed frames are retained (the current one and the
 * one before it) for the interpolation (rd_interp.c, wave 7 R7b); the frame
 * being recorded takes a third slot (RD_FRAME_RING).
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../fmv/rd_video.h"
#include "rd_internal.h"
#include "modelpack.h" /* v0.5.0 (M4): the model pack switch */
#include "rd_mesh.h"
#include "rd_tex.h"
#include "texpack.h"

RdContext g_rd;

static RdHostCall s_hostCall;

double rd__NowMs(void)
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

void rd_SetHostCall(RdHostCall call)
{
    s_hostCall = call;
}

void rd__OnHost(void (*fn)(void *arg), void *arg)
{
    if (s_hostCall) {
        s_hostCall(fn, arg);
    } else {
        fn(arg);
    }
}

static bool s_notImplementedFatal = true;

static uint32_t s_notImplementedCount;

/* ------------------------------------------------------------ diagnostics */

void rd__Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("rd: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void rd__LogOnce(int bit, const char *fmt, ...)
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

void rd__NotImplemented(const char *what)
{
    s_notImplementedCount++;
    fprintf(stderr, "rd: replay of %s is not implemented yet\n", what);
    if (s_notImplementedFatal) {
        assert(!"rd: command not implemented in this wave");
        abort();
    }
}

void rd__SetNotImplementedFatal(bool fatal)
{
    s_notImplementedFatal = fatal;
}

uint32_t rd__NotImplementedCount(void)
{
    return s_notImplementedCount;
}

/* ------------------------------------------------------------ state block */

void rd__ResetStateBlock(RdStateBlock *s)
{
    memset(s, 0, sizeof(*s));
    s->ds.test = rd_TestFromGs(RD_TEST_Z_ALWAYS);
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
            rd__LogOnce(RD_ONCE_FBMSK,
                        "FRAME.FBMSK 0x%08x masks part of a channel; the port masks whole channels",
                        fbmsk);
        }
        if (bits != 0xFF) {
            m |= (uint8_t)(1u << c);
        }
    }
    return m;
}

bool rd__ApplyState(RdStateBlock *s, const RdCmd *c)
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

void rd__FrameReset(RdFrame *f)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        f->lists[l].count = 0;
    }
    f->payloadSize = 0;
    for (uint32_t i = 0; i < f->tempCount; i++) {
        /* package PHOTO: a target the pinned copy of this frame still names
         * is the pin's to free */
        if (!rd__PhotoAdoptTemp(f->tempTargets[i])) {
            rd__TempTargetFree(f->tempTargets[i]);
        }
    }
    f->tempCount = 0;
    rd__WaterFrameReset(f); /* R5b: block targets, aliases, camera scopes */
    f->hasCamera = 0;
    f->hasVu = 0;     /* R2c */
    f->headValid = 0; /* R2c */
    f->cut = 0;       /* R7b */
    f->fade = 0;
    f->textItems = 0; /* package DEF */
    f->closed = 0;
    f->keep = 0;
    f->number = 0;
}

void rd__FrameFree(RdFrame *f)
{
    rd__FrameReset(f);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(f->lists[l].cmds);
        f->lists[l].cmds = NULL;
        f->lists[l].cap = 0;
    }
    free(f->payload);
    f->payload = NULL;
    f->payloadCap = 0;
}

uint32_t rd__FramePayload(RdFrame *f, const void *data, uint32_t size)
{
    uint32_t off = (f->payloadSize + 7u) & ~7u;
    uint32_t end = off + size;
    if (end < off || end > 0x80000000u) {
        /* past 2 GiB the doubling below would wrap to 0 and never end */
        rd__Log("payload arena: %u + %u bytes is past 2 GiB", off, size);
        abort();
    }
    if (end > f->payloadCap) {
        uint32_t cap = f->payloadCap ? f->payloadCap : 64 * 1024;
        while (cap < end) {
            cap *= 2;
        }
        uint8_t *p = realloc(f->payload, cap);
        if (!p) {
            rd__Log("payload arena: out of memory (%u bytes)", cap);
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

void rd__Walk(const RdFrame *f, int keep, RdStateBlock *state, RdWalkFn fn, void *user)
{
    for (int l = rd__FirstList(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            rd__ApplyState(state, &cl->cmds[i]);
            if (fn) {
                fn(user, l, i, &cl->cmds[i], state);
            }
        }
    }
}

RdFrame *rd__RecFrame(void)
{
    return g_rd.recIndex >= 0 ? &g_rd.frames[g_rd.recIndex] : NULL;
}

RdCmd *rd__Push(uint8_t type)
{
    RdFrame *f = rd__RecFrame();
    if (!f) {
        rd__LogOnce(RD_ONCE_OUTSIDE_FRAME,
                    "rd call outside rd_BeginFrame/rd_EndFrame dropped (command %u)",
                    (unsigned)type);
        return NULL;
    }
    RdCmdList *cl = &f->lists[g_rd.list];
    if (cl->count == cl->cap) {
        uint32_t cap = cl->cap ? cl->cap * 2 : 256;
        RdCmd *p = realloc(cl->cmds, (size_t)cap * sizeof(RdCmd));
        if (!p) {
            rd__Log("command list: out of memory");
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

/* wave 5 (R5a): the work buffers' resolution scale; applied since wave 7
 * (R7a), when the replay sizes the GS window apart from the target's
 * texture (RdTargetRec.tw/th/sx/sy, rd__TargetScaleOf) */
#define RD_WORK_SCALE_APPLY 1

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
    case RD_TARGET_WORK3: /* wave 5 (R5a): TBP 0x3000, 256 x 128 */
        *w = 256;
        *h = 128;
        break;
    case RD_TARGET_WORK2: /* wave 5 (R5a): TBP 0x2E00, the scene-sized flare mask */
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
    /* wave 5 (R5a): the work buffers' resolution scale (rd.h
     * rd_WorkTargetScale) is the texture's, not the GS size's, since wave 7
     * (R7a): rd__TargetScaleOf */
}

/* Wave 7 (R7a): the scene-class targets, which take the Enhanced scene
 * resolution and (SCENE, WORK2, AURA_WORK) the wide projection: the
 * scene-sized buffers and DISPLAY.  The other named targets are the fixed
 * work buffers, scaled by rd_WorkTargetScale. */
static int sceneClass(int id)
{
    return id == RD_TARGET_SCENE || id == RD_TARGET_DISPLAY || id == RD_TARGET_WORK2 ||
           id == RD_TARGET_AURA_WORK || id == RD_TARGET_DATE_SNAPSHOT;
}

/* Package V3: Shadow.c's blur levels (SHADOW0..2) keep the PS2 sizes at
 * every scale.  The shadow's blur is not a GS distance there but the levels'
 * resolution itself (each level a bilinear half of the one before, 256, 128
 * and 64 texels over the screen, composited back with bilinear
 * magnification), so levels at the work scale halved the penumbra: at 4x
 * the softest level needed a further Gaussian of about 4 GS pixels to match
 * the Original preset's (package V3). */
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
void rd__TargetScaleOf(RdTargetRec *t, int named)
{
    float sx = 1.0f, sy = 1.0f;
    int scene = named >= 0 ? sceneClass(named) : (t->w == g_rd.gsW && t->h == g_rd.gsH);
    if (scene) {
        sx = g_rd.sceneSx > 0.0f ? g_rd.sceneSx : 1.0f;
        sy = g_rd.sceneSy > 0.0f ? g_rd.sceneSy : 1.0f;
        if (named == RD_TARGET_DISPLAY && g_rd.fullHeight) {
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
    t->wide = (scene && named != RD_TARGET_DISPLAY && named != RD_TARGET_DATE_SNAPSHOT) || block;
    t->wideBlock = (uint8_t)block;
}

float rd_WorkTargetScale(uint32_t sceneHeight)
{
    if (sceneHeight <= 448) {
        return 1.0f; /* the GS height (1x): the literal PS2 sizes */
    }
    float k = (float)sceneHeight / 448.0f;
    return k < 1.0f ? 1.0f : (k > 2.0f ? 2.0f : k);
}

static const char *const s_targetNames[RD_TARGET_COUNT] = {
    "SCENE",         "DISPLAY",   "SHADOW0",  "SHADOW1",   "SHADOW2",  "WORK0",
    "WORK1",         "WORK2",     "WORK3",    "AA0",       "AA1",      "FEED128",
    "DATE_SNAPSHOT", "AURA_WORK", "AURA_TAP", "WORK2_PAD", "FEED_HELD"};

RdTargetRec *rd__TargetRec(uint32_t id)
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

bool rd__TargetCreateGpu(RdTargetRec *t, const char *name)
{
    if (!g_rd.hasDevice) {
        return true;
    }
    uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST;
    t->color = rhi_CreateTexture(&(RhiTextureDesc){t->tw, t->th, 1, t->format, usage, name});
    t->colorState = RHI_STATE_UNDEFINED;
    if (t->withDepth) {
        t->depth = rhi_CreateTexture(&(RhiTextureDesc){
            t->tw, t->th, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_SRC, name});
        t->depthState = RHI_STATE_UNDEFINED;
    }
    return t->color.id != 0 && (!t->withDepth || t->depth.id != 0);
}

void rd__TargetDestroyGpu(RdTargetRec *t)
{
    if (!g_rd.hasDevice) {
        return;
    }
    if (t->color.id) {
        rhi_DestroyTexture(t->color);
    }
    if (t->depth.id) {
        rhi_DestroyTexture(t->depth);
    }
    if (t->snap.id) {
        rhi_DestroyTexture(t->snap);
    }
    t->color = t->depth = t->snap = (RhiTexture){0};
}

static void createNamedTargets(void)
{
    for (int i = 0; i < RD_TARGET_COUNT; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (t->live) {
            rd__TargetDestroyGpu(t);
        }
        uint32_t keepViews[3];
        memcpy(keepViews, t->viewTex, sizeof(keepViews));
        memset(t, 0, sizeof(*t));
        memcpy(t->viewTex, keepViews, sizeof(keepViews));
        t->live = 1;
        t->named = 1;
        namedTargetDesc(i, g_rd.gsW, g_rd.gsH, &t->w, &t->h, &t->format, &t->withDepth);
        rd__TargetScaleOf(t, i);
        if (!rd__TargetCreateGpu(t, s_targetNames[i])) {
            rd__Log("could not create target %s", s_targetNames[i]);
        }
    }
}

/* Package P1: temporary targets come from a pool.  A freed record keeps
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

uint32_t rd__TempTargetAlloc(uint32_t w, uint32_t h, int withDepth, int keepAcross)
{
    RdTargetRec want;
    memset(&want, 0, sizeof(want));
    want.w = w ? w : 1;
    want.h = h ? h : 1;
    want.format = RHI_FMT_RGBA8_UNORM;
    want.withDepth = withDepth ? 1 : 0;
    rd__TargetScaleOf(&want, -1);
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
        rd__LogOnce(RD_ONCE_TEMP_FULL, "out of temporary targets (%d)",
                    RD_MAX_TARGETS - RD_TARGET_COUNT);
        return 0;
    }
    RdTargetRec *t = &g_rd.targets[pick];
    RdTargetRec keep = *t;
    if (!reuse && t->parked) {
        rd__TargetDestroyGpu(t);
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
    } else {
        rd__TargetCreateGpu(t, "temp target");
    }
    t->clearPending = 1;
    g_rd.stats.tempTargets++;
    return (t->gen << 16) | (uint32_t)(pick + 1);
}

void rd__TempTargetFree(uint32_t id)
{
    RdTargetRec *t = rd__TargetRec(id);
    if (!t || t->named) {
        return;
    }
    for (int v = 0; v < 3; v++) {
        if (t->viewTex[v]) {
            rd_DestroyTexture((RdTex){t->viewTex[v]});
        }
    }
    /* P1: parked with its textures for the next allocation of its size,
     * up to RD_TEMP_PARKED of them (more are sizes no frame asks for any
     * more: destroyed) */
    uint32_t parked = 0;
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        parked += g_rd.targets[i].parked;
    }
    if (parked >= RD_TEMP_PARKED) {
        rd__TargetDestroyGpu(t);
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

void rd__TempTargetPoolClear(void)
{
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (!t->live && t->parked) {
            rd__TargetDestroyGpu(t);
            const uint32_t gen = t->gen;
            memset(t, 0, sizeof(*t));
            t->gen = gen;
        }
    }
}

RdTarget rd_Target(RdTargetId id)
{
    if ((int)id < 0 || id >= RD_TARGET_COUNT) {
        return (RdTarget){0};
    }
    return (RdTarget){(uint32_t)id + 1};
}

RdTarget rd_TempTarget(uint32_t gsW, uint32_t gsH, int withDepth, int keepAcrossFrames)
{
    uint32_t id = rd__TempTargetAlloc(gsW, gsH, withDepth, keepAcrossFrames);
    RdFrame *f = rd__RecFrame();
    if (id && !keepAcrossFrames) {
        if (f && f->tempCount < RD_MAX_TEMP_PER_FRAME) {
            f->tempTargets[f->tempCount++] = id;
        } else {
            rd__Log("rd_TempTarget outside a frame or past %d per frame: it is never freed",
                    RD_MAX_TEMP_PER_FRAME);
        }
    }
    return (RdTarget){id};
}

void rd_SetTarget(RdTarget color, RdTarget depth, uint32_t gsW, uint32_t gsH, int useOffset)
{
    /* R5b: a target rd_AliasTarget stands in for, with its own depth buffer */
    uint32_t alias = rd__AliasOf(color.id);
    if (alias != 0) {
        const RdTargetRec *a = rd__TargetRec(alias);
        if (depth.id == 0 || depth.id == color.id) {
            depth.id = a != NULL && a->withDepth ? alias : 0;
        }
        color.id = alias;
    }
    RdCmd *c = rd__Push(RDC_TARGET);
    if (!c) {
        return;
    }
    c->u[0] = color.id;
    c->u[1] = depth.id;
    c->u[2] = (gsW & 0xFFFF) | ((gsH & 0xFFFF) << 16);
    /* bit 0 RD_TARGET_OFFSET, bit 1 RD_TARGET_HALF_Y (R2c, the flip's half offset) */
    c->b[0] = (uint8_t)(useOffset & (RD_TARGET_OFFSET | RD_TARGET_HALF_Y));
}

void rd_ClearTarget(RdTarget t, const uint8_t rgba[4], int clearDepth, uint32_t z)
{
    if (rd__AliasOf(t.id) != 0) { /* R5b */
        t.id = rd__AliasOf(t.id);
    }
    RdCmd *c = rd__Push(RDC_CLEAR);
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

RdTexRec *rd__TexRec(uint32_t id)
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
    rd__Log("out of texture slots (%d)", RD_MAX_TEXTURES);
    return 0;
}

/* R8: the whole texture changed (a create, rd_UpdateTexture) */
static void texDirtyAll(RdTexRec *t)
{
    if (!t->dirty) {
        g_rd.texDirtyCount++; /* P1 */
    }
    t->dirty = 1;
    t->dirtyX0 = t->dirtyY0 = 0;
    t->dirtyX1 = t->w;
    t->dirtyY1 = t->h;
}

RdTex rd__CreateTextureFmt(uint32_t w, uint32_t h, const void *px, uint8_t format, RdTexSrc src,
                           const char *debugName)
{
    if (!g_rd.inited || w == 0 || h == 0 || format >= RD_TEXEL_COUNT) {
        return (RdTex){0};
    }
    uint32_t id = texAlloc();
    RdTexRec *t = rd__TexRec(id);
    if (!t) {
        return (RdTex){0};
    }
    const size_t bytes = (size_t)w * h * rd__TexelBytes(format);
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

RdTex rd__CreateTextureReplacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
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
    RdTexRec *t = rd__TexRec(id);
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

void rd__FreePending(RdTexRec *t)
{
    if (t && t->pending) {
        texpack_FreeImage(t->pending);
        free(t->pending);
        t->pending = NULL;
    }
}

RdTex rd_CreateTextureSrc(uint32_t w, uint32_t h, const void *rgba8, RdTexSrc src,
                          const char *debugName)
{
    return rd__CreateTextureFmt(w, h, rgba8, RD_TEXEL_RGBA8, src, debugName);
}

RdTex rd_CreateTextureR8(uint32_t w, uint32_t h, const uint8_t *cov, const char *debugName)
{
    return rd__CreateTextureFmt(w, h, cov, RD_TEXEL_R8, RD_TEXSRC_RGBA32, debugName);
}

RdTex rd_CreateTexture(uint32_t w, uint32_t h, const void *rgba8, RdTexA texaMode,
                       const char *debugName)
{
    RdTex r = rd_CreateTextureSrc(w, h, rgba8, RD_TEXSRC_RGBA32, debugName);
    RdTexRec *t = rd__TexRec(r.id);
    if (t) {
        t->bakedTexa = (uint8_t)texaMode;
    }
    return r;
}

void rd_UpdateTexture(RdTex tex, const void *rgba8)
{
    RdTexRec *t = rd__TexRec(tex.id);
    if (!t || t->kind != RD_TEXKIND_IMAGE || !rgba8 || !t->pixels) {
        return; /* a pack replacement has no CPU texels to update */
    }
    const size_t bytes = (size_t)t->w * t->h * rd__TexelBytes(t->format);
    /* P1: an update that changes nothing (a page or CLUT re-expanded to the
     * same texels) is not uploaded again */
    if (memcmp(t->pixels, rgba8, bytes) == 0) {
        return;
    }
    memcpy(t->pixels, rgba8, bytes);
    texDirtyAll(t);
    g_rd.texFullUpdates++;
}

void rd_UpdateTextureRect(RdTex tex, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void *px)
{
    RdTexRec *t = rd__TexRec(tex.id);
    if (!t || t->kind != RD_TEXKIND_IMAGE || !px || !t->pixels || x >= t->w || y >= t->h || !w ||
        !h) {
        return;
    }
    const uint32_t bpp = rd__TexelBytes(t->format);
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
        return; /* P1's rule: nothing to upload */
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

void rd_DestroyTexture(RdTex tex)
{
    RdTexRec *t = rd__TexRec(tex.id);
    if (!t) {
        return;
    }
    if (t->kind == RD_TEXKIND_TARGET) {
        RdTargetRec *tr = rd__TargetRec(t->target);
        if (tr && t->view < 3 && tr->viewTex[t->view] == tex.id) {
            tr->viewTex[t->view] = 0;
        }
    }
    if (t->rhi.id && g_rd.hasDevice) {
        rhi_DestroyTexture(t->rhi);
    }
    if (t->dirty && g_rd.texDirtyCount) {
        g_rd.texDirtyCount--; /* P1 */
    }
    free(t->pixels);
    rd__FreePending(t); /* a replacement destroyed before its upload */
    uint32_t gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->gen = gen;
}

RdTex rd_TargetTexture(RdTarget target, RdTexView view)
{
    RdTargetRec *tr = rd__TargetRec(target.id);
    if (!tr || (unsigned)view > RD_VIEW_DEPTH) {
        return (RdTex){0};
    }
    if (tr->viewTex[view] && rd__TexRec(tr->viewTex[view])) {
        return (RdTex){tr->viewTex[view]};
    }
    uint32_t id = texAlloc();
    RdTexRec *t = rd__TexRec(id);
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

/* ----------------------------------------------------------------- meshes */

RdMesh rd_CreateMesh(const RdMeshDesc *desc)
{
    if (!g_rd.meshes || !desc) {
        return (RdMesh){0};
    }
    for (uint32_t i = 0; i < RD_MAX_MESHES; i++) {
        RdMeshRec *m = &g_rd.meshes[i];
        if (m->live) {
            continue;
        }
        uint32_t gen = (m->gen + 1) & 0xFFFF;
        memset(m, 0, sizeof(*m));
        m->gen = gen ? gen : 1;
        m->live = 1;
        m->vertexCount = desc->vertexCount;
        m->stripCount = desc->stripCount;
        m->materialCount = desc->materialCount;
        /* wave 3 (R3ab): the seki layer builds VU meshes (rd_CreateVuMesh,
           rd_mesh.h); this semantic description is kept for an Enhanced
           path and holds no geometry */
        return (RdMesh){(m->gen << 16) | (i + 1)};
    }
    return (RdMesh){0};
}

void rd_DestroyMesh(RdMesh mesh)
{
    RdMeshRec *m = rd__MeshRec(mesh.id);
    if (m && m->vu) {
        rd_DestroyVuMesh(mesh);
    } else if (m) {
        m->live = 0;
    }
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
    g_rd.texLevelsFilter = -1; /* P1 */
    rd__ResetStateBlock(&g_rd.persistent);
    rd__VuInit(); /* wave 3: the per-list VU images */
    g_rd.inited = true;
}

bool rd__InitRecordOnly(uint32_t gsW, uint32_t gsH)
{
    if (g_rd.inited) {
        rd_Shutdown();
    }
    initCommon(gsW, gsH, NULL);
    if (!g_rd.textures || !g_rd.meshes) {
        return false;
    }
    rd__ApplyDisplay();
    createNamedTargets();
    return true;
}

/* Frame dumps every N replayed frames (wave 3): ico-pc.ini dump_every= and
 * dump_dir=, handed over by port/platform/host_config.c in the environment
 * (ICO_RD_DUMP_EVERY, ICO_RD_DUMP_DIR), read at rd_Init. */
static uint32_t s_dumpEvery;

static char s_dumpDir[512];

/* Wave 7 (R7b): ICO_RD_DUMP_INTERP=1 (since R7d the hand-over of the config
 * key [dev] dump_interp, port/platform/host_config.c) also dumps, next to
 * each frame dump, the frame interpolated half way from the one before
 * (rd-NNNNN-i50.rddump; rd__InterpFrame at alpha 0.5, the feedback passes as
 * a first present) */
static int s_dumpInterp;

/* S2: ICO_RD_DUMP_FROM (config key [dev] dump_from): no frame numbered
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
        rd__Log("dumping every %u frames from frame %u into %s%s", s_dumpEvery, s_dumpFrom,
                s_dumpDir, s_dumpInterp ? ", with the frame interpolated half way" : "");
    }
}

bool rd_Init(uint32_t gsWidth, uint32_t gsHeight, const RdSettings *settings, void *sdlWindow)
{
    if (g_rd.inited) {
        rd_Shutdown();
    }
    initCommon(gsWidth, gsHeight, settings);
    if (!g_rd.textures || !g_rd.meshes) {
        return false;
    }
    if (!rd__GpuInit(sdlWindow)) {
        rd__GpuShutdown();
        free(g_rd.textures);
        free(g_rd.meshes);
        memset(&g_rd, 0, sizeof(g_rd));
        return false;
    }
    g_rd.hasDevice = true;
    /* package AN-E: the two-pass blend without dual-source blending (a
     * device without dualSrcBlend; ICO_RD_NO_DUAL=1 forces it for tests) */
    const char *noDual = getenv("ICO_RD_NO_DUAL");
    g_rd.noDual = !rhi_Limits()->dualSourceBlend || (noDual && noDual[0] && noDual[0] != '0');
    if (g_rd.noDual) {
        rd__Log("blend: two-pass fallback (no dualSrcBlend)");
    }
    rd__ApplyDisplay(); /* wave 7 (R7a): the scales the named targets take */
    createNamedTargets();
    readDumpConfig();
    return true;
}

void rd_Shutdown(void)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.hasDevice) {
        rhi_WaitIdle();
    }
    rd__PhotoShutdown(); /* package PHOTO: the pin first, then the frames it named */
    for (int i = 0; i < RD_FRAME_RING; i++) {
        rd__FrameFree(&g_rd.frames[i]);
    }
    rd__InterpShutdown(); /* R7b */
    for (int i = 0; i < RD_MAX_TARGETS; i++) {
        if (g_rd.targets[i].live || g_rd.targets[i].parked) {
            rd__TargetDestroyGpu(&g_rd.targets[i]);
        }
    }
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        RdTexRec *t = &g_rd.textures[i];
        if (t->live) {
            if (t->rhi.id && g_rd.hasDevice) {
                rhi_DestroyTexture(t->rhi);
            }
            free(t->pixels);
            rd__FreePending(t);
        }
    }
    if (g_rd.hasDevice) {
        rd_VideoShutdown(); /* the FMV path's objects (rd_video.c), before the device */
        rd__PresentShutdown();
        rd__GpuShutdown();
    }
    rd__PipelineCacheClear();
    rd__MeshShutdown();
    rd__VuShutdown();
    free(g_rd.textures);
    free(g_rd.meshes);
    memset(&g_rd, 0, sizeof(g_rd));
}

void rd_ResetScene(uint32_t gsWidth, uint32_t gsHeight)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.hasDevice) {
        rhi_WaitIdle();
    }
    g_rd.gsW = gsWidth;
    g_rd.gsH = gsHeight;
    rd__ApplyDisplay();
    createNamedTargets();
    rd__TempTargetPoolClear(); /* P1: the parked ones have the old scene size */
    for (int v = 0; v < 3; v++) {
        for (int i = 0; i < RD_TARGET_COUNT; i++) {
            RdTexRec *t = rd__TexRec(g_rd.targets[i].viewTex[v]);
            if (t) {
                t->w = g_rd.targets[i].w;
                t->h = g_rd.targets[i].h;
            }
        }
    }
}

void rd_SetSettings(const RdSettings *settings)
{
    if (settings) {
        g_rd.pendingSettings = *settings;
        g_rd.settingsPending = true;
    }
}

const RdSettings *rd_GetSettings(void)
{
    return &g_rd.settings;
}

void rd_SetMirror(int on)
{
    if (g_rd.mirrorRun != (on != 0)) {
        rd__Log("mirror mode %s (frame %u)", on ? "on" : "off", g_rd.frameCounter);
    }
    g_rd.mirrorRun = on != 0;
}

bool rd_MirrorActive(void)
{
    return rd__MirrorOn();
}

bool rd_SetNoDual(bool on)
{
    if (!on && g_rd.hasDevice && !rhi_Limits()->dualSourceBlend) {
        return false; /* the device has no dual-source blending: the fallback stays */
    }
    if (g_rd.noDual != on) {
        rd__Log("blend: two-pass fallback %s", on ? "on" : "off");
    }
    g_rd.noDual = on;
    return true;
}

bool rd_NoDual(void)
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
        rd_SelectList(l);
        rd_TestGs(d->test);
        rd_ZWrite(d->zwrite);
        rd_FBA(0);
        rd_TexA(d->texa);
    }
    rd_SelectList(0);
}

static void recreateTargets(void *arg)
{
    (void)arg;
    rhi_WaitIdle();
    createNamedTargets();
    rd__TempTargetPoolClear(); /* P1: the parked textures have the old scale */
}

typedef struct ReplayCall {
    const RdFrame *f;
    int keep;
} ReplayCall;

static void replayOnHost(void *arg)
{
    const ReplayCall *c = (const ReplayCall *)arg;
    rd__ReplayFrame(c->f, c->keep, true);
}

void rd_BeginFrame(void)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.recIndex >= 0) {
        rd__Log("rd_BeginFrame without rd_EndFrame: the open frame is discarded");
    }
    if (g_rd.settingsPending) {
        /* v0.4.0: the texture pack switched off: the originals back (once
         * per edge; rd_tex.h rdtex_RevertReplacements) */
        const bool packOff = g_rd.settings.texturePack && !g_rd.pendingSettings.texturePack;
        /* v0.5.0 (M4): the model pack's switch and its dump, on a change */
        const bool modelsChanged = g_rd.settings.modelPack != g_rd.pendingSettings.modelPack;
        const bool dumpChanged = g_rd.settings.dumpModels != g_rd.pendingSettings.dumpModels;
        g_rd.settings = g_rd.pendingSettings;
        g_rd.settingsPending = false;
        if (packOff) {
            rdtex_RevertReplacements();
        }
        if (modelsChanged) {
            modelpack_SetEnabled(g_rd.settings.modelPack != 0);
        }
        if (dumpChanged) {
            modelpack_SetDumpEnabled(g_rd.settings.dumpModels != 0);
        }
        /* wave 7 (R7a): the Settings menu applies here; a change of the
         * targets' scales recreates them (their content is lost: the next
         * frame redraws SCENE; DISPLAY's motion-blur history restarts) */
        if (rd__ApplyDisplay() && g_rd.hasDevice) {
            rd__Log("display: targets recreated at scene %gx%g, work %g", (double)g_rd.sceneSx,
                    (double)g_rd.sceneSy, (double)g_rd.workScale);
            rd__OnHost(recreateTargets, NULL);
            /* R7b: the retained frames' history is dropped: the frame opened
             * now and the next are the first pair interpolated */
            g_rd.interpFloor = g_rd.frameCounter + 1;
        }
    }
    /* R7b: the slot after the last closed frame, which is neither it nor
     * the one before it */
    int idx = g_rd.lastIndex < 0 ? 0 : (g_rd.lastIndex + 1) % RD_FRAME_RING;
    RdFrame *f = &g_rd.frames[idx];
    rd__FrameReset(f);
    f->number = ++g_rd.frameCounter;
    rd__VuMeshSweepStale(); /* v0.5.0 (M0): retired meshes no kept frame drew */
    f->startState = g_rd.persistent;
    f->gsW = g_rd.gsW;
    f->gsH = g_rd.gsH;
    g_rd.recIndex = idx;
    g_rd.stats.draws = 0;
    f->cut = g_rd.cutPending; /* R7b: rd_CameraCut between frames */
    g_rd.cutPending = 0;
    recordDefaults();
}

void rd_EndFrame(int keep)
{
    RdFrame *f = rd__RecFrame();
    if (!f) {
        return;
    }
    f->keep = keep ? 1 : 0;
    if (f->cut) {
        f->camera.cut = 1; /* R7b */
    }
    rd__FrameHeadResolve(f, f->keep); /* R2c: the flip's head in the first replayed list */
    f->closed = 1;
    RdStateBlock s = f->startState;
    rd__Walk(f, f->keep, &s, NULL, NULL);
    f->endState = s;
    g_rd.persistent = s;
    g_rd.lastIndex = g_rd.recIndex;
    g_rd.recIndex = -1;
    g_rd.stats.bytesPayload = f->payloadSize;
    g_rd.videoShown = 0; /* R7b */
    /* R7b: with interpolation the host presents (rd_Present); otherwise,
     * the Original preset always, the frame is replayed and presented once
     * here, as before */
    /* package PHOTO: with the camera override on, the scene frames are
     * pinned, and the present below replays the pin through the override */
    rd__PhotoPin(f);
    if (g_rd.hasDevice && !rd_InterpolationActive()) {
        const RdFrame *pf = rd__PhotoPresentFrame();
        ReplayCall c = {pf ? pf : f, pf ? 0 : f->keep};
        rd__OnHost(replayOnHost, &c);
    }
    if (s_dumpEvery && f->number % s_dumpEvery == 0 && f->number >= s_dumpFrom) {
        char path[600];
        snprintf(path, sizeof(path), "%s/rd-%05u.rddump", s_dumpDir, f->number);
        if (rd__DumpFrame(f, path)) {
            rd__Log("frame %u dumped to %s", f->number, path);
        }
        if (s_dumpInterp) {
            RdInterpStats st;
            /* S6: the previous frame too (rd-NNNNN-prev.rddump), so a
             * half-way dump can be checked against both its ticks */
            const RdFrame *pv = rd__PrevFrame();
            snprintf(path, sizeof(path), "%s/rd-%05u-prev.rddump", s_dumpDir, f->number);
            if (pv && pv->closed && pv->number + 1 == f->number) {
                (void)rd__DumpFrame(pv, path);
            }
            const RdFrame *i = rd__InterpFrame(pv, f, 0.5f, 1.0f, 1, &st);
            snprintf(path, sizeof(path), "%s/rd-%05u-i50.rddump", s_dumpDir, f->number);
            if (i && rd__DumpFrame(i, path)) {
                rd__Log("frame %u interpolated half way (snap %u, %u keyed draws: %u blended, "
                        "%u unmatched, %u mismatched, %u jumped; %u mesh streams kept or "
                        "blended; %u blended as rotations; %u VU draws through the blended "
                        "camera, %u of them the tick's) dumped to %s",
                        f->number, st.snap, st.keyed, st.lerped, st.missing, st.mismatch, st.jump,
                        st.morph, st.rotated, st.rebased, st.rebasedCur, path);
            }
        }
    }
}

void rd_DiscardFrame(void)
{
    RdFrame *f = rd__RecFrame();
    if (!f) {
        return;
    }
    /* the state the dropped lists would have left is not applied: nothing
     * of them reached the GS */
    rd__FrameReset(f);
    g_rd.recIndex = -1;
}

bool rd_FrameOpen(void)
{
    return rd__RecFrame() != NULL;
}

void rd_SetCamera(const RdCamera *cam)
{
    RdFrame *f = rd__RecFrame();
    if (f && cam) {
        f->camera = *cam;
        f->hasCamera = 1;
    }
}

void rd_CameraCut(void)
{
    RdFrame *f = rd__RecFrame();
    if (f) {
        f->cut = 1;
    } else {
        g_rd.cutPending = 1;
    }
}

uint32_t rd_FrameNumber(void)
{
    const RdFrame *f = rd__LastFrame();
    return f && f->closed ? f->number : 0;
}

const RdFrame *rd__LastFrame(void)
{
    return g_rd.lastIndex >= 0 ? &g_rd.frames[g_rd.lastIndex] : NULL;
}

const RdFrame *rd__PrevFrame(void)
{
    if (g_rd.lastIndex < 0) {
        return NULL;
    }
    /* R7b: the slot before the last closed one in the ring */
    const RdFrame *f = &g_rd.frames[(g_rd.lastIndex + RD_FRAME_RING - 1) % RD_FRAME_RING];
    const RdFrame *last = &g_rd.frames[g_rd.lastIndex];
    return f->closed && f->number < last->number ? f : NULL;
}

/* ------------------------------------------------------------------ lists */

void rd_SelectList(int list)
{
    if (list < 0 || list >= RD_LIST_COUNT) {
        rd__LogOnce(RD_ONCE_LIST_RANGE, "rd_SelectList(%d) out of range", list);
        return;
    }
    g_rd.list = list;
}

int rd_CurrentList(void)
{
    return g_rd.list;
}

/* ----------------------------------------------------------- state deltas */

static void push1(uint8_t type, int v)
{
    RdCmd *c = rd__Push(type);
    if (c) {
        c->b[0] = (uint8_t)v;
    }
}

void rd_Blend(RdBlend eq, uint8_t fix, int abe)
{
    if ((unsigned)eq >= RD_BLEND_COUNT) {
        rd__LogOnce(RD_ONCE_BLEND_RANGE, "rd_Blend(%d) out of range: blending off", (int)eq);
        eq = RD_BLEND_LERP_AS;
        abe = 0;
    }
    RdCmd *c = rd__Push(RDC_BLEND);
    if (c) {
        c->b[0] = (uint8_t)eq;
        c->b[1] = fix;
        c->b[2] = abe ? 1 : 0;
    }
}

void rd_Test(const RdTestState *t)
{
    RdCmd *c = rd__Push(RDC_TEST);
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

void rd_TestGs(uint64_t gsTestWord)
{
    RdTestState t = rd_TestFromGs(gsTestWord);
    rd_Test(&t);
}

void rd_ZWrite(int on)
{
    push1(RDC_ZWRITE, on ? RD_ZWRITE_ON : RD_ZWRITE_OFF);
}

void rd_FBA(int on)
{
    push1(RDC_FBA, on ? 1 : 0);
}

void rd_PABE(int on)
{
    push1(RDC_PABE, on ? 1 : 0);
}

void rd_ColClamp(int on)
{
    push1(RDC_COLCLAMP, on ? 1 : 0);
}

void rd_TexA(RdTexA mode)
{
    push1(RDC_TEXA, (int)mode < RD_TEXA_COUNT ? (int)mode : RD_TEXA_80_80);
}

void rd__RecFilter(RdFilter mag, RdFilter min)
{
    RdCmd *c = rd__Push(RDC_FILTER);
    if (c) {
        c->b[0] = (uint8_t)mag;
        c->b[1] = (uint8_t)min;
    }
}

void rd_Sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t)
{
    rd_SamplerFilter(mag, min);
    rd_SamplerWrap(s, t);
}

void rd_Texture(RdTex tex, RdTexFn fn, RdTcc tcc)
{
    /* R5b: a view of a target rd_AliasTarget stands in for */
    const RdTexRec *tr = rd__TexRec(tex.id);
    if (tr != NULL && tr->kind == RD_TEXKIND_TARGET && rd__AliasOf(tr->target) != 0) {
        tex = rd_TargetTexture((RdTarget){rd__AliasOf(tr->target)}, (RdTexView)tr->view);
    }
    RdCmd *c = rd__Push(RDC_TEXTURE);
    if (c) {
        c->u[0] = tex.id;
        c->b[0] = (uint8_t)fn;
        c->b[1] = (uint8_t)tcc;
    }
}

void rd_TextureOff(void)
{
    rd__Push(RDC_TEXTURE_OFF);
}

void rd_UVOffset(float u, float v)
{
    RdCmd *c = rd__Push(RDC_UVOFFSET);
    if (c) {
        c->f[0] = u;
        c->f[1] = v;
    }
}

void rd_ColorMask(uint32_t fbmsk)
{
    RdCmd *c = rd__Push(RDC_COLORMASK);
    if (c) {
        c->u[0] = fbmsk;
    }
}

void rd__RecScissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    RdCmd *c = rd__Push(RDC_SCISSOR);
    if (c) {
        c->u[0] = (uint32_t)x0;
        c->u[1] = (uint32_t)y0;
        c->u[2] = (uint32_t)x1;
        c->u[3] = (uint32_t)y1;
    }
}

void rd__RecABE(int abe)
{
    push1(RDC_ABE, abe ? 1 : 0);
}

void rd_ABE(int abe)
{
    rd__RecABE(abe);
}

void rd_BlendFunc(RdBlend eq, uint8_t fix)
{
    if ((unsigned)eq >= RD_BLEND_COUNT) {
        rd__LogOnce(RD_ONCE_BLEND_RANGE, "rd_BlendFunc(%d) out of range: ignored", (int)eq);
        return;
    }
    RdCmd *c = rd__Push(RDC_ALPHA);
    if (c) {
        c->b[0] = (uint8_t)eq;
        c->b[1] = fix;
    }
}

void rd_Scissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    rd__RecScissor(x0, y0, x1, y1);
}

void rd_SamplerFilter(RdFilter mag, RdFilter min)
{
    rd__RecFilter(mag, min);
}

void rd_SamplerWrap(RdWrap s, RdWrap t)
{
    RdCmd *c = rd__Push(RDC_WRAP);
    if (c) {
        c->b[0] = (uint8_t)s;
        c->b[1] = (uint8_t)t;
    }
}

void rd_Gouraud(int iip)
{
    push1(RDC_SHADE, iip ? 1 : 0);
}

void rd_AA1(int aa1)
{
    push1(RDC_AA1, aa1 ? 1 : 0);
}

/* ------------------------------------------------------------------ draws */

/* --------------------------------------------- the draw filter (package MV)
 * rd.h rd_SetDrawFilter: a set of object words (RD_KEY's objptr, the key
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

void rd_SetDrawFilter(bool on, const void *const *objs, uint32_t n)
{
    memset(&s_filter, 0, sizeof(s_filter));
    s_filter.on = on;
    for (uint32_t i = 0; on && objs && i < n; i++) {
        filterAdd(RD_KEY(objs[i], 0, 0) >> 16);
    }
}

void rd_DrawFilterOpen(bool open)
{
    s_filter.open = s_filter.on && open;
}

bool rd_DrawFilterKeeps(RdKey key)
{
    return !s_filter.on || s_filter.open || (key != 0 && filterHas(key >> 16));
}

bool rd__DrawFilterPass(RdKey key)
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

void rd_ScreenPrims(RdPrim type, const RdScreenVtx *v, uint32_t count, RdSpace space, int uvFixed,
                    RdKey key)
{
    if (!v || count == 0 || (unsigned)type > RD_PRIM_SPRITES) {
        return;
    }
    if ((g_rd.spaceOverride > 0 ? g_rd.spaceOverride - 1 : (int)space) == RD_SPACE_WORLD &&
        !rd__DrawFilterPass(key)) {
        return;
    }
    RdFrame *f = rd__RecFrame();
    if (!f) {
        rd__Push(RDC_SCREEN); /* reports once */
        return;
    }
    uint32_t off = rd__FramePayload(f, v, count * (uint32_t)sizeof(RdScreenVtx));
    RdCmd *c = rd__Push(RDC_SCREEN);
    c->b[0] = (uint8_t)type;
    c->b[1] = (uint8_t)(g_rd.spaceOverride > 0 ? g_rd.spaceOverride - 1 : (int)space);
    /* T1: RD_UV_FIXED_CONTINUOUS is kept (port UI text, no GS-pixel snap) */
    c->b[2] = uvFixed == RD_UV_FIXED_CONTINUOUS ? RD_UV_FIXED_CONTINUOUS : (uvFixed ? 1 : 0);
    c->u[0] = off;
    c->u[1] = count;
    c->b[3] = g_rd.textQuads ? RD_SCREEN_TEXT_QUADS : 0; /* package DEF */
    setKey(c, key);
    g_rd.stats.draws++;
}

/* ------------------------------------------- deferred text (package DEF) */

void rd_DeferredText(const RdTextItem *item, RdKey key)
{
    RdFrame *f = rd__RecFrame();
    if (!item || !f) {
        if (item) {
            rd__Push(RDC_OVERLAY_TEXT); /* reports once */
        }
        return;
    }
    RdTextItem it = *item;
    it.utf8[RD_TEXT_BYTES - 1] = '\0';
    it.pad[0] = it.pad[1] = 0;
    const uint32_t off = rd__FramePayload(f, &it, (uint32_t)sizeof(it));
    RdCmd *c = rd__Push(RDC_OVERLAY_TEXT);
    c->b[0] = RD_OTEXT_ITEM;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(it);
    setKey(c, key);
    f->textItems++;
}

void rd_DeferredTextQuads(int on)
{
    g_rd.textQuads = on ? 1 : 0;
}

/* rd_post.c: a post pass after deferred text in this frame (RdTextOp) */
void rd__DeferredTextOp(uint8_t kind, const RdTextOp *op, RdKey key)
{
    RdFrame *f = rd__RecFrame();
    if (!f || !f->textItems || !op) {
        return;
    }
    const uint32_t off = rd__FramePayload(f, op, (uint32_t)sizeof(*op));
    RdCmd *c = rd__Push(RDC_OVERLAY_TEXT);
    c->b[0] = RD_OTEXT_OP;
    c->b[1] = kind;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(*op);
    setKey(c, key);
}

int rd_SetSpaceOverride(int space)
{
    const int prev = g_rd.spaceOverride - 1;
    g_rd.spaceOverride = space < 0 || space > RD_SPACE_FULLSCREEN ? 0 : space + 1;
    return prev;
}

/* Records a stubbed draw: the payload holds the parts concatenated. */
static RdCmd *pushStub(uint8_t type, RdKey key, const void *const *parts, const uint32_t *sizes,
                       int n)
{
    if (!rd__DrawFilterPass(key)) {
        return NULL; /* rd_WorldPrims, rd_ShadowStrip: world draws */
    }
    RdFrame *f = rd__RecFrame();
    if (!f) {
        rd__Push(type);
        return NULL;
    }
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        total += (sizes[i] + 7u) & ~7u;
    }
    uint32_t off = rd__FramePayload(f, NULL, total);
    uint32_t at = off;
    for (int i = 0; i < n; i++) {
        if (parts[i] && sizes[i]) {
            memcpy(f->payload + at, parts[i], sizes[i]);
        }
        at += (sizes[i] + 7u) & ~7u;
    }
    RdCmd *c = rd__Push(type);
    c->u[1] = off;
    c->u[2] = total;
    setKey(c, key);
    g_rd.stats.draws++;
    return c;
}

/* The wave-0 semantic mesh calls (rd.h): superseded by rd_mesh.h's exact
 * path in wave 3 (R3ab), whose payload RDC_MESH and the other three now
 * carry.  Kept declared for an Enhanced path; they record nothing. */
static void semanticMesh(const char *what)
{
    rd__LogOnce(RD_ONCE_SEMANTIC_MESH, "%s is not recorded: the mesh path is rd_mesh.h's", what);
}

void rd_DrawMesh(RdMesh m, RdProg prog, const RdXform *xf, const RdLights *lights,
                 const RdMaterial *materials, RdKey key)
{
    (void)m, (void)prog, (void)xf, (void)lights, (void)materials, (void)key;
    semanticMesh("rd_DrawMesh");
}

void rd_DrawSkinned(RdMesh m, RdProg prog, const RdXform *xf, const float (*bones)[16],
                    uint32_t boneCount, const RdLights *lights, const RdMaterial *materials,
                    RdKey key)
{
    (void)m, (void)prog, (void)xf, (void)bones, (void)boneCount, (void)lights, (void)materials;
    (void)key;
    semanticMesh("rd_DrawSkinned");
}

void rd_DrawGrid(const struct Mesh3D *grid, const RdXform *xf, const RdLights *lights,
                 const RdMaterial *mat, RdKey key)
{
    (void)grid, (void)xf, (void)lights, (void)mat, (void)key;
    semanticMesh("rd_DrawGrid");
}

void rd_DrawParticles(const RdParticleBatch *b, RdKey key)
{
    (void)b, (void)key;
    semanticMesh("rd_DrawParticles");
}

void rd_WorldPrims(RdPrim type, const RdWorldVtx *v, uint32_t count, const float *mtx, RdKey key)
{
    const void *parts[2] = {v, mtx};
    uint32_t sizes[2] = {v ? count * (uint32_t)sizeof(RdWorldVtx) : 0, mtx ? 64u : 0};
    RdCmd *c = pushStub(RDC_WORLD_PRIMS, key, parts, sizes, 2);
    if (c) {
        c->b[0] = (uint8_t)type;
        c->u[0] = count;
    }
}

void rd_ShadowStrip(const float (*v)[4], uint32_t count, float sign, RdKey key)
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

bool rd_DumpFrame(const char *path)
{
    const RdFrame *f = rd__LastFrame();
    return f ? rd__DumpFrame(f, path) : false;
}

bool rd_ReadDisplay(void *dst, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__TargetRec(RD_TARGET_DISPLAY + 1);
    if (!t || !dst) {
        return false;
    }
    return rd__ReadTarget(rd_Target(RD_TARGET_DISPLAY), dst, (size_t)t->tw * t->th * 4, w, h);
}

const RdStats *rd_GetStats(void)
{
    g_rd.stats.pipelines = rd__PipelineCount();
    return &g_rd.stats;
}
