/*
 * port/platform/pipeline_skip.c
 *
 * The record of a pipeline family a device's driver stopped in
 * (pipeline_skip.h; issue 161).
 */
#include "pipeline_skip.h"

#include <stdio.h>
#include <string.h>

/* the sprite planes builds' vertex shaders (port/shaders/CMakeLists.txt,
   ICO_SPRITE_PLANES); every key of the family has one, no other key does
   (port/render/rd_screen_planes.c rd__plane_pass) */
static const char *const k_planes_vs[] = {"sprite_plane_ui_vs", "sprite_plane_world_vs",
                                          "sprite_stq_plane_ui_vs", "sprite_stq_plane_world_vs"};

int pipeline_skip_vs_is_sprite_planes(const char *name)
{
    size_t i;
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof k_planes_vs / sizeof k_planes_vs[0]; i++) {
        if (strcmp(name, k_planes_vs[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* the name at s up to a space, comma, ';' or ')' (at most size - 1) */
static void name_at(const char *s, char *out, size_t size)
{
    const size_t n = strcspn(s, " ,;)");
    const size_t k = n < size - 1 ? n : size - 1;
    memcpy(out, s, k);
    out[k] = '\0';
}

int pipeline_skip_family_of_note(const char *note)
{
    static const char single[] = "creating pipeline ";
    static const char batch[] = "creating pipelines ";
    char name[64];
    if (note == NULL) {
        return PIPELINE_SKIP_NONE;
    }
    if (strncmp(note, single, sizeof single - 1) == 0) {
        /* "creating pipeline <key words> (<vs> N words + <fs> M words)" */
        const char *open = strrchr(note, '(');
        if (open == NULL || strstr(open, " words + ") == NULL) {
            return PIPELINE_SKIP_NONE;
        }
        name_at(open + 1, name, sizeof name);
        return pipeline_skip_vs_is_sprite_planes(name) ? PIPELINE_SKIP_SPRITE_PLANES
                                                       : PIPELINE_SKIP_NONE;
    }
    if (strncmp(note, batch, sizeof batch - 1) == 0) {
        /* "...; vertex A N words (I instructions), B ... [and K more];
           largest fragment F M words" (rd_pipeline.c batchModules): every
           vertex shader named, none left unnamed */
        const char *v = strstr(note, "; vertex ");
        const char *end = v != NULL ? strstr(v, "; largest fragment ") : NULL;
        int named = 0;
        if (v == NULL || end == NULL) {
            return PIPELINE_SKIP_NONE;
        }
        v += 9;
        while (v < end) {
            const char *comma;
            name_at(v, name, sizeof name);
            if (!pipeline_skip_vs_is_sprite_planes(name)) {
                return PIPELINE_SKIP_NONE;
            }
            named++;
            /* the next name: after ", " inside the list; " and K more"
               (unnamed shaders) is not one of the family's for sure */
            comma = strstr(v, ", ");
            if (comma == NULL || comma > end) {
                const char *more = strstr(v, " and ");
                if (more != NULL && more < end) {
                    return PIPELINE_SKIP_NONE;
                }
                break;
            }
            v = comma + 2;
        }
        return named > 0 ? PIPELINE_SKIP_SPRITE_PLANES : PIPELINE_SKIP_NONE;
    }
    return PIPELINE_SKIP_NONE;
}

const char *pipeline_skip_family_word(int family)
{
    return family == PIPELINE_SKIP_SPRITE_PLANES ? "sprite_planes" : "";
}

const char *pipeline_skip_family_text(int family)
{
    return family == PIPELINE_SKIP_SPRITE_PLANES ? "the sprite planes builds" : "no family";
}

const char *pipeline_skip_family_plain(int family)
{
    return family == PIPELINE_SKIP_SPRITE_PLANES ? "some of the shapes on the screen"
                                                 : "the graphics";
}

/* src into out with the separators and quotes made spaces */
static void field_put(char *out, size_t size, const char *src)
{
    size_t i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1 < size; i++) {
        const char c = src[i];
        out[i] = c == ';' || c == '"' || c == '\\' || c == '\n' || c == '\r' ? ' ' : c;
    }
    out[i] = '\0';
}

int pipeline_skip_format(char *out, size_t size, const PipelineSkip *s)
{
    char dev[sizeof s->device], drv[sizeof s->driver], date[sizeof s->date];
    int n;
    if (out == NULL || size == 0 || s == NULL || pipeline_skip_family_word(s->family)[0] == '\0') {
        return -1;
    }
    field_put(dev, sizeof dev, s->device);
    field_put(drv, sizeof drv, s->driver);
    field_put(date, sizeof date, s->date);
    n = snprintf(out, size, "%s; %s; %s; %s", pipeline_skip_family_word(s->family), dev, drv, date);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

/* the field at *p up to ';' into out, spaces around it taken off; *p past
   the ';' (or at the end) */
static void field_get(const char **p, char *out, size_t size)
{
    const char *s = *p;
    size_t n, k;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    n = strcspn(s, ";");
    *p = s[n] == ';' ? s + n + 1 : s + n;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        n--;
    }
    k = n < size - 1 ? n : size - 1;
    memcpy(out, s, k);
    out[k] = '\0';
}

int pipeline_skip_parse(const char *value, PipelineSkip *out)
{
    char fam[32];
    const char *p = value;
    memset(out, 0, sizeof(*out));
    if (value == NULL) {
        return -1;
    }
    field_get(&p, fam, sizeof fam);
    if (strcmp(fam, pipeline_skip_family_word(PIPELINE_SKIP_SPRITE_PLANES)) != 0) {
        return -1;
    }
    out->family = PIPELINE_SKIP_SPRITE_PLANES;
    field_get(&p, out->device, sizeof out->device);
    field_get(&p, out->driver, sizeof out->driver);
    field_get(&p, out->date, sizeof out->date);
    return 0;
}

/* ASCII, any case; an empty a matches any b */
static int same_or_any(const char *a, const char *b)
{
    if (a[0] == '\0') {
        return 1;
    }
    if (b == NULL) {
        return 0;
    }
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        const char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        const char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

int pipeline_skip_matches(const PipelineSkip *s, const char *device, const char *driver)
{
    return s != NULL && s->family != PIPELINE_SKIP_NONE && same_or_any(s->device, device) &&
           same_or_any(s->driver, driver);
}

void pipeline_skip_driver_word(char *out, size_t size, const char *compiler,
                               const char *driver_text)
{
    const char *v = compiler != NULL ? strstr(compiler, "Version:") : NULL;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (v != NULL || (compiler != NULL && compiler[0] != '\0')) {
        v = v != NULL ? v + 8 : compiler;
        while (*v == ' ') {
            v++;
        }
        field_put(out, size, v);
        out[strcspn(out, " ")] = '\0';
        if (out[0] != '\0') {
            return;
        }
    }
    if (driver_text != NULL) {
        const char *last = NULL;
        const char *at = driver_text;
        while ((at = strstr(at, "version ")) != NULL) {
            last = at + 8;
            at = last;
        }
        if (last != NULL) {
            field_put(out, size, last);
            out[strcspn(out, " ,")] = '\0';
        }
    }
}
