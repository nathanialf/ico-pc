/*
 * port/null/debug_null.c
 *
 * What the headless build needs from common/src/debug.c and
 * common/src/debug_exception.c, which stay renderer-owned (their font,
 * profiler bar, exception screen and debug menu are raw GS packet code with
 * VU0 assembly; renderer wave 6 converts them, and the plan's developer mode
 * restores the menu).  docs/port/HEADLESS_STUBS.md lists each symbol.
 *
 * Two kinds of thing are here:
 *   - debug.c's option variables that simulation code reads (chain and hair
 *     tuning, enemy battle type, girl detour, zoom percentage, ...), with
 *     debug.c's initialisers, and debug_VariableInit, which main() calls at
 *     boot and which gives several of them their retail values (zoom 100,
 *     chain speeds 4, enemy battle type 3, ...).  Its assignments are copied
 *     in debug.c's order for every variable defined here; the ones it sets
 *     that only debug.c reads are left out.
 *   - debug.c's drawing, profiling, host-file and selector entry points, as
 *     no-ops that return what the retail build returns when nothing is shown
 *     (the retail build ran with the debug menu compiled out).
 * Plus libgcc's fptodp, whose only callers pass it to debug printfs.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "diag_host.h"
#include <libgraph.h>
#include <sifdev.h>

/* --- debug.c's option variables (initialisers as debug.c's) --------------- */

int LoadFileType = 1;

int debug_act_sub_thread = 0;

int debug_actnode_flag = 0;

int debug_ambient_volume = 0;

int debug_bar_flag; /* debug.c only declares it (extern) */

int debug_bounding_flag = 0;

int debug_brain_bar_flag = 0;

int debug_brain_flag = 0;

int debug_camera_flag = 0;

int debug_chain_cycle_speed = 0;

int debug_chain_slow_speed = 0;

int debug_cloth_info = 0;

int debug_col_old_proc = 0;

int debug_disp_escort_ball = 0;

int debug_disp_mesh = 0;

int debug_disp_particle = 0;

int debug_enemy_battle_type = 0;

int debug_enemy_fly_with_girl = 0;

int debug_enemy_kidnap_timer = 0;

int debug_face_rot_w_ratio = 0;

int debug_fieldcollision_flag = 0;

int debug_fly_limit_test = 0;

int debug_font_flag = 0;

int debug_font_flag2 = 0;

int debug_font_flag3 = 0;

int debug_fullscreen_effect = 0;

int debug_girl_detour_flag = 0;

int debug_hair_bend_angle = 0;

int debug_hair_collision = 0;

int debug_hair_gravity_level = 0;

int debug_hair_tight_level = 0;

int debug_hand_camera = 0;

int debug_ignore_demo_camera = 0;

int debug_ignore_dodge = 0;

int debug_lwskyomi_lookonly = 0;

int debug_mem_partition_flag = 0;

int debug_memory_bar = 0;

int debug_mot_debug_target = 0;

int debug_mot_slope_interp = 0;

int debug_motion_interporate = 0;

int debug_no_breast_hang = 0;

int debug_now_motion_viewer = 0;

int debug_one_hit_only = 0;

int debug_seslotdisp_flag = 0;

int debug_shadow_flag = 0;

int debug_skel_flag = 0;

int debug_snapshot_num = 0;

int debug_snapshot_reserve = 0;

int debug_specular_flag = 0;

int debug_stick_input = 0;

int debug_stick_simulate = 0;

int debug_use_new_queen_battle = 0;

int debug_wallcheck_flag = 0;

int debug_wallhitcoldisp = 0;

int debug_wayline = 0;

int debug_window_flag = 0;

int debug_wire_string = 0;

int debug_zoom_per = 0;

/* the texture statistics Texture.c counts and debug.c's bar prints */
int texregs = 0;

int textures = 0;

int texturetranssize = 0;

/* the game's (common/src/main.c, fumi/src/fieldCollision.c) */
extern int game_pause;
int ChangeFieldCollisionDebugMode(int drawRay);

/* debug.c:953, the assignments to the variables above, in its order.  It
   ends with ChangeGirlControlMode(debug_girl_pad_control), which does
   nothing for the 0 it has just stored (debug.c:774). */
void debug_VariableInit(void)
{
    debug_window_flag = 0;
    debug_ignore_demo_camera = 0;
    debug_bar_flag = 0;
    debug_memory_bar = 0;
    debug_font_flag = 0;
    debug_font_flag2 = 0;
    debug_font_flag3 = 0;
    debug_skel_flag = 0;
    debug_seslotdisp_flag = 0;
    debug_wallcheck_flag = 0;
    debug_wallhitcoldisp = 0;
    debug_fieldcollision_flag = 0;
    debug_actnode_flag = 0;
    debug_motion_interporate = 1;
    debug_wayline = 1;
    debug_mem_partition_flag = 0;
    debug_camera_flag = 0;
    debug_brain_bar_flag = 0;
    debug_brain_flag = 1;
    debug_bounding_flag = 0;
    debug_wire_string = 0;
    debug_now_motion_viewer = 0;
    debug_mot_debug_target = 0;
    debug_shadow_flag = 0;
    debug_specular_flag = 1;
    debug_zoom_per = 100;
    debug_snapshot_num = 100;
    debug_ambient_volume = 0;
    debug_cloth_info = 0;
    debug_hair_tight_level = 20;
    debug_hair_gravity_level = 0;
    debug_hair_bend_angle = 256;
    debug_hair_collision = 0;
    debug_face_rot_w_ratio = 0;
    debug_fullscreen_effect = 1;
    debug_disp_particle = 1;
    debug_disp_mesh = 1;
    debug_stick_input = 0;
    debug_enemy_battle_type = 3;
    debug_stick_simulate = 1;
    debug_act_sub_thread = 0;
    debug_use_new_queen_battle = 1;
    debug_chain_cycle_speed = 4;
    debug_chain_slow_speed = 4;
    debug_mot_slope_interp = 5;
    debug_enemy_fly_with_girl = 0;
    debug_disp_escort_ball = 0;
    debug_girl_detour_flag = 1;
    debug_col_old_proc = 0;
    debug_fly_limit_test = 0;
    debug_lwskyomi_lookonly = 0;
    debug_one_hit_only = 0;
    debug_ignore_dodge = 0;
    debug_hand_camera = 1;
    debug_enemy_kidnap_timer = 1;
    debug_no_breast_hang = 0;
    debug_snapshot_reserve = 0;
    game_pause = 0;
    ChangeFieldCollisionDebugMode(0);
}

/* --- entry points ----------------------------------------------------------- */

/* debug.c:1036: clears the font window and the profiler counters, starts
   the EE timers and grabs the back image; nothing the simulation reads. */
void debug_Init(void) {}

/* debug.c:944: hooks debug_CallbackGsFinish (the profiler) to the GIF DMA
   interrupt; no DMA interrupts on the host. */
void debug_SetDmaCallback(void) {}

/* debug.c:847: no start-stage file in retail (-1).  PC port (renderer wave
   5, R5b): the developer key [dev] start_stage (docs/port/CONFIG.md), which
   host_config.c hands over as ICO_START_STAGE, takes the place of the
   development build's start-stage file: Main (common/src/main.c:147) clamps
   the value to 1..105 and switches to that stage instead of stage 1 (boot,
   language and title).  Unset or not a number: -1, the retail path. */
int debug_TryToGetStartStage(void)
{
    const char *v = getenv("ICO_START_STAGE");
    char *end;
    long n;

    if (v == NULL || v[0] == '\0') {
        return -1;
    }
    n = strtol(v, &end, 10);
    if (*end != '\0' || n <= 0 || n > 105) {
        return -1;
    }
    return (int)n;
}

void debug_openLog(void) {}

void debug_closeLog(void) {}

/* the EE timer profiler (debug.c:1051, :1057, :1888) */
void debug_BeginTimer(int mode)
{
    (void)mode;
}

float debug_GetTimerSec(void)
{
    return -1.0f; /* as debug.c's ICO_HOST branch */
}

void debug_ResetBar(void) {}

/* the debug font, the font window and the SE slot display: drawing only
   (debug.c:2156, :2172, :3120, the font flush GsBase.c calls each frame,
   and the quadword dump) */
void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    (void)a;
    (void)b;
    (void)c;
    (void)fmt;
}

void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...)
{
    (void)x;
    (void)y;
    (void)col;
    (void)fmt;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_PrintFontWindow(int col, const char *fmt, ...)
{
    (void)col;
    (void)fmt;
}

void debug_FlushFont(void) {}

void debug_SESlotDisp(void) {}

void debug_DispQW(void *p, int size)
{
    (void)p;
    (void)size;
}

/* the load-time statistics (debug.c:3195, :3202) */
void debugCdvdLoadInfoSegInit(int page)
{
    (void)page;
}

void debugCdvdLoadInfoSegAdd(int page, int idx, int delta)
{
    (void)page;
    (void)idx;
    (void)delta;
}

/* debug.c:3772: host0-style file open on the disc root; the host's sceOpen
   (port/data/sif_host.c) has no files, as retail found none. */
static int sceFd = -1;

int debugSceOpen(const char *name, int mode)
{
    char path[256];

    snprintf(path, sizeof path, "%s%s;1", "cdrom0:\\", name);
    return sceFd = sceOpen((unsigned char *)path, mode);
}

int debugSceClose(int fd)
{
    if (fd == sceFd) {
        sceFd = -1;
    }
    return sceClose((unsigned int)fd);
}

/* debug.c:2364: kanbanBoot.c resets the GS after the 50/60 Hz choice. */
void gsb_Init(void *db);
extern sceGsDBuff db; /* GsBase.c's double buffer (common/src/main.c) */

int gsResetFunc(int val)
{
    (void)val;
    gsb_Init(&db);
    return 1;
}

/* The debug tools' list selectors (debug.c:2439, :2575): 0 keeps the window
   open, 1 selects, -1 cancels.  Only the debug menu's tools call them; a
   cancel closes the tool. */
int debug_SelectCsvWindow(char *title, int x, int y, int rows, const void *base, int stride,
                          int off, int deref, int n, int *psel)
{
    (void)title;
    (void)x;
    (void)y;
    (void)rows;
    (void)base;
    (void)stride;
    (void)off;
    (void)deref;
    (void)n;
    (void)psel;
    return -1;
}

int debug_SelectCsvWindowVal(char *title, int x, int y, int rows, int count, int *psel,
                             int (*fn)(int, int), int arg)
{
    (void)title;
    (void)x;
    (void)y;
    (void)rows;
    (void)count;
    (void)psel;
    (void)fn;
    (void)arg;
    return -1;
}

/* --- failures ---------------------------------------------------------------- */

/* debug_exception.c:693, :699: the exception screen, then a hang.  The host
   reports and stops instead, so a test run ends with the location rather
   than spinning forever. */
void debug_assertMessage(const char *file, int line, const char *mes)
{
    fprintf(stderr, "debug_assertMessage: %s:%d: %s\n", file, line, mes != NULL ? mes : "");
    fflush(stderr);
    ico_diag_set_failure("debug_assertMessage: %s:%d: %s", file, line, mes != NULL ? mes : "");
    abort();
}

void debug_assert(const char *file, int line)
{
    fprintf(stderr, "debug_assert: %s:%d\n", file, line);
    fflush(stderr);
    ico_diag_set_failure("debug_assert: %s:%d", file, line);
    abort();
}

/* debug.c:780: formats the message and fails as debug_assertMessage. */
void debug_Assert(char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    debug_assertMessage("src/debug.c", 1392, buf);
}

/* --- libgcc ------------------------------------------------------------------ */

/* sce/libgcc/fp-bit.c:622: a float widened to a soft-float double, which
   the EE build passes to %f.  Its callers (boyact.c, girl_act.c) hand the
   result only to debug printfs, which print nothing. */
int fptodp(float v)
{
    (void)v;
    return 0;
}
