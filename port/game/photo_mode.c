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
#define ZOOM_RATE 0.35f /* the magnification's log */
#define DEAD_ZONE 0.12f
#define MAX_ELEVATION DEG(85.0f)
#define MIN_DOLLY 0.05f
#define MAX_DOLLY 10.0f
#define MIN_FOCUS 50.0f
#define MAX_FOCUS 5000.0f
#define MIN_FOV 10.0f
#define MAX_FOV 100.0f
/* the most a tick's stick input moves the eye and turns the view: under the
   renderer's camera cut thresholds (rd_internal.h RD_INTERP_CAMERA_MOVE 300,
   RD_INTERP_CAMERA_TURN 30 degrees), so the presenter blends every step of a
   fast move instead of snapping to it */
#define STEP_MOVE 250.0f
#define STEP_TURN DEG(25.0f)

/* ------------------------------------------------------------ vectors */

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

static V3 sub(V3 a, V3 b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
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

/* the game camera's basis (ico_photo_set_game's, else the last
   ico_photo_camera's): what the cameras are built from and the free
   camera's moves in ico_photo_update are measured in */
typedef struct {
    V3 r0, y0, f0; /* the game view's rows */
    V3 rn, fn0;    /* the camera's right and forward, unit */
    V3 w;          /* the world's vertical nearest the picture's up */
    V3 eye0;       /* the eye */
    float e0;      /* the forward axis' elevation above the horizontal */
    float pUp;     /* the turn about rn that raises the forward axis: +1 or -1 */
    float yRt;     /* the turn about w that swings it to the right: +1 or -1 */
} Basis;

static struct {
    IcoPhotoState st;
    int tickHz;
    int capturePending;
    float speed;
    int invertY;
    char pngDir[256];
    int hideUi;    /* [photo] hide_ui, and Square's last choice */
    float fovGame; /* the game camera's vertical field of view at the last ico_photo_camera */
    Basis b;       /* and its basis */
    int haveBasis;
    int fixedBasis; /* the basis and fovGame from ico_photo_set_game, for the session */
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
    s.hideUi = ico_config_get_bool("photo.hide_ui", 0);
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

int ico_photo_hide_ui(void)
{
    return s.hideUi;
}

int ico_photo_mode(void)
{
    return s.st.mode;
}

int ico_photo_speed(void)
{
    return s.st.speed;
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
    readConfig();
    memset(&s.st, 0, sizeof(s.st));
    s.st.active = 1;
    s.st.hud = !s.hideUi;
    s.st.mode = ICO_PHOTO_CAM_FREE;
    s.st.speed = ICO_PHOTO_SPEED_NORMAL;
    s.st.dolly = 1.0f;
    s.st.zoom = 1.0f;
    s.capturePending = 0;
    s.fixedBasis = 0;
    fprintf(stderr, "photo: enter (stick_speed %.2f, invert_y %d, hide_ui %d, png_dir \"%s\")\n",
            (double)s.speed, s.invertY, s.hideUi, s.pngDir);
}

void ico_photo_exit(void)
{
    if (!s.st.active) {
        return;
    }
    fprintf(stderr,
            "photo: exit (%u captures, the %s camera; orbit at yaw %.1f, pitch %.1f, roll %.1f "
            "degrees, dolly %.2f, pan %.2f; free at yaw %.1f, pitch %.1f, roll %.1f degrees, "
            "moved (%.0f, %.0f, %.0f); magnification %.2f)\n",
            s.st.captures, s.st.mode == ICO_PHOTO_CAM_FREE ? "free" : "orbit",
            (double)(s.st.yaw * 180.0f / PI_F), (double)(s.st.pitch * 180.0f / PI_F),
            (double)(s.st.roll * 180.0f / PI_F), (double)s.st.dolly, (double)s.st.pan,
            (double)(s.st.fyaw * 180.0f / PI_F), (double)(s.st.fpitch * 180.0f / PI_F),
            (double)(s.st.froll * 180.0f / PI_F), (double)s.st.pos[0], (double)s.st.pos[1],
            (double)s.st.pos[2], (double)s.st.zoom);
    s.st.active = 0;
    s.capturePending = 0;
    s.fixedBasis = 0;
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

/* the basis before any ico_photo_camera: the game's convention (view axes
   on the world's, +y down the screen, the world's up -y) */
static const Basis *basis(void)
{
    if (!s.haveBasis) {
        memset(&s.b, 0, sizeof(s.b));
        s.b.rn = s.b.r0 = v3(1.0f, 0.0f, 0.0f);
        s.b.y0 = v3(0.0f, 1.0f, 0.0f);
        s.b.fn0 = s.b.f0 = v3(0.0f, 0.0f, 1.0f);
        s.b.w = v3(0.0f, -1.0f, 0.0f);
        s.b.pUp = 1.0f;
        s.b.yRt = -1.0f;
    }
    return &s.b;
}

/* the free camera's forward (the view, pitch included) and right (level
   when the game's is, the roll left out) */
static void freeAxes(const Basis *b, float fyaw, float fpitch, V3 *fwd, V3 *right)
{
    const float a = b->yRt * fyaw;
    *fwd = unit(turn(turn(b->fn0, b->rn, b->pUp * fpitch), b->w, a));
    *right = unit(turn(b->rn, b->w, a));
}

static float wrapPi(float a)
{
    if (a > PI_F) {
        return a - 2.0f * PI_F;
    }
    return a < -PI_F ? a + 2.0f * PI_F : a;
}

static void orbitStep(IcoPhotoState *st, const IcoPhotoPad *pad, float k, float dt)
{
    const float rx = axis(pad->ana[0]), ry = axis(pad->ana[1]);
    const float lx = axis(pad->ana[2]), ly = axis(pad->ana[3]);
    st->yaw += lx * YAW_RATE * k * dt;
    /* up on the stick (ly < 0) raises the camera */
    st->pitch += (s.invertY ? ly : -ly) * PITCH_RATE * k * dt;
    if (s.haveBasis) {
        const float hi = s.b.e0 + MAX_ELEVATION, lo = s.b.e0 - MAX_ELEVATION;
        st->pitch = st->pitch > hi ? hi : (st->pitch < lo ? lo : st->pitch);
    }
    st->dolly *= expf(ry * DOLLY_RATE * k * dt);
    st->dolly = st->dolly < MIN_DOLLY ? MIN_DOLLY : (st->dolly > MAX_DOLLY ? MAX_DOLLY : st->dolly);
    st->pan += rx * st->dolly * PAN_RATE * k * dt;
}

static void freeStep(IcoPhotoState *st, const IcoPhotoPad *pad, float k, float dt)
{
    static const float mul[3] = {0.25f, 1.0f, 4.0f};
    const float rx = axis(pad->ana[0]), ry = axis(pad->ana[1]);
    const float lx = axis(pad->ana[2]), ly = axis(pad->ana[3]);
    const Basis *b = basis();
    /* look: right on the stick turns right, up (ry < 0) looks up */
    st->fyaw += rx * YAW_RATE * k * dt;
    st->fpitch += (s.invertY ? ry : -ry) * PITCH_RATE * k * dt;
    /* the view's elevation is e0 + fpitch: kept within MAX_ELEVATION of
       the horizontal (ico_photo_camera clamps the same) */
    const float hi = MAX_ELEVATION - b->e0, lo = -MAX_ELEVATION - b->e0;
    st->fpitch = st->fpitch > hi ? hi : (st->fpitch < lo ? lo : st->fpitch);
    /* move: in the camera's frame as it now looks, up and down along the
       world's vertical */
    V3 fwd, right;
    freeAxes(b, st->fyaw, st->fpitch, &fwd, &right);
    float rise = 0.0f;
    if (pad->held & ICO_PHOTO_UP) {
        rise += 1.0f;
    }
    if (pad->held & ICO_PHOTO_DOWN) {
        rise -= 1.0f;
    }
    const int sp = st->speed >= ICO_PHOTO_SPEED_SLOW && st->speed <= ICO_PHOTO_SPEED_FAST
                       ? st->speed
                       : ICO_PHOTO_SPEED_NORMAL;
    const float step = ICO_PHOTO_MOVE * mul[sp] * k * dt;
    const V3 d = add(add(scale(fwd, -ly), scale(right, lx)), scale(b->w, rise));
    st->pos[0] += d.x * step;
    st->pos[1] += d.y * step;
    st->pos[2] += d.z * step;
}

static void pose(const IcoPhotoState *st, V3 rows[3], V3 *eye);

/* the eye's move and the view's turn (radians, the angle of the rotation
   between the two, as the renderer measures a camera cut) from a to b */
static void stepOf(const IcoPhotoState *a, const IcoPhotoState *b, float *move, float *turnBy)
{
    V3 ra[3], rb[3], ea, eb;
    pose(a, ra, &ea);
    pose(b, rb, &eb);
    const V3 d = sub(eb, ea);
    *move = sqrtf(dot(d, d));
    float c = 0.0f;
    for (int i = 0; i < 3; i++) {
        c += dot(unit(ra[i]), unit(rb[i]));
    }
    c = (c - 1.0f) * 0.5f;
    *turnBy = acosf(c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c));
}

/* st's camera state t of the way from a to full (the cameras' angles,
   distances and offsets; the mode, speed and zoom are st's) */
static void lerpState(IcoPhotoState *st, const IcoPhotoState *a, const IcoPhotoState *full, float t)
{
#define PHOTO_LERP(f) st->f = a->f + (full->f - a->f) * t
    PHOTO_LERP(yaw);
    PHOTO_LERP(pitch);
    PHOTO_LERP(roll);
    PHOTO_LERP(dolly);
    PHOTO_LERP(pan);
    PHOTO_LERP(fyaw);
    PHOTO_LERP(fpitch);
    PHOTO_LERP(froll);
    PHOTO_LERP(pos[0]);
    PHOTO_LERP(pos[1]);
    PHOTO_LERP(pos[2]);
#undef PHOTO_LERP
}

/* the tick's camera step from before to *st shortened to STEP_MOVE and
   STEP_TURN at most (the camera is a function of the state through the
   basis, so a few shortenings settle it); nothing before a basis */
static void clampStep(IcoPhotoState *st, const IcoPhotoState *before)
{
    if (!s.haveBasis) {
        return;
    }
    const IcoPhotoState full = *st;
    float t = 1.0f;
    for (int i = 0; i < 16; i++) {
        float move, turnBy;
        stepOf(before, st, &move, &turnBy);
        if (move <= STEP_MOVE && turnBy <= STEP_TURN) {
            return;
        }
        float f = 1.0f;
        if (move > STEP_MOVE) {
            f = fminf(f, STEP_MOVE / move);
        }
        if (turnBy > STEP_TURN) {
            f = fminf(f, STEP_TURN / turnBy);
        }
        t *= f * 0.95f;
        lerpState(st, before, &full, t);
    }
    lerpState(st, before, &full, 0.0f);
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
    IcoPhotoState *st = &s.st;
    const IcoPhotoState before = *st;
    const int freeCam = st->mode == ICO_PHOTO_CAM_FREE;
    if (freeCam) {
        freeStep(st, pad, k, dt);
    } else {
        orbitStep(st, pad, k, dt);
    }
    /* the lens: roll and zoom, at stick_speed's rate in both cameras */
    float *roll = freeCam ? &st->froll : &st->roll;
    if (pad->held & ICO_PHOTO_L1) {
        *roll -= ROLL_RATE * k * dt;
    }
    if (pad->held & ICO_PHOTO_R1) {
        *roll += ROLL_RATE * k * dt;
    }
    /* the step within STEP_MOVE and STEP_TURN (before the roll's wrap, so
       the step is the one taken) */
    clampStep(st, &before);
    *roll = wrapPi(*roll);
    /* R2 narrows, L2 widens (the orbit camera's Up and Down too: the free
       camera's rise and sink) */
    const unsigned in = ICO_PHOTO_R2 | (freeCam ? 0u : ICO_PHOTO_UP);
    const unsigned out = ICO_PHOTO_L2 | (freeCam ? 0u : ICO_PHOTO_DOWN);
    float z = 0.0f;
    if (pad->held & in) {
        z += 1.0f;
    }
    if (pad->held & out) {
        z -= 1.0f;
    }
    st->zoom *= expf(z * ZOOM_RATE * k * dt);
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
        /* the current camera only (and the shared zoom) */
        if (freeCam) {
            st->fyaw = st->fpitch = st->froll = 0.0f;
            st->pos[0] = st->pos[1] = st->pos[2] = 0.0f;
        } else {
            st->yaw = st->pitch = st->roll = st->pan = 0.0f;
            st->dolly = 1.0f;
        }
        st->zoom = 1.0f;
    }
    if (pad->pressed & ICO_PHOTO_R3) {
        st->speed = st->speed >= ICO_PHOTO_SPEED_FAST ? ICO_PHOTO_SPEED_SLOW : st->speed + 1;
        static const char *const names[3] = {"slow", "normal", "fast"};
        fprintf(stderr, "photo: speed %s\n", names[st->speed]);
    }
    if (pad->pressed & ICO_PHOTO_L3) {
        st->mode = freeCam ? ICO_PHOTO_CAM_ORBIT : ICO_PHOTO_CAM_FREE;
        fprintf(stderr, "photo: %s camera\n", freeCam ? "orbit" : "free");
    }
    if (pad->pressed & ICO_PHOTO_SQUARE) {
        /* the help panel, and the choice kept for the next time (as the
           Settings screen's rows save theirs) */
        st->hud = !st->hud;
        s.hideUi = !st->hud;
        if (ico_config_set_bool("photo.hide_ui", s.hideUi) != 0 || ico_config_save() != 0) {
            fprintf(stderr, "photo: could not save hide_ui\n");
        }
    }
    if (pad->pressed & ICO_PHOTO_CROSS) {
        s.capturePending++;
        st->captures++;
        fprintf(stderr, "photo: capture %u asked\n", st->captures);
    }
    return 0;
}

/* ------------------------------------------------------------ the camera */

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

/* game's basis into s.b (the eye, the world's vertical, the turns' signs) */
static void keepBasis(const RdCamera *game, V3 r0, V3 y0, V3 f0)
{
    const float *v = game->view;
    Basis *b = &s.b;
    b->r0 = r0;
    b->y0 = y0;
    b->f0 = f0;
    b->rn = unit(r0);
    b->fn0 = unit(f0);
    /* the eye: -R^T t (rows of a rotation; the view is one to 1e-4) */
    const V3 t = v3(v[12], v[13], v[14]);
    b->eye0 = scale(add(add(scale(r0, t.x), scale(y0, t.y)), scale(f0, t.z)), -1.0f);
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
    b->w = w;
    b->e0 = asinf(fmaxf(-1.0f, fminf(1.0f, dot(b->fn0, w))));
    b->pUp = dot(turn(b->fn0, b->rn, 0.01f), w) > dot(b->fn0, w) ? 1.0f : -1.0f;
    b->yRt = dot(turn(b->fn0, w, 0.01f), b->rn) > 0.0f ? 1.0f : -1.0f;
    s.haveBasis = 1;
}

/* game's view rows, 0 when one is zero (the view does not invert) */
static int viewRows(const RdCamera *game, V3 *r0, V3 *y0, V3 *f0)
{
    const float *v = game->view;
    *r0 = row(v, 0);
    *y0 = row(v, 1);
    *f0 = row(v, 2);
    return dot(*r0, *r0) > 0.0f && dot(*f0, *f0) > 0.0f && dot(*y0, *y0) > 0.0f;
}

int ico_photo_set_game(const RdCamera *game)
{
    V3 r0, y0, f0;
    if (!game) {
        s.fixedBasis = 0;
        return 0;
    }
    if (!viewRows(game, &r0, &y0, &f0)) {
        return 0;
    }
    s.fovGame = ico_photo_fov_deg(game);
    keepBasis(game, r0, y0, f0);
    s.fixedBasis = 1;
    return 1;
}

float ico_photo_fov_now(void)
{
    if (!(s.fovGame > 0.0f)) {
        return 0.0f;
    }
    const float k = s.st.zoom > 0.0f ? s.st.zoom : 1.0f;
    return 2.0f * atanf(tanf(DEG(s.fovGame) * 0.5f) / k) * 180.0f / PI_F;
}

/* the camera st shows, from the basis: its view's rows (right, down,
   forward) and its eye */
static void pose(const IcoPhotoState *st, V3 rows[3], V3 *eye)
{
    const Basis *b = basis();
    const V3 r0 = b->r0, y0 = b->y0, f0 = b->f0;
    const V3 rn = b->rn, fn0 = b->fn0, w = b->w;
    const float e0 = b->e0;
    V3 r, y, f;
    if (st->mode == ICO_PHOTO_CAM_FREE) {
        /* pitch about the camera's right (the view's elevation e0 + fpitch
           kept within MAX_ELEVATION, as update keeps it), yaw about the
           vertical, roll about the new forward; the eye moved by pos */
        float pitch = st->fpitch;
        pitch = pitch > MAX_ELEVATION - e0 ? MAX_ELEVATION - e0 : pitch;
        pitch = pitch < -MAX_ELEVATION - e0 ? -MAX_ELEVATION - e0 : pitch;
        const float pa = b->pUp * pitch, ya = b->yRt * st->fyaw;
        r = turn(turn(r0, rn, pa), w, ya);
        y = turn(turn(y0, rn, pa), w, ya);
        f = turn(turn(f0, rn, pa), w, ya);
        const V3 fn = unit(f);
        r = turn(r, fn, st->froll);
        y = turn(y, fn, st->froll);
        *eye = add(b->eye0, v3(st->pos[0], st->pos[1], st->pos[2]));
    } else {
        /* the pivot: on the forward axis, nearest the subject */
        float focus = ICO_PHOTO_FOCUS;
        if (s.haveSubject) {
            const float d = dot(
                v3(s.subject[0] - b->eye0.x, s.subject[1] - b->eye0.y, s.subject[2] - b->eye0.z),
                fn0);
            focus = d >= MIN_FOCUS && d <= MAX_FOCUS ? d : focus;
        }
        const V3 pivot0 = add(b->eye0, scale(fn0, focus));
        /* pitch: about the camera's right, the sign that raises the eye
           (turns the forward axis away from the vertical); the eye's
           elevation above the pivot is -e0 + pitch: kept within
           MAX_ELEVATION of the horizontal (update clamps the state the same) */
        float pitch = st->pitch;
        pitch = pitch > e0 + MAX_ELEVATION ? e0 + MAX_ELEVATION : pitch;
        pitch = pitch < e0 - MAX_ELEVATION ? e0 - MAX_ELEVATION : pitch;
        const float pSign = -b->pUp;
        /* yaw: about the vertical, the sign that moves the eye to the right */
        const V3 eyeDir = scale(fn0, -1.0f);
        const float ySign = dot(turn(eyeDir, w, 0.01f), rn) > 0.0f ? 1.0f : -1.0f;
        r = turn(r0, rn, pSign * pitch);
        y = turn(y0, rn, pSign * pitch);
        f = turn(f0, rn, pSign * pitch);
        r = turn(r, w, ySign * st->yaw);
        y = turn(y, w, ySign * st->yaw);
        f = turn(f, w, ySign * st->yaw);
        const V3 fn = unit(f);
        r = turn(r, fn, st->roll);
        y = turn(y, fn, st->roll);
        const V3 pivot = add(pivot0, scale(unit(turn(rn, w, ySign * st->yaw)), st->pan * focus));
        *eye = add(pivot, scale(fn, -st->dolly * focus));
    }
    rows[0] = r;
    rows[1] = y;
    rows[2] = f;
}

int ico_photo_camera(RdCamera *out, const RdCamera *game)
{
    if (!out || !game) {
        return 0;
    }
    V3 r0, y0, f0;
    if (!viewRows(game, &r0, &y0, &f0)) {
        return 0;
    }
    /* the session's basis when ico_photo_set_game gave one (a constant
       camera: the picture never feeds back into it), else game's */
    if (!s.fixedBasis) {
        s.fovGame = ico_photo_fov_deg(game);
        keepBasis(game, r0, y0, f0);
    }
    const IcoPhotoState *st = &s.st;
    const int freeCam = st->mode == ICO_PHOTO_CAM_FREE;
    const int still = freeCam ? st->fyaw == 0.0f && st->fpitch == 0.0f && st->froll == 0.0f &&
                                    st->pos[0] == 0.0f && st->pos[1] == 0.0f && st->pos[2] == 0.0f
                              : st->yaw == 0.0f && st->pitch == 0.0f && st->roll == 0.0f &&
                                    st->pan == 0.0f && st->dolly == 1.0f;
    if (still && st->zoom == 1.0f) {
        *out = *game;
        return 1;
    }
    V3 rows[3], eye;
    pose(st, rows, &eye);
    *out = *game;
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
