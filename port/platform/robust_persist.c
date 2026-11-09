/*
 * port/platform/robust_persist.c
 *
 * The port's own [dev] gpu_robust turning itself off (robust_persist.h).
 */
#include "robust_persist.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_reason[] = "losses";

/* the text from s to the next ';' or the end, trimmed, into out; s moved
   past the separator; -1 when out is too small */
static int field(const char **s, char *out, size_t size)
{
    const char *p = *s;
    const char *end = strchr(p, ';');
    const char *stop = end != NULL ? end : p + strlen(p);
    size_t n;

    while (p < stop && isspace((unsigned char)*p)) {
        p++;
    }
    while (stop > p && isspace((unsigned char)stop[-1])) {
        stop--;
    }
    n = (size_t)(stop - p);
    if (n >= size) {
        return -1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    *s = end != NULL ? end + 1 : p + strlen(p);
    return 0;
}

int robust_mark_parse(const char *value, RobustMark *out)
{
    char reason[16], num[16], state[16];
    const char *s = value;
    char *endp;
    long n;

    if (value == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (field(&s, reason, sizeof(reason)) != 0 || strcmp(reason, k_reason) != 0 ||
        field(&s, out->version, sizeof(out->version)) != 0 || out->version[0] == '\0' ||
        field(&s, num, sizeof(num)) != 0 || field(&s, state, sizeof(state)) != 0) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    n = strtol(num, &endp, 10);
    if (num[0] == '\0' || *endp != '\0' || n < 0 || n > ROBUST_PERSIST_CLEAN_STARTS) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    out->clean = (int)n;
    out->running = strcmp(state, "run") == 0;
    return 0;
}

int robust_mark_format(char *out, size_t size, const RobustMark *mark)
{
    const int n = snprintf(out, size, "%s; %s; %d; %s", k_reason, mark->version, mark->clean,
                           mark->running ? "run" : "-");
    return n > 0 && (size_t)n < size ? 0 : -1;
}

RobustStep robust_persist_step(const RobustMark *old, const char *version, int prevLost,
                               int prevEnded)
{
    RobustStep s;

    memset(&s, 0, sizeof(s));
    if (old == NULL) {
        return s;
    }
    s.mark = *old;
    s.mark.running = 0;
    if (version == NULL || strcmp(old->version, version) != 0) {
        s.action = ROBUST_OFF_VERSION;
        return s;
    }
    if (prevLost || prevEnded) {
        s.mark.clean = 0;
    } else if (old->running) {
        s.mark.clean++;
    }
    s.action = s.mark.clean >= ROBUST_PERSIST_CLEAN_STARTS ? ROBUST_OFF_CLEAN : ROBUST_STAYS;
    return s;
}

int robust_persist_line(char *out, size_t size, const RobustStep *step, const RobustMark *old,
                        const char *version, const char *cfgName)
{
    const char *cfg = cfgName != NULL ? cfgName : "config.toml";
    int n = 0;

    switch (step->action) {
    case ROBUST_STAYS:
        n = snprintf(out, size,
                     "ico_pc: robust access on (set by the game after 2 lost starts on %s): %d "
                     "clean starts of %d before it turns off",
                     old->version, step->mark.clean, ROBUST_PERSIST_CLEAN_STARTS);
        break;
    case ROBUST_OFF_CLEAN:
        n = snprintf(out, size,
                     "ico_pc: robust access turned off (set by the game after 2 lost starts on "
                     "%s): %d clean starts in a row; [dev] gpu_robust and gpu_robust_by are "
                     "removed from %s, and two lost starts in a row turn it on again",
                     old->version, ROBUST_PERSIST_CLEAN_STARTS, cfg);
        break;
    case ROBUST_OFF_VERSION:
        n = snprintf(out, size,
                     "ico_pc: robust access turned off (set by the game after 2 lost starts on "
                     "%s): this is version %s, so those losses were another build's; [dev] "
                     "gpu_robust and gpu_robust_by are removed from %s, and two lost starts in a "
                     "row turn it on again",
                     old->version, version != NULL ? version : "", cfg);
        break;
    default:
        return -1;
    }
    return n > 0 && (size_t)n < size ? 0 : -1;
}

int robust_unmarked_line(char *out, size_t size, const char *cfgName)
{
    const int n =
        snprintf(out, size,
                 "ico_pc: [dev] gpu_robust = true in %s has no gpu_robust_by mark: set by "
                 "hand (or by a build before v0.10.2), so it is never dropped; it costs "
                 "bounds checks on every 3D load (delete the line to turn it off)",
                 cfgName != NULL ? cfgName : "config.toml");

    return n > 0 && (size_t)n < size ? 0 : -1;
}

/* the key of a `key = value` line, trimmed, into out ("" when the line has
   no '=' or is a comment or a header) */
static void line_key(const char *line, size_t len, char *out, size_t size)
{
    const char *end = line + len;
    const char *eq = memchr(line, '=', len);
    size_t n;

    out[0] = '\0';
    while (line < end && (*line == ' ' || *line == '\t')) {
        line++;
    }
    if (eq == NULL || line >= end || *line == '#' || *line == '[') {
        return;
    }
    while (eq > line && (eq[-1] == ' ' || eq[-1] == '\t')) {
        eq--;
    }
    n = (size_t)(eq - line);
    if (n < size) {
        memcpy(out, line, n);
        out[n] = '\0';
    }
}

/* whether a line is a section header; the name, trimmed, into out */
static int line_header(const char *line, size_t len, char *out, size_t size)
{
    const char *end = line + len;
    const char *close;
    size_t n;

    while (line < end && (*line == ' ' || *line == '\t')) {
        line++;
    }
    if (line >= end || *line != '[' || (line + 1 < end && line[1] == '[')) {
        return 0;
    }
    close = memchr(line, ']', (size_t)(end - line));
    if (close == NULL) {
        return 0;
    }
    line++;
    while (line < close && (*line == ' ' || *line == '\t')) {
        line++;
    }
    while (close > line && (close[-1] == ' ' || close[-1] == '\t')) {
        close--;
    }
    n = (size_t)(close - line);
    out[0] = '\0';
    if (n < size) {
        memcpy(out, line, n);
        out[n] = '\0';
    }
    return 1;
}

char *robust_persist_strip(const char *text)
{
    const size_t total = strlen(text);
    char *out = malloc(total + 1);
    size_t w = 0;
    const char *p = text;
    int inDev = 0;

    if (out == NULL) {
        return NULL;
    }
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        const size_t len = nl != NULL ? (size_t)(nl - p) + 1 : strlen(p);
        char name[32], key[32];
        int drop = 0;

        if (line_header(p, len, name, sizeof(name))) {
            inDev = strcmp(name, "dev") == 0;
        } else if (inDev) {
            line_key(p, len, key, sizeof(key));
            drop = strcmp(key, "gpu_robust") == 0 || strcmp(key, "gpu_robust_by") == 0;
        }
        if (!drop) {
            memcpy(out + w, p, len);
            w += len;
        }
        p += len;
    }
    out[w] = '\0';
    return out;
}
