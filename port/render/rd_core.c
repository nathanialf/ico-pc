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
 * The two most recent frames are retained (the closed one and the one
 * before it) for the interpolation package; recording reuses the older.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"

RdContext g_rd;

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
    fprintf(stderr, "rd: replay of %s is not implemented yet (docs/port/RENDER_API.md)\n", what);
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
        rd__TempTargetFree(f->tempTargets[i]);
    }
    f->tempCount = 0;
    rd__WaterFrameReset(f); /* R5b: block targets, aliases, camera scopes */
    f->hasCamera = 0;
    f->hasVu = 0;     /* R2c */
    f->headValid = 0; /* R2c */
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

/* wave 5 (R5a): 0 until the replay sizes the GS window apart from the
 * target's texture size (Enhanced, wave 6) */
#define RD_WORK_SCALE_APPLY 0

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
     * rd_WorkTargetScale).  1 in Original; Enhanced would scale the
     * fixed-size buffers, which the replay does not support yet (it sizes
     * the GS window by the target), so the rule is not applied until it
     * does: RD_WORK_SCALE_APPLY */
    if (RD_WORK_SCALE_APPLY && id != RD_TARGET_SCENE && id != RD_TARGET_DISPLAY &&
        id != RD_TARGET_DATE_SNAPSHOT && id != RD_TARGET_WORK2 && id != RD_TARGET_AURA_WORK) {
        const float k = rd_WorkTargetScale(g_rd.settings.preset, g_rd.settings.outputHeight);
        *w = (uint32_t)((float)*w * k + 0.5f);
        *h = (uint32_t)((float)*h * k + 0.5f);
    }
}

float rd_WorkTargetScale(RdPreset preset, uint32_t outputHeight)
{
    if (preset != RD_PRESET_ENHANCED || outputHeight == 0) {
        return 1.0f; /* Original: the literal PS2 sizes */
    }
    float k = (float)outputHeight / 448.0f;
    return k < 1.0f ? 1.0f : (k > 2.0f ? 2.0f : k);
}

static const char *const s_targetNames[RD_TARGET_COUNT] = {
    "SCENE",         "DISPLAY",   "SHADOW0",  "SHADOW1",  "SHADOW2", "WORK0",
    "WORK1",         "WORK2",     "WORK3",    "AA0",      "AA1",     "FEED128",
    "DATE_SNAPSHOT", "AURA_WORK", "AURA_TAP", "WORK2_PAD"};

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
    t->color = rhi_CreateTexture(&(RhiTextureDesc){t->w, t->h, 1, t->format, usage, name});
    t->colorState = RHI_STATE_UNDEFINED;
    if (t->withDepth) {
        t->depth = rhi_CreateTexture(&(RhiTextureDesc){
            t->w, t->h, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_SRC, name});
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
        if (!rd__TargetCreateGpu(t, s_targetNames[i])) {
            rd__Log("could not create target %s", s_targetNames[i]);
        }
    }
}

uint32_t rd__TempTargetAlloc(uint32_t w, uint32_t h, int withDepth, int keepAcross)
{
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (t->live) {
            continue;
        }
        uint32_t gen = (t->gen + 1) & 0xFFFF;
        memset(t, 0, sizeof(*t));
        t->gen = gen ? gen : 1;
        t->live = 1;
        t->w = w ? w : 1;
        t->h = h ? h : 1;
        t->format = RHI_FMT_RGBA8_UNORM;
        t->withDepth = withDepth ? 1 : 0;
        t->keepAcross = keepAcross ? 1 : 0;
        rd__TargetCreateGpu(t, "temp target");
        g_rd.stats.tempTargets++;
        return (t->gen << 16) | (uint32_t)(i + 1);
    }
    rd__LogOnce(RD_ONCE_TEMP_FULL, "out of temporary targets (%d)",
                RD_MAX_TARGETS - RD_TARGET_COUNT);
    return 0;
}

void rd__TempTargetFree(uint32_t id)
{
    RdTargetRec *t = rd__TargetRec(id);
    if (!t || t->named) {
        return;
    }
    rd__TargetDestroyGpu(t);
    for (int v = 0; v < 3; v++) {
        if (t->viewTex[v]) {
            rd_DestroyTexture((RdTex){t->viewTex[v]});
        }
    }
    uint32_t gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->gen = gen;
    if (g_rd.stats.tempTargets) {
        g_rd.stats.tempTargets--;
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

RdTex rd_CreateTextureSrc(uint32_t w, uint32_t h, const void *rgba8, RdTexSrc src,
                          const char *debugName)
{
    if (!g_rd.inited || w == 0 || h == 0) {
        return (RdTex){0};
    }
    uint32_t id = texAlloc();
    RdTexRec *t = rd__TexRec(id);
    if (!t) {
        return (RdTex){0};
    }
    t->kind = RD_TEXKIND_IMAGE;
    t->src = (uint8_t)src;
    t->w = w;
    t->h = h;
    t->pixels = malloc((size_t)w * h * 4);
    if (!t->pixels) {
        t->live = 0;
        return (RdTex){0};
    }
    if (rgba8) {
        memcpy(t->pixels, rgba8, (size_t)w * h * 4);
    } else {
        memset(t->pixels, 0, (size_t)w * h * 4);
    }
    t->dirty = 1;
    (void)debugName;
    return (RdTex){id};
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
    if (!t || t->kind != RD_TEXKIND_IMAGE || !rgba8) {
        return;
    }
    memcpy(t->pixels, rgba8, (size_t)t->w * t->h * 4);
    t->dirty = 1;
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
    free(t->pixels);
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
    createNamedTargets();
    return true;
}

/* Frame dumps every N replayed frames (wave 3): ico-pc.ini dump_every= and
 * dump_dir=, handed over by port/platform/host_config.c in the environment
 * (ICO_RD_DUMP_EVERY, ICO_RD_DUMP_DIR), read at rd_Init. */
static uint32_t s_dumpEvery;

static char s_dumpDir[512];

static void readDumpConfig(void)
{
    const char *every = getenv("ICO_RD_DUMP_EVERY");
    const char *dir = getenv("ICO_RD_DUMP_DIR");
    s_dumpEvery = every ? (uint32_t)strtoul(every, NULL, 10) : 0;
    snprintf(s_dumpDir, sizeof(s_dumpDir), "%s", dir ? dir : ".");
    if (s_dumpEvery) {
        rd__Log("dumping every %u frames into %s", s_dumpEvery, s_dumpDir);
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
    for (int i = 0; i < 2; i++) {
        rd__FrameFree(&g_rd.frames[i]);
    }
    for (int i = 0; i < RD_MAX_TARGETS; i++) {
        if (g_rd.targets[i].live) {
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
        }
    }
    if (g_rd.hasDevice) {
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
    createNamedTargets();
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

void rd_BeginFrame(void)
{
    if (!g_rd.inited) {
        return;
    }
    if (g_rd.recIndex >= 0) {
        rd__Log("rd_BeginFrame without rd_EndFrame: the open frame is discarded");
    }
    if (g_rd.settingsPending) {
        g_rd.settings = g_rd.pendingSettings;
        g_rd.settingsPending = false;
    }
    int idx = g_rd.lastIndex < 0 ? 0 : g_rd.lastIndex ^ 1;
    RdFrame *f = &g_rd.frames[idx];
    rd__FrameReset(f);
    f->number = ++g_rd.frameCounter;
    f->startState = g_rd.persistent;
    f->gsW = g_rd.gsW;
    f->gsH = g_rd.gsH;
    g_rd.recIndex = idx;
    g_rd.stats.draws = 0;
    recordDefaults();
}

void rd_EndFrame(int keep)
{
    RdFrame *f = rd__RecFrame();
    if (!f) {
        return;
    }
    f->keep = keep ? 1 : 0;
    rd__FrameHeadResolve(f, f->keep); /* R2c: the flip's head in the first replayed list */
    f->closed = 1;
    RdStateBlock s = f->startState;
    rd__Walk(f, f->keep, &s, NULL, NULL);
    f->endState = s;
    g_rd.persistent = s;
    g_rd.lastIndex = g_rd.recIndex;
    g_rd.recIndex = -1;
    g_rd.stats.bytesPayload = f->payloadSize;
    if (g_rd.hasDevice) {
        rd__ReplayFrame(f, f->keep, true);
    }
    if (s_dumpEvery && f->number % s_dumpEvery == 0) {
        char path[600];
        snprintf(path, sizeof(path), "%s/rd-%05u.rddump", s_dumpDir, f->number);
        if (rd__DumpFrame(f, path)) {
            rd__Log("frame %u dumped to %s", f->number, path);
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

const RdFrame *rd__LastFrame(void)
{
    return g_rd.lastIndex >= 0 ? &g_rd.frames[g_rd.lastIndex] : NULL;
}

const RdFrame *rd__PrevFrame(void)
{
    if (g_rd.lastIndex < 0) {
        return NULL;
    }
    const RdFrame *f = &g_rd.frames[g_rd.lastIndex ^ 1];
    return f->closed ? f : NULL;
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

/* ------------------------------------------------------------------ draws */

void rd_ScreenPrims(RdPrim type, const RdScreenVtx *v, uint32_t count, RdSpace space, int uvFixed,
                    RdKey key)
{
    if (!v || count == 0 || (unsigned)type > RD_PRIM_SPRITES) {
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
    c->b[1] = (uint8_t)space;
    c->b[2] = uvFixed ? 1 : 0;
    c->u[0] = off;
    c->u[1] = count;
    setKey(c, key);
    g_rd.stats.draws++;
}

/* Records a stubbed draw: the payload holds the parts concatenated. */
static RdCmd *pushStub(uint8_t type, RdKey key, const void *const *parts, const uint32_t *sizes,
                       int n)
{
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
    return rd__ReadTarget(rd_Target(RD_TARGET_DISPLAY), dst, (size_t)t->w * t->h * 4, w, h);
}

const RdStats *rd_GetStats(void)
{
    g_rd.stats.pipelines = rd__PipelineCount();
    return &g_rd.stats;
}
