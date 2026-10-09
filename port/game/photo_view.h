/*
 * port/game/photo_view.h
 *
 * Photo mode's camera in the game's own matrices (issue 14): while photo
 * mode is on (port/game/photo_mode.h) and the game is paused, the game
 * draws every tick from the photo camera, so culling, shadows, reflections,
 * the sun, the flare and every full-screen pass follow it as they follow
 * the game camera in play.
 *
 * ico_photo_view_tick runs once a Main tick (ico2/common/src/main.c),
 * after the objects' update and before the draw, where the camera's update
 * sets the camera in play:
 *   enter  photo mode on, the game paused, nothing saved: the game camera
 *          saved (GsBase.c gsb_PushView) and kept as the photo camera's
 *          base (ico_photo_set_game: its view +0x80, its screen matrix
 *          +0xC0, its distance gsb_ViewFocus); the stage number noted
 *   tick   the photo camera's view into +0x80, the projection rebuilt at
 *          the saved distance times the zoom (gsb_SetVSMatrix at the full
 *          screen) and the common matrices with the renderer's camera
 *          (gsb_MakeCommonMatrix); the camera stays in the matrices between
 *          ticks, where the sun and the flare read it
 *   leave  photo mode off: the game camera back (gsb_PopView,
 *          gsb_MakeCommonMatrix for the renderer, gsb_PopView again so the
 *          eleven matrices are the game's byte for byte) and a camera cut
 *          for the presenter; when the stage changed or the game runs again
 *          the save is dropped instead (the game has set its own camera by
 *          then)
 * With photo mode off it does nothing.  The headless build runs it too.
 */
#ifndef ICO_PORT_GAME_PHOTO_VIEW_H
#define ICO_PORT_GAME_PHOTO_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

void ico_photo_view_tick(void);
/* 1 while the game camera is saved (between enter and leave) */
int ico_photo_view_saved(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_PHOTO_VIEW_H */
