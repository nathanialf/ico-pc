/*
 * port/config/test/sim_keys_test.c
 *
 * The config keys that change the simulation (port/platform/sim_keys.h),
 * over a config.toml in the folder given that does not exist (every key
 * unset):
 *
 *   the list    NULL-terminated, "section.key" names, no repeats, the pad
 *               recording's keys (mirror, shadow speed, a variant) among
 *               them
 *   the text    "key=value\n" for every key in the list's order, "" for an
 *               unset key; a short buffer gets the start, NUL-terminated,
 *               and the hash of the whole
 *   the hash    changes when one key's value does and comes back when the
 *               value does; an empty value hashes as an unset key
 *   first_diff  NULL against the current text; names the first key whose
 *               value differs ("key=saved (now cur)"), the earliest in the
 *               list when two differ; a key the saved text lacks (a key a
 *               later version added) reads as its default: equal while the
 *               key is unset or at its default, else "key=(absent) (now cur)"
 *   defaults    every key's default: unset is always the default, the
 *               default's text is, another value is not, a key without one
 *               fixed text only unset, a key not in the list never; the
 *               first-run config.toml writes every listed key it holds at
 *               that key's default
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "host_config.h"
#include "sim_keys.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int listed(const char *key)
{
    int i;

    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        if (strcmp(ICO_SIM_KEYS[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_list(void)
{
    int i, j, n = 0;

    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        const char *dot = strchr(ICO_SIM_KEYS[i], '.');

        CHECK(dot != NULL && dot != ICO_SIM_KEYS[i] && dot[1] != '\0');
        CHECK(strchr(ICO_SIM_KEYS[i], '=') == NULL && strchr(ICO_SIM_KEYS[i], '\n') == NULL);
        for (j = 0; j < i; j++) {
            CHECK(strcmp(ICO_SIM_KEYS[i], ICO_SIM_KEYS[j]) != 0);
        }
        n++;
    }
    CHECK(n >= 20);
    CHECK(listed("gameplay.mirror"));
    CHECK(listed("gameplay.shadow_speed"));
    CHECK(listed("gameplay.shadows_together"));
    CHECK(listed("variants.one_hit"));
    CHECK(listed("variants.auto_guard") && listed("variants.attack_turn") &&
          listed("variants.shadows_wait_down") && listed("variants.peaceful_shadows"));
    CHECK(strcmp(ico_sim_key_default("variants.peaceful_shadows"), "false") == 0);
    CHECK(strcmp(ico_sim_key_default("variants.auto_guard"), "true") == 0 &&
          strcmp(ico_sim_key_default("variants.attack_turn"), "true") == 0 &&
          strcmp(ico_sim_key_default("variants.shadows_wait_down"), "true") == 0);
    CHECK(listed("video.video_mode"));
}

static void test_text_and_hash(void)
{
    static char t0[4096], t1[4096], t2[4096];
    char shortbuf[10];
    char out[160];
    uint64_t h0, h1, h2, hs;
    int i;
    const char *k;

    h0 = ico_sim_keys_hash(t0, sizeof t0);
    /* every key, in order, unset */
    {
        const char *p = t0;

        for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
            const size_t kn = strlen(ICO_SIM_KEYS[i]);

            CHECK(strncmp(p, ICO_SIM_KEYS[i], kn) == 0 && p[kn] == '=' && p[kn + 1] == '\n');
            p += kn + 2;
        }
        CHECK(*p == '\0');
    }
    CHECK(ico_sim_keys_first_diff(t0, out, sizeof out) == NULL);
    CHECK(out[0] == '\0');
    /* the same twice; NULL text */
    CHECK(ico_sim_keys_hash(NULL, 0) == h0);
    /* a short buffer: the start, and the whole text's hash */
    hs = ico_sim_keys_hash(shortbuf, sizeof shortbuf);
    CHECK(hs == h0);
    CHECK(strlen(shortbuf) == sizeof shortbuf - 1 &&
          strncmp(shortbuf, t0, sizeof shortbuf - 1) == 0);

    /* one value changes the hash and is named */
    CHECK(ico_config_set_string("gameplay.mirror", "true") == 0);
    h1 = ico_sim_keys_hash(t1, sizeof t1);
    CHECK(h1 != h0);
    CHECK(strstr(t1, "\ngameplay.mirror=true\n") != NULL);
    k = ico_sim_keys_first_diff(t0, out, sizeof out);
    CHECK(k != NULL && strcmp(k, "gameplay.mirror") == 0);
    CHECK(strcmp(out, "gameplay.mirror= (now true)") == 0);
    CHECK(ico_sim_keys_first_diff(t1, out, sizeof out) == NULL);

    /* two differ: the earlier in the list is named */
    CHECK(ico_config_set_string("variants.one_hit", "true") == 0);
    h2 = ico_sim_keys_hash(t2, sizeof t2);
    CHECK(h2 != h1 && h2 != h0);
    k = ico_sim_keys_first_diff(t0, out, sizeof out);
    CHECK(k != NULL && strcmp(k, "gameplay.mirror") == 0);
    k = ico_sim_keys_first_diff(t1, out, sizeof out);
    CHECK(k != NULL && strcmp(k, "variants.one_hit") == 0);
    CHECK(strcmp(out, "variants.one_hit= (now true)") == 0);

    /* back to empty values: the unset text and hash again */
    CHECK(ico_config_set_string("gameplay.mirror", "") == 0);
    CHECK(ico_config_set_string("variants.one_hit", "") == 0);
    CHECK(ico_sim_keys_hash(NULL, 0) == h0);
    CHECK(ico_sim_keys_first_diff(t0, NULL, 0) == NULL);

    /* a saved text without a key reads it as its default */
    CHECK(ico_sim_keys_first_diff("", out, sizeof out) == NULL);
    CHECK(ico_sim_keys_first_diff(NULL, out, sizeof out) == NULL);
    CHECK(ico_config_set_string("gameplay.pit_time", "8") == 0);
    k = ico_sim_keys_first_diff("gameplay.mirror=\n", out, sizeof out);
    CHECK(k != NULL && strcmp(k, "gameplay.pit_time") == 0);
    CHECK(strcmp(out, "gameplay.pit_time=(absent) (now 8)") == 0);
    /* a short out buffer is cut, NUL-terminated */
    k = ico_sim_keys_first_diff("", out, 8);
    CHECK(k != NULL && strlen(out) == 7);
    /* the key at its default now: an older state without it is equal */
    CHECK(ico_config_set_string("gameplay.pit_time", "original") == 0);
    CHECK(ico_sim_keys_first_diff("gameplay.mirror=\n", out, sizeof out) == NULL && out[0] == '\0');
    CHECK(ico_config_set_string("gameplay.pit_time", "") == 0);
}

/* the text of t without the line of key (a state saved before the key
   existed) */
static void drop_line(const char *t, const char *key, char *out, size_t size)
{
    const size_t kn = strlen(key);
    size_t at = 0;
    const char *line = t;

    while (*line != '\0' && at + 1 < size) {
        const char *end = strchr(line, '\n');
        const size_t len = end != NULL ? (size_t)(end - line) + 1 : strlen(line);

        if (!(len > kn && memcmp(line, key, kn) == 0 && line[kn] == '=') && at + len < size) {
            memcpy(out + at, line, len);
            at += len;
        }
        line += len;
    }
    out[at] = '\0';
}

static void test_defaults(const char *dir)
{
    static char t[4096], older[4096];
    char out[160], toml[ICO_PATH_MAX];
    const char *k;
    int i;

    /* every key: unset is its default; a key not in the list never is */
    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        CHECK(ico_sim_key_is_default(ICO_SIM_KEYS[i], strlen(ICO_SIM_KEYS[i]), "", 0) == 1);
    }
    CHECK(ico_sim_key_is_default("gameplay.no_such", 16, "", 0) == 0);
    CHECK(ico_sim_key_default("gameplay.no_such") == NULL && ico_sim_key_default(NULL) == NULL);
    /* the default's text is, another value is not; the key's length counts */
    CHECK(ico_sim_key_default("gameplay.yorda_trips") != NULL &&
          strcmp(ico_sim_key_default("gameplay.yorda_trips"), "false") == 0);
    CHECK(ico_sim_key_is_default("gameplay.yorda_trips", 20, "false", 5) == 1);
    CHECK(ico_sim_key_is_default("gameplay.yorda_trips", 20, "true", 4) == 0);
    CHECK(ico_sim_key_is_default("gameplay.yorda_tripsX", 20, "false", 5) == 1);
    CHECK(ico_sim_key_is_default("gameplay.yorda_trip", 19, "false", 5) == 0);
    CHECK(strcmp(ico_sim_key_default("gameplay.pit_time"), "original") == 0 &&
          strcmp(ico_sim_key_default("gameplay.hold_type"), "disc") == 0 &&
          strcmp(ico_sim_key_default("video.anti_aliasing"), "off") == 0 &&
          strcmp(ico_sim_key_default("variants.aggression"), "own") == 0);
    /* a key without one fixed text: only unset */
    CHECK(ico_sim_key_default("variants.barrier_hits") == NULL &&
          ico_sim_key_default("video.video_mode") == NULL);
    CHECK(ico_sim_key_is_default("variants.barrier_hits", 21, "5", 1) == 0);

    /* a state saved before gameplay.yorda_trips existed */
    (void)ico_sim_keys_hash(t, sizeof t);
    drop_line(t, "gameplay.yorda_trips", older, sizeof older);
    CHECK(strstr(older, "gameplay.yorda_trips=") == NULL && strlen(older) < strlen(t));
    CHECK(ico_sim_keys_first_diff(older, out, sizeof out) == NULL); /* unset now: equal */
    CHECK(ico_config_set_bool("gameplay.yorda_trips", 0) == 0);
    CHECK(ico_sim_keys_first_diff(older, out, sizeof out) == NULL); /* false, the default */
    CHECK(ico_config_set_bool("gameplay.yorda_trips", 1) == 0);
    k = ico_sim_keys_first_diff(older, out, sizeof out);
    CHECK(k != NULL && strcmp(k, "gameplay.yorda_trips") == 0 &&
          strcmp(out, "gameplay.yorda_trips=(absent) (now true)") == 0);
    CHECK(ico_config_set_string("gameplay.yorda_trips", "") == 0);
    /* a state saved before gameplay.shadows_together existed: loads with the
       option off (the game's turns), is refused with it on */
    CHECK(strcmp(ico_sim_key_default("gameplay.shadows_together"), "false") == 0);
    drop_line(t, "gameplay.shadows_together", older, sizeof older);
    CHECK(strstr(older, "gameplay.shadows_together=") == NULL && strlen(older) < strlen(t));
    CHECK(ico_config_set_bool("gameplay.shadows_together", 0) == 0);
    CHECK(ico_sim_keys_first_diff(older, out, sizeof out) == NULL);
    CHECK(ico_config_set_bool("gameplay.shadows_together", 1) == 0);
    k = ico_sim_keys_first_diff(older, out, sizeof out);
    CHECK(k != NULL && strcmp(k, "gameplay.shadows_together") == 0 &&
          strcmp(out, "gameplay.shadows_together=(absent) (now true)") == 0);
    CHECK(ico_config_set_string("gameplay.shadows_together", "") == 0);
    /* a key without one fixed default, absent, set now: differs */
    drop_line(t, "variants.barrier_hits", older, sizeof older);
    CHECK(ico_sim_keys_first_diff(older, out, sizeof out) == NULL);
    CHECK(ico_config_set_string("variants.barrier_hits", "7") == 0);
    k = ico_sim_keys_first_diff(older, out, sizeof out);
    CHECK(k != NULL && strcmp(k, "variants.barrier_hits") == 0);
    CHECK(ico_config_set_string("variants.barrier_hits", "") == 0);

    /* the first-run config.toml: every listed key it writes is at its
       default (the table and the file agree) */
    snprintf(toml, sizeof toml, "%s/sim-keys-test-first-run.toml", dir);
    remove(toml);
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_write_first_run() == 0);
    ico_config_reset(toml, "no-such.ini");
    for (i = 0; ICO_SIM_KEYS[i] != NULL; i++) {
        const char *v = ico_config_get_string(ICO_SIM_KEYS[i], NULL);

        if (v != NULL &&
            !ico_sim_key_is_default(ICO_SIM_KEYS[i], strlen(ICO_SIM_KEYS[i]), v, strlen(v))) {
            printf("  the first run writes %s = %s, its default %s\n", ICO_SIM_KEYS[i], v,
                   ico_sim_key_default(ICO_SIM_KEYS[i]) ? ico_sim_key_default(ICO_SIM_KEYS[i])
                                                        : "(none)");
            CHECK(0);
        }
    }
    CHECK(ico_config_get_string("gameplay.yorda_trips", NULL) != NULL);
    CHECK(ico_config_get_string("gameplay.shadows_together", NULL) != NULL);
    remove(toml);
}

int main(int argc, char **argv)
{
    char toml[ICO_PATH_MAX];

    snprintf(toml, sizeof toml, "%s/sim-keys-test-absent.toml", argc > 1 ? argv[1] : ".");
    remove(toml);
    ico_config_reset(toml, "no-such.ini");
    test_list();
    test_text_and_hash();
    test_defaults(argc > 1 ? argv[1] : ".");
    if (fails != 0) {
        printf("sim_keys_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("sim_keys_test: ok\n");
    return 0;
}
