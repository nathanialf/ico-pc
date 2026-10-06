/*
 * ico2/omori/include/fightSound.h
 *
 * The declarations of what fightSound.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef FIGHTSOUND_H
#define FIGHTSOUND_H

struct SqEntry;

void fightSoundClose(void);
struct SqEntry *fightSoundPlayChk(void);
void fightSoundProcessRequestPause(void);
void fightSoundProcessRequestStart(void);
void fightSoundProcess(void);
int fightSoundProcessRequestStatus(void);
/* PC port (MUS3): set while the music gallery is open (fightSound.c) */
extern int fightSoundHostHold;

#endif /* FIGHTSOUND_H */
