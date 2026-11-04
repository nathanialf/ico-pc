/*
 * port/platform/window_switch.c
 *
 * Options > Graphics > Apply: the renderer, or the graphics card it runs on,
 * switched in the process (window_switch.h).  The game's state is the CPU's: the renderer keeps
 * what the game set up on it and does not set again (rd_suspend), and
 * makes the rest again as the game draws (its textures from the game's
 * memory, its meshes from the packets); the glow and the blur trail start
 * again, as after a resume, and the pipelines come from the backend's
 * cache or are compiled.  The audio is not touched.  Nothing is written:
 * the page writes [video] backend and [video] adapter when the player keeps
 * the choice.  The card goes to the backend as rhi_set_adapter_wanted
 * before the renderer is made again; the page's card names are installed
 * again after every switch (the next renderer lists its own).
 *
 * Between Apply and Keep the renderer is on trial: a driver crash or a
 * device loss writes the report and plans no restart (the file names the
 * renderer before; diag_host.h ico_diag_set_backend_trial).  A switch back
 * (Revert, the countdown's end, a renderer that did not start) gives the
 * crash and loss steps the start's choice again (Automatic's, or the
 * player's); Keep gives them the kept renderer as the player's.
 */
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "diag_host.h"
#include "font.h" /* ui_font_forget_textures */
#include "input_sdl.h"
#include "options.h" /* ico_mv_active */
#include "photo_mode.h"
#include "rd.h"
#include "rhi.h"
#include "settings.h"
#include "window_switch.h"

/* the switch's watch (ico_diag_watchdog_switch): this long without a
   step, a shader or a pipeline made ends the run with the report; a cold
   driver's longest single pipeline batch is about 12 s (the 10 s
   pipeline lines), one OpenGL shader compile is held to the GPU hang
   limit (15 s by default) before the switch goes back */
#define SWITCH_WATCH_S 60u

extern int mpegPlay;
/* ico2/seki/src/GifPacket.c: the placeholder textures it made */
extern void gif_HostForgetTextures(void);

/* the switch the page asked for, run by the pump */
static struct {
    char want[16];         /* "" none */
    char wantAdapter[256]; /* the card asked for, "" automatic */
    int why;               /* UI_GFX_WHY_* */
    /* what the last Apply changed: the renderer, the card (Keep's line) */
    bool backendChanged, adapterChanged;
    int result; /* UI_GFX_SWITCH_* of the last one asked */
    int ended;  /* -3: no renderer left (ico_window_switch_end_text) */
    char endText[512];
} s_sw;

/* the start's choice (ico_window_switch_started), Keep's after it: the
   crash and loss steps' explicit flag and backend for a switch back; and
   the renderer Automatic started this run ("" when the player chose one) */
static struct {
    bool backendExplicit;
    char backend[16];
    char automatic[16];
} s_start = {false, "vulkan", ""};

void ico_window_switch_started(bool backendExplicit, const char *backend)
{
    s_start.backendExplicit = backendExplicit;
    snprintf(s_start.backend, sizeof(s_start.backend), "%s", backend != NULL ? backend : "vulkan");
    snprintf(s_start.automatic, sizeof(s_start.automatic), "%s",
             backendExplicit || backend == NULL ? "" : backend);
}

/* the crash and loss steps' explicit flag for name: the start's (or
   Keep's) for its renderer, the player's for another */
static bool explicitFor(const char *name)
{
    return strcmp(name, s_start.backend) == 0 ? s_start.backendExplicit : true;
}

/* a renderer's proper name, for the box */
static const char *properName(const char *name)
{
    if (strcmp(name, "opengl") == 0) {
        return "OpenGL";
    }
    if (strcmp(name, "d3d12") == 0) {
        return "Direct3D 12";
    }
    if (strcmp(name, "d3d11") == 0) {
        return "Direct3D 11";
    }
    return "Vulkan";
}

const char *ico_window_switch_running(void)
{
    switch (rhi_backend()) {
    case RHI_BACKEND_OPENGL:
        return "opengl";
    case RHI_BACKEND_D3D12:
        return "d3d12";
    case RHI_BACKEND_D3D11:
        return "d3d11";
    default:
        return "vulkan"; /* Metal is Vulkan over MoltenVK */
    }
}

static int linked(const char *name)
{
    const char *n;
    for (uint32_t i = 0; name != NULL && (n = rhi_backend_name(i)) != NULL; i++) {
        if (strcmp(n, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* the card Automatic picks on the renderer running ("" when not listed) */
static const char *automaticAdapter(void)
{
    static RhiAdapterInfo list[RHI_MAX_ADAPTERS];
    const uint32_t n = rhi_adapter_list(list, RHI_MAX_ADAPTERS);

    for (uint32_t i = 0; i < n && i < RHI_MAX_ADAPTERS; i++) {
        if (list[i].automatic) {
            return list[i].name;
        }
    }
    return "";
}

/* whether a switch to adapter ("" automatic) on the renderer running
   would run on the card it runs on now */
static bool adapterRunning(const char *adapter)
{
    const char *now = rhi_adapter_name();

    if (adapter == NULL) {
        return true;
    }
    return strcmp(adapter[0] != '\0' ? adapter : automaticAdapter(), now != NULL ? now : "") == 0;
}

/* Graphics > Graphics card's names: the suitable cards of the renderer
   running, the card running and Automatic's (settings.h
   ui_settings_set_adapter_names; the row shows with two or more) */
static void installAdapters(void)
{
    static RhiAdapterInfo list[RHI_MAX_ADAPTERS];
    static const char *names[RHI_MAX_ADAPTERS];
    const uint32_t n = rhi_adapter_list(list, RHI_MAX_ADAPTERS);
    int k = 0;

    for (uint32_t i = 0; i < n && i < RHI_MAX_ADAPTERS; i++) {
        if (list[i].suitable) {
            names[k++] = list[i].name;
        }
    }
    ui_settings_set_adapter_names(names, k, rhi_adapter_name(), automaticAdapter());
}

/* why the switch is refused now, NULL when it is not; a switch of the
   card alone (name the renderer running) needs no second renderer */
static const char *refusal(const char *name)
{
    const IcoTouchEditor *e = ico_input_sdl_touch_editor();
    const bool same = name != NULL && strcmp(name, ico_window_switch_running()) == 0;

    if (rhi_backend_name(1) == NULL && !same) {
        return "this build has one renderer";
    }
    if (!linked(name)) {
        return "not in this build";
    }
    if (mpegPlay != 0) {
        return "a movie plays";
    }
    if (ico_photo_active()) {
        return "photo mode is on";
    }
    if (ico_mv_active != 0) {
        return "the model viewer is open";
    }
    if (e != NULL && e->active) {
        return "the touch layout editor is open";
    }
    return NULL;
}

int ico_window_switch_backend(const char *name, const char *adapter, int trial)
{
    char old[16], oldWanted[256];
    const char *why = refusal(name);

    snprintf(old, sizeof(old), "%s", ico_window_switch_running());
    snprintf(oldWanted, sizeof(oldWanted), "%s", rhi_adapter_wanted());
    if (why != NULL) {
        fprintf(stderr, "window: Graphics API %s not switched to: %s\n",
                name != NULL ? name : "(none)", why);
        return -2;
    }
    if (strcmp(name, old) == 0 && adapterRunning(adapter)) {
        return 0;
    }
    /* each step's line goes to the log at once (diag_host.h ico_diag_step;
       the rest of the switch's lines sit in stderr's buffer on Windows
       until the host loop's next flush): a hang leaves the step named */
    ico_diag_step("window: switch: %s -> %s%s%s begins", old, name,
                  adapter != NULL && adapter[0] != '\0' ? " on " : "",
                  adapter != NULL ? adapter : "");
    if (adapter != NULL) {
        rhi_set_adapter_wanted(adapter);
    }
    /* rd_shutdown destroys every texture: the ids kept outside the
       renderer are forgotten (the font pages are uploaded again from their
       atlases, the placeholders made again) */
    gif_HostForgetTextures();
    ui_font_forget_textures();
    /* on trial from before its pipeline set (the likeliest crash) */
    ico_diag_set_backend_trial(trial ? name : NULL);
    if (ico_window_reopen(name, trial ? true : explicitFor(name))) {
        rd_resume_frame();
        installAdapters();
        return 0;
    }
    if (strcmp(name, old) == 0) {
        fprintf(stderr,
                "window: Graphics card %s did not start (the lines above say why); back on %s\n",
                adapter != NULL && adapter[0] != '\0' ? adapter : "automatic",
                oldWanted[0] != '\0' ? oldWanted : "automatic");
    } else {
        fprintf(stderr,
                "window: Graphics API %s did not start (the lines above say why); back on %s\n",
                name, old);
    }
    /* back on the start's choice, on the card asked for before; a switch
       back that failed has the renderer on trial again */
    ico_diag_set_backend_trial(trial ? NULL : old);
    rhi_set_adapter_wanted(oldWanted);
    ico_diag_step("window: switch: %s did not start; back to %s", name, old);
    if (ico_window_reopen(old, trial ? explicitFor(old) : true)) {
        rd_resume_frame();
        installAdapters();
        return -1;
    }
    fprintf(stderr, "window: Graphics API %s did not start again either; the game ends\n", old);
    return -3;
}

int ico_window_switch_pump(void)
{
    char want[16], old[16], wantAdapter[256], oldAdapter[256];

    if (s_sw.want[0] == '\0') {
        return 0;
    }
    /* after the game's step: the frame dl_Clear opened for the next tick
       is open again after the switch (rd_resume_frame) and closed without
       a picture, so the tick of the switch shows none */
    snprintf(want, sizeof(want), "%s", s_sw.want);
    snprintf(old, sizeof(old), "%s", ico_window_switch_running());
    snprintf(wantAdapter, sizeof(wantAdapter), "%s", s_sw.wantAdapter);
    snprintf(oldAdapter, sizeof(oldAdapter), "%s", rhi_adapter_name());
    s_sw.want[0] = '\0';
    const Uint64 t0 = SDL_GetTicksNS();
    /* the device's start and the pipeline set are not a hang of the game,
       but a step that never ends is one: the switch's own watch
       (diag_host.h ico_diag_watchdog_switch) in place of a pause without a
       limit, so a hang inside the driver ends with the report and the main
       thread's stack instead of a window that says "Not Responding" for
       good (the rc2 report: the watchdog paused, the log ending there) */
    {
        char what[96];

        snprintf(what, sizeof(what), "switching the renderer from %s to %s", old, want);
        ico_diag_watchdog_switch(SWITCH_WATCH_S, what);
    }
    const int r = ico_window_switch_backend(want, wantAdapter, s_sw.why == UI_GFX_WHY_APPLY);
    ico_diag_watchdog_switch(0, NULL);
    const double secs = (double)(SDL_GetTicksNS() - t0) / 1e9;
    const char *how = s_sw.why == UI_GFX_WHY_TIMEOUT ? "not kept in 10 s" : "reverted";
    /* what changed: the renderer, the card it runs on (or both) */
    const bool backendChanged = strcmp(want, old) != 0;
    const bool adapterChanged = r == 0 && strcmp(oldAdapter, rhi_adapter_name()) != 0;
    if (r == 0 && s_sw.why == UI_GFX_WHY_APPLY) {
        s_sw.backendChanged = backendChanged;
        s_sw.adapterChanged = adapterChanged;
        if (backendChanged || !adapterChanged) {
            fprintf(stderr, "window: Graphics API %s applied from the menu (was %s), %.1f s\n",
                    want, old, secs);
        }
        if (adapterChanged) {
            fprintf(stderr, "window: Graphics card %s applied from the menu (was %s), %.1f s\n",
                    rhi_adapter_name(), oldAdapter, secs);
        }
    } else if (r == 0) {
        if (backendChanged || !adapterChanged) {
            fprintf(stderr, "window: Graphics API %s restored (%s), %.1f s\n", want, how, secs);
        }
        if (adapterChanged) {
            fprintf(stderr, "window: Graphics card %s restored (%s), %.1f s\n", rhi_adapter_name(),
                    how, secs);
        }
    }
    s_sw.result = r == 0    ? UI_GFX_SWITCH_DONE
                  : r == -2 ? UI_GFX_SWITCH_REFUSED
                            : UI_GFX_SWITCH_FAILED;
    if (r == -3) {
        s_sw.ended = 1;
        snprintf(s_sw.endText, sizeof(s_sw.endText),
                 "The game's graphics could not start again after Options > Graphics > Apply: "
                 "neither %s nor %s started.\n"
                 "The game ends. Your settings were not saved, so the next start uses the "
                 "renderer it used before.",
                 properName(want), properName(old));
        return -1;
    }
    return 0;
}

const char *ico_window_switch_end_text(void)
{
    return s_sw.ended ? s_sw.endText : NULL;
}

/* --- the Graphics page's host (settings.h UiGraphicsHost) --------------- */

static int hostBegin(const char *name, const char *adapter, int why)
{
    const char *no = refusal(name);

    if (no != NULL) {
        fprintf(stderr, "window: Graphics API %s not switched to: %s\n",
                name != NULL ? name : "(none)", no);
        return UI_GFX_SWITCH_REFUSED;
    }
    snprintf(s_sw.want, sizeof(s_sw.want), "%s", name);
    snprintf(s_sw.wantAdapter, sizeof(s_sw.wantAdapter), "%s", adapter != NULL ? adapter : "");
    s_sw.why = why;
    s_sw.result = UI_GFX_SWITCH_PENDING;
    return UI_GFX_SWITCH_PENDING;
}

static int hostPoll(void)
{
    return s_sw.result;
}

/* Keep: the crash and loss steps take the kept renderer as the player's
   (the page writes it) */
static void hostKept(void)
{
    const char *now = ico_window_switch_running();

    ico_diag_set_backend_trial(NULL);
    s_start.backendExplicit = true;
    snprintf(s_start.backend, sizeof(s_start.backend), "%s", now);
    ico_diag_set_backend_choice(1, now);
    if (s_sw.backendChanged || !s_sw.adapterChanged) {
        fprintf(stderr, "window: Graphics API %s kept\n", now);
    }
    if (s_sw.adapterChanged) {
        fprintf(stderr, "window: Graphics card %s kept\n", rhi_adapter_name());
    }
}

static const char *hostRunningAdapter(void)
{
    return rhi_adapter_name();
}

/* the renderer Automatic started this run, NULL when the player chose one
   at the start (Automatic then starts Vulkan first) */
static const char *hostAutomatic(void)
{
    return s_start.automatic[0] != '\0' ? s_start.automatic : NULL;
}

void ico_window_switch_install(void)
{
    UiGraphicsHost h;

    h.running = ico_window_switch_running;
    h.switchBegin = hostBegin;
    h.switchPoll = hostPoll;
    h.kept = hostKept;
    h.automatic = hostAutomatic;
    h.runningAdapter = hostRunningAdapter;
    ui_settings_set_graphics_host(&h);
    installAdapters();
}
