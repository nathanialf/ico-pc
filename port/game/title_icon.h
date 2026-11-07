/*
 * port/game/title_icon.h
 *
 * The application icon's capture on the title (issue 166): the title
 * lettering's "I" alone, from two title frames, one over a black clear and
 * one over a white clear, for the difference matte that gives its exact
 * alpha and colour (port/platform/app_icon.h ico_app_icon_deliver).
 *
 * The pass, while on: title_logo.c's ico_title_logo_skip leaves out every
 * object but "I" (the title's backdrop, the castle, C, O, the glows and the
 * shadows); the renderer leaves out the menus and clears to the pass's
 * colour (rd.h rd_set_icon_pass); the port's overlay covers the output with
 * black (ui_host.h ui_host_curtain), so the player sees a few black frames.
 * The capture is rd_capture_shot's (rd.h), copied before the overlay, with
 * its own answer slot (the pause picture's deferred slot,
 * rd_capture_shot_arm, is polled by state_host.c every host step).
 *
 * The steps (ico_title_icon_tick, once a Main tick from
 * ico_title_logo_update, with whether the title's menu is up and the logo
 * shown):
 *   idle      counts the ticks the menu has been up; at
 *             ICO_TITLE_ICON_WAIT (the intro is over) asks the icon files
 *             whether an icon is wanted (none for this disc, or one made by
 *             another version): no, done for the run; yes, the pass on
 *             over black and the curtain on
 *   settle A  ICO_TITLE_ICON_SETTLE ticks with the pass in force before the
 *             shot is armed: the tick the pass came on had already
 *             recorded the menus, and a picture blended between two ticks
 *             takes the older one's too
 *   shot A    waits for the answer; a picture (black) kept, the clear
 *             turned white, settle B, shot B
 *   shot B    a picture: the pass off, the pair delivered (the matte, the
 *             files); "the frames differ" (the picture moved between
 *             them): the pair taken again once, then the black frame alone
 *             (ico_app_icon_deliver_single)
 *   curtain   the curtain off ICO_TITLE_ICON_HOLD ticks after the pass
 *             went off (the frames recorded in the pass may still be
 *             presented), then done for the run
 * A shot that comes back empty (the present dropped it: a later frame was
 * presented first, the window was minimised) or not at all within
 * ICO_TITLE_ICON_TIMEOUT ticks is armed again, at most
 * ICO_TITLE_ICON_RETRIES times in the run; then the capture gives up for
 * the run (one log line).  The menu left during the pass (New Game,
 * Continue, a Settings page) ends it at once: the pass off, the curtain off
 * after the hold, and the next time the menu is up the count starts again.
 *
 * Only the window build captures (ICO_RD, the renderer and the icon files);
 * on Android the launcher icon is the package's, so nothing is captured
 * there either.  The steps reach the renderer, the curtain and the files
 * through IcoTitleIconOps, which the test replaces.
 */
#ifndef PORT_GAME_TITLE_ICON_H
#define PORT_GAME_TITLE_ICON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ICO_TITLE_ICON_WAIT 30    /* ticks of the title's menu before the capture */
#define ICO_TITLE_ICON_SETTLE 3   /* ticks of a pass colour in force before its shot */
#define ICO_TITLE_ICON_HOLD 2     /* ticks of curtain after the pass went off */
#define ICO_TITLE_ICON_TIMEOUT 60 /* ticks without an answer: a dropped shot */
#define ICO_TITLE_ICON_RETRIES 3  /* shots armed again in a run before giving up */

typedef struct IcoTitleIconOps {
    int (*wanted)(void); /* ico_app_icon_wanted */
    /* arms a shot of the open frame (rd_capture_shot): 1, or 0 (no renderer) */
    int (*arm)(void);
    /* the answer: 1 with *rgba (NULL: dropped; the caller frees it), 0 while none */
    int (*take)(uint8_t **rgba, uint32_t *w, uint32_t *h);
    void (*cancel)(void);                                             /* an armed shot forgotten */
    void (*pass)(const uint8_t *clearRgba);                           /* rd_set_icon_pass */
    void (*curtain)(int on);                                          /* ui_host_curtain */
    int (*deliver)(const uint8_t *a, const uint8_t *b, int w, int h); /* app_icon.h */
    int (*deliver_single)(const uint8_t *a, int w, int h);
} IcoTitleIconOps;

/* The steps' states (ico_title_icon_state) */
enum {
    ICO_TITLE_ICON_IDLE = 0,
    ICO_TITLE_ICON_SETTLE_A,
    ICO_TITLE_ICON_SHOT_A,
    ICO_TITLE_ICON_SETTLE_B,
    ICO_TITLE_ICON_SHOT_B,
    ICO_TITLE_ICON_CURTAIN,
    ICO_TITLE_ICON_DONE
};

/* One Main tick (ico_title_logo_update): onMenu nonzero when the title's
   stage is up with its Continue/New Game menu, the logo shown and the model
   viewer off. */
void ico_title_icon_tick(int onMenu);

/* Whether the pass is on: title_logo.c's skip rule leaves out every object
   but "I" */
int ico_title_icon_pass_on(void);

/* The test's: the ops the steps use (NULL: the build's own, none outside
   the window build) and the state back to idle (the pass and the curtain
   off, a kept picture freed) */
void ico_title_icon_set_ops(const IcoTitleIconOps *ops);
void ico_title_icon_reset(void);
int ico_title_icon_state(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GAME_TITLE_ICON_H */
