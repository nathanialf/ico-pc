/* rhi_vk_tile_rule_test.c: rhi_tile_rule.h's rule (pure: no device).
 *
 *   - which devices are tile-based phone GPUs (rhi_tile_phone): every
 *     Android GPU, iOS through MoltenVK, a faked tile-based vendor; not a
 *     Mac, a Qualcomm or Turnip desktop or any other desktop;
 *   - a tile-based phone GPU runs the wide scopes and the single wait, any
 *     other GPU the narrow scopes and the polled wait, each reason naming
 *     why;
 *   - the loss ladder on a desktop: one lost start in a row before this
 *     one gives the wide scopes (the wait stays polled, the reason says a
 *     second gives the single wait), two or more the single wait too; the
 *     reasons say nothing is written to the settings; never the pre-pass
 *     rung there;
 *   - on a phone one or more lost starts turn the VU pre-pass off for the
 *     start (noPrepass, its reason the loss's words);
 *   - the tests' override ICO_VK_FAKE_SCOPES (wide, narrow) picks the scopes
 *     alone on any device, the wait still the device's; another value is
 *     not an override;
 *   - the vendor table and ICO_LOSS_ROW's parse.
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

static RhiTileRule rule(const char *fake, bool phone, uint32_t row)
{
    RhiTileRule r;
    rhi_tile_rule(fake, phone, "Arm, vendor 0x13B5", row, &r);
    printf("fake %s phone %d row %u: barriers %s | wait %s | %s, %s | pre-pass %s (%s)\n",
           fake ? fake : "(unset)", (int)phone, (unsigned)row, r.wideWhy, r.polledWhy, r.wideTag,
           r.polledTag, r.noPrepass ? "off" : "as the device", r.prepassWhy);
    return r;
}

int main(void)
{
    RhiTileRule r;

    /* which devices are phones: the Android branch (vk_device.c: every
       Android GPU, whatever its vendor), iOS through MoltenVK, a faked
       tile-based vendor (the tests); a Mac or a Qualcomm desktop is not
       (the caller passes ios false off iOS, fakedTileVendor false for a
       real vendor id) */
    CHECK(rhi_tile_phone(true, false, false), "an Android GPU is a phone GPU");
    CHECK(rhi_tile_phone(true, false, true), "an Android GPU with a faked vendor");
    CHECK(rhi_tile_phone(false, true, false), "iOS through MoltenVK is a phone GPU");
    CHECK(rhi_tile_phone(false, false, true), "a faked tile-based vendor is a phone GPU");
    CHECK(!rhi_tile_phone(false, false, false),
          "a desktop (a Mac, a Qualcomm or Turnip desktop, any other) is not");

    /* a tile-based phone GPU: both of v0.11.2, the pre-pass as the device */
    r = rule(NULL, true, 0);
    CHECK(r.phone && r.wide && !r.polled && !r.noPrepass && r.prepassWhy[0] == '\0',
          "phone: wide %d polled %d no pre-pass %d", (int)r.wide, (int)r.polled, (int)r.noPrepass);
    CHECK(strcmp(r.wideWhy, "wide (a tile-based phone GPU, Arm, vendor 0x13B5)") == 0,
          "phone scope reason: %s", r.wideWhy);
    CHECK(strcmp(r.polledWhy, "single (a tile-based phone GPU, Arm, vendor 0x13B5)") == 0,
          "phone wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.wideTag, "wide (tile-based phone)") == 0 &&
              strcmp(r.polledTag, "single (tile-based phone)") == 0,
          "phone tags: %s, %s", r.wideTag, r.polledTag);

    /* elsewhere: both of v0.11.3 */
    r = rule(NULL, false, 0);
    CHECK(!r.phone && !r.wide && r.polled && !r.noPrepass, "desktop: wide %d polled %d",
          (int)r.wide, (int)r.polled);
    CHECK(strcmp(r.wideWhy, "narrow (not a tile-based phone GPU)") == 0 &&
              strcmp(r.polledWhy, "polled (not a tile-based phone GPU)") == 0,
          "desktop reasons: %s | %s", r.wideWhy, r.polledWhy);
    CHECK(strcmp(r.wideTag, "narrow (not a phone)") == 0 &&
              strcmp(r.polledTag, "polled (not a phone)") == 0,
          "desktop tags: %s, %s", r.wideTag, r.polledTag);

    /* the loss ladder on a desktop */
    r = rule(NULL, false, 1);
    CHECK(r.wide && r.polled, "one lost start: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strcmp(r.wideWhy, "wide (the previous start lost the graphics device: this start only, "
                            "nothing written to the settings)") == 0,
          "one lost start, scope reason: %s", r.wideWhy);
    CHECK(strstr(r.polledWhy, "a second in a row gives the single wait") != NULL,
          "one lost start, wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.wideTag, "wide (a lost start)") == 0, "one lost start tag: %s", r.wideTag);
    r = rule(NULL, false, 2);
    CHECK(r.wide && !r.polled, "two lost starts: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strcmp(r.polledWhy, "single (2 starts in a row lost the graphics device: this start "
                              "only, nothing written to the settings)") == 0,
          "two lost starts, wait reason: %s", r.polledWhy);
    CHECK(strcmp(r.polledTag, "single (lost starts)") == 0, "two lost starts tag: %s", r.polledTag);
    r = rule(NULL, false, 9);
    CHECK(r.wide && !r.polled && !r.noPrepass, "nine lost starts");
    /* ... and on a phone (already both): the scope's reason names the
       loss, and the phones' rung turns the pre-pass off for the start */
    r = rule(NULL, true, 1);
    CHECK(r.wide && !r.polled && r.noPrepass, "a phone after a lost start");
    CHECK(strcmp(r.wideWhy, "wide (a tile-based phone GPU, Arm, vendor 0x13B5; the previous start "
                            "lost the graphics device)") == 0,
          "a phone after a lost start, reason: %s", r.wideWhy);
    CHECK(strcmp(r.prepassWhy, "the previous start lost the graphics device") == 0,
          "a phone after a lost start, the pre-pass reason: %s", r.prepassWhy);
    r = rule(NULL, true, 2);
    CHECK(r.wide && !r.polled && r.noPrepass &&
              strcmp(r.prepassWhy, "2 starts in a row lost the graphics device") == 0,
          "a phone after two lost starts: the pre-pass reason: %s", r.prepassWhy);

    /* the tests' override: the scopes alone */
    r = rule("narrow", true, 3);
    CHECK(!r.wide && !r.polled && r.noPrepass,
          "narrow faked on a phone after 3 losses: wide %d polled %d", (int)r.wide, (int)r.polled);
    CHECK(strcmp(r.wideWhy, "narrow (ICO_VK_FAKE_SCOPES=narrow, a test)") == 0 &&
              strcmp(r.wideTag, "narrow (faked)") == 0,
          "narrow faked: %s, %s", r.wideWhy, r.wideTag);
    r = rule("wide", false, 0);
    CHECK(r.wide && r.polled, "wide faked on a desktop: wide %d polled %d", (int)r.wide,
          (int)r.polled);
    CHECK(strcmp(r.wideWhy, "wide (ICO_VK_FAKE_SCOPES=wide, a test)") == 0 &&
              strcmp(r.wideTag, "wide (faked)") == 0,
          "wide faked: %s, %s", r.wideWhy, r.wideTag);
    r = rule("sideways", false, 0);
    CHECK(!r.wide && r.polled, "another value is not an override");
    r = rule("", true, 0);
    CHECK(r.wide && !r.polled, "an empty value is not an override");

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
