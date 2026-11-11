/* rhi_tile_rule.c: the barrier scopes and the submission wait a start uses
 * (rhi_tile_rule.h; issues 179 and 211).  Pure: no Vulkan, no I/O. */
#include "rhi_tile_rule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rhi_tile_rule(const char *fakeScopes, bool tiler, const char *device, uint32_t lossRow,
                   RhiTileRule *out)
{
    const char *dev = device != NULL && device[0] != '\0' ? device : "the GPU";
    /* the tests' override of the scopes alone (ICO_VK_FAKE_SCOPES) */
    const int fake = fakeScopes == NULL                  ? -1
                     : strcmp(fakeScopes, "wide") == 0   ? 1
                     : strcmp(fakeScopes, "narrow") == 0 ? 0
                                                         : -1;
    char lossWords[96] = "";

    memset(out, 0, sizeof(*out));
    if (lossRow == 1u) {
        snprintf(lossWords, sizeof lossWords, "the previous start lost the graphics device");
    } else if (lossRow > 1u) {
        snprintf(lossWords, sizeof lossWords, "%u starts in a row lost the graphics device",
                 (unsigned)lossRow);
    }

    /* the barrier scopes */
    if (fake == 1) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy, "wide (ICO_VK_FAKE_SCOPES=wide, a test)");
        snprintf(out->wideTag, sizeof out->wideTag, "wide (faked)");
    } else if (fake == 0) {
        snprintf(out->wideWhy, sizeof out->wideWhy, "narrow (ICO_VK_FAKE_SCOPES=narrow, a test)");
        snprintf(out->wideTag, sizeof out->wideTag, "narrow (faked)");
    } else if (tiler) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy, "wide (a tile-based GPU, %s%s%s)", dev,
                 lossRow ? "; " : "", lossRow ? lossWords : "");
        snprintf(out->wideTag, sizeof out->wideTag, "wide (tile-based)");
    } else if (lossRow) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy,
                 "wide (%s: this start only, nothing written to the settings)", lossWords);
        snprintf(out->wideTag, sizeof out->wideTag, "wide (a lost start)");
    } else {
        snprintf(out->wideWhy, sizeof out->wideWhy, "narrow (a GPU that is not tile-based)");
        snprintf(out->wideTag, sizeof out->wideTag, "narrow (not tile-based)");
    }

    /* the submission wait */
    if (tiler) {
        snprintf(out->polledWhy, sizeof out->polledWhy, "single (a tile-based GPU, %s)", dev);
        snprintf(out->polledTag, sizeof out->polledTag, "single (tile-based)");
    } else if (lossRow > 1u) {
        snprintf(out->polledWhy, sizeof out->polledWhy,
                 "single (%s: this start only, nothing written to the settings)", lossWords);
        snprintf(out->polledTag, sizeof out->polledTag, "single (lost starts)");
    } else {
        out->polled = true;
        snprintf(out->polledWhy, sizeof out->polledWhy, "polled (a GPU that is not tile-based%s)",
                 lossRow == 1u ? "; one lost start before this one: a second in a row gives the "
                                 "single wait"
                               : "");
        snprintf(out->polledTag, sizeof out->polledTag, "polled (not tile-based)");
    }
}

bool rhi_tile_vendor(uint32_t vendorId)
{
    return rhi_tile_vendor_name(vendorId) != NULL;
}

const char *rhi_tile_vendor_name(uint32_t vendorId)
{
    switch (vendorId) {
    case 0x5143:
        return "Qualcomm";
    case 0x13B5:
        return "Arm";
    case 0x1010:
        return "Imagination";
    case 0x144D:
        return "Samsung";
    case 0x106B:
        return "Apple";
    case 0x14E4:
        return "Broadcom";
    default:
        return NULL;
    }
}

uint32_t rhi_tile_loss_row(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    char *end = NULL;
    const unsigned long n = strtoul(value, &end, 10);
    if (end == value || *end != '\0') {
        return 0;
    }
    return n > 9ul ? 9u : (uint32_t)n;
}
