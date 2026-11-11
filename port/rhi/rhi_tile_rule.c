/* rhi_tile_rule.c: the barrier scopes and the submission wait a start uses
 * (rhi_tile_rule.h; issues 179 and 211).  Pure: no Vulkan, no I/O. */
#include "rhi_tile_rule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -1 auto (unset, "", "auto", or a value not known: *unknown set), 1 the
 * first word, 0 the second */
static int pick(const char *v, const char *const *one, const char *const *zero, bool *unknown)
{
    *unknown = false;
    if (v == NULL || v[0] == '\0' || strcmp(v, "auto") == 0) {
        return -1;
    }
    for (int i = 0; one[i] != NULL; i++) {
        if (strcmp(v, one[i]) == 0) {
            return 1;
        }
    }
    for (int i = 0; zero[i] != NULL; i++) {
        if (strcmp(v, zero[i]) == 0) {
            return 0;
        }
    }
    *unknown = true;
    return -1;
}

void rhi_tile_rule(const char *scope, const char *polled, bool tiler, const char *device,
                   uint32_t lossRow, RhiTileRule *out)
{
    static const char *const wideWords[] = {"wide", NULL};
    static const char *const narrowWords[] = {"narrow", NULL};
    static const char *const onWords[] = {"1", "on", "true", "yes", NULL};
    static const char *const offWords[] = {"0", "off", "false", "no", NULL};
    const char *dev = device != NULL && device[0] != '\0' ? device : "the GPU";
    bool badScope = false, badPolled = false;
    const int s = pick(scope, wideWords, narrowWords, &badScope);
    const int p = pick(polled, onWords, offWords, &badPolled);
    char note[96] = "";
    char lossWords[96] = "";

    memset(out, 0, sizeof(*out));
    if (lossRow == 1u) {
        snprintf(lossWords, sizeof lossWords, "the previous start lost the graphics device");
    } else if (lossRow > 1u) {
        snprintf(lossWords, sizeof lossWords, "%u starts in a row lost the graphics device",
                 (unsigned)lossRow);
    }

    /* the barrier scopes */
    if (badScope) {
        snprintf(note, sizeof note, "; \"%.24s\" is none of auto, wide, narrow: auto", scope);
    }
    if (s == 1) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy, "wide ([dev] barrier_scope = wide)");
        snprintf(out->wideTag, sizeof out->wideTag, "wide (the setting)");
    } else if (s == 0) {
        snprintf(out->wideWhy, sizeof out->wideWhy, "narrow ([dev] barrier_scope = narrow%s%s%s)",
                 tiler || lossRow ? "; auto would give wide here: " : "",
                 tiler ? "a tile-based GPU" : "", !tiler && lossRow ? lossWords : "");
        snprintf(out->wideTag, sizeof out->wideTag, "narrow (the setting)");
    } else if (tiler) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy,
                 "wide (a tile-based GPU, %s: the default on every such GPU; [dev] "
                 "barrier_scope = narrow tries the narrow ones%s%s%s)",
                 dev, lossRow ? "; " : "", lossRow ? lossWords : "", note);
        snprintf(out->wideTag, sizeof out->wideTag, "wide (tile-based)");
    } else if (lossRow) {
        out->wide = true;
        snprintf(out->wideWhy, sizeof out->wideWhy,
                 "wide (%s: this start only, nothing written to the settings; [dev] "
                 "barrier_scope = narrow keeps the narrow ones%s)",
                 lossWords, note);
        snprintf(out->wideTag, sizeof out->wideTag, "wide (a lost start)");
    } else {
        snprintf(out->wideWhy, sizeof out->wideWhy,
                 "narrow (the default where the GPU is not tile-based; [dev] barrier_scope = "
                 "wide gives the wide ones%s)",
                 note);
        snprintf(out->wideTag, sizeof out->wideTag, "narrow (the default)");
    }

    /* the submission wait */
    note[0] = '\0';
    if (badPolled) {
        snprintf(note, sizeof note, "; \"%.24s\" is none of auto, on, off: auto", polled);
    }
    if (p == 1) {
        out->polled = true;
        snprintf(out->polledWhy, sizeof out->polledWhy, "polled ([dev] polled_wait = on%s%s%s)",
                 tiler || lossRow > 1u ? "; auto would give the single wait here: " : "",
                 tiler ? "a tile-based GPU" : "", !tiler && lossRow > 1u ? lossWords : "");
        snprintf(out->polledTag, sizeof out->polledTag, "polled (the setting)");
    } else if (p == 0) {
        snprintf(out->polledWhy, sizeof out->polledWhy, "single ([dev] polled_wait = off)");
        snprintf(out->polledTag, sizeof out->polledTag, "single (the setting)");
    } else if (tiler) {
        snprintf(out->polledWhy, sizeof out->polledWhy,
                 "single (a tile-based GPU, %s: the default on every such GPU; [dev] "
                 "polled_wait = on tries the polled one%s)",
                 dev, note);
        snprintf(out->polledTag, sizeof out->polledTag, "single (tile-based)");
    } else if (lossRow > 1u) {
        snprintf(out->polledWhy, sizeof out->polledWhy,
                 "single (%s: this start only, nothing written to the settings; [dev] "
                 "polled_wait = on keeps the polled one%s)",
                 lossWords, note);
        snprintf(out->polledTag, sizeof out->polledTag, "single (lost starts)");
    } else {
        out->polled = true;
        snprintf(out->polledWhy, sizeof out->polledWhy,
                 "polled (the default where the GPU is not tile-based%s; [dev] polled_wait = off "
                 "gives the single wait%s)",
                 lossRow == 1u ? "; one lost start before this one: a second in a row gives the "
                                 "single wait"
                               : "",
                 note);
        snprintf(out->polledTag, sizeof out->polledTag, "polled (the default)");
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
