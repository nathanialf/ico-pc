/*
 * port/game/title_logo.h
 *
 * The title's logo under the port's menus.  The logo (the
 * models I, C and O, their glows I_f, C_f and O_f and their shadows I_sd,
 * C_sd and O_sd, stage animations of the title's stage, stage 1) stays on
 * the stage behind the layouts; while a Settings or Extras page opened from
 * the title is up (settings.h ui_settings_covers_title, and the model list
 * the Extras page opens: model_viewer.h ico_mv_title_list_layout) it is not
 * drawn.  Its animation runs on (op.c's actTitleCamera2 waits on
 * opTitleLogoMode), so back on the title it shows settled, with no intro.
 *
 * Once hidden it stays hidden until the title's own menu (layout 12 or 13)
 * is back or the stage changes, so Extras > Credits and a model's stage
 * load do not show it again while the title fades out.  The pause menu's
 * Settings (any other stage) never hides anything.
 *
 * The title's full-screen models: title_back, the plane that darkens the
 * picture behind the logo as it brightens, is built for the 4:3 screen.
 * Drawn like the rest of the scene on a wide picture it would cover only a
 * centred part of it, so RegistPacket.c widens it to the native frame (its
 * node matrices' x row by the frame width / 512: it is a camera-locked
 * node, whose draw never reads the node scale), as it does for a
 * full-frame item.
 */
#ifndef PORT_GAME_TITLE_LOGO_H
#define PORT_GAME_TITLE_LOGO_H

#ifdef __cplusplus
extern "C" {
#endif

/* common/src/main.c, once a Main tick after the layouts ran (ExecIcoMisc)
   and before the objects are drawn: the hidden state for this tick, then
   the icon capture's step (title_icon.h ico_title_icon_tick). */
void ico_title_logo_update(void);

/* seki/src/RegistPacket.c reg_DispObj, for every object it draws: nonzero
   when the object's model (its name) is the logo's and the logo is hidden
   (as the tick's ico_title_logo_update left it).  While the application
   icon's capture pass is on (title_icon.h), nonzero for every model but
   "I": the letter is drawn alone. */
int ico_title_logo_skip(const char *model);

/* seki/src/RegistPacket.c reg_DispObj, for every object it draws: nonzero
   when the object's model (its name) is one of the title stage's
   full-frame models: built to cover the 4:3 screen, they are widened by the
   game to the native frame (RegistPacket.c reg_DispObj scales their node
   matrices' x row by gsFrame.w / 512, nothing at 4:3) instead of the
   renderer detecting them. */
int ico_title_fullframe_model(const char *model);

/* The parts, for the test: whether `model` names one of the logo's nine
   models, and one step of the hidden state (*hidden) for the stage, the
   current layout and whether a menu covers the title; returns *hidden. */
int ico_title_logo_model(const char *model);
int ico_title_logo_step(int *hidden, int stage, int layout, int covered);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GAME_TITLE_LOGO_H */
