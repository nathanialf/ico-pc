/* rhi_vk_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * Vulkan backend, headless, then a hazard-tracking cell.
 *
 * Exit 0 on success, 1 on a mismatch or a validation error, 77 (skipped)
 * when no Vulkan device can be created.  Set VK_ICD_FILENAMES to pick a
 * driver (lavapipe in the container). */
#include "rhi.h"
#include "rhi_test_common.h"
#include "vk/rhi_vk.h"
#include <stdlib.h>

/* The barriers one render pass adds to a list, read from RhiStats. */
static uint64_t passBarriers(RhiCommandList cl, RhiTexture target)
{
    RhiStats a, b;
    rhi_GetStats(&a);
    RhiRenderPassDesc p = {0};
    p.color[0].texture = target;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = p.height = 16;
    rhi_CmdBeginRenderPass(cl, &p);
    rhi_CmdEndRenderPass(cl);
    rhi_GetStats(&b);
    return b.barriers - a.barriers;
}

/* Package PB's tracked path: a second pass on the same target adds one
 * hazard barrier (write after write), a pass on another target none.
 * ICO_VK_GLOBAL_BARRIERS=1 emits a global barrier before every pass, so the
 * exact counts do not apply and only the run itself is checked. */
static int hazardCell(void)
{
    const char *gb = getenv("ICO_VK_GLOBAL_BARRIERS");
    const bool global = gb && gb[0] && gb[0] != '0';
    if (!rhi_CreateBackend("vulkan")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test hazards"};
    if (!rhi_Init(&dd)) {
        return 0;
    }
    int failures = 0;
    RhiTextureDesc td = {16, 16, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET, "hazardA"};
    RhiTexture a = rhi_CreateTexture(&td);
    td.debugName = "hazardB";
    RhiTexture b = rhi_CreateTexture(&td);
    RhiCommandList cl = rhi_BeginCommands();
    if (!a.id || !b.id || !cl.id) {
        rhi_test_Log("FAIL hazard cell: setup\n");
        failures++;
    } else {
        const RhiTextureBarrier to[2] = {{a, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                                         {b, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET}};
        rhi_CmdBarrier(cl, to, 2);
        passBarriers(cl, a);
        const uint64_t again = passBarriers(cl, a);
        const uint64_t other = passBarriers(cl, b);
        rhi_test_Log("hazard cell: second pass on A +%llu, pass on B +%llu%s\n",
                     (unsigned long long)again, (unsigned long long)other,
                     global ? " (ICO_VK_GLOBAL_BARRIERS: not asserted)" : "");
        if (!global && (again != 1 || other != 0)) {
            rhi_test_Log("FAIL hazard cell: want +1 and +0\n");
            failures++;
        }
        rhi_EndCommands(cl);
        rhi_Submit(cl);
    }
    rhi_WaitIdle();
    rhi_DestroyTexture(a);
    rhi_DestroyTexture(b);
    rhi_Shutdown();
    failures += (int)rhi_vk_ValidationErrorCount();
    return failures;
}

int main(void)
{
    const RhiTestConfig cfg = {"vulkan", "rhi_vk_test", true, NULL, rhi_vk_ValidationErrorCount};
    int rc = rhi_test_RunCells(&cfg);
    if (rc != 0) {
        return rc;
    }
    return hazardCell() ? 1 : 0;
}
