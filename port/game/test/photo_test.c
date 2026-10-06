/* photo_test.c: photo mode's state and camera (port/game/photo_mode.c,
 * package PHOTO).  CPU only.
 *
 *   identity  nothing moved: the override is the game camera, byte for byte
 *   orbit     a second of the left stick right at stick_speed 1: 90 degrees
 *             of yaw about the vertical through the pivot (400 in front of
 *             the eye without a subject), the eye 400 from the pivot, level,
 *             looking at it; with a subject 900 ahead (off the axis), the
 *             pivot is the axis' point nearest it
 *   pitch     the stick held up: the eye rises, its elevation stops at 85
 *             degrees
 *   zoom      R2 held: the vertical field of view narrows, the picture's
 *             centre stays; Select: the game camera again
 *   keys      Square toggles the HUD, Cross asks one capture, Triangle,
 *             Circle and Start ask to leave; [photo] keys from a config
 *   name      the capture's file name
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "photo_mode.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the game camera: the eye at (0, -100, -500), looking along +z, the GS's
   y down the screen (view +y = world +y, the world's up is -y as in the
   game), focal length 500, the centre at 2048 */
static void gameCamera(RdCamera *c)
{
    memset(c, 0, sizeof(*c));
    c->view[0] = c->view[5] = c->view[10] = c->view[15] = 1.0f;
    c->view[12] = 0.0f;
    c->view[13] = 100.0f;
    c->view[14] = 500.0f;
    c->proj43[0] = c->proj43[5] = 500.0f;
    c->proj43[8] = c->proj43[9] = 2048.0f;
    c->proj43[10] = 1.0f;
    c->proj43[11] = 1.0f;
    c->proj43[14] = 1.0f;
    c->zoom = 500.0f;
}

static void eyeOf(const RdCamera *c, float e[3])
{
    const float *v = c->view;
    for (int j = 0; j < 3; j++) {
        e[j] = -(v[j * 4 + 0] * v[12] + v[j * 4 + 1] * v[13] + v[j * 4 + 2] * v[14]);
    }
}

static IcoPhotoPad idle(void)
{
    IcoPhotoPad p;
    memset(&p, 0, sizeof(p));
    memset(p.ana, 128, sizeof(p.ana));
    return p;
}

int main(int argc, char **argv)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/photo_test.toml", argc > 1 ? argv[1] : ".");
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs("version = 1\n[photo]\nstick_speed = 1.0\npng_dir = \"pics\"\n", f);
        fclose(f);
    }
    ico_config_reset(path, "");
    ico_photo_reset();
    RdCamera game, out;
    gameCamera(&game);
    CHECK(!ico_photo_active() && ico_photo_update(NULL) == 0, "inactive at start");
    ico_photo_set_tick_hz(25);
    ico_photo_enter();
    CHECK(ico_photo_active() && ico_photo_hud(), "enter: active, the HUD shown");
    CHECK(strcmp(ico_photo_png_dir(), "pics") == 0 && ico_photo_stick_speed() == 1.0f,
          "[photo] png_dir and stick_speed (%s)", ico_photo_png_dir());
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0,
          "identity: the game camera");
    const float fov0 = ico_photo_fov_deg(&game);
    CHECK(fabsf(fov0 - 2.0f * atanf(256.0f / 500.0f) * 57.29578f) < 1e-3f, "fov %.3f",
          (double)fov0);

    /* orbit */
    IcoPhotoPad p = idle();
    p.ana[2] = 255;
    for (int i = 0; i < 25; i++) {
        CHECK(ico_photo_update(&p) == 0, "orbit: stays");
    }
    IcoPhotoState st;
    ico_photo_get(&st);
    CHECK(fabsf(st.yaw - 1.5707963f) < 1e-3f, "orbit: 90 degrees of yaw (%.4f)", (double)st.yaw);
    ico_photo_camera(&out, &game);
    float e[3];
    eyeOf(&out, e);
    /* the pivot: (0, -100, -100); a quarter turn about -y puts the eye 400
       to one side, at the pivot's height */
    const float dx = e[0], dy = e[1] + 100.0f, dz = e[2] + 100.0f;
    CHECK(fabsf(sqrtf(dx * dx + dy * dy + dz * dz) - 400.0f) < 0.05f && fabsf(dy) < 0.05f &&
              fabsf(fabsf(dx) - 400.0f) < 0.05f,
          "orbit: the eye (%.2f, %.2f, %.2f) 400 from the pivot, level", (double)e[0], (double)e[1],
          (double)e[2]);
    /* the eye moved to the camera's right (+x), looking at the pivot */
    CHECK(e[0] > 0.0f, "orbit: the eye to the right (%.2f)", (double)e[0]);
    const float fwd[3] = {out.view[2], out.view[6], out.view[10]};
    CHECK(fabsf(fwd[0] + 1.0f) < 1e-3f && fabsf(fwd[1]) < 1e-3f, "orbit: looking at the pivot");

    /* a subject 900 in front of the eye, 50 to the side: the pivot 900 ahead */
    const float subject[3] = {50.0f, -100.0f, 400.0f};
    ico_photo_set_subject(subject);
    ico_photo_camera(&out, &game);
    eyeOf(&out, e);
    CHECK(fabsf(e[0] - 900.0f) < 0.1f && fabsf(e[1] + 100.0f) < 0.1f && fabsf(e[2] - 400.0f) < 0.1f,
          "orbit: about the subject's point of the axis (%.2f, %.2f, %.2f)", (double)e[0],
          (double)e[1], (double)e[2]);
    ico_photo_set_subject(NULL);
    /* pitch: up raises the eye, to 85 degrees at most */
    p = idle();
    p.ana[3] = 0;
    for (int i = 0; i < 25 * 3; i++) {
        ico_photo_update(&p);
        ico_photo_camera(&out, &game);
    }
    eyeOf(&out, e);
    const float elev = asinf(-(e[1] + 100.0f) / 400.0f) * 57.29578f;
    CHECK(fabsf(elev - 85.0f) < 0.05f, "pitch: the elevation stops at 85 (%.3f)", (double)elev);

    /* zoom: R2 for a second narrows the picture by e^0.8 */
    p = idle();
    p.held = ICO_PHOTO_R2;
    for (int i = 0; i < 25; i++) {
        ico_photo_update(&p);
    }
    ico_photo_camera(&out, &game);
    CHECK(fabsf(out.proj43[5] / 500.0f - expf(0.8f)) < 1e-3f && out.proj43[8] == 2048.0f &&
              fabsf(out.zoom - 500.0f * expf(0.8f)) < 0.05f,
          "zoom: focal %.2f, centre %.1f", (double)out.proj43[5], (double)out.proj43[8]);
    CHECK(ico_photo_fov_deg(&out) < fov0, "zoom: the field of view narrower");
    p = idle();
    p.pressed = ICO_PHOTO_SELECT;
    ico_photo_update(&p);
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0,
          "Select: the game camera again");

    /* keys */
    p = idle();
    p.pressed = ICO_PHOTO_SQUARE;
    ico_photo_update(&p);
    CHECK(!ico_photo_hud(), "Square: the HUD hidden");
    p.pressed = ICO_PHOTO_CROSS;
    ico_photo_update(&p);
    CHECK(ico_photo_take_capture() == 1 && ico_photo_take_capture() == 0, "Cross: one capture");
    const unsigned leave[3] = {ICO_PHOTO_TRIANGLE, ICO_PHOTO_CIRCLE, ICO_PHOTO_START};
    for (int i = 0; i < 3; i++) {
        p.pressed = leave[i];
        CHECK(ico_photo_update(&p) == 1, "button %x leaves", leave[i]);
    }
    ico_photo_exit();
    CHECK(!ico_photo_active() && !ico_photo_hud(), "exit");

    char name[64];
    ico_photo_file_name(name, sizeof(name), 2026, 10, 6, 9, 5, 7, 1);
    CHECK(strcmp(name, "ico-20261006-090507.png") == 0, "name %s", name);
    ico_photo_file_name(name, sizeof(name), 2026, 10, 6, 9, 5, 7, 3);
    CHECK(strcmp(name, "ico-20261006-090507-3.png") == 0, "name %s", name);
    if (failures) {
        printf("photo_test: %d failures\n", failures);
        return 1;
    }
    printf("photo_test: ok\n");
    return 0;
}
