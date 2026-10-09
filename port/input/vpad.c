/*
 * port/input/vpad.c
 *
 * The virtual pad's arithmetic: merging sources, the radial dead zone, the
 * stick fix, mirror, and the quantisation to a DualShock's bytes. No libm:
 * the stick fix's angle uses a small arctangent, accurate to 0.25 degrees,
 * which moves the scale by under 0.002 percent.
 */
#include "input.h"

static float absf(float v)
{
    return v < 0.0f ? -v : v;
}

static float sqrtf_(float v)
{
    float x, prev;
    int i;

    if (v <= 0.0f) {
        return 0.0f;
    }
    x = v > 1.0f ? v : 1.0f;
    for (i = 0; i < 24; i++) { /* Newton from above: monotone, converges */
        prev = x;
        x = 0.5f * (x + v / x);
        if (x >= prev) {
            return prev;
        }
    }
    return x;
}

void ico_vpad_merge(IcoVirtualPad *dst, const IcoVirtualPad *src)
{
    float a, b;

    dst->buttons |= src->buttons;
    dst->menu_buttons |= src->menu_buttons;
    a = dst->lx * dst->lx + dst->ly * dst->ly;
    b = src->lx * src->lx + src->ly * src->ly;
    if (b > a) {
        dst->lx = src->lx;
        dst->ly = src->ly;
    }
    a = dst->rx * dst->rx + dst->ry * dst->ry;
    b = src->rx * src->rx + src->ry * src->ry;
    if (b > a) {
        dst->rx = src->rx;
        dst->ry = src->ry;
    }
}

unsigned char ico_input_quantise(float v)
{
    float f = (v + 1.0f) * 127.5f + 0.5f;

    if (!(f > 0.0f)) { /* also NaN */
        return 0;
    }
    if (f >= 255.0f) {
        return 255;
    }
    return (unsigned char)f;
}

void ico_input_deadzone(float *x, float *y, float dz)
{
    float len = sqrtf_(*x * *x + *y * *y);
    float s;

    if (dz <= 0.0f) {
        return;
    }
    if (len <= dz) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }
    s = (len - dz) / (1.0f - dz) / len;
    if (s * len > 1.0f) {
        s = 1.0f / len;
    }
    *x *= s;
    *y *= s;
}

/* atan(r) in degrees for r in 0..1: (pi/4) r + 0.273 r (1 - r) radians */
static float atan01_deg(float r)
{
    return (0.78539816f * r + 0.273f * r * (1.0f - r)) * 57.2957795f;
}

float ico_input_stick_fix_scale(float x, float y)
{
    float ax = absf(x), ay = absf(y);
    float hi = ax > ay ? ax : ay;
    float lo = ax > ay ? ay : ax;
    float d;

    if (hi <= 0.0f) {
        return 1.0f;
    }
    d = atan01_deg(lo / hi); /* 0..45 */
    if (d > 45.0f) {
        d = 45.0f;
    }
    return 1.0f + 0.2f * d / 45.0f;
}

void ico_input_stick_fix(float *x, float *y)
{
    float s = ico_input_stick_fix_scale(*x, *y);
    float hi = absf(*x) > absf(*y) ? absf(*x) : absf(*y);

    if (hi * s > 1.0f) {
        s = 1.0f / hi;
    }
    if (s < 1.0f) {
        s = 1.0f; /* never shrinks (a component already past 1) */
    }
    *x *= s;
    *y *= s;
}

void ico_input_vpad_to_frame(const IcoVirtualPad *v, int stick_fix, int mirror, IcoPadFrame *out)
{
    float lx = v->lx, ly = v->ly, rx = v->rx, ry = v->ry;

    if (stick_fix) {
        ico_input_stick_fix(&lx, &ly);
        ico_input_stick_fix(&rx, &ry);
    }
    if (mirror) {
        lx = -lx;
        rx = -rx;
    }
    out->buttons = v->buttons & 0xFFFFu;
    out->lx = ico_input_quantise(lx);
    out->ly = ico_input_quantise(ly);
    out->rx = ico_input_quantise(rx);
    out->ry = ico_input_quantise(ry);
}
