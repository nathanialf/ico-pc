/* gl_resource.c: buffers, textures, samplers, separable shader programs,
 * bind group layouts, transient bind groups, deferred deletes and the
 * synchronous readback of the OpenGL backend (gl_internal.h). */
#include "gl_internal.h"
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------- formats
 * BGRA8 is stored as RGBA8 and moved as GL_BGRA, so its bytes in buffers
 * are B, G, R, A as on the other backends; the swapchain itself is RGBA8
 * (gl_swapchain.c, rhi_swapchain_format). */
static const GlFormat s_formats[RHI_FMT_COUNT] = {
    [RHI_FMT_UNKNOWN] = {0, 0, 0, 0, false, false, false, false},
    [RHI_FMT_RGBA8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false, false, false},
    [RHI_FMT_RGBA8_UINT] = {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 4, false, false, true,
                            false},
    [RHI_FMT_R8_UNORM] = {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, false, false, false, false},
    [RHI_FMT_R8_UINT] = {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 1, false, false, true, false},
    [RHI_FMT_R16_UINT] = {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 2, false, false, true,
                          false},
    [RHI_FMT_RGBA16F] = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, false, false, false, false},
    [RHI_FMT_D32F] = {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4, true, false, false,
                      false},
    [RHI_FMT_D32F_S8] = {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV,
                         8, true, true, false, false},
    [RHI_FMT_BGRA8_UNORM] = {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 4, false, false, false, false},
    [RHI_FMT_BC1_UNORM] = {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 8, false, false, false, true},
    [RHI_FMT_BC2_UNORM] = {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, 16, false, false, false, true},
    [RHI_FMT_BC3_UNORM] = {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 16, false, false, false, true},
    [RHI_FMT_BC7_UNORM] = {GL_COMPRESSED_RGBA_BPTC_UNORM, 0, 0, 16, false, false, false, true},
    [RHI_FMT_R32_UINT] = {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 4, false, false, true, false},
    [RHI_FMT_RGBA32_UINT] = {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 16, false, false, true,
                             false},
};

const GlFormat *gl_format(RhiFormat f)
{
    return &s_formats[(f > RHI_FMT_UNKNOWN && f < RHI_FMT_COUNT) ? f : RHI_FMT_UNKNOWN];
}

static void gl_label(GLenum kind, GLuint name, const char *debugName)
{
    if (g_gl.debug && debugName && name) {
        glf.ObjectLabel(kind, name, -1, debugName);
    }
}

/* ----------------------------------------------------- deferred deletes */
static void gl_delete_now(uint8_t kind, GLuint name)
{
    switch (kind) {
    case GL_GARBAGE_BUFFER:
        gl_forget_units();
        glf.DeleteBuffers(1, &name);
        break;
    case GL_GARBAGE_TEXTURE:
        gl_fbo_forget_texture(name);
        gl_forget_units();
        glf.DeleteTextures(1, &name);
        break;
    case GL_GARBAGE_SAMPLER:
        gl_forget_units();
        glf.DeleteSamplers(1, &name);
        break;
    case GL_GARBAGE_PROGRAM:
        glf.DeleteProgram(name);
        break;
    case GL_GARBAGE_PIPELINE:
        glf.DeleteProgramPipelines(1, &name);
        break;
    case GL_GARBAGE_VAO:
        glf.DeleteVertexArrays(1, &name);
        break;
    case GL_GARBAGE_FRAMEBUFFER:
        glf.DeleteFramebuffers(1, &name);
        break;
    default:
        break;
    }
}

void gl_defer(uint8_t kind, GLuint name)
{
    if (!name || !g_gl.initialised) {
        return;
    }
    GlFrame *f = gl_cur_frame();
    if (f->garbageCount == f->garbageCap) {
        const uint32_t cap = f->garbageCap ? f->garbageCap * 2u : 256u;
        GlGarbage *g = realloc(f->garbage, cap * sizeof(*g));
        if (!g) {
            /* GL keeps the object for the queued commands that use it;
             * only its name can come back early */
            static uint32_t s_oom;
            if (gl_log_rate(&s_oom)) {
                GL_LOG("out of memory for deferred deletes: deleting at once (%u so far)", s_oom);
            }
            gl_delete_now(kind, name);
            return;
        }
        f->garbage = g;
        f->garbageCap = cap;
    }
    f->garbage[f->garbageCount].kind = kind;
    f->garbage[f->garbageCount].name = name;
    f->garbageCount++;
}

void gl_destroy_garbage(GlFrame *f)
{
    for (uint32_t i = 0; i < f->garbageCount; i++) {
        gl_delete_now(f->garbage[i].kind, f->garbage[i].name);
    }
    f->garbageCount = 0;
}

/* --------------------------------------------------------------- buffers */
GlBuffer *gl_get_buffer(RhiBuffer b)
{
    return d3dp_pool_get(&g_gl.buffers, b.id);
}

static RhiBuffer createBuffer(const RhiBufferDesc *desc);

/* timed into RhiStats.createNs */
RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    const uint64_t t0 = gl_now_ns();
    const RhiBuffer b = createBuffer(desc);
    g_gl.counts.createNs += gl_now_ns() - t0;
    return b;
}

/* Immutable storage (glBufferStorage): UPLOAD is mapped for writing,
 * READBACK for reading, both persistent and coherent, once at creation
 * (rhi.h: the same pointer each time); DEVICE buffers are written by
 * copies only. */
static RhiBuffer createBuffer(const RhiBufferDesc *desc)
{
    RhiBuffer out = {0};
    if (!desc || desc->size == 0 || !g_gl.initialised) {
        return out;
    }
    GlBuffer *b = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_gl.buffers, (void **)&b);
    if (!id) {
        GL_LOG("buffer pool full");
        return out;
    }
    memset(b, 0, sizeof(*b));
    const uint64_t size = d3dp_align_up(desc->size, 16u);
    GLbitfield flags = 0;
    if (desc->memory == RHI_MEM_UPLOAD) {
        flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    } else if (desc->memory == RHI_MEM_READBACK) {
        flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    }
    gl_check("before rhi_create_buffer");
    glf.GenBuffers(1, &b->name);
    glf.BindBuffer(GL_COPY_WRITE_BUFFER, b->name);
    glf.BufferStorage(GL_COPY_WRITE_BUFFER, (GLsizeiptr)size, NULL, flags);
    if (flags) {
        b->mapped = glf.MapBufferRange(GL_COPY_WRITE_BUFFER, 0, (GLsizeiptr)size, flags);
    }
    glf.BindBuffer(GL_COPY_WRITE_BUFFER, 0);
    if (!gl_check("rhi_create_buffer") || (flags && !b->mapped)) {
        GL_LOG("buffer %s: %llu bytes not made", desc->debugName ? desc->debugName : "?",
               (unsigned long long)size);
        glf.DeleteBuffers(1, &b->name);
        d3dp_pool_release(&g_gl.buffers, id);
        return out;
    }
    b->size = size;
    b->usage = desc->usage;
    b->memory = desc->memory;
    gl_label(GL_BUFFER, b->name, desc->debugName);
    g_gl.counts.buffersCreated++;
    out.id = id;
    return out;
}

void rhi_destroy_buffer(RhiBuffer h)
{
    GlBuffer *b = gl_get_buffer(h);
    if (!b) {
        return;
    }
    gl_defer(GL_GARBAGE_BUFFER, b->name); /* deleting it unmaps it */
    g_gl.counts.buffersDestroyed++;
    d3dp_pool_release(&g_gl.buffers, h.id);
}

void *rhi_map_buffer(RhiBuffer h)
{
    GlBuffer *b = gl_get_buffer(h);
    return b ? b->mapped : NULL;
}

void rhi_unmap_buffer(RhiBuffer h)
{
    (void)h; /* persistently mapped and coherent */
}

/* -------------------------------------------------------------- textures */
GlTexture *gl_get_texture(RhiTexture t)
{
    return d3dp_pool_get(&g_gl.textures, t.id);
}

/* the levels a w x h chain can have */
static uint32_t gl_max_levels(uint32_t w, uint32_t h)
{
    uint32_t m = w > h ? w : h, n = 1;
    while (m > 1u) {
        m >>= 1;
        n++;
    }
    return n;
}

/* A texture's own sampling state, in force where no sampler object is
 * bound (integer, multisampled and UINT-view textures, a group without a
 * sampler): point sampling, clamped, its levels only; integer textures are
 * incomplete under any filter but NEAREST. */
static void gl_texture_params(GLenum target, uint32_t mips)
{
    if (target != GL_TEXTURE_2D) {
        return; /* multisampled textures take no sampling state */
    }
    glf.TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glf.TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glf.TexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glf.TexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glf.TexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glf.TexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)(mips - 1u));
}

static RhiTexture createTexture(const RhiTextureDesc *desc, bool backbuffer);

/* timed into RhiStats.createNs */
RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    const uint64_t t0 = gl_now_ns();
    const RhiTexture t = createTexture(desc, false);
    g_gl.counts.createNs += gl_now_ns() - t0;
    return t;
}

static RhiTexture createTexture(const RhiTextureDesc *desc, bool backbuffer)
{
    RhiTexture out = {0};
    if (!desc || desc->format <= RHI_FMT_UNKNOWN || desc->format >= RHI_FMT_COUNT ||
        desc->width == 0 || desc->height == 0 || !g_gl.initialised) {
        return out;
    }
    const GlFormat *fm = gl_format(desc->format);
    if (fm->block && (!g_gl.limits.bcTextures ||
                      (desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)))) {
        /* texture packs: BC needs s3tc and bptc (RhiLimits.bcTextures) and
         * is sampled and copied into only, never drawn to */
        return out;
    }
    if (desc->width > g_gl.limits.maxTextureSize || desc->height > g_gl.limits.maxTextureSize) {
        GL_LOG("rhi_create_texture %s: %ux%u is over the limit %u",
               desc->debugName ? desc->debugName : "?", desc->width, desc->height,
               g_gl.limits.maxTextureSize);
        return out;
    }
    uint32_t mips = desc->mipLevels ? desc->mipLevels : 1u;
    const uint32_t samples = desc->samples > 1u ? desc->samples : 1u;
    if (samples > 1u &&
        ((g_gl.limits.msaaCounts & samples) == 0u || (samples & (samples - 1u)) != 0u ||
         mips != 1u || !(desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)))) {
        GL_LOG("rhi_create_texture: %u samples not taken", samples);
        return out;
    }
    if (mips > gl_max_levels(desc->width, desc->height)) {
        GL_LOG("rhi_create_texture %s: %u levels for %ux%u; %u made",
               desc->debugName ? desc->debugName : "?", mips, desc->width, desc->height,
               gl_max_levels(desc->width, desc->height));
        mips = gl_max_levels(desc->width, desc->height);
    }
    GlTexture *t = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_gl.textures, (void **)&t);
    if (!id) {
        GL_LOG("texture pool full");
        return out;
    }
    memset(t, 0, sizeof(*t));
    t->target = samples > 1u ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    gl_check("before rhi_create_texture");
    glf.ActiveTexture(GL_TEXTURE0); /* the scratch unit (gl_internal.h) */
    glf.GenTextures(1, &t->name);
    glf.BindTexture(t->target, t->name);
    if (samples > 1u) {
        glf.TexStorage2DMultisample(t->target, (GLsizei)samples, fm->internal, (GLsizei)desc->width,
                                    (GLsizei)desc->height, GL_TRUE);
    } else {
        glf.TexStorage2D(t->target, (GLsizei)mips, fm->internal, (GLsizei)desc->width,
                         (GLsizei)desc->height);
    }
    gl_texture_params(t->target, mips);
    glf.BindTexture(t->target, 0);
    bool ok = gl_check("rhi_create_texture");
    /* the RGBA8_UINT view of an RGBA8 texture (RHI_ASPECT_UINT): the same
     * texels read as integers, without a float conversion */
    if (ok && (desc->usage & RHI_TEX_UINT_VIEW) && desc->format == RHI_FMT_RGBA8_UNORM) {
        glf.GenTextures(1, &t->uintView);
        glf.TextureView(t->uintView, t->target, t->name, GL_RGBA8UI, 0, mips, 0, 1);
        glf.BindTexture(t->target, t->uintView);
        gl_texture_params(t->target, mips);
        glf.BindTexture(t->target, 0);
        ok = gl_check("rhi_create_texture (UINT view)");
    }
    if (!ok) {
        GL_LOG("texture %s: %ux%u format %d, %u levels, %u samples not made",
               desc->debugName ? desc->debugName : "?", desc->width, desc->height,
               (int)desc->format, mips, samples);
        if (t->uintView) {
            glf.DeleteTextures(1, &t->uintView);
        }
        glf.DeleteTextures(1, &t->name);
        d3dp_pool_release(&g_gl.textures, id);
        return out;
    }
    t->format = desc->format;
    t->width = desc->width;
    t->height = desc->height;
    t->mips = mips;
    t->samples = samples;
    t->usage = desc->usage;
    t->backbuffer = backbuffer;
    gl_label(GL_TEXTURE, t->name, desc->debugName);
    if (!backbuffer) {
        g_gl.counts.texturesCreated++;
    }
    out.id = id;
    return out;
}

void rhi_destroy_texture(RhiTexture h)
{
    GlTexture *t = gl_get_texture(h);
    if (!t || t->backbuffer) {
        return; /* the swapchain's image is gl_swapchain.c's */
    }
    gl_defer(GL_GARBAGE_TEXTURE, t->uintView);
    gl_defer(GL_GARBAGE_TEXTURE, t->name);
    g_gl.counts.texturesDestroyed++;
    d3dp_pool_release(&g_gl.textures, h.id);
}

uint32_t gl_register_backbuffer(uint32_t w, uint32_t h)
{
    RhiTextureDesc d;
    memset(&d, 0, sizeof(d));
    d.width = w;
    d.height = h;
    d.mipLevels = 1;
    d.format = RHI_FMT_RGBA8_UNORM;
    d.usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST;
    d.debugName = "backbuffer";
    return createTexture(&d, true).id;
}

void gl_release_backbuffer(uint32_t id)
{
    RhiTexture h = {id};
    GlTexture *t = gl_get_texture(h);
    if (!t) {
        return;
    }
    /* the device is idle (gl_swapchain.c): deleted now */
    gl_delete_now(GL_GARBAGE_TEXTURE, t->name);
    d3dp_pool_release(&g_gl.textures, id);
}

/* -------------------------------------------------------------- samplers */
GlSampler *gl_get_sampler(RhiSampler s)
{
    return d3dp_pool_get(&g_gl.samplers, s.id);
}

RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc)
{
    RhiSampler out = {0};
    if (!desc || !g_gl.initialised) {
        return out;
    }
    GlSampler *s = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_gl.samplers, (void **)&s);
    if (!id) {
        GL_LOG("sampler pool full (%u)", g_gl.samplers.cap);
        return out;
    }
    memset(s, 0, sizeof(*s));
    gl_check("before rhi_create_sampler"); /* an earlier error is not the sampler's */
    glf.GenSamplers(1, &s->name);
    const bool minLinear = desc->min == RHI_FILTER_LINEAR;
    const bool mipLinear = desc->mip == RHI_FILTER_LINEAR;
    const GLenum minFilter =
        minLinear ? (mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST)
                  : (mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
    glf.SamplerParameteri(s->name, GL_TEXTURE_MIN_FILTER, (GLint)minFilter);
    glf.SamplerParameteri(s->name, GL_TEXTURE_MAG_FILTER,
                          desc->mag == RHI_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    const GLint wrap[2] = {GL_REPEAT, GL_CLAMP_TO_EDGE};
    glf.SamplerParameteri(s->name, GL_TEXTURE_WRAP_S, wrap[desc->s & 1]);
    glf.SamplerParameteri(s->name, GL_TEXTURE_WRAP_T, wrap[desc->t & 1]);
    glf.SamplerParameteri(s->name, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glf.SamplerParameterf(s->name, GL_TEXTURE_LOD_BIAS, desc->lodBias);
    glf.SamplerParameterf(s->name, GL_TEXTURE_MIN_LOD, desc->minLod);
    glf.SamplerParameterf(s->name, GL_TEXTURE_MAX_LOD, desc->maxLod);
    glf.SamplerParameteri(s->name, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    if (g_gl.hasAnisotropy) {
        float a = desc->maxAnisotropy;
        if (a < 1.0f) {
            a = 1.0f;
        }
        if (a > g_gl.limits.maxAnisotropy) {
            a = g_gl.limits.maxAnisotropy;
        }
        glf.SamplerParameterf(s->name, GL_TEXTURE_MAX_ANISOTROPY, a);
    }
    if (!gl_check("rhi_create_sampler")) {
        glf.DeleteSamplers(1, &s->name);
        d3dp_pool_release(&g_gl.samplers, id);
        return out;
    }
    out.id = id;
    return out;
}

void rhi_destroy_sampler(RhiSampler h)
{
    GlSampler *s = gl_get_sampler(h);
    if (!s) {
        return;
    }
    gl_defer(GL_GARBAGE_SAMPLER, s->name); /* a bind group of this frame may hold it */
    d3dp_pool_release(&g_gl.samplers, h.id);
}

/* --------------------------------------------------------------- shaders
 * Each shader is a separable program of its stage (glCreateShaderProgramv:
 * compile and link in one), so a pipeline pairs two programs in a program
 * pipeline object without a link of its own. */
GlShader *gl_get_shader(RhiShader s)
{
    return d3dp_pool_get(&g_gl.shaders, s.id);
}

/* The text of a description: the table's deflated GLSL (textSize its
 * length), or plain GLSL text with textSize 0 (the tests).  malloc'd. */
static char *gl_shader_text(const RhiShaderDesc *desc)
{
    const char *bytes = (const char *)desc->bytecode;
    if (!bytes || desc->bytecodeSize == 0) {
        return NULL;
    }
    if (desc->textSize > 0) {
        return rhi_gl_inflate_glsl(bytes, desc->bytecodeSize, desc->textSize);
    }
    if (desc->bytecodeSize < 8 || memcmp(bytes, "#version", 8) != 0) {
        return NULL;
    }
    size_t n = desc->bytecodeSize;
    while (n > 0 && bytes[n - 1] == '\0') {
        n--;
    }
    char *t = malloc(n + 1);
    if (t) {
        memcpy(t, bytes, n);
        t[n] = '\0';
    }
    return t;
}

/* A separable vertex program's built-in outputs go through gl_PerVertex,
 * which strict drivers want redeclared; inserted after the leading
 * preprocessor lines (#version, #extension, their #if guards) when the
 * text has none of its own and uses no clip or cull distance. */
static char *gl_redeclare_per_vertex(char *text)
{
    static const char block[] = "out gl_PerVertex\n{\n    vec4 gl_Position;\n"
                                "    float gl_PointSize;\n};\n";
    if (strstr(text, "gl_PerVertex") || strstr(text, "gl_ClipDistance") ||
        strstr(text, "gl_CullDistance")) {
        return text;
    }
    /* the first line that is neither blank nor a preprocessor line */
    const char *p = text;
    while (*p) {
        const char *q = p;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (*q != '#' && *q != '\n' && *q != '\r' && *q != '\0') {
            break;
        }
        const char *nl = strchr(p, '\n');
        p = nl ? nl + 1 : p + strlen(p);
    }
    const size_t at = (size_t)(p - text), len = strlen(text), add = sizeof(block) - 1u;
    char *out = malloc(len + add + 1u);
    if (!out) {
        return text;
    }
    memcpy(out, text, at);
    memcpy(out + at, block, add);
    memcpy(out + at + add, text + at, len - at + 1u);
    free(text);
    return out;
}

/* The vertex program's main wrapped so that gl_Position.y is negated after
 * it (every return included): the device draws with GL_LOWER_LEFT clip
 * control (gl_device.c gl_defaults), whose window y runs the same way as
 * the renderer's D3D-style clip y only with that negation.  The text's own
 * main is renamed by a #define after the #version line. */
static char *gl_flip_vertex_y(char *text)
{
    static const char rename[] = "#define main ico_gl_vertex_main\n";
    static const char tail[] = "\n#undef main\nvoid main()\n{\n    ico_gl_vertex_main();\n"
                               "    gl_Position.y = -gl_Position.y;\n}\n";
    const char *nl = strchr(text, '\n');
    if (!nl) {
        return text;
    }
    const size_t at = (size_t)(nl + 1 - text), len = strlen(text);
    const size_t addR = sizeof(rename) - 1u, addT = sizeof(tail) - 1u;
    char *out = malloc(len + addR + addT + 1u);
    if (!out) {
        return text;
    }
    memcpy(out, text, at);
    memcpy(out + at, rename, addR);
    memcpy(out + at + addR, text + at, len - at);
    memcpy(out + len + addR, tail, addT + 1u);
    free(text);
    return out;
}

RhiShader rhi_create_shader(const RhiShaderDesc *desc)
{
    RhiShader out = {0};
    static uint32_t s_failures;
    const char *name = desc && desc->debugName ? desc->debugName : "?";
    if (!desc || !g_gl.initialised) {
        return out;
    }
    char *text = gl_shader_text(desc);
    if (!text) {
        if (gl_log_rate(&s_failures)) {
            if (desc->textSize) {
                GL_LOG("shader %s: the GLSL (%zu bytes) does not inflate to %zu bytes", name,
                       desc->bytecodeSize, desc->textSize);
            } else {
                GL_LOG("shader %s: no GLSL (%zu bytes, neither deflated with its length nor text "
                       "starting #version)",
                       name, desc->bytecodeSize);
            }
        }
        return out;
    }
    if (desc->stage == RHI_STAGE_VERTEX) {
        text = gl_flip_vertex_y(gl_redeclare_per_vertex(text));
    }
    const GLenum type = desc->stage == RHI_STAGE_VERTEX ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    const GLchar *src = text;
    const GLuint program = glf.CreateShaderProgramv(type, 1, &src);
    free(text);
    GLint linked = GL_FALSE;
    if (program) {
        glf.GetProgramiv(program, GL_LINK_STATUS, &linked);
    }
    if (!program || linked != GL_TRUE) {
        if (gl_log_rate(&s_failures)) {
            char log[2048];
            GLsizei n = 0;
            log[0] = '\0';
            if (program) {
                glf.GetProgramInfoLog(program, (GLsizei)sizeof(log), &n, log);
            }
            while (n > 0 && (log[n - 1] == '\n' || log[n - 1] == '\r')) {
                log[--n] = '\0';
            }
            GL_LOG("shader %s: %s", name, log[0] ? log : "the program was not made");
        }
        if (program) {
            glf.DeleteProgram(program);
        }
        gl_check("rhi_create_shader");
        return out;
    }
    GlShader *s = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_gl.shaders, (void **)&s);
    if (!id) {
        GL_LOG("shader pool full (%u)", g_gl.shaders.cap);
        glf.DeleteProgram(program);
        return out;
    }
    s->program = program;
    s->stage = desc->stage;
    gl_label(GL_PROGRAM, program, desc->debugName);
    out.id = id;
    return out;
}

void rhi_destroy_shader(RhiShader h)
{
    GlShader *s = gl_get_shader(h);
    if (!s) {
        return;
    }
    /* a program pipeline made with it may still be in use this frame */
    gl_defer(GL_GARBAGE_PROGRAM, s->program);
    d3dp_pool_release(&g_gl.shaders, h.id);
}

/* ----------------------------------------------------- bind group layouts
 * d3d12_plan.h's layout rules: at most 16 slots, 4 dynamic uniforms ranked
 * in ascending slot order (their bind-time offsets' order), no input
 * attachment (RhiLimits.localRead is false), one binding per register. */
GlLayout *gl_get_layout(RhiBindGroupLayout l)
{
    return d3dp_pool_get(&g_gl.layouts, l.id);
}

RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc)
{
    RhiBindGroupLayout out = {0};
    if (!desc || !g_gl.initialised) {
        return out;
    }
    D3dpLayout l;
    if (!d3dp_layout_build(desc->slots, desc->slotCount, &l)) {
        GL_LOG("bind group layout %s: invalid", desc->debugName ? desc->debugName : "?");
        return out;
    }
    GlLayout *gl = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_gl.layouts, (void **)&gl);
    if (!id) {
        GL_LOG("bind group layout pool full (%u)", g_gl.layouts.cap);
        return out;
    }
    gl->l = l;
    out.id = id;
    return out;
}

void rhi_destroy_bind_group_layout(RhiBindGroupLayout h)
{
    d3dp_pool_release(&g_gl.layouts, h.id); /* nothing on the GPU */
}

/* ---------------------------------------------------- transient bind groups
 * The frame slot's arena (gl_internal.h, "bind groups"): reset when the
 * slot is recycled (rhi_wait_frame).  Handle id: (frame tag << 20) |
 * (index + 1). */
GlBindGroup *gl_get_bind_group(RhiBindGroup g)
{
    GlFrame *f = gl_cur_frame();
    const uint32_t idx = g.id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->groupCount || (g.id >> D3DP_GEN_SHIFT) != gl_frame_tag()) {
        return NULL;
    }
    return &f->groups[idx - 1];
}

static const RhiBinding *gl_find_binding(const RhiBindGroupDesc *d, uint32_t slot, RhiBindType type)
{
    for (uint32_t i = 0; i < d->bindingCount; i++) {
        if (d->bindings[i].slot == slot && d->bindings[i].type == type) {
            return &d->bindings[i];
        }
    }
    return NULL;
}

/* The sampler a texture at slot t samples through: the group's sampler at
 * slot t, else its lowest sampler slot (GLSL's combined samplers name no
 * sampler register; the game's shaders pair s1 with each texture they
 * filter); 0 when the group has none. */
static GLuint gl_texture_sampler(const RhiBindGroupDesc *d, const D3dpLayout *l, uint32_t slot)
{
    const RhiBinding *b = NULL;
    for (uint32_t i = 0; i < l->slotCount; i++) {
        const RhiBindSlot *s = &l->slots[i];
        if (s->type != RHI_BIND_SAMPLER) {
            continue;
        }
        const RhiBinding *c = gl_find_binding(d, s->slot, RHI_BIND_SAMPLER);
        if (!c || !gl_get_sampler(c->sampler)) {
            continue;
        }
        if (s->slot == slot) {
            b = c;
            break;
        }
        if (!b || c->slot < b->slot) {
            b = c;
        }
    }
    GlSampler *smp = b ? gl_get_sampler(b->sampler) : NULL;
    return smp ? smp->name : 0u;
}

/* A zeroed buffer bound where a uniform or storage slot has no usable
 * binding, so a shader that reads it reads zeros as on D3D12's null
 * views (64 KiB: GL_MAX_UNIFORM_BLOCK_SIZE's usual value; made at the
 * first need). */
#define GL_NULL_BUFFER_SIZE 65536u

static GLuint gl_null_buffer(void)
{
    if (g_gl.nullBuffer) {
        return g_gl.nullBuffer;
    }
    void *zeros = calloc(1, GL_NULL_BUFFER_SIZE);
    if (!zeros) {
        return 0;
    }
    glf.GenBuffers(1, &g_gl.nullBuffer);
    glf.BindBuffer(GL_COPY_WRITE_BUFFER, g_gl.nullBuffer);
    glf.BufferStorage(GL_COPY_WRITE_BUFFER, GL_NULL_BUFFER_SIZE, zeros, 0);
    glf.BindBuffer(GL_COPY_WRITE_BUFFER, 0);
    free(zeros);
    gl_label(GL_BUFFER, g_gl.nullBuffer, "null binding");
    return g_gl.nullBuffer;
}

static void gl_null_entry(GlBindEntry *e, const RhiBindSlot *s)
{
    if (s->type == RHI_BIND_UNIFORM_BUFFER || s->type == RHI_BIND_UNIFORM_BUFFER_DYNAMIC ||
        s->type == RHI_BIND_STORAGE_BUFFER) {
        e->kind = s->type == RHI_BIND_STORAGE_BUFFER ? GL_BIND_STORAGE : GL_BIND_UNIFORM;
        e->name = gl_null_buffer();
        e->offset = 0;
        e->size = GL_NULL_BUFFER_SIZE;
        /* a dynamic slot's bind-time offset is not added to the null range */
        e->dyn = GL_BIND_STATIC;
    }
}

/* One entry for a slot; false when the binding is unusable (logged once a
 * kind of fault) and the slot gets a null entry or none. */
static bool gl_make_entry(const RhiBindGroupDesc *d, const D3dpLayout *l, uint32_t i,
                          GlBindEntry *e)
{
    static bool s_logged[RHI_BIND_COUNT];
    const RhiBindSlot *s = &l->slots[i];
    const RhiBinding *b = gl_find_binding(d, s->slot, s->type);
    memset(e, 0, sizeof(*e));
    e->dyn = GL_BIND_STATIC;
    e->point = (uint16_t)gl_bind_point(s->type, s->slot);
    const char *why = NULL;
    switch (s->type) {
    case RHI_BIND_UNIFORM_BUFFER:
    case RHI_BIND_UNIFORM_BUFFER_DYNAMIC: {
        GlBuffer *buf = b ? gl_get_buffer(b->buffer) : NULL;
        const bool dynamic = s->type == RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
        uint64_t size = 0;
        if (buf && b->offset < buf->size) {
            size = b->size ? b->size : buf->size - b->offset;
            if (!b->size && g_gl.maxUniformBlockSize > 0 &&
                size > (uint64_t)g_gl.maxUniformBlockSize) {
                size = (uint64_t)g_gl.maxUniformBlockSize; /* "to the end": one block's most */
            }
        }
        if (!buf) {
            why = b ? "invalid buffer" : "no binding";
        } else if (b->offset % (uint64_t)g_gl.limits.uniformAlign != 0) {
            why = "offset not a multiple of uniformAlign";
        } else if (size == 0 || size > buf->size - b->offset || (dynamic && !b->size)) {
            why = "range outside the buffer (a dynamic uniform needs its size)";
        }
        if (why) {
            break;
        }
        e->kind = GL_BIND_UNIFORM;
        e->name = buf->name;
        e->offset = b->offset;
        e->size = size;
        e->dyn = dynamic ? l->offset[i] : GL_BIND_STATIC;
        return true;
    }
    case RHI_BIND_STORAGE_BUFFER: {
        GlBuffer *buf = b ? gl_get_buffer(b->buffer) : NULL;
        uint64_t size = 0;
        if (buf && b->offset < buf->size) {
            size = b->size ? b->size : buf->size - b->offset;
        }
        if (!buf) {
            why = b ? "invalid buffer" : "no binding";
        } else if (g_gl.ssboAlign > 0 && b->offset % (uint64_t)g_gl.ssboAlign != 0) {
            why = "offset not a multiple of GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT";
        } else if (size < 16 || size > buf->size - b->offset) {
            why = "range outside the buffer";
        }
        if (why) {
            break;
        }
        e->kind = GL_BIND_STORAGE;
        e->name = buf->name;
        e->offset = b->offset;
        e->size = size;
        return true;
    }
    case RHI_BIND_SAMPLED_TEXTURE: {
        GlTexture *t = b ? gl_get_texture(b->texture) : NULL;
        if (!t) {
            why = b ? "invalid texture" : "no binding";
            break;
        }
        const GlFormat *fm = gl_format(t->format);
        const bool uintView = b->aspect == RHI_ASPECT_UINT && t->uintView;
        e->kind = GL_BIND_TEXTURE;
        e->name = uintView ? t->uintView : t->name;
        e->target = t->target;
        /* GL filters neither integer nor multisampled textures (an
         * integer texture under a filtering sampler is incomplete) */
        e->sampler =
            (fm->integer || uintView || t->samples > 1u) ? 0u : gl_texture_sampler(d, l, s->slot);
        return true;
    }
    case RHI_BIND_STORAGE_TEXTURE: {
        GlTexture *t = b ? gl_get_texture(b->texture) : NULL;
        if (!t || !(t->usage & RHI_TEX_STORAGE) || t->format != RHI_FMT_RGBA8_UNORM ||
            t->samples > 1u) {
            why = "invalid storage texture (an RGBA8 texture made with RHI_TEX_STORAGE)";
            break;
        }
        e->kind = GL_BIND_IMAGE;
        e->name = t->name;
        e->target = t->target;
        return true;
    }
    case RHI_BIND_SAMPLER:
    default:
        return false; /* folded into the texture entries */
    }
    if ((uint32_t)s->type < RHI_BIND_COUNT && !s_logged[s->type]) {
        s_logged[s->type] = true;
        GL_LOG("bind group: slot %u (type %d): %s (reported once a type)", s->slot, (int)s->type,
               why ? why : "unusable");
    }
    return false;
}

static bool gl_grow(void **p, uint32_t *cap, uint32_t need, size_t elem, uint32_t first)
{
    if (need <= *cap) {
        return true;
    }
    uint32_t c = *cap ? *cap : first;
    while (c < need) {
        c *= 2u;
    }
    void *q = realloc(*p, (size_t)c * elem);
    if (!q) {
        return false;
    }
    *p = q;
    *cap = c;
    return true;
}

RhiBindGroup rhi_create_bind_group(const RhiBindGroupDesc *desc)
{
    RhiBindGroup out = {0};
    GlLayout *gl = desc ? gl_get_layout(desc->layout) : NULL;
    if (!gl || !g_gl.initialised) {
        return out;
    }
    const D3dpLayout *l = &gl->l;
    GlFrame *f = gl_cur_frame();
    if (f->groupCount >= D3DP_INDEX_MASK ||
        !gl_grow((void **)&f->groups, &f->groupCap, f->groupCount + 1u, sizeof(GlBindGroup),
                 1024u) ||
        !gl_grow((void **)&f->entries, &f->entryCap, f->entryCount + l->slotCount,
                 sizeof(GlBindEntry), 4096u)) {
        GL_LOG("out of memory for this frame's bind groups");
        return out;
    }
    GlBindGroup *g = &f->groups[f->groupCount];
    g->layoutId = desc->layout.id;
    g->first = f->entryCount;
    g->dynCount = l->dynCount;
    for (uint32_t i = 0; i < l->slotCount; i++) {
        GlBindEntry *e = &f->entries[f->entryCount];
        if (gl_make_entry(desc, l, i, e)) {
            f->entryCount++;
            continue;
        }
        gl_null_entry(e, &l->slots[i]);
        if (e->name) {
            f->entryCount++;
        }
    }
    g->count = f->entryCount - g->first;
    f->groupCount++;
    g_gl.counts.bindGroups++;
    out.id = (gl_frame_tag() << D3DP_GEN_SHIFT) | f->groupCount;
    return out;
}

/* ---------------------------------------------------------------- readback
 * glFinish, then glReadPixels of level 0 through the scratch read
 * framebuffer into the caller's memory, rows tightly packed from row 0
 * (the top: gl_internal.h); depth as 32-bit floats (the depth of a D32F_S8
 * texture without its stencil).  The bindings it changes are put back. */
bool rhi_readback_texture(RhiTexture h, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch)
{
    GlTexture *t = gl_get_texture(h);
    if (!t || !dst || t->samples > 1u || !g_gl.initialised) {
        return false; /* a multisampled texture is resolved first (rhi.h) */
    }
    const GlFormat *fm = gl_format(t->format);
    if (fm->block || (aspect == RHI_ASPECT_DEPTH && !fm->depth)) {
        return false;
    }
    const uint32_t texel = fm->depth ? 4u : fm->texelBytes;
    const uint32_t pitch = t->width * texel;
    const size_t size = (size_t)pitch * t->height;
    if (outRowPitch) {
        *outRowPitch = pitch;
    }
    if (dstSize < size) {
        return false;
    }
    g_gl.counts.readbacks++;
    gl_check("before rhi_readback_texture");
    const uint64_t t0 = gl_now_ns();
    glf.Finish(); /* work already submitted completes first (rhi.h) */
    g_gl.fenceWaitNs += gl_now_ns() - t0;
    GLint prevRead = 0, prevPack = 0, prevRowLength = 0;
    glf.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glf.GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPack);
    glf.GetIntegerv(GL_PACK_ROW_LENGTH, &prevRowLength);
    const GLenum attachment = !fm->depth    ? GL_COLOR_ATTACHMENT0
                              : fm->stencil ? GL_DEPTH_STENCIL_ATTACHMENT
                                            : GL_DEPTH_ATTACHMENT;
    glf.BindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.scratchRead);
    glf.FramebufferTexture2D(GL_READ_FRAMEBUFFER, attachment, GL_TEXTURE_2D, t->name, 0);
    glf.ReadBuffer(fm->depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
    bool ok = glf.CheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glf.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glf.PixelStorei(GL_PACK_ALIGNMENT, 1);
        glf.PixelStorei(GL_PACK_ROW_LENGTH, 0);
        if (fm->depth) {
            glf.ReadPixels(0, 0, (GLsizei)t->width, (GLsizei)t->height, GL_DEPTH_COMPONENT,
                           GL_FLOAT, dst);
        } else {
            glf.ReadPixels(0, 0, (GLsizei)t->width, (GLsizei)t->height, fm->format, fm->type, dst);
        }
    } else {
        GL_LOG("readback: texture %u (format %d) cannot be read through a framebuffer", h.id,
               (int)t->format);
    }
    glf.FramebufferTexture2D(GL_READ_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
    glf.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glf.BindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPack);
    glf.PixelStorei(GL_PACK_ROW_LENGTH, prevRowLength);
    return gl_check("rhi_readback_texture") && ok;
}

/* ---------------------------------------------------------------- teardown */
void gl_release_all_objects(void)
{
    if (g_gl.nullBuffer) {
        glf.DeleteBuffers(1, &g_gl.nullBuffer);
        g_gl.nullBuffer = 0;
    }
    for (uint32_t i = 0; i < g_gl.buffers.next; i++) {
        GlBuffer *b = d3dp_pool_at(&g_gl.buffers, i);
        if (b && b->name) {
            glf.DeleteBuffers(1, &b->name);
        }
    }
    for (uint32_t i = 0; i < g_gl.textures.next; i++) {
        GlTexture *t = d3dp_pool_at(&g_gl.textures, i);
        if (t) {
            if (t->uintView) {
                glf.DeleteTextures(1, &t->uintView);
            }
            if (t->name) {
                glf.DeleteTextures(1, &t->name);
            }
        }
    }
    for (uint32_t i = 0; i < g_gl.samplers.next; i++) {
        GlSampler *s = d3dp_pool_at(&g_gl.samplers, i);
        if (s && s->name) {
            glf.DeleteSamplers(1, &s->name);
        }
    }
    for (uint32_t i = 0; i < g_gl.shaders.next; i++) {
        GlShader *s = d3dp_pool_at(&g_gl.shaders, i);
        if (s && s->program) {
            glf.DeleteProgram(s->program);
        }
    }
    for (uint32_t i = 0; i < g_gl.pipelines.next; i++) {
        GlPipeline *p = d3dp_pool_at(&g_gl.pipelines, i);
        if (p) {
            if (p->ppo) {
                glf.DeleteProgramPipelines(1, &p->ppo);
            }
            if (p->vao) {
                glf.DeleteVertexArrays(1, &p->vao);
            }
        }
    }
}
