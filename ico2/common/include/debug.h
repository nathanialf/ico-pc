/*
 * ico2/common/include/debug.h
 *
 * The declarations of what debug.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef DEBUG_H
#define DEBUG_H

/* debug.o's globals: debugBackGroundDisableFlag, charNumH, LoadFileType,
   the display-list counters and the debug option words; the eleven after
   debug_fly_limit_test are named from their option captions (derived names).
   used_dl_memory is .data, the rest .sdata. */
/* one debug-menu entry: the label the selector prints, the handler, and a
   "stay in the menu" flag */
typedef struct { /* field names derived */
    char *label;
    int (*fn)(int);
    int stay;
} DbgMenuItem; /* derived name */

/* the debug menu debug_Menu runs (.data) */
extern DbgMenuItem debugMenu[];
extern int debugBackGroundDisableFlag;
extern int charNumH;
extern int LoadFileType;
extern int polygons;
extern int strips;
extern int packets;
extern int textures;
extern int texregs;
extern int texturetranssize;
extern int used_dl_max;
extern int used_dl_num;
extern int used_dl_memory[];
/* debug_bar_flag is not declared here: motionOrientManager.c declares it as
   its display-mode enum, and debug.c declares it itself. */
extern int debug_ignore_demo_camera;
extern int debug_brain_bar_flag;
extern int debug_font_flag;
extern int debug_font_flag2;
extern int debug_font_flag3;
extern int debug_skel_flag;
extern int debug_seslotdisp_flag;
extern int debug_wallcheck_flag;
extern int debug_wallhitcoldisp;
extern int debug_actnode_flag;
extern int debug_printf_flag;
extern int debug_window_flag;
extern int debug_fieldcollision_flag;
extern int debug_wayline;
extern int debug_motion_interporate;
extern int debug_frame;
extern int debug_mem_partition_flag;
extern int debug_camera_flag;
extern int debug_chara_target;
extern int debug_brain_flag;
extern int debug_bounding_flag;
extern int debug_wire_string;
extern int debug_gbrain_info_flag;
extern int debug_scissor;
extern int debug_mot_debug_target;
extern int debug_now_motion_viewer;
extern int debug_debug_bar_multiply;
extern int debug_debug_bar_start_item;
extern int debug_memory_bar;
extern int debug_shadow_flag;
extern int debug_specular_flag;
extern int debug_zoom_per;
extern int debug_ripple_roughness;
extern int debug_snapshot_num;
extern int debug_snapshot_size;
extern int debug_snapshot_format;
extern int debug_snapshot_counter;
extern int debug_snapshot_reserve;
extern int debug_ambient_volume;
extern int debug_jimaku;
extern int debug_profile_type;
extern int debug_cloth_info;
extern int debug_face_chest_ratio;
extern int debug_face_rot_w_ratio;
extern int debug_def_smpmin;
extern int debug_stick_input;
extern int debug_enemy_battle_type;
extern int debug_fullscreen_effect;
extern int debug_disp_cluster;
extern int debug_disp_normal;
extern int debug_disp_lws;
extern int debug_disp_particle;
extern int debug_disp_mesh;
extern int debug_act_sub_thread;
extern int debug_stick_simulate;
extern int debug_use_new_queen_battle;
extern int debug_chain_cycle_speed;
extern int debug_chain_slow_speed;
extern int debug_mot_slope_interp;
extern int debug_enemy_fly_with_girl;
extern int debug_disp_enemy_state;
extern int debug_disp_escort_ball;
extern int debug_girl_detour_flag;
extern int debug_col_old_proc;
extern int debug_fly_limit_test;
extern int debug_lwskyomi_lookonly;
extern int debug_one_hit_only;
extern int debug_ignore_dodge;
extern int debug_hand_camera;
extern int debug_enemy_kidnap_timer;
extern int debug_hair_tight_level;
extern int debug_hair_gravity_level;
extern int debug_hair_bend_angle;
extern int debug_hair_collision;
extern int debug_no_breast_hang;
/* debug.c's `inline` functions, in the order of their out-of-line copies at
   the end of the object (first-declaration order).  Callers after a
   definition inline it; the out-of-line copies serve the rest of the game
   and the menu tables. */
void debug_BeginTimer(int mode);
float debug_GetTimerSec(void);
float debug_GetTimerCount(void);
inline void debug_ClearFontWindow(void);
void debug_ResizeFontWindowHeight(int val);
void debug_SetBar(char *name, unsigned int col, char *file, int line);
void debug_SetBar2(char *name, unsigned int col, char *file, int line);
void debug_ResetBar(void);
void debug_DispVu1IReg(int no);
void debug_DispVu1SReg(int no);
void debug_DispMatrix(int *m);
void debug_SetBarDummy(void);

int debug_SelectCsvWindowWithLine(char *title, int x, int y, int rows, const void *base, int stride,
                                  int off, int deref, int n, int *psel);

int debug_TryToGetStartStage(void);
inline int debugSceOpen(const char *name, int mode);
inline int debugSceClose(int fd);
int debugSceCloseFdNew(void);
void debug_closeLog(void);
void debugCdvdLoadInfoSegInit(int page);
void debugCdvdLoadInfoSegAdd(int page, int idx, int delta);
void debugCdvdLoadInfoSegCls(int page, int idx);
inline int gsResetFunc(int val);
inline void ChangeGirlControlMode(int mode);
inline int debug_CallbackGsFinish(int channel);
void debug_SaveStartStageFile(int stage);

int _debug_SelectCsvWindow(char *title, int x, int y, int rows, int base, int stride, int off,
                           int deref, int n, int *psel, void (*getline)(), int (*colfunc)(int));

int debug_SelectCsvWindowWithLineColor(char *title, int x, int y, int rows, const void *base,
                                       int stride, int off, int deref, int n, int *psel,
                                       int (*colfunc)(int));

struct McMgr;

int debug_mcFormat(struct McMgr *mc);
int debug_mcUnformat(struct McMgr *mc);
void *debug_saveNumFunc(int no, void *mc);
int debug_mcTest(void);
int debug_STAFFROLLTest(void);
int debug_SETest_color(int idx);
int debug_reverbTest(void);
int debug_AdpcmTest(int first);
int debugCdvdLoadInfoSegDisp(void);
int debug_GameOver(void);
int debug_EndingDemo(void);
int debug_BackStageTest(void);
int debug_tsuresariTimeZero(void);
int debug_hintStart(void);
int debug_SelectPad2ControlGobj(int reset);
void debug_Assert(char *fmt, ...);
void debug_DispQW(void *p, int size);
void debug_Init(void);
int debug_MemoryCard(void);
void debug_PrintFontWindow(int col, const char *fmt, ...);
void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...);
void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...);
int debug_SETest(int reset);

int debug_SelectCsvWindow(char *title, int x, int y, int rows, const void *base, int stride,
                          int off, int deref, int n, int *psel);

int debug_SelectStage(void);
void debug_Menu_off(void);
void debug_SetDmaCallback(void);
void debug_StdPrintfDummy(const char *fmt, ...);
void debug_openLog(void);
void debug_FlushFont(void);
void debug_VariableInit(void);
int debug_SnapShot(int idx);

int debug_SelectCsvWindowVal(char *title, int x, int y, int rows, int count, int *psel,
                             int (*fn)(int, int), int arg);

void debug_SESlotDisp(void);

#endif /* DEBUG_H */
