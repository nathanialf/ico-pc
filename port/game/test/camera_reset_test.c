/*
 * port/game/test/camera_reset_test.c
 *
 * Camera reset's press and stick mute (port/game/camera_reset.h) on the CPU:
 * off passes the stick through tick for tick over pseudo-random input; a
 * press resets once and a held press does not repeat; a stick still held at
 * the press is muted until it is back in its dead zone; no reset while the
 * look-around is not live (paused, no boy control, a scripted shot), and that
 * ends a mute; a reset ends a toggled zoom (zoom_toggle.h) and marks a cut;
 * a copy of the camera arm's use (ClearHandCameraCorrect's two angles); the
 * save-state block (one FRESH block whose bytes copied back resume the mute).
 */
#include <stdio.h>
#include <string.h>

#include "camera_reset.h"
#include "config.h"
#include "options.h"
#include "state_registry.h"
#include "video_options.h"
#include "zoom_toggle.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define R3 0x400

static unsigned s_rng = 4242u;

static unsigned rnd(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 16) & 0x7fffu;
}

/* one live tick: returns the reset, the stick as the arm hands it on */
static int tick(int r3, float x, float z, float *ox, float *oz)
{
    float sx = x, sz = z;
    const int r = ico_cam_reset(r3 ? R3 : 0, 1, &sx, &sz);

    if (ox != NULL) {
        *ox = sx;
        *oz = sz;
    }
    return r;
}

static void fresh(int on)
{
    ico_cam_reset_clear();
    ico_opt_set_camera_reset(on);
}

static void test_off(void)
{
    int i, same = 0;
    const unsigned long n0 = ico_cam_reset_count();
    const unsigned cuts = ico_video_cut_serial();

    fresh(0);
    for (i = 0; i < 2000; i++) {
        const int r3 = (rnd() & 1) ? R3 : 0;
        const int live = (rnd() % 4) != 0;
        const float x = (rnd() % 3 == 0) ? 0.0f : ((float)(rnd() % 2001) - 1000.0f) / 1000.0f;
        const float z = (rnd() % 3 == 0) ? 0.0f : ((float)(rnd() % 2001) - 1000.0f) / 1000.0f;
        float sx = x, sz = z;
        const int r = ico_cam_reset(r3, live, &sx, &sz);

        same += r == 0 && sx == x && sz == z;
    }
    CHECK(same == 2000);
    CHECK(ico_cam_reset_count() == n0);
    CHECK(ico_video_cut_serial() == cuts);
}

static void test_press(void)
{
    float sx, sz;
    const unsigned long n0 = ico_cam_reset_count();
    const unsigned cuts = ico_video_cut_serial();
    int i;

    fresh(1);
    CHECK(tick(0, 0.5f, 0.0f, &sx, &sz) == 0 && sx == 0.5f); /* the stick passes */
    CHECK(tick(0, 0.0f, 0.0f, NULL, NULL) == 0);
    /* a press with the stick let go: one reset, the cut marked */
    CHECK(tick(1, 0.0f, 0.0f, &sx, &sz) == 1 && sx == 0.0f && sz == 0.0f);
    CHECK(ico_cam_reset_count() == n0 + 1 && ico_video_cut_serial() == cuts + 1);
    /* held: no repeat */
    for (i = 0; i < 30; i++) {
        CHECK(tick(1, 0.0f, 0.0f, NULL, NULL) == 0);
    }
    CHECK(ico_cam_reset_count() == n0 + 1);
    /* the stick works again at once (it was in its dead zone) */
    CHECK(tick(1, -0.7f, 0.2f, &sx, &sz) == 0 && sx == -0.7f && sz == 0.2f);
    /* release, press again: a second reset */
    CHECK(tick(0, 0.0f, 0.0f, NULL, NULL) == 0);
    CHECK(tick(1, 0.0f, 0.0f, NULL, NULL) == 1);
    CHECK(ico_cam_reset_count() == n0 + 2);
}

static void test_stick_held(void)
{
    float sx, sz;
    int i;

    fresh(1);
    CHECK(tick(0, 0.9f, 0.0f, NULL, NULL) == 0);
    /* the press with the stick still pushed: reset, the stick muted */
    CHECK(tick(1, 0.9f, 0.0f, &sx, &sz) == 1 && sx == 0.0f && sz == 0.0f);
    for (i = 0; i < 20; i++) {
        CHECK(tick(1, 0.9f, -0.3f, &sx, &sz) == 0 && sx == 0.0f && sz == 0.0f);
    }
    /* one axis back is not enough */
    CHECK(tick(0, 0.0f, -0.3f, &sx, &sz) == 0 && sx == 0.0f && sz == 0.0f);
    /* back in the dead zone: the mute ends; the next push turns the camera */
    CHECK(tick(0, 0.0f, 0.0f, &sx, &sz) == 0 && sx == 0.0f && sz == 0.0f);
    CHECK(tick(0, 0.4f, 0.0f, &sx, &sz) == 0 && sx == 0.4f);
}

static void test_not_live(void)
{
    float sx = 0.8f, sz = 0.0f;
    const unsigned long n0 = ico_cam_reset_count();

    /* a press while the look-around is not live (paused, the boy not under
       control, a scripted shot): nothing; the stick passes as the arm's own
       test then clears the look-around itself */
    fresh(1);
    CHECK(ico_cam_reset(0, 0, &sx, &sz) == 0);
    CHECK(ico_cam_reset(R3, 0, &sx, &sz) == 0 && sx == 0.8f);
    CHECK(ico_cam_reset_count() == n0);
    /* held from there into live: no press */
    CHECK(tick(1, 0.0f, 0.0f, NULL, NULL) == 0);
    CHECK(tick(0, 0.0f, 0.0f, NULL, NULL) == 0 && tick(1, 0.0f, 0.0f, NULL, NULL) == 1);
    /* not live ends a mute */
    fresh(1);
    CHECK(tick(1, 0.9f, 0.0f, &sx, &sz) == 1 && sx == 0.0f);
    sx = 0.9f;
    CHECK(ico_cam_reset(R3, 0, &sx, &sz) == 0 && sx == 0.9f);
    CHECK(tick(1, 0.9f, 0.0f, &sx, &sz) == 0 && sx == 0.9f);
}

static void test_zoom(void)
{
    /* a toggled zoom ends with the reset; R2 still held is no new press */
    ico_zoom_reset();
    ico_opt_set_zoom_type(ICO_ZOOM_TOGGLE);
    (void)ico_zoom_held(0, 0, 1, ICO_ZOOM_SRC_BOY, 3);
    CHECK(ico_zoom_held(2, 0, 1, ICO_ZOOM_SRC_BOY, 3) == 1);
    CHECK(ico_zoom_wanted(2) == 1);
    fresh(1);
    CHECK(tick(1, 0.0f, 0.0f, NULL, NULL) == 1);
    CHECK(ico_zoom_wanted(2) == 0);
    CHECK(ico_zoom_held(2, 0, 1, ICO_ZOOM_SRC_BOY, 3) == 0);
    CHECK(ico_zoom_held(0, 0, 1, ICO_ZOOM_SRC_BOY, 3) == 0);
    CHECK(ico_zoom_held(2, 0, 1, ICO_ZOOM_SRC_BOY, 3) == 1);
    /* Hold: R2 held keeps zooming (the game's own) */
    ico_opt_set_zoom_type(ICO_ZOOM_HOLD);
    CHECK(tick(0, 0.0f, 0.0f, NULL, NULL) == 0 && tick(1, 0.0f, 0.0f, NULL, NULL) == 1);
    CHECK(ico_zoom_held(2, 0, 1, ICO_ZOOM_SRC_BOY, 3) == 2);
    ico_zoom_reset();
}

/* A copy of the arm's use (camera-ico2.c, the four SetCameraMatrix_Ico2
   copies: options_test finds the call four times): the look-around's two
   angles (hand-camera.c handCameraWork[0], [1]) stepping toward the stick's
   target a tenth of the way each tick, as RotateAccordingToStick does near
   the target; ClearHandCameraCorrect zeroes them. */
static float s_pitch, s_yaw;

static void arm_tick(int r3, float x, float z)
{
    float sx = x, sz = z;

    if (ico_cam_reset(r3 ? R3 : 0, 1, &sx, &sz)) {
        s_pitch = s_yaw = 0.0f; /* ClearHandCameraCorrect */
    }
    s_yaw += (sx * 2.0f - s_yaw) / 10.0f;
    s_pitch += (-sz * 1.0f - s_pitch) / 10.0f;
}

static void test_arm(void)
{
    int i;

    fresh(1);
    s_pitch = s_yaw = 0.0f;
    for (i = 0; i < 40; i++) {
        arm_tick(0, 1.0f, -0.5f);
    }
    CHECK(s_yaw > 1.5f && s_pitch > 0.4f);
    /* let go: the game eases back over many ticks */
    arm_tick(0, 0.0f, 0.0f);
    CHECK(s_yaw > 1.0f);
    /* the press: at rest in that tick, and stays at rest */
    arm_tick(1, 0.0f, 0.0f);
    CHECK(s_yaw == 0.0f && s_pitch == 0.0f);
    arm_tick(1, 0.0f, 0.0f);
    CHECK(s_yaw == 0.0f && s_pitch == 0.0f);
    /* off: the same input eases back as the game does */
    fresh(0);
    for (i = 0; i < 40; i++) {
        arm_tick(0, 1.0f, -0.5f);
    }
    arm_tick(1, 0.0f, 0.0f);
    CHECK(s_yaw > 1.0f);
}

static void test_block(void)
{
    const IcoStateEntry *e = NULL;
    unsigned char saved[32];
    float sx, sz;
    int i;

    ico_state_registry_reset();
    CHECK(ico_cam_reset_state_register() == 0);
    CHECK(ico_state_registry_count() == 1);
    for (i = 0; i < ico_state_registry_count(); i++) {
        if (strcmp(ico_state_registry_entry(i)->name, "camera_reset.state") == 0) {
            e = ico_state_registry_entry(i);
        }
    }
    CHECK(e != NULL);
    if (e == NULL) {
        return;
    }
    CHECK(e->flags == ICO_STATE_FRESH && e->n == 2 * sizeof(int) && e->n <= sizeof saved);
    CHECK(ico_cam_reset_state_register() != 0);
    /* muted with the stick pushed, saved; the mute ends; copied back: muted */
    fresh(1);
    CHECK(tick(1, 0.9f, 0.0f, &sx, &sz) == 1);
    memcpy(saved, e->p, e->n);
    CHECK(tick(0, 0.0f, 0.0f, NULL, NULL) == 0 && tick(0, 0.9f, 0.0f, &sx, &sz) == 0 && sx == 0.9f);
    memcpy(e->p, saved, e->n);
    CHECK(tick(1, 0.9f, 0.0f, &sx, &sz) == 0 && sx == 0.0f); /* held, muted, no press */
    ico_state_registry_reset();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_config_reset("/nonexistent/camera_reset_test.toml", "/nonexistent/camera_reset_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_camera_reset() == 0); /* the default */
    test_off();
    test_press();
    test_stick_held();
    test_not_live();
    test_zoom();
    test_arm();
    test_block();
    if (failures != 0) {
        fprintf(stderr, "camera_reset_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("camera_reset_test: ok\n");
    return 0;
}
