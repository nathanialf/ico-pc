/* rhi_backend.c: the rhi.h entry points, forwarded to the backend
 * rhi_create_backend selected (rhi_backend.h describes the scheme).
 *
 * The backends linked into the build are listed by ICO_RHI_HAVE_VK and
 * ICO_RHI_HAVE_D3D12 (port/rhi/CMakeLists.txt), Vulkan first, so it is the
 * default; [video] backend or ICO_RHI_BACKEND picks another. */
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
    NULL};

static const RhiBackendTable *s_sel;
static bool s_up; /* rhi_init succeeded and rhi_shutdown has not run */

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
    /* "vk" is accepted for "vulkan" */
    if (name && nameEq(name, "vk")) {
        name = "vulkan";
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

/* ------------------------------------------------------------- forwarders */
bool rhi_init(const RhiDeviceDesc *desc)
{
    if (s_up) {
        return true;
    }
    s_up = be()->init(desc);
    return s_up;
}

void rhi_shutdown(void)
{
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

bool rhi_device_lost(void)
{
    return s_up && be()->device_lost();
}

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
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

void rhi_surface_poll_restart(void)
{
    be()->surface_poll_restart();
}

RhiTexture rhi_acquire_backbuffer(void)
{
    return be()->acquire_backbuffer();
}

void rhi_present(void)
{
    be()->present();
}

void rhi_release_surface(void)
{
    be()->release_surface();
}

bool rhi_recreate_surface(void *window)
{
    return be()->recreate_surface(window);
}

RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    return be()->create_buffer(desc);
}

void rhi_destroy_buffer(RhiBuffer b)
{
    be()->destroy_buffer(b);
}

void *rhi_map_buffer(RhiBuffer b)
{
    return be()->map_buffer(b);
}

void rhi_unmap_buffer(RhiBuffer b)
{
    be()->unmap_buffer(b);
}

RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    return be()->create_texture(desc);
}

void rhi_destroy_texture(RhiTexture t)
{
    be()->destroy_texture(t);
}

RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc)
{
    return be()->create_sampler(desc);
}

void rhi_destroy_sampler(RhiSampler s)
{
    be()->destroy_sampler(s);
}

RhiShader rhi_create_shader(const RhiShaderDesc *desc)
{
    return be()->create_shader(desc);
}

void rhi_destroy_shader(RhiShader s)
{
    be()->destroy_shader(s);
}

RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc)
{
    return be()->create_bind_group_layout(desc);
}

void rhi_destroy_bind_group_layout(RhiBindGroupLayout l)
{
    be()->destroy_bind_group_layout(l);
}

RhiBindGroup rhi_create_bind_group(const RhiBindGroupDesc *desc)
{
    return be()->create_bind_group(desc);
}

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *desc)
{
    return be()->create_pipeline(desc);
}

void rhi_destroy_pipeline(RhiPipeline p)
{
    be()->destroy_pipeline(p);
}

RhiCommandList rhi_begin_commands(void)
{
    return be()->begin_commands();
}

void rhi_end_commands(RhiCommandList cl)
{
    be()->end_commands(cl);
}

void rhi_submit(RhiCommandList cl)
{
    be()->submit(cl);
}

void rhi_wait_frame(void)
{
    be()->wait_frame();
}

uint32_t rhi_frame_slot(void)
{
    return be()->frame_slot();
}

void rhi_wait_idle(void)
{
    be()->wait_idle();
}

void rhi_collect_garbage_now(void)
{
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

void rhi_cmd_begin_label(RhiCommandList cl, const char *name)
{
    be()->cmd_begin_label(cl, name);
}

void rhi_cmd_end_label(RhiCommandList cl)
{
    be()->cmd_end_label(cl);
}

bool rhi_readback_texture(RhiTexture t, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch)
{
    return be()->readback_texture(t, aspect, dst, dstSize, outRowPitch);
}

void rhi_get_stats(RhiStats *out)
{
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
    return be()->read_timestamps(ns, max);
}

void rhi_prefer_mailbox(bool on)
{
    be()->prefer_mailbox(on);
}

bool rhi_present_mailbox(void)
{
    return be()->present_mailbox();
}

const char *rhi_present_mode_name(void)
{
    return be()->present_mode_name();
}

void rhi_set_pipeline_cache_path(const char *path)
{
    be()->set_pipeline_cache_path(path);
}

void rhi_set_vulkan_loader(void *getInstanceProcAddr)
{
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
