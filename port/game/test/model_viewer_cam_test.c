/* model_viewer_cam_test: the viewer's camera step (model_viewer_cam.c) driven
   without the game. */
#include <math.h>
#include <stdio.h>

#include "model_viewer_cam.h"

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("model_viewer_cam_test: FAIL: ");                                               \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const unsigned char kRest[4] = {128, 128, 128, 128};

static MvCam fresh(void)
{
    MvCam c = {1000.0f, 120.0f, 4000.0f, 0.0f, 0.3f, 0.18f, 0.0f};
    return c;
}

static int near(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * fmaxf(1.0f, fabsf(b));
}

int main(void)
{
    MvCam c = fresh();
    for (int i = 0; i < 25; i++) {
        mv_cam_step(&c, MV_PAD_R2, kRest);
    }
    CHECK(near(c.dist, 1000.0f * expf(-25 * 0.05f)), "R2 25 ticks: dist %f", c.dist);
    for (int i = 0; i < 200; i++) {
        mv_cam_step(&c, MV_PAD_R2, kRest);
    }
    CHECK(c.dist == c.distMin, "R2 stops at distMin (%f)", c.dist);
    for (int i = 0; i < 400; i++) {
        mv_cam_step(&c, MV_PAD_L2, kRest);
    }
    CHECK(c.dist == c.distMax, "L2 grows to distMax (%f)", c.dist);
    c = fresh();
    mv_cam_step(&c, MV_PAD_L2 | MV_PAD_R2, kRest);
    CHECK(c.dist == 1000.0f, "both triggers cancel");
    CHECK(c.yaw == 0.3f && c.pitch == 0.18f && c.panY == 0.0f, "triggers leave the rest alone");

    /* stick up (small y byte): the model rises, panY positive (y points down) */
    c = fresh();
    unsigned char up[4] = {128, 128, 128, 0};
    for (int i = 0; i < 10; i++) {
        mv_cam_step(&c, 0, up);
    }
    float full = mv_cam_stick(0);
    CHECK(near(full, -1.0f), "full up is -1 (%f)", full);
    CHECK(near(c.panY, 10 * MV_PAN_RATE * 1000.0f), "stick up 10 ticks: panY %f", c.panY);
    CHECK(c.dist == 1000.0f && c.yaw == 0.3f && c.pitch == 0.18f, "pan leaves the orbit alone");
    unsigned char down[4] = {128, 128, 128, 255};
    for (int i = 0; i < 10; i++) {
        mv_cam_step(&c, 0, down);
    }
    CHECK(fabsf(c.panY) < 5.0f, "stick down undoes it (%f)", c.panY);

    /* the clamp follows dist: a pan at distMax, then zoomed in */
    c = fresh();
    for (int i = 0; i < 400; i++) {
        mv_cam_step(&c, MV_PAD_L2, kRest);
    }
    for (int i = 0; i < 400; i++) {
        mv_cam_step(&c, 0, up);
    }
    CHECK(near(c.panY, MV_PAN_MAX * c.dist), "pan stops at 0.6 dist (%f)", c.panY);
    for (int i = 0; i < 400; i++) {
        mv_cam_step(&c, MV_PAD_R2, kRest);
        CHECK(c.panY <= MV_PAN_MAX * c.dist * 1.00001f, "panY %f over 0.6 dist %f", c.panY, c.dist);
    }
    for (int i = 0; i < 400; i++) {
        mv_cam_step(&c, 0, down);
    }
    CHECK(near(c.panY, -MV_PAN_MAX * c.dist), "pan down stops at -0.6 dist (%f)", c.panY);

    /* dead zone: 24 off centre is ignored, 25 is not */
    c = fresh();
    unsigned char dz[4] = {128 + 24, 128 - 24, 128 + 23, 128 - 23};
    for (int i = 0; i < 10; i++) {
        mv_cam_step(&c, 0, dz);
    }
    CHECK(c.yaw == 0.3f && c.pitch == 0.18f && c.panY == 0.0f && c.dist == 1000.0f,
          "dead zone ignored");
    unsigned char out[4] = {128, 128, 128, 128 - 25};
    mv_cam_step(&c, 0, out);
    CHECK(c.panY > 0.0f, "just past the dead zone moves");

    /* the left stick's horizontal axis: right moves the model right (panX
       positive), left undoes it, and it stops at 0.6 of the distance */
    {
        MvCam h = {1000.0f, 100.0f, 5000.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        unsigned char right[4] = {128, 128, 255, 128}, left[4] = {128, 128, 0, 128};
        for (int i = 0; i < 10; i++) {
            mv_cam_step(&h, 0, right);
        }
        CHECK(near(h.panX, 10 * MV_PAN_RATE * 1000.0f * mv_cam_stick(255)),
              "stick right 10 ticks: panX %f", h.panX);
        CHECK(h.panY == 0.0f, "sideways leaves panY alone");
        for (int i = 0; i < 10; i++) {
            mv_cam_step(&h, 0, left);
        }
        CHECK(fabsf(h.panX) < 5.0f, "stick left undoes it (%f)", h.panX);
        for (int i = 0; i < 400; i++) {
            mv_cam_step(&h, 0, right);
        }
        CHECK(near(h.panX, MV_PAN_MAX * h.dist), "pan right stops at 0.6 dist (%f)", h.panX);
    }

    /* the right stick still turns: right (large x) lowers yaw, as before */
    c = fresh();
    unsigned char right[4] = {255, 128, 128, 128};
    mv_cam_step(&c, 0, right);
    CHECK(near(c.yaw, 0.3f - MV_YAW_RATE * mv_cam_stick(255)), "right stick turns (%f)", c.yaw);
    for (int i = 0; i < 200; i++) {
        unsigned char pu[4] = {128, 255, 128, 128};
        mv_cam_step(&c, 0, pu);
    }
    CHECK(c.pitch == MV_PITCH_MAX, "pitch clamps");

    if (failures == 0) {
        printf("model_viewer_cam_test: ok\n");
    }
    return failures ? 1 : 0;
}
