/*
 * port/platform/test/state_registry_test.c
 *
 * The save state registry (state_registry.h): blocks kept in registration
 * order with their name, address, size and flags; a repeated name, a
 * missing name, address or size refused (and the table unchanged); the
 * table full at ICO_STATE_MAX_ENTRIES; the reset empties it; out-of-range
 * indexes give NULL.
 */
#include <stdio.h>
#include <string.h>

#include "../state_registry.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int a[4];
static double b;
static char many[ICO_STATE_MAX_ENTRIES + 1][8];
static char names[ICO_STATE_MAX_ENTRIES + 1][16];

int main(void)
{
    const IcoStateEntry *e;
    int i;

    ico_state_registry_reset();
    CHECK(ico_state_registry_count() == 0);
    CHECK(ico_state_registry_entry(0) == NULL);

    CHECK(ico_state_register("test.a", a, sizeof a, ICO_STATE_SCAN) == 0);
    CHECK(ico_state_register("test.b", &b, sizeof b, ICO_STATE_AUDIO | ICO_STATE_CLOCK) == 0);
    CHECK(ico_state_registry_count() == 2);
    e = ico_state_registry_entry(0);
    CHECK(e != NULL && strcmp(e->name, "test.a") == 0 && e->p == (void *)a && e->n == sizeof a &&
          e->flags == ICO_STATE_SCAN);
    e = ico_state_registry_entry(1);
    CHECK(e != NULL && strcmp(e->name, "test.b") == 0 && e->p == (void *)&b && e->n == sizeof b &&
          e->flags == (ICO_STATE_AUDIO | ICO_STATE_CLOCK));
    CHECK(ico_state_registry_entry(2) == NULL);
    CHECK(ico_state_registry_entry(-1) == NULL);

    /* refusals leave the table as it was */
    CHECK(ico_state_register("test.a", a, sizeof a, ICO_STATE_SCAN) == -1); /* the same again */
    CHECK(ico_state_register("test.a", &b, sizeof b, 0) == -1);             /* another block */
    CHECK(ico_state_register(NULL, a, sizeof a, 0) == -1);
    CHECK(ico_state_register("", a, sizeof a, 0) == -1);
    CHECK(ico_state_register("test.c", NULL, 4, 0) == -1);
    CHECK(ico_state_register("test.c", a, 0, 0) == -1);
    CHECK(ico_state_registry_count() == 2);

    /* full */
    ico_state_registry_reset();
    CHECK(ico_state_registry_count() == 0);
    for (i = 0; i < ICO_STATE_MAX_ENTRIES; i++) {
        snprintf(names[i], sizeof names[i], "block.%d", i);
        CHECK(ico_state_register(names[i], many[i], sizeof many[i], 0) == 0);
    }
    snprintf(names[i], sizeof names[i], "block.%d", i);
    CHECK(ico_state_register(names[i], many[i], sizeof many[i], 0) == -1);
    CHECK(ico_state_registry_count() == ICO_STATE_MAX_ENTRIES);
    e = ico_state_registry_entry(ICO_STATE_MAX_ENTRIES - 1);
    CHECK(e != NULL && e->p == (void *)many[ICO_STATE_MAX_ENTRIES - 1]);
    ico_state_registry_reset();
    CHECK(ico_state_registry_count() == 0);

    if (fails != 0) {
        printf("state_registry_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("state_registry_test: ok\n");
    return 0;
}
