/*
 * port/game/options.c
 *
 * The port's gameplay options (options.h).
 */
#include "options.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

/* -1: not read yet; 0 or 1 */
/* package MV (options.h): the model viewer is up */
int ico_mv_active;

static int s_stick_fix = -1;

static int s_yorda_safe = -1;

static int s_mirror = -1;

static int s_developer_mode = -1;

static int s_circle_back = -1;

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

static void (*s_mirror_listener)(int on);

void ico_opt_set_mirror(int on)
{
    s_mirror = on != 0;
    if (s_mirror_listener != NULL) {
        s_mirror_listener(s_mirror);
    }
}

void ico_opt_mirror_reset(void)
{
    ico_opt_set_mirror(ico_config_get_bool("gameplay.mirror", 0) != 0);
}

void ico_opt_set_mirror_listener(void (*fn)(int on))
{
    s_mirror_listener = fn;
    if (fn != NULL) {
        fn(ico_opt_mirror());
    }
}

/* [mirror] slot_N, slot_N_sum (options.h) */
#define MIRROR_NO_SUM (-1LL - 0xFFFFFFFFLL) /* outside the uint32 range */

static int slot_keys(int slot, char *flag, char *sum, size_t size)
{
    if (slot < 0 || slot > 99) {
        return -1;
    }
    snprintf(flag, size, "mirror.slot_%d", slot);
    snprintf(sum, size, "mirror.slot_%d_sum", slot);
    return 0;
}

int ico_mirror_slot_get(int slot, unsigned int sum)
{
    char flag[32], key[32];

    if (slot_keys(slot, flag, key, sizeof(flag)) != 0) {
        return -1;
    }
    if (ico_config_get_int(key, MIRROR_NO_SUM) != (long long)sum) {
        return -1;
    }
    return ico_config_get_bool(flag, 0) != 0;
}

int ico_mirror_slot_saved(int slot, unsigned int sum)
{
    char flag[32], key[32];

    if (slot_keys(slot, flag, key, sizeof(flag)) != 0) {
        return -1;
    }
    if (ico_config_set_bool(flag, ico_opt_mirror()) != 0 ||
        ico_config_set_int(key, (long long)sum) != 0) {
        return -1;
    }
    return ico_config_save() == 0 ? 0 : -1;
}

int ico_mirror_slot_loaded(int slot, unsigned int sum)
{
    int v = ico_mirror_slot_get(slot, sum);

    ico_opt_set_mirror(v > 0);
    return v > 0;
}

int ico_opt_developer_mode(void)
{
    return get(&s_developer_mode, "gameplay.developer_mode");
}

void ico_opt_set_developer_mode(int on)
{
    s_developer_mode = on != 0;
}

int ico_opt_circle_back(void)
{
    if (s_circle_back < 0) {
        s_circle_back = ico_config_get_bool("game.circle_back", 1) != 0;
    }
    return s_circle_back;
}

void ico_opt_set_circle_back(int on)
{
    s_circle_back = on != 0;
}

int ico_opt_debug_option(void)
{
    long long v = ico_config_get_int("dev.debug_option", 0);

    return v < -0x7FFFFFFF || v > 0x7FFFFFFF ? 0 : (int)v;
}

/* [audio] output (options.h): -2 not read yet, else ICO_OUTPUT_* */
static int s_output = -2;
/* the game's own mode (the card's, the Options row's), kept as the game
   set it; s_game_known 0 until it is */
static int s_game_output;
static int s_game_known;

int ico_opt_output_mode(void)
{
    if (s_output == -2) {
        const char *v = ico_config_get_string("audio.output", "auto");
        s_output = strcmp(v, "stereo") == 0 ? ICO_OUTPUT_STEREO
                   : strcmp(v, "mono") == 0 ? ICO_OUTPUT_MONO
                                            : ICO_OUTPUT_AUTO;
    }
    return s_output;
}

void ico_opt_set_output_mode(int mode)
{
    s_output = mode < 0 ? ICO_OUTPUT_AUTO : mode == 0 ? ICO_OUTPUT_STEREO : ICO_OUTPUT_MONO;
}

const char *ico_opt_output_name(int mode)
{
    return mode < 0 ? "auto" : mode == 0 ? "stereo" : "mono";
}

int ico_opt_output_resolve(int current)
{
    int m = ico_opt_output_mode();

    if (!s_game_known) {
        s_game_output = current;
        s_game_known = 1;
    }
    return m != ICO_OUTPUT_AUTO ? m : s_game_output;
}

int ico_opt_output_card(int card_mode)
{
    s_game_output = card_mode;
    s_game_known = 1;
    return ico_opt_output_resolve(card_mode);
}

void ico_opt_output_toggled(int mode)
{
    s_game_output = mode;
    s_game_known = 1;
    if (ico_opt_output_mode() != ICO_OUTPUT_AUTO) {
        ico_opt_set_output_mode(mode);
        if (ico_config_set_string("audio.output", ico_opt_output_name(s_output)) != 0 ||
            ico_config_save() != 0) {
            fprintf(stderr, "options: cannot write [audio] output\n");
        }
    }
}

void ico_opt_reload(void)
{
    s_stick_fix = s_yorda_safe = s_mirror = s_developer_mode = s_circle_back = -1;
    s_output = -2;
}
