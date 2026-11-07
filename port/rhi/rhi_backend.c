/* rhi_backend.c: the rhi.h entry points, forwarded to the backend
 * rhi_create_backend selected (rhi_backend.h describes the scheme).
 *
 * The backends linked into the build are listed by ICO_RHI_HAVE_VK,
 * ICO_RHI_HAVE_D3D12, ICO_RHI_HAVE_GL and ICO_RHI_HAVE_D3D11
 * (port/rhi/CMakeLists.txt), Vulkan first, so it is the default; [video]
 * backend or ICO_RHI_BACKEND picks another. */
#include "rhi_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const RhiBackendTable *const s_backends[] = {
#ifdef ICO_RHI_HAVE_VK
    &rhi_backend_vk,
#endif
#ifdef ICO_RHI_HAVE_D3D12
    &rhi_backend_d3d12,
#endif
#ifdef ICO_RHI_HAVE_GL
    &rhi_backend_gl,
#endif
#ifdef ICO_RHI_HAVE_D3D11
    &rhi_backend_d3d11,
#endif
    NULL};

static const RhiBackendTable *s_sel;
/* rhi_set_call_guard's (rhi.h): copied, so the caller's struct may go */
static RhiCallGuard s_guard;
static bool s_guardOn;
/* rhi_call_guard_snapshot's counters, rhi_get_stats's answer while busy */
static RhiStats s_guardStats;
/* rhi_set_slot_guard's (rhi.h): copied as the call guard is */
static RhiSlotGuard s_slotGuard;
static bool s_slotOn;
static bool s_up; /* rhi_init succeeded and rhi_shutdown has not run */
/* [video] adapter (rhi_set_adapter_wanted): handed to the backend at each
   rhi_init, whichever backend is selected then */
static char s_adapterWanted[256];
/* rhi_set_wait_hooks (rhi.h): copied, handed to every rhi_init */
static RhiWaitHooks s_waits;
static bool s_waitsOn;
/* rhi_plan_shaders (rhi.h): the start-up set's size and the shaders of it
   made so far, from the first rhi_plan_shaders until rhi_plan_end or the
   first rhi_begin_commands */
static uint32_t s_planTotal, s_planMade;
static bool s_planOpen;

static bool nameEq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') {
            x = (char)(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (char)(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

static const RhiBackendTable *find(const char *name)
{
    /* "vk" is accepted for "vulkan", "gl" for "opengl" */
    if (name && nameEq(name, "vk")) {
        name = "vulkan";
    } else if (name && nameEq(name, "gl")) {
        name = "opengl";
    }
    for (const RhiBackendTable *const *p = s_backends; *p; p++) {
        if (name && nameEq(name, (*p)->name)) {
            return *p;
        }
    }
    return NULL;
}

static const RhiBackendTable *defaultBackend(void)
{
    const char *env = getenv("ICO_RHI_BACKEND");
    const RhiBackendTable *t = (env && *env) ? find(env) : NULL;
    if (env && *env && !t) {
        fprintf(stderr, "rhi: ICO_RHI_BACKEND=%s is not linked into this build; using %s\n", env,
                s_backends[0] ? s_backends[0]->name : "nothing");
    }
    return t ? t : s_backends[0];
}

bool rhi_create_backend(const char *name)
{
    if (s_up) {
        fprintf(stderr, "rhi: rhi_create_backend(%s) while a device is up\n", name ? name : "");
        return false;
    }
    const RhiBackendTable *t = (name && *name) ? find(name) : defaultBackend();
    if (!t) {
        fprintf(stderr, "rhi: backend \"%s\" is not linked into this build\n",
                name ? name : "(default)");
        return false;
    }
    s_sel = t;
    return true;
}

const char *rhi_backend_name(uint32_t index)
{
    for (uint32_t i = 0; s_backends[i]; i++) {
        if (i == index) {
            return s_backends[i]->name;
        }
    }
    return NULL;
}

const char *rhi_backend_kind_name(RhiBackendKind kind)
{
    switch (kind) {
    case RHI_BACKEND_VULKAN:
        return "Vulkan";
    case RHI_BACKEND_D3D12:
        return "D3D12";
    case RHI_BACKEND_METAL:
        return "Metal";
    case RHI_BACKEND_OPENGL:
        return "OpenGL";
    case RHI_BACKEND_D3D11:
        return "D3D11";
    }
    return "?";
}

static const RhiBackendTable *be(void)
{
    if (!s_sel) {
        s_sel = defaultBackend();
        if (!s_sel) {
            fprintf(stderr, "rhi: no backend linked into this build\n");
            abort();
        }
    }
    return s_sel;
}

/* an entry point that changes the device's state waits while the guard's
 * other thread works (rhi.h rhi_set_call_guard) */
#define RHI_GUARD()                                                                                \
    do {                                                                                           \
        if (s_guardOn && s_guard.busy(s_guard.user)) {                                             \
            s_guard.wait(s_guard.user);                                                            \
        }                                                                                          \
    } while (0)

/* a slot call (rhi.h rhi_set_slot_guard) on frame slot `slot`: the call
 * guard's wait, but where the slot guard lets the calling thread run beside
 * the other thread's work of another slot (the backend's slots apart) */
static bool slotFree(uint32_t slot)
{
    return s_slotOn && s_up && be()->limits()->slotOverlap &&
           s_slotGuard.free(s_slotGuard.user, slot);
}

#define RHI_GUARD_SLOT(slot)                                                                       \
    do {                                                                                           \
        if (s_guardOn && s_guard.busy(s_guard.user) && !slotFree(slot)) {                          \
            s_guard.wait(s_guard.user);                                                            \
        }                                                                                          \
    } while (0)

/* the recording thread's slot, and the one its next rhi_wait_frame
 * recycles */
#define RHI_SLOT_NOW() (s_up ? be()->frame_slot() : 0u)
#define RHI_SLOT_NEXT() ((RHI_SLOT_NOW() + 1u) % RHI_FRAMES_IN_FLIGHT)

void rhi_set_slot_guard(const RhiSlotGuard *guard)
{
    if (guard != NULL && guard->free != NULL && guard->pending != NULL && guard->worker != NULL) {
        s_slotGuard = *guard;
        s_slotOn = true;
    } else {
        s_slotOn = false;
        memset(&s_slotGuard, 0, sizeof(s_slotGuard));
    }
}

bool rhi_slot_guard_pending(uint32_t slot)
{
    return s_slotOn && s_slotGuard.pending(s_slotGuard.user, slot);
}

bool rhi_slot_guard_worker(void)
{
    return s_slotOn && s_slotGuard.worker(s_slotGuard.user);
}

/* rhi_set_thread_begin (rhi.h): set once before the first rhi_init, read
   by the threads the backend makes */
static void (*s_threadBegin)(void);

void rhi_set_thread_begin(void (*fn)(void))
{
    __atomic_store_n(&s_threadBegin, fn, __ATOMIC_RELEASE);
}

void rhi_thread_begin(void)
{
    void (*fn)(void) = __atomic_load_n(&s_threadBegin, __ATOMIC_ACQUIRE);

    if (fn != NULL) {
        fn();
    }
}

void rhi_set_call_guard(const RhiCallGuard *guard)
{
    if (guard != NULL && guard->busy != NULL && guard->wait != NULL) {
        s_guard = *guard;
        s_guardOn = true;
    } else {
        s_guardOn = false;
        memset(&s_guard, 0, sizeof(s_guard));
    }
}

void rhi_call_guard_settle(void)
{
    RHI_GUARD();
}

void rhi_call_guard_snapshot(void)
{
    if (s_up) {
        be()->get_stats(&s_guardStats);
    }
}

/* ------------------------------------------------------------- forwarders */
bool rhi_init(const RhiDeviceDesc *desc)
{
    RHI_GUARD();
    if (s_up) {
        return true;
    }
    be()->set_adapter_wanted(s_adapterWanted);
    /* the renderer plans the new device's set */
    s_planTotal = s_planMade = 0;
    s_planOpen = false;
    if (desc && !desc->waits && s_waitsOn) {
        RhiDeviceDesc d = *desc;
        d.waits = &s_waits;
        s_up = be()->init(&d);
    } else {
        s_up = be()->init(desc);
    }
    return s_up;
}

void rhi_set_wait_hooks(const RhiWaitHooks *hooks)
{
    s_waitsOn = hooks != NULL;
    if (hooks) {
        s_waits = *hooks;
    } else {
        memset(&s_waits, 0, sizeof(s_waits));
    }
}

void rhi_plan_shaders(uint32_t count)
{
    s_planTotal += count;
    s_planOpen = s_planOpen || count > 0;
}

void rhi_plan_progress(uint32_t *made, uint32_t *total)
{
    *made = s_planOpen ? s_planMade : 0u;
    *total = s_planOpen ? (s_planMade > s_planTotal ? s_planMade : s_planTotal) : 0u;
}

/* the device's start moves (RhiWaitHooks.made) */
static void noteMade(void)
{
    if (s_waitsOn && s_waits.made) {
        s_waits.made(s_waits.ctx);
    }
}

void rhi_plan_end(void)
{
    if (!s_planOpen) {
        return;
    }
    if (s_planMade != s_planTotal) {
        /* the count and rd__gpu_init's loops apart (rd_shader_plan.c):
           the screens counted wrong, nothing else */
        fprintf(stderr, "rhi: the start-up shader set ended with %u made of the %u planned (%s)\n",
                s_planMade, s_planTotal,
                s_planMade < s_planTotal
                    ? "the renderer's start stopped early, or the plan counted modules it did "
                      "not make"
                    : "the plan counted fewer than the renderer made");
    }
    s_planOpen = false;
    s_planTotal = s_planMade = 0;
    noteMade(); /* the window's line for the set's end (rhi_plan_progress 0 and 0) */
}

void rhi_shutdown(void)
{
    RHI_GUARD();
    be()->shutdown();
    s_up = false;
}

RhiBackendKind rhi_backend(void)
{
    return be()->backend();
}

const RhiLimits *rhi_limits(void)
{
    return be()->limits();
}

const char *rhi_adapter_name(void)
{
    return be()->adapter_name();
}

uint32_t rhi_adapter_list(RhiAdapterInfo *out, uint32_t max)
{
    return be()->adapter_list(out, max);
}

void rhi_set_adapter_wanted(const char *name)
{
    snprintf(s_adapterWanted, sizeof(s_adapterWanted), "%s", name != NULL ? name : "");
}

const char *rhi_adapter_wanted(void)
{
    return s_adapterWanted;
}

bool rhi_device_lost(void)
{
    return s_up && be()->device_lost();
}

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
    RHI_GUARD();
    return be()->resize_swapchain(width, height, vsync);
}

RhiFormat rhi_swapchain_format(void)
{
    return be()->swapchain_format();
}

bool rhi_swapchain_size(uint32_t *w, uint32_t *h)
{
    return be()->swapchain_size(w, h);
}

uint32_t rhi_swapchain_rotation(void)
{
    return be()->swapchain_rotation();
}

void rhi_surface_poll_restart(void)
{
    RHI_GUARD();
    be()->surface_poll_restart();
}

RhiTexture rhi_acquire_backbuffer(void)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->acquire_backbuffer();
}

void rhi_present(void)
{
    RHI_GUARD();
    be()->present();
}

void rhi_release_surface(void)
{
    RHI_GUARD();
    be()->release_surface();
}

bool rhi_recreate_surface(void *window)
{
    RHI_GUARD();
    return be()->recreate_surface(window);
}

RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    RHI_GUARD();
    return be()->create_buffer(desc);
}

void rhi_destroy_buffer(RhiBuffer b)
{
    RHI_GUARD();
    be()->destroy_buffer(b);
}

void *rhi_map_buffer(RhiBuffer b)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->map_buffer(b);
}

void rhi_unmap_buffer(RhiBuffer b)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    be()->unmap_buffer(b);
}

void rhi_buffer_note_written(RhiBuffer b, uint64_t hi)
{
    be()->buffer_note_written(b, hi);
}

RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    RHI_GUARD();
    return be()->create_texture(desc);
}

void rhi_destroy_texture(RhiTexture t)
{
    RHI_GUARD();
    be()->destroy_texture(t);
}

RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc)
{
    RHI_GUARD();
    return be()->create_sampler(desc);
}

void rhi_destroy_sampler(RhiSampler s)
{
    RHI_GUARD();
    be()->destroy_sampler(s);
}

RhiShader rhi_create_shader(const RhiShaderDesc *desc)
{
    RHI_GUARD();
    RhiShader s;
    if (desc && s_planOpen) {
        /* one of the start-up set: its number for the backend's progress
           screen (OpenGL's, gl_start_screen.c) */
        RhiShaderDesc d = *desc;
        d.planIndex = ++s_planMade;
        d.planTotal = s_planMade > s_planTotal ? s_planMade : s_planTotal;
        s = be()->create_shader(&d);
    } else {
        s = be()->create_shader(desc);
    }
    noteMade(); /* the window's lines read rhi_plan_progress there */
    return s;
}

void rhi_destroy_shader(RhiShader s)
{
    RHI_GUARD();
    be()->destroy_shader(s);
}

RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc)
{
    RHI_GUARD();
    return be()->create_bind_group_layout(desc);
}

void rhi_destroy_bind_group_layout(RhiBindGroupLayout l)
{
    RHI_GUARD();
    be()->destroy_bind_group_layout(l);
}

RhiBindGroup rhi_create_bind_group(const RhiBindGroupDesc *desc)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->create_bind_group(desc);
}

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *desc)
{
    RHI_GUARD();
    const RhiPipeline p = be()->create_pipeline(desc);
    noteMade();
    return p;
}

uint32_t rhi_create_pipelines(const RhiPipelineDesc *descs, uint32_t n, RhiPipeline *out,
                              RhiPipelineBatch *batch)
{
    RHI_GUARD();
    return be()->create_pipelines(descs, n, out, batch);
}

RhiPipelineJobs *rhi_create_pipelines_begin(const RhiPipelineDesc *descs, uint32_t n,
                                            uint32_t maxThreads, bool lowPriority)
{
    RHI_GUARD();
    return be()->create_pipelines_begin(descs, n, maxThreads, lowPriority);
}

bool rhi_create_pipelines_end(RhiPipelineJobs *jobs, bool wait, RhiPipeline *out, float *ms,
                              uint32_t *threads)
{
    RHI_GUARD();
    return be()->create_pipelines_end(jobs, wait, out, ms, threads);
}

void rhi_destroy_pipeline(RhiPipeline p)
{
    RHI_GUARD();
    be()->destroy_pipeline(p);
}

RhiCommandList rhi_begin_commands(void)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    if (s_planOpen) {
        rhi_plan_end(); /* a frame: the start-up set is over */
    }
    return be()->begin_commands();
}

void rhi_end_commands(RhiCommandList cl)
{
    /* a slot call: the renderer ends a picture's scene list on the render
     * thread before it begins the present's (port/render/rd_submit.c),
     * beside the submit thread's work of the other slot; an end touches
     * only its own list (vk_cmd.c "Frame slots") */
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    be()->end_commands(cl);
}

void rhi_submit(RhiCommandList cl)
{
    RHI_GUARD();
    be()->submit(cl);
}

void rhi_wait_frame(void)
{
    RHI_GUARD_SLOT(RHI_SLOT_NEXT());
    be()->wait_frame();
}

bool rhi_commands_left(void)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->commands_left();
}

uint32_t rhi_frame_slot(void)
{
    return be()->frame_slot();
}

void rhi_wait_idle(void)
{
    RHI_GUARD();
    be()->wait_idle();
}

void rhi_collect_garbage_now(void)
{
    RHI_GUARD();
    be()->collect_garbage_now();
}

void rhi_cmd_barrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    be()->cmd_barrier(cl, barriers, count);
}

void rhi_cmd_begin_render_pass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    be()->cmd_begin_render_pass(cl, pass);
}

void rhi_cmd_end_render_pass(RhiCommandList cl)
{
    be()->cmd_end_render_pass(cl);
}

void rhi_cmd_set_viewport(RhiCommandList cl, const RhiViewport *vp)
{
    be()->cmd_set_viewport(cl, vp);
}

void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *rect)
{
    be()->cmd_set_scissor(cl, rect);
}

void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p)
{
    be()->cmd_set_pipeline(cl, p);
}

void rhi_cmd_set_bind_group(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    be()->cmd_set_bind_group(cl, group, bg);
}

void rhi_cmd_set_bind_group_offsets(RhiCommandList cl, uint32_t group, RhiBindGroup bg,
                                    const uint32_t *offsets, uint32_t count)
{
    be()->cmd_set_bind_group_offsets(cl, group, bg, offsets, count);
}

void rhi_cmd_set_vertex_buffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    be()->cmd_set_vertex_buffer(cl, binding, b, offset);
}

void rhi_cmd_set_index_buffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    be()->cmd_set_index_buffer(cl, b, offset, u32);
}

void rhi_cmd_set_stencil_ref(RhiCommandList cl, uint8_t ref)
{
    be()->cmd_set_stencil_ref(cl, ref);
}

void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4])
{
    be()->cmd_set_blend_constant(cl, rgba);
}

void rhi_cmd_set_depth_state(RhiCommandList cl, bool test, bool write, RhiCompare cmp)
{
    be()->cmd_set_depth_state(cl, test, write, cmp);
}

void rhi_cmd_set_blend_state(RhiCommandList cl, const RhiBlendState *blend)
{
    be()->cmd_set_blend_state(cl, blend);
}

void rhi_cmd_local_read_barrier(RhiCommandList cl, uint32_t mask)
{
    be()->cmd_local_read_barrier(cl, mask);
}

void rhi_cmd_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount)
{
    be()->cmd_draw(cl, vertexCount, firstVertex, instanceCount);
}

void rhi_cmd_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                          int32_t vertexOffset, uint32_t instanceCount)
{
    be()->cmd_draw_indexed(cl, indexCount, firstIndex, vertexOffset, instanceCount);
}

void rhi_cmd_copy_buffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                         uint64_t dstOffset, uint64_t size)
{
    be()->cmd_copy_buffer(cl, src, srcOffset, dst, dstOffset, size);
}

void rhi_cmd_copy_buffer_to_texture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                    uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    be()->cmd_copy_buffer_to_texture(cl, src, srcOffset, rowPitch, dst, mip, region);
}

void rhi_cmd_copy_texture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                          int32_t dstX, int32_t dstY)
{
    be()->cmd_copy_texture(cl, src, srcRegion, dst, dstX, dstY);
}

void rhi_cmd_copy_texture_to_buffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                    RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                    uint32_t rowPitch)
{
    be()->cmd_copy_texture_to_buffer(cl, src, aspect, region, dst, dstOffset, rowPitch);
}

void rhi_cmd_resolve_color(RhiCommandList cl, RhiTexture src, RhiTexture dst)
{
    be()->cmd_resolve_color(cl, src, dst);
}

void rhi_cmd_begin_label(RhiCommandList cl, const char *name)
{
    be()->cmd_begin_label(cl, name);
}

void rhi_cmd_end_label(RhiCommandList cl)
{
    be()->cmd_end_label(cl);
}

void rhi_cmd_checkpoint(RhiCommandList cl, const void *marker)
{
    be()->cmd_checkpoint(cl, marker);
}

void rhi_set_submit_tag(uint32_t replay, uint32_t frame)
{
    if (s_up) {
        be()->set_submit_tag(replay, frame);
    }
}

/* any thread (rhi.h): s_up and s_sel are set by rhi_init on the main
 * thread long before a watchdog asks */
bool rhi_gpu_progress(RhiGpuProgress *out)
{
    if (!s_up || !s_sel) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return s_sel->gpu_progress(out);
}

bool rhi_readback_texture(RhiTexture t, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch)
{
    RHI_GUARD();
    return be()->readback_texture(t, aspect, dst, dstSize, outRowPitch);
}

void rhi_get_stats(RhiStats *out)
{
    if (s_guardOn &&
        (s_guard.stale != NULL ? s_guard.stale(s_guard.user) : s_guard.busy(s_guard.user))) {
        *out = s_guardStats; /* the hand-over's counters (rhi.h rhi_set_call_guard) */
        return;
    }
    be()->get_stats(out);
}

bool rhi_timestamps_supported(void)
{
    return be()->timestamps_supported();
}

void rhi_cmd_write_timestamp(RhiCommandList cl, uint32_t index)
{
    be()->cmd_write_timestamp(cl, index);
}

uint32_t rhi_read_timestamps(uint64_t *ns, uint32_t max)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->read_timestamps(ns, max);
}

uint32_t rhi_read_timestamps_busy(uint64_t *ns, uint32_t max)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->read_timestamps_busy(ns, max);
}

bool rhi_occlusion_supported(void)
{
    return be()->occlusion_supported();
}

bool rhi_occlusion_prepare(uint32_t count)
{
    return be()->occlusion_prepare(count);
}

void rhi_cmd_begin_occlusion(RhiCommandList cl, uint32_t index)
{
    be()->cmd_begin_occlusion(cl, index);
}

void rhi_cmd_end_occlusion(RhiCommandList cl, uint32_t index)
{
    be()->cmd_end_occlusion(cl, index);
}

uint32_t rhi_read_occlusion(uint64_t *samples, uint32_t max)
{
    RHI_GUARD_SLOT(RHI_SLOT_NOW());
    return be()->read_occlusion(samples, max);
}

void rhi_prefer_mailbox(bool on)
{
    RHI_GUARD();
    be()->prefer_mailbox(on);
}

bool rhi_present_shown(void)
{
    return be()->present_shown();
}

bool rhi_present_split(RhiPresentSplit *out)
{
    return be()->present_split(out);
}

bool rhi_wait_present_room(void)
{
    RHI_GUARD();
    return be()->wait_present_room();
}

bool rhi_present_mailbox(void)
{
    return be()->present_mailbox();
}

void rhi_present_fifo_fallback(bool on)
{
    RHI_GUARD();
    be()->present_fifo_fallback(on);
}

bool rhi_present_fifo_fallback_on(void)
{
    return be()->present_fifo_fallback_on();
}

void rhi_present_fifo_start(RhiFifoStartFn eligible)
{
    RHI_GUARD();
    be()->present_fifo_start(eligible);
}

void rhi_set_exclusive_fullscreen(bool allowed)
{
    RHI_GUARD();
    be()->set_exclusive_fullscreen(allowed);
}

const char *rhi_present_mode_name(void)
{
    return be()->present_mode_name();
}

bool rhi_present_timing(void)
{
    return s_up && be()->present_timing();
}

void rhi_present_timing_use(bool on)
{
    RHI_GUARD();
    if (s_up) {
        be()->present_timing_use(on);
    }
}

uint64_t rhi_present_last_id(void)
{
    return s_up ? be()->present_last_id() : 0;
}

int rhi_present_timing_next(uint64_t *id, uint64_t *ns)
{
    return s_up ? be()->present_timing_next(id, ns) : -1;
}

bool rhi_present_statistics(uint64_t *presentCount, uint64_t *presentRefresh, uint64_t *syncRefresh,
                            uint64_t *syncNs)
{
    return s_up && be()->present_statistics(presentCount, presentRefresh, syncRefresh, syncNs);
}

void rhi_set_pipeline_cache_path(const char *path)
{
    RHI_GUARD();
    be()->set_pipeline_cache_path(path);
}

bool rhi_pipeline_cache_loaded(void)
{
    return be()->pipeline_cache_loaded();
}

bool rhi_pipeline_cache_save(void)
{
    RHI_GUARD();
    return s_up && be()->pipeline_cache_save();
}

uint64_t rhi_pipeline_cache_size(void)
{
    return s_up ? be()->pipeline_cache_size() : 0;
}

void rhi_set_vulkan_loader(void *getInstanceProcAddr)
{
    RHI_GUARD();
    be()->set_vulkan_loader(getInstanceProcAddr);
}

/* The injector and the overlay, from the backend that
 * rhi_init brought up */
const char *rhi_injector_name(void)
{
    return s_up ? be()->injector_name() : NULL;
}

const char *rhi_overlay_name(void)
{
    return s_up ? be()->overlay_name() : NULL;
}

/* ------------------------------------------------ the layer classifiers
 * The known layers: name (a trailing '*' matches any rest), the program,
 * whether it is an overlay, whether the loader takes it as an explicit
 * layer, the variable an implicit one needs set to "1" (its manifest's
 * enable_environment), NULL for none, and the variable that switches it off
 * when set to anything (its manifest's disable_environment), NULL for none
 * (the Mesa overlay is explicit and has none). */
typedef struct RhiKnownLayer {
    const char *layer, *program;
    bool overlay, isExplicit;
    const char *enableEnv, *disableEnv;
} RhiKnownLayer;

static const RhiKnownLayer s_knownLayers[] = {
    {"VK_LAYER_reshade", "ReShade", false, false, NULL, "DISABLE_VK_LAYER_reshade_1"},
    {"VK_LAYER_VKBASALT_post_processing", "vkBasalt", false, false, "ENABLE_VKBASALT",
     "DISABLE_VKBASALT"},
    {"VK_LAYER_VALVE_steam_overlay_*", "Steam overlay", true, false, NULL,
     "DISABLE_VK_LAYER_VALVE_steam_overlay_1"},
    {"VK_LAYER_MESA_overlay", "Mesa overlay", true, true, NULL, NULL},
};

/* a glob with '*' (any run of characters) against s */
static bool globMatch(const char *g, size_t gn, const char *s)
{
    while (gn && *g != '*') {
        if (*s == '\0' || *s != *g) {
            return false;
        }
        g++;
        s++;
        gn--;
    }
    if (gn == 0) {
        return *s == '\0';
    }
    for (;; s++) { /* *g == '*' */
        if (globMatch(g + 1, gn - 1, s)) {
            return true;
        }
        if (*s == '\0') {
            return false;
        }
    }
}

static const RhiKnownLayer *knownLayer(const char *layer)
{
    if (!layer) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_knownLayers) / sizeof(s_knownLayers[0]); i++) {
        const char *g = s_knownLayers[i].layer;
        if (globMatch(g, strlen(g), layer)) {
            return &s_knownLayers[i];
        }
    }
    return NULL;
}

const char *rhi_injector_from_layer_name(const char *layer)
{
    const RhiKnownLayer *k = knownLayer(layer);
    return k && !k->overlay ? k->program : NULL;
}

const char *rhi_overlay_from_layer_name(const char *layer)
{
    const RhiKnownLayer *k = knownLayer(layer);
    return k && k->overlay ? k->program : NULL;
}

/* list (the loader's layer list variables: separated by ',', ':' or ';')
 * names layer: the name, a glob of it, "~all~", or "~explicit~" /
 * "~implicit~" for its kind */
static bool listNames(const char *list, const char *layer, bool isExplicit)
{
    while (list && *list) {
        const size_t n = strcspn(list, ",:;");
        if ((n == 5 && memcmp(list, "~all~", 5) == 0) ||
            (n == 10 && memcmp(list, isExplicit ? "~explicit~" : "~implicit~", 10) == 0) ||
            (n && globMatch(list, n, layer))) {
            return true;
        }
        list += n;
        list += *list != '\0';
    }
    return false;
}

bool rhi_layer_switched_on(const char *layer, const char *(*env)(const char *name))
{
    const RhiKnownLayer *k = knownLayer(layer);
    if (!k || !env) {
        return false;
    }
    if (listNames(env("VK_LOADER_LAYERS_DISABLE"), layer, k->isExplicit)) {
        return false;
    }
    if (k->disableEnv) {
        const char *d = env(k->disableEnv);
        if (d && *d) {
            return false;
        }
    }
    if (k->isExplicit) {
        return listNames(env("VK_INSTANCE_LAYERS"), layer, true) ||
               listNames(env("VK_LOADER_LAYERS_ENABLE"), layer, true);
    }
    if (k->enableEnv) {
        const char *v = env(k->enableEnv);
        return (v && strcmp(v, "1") == 0) ||
               listNames(env("VK_LOADER_LAYERS_ENABLE"), layer, false);
    }
    return true;
}
