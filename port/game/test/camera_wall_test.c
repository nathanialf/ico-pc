/*
 * port/game/test/camera_wall_test.c
 *
 * The blended camera's wall rule (port/game/camera_wall.c) on the CPU, no
 * game sources:
 *   wall      a segment through a wall's middle crosses it, front to back
 *             and back to front (the game's ClipPlane counts only the
 *             first); one in front of it, one behind it, one parallel to it
 *             do not; one past its end or over its top by less than the
 *             radius crosses, by more does not; a sloped bottom edge is
 *             followed along the wall; an end on the plane counts once
 *   floor     a segment down through a floor's polygon crosses, and up
 *             through its underside; one beside it does not, one within
 *             the radius of its rim does; a triangle and a quad
 *   rule      the first flip has no eye before it (no query); a step through
 *             a wall is reported and the eye stored; a free path is none;
 *             the reverse step through the same wall is reported; a pending
 *             cut and photo mode answer none without asking the query (the
 *             eye still stored); a still eye and a step past
 *             RD_INTERP_CAMERA_MOVE are not asked either; forgetting (a
 *             stage's start) makes the next flip a first one; a non-finite
 *             eye forgets; the moved distance
 */
#include <math.h>
#include <string.h>

#include "camera_wall.h"
#include "../../test/ico_check.h"
#include "rd_camera_blend.h"

/* a wall on the plane z = 0 facing +z, from x = 0 to x = 100, its bottom
   at 0 and its top at 50 */
static IcoCameraWallQuad wallZ0(void)
{
    IcoCameraWallQuad w;

    memset(&w, 0, sizeof(w));
    w.x = 0.0f;
    w.z = 0.0f;
    w.nx = 0.0f;
    w.nz = 1.0f;
    /* along (nz, -nx) = +x */
    w.length = 100.0f;
    w.bottom[0] = w.bottom[1] = 0.0f;
    w.top[0] = w.top[1] = 50.0f;
    return w;
}

static void checkWall(void)
{
    IcoCameraWallQuad w = wallZ0();
    const float r = ICO_CAMERA_WALL_RADIUS;
    const float front[3] = {50.0f, 25.0f, 20.0f};
    const float back[3] = {50.0f, 25.0f, -20.0f};

    CHECK(ico_camera_wall_cross_wall(front, back, &w, r) == 1, "front to back through the middle");
    CHECK(ico_camera_wall_cross_wall(back, front, &w, r) == 1, "back to front through the middle");
    {
        const float a[3] = {50.0f, 25.0f, 30.0f};
        CHECK(ico_camera_wall_cross_wall(a, front, &w, r) == 0, "in front all the way");
    }
    {
        const float a[3] = {50.0f, 25.0f, -30.0f};
        CHECK(ico_camera_wall_cross_wall(a, back, &w, r) == 0, "behind all the way");
    }
    {
        const float a[3] = {0.0f, 25.0f, 10.0f}, b[3] = {100.0f, 25.0f, 10.0f};
        CHECK(ico_camera_wall_cross_wall(a, b, &w, r) == 0, "parallel in front");
    }
    {
        /* past the end at x = 100: within the radius, then beyond it */
        const float a[3] = {102.0f, 25.0f, 20.0f}, b[3] = {102.0f, 25.0f, -20.0f};
        const float c[3] = {110.0f, 25.0f, 20.0f}, d[3] = {110.0f, 25.0f, -20.0f};
        const float e[3] = {-3.0f, 25.0f, 20.0f}, f[3] = {-3.0f, 25.0f, -20.0f};
        CHECK(ico_camera_wall_cross_wall(a, b, &w, r) == 1, "past the far end by 2");
        CHECK(ico_camera_wall_cross_wall(c, d, &w, r) == 0, "past the far end by 10");
        CHECK(ico_camera_wall_cross_wall(e, f, &w, r) == 1, "past the near end by 3");
    }
    {
        /* over the top at 50 */
        const float a[3] = {50.0f, 53.0f, 20.0f}, b[3] = {50.0f, 53.0f, -20.0f};
        const float c[3] = {50.0f, 60.0f, 20.0f}, d[3] = {50.0f, 60.0f, -20.0f};
        const float e[3] = {50.0f, -10.0f, 20.0f}, f[3] = {50.0f, -10.0f, -20.0f};
        CHECK(ico_camera_wall_cross_wall(a, b, &w, r) == 1, "over the top by 3");
        CHECK(ico_camera_wall_cross_wall(c, d, &w, r) == 0, "over the top by 10");
        CHECK(ico_camera_wall_cross_wall(e, f, &w, r) == 0, "under the bottom by 10");
    }
    {
        /* the bottom slopes from 0 at x = 0 to 40 at x = 100: at x = 90 it
           is 36, so a crossing at y 20 is under it, at x = 10 (4) over it */
        const float a[3] = {90.0f, 20.0f, 20.0f}, b[3] = {90.0f, 20.0f, -20.0f};
        const float c[3] = {10.0f, 20.0f, 20.0f}, d[3] = {10.0f, 20.0f, -20.0f};
        w.bottom[1] = 40.0f;
        CHECK(ico_camera_wall_cross_wall(a, b, &w, r) == 0, "under a sloped bottom");
        CHECK(ico_camera_wall_cross_wall(c, d, &w, r) == 1, "over a sloped bottom");
        w = wallZ0();
    }
    {
        /* an end on the plane: in front to on it crosses, on it to in front
           does too (the other side's view), on it both times does not */
        const float on[3] = {50.0f, 25.0f, 0.0f};
        CHECK(ico_camera_wall_cross_wall(front, on, &w, r) == 1, "in front to the plane");
        CHECK(ico_camera_wall_cross_wall(on, front, &w, r) == 1, "the plane to in front");
        CHECK(ico_camera_wall_cross_wall(on, on, &w, r) == 0, "on the plane, still");
    }
    {
        /* a wall at x 200 facing -x, along (nz, -nx) = +z from z 0 to 80 */
        IcoCameraWallQuad v = wallZ0();
        const float a[3] = {190.0f, 10.0f, 40.0f}, b[3] = {210.0f, 10.0f, 40.0f};
        const float c[3] = {190.0f, 10.0f, -40.0f}, d[3] = {210.0f, 10.0f, -40.0f};
        v.x = 200.0f;
        v.z = 0.0f;
        v.nx = -1.0f;
        v.nz = 0.0f;
        v.length = 80.0f;
        CHECK(ico_camera_wall_cross_wall(a, b, &v, r) == 1, "a wall facing -x");
        CHECK(ico_camera_wall_cross_wall(c, d, &v, r) == 0, "beside a wall facing -x");
    }
}

static IcoCameraWallFloor floorQuad(void)
{
    IcoCameraWallFloor f;

    memset(&f, 0, sizeof(f));
    /* y = 10 facing up: 0 x + 1 y + 0 z - 10 */
    f.n[1] = 1.0f;
    f.d = -10.0f;
    f.count = 4;
    f.x[0] = 0.0f;
    f.z[0] = 0.0f;
    f.x[1] = 100.0f;
    f.z[1] = 0.0f;
    f.x[2] = 100.0f;
    f.z[2] = 100.0f;
    f.x[3] = 0.0f;
    f.z[3] = 100.0f;
    return f;
}

static void checkFloor(void)
{
    IcoCameraWallFloor f = floorQuad();
    const float r = ICO_CAMERA_WALL_RADIUS;
    const float above[3] = {50.0f, 30.0f, 50.0f};
    const float below[3] = {50.0f, -10.0f, 50.0f};

    CHECK(ico_camera_wall_cross_floor(above, below, &f, r) == 1, "down through a floor");
    CHECK(ico_camera_wall_cross_floor(below, above, &f, r) == 1, "up through its underside");
    {
        const float a[3] = {50.0f, 30.0f, 50.0f}, b[3] = {60.0f, 12.0f, 70.0f};
        CHECK(ico_camera_wall_cross_floor(a, b, &f, r) == 0, "above it all the way");
    }
    {
        const float a[3] = {150.0f, 30.0f, 50.0f}, b[3] = {150.0f, -10.0f, 50.0f};
        const float c[3] = {102.0f, 30.0f, 50.0f}, d[3] = {102.0f, -10.0f, 50.0f};
        CHECK(ico_camera_wall_cross_floor(a, b, &f, r) == 0, "beside it");
        CHECK(ico_camera_wall_cross_floor(c, d, &f, r) == 1, "within the radius of its rim");
    }
    {
        /* a triangle: (0,0) (100,0) (0,100); (70, 70) is outside it */
        const float a[3] = {70.0f, 30.0f, 70.0f}, b[3] = {70.0f, -10.0f, 70.0f};
        const float c[3] = {20.0f, 30.0f, 20.0f}, d[3] = {20.0f, -10.0f, 20.0f};
        f.count = 3;
        f.x[2] = 0.0f;
        f.z[2] = 100.0f;
        CHECK(ico_camera_wall_cross_floor(a, b, &f, r) == 0, "outside a triangle");
        CHECK(ico_camera_wall_cross_floor(c, d, &f, r) == 1, "inside a triangle");
    }
}

/* a stub stage: one wall, the one of checkWall, and a call count */
static int s_calls;

static int stubStage(const float a[3], const float b[3], float r, void *user)
{
    const IcoCameraWallQuad *w = (const IcoCameraWallQuad *)user;

    s_calls++;
    return ico_camera_wall_cross_wall(a, b, w, r) ? ICO_CAMERA_WALL_WALL : ICO_CAMERA_WALL_NONE;
}

static void checkRule(void)
{
    IcoCameraWallQuad w = wallZ0();
    IcoCameraWall s;
    const float front[3] = {50.0f, 25.0f, 20.0f};
    const float back[3] = {50.0f, 25.0f, -20.0f};
    const float side[3] = {80.0f, 25.0f, 30.0f};
    float moved = -1.0f;

    memset(&s, 0, sizeof(s));
    s_calls = 0;
    CHECK(ico_camera_wall_step(&s, front, 0, 0, stubStage, &w, &moved) == ICO_CAMERA_WALL_NONE &&
              s_calls == 0,
          "the first flip asks nothing");
    CHECK(s.valid && s.eye[2] == 20.0f, "the first flip stores the eye");
    CHECK(ico_camera_wall_step(&s, back, 0, 0, stubStage, &w, &moved) == ICO_CAMERA_WALL_WALL &&
              s_calls == 1,
          "a step through the wall");
    CHECK(fabsf(moved - 40.0f) < 1e-4f, "moved 40 (%g)", (double)moved);
    CHECK(s.eye[2] == -20.0f, "the eye stored after a crossing");
    CHECK(ico_camera_wall_step(&s, front, 0, 0, stubStage, &w, NULL) == ICO_CAMERA_WALL_WALL,
          "the reverse step through the same wall");
    CHECK(ico_camera_wall_step(&s, side, 0, 0, stubStage, &w, NULL) == ICO_CAMERA_WALL_NONE &&
              s_calls == 3,
          "a free path");
    s_calls = 0;
    CHECK(ico_camera_wall_step(&s, (const float[3]){80.0f, 25.0f, -20.0f}, 1, 0, stubStage, &w,
                               NULL) == ICO_CAMERA_WALL_NONE &&
              s_calls == 0,
          "a pending cut: not asked");
    CHECK(s.eye[2] == -20.0f, "a pending cut: the eye stored");
    CHECK(ico_camera_wall_step(&s, (const float[3]){80.0f, 25.0f, 20.0f}, 0, 1, stubStage, &w,
                               NULL) == ICO_CAMERA_WALL_NONE &&
              s_calls == 0,
          "photo mode: not asked");
    CHECK(s.eye[2] == 20.0f, "photo mode: the eye stored");
    CHECK(ico_camera_wall_step(&s, (const float[3]){80.0f, 25.0f, 20.0f}, 0, 0, stubStage, &w,
                               &moved) == ICO_CAMERA_WALL_NONE &&
              s_calls == 0 && moved == 0.0f,
          "a still eye: not asked");
    {
        const float far[3] = {80.0f, 25.0f, 20.0f - (RD_INTERP_CAMERA_MOVE + 10.0f)};
        CHECK(ico_camera_wall_step(&s, far, 0, 0, stubStage, &w, NULL) == ICO_CAMERA_WALL_NONE &&
                  s_calls == 0,
              "a step past RD_INTERP_CAMERA_MOVE: not asked (the presenter snaps on its own)");
    }
    /* a stage's start (ico_camera_wall_forget clears valid) */
    ico_camera_wall_step(&s, front, 0, 0, stubStage, &w, NULL);
    s.valid = 0;
    s_calls = 0;
    CHECK(ico_camera_wall_step(&s, back, 0, 0, stubStage, &w, NULL) == ICO_CAMERA_WALL_NONE &&
              s_calls == 0,
          "after forgetting: a first flip");
    {
        const float bad[3] = {NAN, 0.0f, 0.0f};
        CHECK(ico_camera_wall_step(&s, bad, 0, 0, stubStage, &w, NULL) == ICO_CAMERA_WALL_NONE &&
                  !s.valid,
              "a non-finite eye forgets");
        CHECK(ico_camera_wall_step(&s, front, 0, 0, stubStage, &w, NULL) == ICO_CAMERA_WALL_NONE &&
                  s_calls == 0,
              "and the flip after is a first one");
    }
}

int main(void)
{
    checkWall();
    checkFloor();
    checkRule();
    return ico_check_summary("camera_wall_test");
}
