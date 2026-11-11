/* rhi_traffic_test.c: rhi_traffic.h's counts (pure: no device).
 *
 *   - the bytes a texel by format and aspect;
 *   - a scene pass at the Mali's 912x512: colour loaded and stored, the
 *     depth-stencil loaded and stored (4 + 1 bytes), the same with a clear,
 *     a read-only depth (never stored), a render area (clamped to the size),
 *     MSAA 4x, an attachment the table does not know (4 bytes, untracked);
 *   - the handle table: made, found, gone, a run of colliding handles kept
 *     reachable across a deletion (the backward shift), forget;
 *   - the sums through rhi_traffic_pass / rhi_traffic_copy into RhiStats.
 * Exit 0 on success, 1 on any mismatch. */
#include "rhi_traffic.h"

#include <stdio.h>
#include <string.h>

static int s_fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            s_fails++;                                                                             \
        }                                                                                          \
    } while (0)

static RhiRenderPassDesc scenePass(void)
{
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.colorCount = 1;
    p.color[0].texture.id = 11;
    p.color[0].load = RHI_LOAD_LOAD;
    p.color[0].store = RHI_STORE_STORE;
    p.depth.texture.id = 12;
    p.depth.depthLoad = RHI_LOAD_LOAD;
    p.depth.stencilLoad = RHI_LOAD_LOAD;
    p.depth.store = RHI_STORE_STORE;
    p.depth.stencilStore = RHI_STORE_STORE;
    p.width = 912;
    p.height = 512;
    return p;
}

static void testBytes(void)
{
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_RGBA8_UNORM, RHI_ASPECT_COLOR) == 4);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_BGRA8_UNORM, RHI_ASPECT_COLOR) == 4);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_R8_UNORM, RHI_ASPECT_COLOR) == 1);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_RGBA16F, RHI_ASPECT_COLOR) == 8);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_RGBA32_UINT, RHI_ASPECT_COLOR) == 16);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_RGBA16_UINT, RHI_ASPECT_COLOR) == 8);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_BC7_UNORM, RHI_ASPECT_COLOR) == 0);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_D32F_S8, RHI_ASPECT_DEPTH) == 4);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_D32F_S8, RHI_ASPECT_STENCIL) == 1);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_D32F, RHI_ASPECT_STENCIL) == 0);
    CHECK(rhi_traffic_texel_bytes(RHI_FMT_RGBA8_UNORM, RHI_ASPECT_DEPTH) == 0);
}

static void testPass(void)
{
    const RhiTrafficFmt c = {RHI_FMT_RGBA8_UNORM, 1, true};
    const RhiTrafficFmt d = {RHI_FMT_D32F_S8, 1, true};
    const uint64_t px = 912u * 512u;
    RhiRenderPassDesc p = scenePass();
    RhiTrafficPass t;

    rhi_traffic_pass_sum(&p, &c, d, &t);
    CHECK(t.pixels == px && !t.untracked);
    CHECK(t.colorLoad == px * 4 && t.colorStore == px * 4);
    CHECK(t.depthLoad == px * 5 && t.depthStore == px * 5 && t.clear == 0);

    /* cleared, the depth not stored, the stencil kept */
    p.color[0].load = RHI_LOAD_CLEAR;
    p.depth.depthLoad = RHI_LOAD_CLEAR;
    p.depth.stencilLoad = RHI_LOAD_DONT_CARE;
    p.depth.store = RHI_STORE_DONT_CARE;
    rhi_traffic_pass_sum(&p, &c, d, &t);
    CHECK(t.colorLoad == 0 && t.depthLoad == 0 && t.clear == px * 8);
    CHECK(t.colorStore == px * 4 && t.depthStore == px * 1);

    /* a read-only depth is never stored */
    p = scenePass();
    p.depth.readOnlyDepth = true;
    p.depth.stencilStore = RHI_STORE_DONT_CARE;
    rhi_traffic_pass_sum(&p, &c, d, &t);
    CHECK(t.depthStore == 0 && t.depthLoad == px * 5);

    /* a render area, the part past the size cut off */
    p = scenePass();
    p.area.x = 900;
    p.area.y = 500;
    p.area.w = 64;
    p.area.h = 64;
    rhi_traffic_pass_sum(&p, &c, d, &t);
    CHECK(t.pixels == 12u * 12u && t.colorLoad == 12u * 12u * 4u);

    /* MSAA 4x: four samples a texel */
    const RhiTrafficFmt c4 = {RHI_FMT_RGBA8_UNORM, 4, true};
    const RhiTrafficFmt d4 = {RHI_FMT_D32F_S8, 4, true};
    p = scenePass();
    rhi_traffic_pass_sum(&p, &c4, d4, &t);
    CHECK(t.colorLoad == px * 16 && t.depthStore == px * 20 && t.pixels == px);

    /* not in the table: 4 bytes a texel, the depth as D32F (no stencil) */
    const RhiTrafficFmt none = {0, 0, false};
    rhi_traffic_pass_sum(&p, &none, none, &t);
    CHECK(t.untracked && t.colorLoad == px * 4 && t.depthLoad == px * 4);

    /* no attachments at all: the area alone */
    memset(&p, 0, sizeof(p));
    p.width = 10;
    p.height = 10;
    rhi_traffic_pass_sum(&p, &c, d, &t);
    CHECK(t.pixels == 100 && t.colorLoad == 0 && !t.untracked);
}

static void testTable(void)
{
    RhiTextureDesc td;
    RhiTrafficFmt f;

    memset(&td, 0, sizeof(td));
    td.width = 912;
    td.height = 512;
    td.format = RHI_FMT_RGBA16F;
    td.samples = 0;
    rhi_traffic_texture_made((RhiTexture){5}, &td);
    CHECK(rhi_traffic_format_of((RhiTexture){5}, &f) && f.format == RHI_FMT_RGBA16F &&
          f.samples == 1);
    CHECK(!rhi_traffic_format_of((RhiTexture){6}, &f) && !f.known);

    /* many handles: some share a home; delete every third, the rest found */
    td.format = RHI_FMT_R8_UNORM;
    for (uint32_t id = 100; id < 3100; id++) {
        rhi_traffic_texture_made((RhiTexture){id}, &td);
    }
    for (uint32_t id = 100; id < 3100; id += 3) {
        rhi_traffic_texture_gone((RhiTexture){id});
    }
    int lost = 0, kept = 0;
    for (uint32_t id = 100; id < 3100; id++) {
        const bool in = rhi_traffic_format_of((RhiTexture){id}, &f);
        if ((id - 100) % 3 == 0) {
            kept += in;
        } else {
            lost += !in || f.format != RHI_FMT_R8_UNORM;
        }
    }
    CHECK(lost == 0 && kept == 0);
    rhi_traffic_forget();
    CHECK(!rhi_traffic_format_of((RhiTexture){5}, &f));
    CHECK(!rhi_traffic_format_of((RhiTexture){101}, &f));
}

static void testSums(void)
{
    RhiTextureDesc td;
    RhiStats a, b;

    memset(&td, 0, sizeof(td));
    td.format = RHI_FMT_RGBA8_UNORM;
    rhi_traffic_texture_made((RhiTexture){11}, &td);
    td.format = RHI_FMT_D32F_S8;
    rhi_traffic_texture_made((RhiTexture){12}, &td);
    memset(&a, 0, sizeof(a));
    rhi_traffic_stats(&a);
    const RhiRenderPassDesc p = scenePass();
    rhi_traffic_pass(&p);
    rhi_traffic_copy((RhiTexture){11}, RHI_ASPECT_COLOR, (RhiRect){0, 0, 100, 10});
    rhi_traffic_copy((RhiTexture){12}, RHI_ASPECT_STENCIL, (RhiRect){0, 0, 100, 10});
    memset(&b, 0, sizeof(b));
    rhi_traffic_stats(&b);
    const uint64_t px = 912u * 512u;
    CHECK(b.passColorLoadBytes - a.passColorLoadBytes == px * 4);
    CHECK(b.passColorStoreBytes - a.passColorStoreBytes == px * 4);
    CHECK(b.passDepthLoadBytes - a.passDepthLoadBytes == px * 5);
    CHECK(b.passDepthStoreBytes - a.passDepthStoreBytes == px * 5);
    CHECK(b.passPixels - a.passPixels == px && b.passUntracked == a.passUntracked);
    /* each copy once: its texels times the aspect's bytes */
    CHECK(b.copyTextureBytes - a.copyTextureBytes == 1000u * 4u + 1000u * 1u);
    /* the window's image (never made through the table): untracked */
    RhiRenderPassDesc w;
    memset(&w, 0, sizeof(w));
    w.colorCount = 1;
    w.color[0].texture.id = 999;
    w.color[0].load = RHI_LOAD_DONT_CARE;
    w.width = 1612;
    w.height = 720;
    rhi_traffic_pass(&w);
    rhi_traffic_stats(&a);
    CHECK(a.passUntracked == b.passUntracked + 1 &&
          a.passColorStoreBytes - b.passColorStoreBytes == 1612u * 720u * 4u);
}

int main(void)
{
    testBytes();
    testPass();
    testTable();
    testSums();
    printf("rhi_traffic_test: %s\n", s_fails ? "FAILED" : "ok");
    return s_fails ? 1 : 0;
}
