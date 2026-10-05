/* rhi_vk_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * Vulkan backend, headless.
 *
 * Exit 0 on success, 1 on a mismatch or a validation error, 77 (skipped)
 * when no Vulkan device can be created.  Set VK_ICD_FILENAMES to pick a
 * driver (lavapipe in the container). */
#include "rhi_test_common.h"
#include "vk/rhi_vk.h"

int main(void)
{
    const RhiTestConfig cfg = {"vulkan", "rhi_vk_test", true, NULL, rhi_vk_ValidationErrorCount};
    return rhi_test_RunCells(&cfg);
}
