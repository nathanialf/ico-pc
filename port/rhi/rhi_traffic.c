/* rhi_traffic.c: the attachment memory each render pass asks for, and the
 * texture copies' bytes, counted on the CPU (rhi_traffic.h). */
#include "rhi_traffic.h"

#include <string.h>

/* The textures alive by handle: open addressing, linear probing, deletion
   by backward shift (no tombstones: the per-frame targets come and go). */
#define TABLE_BITS 14u
#define TABLE_SIZE (1u << TABLE_BITS)

typedef struct Slot {
    uint32_t id; /* 0 empty */
    uint8_t format, samples;
} Slot;

static Slot s_table[TABLE_SIZE];
static uint32_t s_live;
static char s_lock;

static uint64_t s_colorLoad, s_colorStore, s_depthLoad, s_depthStore, s_clear, s_pixels;
static uint64_t s_copy, s_untracked;

static void lock(void)
{
    while (__atomic_test_and_set(&s_lock, __ATOMIC_ACQUIRE)) {}
}

static void unlock(void)
{
    __atomic_clear(&s_lock, __ATOMIC_RELEASE);
}

static uint32_t home(uint32_t id)
{
    return (id * 2654435761u) >> (32u - TABLE_BITS);
}

static void add(uint64_t *sum, uint64_t v)
{
    if (v) {
        __atomic_fetch_add(sum, v, __ATOMIC_RELAXED);
    }
}

uint32_t rhi_traffic_texel_bytes(RhiFormat f, RhiViewAspect aspect)
{
    if (aspect == RHI_ASPECT_STENCIL) {
        return f == RHI_FMT_D32F_S8 ? 1u : 0u;
    }
    if (aspect == RHI_ASPECT_DEPTH) {
        return f == RHI_FMT_D32F || f == RHI_FMT_D32F_S8 ? 4u : 0u;
    }
    switch (f) {
    case RHI_FMT_R8_UNORM:
    case RHI_FMT_R8_UINT:
        return 1u;
    case RHI_FMT_R16_UINT:
        return 2u;
    case RHI_FMT_RGBA16F:
    case RHI_FMT_RGBA16_UINT:
        return 8u;
    case RHI_FMT_RGBA32_UINT:
        return 16u;
    case RHI_FMT_BC1_UNORM:
    case RHI_FMT_BC2_UNORM:
    case RHI_FMT_BC3_UNORM:
    case RHI_FMT_BC7_UNORM:
        return 0u;
    default:
        return 4u; /* RGBA8, BGRA8, R32_UINT, D32F (its depth), unknown */
    }
}

static uint64_t areaTexels(const RhiRenderPassDesc *p)
{
    uint64_t w = p->width, h = p->height;
    if (p->area.w != 0 && p->area.h != 0) {
        /* clamped to the size, as the backend clamps it (rhi.h RhiRenderPassDesc) */
        const int64_t x = p->area.x > 0 ? p->area.x : 0, y = p->area.y > 0 ? p->area.y : 0;
        const int64_t r = x + (int64_t)p->area.w, b = y + (int64_t)p->area.h;
        const int64_t cw = (r < (int64_t)p->width ? r : (int64_t)p->width) - x;
        const int64_t ch = (b < (int64_t)p->height ? b : (int64_t)p->height) - y;
        if (cw > 0 && ch > 0) {
            w = (uint64_t)cw;
            h = (uint64_t)ch;
        }
    }
    return w * h;
}

void rhi_traffic_pass_sum(const RhiRenderPassDesc *pass, const RhiTrafficFmt *color,
                          RhiTrafficFmt depth, RhiTrafficPass *out)
{
    memset(out, 0, sizeof(*out));
    if (pass == NULL) {
        return;
    }
    const uint64_t px = areaTexels(pass);
    out->pixels = px;
    const uint32_t n =
        pass->colorCount < RHI_MAX_COLOR_TARGETS ? pass->colorCount : RHI_MAX_COLOR_TARGETS;
    for (uint32_t i = 0; i < n; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        if (a->texture.id == 0) {
            continue;
        }
        const RhiTrafficFmt f = color[i];
        const uint64_t s = f.known && f.samples > 1 ? f.samples : 1u;
        const uint64_t bytes =
            px * s *
            (f.known ? rhi_traffic_texel_bytes((RhiFormat)f.format, RHI_ASPECT_COLOR) : 4u);
        out->untracked = out->untracked || !f.known;
        if (a->load == RHI_LOAD_LOAD) {
            out->colorLoad += bytes;
        } else if (a->load == RHI_LOAD_CLEAR) {
            out->clear += bytes;
        }
        if (a->store == RHI_STORE_STORE) {
            out->colorStore += bytes;
        }
    }
    const RhiDepthAttachment *d = &pass->depth;
    if (d->texture.id != 0) {
        const uint64_t s = depth.known && depth.samples > 1 ? depth.samples : 1u;
        const RhiFormat fmt = depth.known ? (RhiFormat)depth.format : RHI_FMT_D32F;
        const uint64_t db = px * s * rhi_traffic_texel_bytes(fmt, RHI_ASPECT_DEPTH);
        const uint64_t sb = px * s * rhi_traffic_texel_bytes(fmt, RHI_ASPECT_STENCIL);
        out->untracked = out->untracked || !depth.known;
        if (d->depthLoad == RHI_LOAD_LOAD) {
            out->depthLoad += db;
        } else if (d->depthLoad == RHI_LOAD_CLEAR) {
            out->clear += db;
        }
        if (d->stencilLoad == RHI_LOAD_LOAD) {
            out->depthLoad += sb;
        } else if (d->stencilLoad == RHI_LOAD_CLEAR) {
            out->clear += sb;
        }
        if (!d->readOnlyDepth && d->store == RHI_STORE_STORE) {
            out->depthStore += db;
        }
        if (d->stencilStore == RHI_STORE_STORE) {
            out->depthStore += sb;
        }
    }
}

bool rhi_traffic_format_of(RhiTexture t, RhiTrafficFmt *out)
{
    memset(out, 0, sizeof(*out));
    if (t.id == 0) {
        return false;
    }
    bool found = false;
    lock();
    for (uint32_t k = 0, i = home(t.id); k < TABLE_SIZE; k++, i = (i + 1u) & (TABLE_SIZE - 1u)) {
        if (s_table[i].id == 0) {
            break;
        }
        if (s_table[i].id == t.id) {
            out->format = s_table[i].format;
            out->samples = s_table[i].samples;
            out->known = true;
            found = true;
            break;
        }
    }
    unlock();
    return found;
}

void rhi_traffic_texture_made(RhiTexture t, const RhiTextureDesc *desc)
{
    if (t.id == 0 || desc == NULL) {
        return;
    }
    lock();
    /* a full table (three quarters: probes stay short) keeps the rest out:
       their passes count as untracked */
    if (s_live < TABLE_SIZE / 4u * 3u) {
        uint32_t i = home(t.id);
        while (s_table[i].id != 0 && s_table[i].id != t.id) {
            i = (i + 1u) & (TABLE_SIZE - 1u);
        }
        if (s_table[i].id == 0) {
            s_live++;
        }
        s_table[i].id = t.id;
        s_table[i].format = (uint8_t)desc->format;
        s_table[i].samples = (uint8_t)(desc->samples > 1u ? desc->samples : 1u);
    }
    unlock();
}

void rhi_traffic_texture_gone(RhiTexture t)
{
    if (t.id == 0) {
        return;
    }
    lock();
    uint32_t i = home(t.id);
    for (uint32_t k = 0; k < TABLE_SIZE && s_table[i].id != 0; k++) {
        if (s_table[i].id == t.id) {
            break;
        }
        i = (i + 1u) & (TABLE_SIZE - 1u);
    }
    if (s_table[i].id == t.id) {
        /* backward shift: each later entry of the run that may live in the
           hole moves into it */
        uint32_t hole = i;
        s_table[hole].id = 0;
        s_live--;
        for (uint32_t j = (hole + 1u) & (TABLE_SIZE - 1u); s_table[j].id != 0;
             j = (j + 1u) & (TABLE_SIZE - 1u)) {
            const uint32_t h = home(s_table[j].id);
            /* j's entry may move to the hole when its home is not in (hole, j] */
            const bool between = hole <= j ? (h > hole && h <= j) : (h > hole || h <= j);
            if (!between) {
                s_table[hole] = s_table[j];
                s_table[j].id = 0;
                hole = j;
            }
        }
    }
    unlock();
}

void rhi_traffic_pass(const RhiRenderPassDesc *pass)
{
    if (pass == NULL) {
        return;
    }
    RhiTrafficFmt color[RHI_MAX_COLOR_TARGETS];
    RhiTrafficFmt depth;
    const uint32_t n =
        pass->colorCount < RHI_MAX_COLOR_TARGETS ? pass->colorCount : RHI_MAX_COLOR_TARGETS;
    for (uint32_t i = 0; i < n; i++) {
        (void)rhi_traffic_format_of(pass->color[i].texture, &color[i]);
    }
    (void)rhi_traffic_format_of(pass->depth.texture, &depth);
    RhiTrafficPass t;
    rhi_traffic_pass_sum(pass, color, depth, &t);
    add(&s_colorLoad, t.colorLoad);
    add(&s_colorStore, t.colorStore);
    add(&s_depthLoad, t.depthLoad);
    add(&s_depthStore, t.depthStore);
    add(&s_clear, t.clear);
    add(&s_pixels, t.pixels);
    add(&s_untracked, t.untracked ? 1u : 0u);
}

void rhi_traffic_copy(RhiTexture src, RhiViewAspect aspect, RhiRect region)
{
    RhiTrafficFmt f;
    const uint32_t b =
        rhi_traffic_format_of(src, &f) ? rhi_traffic_texel_bytes((RhiFormat)f.format, aspect) : 4u;
    add(&s_copy, 2u * (uint64_t)region.w * (uint64_t)region.h * b);
}

void rhi_traffic_forget(void)
{
    lock();
    memset(s_table, 0, sizeof(s_table));
    s_live = 0;
    unlock();
}

void rhi_traffic_stats(RhiStats *out)
{
    out->passColorLoadBytes = __atomic_load_n(&s_colorLoad, __ATOMIC_RELAXED);
    out->passColorStoreBytes = __atomic_load_n(&s_colorStore, __ATOMIC_RELAXED);
    out->passDepthLoadBytes = __atomic_load_n(&s_depthLoad, __ATOMIC_RELAXED);
    out->passDepthStoreBytes = __atomic_load_n(&s_depthStore, __ATOMIC_RELAXED);
    out->passClearBytes = __atomic_load_n(&s_clear, __ATOMIC_RELAXED);
    out->passPixels = __atomic_load_n(&s_pixels, __ATOMIC_RELAXED);
    out->copyTextureBytes = __atomic_load_n(&s_copy, __ATOMIC_RELAXED);
    out->passUntracked = __atomic_load_n(&s_untracked, __ATOMIC_RELAXED);
}
