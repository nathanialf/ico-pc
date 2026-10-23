/* gl_internal.h: shared state of the OpenGL backend of port/rhi/rhi.h.
 *
 * OpenGL 4.3 core with ARB_clip_control and ARB_buffer_storage, on a context
 * SDL makes (on the game's window, or on a hidden 1x1 window the backend
 * owns when headless); every entry point through gl_loader.h (gl.Name).
 * docs/port/RHI.md, "OpenGL backend", describes the conventions:
 *
 *  - Clip space: glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE) at init, so
 *    the renderer's D3D-style clip space (depth 0..1, row 0 at the top,
 *    FragCoord from the top left) and its viewports and scissors pass
 *    through unflipped.  Texture row 0 is memory row 0 everywhere; only the
 *    default framebuffer is upside down, so the swapchain's image is an
 *    RGBA8 texture blitted y-flipped to framebuffer 0 at present
 *    (gl_swapchain.c).
 *  - Bindings (tools/spirv_to_glsl.py, docs/port/SHADERS.md): uniform
 *    block b at binding b; storage buffer t at 16 + t; a sampled texture t
 *    at texture unit 16 + t, its sampler object at the same unit (GLSL has
 *    combined samplers only: gl_bind_point); storage image u at image unit
 *    48 + u.  Texture unit 0 and the GL_COPY_READ_BUFFER /
 *    GL_COPY_WRITE_BUFFER / GL_PIXEL_PACK_BUFFER / GL_PIXEL_UNPACK_BUFFER
 *    binding points are scratch: any create, copy or readback may change
 *    them, and the active texture unit is unknown between calls.  The
 *    command code (gl_cmd.c) sets what it uses before each use.
 *  - Command lists execute while they are recorded (lists are recorded and
 *    submitted in order, rhi.h); rhi_submit places the frame slot's fence
 *    (gl_frame_fence); rhi_wait_frame waits for the slot's previous fence,
 *    then runs its deferred deletes, resets its bind group arena and reads
 *    its timestamps.
 *
 * Who defines which rhi.h entry point:
 *   gl_device.c     init, shutdown, backend, limits, adapter_name,
 *                   device_lost, get_stats, wait_frame, frame_slot,
 *                   wait_idle, collect_garbage_now, set_submit_tag,
 *                   gpu_progress, cmd_checkpoint, cmd_begin_label,
 *                   cmd_end_label, timestamps_supported,
 *                   cmd_write_timestamp, read_timestamps, the pipeline
 *                   cache entries, set_vulkan_loader, injector_name,
 *                   overlay_name, prefer_mailbox, present_mailbox,
 *                   set_exclusive_fullscreen, the present timing entries,
 *                   the backend table (RHI_BACKEND_DEFINE)
 *   gl_resource.c   buffers, textures, samplers, shaders, bind group
 *                   layouts, bind groups, readback_texture
 *   gl_swapchain.c  resize_swapchain, swapchain_format, swapchain_size,
 *                   surface_poll_restart, acquire_backbuffer, present,
 *                   present_shown, release_surface, recreate_surface,
 *                   present_mode_name, present_statistics
 *   gl_pipeline.c   create_pipeline, create_pipelines, destroy_pipeline
 *   gl_cmd.c        begin_commands, end_commands, submit, and every other
 *                   rhi_cmd_* entry point
 *   gl_glsl.c       rhi_gl_inflate_glsl (the deflated GLSL of the table)
 */
#ifndef PORT_RHI_GL_GL_INTERNAL_H
#define PORT_RHI_GL_GL_INTERNAL_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../d3d12/d3d12_plan.h" /* the handle pools and the bind group layout rules */
#include "../rhi.h"
#include "gl_loader.h"

/* gl_glsl.c (port/rhi/gl/gl_glsl.h): the text of a table entry's deflated
 * GLSL as a malloc'd NUL-terminated string, NULL on failure. */
char *rhi_gl_inflate_glsl(const void *deflated, size_t len, size_t textLen);

/* ------------------------------------------------------------ formats */
typedef struct GlFormat {
    GLenum internal;    /* the storage format (glTexStorage2D) */
    GLenum format;      /* the transfer format (glTexSubImage2D, glReadPixels) */
    GLenum type;        /* the transfer type */
    uint8_t texelBytes; /* a texel in a tightly packed buffer row (a block for BC) */
    bool depth, stencil;
    bool integer; /* sampled as an integer texture: no filtering, sampler 0 */
    bool block;   /* BC: 4x4 blocks of texelBytes, glCompressedTexSubImage2D */
} GlFormat;

/* gl_resource.c: the table entry of an RhiFormat (RHI_FMT_UNKNOWN's for any
 * value out of range: internal 0). */
const GlFormat *gl_format(RhiFormat f);

/* ------------------------------------------------------------ objects */
typedef struct GlBuffer {
    GLuint name;
    uint64_t size;
    uint32_t usage; /* RhiBufferUsage bits */
    RhiMemory memory;
    void *mapped; /* UPLOAD and READBACK: persistently mapped at creation */
} GlBuffer;

typedef struct GlTexture {
    GLuint name;
    GLuint uintView; /* RHI_TEX_UINT_VIEW: the GL_RGBA8UI view (glTextureView), 0 none */
    GLenum target;   /* GL_TEXTURE_2D or GL_TEXTURE_2D_MULTISAMPLE */
    RhiFormat format;
    uint32_t width, height, mips, usage;
    uint32_t samples; /* 1, or the multisampled count */
    bool backbuffer;  /* the swapchain's image (gl_swapchain.c owns it) */
} GlTexture;

typedef struct GlSampler {
    GLuint name;
} GlSampler;

/* A separable program of one stage (glCreateShaderProgramv). */
typedef struct GlShader {
    GLuint program;
    RhiShaderStage stage;
} GlShader;

typedef struct GlLayout {
    D3dpLayout l; /* d3dp_layout_build: slots, dynamic ranks in ascending slot order */
} GlLayout;

/* A pipeline (gl_pipeline.c fills it; the command code applies it): a
 * program pipeline object with the two programs, a vertex array object
 * with the attribute formats and bindings (glVertexAttrib*Format, the
 * buffers bound per draw with glBindVertexBuffer and the strides below),
 * and the fixed-function state, kept in the RHI's terms. */
typedef struct GlPipeline {
    GLuint ppo;
    GLuint vao;
    GLenum mode; /* GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_LINES, GL_POINTS */
    uint32_t strides[RHI_MAX_VERTEX_ATTRS]; /* per vertex buffer binding */
    uint32_t bindingMask;                   /* the bindings the layout uses */
    RhiBlendState blend[RHI_MAX_COLOR_TARGETS];
    RhiDepthStencilState depthStencil;
    RhiFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    uint32_t colorCount;
    RhiFormat depthFormat;
    uint32_t samples;
    RhiDstRead dstRead;
    uint32_t dynamic; /* RHI_DYN_* */
    uint32_t layoutCount;
    uint32_t layoutIds[RHI_MAX_BIND_SLOTS];
} GlPipeline;

/* ----------------------------------------------------- bind groups
 * A transient bind group (rhi.h: valid for the frame it was made in) is a
 * run of entries in its frame slot's arena, each ready for one GL bind:
 *   GL_BIND_UNIFORM  glBindBufferRange(GL_UNIFORM_BUFFER, point, name,
 *                    offset (+ the bind-time offset of rank dyn), size)
 *   GL_BIND_STORAGE  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, point,
 *                    name, offset, size)
 *   GL_BIND_TEXTURE  glActiveTexture(GL_TEXTURE0 + point),
 *                    glBindTexture(target, name), glBindSampler(point,
 *                    sampler)
 *   GL_BIND_IMAGE    glBindImageTexture(point, name, 0, GL_FALSE, 0,
 *                    GL_READ_WRITE, GL_RGBA8)
 * Samplers have no entry of their own: GLSL samples through combined
 * samplers at the texture's unit, so each texture entry carries the sampler
 * object of the group's sampler slot with its number (s1 for t1), else the
 * group's lowest sampler slot; 0 (the texture's own NEAREST parameters) for
 * integer textures, the UINT view and multisampled textures, which GL does
 * not filter.  A slot without a usable binding gets no entry (logged once
 * per kind of fault). */
enum { GL_BIND_UNIFORM = 0, GL_BIND_STORAGE, GL_BIND_TEXTURE, GL_BIND_IMAGE };

#define GL_BIND_STATIC 0xFFu /* GlBindEntry.dyn of a binding without a bind-time offset */
#define GL_UNIT_TEXTURE_BASE 16u
#define GL_UNIT_STORAGE_BASE 16u
#define GL_UNIT_IMAGE_BASE 48u

typedef struct GlBindEntry {
    uint8_t kind;   /* GL_BIND_* */
    uint8_t dyn;    /* the rank of a dynamic uniform (its bind-time offset), or GL_BIND_STATIC */
    uint16_t point; /* binding point, texture unit or image unit */
    GLuint name;    /* buffer, texture (or its UINT view) */
    GLuint sampler; /* GL_BIND_TEXTURE: the sampler object, 0 for none */
    GLenum target;  /* GL_BIND_TEXTURE: GL_TEXTURE_2D or GL_TEXTURE_2D_MULTISAMPLE */
    uint64_t offset, size; /* buffers: the range (a dynamic uniform's base and per-draw size) */
} GlBindEntry;

typedef struct GlBindGroup {
    uint32_t layoutId;
    uint32_t first, count; /* its entries in the frame's arena */
    uint32_t dynCount;     /* the bind-time offsets it takes */
} GlBindGroup;

/* The binding point of an RHI slot of a type (the map above):
 * b -> b, storage buffer t -> 16 + t, sampled texture t -> unit 16 + t,
 * storage texture u -> image unit 48 + u. */
static inline uint32_t gl_bind_point(RhiBindType type, uint32_t slot)
{
    switch (type) {
    case RHI_BIND_UNIFORM_BUFFER:
    case RHI_BIND_UNIFORM_BUFFER_DYNAMIC:
        return slot;
    case RHI_BIND_STORAGE_BUFFER:
        return GL_UNIT_STORAGE_BASE + slot;
    case RHI_BIND_STORAGE_TEXTURE:
        return GL_UNIT_IMAGE_BASE + slot;
    case RHI_BIND_SAMPLED_TEXTURE:
    case RHI_BIND_SAMPLER:
    default:
        return GL_UNIT_TEXTURE_BASE + slot;
    }
}

/* ------------------------------------------------------ command lists
 * gl_cmd.c owns the command state; the frame slot only counts and resets
 * the lists (handle id: (frame tag << 20) | (index + 1), as D3D12's).
 * gl_cmd.c may add fields. */
#define GL_MAX_CMD_LISTS 8

typedef struct GlCmdList {
    bool recording, submitted;
    bool inPass;
    uint64_t serial; /* unique per recording, never 0 */
} GlCmdList;

/* ---------------------------------------------------- deferred deletes
 * GL keeps an object alive while queued commands use it, but its name can
 * be handed out again at once; a bind group made earlier in the frame
 * holds names, so deletes wait for the slot's recycle (rhi_wait_frame). */
enum {
    GL_GARBAGE_BUFFER = 0,
    GL_GARBAGE_TEXTURE,
    GL_GARBAGE_SAMPLER,
    GL_GARBAGE_PROGRAM,
    GL_GARBAGE_PIPELINE, /* a program pipeline object */
    GL_GARBAGE_VAO,
    GL_GARBAGE_FRAMEBUFFER,
    GL_GARBAGE_KINDS
};

typedef struct GlGarbage {
    uint8_t kind;
    GLuint name;
} GlGarbage;

/* --------------------------------------------------------- frame slot */
typedef struct GlFrame {
    GLsync fence; /* placed by the slot's last submit (gl_frame_fence) */
    GlCmdList lists[GL_MAX_CMD_LISTS];
    uint32_t listCount;
    /* the transient bind groups and their entries */
    GlBindGroup *groups;
    uint32_t groupCount, groupCap;
    GlBindEntry *entries;
    uint32_t entryCount, entryCap;
    GlGarbage *garbage;
    uint32_t garbageCount, garbageCap;
    /* GPU timestamps: a query object per index, made at its first use;
     * the indices written this frame */
    GLuint tsQuery[RHI_MAX_TIMESTAMPS];
    uint32_t tsWritten[RHI_TIMESTAMP_WORDS];
} GlFrame;

/* --------------------------------------------------------------- device */
typedef struct GlState {
    bool initialised;
    RhiLimits limits;
    char adapterName[160];
    char driverText[320];
    char versionText[160];
    int major, minor; /* GL_MAJOR_VERSION, GL_MINOR_VERSION */
    bool core;        /* a core profile context */

    /* the context and the window it is current on */
    SDL_GLContext context;
    SDL_Window *window;    /* the game's window (RhiDeviceDesc.sdlWindow), NULL headless */
    SDL_Window *ownWindow; /* the hidden 1x1 window of a headless device */
    bool ownVideo;         /* rhi_init started SDL's video subsystem */

    /* extensions and limits the other files read */
    bool hasTextureBarrier; /* glTextureBarrier (4.5 or ARB_texture_barrier) */
    bool hasInterlock;      /* ARB_fragment_shader_interlock */
    bool hasAnisotropy;     /* GL_MAX_TEXTURE_MAX_ANISOTROPY (4.6 or the extensions) */
    bool hasTimerQuery;
    bool hasS3tc, hasBptc;
    GLint uboAlign, ssboAlign; /* the offset alignments of uniform and storage ranges */
    GLint maxUniformBlockSize;
    GLint maxImageUnits;

    /* the debug output (KHR_debug, RhiDeviceDesc.debugLayers) */
    bool debug;
    uint32_t debugErrors, debugOther;
    uint32_t labelDepth; /* debug groups open (rhi_cmd_begin_label) */
    GLint maxLabelDepth;

    /* frames */
    GlFrame frames[RHI_FRAMES_IN_FLIGHT];
    uint64_t frameIndex;
    uint64_t listSerial;

    /* objects (d3d12_plan.h's pools: id = (generation << 20) | (index + 1)) */
    D3dpPool buffers, textures, samplers, shaders, layouts, pipelines;

    /* scratch framebuffers: a read and a draw one for readbacks, copies,
     * resolves and the present's blit */
    GLuint scratchRead, scratchDraw;

    /* the swapchain (gl_swapchain.c): the backbuffer texture's id, its
     * size, the vsync asked for; absent headless and after
     * rhi_release_surface */
    uint32_t backbuffer;
    uint32_t swapWidth, swapHeight;
    bool vsync;
    bool surface;      /* a window to present to */
    bool hadSurface;   /* rhi_swapchain_format keeps RGBA8 after a release */
    bool acquired;     /* rhi_acquire_backbuffer since the last present */
    bool presentShown; /* rhi_present_shown */
    bool preferMailbox, exclusiveFullscreen;
    uint64_t swapchains, presents, presentNs, acquireNs;

    /* GPU timestamps (ARB_timer_query): the last recycled slot's, in ns */
    bool timestamps;
    uint64_t tsResult[RHI_MAX_TIMESTAMPS];
    uint32_t tsCount;

    /* rhi_set_submit_tag (kept for the logs) */
    uint32_t tagReplay, tagFrame;

    /* RhiStats: the counts the other files keep at the Vulkan backend's
     * points, and the fence waits */
    RhiStats counts;
    uint64_t fenceWaits, fenceWaitNs;
} GlState;

extern GlState g_gl;

#define GL_LOG(...)                                                                                \
    do {                                                                                           \
        fprintf(stderr, "rhi_gl: " __VA_ARGS__);                                                   \
        fputc('\n', stderr);                                                                       \
    } while (0)

/* A rate for repeated failure lines: true for the first 8 of a counter,
 * then for every 100th (*n counts every call). */
static inline bool gl_log_rate(uint32_t *n)
{
    const uint32_t k = ++*n;
    return k <= 8u || k % 100u == 0u;
}

static inline uint64_t gl_now_ns(void)
{
    return SDL_GetTicksNS();
}

static inline GlFrame *gl_cur_frame(void)
{
    return &g_gl.frames[g_gl.frameIndex % RHI_FRAMES_IN_FLIGHT];
}

/* the frame tag of transient handles (command lists, bind groups) */
static inline uint32_t gl_frame_tag(void)
{
    return (uint32_t)(g_gl.frameIndex & D3DP_GEN_MASK);
}

/* gl_device.c */
/* After a submit's commands: the slot's fence (glFenceSync, then glFlush),
 * replacing an earlier one of the same frame; rhi_submit calls it. */
void gl_frame_fence(void);
/* True when the context offers an extension (GL_EXTENSIONS, GetStringi). */
bool gl_has_extension(const char *name);
/* Drains glGetError after a step of the backend's own (create, readback):
 * logs "rhi_gl: <what>: GL error 0x...." (rate-limited) and returns false
 * when there was one. */
bool gl_check(const char *what);

/* gl_resource.c */
GlBuffer *gl_get_buffer(RhiBuffer b);
GlTexture *gl_get_texture(RhiTexture t);
GlSampler *gl_get_sampler(RhiSampler s);
GlShader *gl_get_shader(RhiShader s);
GlLayout *gl_get_layout(RhiBindGroupLayout l);
GlBindGroup *gl_get_bind_group(RhiBindGroup g);

/* the entries of a bind group of the current frame */
static inline const GlBindEntry *gl_bind_entries(const GlBindGroup *g)
{
    return &gl_cur_frame()->entries[g->first];
}

/* A GL name deleted when the current frame slot is recycled. */
void gl_defer(uint8_t kind, GLuint name);
/* Runs a slot's deferred deletes. */
void gl_destroy_garbage(GlFrame *f);
/* The backbuffer texture (gl_swapchain.c): made and released outside the
 * deferred path (the device is idle). */
uint32_t gl_register_backbuffer(uint32_t w, uint32_t h);
void gl_release_backbuffer(uint32_t id);
/* rhi_shutdown: every object still alive */
void gl_release_all_objects(void);

/* gl_cmd.c: a texture's GL name is about to be deleted (its deferred
 * delete runs): the framebuffers the command code keeps for it go. */
void gl_fbo_forget_texture(GLuint name);
/* gl_cmd.c: the command state at rhi_init (after the device defaults) and
 * at rhi_shutdown (its cached framebuffers deleted while the context is
 * current). */
void gl_cmd_init(void);
void gl_cmd_shutdown(void);

/* gl_swapchain.c */
bool gl_swapchain_create(uint32_t w, uint32_t h, bool vsync);
void gl_swapchain_destroy(void);

#endif /* PORT_RHI_GL_GL_INTERNAL_H */
