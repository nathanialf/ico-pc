/*
 * port/platform/state_legacy.c
 *
 * Legacy tables (state_legacy.h): the index, the files, inflated.
 */
#include "state_legacy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"
#include "state_syms.h"

static void say(char *why, size_t n, const char *s)
{
    if (why != NULL && n > 0) {
        snprintf(why, n, "%s", s);
    }
}

/* the next line of [p, end): its bounds, NUL-free; returns the line after */
static const char *line_of(const char *p, const char *end, const char **le)
{
    const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));

    *le = nl != NULL ? nl : end;
    return nl != NULL ? nl + 1 : end;
}

int ico_legacy_find(IcoLegacyRead rd, void *user, const char *platform, const char *commit,
                    char *name, size_t n)
{
    void *data = NULL;
    size_t len = 0;
    const char *p, *end;
    int found = 0;

    if (rd == NULL || rd(user, "index.txt", &data, &len) != 0) {
        return -1;
    }
    p = (const char *)data;
    end = p + len;
    while (p < end && !found) {
        const char *le;
        const char *next = line_of(p, end, &le);
        char l[256], plat[64], kind[16], nm[64], cm[96];

        if (le - p > 0 && le - p < (long)sizeof l && p[0] != '#') {
            memcpy(l, p, (size_t)(le - p));
            l[le - p] = '\0';
            if (sscanf(l, "%63s %15s %63s %95s", plat, kind, nm, cm) == 4 &&
                strcmp(plat, platform) == 0 && strcmp(cm, commit) == 0) {
                snprintf(name, n, "%s", nm);
                found = 1;
            }
        }
        p = next;
    }
    free(data);
    return found;
}

static void *inflate_raw(const void *z, size_t n, size_t *out_n)
{
    return tinfl_decompress_mem_to_heap(z, n, out_n, 0);
}

static int read_file(IcoLegacyRead rd, void *user, const char *dir, const char *file, void **data,
                     size_t *n)
{
    char path[160];

    snprintf(path, sizeof path, "%s/%s", dir, file);
    return rd(user, path, data, n);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

IcoLegacyTable *ico_legacy_load(IcoLegacyRead rd, void *user, const char *name, char *why,
                                size_t why_n)
{
    IcoLegacyTable *t = (IcoLegacyTable *)calloc(1, sizeof *t);
    void *man = NULL, *gz = NULL, *sec = NULL, *idx = NULL, *runs = NULL;
    size_t man_n = 0, gz_n = 0, sec_n = 0, idx_n = 0, runs_n = 0, text_n = 0, files = 0;
    char *syms = NULL;
    size_t syms_n = 0, i, k, at;
    const char *p, *end;

    if (t == NULL) {
        say(why, why_n, "no memory");
        return NULL;
    }
    snprintf(t->name, sizeof t->name, "%s", name);
    if (read_file(rd, user, name, "manifest.txt", &man, &man_n) != 0) {
        say(why, why_n, "its manifest is missing");
        goto fail;
    }
    p = (const char *)man;
    end = p + man_n;
    while (p < end) {
        const char *le;
        const char *next = line_of(p, end, &le);
        char l[256];

        if (le - p > 0 && le - p < (long)sizeof l) {
            memcpy(l, p, (size_t)(le - p));
            l[le - p] = '\0';
            if (strncmp(l, "commit ", 7) == 0) {
                snprintf(t->commit, sizeof t->commit, "%.95s", l + 7);
            } else if (strcmp(l, "kind rc") == 0) {
                t->rc = 1;
            } else if (strncmp(l, "texts ", 6) == 0) {
                unsigned long long a = 0, b = 0;

                if (sscanf(l + 6, "%llu %llu", &a, &b) == 2) {
                    t->n_run = (size_t)a;
                    text_n = (size_t)b;
                }
            } else if (strncmp(l, "text_files ", 11) == 0) {
                files = (size_t)strtoul(l + 11, NULL, 10);
            } else if (strncmp(l, "hash_rules ", 11) == 0) {
                t->hash_rules = (int)strtol(l + 11, NULL, 10);
            }
        }
        p = next;
    }
    if (t->commit[0] == '\0' || files == 0 || files > 64 || t->n_run == 0) {
        say(why, why_n, "its manifest is damaged");
        goto fail;
    }
    if (read_file(rd, user, name, "syms.gz", &gz, &gz_n) != 0 ||
        read_file(rd, user, name, "sections.txt", &sec, &sec_n) != 0) {
        say(why, why_n, "its symbol file or its sections are missing");
        goto fail;
    }
    syms = ico_symtab_gunzip(gz, gz_n, &syms_n);
    if (syms == NULL) {
        say(why, why_n, "its symbol file does not inflate");
        goto fail;
    }
    t->syms = (char *)malloc(syms_n + sec_n + 2);
    if (t->syms == NULL) {
        say(why, why_n, "no memory");
        goto fail;
    }
    memcpy(t->syms, syms, syms_n);
    t->syms_n = syms_n;
    if (syms_n > 0 && syms[syms_n - 1] != '\n') {
        t->syms[t->syms_n++] = '\n';
    }
    memcpy(t->syms + t->syms_n, sec, sec_n);
    t->syms_n += sec_n;
    /* .rodata's line */
    p = (const char *)sec;
    end = p + sec_n;
    while (p < end) {
        const char *le;
        const char *next = line_of(p, end, &le);
        char l[256], nm[64], fl[16];
        unsigned long long a = 0, s = 0;

        if (le - p > 0 && le - p < (long)sizeof l) {
            memcpy(l, p, (size_t)(le - p));
            l[le - p] = '\0';
            if (sscanf(l, "section %llx %llx %15s %63s", &a, &s, fl, nm) == 4 &&
                strcmp(nm, ".rodata") == 0) {
                t->ro_lo = a;
                t->ro_hi = a + s;
            }
        }
        p = next;
    }
    if (t->ro_hi <= t->ro_lo || t->ro_hi - t->ro_lo > (256u << 20)) {
        say(why, why_n, "its sections name no .rodata");
        goto fail;
    }
    if (read_file(rd, user, name, "texts.idx", &idx, &idx_n) != 0 ||
        (runs = inflate_raw(idx, idx_n, &runs_n)) == NULL || runs_n != t->n_run * 8) {
        say(why, why_n, "its text index is missing or damaged");
        goto fail;
    }
    t->run = (uint32_t *)malloc(t->n_run * 2 * sizeof *t->run);
    t->ro = (uint8_t *)calloc(1, (size_t)(t->ro_hi - t->ro_lo));
    if (t->run == NULL || t->ro == NULL) {
        say(why, why_n, "no memory");
        goto fail;
    }
    for (i = 0; i < t->n_run; i++) {
        t->run[2 * i] = get32((const uint8_t *)runs + 8 * i);
        t->run[2 * i + 1] = get32((const uint8_t *)runs + 8 * i + 4);
        if (t->run[2 * i] < t->ro_lo || (uint64_t)t->run[2 * i] + t->run[2 * i + 1] > t->ro_hi ||
            (i > 0 && t->run[2 * i] < (uint64_t)t->run[2 * i - 2] + t->run[2 * i - 1])) {
            say(why, why_n, "its text index is out of order");
            goto fail;
        }
    }
    /* the runs' bytes, in order, across the files */
    i = 0;  /* the run */
    at = 0; /* bytes of run i placed */
    for (k = 0; k < files; k++) {
        char file[32];
        void *z = NULL, *b = NULL;
        size_t z_n = 0, b_n = 0, j = 0;

        snprintf(file, sizeof file, "texts.%u.z", (unsigned)k);
        if (read_file(rd, user, name, file, &z, &z_n) != 0 ||
            (b = inflate_raw(z, z_n, &b_n)) == NULL) {
            free(z);
            say(why, why_n, "a text file is missing or damaged");
            goto fail;
        }
        free(z);
        while (j < b_n && i < t->n_run) {
            const size_t want = t->run[2 * i + 1] - at;
            const size_t take = want < b_n - j ? want : b_n - j;

            memcpy(t->ro + (t->run[2 * i] - t->ro_lo) + at, (const uint8_t *)b + j, take);
            j += take;
            at += take;
            text_n -= take < text_n ? take : text_n;
            if (at == t->run[2 * i + 1]) {
                i++;
                at = 0;
            }
        }
        free(b);
        if (j != b_n) {
            say(why, why_n, "the text files hold more than the index");
            goto fail;
        }
    }
    if (i != t->n_run || text_n != 0) {
        say(why, why_n, "the text files hold less than the index");
        goto fail;
    }
    {
        void *hz = NULL, *hb = NULL;
        size_t hz_n = 0, hb_n = 0, j;

        if (read_file(rd, user, name, "hashes.z", &hz, &hz_n) != 0 ||
            (hb = inflate_raw(hz, hz_n, &hb_n)) == NULL || hb_n % 20 != 0 || hb_n == 0) {
            free(hz);
            free(hb);
            say(why, why_n, "its code hashes are missing or damaged");
            goto fail;
        }
        free(hz);
        t->n_hash = hb_n / 20;
        t->hash = (uint64_t *)malloc(t->n_hash * 3 * sizeof *t->hash);
        if (t->hash == NULL) {
            free(hb);
            say(why, why_n, "no memory");
            goto fail;
        }
        for (j = 0; j < t->n_hash; j++) {
            const uint8_t *e = (const uint8_t *)hb + 20 * j;

            t->hash[3 * j] = get32(e);
            t->hash[3 * j + 1] = (uint64_t)get32(e + 4) | (uint64_t)get32(e + 8) << 32;
            t->hash[3 * j + 2] = (uint64_t)get32(e + 12) | (uint64_t)get32(e + 16) << 32;
        }
        free(hb);
    }
    free(man);
    free(gz);
    free(sec);
    free(idx);
    free(runs);
    free(syms);
    say(why, why_n, "");
    return t;
fail:
    free(man);
    free(gz);
    free(sec);
    free(idx);
    free(runs);
    free(syms);
    ico_legacy_free(t);
    return NULL;
}

void ico_legacy_free(IcoLegacyTable *t)
{
    if (t == NULL) {
        return;
    }
    free(t->syms);
    free(t->ro);
    free(t->run);
    free(t->hash);
    free(t);
}

IcoSymTab *ico_legacy_symtab(const IcoLegacyTable *t, const IcoSymTabEnv *env, char *why,
                             size_t why_n)
{
    const char *k = " T ico_diag_init\n";
    const char *hit = NULL, *p = t->syms, *end = t->syms + t->syms_n;
    unsigned long long a = 0;
    IcoSymTabEnv e = *env;

    while (p < end) {
        const char *q = (const char *)memchr(p, ' ', (size_t)(end - p));

        if (q == NULL) {
            break;
        }
        if ((size_t)(end - q) >= strlen(k) && memcmp(q, k, strlen(k)) == 0) {
            hit = q;
            break;
        }
        p = q + 1;
    }
    if (hit == NULL) {
        say(why, why_n, "its symbol file names no ico_diag_init");
        return NULL;
    }
    for (p = hit; p > t->syms && p[-1] != '\n'; p--) {}
    if (sscanf(p, "%llx", &a) != 1) {
        say(why, why_n, "its symbol file is damaged");
        return NULL;
    }
    e.anchor = "ico_diag_init";
    e.anchor_addr = env->image_lo + (uint64_t)a;
    e.check = NULL;
    return ico_symtab_parse(t->syms, t->syms_n, &e, why, why_n);
}

int ico_legacy_run(const IcoLegacyTable *t, uint64_t off, uint64_t *lo, uint64_t *hi)
{
    size_t a = 0, b = t->n_run;

    while (a < b) {
        const size_t mid = a + (b - a) / 2;

        if (t->run[2 * mid] <= off) {
            a = mid + 1;
        } else {
            b = mid;
        }
    }
    if (a > 0 && off - t->run[2 * (a - 1)] < t->run[2 * (a - 1) + 1]) {
        *lo = t->run[2 * (a - 1)];
        *hi = (uint64_t)t->run[2 * (a - 1)] + t->run[2 * (a - 1) + 1];
        return 1;
    }
    return 0;
}
