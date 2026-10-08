/* settings_fixture.h: what settings_test.c and settings_extra_test.c share (the
 * stubs for the game's imports, the fake tables, frame/settle/press, the
 * config helpers, enterMain/openPage).  The fixture is compiled once per
 * test executable, with SETTINGS_RENDER for settings_render. */
#ifndef SETTINGS_FIXTURE_H
#define SETTINGS_FIXTURE_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "typedef.h"
#include "main.h"
#include "layout_texture.h"
#include "charFileManager.h"
#include "StageManager.h"
#include <libscf.h>
#include "achievements.h"
#include "config.h"
#include "font.h"
#include "gallery.h"
#include "host_config.h"
#include "ico_credits.h"
#include "ico_gamestate.h"
#include "input.h"
#include "layout_ext.h"
#include "appearance.h"
#include "audio_host.h"
#include "menu_font.h"
#include "menu_text.h"
#include "mix_gain.h"
#include "options.h"
#include "photo_mode.h"
#include "photo_ui.h"
#include "settings.h"
#include "strings.h"
#include "sysconf.h"
#include "ui_hint.h"
#include "ui_list.h"
#include "video_options.h"

#ifdef SETTINGS_RENDER

#include "DisplayList.h"
#include "GifPacket.h"
#include "GifHost.h"
#include "rd_internal.h"
#include "ui_internal.h"

#endif

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the stubs' data, read and set by the tests */
extern int ScreenWidth, ScreenHeight;
extern float center_X, center_Y;
extern int gFlagGameClear;
extern int stage_no;
extern int frame_count;
extern int layout_boot_flag;
extern int title_demo_mode;
extern unsigned int stage_after_skipping_demo;
extern int mpegPlayReturnStage;
extern int optionScreenMode, optionControlType, girlControlMode;
extern int iosPadActRequestEnable;
extern int s_resets, s_sounds[3], s_leaves;
extern int s_newGames;
extern int s_outputMode, s_outputSets;
extern int s_devCount;
extern const char *s_devNames[3];
extern char s_reopened[ICO_AUDIO_DEVICE_NAME_MAX];
extern int s_reopens;
extern int s_filmCalls, s_filmLast;
extern int failures;
void setEnv(const char *k, const char *v);
void setRow(int i, int up, int down, int downItem, int upItem, int left, int right, int y);
void setLayout(int i, int first, int last, int def, int link);
void fakeTables(void);
void frame(int flags);
int settle(int layout, int max);
void press(int flags);
extern char s_dir[1024];
void path(char *out, size_t n, const char *name);
void writeFile(const char *p, const char *text);
void useConfig(const char *text);
int labelsAre(UiSettingsPage page, const int *opts, const int *strs, int n);
void pauseToMain(void);
void la_host_film_effect(int mode);
int enterMain(int title);
int enterMainKeep(int title);
int openPage(int mainL, int mainRow, UiSettingsPage page);
#ifdef SETTINGS_RENDER
extern int s_reduce;
/* defined by settings_test.c (the render variant's fake sheets) */
void bindFakeSheet(int no);
#endif

#endif
