/*
 * port/input/test/mouse_camera_test.c
 *
 * The mouse camera's speed and range for the game's hand camera
 * (mouse_camera.c): the step arithmetic, and that the speed and the range
 * apply only while the mouse's stick drives the camera.
 */
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "mouse_camera.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int near_(float a, float b, float tol)
{
    const float d = a - b;

    return (d < 0 ? -d : d) <= tol;
}

/* a speed of 1 is the game's own tenth of the remainder, bit for bit, and
   its limited move farther out */
static void test_step_unit(void)
{
    const float da0 = 0.37f, db0 = -0.21f;
    float da = da0, db = db0;

    ico_mouse_camera_step(&da, &db, 1.5f, 0.1f, 1.0f); /* 1.5 is not under 0.1 * 10: limited */
    CHECK(da == da0 * 0.1f / 1.5f && db == db0 * 0.1f / 1.5f);

    da = da0;
    db = db0;
    ico_mouse_camera_step(&da, &db, 0.4f, 0.5f, 1.0f); /* close: a tenth */
    CHECK(da == da0 / 10.0f && db == db0 / 10.0f);

    da = da0;
    db = db0;
    ico_mouse_camera_step(&da, &db, 0.4f, 0.01f, 1.0f); /* far: spd along the line */
    CHECK(da == da0 * 0.01f / 0.4f && db == db0 * 0.01f / 0.4f);

    da = da0;
    db = db0;
    ico_mouse_camera_step(&da, &db, 0.0f, 0.0f, 1.0f); /* nothing to do, spd 0: unchanged */
    CHECK(da == da0 && db == db0);
}

/* a speed of 2 covers a fifth of the remainder, 10 all of it, 0.5 a twentieth */
static void test_step_speeds(void)
{
    float da = 0.5f, db = -0.25f;

    ico_mouse_camera_step(&da, &db, 0.56f, 1.0f, 2.0f); /* 0.56 < 1.0 * 10 */
    CHECK(near_(da, 0.1f, 1e-6f) && near_(db, -0.05f, 1e-6f));

    da = 0.5f;
    db = -0.25f;
    ico_mouse_camera_step(&da, &db, 0.56f, 1.0f, 10.0f);
    CHECK(da == 0.5f && db == -0.25f);

    da = 0.5f;
    db = -0.25f;
    ico_mouse_camera_step(&da, &db, 0.56f, 1.0f, 5.0f); /* half */
    CHECK(near_(da, 0.25f, 1e-6f) && near_(db, -0.125f, 1e-6f));

    da = 0.5f;
    db = -0.25f;
    ico_mouse_camera_step(&da, &db, 0.56f, 1.0f, 0.5f);
    CHECK(near_(da, 0.025f, 1e-6f) && near_(db, -0.0125f, 1e-6f));

    /* beyond the close range the move is limited to spd, whatever k is */
    da = 0.5f;
    db = -0.25f;
    ico_mouse_camera_step(&da, &db, 0.56f, 0.01f, 3.0f);
    CHECK(near_(da, 0.5f * 0.01f / 0.56f, 1e-7f) && near_(db, -0.25f * 0.01f / 0.56f, 1e-7f));
}

/* the speed and the range reach the hand camera only while the mouse's stick
   drives it */
static void test_gating(void)
{
    IcoBindings *b = ico_input_live_bindings();

    memset(b, 0, sizeof(*b));
    CHECK(ico_mouse_camera_speed() == 1.0f && ico_mouse_camera_full_range() == 0);

    ico_bindings_defaults(b);
    b->mouse_camera_speed = 3.0f;
    b->mouse_full_range = 1;
    b->mouse_drives = 0; /* the pad or the keys are in charge */
    CHECK(ico_mouse_camera_speed() == 1.0f && ico_mouse_camera_full_range() == 0);

    b->mouse_drives = 1;
    CHECK(ico_mouse_camera_speed() == 3.0f && ico_mouse_camera_full_range() == 1);

    b->mouse_full_range = 0;
    CHECK(ico_mouse_camera_full_range() == 0);

    b->mouse_camera_speed = 0.0f; /* never a stopped camera */
    CHECK(ico_mouse_camera_speed() == 1.0f);

    /* through the binding step: only the mouse's own motion drives */
    {
        IcoInputRaw r;
        IcoVirtualPad v;

        ico_bindings_defaults(b);
        b->mouse_camera_speed = 5.0f;
        memset(&r, 0, sizeof(r));
        ico_bindings_step(b, &r, &v);
        CHECK(ico_mouse_camera_speed() == 1.0f);
        r.mouse_dx = 100.0f;
        ico_bindings_step(b, &r, &v);
        CHECK(ico_mouse_camera_speed() == 5.0f);
        r.mouse_dx = 0.0f;
        r.gamepads = 1;
        r.axis[2] = 1.0f;
        ico_bindings_step(b, &r, &v);
        CHECK(ico_mouse_camera_speed() == 1.0f);
    }
}

int main(void)
{
    test_step_unit();
    test_step_speeds();
    test_gating();
    if (failures != 0) {
        fprintf(stderr, "mouse_camera_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("mouse_camera_test: ok\n");
    return 0;
}
