/* gl_device.c: the context, the entry points, the version and extension
 * checks, the debug output, the device defaults, limits, the frame model
 * (fences, deferred deletes, bind group arenas, timestamps) and
 * rhi_init/rhi_shutdown of the OpenGL backend (gl_internal.h). */
#include "gl_internal.h"
#include "rhi_gl.h"
#include "../rhi_backend.h"
#include <stdlib.h>
#include <string.h>

GlState g_gl;

/* kept across rhi_shutdown (rhi_gl.h) */
static bool s_fakeNoDual;
static uint32_t s_debugErrors;
static bool s_debugActive;

void rhi_gl_fake_no_dual(bool on)
{
    s_fakeNoDual = on;
}

uint32_t rhi_gl_debug_error_count(void)
{
    return g_gl.initialised ? g_gl.debugErrors : s_debugErrors;
}

bool rhi_gl_debug_active(void)
{
    return s_debugActive;
}

static bool gl_env_on(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

/* ------------------------------------------------------------- queries */
bool gl_has_extension(const char *name)
{
    if (!glf.GetIntegerv || !glf.GetStringi || !name) {
        return false;
    }
    GLint n = 0;
    glf.GetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char *e = (const char *)glf.GetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) {
            return true;
        }
    }
    return false;
}

bool gl_check(const char *what)
{
    static uint32_t s_errors;
    if (!glf.GetError) {
        return true;
    }
    bool ok = true;
    for (int i = 0; i < 16; i++) {
        const GLenum e = glf.GetError();
        if (e == GL_NO_ERROR) {
            break;
        }
        ok = false;
        if (gl_log_rate(&s_errors)) {
            GL_LOG("%s: GL error 0x%04x (%u so far)", what ? what : "?", (unsigned)e, s_errors);
        }
    }
    return ok;
}

static GLint gl_int(GLenum pname)
{
    GLint v = 0;
    glf.GetIntegerv(pname, &v);
    return v;
}

static const char *gl_str(GLenum pname)
{
    const char *s = (const char *)glf.GetString(pname);
    return s ? s : "?";
}

static bool gl_version_at_least(int major, int minor)
{
    return g_gl.major > major || (g_gl.major == major && g_gl.minor >= minor);
}

/* --------------------------------------------------------- debug output
 * KHR_debug (core in 4.3) with RhiDeviceDesc.debugLayers: a synchronous
 * callback that counts the driver's errors (rhi_gl_debug_error_count) and
 * prints the first 8 of them and every 100th after, and likewise the other
 * messages of low severity or above (performance, portability, deprecated);
 * notifications are switched off. */
static const char *gl_debug_type_name(GLenum type)
{
    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        return "error";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        return "undefined behaviour";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        return "deprecated";
    case GL_DEBUG_TYPE_PORTABILITY:
        return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE:
        return "performance";
    default:
        return "other";
    }
}

static void APIENTRY gl_debug_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                                      GLsizei length, const GLchar *message, const void *user)
{
    (void)source;
    (void)length;
    (void)user;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || type == GL_DEBUG_TYPE_PUSH_GROUP ||
        type == GL_DEBUG_TYPE_POP_GROUP || type == GL_DEBUG_TYPE_MARKER) {
        return;
    }
    const bool error = type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR;
    static uint32_t s_shownErrors, s_shownOther;
    if (error) {
        g_gl.debugErrors++;
        if (gl_log_rate(&s_shownErrors)) {
            GL_LOG("debug: %s #%u (id 0x%x): %s", gl_debug_type_name(type), g_gl.debugErrors,
                   (unsigned)id, message ? message : "");
        }
    } else {
        g_gl.debugOther++;
        if (gl_log_rate(&s_shownOther)) {
            GL_LOG("debug: %s (id 0x%x): %s", gl_debug_type_name(type), (unsigned)id,
                   message ? message : "");
        }
    }
}

/* ICO_GL_DEBUG=0/1 overrides the caller's debugLayers (and a Debug build's
 * default, ICO_RHI_DEBUG_DEFAULT) */
static bool gl_debug_wanted(const RhiDeviceDesc *desc)
{
    const char *env = getenv("ICO_GL_DEBUG");
    if (env && env[0]) {
        return env[0] != '0';
    }
#ifdef ICO_RHI_DEBUG_DEFAULT
    return true;
#else
    return desc && desc->debugLayers;
#endif
}

static void gl_debug_setup(void)
{
    glf.Enable(GL_DEBUG_OUTPUT);
    glf.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glf.DebugMessageCallback(gl_debug_message, NULL);
    glf.DebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL,
                            GL_FALSE);
    glf.GetIntegerv(GL_MAX_DEBUG_GROUP_STACK_DEPTH, &g_gl.maxLabelDepth);
    g_gl.debug = true;
}

/* ------------------------------------------------------------- context
 * The context's attributes before SDL_GL_CreateContext: core 4.3 (a driver
 * gives its newest compatible version), the debug flag with debugLayers.
 * Where the backend owns the window (headless) also its pixel format,
 * before the window is made: double-buffered, no depth or stencil (the
 * backend draws into textures), no sRGB. */
static void gl_context_attributes(bool debug)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
}

static bool gl_make_own_window(void)
{
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        /* no display needed: SDL's offscreen driver makes its contexts
         * through EGL without one (SDL_VIDEO_DRIVER in the environment
         * still picks another) */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            GL_LOG("headless: SDL's video did not start (%s); no OpenGL context", SDL_GetError());
            return false;
        }
        g_gl.ownVideo = true;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
    g_gl.ownWindow = SDL_CreateWindow("ico rhi_gl", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_gl.ownWindow) {
        GL_LOG("headless: no hidden window for the context on SDL's %s driver (%s)",
               SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?", SDL_GetError());
        return false;
    }
    return true;
}

static bool gl_create_context(const RhiDeviceDesc *desc, bool debug)
{
    SDL_Window *w = desc ? (SDL_Window *)desc->sdlWindow : NULL;
    if (w) {
        if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_OPENGL)) {
            GL_LOG("the window was made without SDL_WINDOW_OPENGL; no OpenGL context on it");
            return false;
        }
        g_gl.window = w;
    } else if (!gl_make_own_window()) {
        return false;
    }
    SDL_Window *target = w ? w : g_gl.ownWindow;
    gl_context_attributes(debug);
    g_gl.context = SDL_GL_CreateContext(target);
    if (!g_gl.context) {
        GL_LOG("no OpenGL 4.3 core context (%s)", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(target, g_gl.context)) {
        GL_LOG("the OpenGL context is not current (%s)", SDL_GetError());
        return false;
    }
    return true;
}

/* ------------------------------------------------------------- limits */
/* The sample counts (2, 4, 8) one internal format takes as a multisampled
 * texture, as RhiLimits.msaaCounts bits. */
static uint32_t gl_format_samples(GLenum internal)
{
    GLint n = 0;
    glf.GetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, internal, GL_NUM_SAMPLE_COUNTS, 1, &n);
    if (n <= 0) {
        return 0;
    }
    GLint counts[32];
    memset(counts, 0, sizeof(counts));
    if (n > 32) {
        n = 32;
    }
    glf.GetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, internal, GL_SAMPLES, n, counts);
    uint32_t bits = 0;
    for (GLint i = 0; i < n; i++) {
        if (counts[i] == 2 || counts[i] == 4 || counts[i] == 8) {
            bits |= (uint32_t)counts[i];
        }
    }
    return bits;
}

/* every multisampled target the renderer makes: RGBA8, RGBA8UI (held to
 * GL_MAX_INTEGER_SAMPLES as well) and the D32F_S8 depth-stencil, each with
 * GL_MAX_SAMPLES; 1 always */
static uint32_t gl_msaa_counts(void)
{
    if (gl_env_on("ICO_GL_FAKE_NO_MSAA")) {
        return 1u;
    }
    uint32_t bits = gl_format_samples(GL_RGBA8) & gl_format_samples(GL_RGBA8UI) &
                    gl_format_samples(GL_DEPTH32F_STENCIL8);
    const GLint maxSamples = gl_int(GL_MAX_SAMPLES);
    const GLint maxInt = gl_int(GL_MAX_INTEGER_SAMPLES);
    const GLint maxDepth = gl_int(GL_MAX_DEPTH_TEXTURE_SAMPLES);
    const GLint maxColor = gl_int(GL_MAX_COLOR_TEXTURE_SAMPLES);
    for (uint32_t c = 2; c <= 8; c <<= 1) {
        if ((GLint)c > maxSamples || (GLint)c > maxInt || (GLint)c > maxDepth ||
            (GLint)c > maxColor) {
            bits &= ~c;
        }
    }
    return bits | 1u;
}

static uint32_t gl_vendor_id(const char *vendor, const char *renderer)
{
    char v[160];
    snprintf(v, sizeof(v), "%s %s", vendor ? vendor : "", renderer ? renderer : "");
    for (char *p = v; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    if (strstr(v, "nvidia")) {
        return 0x10DEu;
    }
    if (strstr(v, "amd") || strstr(v, "ati technologies") || strstr(v, "advanced micro") ||
        strstr(v, "radeon")) {
        return 0x1002u;
    }
    if (strstr(v, "intel")) {
        return 0x8086u;
    }
    return 0u;
}

static void gl_fill_limits(void)
{
    RhiLimits *o = &g_gl.limits;
    memset(o, 0, sizeof(*o));
    g_gl.uboAlign = gl_int(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
    g_gl.ssboAlign = gl_int(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT);
    g_gl.maxUniformBlockSize = gl_int(GL_MAX_UNIFORM_BLOCK_SIZE);
    g_gl.maxImageUnits = gl_int(GL_MAX_IMAGE_UNITS);
    uint32_t align = g_gl.uboAlign > 16 ? (uint32_t)g_gl.uboAlign : 16u;
    o->uniformAlign = align;
    const GLint maxTex = gl_int(GL_MAX_TEXTURE_SIZE);
    o->maxTextureSize = maxTex > 0 ? (uint32_t)maxTex : 0u;
    /* a target's side: the texture, renderbuffer, framebuffer and
     * viewport limits, the smallest */
    GLint rt = maxTex;
    const GLint lims[3] = {gl_int(GL_MAX_RENDERBUFFER_SIZE), gl_int(GL_MAX_FRAMEBUFFER_WIDTH),
                           gl_int(GL_MAX_FRAMEBUFFER_HEIGHT)};
    for (int i = 0; i < 3; i++) {
        if (lims[i] > 0 && lims[i] < rt) {
            rt = lims[i];
        }
    }
    GLint vp[2] = {0, 0};
    glf.GetIntegerv(GL_MAX_VIEWPORT_DIMS, vp);
    for (int i = 0; i < 2; i++) {
        if (vp[i] > 0 && vp[i] < rt) {
            rt = vp[i];
        }
    }
    o->maxRenderTargetSize = rt > 0 ? (uint32_t)rt : 0u;
    o->dualSourceBlend = gl_int(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS) >= 1 && !s_fakeNoDual &&
                         !gl_env_on("ICO_GL_FAKE_NO_DUAL");
    o->stencilWrap = true; /* GL_INCR_WRAP / GL_DECR_WRAP: core since 1.4 */
    o->depthReadback = true;
    o->depthStencilFormatName = "D32S8"; /* GL_DEPTH32F_STENCIL8, required of every 3.0 driver */
    o->depthSampled = true;
    o->copyRowPitchAlign = 16;
    o->copyOffsetAlign = 4;
    o->textureMips = true;
    o->maxAnisotropy = 1.0f;
    if (g_gl.hasAnisotropy) {
        GLfloat a = 1.0f;
        glf.GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &a);
        o->maxAnisotropy = a > 1.0f ? a : 1.0f;
    }
    /* four uniform ranges a pipeline's layouts bind at a bind-time offset
     * (glBindBufferRange at each draw that changes one) */
    o->maxDynamicUniforms = 4;
    GLint64 ssbo = 0;
    glf.GetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &ssbo);
    o->maxStorageRange = ssbo > 0 ? (uint64_t)ssbo : 0u;
    o->bcTextures = g_gl.hasS3tc && g_gl.hasBptc;
    o->tiler = false;
    o->msaaCounts = gl_msaa_counts();
    o->sampleRateShading = true; /* glMinSampleShading, core since 4.0 */
    /* the destination read through a storage image in rasterization order:
     * ARB_fragment_shader_interlock, and the image unit the translated
     * shaders name (u, tools/spirv_to_glsl.py takes the SPIR-V's 48 off)
     * within GL_MAX_IMAGE_UNITS (8 at least on 4.3) */
    o->fragmentInterlock = g_gl.hasInterlock && g_gl.maxImageUnits > (GLint)GL_UNIT_IMAGE_BASE &&
                           !gl_env_on("ICO_GL_FAKE_NO_DST");
    o->rov = false;
    o->rasterOrderAttachment = false;
    o->rasterOrderOff = NULL;
    o->moltenvk = false;
    o->localRead = false;
    o->vendorId = gl_vendor_id(gl_str(GL_VENDOR), gl_str(GL_RENDERER));
    /* depth and blend state are plain GL state, set at any draw */
    o->dynamicDepth = true;
    o->dynamicBlend = true;
    o->uintView = true; /* glTextureView, core in 4.3 */
    o->vuFallbackPlain = NULL;
    o->compilerVersion = NULL;
    o->carefulPipelines = NULL;
    GLint64 maxIndex = 0;
    glf.GetInteger64v(GL_MAX_ELEMENT_INDEX, &maxIndex);
    o->maxDrawIndexedIndexValue =
        (maxIndex <= 0 || maxIndex >= (GLint64)0xFFFFFFFF) ? 0xFFFFFFFFu : (uint32_t)maxIndex;
    o->fullDrawIndexUint32 = o->maxDrawIndexedIndexValue == 0xFFFFFFFFu;
    o->colorBytesPerPixel = 0; /* no stated bound: the VU vertex pre-pass stays on */
    o->driverText = g_gl.driverText;
    o->robustText = "off (OpenGL)";
    o->robustDefaultOff = false;
}

/* --------------------------------------------------------- frame model */
GlCmdList *gl_get_cmd(RhiCommandList cl)
{
    GlFrame *f = gl_cur_frame();
    const uint32_t idx = cl.id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->listCount || (cl.id >> D3DP_GEN_SHIFT) != gl_frame_tag()) {
        return NULL;
    }
    return &f->lists[idx - 1];
}

void gl_frame_fence(void)
{
    if (!g_gl.initialised) {
        return;
    }
    GlFrame *f = gl_cur_frame();
    GLsync s = glf.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!s) {
        static uint32_t s_failed;
        if (gl_log_rate(&s_failed)) {
            GL_LOG("glFenceSync failed (%u so far): the frame slot waits with glFinish", s_failed);
        }
    }
    /* a later fence of the same frame covers the earlier one; a failed one
       leaves the slot to glFinish at its recycle, until a later submit's
       fence covers it */
    if (f->fence) {
        glf.DeleteSync(f->fence);
    }
    f->fence = s;
    f->needFinish = !s;
    glf.Flush(); /* the fence reaches the GPU without a wait's flush */
}

/* Waits for a fence; the time blocked counts (RhiStats.fenceWaitNs).  No
 * fence (its creation failed): glFinish. */
static void gl_wait_sync(GLsync s)
{
    if (!s) {
        const uint64_t t0 = gl_now_ns();
        glf.Finish();
        g_gl.fenceWaitNs += gl_now_ns() - t0;
        g_gl.fenceWaits++;
        return;
    }
    GLenum r = glf.ClientWaitSync(s, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (r == GL_TIMEOUT_EXPIRED) {
        const uint64_t t0 = gl_now_ns();
        bool logged = false;
        do {
            r = glf.ClientWaitSync(s, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
            if (r == GL_TIMEOUT_EXPIRED && !logged && gl_now_ns() - t0 > 2000000000ull) {
                logged = true;
                GL_LOG("a frame's fence has not signalled after 2 s; still waiting");
            }
        } while (r == GL_TIMEOUT_EXPIRED);
        g_gl.fenceWaitNs += gl_now_ns() - t0;
        g_gl.fenceWaits++;
    }
    if (r == GL_WAIT_FAILED) {
        static uint32_t s_failed;
        if (gl_log_rate(&s_failed)) {
            GL_LOG("glClientWaitSync failed (%u so far); waiting with glFinish", s_failed);
        }
        glf.Finish();
    }
}

/* The slot just recycled (its fence waited for): its stamps into
 * g_gl.tsResult (GL_TIMESTAMP results are ns); rhi_read_timestamps hands
 * them back. */
static void gl_collect_timestamps(GlFrame *f)
{
    g_gl.tsCount = 0;
    memset(g_gl.tsResult, 0, sizeof(g_gl.tsResult));
    if (!g_gl.timestamps) {
        memset(f->tsWritten, 0, sizeof(f->tsWritten));
        return;
    }
    for (uint32_t i = 0; i < RHI_MAX_TIMESTAMPS; i++) {
        if (!rhi_ts_has(f->tsWritten, i) || !f->tsQuery[i]) {
            continue;
        }
        GLint avail = 0;
        glf.GetQueryObjectiv(f->tsQuery[i], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) {
            continue;
        }
        GLuint64 t = 0;
        glf.GetQueryObjectui64v(f->tsQuery[i], GL_QUERY_RESULT, &t);
        if (t) {
            g_gl.tsResult[i] = t;
            g_gl.tsCount = i + 1;
        }
    }
    memset(f->tsWritten, 0, sizeof(f->tsWritten));
}

static void gl_recycle_frame(GlFrame *f)
{
    if (f->needFinish) {
        gl_wait_sync(NULL); /* a submit without a fence: glFinish */
        f->needFinish = false;
    }
    if (f->fence) {
        gl_wait_sync(f->fence);
        glf.DeleteSync(f->fence);
        f->fence = NULL;
    }
    gl_collect_timestamps(f);
    gl_destroy_garbage(f);
    memset(f->lists, 0, sizeof(f->lists));
    f->listCount = 0;
    f->groupCount = 0;
    f->entryCount = 0;
}

void rhi_wait_frame(void)
{
    if (!g_gl.initialised) {
        return;
    }
    g_gl.frameIndex++;
    gl_recycle_frame(gl_cur_frame());
}

uint32_t rhi_frame_slot(void)
{
    return (uint32_t)(g_gl.frameIndex % RHI_FRAMES_IN_FLIGHT);
}

void rhi_wait_idle(void)
{
    if (!g_gl.initialised) {
        return;
    }
    g_gl.counts.waitIdles++;
    const uint64_t t0 = gl_now_ns();
    glf.Finish();
    g_gl.fenceWaitNs += gl_now_ns() - t0;
    /* the other slots' deletes are safe now; the current frame's are not
     * (a bind group made this frame may still be bound) */
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (&g_gl.frames[i] != gl_cur_frame()) {
            gl_destroy_garbage(&g_gl.frames[i]);
        }
    }
    gl_check("rhi_wait_idle");
}

void rhi_collect_garbage_now(void)
{
    rhi_wait_idle();
    if (g_gl.initialised) {
        gl_destroy_garbage(gl_cur_frame());
    }
}

/* ---------------------------------------------------------- timestamps
 * One query object per index and frame slot, made at its first use;
 * rhi_cmd_write_timestamp is glQueryCounter(GL_TIMESTAMP), read at the
 * slot's recycle once its fence has passed, like the other backends', so a
 * slot is never read while the GPU can still write it. */
static void gl_timestamps_init(void)
{
    g_gl.timestamps = false;
    g_gl.tsCount = 0;
    if (gl_env_on("ICO_GL_FAKE_NO_TIMESTAMPS")) {
        GL_LOG("GPU timestamps: off (ICO_GL_FAKE_NO_TIMESTAMPS)");
        return;
    }
    if (!g_gl.hasTimerQuery) {
        GL_LOG("GPU timestamps: off (no ARB_timer_query)");
        return;
    }
    g_gl.timestamps = true;
    GL_LOG("GPU timestamps: on (glQueryCounter, %u per frame slot)", (unsigned)RHI_MAX_TIMESTAMPS);
}

bool rhi_timestamps_supported(void)
{
    return g_gl.timestamps;
}

void rhi_cmd_write_timestamp(RhiCommandList cl, uint32_t index)
{
    /* the barrier flag (Vulkan's held-back image barriers) has no meaning here */
    index &= ~RHI_TIMESTAMP_KEEP_BARRIERS;
    GlFrame *f = gl_cur_frame();
    if (!g_gl.timestamps || index >= RHI_MAX_TIMESTAMPS || !gl_get_cmd(cl) ||
        rhi_ts_has(f->tsWritten, index)) {
        return;
    }
    if (!f->tsQuery[index]) {
        glf.GenQueries(1, &f->tsQuery[index]);
        if (!f->tsQuery[index]) {
            return;
        }
    }
    glf.QueryCounter(f->tsQuery[index], GL_TIMESTAMP);
    rhi_ts_add(f->tsWritten, index);
}

uint32_t rhi_read_timestamps(uint64_t *ns, uint32_t max)
{
    if (!ns || !g_gl.timestamps) {
        return 0;
    }
    const uint32_t n = g_gl.tsCount < max ? g_gl.tsCount : max;
    memcpy(ns, g_gl.tsResult, (size_t)n * sizeof(uint64_t));
    return n;
}

/* ------------------------------------------------- labels, diagnostics */
void rhi_cmd_begin_label(RhiCommandList cl, const char *name)
{
    if (!g_gl.debug || !name || !gl_get_cmd(cl)) {
        return;
    }
    if (g_gl.maxLabelDepth > 0 && g_gl.labelDepth + 1u >= (uint32_t)g_gl.maxLabelDepth) {
        return;
    }
    glf.PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    g_gl.labelDepth++;
}

void rhi_cmd_end_label(RhiCommandList cl)
{
    if (!g_gl.debug || g_gl.labelDepth == 0 || !gl_get_cmd(cl)) {
        return;
    }
    glf.PopDebugGroup();
    g_gl.labelDepth--;
}

/* no GPU-written markers in OpenGL (rhi.h: a no-op without them) */
void rhi_cmd_checkpoint(RhiCommandList cl, const void *marker)
{
    (void)cl;
    (void)marker;
}

void rhi_set_submit_tag(uint32_t replay, uint32_t frame)
{
    g_gl.tagReplay = replay;
    g_gl.tagFrame = frame;
}

/* not tracked on OpenGL (rhi.h): a driver's own watchdog ends a hung
 * context */
bool rhi_gpu_progress(RhiGpuProgress *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return false;
}

/* ------------------------------------------------------------ lifecycle */
static bool gl_check_version(void)
{
    const char *version = gl_str(GL_VERSION);
    g_gl.major = gl_int(GL_MAJOR_VERSION);
    g_gl.minor = gl_int(GL_MINOR_VERSION);
    if (!gl_version_at_least(4, 3)) {
        GL_LOG("OpenGL 4.3 with ARB_clip_control is needed; this driver offers %s (%s)", version,
               gl_str(GL_RENDERER));
        return false;
    }
    if (!(gl_version_at_least(4, 5) || gl_has_extension("GL_ARB_clip_control")) ||
        !glf.ClipControl) {
        GL_LOG("OpenGL 4.3 with ARB_clip_control is needed; this driver offers %s (%s)", version,
               gl_str(GL_RENDERER));
        return false;
    }
    if (!(gl_version_at_least(4, 4) || gl_has_extension("GL_ARB_buffer_storage")) ||
        !glf.BufferStorage) {
        GL_LOG("OpenGL 4.3 with ARB_buffer_storage is needed; this driver offers %s (%s)", version,
               gl_str(GL_RENDERER));
        return false;
    }
    g_gl.core = (gl_int(GL_CONTEXT_PROFILE_MASK) & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
    g_gl.hasTextureBarrier =
        (gl_version_at_least(4, 5) || gl_has_extension("GL_ARB_texture_barrier")) &&
        glf.TextureBarrier;
    g_gl.hasInterlock = gl_has_extension("GL_ARB_fragment_shader_interlock");
    g_gl.hasAnisotropy = gl_version_at_least(4, 6) ||
                         gl_has_extension("GL_ARB_texture_filter_anisotropic") ||
                         gl_has_extension("GL_EXT_texture_filter_anisotropic");
    g_gl.hasTimerQuery = gl_version_at_least(3, 3) || gl_has_extension("GL_ARB_timer_query");
    g_gl.hasS3tc = gl_has_extension("GL_EXT_texture_compression_s3tc");
    g_gl.hasBptc = gl_version_at_least(4, 2) || gl_has_extension("GL_ARB_texture_compression_bptc");
    snprintf(g_gl.adapterName, sizeof(g_gl.adapterName), "%s", gl_str(GL_RENDERER));
    snprintf(g_gl.versionText, sizeof(g_gl.versionText), "%s", version);
    /* the summary line's driver part: renderer and version */
    snprintf(g_gl.driverText, sizeof(g_gl.driverText), "%s %s", gl_str(GL_RENDERER), version);
    return true;
}

/* The device's fixed state: the D3D-style clip space, no dithering or sRGB
 * conversion, the vertex shader's point size, no culling or depth clamp,
 * the scissor test always on (the passes set the scissor), the first
 * vertex's flat attributes (D3D's and Vulkan's provoking vertex), tightly
 * packed pixel rows. */
static void gl_defaults(void)
{
    glf.ClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    glf.Disable(GL_DITHER);
    glf.Disable(GL_FRAMEBUFFER_SRGB);
    glf.Enable(GL_PROGRAM_POINT_SIZE);
    glf.Disable(GL_CULL_FACE);
    glf.Disable(GL_DEPTH_CLAMP);
    glf.Enable(GL_SCISSOR_TEST);
    glf.Disable(GL_PRIMITIVE_RESTART);
    glf.Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    glf.ProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    glf.DepthRangef(0.0f, 1.0f);
    glf.PixelStorei(GL_PACK_ALIGNMENT, 1);
    glf.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glf.ActiveTexture(GL_TEXTURE0);
}

static bool gl_pools_init(void)
{
    return d3dp_pool_init(&g_gl.buffers, "buffer", 4096, sizeof(GlBuffer)) &&
           d3dp_pool_init(&g_gl.textures, "texture", 8192, sizeof(GlTexture)) &&
           d3dp_pool_init(&g_gl.samplers, "sampler", 1024, sizeof(GlSampler)) &&
           d3dp_pool_init(&g_gl.shaders, "shader", 512, sizeof(GlShader)) &&
           d3dp_pool_init(&g_gl.layouts, "bind group layout", 64, sizeof(GlLayout)) &&
           d3dp_pool_init(&g_gl.pipelines, "pipeline", RHI_PIPELINE_POOL_MAX, sizeof(GlPipeline));
}

static void gl_pools_free(void)
{
    d3dp_pool_free(&g_gl.buffers);
    d3dp_pool_free(&g_gl.textures);
    d3dp_pool_free(&g_gl.samplers);
    d3dp_pool_free(&g_gl.shaders);
    d3dp_pool_free(&g_gl.layouts);
    d3dp_pool_free(&g_gl.pipelines);
}

bool rhi_init(const RhiDeviceDesc *desc)
{
    if (g_gl.initialised) {
        return true;
    }
    memset(&g_gl, 0, sizeof(g_gl));
    s_debugErrors = 0;
    s_debugActive = false;
    const bool debug = gl_debug_wanted(desc);
    if (!gl_create_context(desc, debug)) {
        rhi_shutdown();
        return false;
    }
    unsigned optionalMissing = 0;
    if (!gl_loader_load(&optionalMissing) || !gl_check_version()) {
        rhi_shutdown();
        return false;
    }
    if (debug) {
        gl_debug_setup();
    }
    gl_defaults();
    gl_fill_limits();
    if (!gl_pools_init()) {
        GL_LOG("out of memory for the object pools");
        rhi_shutdown();
        return false;
    }
    glf.GenFramebuffers(1, &g_gl.scratchRead);
    glf.GenFramebuffers(1, &g_gl.scratchDraw);
    gl_timestamps_init();
    g_gl.initialised = true;
    gl_cmd_init();
    if (g_gl.window) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(g_gl.window, &pw, &ph);
        g_gl.vsync = desc->vsync;
        if (!gl_swapchain_create(pw > 0 ? (uint32_t)pw : 0u, ph > 0 ? (uint32_t)ph : 0u,
                                 desc->vsync)) {
            GL_LOG("no backbuffer for the window (%dx%d)", pw, ph);
            rhi_shutdown();
            return false;
        }
    }
    s_debugActive = g_gl.debug;
    const RhiLimits *l = &g_gl.limits;
    char msaa[16];
    snprintf(msaa, sizeof(msaa), "1%s%s%s", (l->msaaCounts & 2u) ? "|2" : "",
             (l->msaaCounts & 4u) ? "|4" : "", (l->msaaCounts & 8u) ? "|8" : "");
    GL_LOG("%s (OpenGL %d.%d %s, %s; GLSL %s)%s; index values up to %u", g_gl.adapterName,
           g_gl.major, g_gl.minor, g_gl.core ? "core" : "compatibility", g_gl.versionText,
           gl_str(GL_SHADING_LANGUAGE_VERSION), g_gl.window ? "" : ", headless",
           l->maxDrawIndexedIndexValue);
    GL_LOG("limits: uniform align %u, dual-source %s, MSAA %s, interlock %s (image units %d), "
           "texture barrier %s, BC %s, anisotropy %.0f, max texture %u, entry points %u (%u "
           "optional missing), debug output %s",
           l->uniformAlign, l->dualSourceBlend ? "yes" : (s_fakeNoDual ? "no (faked)" : "no"), msaa,
           l->fragmentInterlock ? "yes" : "no", g_gl.maxImageUnits,
           g_gl.hasTextureBarrier ? "yes" : "no", l->bcTextures ? "yes" : "no",
           (double)l->maxAnisotropy, l->maxTextureSize, gl_loader_count(), optionalMissing,
           g_gl.debug ? "on" : "off");
    /* the translated shaders' storage buffers (bindings 16 + t, t up to 4,
     * read by the VU vertex shaders) against limits OpenGL 4.3 lets a
     * driver keep small (8 bindings, no vertex storage blocks) */
    const GLint ssboBindings = gl_int(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS);
    const GLint vertexSsbos = gl_int(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS);
    const GLint units = gl_int(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    if (ssboBindings <= (GLint)GL_UNIT_STORAGE_BASE + 4 || vertexSsbos < 1 ||
        units <= (GLint)GL_UNIT_TEXTURE_BASE + 4) {
        GL_LOG("this driver's limits are below the shaders' bindings: storage buffer bindings %d "
               "(21 needed), vertex shader storage blocks %d (1 needed), texture units %d (21 "
               "needed); the draws that use them will fail",
               ssboBindings, vertexSsbos, units);
    }
    gl_check("rhi_init");
    return true;
}

void rhi_shutdown(void)
{
    const bool loaded = glf.GetString != NULL;
    if (loaded && g_gl.context) {
        glf.Finish();
        if (g_gl.initialised) {
            gl_cmd_shutdown();
        }
        gl_swapchain_destroy();
        for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
            GlFrame *f = &g_gl.frames[i];
            gl_destroy_garbage(f);
            if (f->fence) {
                glf.DeleteSync(f->fence);
            }
            for (uint32_t q = 0; q < RHI_MAX_TIMESTAMPS; q++) {
                if (f->tsQuery[q]) {
                    glf.DeleteQueries(1, &f->tsQuery[q]);
                }
            }
        }
        gl_release_all_objects();
        if (g_gl.scratchRead) {
            glf.DeleteFramebuffers(1, &g_gl.scratchRead);
        }
        if (g_gl.scratchDraw) {
            glf.DeleteFramebuffers(1, &g_gl.scratchDraw);
        }
        if (g_gl.debug) {
            glf.Disable(GL_DEBUG_OUTPUT);
            GL_LOG("debug output: %u errors, %u other messages", g_gl.debugErrors, g_gl.debugOther);
        }
    }
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        GlFrame *f = &g_gl.frames[i];
        free(f->groups);
        free(f->entries);
        free(f->garbage);
    }
    gl_pools_free();
    gl_loader_clear();
    if (g_gl.context) {
        SDL_GL_MakeCurrent(g_gl.window ? g_gl.window : g_gl.ownWindow, NULL);
        SDL_GL_DestroyContext(g_gl.context);
    }
    if (g_gl.ownWindow) {
        SDL_DestroyWindow(g_gl.ownWindow);
    }
    if (g_gl.ownVideo) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    s_debugErrors = g_gl.debugErrors; /* readable after shutdown, for tests */
    memset(&g_gl, 0, sizeof(g_gl));
}

RhiBackendKind rhi_backend(void)
{
    return RHI_BACKEND_OPENGL;
}

const RhiLimits *rhi_limits(void)
{
    return &g_gl.limits;
}

const char *rhi_adapter_name(void)
{
    return g_gl.adapterName;
}

/* A context reset (ARB_robustness) is not asked for: no loss to report */
bool rhi_device_lost(void)
{
    return false;
}

/* The counts the Vulkan backend keeps, at the same points (g_gl.counts:
 * draws, passes, binds, barriers, copies, submits by gl_cmd.c; buffers,
 * textures and bind groups by gl_resource.c), the fence waits, swapchains
 * and presents; the memory gauges read zero (GL hides its allocations). */
void rhi_get_stats(RhiStats *out)
{
    if (!out) {
        return;
    }
    *out = g_gl.counts;
    out->swapchains = g_gl.swapchains;
    out->presents = g_gl.presents;
    out->presentNs = g_gl.presentNs;
    out->acquireNs = g_gl.acquireNs;
    out->fenceWaits = g_gl.fenceWaits;
    out->fenceWaitNs = g_gl.fenceWaitNs;
    out->pipelinePoolSize = g_gl.pipelines.cap;
    out->pipelinesLive = g_gl.pipelines.next - g_gl.pipelines.freeCount;
    out->pipelinesPeak = g_gl.pipelines.next;
    out->pipelinePoolFull = g_gl.pipelines.full;
}

/* ------------------------------------------------------ present modes
 * The swap interval is the only control GL gives: vsync on is FIFO, off
 * immediate; no mailbox, no exclusive fullscreen choice, no display timing
 * (rhi.h's answers for a backend without them). */
void rhi_prefer_mailbox(bool on)
{
    if (on != g_gl.preferMailbox) {
        GL_LOG("mailbox presents asked for %s: OpenGL presents FIFO with vsync, immediate without",
               on ? "on" : "off");
    }
    g_gl.preferMailbox = on;
}

bool rhi_present_mailbox(void)
{
    return false;
}

void rhi_set_exclusive_fullscreen(bool allowed)
{
    g_gl.exclusiveFullscreen = allowed; /* stored: the driver decides */
}

bool rhi_present_timing(void)
{
    return false;
}

void rhi_present_timing_use(bool on)
{
    (void)on;
}

uint64_t rhi_present_last_id(void)
{
    return 0;
}

int rhi_present_timing_next(uint64_t *id, uint64_t *ns)
{
    (void)id;
    (void)ns;
    return -1;
}

/* ------------------------------------------------------- pipeline cache
 * Program binaries (ARB_get_program_binary) are for a later version: the
 * driver's own shader cache applies. */
void rhi_set_pipeline_cache_path(const char *path)
{
    (void)path;
}

bool rhi_pipeline_cache_loaded(void)
{
    return false;
}

bool rhi_pipeline_cache_save(void)
{
    return false;
}

uint64_t rhi_pipeline_cache_size(void)
{
    return 0;
}

void rhi_set_vulkan_loader(void *getInstanceProcAddr)
{
    (void)getInstanceProcAddr; /* Vulkan only */
}

const char *rhi_injector_name(void)
{
    return NULL;
}

const char *rhi_overlay_name(void)
{
    return NULL;
}

/* The rhi_create_backend entry (port/rhi/rhi_backend.h). */
RHI_BACKEND_DEFINE(rhi_backend_gl, "opengl");
