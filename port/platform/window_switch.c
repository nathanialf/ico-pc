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
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

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
    int waited;    /* pumps waited for the game's frame to close */
} s_sw;

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

int ico_window_switch_backend(const char *name)
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
    if (ico_window_reopen(name)) {
        return 0;
    }
    fprintf(stderr, "window: Graphics API %s did not start (the lines above say why); back on %s\n",
            name, old);
    if (ico_window_reopen(old)) {
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
    /* between two of the game's frames when it can: a frame the game is
       drawing is dropped with the renderer (half a second at most) */
    if (rd_frame_open() && ++s_sw.waited < 30) {
        return 0;
    }
    snprintf(want, sizeof(want), "%s", s_sw.want);
    snprintf(old, sizeof(old), "%s", ico_window_switch_running());
    s_sw.want[0] = '\0';
    const Uint64 t0 = SDL_GetTicksNS();
    const int r = ico_window_switch_backend(want);
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
    return r == -3 ? -1 : 0;
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
    s_sw.waited = 0;
    s_sw.result = UI_GFX_SWITCH_PENDING;
    return UI_GFX_SWITCH_PENDING;
}

static int hostPoll(void)
{
    return s_sw.result;
}

static void hostKept(void)
{
    fprintf(stderr, "window: Graphics API %s kept\n", ico_window_switch_running());
}

void ico_window_switch_install(void)
{
    UiGraphicsHost h;

    h.running = ico_window_switch_running;
    h.switchBegin = hostBegin;
    h.switchPoll = hostPoll;
    h.kept = hostKept;
    ui_settings_set_graphics_host(&h);
}
