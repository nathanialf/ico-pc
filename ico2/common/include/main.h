/*
 * ico2/common/include/main.h
 *
 * The globals main.c defines, as it defines them.  The file name is
 * derived.
 */
#ifndef MAIN_H
#define MAIN_H

#include "typedef.h"
#include <libgraph.h>

extern int systemStatus[12];
extern sceGsDBuff db;
extern StageSetting GlobalStageSetting;
extern PadState pad[16];

/* the stage manager's message: switch (0) or switch with a fade (1), the
   stage, the fade-out and fade-in speeds and the fade colour */
typedef struct { /* field names derived */
    int cmd;
    int stage;
    char pad8[4];
    float fadeOut;
    float fadeIn;
    unsigned char r;
    unsigned char g;
    unsigned char b;
} StgMgrMsg; /* derived name */

extern StgMgrMsg stageMgrMsg;
extern struct IosMsgQueue SchedulerMsgQ;
/* .sdata; the ones marked derived carry derived names */
extern char NetLoadTARGET[];
extern int buffer_ID;
extern int odd_even;
extern int frame_count;
extern char *matrixptr;
extern int debugMoveMode;
extern int stage_no;
extern int before_stage_no;
extern int exit_no;
extern int motionFrameUpdate;
extern int interlace;
extern int thisIsYourStartStage;
extern int NonLinearCameraMove;
extern int GlobalTimer;
extern int lock_execIcoMisc;
extern int graphics_ready;
extern int game_pause;
extern int data_loading;
extern int screen_offset_x;
extern int screen_offset_y;
extern int fall_death_active;
extern int title_fading_out;
extern int collis_flg_stock;
extern int IosPadLock;
extern int IosCdLock;
extern int IosSndLock;
extern int IosStgMgrLock;
extern int systemFault;
extern int optionControlType; /* derived name */
extern int optionScreenMode;  /* derived name */
extern int girlControlMode;   /* derived name */
extern GObj *boyGObj;
extern GObj *girlGObj;
extern IosPadCtx *boyPad;
extern IosPadCtx *girlPad; /* derived name */
extern int gameover_flag;
extern int gameover_layout_flag;
extern int itemWatchOff; /* derived name */
extern GObj *CurrentTargetGObj;
extern GObj *CurrentTargetGObjSub;
extern int current_stage_no;
extern void (*system_stage_func)(void);
extern int InterStageSwitchLock;
void Main(void);
void Emergency_DestroyAllThread(void);

#endif /* MAIN_H */
