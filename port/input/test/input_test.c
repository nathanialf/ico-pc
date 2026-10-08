/*
 * port/input/test/input_test.c
 *
 * The input layers on the CPU (no SDL): binding resolution from config
 * text, the dead zone and quantisation, the stick fix, merging, the libpad
 * read buffer against a hand-written DualShock 2 frame, and rumble.
 */
#include <libpad.h>
#include <stdio.h>
#include <string.h>
#include "host_config.h"
#include "input.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static int near_(float a, float b, float tol)
{
    return absf(a - b) <= tol;
}

static int has_key(const unsigned char *row, int key)
{
    int i;

    for (i = 0; i < ICO_BIND_MAX; i++) {
        if (row[i] == key) {
            return 1;
        }
    }
    return 0;
}

static void test_toml(void)
{
    IcoToml *t = ico_toml_parse("top = 1\n"
                                "# comment\n"
                                "[input]\n"
                                "deadzone = 0.2   # trailing\n"
                                "kb.cross = \"Space # not a comment\"\n"
                                "  mouse = false\n"
                                "[input.kb]\n"
                                "circle = [\"E\", 'Enter']\n"
                                "[gameplay]\r\n"
                                "stick_fix = true\r\n"
                                "deadzone = 1\n"
                                "[input]\n"
                                "deadzone = 0.3\n");

    CHECK(t != NULL);
    CHECK(strcmp(ico_toml_get(t, "top"), "1") == 0);
    CHECK(near_((float)ico_toml_get_float(t, "input.deadzone", 0), 0.3f, 1e-6f));
    CHECK(strcmp(ico_toml_get(t, "input.kb.cross"), "Space # not a comment") == 0);
    CHECK(strcmp(ico_toml_get(t, "input.kb.circle"), "[\"E\", 'Enter']") == 0);
    CHECK(ico_toml_get_bool(t, "input.mouse", 1) == 0);
    CHECK(ico_toml_get_bool(t, "gameplay.stick_fix", 0) == 1);
    CHECK(ico_toml_get(t, "input.nothing") == NULL);
    CHECK(ico_toml_get_bool(t, "input.nothing", 1) == 1);
    ico_toml_free(t);
}

static void test_bindings_defaults(void)
{
    IcoBindings b;

    ico_bindings_defaults(&b);
    CHECK(has_key(b.kb[ICO_T_CROSS], ICO_KEY_SPACE));
    CHECK(has_key(b.kb[ICO_T_START], ICO_KEY_RETURN));
    CHECK(has_key(b.kb[ICO_T_SELECT], ICO_KEY_BACKSPACE));
    CHECK(has_key(b.kb[ICO_T_L1], ICO_KEY_TAB) && has_key(b.kb[ICO_T_L1], ICO_KEY_GRAVE));
    CHECK(has_key(b.kb[ICO_T_LSTICK_UP], ICO_KEY_W) && has_key(b.kb[ICO_T_LSTICK_LEFT], ICO_KEY_A));
    CHECK(has_key(b.kb[ICO_T_RSTICK_RIGHT], ICO_KEY_L));
    CHECK(has_key(b.kb[ICO_T_LEFT], ICO_KEY_LEFT));
    CHECK(has_key(b.walk, ICO_KEY_LSHIFT));
    CHECK(has_key(b.mouse[ICO_T_CROSS], 1) && has_key(b.mouse[ICO_T_CIRCLE], 2));
    CHECK(has_key(b.gp[ICO_T_CROSS], ICO_GP_SOUTH) && has_key(b.gp[ICO_T_CIRCLE], ICO_GP_EAST));
    CHECK(has_key(b.gp[ICO_T_SQUARE], ICO_GP_WEST) && has_key(b.gp[ICO_T_TRIANGLE], ICO_GP_NORTH));
    CHECK(has_key(b.gp[ICO_T_L2], ICO_GP_LTRIGGER) && has_key(b.gp[ICO_T_R3], ICO_GP_RSTICK));
    CHECK(has_key(b.gp[ICO_T_START], ICO_GP_START) && has_key(b.gp[ICO_T_SELECT], ICO_GP_BACK));
    CHECK(near_(b.deadzone, 0.12f, 1e-6f) && near_(b.walk_scale, 0.5f, 1e-6f));
    /* package AN-D: Android's Back key joins Start, once, and nothing else
       changes */
    {
        IcoBindings a = b;
        unsigned char row[ICO_BIND_MAX];

#ifdef __ANDROID__
        CHECK(has_key(b.kb[ICO_T_START], ICO_KEY_AC_BACK));
#else
        CHECK(!has_key(b.kb[ICO_T_START], ICO_KEY_AC_BACK));
#endif
        ico_bindings_android_defaults(&a);
        CHECK(has_key(a.kb[ICO_T_START], ICO_KEY_AC_BACK) &&
              has_key(a.kb[ICO_T_START], ICO_KEY_RETURN));
        memcpy(row, a.kb[ICO_T_START], sizeof(row));
        ico_bindings_android_defaults(&a);
        CHECK(memcmp(row, a.kb[ICO_T_START], sizeof(row)) == 0);
        memcpy(a.kb[ICO_T_START], b.kb[ICO_T_START], sizeof(row));
        CHECK(memcmp(a.kb, b.kb, sizeof(a.kb)) == 0 && memcmp(a.gp, b.gp, sizeof(a.gp)) == 0 &&
              memcmp(a.mouse, b.mouse, sizeof(a.mouse)) == 0);
        /* a full row: Back takes the last slot */
        memset(a.kb[ICO_T_START], ICO_KEY_A, sizeof(row));
        ico_bindings_android_defaults(&a);
        CHECK(a.kb[ICO_T_START][ICO_BIND_MAX - 1] == ICO_KEY_AC_BACK &&
              a.kb[ICO_T_START][0] == ICO_KEY_A);
        CHECK(ico_key_from_name("back") == ICO_KEY_AC_BACK);
        CHECK(strcmp(ico_key_name(ICO_KEY_AC_BACK), "Back") == 0);
    }
    /* names round trip */
    CHECK(ico_key_from_name("left shift") == ICO_KEY_LSHIFT);
    CHECK(ico_key_from_name("LSHIFT") == ICO_KEY_LSHIFT);
    CHECK(ico_key_from_name("return") == ICO_KEY_RETURN);
    CHECK(ico_key_from_name("nonsense") == ICO_KEY_NONE);
    CHECK(strcmp(ico_key_name(ico_key_from_name("PageDown")), "PageDown") == 0);
    CHECK(ico_target_from_name("Cross") == ICO_T_CROSS);
    CHECK(ico_target_from_name("rstick_down") == ICO_T_RSTICK_DOWN);
    CHECK(ico_gp_from_name("leftx-") == ICO_GP_LX_NEG);
    CHECK(ICO_T_CROSS == 6 && (1u << ICO_T_CROSS) == ICO_PAD_CROSS);
    CHECK((1u << ICO_T_START) == ICO_PAD_START && (1u << ICO_T_LEFT) == ICO_PAD_LEFT);
}

static void test_bindings_config(void)
{
    IcoBindings b;
    IcoToml *t = ico_toml_parse("[input]\n"
                                "deadzone = 0.25\n"
                                "gamepad = false\n"
                                "kb.cross = \"Enter, X\"\n"
                                "kb.walk = [\"LeftCtrl\"]\n"
                                "mouse_decay = 0.5\n"
                                "[input.pad]\n"
                                "l1 = \"none\"\n"
                                "cross = \"east\"\n"
                                "[input.mouse]\n"
                                "cross = \"\"\n"
                                "[gameplay]\n"
                                "stick_fix = true\n");

    ico_input_set_stick_fix(0);
    ico_bindings_defaults(&b);
    CHECK(ico_input_apply_toml(&b, t) == 0);
    CHECK(near_(b.deadzone, 0.25f, 1e-6f) && b.gamepad == 0 && near_(b.mouse_decay, 0.5f, 1e-6f));
    CHECK(has_key(b.kb[ICO_T_CROSS], ICO_KEY_RETURN) && has_key(b.kb[ICO_T_CROSS], ICO_KEY_X));
    CHECK(!has_key(b.kb[ICO_T_CROSS], ICO_KEY_SPACE)); /* replaced, not added */
    CHECK(has_key(b.walk, ICO_KEY_LCTRL) && !has_key(b.walk, ICO_KEY_LSHIFT));
    CHECK(b.gp[ICO_T_L1][0] == 0);
    CHECK(has_key(b.gp[ICO_T_CROSS], ICO_GP_EAST) && !has_key(b.gp[ICO_T_CROSS], ICO_GP_SOUTH));
    CHECK(b.mouse[ICO_T_CROSS][0] == 0 && has_key(b.mouse[ICO_T_CIRCLE], 2)); /* others kept */
    CHECK(has_key(b.kb[ICO_T_CIRCLE], ICO_KEY_E));                            /* untouched */
    CHECK(ico_input_stick_fix_enabled() == 1);
    ico_toml_free(t);
    ico_input_set_stick_fix(0);

    /* mistakes: reported, the rest applied */
    ico_bindings_defaults(&b);
    CHECK(ico_bindings_set(&b, "kb.cross", "Space, Bogus") == -1);
    CHECK(has_key(b.kb[ICO_T_CROSS], ICO_KEY_SPACE));
    CHECK(ico_bindings_set(&b, "kb.nope", "A") == -1);
    CHECK(ico_bindings_set(&b, "deadzone", "7") == -1 && near_(b.deadzone, 0.12f, 1e-6f));
    CHECK(ico_bindings_set(&b, "bogus", "1") == -1);
    CHECK(ico_bindings_set(&b, "kb.cross", "A,B,C,D,E") == 0 && b.kb[ICO_T_CROSS][3] != 0);
}

static void test_quantise(void)
{
    float x, y;

    CHECK(ico_input_quantise(0.0f) == 128);
    CHECK(ico_input_quantise(-1.0f) == 0);
    CHECK(ico_input_quantise(1.0f) == 255);
    CHECK(ico_input_quantise(-5.0f) == 0 && ico_input_quantise(5.0f) == 255);
    CHECK(ico_input_quantise(0.5f) == 191); /* floor(1.5 * 127.5 + .5) = 191 */
    CHECK(ico_input_quantise(-0.5f) == 64);
    {
        int v, last = -1, ok = 1;

        for (v = -1000; v <= 1000; v++) {
            int q = ico_input_quantise((float)v / 1000.0f);

            ok &= q >= last;
            last = q;
        }
        CHECK(ok);
    }
    /* dead zone: inside the zone is an exact centre; the edge is continuous */
    x = 0.08f;
    y = -0.05f;
    ico_input_deadzone(&x, &y, 0.12f);
    CHECK(x == 0.0f && y == 0.0f);
    CHECK(ico_input_quantise(x) == 128 && ico_input_quantise(y) == 128);
    x = 0.56f;
    y = 0.0f;
    ico_input_deadzone(&x, &y, 0.12f);
    CHECK(near_(x, 0.5f, 1e-4f) && y == 0.0f);
    x = 1.0f;
    y = 0.0f;
    ico_input_deadzone(&x, &y, 0.12f);
    CHECK(near_(x, 1.0f, 1e-5f));
    x = 0.6f;
    y = 0.8f;
    ico_input_deadzone(&x, &y, 0.2f); /* length 1 stays 1, direction kept */
    CHECK(near_(x, 0.6f, 1e-4f) && near_(y, 0.8f, 1e-4f));
}

static void test_stick_fix(void)
{
    static const struct {
        float ratio; /* tan of the angle to the nearest axis */
        float deg;
    } t[] = {{0.0f, 0.0f},      {0.17633f, 10.0f}, {0.41421f, 22.5f},
             {0.57735f, 30.0f}, {0.83910f, 40.0f}, {1.0f, 45.0f}};

    unsigned i;
    float x, y;

    /* the stick fix table: scale = 1 + 0.2 * d / 45 */
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        float s = ico_input_stick_fix_scale(1.0f, t[i].ratio);

        CHECK(near_(s, 1.0f + 0.2f * t[i].deg / 45.0f, 0.002f));
        s = ico_input_stick_fix_scale(-t[i].ratio, -1.0f); /* the other octants */
        CHECK(near_(s, 1.0f + 0.2f * t[i].deg / 45.0f, 0.002f));
    }
    /* axis aligned: unchanged */
    x = 0.7f;
    y = 0.0f;
    ico_input_stick_fix(&x, &y);
    CHECK(x == 0.7f && y == 0.0f);
    x = 0.0f;
    y = -1.0f;
    ico_input_stick_fix(&x, &y);
    CHECK(x == 0.0f && y == -1.0f);
    /* 45 degrees: magnitude x1.2 */
    x = 0.4f;
    y = 0.4f;
    ico_input_stick_fix(&x, &y);
    CHECK(near_(x, 0.48f, 0.002f) && near_(y, 0.48f, 0.002f));
    x = -0.4f;
    y = 0.4f;
    ico_input_stick_fix(&x, &y);
    CHECK(near_(x, -0.48f, 0.002f) && near_(y, 0.48f, 0.002f));
    /* capped to the square, direction kept, never shrunk */
    x = 0.9f;
    y = 0.9f;
    ico_input_stick_fix(&x, &y);
    CHECK(near_(x, 1.0f, 1e-5f) && near_(y, 1.0f, 1e-5f));
    x = 1.0f;
    y = 0.5f;
    ico_input_stick_fix(&x, &y);
    CHECK(x == 1.0f && y == 0.5f); /* component already at the edge: scale 1 */
    x = 0.8f;
    y = 0.4f;
    {
        float before = x * x + y * y;

        ico_input_stick_fix(&x, &y);
        CHECK(x * x + y * y >= before);
        CHECK(near_(y / x, 0.5f, 1e-4f));
        CHECK(x <= 1.0f);
    }
    /* what the game does with it: iosPadNormalizeStick divides the length by
       1 + 0.2 * d / 45 with d the whole degrees folded to 0..45; the fix
       leaves the length within 0.5 percent of the input, never below it */
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        float s = ico_input_stick_fix_scale(1.0f, t[i].ratio);
        int whole = (int)t[i].deg; /* the game truncates (an int degree) */
        float game = 1.0f + (float)whole * 0.2f / 45.0f;
        float net = s / game;

        CHECK(net >= 0.998f && net <= 1.0f + 0.2f / 45.0f + 0.002f);
    }
}

static void test_merge(void)
{
    IcoVirtualPad a, b;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.buttons = ICO_PAD_CROSS;
    b.buttons = ICO_PAD_START | ICO_PAD_CROSS;
    a.lx = 0.3f;
    a.ly = 0.3f;
    b.lx = -0.9f;
    b.ly = 0.0f;
    a.rx = 0.5f;
    b.rx = 0.2f;
    b.ry = 0.2f;
    ico_vpad_merge(&a, &b);
    CHECK(a.buttons == (ICO_PAD_CROSS | ICO_PAD_START));
    CHECK(a.lx == -0.9f && a.ly == 0.0f); /* the larger magnitude */
    CHECK(a.rx == 0.5f && a.ry == 0.0f);
    /* a tie keeps dst */
    a.lx = 0.5f;
    a.ly = 0.0f;
    b.lx = 0.0f;
    b.ly = 0.5f;
    ico_vpad_merge(&a, &b);
    CHECK(a.lx == 0.5f && a.ly == 0.0f);
}

static void blank(IcoInputRaw *r)
{
    memset(r, 0, sizeof(*r));
}

static void test_step(void)
{
    IcoBindings b;
    IcoInputRaw r;
    IcoVirtualPad v;
    int i;

    ico_bindings_defaults(&b);
    blank(&r);
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == 0 && v.lx == 0 && v.ly == 0 && v.rx == 0 && v.ry == 0);
    CHECK(ico_input_quantise(v.lx) == 128);

    /* keyboard: W is stick up at full deflection, Shift walks at 0.5 */
    r.key[ICO_KEY_W] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.lx == 0.0f && v.ly == -1.0f);
    r.key[ICO_KEY_LSHIFT] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.ly == -0.5f);
    CHECK(ico_input_quantise(v.ly) == 64);
    r.key[ICO_KEY_D] = 1; /* diagonal: a unit vector, not the square's corner */
    r.key[ICO_KEY_LSHIFT] = 0;
    ico_bindings_step(&b, &r, &v);
    CHECK(near_(v.lx, 0.7071f, 1e-3f) && near_(v.ly, -0.7071f, 1e-3f));
    r.key[ICO_KEY_S] = 1; /* opposite keys cancel */
    ico_bindings_step(&b, &r, &v);
    CHECK(v.ly == 0.0f && v.lx == 1.0f);
    blank(&r);
    /* buttons and the right stick, d-pad */
    r.key[ICO_KEY_SPACE] = 1;
    r.key[ICO_KEY_RETURN] = 1;
    r.key[ICO_KEY_LEFT] = 1;
    r.key[ICO_KEY_J] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == (ICO_PAD_CROSS | ICO_PAD_START | ICO_PAD_LEFT));
    CHECK(v.rx == -1.0f && v.ry == 0.0f);
    blank(&r);
    r.key[ICO_KEY_TAB] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == ICO_PAD_L1);
    blank(&r);
    r.key[ICO_KEY_BACKSPACE] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == ICO_PAD_SELECT);

    /* mouse buttons */
    blank(&r);
    r.mouse[1] = 1;
    r.mouse[2] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == (ICO_PAD_CROSS | ICO_PAD_CIRCLE));

    /* mouse motion drives the right stick and decays to centre */
    blank(&r);
    r.mouse_dx = 40.0f; /* 40 counts * 0.015 = 0.6 */
    ico_bindings_step(&b, &r, &v);
    CHECK(near_(v.rx, 0.6f, 1e-4f) && v.ry == 0.0f);
    r.mouse_dx = 0.0f;
    ico_bindings_step(&b, &r, &v);
    CHECK(near_(v.rx, 0.48f, 1e-4f)); /* decay 0.8 per vsync */
    for (i = 0; i < 60; i++) {
        ico_bindings_step(&b, &r, &v);
    }
    CHECK(absf(v.rx) < 0.0001f);
    r.mouse_dx = 5000.0f; /* a huge flick clamps to the unit circle */
    r.mouse_dy = 5000.0f;
    ico_bindings_step(&b, &r, &v);
    CHECK(near_(v.rx * v.rx + v.ry * v.ry, 1.0f, 1e-3f) && v.rx > 0 && v.ry > 0);
    r.mouse_dx = r.mouse_dy = 0.0f;
    for (i = 0; i < 80; i++) {
        ico_bindings_step(&b, &r, &v);
    }
    CHECK(absf(v.rx) < 0.0001f && absf(v.ry) < 0.0001f);
    /* mouse off */
    b.mouse_on = 0;
    r.mouse_dx = 40.0f;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.rx == 0.0f);
    b.mouse_on = 1;

    /* gamepad: dead zone, deflection, buttons, triggers */
    blank(&r);
    r.gamepads = 1;
    r.axis[0] = 0.08f; /* inside 0.12 */
    r.axis[1] = -0.05f;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.lx == 0.0f && v.ly == 0.0f);
    CHECK(ico_input_quantise(v.lx) == 128 && ico_input_quantise(v.ly) == 128);
    r.axis[0] = 1.0f;
    r.axis[1] = 0.0f;
    r.axis[3] = -1.0f;
    r.gp[ICO_GP_SOUTH] = 1.0f;
    r.gp[ICO_GP_LTRIGGER] = 0.7f;
    r.gp[ICO_GP_RTRIGGER] = 0.3f; /* under the threshold */
    r.gp[ICO_GP_DPUP] = 1.0f;
    ico_bindings_step(&b, &r, &v);
    CHECK(near_(v.lx, 1.0f, 1e-4f) && near_(v.ry, -1.0f, 1e-4f));
    CHECK(v.buttons == (ICO_PAD_CROSS | ICO_PAD_L2 | ICO_PAD_UP));
    /* a stick direction as a source for a button (config can do it) */
    ico_bindings_set(&b, "pad.square", "leftx+");
    ico_bindings_step(&b, &r, &v);
    CHECK((v.buttons & ICO_PAD_SQUARE) != 0);

    /* merged: pad and keyboard together; buttons OR, the larger stick wins */
    blank(&r);
    r.gamepads = 1;
    r.axis[0] = 0.45f; /* after the dead zone: 0.375; under the 50 % button threshold */
    r.gp[ICO_GP_EAST] = 1.0f;
    r.key[ICO_KEY_SPACE] = 1;
    r.key[ICO_KEY_A] = 1; /* left, full: bigger than the pad's */
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == (ICO_PAD_CIRCLE | ICO_PAD_CROSS));
    CHECK(v.lx == -1.0f);
    r.key[ICO_KEY_A] = 0;
    ico_bindings_step(&b, &r, &v);
    CHECK(near_(v.lx, 0.375f, 1e-3f));

    /* disabled devices */
    b.keyboard = 0;
    r.key[ICO_KEY_A] = 1;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == ICO_PAD_CIRCLE && v.lx > 0);
    b.gamepad = 0;
    ico_bindings_step(&b, &r, &v);
    CHECK(v.buttons == 0 && v.lx == 0.0f);
}

static void test_frame(void)
{
    IcoVirtualPad v;
    IcoPadFrame f;

    memset(&v, 0, sizeof(v));
    v.lx = -1.0f;
    v.ly = 0.4f;
    v.rx = 0.4f;
    v.ry = 0.4f;
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 0 && f.ly == 179 && f.rx == 179 && f.ry == 179);
    ico_input_vpad_to_frame(&v, 1, 0, &f); /* the fix: 0.4 -> 0.48 at 45 degrees */
    CHECK(f.rx == 189 && f.ry == 189);
    CHECK(f.lx == 0);                      /* an x of -1 and y 0.4 is capped: already at the edge */
    ico_input_vpad_to_frame(&v, 0, 1, &f); /* mirror: X negated, both sticks */
    CHECK(f.lx == 255 && f.ly == 179 && f.rx == 77 && f.ry == 179);
    memset(&v, 0, sizeof(v));
    ico_input_vpad_to_frame(&v, 1, 1, &f); /* idle stays 128 under both */
    CHECK(f.lx == 128 && f.ly == 128 && f.rx == 128 && f.ry == 128);
}

static void expect_frame(const unsigned char *got, const unsigned char *want, int line)
{
    if (memcmp(got, want, 32) != 0) {
        int i;

        fprintf(stderr, "FAIL line %d: pad buffer differs:\n", line);
        for (i = 0; i < 32; i++) {
            if (got[i] != want[i]) {
                fprintf(stderr, "  byte %d: got %02x want %02x\n", i, got[i], want[i]);
            }
        }
        failures++;
    }
}

static void test_libpad(void)
{
    unsigned char dma[256], d[32];
    IcoVirtualPad v;
    unsigned short hi, lo;
    unsigned char align[6] = {0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
    unsigned char act[6];

    ico_pad_script_clear();
    ico_input_set_live(0);
    ico_input_set_stick_fix(0);
    ico_input_set_mirror(0);
    CHECK(scePadInit(0) == 1);
    CHECK(scePadPortOpen(0, 0, dma) == 1);
    /* nothing feeding it: no controller */
    CHECK(scePadGetState(0, 0) == 0 && scePadRead(0, 0, d) == 0);
    CHECK(scePadInfoMode(0, 0, 1, 0) == 0 && scePadSetMainMode(0, 0, 1, 3) == 0);

    ico_input_set_live(1);
    CHECK(scePadGetState(0, 0) == 6 && scePadGetState(1, 0) == 0);
    CHECK(scePadRead(1, 0, d) == 0);
    /* the DualShock 2 power-up and pad.c's walk to analog */
    CHECK(scePadInfoMode(0, 0, 1, 0) == 4);
    CHECK(scePadInfoMode(0, 0, 2, 0) == 0);
    CHECK(scePadInfoMode(0, 0, 4, -1) == 2);
    CHECK(scePadSetMainMode(0, 0, 1, 3) == 1 && scePadGetReqState(0, 0) == 0);
    CHECK(scePadInfoMode(0, 0, 1, 0) == 7);
    CHECK(scePadInfoPressMode(0, 0) == 0);
    CHECK(scePadInfoAct(0, 0, -1, 0) == 2);
    CHECK(scePadSetActAlign(0, 0, (char *)align) == 1);

    memset(&v, 0, sizeof(v));
    v.buttons = ICO_PAD_CROSS | ICO_PAD_START | ICO_PAD_LEFT;
    v.lx = 1.0f;
    v.ly = -1.0f;
    v.rx = 0.0f;
    v.ry = 1.0f;
    ico_input_set_vpad(&v);
    memset(d, 0xEE, sizeof(d));
    CHECK(scePadRead(0, 0, d) == 32);
    {
        /* analog DualShock 2, hand written: ok, id 0x73, buttons ~0x8840 as
           byte 2 = ~0x88 = 0x77 and byte 3 = ~0x40 = 0xBF (active low), then
           right x, right y, left x, left y */
        unsigned char want[32] = {0x00, 0x73, 0x77, 0xBF, 128, 255, 255, 0};

        /* hand-written: START 0x0800, LEFT 0x8000 in byte 2 high: 0x88 */
        expect_frame(d, want, __LINE__);
    }
    /* pad.c's own decode of that: ((hi << 8) | lo) ^ 0xFFFF is the logical word */
    CHECK((unsigned)(((d[2] << 8) | d[3]) ^ 0xFFFF) ==
          (ICO_PAD_CROSS | ICO_PAD_START | ICO_PAD_LEFT));

    /* digital mode: id 0x41, sticks centred */
    CHECK(scePadSetMainMode(0, 0, 0, 2) == 1);
    CHECK(scePadRead(0, 0, d) == 32);
    {
        unsigned char want[32] = {0x00, 0x41, 0x77, 0xBF, 128, 128, 128, 128};

        expect_frame(d, want, __LINE__);
    }
    CHECK(scePadInfoMode(0, 0, 1, 0) == 4);

    /* analog with pressure (id 0x79): bytes 8-19 are right left up down
       triangle circle cross square L1 R1 L2 R2 */
    CHECK(scePadSetMainMode(0, 0, 1, 3) == 1);
    CHECK(scePadEnterPressMode(0, 0) == 1);
    v.buttons = ICO_PAD_CROSS | ICO_PAD_LEFT | ICO_PAD_R2;
    ico_input_set_vpad(&v);
    CHECK(scePadRead(0, 0, d) == 32);
    {
        unsigned char want[32] = {0x00, 0x79, 0x7F, 0xBF, 128, 255, 255, 0};

        want[2] = (unsigned char)(~(ICO_PAD_LEFT >> 8));
        want[3] = (unsigned char)~(ICO_PAD_CROSS | ICO_PAD_R2);
        want[9] = 0xFF;  /* left */
        want[14] = 0xFF; /* cross */
        want[19] = 0xFF; /* R2 */
        expect_frame(d, want, __LINE__);
    }
    CHECK(scePadInit(0) == 1); /* power cycle: back to digital, no pressure */
    CHECK(scePadRead(0, 0, d) == 32 && d[1] == 0x41);
    CHECK(scePadSetMainMode(0, 0, 1, 3) == 1);

    /* a script wins over live sources, and is not mirrored or fixed */
    ico_input_set_mirror(1);
    CHECK(ico_pad_script_parse("0 0001 10 20 30 40\n", "t") == 0);
    scePadRead(0, 0, d);
    CHECK(d[1] == 0x73 && d[3] == 0xFE && d[4] == 30 && d[5] == 40 && d[6] == 10 && d[7] == 20);
    ico_pad_script_clear();
    scePadRead(0, 0, d);
    CHECK(d[6] == 0 && d[7] == 0); /* live again, mirrored: lx 1 -> 0, ly -1 -> 0 */
    CHECK(d[4] == 128);            /* rx 0 stays at centre mirrored */
    ico_input_set_mirror(0);

    /* rumble: SetActAlign decides which data byte drives which motor */
    CHECK(scePadSetActAlign(0, 0, (char *)align) == 1);
    memset(act, 0, sizeof(act));
    act[0] = 1;
    act[1] = 128;
    CHECK(scePadSetActDirect(0, 0, act) == 1);
    ico_input_rumble_get(&hi, &lo);
    CHECK(hi == 0xFFFF && lo == 128 * 257);
    act[0] = 0;
    act[1] = 255;
    scePadSetActDirect(0, 0, act);
    ico_input_rumble_get(&hi, &lo);
    CHECK(hi == 0 && lo == 0xFFFF);
    act[1] = 0;
    scePadSetActDirect(0, 0, act);
    ico_input_rumble_get(&hi, &lo);
    CHECK(hi == 0 && lo == 0);
    CHECK(scePadSetActDirect(1, 0, act) == 0);
    {
        unsigned char swapped[6] = {1, 0, 0xFF, 0xFF, 0xFF, 0xFF};
        unsigned char both[6] = {200, 1, 0, 0, 0, 0};

        ico_pad_rumble_map(swapped, both, &hi, &lo);
        CHECK(hi == 0xFFFF && lo == 200 * 257);
        ico_pad_rumble_map(align, both, &hi, &lo);
        CHECK(hi == 0xFFFF && lo == 257);
        memset(both, 0, sizeof(both));
        ico_pad_rumble_map(align, both, &hi, &lo);
        CHECK(hi == 0 && lo == 0);
        memset(swapped, 0xFF, sizeof(swapped)); /* no alignment: no rumble */
        both[0] = both[1] = 255;
        ico_pad_rumble_map(swapped, both, &hi, &lo);
        CHECK(hi == 0 && lo == 0);
    }
    /* with the pad unplugged the motors go quiet */
    act[0] = 1;
    scePadSetActDirect(0, 0, act);
    ico_input_set_live(0);
    ico_input_rumble_get(&hi, &lo);
    CHECK(hi == 0 && lo == 0);
    CHECK(scePadSetActDirect(0, 0, act) == 0);
}

int main(void)
{
    test_toml();
    test_bindings_defaults();
    test_bindings_config();
    test_quantise();
    test_stick_fix();
    test_merge();
    test_step();
    test_frame();
    test_libpad();
    if (failures != 0) {
        fprintf(stderr, "input_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("input_test: ok\n");
    return 0;
}
