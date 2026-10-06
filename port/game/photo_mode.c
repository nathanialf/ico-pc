/*
 * port/game/photo_mode.c
 *
 * Photo mode's state and camera (photo_mode.h).  Port state only: the game never reads it.
 */
#include "photo_mode.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#define PI_F 3.14159265358979f
#define DEG(d) ((d) * PI_F / 180.0f)

/* rates at stick_speed 1, per second at full deflection */
#define YAW_RATE DEG(90.0f)
#define PITCH_RATE DEG(60.0f)
#define ROLL_RATE DEG(45.0f)
#define DOLLY_RATE 1.2f /* the distance's log */
#define PAN_RATE 0.6f   /* the distance's fraction */
#define ZOOM_RATE 0.8f  /* the magnification's log */
#define DEAD_ZONE 0.12f
#define MAX_ELEVATION DEG(85.0f)
#define MIN_DOLLY 0.05f
#define MAX_DOLLY 10.0f
#define MIN_FOCUS 50.0f
#define MAX_FOCUS 5000.0f
#define MIN_FOV 10.0f
#define MAX_FOV 100.0f

static struct {
    IcoPhotoState st;
    int tickHz;
    int capturePending;
    float speed;
    int invertY;
    char pngDir[256];
    float fovGame; /* the game camera's vertical field of view at the last ico_photo_camera */
    float elev0;   /* and the elevation of its forward axis */
    int haveElev;
    int haveSubject;
    float subject[3];
} s;

void ico_photo_reset(void)
{
    memset(&s, 0, sizeof(s));
}

void ico_photo_set_tick_hz(int hz)
{
    s.tickHz = hz > 0 ? hz : 0;
}

static void readConfig(void)
{
    s.speed = (float)ico_config_get_float("photo.stick_speed", 1.0);
    if (!(s.speed > 0.0f)) {
        s.speed = 1.0f;
    }
    if (s.speed > 10.0f) {
        s.speed = 10.0f;
    }
    s.invertY = ico_config_get_bool("photo.invert_y", 0);
    const char *d = ico_config_get_string("photo.png_dir", "screenshots");
    snprintf(s.pngDir, sizeof(s.pngDir), "%s", d && d[0] ? d : "screenshots");
}

float ico_photo_stick_speed(void)
{
    return s.speed > 0.0f ? s.speed : (float)ico_config_get_float("photo.stick_speed", 1.0);
}

int ico_photo_invert_y(void)
{
    return s.invertY;
}

const char *ico_photo_png_dir(void)
{
    if (!s.pngDir[0]) {
        readConfig();
    }
    return s.pngDir;
}

void ico_photo_enter(void)
{
    const int hud = s.st.active ? s.st.hud : 1;
    readConfig();
    memset(&s.st, 0, sizeof(s.st));
    s.st.active = 1;
    s.st.hud = hud;
    s.st.dolly = 1.0f;
    s.st.zoom = 1.0f;
    s.capturePending = 0;
    fprintf(stderr, "photo: enter (stick_speed %.2f, invert_y %d, png_dir \"%s\")\n",
            (double)s.speed, s.invertY, s.pngDir);
}

void ico_photo_exit(void)
{
    if (!s.st.active) {
        return;
    }
    fprintf(stderr,
            "photo: exit (%u captures; the camera at yaw %.1f, pitch %.1f, roll %.1f degrees, "
            "dolly %.2f, pan %.2f, magnification %.2f)\n",
            s.st.captures, (double)(s.st.yaw * 180.0f / PI_F), (double)(s.st.pitch * 180.0f / PI_F),
            (double)(s.st.roll * 180.0f / PI_F), (double)s.st.dolly, (double)s.st.pan,
            (double)s.st.zoom);
    s.st.active = 0;
    s.capturePending = 0;
}

void ico_photo_set_subject(const float *pos)
{
    s.haveSubject = pos != NULL;
    if (pos) {
        memcpy(s.subject, pos, sizeof(s.subject));
    }
}

int ico_photo_active(void)
{
    return s.st.active;
}

int ico_photo_hud(void)
{
    return s.st.active && s.st.hud;
}

void ico_photo_get(IcoPhotoState *out)
{
    if (out) {
        *out = s.st;
    }
}

int ico_photo_take_capture(void)
{
    if (s.capturePending > 0) {
        s.capturePending--;
        return 1;
    }
    return 0;
}

static float axis(unsigned char b)
{
    float v = ((float)b - 128.0f) / 127.0f;
    v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    if (fabsf(v) < DEAD_ZONE) {
        return 0.0f;
    }
    /* rescaled past the dead zone, so a small push starts from 0 */
    return (v > 0.0f ? v - DEAD_ZONE : v + DEAD_ZONE) / (1.0f - DEAD_ZONE);
}

int ico_photo_update(const IcoPhotoPad *pad)
{
    if (!s.st.active || !pad) {
        return 0;
    }
    if (pad->pressed & (ICO_PHOTO_TRIANGLE | ICO_PHOTO_CIRCLE | ICO_PHOTO_START)) {
        return 1;
    }
    const float dt = 1.0f / (float)(s.tickHz > 0 ? s.tickHz : 25);
    const float k = s.speed > 0.0f ? s.speed : 1.0f;
    const float rx = axis(pad->ana[0]), ry = axis(pad->ana[1]);
    const float lx = axis(pad->ana[2]), ly = axis(pad->ana[3]);
    IcoPhotoState *st = &s.st;
    st->yaw += lx * YAW_RATE * k * dt;
    /* up on the stick (ly < 0) raises the camera */
    st->pitch += (s.invertY ? ly : -ly) * PITCH_RATE * k * dt;
    if (s.haveElev) {
        const float hi = s.elev0 + MAX_ELEVATION, lo = s.elev0 - MAX_ELEVATION;
        st->pitch = st->pitch > hi ? hi : (st->pitch < lo ? lo : st->pitch);
    }
    st->dolly *= expf(ry * DOLLY_RATE * k * dt);
    st->dolly = st->dolly < MIN_DOLLY ? MIN_DOLLY : (st->dolly > MAX_DOLLY ? MAX_DOLLY : st->dolly);
    st->pan += rx * st->dolly * PAN_RATE * k * dt;
    if (pad->held & ICO_PHOTO_L1) {
        st->roll -= ROLL_RATE * dt;
    }
    if (pad->held & ICO_PHOTO_R1) {
        st->roll += ROLL_RATE * dt;
    }
    if (st->roll > PI_F) {
        st->roll -= 2.0f * PI_F;
    } else if (st->roll < -PI_F) {
        st->roll += 2.0f * PI_F;
    }
    float z = 0.0f;
    if (pad->held & (ICO_PHOTO_R2 | ICO_PHOTO_UP)) {
        z += 1.0f;
    }
    if (pad->held & (ICO_PHOTO_L2 | ICO_PHOTO_DOWN)) {
        z -= 1.0f;
    }
    st->zoom *= expf(z * ZOOM_RATE * dt);
    /* the field of view between MIN_FOV and MAX_FOV, from the game's last
       seen (the window computes it; before that, a 0.25 .. 4 range) */
    float lo = 0.25f, hi = 4.0f;
    if (s.fovGame > 0.0f) {
        const float t0 = tanf(DEG(s.fovGame) * 0.5f);
        hi = t0 / tanf(DEG(MIN_FOV) * 0.5f);
        lo = t0 / tanf(DEG(MAX_FOV) * 0.5f);
    }
    st->zoom = st->zoom < lo ? lo : (st->zoom > hi ? hi : st->zoom);
    if (pad->pressed & ICO_PHOTO_SELECT) {
        st->yaw = st->pitch = st->roll = st->pan = 0.0f;
        st->dolly = 1.0f;
        st->zoom = 1.0f;
    }
    if (pad->pressed & ICO_PHOTO_SQUARE) {
        st->hud = !st->hud;
    }
    if (pad->pressed & ICO_PHOTO_CROSS) {
        s.capturePending++;
        st->captures++;
        fprintf(stderr, "photo: capture %u asked\n", st->captures);
    }
    return 0;
}

/* ------------------------------------------------------------ the camera */

typedef struct {
    float x, y, z;
} V3;

static V3 v3(float x, float y, float z)
{
    V3 r = {x, y, z};
    return r;
}

static float dot(V3 a, V3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static V3 cross(V3 a, V3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static V3 add(V3 a, V3 b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static V3 scale(V3 a, float k)
{
    return v3(a.x * k, a.y * k, a.z * k);
}

static V3 unit(V3 a)
{
    const float n = sqrtf(dot(a, a));
    return n > 0.0f ? scale(a, 1.0f / n) : a;
}

/* v turned by a about the unit axis u (Rodrigues) */
static V3 turn(V3 v, V3 u, float a)
{
    const float c = cosf(a), sn = sinf(a);
    return add(add(scale(v, c), scale(cross(u, v), sn)), scale(u, dot(u, v) * (1.0f - c)));
}

/* the rows of a column-major view's rotation: x_v = R (x - eye) */
static V3 row(const float *v, int i)
{
    return v3(v[i], v[4 + i], v[8 + i]);
}

float ico_photo_fov_deg(const RdCamera *cam)
{
    const float fy = cam ? fabsf(cam->proj43[5]) : 0.0f;
    return fy > 0.0f ? 2.0f * atanf(ICO_PHOTO_HALF_H / fy) * 180.0f / PI_F : 0.0f;
}

int ico_photo_camera(RdCamera *out, const RdCamera *game)
{
    if (!out || !game) {
        return 0;
    }
    const float *v = game->view;
    const V3 r0 = row(v, 0), y0 = row(v, 1), f0 = row(v, 2);
    const V3 rn = unit(r0), fn0 = unit(f0);
    if (!(dot(r0, r0) > 0.0f) || !(dot(f0, f0) > 0.0f) || !(dot(y0, y0) > 0.0f)) {
        return 0;
    }
    s.fovGame = ico_photo_fov_deg(game);
    const IcoPhotoState *st = &s.st;
    if (st->yaw == 0.0f && st->pitch == 0.0f && st->roll == 0.0f && st->pan == 0.0f &&
        st->dolly == 1.0f && st->zoom == 1.0f) {
        *out = *game;
        return 1;
    }
    /* the eye: -R^T t (rows of a rotation; the view is one to 1e-4) */
    const V3 t = v3(v[12], v[13], v[14]);
    const V3 eye0 = scale(add(add(scale(r0, t.x), scale(y0, t.y)), scale(f0, t.z)), -1.0f);
    /* the pivot: on the forward axis, nearest the subject */
    float focus = ICO_PHOTO_FOCUS;
    if (s.haveSubject) {
        const float d =
            dot(v3(s.subject[0] - eye0.x, s.subject[1] - eye0.y, s.subject[2] - eye0.z), fn0);
        focus = d >= MIN_FOCUS && d <= MAX_FOCUS ? d : focus;
    }
    const V3 pivot0 = add(eye0, scale(fn0, focus));
    /* the world's vertical: the axis nearest the picture's up (view -y when
       the projection puts +y down the screen, as the GS does) */
    const V3 upv = game->proj43[5] >= 0.0f ? scale(unit(y0), -1.0f) : unit(y0);
    V3 w = v3(0.0f, 0.0f, 0.0f);
    if (fabsf(upv.x) >= fabsf(upv.y) && fabsf(upv.x) >= fabsf(upv.z)) {
        w.x = upv.x >= 0.0f ? 1.0f : -1.0f;
    } else if (fabsf(upv.y) >= fabsf(upv.z)) {
        w.y = upv.y >= 0.0f ? 1.0f : -1.0f;
    } else {
        w.z = upv.z >= 0.0f ? 1.0f : -1.0f;
    }
    /* pitch: about the camera's right, the sign that raises the eye (turns
       the forward axis away from the vertical), the elevation clamped */
    const float e0 = asinf(fmaxf(-1.0f, fminf(1.0f, dot(fn0, w))));
    /* the eye's elevation above the pivot is -e0 + pitch: kept within
       MAX_ELEVATION of the horizontal (update clamps the state the same) */
    s.elev0 = e0;
    s.haveElev = 1;
    float pitch = st->pitch;
    pitch = pitch > e0 + MAX_ELEVATION ? e0 + MAX_ELEVATION : pitch;
    pitch = pitch < e0 - MAX_ELEVATION ? e0 - MAX_ELEVATION : pitch;
    const float pSign = dot(turn(fn0, rn, 0.01f), w) < dot(fn0, w) ? 1.0f : -1.0f;
    /* yaw: about the vertical, the sign that moves the eye to the right */
    const V3 eyeDir = scale(fn0, -1.0f);
    const float ySign = dot(turn(eyeDir, w, 0.01f), rn) > 0.0f ? 1.0f : -1.0f;
    V3 r = turn(r0, rn, pSign * pitch), y = turn(y0, rn, pSign * pitch),
       f = turn(f0, rn, pSign * pitch);
    r = turn(r, w, ySign * st->yaw);
    y = turn(y, w, ySign * st->yaw);
    f = turn(f, w, ySign * st->yaw);
    const V3 fn = unit(f);
    r = turn(r, fn, st->roll);
    y = turn(y, fn, st->roll);
    const V3 pivot = add(pivot0, scale(unit(turn(rn, w, ySign * st->yaw)), st->pan * focus));
    const V3 eye = add(pivot, scale(fn, -st->dolly * focus));
    *out = *game;
    const V3 rows[3] = {r, y, f};
    for (int i = 0; i < 3; i++) {
        out->view[i] = rows[i].x;
        out->view[4 + i] = rows[i].y;
        out->view[8 + i] = rows[i].z;
        out->view[12 + i] = -dot(rows[i], eye);
    }
    /* the projection narrowed by zoom about the picture's centre (the image
       of the forward axis: column 2 over its w) */
    const float *p = game->proj43;
    const float k = st->zoom > 0.0f ? st->zoom : 1.0f;
    if (k != 1.0f && p[11] != 0.0f) {
        const float cx = p[8] / p[11], cy = p[9] / p[11];
        for (int c = 0; c < 4; c++) {
            out->proj43[c * 4 + 0] = k * p[c * 4 + 0] + (1.0f - k) * cx * p[c * 4 + 3];
            out->proj43[c * 4 + 1] = k * p[c * 4 + 1] + (1.0f - k) * cy * p[c * 4 + 3];
        }
        out->zoom = game->zoom * k;
    }
    out->cut = 0;
    return 1;
}

void ico_photo_file_name(char *buf, unsigned size, int year, int mon, int day, int hour, int min,
                         int sec, int seq)
{
    if (!buf || !size) {
        return;
    }
    if (seq > 1) {
        snprintf(buf, size, "ico-%04d%02d%02d-%02d%02d%02d-%d.png", year, mon, day, hour, min, sec,
                 seq);
    } else {
        snprintf(buf, size, "ico-%04d%02d%02d-%02d%02d%02d.png", year, mon, day, hour, min, sec);
    }
}
