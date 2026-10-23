/* gl_pipeline.c: pipelines of the OpenGL backend (gl_internal.h).
 *
 * A pipeline is a program pipeline object pairing the two separable
 * programs (rhi_create_shader), a vertex array object holding the vertex
 * layout (glVertexAttribFormat / glVertexAttribIFormat, glVertexAttribBinding,
 * glVertexBindingDivisor: ARB_vertex_attrib_binding, core in 4.3; the
 * buffers are bound per draw with glBindVertexBuffer and the strides kept
 * here), and the fixed-function state in the RHI's own terms, which the
 * command code applies at each draw (gl_cmd.c).  OpenGL compiles nothing
 * here (the programs are linked; the driver may still specialise at the
 * first draw), so rhi_create_pipelines makes its batch on the calling
 * thread. */
#include "gl_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- enum maps */
GLenum gl_blend_factor(RhiBlendFactor f)
{
    static const GLenum map[RHI_BF_COUNT] = {
        [RHI_BF_ZERO] = GL_ZERO,
        [RHI_BF_ONE] = GL_ONE,
        [RHI_BF_SRC_COLOR] = GL_SRC_COLOR,
        [RHI_BF_ONE_MINUS_SRC_COLOR] = GL_ONE_MINUS_SRC_COLOR,
        [RHI_BF_DST_COLOR] = GL_DST_COLOR,
        [RHI_BF_ONE_MINUS_DST_COLOR] = GL_ONE_MINUS_DST_COLOR,
        [RHI_BF_SRC_ALPHA] = GL_SRC_ALPHA,
        [RHI_BF_ONE_MINUS_SRC_ALPHA] = GL_ONE_MINUS_SRC_ALPHA,
        [RHI_BF_DST_ALPHA] = GL_DST_ALPHA,
        [RHI_BF_ONE_MINUS_DST_ALPHA] = GL_ONE_MINUS_DST_ALPHA,
        /* the constant's alpha where an alpha factor names the colour, as
         * Vulkan's and D3D12's */
        [RHI_BF_CONSTANT] = GL_CONSTANT_COLOR,
        [RHI_BF_ONE_MINUS_CONSTANT] = GL_ONE_MINUS_CONSTANT_COLOR,
        [RHI_BF_SRC1_COLOR] = GL_SRC1_COLOR,
        [RHI_BF_ONE_MINUS_SRC1_COLOR] = GL_ONE_MINUS_SRC1_COLOR,
        [RHI_BF_SRC1_ALPHA] = GL_SRC1_ALPHA,
        [RHI_BF_ONE_MINUS_SRC1_ALPHA] = GL_ONE_MINUS_SRC1_ALPHA,
    };
    return (unsigned)f < RHI_BF_COUNT ? map[f] : GL_ONE;
}

GLenum gl_blend_op(RhiBlendOp op)
{
    switch (op) {
    case RHI_BO_SUBTRACT:
        return GL_FUNC_SUBTRACT;
    case RHI_BO_REVERSE_SUBTRACT:
        return GL_FUNC_REVERSE_SUBTRACT;
    case RHI_BO_ADD:
    default:
        return GL_FUNC_ADD;
    }
}

GLenum gl_compare(RhiCompare c)
{
    static const GLenum map[RHI_CMP_COUNT] = {
        [RHI_CMP_NEVER] = GL_NEVER,     [RHI_CMP_LESS] = GL_LESS,
        [RHI_CMP_EQUAL] = GL_EQUAL,     [RHI_CMP_LEQUAL] = GL_LEQUAL,
        [RHI_CMP_GREATER] = GL_GREATER, [RHI_CMP_NOTEQUAL] = GL_NOTEQUAL,
        [RHI_CMP_GEQUAL] = GL_GEQUAL,   [RHI_CMP_ALWAYS] = GL_ALWAYS,
    };
    return (unsigned)c < RHI_CMP_COUNT ? map[c] : GL_ALWAYS;
}

GLenum gl_stencil_op(RhiStencilOp op)
{
    static const GLenum map[RHI_SO_COUNT] = {
        [RHI_SO_KEEP] = GL_KEEP,           [RHI_SO_ZERO] = GL_ZERO,
        [RHI_SO_REPLACE] = GL_REPLACE,     [RHI_SO_INCR_WRAP] = GL_INCR_WRAP,
        [RHI_SO_DECR_WRAP] = GL_DECR_WRAP, [RHI_SO_INCR_CLAMP] = GL_INCR,
        [RHI_SO_DECR_CLAMP] = GL_DECR,     [RHI_SO_INVERT] = GL_INVERT,
    };
    return (unsigned)op < RHI_SO_COUNT ? map[op] : GL_KEEP;
}

static const GLenum s_topology[RHI_TOPO_COUNT] = {
    [RHI_TOPO_TRIANGLE_LIST] = GL_TRIANGLES,
    [RHI_TOPO_TRIANGLE_STRIP] = GL_TRIANGLE_STRIP,
    [RHI_TOPO_LINE_LIST] = GL_LINES,
    [RHI_TOPO_POINT_LIST] = GL_POINTS,
};

/* A vertex format: components, type, normalised, read as integers by the
 * shader (glVertexAttribIFormat). */
typedef struct GlVertexFormat {
    GLint size;
    GLenum type;
    GLboolean normalized;
    bool integer;
} GlVertexFormat;

static const GlVertexFormat s_vertexFormats[RHI_VTX_COUNT] = {
    [RHI_VTX_F32x1] = {1, GL_FLOAT, GL_FALSE, false},
    [RHI_VTX_F32x2] = {2, GL_FLOAT, GL_FALSE, false},
    [RHI_VTX_F32x3] = {3, GL_FLOAT, GL_FALSE, false},
    [RHI_VTX_F32x4] = {4, GL_FLOAT, GL_FALSE, false},
    [RHI_VTX_U8x4_UNORM] = {4, GL_UNSIGNED_BYTE, GL_TRUE, false},
    [RHI_VTX_U8x4_UINT] = {4, GL_UNSIGNED_BYTE, GL_FALSE, true},
    [RHI_VTX_U16x2_UINT] = {2, GL_UNSIGNED_SHORT, GL_FALSE, true},
    [RHI_VTX_U32x1] = {1, GL_UNSIGNED_INT, GL_FALSE, true},
};

/* -------------------------------------------------------------- checks */
static bool gl_valid_pipeline_desc(const RhiPipelineDesc *d)
{
    if (d->topology >= RHI_TOPO_COUNT || d->colorCount > RHI_MAX_COLOR_TARGETS ||
        d->depthFormat >= RHI_FMT_COUNT || d->vertexAttrCount > RHI_MAX_VERTEX_ATTRS ||
        d->vertexBindingCount > RHI_MAX_VERTEX_ATTRS || d->layoutCount > RHI_MAX_BIND_SLOTS ||
        (d->vertexAttrCount && !d->vertexAttrs) || (d->vertexBindingCount && !d->vertexBindings) ||
        (d->layoutCount && !d->layouts)) {
        return false;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        const RhiBlendState *b = &d->blend[i];
        if (d->colorFormats[i] <= RHI_FMT_UNKNOWN || d->colorFormats[i] >= RHI_FMT_COUNT ||
            b->srcColor >= RHI_BF_COUNT || b->dstColor >= RHI_BF_COUNT ||
            b->srcAlpha >= RHI_BF_COUNT || b->dstAlpha >= RHI_BF_COUNT ||
            b->colorOp >= RHI_BO_COUNT || b->alphaOp >= RHI_BO_COUNT) {
            return false;
        }
    }
    const RhiDepthStencilState *ds = &d->depthStencil;
    const RhiStencilFace *faces[2] = {&ds->front, &ds->back};
    for (int i = 0; i < 2; i++) {
        if (faces[i]->compare >= RHI_CMP_COUNT || faces[i]->pass >= RHI_SO_COUNT ||
            faces[i]->fail >= RHI_SO_COUNT || faces[i]->depthFail >= RHI_SO_COUNT) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        if (d->vertexAttrs[i].format >= RHI_VTX_COUNT ||
            d->vertexAttrs[i].binding >= RHI_MAX_VERTEX_ATTRS ||
            d->vertexAttrs[i].location >= RHI_MAX_VERTEX_ATTRS) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        if (d->vertexBindings[i].binding >= RHI_MAX_VERTEX_ATTRS) {
            return false;
        }
    }
    return ds->depthCompare < RHI_CMP_COUNT;
}

static bool gl_is_dual_factor(RhiBlendFactor f)
{
    return f == RHI_BF_SRC1_COLOR || f == RHI_BF_ONE_MINUS_SRC1_COLOR || f == RHI_BF_SRC1_ALPHA ||
           f == RHI_BF_ONE_MINUS_SRC1_ALPHA;
}

static bool gl_uses_dual_source(const RhiPipelineDesc *d)
{
    for (uint32_t i = 0; i < d->colorCount; i++) {
        const RhiBlendState *b = &d->blend[i];
        if (b->enable && (gl_is_dual_factor(b->srcColor) || gl_is_dual_factor(b->dstColor) ||
                          gl_is_dual_factor(b->srcAlpha) || gl_is_dual_factor(b->dstAlpha))) {
            return true;
        }
    }
    return false;
}

/* The vertex program's active inputs (GL_PROGRAM_INPUT) that no attribute
 * of the description feeds: OpenGL would read the attribute's current
 * value, (0, 0, 0, 1), where Vulkan reads undefined data.  Logged, not
 * refused (DXC gives every input its location, so this is a description
 * that names the wrong location). */
static void gl_check_inputs(const RhiPipelineDesc *d, GLuint program, const char *name)
{
    static uint32_t s_unfed;
    GLint n = 0;
    glf.GetProgramInterfaceiv(program, GL_PROGRAM_INPUT, GL_ACTIVE_RESOURCES, &n);
    for (GLint i = 0; i < n && i < 64; i++) {
        const GLenum prop = GL_LOCATION;
        GLint loc = -1;
        glf.GetProgramResourceiv(program, GL_PROGRAM_INPUT, (GLuint)i, 1, &prop, 1, NULL, &loc);
        if (loc < 0) {
            continue; /* a built-in (gl_VertexID, gl_InstanceID) */
        }
        bool fed = false;
        for (uint32_t k = 0; k < d->vertexAttrCount && !fed; k++) {
            fed = d->vertexAttrs[k].location == (uint32_t)loc;
        }
        if (!fed && gl_log_rate(&s_unfed)) {
            GL_LOG("pipeline %s: the vertex shader reads location %d, which no vertex attribute "
                   "feeds (%u so far)",
                   name, loc, s_unfed);
        }
    }
}

/* a creation that failed other than for a full pool (RhiStats
 * pipelineFailures): err the GL error, 0 for a description this backend
 * refused (its line already written) */
static void gl_pipe_failed(GLenum err)
{
    g_gl.counts.pipelineFailures++;
    g_gl.counts.pipelineLastError = (int64_t)err;
}

/* The first GL error pending (all of them drained), GL_NO_ERROR for none. */
static GLenum gl_first_error(void)
{
    GLenum first = GL_NO_ERROR;
    for (int i = 0; i < 16; i++) {
        const GLenum e = glf.GetError();
        if (e == GL_NO_ERROR) {
            break;
        }
        if (first == GL_NO_ERROR) {
            first = e;
        }
    }
    return first;
}

/* ------------------------------------------------------------ creation */
static RhiPipeline gl_pipe_create(const RhiPipelineDesc *d, float *ms)
{
    RhiPipeline out = {0};
    const char *name = d && d->debugName ? d->debugName : "?";
    if (ms) {
        *ms = 0.0f;
    }
    if (!g_gl.initialised) {
        return out;
    }
    if (!d || !gl_valid_pipeline_desc(d)) {
        GL_LOG("pipeline %s: invalid description", name);
        gl_pipe_failed(GL_NO_ERROR);
        return out;
    }
    /* the destination read: a storage image in rasterization order only
     * (ARB_fragment_shader_interlock), with no colour target (the fragment
     * shader writes the target through its image); GLSL has no input
     * attachments */
    if (d->dstRead == RHI_DST_READ_ATTACHMENT ||
        (d->dstRead == RHI_DST_READ_STORAGE &&
         (!g_gl.limits.fragmentInterlock || d->colorCount != 0)) ||
        d->dstRead >= RHI_DST_READ_COUNT) {
        GL_LOG("pipeline %s: destination read %d not available", name, (int)d->dstRead);
        gl_pipe_failed(GL_NO_ERROR);
        return out;
    }
    if (gl_uses_dual_source(d) && !g_gl.limits.dualSourceBlend) {
        GL_LOG("pipeline %s: dual-source blend factors without dual-source blending", name);
        gl_pipe_failed(GL_NO_ERROR);
        return out;
    }
    GlShader *vs = gl_get_shader(d->vertex);
    GlShader *fs = d->fragment.id ? gl_get_shader(d->fragment) : NULL;
    if (!vs || vs->stage != RHI_STAGE_VERTEX) {
        GL_LOG("pipeline %s: no vertex shader", name);
        gl_pipe_failed(GL_NO_ERROR);
        return out;
    }
    if (d->fragment.id && (!fs || fs->stage != RHI_STAGE_FRAGMENT)) {
        GL_LOG("pipeline %s: the fragment shader is not one", name);
        gl_pipe_failed(GL_NO_ERROR);
        return out;
    }
    GlPipeline *p = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_gl.pipelines, (void **)&p);
    if (!id) {
        /* the first 8 then every 100th, as the other backends' line; the
           10 s summary has the count */
        const uint64_t full = g_gl.pipelines.full;
        if (full <= 8u || full % 100u == 0u) {
            GL_LOG("pipeline pool full (%u); %llu refused so far%s", g_gl.pipelines.cap,
                   (unsigned long long)full,
                   full == 8u ? "; from here every 100th is written" : "");
        }
        return out;
    }
    memset(p, 0, sizeof(*p));
    const uint64_t t0 = gl_now_ns();
    gl_check("before rhi_create_pipeline");

    glf.GenProgramPipelines(1, &p->ppo);
    glf.UseProgramStages(p->ppo, GL_VERTEX_SHADER_BIT, vs->program);
    glf.UseProgramStages(p->ppo, GL_FRAGMENT_SHADER_BIT, fs ? fs->program : 0u);

    /* the vertex layout; the VAO bound before is bound again after */
    GLint prevVao = 0;
    glf.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glf.GenVertexArrays(1, &p->vao);
    glf.BindVertexArray(p->vao);
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        const RhiVertexAttr *a = &d->vertexAttrs[i];
        const GlVertexFormat *vf = &s_vertexFormats[a->format];
        glf.EnableVertexAttribArray(a->location);
        if (vf->integer) {
            glf.VertexAttribIFormat(a->location, vf->size, vf->type, a->offset);
        } else {
            glf.VertexAttribFormat(a->location, vf->size, vf->type, vf->normalized, a->offset);
        }
        glf.VertexAttribBinding(a->location, a->binding);
        p->bindingMask |= 1u << a->binding;
    }
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        const RhiVertexBinding *b = &d->vertexBindings[i];
        p->strides[b->binding] = b->stride;
        glf.VertexBindingDivisor(b->binding, b->perInstance ? 1u : 0u);
    }
    glf.BindVertexArray((GLuint)prevVao);
    const GLenum err = gl_first_error();
    if (err != GL_NO_ERROR) {
        GL_LOG("pipeline %s not created: GL error 0x%04x", name, (unsigned)err);
        glf.DeleteProgramPipelines(1, &p->ppo);
        glf.DeleteVertexArrays(1, &p->vao);
        d3dp_pool_release(&g_gl.pipelines, id);
        gl_pipe_failed(err);
        return out;
    }
    gl_check_inputs(d, vs->program, name);

    p->mode = s_topology[d->topology];
    memcpy(p->blend, d->blend, sizeof(p->blend));
    p->depthStencil = d->depthStencil;
    memcpy(p->colorFormats, d->colorFormats, sizeof(p->colorFormats));
    p->colorCount = d->colorCount;
    p->depthFormat = d->depthFormat;
    p->samples = d->samples > 1u ? d->samples : 1u;
    p->dstRead = d->dstRead;
    /* as Vulkan's (vk_pipeline.c): depth dynamic only with a depth target,
     * blend only on a pipeline with one colour target */
    if ((d->dynamic & RHI_DYN_DEPTH) && g_gl.limits.dynamicDepth &&
        d->depthFormat != RHI_FMT_UNKNOWN) {
        p->dynamic |= RHI_DYN_DEPTH;
    }
    if ((d->dynamic & RHI_DYN_BLEND) && g_gl.limits.dynamicBlend && d->colorCount == 1) {
        p->dynamic |= RHI_DYN_BLEND;
    }
    p->layoutCount = d->layoutCount;
    for (uint32_t g = 0; g < d->layoutCount; g++) {
        p->layoutIds[g] = d->layouts[g].id;
    }
    /* the VAO has been bound, so it exists for the label; the program
     * pipeline object only once a draw binds it */
    if (g_gl.debug && d->debugName) {
        glf.ObjectLabel(GL_VERTEX_ARRAY, p->vao, -1, d->debugName);
    }
    if (ms) {
        *ms = (float)((double)(gl_now_ns() - t0) / 1e6);
    }
    out.id = id;
    return out;
}

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *d)
{
    return gl_pipe_create(d, NULL);
}

/* Several at once (rhi.h): OpenGL has no threads for this, so the batch
 * is made on the calling thread in order, the caller's wait function
 * called about every RHI_PIPELINE_WAIT_MS with the count made so far. */
uint32_t rhi_create_pipelines(const RhiPipelineDesc *descs, uint32_t n, RhiPipeline *out,
                              RhiPipelineBatch *batch)
{
    uint32_t made = 0;
    if (batch) {
        batch->threads = 1;
    }
    if (!descs || !out || n == 0) {
        return 0;
    }
    uint64_t last = gl_now_ns();
    for (uint32_t i = 0; i < n; i++) {
        float ms = 0.0f;
        out[i] = gl_pipe_create(&descs[i], &ms);
        made += out[i].id != 0;
        if (batch && batch->ms) {
            batch->ms[i] = ms;
        }
        const uint64_t now = gl_now_ns();
        if (batch && batch->wait && i + 1 < n &&
            now - last >= (uint64_t)RHI_PIPELINE_WAIT_MS * 1000000ull) {
            batch->wait(batch->ctx, i + 1);
            last = gl_now_ns();
        }
    }
    return made;
}

void rhi_destroy_pipeline(RhiPipeline h)
{
    GlPipeline *p = d3dp_pool_get(&g_gl.pipelines, h.id);
    if (!p) {
        return;
    }
    /* a list recorded this frame may still name them */
    gl_defer(GL_GARBAGE_PIPELINE, p->ppo);
    gl_defer(GL_GARBAGE_VAO, p->vao);
    d3dp_pool_release(&g_gl.pipelines, h.id);
}
