/*
 * port/game/options.c
 *
 * The port's gameplay options (options.h, docs/port/OPTIONS.md).
 */
#include "options.h"
#include "config.h"

/* -1: not read yet; 0 or 1 */
static int s_stick_fix = -1;

static int s_yorda_safe = -1;

static int s_mirror = -1;

static int s_developer_mode = -1;

static int get(int *v, const char *path)
{
    if (*v < 0) {
        *v = ico_config_get_bool(path, 0) != 0;
    }
    return *v;
}

int ico_opt_stick_fix(void)
{
    return get(&s_stick_fix, "gameplay.stick_fix");
}

void ico_opt_set_stick_fix(int on)
{
    s_stick_fix = on != 0;
}

int ico_opt_yorda_safe(void)
{
    return get(&s_yorda_safe, "gameplay.yorda_safe");
}

void ico_opt_set_yorda_safe(int on)
{
    s_yorda_safe = on != 0;
}

int ico_opt_mirror(void)
{
    return get(&s_mirror, "gameplay.mirror");
}

void ico_opt_set_mirror(int on)
{
    s_mirror = on != 0;
}

int ico_opt_developer_mode(void)
{
    return get(&s_developer_mode, "gameplay.developer_mode");
}

void ico_opt_set_developer_mode(int on)
{
    s_developer_mode = on != 0;
}

int ico_opt_debug_option(void)
{
    long long v = ico_config_get_int("dev.debug_option", 0);

    return v < -0x7FFFFFFF || v > 0x7FFFFFFF ? 0 : (int)v;
}

void ico_opt_reload(void)
{
    s_stick_fix = s_yorda_safe = s_mirror = s_developer_mode = -1;
}
