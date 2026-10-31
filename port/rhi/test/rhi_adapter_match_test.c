/* rhi_adapter_match_test.c: the [video] adapter rules
 * (port/rhi/rhi_adapter.c):
 * - rhi_adapter_match: an exact name finds its index; the first of two
 *   equal names wins; a name that differs in case, a part of a name, a
 *   longer name, "" and NULL find none; a NULL entry (a device that cannot
 *   run the renderer) never matches; no names find none;
 * - rhi_adapter_list_text: suitable entries with their score, the score
 *   left out at 0 (Direct3D), an unsuitable entry with its reason and its
 *   score when it has one, the reason "?" when it has none; an empty list;
 *   a short buffer is cut and terminated.
 * Exit 0 on success, 1 on a failure. */
#include <string.h>

#include "rhi_adapter.h"
#include "../../test/ico_check.h"

static RhiAdapterInfo entry(const char *name, bool suitable, int score, const char *why)
{
    RhiAdapterInfo a;
    memset(&a, 0, sizeof(a));
    snprintf(a.name, sizeof(a.name), "%s", name);
    a.suitable = suitable;
    a.score = score;
    a.why = why;
    return a;
}

int main(void)
{
    static const char *const kNames[] = {"Intel(R) UHD Graphics 630",
                                         "NVIDIA GeForce RTX 3060 Laptop GPU", NULL,
                                         "NVIDIA GeForce RTX 3060 Laptop GPU"};
    CHECK(rhi_adapter_match("Intel(R) UHD Graphics 630", kNames, 4) == 0);
    CHECK(rhi_adapter_match("NVIDIA GeForce RTX 3060 Laptop GPU", kNames, 4) == 1,
          "the first of two equal names");
    CHECK(rhi_adapter_match("nvidia geforce rtx 3060 laptop gpu", kNames, 4) == -1,
          "the case counts");
    CHECK(rhi_adapter_match("NVIDIA", kNames, 4) == -1, "a part of a name");
    CHECK(rhi_adapter_match("Intel(R) UHD Graphics 630 ", kNames, 4) == -1, "a longer name");
    CHECK(rhi_adapter_match("", kNames, 4) == -1, "\"\" is automatic");
    CHECK(rhi_adapter_match(NULL, kNames, 4) == -1, "NULL is automatic");
    CHECK(rhi_adapter_match("Intel(R) UHD Graphics 630", kNames, 0) == -1, "no names");
    CHECK(rhi_adapter_match("Intel(R) UHD Graphics 630", NULL, 4) == -1, "a NULL list");
    {
        static const char *const kHole[] = {NULL, "llvmpipe"};
        CHECK(rhi_adapter_match("llvmpipe", kHole, 2) == 1, "past a NULL entry");
    }

    RhiAdapterInfo list[4];
    char buf[1024];
    list[0] = entry("NVIDIA GeForce RTX 3060 Laptop GPU", true, 1065, NULL);
    list[1] = entry("Intel(R) UHD Graphics 630", true, 565, NULL);
    list[2] = entry("llvmpipe (LLVM 19.1.7, 256 bits)", false, 165, "a software renderer");
    list[3] = entry("Old GPU", false, -1, "no dynamic rendering");
    CHECK_STR(rhi_adapter_list_text(list, 4, buf, sizeof(buf)),
              "0 NVIDIA GeForce RTX 3060 Laptop GPU (suitable, score 1065) "
              "1 Intel(R) UHD Graphics 630 (suitable, score 565) "
              "2 llvmpipe (LLVM 19.1.7, 256 bits) (unsuitable: a software renderer; score 165) "
              "3 Old GPU (unsuitable: no dynamic rendering)");
    /* Direct3D: no score */
    list[0] = entry("NVIDIA GeForce RTX 3060 Laptop GPU", true, 0, NULL);
    list[1] = entry("Microsoft Basic Render Driver", false, -1, "a software adapter");
    list[2] = entry("X", false, -1, NULL);
    CHECK_STR(rhi_adapter_list_text(list, 3, buf, sizeof(buf)),
              "0 NVIDIA GeForce RTX 3060 Laptop GPU (suitable) "
              "1 Microsoft Basic Render Driver (unsuitable: a software adapter) "
              "2 X (unsuitable: ?)");
    CHECK_STR(rhi_adapter_list_text(list, 0, buf, sizeof(buf)), "");
    {
        char small[8];
        rhi_adapter_list_text(list, 3, small, sizeof(small));
        CHECK_STR(small, "0 NVIDI");
    }
    return ico_check_summary("rhi_adapter_match");
}
