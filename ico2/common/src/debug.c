#include "debug.h"
#include "cdvd.h"
#include "StageManager.h"
#include "backStage.h"
#include "kanban.h"
#include "gobj.h"
#include "s_init.h"
#include "act-game.h"
#include "commonact.h"
#include "fieldCollision.h"
#include "memory.h"
#include "brain.h"
#include "camera-root.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "GsBase.h"
#include <string.h>
#include <stdio.h>
#include <eekernel.h>
#include <sound.h>
#include <sifdev.h>
#include <libvu0.h>
#include "gamesys.h"
#include "typedef.h"
#include "DisplayList.h"
#include "layout_action.h"
#include "main.h"
#include "adpcm_init.h"
#include "script.h"
#include <eeregs.h>
#include "Matrix.h"
#include "Basic.h"
#include "geometryManager.h"
#include "Primitive.h"
#include "debug_menu.h"
#include "way_tool.h"
#include "camera-editor.h"
#include <stdlib.h>
#include "matrixDrive.h"
#include "pad.h"
#include "ios.h"
#include "staffroll.h"
#include "DmaPacket.h"
#include "Texture.h"
#include "debug_exception.h"
#include <assert.h>

static int debug_CollisionTest(int reset);
static int debug_DispBall(int on);
static int debug_DispBox(int on);
static void debug_DrawBar(void);
static void debug_MakeFont(void);
static int debug_Mode(void);
static void debug_PrintCharacter(char *str, int x, int y, int r, int g, int b, int sz);
static void debug_PrintFont(int x, int y, int col, char *str);
static int debug_selectFile(McMgr *mc);
static void debug_makeBackImage(void);

static int debug_girl_pad_control;

typedef struct { /* field names derived */
    int x, y;
    unsigned int w, h;
} FR; /* derived name */

/* the debug-option table: 76 records of 0x1C bytes */
typedef struct { /* field names derived */
    /* 0x00 */ char *name;
    /* 0x04 */ unsigned int col;
    /* 0x08 */ int *val;
    /* 0x0C */ int min;
    /* 0x10 */ int max;
    /* 0x14 */ char **strs;
    /* 0x18 */ void (*func)(int);
} DbgOpt; /* derived name */

/* the collision ray display option, the one debug option word that is not
   a global (ChangeFieldCollisionDebugMode reads it through the table) */
static int debug_col_ray_disp = 0; /* derived name */

/* debug.h leaves it out (see there) */
extern int debug_bar_flag;

/* clang-format off */
/* the 8x8 1bpp debug font, eight bytes per glyph, 256 glyphs */
static const unsigned char fontBitmap[256 * 8] = { /* derived name */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00,
    0x36, 0x24, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x14, 0x7f, 0x14, 0x7f, 0x14, 0x14, 0x00,
    0x08, 0x7e, 0x09, 0x3e, 0x48, 0x3f, 0x08, 0x00,
    0x42, 0x25, 0x12, 0x08, 0x24, 0x52, 0x21, 0x00,
    0x3e, 0x03, 0x03, 0x0e, 0xe3, 0x63, 0x7e, 0x00,
    0x18, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x38, 0x0c, 0x06, 0x06, 0x06, 0x0c, 0x38, 0x00,
    0x0e, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0e, 0x00,
    0x49, 0x2a, 0x1c, 0x08, 0x1c, 0x2a, 0x49, 0x00,
    0x08, 0x08, 0x08, 0x7f, 0x08, 0x08, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x10, 0x08,
    0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x00,
    0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00,
    0x7f, 0x63, 0x73, 0x6b, 0x67, 0x63, 0x7f, 0x00,
    0x1c, 0x1c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00,
    0x7f, 0x63, 0x60, 0x7f, 0x07, 0x07, 0x7f, 0x00,
    0x7f, 0x60, 0x60, 0x7e, 0x70, 0x70, 0x7f, 0x00,
    0x30, 0x33, 0x33, 0x33, 0x7f, 0x38, 0x38, 0x00,
    0x7f, 0x03, 0x03, 0x7f, 0x70, 0x70, 0x7f, 0x00,
    0x7f, 0x03, 0x03, 0x7f, 0x67, 0x67, 0x7f, 0x00,
    0x7f, 0x63, 0x70, 0x38, 0x1c, 0x1c, 0x1c, 0x00,
    0x3f, 0x33, 0x33, 0x7f, 0x73, 0x73, 0x7f, 0x00,
    0x7f, 0x63, 0x63, 0x7f, 0x70, 0x70, 0x7f, 0x00,
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00,
    0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x10, 0x08,
    0x60, 0x18, 0x06, 0x03, 0x06, 0x18, 0x60, 0x00,
    0x00, 0x00, 0x7f, 0x00, 0x7f, 0x00, 0x00, 0x00,
    0x03, 0x0c, 0x30, 0x40, 0x30, 0x0c, 0x03, 0x00,
    0x3e, 0x63, 0x63, 0x30, 0x1c, 0x00, 0x0c, 0x00,
    0x3e, 0x41, 0x59, 0x55, 0x3d, 0x01, 0x7e, 0x00,
    0x3e, 0x67, 0x63, 0x63, 0x7f, 0x63, 0x63, 0x00,
    0x3f, 0x63, 0x63, 0x3f, 0x63, 0x63, 0x3f, 0x00,
    0x7e, 0x07, 0x07, 0x07, 0x07, 0x07, 0x7e, 0x00,
    0x1f, 0x33, 0x63, 0x67, 0x67, 0x37, 0x1f, 0x00,
    0x7e, 0x07, 0x07, 0x3f, 0x07, 0x07, 0x7e, 0x00,
    0x7e, 0x07, 0x07, 0x3f, 0x07, 0x07, 0x07, 0x00,
    0x7e, 0x07, 0x03, 0x7b, 0x63, 0x67, 0x7e, 0x00,
    0x63, 0x63, 0x63, 0x7f, 0x63, 0x63, 0x63, 0x00,
    0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00,
    0x7c, 0x30, 0x30, 0x30, 0x30, 0x38, 0x1f, 0x00,
    0x47, 0x67, 0x37, 0x1f, 0x3f, 0x77, 0x67, 0x00,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x7e, 0x00,
    0x22, 0x77, 0x7f, 0x6b, 0x6b, 0x63, 0x63, 0x00,
    0x63, 0x67, 0x6f, 0x7d, 0x79, 0x71, 0x61, 0x00,
    0x3e, 0x67, 0x67, 0x67, 0x67, 0x67, 0x3e, 0x00,
    0x3f, 0x67, 0x67, 0x67, 0x3f, 0x07, 0x07, 0x00,
    0x1e, 0x33, 0x33, 0x3b, 0x37, 0x73, 0x1e, 0x00,
    0x3f, 0x63, 0x63, 0x3f, 0x33, 0x63, 0x63, 0x00,
    0x3e, 0x67, 0x07, 0x3e, 0x70, 0x73, 0x3e, 0x00,
    0x7f, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x00,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3e, 0x00,
    0x63, 0x63, 0x36, 0x36, 0x1c, 0x1c, 0x08, 0x00,
    0x63, 0x63, 0x63, 0x6b, 0x6b, 0x7f, 0x36, 0x00,
    0x63, 0x37, 0x1e, 0x1c, 0x3c, 0x76, 0x63, 0x00,
    0x43, 0x47, 0x2e, 0x1c, 0x18, 0x18, 0x18, 0x00,
    0x7f, 0x71, 0x38, 0x1c, 0x0e, 0x47, 0x7f, 0x00,
    0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 0x00,
    0x22, 0x14, 0x08, 0x3e, 0x08, 0x3e, 0x08, 0x00,
    0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1e, 0x00,
    0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00,
    0x18, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x3f, 0x70, 0x7e, 0x73, 0x73, 0x7e, 0x00,
    0x07, 0x07, 0x07, 0x3f, 0x67, 0x67, 0x3f, 0x00,
    0x00, 0x7e, 0x07, 0x07, 0x07, 0x07, 0x7e, 0x00,
    0x70, 0x70, 0x70, 0x7e, 0x73, 0x73, 0x7e, 0x00,
    0x00, 0x3e, 0x67, 0x67, 0x7f, 0x07, 0x7e, 0x00,
    0x78, 0x1c, 0x1c, 0x7f, 0x1c, 0x1c, 0x1c, 0x00,
    0x70, 0x7e, 0x73, 0x73, 0x7e, 0x70, 0x3f, 0x00,
    0x07, 0x07, 0x07, 0x3f, 0x67, 0x67, 0x67, 0x00,
    0x1c, 0x1c, 0x00, 0x1e, 0x1c, 0x1c, 0x1c, 0x00,
    0x38, 0x38, 0x00, 0x38, 0x38, 0x38, 0x1f, 0x00,
    0x07, 0x47, 0x67, 0x37, 0x3f, 0x7f, 0x67, 0x00,
    0x0e, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x3e, 0x00,
    0x00, 0x33, 0x7f, 0x6d, 0x6d, 0x6d, 0x6d, 0x00,
    0x00, 0x3b, 0x67, 0x67, 0x67, 0x67, 0x67, 0x00,
    0x00, 0x3e, 0x67, 0x67, 0x67, 0x67, 0x3e, 0x00,
    0x00, 0x3b, 0x67, 0x67, 0x3f, 0x07, 0x07, 0x00,
    0x00, 0x6e, 0x73, 0x73, 0x7e, 0x70, 0x70, 0x00,
    0x00, 0x07, 0x77, 0x7f, 0x0f, 0x07, 0x07, 0x00,
    0x00, 0x3e, 0x47, 0x1e, 0x3c, 0x71, 0x3e, 0x00,
    0x1c, 0x1c, 0x7f, 0x1c, 0x1c, 0x1c, 0x38, 0x00,
    0x00, 0x73, 0x73, 0x73, 0x73, 0x73, 0x6e, 0x00,
    0x00, 0x63, 0x63, 0x63, 0x36, 0x1c, 0x08, 0x00,
    0x00, 0x63, 0x6b, 0x6b, 0x6b, 0x7f, 0x36, 0x00,
    0x00, 0x63, 0x36, 0x1c, 0x1c, 0x36, 0x63, 0x00,
    0x00, 0x73, 0x73, 0x7e, 0x60, 0x71, 0x3e, 0x00,
    0x00, 0x7f, 0x30, 0x18, 0x0c, 0x06, 0x7f, 0x00,
    0x30, 0x08, 0x08, 0x04, 0x08, 0x08, 0x30, 0x00,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00,
    0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06, 0x00,
    0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1c, 0x3e, 0x77, 0x63, 0x77, 0x3e, 0x1c, 0x00,
    0x63, 0x77, 0x3e, 0x1c, 0x3e, 0x77, 0x63, 0x00,
    0x1c, 0x1c, 0x36, 0x36, 0x63, 0x7f, 0x7f, 0x00,
    0x7f, 0x7f, 0x63, 0x63, 0x63, 0x7f, 0x7f, 0x00,
    0x08, 0x1c, 0x3e, 0x7f, 0x1c, 0x1c, 0x1c, 0x00,
    0x1c, 0x1c, 0x1c, 0x7f, 0x3e, 0x1c, 0x08, 0x00,
    0x08, 0x0c, 0x7e, 0x7f, 0x7e, 0x0c, 0x08, 0x00,
    0x08, 0x18, 0x3f, 0x7f, 0x3f, 0x18, 0x08, 0x00,
    0x1f, 0x0f, 0x1f, 0x3f, 0x7d, 0x38, 0x10, 0x00,
    0x7c, 0x78, 0x7c, 0x7e, 0x5f, 0x0e, 0x04, 0x00,
    0x04, 0x0e, 0x5f, 0x7e, 0x7c, 0x78, 0x7c, 0x00,
    0x10, 0x38, 0x7d, 0x3f, 0x1f, 0x0f, 0x1f, 0x00,
    0x63, 0x73, 0x63, 0x63, 0x63, 0x63, 0x6f, 0x00,
    0x33, 0x63, 0x63, 0x33, 0x33, 0x33, 0x77, 0x00,
    0x1c, 0x22, 0x45, 0x45, 0x5d, 0x22, 0x1c, 0x00,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x00,
    0x63, 0x75, 0x65, 0x63, 0x67, 0x65, 0x65, 0x00,
    0x33, 0x65, 0x65, 0x33, 0x37, 0x35, 0x75, 0x00,
    0x1c, 0x22, 0x5d, 0x4d, 0x55, 0x22, 0x1c, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* clang-format on */

/* the value names the option table prints, one list per option kind */
static char *debugNtscPalName[] = {"NTSC", "PAL"}; /* derived name */

static char *debugFrameStepName[] = {"Single", "Double"}; /* derived name */

static char *debugOffOnName[] = {"Off", "On"}; /* derived name */

static char *debugFontName[] = {"Through-Low", "Through-High", "Hide-Low",
                                "Hide-High"}; /* derived name */

static char *debugBarName[] = {"Off", "Low-Deco", "High-Deco"}; /* derived name */

static char *debugProfileName[] = {"Variable", "Process"}; /* derived name */

static char *debugSnapSizeName[] = {"None", "1x1", "2x2", "4x4", "8x8"}; /* derived name */

static char *debugSnapFormName[] = {"TIM2", "BMP"}; /* derived name */

static char *debugBarScaleName[] = {"x512", "x256", "x128", "x64", "x32", "x16", "x8", "x4",
                                    "x2",   "x1",   "/2",   "/4",  "/8",  "/16"}; /* derived name */

static char *debugEnemyBattleName[] = {"Positive-Low", "Negative-Low", "Positive-High",
                                       "Negative-High"}; /* derived name */

static char *debugSpecularName[] = {"AllOff", "1Layer", "2Layer"}; /* derived name */

static char *debugLightVolumeName[] = {"AllOff", "LightOnly", "AmbientOnly",
                                       "Light+Ambient"}; /* derived name */

static char *debugBoundingName[] = {"AllOff", "Object", "Material", "AllOn"}; /* derived name */

static char *debugShadowName[] = {"On", "ExceptCharacter", "Line", "Off"}; /* derived name */

static char *debugScissorName[] = {"AllOff", "CarryZOnly", "IgnoreCarryZ",
                                   "AllOn"}; /* derived name */

static char *debugMotionTargetName[] = {"BOY", "GIRL", "BIRD", "ENEMY", "QUEEN"}; /* derived name */

/* the debug options: name, colour, value, range, value names and the function
   called on a change, in the order debug_Mode pages through them */
static const DbgOpt debugOption[76] = {
    {" FrameStep          ", 0x0080FF80, &systemStatus[1], 1, 2, debugFrameStepName,
     (void (*)(int))gsResetFunc},
    {" NTSC/PAL           ", 0x0080FF80, &systemStatus[0], 0, 1, debugNtscPalName,
     (void (*)(int))gsResetFunc},
    {" IgnoreDemoCamera   ", 0xC0C0C080, &debug_ignore_demo_camera, 0, 1, debugOffOnName, 0},
    {" DebugFrameStep     ", 0xC0C0C080, &debug_frame, 0, 60, 0, 0},
    {" DebugFont          ", 0xC0C0C080, &debug_font_flag, 0, 3, debugFontName, 0},
    {" DebugFont2         ", 0xC0C0C080, &debug_font_flag2, 0, 1, debugOffOnName, 0},
    {" DebugFont3         ", 0xC0C0C080, &debug_font_flag3, 0, 1, debugOffOnName, 0},
    {" Printf             ", 0xC0C0C080, &debug_printf_flag, 0, 1, debugOffOnName, 0},
    {" MemPartition       ", 0xC0C0C080, &debug_mem_partition_flag, 0, 1, debugOffOnName, 0},
    {" WireString         ", 0xC0C0C080, &debug_wire_string, 0, 1, debugOffOnName, 0},
    {" KIND ROUTINE OLD   ", 0xC0C0C080, &debugKindOld, 0, 1, debugOffOnName, 0},
    {" DebugBar           ", 0xFF808080, &debug_bar_flag, 0, 2, debugBarName, 0},
    {" DebugBarProfileType", 0xFF808080, &debug_profile_type, 0, 1, debugProfileName, 0},
    {" DebugBarStartItem  ", 0xFF808080, &debug_debug_bar_start_item, 0, 1000, 0, 0},
    {" DebugBarScale      ", 0xFF808080, &debug_debug_bar_multiply, -9, 4, debugBarScaleName, 0},
    {" DebugMemoryBar     ", 0xFF808080, &debug_memory_bar, 0, 1, debugOffOnName, 0},
    {" ADPCM PLAY         ", 0xFFC0C080, &debugAdpcmOn, 0, 1, debugOffOnName, 0},
    {" SE SLOT DISP       ", 0xFFC0C080, &debug_seslotdisp_flag, 0, 1, debugOffOnName, 0},
    {" WallCheck          ", 0x90C0FF80, &debug_wallcheck_flag, 0, 1, debugOffOnName, 0},
    {" WallHitColDisp     ", 0x90C0FF80, &debug_wallhitcoldisp, 0, 1, debugOffOnName, 0},
    {" FieldCollision     ", 0x90C0FF80, &debug_fieldcollision_flag, 0, 1, debugOffOnName, 0},
    {" CollisionRayDisp   ", 0x90C0FF80, &debug_col_ray_disp, 0, 1, debugOffOnName,
     (void (*)(int))ChangeFieldCollisionDebugMode},
    {" CollisionOldProc   ", 0x90C0FF80, &debug_col_old_proc, 0, 1, debugOffOnName, 0},
    {" Skelton            ", 0x80FFC080, &debug_skel_flag, 0, 1, debugOffOnName, 0},
    {" ClothInfo          ", 0x80FFC080, &debug_cloth_info, 0, 1, debugOffOnName, 0},
    {" HairTightLevel     ", 0x80FFC080, &debug_hair_tight_level, 0, 100, 0, 0},
    {" HairGravityLevel   ", 0x80FFC080, &debug_hair_gravity_level, 0, 500, 0, 0},
    {" HairBendAngle      ", 0x80FFC080, &debug_hair_bend_angle, 0, 512, 0, 0},
    {" HairCollision      ", 0x80FFC080, &debug_hair_collision, 0, 1, debugOffOnName, 0},
    {" FaceWInterpRatio   ", 0x80FFC080, &debug_face_rot_w_ratio, 0, 100, 0, 0},
    {" CharaTarget        ", 0x80FFC080, &debug_chara_target, 0, 1, debugOffOnName, 0},
    {" MotionActNode      ", 0x80FFC080, &debug_actnode_flag, 0, 1, debugOffOnName, 0},
    {" MotionInterporate  ", 0x80FFC080, &debug_motion_interporate, 0, 1, debugOffOnName, 0},
    {" MotionDebugWin     ", 0x80FFC080, &debug_window_flag, 0, 1, debugOffOnName, 0},
    {" MotionDebugTgt     ", 0x80FFC080, &debug_mot_debug_target, 0, 4, debugMotionTargetName, 0},
    {" MotionSlopeInterp  ", 0x80FFC080, &debug_mot_slope_interp, 0, 10, 0, 0},
    {" Fly Limit Info     ", 0x80FFC080, &debug_fly_limit_test, 0, 1, debugOffOnName, 0},
    {" BrainBar           ", 0xFF80FF80, &debug_brain_bar_flag, 0, 1, debugOffOnName, 0},
    {" BrainOnOff         ", 0xFF80FF80, &debug_brain_flag, 0, 1, debugOffOnName, 0},
    {" GBrainInfo         ", 0xFF80FF80, &debug_gbrain_info_flag, 0, 1, debugOffOnName, 0},
    {" WayTool            ", 0xFF80FF80, &debug_wayline, 0, 1, debugOffOnName, 0},
    {" SnapShotView       ", 0xC0C0C080, &debug_snapshot_num, 20, 500, 0, 0},
    {" SnapSize           ", 0xC0C0C080, &debug_snapshot_size, 0, 4, debugSnapSizeName, 0},
    {" SnapForm           ", 0xC0C0C080, &debug_snapshot_format, 0, 1, debugSnapFormName, 0},
    {" Jimaku Test        ", 0xC0C0C080, &debug_jimaku, 0, 1, debugOffOnName, 0},
    {" Camera             ", 0xC0C0C080, &debug_camera_flag, 0, 1, debugOffOnName, 0},
    {" RippleRoughness    ", 0xFFFF8080, &debug_ripple_roughness, 0, 100, 0, 0},
    {" Bounding           ", 0xFFFF8080, &debug_bounding_flag, 0, 3, debugBoundingName, 0},
    {" Specular           ", 0xFFFF8080, &debug_specular_flag, 0, 2, debugSpecularName, 0},
    {" LightVolume        ", 0xFFFF8080, &debug_ambient_volume, 0, 3, debugLightVolumeName, 0},
    {" ShadowOff          ", 0xFFFF8080, &debug_shadow_flag, 0, 3, debugShadowName, 0},
    {" Scissoring         ", 0xFFFF8080, &debug_scissor, 0, 3, debugScissorName, 0},
    {" FullScreenEffect   ", 0xFFFF8080, &debug_fullscreen_effect, 0, 1, debugOffOnName, 0},
    {" DispClusterModel   ", 0xFFFF8080, &debug_disp_cluster, 0, 1, debugOffOnName, 0},
    {" DispNormalModel    ", 0xFFFF8080, &debug_disp_normal, 0, 1, debugOffOnName, 0},
    {" DispLwsModel       ", 0xFFFF8080, &debug_disp_lws, 0, 1, debugOffOnName, 0},
    {" DispParticle       ", 0xFFFF8080, &debug_disp_particle, 0, 1, debugOffOnName, 0},
    {" DispMesh           ", 0xFFFF8080, &debug_disp_mesh, 0, 1, debugOffOnName, 0},
    {" DISPLAY BRIGHTNESS ", 0xFFFF8080, &systemStatus[11], 0, 15, 0, 0},
    {" STICK INPUT        ", 0xC0C0C080, &debug_stick_input, 0, 1, debugOffOnName, 0},
    {" STICK SIMULATE     ", 0xC0C0C080, &debug_stick_simulate, 0, 1, debugOffOnName, 0},
    {" DEBUG SUB THREAD   ", 0xC0C0C080, &debug_act_sub_thread, 0, 1, debugOffOnName, 0},
    {" NEW QUEEN BATTLE   ", 0xC0C0C080, &debug_use_new_queen_battle, 0, 1, debugOffOnName, 0},
    {" CHAIN CYCLE SPEED  ", 0xC0C0C080, &debug_chain_cycle_speed, 1, 10, 0, 0},
    {" CHAIN SLOW  SPEED  ", 0xC0C0C080, &debug_chain_slow_speed, 1, 10, 0, 0},
    {" DISP ENEMY STATE   ", 0x80C0FF80, &debug_disp_enemy_state, 0, 1, debugOffOnName, 0},
    {" ENEMY BATTLE TYPE  ", 0x80C0FF80, &debug_enemy_battle_type, 0, 3, debugEnemyBattleName, 0},
    {" ENEMY FLY WITH GIRL", 0x80C0FF80, &debug_enemy_fly_with_girl, 0, 1, 0, 0},
    {" DISP ESCORT BALL   ", 0xC0C0C080, &debug_disp_escort_ball, 0, 1, debugOffOnName, 0},
    {" GIRL DETOUR        ", 0xC0C0C080, &debug_girl_detour_flag, 0, 1, debugOffOnName, 0},
    {" LWSKYOMI LOOKONLY  ", 0xC0C0C080, &debug_lwskyomi_lookonly, 0, 1, debugOffOnName, 0},
    {" ONE HIT ONLY       ", 0xC0C0C080, &debug_one_hit_only, 0, 1, debugOffOnName, 0},
    {" IGNORE DODGE       ", 0xC0C0C080, &debug_ignore_dodge, 0, 1, debugOffOnName, 0},
    {" GAME CLEAR COUNT   ", 0xC0C0C080, &gFlagGameClear, 0, 9, 0, 0},
    {" GIRL PAD CONTROL   ", 0xC0C0C080, &debug_girl_pad_control, 0, 1, debugOffOnName,
     ChangeGirlControlMode},
    {" NO BREAST HANG     ", 0xC0C0C080, &debug_no_breast_hang, 0, 1, debugOffOnName, 0},
}; /* derived name */

/* the font window's line count */
static int fontWindowLine = 0; /* derived name */

/* the timer count debug_CallbackGsFinish latches when drawing ends */
static int drawTimerCount = 320; /* derived name */

/* debug_SelectStage's cursor */
static int stageSelectNo = 0; /* derived name */

/* unreferenced in the retail build */
static int startStageNo = -1; /* derived name */

int debugBackGroundDisableFlag = 0;

/* the areas debug_Load parcels its files into; the report prints how much of
   each one is in use once the file has been allocated out of it */

/* one glyph's image packet, built by debug_MakeFont and sent by
   debug_PrintCharacter: its size in quadwords and its address */
typedef struct { /* field names derived */
    int qwc;
    void *packet;
} DbgGlyphPacket; /* derived name */

/* the on-screen font window's line table: 26 records of 0x38 bytes, the colour
   word at +0 and the text at +4 (the strncpy below bounds it at 50) */
typedef struct { /* field names derived */
    int col;
    char text[52];
} DbgFontLine; /* derived name */

/* The bar colours are 4-byte GS colour records this TU only sees as far
   (incomplete-array) symbols; their byte alignment is what makes every copy
   an lwl/lwr pair. */
typedef struct { /* field names derived */
    unsigned char r, g, b, a;
} DbgCol; /* derived name */

/* declared ahead with no size; the sized definitions are below */
static DbgCol markCol[];

static DbgCol brainColLow[];

static DbgCol brainColMid[];

static DbgCol brainColHigh[];

static DbgCol brainColMax[];

static DbgCol barBackCol[];

static DbgCol barScaleCol[];

static DbgCol barLabelCol[];

/* the profiler ring: 0x400 entries of 28 bytes, filled by debug_SetBar. */
typedef struct {   /* field names derived */
    char name[12]; /* 0x00 */
    DbgCol col;    /* 0x0C */
    char *file;    /* 0x10 */
    short count;   /* 0x14 */
    short pad16;   /* 0x16 */
    int line;      /* 0x18 */
} DebugBar;        /* derived name */

/* .bss: debug_MakeBarString's string, debug_PrintFontf's line, the load
   info line, the debug box and ball, the collision ray, debugSceOpen's path,
   the font images and packets, the font window, the profiler ring and the
   load info table. */
static char barString[64]; /* derived name */

static char fontfLine[512]; /* derived name */

static char loadInfoLine[32]; /* derived name */

/* the debug box's centre and half extents, VU0 vectors */
static sceVu0FVECTOR boxCentre; /* derived name */

static sceVu0FVECTOR boxWidth; /* derived name */

/* the debug ball's centre */
static sceVu0FVECTOR ballCentre; /* derived name */

/* the ray the collision test drives */
static ClipWork collisionRay; /* derived name */

static char sceOpenPath[256]; /* derived name */

/* the 3x3-dilated outline, 16 shorts per glyph */
static unsigned short fontOutline[256 * 16]; /* derived name */

/* the glyph re-expanded to 8 shorts */
static unsigned short fontGlyph[256 * 8]; /* derived name */

static DbgGlyphPacket fontPacket[256]; /* derived name */

static DbgFontLine fontLines[26]; /* derived name */

static DebugBar debugBars[1024]; /* derived name */

/* two pages of 26 {count, mark} pairs */
static int loadInfoSeg[2][26][2]; /* derived name */

/* one 64-bit packet slot, written whole or as its two 32-bit halves */
typedef union { /* field names derived */
    long long d;
    int w[2];
    float f[2];
} DbgPkWord; /* derived name */

/* a whole quadword, for the vertex copies */
typedef ICO_QW Qw128;

/* clang-format on */

/* fontBitmap = the 8x8 1bpp font bitmap (8 bytes per glyph);
   fontGlyph = the glyph re-expanded to 8 shorts (shifted left one column);
   fontOutline = the 3x3-dilated outline, 16 shorts per glyph. */

/* GifPacket.h's entry points, which this TU does not include: its calls pass
   gif_Sprite's and gif_Line's z as a 32-bit unsigned int (lui/ori), where the
   header takes a long long */
extern int gif_CheckOpen(void);
extern void gif_EndPacket(void);
/* GifPacket.h's parameter list: debug_DrawBar passes its 64-bit alpha untruncated */
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);
extern void gif_SetZTest(int on);
extern void gif_SetZWrite(int on);
extern void gif_Sprite(FR *r, unsigned int z, FR *uv, DbgCol *col, int prim);
extern void gif_Line(int *v0, int *v1, unsigned int z0, unsigned int z1, DbgCol *col, int prim);
extern void gif_StartPacketPri(int pri);
extern void gif_SetDrawEnviroment(unsigned long long fbp, unsigned long long psm, unsigned int w,
                                  unsigned int h, int useoffset, int clear);

typedef struct { /* field names derived */
    int x, y, z;
} DbgPos; /* derived name */

typedef struct { /* field names derived */
    int x, y, z, w;
} DbgVtx; /* derived name */

/* .sbss: the rows debug_DispBox, debug_DispBall and debug_CollisionTest
   select, and the profiler's bar count. */
static int dispBoxRow; /* derived name */

static int dispBallRow; /* derived name */

static int collisionTestRow; /* derived name */

static int debugBarCount; /* derived name */

/* the two sprite rectangles and the three line colours the bar display
   starts from */

/* clang-format on */

/* TIM2 image file: a 16-byte file header followed by one 48-byte picture
   header and the raw 32-bit image, one row per write */
typedef struct { /* field names derived */
    char id[4];  /* "TIM2" */
    unsigned char ver;
    unsigned char fmt;
    short nPictures;
    long long pad;
} Tim2FileHdr; /* derived name */

typedef struct {                  /* field names derived */
    int totalSize;                /* 0x00 */
    int clutSize;                 /* 0x04 */
    int imageSize;                /* 0x08 */
    short headerSize;             /* 0x0C */
    short clutColors;             /* 0x0E */
    unsigned char imageType;      /* 0x10 */
    unsigned char mipMapTextures; /* 0x11 */
    unsigned char clutType;       /* 0x12 */
    unsigned char imageColorType; /* 0x13 */
    short imageWidth;             /* 0x14 */
    short imageHeight;            /* 0x16 */
    long long gsTex0;             /* 0x18 */
    long long gsTex1;             /* 0x20 */
    int gsRegs;                   /* 0x28 */
    int gsTexClut;                /* 0x2C */
} Tim2PicHdr;                     /* derived name */

/* 24-bit BMP file header, offset by two pad bytes so the 32-bit fields land
   4-aligned on the stack; the file image starts at &hdr.bfType. */
typedef struct {             /* field names derived */
    unsigned char pad[2];    /* 0x00 */
    unsigned char bfType[2]; /* 0x02 */
    int bfSize;              /* 0x04 */
    int bfReserved;          /* 0x08 */
    int bfOffBits;           /* 0x0C */
    int biSize;              /* 0x10 */
    int biWidth;             /* 0x14 */
    int biHeight;            /* 0x18 */
    short biPlanes;          /* 0x1C */
    short biBitCount;        /* 0x1E */
    int biCompression;       /* 0x20 */
    int biSizeImage;         /* 0x24 */
    int biXPelsPerMeter;     /* 0x28 */
    int biYPelsPerMeter;     /* 0x2C */
    int biClrUsed;           /* 0x30 */
    int biClrImportant;      /* 0x34 */
} BmpHeader;                 /* derived name */

/* libgraph.h leaves it out for Texture.c's int view; this TU takes the
   library's short parameters */
extern int sceGsSetDefStoreImage(sceGsStoreImage *si, short fbp, short fbw, short psm, short x,
                                 short y, short w, short h);
/* libgcc's dp-bit.c helper, which no header declares */
extern float dptofp(double v);

/* clang-format on */

/* the "*" wildcard pattern "*" is copied into the request block's name
   field as a 2-byte object, not by strcpy */
typedef struct { /* field names derived */
    char c[2];
} McPat; /* derived name */

/* mcard.c's request entry points return iosMsgSend's result; this TU never
   reads it and declares them void, which its calls pin, so it does not
   include mcard.h */
extern void iosMcChdirProduct(McMgr *mp);
extern void iosMcGetDir(McMgr *mp);
extern int iosMcSync(McMgr *mp);
extern void iosMcGetBlockSaveInfo(McMgr *mp);
extern void iosMcSaveIconBlock(McMgr *mp);
extern void iosMcSaveProductBlock(McMgr *mp);
extern void iosMcSaveGameBlock(McMgr *mp, void *arg);
extern void iosMcLoadProductBlock(McMgr *mp);
extern void iosMcLoadGameBlock(McMgr *mp, void *arg);
extern void iosMcDelete(McMgr *mp);
extern void iosMcGetInfo(McMgr *mp);
extern void iosMcFormat(McMgr *mp);
extern void iosMcUnformat(McMgr *mp);
extern void iosMcTest(void);

/* the default save-file name "game." lives in .sdata as 6 bytes */
typedef struct { /* field names derived */
    char c[6];
} McName6; /* derived name */

/* one line of the memory-card menu: the label debug_SelectCsvWindow prints and
   the state machine it hands control to */
typedef struct { /* field names derived */
    char *label;
    int (*fn)();
} McMenuItem; /* derived name */

/* the card-state line: the iosMc state code, its colour and its caption */
typedef struct { /* field names derived */
    int type;
    unsigned int col;
    char *msg;
} McTypeMsg; /* derived name */

/* the generated sedef member's rows; s_init.h declares no seDef, since s_init.c
   writes procRan into them */
extern const SeDef seDef[];

/* the sibling of debug_ListPadControlGobj that lists the actor GObjs the debug menu can print (kinds 1, 2, 4 and 0x2F). */
typedef struct { /* field names derived */
    char *name;
    void *obj;
} DbgGobjEnt; /* derived name */

/* one editable row of the debug box: its label and the value shown */
typedef struct { /* field names derived */
    char *name;
    int val;
} DbgBoxVal; /* derived name */

/* one editable value of the debug ball: its label and the cell it moves */
typedef struct { /* field names derived */
    char *name;
    float *val;
} DbgBallVal; /* derived name */

typedef struct { /* field names derived */
    DbgBallVal v[4];
} DbgBallList; /* derived name */

/* the four-row initialiser template (centerX, centerY, centerZ, radius), blob-owned
   by address until the TU's plain .rodata run closes up */

/* The strings these tables point at stay blob-owned by address until the
   TU's plain .rodata and .sdata runs close up. */
/* the menu's handlers defined further down this file or in other TUs */
/* no header declares it */
extern int MotionViewer(void);
/* effectTool.h does not declare it */
extern int EffectTool(void);
/* motionManager2.h does not declare it; the definition takes int * */
extern void DebugDisp1Collision(void *hit);

/* Profiler bar table: 0x400 entries of 0x1C bytes; debugBarCount = live count.
   Callers pass (label, colour, __FILE__, __LINE__) -- see the call sites in
   main.c and motionManager2.c, where the fourth argument is literally the caller's line number.
   +0x14 samples the EE timer T0_COUNT at 0x10000000, volatile because it is a
   hardware counter. */

inline void ChangeGirlControlMode(int mode)
{
    if (mode == 1) {
        girlControlMode = mode;
    }
}

void debug_Assert(char *fmt, ...)
{
    char buf[256];
    vsprintf(buf, fmt, (char *)__builtin_next_arg(fmt) - 56);
    debug_assertMessage("src/debug.c", 1392, buf);
    __assert("src/debug.c", 1392, "e");
    debug_assert("src/debug.c", 1393);
    __assert("src/debug.c", 1393, "0");
}

/* the host log file (the DEBUG build opens it) */
static int logFd = -1; /* derived name */

/* unreferenced in the retail build */
static int logAppend = 0; /* derived name */

/* debug_SetDmaCallback's DMA handler id */
static unsigned int dmaHandlerId = -1; /* derived name */

/* The log goes to a file on the host only in the DEBUG build; retail leaves
   the handle at -1 and the path buffer unused.  The open and its reports are
   disabled in retail. */
void debug_openLog(void)
{
    char buf[256];

    logFd = -1;
    if (0) {
        debug_StdPrintfDummy("ico_debug.log");
        debug_StdPrintfDummy("log-file opened.\n");
        debug_StdPrintfDummy("log-file appended.\n");
    }
}

/* the close report, disabled in retail */
inline void debug_closeLog(void)
{
    if (0) {
        debug_StdPrintfDummy("log-file closed.\n");
    }
}

void debug_LogPrintf(const char *fmt, ...)
{
    char buf[256];
    int info;
    vsprintf(buf, fmt, (char *)__builtin_next_arg(fmt) - 0x38);
    info = strlen(buf);
    sceWrite(logFd, buf, info);
}

inline void debug_SaveStartStageFile(int stage)
{
    char buf[256];
    debug_StdPrintfDummy("==== Save start stage =======================================\n");
    if (debugSceOpen("thisIsYourStartStage", 0x602) < 0) {
        debug_StdPrintfDummy("debug_SaveStartStageFile: host file open error.\n");
    } else {
        sprintf(buf, "%d", stage);
        sceWrite(0, buf, strlen(buf));
        debug_StdPrintfDummy("Save start stage file. Setting to [ \033[36m%d\033[m ]\n", stage);
        debugSceClose(0);
    }
    debug_StdPrintfDummy("=============================================================\n");
    debug_openLog();
}

inline int debug_TryToGetStartStage(void)
{
    return -1;
}

static void debug_SaveDebugOptionFile(void)
{
    char buf[256];
    int i;
    int fd;
    debug_StdPrintfDummy("==== Save Debug Option ======================================\n");
    fd = debugSceOpen("thisIsYourDebugOption", 0x602) < 0;
    if (fd) {
        debug_StdPrintfDummy("debug_SaveDebugOptionFile: host file open error.\n");
    } else {
        for (i = 0; i < 76; i++) {
            sprintf(buf, "#%s\n", debugOption[i].name);
            sceWrite(fd, buf, strlen(buf));
            sprintf(buf, "%d\n", *debugOption[i].val);
            sceWrite(fd, buf, strlen(buf));
        }
        debugSceClose(fd);
        debug_StdPrintfDummy("Save Debug Option file.\n");
    }
    debug_StdPrintfDummy("=============================================================\n");
    debug_openLog();
}

static int debug_GetDebugOption(void)
{
    char buf[256];
    int fd;
    int size;
    int i;
    int n;
    int cnt;
    const DbgOpt *o;
    const DbgOpt *p;

    debug_StdPrintfDummy("==== Try to read Debug Option file. =========================\n");
    fd = debugSceOpen("thisIsYourDebugOption", 1);
    if (fd < 0) {
        debug_StdPrintfDummy("debug_GetDebugOption:No Debug Option file. Setting to default.\n");
        fd = -1;
    } else {
        cnt = 0;
        size = sceLseek(fd, 0, 2);
        sceLseek(fd, 0, 0);
        /* clang-format off */
        n = 0; i = 0;
        p = o = debugOption; do {
            /* clang-format on */
            sceRead(fd, &buf[n], 1);
            if (buf[n++] == '\n') {
                switch (buf[0]) {
                default:
                    if (buf[0] != '\n') {
                        /* clang-format off */
                        *o->val = atoi(buf); o++; p++; cnt++;
                        /* clang-format on */
                    } else {
                        debug_StdPrintfDummy(
                            "\033[36mInvalid line appeard.\n    Use default one.\033[m\n");
                        cnt = -1;
                        goto done;
                    }
                    break;
                case '#':
                    buf[strlen(buf) - 1] = 0;
                    if (strcmp(o->name, &buf[1]) != 0) {
                        debug_StdPrintfDummy(
                            "\033[36mInvalid name appeard.(%s!=%s)\n    Use default one.\033[m\n",
                            p->name, &buf[1]);
                        cnt = -1;
                        goto done;
                    }
                }
                n = 0;
            }
        } while (++i < size);
    done:
        debugSceClose(fd);
        if (cnt != 76) {
            fd = -1;
            debug_StdPrintfDummy("debug_GetDebugOption:Found Debug Option file, But illegal.\n");
        } else {
            debug_StdPrintfDummy("Found Debug Option file.\n");
            for (i = 0; i < 76; i++) {
                debug_StdPrintfDummy("%s => %d\n", debugOption[i].name, *debugOption[i].val);
            }
        }
    }
    debug_StdPrintfDummy("=============================================================\n");
    debug_openLog();
    return fd;
}

void debug_SetDmaCallback(void)
{
    if ((int)dmaHandlerId != -1) {
        RemoveDmacHandler(1, dmaHandlerId);
    }
    dmaHandlerId = AddDmacHandler(1, debug_CallbackGsFinish, -1);
    EnableDmac(1);
}

void debug_VariableInit(void)
{
    debug_frame = 15;
    debug_window_flag = 0;
    debug_ignore_demo_camera = 0;
    debug_bar_flag = 0;
    debug_memory_bar = 0;
    debug_font_flag = 0;
    debug_font_flag2 = 0;
    debug_font_flag3 = 0;
    debug_printf_flag = 0;
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
    debug_chara_target = 0;
    debug_brain_flag = 1;
    debug_bounding_flag = 0;
    debug_wire_string = 0;
    debug_gbrain_info_flag = 1;
    debug_now_motion_viewer = 0;
    fontWindowLine = 0;
    debug_mot_debug_target = 0;
    debug_debug_bar_multiply = 1;
    debug_shadow_flag = 0;
    debug_specular_flag = 1;
    debug_zoom_per = 100;
    debug_debug_bar_start_item = 0;
    debug_ripple_roughness = 25;
    debug_snapshot_num = 100;
    debug_snapshot_size = 0;
    debug_snapshot_format = 0;
    debug_ambient_volume = 0;
    debug_cloth_info = 0;
    debug_hair_tight_level = 20;
    debug_hair_gravity_level = 0;
    debug_hair_bend_angle = 256;
    debug_hair_collision = 0;
    debug_face_chest_ratio = 0;
    debug_face_rot_w_ratio = 0;
    debug_def_smpmin = 4;
    debug_scissor = 1;
    debug_fullscreen_effect = 1;
    debug_disp_cluster = 1;
    debug_disp_normal = 1;
    debug_disp_lws = 1;
    debug_disp_particle = 1;
    debug_disp_mesh = 1;
    debug_col_ray_disp = 0;
    debug_stick_input = 0;
    debug_enemy_battle_type = 3;
    debug_stick_simulate = 1;
    debug_act_sub_thread = 0;
    debug_use_new_queen_battle = 1;
    debug_chain_cycle_speed = 4;
    debug_chain_slow_speed = 4;
    debug_mot_slope_interp = 5;
    debug_enemy_fly_with_girl = 0;
    debug_disp_enemy_state = 0;
    debug_disp_escort_ball = 0;
    debug_girl_detour_flag = 1;
    debug_col_old_proc = 0;
    debug_fly_limit_test = 0;
    debug_lwskyomi_lookonly = 0;
    debug_one_hit_only = 0;
    debug_ignore_dodge = 0;
    debug_hand_camera = 1;
    debug_enemy_kidnap_timer = 1;
    debug_girl_pad_control = 0;
    debug_no_breast_hang = 0;
    debug_snapshot_reserve = 0;
    game_pause = 0;
    ChangeFieldCollisionDebugMode(0);
    ChangeGirlControlMode(debug_girl_pad_control);
}

void debug_Init(void)
{
    debug_ClearFontWindow();
    polygons = 0;
    strips = 0;
    packets = 0;
    textures = 0;
    texregs = 0;
    texturetranssize = 0;
    debug_snapshot_counter = 0;
    *T0_MODE = 0x82;
    *T1_MODE = 0x82;
    debug_makeBackImage();
}

inline void debug_BeginTimer(int mode)
{
    *T1_COUNT = 0;
    *T1_MODE = mode | 0x80;
}

inline float debug_GetTimerSec(void)
{
    float clock[4] = {2500000.0f, 156250.0f, 9000.0f, 260.0f};
    int v;
    float f2;

#ifdef ICO_HOST
    return -1.0f; /* no EE timer 1 on the host */
#else
    if (*T1_MODE & 0x800) {
        return -1.0f;
    }
    v = *T1_COUNT;
    f2 = (float)(unsigned int)v;
    return f2 / clock[*T1_MODE & 3] / 60.0f;
#endif
}

inline float debug_GetTimerCount(void)
{
#ifdef ICO_HOST
    return -1.0f; /* no EE timer 1 on the host */
#else
    if ((*T1_MODE) & 0x800) {
        return -1.0f;
    }
    return (float)(*(volatile unsigned int *)T1_COUNT);
#endif
}

int debug_Load(char **dst, char *name, int kind)
{
    char buf[256];
    int size;
    int sz;
    int fd;

    sprintf(buf, "ico2Data/%s", name);
    fd = debugSceOpen(buf, 1);
    if (fd < 0) {
        debug_StdPrintfDummy("file is not exist(%s)\n", name);
        return -1;
    }
    size = sceLseek(fd, 0, 2);
    sceLseek(fd, 0, 0);
    sz = (size / 16 + 1) * 16;
    {
        /* the line every arm prints once it has the file's address, a nested
           function that reads dst, name and size from the enclosing frame;
           the iosMallocDebug calls pass the source's own line numbers */
        inline void loadReport(void)
        {
            debug_StdPrintfDummy(
                "loading:\"\033[33m%s\033[m\"\n\t(address:\033[35m%p\033[m/size:\033[35m%d\033[m)",
                name, *dst, size);
        }
        switch (kind) {
        case 0:
        default:
            *dst = iosMallocDebug(ios_partition_seki, sz, "src/debug.c", 1958);
            loadReport();
            debug_StdPrintfDummy(" to seki area.(%2.1f%%)\n",
                                 (*dst + sz - ios_partition_seki->start) * 100.0f / 10059776.0f);
            break;
        case 1:
            *dst = iosMallocDebug(ios_partition_sugipon, sz, "src/debug.c", 1965);
            loadReport();
            debug_StdPrintfDummy(" to sugi area.(%2.1f%%/%2.1f%%)\n", sz * 100.0f / 524288.0f,
                                 (*dst + sz - ios_partition_sugipon->start) * 100.0f / 524288.0f);
            break;
        case 2:
            *dst = iosMallocDebug(ios_partition_common, sz, "src/debug.c", 1973);
            loadReport();
            debug_StdPrintfDummy(" to static object area.(%2.1f%%/%2.1f%%)\n", sz * 100.0f / 2.0f,
                                 (*dst + sz - ios_partition_common->start) * 100.0f / 2.0f);
            break;
        case 3:
            *dst = iosMallocDebug(ios_partition_smotion, sz, "src/debug.c", 1982);
            loadReport();
            debug_StdPrintfDummy(" to static motion area.(%2.1f%%/%2.1f%%)\n",
                                 sz * 100.0f / 1179648.0f,
                                 (*dst + sz - ios_partition_smotion->start) * 100.0f / 1179648.0f);
            break;
        case 5:
            *dst = iosMallocDebug(ios_partition_dmotion, sz, "src/debug.c", 1991);
            loadReport();
            debug_StdPrintfDummy(" to dynamic motion area.(%2.1f%%/%2.1f%%)\n",
                                 sz * 100.0f / 3670016.0f,
                                 (*dst + sz - ios_partition_dmotion->start) * 100.0f / 3670016.0f);
            break;
        case 6:
            *dst = iosMallocDebug(ios_partition_hara, sz, "src/debug.c", 2000);
            loadReport();
            debug_StdPrintfDummy(" to hara-area.(%2.1f%%)\n",
                                 (*dst + sz - ios_partition_hara->start) * 100.0f);
            break;
        case 7:
            *dst = iosMallocDebug(ios_partition_oomori, sz, "src/debug.c", 2007);
            loadReport();
            debug_StdPrintfDummy(" to oomori area.(%2.1f%%)\n",
                                 (*dst + sz - ios_partition_oomori->start) * 100.0f / 327680.0f);
            break;
        case 8:
            *dst = iosMallocDebug(ios_partition_horagai, sz, "src/debug.c", 2014);
            loadReport();
            debug_StdPrintfDummy(" to horagai-area.\n");
            break;
        case 9:
            *dst = iosMallocDebug(ios_partition_sound, sz, "src/debug.c", 2019);
            loadReport();
            debug_StdPrintfDummy(" to sound-area.\n");
            break;
        case 10:
            *dst = iosMallocDebug(ios_partition_sound_semi, sz, "src/debug.c", 2024);
            loadReport();
            debug_StdPrintfDummy(" to sound_semi-area.\n");
            break;
        }
    }
    sceRead(fd, *dst, size);
    debugSceClose(fd);
    FlushCache(0);
    sceGsSyncPath(0, 0);
    return size;
}

/* a GIF tag as the two 64-bit words the packet takes it in, on the
   quadword boundary the GIF reads it at */
typedef unsigned long long GifTag[2] __attribute__((aligned(16))); /* derived name */

/* the GIF tag debug_MakeFont copies ahead of each font packet, as
   debug_exception's fontTag */
static GifTag debugFontTag = {0x2000400000008000LL, 0x51}; /* derived name */

/* clang-format off */
static void debug_MakeFont(void)
{
    int on = 1, off = 0;
    struct { /* field names derived */ float v[4]; char *volatile ptr; } w; /* the cursor is re-read from the frame at every push */
    char *base;
    unsigned short *a, *b; unsigned short m0, m1; int i, j, k, n;
    base = iosMallocDebug(ios_partition_seki, 1, "src/debug.c", 2069); w.ptr = base;
    *ICO_POSTINC(int *, w.ptr) = 0x1400000C;
    *ICO_POSTINC(int *, w.ptr) = 0;
    *ICO_POSTINC(long long *, w.ptr) = 0;

    for (i = 0; i < 256; i++) {
        a = &fontOutline[i * 16]; b = &fontGlyph[i * 8];

        for (j = 0, n = 0; j < 9; j++) {
            m1 = a[j];
            m0 = b[j];
            for (k = 0; k < 10; k++, m0 >>= 1, m1 >>= 1)
                if ((m0 & 1) || (m1 & 1)) n++;
        }
        if (n == 0) {
            fontPacket[i].qwc = 1;
            fontPacket[i].packet = base;
        } else {


            fontPacket[i].qwc = n + 3;
            w.ptr = fontPacket[i].packet = iosMallocDebug(ios_partition_seki, (n + 3) * 16, "src/debug.c", 2090);
            *ICO_POSTINC(long long *, w.ptr) = 0;
            *ICO_POSTINC(int *, w.ptr) = 0;
            *ICO_POSTINC(int *, w.ptr) = ((n + 1) << 16) | 0x6C008000;

            *ICO_POSTINC(long long *, w.ptr) = n | ((long long)0x8000 << 38) | debugFontTag[0];
            *ICO_POSTINC(long long *, w.ptr) = debugFontTag[1];
            for (j = 0; j < 9; j++) {
                m0 = b[j];
                m1 = a[j];
                for (k = 0; k < 10; k++, m0 >>= 1, m1 >>= 1) {
                    w.v[0] = (float)k;

                    w.v[1] = (float)(j * 2);



                    if (m0 & 1) {
                        w.v[2] = 3.4028235e+38f;
                        w.v[3] = *(float *)&off;
                        *ICO_POSTINC(Qw128 *, w.ptr) = *(Qw128 *)w.v;
                    } else if (m1 & 1) {
                        w.v[2] = 1.7014117e+38f;
                        w.v[3] = *(float *)&on;
                        *ICO_POSTINC(Qw128 *, w.ptr) = *(Qw128 *)w.v;
                    }
                }
            }
            *ICO_POSTINC(int *, w.ptr) = 0x1400000A;
            *ICO_POSTINC(int *, w.ptr) = 0;
            *ICO_POSTINC(long long *, w.ptr) = 0;
        }
    }
}

static void debug_makeBackImage(void)
{
    int i;
    int j;
    const unsigned char *src;
    unsigned short *a;
    unsigned short *b;
    for (i = 0; i < 256; i++) {
        src = &fontBitmap[i * 8];
        a = &fontOutline[i * 16];
        b = &fontGlyph[i * 8];

        for (j = 0; j < 16; j++) {
            a[j] = 0;
            b[j] = 0;
        }
        for (j = 0; j < 8; j++)
            b[j + 1] = src[j] << 1;
        a[0] = b[0] | (b[0] << 1) | (b[0] >> 1) | b[1] | (b[1] << 1) | (b[1] >> 1);
        for (j = 1; j < 9; j++)
            a[j] = b[j - 1] | (b[j - 1] << 1) | (b[j - 1] >> 1) | b[j] | (b[j] << 1) | (b[j] >> 1) |
                   b[j + 1] | (b[j + 1] << 1) | (b[j + 1] >> 1);
        for (j = 0; j < 10; j++)
            a[j] = a[j] << 1;
        for (j = 0; j < 10; j++)
            a[j] = a[j] & ~b[j];
    }
    debug_MakeFont();
}

static void debug_PrintCharacter(char *str, int x, int y, int r, int g, int b, int sz)
{
    char *p;
    char *q;
    int px, py;
    int v[4] = {0, 0, 0, 0x60};
    int c;
    int col[4] = {r, g, b, sz};

    /* one packet word and its cursor advance per line; the DMA tag's line
       also opens the tail */
    /* clang-format off */
    p = PacketBufferStruct.ptr.c; PacketBufferStruct.dma.c = p; PacketBufferStruct.gif.c = 0; PacketBufferStruct.end.c = 0;

    PacketBufferStruct.tail.c = p; ((DbgPkWord *)p)->d = 0x10000006; PacketBufferStruct.ptr.c = p + 8;
    ((DbgPkWord *)(p + 8))->w[0] = 0x11000000; PacketBufferStruct.ptr.c = p + 0xC;
    ((DbgPkWord *)(p + 0xC))->w[0] = 0x3000104; PacketBufferStruct.ptr.c = p + 0x10;
    ((DbgPkWord *)(p + 0x10))->d = 0; PacketBufferStruct.ptr.c = p + 0x18;
    ((DbgPkWord *)(p + 0x18))->w[0] = 0x200017E; PacketBufferStruct.ptr.c = p + 0x1C;
    ((DbgPkWord *)(p + 0x1C))->w[0] = 0x6C048000; PacketBufferStruct.ptr.c = p + 0x20;

    _CopyIVector(ICO_POSTINC(sceVu0IVECTOR *, PacketBufferStruct.ptr.c), col);
    _CopyIVector(ICO_POSTINC(sceVu0IVECTOR *, PacketBufferStruct.ptr.c), v);

    q = PacketBufferStruct.ptr.c; px = x * ScreenWidth / 640 + 2048; px -= ScreenWidth / 2; ((DbgPkWord *)q)->f[0] = (float)px; q += 4; PacketBufferStruct.ptr.c = q;
    py = y * ScreenHeight / 224 + 2048; py -= ScreenHeight / 2; py--; ((DbgPkWord *)q)->f[0] = (float)py; PacketBufferStruct.ptr.c = q + 4;
    ((DbgPkWord *)(q + 4))->d = 0; PacketBufferStruct.ptr.c = q + 0xC;
    ((DbgPkWord *)(q + 0xC))->f[0] = (float)ScreenWidth * 12.0f / 640.0f; PacketBufferStruct.ptr.c = q + 0x10;
    ((DbgPkWord *)(q + 0x10))->w[0] = 0; PacketBufferStruct.ptr.c = q + 0x14;
    ((DbgPkWord *)(q + 0x14))->d = 0; PacketBufferStruct.ptr.c = q + 0x1C;

    ((DbgPkWord *)(q + 0x1C))->w[0] = 0x14000008; PacketBufferStruct.ptr.c = q + 0x20;
    ((DbgPkWord *)(q + 0x20))->w[0] = 0; PacketBufferStruct.ptr.c = q + 0x24;
    ((DbgPkWord *)(q + 0x24))->d = 0; PacketBufferStruct.ptr.c = q + 0x2C;

    PacketBufferStruct.tail.c = q + 0x2C; ((DbgPkWord *)(q + 0x2C))->d = 0x60000000; PacketBufferStruct.ptr.c = q + 0x34; ((DbgPkWord *)(q + 0x34))->w[0] = 0; PacketBufferStruct.ptr.c = q + 0x38; ((DbgPkWord *)(q + 0x38))->w[0] = 0; PacketBufferStruct.ptr.c = q + 0x3C;

    dl_SetDLPriority(12); dl_OpenDma(5, PacketBufferStruct.dma.c, 0); dl_CloseDma();

    dl_SetDLPriority(12);
    while ((c = (unsigned char)*str++) != 0) {
        /* clang-format on */
        if (fontPacket[c].packet != 0) {
            dl_OpenDma(2, fontPacket[c].packet, fontPacket[c].qwc);
            dl_CloseDma();
        }
    }
}

/* debug_PrintFont's backdrop colour */
static DbgCol fontBackCol = {0x00, 0x00, 0x00, 0x80}; /* derived name */

static void debug_PrintFont(int x, int y, int col, char *str)
{
    FR buf[2];
    int r;

    buf[1].x = x - 0x142;
    buf[1].y = y - 0x71;
    r = strlen(str);
    buf[1].h = 9;
    buf[1].w = r * 0xC + 4;
    buf[0] = buf[1];

    if (gif_CheckOpen() != 0) {
        return;
    }
    if (debug_font_flag & 2) {
        gif_StartPacketPri(11);
        gif_SetZTest(0);
        gif_SetZWrite(0);
        gif_SetAlpha(1, 2, 0x80);
        gif_Sprite(&buf[0], 0xFFFFFFFDU, 0, &fontBackCol, 1);
        gif_EndPacket();
    } else {
        gif_StartPacketPri(11);
        gif_SetZTest(0);
        gif_SetAlpha(1, 2, 0x80);
        gif_EndPacket();
    }
    debug_PrintCharacter(str, x, y, (unsigned)col >> 24, ((unsigned)col >> 16) & 0xFF,
                         ((unsigned)col >> 8) & 0xFF, 0x70);
}

int charNumH = 10;

inline void debug_ClearFontWindow(void)
{
    char *p = (char *)fontLines;
    int i;
    p += 0x5B4;
    for (i = 26; i >= 0; i--) {
        *p = 0;
        p -= 0x38;
    }
    fontWindowLine = 0;
}

static void debug_FlushFontWindow(void)
{
    FR r = {16, (int)(224.0f - ((float)charNumH + 0.5f) * 8.0f), 50, charNumH};
    FR rect;
    FR tmp;
    DbgCol *col;
    int i;

    tmp.x = r.x - 0x144;
    tmp.y = r.y - 0x72;
    tmp.w = r.w * 0xC + 8;
    tmp.h = r.h * 8 + 4;
    rect = tmp;
    col = (DbgCol *)&tmp;
    memset(col, 0, 4);
    ((char *)&tmp)[3] = 0x20;
    if (debug_window_flag != 0) {
        gif_StartPacketPri(11);
        gif_SetZWrite(0);
        gif_SetZTest(0);
        gif_SetAlpha(1, 2, 0x20);
        gif_Sprite(&rect, 0xFFFFFFFDU, 0, col, 1);
        gif_SetZWrite(1);
        gif_SetZTest(1);
        gif_EndPacket();
        for (i = 0; i <= fontWindowLine; i++) {
            debug_PrintFont(r.x, r.y + i * 8, *(int *)((char *)fontLines + i * 0x38),
                            (char *)fontLines + i * 0x38 + 4);
        }
    }
}

void debug_FlushFont(void)
{
    debug_FlushFontWindow();
}

inline int debug_CallbackGsFinish(int channel)
{
#ifdef ICO_HOST
    drawTimerCount = 0; /* no EE timer 0 on the host */
#else
    drawTimerCount = *T0_COUNT;
#endif
    return 0;
}

/* The four corners of a marker box, inlined into draw_batsu and
   draw_shikaku.  `q` is read only by the DEBUG build's report (its text
   derived).  The offsets table is built element by element, since `r` makes
   the initialiser non-constant. */
static inline void make_mark_points(DbgVtx *v, DbgPos *p, int r) /* derived name */
{
    DbgPos q;
    int ofs[4][2] = {{-r, -r}, {r, -r}, {-r, r}, {r, r}};
    int i;

#ifdef DEBUG
    q = *p;
    printf("mark %d %d %d (%d)\n", q.x, q.y, q.z, r);
#endif
    for (i = 0; i < 4; i++) {
        v[i].x = p->x;
        v[i].y = p->y;
        v[i].z = p->z;
        v[i].x += ofs[i][0];
        v[i].y += ofs[i][1];
    }
}

/* the brain bar colours: the mark, then the four levels */
static DbgCol markCol[1] = {{0x00, 0x00, 0xFF, 0xFF}}; /* derived name */

static DbgCol brainColLow[1] = {{0x40, 0x40, 0xFF, 0xFF}}; /* derived name */

static DbgCol brainColMid[1] = {{0x40, 0xFF, 0x40, 0xFF}}; /* derived name */

static DbgCol brainColHigh[1] = {{0xFF, 0x40, 0x40, 0xFF}}; /* derived name */

static DbgCol brainColMax[1] = {{0xFF, 0xFF, 0xFF, 0xFF}}; /* derived name */

static void debug_brainBar(void)
{
    void draw_batsu(DbgPos * p)
    {
        DbgCol col = markCol[0];
        DbgVtx v[4];

        make_mark_points(v, p, 3);
        gif_Line(&v[0].x, &v[3].x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col, 1);
        gif_Line(&v[1].x, &v[2].x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col, 1);
    }
    void draw_shikaku(DbgPos * p)
    {
        DbgCol col = markCol[0];
        DbgVtx v[4];

        make_mark_points(v, p, 3);
        gif_Line(&v[0].x, &v[1].x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col, 1);
        gif_Line(&v[1].x, &v[3].x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col, 1);
        gif_Line(&v[3].x, &v[2].x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col, 1);
        gif_Line(&v[2].x, &v[0].x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col, 1);
    }
    DbgCol c0 = brainColLow[0];
    DbgCol c1 = brainColMid[0];
    DbgCol c2 = brainColHigh[0];
    DbgCol c3 = brainColMax[0];
    DbgPos a;
    DbgPos b;
    DbgPos c;
    Brain *brain;
    DbgCol *col;
    int i;
    int y;
    int ytop;

    ytop = -100;
    y = ytop;
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 2, c3.a);
    brain = &brainGirl;
    for (i = 0; i < 40; i++) {
        b.x = 300;
        a.y = b.y = y;
        if (brain->tgt[i].gobj == 0) {
            continue;
        }
        y += 4;
        a.y = b.y = y;
        a.x = 300.0f - brainGetLevel(brain, &brain->tgt[i]) * 20.0f;
        if (brain->idx == i) {
            col = &c2;
        } else if (brainGetLevel(brain, &brain->tgt[i]) < brain->threshold) {
            col = &c0;
        } else {
            col = &c1;
        }
        gif_Line(&a.x, &b.x, 0xFFFFFFFFU, 0xFFFFFFFFU, col, 1);
        if (brain->tgt[i].gobj->kind == 0x3D) {
            /* a.z is never written: the copy reads it uninitialised */
            c.x = a.x;
            c.y = a.y;
            c.z = a.z;
            c.x -= 15;
            if (brain->tgt[i].alwaysSeen) {
                draw_shikaku(&c);
            } else {
                draw_batsu(&c);
            }
            c.x += 10;
            if (brainCheckView(brain, &brain->tgt[i])) {
                draw_shikaku(&c);
            } else {
                draw_batsu(&c);
            }
            c.x += 15;
            if (brain->tgt[i].lookOnly) {
                draw_shikaku(&c);
            } else {
                draw_batsu(&c);
            }
        }
        y += 4;
    }
    b.x = 300.0f - brain->threshold * 20.0f;
    a.x = b.x;
    a.y = ytop;
    b.y = y;
    gif_Line(&a.x, &b.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &c2, 1);
    a.x = b.x = 260;
    a.y = ytop;
    b.y = y;
    gif_Line(&a.x, &b.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &c1, 1);
    a.x = b.x = 220;
    a.y = ytop;
    b.y = y;
    gif_Line(&a.x, &b.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &c0, 1);
    gif_EndPacket();
}

static int debug_MakeBarString(char *p, int a, int b, FR fr, long long x, int line)
{
    char buf[16];
    int i;
    int len;

    len = strlen(p);
    if (len == 0) {
        return 0;
    }
    barString[0] = 0;
    for (i = 0; i < len; i++, p++) {
        if (*p == '$') {
            switch (p[1]) {
            case 'P':
                sprintf(buf, "%d", a);
                strcat(barString, buf);
                break;
            case 'T':
                sprintf(buf, "%d", b);
                strcat(barString, buf);
                break;
            }
            p++;
            i++;
        } else {
            buf[0] = *p;
            buf[1] = 0;
            strcat(barString, buf);
        }
    }
    if (strlen(barString) != 0 && (debug_font_flag & 1)) {
        debug_Printf((int)(x + 0x148), fr.y + line * 7 + (fr.h + 0x71), 0xFFFFFF00u, barString);
    }
    return strlen(barString);
}

/* the profiler bar's backdrop, scale and label colours */
static DbgCol barBackCol[1] = {{0x00, 0x00, 0x40, 0x20}}; /* derived name */

static DbgCol barScaleCol[1] = {{0xA0, 0xA0, 0xA0, 0xA0}}; /* derived name */

static DbgCol barLabelCol[1] = {{0x80, 0x80, 0x80, 0x60}}; /* derived name */

/* clang-format off */
static void debug_DrawBar(void)
{
    long long sh;
    long long x = -256;
    FR rect0 = {-257, 93, 514, 10};
    DbgCol col1 = barBackCol[0];
    DbgCol col2 = barScaleCol[0];
    DbgCol col3 = barLabelCol[0];
    FR rect1 = {0, 100, 0, 2};
    DbgVtx v0, v1;
    int flip;
    float scale;
    int i;
    int w;
    int len;
    inline int barTime(void) { return debugBars[i].count; } /* derived name */ /* the time stamp of bar i, read by both bar loops */


    flip = 0;
    scale = 1.0f / (270000.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));









    if (debug_font_flag & 1) debug_Printf(ScreenWidth / 2 - 298, ScreenHeight / 2 + 92, 0xFFFFFF00u, "draw");
    if (debug_font_flag & 1) { debug_Printf(ScreenWidth / 2 - 282, ScreenHeight / 2 + 102, 0xFFFFFF00u, "cpu"); debugBars[debug_debug_bar_start_item].count = debugBars[debug_debug_bar_start_item].count; }
    /* the first set of sh on the next line is overwritten before any read, and the store-back after the "cpu" print writes a bar's count back unchanged: both are dead */
    sh = debugBars[debug_debug_bar_start_item].count;
    sh = debug_debug_bar_multiply;

    gif_StartPacketPri(12);
    gif_SetDrawEnviroment(2048, 0, ScreenWidth, ScreenHeight, 1, 0);

    gif_SetZWrite(0);
    gif_SetZTest(0);


    gif_SetAlpha(1, 2, 64);
    gif_Sprite(&rect0, 0xFFFFFFFFU, 0, &col1, 1);
    v0.x = v1.x = -257;
    v0.y = 92;
    v1.y = 104;
    gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col1, 1);
    v0.x = v1.x = 257;
    v0.y = 92;
    v1.y = 104;
    gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col1, 1);
    if (sh > 0) {
        gif_SetAlpha(1, 4, 64);
        v0.y = 91;
        v1.y = 105;
        for (i = 0; i < (1 << sh) - 1; i++) {
            v0.x = v1.x = (i + 1) * (512 / (1 << sh)) - 256;
            gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col2, 1);
        }
    }

    for (i = debug_debug_bar_start_item; i < debugBarCount && x < 320; i++) {

        int t = barTime() - (debug_debug_bar_start_item != 0 ? debugBars[debug_debug_bar_start_item - 1].count : 0);

        long long alpha;

        DbgCol c0, c1;
        if (sh > 0)
            w = -256 + ((int)((float)(t << 9) * scale) >> sh);
        else
            w = -256 + (float)((t << -sh) << 9) * scale;
        if (w > 320) w = 320;
        alpha = (signed char)debugBars[i].col.a >= 0;
        rect1.x = x;
        rect1.w = w - x;
        gif_SetAlpha(alpha, 2, debugBars[i].col.a);
        if (i == debug_debug_bar_start_item) {
            static unsigned char barBlink = 0; /* derived name */
            c0 = debugBars[i].col;






            c1 = (DbgCol){(c0.r + 255) >> 1, (c0.g + 255) >> 1, (c0.b + 255) >> 1, 0};
            c0 = c1;
            c0.r >>= 1;
            c0.g >>= 1;
            c0.b >>= 1;
            gif_Sprite(&rect1, 0xFFFFFFFFU, 0, (barBlink & 1) ? &c1 : &c0, alpha);

            barBlink++;
        } else {
            gif_Sprite(&rect1, 0xFFFFFFFFU, 0, &debugBars[i].col, alpha);
        }
        if (debug_bar_flag == 2) {
            v0.x = w; v0.y = rect0.y;
            v1.x = w; v1.y = rect0.y + rect0.h + 6;
            gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &debugBars[i].col, alpha);
        }

        if (debug_font_flag & 1) {
            len = strlen(debugBars[i].name);
            if (len != 0) {
                if (debug_bar_flag == 1) {
                    v0.x = w; v0.y = rect0.y;
                    v1.x = w; v1.y = rect0.y + rect0.h + 6;
                    gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &debugBars[i].col, alpha);
                }
                v0.x = w; v0.y = rect0.y + rect0.h + 7;
                v1.x = w + 7; v1.y = rect0.y + (flip * 7 + 1) + 8 + rect0.h - 1;
                gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col3, alpha);
                v0.x = w + 7; v0.y = rect0.y + (flip * 7 + 1) + 8 + rect0.h - 1;
                v1.x = w + 8 + len * 8; v1.y = rect0.y + (flip * 7 + 1) + 8 + rect0.h - 1;
                gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col3, alpha);
                flip ^= 1;
            }
        }

        x = w;
    }

    gif_SetAlpha(1, 4, 64);
    v0.x = -256; v0.y = 98;
    if (sh > 0) {
        v1.x = ((int)((float)(drawTimerCount << 9) * scale) >> sh) - 256;
    } else {
        v1.x = ((int)((float)(drawTimerCount << 9) * scale) << -sh) - 256;
    }
    v1.y = 98;
    if (v1.x > 320) v1.x = 320;
    col1.r = col1.g = col1.b = col1.a = 164;
    gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col1, 1);


    v0.x = -256; v0.y = 94;
    v1.x = (used_dma_memory << 9) / 100 - 256; v1.y = 94;
    col1.r = 0;
    col1.g = col1.b = col1.a = 164;
    gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col1, 1);


    v0.x = -256; v0.y = 96;
    v1.x = (used_dl_max << 9) / 100 - 256; v1.y = 96;
    col1.b = 0; col1.r = col1.g = col1.a = 164;
    gif_Line(&v0.x, &v1.x, 0xFFFFFFFFU, 0xFFFFFFFFU, &col1, 1);

    gif_SetZTest(1);
    gif_SetZWrite(1);
    gif_EndPacket();

    flip = 0;
    for (i = debug_debug_bar_start_item; i < debugBarCount && x < 320; i++) {
        int t = barTime() - (debug_debug_bar_start_item != 0 ? debugBars[debug_debug_bar_start_item - 1].count : 0);

        int dpct;
        int pct;
        if (sh > 0)
            w = -256 + ((int)((float)(t << 9) * scale) >> sh);
        else
            w = -256 + (float)((t << -sh) << 9) * scale;
        if (w > 320) w = 320;
        if (i > 0)
            dpct = (float)((debugBars[i].count - debugBars[i - 1].count) * 10000) * scale;
        else dpct = 0;
        pct = (float)(t * 10000) * scale;
        len = debug_MakeBarString((char *)&debugBars[i], dpct, pct, rect0, w, flip);
        if (len != 0) flip ^= 1;
    }

    for (i = 0; i < used_dl_num; i++) {
        char buf[16];
        sprintf(buf, "%d", i);
        v0.x = (used_dl_memory[i] << 9) / 100 - 256 + ScreenWidth / 2;
        v0.y = ScreenHeight / 2 + 88;
        if (debug_font_flag & 1) debug_Printf(v0.x, v0.y, 0xFFFFFF00u, buf);
    }

    if (debug_debug_bar_start_item < debugBarCount) {
        static float barMax = 0.0f, barMin = 0.0f; /* derived name */
        static int barItem = 0, barLine = 0;      /* derived name */
        float val;
        int line;
        val = (float)(debugBars[debug_debug_bar_start_item].count - (debug_debug_bar_start_item != 0 ? debugBars[debug_debug_bar_start_item - 1].count : 0)) * 100.0f * scale;


        line = debugBars[debug_debug_bar_start_item].line;
        if (barItem != debug_debug_bar_start_item || line != barLine)
            barMax = barMin = val;
        else {
            if (barMax < val) barMax = val;
            if (val < barMin) barMin = val;
        }
        if (debug_font_flag & 1) debug_Printf(10, ScreenHeight / 2 - 28, 0xFFFFFF00u, "A:%p W:%1.2f%% W~%1.2f%% W_:%1.2f%%",
                         debugBars[debug_debug_bar_start_item].line, val, barMax, barMin);


        barItem = debug_debug_bar_start_item;
        barLine = line;
    }

    if (debug_font_flag & 1) debug_Printf(ScreenWidth + 70, ScreenHeight / 2 - 14, 0xFFFFFF00u,
                         (sh > 0 ? "/%d" : "x%d"), sh > 0 ? (1 << sh) : (1 << -sh));
    if (debug_font_flag & 1) debug_Printf(ScreenWidth + 30, ScreenHeight / 2 - 24, 0xFFFFFF00u, "%.2f%%",
                         sh > 0 ? (float)(100 << sh) : (float)(10000 >> -sh) * 0.01f);
}

inline void debug_SetBar(char *name, unsigned int col, char *file, int line)
{
    DebugBar *p = &debugBars[debugBarCount];
    if (debug_profile_type == 0 && debugBarCount != 0x400) {
        sprintf(p->name, "%8s", name);
#ifdef ICO_HOST
        p->count = 0; /* no EE timer 0 on the host */
#else
        p->count = *T0_COUNT;
#endif
        p->col.r = col >> 24;
        p->col.g = col >> 16;
        p->col.b = col >> 8;
        p->col.a = col;
        p->file = file;
        p->line = line;
        debugBarCount++;
    }
}

/* debug_SetBar with the inverted debug_profile_type gate (records only while the
   profiler flag is set). */
inline void debug_SetBar2(char *name, unsigned int col, char *file, int line)
{
    DebugBar *p = &debugBars[debugBarCount];
    if (debug_profile_type != 0 && debugBarCount != 0x400) {
        sprintf(p->name, "%8s", name);
#ifdef ICO_HOST
        p->count = 0; /* no EE timer 0 on the host */
#else
        p->count = *T0_COUNT;
#endif
        p->col.r = col >> 24;
        p->col.g = col >> 16;
        p->col.b = col >> 8;
        p->col.a = col;
        p->file = file;
        p->line = line;
        debugBarCount++;
    }
}

void debug_DispBar(void)
{
    float inv;
    int va;
    int vb;
    int n;
    inv = 1.0f / (270000.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
    vb = (float)(debugBars[debugBarCount - 1].count * 100) * inv;
    va = (float)(drawTimerCount * 100) * inv;

    if (debug_font_flag2 != 0 || (debug_font_flag & 1) != 0)
        debug_Printf(10, 10, 0xFFFFFF00u, "C%03d G%03d F%d", vb, va, frame_count);
    if (debug_brain_bar_flag != 0)
        debug_brainBar();
    if (debug_bar_flag != 0)
        debug_DrawBar();
    if (strips != 0 || texturetranssize != 0) {
        n = 1;
        if (strips != 0)
            n = strips;
        if (debug_font_flag2 != 0 || (debug_font_flag & 1) != 0)
            debug_Printf(10, 20, 0xFFFFFF00u, "P%d S%d A%d(%d) T%d(%d)", polygons, strips,
                         polygons / n, packets, texturetranssize / 1024, textures);
    }
}

inline void debug_ResetBar(void)
{
    texregs = 0;
    *T0_COUNT = 0;
    textures = 0;
    packets = 0;
    strips = 0;
    polygons = 0;
    texturetranssize = 0;
    debugBarCount = 0;
}

/* Halves a 32-bit snapshot with a 2x2 box filter; the clamp and the
   destination helper are nested functions. */
static void debug_ResizeSnapShot(int dst, int src, int w, int h)
{
    int r, g, b, a;
    int x, y, i, j;
    int row;
    unsigned char *p;

    inline unsigned int *spix(int px)
    {
        return (unsigned int *)(px * 4 + src);
    }
    inline int clip(int v)
    {
        return v < 256 ? (v > -1 ? v : 0) : 255;
    }
    inline unsigned int *dpix(int px, int py)
    {
        return (unsigned int *)((py * ScreenWidth / 2 + px) * 4 + dst);
    }

    for (y = 0; y < h; y += 2) {
        for (x = 0; x < w; x += 2) {
            /* clang-format off */
            r = 0; g = 0; b = 0; a = 0;
            /* clang-format on */
            for (i = 0; i < 2; i++) {
                /* clang-format off */
                for (j = 0, row = (y + i) * ScreenWidth, p = (unsigned char *)(spix(x) + row); j < 2; j++) {
                    /* clang-format on */
                    r = r + p[0] * 0.25f;
                    g = g + p[1] * 0.25f;
                    b = b + p[2] * 0.25f;
                    a = a + p[3] * 0.25f;
                    p += 4;
                }
            }
            r = clip(r);
            g = clip(g);
            b = clip(b);
            a = clip(a);
            *dpix(x / 2, y / 2) = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
    FlushCache(0);
}

static inline void debug_WriteTim2(int fd, int *img, int w, int h) /* derived name */
{
    Tim2FileHdr fh;
    Tim2PicHdr ph;
    int y;

    fh.id[0] = 'T';
    fh.id[1] = 'I';
    fh.id[2] = 'M';
    fh.id[3] = '2';
    fh.ver = 4;
    fh.fmt = 0;
    fh.nPictures = 1;
    fh.pad = 0;
    ph.imageSize = (w * h) * 4;
    ph.clutSize = 0;
    ph.headerSize = 48;
    ph.clutColors = 0;
    ph.imageType = 0;
    ph.mipMapTextures = 1;
    ph.clutType = 0;
    ph.imageColorType = 3;
    ph.gsTex0 = 0;
    ph.gsTex1 = 0;
    ph.gsRegs = 0;
    ph.gsTexClut = 0;
    ph.totalSize = ph.imageSize + ph.clutSize + (unsigned short)ph.headerSize;
    ph.imageWidth = w;
    ph.imageHeight = h;

    sceWrite(fd, &fh, 16);
    sceWrite(fd, &ph, 48);

    for (y = 0; y < h; y++) {
        sceWrite(fd, img + y * w, w * 4);
    }
}

static void debug_WriteBMP(int fd, int w, int h, unsigned int *src)
{
    unsigned char line[w * 3];
    BmpHeader hdr;
    unsigned char *s;
    unsigned char *d;
    int x;
    int y;
    hdr.pad[0] = 0;
    hdr.pad[1] = 0;
    hdr.bfType[0] = 0x42;
    hdr.bfType[1] = 0x4D;
    hdr.bfOffBits = 0x36;
    hdr.biSize = 0x28;
    hdr.biPlanes = 1;
    hdr.biBitCount = 24;
    hdr.biCompression = 0;
    hdr.biXPelsPerMeter = 0;
    hdr.biYPelsPerMeter = 0;
    hdr.biClrUsed = 0;
    hdr.biClrImportant = 0;
    hdr.biWidth = w;
    hdr.biHeight = h;
    hdr.biSizeImage = w * h * 3;
    hdr.bfSize = hdr.biSizeImage + 0x36;
    sceWrite(fd, hdr.bfType, 0x36);
    for (y = h - 1; y >= 0; y--) {
        s = (unsigned char *)&src[y * w];
        d = line;
        for (x = 0; x < w; x++, d += 3) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            s += 4;
        }
        sceWrite(fd, line, w * 3);
    }
}

/* debug_SnapShot's first-call flag */
static int snapFirst = 1; /* derived name */

int debug_SnapShot(int idx)
{
    sceGsStoreImage si;
    char name[256];
    int size;
    int mask;
    unsigned int i;
    int fd;
    int *src;
    int *dst;
    int *buf;
    unsigned int w;
    int h;
    unsigned int y;

    size = ScreenWidth * ScreenHeight * 4;
    if (debug_snapshot_size == 0) {
        return -1;
    }
    mask = 1 << (debug_snapshot_size - 1);
    debugBackGroundDisableFlag = 1;
    if (snapFirst != 0) {
        if (iosCdvdBackGroundMgrRunning != 0) {
            return -1;
        }
        snapFirst = 0;
    }
    sceGsSyncPath(0, 0);
    debug_StdPrintfDummy("Snap:%d:%p\n", idx, 0x2000000);
    sceGsSetDefStoreImage(&si, 0x800, ScreenWidth / 64, 0, 0, 0, ScreenWidth, ScreenHeight);
    FlushCache(0);
    sceGsExecStoreImage(&si, (void *)0x2000000);
    sceGsSyncPath(0, 0);
    src = (int *)0x2000000;
    FlushCache(0);
    if (debug_snapshot_size < 5) {
        if (debug_snapshot_size > 0) {
            w = ScreenWidth;
            h = ScreenHeight;
            buf = (int *)(0x2000000 + size);
            dst = buf + ((idx / mask) * (w * mask) + idx % mask);
            for (y = 0; y < h; y++) {
                for (i = 0; i < w; i++) {
                    *dst = *src++;
                    dst += mask;
                }
                dst += (mask - 1) * (mask * w);
            }
            if (idx == mask * mask - 1) {
                for (i = 0;; i++) {
                    if (debug_snapshot_format == 0) {
                        sprintf(name, "snapshot/snap%07d.tm2", i);
                    } else {
                        sprintf(name, "snapshot/snap%07d.bmp", i);
                    }
                    fd = debugSceOpen(name, 1);
                    debugSceClose(fd);
                    if (fd < 0) {
                        break;
                    }
                }
                if (debug_snapshot_format == 0) {
                    sprintf(name, "snapshot/snap%07d.tm2", i);
                } else {
                    sprintf(name, "snapshot/snap%07d.bmp", i);
                }
                fd = debugSceOpen(name, 0x602);
                if (debug_snapshot_format == 0) {
                    debug_WriteTim2(fd, buf, w * mask, h * mask);
                } else {
                    debug_WriteBMP(fd, w * mask, h * mask, (unsigned int *)buf);
                }
                if (debugSceClose(fd) < 0) {
                    debug_StdPrintfDummy("debug_SnapShot:host file close error.\n");
                }
            }
        }
    }
    dma_init();
    return 1;
}

void debug_DispQW(void *p, int size)
{
    int isf = 0;
    int i;
    int j;

    switch (size) {
    case 0:
        isf = 1;
        size = 4;
        debug_StdPrintfDummy("(addr 0x%08x <fl>) : ", p);
        break;
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
        debug_StdPrintfDummy("(addr 0x%08x <%2d>) : ", p, size);
        break;
    default:
        return;
    }
    for (i = 0; i < 16 / size; i++) {
        if (isf == 0) {
            for (j = 16 / (16 / size) - 1; j >= 0; j--) {
                debug_StdPrintfDummy("%02x", ((unsigned char *)p)[i * size + j]);
            }
            debug_StdPrintfDummy(" ");
        } else {
            debug_StdPrintfDummy("%12f ", ((float *)p)[i]);
        }
    }
    debug_StdPrintfDummy("\n");
}

inline void debug_DispMatrix(int *m)
{
    int *p = m;
    int i;
    for (i = 3; i >= 0; i--) {
        debug_DispQW(p, 0);
        p = (int *)((char *)p + 0x10);
    }
}

void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    char buf[256];
    void *args = (char *)__builtin_next_arg(fmt) - 0x20;
    vsprintf(buf, fmt, args);
    debug_PrintFont(a, b, c, buf);
}

void debug_Printf2(int a, int b, unsigned int c, const char *fmt, ...)
{
    char buf[256];
    void *args = (char *)__builtin_next_arg(fmt) - 0x20;
    vsprintf(buf, fmt, args);
    debug_PrintFont(a, b, c, buf);
}

void debug_PrintFontWindow(int col, const char *fmt, ...)
{
    char buf[256];
    char *p = buf;
    int nl = 0;
    int i;

    vsprintf(buf, fmt, (char *)__builtin_next_arg(fmt) - 0x30);
    if (buf[0] == '\n') {
        fontWindowLine++;
        p = &buf[1];
    }
    if (fontWindowLine == charNumH) {
        for (i = 0; i < charNumH - 1; i++) {
            fontLines[i] = fontLines[i + 1];
        }
        fontWindowLine--;
        fontLines[charNumH - 1].text[0] = 0;
    }
    if (p[strlen(p) - 1] == '\n') {
        p[strlen(p) - 1] = 0;
        nl = 1;
    }
    strncpy(fontLines[fontWindowLine].text, p, 50);
    fontLines[fontWindowLine].col = col;
    if (nl) {
        fontWindowLine++;
    }
    if (fontWindowLine == charNumH) {
        for (i = 0; i < charNumH - 1; i++) {
            fontLines[i] = fontLines[i + 1];
        }
        fontWindowLine--;
        fontLines[charNumH - 1].text[0] = 0;
    }
}

inline void debug_ResizeFontWindowHeight(int val)
{
    charNumH = val;
}

inline void debug_SetBarDummy(void) {}

void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...) {}

void debug_PrintFontWindowDummy(int col, int fmt, ...) {}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_PrintFontf(int x, int y, char *p, ...)
{
    char *d;
    char *va;
    char *f;
    char c;
    float v;
    d = fontfLine;
    va = (char *)__builtin_next_arg(p) - 0x28;
    if (*p == 0) {
        *d = 0;
        return;
    }
    do {
        c = *p;
        if (c == '\n') {
            *d = 0;
            if (debug_font_flag & 1) {
                debug_Printf(x, y, 0xFFFFFF00u, fontfLine);
            }
            d = fontfLine;
            y += 8;
        } else if (c != '%') {
            *d = *p;
            d++;
        } else {
            p++;
            switch (*p) {
            case 'd':
                va += 8;
                f = "%d";
                d += sprintf(d, f, *(int *)(va - 8));
                break;
            case 'x':
                va += 8;
                f = "%x";
                d += sprintf(d, f, *(int *)(va - 8));
                break;
            case 'f':
                va += 8;
                v = dptofp(*(double *)(va - 8));
                f = "%f";
                d += sprintf(d, f, v);
                break;
            default:
                f = "debug_PrintFontf error\n";
                debug_StdPrintfDummy(f);
                break;
            }
        }
        p++;
    } while (*p != 0);
    *d = 0;
}

void debug_PrintMatrix(float *arg)
{
    int i;
    for (i = 3; i >= 0; i--) {
        debug_StdPrintfDummy("%f %f %f %f\n", arg[0], arg[1], arg[2], arg[3]);
        arg += 4;
    }
    debug_StdPrintfDummy("\n");
}

void debug_DispVu1FReg(int no, int mode)
{
    int i;
    float f[4];
    int buf[4];
    if (mode != 0) {
        if (no >= 0) {
            __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(no * 16 + 0x400));
            __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
            __asm__ __volatile__("sqc2 $vf2, 0(%0)" : : "r"(f) : "memory");
            debug_StdPrintfDummy("VF%02d:%f %f %f %f\n", no, f[0], f[1], f[2], f[3]);
        } else {
            __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(0x400));
            for (i = 0; i < 32; i++) {
                __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
                __asm__ __volatile__("sqc2 $vf2, 0(%0)" : : "r"(f) : "memory");
                debug_StdPrintfDummy("VF%02d:%f %f %f %f\n", i, f[0], f[1], f[2], f[3]);
            }
        }
    } else {
        if (no >= 0) {
            __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(no * 16 + 0x400));
            __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
            __asm__ __volatile__("sqc2 $vf2, 0(%0)" : : "r"(buf) : "memory");
            debug_StdPrintfDummy("VF%02d:%8x %8x %8x %8x\n", no, buf[0], buf[1], buf[2], buf[3]);
        } else {
            __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(0x400));
            for (i = 0; i < 32; i++) {
                __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
                __asm__ __volatile__("sqc2 $vf2, 0(%0)" : : "r"(buf) : "memory");
                debug_StdPrintfDummy("VF%02d:%8x %8x %8x %8x\n", i, buf[0], buf[1], buf[2], buf[3]);
            }
        }
    }
}

inline void debug_DispVu1IReg(int no)
{
    int i;
    int buf[4];
    if (no >= 0) {
        __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(no * 16 + 0x420));
        __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
        __asm__ __volatile__("sqc2 $vf2, %0" : "=m"(buf) : : "memory");
        debug_StdPrintfDummy("VI%02d:%08x %08x %08x %08x\n", no, buf[0], buf[1], buf[2], buf[3]);
    } else {
        __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(0x420));
        for (i = 0; i < 16; i++) {
            __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
            __asm__ __volatile__("sqc2 $vf2, %0" : "=m"(buf) : : "memory");
            debug_StdPrintfDummy("VI%02d:%08x %08x %08x %08x\n", i, buf[0], buf[1], buf[2], buf[3]);
        }
    }
}

inline void debug_DispVu1SReg(int no)
{
    int i;
    int buf[4];
    if (no >= 0) {
        __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(no * 16 + 0x420));
        __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
        __asm__ __volatile__("sqc2 $vf2, %0" : "=m"(buf) : : "memory");
        debug_StdPrintfDummy("VS%02d:%08x %08x %08x %08x\n", no, buf[0], buf[1], buf[2], buf[3]);
    } else {
        __asm__ __volatile__("ctc2.ni %0, $vi1" : : "r"(0x420));
        for (i = 0; i < 16; i++) {
            __asm__ __volatile__("vlqi.xyzw $vf2, ($vi1++)");
            __asm__ __volatile__("sqc2 $vf2, %0" : "=m"(buf) : : "memory");
            debug_StdPrintfDummy("VS%02d:%08x %08x %08x %08x\n", i, buf[0], buf[1], buf[2], buf[3]);
        }
    }
}

inline int gsResetFunc(int val)
{
    gsb_Init(&db);
    return 1;
}

/* the value names debug_Mode logs a two-state option with */
static char *modeValueName[] = {"Off", "On"}; /* derived name */

/* debug_Mode's cursor and blink counter */
static int modeSelect = 0; /* derived name */

static unsigned char modeBlink = 0; /* derived name */

/* the colours the cursor line blinks between */
static const unsigned int blinkColor[] = {0xFFFFFF00, 0xC0C0C000}; /* derived name */

/* clang-format off */
static int debug_Mode(void)
{
    int i, end, j;
    int ret = 0;

    modeBlink++;

    debug_PrintfDummy(10, 50, 0xFFFFFF00u, "DEBUG MODE");
    debug_PrintfDummy(138, 50, 0x00FFFF00u, "Push '\202' to save debug options.");

    i = (modeSelect + 70) % 76;
    end = (modeSelect + 83) % 76;
    j = 1;
    while (i != end) {
        if (debugOption[i].strs != 0) {
            debug_PrintfDummy(18, j * 8 + 50, modeSelect == i ? (((modeBlink >> 3) & 1) ? blinkColor[0] : debugOption[i].col) : debugOption[i].col, "%c%s : %s(%d)", modeSelect == i ? 62 : 32, debugOption[i].name, debugOption[i].strs[*debugOption[i].val - debugOption[i].min], *debugOption[i].val);
        } else {
            debug_PrintfDummy(18, j * 8 + 50, modeSelect == i ? (((modeBlink >> 3) & 1) ? blinkColor[0] : debugOption[i].col) : debugOption[i].col, "%c%s : %d", modeSelect == i ? 62 : 32, debugOption[i].name, *debugOption[i].val);
        }
        if (++i == 76) i = 0;
        j++;
    }

    if (pad[0].rep & 0x4000) {
        if (++modeSelect >= 76) modeSelect = 0;
    }
    if (pad[0].rep & 0x1000) {
        if (--modeSelect < 0) modeSelect = 75;
    }
    if (pad[0].rep & 0x2000) {
        if (++*debugOption[modeSelect].val > debugOption[modeSelect].max)
            *debugOption[modeSelect].val = debugOption[modeSelect].min;
        if (debugOption[modeSelect].func != 0)
            debugOption[modeSelect].func(*debugOption[modeSelect].val);
    }
    if (pad[0].rep & 0x8000) {
        if (--*debugOption[modeSelect].val < debugOption[modeSelect].min)
            *debugOption[modeSelect].val = debugOption[modeSelect].max;
        if (debugOption[modeSelect].func != 0)
            debugOption[modeSelect].func(*debugOption[modeSelect].val);
    }
    if (pad[0].flags & 0x20) {
        for (i = 0; i < 76; i++) {
            if (debugOption[i].min == 0 && debugOption[i].max == 1)
                debug_StdPrintfDummy("debug%s => %s\n", debugOption[i].name, modeValueName[*debugOption[i].val]);
            else
                debug_StdPrintfDummy("debug%s => %d\n", debugOption[i].name, *debugOption[i].val);
        }
        ret = 1;
    }
    if (pad[0].flags & 0x40) ret = -1;
    if (pad[0].flags & 0x10) debug_SaveDebugOptionFile();
    if (ret != 0) modeSelect = 0;

    return ret;
}

int debug_SelectCsvWindowVal(char *title, int x, int y, int rows, int count, int *psel,
                             int (*fn)(int, int), int arg)
{
    char buf[count][37];
    int i;
    for (i = 0; i < count; i++) {
        if (fn != 0) {
            int r = fn(i, arg);
            sprintf(buf[i], "%3d %s", i, r);
        } else {
            sprintf(buf[i], "%3d", i);
        }
        if ((unsigned int)strlen(buf[i]) >= 0x26) {
            buf[i][0x24] = 0;
            debug_StdPrintfDummy("debug_SelectCsvWindowVal: func return string length over\n");
        }
    }
    return debug_SelectCsvWindow(title, x, y, rows, buf, 37, 0, 0, count, psel);
}

/* the csv window's scroll counter */
static int csvScroll = 0; /* derived name */

inline int _debug_SelectCsvWindow(char *title, int x, int y, int rows, int base, int stride, int off,
                           int deref, int n, int *psel, void (*getline)(), int (*colfunc)(int))
{
    char buf[256];
    int sel;
    int i;
    int half;
    int k;
    int top;
    int yy;
    int len;
    int v;
    int col;

    sel = *psel;
    debug_PrintfDummy(x, y, 0xFFFFFF00u, "%s", title);
    if ((pad[0].now & 2) == 0) {
        if (pad[0].flags & 0x80) {
            debug_font_flag ^= 2;
        }
        if (sel >= n) {
            sel = n - 1;
        }
        if (pad[0].rep & 0x4000) {
            sel++;
            if (sel >= n) {
                sel = 0;
            }
        }
        if (pad[0].rep & 0x1000) {
            sel--;
            if (sel < 0) {
                sel = n - 1;
            }
        }
        if (n < rows) {
            rows = n;
        }
        half = (int)(((float)rows - 0.5f) * 0.5f);
        if (sel < half) {
            k = sel;
        } else if (n - (rows - half) < sel) {
            k = rows - (n - sel);
        } else {
            k = half;
        }
        top = sel - k;
        yy = y + 8;
        for (i = top; i < top + rows; i++) {
            if (i - top == k) {
                col = 0xFF404000;
            } else if (colfunc == 0) {
                col = 0xFFFFFF00;
            } else {
                col = colfunc(i);
            }
            v = base + stride * i + off;
            if (deref == 1) {
                v = *(int *)v;
            }
            len = csvScroll;
            getline(buf, i, v);
            if (len >= 2) {
                if (len >= 0x100) {
                    len = 0xFF;
                }
                buf[len - 1] = -110;
                buf[len] = 0;
            } else {
                buf[0] = 0;
            }
            debug_PrintfDummy(x, yy, col, "  %s", buf);
            yy += 8;
        }
        if (csvScroll <= 0xFFFE) {
            csvScroll += systemStatus[1];
        }
        *psel = sel;
        if (pad[0].flags & 0x20) {
            csvScroll = 0;
            return 1;
        } else if (pad[0].flags & 0x140) {
            csvScroll = 0;
            return -1;
        }
    }
    return 0;
}

static void getLineBuffer(char *buf, int line, char *str)
{
    sprintf(buf, "%02d:%s", line, str);
}

inline int debug_SelectCsvWindowWithLine(char *title, int x, int y, int rows, const void *base, int stride,
                                  int off, int deref, int n, int *psel)
{
    return _debug_SelectCsvWindow(title, x, y, rows, (int)base, stride, off, deref, n, psel,
                                      getLineBuffer, 0);
}

inline int debug_SelectCsvWindowWithLineColor(char *title, int x, int y, int rows, const void *base, int stride,
                                       int off, int deref, int n, int *psel, int (*colfunc)(int))
{
    return _debug_SelectCsvWindow(title, x, y, rows, (int)base, stride, off, deref, n, psel,
                                      getLineBuffer, colfunc);
}

static void getBuffer(char *buf)
{
    sprintf(buf, "%s");
}

int debug_SelectCsvWindow(char *title, int x, int y, int rows, const void *base, int stride, int off,
                          int deref, int n, int *psel)
{
    return _debug_SelectCsvWindow(title, x, y, rows, (int)base, stride, off, deref, n, psel,
                                      getBuffer, 0);
}

static int debug_SelectStageMain(int ret, int stage)
{
    if (systemStatus[6] != 0) {
        return 0;
    }
    if (ret > 0) {
        debug_StdPrintfDummy("stage:%d\n", stage);
        if (stage != 0) {
            int on = 1;
            if (strstr(stageData[stage].dataFile, "NOCD_") == 0) {
                mpegPlayReturnStage = stage_no;
                soundDataSegAllClose(0, 2);
                seEnvForceClose = on;
                enable_game_pause = on;
                kanbanInit(0);
                gflagOn(394);
                scpBoyControlReadDisable = 0;
                stgmgrForceSwitch(stage);
                gflagOff(388);
                ACTGame_SetActors_Debug(stage, on);
            }
        }
    }
    return ret;
}

int debug_SelectStage(void)
{
    return debug_SelectStageMain(debug_SelectCsvWindowWithLine("stage select", 10, 80, 11, stageData,
                                                               404, 0x20, 0, 106, &stageSelectNo),
                                 stageSelectNo);
}

static inline int debug_mcConfirm(char *msg) /* derived name */
{
    int yes = 0;
    debug_PrintfDummy(80, 70, 0xFFFFFF00u, "%s? Yes:O No:X", msg);
    if (pad[0].flags & 0x20) {
        yes = 1;
    }
    return (pad[0].flags & 0x40) ? -1 : yes;
}

/* the sibling of debug_mcConfirm that prints an already-formatted message instead of a fixed prompt. */
static inline int debug_mcAsk(char *msg) /* derived name */
{
    int yes = 0;
    debug_PrintfDummy(80, 70, 0xFFFFFF00u, "%s", msg);
    if (pad[0].flags & 0x20) {
        yes = 1;
    }
    if (pad[0].flags & 0x40) {
        yes = -1;
    }
    return yes;
}

static int formatState = 0; /* derived name */

static int formatBlink = 0; /* derived name */

inline int debug_mcFormat(int port)
{
    int r;
    switch (formatState) {
    case 0:
        r = debug_mcConfirm("format");
        if (r != 1) {
            return r;
        }
        iosMcFormat(port);
        formatState++;
        break;
    case 1:
        if (formatBlink++ & 0x10) {
            debug_PrintfDummy(120, 70, 0xFFFFFF00u, "now formatting");
        }
        if (iosMcSync(port) != 0) {
            formatState++;
        }
        break;
    default:
        formatState = 0;
        return 1;
    }
    return 0;
}

static int unformatState = 0; /* derived name */

static int unformatBlink = 0; /* derived name */

inline int debug_mcUnformat(int port)
{
    int r;
    switch (unformatState) {
    case 0:
        r = debug_mcConfirm("Unformat");
        if (r != 1) {
            return r;
        }
        iosMcUnformat(port);
        unformatState++;
        break;
    case 1:
        if (unformatBlink++ & 0x10) {
            debug_PrintfDummy(120, 70, 0xFFFFFF00u, "now unformatting");
        }
        if (iosMcSync(port) != 0) {
            unformatState++;
        }
        break;
    default:
        unformatState = 0;
        return 1;
    }
    return 0;
}

static int debug_mcRetErrCheck(McMgr *mc)
{
    char buf[64];
    int r;
    if (mc->result >= 0) {
        return 1;
    }
    switch (mc->result) {
    case 0:
        r = 1;
        break;
    case -9:
    case -2:
        sprintf(buf, "not insert memory card or unformatted %d", mc->result);
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    case -4:
        sprintf(buf, "%s file not found", mc->path);
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    case -14:
        sprintf(buf, "%s Directory not found", mc->dirName);
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    case -16:
        sprintf(buf, "segID %d check sum err rom:%d != load:%d", mc->segment, mc->readSum, mc->sum);
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    case -15:
        sprintf(buf, "%s handler func ret err code", mc->path);
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    case -10:
        sprintf(buf, "memory over");
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    default:
        sprintf(buf, "memory card another err %d", mc->result);
        r = debug_mcAsk(buf) ? -1 : 0;
        break;
    }
    return r;
}

static int selectFileState = 0; /* derived name */

static int debug_selectFile(McMgr *mc)
{
    int i;
    int r = 0;
    int ret = 0;

    switch (selectFileState) {
    case 0:
        mc->flags.ll &= ~2;
        iosMcChdirProduct(mc);
        selectFileState++;
        break;
    case 1:
    case 4:
        if (iosMcSync(mc)) {
            selectFileState++;
        }
        break;
    case 2:
    case 5:
        if ((ret = debug_mcRetErrCheck(mc)) != 0) {
            selectFileState++;
        }
        break;
    case 3:
        *(McPat *)mc->path = *(McPat *)"*";
        iosMcGetDir(mc);
        selectFileState++;
        break;
    case 6:
        for (i = 0; i < mc->dirCount; i++) {
            debug_StdPrintfDummy("%s %d bytes\n", mc->dir[i].EntryName,
                                 ((McDirEnt *)((char *)mc + (i << 6) + 0x4C0))->size);
        }
        selectFileState++;
        break;
    default:
        debug_SelectCsvWindow("FILE LIST", 0x50, 0x46, 0xA, mc->dir, 0x40, 0x20, 0, mc->dirCount,
                              &mc->fileNo);
        if (pad[0].flags & 0x20) {
            r = 1;
        }
        if (pad[0].flags & 0x40) {
            r = -1;
        }
        break;
    }
    if (ret < 0) {
        r = -1;
    }
    if (r) {
        selectFileState = 0;
    }
    return r;
}

inline void *debug_saveNumFunc(int no, void *mc)
{
    if ((1 << no) & ((McMgr *)mc)->mask) {
        return "SAVED";
    }
    return "NEW";
}

static int saveState = 0; /* derived name */

static int debug_mcSaveMainBlock(McMgr *mc)
{
    int r = 0;
    int ret = 0;

    switch (saveState) {
    case 0:
        *(McName6 *)mc->path = *(McName6 *)"game.";
        iosMcGetBlockSaveInfo(mc);
        saveState++;
        break;
    case 1:
        if (iosMcSync(mc)) {
            saveState++;
        }
        break;
    case 2:
    case 8:
    case 13:
        if ((ret = debug_mcRetErrCheck(mc)) != 0) {
            saveState++;
        }
        break;
    case 3:
        if (mc->dirCount >= 11) {
            debug_StdPrintfDummy("debug_mcSaveMainBlock:既に設定された数以上のデータを保存してる\n");
        }
        r = debug_SelectCsvWindowVal("SAVE NO.", 80, 70, 10, 10, &mc->fileNo,
                                     (int (*)(int, int))debug_saveNumFunc, (int)mc);
        if (r > 0) {
            r = 0;
            saveState++;
        }
        break;
    case 4:
        iosMcSaveIconBlock(mc);
        saveState++;
        break;
    case 6:
        iosMcSaveProductBlock(mc);
        saveState++;
        break;
    case 5:
    case 7:
    case 12:
        debug_PrintfDummy(120, 70, 0xFFFFFF00u, "save %s", mc->path);
        if (iosMcSync(mc)) {
            saveState++;
        }
        break;
    case 9:
        gamesysMemorySave(gameSysMemoryFuncList, gameSysMainSaveBuff, 0);
        iosMcSaveGameBlock(mc, gameSysMainSaveBuff);
        saveState++;
        break;
    default:
        r = 1;
        break;
    }
    if (ret < 0) {
        r = -1;
    }
    if (r) {
        saveState = 0;
    }
    return r;
}

static int loadState = 0; /* derived name */

static int debug_mcLoadMainBlock(McMgr *mc)
{
    int r = 0;
    int ret = 0;

    switch (loadState) {
    case 0:
        *(McName6 *)mc->path = *(McName6 *)"game.";
        iosMcGetBlockSaveInfo(mc);
        loadState++;
        break;
    case 1:
        if (iosMcSync(mc)) {
            loadState++;
        }
        break;
    case 2:
    case 6:
    case 10:
        if ((ret = debug_mcRetErrCheck(mc)) != 0) {
            loadState++;
        }
        break;
    case 3:
        if (mc->dirCount >= 11) {
            debug_StdPrintfDummy("debug_mcLoadMainBlock:既に設定された数以上のデータを保存してる\n");
        }
        r = debug_SelectCsvWindowVal("SAVE NO.", 80, 70, 10, 10, &mc->fileNo,
                                     (int (*)(int, int))debug_saveNumFunc, (int)mc);
        if (r > 0) {
            r = 0;
            loadState++;
        }
        break;
    case 4:
        if (((1 << mc->fileNo) & mc->mask) == 0) {
            loadState = 99;
            break;
        }
        iosMcLoadProductBlock(mc);
        loadState++;
        break;
    case 5:
    case 8:
        debug_PrintfDummy(120, 70, 0xFFFFFF00u, "load %s", mc->path);
        if (iosMcSync(mc)) {
            loadState++;
        }
        break;
    case 7:
        iosMcLoadGameBlock(mc, gameSysMainSaveBuff);
        loadState++;
        break;
    case 9:
        gamesysMemoryLoad(gameSysMemoryFuncList, gameSysMainSaveBuff, 0);
        scpBoyControlReadDisable = 0;
        loadState++;
        break;
    case 99:
        if (debug_mcAsk("DATA NOT FOUND")) {
            loadState = 3;
        }
        break;
    default:
        r = 1;
        break;
    }
    if (ret < 0) {
        r = -1;
    }
    if (r) {
        loadState = 0;
    }
    return r;
}

static int deleteState = 0; /* derived name */

static int debug_mcDeleteFile(McMgr *mc)
{
    char buf[32];
    int ret = 0;
    int r;

    switch (deleteState) {
    case 0:
        ret = debug_selectFile(mc);
        if (ret) {
            deleteState++;
        }
        if (ret > 0) {
            ret = 0;
        }
        break;
    case 1:
        sprintf(buf, "delete %s file", mc->dir[mc->fileNo].EntryName);
        r = debug_mcConfirm(buf);
        if (r > 0) {
            deleteState++;
        } else if (r < 0) {
            deleteState = 0;
        }
        break;
    case 2:
        strcpy(mc->path, mc->dir[mc->fileNo].EntryName);
        iosMcDelete(mc);
        deleteState++;
        break;
    case 3:
        debug_PrintfDummy(120, 70, 0xFFFFFF00u, "delete %s", mc->path);
        if (iosMcSync(mc)) {
            deleteState++;
        }
        break;
    case 4:
        if (debug_mcRetErrCheck(mc)) {
            deleteState++;
        }
        break;
    default:
        ret = 1;
        break;
    }
    if (ret) {
        deleteState = 0;
    }
    return ret;
}

inline int debug_mcTest(void)
{
    iosMcTest();
    return 1;
}

static int mcMenuSelect = 0; /* derived name */

static int mcState = 0; /* derived name */

int debug_MemoryCard(void)
{
    McMenuItem menu[6] = {
        {"LOAD", debug_mcLoadMainBlock}, {"SAVE", debug_mcSaveMainBlock},
        {"DELETE", debug_mcDeleteFile},  {"FORMAT", debug_mcFormat},
        {"UNFORMAT", debug_mcUnformat},  {"TEST", debug_mcTest},
    };
    McTypeMsg tm[3] = {
        {-1, 0x00FFFF00, "Formatted"},
        {-2, 0x00FFFF00, "Unformatted"},
        {0, 0xFF222200, "No card"},
    };
    McTypeMsg *p;
    int r;
    int (*fn)();

    switch (mcState) {
    case 0:
        mc.slot = 0;
        mc.port = 0;
        iosMcGetInfo(&mc);
        mcState++;
        break;
    case 1:
        if (iosMcSync(&mc)) {
            mcState++;
        }
        break;
    case 2:
        p = tm;
        while (mc.cardState != p->type && p->type != 0) {
            p++;
        }
        if (mc.type != 2) {
            p = &tm[2];
        }
        debug_PrintfDummy(10, 60, p->col, "Memory card port 0: %s free:%d Kbytes", p->msg,
                          mc.free);
        if (mc.result >= -2) {
            r = debug_SelectCsvWindow("MENU", 0xA, 0x44, 0xA, menu, 8, 0, 1, 6, &mcMenuSelect);
            if (r == 1) {
                mcState++;
            } else if (r == -1) {
                mcState = 0;
                return -1;
            }
        } else if (pad[0].flags & 0x40) {
            mcState = 0;
            return -1;
        }
        break;
    default:
        fn = menu[mcMenuSelect].fn;
        if (fn != 0) {
            if (fn(&mc) != 0) {
                mcState = 0;
                return 1;
            }
        }
        break;
    }
    return 0;
}

inline int debug_STAFFROLLTest(void)
{
    staffRollStart(1.0f, 0x80);
    return 1;
}

inline int debug_SETest_color(int idx)
{
    return seKind[seDef[idx].kind] != 0 ? 0xFFFFFF00 : 0x80808000;
}

static int seSelect = 0; /* derived name */

static int seHandle = -1; /* derived name */

int debug_SETest(int reset)
{
    int r;

    if (reset != 0) {
        seHandle = -1;
    }
    r = debug_SelectCsvWindowWithLineColor("SE LIST", 0xA, 0x3C, 0xA, seDef, 0x3C, 0,
                                               0, 0x592, &seSelect, debug_SETest_color);
    if (r > 0) {
        seHandle = soundSeDefPlay(seSelect, 0, GOBJ_SUB(boyGObj)->nodeMtx + 0x30, 1);
        return 0;
    }
    if (r < 0) {
        soundSeGroupStop(1);
    }
    return r;
}

void debug_SESlotDisp(void)
{
    unsigned char mask[6];
    char buf[32];
    char tmp[16];
    int i = 0;
    int k;
    int bit;
    int y;
    int col;
    memset(mask, 0, 6);
    for (; i < 48; i++) {
        if (SgGetSlotStatus(0, i) == 2) {
            mask[i / 8] |= 1 << (i % 8);
        }
    }
    for (k = 0; k < 2; k++) {
        col = 0xFFFFFF00;
        if (k != 0) {
            col = 0xFF000000;
        }
        for (i = 0; i < 48; i++) {
            y = i / 8 * 8 + 100;
            bit = i % 8;
            if (bit == 0) {
                buf[0] = 0;
            }
            if (((mask[i / 8] >> bit) & 1) == k) {
                sprintf(tmp, "%2.2d", i);
            } else {
                sprintf(tmp, "  ");
            }
            strcat(buf, tmp);
            if (bit == 7) {
                debug_PrintfDummy(400, y, col, "%s", buf);
            }
        }
    }
}

inline int debug_reverbTest(void)
{
    int depth = soundReverbDepthGet();
    if (pad[0].rep & 0x1000) {
        if (depth <= 99) {
            depth++;
        }
    }
    if (pad[0].rep & 0x4000) {
        depth -= (0 < depth);
    }
    soundReverbDepthSet(depth);
    debug_PrintfDummy(10, 80, 0xFFFFFF00u, "REVERB DEPTH %d%%\n", soundReverbDepthGet());
    return (pad[0].flags & 0x60) != 0;
}

static int adpcmSelect = 0; /* derived name */

static int adpcmHandle = -1; /* derived name */

inline int debug_AdpcmTest(int first)
{
    int r;
    if (first != 0) {
        adpcmHandle = -1;
    }
    r = debug_SelectCsvWindow("ADPCM LIST", 10, 0x3C, 10, adpcmFile, 0x40, 0, 0, 0x69, &adpcmSelect);
    if (r > 0) {
        if (adpcmSelect != 0) {
            scpAdpcmPlayRequestFunc(adpcmSelect, 0, 1, 1, 1);
        }
    }
    return r;
}

inline void debugCdvdLoadInfoSegInit(int page)
{
    int i;
    for (i = 25; i >= 0; i--)
        loadInfoSeg[page][i][0] = 0;
}

inline void debugCdvdLoadInfoSegAdd(int page, int idx, int delta)
{
    *(int *)((char *)loadInfoSeg + (page * 0xD0 + idx * 8)) += delta;
}

inline void debugCdvdLoadInfoSegCls(int page, int idx)
{
    *(int *)((char *)loadInfoSeg + (page * 0xD0 + idx * 8)) = 0;
}

static char *debugCdvdLoadInfoSegDispFunc(int idx, int page)
{
    char buf[16];
    int d;
    d = *(int *)((char *)loadInfoSeg + (page * 0xD0 + idx * 8)) -
        *(int *)((char *)loadInfoSeg + (page * 0xD0 + idx * 8) + 4);
    if (d < 0) {
        sprintf(buf, "-%6x", -d);
    } else {
        sprintf(buf, " %6x", d);
    }
    sprintf(loadInfoLine, "%3s %6x %6x %s", initFunc[idx].ext,
            *(int *)((char *)loadInfoSeg + (page * 0xD0 + idx * 8)),
            *(int *)((char *)loadInfoSeg + (page * 0xD0 + idx * 8) + 4), buf);
    return loadInfoLine;
}

static int loadInfoPage = 1; /* derived name */

static int loadInfoSelect = 0; /* derived name */

inline int debugCdvdLoadInfoSegDisp(void)
{
    char title[] = "CD LOAD INFO COMMON";
    int r;
    if (loadInfoPage != 0) {
        strcpy(title, "CD LOAD INFO STAGE");
    }
    r = debug_SelectCsvWindowVal(title, 80, 70, 10, 26, &loadInfoSelect,
                                 (int (*)(int, int))debugCdvdLoadInfoSegDispFunc, loadInfoPage);
    if (pad[0].flags & 0x10) {
        int i;
        int *p = (int *)((char *)loadInfoSeg + loadInfoPage * 0xD0);
        for (i = 25; i >= 0; i--) {
            p[1] = p[0];
            p += 2;
        }
    }
    return r;
}

inline int debug_GameOver(void)
{
    debug_Menu_off();
    return -1;
}

inline int debug_EndingDemo(void)
{
    debug_Menu_off();
    return -1;
}

inline int debug_BackStageTest(void)
{
    backStageProcessInStage(10000000.0f);
    return 1;
}

inline int debug_tsuresariTimeZero(void)
{
    backStageDebugTimeZero();
    return 1;
}

inline int debug_hintStart(void)
{
    void *gobj;
    for (gobj = isysGObjGetExist_begin(); gobj != 0; gobj = isysGObjGetExist_next(gobj)) {
        if (IsTopHint(gobj)) {
            DebugHintStart(gobj);
            break;
        }
    }
    return 1;
}

static inline int debug_ListActGobj(DbgGobjEnt *list) /* derived name */
{
    void *g;
    int n = 0;
    for (g = isysGObjGetExist_begin(); g != 0; g = isysGObjGetExist_next(g)) {
        int kind = ((GObj *)g)->kind;
        switch (kind) {
        case 1:
        case 2:
        case 4:
        case 0x2F:
            list[n].name = objKindData[kind].name;
            list[n].obj = g;
            n++;
        }
    }
    return n;
}

/* builds the {name, gobj} list of the pad-controllable objects (kind 2 = boy, 4 = girl). */
static inline int debug_ListPadControlGobj(DbgGobjEnt *list) /* derived name */
{
    void *g;
    int n = 0;
    for (g = isysGObjGetExist_begin(); g != 0; g = isysGObjGetExist_next(g)) {
        int kind = ((GObj *)g)->kind;
        if (kind == 2 || kind == 4) {
            list[n].obj = g;
            list[n].name = objKindData[kind].name;
            n++;
        }
    }
    return n;
}

static int actGobjSelect = 0; /* derived name */

static int debug_SelectActGobj(int reset)
{
    DbgGobjEnt list[10];
    int n;
    int r;
    n = debug_ListActGobj(list);
    if (reset != 0) {
        actGobjSelect = 0;
    }
    r = debug_SelectCsvWindow("CHARACTER DEBUG INFO", 0xA, 0x32, 0xB, list, 8, 0, 1, n, &actGobjSelect);
    if (actGobjSelect >= 0) {
        _ACTDebugPrint(list[actGobjSelect].obj);
    }
    return (r == -1) ? -1 : 0;
}

static int pad2GobjSelect = 0; /* derived name */

inline int debug_SelectPad2ControlGobj(int reset)
{
    DbgGobjEnt list[10];
    int n;
    int r;
    n = debug_ListPadControlGobj(list);
    if (reset != 0) {
        pad2GobjSelect = 0;
    }
    r = debug_SelectCsvWindow("CHARACTER PAD2 CONTROL", 0xA, 0x32, 0xB, list, 8, 0, 1, n, &pad2GobjSelect);
    if (r > 0) {
        CurrentTargetGObjSub = list[pad2GobjSelect].obj;
        return 1;
    }
    return (r == -1) ? -1 : 0;
}

static int debug_DispBox(int on)
{
    int i;
    int step;
    int num;

    if (on) {
        if (boyGObj != 0) {
            GetRootPosition(boxCentre, boyGObj);
        } else {
            boxCentre[0] = 0.0f;
            boxCentre[1] = 0.0f;
            boxCentre[2] = 0.0f;
        }
        boxWidth[0] = 100.0f;
        boxWidth[1] = 100.0f;
        boxWidth[2] = 100.0f;
        dispBoxRow = 0;
    }
    if (pad[0].rep & 0x1000) {
        dispBoxRow--;
    }
    if (pad[0].rep & 0x4000) {
        dispBoxRow++;
    }
    num = 6;
    dispBoxRow = (dispBoxRow + num) % num;
    step = (pad[0].rep & 0x2000) ? 100 : 0;
    if (pad[0].rep & 0x8000) {
        step = -100;
    }
    switch (dispBoxRow) {
    case 0:
        boxCentre[0] += (float)step;
        break;
    case 1:
        boxCentre[1] += (float)step;
        break;
    case 2:
        boxCentre[2] += (float)step;
        break;
    case 3:
        boxWidth[0] += (float)step;
        break;
    case 4:
        boxWidth[1] += (float)step;
        break;
    case 5:
        boxWidth[2] += (float)step;
        break;
    }
    {
        DbgBoxVal list[6] = {
            {"centerX", (int)boxCentre[0]}, {"centerY", (int)boxCentre[1]},
            {"centerZ", (int)boxCentre[2]}, {" widthX", (int)boxWidth[0]},
            {" widthY", (int)boxWidth[1]},  {" widthZ", (int)boxWidth[2]},
        };

        for (i = 0; i < 6; i++) {
            if (i == dispBoxRow) {
                debug_PrintfDummy(10, i * 10 + 80, 0xFFFFFF00u, ">>%8s = %d\n", list[i].name,
                                  list[i].val);
            } else {
                debug_PrintfDummy(10, i * 10 + 80, 0xFFFFFF00u, "  %8s = %d\n", list[i].name,
                                  list[i].val);
            }
        }
    }
    CameraSetMode(1);
    DebugDispBox(boxCentre, boxWidth);
    if (debug_font_flag & 1) {
        debug_Printf(10, 150, 0xFFFFFF00u, "[%s] %4d %4d %4d", (int)"center", (int)boxCentre[0],
                     (int)boxCentre[1], (int)boxCentre[2]);
    }
    if (debug_font_flag & 1) {
        debug_Printf(10, 160, 0xFFFFFF00u, "[%s] %4d %4d %4d", (int)" width", (int)boxWidth[0],
                     (int)boxWidth[1], (int)boxWidth[2]);
    }
    return (pad[0].flags & 0x40) ? -1 : 0;
}

/* debug_DispBall's sphere radius */
static float ballRadius = 0.0f; /* derived name */

static int debug_DispBall(int on)
{
    DbgBallList list = {{{"centerX", &ballCentre[0]}, {"centerY", &ballCentre[1]}, {"centerZ", &ballCentre[2]}, {" radius", &ballRadius}}};
    /* the wire sphere's colour */
    static const Col4 wireCol = {{0, 0x10, 0x20, 0x80}}; /* derived name */
    float pos[4];
    Col4 col;
    int i;
    int hit;
    int step;
    int num;

    num = 4;
    hit = 0;
    if (on) {
        if (boyGObj != 0) {
            GetRootPosition(ballCentre, boyGObj);
        } else {
            ballCentre[0] = 0.0f;
            ballCentre[1] = 0.0f;
            ballCentre[2] = 0.0f;
        }
        ballRadius = 100.0f;
        dispBallRow = 0;
    }
    if (pad[0].rep & 0x1000) {
        dispBallRow--;
    }
    if (pad[0].rep & 0x4000) {
        dispBallRow++;
    }
    dispBallRow = (dispBallRow + num) % num;
    step = (pad[0].rep & 0x2000) ? 10 : 0;
    if (pad[0].rep & 0x8000) {
        step = -10;
    }
    switch (dispBallRow) {
    case 0:
        ballCentre[0] += (float)step;
        break;
    case 1:
        ballCentre[1] += (float)step;
        break;
    case 2:
        ballCentre[2] += (float)step;
        break;
    case 3:
        ballRadius += (float)step;
        break;
    }
    for (i = 0; i < num; i++) {
        if (i == dispBallRow) {
            if (debug_font_flag & 1) {
                debug_Printf(10, i * 10 + 80, 0xFFFFFF00u, ">>%8s = %d\n", (int)list.v[i].name,
                             (int)*list.v[i].val);
            }
        } else {
            if (debug_font_flag & 1) {
                debug_Printf(10, i * 10 + 80, 0xFFFFFF00u, "  %8s = %d\n", (int)list.v[i].name,
                             (int)*list.v[i].val);
            }
        }
    }
    CameraSetMode(1);
    if (boyGObj != 0) {
        GetRootPosition(pos, boyGObj);
        hit = scpTriggerPosBall(pos, ballCentre, ballRadius);
    }
    MatrixDrive_PushMatrix();
    col = wireCol;
    if (hit) {
        col.c[0] = 255;
    }
    gif_StartPacketPri(11);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrixV(ballCentre);
    prim_DispWireSphere(ballRadius, &col, 16, 8);
    gif_EndPacket();
    MatrixDrive_PopMatrix();
    return (pad[0].flags & 0x40) ? -1 : 0;
}

/* debug_CollisionTest's ray modes */
static char *collisionMoveName[] = {"move all", "move src", "move dst"}; /* derived name */

/* the wall hit sphere colour */
static Col4 collisionWallCol = {{0, 0x80, 0xFF, 0x80}}; /* derived name */

/* the floor hit sphere colour */
static Col4 collisionFloorCol = {{0, 0x80, 0xFF, 0x80}}; /* derived name */

static int debug_CollisionTest(int reset)
{
    float v[4];
    VECTOR mv;
    IosPadCtx padCtx;
    IosPadStick st0;
    IosPadStick st1;
    WallCfg wall;
    int r;

    r = debug_SelectCsvWindow("Collision Test", 10, 50, 11, collisionMoveName, 4, 0, 1, 3,
                              &collisionTestRow);
    if (reset != 0) {
        GetRootPosition(collisionRay.pt[0], boyGObj);
        CopyVector(collisionRay.pt[1], collisionRay.pt[0]);
        collisionRay.radius = 0;
        collisionRay.pt[1][2] += 100.0f;
    }
    memset(&mv, 0, sizeof(mv));
    iosPadConnect(&padCtx, 0, 0, &iosPadConfDefault);
    iosPadRead(&padCtx);
    iosPadGetStick(&padCtx, &st0, 0, 2, 2, 0);
    iosPadGetStick(&padCtx, &st1, 1, 2, 2, 0);
    iosPadStickCameraCoord(v, &st1);
    if (padCtx.now & 8) {
        if (st1.mag > 0.001f) {
            mv.y = st1.dz * st1.mag * 16.0f;
        }
    } else {
        if (st1.mag > 0.001f) {
            mv.x = v[0] * st1.mag * 16.0f;
            mv.z = v[2] * st1.mag * 16.0f;
        }
    }
    switch (collisionTestRow) {
    case 1:
        _AddVector(collisionRay.pt[0], collisionRay.pt[0], &mv);
        break;
    case 2:
        _AddVector(collisionRay.pt[1], collisionRay.pt[1], &mv);
        break;
    case 0:
    default:
        _AddVector(collisionRay.pt[0], collisionRay.pt[0], &mv);
        _AddVector(collisionRay.pt[1], collisionRay.pt[1], &mv);
        break;
    }
    ClipCollision(&collisionRay);
    if (collisionRay.wall.elem != 0) {
        wall.o = collisionRay.wall.o;
        wall.elem = collisionRay.wall.elem;
        *(WallCfg *)&mv = wall;
        gif_StartPacketPri(11);
        gif_SetZWrite(0);
        gif_SetZTest(0);
        gif_SetAlpha(1, 0, 0x80);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV(collisionRay.pt[2]);
        prim_DispWireSphere(5.0f, (void *)&collisionWallCol, 8, 4);
        gif_EndPacket();
        DebugDisp1Collision(&mv);
        debug_PrintfDummy(80, 180, 0xFFFFFF00u, "HIT: %p,%d", collisionRay.wall.o.obj,
                          collisionRay.wall.o.node);
        debug_PrintfDummy(80, 190, 0xFFFFFF00u, "ATTR: %x",
                          GetWallAttribute(&collisionRay));
    }
    if (collisionRay.floor.elem != 0) {
        gif_StartPacketPri(11);
        gif_SetZWrite(0);
        gif_SetZTest(0);
        gif_SetAlpha(1, 0, 0x80);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV(collisionRay.pt[2]);
        prim_DispWireSphere(5.0f, (void *)&collisionFloorCol, 8, 4);
        gif_EndPacket();
    }
    debug_PrintfDummy(80, 160, 0xFFFFFF00u, "SRC: %f, %f, %f", collisionRay.pt[0][0],
                      collisionRay.pt[0][1], collisionRay.pt[0][2]);
    debug_PrintfDummy(80, 170, 0xFFFFFF00u, "DST: %f, %f, %f", collisionRay.pt[1][0],
                      collisionRay.pt[1][1], collisionRay.pt[1][2]);
    CameraSetMode(1);
    DrawCollisionRay(&collisionRay);
    DrawCollision(0);
    return r;
}

static inline int debug_FreeCamera(int first)
{
    if (first != 0) {
        CameraSetMode(1);
    }
    CameraSetMode(1);
    return (pad[0].flags & 0x100) ? -1 : 0;
}

int LoadFileType = 1;

/* the debug menu: caption, entry and whether the menu stays open */
DbgMenuItem debugMenu[27] = {
    {"Debug Mode", (int (*)(int))debug_Mode, 0},
    {"Free Camera", debug_FreeCamera, 0},
    {"Stage Select", (int (*)(int))debug_SelectStage, 1},
    {"Target Object", debug_TargetGObj, 0},
    {"Stage Setting", (int (*)(int))gsb_StageSetting, 0},
    {"Way Test", (int (*)(int))debug_WayTool, 0},
    {"Camera Editor", (int (*)(int))debug_CameraEditor, 0},
    {"Motion Viewer", (int (*)(int))MotionViewer, 0},
    {"Effect Tool", (int (*)(int))EffectTool, 0},
    {"TextureList", (int (*)(int))tex_ListTool, 0},
    {"Snap Shot", debug_SnapShot, 0},
    {"Memory Card", (int (*)(int))debug_MemoryCard, 0},
    {"STAFF ROLL TEST", (int (*)(int))debug_STAFFROLLTest, 0},
    {"ADPCM TEST", debug_AdpcmTest, 0},
    {"SE TEST", debug_SETest, 0},
    {"REVERB TEST", (int (*)(int))debug_reverbTest, 0},
    {"Game Over", (int (*)(int))debug_GameOver, 0},
    {"Ending Demo", (int (*)(int))debug_EndingDemo, 0},
    {"BackStage Test", (int (*)(int))debug_BackStageTest, 0},
    {"LoadINFO", (int (*)(int))debugCdvdLoadInfoSegDisp, 0},
    {"Chara Info", debug_SelectActGobj, 0},
    {"Pad2 Control", debug_SelectPad2ControlGobj, 0},
    {"DispBox", debug_DispBox, 0},
    {"DispBall", debug_DispBall, 0},
    {"Collision Test", debug_CollisionTest, 0},
    {"Hint Start", (int (*)(int))debug_hintStart, 1},
    {"Tsuresari Time Zero", (int (*)(int))debug_tsuresariTimeZero, 1},
};

/* the open bug list debug_MenuHelp prints beside the menu */
static char *debugMenuHelp[] = {"B2850   TOO HARD TO PICK UP LASER", "B2890   ILLEGAL CAGE CHAIN DYNAMICS", 0}; /* derived name */

/* the menu's blink counter, state, cursor and the argument its entry gets */
static int menuBlink = 0; /* derived name */

static int menuState = 0; /* derived name */

static int menuSelect = 0; /* derived name */

static int menuArg = 0; /* derived name */

static inline void debug_MenuBlink(void) /* derived name */
{
    if (menuBlink >> 4) {
        debug_PrintfDummy(220, 60, 0x80C0FF80u, "DISC VER.%s %s", "Jan 17 2002", "15:37:26");
    }
    menuBlink++;
    if (menuBlink >= 0x41) {
        menuBlink = 0;
    }
}

static inline void debug_MenuHelp(void) /* derived name */
{
    int x;
    int y;
    int i;

    debug_PrintfDummy(220, 70, 0xFFFFFF80u, "FIXED BUG ID LIST");
    x = 0xF0;
    y = 0x50;
    i = 0;
    while (debugMenuHelp[i] != 0) {
        debug_PrintfDummy(x, y, 0x80808080u, "%s", debugMenuHelp[i]);
        y += 10;
        if (y >= 301) {
            y = 0x50;
            x += 200;
        }
        i++;
    }
}

void debug_Menu(void)
{
    int state;
    int r;
    int (*fn)(int);

    if ((pad[0].now & 2) == 0) {
        if (pad[0].flags & 0x100) {
            if (menuState == 0) {
                debugBackGroundDisableFlag = 1;
                menuState = 1;
                menuSelect = 0;
                return;
            }
        }
    }
    state = menuState;
    if (state == 1) {
        r = debug_SelectCsvWindow("DEBUG MENU", 0xA, 0x32, 0xB, debugMenu, 0xC, 0, 1, 0x1B,
                                  &menuSelect);
        switch (r) {
        case 0:
            break;
        case -1:
            menuState = 0;
            debugBackGroundDisableFlag = 0;
            break;
        default:
            menuState = 2;
            menuArg = state;
            break;
        }
        debug_MenuHelp();
        debug_MenuBlink();
    } else if (state == 2) {
        fn = debugMenu[menuSelect].fn;
        if (fn == 0) {
            menuState = 0;
            debugBackGroundDisableFlag = 0;
        } else {
            r = fn(menuArg);
            menuArg = 0;
            if (r == -1) {
                menuState = 1;
            } else if (r != 0) {
                if (debugMenu[menuSelect].stay == 0) {
                    menuState = 1;
                } else {
                    menuState = 0;
                    debugBackGroundDisableFlag = 0;
                }
            }
        }
    }
}

void debug_Menu_off(void)
{
    menuState = 0;
}

/* the file debugSceOpen has open */
static int sceFd = -1; /* derived name */

inline int debugSceOpen(const char *name, int mode)
{
    sprintf(sceOpenPath, "%s%s;1", "cdrom0:\\", name);
    return sceFd = sceOpen(sceOpenPath, mode);
}

inline int debugSceClose(int fd)
{
    if (fd == sceFd) {
        sceFd = -1;
    }
    return sceClose(fd);
}

inline int debugSceCloseFdNew(void)
{
    int r = 0;
    int h = sceFd;
    if (h != -1) {
        sceFd = -1;
        r = sceClose(h);
        sceFd = -1;
    }
    return r;
}

/* debug.o's globals (see debug.h) */
int polygons = 0;

int strips = 0;

int packets = 0;

int textures = 0;

int texregs = 0;

int texturetranssize = 0;

int used_dl_max = 0;

int used_dl_num = 0;

int used_dl_memory[34] = {0};

int debug_bar_flag = 0;

int debug_ignore_demo_camera = 0;

int debug_brain_bar_flag = 0;

int debug_font_flag = 0;

int debug_font_flag2 = 0;

int debug_font_flag3 = 0;

int debug_skel_flag = 0;

int debug_seslotdisp_flag = 0;

int debug_wallcheck_flag = 0;

int debug_wallhitcoldisp = 0;

int debug_actnode_flag = 0;

int debug_printf_flag = 0;

int debug_window_flag = 0;

int debug_fieldcollision_flag = 0;

int debug_wayline = 0;

int debug_motion_interporate = 0;

int debug_frame = 0;

int debug_mem_partition_flag = 0;

int debug_camera_flag = 0;

int debug_chara_target = 0;

int debug_brain_flag = 0;

int debug_bounding_flag = 0;

int debug_wire_string = 0;

int debug_gbrain_info_flag = 0;

int debug_scissor = 0;

int debug_mot_debug_target = 0;

int debug_now_motion_viewer = 0;

int debug_debug_bar_multiply = 0;

int debug_debug_bar_start_item = 0;

int debug_memory_bar = 0;

int debug_shadow_flag = 0;

int debug_specular_flag = 0;

int debug_zoom_per = 0;

int debug_ripple_roughness = 0;

int debug_snapshot_num = 0;

int debug_snapshot_size = 0;

int debug_snapshot_format = 0;

int debug_snapshot_counter = 0;

int debug_snapshot_reserve = 0;

int debug_ambient_volume = 0;

int debug_jimaku = 0;

int debug_profile_type = 0;

int debug_cloth_info = 0;

int debug_face_chest_ratio = 0;

int debug_face_rot_w_ratio = 0;

int debug_def_smpmin = 0;

int debug_stick_input = 0;

int debug_enemy_battle_type = 0;

int debug_fullscreen_effect = 0;

int debug_disp_cluster = 0;

int debug_disp_normal = 0;

int debug_disp_lws = 0;

int debug_disp_particle = 0;

int debug_disp_mesh = 0;

int debug_act_sub_thread = 0;

int debug_stick_simulate = 0;

int debug_use_new_queen_battle = 0;

int debug_chain_cycle_speed = 0;

int debug_chain_slow_speed = 0;

int debug_mot_slope_interp = 0;

int debug_enemy_fly_with_girl = 0;

int debug_disp_enemy_state = 0;

int debug_disp_escort_ball = 0;

int debug_girl_detour_flag = 0;

int debug_col_old_proc = 0;

int debug_fly_limit_test = 0;

int debug_lwskyomi_lookonly = 0; /* derived name */

int debug_one_hit_only = 0; /* derived name */

int debug_ignore_dodge = 0; /* derived name */

int debug_hand_camera = 0; /* derived name */

int debug_enemy_kidnap_timer = 0; /* derived name */

static int debug_girl_pad_control = 0; /* derived name */

int debug_hair_tight_level = 0; /* derived name */

int debug_hair_gravity_level = 0; /* derived name */

int debug_hair_bend_angle = 0; /* derived name */

int debug_hair_collision = 0; /* derived name */

int debug_no_breast_hang = 0; /* derived name */
