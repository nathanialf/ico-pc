/* photo_test.c: photo mode's state and camera (port/game/photo_mode.c,
 * package PHOTO; v0.4.1 the free camera).  CPU only.
 *
 *   identity  nothing moved: the override is the game camera, byte for byte
 *   free      the camera at enter, the HUD shown: a second of the left stick
 *             up moves the eye 400 along the view, right 400 along the
 *             camera's right, Up 400 along the world's up; a second of the
 *             right stick right turns the view 90 degrees to the right (and
 *             forward follows it); the right stick up looks up, to 85
 *             degrees at most; R3 cycles Normal, Fast (1600 a second), Slow
 *             (100), Normal
 *   orbit     L3: the orbit camera, the free camera's state kept; a second
 *             of the left stick right at stick_speed 1: 90 degrees of yaw
 *             about the vertical through the pivot (400 in front of the eye
 *             without a subject), the eye 400 from the pivot, level,
 *             looking at it; with a subject 900 ahead (off the axis), the
 *             pivot is the axis' point nearest it
 *   pitch     the stick held up: the eye rises, its elevation stops at 85
 *             degrees
 *   zoom      R2 held a second: the vertical field of view narrows by
 *             e^(0.35 stick_speed), the picture's centre stays, in both
 *             cameras; held long, 10 degrees, L2 held long, 100
 *   reset     Select: the current camera (and the zoom) only
 *   panel     Square toggles the help panel and saves [photo] hide_ui; the
 *             next enter starts with it hidden, as hide_ui = true does
 *   keys      Cross asks one capture, Triangle, Circle and Start ask to
 *             leave; [photo] keys from a config
 *   name      the capture's file name
 *   basis     (issue 14) the game camera kept at enter is the cameras' basis
 *             whatever camera ico_photo_camera is given, the picture never
 *             feeding back; ico_photo_fov_now follows the zoom
 *   step      at stick_speed 10, a tick moves the eye 250 and turns the view
 *             25 degrees at most (the renderer blends below 300 and 30);
 *             Select still jumps
 *   mouse     (I17a) ico_photo_mouse_look: the free camera's yaw grows by the
 *             degrees given, its pitch stops at 85 degrees, a big move
 *             turns 25 degrees a tick at most; the orbit camera's yaw and
 *             pitch, its dolly and pan left alone; nothing while inactive
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

static void ticks(const IcoPhotoPad *p, int n, RdCamera *out, const RdCamera *game)
{
    for (int i = 0; i < n; i++) {
        ico_photo_update(p);
        ico_photo_camera(out, game);
    }
}

static IcoPhotoPad pressed(unsigned bits)
{
    IcoPhotoPad p = idle();
    p.pressed = bits;
    return p;
}

static int near3(const float *a, float x, float y, float z, float tol)
{
    return fabsf(a[0] - x) < tol && fabsf(a[1] - y) < tol && fabsf(a[2] - z) < tol;
}

static void writeConfig(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
    ico_config_reset(path, "");
}

int main(int argc, char **argv)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/photo_test.toml", argc > 1 ? argv[1] : ".");
    writeConfig(path, "version = 1\n[photo]\nstick_speed = 1.0\npng_dir = \"pics\"\n");
    ico_photo_reset();
    RdCamera game, out;
    gameCamera(&game);
    CHECK(!ico_photo_active() && ico_photo_update(NULL) == 0, "inactive at start");
    ico_photo_set_tick_hz(25);
    ico_photo_enter();
    CHECK(ico_photo_active() && ico_photo_hud(), "enter: active, the HUD shown");
    CHECK(ico_photo_mode() == ICO_PHOTO_CAM_FREE && ico_photo_speed() == ICO_PHOTO_SPEED_NORMAL &&
              !ico_photo_hide_ui(),
          "enter: the free camera at Normal speed (%d, %d)", ico_photo_mode(), ico_photo_speed());
    CHECK(strcmp(ico_photo_png_dir(), "pics") == 0 && ico_photo_stick_speed() == 1.0f,
          "[photo] png_dir and stick_speed (%s)", ico_photo_png_dir());
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0,
          "identity: the game camera");
    const float fov0 = ico_photo_fov_deg(&game);
    CHECK(fabsf(fov0 - 2.0f * atanf(256.0f / 500.0f) * 57.29578f) < 1e-3f, "fov %.3f",
          (double)fov0);

    /* free: the eye at (0, -100, -500) looking along +z, the camera's right
       +x, the world's up -y */
    IcoPhotoPad p = idle();
    float e[3];
    IcoPhotoState st;
    p.ana[3] = 0; /* the left stick up */
    ticks(&p, 25, &out, &game);
    eyeOf(&out, e);
    CHECK(near3(e, 0.0f, -100.0f, -100.0f, 0.05f), "free: forward 400 (%.2f, %.2f, %.2f)",
          (double)e[0], (double)e[1], (double)e[2]);
    ico_photo_get(&st);
    CHECK(near3(st.pos, 0.0f, 0.0f, 400.0f, 0.05f), "free: pos (%.2f, %.2f, %.2f)",
          (double)st.pos[0], (double)st.pos[1], (double)st.pos[2]);
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0,
          "free: Select, the game camera again");
    p = idle();
    p.ana[2] = 255; /* the left stick right */
    ticks(&p, 25, &out, &game);
    eyeOf(&out, e);
    CHECK(near3(e, 400.0f, -100.0f, -500.0f, 0.05f), "free: strafe 400 (%.2f, %.2f, %.2f)",
          (double)e[0], (double)e[1], (double)e[2]);
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    p = idle();
    p.held = ICO_PHOTO_UP;
    ticks(&p, 25, &out, &game);
    eyeOf(&out, e);
    CHECK(near3(e, 0.0f, -500.0f, -500.0f, 0.05f), "free: Up rises 400 (%.2f, %.2f, %.2f)",
          (double)e[0], (double)e[1], (double)e[2]);
    CHECK(fabsf(out.proj43[5] - 500.0f) < 1e-3f, "free: Up leaves the zoom alone (%.3f)",
          (double)out.proj43[5]);
    p.held = ICO_PHOTO_DOWN;
    ticks(&p, 50, &out, &game);
    eyeOf(&out, e);
    CHECK(near3(e, 0.0f, 300.0f, -500.0f, 0.05f), "free: Down sinks (%.2f, %.2f, %.2f)",
          (double)e[0], (double)e[1], (double)e[2]);
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    /* look: a second of the right stick right, 90 degrees to the right */
    p = idle();
    p.ana[0] = 255;
    ticks(&p, 25, &out, &game);
    ico_photo_get(&st);
    CHECK(fabsf(st.fyaw - 1.5707963f) < 1e-3f, "free: 90 degrees of yaw (%.4f)", (double)st.fyaw);
    CHECK(fabsf(out.view[2] - 1.0f) < 1e-3f && fabsf(out.view[6]) < 1e-3f &&
              fabsf(out.view[10]) < 1e-3f,
          "free: looking along +x (%.3f, %.3f, %.3f)", (double)out.view[2], (double)out.view[6],
          (double)out.view[10]);
    eyeOf(&out, e);
    CHECK(near3(e, 0.0f, -100.0f, -500.0f, 0.05f), "free: turning keeps the eye");
    p = idle();
    p.ana[3] = 0;
    ticks(&p, 25, &out, &game);
    eyeOf(&out, e);
    CHECK(near3(e, 400.0f, -100.0f, -500.0f, 0.05f),
          "free: forward follows the view (%.2f, %.2f, %.2f)", (double)e[0], (double)e[1],
          (double)e[2]);
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    /* the right stick up looks up (the world's up is -y), 85 degrees at most */
    p = idle();
    p.ana[1] = 0;
    ticks(&p, 25 * 3, &out, &game);
    const float look = asinf(-out.view[6]) * 57.29578f;
    CHECK(fabsf(look - 85.0f) < 0.05f, "free: the view's elevation stops at 85 (%.3f)",
          (double)look);
    ico_photo_get(&st);
    CHECK(fabsf(st.fpitch * 57.29578f - 85.0f) < 0.05f, "free: fpitch %.3f",
          (double)(st.fpitch * 57.29578f));
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    /* R3: Fast, Slow, Normal */
    p = pressed(ICO_PHOTO_R3);
    ico_photo_update(&p);
    CHECK(ico_photo_speed() == ICO_PHOTO_SPEED_FAST, "R3: Fast (%d)", ico_photo_speed());
    p = idle();
    p.ana[3] = 0;
    ticks(&p, 25, &out, &game);
    ico_photo_get(&st);
    CHECK(near3(st.pos, 0.0f, 0.0f, 1600.0f, 0.2f), "Fast: 1600 in a second (%.2f)",
          (double)st.pos[2]);
    p = pressed(ICO_PHOTO_R3 | ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    CHECK(ico_photo_speed() == ICO_PHOTO_SPEED_SLOW, "R3: Slow (%d)", ico_photo_speed());
    p = idle();
    p.ana[3] = 0;
    ticks(&p, 25, &out, &game);
    ico_photo_get(&st);
    CHECK(near3(st.pos, 0.0f, 0.0f, 100.0f, 0.05f), "Slow: 100 in a second (%.2f)",
          (double)st.pos[2]);
    p = pressed(ICO_PHOTO_R3 | ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    CHECK(ico_photo_speed() == ICO_PHOTO_SPEED_NORMAL, "R3: Normal again (%d)", ico_photo_speed());
    /* the free camera turned a little, kept through the orbit camera */
    p = idle();
    p.ana[0] = 255;
    ticks(&p, 5, &out, &game);
    IcoPhotoState kept;
    ico_photo_get(&kept);

    /* L3: the orbit camera, its state untouched by the free camera's */
    p = pressed(ICO_PHOTO_L3);
    ico_photo_update(&p);
    ico_photo_get(&st);
    CHECK(ico_photo_mode() == ICO_PHOTO_CAM_ORBIT && st.yaw == 0.0f && st.pitch == 0.0f &&
              st.roll == 0.0f && st.pan == 0.0f && st.dolly == 1.0f,
          "L3: the orbit camera, as it was");
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0,
          "L3: the orbit camera unmoved shows the game camera");

    /* orbit */
    p = idle();
    p.ana[2] = 255;
    for (int i = 0; i < 25; i++) {
        CHECK(ico_photo_update(&p) == 0, "orbit: stays");
    }
    ico_photo_get(&st);
    CHECK(fabsf(st.yaw - 1.5707963f) < 1e-3f, "orbit: 90 degrees of yaw (%.4f)", (double)st.yaw);
    CHECK(st.fyaw == kept.fyaw && near3(st.pos, kept.pos[0], kept.pos[1], kept.pos[2], 1e-6f),
          "orbit: the free camera's state kept");
    ico_photo_camera(&out, &game);
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

    /* zoom: R2 for a second narrows the picture by e^0.35 */
    p = idle();
    p.held = ICO_PHOTO_R2;
    for (int i = 0; i < 25; i++) {
        ico_photo_update(&p);
    }
    ico_photo_camera(&out, &game);
    CHECK(fabsf(out.proj43[5] / 500.0f - expf(0.35f)) < 1e-3f && out.proj43[8] == 2048.0f &&
              fabsf(out.zoom - 500.0f * expf(0.35f)) < 0.05f,
          "zoom: focal %.2f, centre %.1f", (double)out.proj43[5], (double)out.proj43[8]);
    CHECK(ico_photo_fov_deg(&out) < fov0, "zoom: the field of view narrower");
    /* the field of view's range, orbit */
    ticks(&p, 25 * 10, &out, &game);
    CHECK(fabsf(ico_photo_fov_deg(&out) - 10.0f) < 0.01f, "orbit: R2 stops at 10 (%.3f)",
          (double)ico_photo_fov_deg(&out));
    p.held = ICO_PHOTO_L2;
    ticks(&p, 25 * 20, &out, &game);
    CHECK(fabsf(ico_photo_fov_deg(&out) - 100.0f) < 0.01f, "orbit: L2 stops at 100 (%.3f)",
          (double)ico_photo_fov_deg(&out));
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0,
          "Select: the game camera again");
    ico_photo_get(&st);
    CHECK(st.fyaw == kept.fyaw && st.fyaw != 0.0f, "orbit's Select: the free camera kept (%.4f)",
          (double)st.fyaw);
    /* the orbit camera moved, then Select in the free camera */
    p = idle();
    p.ana[2] = 255;
    ticks(&p, 5, &out, &game);
    ico_photo_get(&kept);
    p = pressed(ICO_PHOTO_L3);
    ico_photo_update(&p);
    CHECK(ico_photo_mode() == ICO_PHOTO_CAM_FREE, "L3: the free camera again");
    ico_photo_camera(&out, &game);
    CHECK(memcmp(&out, &game, sizeof(game)) != 0, "the free camera still turned");
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    ico_photo_get(&st);
    CHECK(ico_photo_camera(&out, &game) && memcmp(&out, &game, sizeof(game)) == 0 &&
              st.fyaw == 0.0f,
          "free's Select: the game camera");
    CHECK(st.yaw == kept.yaw && st.yaw != 0.0f, "free's Select: the orbit camera kept (%.4f)",
          (double)st.yaw);
    /* zoom in the free camera, and its range */
    p = idle();
    p.held = ICO_PHOTO_R2;
    ticks(&p, 25, &out, &game);
    CHECK(fabsf(out.proj43[5] / 500.0f - expf(0.35f)) < 1e-3f && out.proj43[8] == 2048.0f,
          "free: zoom focal %.2f", (double)out.proj43[5]);
    ticks(&p, 25 * 10, &out, &game);
    CHECK(fabsf(ico_photo_fov_deg(&out) - 10.0f) < 0.01f, "free: R2 stops at 10 (%.3f)",
          (double)ico_photo_fov_deg(&out));
    p.held = ICO_PHOTO_L2;
    ticks(&p, 25 * 20, &out, &game);
    CHECK(fabsf(ico_photo_fov_deg(&out) - 100.0f) < 0.01f, "free: L2 stops at 100 (%.3f)",
          (double)ico_photo_fov_deg(&out));
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);

    /* the help panel: Square hides it and saves the choice */
    p = pressed(ICO_PHOTO_SQUARE);
    ico_photo_update(&p);
    CHECK(!ico_photo_hud() && ico_photo_hide_ui() && ico_config_get_bool("photo.hide_ui", 0) == 1,
          "Square: the panel hidden, hide_ui saved");
    ico_photo_update(&p);
    CHECK(ico_photo_hud() && !ico_photo_hide_ui() && ico_config_get_bool("photo.hide_ui", 1) == 0,
          "Square again: shown, hide_ui false");
    ico_photo_update(&p);
    CHECK(!ico_photo_hud(), "Square: hidden again");
    /* written to the file: read back from it */
    ico_config_reset(path, "");
    CHECK(ico_config_get_bool("photo.hide_ui", 0) == 1, "hide_ui in the file");
    ico_photo_exit();
    ico_photo_enter();
    CHECK(ico_photo_active() && !ico_photo_hud() && ico_photo_mode() == ICO_PHOTO_CAM_FREE,
          "the next enter: the panel hidden, the free camera");
    p = pressed(ICO_PHOTO_SQUARE);
    ico_photo_update(&p);
    CHECK(ico_photo_hud(), "Square: shown");
    ico_photo_exit();
    /* hide_ui = true from the file; stick_speed 2 doubles the zoom's rate */
    writeConfig(path, "version = 1\n[photo]\nstick_speed = 2.0\nhide_ui = true\n");
    ico_photo_enter();
    CHECK(ico_photo_active() && !ico_photo_hud() && ico_photo_hide_ui(),
          "hide_ui = true: the panel hidden at enter");
    ico_photo_camera(&out, &game);
    p = idle();
    p.held = ICO_PHOTO_R2;
    ticks(&p, 25, &out, &game);
    CHECK(fabsf(out.proj43[5] / 500.0f - expf(0.7f)) < 1e-3f,
          "stick_speed 2: the zoom e^0.7 in a second (%.4f)", (double)(out.proj43[5] / 500.0f));
    p = idle();
    p.held = ICO_PHOTO_R1;
    ticks(&p, 25, &out, &game);
    ico_photo_get(&st);
    CHECK(fabsf(st.froll - 2.0f * 0.7853982f) < 1e-3f, "stick_speed 2: roll 90 a second (%.4f)",
          (double)st.froll);

    /* keys */
    p = idle();
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

    /* issue 14: the game camera kept at enter (ico_photo_set_game) is the
       basis whatever camera ico_photo_camera is given, so the picture
       drawn from the photo camera never feeds back into it */
    writeConfig(path, "version = 1\n[photo]\nstick_speed = 1.0\n");
    ico_photo_enter();
    CHECK(ico_photo_set_game(&game), "basis: kept");
    p = idle();
    p.ana[3] = 0;   /* forward */
    p.ana[0] = 200; /* and turning right */
    ticks(&p, 10, &out, &game);
    RdCamera fed, again;
    CHECK(ico_photo_camera(&fed, &out), "basis: the photo camera given back");
    CHECK(memcmp(fed.view, out.view, sizeof(out.view)) == 0,
          "basis: the same camera from the photo camera as from the game's");
    ticks(&p, 10, &out, &game);
    ico_photo_camera(&again, &fed);
    CHECK(memcmp(again.view, out.view, sizeof(out.view)) == 0, "basis: stable over ticks");
    CHECK(fabsf(ico_photo_fov_now() - fov0) < 1e-3f, "fov now: the game's at zoom 1 (%.4f)",
          (double)ico_photo_fov_now());
    /* the field of view now: 2 atan(tan(fov / 2) / zoom) */
    p = idle();
    p.held = ICO_PHOTO_R2;
    ticks(&p, 25, &out, &game);
    const float want = 2.0f * atanf(tanf(fov0 * 0.5f / 57.29578f) / expf(0.35f)) * 57.29578f;
    CHECK(fabsf(ico_photo_fov_now() - want) < 1e-3f &&
              fabsf(ico_photo_fov_now() - ico_photo_fov_deg(&out)) < 1e-3f,
          "fov now: %.4f after a second of R2 (want %.4f, the camera's %.4f)",
          (double)ico_photo_fov_now(), (double)want, (double)ico_photo_fov_deg(&out));
    ico_photo_exit();
    CHECK(ico_photo_set_game(NULL) == 0, "basis: forgotten");

    /* the step: at stick_speed 10 a tick of Fast forward asks 640 and a
       tick of the right stick 36 degrees; each tick moves 250 and turns 25
       at most (under the renderer's cut thresholds, 300 and 30) and still
       goes most of the way */
    writeConfig(path, "version = 1\n[photo]\nstick_speed = 10.0\n");
    ico_photo_enter();
    ico_photo_set_game(&game);
    p = pressed(ICO_PHOTO_R3);
    ico_photo_update(&p);
    CHECK(ico_photo_speed() == ICO_PHOTO_SPEED_FAST, "step: Fast");
    RdCamera prev = game;
    float maxMove = 0.0f, maxTurn = 0.0f, minMove = 1e9f;
    p = idle();
    p.ana[3] = 0;
    for (int i = 0; i < 10; i++) {
        ticks(&p, 1, &out, &game);
        float a[3], b2[3];
        eyeOf(&prev, a);
        eyeOf(&out, b2);
        const float dx = b2[0] - a[0], dy = b2[1] - a[1], dz = b2[2] - a[2];
        const float m = sqrtf(dx * dx + dy * dy + dz * dz);
        maxMove = fmaxf(maxMove, m);
        minMove = fminf(minMove, m);
        prev = out;
    }
    CHECK(maxMove <= 250.0f + 0.05f && minMove > 200.0f,
          "step: the eye moves %.2f .. %.2f a tick (at most 250)", (double)minMove,
          (double)maxMove);
    p = idle();
    p.ana[0] = 255;
    for (int i = 0; i < 10; i++) {
        ticks(&p, 1, &out, &game);
        float c = 0.0f;
        for (int r = 0; r < 3; r++) {
            float pr[3] = {prev.view[r], prev.view[4 + r], prev.view[8 + r]};
            float cr[3] = {out.view[r], out.view[4 + r], out.view[8 + r]};
            c += pr[0] * cr[0] + pr[1] * cr[1] + pr[2] * cr[2];
        }
        const float turnDeg = acosf(fminf(1.0f, (c - 1.0f) * 0.5f)) * 57.29578f;
        maxTurn = fmaxf(maxTurn, turnDeg);
        prev = out;
    }
    CHECK(maxTurn <= 25.0f + 0.05f && maxTurn > 20.0f, "step: the view turns %.3f degrees a tick",
          (double)maxTurn);
    /* Select still jumps back in one tick */
    p = pressed(ICO_PHOTO_SELECT);
    ico_photo_update(&p);
    ico_photo_camera(&out, &game);
    CHECK(memcmp(&out, &game, sizeof(game)) == 0, "step: Select jumps back to the game camera");
    ico_photo_exit();

    /* I17a: the mouse's look */
    {
        writeConfig(path, "version = 1\n[photo]\nstick_speed = 1.0\n");
        IcoPhotoState was, now;
        ico_photo_get(&was);
        ico_photo_mouse_look(10.0f, 10.0f);
        ico_photo_get(&now);
        CHECK(memcmp(&was, &now, sizeof(was)) == 0, "mouse: nothing while inactive");
        ico_photo_enter();
        ico_photo_set_game(&game);
        ico_photo_mouse_look(10.0f, 0.0f);
        ico_photo_get(&now);
        CHECK(fabsf(now.fyaw - 0.1745329f) < 1e-4f && now.fpitch == 0.0f,
              "mouse: 10 degrees of yaw (%.4f)", (double)now.fyaw);
        ico_photo_mouse_look(10.0f, 0.0f);
        ico_photo_get(&now);
        CHECK(fabsf(now.fyaw - 0.3490659f) < 1e-4f, "mouse: the yaw grows (%.4f)",
              (double)now.fyaw);
        for (int i = 0; i < 10; i++) {
            ico_photo_mouse_look(0.0f, 20.0f);
        }
        ico_photo_get(&now);
        CHECK(fabsf(now.fpitch - 1.4835299f) < 1e-3f, "mouse: the pitch stops at 85 (%.4f)",
              (double)now.fpitch);
        ico_photo_mouse_look(0.0f, -200.0f);
        ico_photo_get(&was);
        CHECK(was.fpitch < now.fpitch && was.fpitch >= now.fpitch - 0.4363323f - 1e-3f,
              "mouse: a big move turns 25 degrees at most (%.4f to %.4f)", (double)now.fpitch,
              (double)was.fpitch);
        IcoPhotoPad l3 = pressed(ICO_PHOTO_L3);
        ico_photo_update(&l3);
        CHECK(ico_photo_mode() == ICO_PHOTO_CAM_ORBIT, "mouse: the orbit camera");
        ico_photo_get(&was);
        ico_photo_mouse_look(15.0f, 10.0f);
        ico_photo_get(&now);
        CHECK(fabsf(now.yaw - was.yaw - 0.2617994f) < 1e-4f &&
                  fabsf(now.pitch - was.pitch - 0.1745329f) < 1e-4f,
              "mouse: orbit yaw and pitch (%.4f, %.4f)", (double)(now.yaw - was.yaw),
              (double)(now.pitch - was.pitch));
        CHECK(now.dolly == was.dolly && now.pan == was.pan && now.fyaw == was.fyaw &&
                  now.fpitch == was.fpitch,
              "mouse: orbit leaves the dolly, the pan and the free camera");
        ico_photo_exit();
    }

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
