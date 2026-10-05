/*
 * ico2/ito/include/itou_boss.h
 *
 * The declarations of what itou_boss.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ITOU_BOSS_H
#define ITOU_BOSS_H

#include "typedef.h"

/* the functions itou_boss.c defines `inline` */
int InqCapsuleGhostBossStage(void);
void actBossCtrlStart(void *gobj);
ICO_WORD InitBossCtrlGeo(void *gobj);
void CapsuleGhostBossStart(void);
int InqCapsuleGhostBossEnd(void);
void BossCtrlGeo(void *self);
void itou_boss_gflag_init(void);

#endif /* ITOU_BOSS_H */
