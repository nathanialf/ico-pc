/* vk_range.c: the commands robust access does not cover, checked against
 * the resources they name (issue 123).
 *
 * gpu_robust (robustBufferAccess, VK_EXT_robustness2) bounds what shaders
 * read and the vertex input: "access to vertex input data is bounds checked
 * against the bound vertex buffer range" (Vulkan spec, "Robust Buffer
 * Access").  It says nothing of the index buffer's fetch or of transfer
 * commands, whose ranges are valid-usage rules only (vkCmdDrawIndexed's
 * index range within the bound index buffer, vkCmdCopyBuffer's regions
 * within both buffers, vkCmdCopyBufferToImage's within the image's
 * subresource and the buffer): a driver may page-fault on them.  The
 * Adreno 740 of issue 123 stalls about 340 ms at random with its timestamp
 * counter restarted (a GPU fault and the kernel driver's reset fit); the
 * validation layer found nothing in the port's replays on lavapipe, but the
 * reporter's frames are not those replays.  So every indexed draw and
 * every copy is checked here, on every device, against the sizes the
 * backend knows, and one that reaches past them is logged with the
 * replay and frame of its slot (the first 8, then every 100th) and counted
 * (RhiStats has no room: the count is in the line).  Nothing is changed:
 * the command is recorded as before, so the picture is the same; the line
 * names a command for the next fix. */
#include "vk_internal.h"
#include "vk_enums.h"
#include "rhi_vk.h"
#include <string.h>

static uint32_t s_bad;

static void bad(const VkrCmdList *c, const char *what, uint64_t need, uint64_t have)
{
    const uint32_t n = ++s_bad;
    if (n > 8u && n % 100u != 0u) {
        return;
    }
    const VkrFrame *f = c ? &g_vkr.frames[c->slot % RHI_FRAMES_IN_FLIGHT] : NULL;
    VKR_LOG("a command past its resource: %s reaches %llu of %llu (replay %u, frame %u; %u such "
            "commands this run): robust access does not cover it, a GPU may fault on it",
            what, (unsigned long long)need, (unsigned long long)have, f ? f->tagReplay : 0u,
            f ? f->tagFrame : 0u, n);
}

uint32_t vkr_range_bad_count(void)
{
    return s_bad;
}

uint32_t vkr_test_range_bad(void)
{
    return s_bad;
}

void vkr_range_index(const VkrCmdList *c, uint32_t firstIndex, uint32_t indexCount)
{
    if (!(c->known & 4u) || indexCount == 0) {
        return; /* (VKR_KNOW_INDEX, vk_cmd.c: no index buffer this list knows) */
    }
    const uint64_t need = ((uint64_t)firstIndex + (uint64_t)indexCount) * (c->ib32 ? 4u : 2u);
    if (need > c->ibBytes) {
        bad(c, "an indexed draw's indices", need, c->ibBytes);
    }
}

void vkr_range_buffer(const VkrCmdList *c, const char *what, const VkrBuffer *b, uint64_t offset,
                      uint64_t size)
{
    if (offset > b->size || size > b->size - offset) {
        bad(c, what, size > UINT64_MAX - offset ? UINT64_MAX : offset + size, b->size);
    }
}

/* the bytes a copy of w x h texels with rows rowPitch bytes apart spans in a
   buffer, from its offset (the last row only its own texels) */
static uint64_t rowsBytes(const VkrTexture *t, uint32_t rowPitch, uint32_t w, uint32_t h)
{
    const uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    const uint32_t block = rhi_format_is_block(t->rhiFormat) ? 4u : 1u;
    const uint64_t rows = (h + block - 1u) / block;
    const uint64_t last = (uint64_t)((w + block - 1u) / block) * texel;
    return rows ? (rows - 1u) * rowPitch + last : 0u;
}

void vkr_range_image(const VkrCmdList *c, const char *what, const VkrTexture *t, uint32_t mip,
                     RhiRect r)
{
    const uint32_t mw = (t->width >> mip) ? (t->width >> mip) : 1u;
    const uint32_t mh = (t->height >> mip) ? (t->height >> mip) : 1u;
    if (r.x < 0 || r.y < 0 || mip >= (t->mips ? t->mips : 1u)) {
        bad(c, what, 0, 0);
        return;
    }
    if ((uint64_t)(uint32_t)r.x + r.w > mw) {
        bad(c, what, (uint64_t)(uint32_t)r.x + r.w, mw); /* columns */
    } else if ((uint64_t)(uint32_t)r.y + r.h > mh) {
        bad(c, what, (uint64_t)(uint32_t)r.y + r.h, mh); /* rows */
    }
}

void vkr_range_rows(const VkrCmdList *c, const char *what, const VkrBuffer *b, uint64_t offset,
                    uint32_t rowPitch, const VkrTexture *t, RhiRect r)
{
    vkr_range_buffer(c, what, b, offset, rowsBytes(t, rowPitch, r.w, r.h));
}

/* rhi_vk.h vkr_test_range_cases: each check on made-up resources (no
 * device), at the edge and one past it; the cases whose verdict (a line
 * counted or not) is not the one wanted */
uint32_t vkr_test_range_cases(void)
{
    uint32_t wrong = 0;
    VkrCmdList c;
    VkrBuffer b;
    VkrTexture t;
    memset(&c, 0, sizeof c);
    memset(&b, 0, sizeof b);
    memset(&t, 0, sizeof t);
#define CASE(call, wantBad)                                                                        \
    do {                                                                                           \
        const uint32_t n0 = s_bad;                                                                 \
        call;                                                                                      \
        if ((s_bad != n0) != (wantBad)) {                                                          \
            VKR_LOG("range case %s: %s (want %s)", #call, s_bad != n0 ? "past" : "inside",         \
                    (wantBad) ? "past" : "inside");                                                \
            wrong++;                                                                               \
        }                                                                                          \
    } while (0)
    /* indices: 64 bytes from the bound offset, 32-bit then 16-bit */
    CASE(vkr_range_index(&c, 0, 1000), false); /* no index buffer known */
    c.known = 4u;
    c.ib32 = true;
    c.ibBytes = 64;
    CASE(vkr_range_index(&c, 4, 12), false);
    CASE(vkr_range_index(&c, 4, 13), true);
    CASE(vkr_range_index(&c, 0xFFFFFFFFu, 2), true); /* no wrap */
    c.ib32 = false;
    CASE(vkr_range_index(&c, 16, 16), false);
    CASE(vkr_range_index(&c, 16, 17), true);
    /* a buffer of 256 bytes */
    b.size = 256;
    CASE(vkr_range_buffer(&c, "case", &b, 192, 64), false);
    CASE(vkr_range_buffer(&c, "case", &b, 193, 64), true);
    CASE(vkr_range_buffer(&c, "case", &b, 300, 0), true);
    CASE(vkr_range_buffer(&c, "case", &b, 8, ~0ull), true); /* no wrap */
    /* an 8 x 8 RGBA8 texture with 2 mips: mip 1 is 4 x 4 */
    t.rhiFormat = RHI_FMT_RGBA8_UNORM;
    t.width = t.height = 8;
    t.mips = 2;
    CASE(vkr_range_image(&c, "case", &t, 1, (RhiRect){0, 0, 4, 4}), false);
    CASE(vkr_range_image(&c, "case", &t, 1, (RhiRect){1, 0, 4, 4}), true);
    CASE(vkr_range_image(&c, "case", &t, 0, (RhiRect){0, 4, 8, 5}), true);
    CASE(vkr_range_image(&c, "case", &t, 0, (RhiRect){-1, 0, 2, 2}), true);
    CASE(vkr_range_image(&c, "case", &t, 2, (RhiRect){0, 0, 1, 1}), true); /* no mip 2 */
    /* rows: 4 x 4 texels, rows 64 bytes apart: 3 * 64 + 16 = 208 bytes */
    b.size = 208;
    CASE(vkr_range_rows(&c, "case", &b, 0, 64, &t, (RhiRect){0, 0, 4, 4}), false);
    CASE(vkr_range_rows(&c, "case", &b, 1, 64, &t, (RhiRect){0, 0, 4, 4}), true);
    /* BC1: 8 bytes a 4 x 4 block; 8 x 6 texels are 2 x 2 blocks, rows of
       blocks 16 bytes apart: 16 + 16 = 32 bytes */
    t.rhiFormat = RHI_FMT_BC1_UNORM;
    b.size = 32;
    CASE(vkr_range_rows(&c, "case", &b, 0, 16, &t, (RhiRect){0, 0, 8, 6}), false);
    CASE(vkr_range_rows(&c, "case", &b, 0, 16, &t, (RhiRect){0, 0, 8, 9}), true);
#undef CASE
    return wrong;
}
