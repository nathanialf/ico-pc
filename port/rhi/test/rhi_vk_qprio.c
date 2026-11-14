/* rhi_vk_qprio.c: rhi_vk_test's `qprio` cell, the game's queue at high
 * global priority (port/rhi/vk/vk_qprio.c, port/rhi/rhi_qprio.h; issue
 * 123's second attempt), on whatever Vulkan device is present (lavapipe in
 * the container).
 *
 * First without the fake (ICO_VK_FAKE_QUEUE_PRIORITY cleared): a device
 * off the row (no test build is Android) is not asked, RhiLimits says
 * RHI_QPRIO_NOT_ASKED with the reason "not Qualcomm's own driver on
 * Android"; no device: 77.  Then with ICO_VK_FAKE_QUEUE_PRIORITY set to
 * ICO_TEST_QPRIO's word (the ctest's environment): every exact-texel cell
 * (rhi_test_run_cells: the device made through vk_qprio.c, drawing), then
 * one more start whose RhiLimits must say:
 *   grant      RHI_QPRIO_GRANTED, the reason "high granted through ...";
 *   refuse     RHI_QPRIO_REFUSED, queuePriorityResult -1000174001
 *              (VK_ERROR_NOT_PERMITTED), the reason "... not permitted ...":
 *              the device made again without the priority, and drawing;
 *   unoffered  RHI_QPRIO_NOT_ASKED, the reason "... not offered".
 * grant and refuse need the device to offer a global priority extension
 * with high in the queue family's list (lavapipe lists VK_KHR_global_priority
 * with low, medium, high, realtime): where the start says it is not
 * offered or high is not listed, 77.  Exit 0, 1 on a failure, 77 skipped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rhi.h"
#include "rhi_qprio.h"
#include "rhi_test_common.h"

static void qpSetEnv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/* the start's RhiLimits fields, the device brought up and down; false
   without a device */
static bool qpStart(int *state, int32_t *result, const char **why)
{
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test qprio", NULL};
    if (!rhi_create_backend("vulkan") || !rhi_init(&dd)) {
        return false;
    }
    const RhiLimits *l = rhi_limits();
    *state = l->queuePriority;
    *result = l->queuePriorityResult;
    *why = l->queuePriorityWhy; /* a static phrase: kept past the shutdown */
    rhi_shutdown();
    return true;
}

static bool has(const char *why, const char *part)
{
    return why != NULL && strstr(why, part) != NULL;
}

int rhi_vk_qprio_cell(const RhiTestConfig *cfg)
{
    const char *word = getenv("ICO_TEST_QPRIO");
    const int fake = rhi_vk_qprio_fake(word);
    int fails = 0;
    int state = -1;
    int32_t result = 0;
    const char *why = NULL;
    qpSetEnv("ICO_VK_FAKE_QUEUE_PRIORITY", NULL);
    if (!qpStart(&state, &result, &why)) {
        rhi_test_log("SKIP qprio: no Vulkan device\n");
        return 77;
    }
    rhi_test_log("qprio: without the fake: state %d, why %s\n", state, why ? why : "(none)");
    if (state != RHI_QPRIO_NOT_ASKED || !has(why, "not Qualcomm's own driver on Android")) {
        rhi_test_log("FAIL qprio: a device off the row asked or said another reason\n");
        fails++;
    }
    if (fake == RHI_QPRIO_FAKE_NONE) {
        rhi_test_log("FAIL qprio: ICO_TEST_QPRIO is not grant, refuse or unoffered\n");
        return 1;
    }
    qpSetEnv("ICO_VK_FAKE_QUEUE_PRIORITY", word);
    /* the device made through the priority's path draws texel for texel */
    const int cells = rhi_test_run_cells(cfg);
    if (cells == 1) {
        rhi_test_log("FAIL qprio: the cells failed under ICO_VK_FAKE_QUEUE_PRIORITY=%s\n", word);
        fails++;
    }
    if (!qpStart(&state, &result, &why)) {
        rhi_test_log("FAIL qprio: no device under ICO_VK_FAKE_QUEUE_PRIORITY=%s\n", word);
        qpSetEnv("ICO_VK_FAKE_QUEUE_PRIORITY", NULL);
        return 1;
    }
    qpSetEnv("ICO_VK_FAKE_QUEUE_PRIORITY", NULL);
    rhi_test_log("qprio: %s: state %d, result %d, why %s\n", word, state, (int)result,
                 why ? why : "(none)");
    if (fake != RHI_QPRIO_FAKE_UNOFFERED && state == RHI_QPRIO_NOT_ASKED &&
        (has(why, "not offered") || has(why, "does not list high"))) {
        rhi_test_log("SKIP qprio: the device offers no global priority with high\n");
        return fails ? 1 : 77;
    }
    if (fake == RHI_QPRIO_FAKE_GRANT &&
        (state != RHI_QPRIO_GRANTED || result != 0 || !has(why, "high granted through"))) {
        rhi_test_log("FAIL qprio: grant: not granted\n");
        fails++;
    }
    if (fake == RHI_QPRIO_FAKE_REFUSE &&
        (state != RHI_QPRIO_REFUSED || result != -1000174001 || !has(why, "not permitted"))) {
        rhi_test_log("FAIL qprio: refuse: not the refusal and its fallback\n");
        fails++;
    }
    if (fake == RHI_QPRIO_FAKE_UNOFFERED &&
        (state != RHI_QPRIO_NOT_ASKED || !has(why, "global queue priority not offered"))) {
        rhi_test_log("FAIL qprio: unoffered: asked, or another reason\n");
        fails++;
    }
    return fails ? 1 : 0;
}
