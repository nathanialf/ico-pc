/*
 * port/game/test/ico_rows_test.c
 *
 * Free movement and Ask Yorda for a hint (port/game/ico_rows.h) on the CPU:
 * the row's key, its dependence on Developer mode, the reload; the hint
 * binding's presses, one fire for each press, a held button, a stale press,
 * a second press.  No game sources.
 */
#include <stdio.h>

#include "config.h"
#include "ico_rows.h"
#include "options.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_free_movement(void)
{
    ico_config_reset("/nonexistent/ico_rows_test.toml", "/nonexistent/ico_rows_test.ini");
    ico_opt_reload();
    /* nothing set: off, with Developer mode on or off */
    ico_opt_set_developer_mode(0);
    CHECK(ico_free_movement_row() == 0 && ico_free_movement() == 0);
    ico_opt_set_developer_mode(1);
    CHECK(ico_free_movement() == 0);
    /* the key turns the row on after a reload; it works only with Developer mode */
    CHECK(ico_config_set_bool("developer.free_movement", 1) == 0);
    ico_opt_reload();
    ico_opt_set_developer_mode(0);
    CHECK(ico_free_movement_row() == 1 && ico_free_movement() == 0);
    ico_opt_set_developer_mode(1);
    CHECK(ico_free_movement_row() == 1 && ico_free_movement() == 1);
    /* the row's switch, without the file */
    ico_free_movement_set(0);
    CHECK(ico_free_movement_row() == 0 && ico_free_movement() == 0);
    ico_free_movement_set(1);
    CHECK(ico_free_movement() == 1);
    /* a reload reads the key again */
    CHECK(ico_config_set_bool("developer.free_movement", 0) == 0);
    ico_opt_reload();
    ico_opt_set_developer_mode(1);
    CHECK(ico_free_movement_row() == 0 && ico_free_movement() == 0);
    ico_opt_set_developer_mode(0);
}

static void frames(int down, int n)
{
    while (n-- > 0) {
        ico_hint_ask_note(down);
    }
}

static void test_hint(void)
{
    ico_hint_ask_reset();
    CHECK(ico_hint_ask() == 0 && ico_hint_ask_presses() == 0);
    /* a press fires once, however long the button is held */
    frames(0, 3);
    frames(1, 1);
    CHECK(ico_hint_ask_presses() == 1);
    CHECK(ico_hint_ask() == 1 && ico_hint_ask_fired() == 1);
    CHECK(ico_hint_ask() == 0);
    frames(1, 30);
    CHECK(ico_hint_ask_presses() == 1 && ico_hint_ask() == 0);
    /* let go and press again: a second fire */
    frames(0, 2);
    frames(1, 1);
    CHECK(ico_hint_ask_presses() == 2 && ico_hint_ask() == 1 && ico_hint_ask_fired() == 2);
    /* the game looks a little later, within the age: still fires */
    frames(0, 1);
    frames(1, 1);
    frames(0, ICO_HINT_ASK_AGE - 1);
    CHECK(ico_hint_ask() == 1 && ico_hint_ask_fired() == 3);
    /* too late (nothing was waiting): dropped, and not kept for later */
    frames(0, 1);
    frames(1, 1);
    frames(0, ICO_HINT_ASK_AGE + 1);
    CHECK(ico_hint_ask_presses() == 4);
    CHECK(ico_hint_ask() == 0 && ico_hint_ask_fired() == 3);
    frames(0, 5);
    CHECK(ico_hint_ask() == 0);
    /* two presses before one look count as one fire (the newest is the one taken) */
    frames(0, 1);
    frames(1, 1);
    frames(0, 1);
    frames(1, 1);
    CHECK(ico_hint_ask_presses() == 6 && ico_hint_ask() == 1 && ico_hint_ask() == 0);
    ico_hint_ask_reset();
    CHECK(ico_hint_ask_presses() == 0 && ico_hint_ask_fired() == 0);
}

int main(void)
{
    test_free_movement();
    test_hint();
    if (failures != 0) {
        fprintf(stderr, "ico_rows_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("ico_rows_test: ok\n");
    return 0;
}
