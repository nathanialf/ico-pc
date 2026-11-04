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

#include "config.h" /* [dev] backend_switch_test */
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

/* the call the driver stalled in during the switch (RhiWaitHooks.stalled:
   "sprite_aa1_ps", "" none) and how long it had held */
static char s_stalled[128];
static double s_stalledS;

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
    s_stalled[0] = '\0';
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
        if (s_stalled[0] != '\0') {
            /* the driver stalled in a compile (rc2: NVIDIA's GLSL compiler
               on sprite_aa1_ps): the player is told, and the switch is
               back where it was; the box's time is not a hang */
            char text[640];

            snprintf(text, sizeof(text),
                     "%s could not start: the graphics driver stalled compiling %s (no answer "
                     "in %.0f s).\n\nThe game went back to %s. The log in the logs folder "
                     "says more.",
                     properName(name), s_stalled, s_stalledS, properName(old));
            fprintf(stderr,
                    "window: Graphics API %s: the driver stalled compiling %s; the box "
                    "says so\n",
                    name, s_stalled);
            ico_diag_watchdog_pause(1);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "ICO PC", text, NULL);
            ico_diag_watchdog_pause(0);
        }
        return -1;
    }
    fprintf(stderr, "window: Graphics API %s did not start again either; the game ends\n", old);
    return -3;
}

static int hostBegin(const char *name, const char *adapter, int why);
static void hostKept(void);

/* [dev] backend_switch_test = "opengl,vulkan" (ini backend_switch_test):
   the renderers in turn through Apply's own path (hostBegin, the pump's
   switch, Keep), the first after SELFTEST_FIRST pumps and each next one
   SELFTEST_NEXT pumps after the last returned, waiting while a switch is
   refused (a movie); each line says how it went, the last one the count.
   The window build's switch test (port/platform/test/backend_switch.cmake,
   ctest backend_switch_window) runs it on lavapipe and llvmpipe. */
#define SELFTEST_FIRST 300u
#define SELFTEST_NEXT 120u

static struct {
    int inited, asked, done;
    char list[128];
    size_t pos;
    unsigned wait, switches, failed;
    char from[16], to[16];
    Uint64 t0;
    char refused[64];
} s_st;

static void selftestPoll(void)
{
    if (!s_st.inited) {
        const char *v = ico_config_get_string("dev.backend_switch_test", "");

        s_st.inited = 1;
        snprintf(s_st.list, sizeof(s_st.list), "%s", v != NULL ? v : "");
        s_st.wait = SELFTEST_FIRST;
        if (s_st.list[0] != '\0') {
            ico_diag_step("window: backend switch test: %s, from %s, the first after %u pumps",
                          s_st.list, ico_window_switch_running(), SELFTEST_FIRST);
        }
    }
    if (s_st.list[0] == '\0' || s_st.done || s_sw.want[0] != '\0') {
        return;
    }
    if (s_st.asked) {
        const int r = s_sw.result;
        const double secs = (double)(SDL_GetTicksNS() - s_st.t0) / 1e9;

        s_st.asked = 0;
        s_st.switches++;
        if (r == UI_GFX_SWITCH_DONE && strcmp(ico_window_switch_running(), s_st.to) == 0) {
            hostKept();
            ico_diag_step("window: backend switch test: %s -> %s: applied and kept in %.1f s",
                          s_st.from, s_st.to, secs);
        } else {
            s_st.failed++;
            ico_diag_step("window: backend switch test: %s -> %s: FAILED (%s; running %s) after "
                          "%.1f s",
                          s_st.from, s_st.to,
                          r == UI_GFX_SWITCH_REFUSED ? "refused"
                          : r == UI_GFX_SWITCH_DONE  ? "done on another renderer"
                                                     : "did not start",
                          ico_window_switch_running(), secs);
        }
        s_st.wait = SELFTEST_NEXT;
    }
    if (s_st.wait > 0) {
        s_st.wait--;
        return;
    }
    while (s_st.list[s_st.pos] == ',' || s_st.list[s_st.pos] == ' ') {
        s_st.pos++;
    }
    if (s_st.list[s_st.pos] == '\0') {
        s_st.done = 1;
        ico_diag_step("window: backend switch test: done, %u switch%s, %u failed", s_st.switches,
                      s_st.switches == 1 ? "" : "es", s_st.failed);
        return;
    }
    size_t n = strcspn(s_st.list + s_st.pos, ", ");
    if (n >= sizeof(s_st.to)) {
        n = sizeof(s_st.to) - 1;
    }
    memcpy(s_st.to, s_st.list + s_st.pos, n);
    s_st.to[n] = '\0';
    {
        const char *no = refusal(s_st.to);

        if (no != NULL && strcmp(no, "a movie plays") == 0) {
            /* the opening's movie: the next pump asks again */
            if (strcmp(s_st.refused, no) != 0) {
                snprintf(s_st.refused, sizeof(s_st.refused), "%s", no);
                fprintf(stderr, "window: backend switch test: waiting (%s)\n", no);
            }
            return;
        }
    }
    s_st.refused[0] = '\0';
    s_st.pos += strcspn(s_st.list + s_st.pos, ", ");
    snprintf(s_st.from, sizeof(s_st.from), "%s", ico_window_switch_running());
    s_st.t0 = SDL_GetTicksNS();
    s_st.asked = 1;
    if (hostBegin(s_st.to, "", UI_GFX_WHY_APPLY) != UI_GFX_SWITCH_PENDING) {
        s_sw.result = UI_GFX_SWITCH_REFUSED; /* counted at the next pump */
    }
}

int ico_window_switch_pump(void)
{
    char want[16], old[16], wantAdapter[256], oldAdapter[256];

    selftestPoll();
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

/* --- the renderer's wait hooks (rhi.h RhiWaitHooks) --------------------- */

/* a long driver call in progress: the window's events taken, when the
   thread waiting is the window's (the render thread's waits take none) */
static void waitPump(void *ctx, const char *what, uint64_t heldNs)
{
    (void)ctx;
    (void)what;
    (void)heldNs;
    if (SDL_IsMainThread()) {
        SDL_PumpEvents();
    }
}

/* a call given up on past the limit (the backend's line says which) */
static void waitStalled(void *ctx, const char *what, uint64_t heldNs)
{
    (void)ctx;
    snprintf(s_stalled, sizeof(s_stalled), "%s", what != NULL ? what : "a shader");
    s_stalledS = (double)heldNs / 1e9;
}

/* a shader or pipeline made: the switch's watch moves (any thread); the
   start-up shader set's lines (diag_host.h ico_diag_shader_set) on the
   thread that makes it, which is the window's (the device's start) */
static void waitMade(void *ctx)
{
    static Uint64 s_setNs;
    uint32_t made = 0, total = 0;

    (void)ctx;
    ico_diag_switch_progress();
    if (!SDL_IsMainThread()) {
        return;
    }
    rhi_plan_progress(&made, &total);
    if (made == 1 && total > 0) {
        s_setNs = SDL_GetTicksNS();
    }
    ico_diag_shader_set(made, total, (double)(SDL_GetTicksNS() - s_setNs) / 1e9);
}

void ico_window_switch_wait_hooks(void)
{
    const double limit = ico_diag_gpu_hang_limit();
    RhiWaitHooks h;

    memset(&h, 0, sizeof(h));
    h.pump = waitPump;
    h.stalled = waitStalled;
    h.made = waitMade;
    h.limitNs = limit > 0.0 ? (uint64_t)(limit * 1e9) : 0u;
    rhi_set_wait_hooks(&h);
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
