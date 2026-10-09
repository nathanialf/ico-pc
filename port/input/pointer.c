/*
 * port/input/pointer.c
 *
 * The mouse pointer in the menus (pointer.h; package I17b).
 */
#include "input.h"
#include "pointer.h"

#include <string.h>

static struct {
    int valid;
    float x, y;
    int moved;
    int clicks;
    float wheel;
    int activity;
    int menu;
} s_p;

void ico_pointer_move(float nx, float ny)
{
    s_p.valid = 1;
    s_p.x = ico_clamp01f(nx);
    s_p.y = ico_clamp01f(ny);
    s_p.moved = 1;
    s_p.activity = 1;
}

void ico_pointer_leave(void)
{
    s_p.valid = 0;
    s_p.moved = 0;
}

void ico_pointer_button(int down)
{
    if (down) {
        s_p.clicks++;
    }
    s_p.activity = 1;
}

void ico_pointer_other_button(void)
{
    s_p.activity = 1;
}

void ico_pointer_wheel(float steps)
{
    s_p.wheel += steps;
    if (steps != 0.0f) {
        s_p.activity = 1;
    }
}

int ico_pointer_take(IcoPointerTick *t)
{
    IcoPointerTick out;
    /* whole notches toward zero; the fraction stays for the next take */
    const int notches = (int)s_p.wheel;

    out.valid = s_p.valid;
    out.x = s_p.x;
    out.y = s_p.y;
    out.moved = s_p.valid && s_p.moved;
    out.clicks = s_p.clicks;
    out.wheel = notches;
    out.activity = s_p.activity;
    s_p.wheel -= (float)notches;
    s_p.moved = 0;
    s_p.clicks = 0;
    s_p.activity = 0;
    if (t) {
        *t = out;
    }
    return out.activity;
}

void ico_pointer_set_menu(int on)
{
    s_p.menu = on != 0;
}

int ico_pointer_menu(void)
{
    return s_p.menu;
}

void ico_pointer_reset(void)
{
    memset(&s_p, 0, sizeof(s_p));
}
