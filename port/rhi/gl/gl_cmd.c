/* gl_cmd.c: command lists, render passes, draws, barriers and copies of the
 * OpenGL backend (gl_internal.h).
 *
 * A command list executes while it is recorded: each rhi_cmd_* call makes
 * its OpenGL calls at once (rhi.h: lists are recorded and submitted in
 * order), and rhi_submit places the frame slot's fence.  What a list sets
 * (pipeline, bind groups, vertex and index buffers, stencil reference,
 * dynamic state) is kept in its GlCmdList and given to OpenGL at the next
 * draw; the fixed-function state OpenGL holds is shadowed here
 * (GlRaster), so a draw changes only what differs.  A command on another
 * list than the last one sets that list's bindings, viewport, scissor and
 * framebuffer again.
 *
 * Render passes bind a framebuffer object from a cache keyed by the
 * attachments' texture names (gl_fbo_get; a texture's deferred delete drops
 * the framebuffers holding it, gl_fbo_forget_texture), clear with
 * glClearBuffer* with the scissor test off (a clear covers the attachment,
 * as D3D12's clear) and the write masks open, and invalidate the
 * attachments loaded DONT_CARE.  Barriers: OpenGL orders rendering, copies
 * and texture reads itself; only a texture leaving or entering
 * RHI_STATE_STORAGE (image loads and stores) takes a glMemoryBarrier. */
#include "gl_internal.h"
#include <stdlib.h>
#include <string.h>

#define GL_UNKNOWN_NAME 0xFFFFFFFFu
#define GL_FBO_MAX 256u
/* the binding points whose binds gl_bind_groups shadows (uniform blocks,
 * storage buffers, texture and image units below it; the translated
 * shaders use up to 21) */
#define GL_SHADOW_POINTS 64u

/* the fixed-function state OpenGL holds, per draw buffer and face */
typedef struct GlRaster {
    bool blend[RHI_MAX_COLOR_TARGETS];
    GLenum src[RHI_MAX_COLOR_TARGETS], dst[RHI_MAX_COLOR_TARGETS], eq[RHI_MAX_COLOR_TARGETS];
    GLenum srcA[RHI_MAX_COLOR_TARGETS], dstA[RHI_MAX_COLOR_TARGETS], eqA[RHI_MAX_COLOR_TARGETS];
    uint8_t mask[RHI_MAX_COLOR_TARGETS];
    bool depthTest, depthWrite;
    GLenum depthFunc;
    bool stencilTest;
    GLenum sFunc[2], sFail[2], sZFail[2], sPass[2]; /* [0] front, [1] back */
    GLint sRef[2];
    GLuint sRead[2], sWrite[2];
} GlRaster;

/* a buffer range bound at an indexed binding point */
typedef struct GlRangeBind {
    GLuint name;
    uint64_t offset, size;
} GlRangeBind;

/* a cached framebuffer: its attachments' names (an attachment-less one
 * keyed by its default size) */
typedef struct GlFbo {
    GLuint fbo;
    GLuint color[RHI_MAX_COLOR_TARGETS];
    GLuint depth;
    uint32_t colorCount;
    uint32_t w, h;
    uint64_t used;
} GlFbo;

static struct {
    bool rasterValid; /* raster is what OpenGL holds */
    GlRaster raster;
    GLuint ppo, vao;   /* bound now; GL_UNKNOWN_NAME when not known */
    GlCmdList *active; /* the list the last command was on */
    /* what gl_bind_groups last gave each binding point, so a bind of the
     * same is skipped (gl_forget_units: every name GL_UNKNOWN_NAME) */
    GlRangeBind uniform[GL_SHADOW_POINTS], storage[GL_SHADOW_POINTS];
    GLuint texName[GL_SHADOW_POINTS], sampler[GL_SHADOW_POINTS], image[GL_SHADOW_POINTS];
    GLenum texTarget[GL_SHADOW_POINTS];
    GlFbo fbos[GL_FBO_MAX];
    uint32_t fboCount;
    uint64_t fboClock;
    uint32_t fboEvicted, fboIncomplete;
    /* log rates */
    uint32_t badDraws, badPasses, badCopies, passArea, readOnlyClears, unboundVb;
} s;

/* ------------------------------------------------------------ the shadow */
static void gl_apply_raster(const GlRaster *w)
{
    GlRaster *h = &s.raster;
    const bool all = !s.rasterValid;
    for (uint32_t i = 0; i < RHI_MAX_COLOR_TARGETS; i++) {
        if (all || w->blend[i] != h->blend[i]) {
            if (w->blend[i]) {
                glf.Enablei(GL_BLEND, i);
            } else {
                glf.Disablei(GL_BLEND, i);
            }
        }
        if (all || w->src[i] != h->src[i] || w->dst[i] != h->dst[i] || w->srcA[i] != h->srcA[i] ||
            w->dstA[i] != h->dstA[i]) {
            glf.BlendFuncSeparatei(i, w->src[i], w->dst[i], w->srcA[i], w->dstA[i]);
        }
        if (all || w->eq[i] != h->eq[i] || w->eqA[i] != h->eqA[i]) {
            glf.BlendEquationSeparatei(i, w->eq[i], w->eqA[i]);
        }
        if (all || w->mask[i] != h->mask[i]) {
            const uint8_t m = w->mask[i];
            glf.ColorMaski(i, (m & 1u) ? GL_TRUE : GL_FALSE, (m & 2u) ? GL_TRUE : GL_FALSE,
                           (m & 4u) ? GL_TRUE : GL_FALSE, (m & 8u) ? GL_TRUE : GL_FALSE);
        }
    }
    if (all || w->depthTest != h->depthTest) {
        if (w->depthTest) {
            glf.Enable(GL_DEPTH_TEST);
        } else {
            glf.Disable(GL_DEPTH_TEST);
        }
    }
    if (all || w->depthWrite != h->depthWrite) {
        glf.DepthMask(w->depthWrite ? GL_TRUE : GL_FALSE);
    }
    if (all || w->depthFunc != h->depthFunc) {
        glf.DepthFunc(w->depthFunc);
    }
    if (all || w->stencilTest != h->stencilTest) {
        if (w->stencilTest) {
            glf.Enable(GL_STENCIL_TEST);
        } else {
            glf.Disable(GL_STENCIL_TEST);
        }
    }
    static const GLenum faces[2] = {GL_FRONT, GL_BACK};
    for (int f = 0; f < 2; f++) {
        if (all || w->sFunc[f] != h->sFunc[f] || w->sRef[f] != h->sRef[f] ||
            w->sRead[f] != h->sRead[f]) {
            glf.StencilFuncSeparate(faces[f], w->sFunc[f], w->sRef[f], w->sRead[f]);
        }
        if (all || w->sFail[f] != h->sFail[f] || w->sZFail[f] != h->sZFail[f] ||
            w->sPass[f] != h->sPass[f]) {
            glf.StencilOpSeparate(faces[f], w->sFail[f], w->sZFail[f], w->sPass[f]);
        }
        if (all || w->sWrite[f] != h->sWrite[f]) {
            glf.StencilMaskSeparate(faces[f], w->sWrite[f]);
        }
    }
    *h = *w;
    s.rasterValid = true;
}

/* OpenGL's defaults: no blending, every channel, no depth or stencil test */
static void gl_default_raster(GlRaster *w)
{
    memset(w, 0, sizeof(*w));
    for (uint32_t i = 0; i < RHI_MAX_COLOR_TARGETS; i++) {
        w->src[i] = w->srcA[i] = GL_ONE;
        w->dst[i] = w->dstA[i] = GL_ZERO;
        w->eq[i] = w->eqA[i] = GL_FUNC_ADD;
        w->mask[i] = 0xFu;
    }
    w->depthWrite = true;
    w->depthFunc = GL_LESS;
    for (int f = 0; f < 2; f++) {
        w->sFunc[f] = GL_ALWAYS;
        w->sFail[f] = w->sZFail[f] = w->sPass[f] = GL_KEEP;
        w->sRead[f] = w->sWrite[f] = 0xFFu;
    }
}

/* the write masks a clear honours, opened (colour draw buffers in
 * colourBits, the depth, the stencil) */
static void gl_open_masks(uint32_t colourBits, bool depth, bool stencil)
{
    GlRaster w;
    if (s.rasterValid) {
        w = s.raster;
    } else {
        gl_default_raster(&w);
    }
    for (uint32_t i = 0; i < RHI_MAX_COLOR_TARGETS; i++) {
        if (colourBits & (1u << i)) {
            w.mask[i] = 0xFu;
        }
    }
    if (depth) {
        w.depthWrite = true;
    }
    if (stencil) {
        w.sWrite[0] = w.sWrite[1] = 0xFFu;
    }
    gl_apply_raster(&w);
}

/* --------------------------------------------------------- dynamic state */
static GlBlendDyn gl_blend_dyn(const RhiBlendState *b, RhiFormat target)
{
    GlBlendDyn d;
    memset(&d, 0, sizeof(d));
    /* OpenGL does not blend integer targets; off, as the other backends */
    d.enable = b->enable && !gl_format(target)->integer;
    d.src = gl_blend_factor(b->srcColor);
    d.dst = gl_blend_factor(b->dstColor);
    d.eq = gl_blend_op(b->colorOp);
    d.srcA = gl_blend_factor(b->srcAlpha);
    d.dstA = gl_blend_factor(b->dstAlpha);
    d.eqA = gl_blend_op(b->alphaOp);
    d.mask = (uint8_t)(b->writeMask & 0xFu);
    return d;
}

static GlDepthDyn gl_depth_dyn(bool test, bool write, RhiCompare cmp)
{
    GlDepthDyn d;
    memset(&d, 0, sizeof(d));
    d.test = test;
    d.write = test && write;
    d.func = gl_compare(cmp);
    return d;
}

/* The state a draw wants: the pipeline's, its dynamic groups' wanted
 * values, the list's stencil reference, no depth or stencil writes in a
 * read-only depth pass. */
static void gl_want_raster(const GlCmdList *c, const GlPipeline *p, GlRaster *w)
{
    gl_default_raster(w);
    for (uint32_t i = 0; i < RHI_MAX_COLOR_TARGETS; i++) {
        GlBlendDyn b;
        if (i == 0 && (p->dynamic & RHI_DYN_BLEND)) {
            b = c->blendWant;
        } else if (i < p->colorCount) {
            b = gl_blend_dyn(&p->blend[i], p->colorFormats[i]);
        } else {
            continue;
        }
        w->blend[i] = b.enable;
        w->src[i] = b.src;
        w->dst[i] = b.dst;
        w->eq[i] = b.eq;
        w->srcA[i] = b.srcA;
        w->dstA[i] = b.dstA;
        w->eqA[i] = b.eqA;
        w->mask[i] = b.mask;
    }
    const RhiDepthStencilState *ds = &p->depthStencil;
    const GlFormat *df = gl_format(p->depthFormat);
    if (p->depthFormat == RHI_FMT_UNKNOWN) {
        w->depthTest = false;
        w->depthWrite = false;
        w->depthFunc = GL_ALWAYS;
    } else {
        const GlDepthDyn d = (p->dynamic & RHI_DYN_DEPTH)
                                 ? c->depthWant
                                 : gl_depth_dyn(ds->depthTest, ds->depthWrite, ds->depthCompare);
        w->depthTest = d.test;
        w->depthWrite = d.write && !c->readOnlyDepth;
        w->depthFunc = d.func;
    }
    w->stencilTest = ds->stencilTest && df->stencil;
    const RhiStencilFace *faces[2] = {&ds->front, &ds->back};
    for (int f = 0; f < 2; f++) {
        w->sFunc[f] = gl_compare(faces[f]->compare);
        w->sRef[f] = (GLint)c->stencilRef;
        w->sRead[f] = ds->stencilReadMask;
        w->sFail[f] = gl_stencil_op(faces[f]->fail);
        w->sZFail[f] = gl_stencil_op(faces[f]->depthFail);
        w->sPass[f] = gl_stencil_op(faces[f]->pass);
        w->sWrite[f] = c->readOnlyDepth ? 0u : ds->stencilWriteMask;
    }
}

/* RhiStats.dynamicSets: the commands the Vulkan backend records for the
 * bound pipeline's dynamic groups at this draw (vk_cmd.c vkr_flush_dynamic):
 * every command of a group after a bind of a pipeline with it static, else
 * those whose value changed; the blend equation only while blending. */
static void gl_count_dynamic(GlCmdList *c, const GlPipeline *p)
{
    static bool s_unsetLogged;
    const uint32_t dyn = p->dynamic;
    if (!dyn) {
        return;
    }
    if ((dyn & ~c->dynSet) != 0u && !s_unsetLogged) {
        s_unsetLogged = true;
        GL_LOG("dynamic state: a draw with its pipeline's %s%s%s left unset since the bind (the "
               "pipeline's own values used; reported once)",
               (dyn & ~c->dynSet & RHI_DYN_DEPTH) ? "depth" : "",
               (dyn & ~c->dynSet) == (RHI_DYN_DEPTH | RHI_DYN_BLEND) ? " and " : "",
               (dyn & ~c->dynSet & RHI_DYN_BLEND) ? "blend" : "");
    }
    if (dyn & RHI_DYN_DEPTH) {
        const bool all = !(c->dynValid & RHI_DYN_DEPTH);
        const GlDepthDyn *w = &c->depthWant, *h = &c->depthHave;
        g_gl.counts.dynamicSets += (uint64_t)(all || w->test != h->test) +
                                   (uint64_t)(all || w->write != h->write) +
                                   (uint64_t)(all || w->func != h->func);
        c->depthHave = *w;
        c->dynValid |= RHI_DYN_DEPTH;
    }
    if (dyn & RHI_DYN_BLEND) {
        const bool all = !(c->dynValid & RHI_DYN_BLEND);
        const GlBlendDyn *w = &c->blendWant;
        GlBlendDyn *h = &c->blendHave;
        if (all || w->enable != h->enable) {
            g_gl.counts.dynamicSets++;
            h->enable = w->enable;
        }
        if (all || (w->enable && (w->src != h->src || w->dst != h->dst || w->eq != h->eq ||
                                  w->srcA != h->srcA || w->dstA != h->dstA || w->eqA != h->eqA))) {
            g_gl.counts.dynamicSets++;
            h->src = w->src;
            h->dst = w->dst;
            h->eq = w->eq;
            h->srcA = w->srcA;
            h->dstA = w->dstA;
            h->eqA = w->eqA;
        }
        if (all || w->mask != h->mask) {
            g_gl.counts.dynamicSets++;
            h->mask = w->mask;
        }
        c->dynValid |= RHI_DYN_BLEND;
    }
}

/* ------------------------------------------------------- framebuffers */
static void gl_fbo_delete_at(uint32_t i)
{
    glf.DeleteFramebuffers(1, &s.fbos[i].fbo);
    s.fbos[i] = s.fbos[--s.fboCount];
}

void gl_fbo_forget_texture(GLuint name)
{
    if (!name) {
        return;
    }
    for (uint32_t i = 0; i < s.fboCount;) {
        const GlFbo *f = &s.fbos[i];
        bool holds = f->depth == name;
        for (uint32_t k = 0; k < f->colorCount; k++) {
            holds = holds || f->color[k] == name;
        }
        if (holds) {
            gl_fbo_delete_at(i); /* the last one moved here: look at i again */
        } else {
            i++;
        }
    }
}

/* The framebuffer of a pass's attachments (or of a copy's or resolve's
 * texture: the same cache, so no attach and detach per copy), bound to
 * GL_FRAMEBUFFER; 0 when OpenGL finds it incomplete (logged as `what`'s,
 * with `outcome`).  w and h key only a framebuffer without attachments;
 * for the others they are the log's.  The one just returned is the most recently used, so a
 * second call never evicts it (the resolve's pair). */
static GLuint gl_fbo_get(GlTexture *const *colour, uint32_t colourCount, const GlTexture *depth,
                         uint32_t w, uint32_t h, const char *what, const char *outcome)
{
    GlFbo key;
    memset(&key, 0, sizeof(key));
    key.colorCount = colourCount;
    for (uint32_t i = 0; i < colourCount; i++) {
        key.color[i] = colour[i]->name;
    }
    key.depth = depth ? depth->name : 0u;
    if (colourCount == 0 && !depth) {
        key.w = w;
        key.h = h;
    }
    for (uint32_t i = 0; i < s.fboCount; i++) {
        GlFbo *f = &s.fbos[i];
        if (f->colorCount == key.colorCount && f->depth == key.depth && f->w == key.w &&
            f->h == key.h && memcmp(f->color, key.color, sizeof(key.color)) == 0) {
            f->used = ++s.fboClock;
            glf.BindFramebuffer(GL_FRAMEBUFFER, f->fbo);
            return f->fbo;
        }
    }
    if (s.fboCount == GL_FBO_MAX) {
        /* the least recently used goes; made again when a pass needs it */
        uint32_t lru = 0;
        for (uint32_t i = 1; i < s.fboCount; i++) {
            if (s.fbos[i].used < s.fbos[lru].used) {
                lru = i;
            }
        }
        gl_fbo_delete_at(lru);
        if (gl_log_rate(&s.fboEvicted)) {
            GL_LOG("framebuffer cache full (%u): the least recently used one deleted (%u so far)",
                   GL_FBO_MAX, s.fboEvicted);
        }
    }
    GLuint fbo = 0;
    glf.GenFramebuffers(1, &fbo);
    glf.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum bufs[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < colourCount; i++) {
        glf.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, colour[i]->target,
                                 colour[i]->name, 0);
        bufs[i] = GL_COLOR_ATTACHMENT0 + i;
    }
    if (depth) {
        glf.FramebufferTexture2D(GL_FRAMEBUFFER,
                                 gl_format(depth->format)->stencil ? GL_DEPTH_STENCIL_ATTACHMENT
                                                                   : GL_DEPTH_ATTACHMENT,
                                 depth->target, depth->name, 0);
    }
    if (colourCount == 0 && !depth) {
        /* a pass without attachments (the destination read's storage
         * image): its size is the framebuffer's default
         * (ARB_framebuffer_no_attachments, core in 4.3) */
        glf.FramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, (GLint)w);
        glf.FramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, (GLint)h);
    }
    if (colourCount) {
        glf.DrawBuffers((GLsizei)colourCount, bufs);
        glf.ReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        glf.DrawBuffer(GL_NONE);
        glf.ReadBuffer(GL_NONE);
    }
    const GLenum status = glf.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (gl_log_rate(&s.fboIncomplete)) {
            GL_LOG("%s: framebuffer incomplete (status 0x%04x; %u colour, %s, %ux%u); %s (%u so "
                   "far)",
                   what, (unsigned)status, colourCount, depth ? "depth" : "no depth", w, h, outcome,
                   s.fboIncomplete);
        }
        glf.BindFramebuffer(GL_FRAMEBUFFER, 0);
        glf.DeleteFramebuffers(1, &fbo);
        return 0;
    }
    key.fbo = fbo;
    key.used = ++s.fboClock;
    s.fbos[s.fboCount++] = key;
    return fbo;
}

/* ------------------------------------------------------------ lifecycle */
/* Nothing bound at the binding points is taken as known: the next bind of
 * each is made.  Needed where a name the shadow holds may stop being bound
 * there: a delete (OpenGL unbinds a deleted buffer, texture or sampler from
 * every point, and may hand its name out again), and a new list (the
 * deletes of a recycle ran before it).  Nothing else binds these points:
 * the creates, copies, readbacks and the present use texture unit 0 and the
 * non-indexed buffer targets (gl_internal.h, scratch), and framebuffer
 * attachments are not bindings of a unit. */
void gl_forget_units(void)
{
    memset(s.uniform, 0xFF, sizeof(s.uniform));
    memset(s.storage, 0xFF, sizeof(s.storage));
    memset(s.texName, 0xFF, sizeof(s.texName));
    memset(s.sampler, 0xFF, sizeof(s.sampler));
    memset(s.image, 0xFF, sizeof(s.image));
    memset(s.texTarget, 0xFF, sizeof(s.texTarget));
}

static void gl_forget_bindings(void)
{
    s.rasterValid = false;
    s.ppo = s.vao = GL_UNKNOWN_NAME;
    s.active = NULL;
    gl_forget_units();
}

void gl_cmd_init(void)
{
    memset(&s, 0, sizeof(s));
    gl_forget_bindings();
    /* program pipeline objects draw only with no program current */
    glf.UseProgram(0);
}

void gl_cmd_shutdown(void)
{
    while (s.fboCount) {
        gl_fbo_delete_at(s.fboCount - 1u);
    }
    memset(&s, 0, sizeof(s));
}

/* --------------------------------------------------------- command lists */
static void gl_apply_viewport(const RhiViewport *v)
{
    glf.ViewportIndexedf(0, v->x, v->y, v->w, v->h);
    glf.DepthRangef(v->minDepth, v->maxDepth);
}

static void gl_apply_scissor(const RhiRect *r)
{
    glf.Scissor(r->x, r->y, (GLsizei)(r->w > 0x7FFFFFFFu ? 0x7FFFFFFFu : r->w),
                (GLsizei)(r->h > 0x7FFFFFFFu ? 0x7FFFFFFFu : r->h));
}

/* A command on another list than the last one: its own state again. */
static void gl_cmd_activate(GlCmdList *c)
{
    s.active = c;
    c->groupDirty = 0xFFu;
    c->vbVao = 0;
    if (c->inPass && c->passFbo) {
        glf.BindFramebuffer(GL_FRAMEBUFFER, c->passFbo);
    }
    if (c->viewportSet) {
        gl_apply_viewport(&c->viewport);
    }
    if (c->scissorSet) {
        gl_apply_scissor(&c->scissor);
    }
    if (c->blendConstSet) {
        glf.BlendColor(c->blendConst[0], c->blendConst[1], c->blendConst[2], c->blendConst[3]);
    }
}

/* The list a command is recorded into; NULL for a stale id or a list not
 * recording. */
static GlCmdList *gl_cmd(RhiCommandList cl)
{
    GlCmdList *c = gl_get_cmd(cl);
    if (!c || !c->recording) {
        return NULL;
    }
    if (s.active != c) {
        gl_cmd_activate(c);
    }
    return c;
}

RhiCommandList rhi_begin_commands(void)
{
    RhiCommandList out = {0};
    if (!g_gl.initialised) {
        return out;
    }
    GlFrame *f = gl_cur_frame();
    if (f->listCount == GL_MAX_CMD_LISTS) {
        GL_LOG("more than %d command lists in one frame", GL_MAX_CMD_LISTS);
        return out;
    }
    GlCmdList *c = &f->lists[f->listCount];
    memset(c, 0, sizeof(*c));
    c->recording = true;
    c->serial = ++g_gl.listSerial;
    f->listCount++;
    /* objects deleted since the last list may have given their names to
     * new ones: nothing bound is taken as known */
    gl_forget_bindings();
    s.active = c;
    out.id = (gl_frame_tag() << D3DP_GEN_SHIFT) | f->listCount;
    return out;
}

void rhi_end_commands(RhiCommandList cl)
{
    GlCmdList *c = gl_get_cmd(cl);
    if (!c || !c->recording) {
        return;
    }
    if (c->inPass) {
        GL_LOG("rhi_end_commands inside a render pass");
        c->inPass = false;
    }
    c->recording = false;
}

void rhi_submit(RhiCommandList cl)
{
    GlCmdList *c = gl_get_cmd(cl);
    if (!c || c->submitted) {
        return;
    }
    if (c->recording) {
        rhi_end_commands(cl);
    }
    c->submitted = true;
    g_gl.counts.submits++;
    gl_frame_fence(); /* glFenceSync, then glFlush */
}

/* ---------------------------------------------------------------- barriers
 * No layouts and no hazards to order: OpenGL orders rendering, clears,
 * copies and texture fetches of one context itself.  Image loads and
 * stores (RHI_STATE_STORAGE, the destination read) are the exception: a
 * texture leaving or entering it takes a glMemoryBarrier for every later
 * kind of access, one per call. */
void rhi_cmd_barrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    GlCmdList *c = gl_cmd(cl);
    if (!c || (!barriers && count)) {
        return;
    }
    GLbitfield bits = 0;
    for (uint32_t i = 0; i < count; i++) {
        const RhiTextureBarrier *b = &barriers[i];
        if (!gl_get_texture(b->texture) || b->before >= RHI_STATE_COUNT ||
            b->after >= RHI_STATE_COUNT) {
            GL_LOG("rhi_cmd_barrier: invalid barrier %u", i);
            continue;
        }
        if (b->before == RHI_STATE_STORAGE || b->after == RHI_STATE_STORAGE) {
            /* every bit: the named ones do not list glCopyImageSubData
               (rhi_cmd_copy_texture) among the reads and writes they order,
               and a storage transition is rare (an interlock driver's
               destination read only) */
            bits |= GL_ALL_BARRIER_BITS;
        }
    }
    if (bits) {
        glf.MemoryBarrier(bits);
        g_gl.counts.barriers++;
    }
}

/* Inside a pass, between draws: Vulkan's by-region barrier (no local read
 * on OpenGL: RhiLimits.localRead is false) */
void rhi_cmd_local_read_barrier(RhiCommandList cl, uint32_t mask)
{
    (void)cl;
    (void)mask;
}

/* ------------------------------------------------------------ render passes */
/* A pass larger than an attachment would draw past it: logged once with
 * the sizes and the area clamped to the smallest attachment, as the other
 * backends do (vk_cmd.c vkr_pass_area_fits). */
static void gl_pass_area_check(uint32_t *w, uint32_t *h, GlTexture *const *colour,
                               uint32_t colourCount, const GlTexture *depth)
{
    uint32_t mw = *w, mh = *h;
    for (uint32_t i = 0; i < colourCount; i++) {
        mw = colour[i]->width < mw ? colour[i]->width : mw;
        mh = colour[i]->height < mh ? colour[i]->height : mh;
    }
    if (depth) {
        mw = depth->width < mw ? depth->width : mw;
        mh = depth->height < mh ? depth->height : mh;
    }
    if (mw == *w && mh == *h) {
        return;
    }
    if (s.passArea++ == 0) {
        GL_LOG("render pass %ux%u is larger than an attachment (depth %ux%u): drawn at %ux%u "
               "(reported once)",
               *w, *h, depth ? depth->width : 0u, depth ? depth->height : 0u, mw, mh);
    }
    *w = mw;
    *h = mh;
}

void rhi_cmd_begin_render_pass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    GlCmdList *c = gl_cmd(cl);
    if (!c || !pass) {
        return;
    }
    if (c->inPass) {
        GL_LOG("rhi_cmd_begin_render_pass inside a render pass");
    }
    c->inPass = true;
    c->passBad = true; /* until the framebuffer is bound */
    c->passFbo = 0;
    c->readOnlyDepth = false;
    if (pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        GL_LOG("render pass: %u colour targets", pass->colorCount);
        return;
    }
    GlTexture *colour[RHI_MAX_COLOR_TARGETS] = {NULL, NULL, NULL};
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        colour[i] = gl_get_texture(pass->color[i].texture);
        const GlFormat *fm = colour[i] ? gl_format(colour[i]->format) : NULL;
        if (!fm || fm->depth || fm->block) {
            if (gl_log_rate(&s.badPasses)) {
                GL_LOG("render pass: invalid colour target %u (%u so far)", i, s.badPasses);
            }
            return;
        }
    }
    GlTexture *dt = NULL;
    if (pass->depth.texture.id) {
        dt = gl_get_texture(pass->depth.texture);
        if (!dt || !gl_format(dt->format)->depth) {
            if (gl_log_rate(&s.badPasses)) {
                GL_LOG("render pass: invalid depth target (%u so far)", s.badPasses);
            }
            return;
        }
    }
    uint32_t areaW = pass->width, areaH = pass->height;
    gl_pass_area_check(&areaW, &areaH, colour, pass->colorCount, dt);
    const GLuint fbo = gl_fbo_get(colour, pass->colorCount, dt, areaW, areaH, "render pass",
                                  "the pass draws nothing");
    if (!fbo) {
        return;
    }
    c->passFbo = fbo;
    c->passBad = false;
    c->readOnlyDepth = dt && pass->depth.readOnlyDepth;
    g_gl.counts.renderPasses++;

    /* the clears: unscissored, through open write masks */
    uint32_t clearBits = 0;
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        clearBits |= pass->color[i].load == RHI_LOAD_CLEAR ? 1u << i : 0u;
    }
    const bool hasStencil = dt && gl_format(dt->format)->stencil;
    bool clearDepth = dt && pass->depth.depthLoad == RHI_LOAD_CLEAR;
    bool clearStencil = hasStencil && pass->depth.stencilLoad == RHI_LOAD_CLEAR;
    if ((clearDepth || clearStencil) && c->readOnlyDepth) {
        if (gl_log_rate(&s.readOnlyClears)) {
            GL_LOG("render pass: a read-only depth target cannot be cleared; clear ignored (%u "
                   "so far)",
                   s.readOnlyClears);
        }
        clearDepth = clearStencil = false;
    }
    if (clearBits || clearDepth || clearStencil) {
        gl_open_masks(clearBits, clearDepth, clearStencil);
        glf.Disable(GL_SCISSOR_TEST);
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            const RhiColorAttachment *a = &pass->color[i];
            if (a->load != RHI_LOAD_CLEAR) {
                continue;
            }
            if (gl_format(colour[i]->format)->integer) {
                /* the integer per channel, rounded as on Vulkan */
                GLuint u[4];
                for (int k = 0; k < 4; k++) {
                    u[k] = (GLuint)d3dp_int_clear_value(a->clear[k]);
                }
                glf.ClearBufferuiv(GL_COLOR, (GLint)i, u);
            } else {
                glf.ClearBufferfv(GL_COLOR, (GLint)i, a->clear);
            }
        }
        const GLfloat depthValue = pass->depth.clearDepth;
        const GLint stencilValue = pass->depth.clearStencil;
        if (clearDepth && clearStencil) {
            glf.ClearBufferfi(GL_DEPTH_STENCIL, 0, depthValue, stencilValue);
        } else if (clearDepth) {
            glf.ClearBufferfv(GL_DEPTH, 0, &depthValue);
        } else if (clearStencil) {
            glf.ClearBufferiv(GL_STENCIL, 0, &stencilValue);
        }
        glf.Enable(GL_SCISSOR_TEST); /* the device's state (gl_defaults) */
    }
    /* DONT_CARE: the contents need not be kept */
    GLenum discard[RHI_MAX_COLOR_TARGETS + 2];
    GLsizei n = 0;
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        if (pass->color[i].load == RHI_LOAD_DONT_CARE) {
            discard[n++] = GL_COLOR_ATTACHMENT0 + i;
        }
    }
    if (dt && !c->readOnlyDepth) {
        const bool depthGone = pass->depth.depthLoad == RHI_LOAD_DONT_CARE;
        const bool stencilGone = hasStencil && pass->depth.stencilLoad == RHI_LOAD_DONT_CARE;
        if (depthGone && (stencilGone || !hasStencil)) {
            discard[n++] = hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        } else if (depthGone) {
            discard[n++] = GL_DEPTH_ATTACHMENT;
        } else if (stencilGone) {
            discard[n++] = GL_STENCIL_ATTACHMENT;
        }
    }
    if (n) {
        glf.InvalidateFramebuffer(GL_FRAMEBUFFER, n, discard);
    }
    /* the store ops are not acted on: OpenGL keeps both aspects, which
     * DONT_CARE allows (as D3D12) */
    const RhiViewport vp = {0.0f, 0.0f, (float)areaW, (float)areaH, 0.0f, 1.0f};
    const RhiRect sc = {0, 0, areaW, areaH};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, &sc);
}

void rhi_cmd_end_render_pass(RhiCommandList cl)
{
    GlCmdList *c = gl_cmd(cl);
    if (c) {
        c->inPass = false;
        c->passBad = false;
        c->readOnlyDepth = false;
        /* every colour channel writable between passes, so no blit (a
         * resolve, the present's) depends on whether a driver applies the
         * last draw's colour mask to it; nothing is called when they are */
        gl_open_masks((1u << RHI_MAX_COLOR_TARGETS) - 1u, false, false);
    }
}

/* The renderer's clip space is OpenGL's under clip control (gl_internal.h):
 * no flip */
void rhi_cmd_set_viewport(RhiCommandList cl, const RhiViewport *v)
{
    GlCmdList *c = gl_cmd(cl);
    if (!c || !v) {
        return;
    }
    c->viewport = *v;
    c->viewportSet = true;
    gl_apply_viewport(v);
}

void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *r)
{
    GlCmdList *c = gl_cmd(cl);
    if (!c || !r) {
        return;
    }
    c->scissor = *r;
    c->scissorSet = true;
    gl_apply_scissor(r);
}

/* ----------------------------------------------------------------- state */
void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p)
{
    GlCmdList *c = gl_cmd(cl);
    GlPipeline *pp = d3dp_pool_get(&g_gl.pipelines, p.id);
    if (!c || !pp) {
        return;
    }
    if (c->pipeline != p.id) {
        c->pipeline = p.id;
        g_gl.counts.pipelineBinds++;
        /* a group the new pipeline has static: its own values replace the
         * ones given before, which a later dynamic pipeline gives again */
        c->dynValid &= pp->dynamic;
    }
    /* the next draw's dynamic values: the pipeline's own until a set */
    c->dynSet = 0;
    const RhiDepthStencilState *ds = &pp->depthStencil;
    c->depthWant = gl_depth_dyn(ds->depthTest, ds->depthWrite, ds->depthCompare);
    c->blendWant =
        gl_blend_dyn(&pp->blend[0], pp->colorCount ? pp->colorFormats[0] : RHI_FMT_RGBA8_UNORM);
}

void rhi_cmd_set_bind_group_offsets(RhiCommandList cl, uint32_t group, RhiBindGroup bg,
                                    const uint32_t *offsets, uint32_t count)
{
    GlCmdList *c = gl_cmd(cl);
    if (!c || group >= RHI_MAX_BIND_SLOTS) {
        return;
    }
    GlBindGroup *g = bg.id ? gl_get_bind_group(bg) : NULL;
    if (bg.id && !g) {
        GL_LOG("rhi_cmd_set_bind_group: bind group %08x is not from this frame", bg.id);
        bg.id = 0;
    }
    /* the layout's dynamic slots take the given offsets, the missing ones 0 */
    const uint32_t dyn = g ? g->dynCount : 0u;
    if (count > dyn) {
        GL_LOG("rhi_cmd_set_bind_group_offsets: %u offsets for %u dynamic slots", count, dyn);
        count = dyn;
    }
    uint32_t off[RHI_MAX_DYNAMIC_OFFSETS] = {0};
    for (uint32_t i = 0; i < count && offsets && i < RHI_MAX_DYNAMIC_OFFSETS; i++) {
        off[i] = offsets[i];
    }
    /* the group already bound with the same offsets is not bound again */
    if (c->groups[group] != bg.id || memcmp(c->offsets[group], off, sizeof(off)) != 0) {
        c->groups[group] = bg.id;
        memcpy(c->offsets[group], off, sizeof(off));
        c->groupDirty |= 1u << group;
    }
}

void rhi_cmd_set_bind_group(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    rhi_cmd_set_bind_group_offsets(cl, group, bg, NULL, 0);
}

void rhi_cmd_set_vertex_buffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    GlCmdList *c = gl_cmd(cl);
    GlBuffer *buf = gl_get_buffer(b);
    if (!c || !buf || binding >= RHI_MAX_VERTEX_ATTRS || offset >= buf->size) {
        return;
    }
    if (c->vbName[binding] != buf->name || c->vbOffset[binding] != offset) {
        c->vbName[binding] = buf->name;
        c->vbOffset[binding] = offset;
        c->vbDirty |= 1u << binding;
    }
}

/* GL_ELEMENT_ARRAY_BUFFER is the vertex array object's: bound at the draw,
 * after the pipeline's VAO */
void rhi_cmd_set_index_buffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    GlCmdList *c = gl_cmd(cl);
    GlBuffer *buf = gl_get_buffer(b);
    if (!c || !buf || offset >= buf->size) {
        return;
    }
    c->ibName = buf->name;
    c->ibOffset = offset;
    c->ibU32 = u32;
    c->ibDirty = true;
}

void rhi_cmd_set_stencil_ref(RhiCommandList cl, uint8_t ref)
{
    GlCmdList *c = gl_cmd(cl);
    if (c) {
        c->stencilRef = ref; /* glStencilFuncSeparate at the next draw */
    }
}

void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4])
{
    GlCmdList *c = gl_cmd(cl);
    if (!c || !rgba) {
        return;
    }
    memcpy(c->blendConst, rgba, sizeof(c->blendConst));
    c->blendConstSet = true;
    glf.BlendColor(rgba[0], rgba[1], rgba[2], rgba[3]);
}

/* Dynamic state (rhi.h; docs/port/RHI.md "Dynamic state"): the sets keep
 * what the next draw wants, for a bound pipeline with that group dynamic
 * (for any other they do nothing: its own values are in force); the draw
 * gives OpenGL what differs (gl_apply_raster) and counts the commands the
 * Vulkan backend would record (gl_count_dynamic). */
void rhi_cmd_set_depth_state(RhiCommandList cl, bool test, bool write, RhiCompare cmp)
{
    GlCmdList *c = gl_cmd(cl);
    const GlPipeline *p = c ? d3dp_pool_get(&g_gl.pipelines, c->pipeline) : NULL;
    if (!p || !(p->dynamic & RHI_DYN_DEPTH) || cmp >= RHI_CMP_COUNT) {
        return;
    }
    c->depthWant = gl_depth_dyn(test, write, cmp);
    c->dynSet |= RHI_DYN_DEPTH;
}

void rhi_cmd_set_blend_state(RhiCommandList cl, const RhiBlendState *blend)
{
    GlCmdList *c = gl_cmd(cl);
    const GlPipeline *p = c ? d3dp_pool_get(&g_gl.pipelines, c->pipeline) : NULL;
    if (!p || !blend || !(p->dynamic & RHI_DYN_BLEND) || blend->srcColor >= RHI_BF_COUNT ||
        blend->dstColor >= RHI_BF_COUNT || blend->srcAlpha >= RHI_BF_COUNT ||
        blend->dstAlpha >= RHI_BF_COUNT || blend->colorOp >= RHI_BO_COUNT ||
        blend->alphaOp >= RHI_BO_COUNT) {
        return;
    }
    c->blendWant = gl_blend_dyn(blend, p->colorCount ? p->colorFormats[0] : RHI_FMT_RGBA8_UNORM);
    c->dynSet |= RHI_DYN_BLEND;
}

/* ------------------------------------------------------------------ draws */
/* An indexed buffer range: true when the point already holds it (the
 * shadow), else the shadow takes it and the caller binds. */
static bool gl_range_bound(GlRangeBind *shadow, uint32_t point, GLuint name, uint64_t offset,
                           uint64_t size)
{
    if (point >= GL_SHADOW_POINTS) {
        return false;
    }
    GlRangeBind *h = &shadow[point];
    if (h->name == name && h->offset == offset && h->size == size) {
        return true;
    }
    h->name = name;
    h->offset = offset;
    h->size = size;
    return false;
}

/* The bind groups set since the last draw: each entry one bind
 * (gl_internal.h, "bind groups"), a dynamic uniform at its base plus the
 * group's bind-time offset of its rank; an entry whose point already holds
 * what it binds (the shadow, s.uniform ...) is skipped and counted
 * (RhiStats.entryBindsSkipped).  Groups are dirty at almost every draw (a
 * frame's groups are made per draw, the dynamic offsets move), while most
 * of their entries bind what the draw before bound. */
static void gl_bind_groups(GlCmdList *c)
{
    for (uint32_t g = 0; g < RHI_MAX_BIND_SLOTS && c->groupDirty; g++) {
        if (!(c->groupDirty & (1u << g)) || !c->groups[g]) {
            continue;
        }
        GlBindGroup *bg = gl_get_bind_group((RhiBindGroup){c->groups[g]});
        if (!bg) {
            continue;
        }
        const GlBindEntry *e = gl_bind_entries(bg);
        for (uint32_t k = 0; k < bg->count; k++, e++) {
            const uint32_t pt = e->point;
            bool skipped = false;
            switch (e->kind) {
            case GL_BIND_UNIFORM: {
                const uint64_t off =
                    e->offset + (e->dyn < RHI_MAX_DYNAMIC_OFFSETS ? c->offsets[g][e->dyn] : 0u);
                skipped = gl_range_bound(s.uniform, pt, e->name, off, e->size);
                if (!skipped) {
                    glf.BindBufferRange(GL_UNIFORM_BUFFER, pt, e->name, (GLintptr)off,
                                        (GLsizeiptr)e->size);
                }
                break;
            }
            case GL_BIND_STORAGE:
                skipped = gl_range_bound(s.storage, pt, e->name, e->offset, e->size);
                if (!skipped) {
                    glf.BindBufferRange(GL_SHADER_STORAGE_BUFFER, pt, e->name, (GLintptr)e->offset,
                                        (GLsizeiptr)e->size);
                }
                break;
            case GL_BIND_TEXTURE: {
                /* the texture (per target: a unit holds one of each) and the
                   sampler apart */
                const bool known = pt < GL_SHADOW_POINTS;
                const bool texSame =
                    known && s.texName[pt] == e->name && s.texTarget[pt] == e->target;
                const bool samplerSame = known && s.sampler[pt] == e->sampler;
                if (!texSame) {
                    glf.ActiveTexture(GL_TEXTURE0 + pt);
                    glf.BindTexture(e->target, e->name);
                }
                if (!samplerSame) {
                    glf.BindSampler(pt, e->sampler);
                }
                if (known) {
                    s.texName[pt] = e->name;
                    s.texTarget[pt] = e->target;
                    s.sampler[pt] = e->sampler;
                }
                skipped = texSame && samplerSame;
                break;
            }
            case GL_BIND_IMAGE:
                skipped = pt < GL_SHADOW_POINTS && s.image[pt] == e->name;
                if (!skipped) {
                    glf.BindImageTexture(pt, e->name, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
                    if (pt < GL_SHADOW_POINTS) {
                        s.image[pt] = e->name;
                    }
                }
                break;
            default:
                break;
            }
            g_gl.counts.entryBinds++;
            g_gl.counts.entryBindsSkipped += skipped;
        }
        g_gl.counts.bindGroupBinds++;
    }
    c->groupDirty = 0;
}

/* Everything a draw needs given to OpenGL: the program pipeline and VAO,
 * the vertex (and index) buffers into that VAO, the bind groups, the
 * fixed-function state.  NULL (the draw skipped) outside a usable pass or
 * without a live pipeline. */
static const GlPipeline *gl_flush(GlCmdList *c, bool indexed)
{
    const GlPipeline *p = d3dp_pool_get(&g_gl.pipelines, c->pipeline);
    if (!c->inPass || c->passBad || !p || (indexed && !c->ibName)) {
        if (gl_log_rate(&s.badDraws)) {
            GL_LOG("a draw skipped: %s (%u so far)",
                   !c->inPass   ? "outside a render pass"
                   : c->passBad ? "its render pass has no framebuffer"
                   : !p         ? "no pipeline"
                                : "no index buffer",
                   s.badDraws);
        }
        return NULL;
    }
    if (s.ppo != p->ppo) {
        glf.BindProgramPipeline(p->ppo);
        s.ppo = p->ppo;
    }
    if (s.vao != p->vao) {
        glf.BindVertexArray(p->vao);
        s.vao = p->vao;
    }
    if (c->vbVao != p->vao) {
        /* the buffer bindings are the VAO's state */
        c->vbVao = p->vao;
        c->vbDirty = 0xFFFFFFFFu;
        c->ibDirty = true;
    }
    const uint32_t dirty = c->vbDirty & p->bindingMask;
    for (uint32_t b = 0; dirty && b < RHI_MAX_VERTEX_ATTRS; b++) {
        if (!(dirty & (1u << b))) {
            continue;
        }
        if (!c->vbName[b]) {
            if (gl_log_rate(&s.unboundVb)) {
                GL_LOG("a draw with vertex binding %u unset (%u so far)", b, s.unboundVb);
            }
            continue;
        }
        glf.BindVertexBuffer(b, c->vbName[b], (GLintptr)c->vbOffset[b], (GLsizei)p->strides[b]);
        c->vbDirty &= ~(1u << b);
    }
    if (indexed && c->ibDirty) {
        glf.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->ibName);
        c->ibDirty = false;
    }
    gl_bind_groups(c);
    GlRaster w;
    gl_want_raster(c, p, &w);
    gl_apply_raster(&w);
    gl_count_dynamic(c, p);
    return p;
}

void rhi_cmd_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount)
{
    GlCmdList *c = gl_cmd(cl);
    const GlPipeline *p = c ? gl_flush(c, false) : NULL;
    if (!p) {
        return;
    }
    if (instanceCount > 1u) {
        glf.DrawArraysInstanced(p->mode, (GLint)firstVertex, (GLsizei)vertexCount,
                                (GLsizei)instanceCount);
    } else {
        glf.DrawArrays(p->mode, (GLint)firstVertex, (GLsizei)vertexCount);
    }
    g_gl.counts.draws++;
}

void rhi_cmd_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                          int32_t vertexOffset, uint32_t instanceCount)
{
    GlCmdList *c = gl_cmd(cl);
    const GlPipeline *p = c ? gl_flush(c, true) : NULL;
    if (!p) {
        return;
    }
    const GLenum type = c->ibU32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    const uintptr_t at = (uintptr_t)(c->ibOffset + (uint64_t)firstIndex * (c->ibU32 ? 4u : 2u));
    if (instanceCount > 1u) {
        glf.DrawElementsInstancedBaseVertex(p->mode, (GLsizei)indexCount, type, (const void *)at,
                                            (GLsizei)instanceCount, vertexOffset);
    } else {
        glf.DrawElementsBaseVertex(p->mode, (GLsizei)indexCount, type, (const void *)at,
                                   vertexOffset);
    }
    g_gl.counts.draws++;
}

/* ------------------------------------------------------------------ copies
 * Outside render passes.  The binding points they use are scratch
 * (gl_internal.h).  The texture to buffer copy and the resolve read and
 * draw through the cached framebuffer of their texture (gl_fbo_get), so a
 * copy changes no attachment; they leave framebuffer 0 bound. */
static bool gl_copy_refused(const char *what, const char *why)
{
    if (gl_log_rate(&s.badCopies)) {
        GL_LOG("%s: %s (%u so far)", what, why, s.badCopies);
    }
    return false;
}

static bool gl_region_fits(const RhiRect *r, uint32_t w, uint32_t h)
{
    return r->x >= 0 && r->y >= 0 && r->w > 0 && r->h > 0 && (uint32_t)r->x <= w &&
           (uint32_t)r->y <= h && r->w <= w - (uint32_t)r->x && r->h <= h - (uint32_t)r->y;
}

/* Into a device buffer: the copy waits for earlier reads of it (OpenGL
 * orders them) and the written range is seen by every later read. */
void rhi_cmd_copy_buffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                         uint64_t dstOffset, uint64_t size)
{
    GlCmdList *c = gl_cmd(cl);
    GlBuffer *sb = gl_get_buffer(src);
    GlBuffer *db = gl_get_buffer(dst);
    if (!c || !sb || !db || size == 0) {
        return;
    }
    if (db->memory == RHI_MEM_UPLOAD) {
        gl_copy_refused("rhi_cmd_copy_buffer", "an upload buffer cannot be a copy destination");
        return;
    }
    if (srcOffset > sb->size || size > sb->size - srcOffset || dstOffset > db->size ||
        size > db->size - dstOffset) {
        gl_copy_refused("rhi_cmd_copy_buffer", "the range is outside a buffer");
        return;
    }
    glf.BindBuffer(GL_COPY_READ_BUFFER, sb->name);
    glf.BindBuffer(GL_COPY_WRITE_BUFFER, db->name);
    glf.CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)srcOffset,
                          (GLintptr)dstOffset, (GLsizeiptr)size);
    glf.BindBuffer(GL_COPY_READ_BUFFER, 0);
    glf.BindBuffer(GL_COPY_WRITE_BUFFER, 0);
    g_gl.counts.copies++;
}

/* From the buffer bound as GL_PIXEL_UNPACK_BUFFER, rows rowPitch bytes
 * apart (GL_UNPACK_ROW_LENGTH; row by row when the pitch is no whole number
 * of texels); block formats as whole block rows. */
void rhi_cmd_copy_buffer_to_texture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                    uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    static const char what[] = "rhi_cmd_copy_buffer_to_texture";
    GlCmdList *c = gl_cmd(cl);
    GlBuffer *sb = gl_get_buffer(src);
    GlTexture *t = gl_get_texture(dst);
    if (!c || !sb || !t) {
        return;
    }
    const GlFormat *fm = gl_format(t->format);
    if (mip >= t->mips || t->samples > 1u || fm->depth) {
        gl_copy_refused(what, "not a single-sampled colour texture level");
        return;
    }
    const uint32_t lw = t->width >> mip ? t->width >> mip : 1u;
    const uint32_t lh = t->height >> mip ? t->height >> mip : 1u;
    if (!gl_region_fits(&region, lw, lh)) {
        gl_copy_refused(what, "the region is outside the level");
        return;
    }
    const uint32_t rows = fm->block ? (region.h + 3u) / 4u : region.h;
    const uint64_t rowBytes = fm->block ? (uint64_t)((region.w + 3u) / 4u) * fm->texelBytes
                                        : (uint64_t)region.w * fm->texelBytes;
    if (rowPitch < rowBytes || srcOffset > sb->size ||
        (uint64_t)rowPitch * (rows - 1u) + rowBytes > sb->size - srcOffset) {
        gl_copy_refused(what, "the rows are outside the buffer");
        return;
    }
    glf.BindBuffer(GL_PIXEL_UNPACK_BUFFER, sb->name);
    glf.ActiveTexture(GL_TEXTURE0); /* the scratch unit */
    glf.BindTexture(GL_TEXTURE_2D, t->name);
    if (fm->block) {
        if (rowPitch == rowBytes) {
            glf.CompressedTexSubImage2D(
                GL_TEXTURE_2D, (GLint)mip, region.x, region.y, (GLsizei)region.w, (GLsizei)region.h,
                fm->internal, (GLsizei)(rowBytes * rows), (const void *)(uintptr_t)srcOffset);
        } else {
            /* one row of blocks at a time: 4 texel rows, the last reaching
             * the region's edge */
            for (uint32_t r = 0; r < rows; r++) {
                const uint32_t h = region.h - r * 4u < 4u ? region.h - r * 4u : 4u;
                glf.CompressedTexSubImage2D(
                    GL_TEXTURE_2D, (GLint)mip, region.x, region.y + (GLint)(r * 4u),
                    (GLsizei)region.w, (GLsizei)h, fm->internal, (GLsizei)rowBytes,
                    (const void *)(uintptr_t)(srcOffset + (uint64_t)r * rowPitch));
            }
        }
    } else if (rowPitch % fm->texelBytes == 0) {
        glf.PixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(rowPitch / fm->texelBytes));
        glf.TexSubImage2D(GL_TEXTURE_2D, (GLint)mip, region.x, region.y, (GLsizei)region.w,
                          (GLsizei)region.h, fm->format, fm->type,
                          (const void *)(uintptr_t)srcOffset);
        glf.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        for (uint32_t r = 0; r < region.h; r++) {
            glf.TexSubImage2D(GL_TEXTURE_2D, (GLint)mip, region.x, region.y + (GLint)r,
                              (GLsizei)region.w, 1, fm->format, fm->type,
                              (const void *)(uintptr_t)(srcOffset + (uint64_t)r * rowPitch));
        }
    }
    glf.BindTexture(GL_TEXTURE_2D, 0);
    glf.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    g_gl.counts.copies++;
}

void rhi_cmd_copy_texture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                          int32_t dstX, int32_t dstY)
{
    static const char what[] = "rhi_cmd_copy_texture";
    GlCmdList *c = gl_cmd(cl);
    GlTexture *st = gl_get_texture(src);
    GlTexture *dt = gl_get_texture(dst);
    if (!c || !st || !dt) {
        return;
    }
    if (st->samples > 1u || dt->samples > 1u) {
        gl_copy_refused(what, "a multisampled texture is not copied (resolve it)");
        return;
    }
    const RhiRect dr = {dstX, dstY, srcRegion.w, srcRegion.h};
    if (!gl_region_fits(&srcRegion, st->width, st->height) ||
        !gl_region_fits(&dr, dt->width, dt->height)) {
        gl_copy_refused(what, "a region is outside its texture");
        return;
    }
    /* depth and stencil together for a depth-stencil format */
    glf.CopyImageSubData(st->name, st->target, 0, srcRegion.x, srcRegion.y, 0, dt->name, dt->target,
                         0, dstX, dstY, 0, (GLsizei)srcRegion.w, (GLsizei)srcRegion.h, 1);
    g_gl.counts.copies++;
}

/* Through the texture's cached framebuffer (its colour at attachment 0 and
 * read buffer, or its depth alone) into the buffer bound as
 * GL_PIXEL_PACK_BUFFER (glReadPixels), rows rowPitch bytes apart; the depth
 * aspect as 32-bit floats; integer formats as integers.  No memory barrier
 * after it, also into a buffer the shaders read (the VU pre-pass's record,
 * read as a storage buffer by the next draws): glReadPixels is not a shader
 * write, and OpenGL orders a GL command's buffer writes before the commands
 * after it; glMemoryBarrier is for the writes of shaders (image stores,
 * buffer variables, atomic counters: the glMemoryBarrier reference page).
 * A source the shaders wrote as a storage image took its barrier at its
 * RHI_STATE_STORAGE transition (rhi_cmd_barrier). */
void rhi_cmd_copy_texture_to_buffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                    RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                    uint32_t rowPitch)
{
    static const char what[] = "rhi_cmd_copy_texture_to_buffer";
    GlCmdList *c = gl_cmd(cl);
    GlTexture *t = gl_get_texture(src);
    GlBuffer *db = gl_get_buffer(dst);
    if (!c || !t || !db) {
        return;
    }
    const GlFormat *fm = gl_format(t->format);
    if (db->memory == RHI_MEM_UPLOAD || t->samples > 1u || fm->block ||
        (aspect == RHI_ASPECT_DEPTH && !fm->depth)) {
        gl_copy_refused(what, "not a single-sampled texture into a device or readback buffer");
        return;
    }
    if (!gl_region_fits(&region, t->width, t->height)) {
        gl_copy_refused(what, "the region is outside the texture");
        return;
    }
    const uint32_t texel = fm->depth ? 4u : fm->texelBytes;
    const GLenum format = fm->depth ? GL_DEPTH_COMPONENT : fm->format;
    const GLenum type = fm->depth ? GL_FLOAT : fm->type;
    const uint64_t rowBytes = (uint64_t)region.w * texel;
    if (rowPitch < rowBytes || dstOffset > db->size ||
        (uint64_t)rowPitch * (region.h - 1u) + rowBytes > db->size - dstOffset) {
        gl_copy_refused(what, "the rows are outside the buffer");
        return;
    }
    GlTexture *const one[1] = {t};
    const GLuint fbo = fm->depth
                           ? gl_fbo_get(NULL, 0, t, t->width, t->height, what, "nothing copied")
                           : gl_fbo_get(one, 1, NULL, t->width, t->height, what, "nothing copied");
    if (!fbo) {
        gl_copy_refused(what, "the texture cannot be read through a framebuffer");
    } else {
        glf.BindBuffer(GL_PIXEL_PACK_BUFFER, db->name);
        if (rowPitch % texel == 0) {
            glf.PixelStorei(GL_PACK_ROW_LENGTH, (GLint)(rowPitch / texel));
            glf.ReadPixels(region.x, region.y, (GLsizei)region.w, (GLsizei)region.h, format, type,
                           (void *)(uintptr_t)dstOffset);
            glf.PixelStorei(GL_PACK_ROW_LENGTH, 0);
        } else {
            for (uint32_t r = 0; r < region.h; r++) {
                glf.ReadPixels(region.x, region.y + (GLint)r, (GLsizei)region.w, 1, format, type,
                               (void *)(uintptr_t)(dstOffset + (uint64_t)r * rowPitch));
            }
        }
        glf.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        g_gl.counts.copies++;
    }
    glf.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* A multisampled RGBA8 texture into a single-sampled one of its size:
 * glBlitFramebuffer from the source's cached framebuffer to the
 * destination's (gl_fbo_get), the scissor test off for it (each pixel the
 * mean of its samples). */
void rhi_cmd_resolve_color(RhiCommandList cl, RhiTexture src, RhiTexture dst)
{
    GlCmdList *c = gl_cmd(cl);
    GlTexture *st = gl_get_texture(src);
    GlTexture *dt = gl_get_texture(dst);
    if (!c || !st || !dt) {
        return;
    }
    const GlFormat *fm = gl_format(st->format);
    if (st->samples <= 1u || dt->samples > 1u || st->width != dt->width ||
        st->height != dt->height || st->format != dt->format || fm->depth || fm->integer) {
        gl_copy_refused("rhi_cmd_resolve_color",
                        "needs a multisampled colour texture and a single-sampled one of its size "
                        "and format");
        return;
    }
    static const char what[] = "rhi_cmd_resolve_color";
    GlTexture *const from[1] = {st};
    GlTexture *const to[1] = {dt};
    /* each framebuffer holds its texture at attachment 0, the read and draw
       buffer (gl_fbo_get) */
    const GLuint srcFbo =
        gl_fbo_get(from, 1, NULL, st->width, st->height, what, "nothing resolved");
    const GLuint dstFbo =
        srcFbo ? gl_fbo_get(to, 1, NULL, dt->width, dt->height, what, "nothing resolved") : 0u;
    if (srcFbo && dstFbo) {
        glf.BindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
        glf.BindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
        glf.Disable(GL_SCISSOR_TEST);
        glf.BlitFramebuffer(0, 0, (GLint)st->width, (GLint)st->height, 0, 0, (GLint)dt->width,
                            (GLint)dt->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glf.Enable(GL_SCISSOR_TEST);
        g_gl.counts.copies++;
    } else {
        gl_copy_refused(what, "a texture cannot be used through a framebuffer");
    }
    glf.BindFramebuffer(GL_FRAMEBUFFER, 0);
}
