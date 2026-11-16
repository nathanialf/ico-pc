/* rd_plain_camera_test: the camera rule for static meshes and grids on a
 * device that keeps the VU plain set (port/render/rd_plain_camera.h; issue
 * 123's right wall, issue 163's skinned draws).  CPU only:
 *   - the decision (rd_replay_vu.c vuDropMode's first step) over every
 *     combination: a device that keeps the plain set keeps the drops on a
 *     skinned draw under the camera rule and on every draw under
 *     region_rule = ps2; a mesh or grid there goes on to the camera rule;
 *     off such a device nothing is kept by this step;
 *   - the counts a replay makes (rd__plain_camera_note) and the attempt
 *     ledger's row: its state by device and rule (on only with both), its
 *     counts in the block line word for word;
 *   - the 10 s line's words word for word, nothing without counts or
 *     replays, a short buffer cut and terminated;
 *   - the start line's words (the brief's text). */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_plain_camera.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void testDecision(void)
{
    /* noBeside, camera, skinned -> kept */
    static const struct {
        bool noBeside, camera, skinned, kept;
    } k[] = {
        {false, false, false, false}, {false, false, true, false}, {false, true, false, false},
        {false, true, true, false},   {true, false, false, true},  {true, false, true, true},
        {true, true, false, false},   {true, true, true, true},
    };

    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        CHECK(rd__plain_keeps_drops(k[i].noBeside, k[i].camera, k[i].skinned) == k[i].kept);
    }
}

static void testCounts(void)
{
    ico_attempt_reset();
    ico_attempt_decide_plain_camera(0, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PLAIN_CAMERA) == ICO_ATT_NA);
    ico_attempt_decide_plain_camera(1, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PLAIN_CAMERA) == ICO_ATT_NA);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_PLAIN_CAMERA),
                 "[dev] region_rule = ps2: the PS2's drops on every draw") == 0);
    ico_attempt_decide_plain_camera(1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PLAIN_CAMERA) == ICO_ATT_ON);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_PLAIN_CAMERA),
                 "static meshes and grids by the camera, skinned draws keep the PS2's drops "
                 "(issue 163)") == 0);
    /* a replay of two meshes by the camera, one grid by the camera, one
       skinned draw kept and a mesh under region_rule = ps2 (neither) */
    uint32_t cam = 0, kept = 0;
    rd__plain_camera_note(true, false, &cam, &kept);
    rd__plain_camera_note(true, false, &cam, &kept);
    rd__plain_camera_note(true, false, &cam, &kept);
    rd__plain_camera_note(false, true, &cam, &kept);
    rd__plain_camera_note(false, false, &cam, &kept);
    CHECK(cam == 3u && kept == 1u);
    char block[8192];
    const size_t n = ico_attempt_block_text(block, sizeof block, -1);
    CHECK(n > 0 && n + 1u < sizeof block);
    CHECK(strstr(block, "the camera rule for static meshes on the plain set on 3 mesh and grid "
                        "draws by the camera (1 skinned draws kept)") != NULL);
    /* the counts taken: the next block starts from 0 */
    ico_attempt_block_text(block, sizeof block, -1);
    CHECK(strstr(block, "the camera rule for static meshes on the plain set on 0 mesh and grid "
                        "draws by the camera (0 skinned draws kept)") != NULL);
    ico_attempt_reset();
}

static void testText(void)
{
    char buf[256];
    memset(buf, 'x', sizeof buf);
    CHECK(rd__plain_camera_text(buf, sizeof buf, 0, 0, 10) == 0 && buf[0] == '\0');
    memset(buf, 'x', sizeof buf);
    CHECK(rd__plain_camera_text(buf, sizeof buf, 5, 1, 0) == 0 && buf[0] == '\0');
    CHECK(rd__plain_camera_text(buf, 0, 5, 1, 1) == 0);
    /* 2400 and 280 draws over 20 replays: 120 and 14 a replay */
    const char *want = "; plain set (issue 163): 120 static and grid draws a replay by the "
                       "camera rule, 14 skinned draws a replay keep the PS2's drops";
    const size_t n = rd__plain_camera_text(buf, sizeof buf, 2400, 280, 20);
    CHECK(n == strlen(want) && strcmp(buf, want) == 0);
    /* a short buffer: cut, terminated, the length what was written */
    char small[16];
    const size_t m = rd__plain_camera_text(small, sizeof small, 2400, 280, 20);
    CHECK(m == sizeof small - 1u && strlen(small) == m && strncmp(small, want, m) == 0);
    /* the start line (rd_pipeline.c rd__vu_policy_init) */
    CHECK(strcmp(RD_PLAIN_CAMERA_START,
                 "region rule: the camera decides for static meshes on this device's plain set; "
                 "skinned draws keep the PS2's drops (issue 163)") == 0);
}

int main(void)
{
    testDecision();
    testCounts();
    testText();
    printf("rd_plain_camera_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
