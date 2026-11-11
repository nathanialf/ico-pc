/* rhi_vk_tile_rule_test.c: rhi_tile_rule.h's rule (pure: no device).
 *
 *   - auto (unset, "", "auto"): a tile-based GPU runs v0.11.2's wide scopes
 *     and the single wait, any other GPU v0.11.3's narrow scopes and the
 *     polled wait; a value not known is auto and the reason says so;
 *   - the loss ladder on a GPU that is not tile-based: one lost start in a
 *     row before this one gives the wide scopes (the wait stays polled, the
 *     reason says a second gives the single wait), two or more the single
 *     wait too; nothing about the settings is written (the reasons say so);
 *   - a setting other than auto is never overridden, and its reason names
 *     what auto would have chosen (a tile-based GPU, the lost starts);
 *   - the polled wait's words: 1, on, true, yes and 0, off, false, no;
 *   - the vendor table (Arm, Qualcomm, Imagination, Samsung, Apple,
 *     Broadcom; not lavapipe, AMD, NVIDIA, Intel) and ICO_LOSS_ROW's parse.
 * Exit 0 on success, 1 on any mismatch. */
#include "rhi_tile_rule.h"

#include <stdio.h>
#include <string.h>

static int s_fails;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: ", __LINE__);                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fails++;                                                                             \
        }                                                                                          \
    } while (0)

static RhiTileRule rule(const char *scope, const char *polled, bool tiler, uint32_t row)
{
    RhiTileRule r;
    rhi_tile_rule(scope, polled, tiler, "Arm, vendor 0x13B5", row, &r);
    printf("scope %s polled %s tiler %d row %u: barriers %s | wait %s | %s, %s\n",
           scope ? scope : "(unset)", polled ? polled : "(unset)", (int)tiler, (unsigned)row,
           r.wideWhy, r.polledWhy, r.wideTag, r.polledTag);
    return r;
}

int main(void)
{
    RhiTileRule r;

    /* auto on a tile-based GPU: v0.11.2's both */
    r = rule(NULL, NULL, true, 0);
    CHECK(r.wide && !r.polled, "tile-based auto: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strstr(r.wideWhy, "wide (a tile-based GPU, Arm, vendor 0x13B5:") == r.wideWhy,
          "tile-based scope reason: %s", r.wideWhy);
    CHECK(strstr(r.polledWhy, "single (a tile-based GPU, Arm, vendor 0x13B5:") == r.polledWhy,
          "tile-based wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.wideTag, "wide (tile-based)") == 0 &&
              strcmp(r.polledTag, "single (tile-based)") == 0,
          "tile-based tags: %s, %s", r.wideTag, r.polledTag);
    r = rule("auto", "auto", true, 0);
    CHECK(r.wide && !r.polled, "tile-based \"auto\"");
    r = rule("", "", true, 0);
    CHECK(r.wide && !r.polled, "tile-based \"\"");

    /* auto elsewhere: v0.11.3's both */
    r = rule(NULL, NULL, false, 0);
    CHECK(!r.wide && r.polled, "desktop auto: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strstr(r.wideWhy, "narrow (the default where the GPU is not tile-based") == r.wideWhy,
          "desktop scope reason: %s", r.wideWhy);
    CHECK(strstr(r.polledWhy, "polled (the default where the GPU is not tile-based") == r.polledWhy,
          "desktop wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.wideTag, "narrow (the default)") == 0 &&
              strcmp(r.polledTag, "polled (the default)") == 0,
          "desktop tags: %s, %s", r.wideTag, r.polledTag);

    /* a value not known: auto, said */
    r = rule("sideways", "maybe", false, 0);
    CHECK(!r.wide && r.polled, "unknown values on a desktop: auto");
    CHECK(strstr(r.wideWhy, "\"sideways\" is none of auto, wide, narrow: auto") != NULL,
          "unknown scope said: %s", r.wideWhy);
    CHECK(strstr(r.polledWhy, "\"maybe\" is none of auto, on, off: auto") != NULL,
          "unknown wait said: %s", r.polledWhy);
    r = rule("sideways", "maybe", true, 0);
    CHECK(r.wide && !r.polled, "unknown values on a tiler: auto");

    /* the loss ladder on a desktop */
    r = rule(NULL, NULL, false, 1);
    CHECK(r.wide && r.polled, "one lost start: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strstr(r.wideWhy, "wide (the previous start lost the graphics device: this start only, "
                            "nothing written to the settings") == r.wideWhy,
          "one lost start, scope reason: %s", r.wideWhy);
    CHECK(strstr(r.polledWhy, "a second in a row gives the single wait") != NULL,
          "one lost start, wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.wideTag, "wide (a lost start)") == 0, "one lost start tag: %s", r.wideTag);
    r = rule(NULL, NULL, false, 2);
    CHECK(r.wide && !r.polled, "two lost starts: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strstr(r.polledWhy, "single (2 starts in a row lost the graphics device: this start "
                              "only, nothing written to the settings") == r.polledWhy,
          "two lost starts, wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.polledTag, "single (lost starts)") == 0, "two lost starts tag: %s", r.polledTag);
    r = rule(NULL, NULL, false, 9);
    CHECK(r.wide && !r.polled, "nine lost starts");
    /* ... and on a tiler (already both): the scope's reason names the loss */
    r = rule(NULL, NULL, true, 1);
    CHECK(r.wide && !r.polled, "a tiler after a lost start");
    CHECK(strstr(r.wideWhy, "; the previous start lost the graphics device") != NULL,
          "a tiler after a lost start, reason: %s", r.wideWhy);

    /* the settings rule, whatever the device or the ladder */
    r = rule("narrow", "on", true, 3);
    CHECK(!r.wide && r.polled, "narrow and on over a tiler and 3 losses: wide %d polled %d",
          (int)r.wide, (int)r.polled);
    CHECK(strcmp(r.wideWhy, "narrow ([dev] barrier_scope = narrow; auto would give wide here: a "
                            "tile-based GPU)") == 0,
          "narrow on a tiler: %s", r.wideWhy);
    CHECK(strcmp(r.polledWhy, "polled ([dev] polled_wait = on; auto would give the single wait "
                              "here: a tile-based GPU)") == 0,
          "on on a tiler: %s", r.polledWhy);
    r = rule("narrow", "1", false, 2);
    CHECK(!r.wide && r.polled, "narrow and 1 over 2 losses");
    CHECK(strstr(r.wideWhy, "auto would give wide here: 2 starts in a row lost") != NULL,
          "narrow after losses: %s", r.wideWhy);
    CHECK(strstr(r.polledWhy, "auto would give the single wait here: 2 starts in a row") != NULL,
          "on after losses: %s", r.polledWhy);
    r = rule("narrow", "on", false, 0);
    CHECK(strcmp(r.wideWhy, "narrow ([dev] barrier_scope = narrow)") == 0 &&
              strcmp(r.polledWhy, "polled ([dev] polled_wait = on)") == 0,
          "plain settings: %s | %s", r.wideWhy, r.polledWhy);
    r = rule("wide", "0", false, 0);
    CHECK(r.wide && !r.polled, "wide and 0 on a desktop");
    CHECK(strcmp(r.wideWhy, "wide ([dev] barrier_scope = wide)") == 0 &&
              strcmp(r.polledWhy, "single ([dev] polled_wait = off)") == 0 &&
              strcmp(r.wideTag, "wide (the setting)") == 0 &&
              strcmp(r.polledTag, "single (the setting)") == 0,
          "wide/off settings: %s | %s", r.wideWhy, r.polledWhy);

    /* the wait's words */
    static const char *const on[] = {"1", "on", "true", "yes"};
    static const char *const off[] = {"0", "off", "false", "no"};
    for (int i = 0; i < 4; i++) {
        r = rule(NULL, on[i], true, 0);
        CHECK(r.polled, "polled_wait %s is on", on[i]);
        r = rule(NULL, off[i], false, 0);
        CHECK(!r.polled, "polled_wait %s is off", off[i]);
    }

    /* the vendors */
    static const uint32_t tile[] = {0x5143, 0x13B5, 0x1010, 0x144D, 0x106B, 0x14E4};
    static const uint32_t notTile[] = {0x10005, 0x1002, 0x10DE, 0x8086, 0};
    for (size_t i = 0; i < sizeof tile / sizeof tile[0]; i++) {
        CHECK(rhi_tile_vendor(tile[i]) && rhi_tile_vendor_name(tile[i]) != NULL,
              "vendor 0x%04X is tile-based", (unsigned)tile[i]);
    }
    for (size_t i = 0; i < sizeof notTile / sizeof notTile[0]; i++) {
        CHECK(!rhi_tile_vendor(notTile[i]) && rhi_tile_vendor_name(notTile[i]) == NULL,
              "vendor 0x%04X is not", (unsigned)notTile[i]);
    }
    CHECK(strcmp(rhi_tile_vendor_name(0x13B5), "Arm") == 0, "Arm's name");

    /* ICO_LOSS_ROW */
    CHECK(rhi_tile_loss_row(NULL) == 0 && rhi_tile_loss_row("") == 0 &&
              rhi_tile_loss_row("x") == 0 && rhi_tile_loss_row("2x") == 0,
          "loss row: not numbers are 0");
    CHECK(rhi_tile_loss_row("0") == 0 && rhi_tile_loss_row("1") == 1 &&
              rhi_tile_loss_row("2") == 2 && rhi_tile_loss_row("40") == 9,
          "loss row: numbers, at most 9");

    if (s_fails == 0) {
        printf("rhi_vk_tile_rule: ok\n");
    }
    return s_fails ? 1 : 0;
}
