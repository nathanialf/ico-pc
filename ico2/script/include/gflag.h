/*
 * ico2/script/include/gflag.h
 *
 * The declarations of what gflag.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GFLAG_H
#define GFLAG_H

int gflagChk(int bit_idx);
void gflagInit(void);
void gflagOff(int bit_idx);
void gflagOn(int bit_idx);

/* gflag.o's .sdata: the game-clear state and the save's stage */
extern int gFlagGameClear;

extern int gFlagSaveStage;

struct GamesysMemCursor;

void gflagSave(struct GamesysMemCursor *fp);
void gflagLoad(struct GamesysMemCursor *fp);

#endif /* GFLAG_H */
