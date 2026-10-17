/*
 * port/platform/startup_retry.h
 *
 * What the game does after the graphics driver crashed while building a
 * pipeline (the crash report's activity note, diag_host.h), or after the
 * graphics card stopped responding (the device was lost, rhi_device_lost,
 * window_host.c device_lost_quit): the program starts again at once, in
 * the same session, with ICO_STARTUP_RETRY=<step>:<backend> in its
 * environment, and the new run (window_host.c, host_config.c
 * ico_ini_export) does that step.  Nothing is written to the user folder
 * or the settings: the variable lives only in the relaunched process.
 *
 * After a driver crash (STARTUP_RETRY_CRASH) the steps, in order; each is
 * taken only when the run before crashed:
 *   cache       the crashing run had loaded the pipeline cache file: it is
 *               deleted and the same backend tries again;
 *   noprepass   the crash's activity note names a pipeline of the VU
 *               vertex pre-pass (a " pre" key, a vu_X_pre_vs or vu_tri
 *               module: startup_retry_note_has_vu_pre;
 *               port/render/rd_replay_vu.c): the same backend without the
 *               pre-pass (ICO_RD_VU_PREPASS=0, host_config.c
 *               ico_ini_export), which makes none of them;
 *   fixedblend  the crash's activity note names a pipeline with a
 *               destination read (its "dst N" field nonzero,
 *               startup_retry_note_has_dst: an exact-blend pass, so a
 *               fragment shader interlock or rasterizer-ordered view
 *               fragment entry, port/render/rd_dst_read.c): the same
 *               backend with the fixed-function blend (ICO_RD_BLEND=fixed,
 *               host_config.c ico_ini_export, as the loss steps), which
 *               creates no such pipeline; skipped when the player chose
 *               the blending (blend= / [dev] blend, or ICO_RD_BLEND);
 *   d3d12       a build with the D3D12 backend, whose player did not
 *               choose a backend (backend= / [video] backend): Direct3D
 *               12 (its own blending, not the fixed one); when it cannot
 *               start (no device), that run goes on to lazy on Vulkan
 *               itself (window_host.c), as nothing crashed;
 *   lazy        the up-front creation of the pipeline set is skipped: each
 *               one is built when a frame first needs it, on the same
 *               backend.
 * After lazy, or in a run started after a device loss, there is nothing
 * left: the crash box is shown.  The runs of noprepass and of every step
 * after it (fixedblend, d3d12, lazy) draw without the pre-pass
 * (startup_retry_draws_no_prepass), so a later step never makes its
 * pipelines again.
 *
 * An end without a crash report (the system ends the app, the device
 * freezes, the user swipes it away) never reaches these steps: the
 * start-up activity file names it at the next start, which takes lazy for
 * that start only (diag_host.h ico_diag_startup_pipeline_set), after an
 * extended VU batch draws with the plain variants for that start only
 * (ico_diag_startup_vu_plain), and after a batch of the VU vertex pre-pass
 * (startup_retry_note_has_vu_pre, not an extended one) draws without the
 * pre-pass for that start only (ico_diag_startup_vu_prepass). The second
 * such end in a row, in the foreground, makes it last ([dev] pipeline_set =
 * "lazy", [dev] vu_plain_only = true or [dev] vu_prepass = false written to
 * config.toml, ico_diag_startup_end_persist, not on a device the table
 * already makes lazy); an end in the background is not counted
 * (ico_diag_startup_activity_background).
 *
 * After a device loss (STARTUP_RETRY_LOSS) the steps, in order; each is
 * taken only when the run before lost the device:
 *   lossnoprepass  the same backend without the VU vertex pre-pass
 *            (ICO_RD_VU_PREPASS=0), when the losing run drew with it;
 *   blend    the same backend with the fixed-function blend
 *            (ICO_RD_BLEND=fixed, port/render/rd_dst_read.c rd__dst_init:
 *            no destination-read passes, so no fragment shader interlock
 *            or rasterizer-ordered views), skipped
 *            when the losing run already drew with it or the player chose
 *            the blending (blend= / [dev] blend, or ICO_RD_BLEND);
 *   backend  the other backend (Direct3D 12 from Vulkan, Vulkan from
 *            Direct3D 12) with the fixed-function blend, in a build with
 *            both and when the player chose neither the backend nor a
 *            blending other than fixed; when Direct3D 12 cannot start, that
 *            run goes on with Vulkan itself (window_host.c).
 * After backend there is nothing left: the device-loss box is shown.
 * Every loss step's run draws without the pre-pass, and blend and backend
 * with the fixed-function blend.  A
 * loss restart follows a crash step too (lazy, then blend; fixedblend,
 * whose run already draws with the fixed-function blend, then backend):
 * the step only grows, so a chain always ends.  The restarted run starts at the title as
 * a fresh launch: what was not saved was lost with the card, as at a quit.
 *
 * Android and iOS cannot start the game again (canRelaunch false): the box.
 *
 * Pure functions: no I/O, no allocation (the crash handler calls them).
 */
#ifndef ICO_PLATFORM_STARTUP_RETRY_H
#define ICO_PLATFORM_STARTUP_RETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#define STARTUP_RETRY_ENV "ICO_STARTUP_RETRY"

/* in the orders' sequence: a step only grows */
typedef enum {
    STARTUP_RETRY_NONE = 0, /* the first run; as a result: no step left */
    STARTUP_RETRY_CACHE,
    STARTUP_RETRY_NO_PREPASS,  /* "noprepass": the crash order's run without the pre-pass */
    STARTUP_RETRY_CRASH_BLEND, /* "fixedblend": the crash order's fixed-function blend */
    STARTUP_RETRY_D3D12,
    STARTUP_RETRY_LAZY,
    STARTUP_RETRY_LOSS_NO_PREPASS, /* "lossnoprepass": the loss order's first */
    STARTUP_RETRY_LOSS_BLEND,
    STARTUP_RETRY_LOSS_BACKEND,
} StartupRetry;

/* why the run ended */
typedef enum {
    STARTUP_RETRY_CRASH = 0, /* the driver crashed while building a pipeline */
    STARTUP_RETRY_LOSS,      /* the device was lost (not a crash) */
} StartupRetryWhy;

/* The ending run, for startup_retry_next */
typedef struct {
    StartupRetryWhy why;
    StartupRetry step;       /* the step this run was at (NONE: the first run) */
    bool canRelaunch;        /* this platform can start the program again */
    bool cacheWasLoaded;     /* the run created its pipeline cache from the file */
    bool haveD3d12;          /* the build has the D3D12 backend (and Vulkan) */
    bool backendExplicit;    /* the player named the backend */
    bool blendExplicit;      /* the player named the blending (blend=, ICO_RD_BLEND) */
    bool blendWasFixed;      /* the run drew with the fixed-function blend */
    bool dstPipeline;        /* a crash: its note names a destination-read pipeline
                                (startup_retry_note_has_dst) */
    bool prepassPipeline;    /* a crash: its note names a pipeline of the VU vertex
                                pre-pass (startup_retry_note_has_vu_pre) */
    bool prepassWasOn;       /* the run drew with the VU vertex pre-pass */
    const char *backendName; /* the run's backend; NULL or "": vulkan */
} StartupRetryRun;

/* the loss steps */
static inline bool startup_retry_is_loss(StartupRetry step)
{
    return step == STARTUP_RETRY_LOSS_NO_PREPASS || step == STARTUP_RETRY_LOSS_BLEND ||
           step == STARTUP_RETRY_LOSS_BACKEND;
}

/* value starts with the step name name and a ':' */
static inline bool startup_retry_value_names(const char *value, const char *name)
{
    size_t i;
    if (value == NULL) {
        return false;
    }
    for (i = 0; name[i] != '\0' && value[i] == name[i]; i++) {}
    return name[i] == '\0' && value[i] == ':';
}

/* Whether a value of ICO_STARTUP_RETRY (startup_retry_value) names a loss
 * step: "lossnoprepass:...", "blend:..." or "backend:...".  Inline, so
 * host_config.c, which some programs compile without the platform library,
 * can ask. */
static inline bool startup_retry_value_is_loss(const char *value)
{
    return startup_retry_value_names(value, "lossnoprepass") ||
           startup_retry_value_names(value, "blend") || startup_retry_value_names(value, "backend");
}

/* The steps whose runs draw with the fixed-function blend
 * (ICO_RD_BLEND=fixed set by host_config.c ico_ini_export, not the
 * player's): the crash order's fixedblend and the loss steps blend and
 * backend */
static inline bool startup_retry_draws_fixed(StartupRetry step)
{
    return step == STARTUP_RETRY_CRASH_BLEND || step == STARTUP_RETRY_LOSS_BLEND ||
           step == STARTUP_RETRY_LOSS_BACKEND;
}

/* Whether a value of ICO_STARTUP_RETRY names a step that draws with the
 * fixed-function blend: "fixedblend:...", "blend:..." or "backend:...".
 * Inline, as startup_retry_value_is_loss. */
static inline bool startup_retry_value_draws_fixed(const char *value)
{
    return startup_retry_value_names(value, "fixedblend") ||
           startup_retry_value_names(value, "blend") || startup_retry_value_names(value, "backend");
}

/* The steps whose runs draw without the VU vertex pre-pass
 * (ICO_RD_VU_PREPASS=0 set by host_config.c ico_ini_export; window_host.c
 * rd_set_vu_prepass_off): the crash order's noprepass and every step after
 * it, and every loss step */
static inline bool startup_retry_draws_no_prepass(StartupRetry step)
{
    return step >= STARTUP_RETRY_NO_PREPASS;
}

/* Whether a value of ICO_STARTUP_RETRY names such a step.  Inline, as
 * startup_retry_value_is_loss. */
static inline bool startup_retry_value_no_prepass(const char *value)
{
    static const char *const names[] = {"noprepass",     "fixedblend", "d3d12",  "lazy",
                                        "lossnoprepass", "blend",      "backend"};
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (startup_retry_value_names(value, names[i])) {
            return true;
        }
    }
    return false;
}

/* Whether a crash's activity note (diag_host.h ico_diag_set_activity) names
 * a pipeline with a destination read: it starts with "creating pipeline "
 * (one pipeline) or "creating pipelines " (a batch of the start-up set,
 * whose keys all have a destination read or none has) and its first
 * " dst N" field has N nonzero (port/render/rd_pipeline.c
 * activityCreating, rd_precreate_pipelines).  Tolerant: NULL, another
 * note, a missing or malformed field read as false.  No I/O, no
 * allocation. */
bool startup_retry_note_has_dst(const char *note);

/* Whether an activity note names a pipeline with a VU vertex shader's
 * extended variant (issue 107; port/render/rd_pipeline.c keyWords: a VU
 * key's " ext N" field, N nonzero; a batch's keys are all such keys or
 * none is).  window_host.c: a start that ended without a word inside such
 * a batch (the device froze, the system ended the app) makes the next
 * start draw with the plain variants only, and the second such end in a
 * row every start after it ([dev] vu_plain_only = true in config.toml,
 * diag_host.h ico_diag_startup_vu_plain).  Tolerant as
 * startup_retry_note_has_dst. */
bool startup_retry_note_has_vu_ext(const char *note);

/* Whether an activity note names a pipeline of the VU vertex pre-pass
 * (port/render/rd_replay_vu.c; docs/port/RENDER.md, "The vertex
 * pre-pass"): it starts with "creating pipeline " or "creating pipelines "
 * and names a pre-pass key (port/render/rd_pipeline.c keyWords' " pre"
 * field) or module ("_pre_vs") or the triangle stage's module ("vu_tri_",
 * vu_tri_vs and vu_tri_plain_vs).  The crash order's noprepass step reads
 * it; window_host.c: a start that ended without a word inside such a
 * batch, not an extended one (startup_retry_note_has_vu_ext, issue 107's
 * plain variants first), makes the next start draw without the pre-pass,
 * and the second such end in a row every start after it ([dev] vu_prepass
 * = false, diag_host.h ico_diag_startup_vu_prepass).  Tolerant as
 * startup_retry_note_has_dst. */
bool startup_retry_note_has_vu_pre(const char *note);

/* The step after the run described by run ended (run->why): the crash or
 * loss order above, NONE when nothing is left or the platform cannot start
 * the game again.  reason gets one line in plain words (also for NONE: why
 * nothing is left). */
StartupRetry startup_retry_next(const StartupRetryRun *run, char *reason, size_t reasonLen);

/* "cache", "noprepass", "fixedblend", "d3d12", "lazy", "lossnoprepass",
 * "blend", "backend"; "" for NONE */
const char *startup_retry_name(StartupRetry step);

/* The variable's value for the run after a crash or loss on backendName
 * that takes step next: "<step>:<backend>", the backend being "d3d12" for
 * the d3d12 step, the other one (d3d12 from vulkan, vulkan from d3d12) for
 * the backend step and backendName otherwise. 0, or -1 when it does not
 * fit or next is NONE. */
int startup_retry_value(StartupRetry next, const char *backendName, char *out, size_t size);

/* Reads a value written by startup_retry_value: 0 with *step and backend,
 * -1 (and *step NONE) for NULL, "" or anything else. */
int startup_retry_parse(const char *value, StartupRetry *step, char *backend, size_t size);

/* What a run at step does, for its first log line ("the pipeline cache
 * file was deleted; the shaders are prepared again from scratch", ...) */
const char *startup_retry_what(StartupRetry step);

/* The relaunch's environment, prepared at start-up so the crash handler
 * neither allocates nor reads a changing environ.
 *
 * POSIX: out[] gets env's pointers without any ICO_STARTUP_RETRY=, then
 * extra (when not NULL), then NULL. Returns the count before the NULL, or
 * -1 when max (out's size, the NULL included) is too small. */
int startup_retry_envp(char *const *env, const char *extra, const char **out, size_t max);

/* Windows: a copy of a CreateProcessW environment block (NAME=value
 * strings, each NUL-terminated, a second NUL at the end) without any
 * ICO_STARTUP_RETRY= (any case), the final NUL left off, so
 * startup_retry_block_finish can add the variable later. Returns the
 * wide characters written, or -1 when cap is too small. */
long startup_retry_block_copy(const wchar_t *block, wchar_t *out, size_t cap);
/* Appends ICO_STARTUP_RETRY=<value> and the block's final NUL after used
 * characters (value ASCII). 0, or -1 when it does not fit. */
int startup_retry_block_finish(wchar_t *out, size_t used, size_t cap, const char *value);

#endif /* ICO_PLATFORM_STARTUP_RETRY_H */
