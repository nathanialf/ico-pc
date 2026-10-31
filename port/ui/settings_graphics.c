/*
 * port/ui/settings_graphics.c
 *
 * Settings > Graphics (settings_graphics.h): the Graphics API row
 * ([video] backend), the Graphics card row ([video] adapter) and Apply,
 * which asks the host (settings.h UiGraphicsHost, the window's
 * port/platform/window_switch.c) to switch the renderer and the card now,
 * then counts down with Keep and Revert.  settings.c builds the page and
 * hands its rows' values, steps and presses here.
 */
#include "settings.h"
#include "settings_graphics.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "popup.h"
#include "strings.h"

static int stepIndex(int i, int n, int dir)
{
    return ((i + dir) % n + n) % n;
}

/* the renderers the build links (ui_settings_set_backend_names): the config
   values the Graphics API row writes after "" (Automatic) */
#define MAX_BACKENDS 8
static const char *s_backendNames[MAX_BACKENDS];
static int s_backendCount;

/* the suitable cards of the renderer running (ui_settings_set_adapter_names):
   the config values the Graphics card row writes after "" (Automatic), the
   card running and the one Automatic picks ("" when not said) */
#define MAX_ADAPTERS 8
#define ADAPTER_NAME 256
static char s_adapterNames[MAX_ADAPTERS][ADAPTER_NAME];
static int s_adapterCount;
static char s_adapterCurrent[ADAPTER_NAME];
static char s_adapterAuto[ADAPTER_NAME];

/* Graphics > Apply (settings.h UiGraphicsHost): the host, and the switch
   under way or the countdown that follows it */
static UiGraphicsHost s_gfxHost;
static int s_gfxHostSet;

static struct {
    int state;                          /* UI__GFX_* */
    int left;                           /* the countdown's vsyncs left */
    int focus;                          /* the countdown began: the cursor goes to Revert */
    int haveEntry;                      /* entry and entryAdapter hold the values the page found */
    int backendChanged, adapterChanged; /* what Apply switched */
    char want[16];                      /* the renderer Apply asked for */
    char prev[16];                      /* the renderer running before Apply */
    char entry[32];
    /* the rows' values from Apply until Keep, outside the config (which
       holds the entries: every save path writes those, the close button's
       too) */
    char trial[32];
    char wantAdapter[ADAPTER_NAME]; /* the card Apply asked for ("" Automatic) */
    char prevAdapter[ADAPTER_NAME]; /* the card running before Apply */
    char entryAdapter[ADAPTER_NAME];
    char trialAdapter[ADAPTER_NAME];
} s_gfx;

static int underWay(void)
{
    return s_gfx.state == UI__GFX_APPLYING || s_gfx.state == UI__GFX_COUNTDOWN;
}

/* [video] backend as the row shows it: the trial while Apply's switch or
   the countdown is under way, else the config's */
static const char *backendValue(void)
{
    return underWay() ? s_gfx.trial : ico_config_get_string("video.backend", "");
}

const char *ui__gfx_adapter_value(void)
{
    return underWay() ? s_gfx.trialAdapter : ico_config_get_string("video.adapter", "");
}

/* [video] backend and [video] adapter as the page found them (its first
   step, or Apply), which Revert puts back; forgotten when the page is
   entered again */
static void gfxEntry(void)
{
    if (!s_gfx.haveEntry) {
        snprintf(s_gfx.entry, sizeof(s_gfx.entry), "%s",
                 ico_config_get_string("video.backend", ""));
        snprintf(s_gfx.entryAdapter, sizeof(s_gfx.entryAdapter), "%s",
                 ico_config_get_string("video.adapter", ""));
        s_gfx.haveEntry = 1;
    }
}

void ui_settings_set_backend_names(const char *const *names, int count)
{
    s_backendCount = 0;
    for (int i = 0; names != NULL && i < count && i < MAX_BACKENDS; i++) {
        if (names[i] != NULL && names[i][0] != '\0') {
            s_backendNames[s_backendCount++] = names[i];
        }
    }
}

void ui_settings_set_adapter_names(const char *const *names, int count, const char *current,
                                   const char *automatic)
{
    s_adapterCount = 0;
    for (int i = 0; names != NULL && i < count && s_adapterCount < MAX_ADAPTERS; i++) {
        if (names[i] != NULL && names[i][0] != '\0') {
            snprintf(s_adapterNames[s_adapterCount++], ADAPTER_NAME, "%s", names[i]);
        }
    }
    snprintf(s_adapterCurrent, sizeof(s_adapterCurrent), "%s", current != NULL ? current : "");
    snprintf(s_adapterAuto, sizeof(s_adapterAuto), "%s", automatic != NULL ? automatic : "");
}

int ui__gfx_backend_count(void)
{
    return s_backendCount;
}

int ui__gfx_adapter_count(void)
{
    return s_adapterCount;
}

/* the config's value as rhi_create_backend reads it: any case, "vk" for
   "vulkan" and "gl" for "opengl" (port/rhi/rhi_backend.c), into buf */
static const char *backendCanon(const char *v, char *buf, unsigned size)
{
    unsigned n = 0;
    for (; v != NULL && v[n] != '\0' && n + 1 < size; n++) {
        const char ch = v[n];
        buf[n] = ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
    }
    buf[n] = '\0';
    if (strcmp(buf, "vk") == 0) {
        return "vulkan";
    }
    if (strcmp(buf, "gl") == 0) {
        return "opengl";
    }
    return buf;
}

/* the Graphics API row: Automatic is index 0, then each name; the config's
   value decides (a value the build does not link reads as Automatic when
   stepped and shows as written) */
static int backendCurrent(void)
{
    char buf[32];
    const char *v = backendCanon(backendValue(), buf, sizeof(buf));
    for (int i = 0; i < s_backendCount; i++) {
        if (strcmp(v, s_backendNames[i]) == 0) {
            return i + 1;
        }
    }
    return 0;
}

/* a renderer's name as the player knows it (proper names, not translated) */
static const char *backendLabel(const char *name)
{
    char buf[32];
    const char *raw = name;
    name = backendCanon(name, buf, sizeof(buf));
    if (strcmp(name, "vulkan") == 0) {
        return "Vulkan";
    }
    if (strcmp(name, "opengl") == 0) {
        return "OpenGL";
    }
    if (strcmp(name, "d3d12") == 0) {
        return "Direct3D 12";
    }
    if (strcmp(name, "d3d11") == 0) {
        return "Direct3D 11";
    }
    return raw;
}

const char *ui__gfx_backend_text(char *buf, unsigned size)
{
    const char *v = backendValue();
    if (v == NULL || v[0] == '\0') {
        return ui_str(UI_STR_VAL_GRAPHICS_AUTOMATIC);
    }
    snprintf(buf, size, "%s", backendLabel(v));
    return buf;
}

int ui__gfx_step_backend(int dir)
{
    if (s_backendCount < 2) {
        return 0;
    }
    const int to = stepIndex(backendCurrent(), s_backendCount + 1, dir);
    const char *v = to == 0 ? "" : s_backendNames[to - 1];
    gfxEntry();
    ico_config_set_string("video.backend", v);
    fprintf(stderr, "settings: Graphics API set to %s (used from the next start)\n",
            v[0] ? v : "automatic");
    return 1;
}

/* the Graphics card row: Automatic is index 0, then each card; a saved
   name the list lacks (another renderer's, or a card taken out) shows as
   written and steps from Automatic */
static int adapterIndex(const char *v)
{
    for (int i = 0; v != NULL && v[0] != '\0' && i < s_adapterCount; i++) {
        if (strcmp(v, s_adapterNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

int ui__gfx_step_adapter(int dir)
{
    if (s_adapterCount < 2) {
        return 0;
    }
    const int to = stepIndex(adapterIndex(ui__gfx_adapter_value()) + 1, s_adapterCount + 1, dir);
    const char *v = to == 0 ? "" : s_adapterNames[to - 1];
    gfxEntry();
    ico_config_set_string("video.adapter", v);
    fprintf(stderr, "settings: Graphics card set to %s (used from the next start)\n",
            v[0] ? v : "automatic");
    return 1;
}

/* the renderer the row's value starts: its name; for Automatic the one it
   started this run (the host's automatic(): a fallback or a restart's), else
   "vulkan" (the start's first try); NULL for a value the build does not
   link */
static const char *gfxChosen(void)
{
    const int i = backendCurrent();
    const char *v = backendValue();
    const char *a = s_gfxHostSet && s_gfxHost.automatic != NULL ? s_gfxHost.automatic() : NULL;
    if (i > 0) {
        return s_backendNames[i - 1];
    }
    for (int k = 0; (v == NULL || v[0] == '\0') && k < s_backendCount; k++) {
        if (strcmp(s_backendNames[k], a != NULL ? a : "vulkan") == 0) {
            return s_backendNames[k];
        }
    }
    return NULL;
}

/* the card running: the host's, else the one the names came with */
static const char *runningAdapter(void)
{
    const char *r =
        s_gfxHostSet && s_gfxHost.runningAdapter != NULL ? s_gfxHost.runningAdapter() : NULL;
    return r != NULL ? r : s_adapterCurrent;
}

/* the card the row's value starts: a listed name, or for Automatic the
   card Automatic picks; NULL when not known (a name the list lacks, or
   Automatic's card not said) */
static const char *adapterChosen(void)
{
    const char *v = ui__gfx_adapter_value();
    if (v[0] == '\0') {
        return s_adapterAuto[0] != '\0' ? s_adapterAuto : NULL;
    }
    return adapterIndex(v) >= 0 ? v : NULL;
}

static int backendDiffers(void)
{
    const char *c = gfxChosen();
    return s_backendCount >= 2 && c != NULL && strcmp(c, s_gfxHost.running()) != 0;
}

static int adapterDiffers(void)
{
    const char *c = adapterChosen();
    const char *r = runningAdapter();
    return s_adapterCount >= 2 && c != NULL && r[0] != '\0' && strcmp(c, r) != 0;
}

int ui__gfx_apply_shown(void)
{
    return s_gfxHostSet && s_gfx.state == UI__GFX_IDLE && (backendDiffers() || adapterDiffers());
}

/* a card's name for the failed popup: Automatic's card, or the word */
static const char *adapterLabel(const char *name)
{
    if (name[0] != '\0') {
        return name;
    }
    return s_adapterAuto[0] != '\0' ? s_adapterAuto : ui_str(UI_STR_VAL_GRAPHICS_AUTOMATIC);
}

/* what came of a switch the page asked for */
static void gfxResult(int r)
{
    char body[UI_POPUP_TEXT];
    const int applying = s_gfx.state == UI__GFX_APPLYING;
    s_gfx.state = UI__GFX_IDLE;
    if (r == UI_GFX_SWITCH_DONE && applying) {
        /* the countdown, the cursor on Revert (settings.c gfxRefresh) */
        s_gfx.state = UI__GFX_COUNTDOWN;
        s_gfx.left = UI_GFX_KEEP_SECONDS * ui_popup_vsync_hz();
        s_gfx.focus = 1;
    } else if (r == UI_GFX_SWITCH_REFUSED && applying) {
        /* nothing tried: the rows keep the player's values */
        ico_config_set_string("video.backend", s_gfx.trial);
        ico_config_set_string("video.adapter", s_gfx.trialAdapter);
    } else if (r == UI_GFX_SWITCH_FAILED) {
        /* the host is back on the renderer and card that ran; after Apply
           the values the page found stay (the config held them since
           Apply), so the file names a renderer that starts, as after
           Revert */
        if (applying && s_gfx.backendChanged) {
            fprintf(stderr, "settings: Graphics API back to %s (%s did not start)\n",
                    s_gfx.entry[0] ? s_gfx.entry : "automatic", s_gfx.want);
        }
        if (applying && s_gfx.adapterChanged) {
            fprintf(stderr, "settings: Graphics card back to %s (%s did not start)\n",
                    s_gfx.entryAdapter[0] ? s_gfx.entryAdapter : "automatic",
                    adapterLabel(s_gfx.wantAdapter));
        }
        if (s_gfx.backendChanged) {
            const char *bad = applying ? s_gfx.want : s_gfx.prev;
            const char *kept = applying ? s_gfx.prev : s_gfx.want;
            snprintf(body, sizeof(body), ui_str(UI_STR_GRAPHICS_FAILED), backendLabel(bad),
                     backendLabel(kept));
        } else {
            const char *bad = applying ? adapterLabel(s_gfx.wantAdapter) : s_gfx.prevAdapter;
            const char *kept = applying ? s_gfx.prevAdapter : adapterLabel(s_gfx.wantAdapter);
            snprintf(body, sizeof(body), ui_str(UI_STR_GRAPHICS_ADAPTER_FAILED), bad, kept);
        }
        ui_popup_push(body, "");
    }
}

/* a switch to name on adapter; the host answers now or later (gfxPoll) */
static void gfxBegin(const char *name, const char *adapter, int why, int state)
{
    s_gfx.state = state;
    const int r = s_gfxHost.switchBegin(name, adapter, why);
    if (r != UI_GFX_SWITCH_PENDING) {
        gfxResult(r);
    }
}

void ui__gfx_poll(void)
{
    if (s_gfxHostSet && (s_gfx.state == UI__GFX_APPLYING || s_gfx.state == UI__GFX_REVERTING)) {
        const int r = s_gfxHost.switchPoll();
        if (r != UI_GFX_SWITCH_PENDING) {
            gfxResult(r);
        }
    }
}

void ui__gfx_apply(void)
{
    if (!ui__gfx_apply_shown()) {
        return;
    }
    const char *c = gfxChosen();
    gfxEntry();
    s_gfx.backendChanged = backendDiffers();
    s_gfx.adapterChanged = adapterDiffers();
    snprintf(s_gfx.want, sizeof(s_gfx.want), "%s",
             s_gfx.backendChanged && c != NULL ? c : s_gfxHost.running());
    snprintf(s_gfx.prev, sizeof(s_gfx.prev), "%s", s_gfxHost.running());
    /* the card asked for: the row's value ("" Automatic), the running one
       when only the renderer changes */
    snprintf(s_gfx.wantAdapter, sizeof(s_gfx.wantAdapter), "%s",
             s_gfx.adapterChanged ? ui__gfx_adapter_value() : runningAdapter());
    snprintf(s_gfx.prevAdapter, sizeof(s_gfx.prevAdapter), "%s", runningAdapter());
    /* the choice on trial outside the config, which holds the values the
       page found until Keep */
    snprintf(s_gfx.trial, sizeof(s_gfx.trial), "%s", ico_config_get_string("video.backend", ""));
    snprintf(s_gfx.trialAdapter, sizeof(s_gfx.trialAdapter), "%s",
             ico_config_get_string("video.adapter", ""));
    ico_config_set_string("video.backend", s_gfx.entry);
    ico_config_set_string("video.adapter", s_gfx.entryAdapter);
    gfxBegin(s_gfx.want, s_gfx.wantAdapter, UI_GFX_WHY_APPLY, UI__GFX_APPLYING);
}

void ui__gfx_revert(int why)
{
    if (s_gfx.state != UI__GFX_COUNTDOWN) {
        return;
    }
    ico_config_set_string("video.backend", s_gfx.entry);
    ico_config_set_string("video.adapter", s_gfx.entryAdapter);
    const char *how = why == UI_GFX_WHY_TIMEOUT ? "not kept in 10 s" : "reverted";
    if (s_gfx.backendChanged) {
        fprintf(stderr, "settings: Graphics API back to %s (%s)\n",
                s_gfx.entry[0] ? s_gfx.entry : "automatic", how);
    }
    if (s_gfx.adapterChanged) {
        fprintf(stderr, "settings: Graphics card back to %s (%s)\n",
                s_gfx.entryAdapter[0] ? s_gfx.entryAdapter : "automatic", how);
    }
    gfxBegin(s_gfx.prev, s_gfx.prevAdapter, why, UI__GFX_REVERTING);
}

int ui__gfx_keep(void)
{
    if (s_gfx.state != UI__GFX_COUNTDOWN) {
        return 0;
    }
    s_gfx.state = UI__GFX_IDLE;
    ico_config_set_string("video.backend", s_gfx.trial);
    ico_config_set_string("video.adapter", s_gfx.trialAdapter);
    snprintf(s_gfx.entry, sizeof(s_gfx.entry), "%s", s_gfx.trial);
    snprintf(s_gfx.entryAdapter, sizeof(s_gfx.entryAdapter), "%s", s_gfx.trialAdapter);
    if (s_gfxHost.kept != NULL) {
        s_gfxHost.kept();
    }
    return 1;
}

void ui_settings_set_graphics_host(const UiGraphicsHost *host)
{
    s_gfxHostSet = host != NULL && host->running != NULL && host->switchBegin != NULL &&
                   host->switchPoll != NULL;
    if (s_gfxHostSet) {
        s_gfxHost = *host;
    }
    memset(&s_gfx, 0, sizeof(s_gfx));
}

int ui__gfx_state(void)
{
    return s_gfx.state;
}

int ui__gfx_left(void)
{
    return s_gfx.left;
}

int ui__gfx_take_focus(void)
{
    if (!s_gfx.focus || s_gfx.state != UI__GFX_COUNTDOWN) {
        return 0;
    }
    s_gfx.focus = 0;
    return 1;
}

void ui__gfx_page_entered(void)
{
    if (s_gfx.state == UI__GFX_IDLE) {
        s_gfx.haveEntry = 0;
    }
}

void ui__gfx_vsync(void)
{
    if (s_gfx.state == UI__GFX_COUNTDOWN && --s_gfx.left <= 0) {
        ui__gfx_revert(UI_GFX_WHY_TIMEOUT);
    }
}
