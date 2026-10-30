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
 *               list when two differ; a key the saved text lacks reads as
 *               unset
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
    CHECK(listed("variants.one_hit"));
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

    /* a saved text without a key reads it as unset */
    CHECK(ico_sim_keys_first_diff("", out, sizeof out) == NULL);
    CHECK(ico_sim_keys_first_diff(NULL, out, sizeof out) == NULL);
    CHECK(ico_config_set_string("gameplay.pit_time", "dive") == 0);
    k = ico_sim_keys_first_diff("gameplay.mirror=\n", out, sizeof out);
    CHECK(k != NULL && strcmp(k, "gameplay.pit_time") == 0);
    CHECK(strcmp(out, "gameplay.pit_time= (now dive)") == 0);
    /* a short out buffer is cut, NUL-terminated */
    k = ico_sim_keys_first_diff("", out, 8);
    CHECK(k != NULL && strlen(out) == 7);
}

int main(int argc, char **argv)
{
    char toml[ICO_PATH_MAX];

    snprintf(toml, sizeof toml, "%s/sim-keys-test-absent.toml", argc > 1 ? argv[1] : ".");
    remove(toml);
    ico_config_reset(toml, "no-such.ini");
    test_list();
    test_text_and_hash();
    if (fails != 0) {
        printf("sim_keys_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("sim_keys_test: ok\n");
    return 0;
}
