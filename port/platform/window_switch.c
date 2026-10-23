/*
 * port/platform/window_switch.c
 *
 * Options > Graphics > Apply: the renderer switched in the process
 * (window_switch.h).  The game's state is the CPU's: the renderer keeps
 * what the game set up on it and does not set again (rd_suspend), and
 * makes the rest again as the game draws (its textures from the game's
 * memory, its meshes from the packets); the glow and the blur trail start
 * again, as after a resume, and the pipelines come from the backend's
 * cache or are compiled.  The audio is not touched.  Nothing is written:
 * the page writes [video] backend when the player keeps the choice.
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

extern int mpegPlay;
/* ico2/seki/src/GifPacket.c: the placeholder textures it made */
extern void gif_HostForgetTextures(void);

/* the switch the page asked for, run by the pump */
static struct {
    char want[16]; /* "" none */
    int why;       /* UI_GFX_WHY_* */
    int result;    /* UI_GFX_SWITCH_* of the last one asked */
    int ended;     /* -3: no renderer left (ico_window_switch_end_text) */
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

/* why the switch is refused now, NULL when it is not */
static const char *refusal(const char *name)
{
    const IcoTouchEditor *e = ico_input_sdl_touch_editor();

    if (rhi_backend_name(1) == NULL) {
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

int ico_window_switch_backend(const char *name, int trial)
{
    char old[16];
    const char *why = refusal(name);

    snprintf(old, sizeof(old), "%s", ico_window_switch_running());
    if (why != NULL) {
        fprintf(stderr, "window: Graphics API %s not switched to: %s\n",
                name != NULL ? name : "(none)", why);
        return -2;
    }
    if (strcmp(name, old) == 0) {
        return 0;
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
        return 0;
    }
    fprintf(stderr, "window: Graphics API %s did not start (the lines above say why); back on %s\n",
            name, old);
    /* back on the start's choice; a switch back that failed has the
       renderer on trial again */
    ico_diag_set_backend_trial(trial ? NULL : old);
    if (ico_window_reopen(old, trial ? explicitFor(old) : true)) {
        rd_resume_frame();
        return -1;
    }
    fprintf(stderr, "window: Graphics API %s did not start again either; the game ends\n", old);
    return -3;
}

int ico_window_switch_pump(void)
{
    char want[16], old[16];

    if (s_sw.want[0] == '\0') {
        return 0;
    }
    /* after the game's step: the frame dl_Clear opened for the next tick
       is open again after the switch (rd_resume_frame) and closed without
       a picture, so the tick of the switch shows none */
    snprintf(want, sizeof(want), "%s", s_sw.want);
    snprintf(old, sizeof(old), "%s", ico_window_switch_running());
    s_sw.want[0] = '\0';
    const Uint64 t0 = SDL_GetTicksNS();
    /* the device's start and the pipeline set are not a hang of the game
       (the device-lost box's way) */
    ico_diag_watchdog_pause(1);
    const int r = ico_window_switch_backend(want, s_sw.why == UI_GFX_WHY_APPLY);
    ico_diag_watchdog_pause(0);
    const double secs = (double)(SDL_GetTicksNS() - t0) / 1e9;
    if (r == 0 && s_sw.why == UI_GFX_WHY_APPLY) {
        fprintf(stderr, "window: Graphics API %s applied from the menu (was %s), %.1f s\n", want,
                old, secs);
    } else if (r == 0) {
        fprintf(stderr, "window: Graphics API %s restored (%s), %.1f s\n", want,
                s_sw.why == UI_GFX_WHY_TIMEOUT ? "not kept in 10 s" : "reverted", secs);
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

static int hostBegin(const char *name, int why)
{
    const char *no = refusal(name);

    if (no != NULL) {
        fprintf(stderr, "window: Graphics API %s not switched to: %s\n",
                name != NULL ? name : "(none)", no);
        return UI_GFX_SWITCH_REFUSED;
    }
    snprintf(s_sw.want, sizeof(s_sw.want), "%s", name);
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
    fprintf(stderr, "window: Graphics API %s kept\n", now);
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
    ui_settings_set_graphics_host(&h);
}
