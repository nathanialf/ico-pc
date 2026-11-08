/*
 * port/game/title_icon.c
 *
 * The application icon's capture on the title: title_icon.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "title_icon.h"
#include "app_icon.h" /* port/platform: ICO_APP_ICON_OK, _GHOST, _FAILED */

/* the pass's two clear colours, RGBAQ in GS units */
static const uint8_t kBlack[4] = {0, 0, 0, 0x80};
static const uint8_t kWhite[4] = {0xFF, 0xFF, 0xFF, 0x80};

#if defined(ICO_RD) && !defined(__ANDROID__) && !defined(ICO_IOS)
#include "rd.h"
#include "ui_host.h"

/* the shot's answer, stored by the presenting thread (rd_capture_shot's fn
   runs there after the present) and taken here on the game thread */
static struct {
    uint8_t *px;
    uint32_t w, h;
    int ready;
} s_got;

static void gotShot(uint8_t *rgba, uint32_t w, uint32_t h, void *user)
{
    (void)user;
    s_got.px = rgba;
    s_got.w = rgba ? w : 0;
    s_got.h = rgba ? h : 0;
    __atomic_store_n(&s_got.ready, 1, __ATOMIC_RELEASE);
}

static void gotForget(void)
{
    if (__atomic_load_n(&s_got.ready, __ATOMIC_ACQUIRE)) {
        free(s_got.px);
        s_got.px = NULL;
        __atomic_store_n(&s_got.ready, 0, __ATOMIC_RELEASE);
    }
}

static void realCancel(void)
{
    rd_capture_shot_cancel(); /* an armed request forgotten: its fn is not called */
    gotForget();
}

static int realArm(void)
{
    realCancel(); /* so no replaced request answers NULL into the new one's slot */
    return rd_capture_shot(gotShot, NULL) ? 1 : 0;
}

static int realTake(uint8_t **rgba, uint32_t *w, uint32_t *h)
{
    if (!__atomic_load_n(&s_got.ready, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    *rgba = s_got.px;
    *w = s_got.w;
    *h = s_got.h;
    s_got.px = NULL;
    __atomic_store_n(&s_got.ready, 0, __ATOMIC_RELEASE);
    return 1;
}

static void realPass(const uint8_t *clearRgba)
{
    rd_set_icon_pass(clearRgba);
}

static int realPresented(uint32_t *frame, float *alpha)
{
    RdPresentInfo pi;

    if (!rd_last_present_info(&pi)) {
        return 0;
    }
    *frame = pi.frame;
    *alpha = pi.alpha;
    return 1;
}

static const IcoTitleIconOps kReal = {
    ico_app_icon_wanted,
    realArm,
    realTake,
    realCancel,
    realPass,
    ui_host_curtain,
    ico_app_icon_deliver,
    ico_app_icon_deliver_single,
    rd_icon_pass_last_frame,
    realPresented,
};
static const IcoTitleIconOps *const kDefault = &kReal;
#else
static const IcoTitleIconOps *const kDefault = NULL;
#endif

static const IcoTitleIconOps *s_ops = kDefault;

static struct {
    int state;
    int menuTicks; /* idle: ticks the title's menu has been up */
    int ticks;     /* in the state */
    int drops;     /* shots armed again in this run */
    int ghostRetried;
    int pass, curtain;
    int armed;         /* a shot armed and not answered */
    int doneAfter;     /* the curtain step's end: done for the run, or idle */
    uint32_t passLast; /* the newest frame that may hold the pass (at its off) */
    uint8_t *a;        /* the frame over black */
    uint32_t aw, ah;
} s;

static void passSet(const uint8_t *clearRgba)
{
    s.pass = clearRgba != NULL;
    s_ops->pass(clearRgba);
}

static void curtainSet(int on)
{
    s.curtain = on;
    s_ops->curtain(on);
}

/* the pass off; the curtain held until a present shows none of the pass's
   frames, then done or idle */
static void finish(int doneForRun)
{
    if (s.armed) {
        s.armed = 0;
        s_ops->cancel();
    }
    free(s.a);
    s.a = NULL;
    if (s.pass) {
        passSet(NULL);
    }
    s.passLast = s_ops->pass_last();
    s.doneAfter = doneForRun;
    s.state = ICO_TITLE_ICON_CURTAIN;
    s.ticks = 0;
}

/* a shot of the pass's colour once it settled */
static void settle(int state)
{
    s.state = state;
    s.ticks = 0;
}

static void startPair(void)
{
    free(s.a);
    s.a = NULL;
    passSet(kBlack);
    if (!s.curtain) {
        curtainSet(1);
    }
    settle(ICO_TITLE_ICON_SETTLE_A);
}

/* an empty answer, no answer in time, or no renderer: armed again, or the
   run's capture given up */
static void dropped(const char *why)
{
    if (++s.drops > ICO_TITLE_ICON_RETRIES) {
        fprintf(stderr,
                "ico_pc: icon: no picture of the title came back (%s; %d tries): no icon made "
                "this run, the next start tries again\n",
                why, s.drops);
        finish(1);
        return;
    }
    fprintf(stderr, "ico_pc: icon: the title's picture was not taken (%s): trying again\n", why);
    /* armed on the next tick: the pass's colour is still in force */
    s.state = s.state == ICO_TITLE_ICON_SHOT_A ? ICO_TITLE_ICON_SETTLE_A : ICO_TITLE_ICON_SETTLE_B;
    s.ticks = ICO_TITLE_ICON_SETTLE - 1;
}

static void arm(void)
{
    s.state = s.state == ICO_TITLE_ICON_SETTLE_A ? ICO_TITLE_ICON_SHOT_A : ICO_TITLE_ICON_SHOT_B;
    s.ticks = 0;
    s.armed = s_ops->arm() != 0;
    if (!s.armed) {
        dropped("no renderer");
    }
}

/* the pair (a over black, b over white) to the icon files */
static void deliverPair(uint8_t *b, uint32_t w, uint32_t h)
{
    int r = ICO_APP_ICON_GHOST;
    if (w == s.aw && h == s.ah) {
        r = s_ops->deliver(s.a, b, (int)w, (int)h);
    }
    free(b);
    if (r == ICO_APP_ICON_GHOST && !s.ghostRetried) {
        s.ghostRetried = 1;
        fprintf(stderr,
                "ico_pc: icon: the two pictures differ (%ux%u, %ux%u; the title moved "
                "between them): taking the pair again\n",
                s.aw, s.ah, w, h);
        fprintf(stderr, "ico_pc: icon: capturing the title's I (pass 1 of 2)\n");
        startPair();
        return;
    }
    if (r == ICO_APP_ICON_GHOST) {
        fprintf(stderr, "ico_pc: icon: the two pictures differ again: the icon is cut from the "
                        "picture over black alone\n");
        r = s_ops->deliver_single(s.a, (int)s.aw, (int)s.ah);
    }
    if (r == ICO_APP_ICON_OK) {
        fprintf(stderr, "ico_pc: icon: made from the title's I (%ux%u pictures)\n", s.aw, s.ah);
    } else {
        fprintf(stderr, "ico_pc: icon: could not be made (the lines above say why): no icon "
                        "this run, the next start tries again\n");
    }
    finish(1);
}

void ico_title_icon_tick(int onMenu)
{
    if (!s_ops) {
        return;
    }
    switch (s.state) {
    case ICO_TITLE_ICON_DONE:
        return;
    case ICO_TITLE_ICON_IDLE:
        if (!onMenu) {
            s.menuTicks = 0;
            return;
        }
        if (++s.menuTicks < ICO_TITLE_ICON_WAIT) {
            return;
        }
        {
            const int wanted = s_ops->wanted();

            if (wanted == ICO_APP_ICON_NOT_PLAIN) {
                /* wanted, but the picture is not plain now (the CRT filter,
                   a colour adjustment or Upscaling on): idle, asked again
                   after another ICO_TITLE_ICON_WAIT ticks of the menu, this
                   visit or a later one */
                s.menuTicks = 0;
                return;
            }
            if (!wanted) {
                s.state = ICO_TITLE_ICON_DONE; /* this disc's icon is there and current */
                return;
            }
        }
        fprintf(stderr, "ico_pc: icon: capturing the title's I (pass 1 of 2)\n");
        s.drops = 0;
        s.ghostRetried = 0;
        startPair();
        return;
    case ICO_TITLE_ICON_CURTAIN: {
        /* the curtain is read as each present is recorded: a picture of a
           pass frame (a walk begun before the pass went off, recorded some
           ticks later; a blend with one) shows white or the bare letter, so
           it comes off once a present past them was recorded */
        uint32_t frame = 0;
        float alpha = 1.0f;
        const int clean = s_ops->presented(&frame, &alpha) &&
                          ico_title_icon_present_clean(frame, alpha, s.passLast);

        ++s.ticks;
        if (!clean && s.ticks >= ICO_TITLE_ICON_HOLD_MAX) {
            fprintf(stderr,
                    "ico_pc: icon: no picture past the pass's frames (frame %u) was presented "
                    "in %d ticks: the curtain comes off\n",
                    (unsigned)s.passLast, ICO_TITLE_ICON_HOLD_MAX);
        }
        if (clean || s.ticks >= ICO_TITLE_ICON_HOLD_MAX) {
            curtainSet(0);
            s.state = s.doneAfter ? ICO_TITLE_ICON_DONE : ICO_TITLE_ICON_IDLE;
            s.menuTicks = 0;
        }
        return;
    }
    default:
        break;
    }
    if (!onMenu) {
        fprintf(stderr, "ico_pc: icon: the title's menu was left during the capture: tried "
                        "again the next time it shows\n");
        finish(0);
        return;
    }
    switch (s.state) {
    case ICO_TITLE_ICON_SETTLE_A:
    case ICO_TITLE_ICON_SETTLE_B:
        if (++s.ticks >= ICO_TITLE_ICON_SETTLE) {
            arm();
        }
        return;
    case ICO_TITLE_ICON_SHOT_A:
    case ICO_TITLE_ICON_SHOT_B: {
        uint8_t *px = NULL;
        uint32_t w = 0, h = 0;
        if (!s_ops->take(&px, &w, &h)) {
            if (++s.ticks >= ICO_TITLE_ICON_TIMEOUT) {
                s.armed = 0;
                s_ops->cancel();
                dropped("no present within the wait");
            }
            return;
        }
        s.armed = 0;
        if (px == NULL || w == 0 || h == 0) {
            free(px);
            dropped("the present dropped it");
            return;
        }
        if (s.state == ICO_TITLE_ICON_SHOT_A) {
            s.a = px;
            s.aw = w;
            s.ah = h;
            fprintf(stderr, "ico_pc: icon: capturing the title's I (pass 2 of 2)\n");
            passSet(kWhite);
            settle(ICO_TITLE_ICON_SETTLE_B);
            return;
        }
        deliverPair(px, w, h);
        return;
    }
    default:
        return;
    }
}

int ico_title_icon_pass_on(void)
{
    return s.pass;
}

int ico_title_icon_present_clean(uint32_t frame, float alpha, uint32_t passLast)
{
    if (frame <= passLast) {
        return 0;
    }
    return alpha >= 1.0f || frame - 1u > passLast;
}

void ico_title_icon_reset(void)
{
    if (s_ops) {
        if (s.armed) {
            s_ops->cancel();
        }
        if (s.pass) {
            s_ops->pass(NULL);
        }
        if (s.curtain) {
            s_ops->curtain(0);
        }
    }
    free(s.a);
    memset(&s, 0, sizeof(s));
}

void ico_title_icon_set_ops(const IcoTitleIconOps *ops)
{
    s_ops = ops ? ops : kDefault;
}

int ico_title_icon_state(void)
{
    return s.state;
}
