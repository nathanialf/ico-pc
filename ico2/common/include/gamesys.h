/*
 * ico2/common/include/gamesys.h
 *
 * The declarations of what gamesys.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GAMESYS_H
#define GAMESYS_H

#include "backStage.h"
#include "typedef.h"

/* the save image a load or save handler of gameSysMemoryFuncList reads or
   writes through: the image and the offset the next record goes at */
typedef struct GamesysMemCursor { /* field names derived */
    char *base;
    int offset;
} GamesysMemCursor; /* derived name */

/* gamesys.c's .data globals */
extern char stamp_str[];
extern void *gameSysMemoryFuncList[];
extern int gamesysStageExitTime[];
extern GamesysObjInfo gameSysObjInfo[];
extern char gameSysMainSaveBuff[25596]; /* sized: the port copies it (credits_live.c) */
extern int gamesysTimeCount;
extern int gamesysAnotherStageTsuresari;
extern int gamesysVersionDiff;
extern int gamesysObjBuffOver;
/* the object-kind table (obj-kind-data) and the stage layout objects
   (obj-layout), data members linked with debug.o and gamesys.o */
extern ObjKindEnt objKindData[];
extern GenGeo objLayout[];
int gamesysGetGirlStageIDAndPosition(float *pos);
void gamesysMemoryHandlerRead(GamesysMemCursor *self, void *dst, int size);
void gamesysMemoryHandlerWrite(GamesysMemCursor *self, void *src, int size);
void gamesysMemoryLoad(void **tbl, void *mem, void *arg);
void gamesysMemorySave(void **tbl, void *mem, void *arg);
GamesysObjInfo *gamesysObjInfoBaseSet(GObj *self, int stage);
void gamesysObjInfoCls(int kind, int no);
GamesysObjInfo *gamesysObjInfoGet(int kind, int no);
void gamesysObjInfoInit(void);
GamesysObjInfo *gamesysObjInfoPosNewStageSet(int no, int kind, int stage, float *pos, float *rot);
GamesysObjInfo *gamesysObjInfoPosSetStage(GObj *self, int infoPos, int work1, int stage);
void gamesysObjInfoStageInitFlagCls(void);
GamesysObjInfo *gamesysObjInfoUniqDataSet(GObj *self);
void gamesysStageExitTimeSet(int stage);
void gamesysBackStageProcess(void);
void gamesysNObjInfoInit(void);
void gamesysObjInfoStageInitPosSaveUnlock(void);
int gamesysGirlStageGet(void);

#endif /* GAMESYS_H */
