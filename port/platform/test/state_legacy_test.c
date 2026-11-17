/*
 * port/platform/test/state_legacy_test.c
 *
 * The committed legacy tables (port/state_legacy, issue 217) read by the
 * loader the game uses (state_legacy.c): every table the index names loads,
 * its counts are its manifest's, its code hashes lie in address order under
 * the current code hash rules, and its symbol file places at an image the
 * way an earlier version's state gives one.  The index's lookup by
 * platform and commit (the release, the rc, another platform, an unknown
 * commit), a table that is missing a file, one whose file is cut short, and
 * one of the first format (no text_index line: its runs do not say where a
 * literal starts), refused by name.
 * The argument is the tables' folder in the source tree; no build output,
 * no game.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "code_hash.h"
#include "state_legacy.h"
#include "state_syms.h"

typedef struct Dir {
    const char *root;
    const char *drop; /* a path the reader says is missing, or NULL */
    const char *cut;  /* a path the reader returns half of, or NULL */
    int apk;          /* as Android's APK: no asset ends in .gz (packaging un-gzips them) */
    int old_index;    /* every manifest.txt without its text_index line (the first format) */
} Dir;

static int dir_read(void *user, const char *path, void **data, size_t *n, char *err, size_t err_n)
{
    const Dir *d = (const Dir *)user;
    char full[1024];
    FILE *f;
    long len;
    void *b;

    if (d->drop != NULL && strcmp(path, d->drop) == 0) {
        snprintf(err, err_n, "dropped by the test");
        return -1;
    }
    if (d->apk && strlen(path) > 3 && strcmp(path + strlen(path) - 3, ".gz") == 0) {
        snprintf(err, err_n, "an APK holds no .gz asset (its packaging un-gzips and renames it)");
        return -1;
    }
    snprintf(full, sizeof full, "%s/%s", d->root, path);
    f = fopen(full, "rb");
    if (f == NULL) {
        snprintf(err, err_n, "%s", strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    b = malloc(len > 0 ? (size_t)len : 1);
    if (b == NULL || fread(b, 1, (size_t)len, f) != (size_t)len) {
        free(b);
        fclose(f);
        return -1;
    }
    fclose(f);
    *data = b;
    *n = (size_t)len;
    if (d->cut != NULL && strcmp(path, d->cut) == 0) {
        *n = (size_t)len / 2;
    }
    if (d->old_index && strlen(path) > 13 &&
        strcmp(path + strlen(path) - 13, "/manifest.txt") == 0) {
        char *s = (char *)b, *l = NULL, *e;
        size_t i;

        for (i = 0; i + 11 <= *n; i++) {
            if ((i == 0 || s[i - 1] == '\n') && memcmp(s + i, "text_index ", 11) == 0) {
                l = s + i;
                break;
            }
        }
        if (l != NULL) {
            e = (char *)memchr(l, '\n', *n - (size_t)(l - s));
            e = e != NULL ? e + 1 : s + *n;
            memmove(l, e, *n - (size_t)(e - s));
            *n -= (size_t)(e - l);
        }
    }
    return 0;
}

/* the manifest's value of `key` (its first number), or -1 */
static long manifest_num(const Dir *d, const char *name, const char *key)
{
    char path[256];
    void *m = NULL;
    size_t n = 0;
    long v = -1;
    char *t, *p;

    snprintf(path, sizeof path, "%s/manifest.txt", name);
    char err[160];

    if (dir_read((void *)d, path, &m, &n, err, sizeof err) != 0) {
        return -1;
    }
    t = (char *)malloc(n + 1);
    if (t != NULL) {
        memcpy(t, m, n);
        t[n] = '\0';
        for (p = strtok(t, "\n"); p != NULL; p = strtok(NULL, "\n")) {
            if (strncmp(p, key, strlen(key)) == 0 && p[strlen(key)] == ' ') {
                v = strtol(p + strlen(key) + 1, NULL, 10);
            }
        }
    }
    free(t);
    free(m);
    return v;
}

static void check_table(const Dir *d, const char *name, const char *platform, const char *commit)
{
    char why[200], found[64];
    IcoLegacyTable *t;
    IcoSymTabEnv env;
    IcoSymTab *st;
    size_t i;
    int sorted = 1;

    CHECK(ico_legacy_find(dir_read, (void *)d, platform, commit, found, sizeof found) == 1 &&
              strcmp(found, name) == 0,
          "%s: the index finds it by %s %s", name, platform, commit);
    t = ico_legacy_load(dir_read, (void *)d, name, why, sizeof why);
    CHECK(t != NULL, "%s loads: %s", name, t != NULL ? "" : why);
    if (t == NULL) {
        return;
    }
    CHECK(strcmp(t->commit, commit) == 0, "%s: its manifest's commit %s", name, t->commit);
    CHECK((long)t->n_hash == manifest_num(d, name, "hashes") && t->n_hash > 1000,
          "%s: %zu code hashes, as its manifest says", name, t->n_hash);
    CHECK((long)t->n_run == manifest_num(d, name, "texts") && t->n_run > 1000,
          "%s: %zu text runs, as its manifest says", name, t->n_run);
    CHECK(t->hash_rules == ICO_CODE_HASH_RULES, "%s: hashes under rules %d", name, t->hash_rules);
    for (i = 1; i < t->n_hash; i++) {
        sorted &= t->hash[3 * i] > t->hash[3 * (i - 1)];
    }
    CHECK(sorted, "%s: the code hashes in address order", name);
    CHECK(t->ro_hi > t->ro_lo && t->n_run > 0 && t->run[0] >= t->ro_lo &&
              (uint64_t)t->run[2 * (t->n_run - 1)] + t->run[2 * (t->n_run - 1) + 1] <= t->ro_hi,
          "%s: its text runs inside its .rodata", name);
    {
        uint64_t lo = 0, hi = 0;
        const uint32_t off = t->run[2 * (t->n_run / 2)];
        int ls = -1;
        long nls = 0;

        CHECK(ico_legacy_run(t, off, &lo, &hi, &ls) == 1 && lo == off &&
                  t->ro[off - t->ro_lo + (hi - lo) - 1] == 0 && ls == t->lstart[t->n_run / 2],
              "%s: a run found by its start ends in a zero byte, its literal-start flag given",
              name);
        for (i = 0; i < t->n_run; i++) {
            nls += t->lstart[i] != 0;
        }
        CHECK(nls == manifest_num(d, name, "lstarts") && nls > 0 && nls < (long)t->n_run,
              "%s: %ld runs start a literal, as its manifest says", name, nls);
    }
    /* placed where an Android state's ranges put an image */
    memset(&env, 0, sizeof env);
    env.image_lo = 0x0000007768720000ull;
    env.image_hi = env.image_lo + 0x3000000u;
    env.text_lo = env.image_lo;
    env.text_hi = env.image_hi;
    env.arch = ICO_CODE_A64;
    st = ico_legacy_symtab(t, &env, why, sizeof why);
    CHECK(st != NULL && st->n_unit > 10000 && st->code_from,
          "%s: its symbol file places at an image (%s)", name, st != NULL ? "" : why);
    if (st != NULL) {
        size_t hit = 0;

        for (i = 0; i < st->n_unit; i++) {
            const uint64_t off = st->unit[i].start - env.image_lo;
            size_t a = 0, b = t->n_hash;

            while (a < b) {
                const size_t mid = a + (b - a) / 2;

                if (t->hash[3 * mid] < off) {
                    a = mid + 1;
                } else {
                    b = mid;
                }
            }
            hit += a < t->n_hash && t->hash[3 * a] == off;
        }
        CHECK(hit == st->n_unit, "%s: every function the table cuts has its hashes (%zu of %zu)",
              name, hit, st->n_unit);
    }
    ico_symtab_free(st);
    ico_legacy_free(t);
}

int main(int argc, char **argv)
{
    Dir d;
    char why[200], found[64];
    IcoLegacyTable *t;

    if (argc < 2) {
        fprintf(stderr, "usage: state_legacy_test <port/state_legacy>\n");
        return 2;
    }
    memset(&d, 0, sizeof d);
    d.root = argv[1];
    d.apk = 1; /* every table loads by the names an APK keeps */
    check_table(&d, "v0.11.7-android", "android-arm64", "v0.11.6-183-g66d9d0dcba");
    check_table(&d, "v0.11.8-android", "android-arm64", "v0.11.7-68-gaeb00e6100");
    check_table(&d, "v0.11.8-rc4-android", "android-arm64", "v0.11.7-66-gaf8507442c");
    /* no table: another platform's, an unknown commit, no index */
    CHECK(ico_legacy_find(dir_read, &d, "windows-x64", "v0.11.7-68-gaeb00e6100", found,
                          sizeof found) == 0,
          "no table for another platform");
    CHECK(ico_legacy_find(dir_read, &d, "android-arm64", "v0.11.4-211-g194e7bbe41", found,
                          sizeof found) == 0,
          "no table for the reporter's v0.11.5 (another layout)");
    d.drop = "index.txt";
    CHECK(ico_legacy_find(dir_read, &d, "android-arm64", "v0.11.7-68-gaeb00e6100", found,
                          sizeof found) == -1,
          "no index: -1");
    /* a missing file and one cut short */
    d.drop = "v0.11.8-android/hashes.z";
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL && strstr(why, "v0.11.8-android/hashes.z") != NULL &&
              strstr(why, "dropped by the test") != NULL,
          "no hashes file, named with the reader's reason: %s", why);
    ico_legacy_free(t);
    d.drop = "v0.11.8-android/sections.txt";
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL && strstr(why, "cannot read v0.11.8-android/sections.txt: dropped") != NULL,
          "no sections file, named: %s", why);
    ico_legacy_free(t);
    d.drop = NULL;
    d.cut = "v0.11.8-android/texts.1.z";
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL, "a text file cut short: %s", t == NULL ? why : "loaded");
    ico_legacy_free(t);
    d.cut = NULL;
    d.old_index = 1;
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL && strstr(why, "text index is of format 1, not 2") != NULL &&
              strstr(why, "rebuilt") != NULL,
          "a table of the first format (no text_index): refused, named: %s",
          t == NULL ? why : "loaded");
    ico_legacy_free(t);
    return ico_check_summary("state_legacy");
}
