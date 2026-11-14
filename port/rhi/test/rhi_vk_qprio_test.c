/* rhi_vk_qprio_test.c: the queue's global priority (port/rhi/rhi_qprio.c and
 * rhi_pass_row.c rhi_vk_qprio_row; issue 123's second attempt), pure.  The
 * row: Qualcomm's own driver (its id 8, or none) on vendor 0x5143 on an
 * Android build, the devices of the VkRenderPass row; Turnip, other ids and
 * vendors, and any non-Android build are not on it.  The plan: off the row
 * not asked; on it KHR preferred over EXT, EXT alone taken, neither listed
 * not asked, a read list without high not asked, an unread list asked; the
 * reasons' words.  The fake's words, the results' words and the ledger's
 * reasons.  Exit 0 on success, 1 on a failure. */
#include "rhi_driver_version.h"
#include "rhi_pass_row.h"
#include "rhi_qprio.h"
#include "../../test/ico_check.h"

int main(void)
{
    /* the row: the 123 tablet's driver (id 8), and with no id, on Android */
    const char *row = rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY, 1);
    CHECK(row != NULL && strstr(row, "Qualcomm's own driver on Android") != NULL &&
          strstr(row, "123") != NULL);
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, 0, 1) != NULL);
    /* the same devices as the VkRenderPass row, both ways */
    CHECK((rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, 0, 1) != NULL) ==
          (rhi_vk_pass_row_qualcomm(RHI_VENDOR_QUALCOMM, 0, 1) != NULL));
    /* not Android (Windows on Snapdragon, desktop): not asked */
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY, 0) == NULL);
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, 0, 0) == NULL);
    /* Turnip on the same GPU, other ids and vendors */
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, RHI_DRIVER_ID_MESA_TURNIP, 1) == NULL);
    CHECK(rhi_vk_qprio_row(0x13B5u, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY, 1) == NULL);
    CHECK(rhi_vk_qprio_row(0x13B5u, 0, 1) == NULL);
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_INTEL, 0, 1) == NULL);
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_IMGTEC, 0, 1) == NULL);
    CHECK(rhi_vk_qprio_row(RHI_VENDOR_NVIDIA, RHI_DRIVER_ID_NVIDIA_PROPRIETARY, 1) == NULL);

    /* the plan */
    const char *ext = "x";
    const char *why = "x";
    /* off the row: whatever is offered, not asked */
    CHECK(rhi_vk_qprio_plan(0, 1, 1, 1, 1, &ext, &why) == 0);
    CHECK(ext == NULL);
    CHECK_STR(why, "not Qualcomm's own driver on Android");
    /* the tablet's offer (v0.11.6 log :80): KHR and EXT, the query, family
       0 lists low, medium, high: asked through KHR */
    CHECK(rhi_vk_qprio_plan(1, 1, 1, 1, 1, &ext, &why) == 1);
    CHECK_STR(ext, "VK_KHR_global_priority");
    CHECK(why == NULL);
    /* EXT alone (an older driver): through EXT */
    CHECK(rhi_vk_qprio_plan(1, 0, 1, 1, 1, &ext, &why) == 1);
    CHECK_STR(ext, "VK_EXT_global_priority");
    /* EXT alone without the query: the list not read, asked (a refusal
       falls back) */
    CHECK(rhi_vk_qprio_plan(1, 0, 1, 0, 0, &ext, &why) == 1);
    CHECK_STR(ext, "VK_EXT_global_priority");
    /* neither listed */
    CHECK(rhi_vk_qprio_plan(1, 0, 0, 0, 0, &ext, &why) == 0);
    CHECK(ext == NULL);
    CHECK_STR(why, "Qualcomm's own driver on Android, global queue priority not offered");
    /* the family's list read without high */
    CHECK(rhi_vk_qprio_plan(1, 1, 0, 1, 0, &ext, &why) == 0);
    CHECK(ext == NULL);
    CHECK_STR(why, "Qualcomm's own driver on Android, the game's queue family does not list high");

    /* the fake's words */
    CHECK(rhi_vk_qprio_fake(NULL) == RHI_QPRIO_FAKE_NONE);
    CHECK(rhi_vk_qprio_fake("") == RHI_QPRIO_FAKE_NONE);
    CHECK(rhi_vk_qprio_fake("1") == RHI_QPRIO_FAKE_NONE);
    CHECK(rhi_vk_qprio_fake("granted") == RHI_QPRIO_FAKE_NONE);
    CHECK(rhi_vk_qprio_fake("grant") == RHI_QPRIO_FAKE_GRANT);
    CHECK(rhi_vk_qprio_fake("GRANT") == RHI_QPRIO_FAKE_GRANT);
    CHECK(rhi_vk_qprio_fake("refuse") == RHI_QPRIO_FAKE_REFUSE);
    CHECK(rhi_vk_qprio_fake("unoffered") == RHI_QPRIO_FAKE_UNOFFERED);
    CHECK_STR(rhi_vk_qprio_fake_word(RHI_QPRIO_FAKE_GRANT), "grant");
    CHECK_STR(rhi_vk_qprio_fake_word(RHI_QPRIO_FAKE_REFUSE), "refuse");
    CHECK_STR(rhi_vk_qprio_fake_word(RHI_QPRIO_FAKE_UNOFFERED), "unoffered");
    CHECK_STR(rhi_vk_qprio_fake_word(RHI_QPRIO_FAKE_NONE), "");
    /* each word read back is its own value */
    for (int f = RHI_QPRIO_FAKE_GRANT; f <= RHI_QPRIO_FAKE_UNOFFERED; f++) {
        CHECK(rhi_vk_qprio_fake(rhi_vk_qprio_fake_word(f)) == f);
    }

    /* the results' words: VK_ERROR_NOT_PERMITTED is -1000174001
       (vulkan_core.h) */
    CHECK_STR(rhi_vk_qprio_result_word(-1000174001), "not permitted");
    CHECK_STR(rhi_vk_qprio_result_word(-3), "initialization failed");
    CHECK_STR(rhi_vk_qprio_result_word(-1), "out of host memory");
    CHECK_STR(rhi_vk_qprio_result_word(-4), "device lost");
    CHECK_STR(rhi_vk_qprio_result_word(-13), "another error");

    /* the ledger's reasons */
    CHECK(strstr(rhi_vk_qprio_granted_why(1), "high granted through VK_KHR_global_priority") ==
          rhi_vk_qprio_granted_why(1));
    CHECK(strstr(rhi_vk_qprio_granted_why(0), "VK_EXT_global_priority") != NULL);
    CHECK(strstr(rhi_vk_qprio_refused_why(-1000174001), "not permitted") != NULL);
    CHECK(strstr(rhi_vk_qprio_refused_why(-3), "initialization failed") != NULL);
    CHECK(strstr(rhi_vk_qprio_refused_why(-2), "failed with it") != NULL);
    CHECK(strstr(rhi_vk_qprio_refused_why(-1000174001), "the driver's default (medium)") != NULL);
    return ico_check_summary("rhi_vk_qprio");
}
