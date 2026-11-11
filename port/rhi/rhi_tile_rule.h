/* rhi_tile_rule.h: the two v0.11.3 changes the Vulkan backend undoes on a
 * tile-based phone GPU, and the loss ladder that undoes them on any device
 * after a lost start (issues 179 and 211).  No Vulkan header: built and
 * tested on every host (port/rhi/test/rhi_vk_tile_rule_test.c).
 *
 * Two phones lost the graphics device at the first pictures of the first
 * 3D scene on v0.11.3 and not on v0.11.2: a Mali-G52 MC2 on Arm's driver
 * (issue 179) and an Adreno 710 on Qualcomm's (issue 211).  No cause is
 * proven.  The two v0.11.3 changes that alter such a phone's work there:
 *
 *   barrier scopes  (3a8802f37, 0de1680a2 item M1): the buffer barriers name
 *                   only the stages that can touch the buffer, a command
 *                   list's opening barrier only attachments (its copy half
 *                   moved into the list's closing barrier), and the
 *                   renderer records its texture uploads and the fog's
 *                   table after the VU pre-pass.  So a picture's uploads,
 *                   VU record and vertex work may run beside the picture
 *                   before's fragment work on the GPU (spec-valid; clean
 *                   under synchronisation validation on lavapipe).  "wide"
 *                   is v0.11.2's: every buffer barrier over the vertex
 *                   input, vertex, fragment, compute (where the compute
 *                   record exists) and copy stages, the opening barrier over
 *                   attachment and copy writes, the closing one the host's
 *                   read alone, the texture uploads and the fog's table
 *                   before the meshes.  "narrow" is v0.11.3's.
 *   polled wait     (60fa7a26e): the frame slot's recycle wait that blocks
 *                   past 50 ms becomes 4 ms timed waits reading the slot's
 *                   query results and the submission counter while its work
 *                   is in flight (issue 123's trace), and
 *                   VK_KHR/EXT_calibrated_timestamps enabled where offered.
 *                   Off is v0.11.2's: one wait without a time limit, the
 *                   extension not enabled.
 *
 * The rule, for each, with no setting: v0.11.2's on a tile-based phone GPU
 * (rhi_tile_phone: every Android GPU, and iOS through MoltenVK), v0.11.3's
 * elsewhere (desktops, Macs and Qualcomm or Turnip desktops included), and
 * the loss ladder on every device: on a desktop after one lost start in a
 * row the wide scopes, after two or more the single wait too; on a phone,
 * whose scopes and wait are v0.11.2's already, after one or more the VU
 * pre-pass off for that start (noPrepass: what the Adreno 740's
 * device-table row does, vk_device.c kVuPlainOnly; lossRow: the starts in
 * a row that lost the device before this one, port/platform/diag_host.c's
 * count through ICO_LOSS_ROW; nothing is written to the settings).  The
 * tests alone pick the scopes on any device with ICO_VK_FAKE_SCOPES=wide or
 * narrow (fakeScopes; any other value: not faked), and a phone with
 * ICO_VK_FAKE_VENDOR set to a tile-based vendor's id (that vendor's phone
 * GPU, the wait and the pre-pass rung with it). */
#ifndef PORT_RHI_RHI_TILE_RULE_H
#define PORT_RHI_RHI_TILE_RULE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct RhiTileRule {
    bool phone;     /* a tile-based phone GPU (rhi_tile_phone) */
    bool wide;      /* v0.11.2's barrier scopes and upload order */
    bool polled;    /* v0.11.3's polled recycle wait and calibrated timestamps */
    bool noPrepass; /* the phones' rung: the VU pre-pass off for this start */
    /* its reason (RhiLimits.vuPrepassLoss: "the previous start lost the
     * graphics device", "2 starts in a row lost the graphics device"), ""
     * without the rung */
    char prepassWhy[96];
    /* the start-up lines' reasons ("wide (a tile-based GPU: ...)") */
    char wideWhy[256];
    char polledWhy[256];
    /* the summary's words: "wide (tile-based phone)", "single (lost starts)" */
    char wideTag[48];
    char polledTag[48];
} RhiTileRule;

/* Whether the device is a tile-based phone GPU: android (an Android
 * build: every GPU there), ios (an iOS build through MoltenVK: Apple's
 * GPUs), or fakedTileVendor (the tests: ICO_VK_FAKE_VENDOR set to an id
 * rhi_tile_vendor names).  A Mac, a Qualcomm or Turnip desktop and every
 * other desktop GPU are not (they keep v0.11.3's scopes and wait). */
bool rhi_tile_phone(bool android, bool ios, bool fakedTileVendor);

/* fakeScopes: ICO_VK_FAKE_SCOPES's value (NULL unset); phone: a tile-based
 * phone GPU (rhi_tile_phone); device: its words for the reasons ("Arm,
 * vendor 0x13B5"; NULL "the GPU"); lossRow: the lost starts in a row
 * before this one */
void rhi_tile_rule(const char *fakeScopes, bool phone, const char *device, uint32_t lossRow,
                   RhiTileRule *out);

/* the vendor ids rd_profile.c calls tile-based outside Android (Qualcomm,
 * Arm, Imagination, Samsung, Apple, Broadcom) */
bool rhi_tile_vendor(uint32_t vendorId);

/* the vendor's words for the reasons ("Arm", "Qualcomm", ...; NULL another) */
const char *rhi_tile_vendor_name(uint32_t vendorId);

/* ICO_LOSS_ROW's value as a count (NULL, "", or not a number: 0; at most 9) */
uint32_t rhi_tile_loss_row(const char *value);

#endif
