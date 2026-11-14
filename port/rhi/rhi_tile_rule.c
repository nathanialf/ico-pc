/* rhi_tile_rule.c: the barrier scopes and the submission wait a start uses
 * (rhi_tile_rule.h; issues 179 and 211).  Pure: no Vulkan, no I/O. */
#include "rhi_tile_rule.h"
#include "rhi_device_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool rhi_tile_phone(bool android, bool ios, bool fakedTileVendor)
{
    /* the device table's TILE_PHONE (rows android and ios) on a Vulkan
       identity of the build, iOS through MoltenVK; the tests' faked vendor
       as its fake gives it (a tile-based vendor's id: Qualcomm's stands for
       any of them) */
    RhiDeviceId id;
    RhiDeviceFakes fk;
    RhiDeviceChoices c;
    memset(&id, 0, sizeof id);
    memset(&fk, 0, sizeof fk);
    id.backend = RHI_DT_VULKAN;
    id.os = android ? RHI_DT_ANDROID : ios ? RHI_DT_IOS : RHI_DT_LINUX;
    id.moltenvk = ios ? 1u : 0u;
    fk.vendorSet = fakedTileVendor ? 1u : 0u;
    fk.vendor = 0x5143u;
    rhi_device_table_eval(&id, &fk, &c);
    return c.value[RHI_DC_TILE_PHONE] != 0;
}

void rhi_tile_rule(const char *fakeScopes, bool phone, const char *device, uint32_t lossRow,
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
    out->phone = phone;
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
    } else if (phone) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy, "wide (a tile-based phone GPU, %s%s%s)", dev,
                 lossRow ? "; " : "", lossRow ? lossWords : "");
        snprintf(out->wideTag, sizeof out->wideTag, "wide (tile-based phone)");
    } else if (lossRow) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy,
                 "wide (%s: this start only, nothing written to the settings)", lossWords);
        snprintf(out->wideTag, sizeof out->wideTag, "wide (a lost start)");
    } else {
        snprintf(out->wideWhy, sizeof out->wideWhy, "narrow (not a tile-based phone GPU)");
        snprintf(out->wideTag, sizeof out->wideTag, "narrow (not a phone)");
    }

    /* the submission wait */
    if (phone) {
        snprintf(out->polledWhy, sizeof out->polledWhy, "single (a tile-based phone GPU, %s)", dev);
        snprintf(out->polledTag, sizeof out->polledTag, "single (tile-based phone)");
    } else if (lossRow > 1u) {
        snprintf(out->polledWhy, sizeof out->polledWhy,
                 "single (%s: this start only, nothing written to the settings)", lossWords);
        snprintf(out->polledTag, sizeof out->polledTag, "single (lost starts)");
    } else {
        out->polled = true;
        snprintf(out->polledWhy, sizeof out->polledWhy, "polled (not a tile-based phone GPU%s)",
                 lossRow == 1u ? "; one lost start before this one: a second in a row gives the "
                                 "single wait"
                               : "");
        snprintf(out->polledTag, sizeof out->polledTag, "polled (not a phone)");
    }

    /* the phones' rung: the scopes and the wait are already v0.11.2's
       there, so a start after a lost one draws without the VU pre-pass (the
       Adreno 740's device-table row's remedy, its VU_PREPASS_OFF) */
    if (phone && lossRow) {
        out->noPrepass = true;
        snprintf(out->prepassWhy, sizeof out->prepassWhy, "%s", lossWords);
    }
}

bool rhi_tile_vendor(uint32_t vendorId)
{
    return rhi_tile_vendor_name(vendorId) != NULL;
}

const char *rhi_tile_vendor_name(uint32_t vendorId)
{
    /* the device table's tile-vendors row decides (TILER on a Vulkan
       identity of the vendor off Android); the names are words */
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.backend = RHI_DT_VULKAN;
    id.os = RHI_DT_LINUX;
    id.vendorId = vendorId;
    if (!rhi_device_table_choice(&id, RHI_DC_TILER, NULL)) {
        return NULL;
    }
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
