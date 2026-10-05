/*
 * port/input/pad_script.c
 *
 * The --pad-script reader and the scripted pad's timeline (pad_script.h).
 * port/input/pad_host.c asks it what to report.
 */
#include "pad_script.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"

typedef struct Entry {
    unsigned int tick;
    IcoPadFrame frame;
} Entry;

static Entry *entries;

static int count;

static int loaded;

static unsigned int current_tick;

static const IcoPadFrame released = {0, ICO_PAD_STICK_CENTRE, ICO_PAD_STICK_CENTRE,
                                     ICO_PAD_STICK_CENTRE, ICO_PAD_STICK_CENTRE};

void ico_pad_script_clear(void)
{
    free(entries);
    entries = NULL;
    count = 0;
    loaded = 0;
}

int ico_pad_script_active(void)
{
    return loaded;
}

int ico_pad_script_count(void)
{
    return count;
}

void ico_pad_script_set_tick(unsigned int tick)
{
    current_tick = tick;
}

unsigned int ico_pad_script_tick(void)
{
    return current_tick;
}

void ico_pad_script_frame_at(unsigned int tick, IcoPadFrame *out)
{
    int lo = 0;
    int hi = count; /* first entry with entry.tick > tick */

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        if (entries[mid].tick <= tick) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *out = lo == 0 ? released : entries[lo - 1].frame;
}

void ico_pad_script_frame(IcoPadFrame *out)
{
    ico_pad_script_frame_at(current_tick, out);
}

/* --- parsing ------------------------------------------------------------- */

/* One unsigned number token in [0, max]; base 10, 16 or 0 (C prefixes). */
static int parse_number(const char *tok, int base, unsigned long max, unsigned long *out)
{
    char *end;
    unsigned long v;

    if (tok[0] == '-' || tok[0] == '+') {
        return -1;
    }
    errno = 0;
    v = strtoul(tok, &end, base);
    if (errno != 0 || end == tok || *end != '\0' || v > max) {
        return -1;
    }
    *out = v;
    return 0;
}

static int fail(const char *name, int line, const char *what, const char *tok)
{
    fprintf(stderr, "pad-script: %s:%d: %s%s%s%s\n", name, line, what, tok ? " '" : "",
            tok ? tok : "", tok ? "'" : "");
    return -1;
}

/* Parses one line (comments already cut); appends to *list. 0, 1 for a
   blank line, -1 on error. */
static int parse_line(char *s, const char *name, int line, Entry **list, int *n, int *cap)
{
    char *tok[7];
    int ntok = 0;
    unsigned long v;
    Entry e;
    char *p = s;

    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        if (ntok == 7) {
            return fail(name, line, "too many fields (want: tick buttons [lx ly rx ry])", NULL);
        }
        tok[ntok++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
    }
    if (ntok == 0) {
        return 1;
    }
    if (ntok != 2 && ntok != 6) {
        return fail(name, line, "want 2 or 6 fields: tick buttons [lx ly rx ry]", NULL);
    }
    if (parse_number(tok[0], 10, 0xFFFFFFFFul, &v) != 0) {
        return fail(name, line, "bad tick (decimal Main tick)", tok[0]);
    }
    e.tick = (unsigned int)v;
    if (parse_number(tok[1], 16, 0xFFFFul, &v) != 0) {
        return fail(name, line, "bad buttons (hex, 0 to ffff)", tok[1]);
    }
    e.frame = released;
    e.frame.buttons = (unsigned int)v;
    if (ntok == 6) {
        unsigned char *axis[4] = {&e.frame.lx, &e.frame.ly, &e.frame.rx, &e.frame.ry};
        int i;

        for (i = 0; i < 4; i++) {
            if (parse_number(tok[2 + i], 0, 255, &v) != 0) {
                return fail(name, line, "bad stick value (0 to 255)", tok[2 + i]);
            }
            *axis[i] = (unsigned char)v;
        }
    }
    if (*n > 0 && e.tick <= (*list)[*n - 1].tick) {
        return fail(name, line, "ticks must increase from line to line", tok[0]);
    }
    if (*n == *cap) {
        int ncap = *cap ? *cap * 2 : 64;
        Entry *grown = realloc(*list, (size_t)ncap * sizeof(Entry));

        if (grown == NULL) {
            return fail(name, line, "out of memory", NULL);
        }
        *list = grown;
        *cap = ncap;
    }
    (*list)[(*n)++] = e;
    return 0;
}

int ico_pad_script_parse(const char *text, const char *name)
{
    Entry *list = NULL;
    int n = 0;
    int cap = 0;
    int line = 0;
    const char *p = text;

    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char *buf = malloc(len + 1);
        char *hash;
        int r;

        line++;
        if (buf == NULL) {
            free(list);
            return fail(name, line, "out of memory", NULL);
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        hash = strchr(buf, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        r = parse_line(buf, name, line, &list, &n, &cap);
        free(buf);
        if (r < 0) {
            free(list);
            return -1;
        }
        p += len;
        if (*p == '\n') {
            p++;
        }
    }
    ico_pad_script_clear();
    entries = list;
    count = n;
    loaded = 1;
    return 0;
}

int ico_pad_script_load(const char *path)
{
    FILE *f = ico_fopen(path, "rb"); /* UTF-8 path (ini pad_script=) */
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    int r;

    if (f == NULL) {
        fprintf(stderr, "pad-script: cannot open %s\n", path);
        return -1;
    }
    for (;;) {
        size_t got;

        if (cap - len < 4096) {
            char *grown = realloc(text, cap + 65536);

            if (grown == NULL) {
                free(text);
                fclose(f);
                fprintf(stderr, "pad-script: %s: out of memory\n", path);
                return -1;
            }
            text = grown;
            cap += 65536;
        }
        got = fread(text + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0) {
            break;
        }
    }
    if (ferror(f)) {
        free(text);
        fclose(f);
        fprintf(stderr, "pad-script: cannot read %s\n", path);
        return -1;
    }
    fclose(f);
    text[len] = '\0';
    if (memchr(text, '\0', len) != NULL) {
        free(text);
        fprintf(stderr, "pad-script: %s: not a text file\n", path);
        return -1;
    }
    r = ico_pad_script_parse(text, path);
    free(text);
    return r;
}
