/*
 * port/platform/startup_retry.c
 *
 * The steps after a graphics driver crash while building a pipeline or a
 * device loss (startup_retry.h).
 */
#include "startup_retry.h"
#include <stdio.h>
#include <string.h>

/* ASCII, any case */
static bool same_name(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

/* the crash order (startup_retry.h) */
static StartupRetry crash_next(const StartupRetryRun *run, const char *name, bool onD3d12,
                               char *reason, size_t reasonLen)
{
    const StartupRetry step = run->step;
    if (startup_retry_is_loss(step)) {
        snprintf(reason, reasonLen,
                 "this run was already a restart after the graphics card stopped responding: "
                 "nothing more is tried");
        return STARTUP_RETRY_NONE;
    }
    if (step < STARTUP_RETRY_CACHE && run->cacheWasLoaded) {
        snprintf(reason, reasonLen,
                 "the saved pipeline cache file was in use: it is deleted and %s tries again",
                 name);
        return STARTUP_RETRY_CACHE;
    }
    if (step < STARTUP_RETRY_NO_PREPASS && run->prepassPipeline) {
        snprintf(reason, reasonLen,
                 "the pipeline being built was one of the 3D vertex pre-pass's; the next try on "
                 "%s draws without it",
                 name);
        return STARTUP_RETRY_NO_PREPASS;
    }
    /* the fixed-function blend is the default (port/render/rd_dst_read.c):
       a destination-read pipeline exists only where the player opted into
       the exact blend, which blendExplicit then keeps */
    if (step < STARTUP_RETRY_CRASH_BLEND && run->dstPipeline && !run->blendExplicit &&
        !run->blendWasFixed) {
        snprintf(reason, reasonLen,
                 "the pipeline being built read the screen for the exact blend; the next try "
                 "on %s draws with the simpler blending",
                 name);
        return STARTUP_RETRY_CRASH_BLEND;
    }
    if (step < STARTUP_RETRY_D3D12 && run->haveD3d12 && !run->backendExplicit && !onD3d12) {
        snprintf(reason, reasonLen, "%s crashed; the next try uses Direct3D 12", name);
        return STARTUP_RETRY_D3D12;
    }
    if (step < STARTUP_RETRY_LAZY) {
        snprintf(reason, reasonLen,
                 "the next try on %s builds the shaders as the game needs them, not all at "
                 "the start%s",
                 name,
                 run->backendExplicit && run->haveD3d12 && !onD3d12
                     ? " (Direct3D 12 is not tried: the settings choose the backend)"
                     : "");
        return STARTUP_RETRY_LAZY;
    }
    snprintf(reason, reasonLen,
             "it crashed even with the shaders built as the game needs them: nothing is left "
             "to try");
    return STARTUP_RETRY_NONE;
}

/* the loss order (startup_retry.h) */
static StartupRetry loss_next(const StartupRetryRun *run, const char *name, bool onD3d12,
                              char *reason, size_t reasonLen)
{
    const StartupRetry step = run->step;
    const char *other = onD3d12 ? "vulkan" : "d3d12";
    /* the backend step keeps the player's blending: only a fixed one */
    const bool blendFree = !run->blendExplicit || run->blendWasFixed;
    if (step < STARTUP_RETRY_LOSS_NO_PREPASS && run->prepassWasOn) {
        snprintf(reason, reasonLen, "the next run on %s draws without the 3D vertex pre-pass",
                 name);
        return STARTUP_RETRY_LOSS_NO_PREPASS;
    }
    if (step < STARTUP_RETRY_LOSS_ROBUST && run->robustOffDefault) {
        snprintf(reason, reasonLen,
                 "the next run on %s checks the bounds of every buffer and image read (robust "
                 "access)",
                 name);
        return STARTUP_RETRY_LOSS_ROBUST;
    }
    /* skipped by default: blendWasFixed is true unless the player opted
       into the exact blend, and then blendExplicit is */
    if (step < STARTUP_RETRY_LOSS_BLEND && !run->blendWasFixed && !run->blendExplicit) {
        snprintf(reason, reasonLen, "the next run on %s draws with the simpler blending", name);
        return STARTUP_RETRY_LOSS_BLEND;
    }
    if (step < STARTUP_RETRY_LOSS_BACKEND && run->haveD3d12 && !run->backendExplicit && blendFree) {
        snprintf(reason, reasonLen, "the next run uses %s with the simpler blending", other);
        return STARTUP_RETRY_LOSS_BACKEND;
    }
    snprintf(reason, reasonLen, "nothing is left to try on this computer%s%s",
             run->blendExplicit && !run->blendWasFixed
                 ? " (the settings choose the blending: blend=)"
                 : "",
             run->backendExplicit && run->haveD3d12
                 ? " (the settings choose the graphics backend: backend=)"
                 : "");
    return STARTUP_RETRY_NONE;
}

bool startup_retry_note_has_dst(const char *note)
{
    static const char head[] = "creating pipeline";
    static const char field[] = " dst ";
    const char *p;
    size_t i;
    bool digits = false, nonzero = false;
    if (note == NULL || strncmp(note, head, sizeof head - 1) != 0) {
        return false;
    }
    /* one pipeline's note, or a batch's ("creating pipelines ...": its keys
       all have a destination read or none has, so its first key's field is
       the batch's) */
    p = note + (sizeof head - 1);
    if (*p == 's') {
        p++;
    }
    if (*p != ' ') {
        return false;
    }
    /* the first " dst " field, from the head's closing space on */
    p = strstr(p, field);
    if (p == NULL) {
        return false;
    }
    p += sizeof field - 1;
    for (i = 0; p[i] >= '0' && p[i] <= '9'; i++) {
        digits = true;
        nonzero = nonzero || p[i] != '0';
    }
    /* the number ends the note or a word */
    return digits && nonzero && (p[i] == '\0' || p[i] == ' ');
}

bool startup_retry_note_has_vu_ext(const char *note)
{
    static const char head[] = "creating pipeline";
    static const char field[] = " ext ";
    const char *p;
    size_t i;
    bool digits = false, nonzero = false;
    if (note == NULL || strncmp(note, head, sizeof head - 1) != 0) {
        return false;
    }
    p = note + (sizeof head - 1);
    if (*p == 's') {
        p++;
    }
    if (*p != ' ') {
        return false;
    }
    /* the first " ext " field: a batch's keys are all extended VU keys or
       none is (rd_pipeline.c createKeys), so its first such field is the
       batch's */
    p = strstr(p, field);
    if (p == NULL) {
        return false;
    }
    p += sizeof field - 1;
    for (i = 0; p[i] >= '0' && p[i] <= '9'; i++) {
        digits = true;
        nonzero = nonzero || p[i] != '0';
    }
    return digits && nonzero && (p[i] == '\0' || p[i] == ' ');
}

bool startup_retry_note_has_vu_pre(const char *note)
{
    static const char head[] = "creating pipeline";
    const char *p;
    if (note == NULL || strncmp(note, head, sizeof head - 1) != 0) {
        return false;
    }
    p = note + (sizeof head - 1);
    if (*p == 's') {
        p++;
    }
    if (*p != ' ') {
        return false;
    }
    /* a module of the pre-pass or the triangle stage (keyModules,
       batchModules) */
    if (strstr(p, "_pre_vs") != NULL || strstr(p, "vu_tri_") != NULL) {
        return true;
    }
    /* a pre-pass key's field (keyWords: " pre" after the dst field), a
       word of its own */
    for (const char *q = strstr(p, " pre"); q != NULL; q = strstr(q + 1, " pre")) {
        const char e = q[4];
        if (e == '\0' || e == ' ' || e == ';' || e == ',' || e == ')') {
            return true;
        }
    }
    return false;
}

StartupRetry startup_retry_next(const StartupRetryRun *run, char *reason, size_t reasonLen)
{
    const char *name =
        run->backendName != NULL && run->backendName[0] != '\0' ? run->backendName : "vulkan";
    const bool onD3d12 = same_name(name, "d3d12");
    char dummy[1];
    if (reason == NULL || reasonLen == 0) {
        reason = dummy;
        reasonLen = sizeof dummy;
    }
    if (!run->canRelaunch) {
        snprintf(reason, reasonLen, "the game cannot start itself again on this system");
        return STARTUP_RETRY_NONE;
    }
    return run->why == STARTUP_RETRY_LOSS ? loss_next(run, name, onD3d12, reason, reasonLen)
                                          : crash_next(run, name, onD3d12, reason, reasonLen);
}

const char *startup_retry_name(StartupRetry step)
{
    switch (step) {
    case STARTUP_RETRY_CACHE:
        return "cache";
    case STARTUP_RETRY_NO_PREPASS:
        return "noprepass";
    case STARTUP_RETRY_CRASH_BLEND:
        return "fixedblend";
    case STARTUP_RETRY_D3D12:
        return "d3d12";
    case STARTUP_RETRY_LAZY:
        return "lazy";
    case STARTUP_RETRY_LOSS_NO_PREPASS:
        return "lossnoprepass";
    case STARTUP_RETRY_LOSS_ROBUST:
        return "robust";
    case STARTUP_RETRY_LOSS_BLEND:
        return "blend";
    case STARTUP_RETRY_LOSS_BACKEND:
        return "backend";
    default:
        return "";
    }
}

int startup_retry_value(StartupRetry next, const char *backendName, char *out, size_t size)
{
    const char *name = backendName != NULL && backendName[0] != '\0' ? backendName : "vulkan";
    const char *to = name;
    int n;
    if (next == STARTUP_RETRY_NONE || out == NULL || size == 0) {
        return -1;
    }
    if (next == STARTUP_RETRY_D3D12) {
        to = "d3d12";
    } else if (next == STARTUP_RETRY_LOSS_BACKEND) {
        to = same_name(name, "d3d12") ? "vulkan" : "d3d12";
    }
    n = snprintf(out, size, "%s:%s", startup_retry_name(next), to);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

int startup_retry_parse(const char *value, StartupRetry *step, char *backend, size_t size)
{
    static const StartupRetry steps[] = {
        STARTUP_RETRY_CACHE,       STARTUP_RETRY_NO_PREPASS, STARTUP_RETRY_CRASH_BLEND,
        STARTUP_RETRY_D3D12,       STARTUP_RETRY_LAZY,       STARTUP_RETRY_LOSS_NO_PREPASS,
        STARTUP_RETRY_LOSS_ROBUST, STARTUP_RETRY_LOSS_BLEND, STARTUP_RETRY_LOSS_BACKEND};
    const char *colon;
    size_t i, n;
    *step = STARTUP_RETRY_NONE;
    if (backend != NULL && size > 0) {
        backend[0] = '\0';
    }
    if (value == NULL || (colon = strchr(value, ':')) == NULL || backend == NULL) {
        return -1;
    }
    n = strlen(colon + 1);
    if (n == 0 || n >= size) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        const char c = colon[1 + i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return -1;
        }
    }
    for (i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        const char *name = startup_retry_name(steps[i]);
        const size_t len = strlen(name);
        if (len == (size_t)(colon - value) && strncmp(value, name, len) == 0) {
            memcpy(backend, colon + 1, n + 1);
            *step = steps[i];
            return 0;
        }
    }
    return -1;
}

const char *startup_retry_what(StartupRetry step)
{
    switch (step) {
    case STARTUP_RETRY_CACHE:
        return "the pipeline cache file was deleted; the shaders are prepared again from scratch";
    case STARTUP_RETRY_NO_PREPASS:
        return "this run draws without the 3D vertex pre-pass on the same graphics backend";
    case STARTUP_RETRY_CRASH_BLEND:
        /* reached only by a run whose exact blend was not the player's
           choice: the fixed-function blend is the default */
        return "this run draws with the simpler blending (the graphics card's own, the "
               "default) on the same graphics backend, without the 3D vertex pre-pass";
    case STARTUP_RETRY_D3D12:
        return "this run uses Direct3D 12, without the 3D vertex pre-pass";
    case STARTUP_RETRY_LAZY:
        return "this run builds the shaders as the game needs them, not all at the start, "
               "without the 3D vertex pre-pass";
    case STARTUP_RETRY_LOSS_NO_PREPASS:
        return "this run draws without the 3D vertex pre-pass on the same graphics backend; it "
               "starts at the title as a fresh launch (what was not saved was lost when the "
               "card stopped)";
    case STARTUP_RETRY_LOSS_ROBUST:
        return "this run checks the bounds of every buffer and image read (robust access) on the "
               "same graphics backend, without the 3D vertex pre-pass; it starts at the title as "
               "a fresh launch (what was not saved was lost when the card stopped)";
    case STARTUP_RETRY_LOSS_BLEND:
        return "this run draws with the simpler blending (the graphics card's own, the "
               "default) on the same graphics backend, without the 3D vertex pre-pass; it starts "
               "at the title as a fresh launch (what was not saved was lost when the card "
               "stopped)";
    case STARTUP_RETRY_LOSS_BACKEND:
        return "this run uses the other graphics backend with the simpler blending, without "
               "the 3D vertex pre-pass; it starts at the title as a fresh launch (what was not "
               "saved was lost when the card stopped)";
    default:
        return "";
    }
}

/* s starts with "ICO_STARTUP_RETRY=" (the name in any case) */
static bool is_retry_var(const char *s)
{
    static const char name[] = STARTUP_RETRY_ENV "=";
    size_t i;
    for (i = 0; name[i] != '\0'; i++) {
        char c = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

static bool is_retry_var_w(const wchar_t *s)
{
    static const char name[] = STARTUP_RETRY_ENV "=";
    size_t i;
    for (i = 0; name[i] != '\0'; i++) {
        wchar_t c = s[i] >= L'a' && s[i] <= L'z' ? (wchar_t)(s[i] - 32) : s[i];
        if (c != (wchar_t)name[i]) {
            return false;
        }
    }
    return true;
}

int startup_retry_envp(char *const *env, const char *extra, const char **out, size_t max)
{
    size_t n = 0;
    size_t i;
    for (i = 0; env != NULL && env[i] != NULL; i++) {
        if (is_retry_var(env[i])) {
            continue;
        }
        if (n + 1 >= max) {
            return -1;
        }
        out[n++] = env[i];
    }
    if (extra != NULL) {
        if (n + 1 >= max) {
            return -1;
        }
        out[n++] = extra;
    }
    if (n >= max) {
        return -1;
    }
    out[n] = NULL;
    return (int)n;
}

long startup_retry_block_copy(const wchar_t *block, wchar_t *out, size_t cap)
{
    size_t used = 0;
    const wchar_t *p = block;
    while (p != NULL && *p != L'\0') {
        const size_t len = wcslen(p);
        if (!is_retry_var_w(p)) {
            if (used + len + 1 > cap) {
                return -1;
            }
            memcpy(out + used, p, (len + 1) * sizeof(wchar_t));
            used += len + 1;
        }
        p += len + 1;
    }
    return (long)used;
}

int startup_retry_block_finish(wchar_t *out, size_t used, size_t cap, const char *value)
{
    static const char name[] = STARTUP_RETRY_ENV "=";
    const size_t nl = sizeof name - 1;
    const size_t vl = strlen(value);
    size_t i;
    if (used + nl + vl + 2 > cap) {
        return -1;
    }
    for (i = 0; i < nl; i++) {
        out[used++] = (wchar_t)name[i];
    }
    for (i = 0; i < vl; i++) {
        out[used++] = (wchar_t)(unsigned char)value[i];
    }
    out[used++] = L'\0';
    out[used] = L'\0';
    return 0;
}
