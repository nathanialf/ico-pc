/*
 * port/input/touch.c
 *
 * The touch overlay's input mapper (touch.h): the zones from the output's
 * size, fingers to roles (a button, the left stick, the look pad), the
 * virtual pad they hold, and the overlay's visibility and fade. No SDL and
 * no libm (a Newton square root, as vpad.c).
 */
#include "touch.h"

#include <string.h>

enum { ROLE_PENDING = 0, ROLE_BUTTON, ROLE_STICK, ROLE_LOOK, ROLE_IGNORED };

/* the layout's proportions, in u (the safe height times the size's scale) */
#define M_MARGIN 0.04f /* from the safe area's edges */
#define M_SHOULDER_W 0.22f
#define M_SHOULDER_H 0.10f
#define M_SHOULDER_GAP 0.025f
#define M_DPAD_KEY 0.085f /* one D-pad rect's side */
#define M_DPAD_GAP 0.05f  /* between the shoulder column and the cluster */
#define M_MENU_W 0.13f    /* Start and Select */
#define M_MENU_H 0.075f
#define M_MENU_GAP 0.03f
#define M_FACE_R 0.075f /* a face button's radius */
#define M_FACE_D 0.135f /* its centre from the diamond's centre */

static const float s_scale[3] = {0.85f, 1.0f, 1.2f};

static float sqrtf_(float v)
{
    float x, prev;
    int i;

    if (!(v > 0.0f)) {
        return 0.0f;
    }
    x = v > 1.0f ? v : 1.0f;
    for (i = 0; i < 40; i++) { /* Newton from above: monotone, converges */
        prev = x;
        x = 0.5f * (x + v / x);
        if (x >= prev) {
            return prev;
        }
    }
    return x;
}

static IcoTouchRect rect(float x, float y, float w, float h)
{
    IcoTouchRect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static void button(IcoTouchLayout *l, int z, IcoTouchRect r, int round, unsigned int pad)
{
    l->button[z].rect = r;
    l->button[z].round = round;
    l->button[z].pad = pad;
}

static void face(IcoTouchLayout *l, int z, float cx, float cy, float r, unsigned int pad)
{
    button(l, z, rect(cx - r, cy - r, 2.0f * r, 2.0f * r), 1, pad);
}

IcoTouchLayout ico_touch_layout(uint32_t outW, uint32_t outH, IcoTouchInsets safe, int size)
{
    IcoTouchLayout l;
    float W = (float)outW, H = (float)outH;
    float sx, sy, sw, sh, u, m, k, w;

    memset(&l, 0, sizeof(l));
    if (size < ICO_TOUCH_SMALL || size > ICO_TOUCH_LARGE) {
        size = ICO_TOUCH_MEDIUM;
    }
    if (!(safe.left >= 0.0f)) {
        safe.left = 0.0f;
    }
    if (!(safe.top >= 0.0f)) {
        safe.top = 0.0f;
    }
    if (!(safe.right >= 0.0f)) {
        safe.right = 0.0f;
    }
    if (!(safe.bottom >= 0.0f)) {
        safe.bottom = 0.0f;
    }
    if (safe.left + safe.right >= W || safe.top + safe.bottom >= H) {
        memset(&safe, 0, sizeof(safe));
    }
    l.outW = outW;
    l.outH = outH;
    l.safe = safe;
    l.size = size;
    sx = safe.left;
    sy = safe.top;
    sw = W - safe.left - safe.right;
    sh = H - safe.top - safe.bottom;
    l.safeRect = rect(sx, sy, sw, sh);
    u = sh * s_scale[size];
    l.unit = u;
    m = M_MARGIN * u;

    /* left stick: the left 40 % x bottom 70 % */
    l.stickArea = rect(sx, sy + 0.3f * sh, 0.4f * sw, 0.7f * sh);
    l.stickR = ICO_TOUCH_STICK_RADIUS * u;
    l.runR = ICO_TOUCH_RUN_RING * l.stickR;
    l.stickHomeX = sx + 0.4f * l.stickArea.w;
    l.stickHomeY = sy + sh - m - 1.6f * l.stickR;

    /* look pad: the right 60 % x top 45 % */
    l.lookArea = rect(sx + 0.4f * sw, sy, 0.6f * sw, 0.45f * sh);
    l.lookR = l.stickR;

    /* shoulders in the top corners, L2/R2 under them */
    w = M_SHOULDER_W * u;
    k = M_SHOULDER_H * u;
    button(&l, ICO_TOUCH_B_L1, rect(sx + m, sy + m, w, k), 0, ICO_PAD_L1);
    button(&l, ICO_TOUCH_B_L2, rect(sx + m, sy + m + k + M_SHOULDER_GAP * u, w, k), 0, ICO_PAD_L2);
    button(&l, ICO_TOUCH_B_R1, rect(sx + sw - m - w, sy + m, w, k), 0, ICO_PAD_R1);
    button(&l, ICO_TOUCH_B_R2, rect(sx + sw - m - w, sy + m + k + M_SHOULDER_GAP * u, w, k), 0,
           ICO_PAD_R2);

    /* the D-pad cluster: right of the shoulder column, above the stick */
    k = M_DPAD_KEY * u;
    l.dpadX = sx + m + w + M_DPAD_GAP * u + 1.5f * k;
    l.dpadY = sy + m + 1.5f * k;
    button(&l, ICO_TOUCH_B_UP, rect(l.dpadX - 0.5f * k, l.dpadY - 1.5f * k, k, k), 0, ICO_PAD_UP);
    button(&l, ICO_TOUCH_B_DOWN, rect(l.dpadX - 0.5f * k, l.dpadY + 0.5f * k, k, k), 0,
           ICO_PAD_DOWN);
    button(&l, ICO_TOUCH_B_LEFT, rect(l.dpadX - 1.5f * k, l.dpadY - 0.5f * k, k, k), 0,
           ICO_PAD_LEFT);
    button(&l, ICO_TOUCH_B_RIGHT, rect(l.dpadX + 0.5f * k, l.dpadY - 0.5f * k, k, k), 0,
           ICO_PAD_RIGHT);

    /* Select and Start at the top centre */
    w = M_MENU_W * u;
    k = M_MENU_H * u;
    button(&l, ICO_TOUCH_B_SELECT, rect(sx + 0.5f * sw - 0.5f * M_MENU_GAP * u - w, sy + m, w, k),
           0, ICO_PAD_SELECT);
    button(&l, ICO_TOUCH_B_START, rect(sx + 0.5f * sw + 0.5f * M_MENU_GAP * u, sy + m, w, k), 0,
           ICO_PAD_START);

    /* the face buttons' diamond in the bottom right corner */
    k = M_FACE_D * u;
    w = M_FACE_R * u;
    l.faceX = sx + sw - m - k - w;
    l.faceY = sy + sh - m - k - w;
    face(&l, ICO_TOUCH_B_CROSS, l.faceX, l.faceY + k, w, ICO_PAD_CROSS);
    face(&l, ICO_TOUCH_B_CIRCLE, l.faceX + k, l.faceY, w, ICO_PAD_CIRCLE);
    face(&l, ICO_TOUCH_B_SQUARE, l.faceX - k, l.faceY, w, ICO_PAD_SQUARE);
    face(&l, ICO_TOUCH_B_TRIANGLE, l.faceX, l.faceY - k, w, ICO_PAD_TRIANGLE);
    return l;
}

static int in_rect(const IcoTouchRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* the button zone under (x, y) in pixels, or -1 */
static int hit(const IcoTouchLayout *l, float x, float y)
{
    int z;

    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *b = &l->button[z];

        if (b->round) {
            float r = 0.5f * b->rect.w;
            float dx = x - (b->rect.x + r), dy = y - (b->rect.y + r);

            if (dx * dx + dy * dy <= r * r) {
                return z;
            }
        } else if (in_rect(&b->rect, x, y)) {
            return z;
        }
    }
    return -1;
}

/* --- visibility ----------------------------------------------------------- */

static int any_down(const IcoTouchState *t)
{
    int i;

    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && !t->finger[i].up) {
            return 1;
        }
    }
    return 0;
}

/* 0..1 over ICO_TOUCH_FADE_NS from `from` */
static float ramp(uint64_t now, uint64_t from)
{
    if (now <= from) {
        return 0.0f;
    }
    if (now - from >= ICO_TOUCH_FADE_NS) {
        return 1.0f;
    }
    return (float)(now - from) / (float)ICO_TOUCH_FADE_NS;
}

static int touch_visible(const IcoTouchState *t, uint64_t now)
{
    return t->seen && (any_down(t) || now < t->lastNs + ICO_TOUCH_HIDE_NS);
}

/* the fade by touches alone (no gamepad) */
static float touch_opacity(const IcoTouchState *t, uint64_t now)
{
    uint64_t hide;

    if (!t->seen) {
        return 0.0f;
    }
    if (touch_visible(t, now)) {
        return ramp(now, t->shownNs);
    }
    hide = t->lastNs + ICO_TOUCH_HIDE_NS;
    return ramp(hide, t->shownNs) * (1.0f - ramp(now, hide));
}

int ico_touch_visible(const IcoTouchState *t, int gamepads, uint64_t nowNs)
{
    return gamepads <= 0 && touch_visible(t, nowNs);
}

float ico_touch_opacity(const IcoTouchState *t, int gamepads, uint64_t nowNs)
{
    float o = touch_opacity(t, nowNs);
    int known = t->gamepadsNs != 0 && (t->gamepads > 0) == (gamepads > 0);

    if (gamepads > 0) {
        /* fading out since the first pad connected, else gone */
        return known ? o * (1.0f - ramp(nowNs, t->gamepadsNs)) : 0.0f;
    }
    if (known) {
        o *= ramp(nowNs, t->gamepadsNs); /* back in since the last one left */
    }
    return o;
}

void ico_touch_set_gamepads(IcoTouchState *t, int gamepads, uint64_t nowNs)
{
    if ((gamepads > 0) != (t->gamepads > 0)) {
        t->gamepadsNs = nowNs != 0 ? nowNs : 1;
    }
    t->gamepads = gamepads > 0 ? gamepads : 0;
}

/* --- events --------------------------------------------------------------- */

static void release_all(IcoTouchState *t)
{
    memset(t->finger, 0, sizeof(t->finger));
    t->lookX = t->lookY = 0.0f;
    t->lx = t->ly = 0.0f;
    t->pressed = 0;
    t->stickActive = 0;
    t->lookActive = 0;
}

void ico_touch_reset(IcoTouchState *t, uint64_t nowNs)
{
    int gamepads = t->gamepads;
    uint64_t gamepadsNs = t->gamepadsNs;

    memset(t, 0, sizeof(*t));
    t->look_decay = ICO_TOUCH_LOOK_DECAY;
    t->gamepads = gamepads;
    t->gamepadsNs = gamepadsNs;
    t->seen = 1;
    t->lastNs = nowNs;
    t->shownNs = nowNs;
}

static IcoTouchFinger *find(IcoTouchState *t, uint64_t id)
{
    int i;

    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && t->finger[i].id == id) {
            return &t->finger[i];
        }
    }
    return NULL;
}

void ico_touch_event(IcoTouchState *t, uint64_t finger, float nx, float ny, int kind,
                     uint64_t nowNs)
{
    IcoTouchFinger *f;
    int i;

    nx = ico_clamp01f(nx);
    ny = ico_clamp01f(ny);
    if (kind == ICO_TOUCH_CANCEL) {
        release_all(t);
        if (t->seen) {
            t->lastNs = nowNs;
        }
        return;
    }
    if (kind == ICO_TOUCH_DOWN) {
        if (!touch_visible(t, nowNs)) {
            /* show again, continuing a fade out that has not finished */
            uint64_t back = (uint64_t)(touch_opacity(t, nowNs) * (float)ICO_TOUCH_FADE_NS);

            t->shownNs = nowNs > back ? nowNs - back : 0;
        }
        t->seen = 1;
        t->lastNs = nowNs;
        f = find(t, finger);
        for (i = 0; f == NULL && i < ICO_TOUCH_FINGERS; i++) {
            if (!t->finger[i].used) {
                f = &t->finger[i];
            }
        }
        if (f == NULL) {
            return; /* more fingers than slots: this one does nothing */
        }
        memset(f, 0, sizeof(*f));
        f->id = finger;
        f->x0 = f->x = nx;
        f->y0 = f->y = ny;
        f->used = 1;
        f->role = ROLE_PENDING;
        f->zone = -1;
        return;
    }
    f = find(t, finger);
    if (f == NULL) {
        return;
    }
    t->lastNs = nowNs;
    f->x = nx;
    f->y = ny;
    if (kind == ICO_TOUCH_UP) {
        if (f->role == ROLE_PENDING) {
            f->up = 1; /* a tap between two steps: held for the next one */
        } else {
            memset(f, 0, sizeof(*f));
        }
    }
}

/* --- the step ------------------------------------------------------------- */

void ico_touch_step(IcoTouchState *t, const IcoTouchLayout *l, IcoVirtualPad *out, uint64_t nowNs)
{
    float W = (float)l->outW, H = (float)l->outH;
    float decay = t->look_decay;
    int stick = -1, look = -1;
    int i;

    (void)nowNs;
    memset(out, 0, sizeof(*out));
    if (!(decay >= 0.0f)) {
        decay = 0.0f;
    }
    if (decay > 0.99f) {
        decay = 0.99f;
    }
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && t->finger[i].role == ROLE_STICK) {
            stick = i;
        } else if (t->finger[i].used && t->finger[i].role == ROLE_LOOK) {
            look = i;
        }
    }

    /* new fingers: by where they went down */
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        IcoTouchFinger *f = &t->finger[i];
        float x0 = f->x0 * W, y0 = f->y0 * H;

        if (!f->used || f->role != ROLE_PENDING) {
            continue;
        }
        if (hit(l, x0, y0) >= 0) {
            f->role = ROLE_BUTTON;
        } else if (!f->up && stick < 0 && in_rect(&l->stickArea, x0, y0)) {
            f->role = ROLE_STICK;
            stick = i;
        } else if (!f->up && look < 0 && in_rect(&l->lookArea, x0, y0)) {
            f->role = ROLE_LOOK;
            look = i;
        } else {
            f->role = ROLE_IGNORED;
        }
    }

    /* buttons: whatever is under each button finger now */
    t->pressed = 0;
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        IcoTouchFinger *f = &t->finger[i];

        if (f->used && f->role == ROLE_BUTTON) {
            f->zone = (signed char)hit(l, f->x * W, f->y * H);
            if (f->zone >= 0) {
                t->pressed |= 1u << f->zone;
            }
        }
    }
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        if (t->pressed & (1u << i)) {
            out->buttons |= l->button[i].pad;
        }
    }

    /* the left stick */
    t->lx = t->ly = 0.0f;
    t->stickActive = stick >= 0;
    if (stick >= 0) {
        const IcoTouchFinger *f = &t->finger[stick];
        float dx = (f->x - f->x0) * W, dy = (f->y - f->y0) * H;
        float d = sqrtf_(dx * dx + dy * dy);
        float R = l->stickR > 0.0f ? l->stickR : 1.0f;

        t->stickCX = f->x0 * W;
        t->stickCY = f->y0 * H;
        if (d > R) {
            dx *= R / d;
            dy *= R / d;
        }
        t->stickFX = t->stickCX + dx;
        t->stickFY = t->stickCY + dy;
        if (d >= ICO_TOUCH_STICK_DEADZONE * R) {
            t->lx = dx / R;
            t->ly = dy / R;
        }
    }
    out->lx = t->lx;
    out->ly = t->ly;

    /* the look pad: the offset while held, decaying after */
    t->lookActive = look >= 0;
    if (look >= 0) {
        const IcoTouchFinger *f = &t->finger[look];
        float dx = (f->x - f->x0) * W, dy = (f->y - f->y0) * H;
        float d = sqrtf_(dx * dx + dy * dy);
        float R = l->lookR > 0.0f ? l->lookR : 1.0f;

        t->lookCX = f->x0 * W;
        t->lookCY = f->y0 * H;
        t->lookFX = f->x * W;
        t->lookFY = f->y * H;
        if (d > R) {
            dx *= R / d;
            dy *= R / d;
        }
        t->lookX = dx / R;
        t->lookY = dy / R;
    } else {
        t->lookX *= decay;
        t->lookY *= decay;
        if (t->lookX * t->lookX + t->lookY * t->lookY < 1e-6f) {
            t->lookX = t->lookY = 0.0f;
        }
    }
    out->rx = t->lookX;
    out->ry = t->lookY;

    /* taps that already lifted have had their step */
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && t->finger[i].up) {
            memset(&t->finger[i], 0, sizeof(t->finger[i]));
        }
    }
}

IcoTouchDrawInfo ico_touch_draw_info(const IcoTouchState *t, const IcoTouchLayout *l)
{
    IcoTouchDrawInfo d;

    memset(&d, 0, sizeof(d));
    d.pressed = t->pressed;
    d.stickActive = t->stickActive;
    d.stickR = l->stickR;
    d.runR = l->runR;
    if (t->stickActive) {
        d.stickCX = t->stickCX;
        d.stickCY = t->stickCY;
        d.knobX = t->stickFX;
        d.knobY = t->stickFY;
    } else {
        d.stickCX = d.knobX = l->stickHomeX;
        d.stickCY = d.knobY = l->stickHomeY;
    }
    d.stickMag = sqrtf_(t->lx * t->lx + t->ly * t->ly);
    d.lookActive = t->lookActive;
    d.lookCX = t->lookCX;
    d.lookCY = t->lookCY;
    d.lookFX = t->lookFX;
    d.lookFY = t->lookFY;
    d.rx = t->lookX;
    d.ry = t->lookY;
    return d;
}

int ico_touch_accepts(int mode, int gamepads)
{
    return mode == ICO_TOUCH_MODE_ALWAYS || (mode == ICO_TOUCH_MODE_AUTO && gamepads <= 0);
}

int ico_touch_update(IcoTouchState *t, const IcoTouchLayout *l, int mode, int gamepads,
                     IcoVirtualPad *v, uint64_t nowNs)
{
    IcoVirtualPad tv;

    ico_touch_step(t, l, &tv, nowNs);
    if (!ico_touch_accepts(mode, gamepads)) {
        return 0;
    }
    ico_vpad_merge(v, &tv);
    return 1;
}

float ico_touch_mode_opacity(const IcoTouchState *t, int mode, int gamepads, uint64_t nowNs)
{
    if (mode == ICO_TOUCH_MODE_ALWAYS) {
        return 1.0f;
    }
    if (mode != ICO_TOUCH_MODE_AUTO) {
        return 0.0f;
    }
    return ico_touch_opacity(t, gamepads, nowNs);
}
