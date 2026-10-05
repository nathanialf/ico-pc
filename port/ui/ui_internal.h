/*
 * port/ui/ui_internal.h
 *
 * What port/ui's files share and the tests reach.
 */
#ifndef PORT_UI_INTERNAL_H
#define PORT_UI_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Called before every rd recording font.c makes.  The game build sets it to
   GifPacket.c's gif_HostFlush (ui_host.c): the register decoder emits what
   it still holds into the current list and forgets the state it emitted,
   so its next primitive re-sends PRIM, TEX0 and the rest after the text's
   own rd_Texture and rd_ABE (the pattern DisplayFont.c's host path uses). */
void ui__SetRecordHook(void (*fn)(void));
/* runs the record hook now (a caller about to switch rd lists flushes the
   decoder into the list it was recording first) */
void ui__RunRecordHook(void);
/* while > 0, font.c does not run the record hook (the caller ran it) */
void ui__SuppressRecordHook(int delta);
/* Called before text is laid out for the game: the game build syncs the GS
   frame (ScreenWidth, center_X...), the language and the scale from the game
   and rd (ui_host.c). */
void ui__SetSyncHook(void (*fn)(void));
void ui__Sync(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_INTERNAL_H */
