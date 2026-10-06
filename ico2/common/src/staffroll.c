#include "staffroll.h"
#include "debug.h"
#include "DisplayFont.h"
#include <string.h>
#include "main.h"
#include "debug_exception.h"
#include <assert.h>

/* staffroll.o's whole .data run: the roll's display area, centred on the
   origin, {x, y, width, height}.  Only the first word is read here, as the
   running scroll position. */
static int staffRollArea[4] = {-5120, -1792, 10240, 3584}; /* derived name */

typedef struct {             /* field names derived */
    char **str;              /* 0x00 */
    float y;                 /* 0x04 */
    char align;              /* 0x08, font_CheckAlign's result for the line */
    SprCol col;              /* 0x09 */
    char pad[3];             /* 0x0D */
} StaffRollEntry; /* 0x10 */ /* derived name */

/* .sbss */
static float rollSpeed; /* derived name */ /* lines the roll climbs per frame */

static float rollOffset; /* derived name */ /* how far it has climbed so far */

static int rollNameIdx; /* derived name */ /* the next entry of staffRollNameData to post */

static int rollWidth; /* derived name */ /* the roll's right edge, closing in on 640 */

static float areaStep; /* derived name */ /* per-frame close of the display area */

static float widthStep; /* derived name */ /* per-frame close of rollWidth */

static int closing; /* derived name */ /* the area is closing */

static int rollStep; /* derived name */ /* the roll's own sequence step */

/* .bss: the posted lines, 0x12C0 bytes of StaffRollEntry */
static StaffRollEntry rollLines[300]; /* derived name */

/* .sdata: the three globals the roll's state starts with; the roll's colour
   and staffRollAlpha are defined after staffRollNameOut. */
int staffRollStartFlag = 0;

float staffRollCenterOffsetX = 0.0f;

float staffRollCenterOffsetXDest = 0.0f;

void staffRollStart(float t, int alpha)
{
    staffRollStartFlag = 1;
    rollSpeed = (t + t) * 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    staffRollAlpha = alpha;
    rollNameIdx = 0;
    rollOffset = 0.0f;
    rollStep = 0;
    rollWidth = 0;
    closing = 0;
    staffRollCenterOffsetX = staffRollCenterOffsetXDest = rollOffset;
    staffRollArea[0] = 1280;
    memset(rollLines, 0, sizeof(rollLines));
}

/* The scroll loop walks the 300-entry table by byte offset and spells the
   base at every use site. */
/* the entry holds a pointer, so it is wider than the EE's 16 bytes: index by entry */
#define SROLL(off) (&rollLines[(off) / 16])

static int staffRollScroll(void)
{
    int count;
    int i;
    int a;
    float t;

    count = 0;

    rollOffset += rollSpeed;
    for (i = 0; i < 300 * 16; i += 16) {
        if (SROLL(i)->str == 0) {
            continue;
        }
        count++;

        SROLL(i)->y -= rollSpeed;
        t = SROLL(i)->y - 112.0f;
        a = (int)(184.0f - (t < 0.0f ? -t : t) * 120.0f / 112.0f);
        if (a < 0) {
            a = 0;
        }
        if (a > 128) {
            a = 128;
        }
        if ((float)(-(font_GetHeight() + 449)) < SROLL(i)->y) {
            font_Print(a | 0x70707000, *SROLL(i)->str, (float)rollWidth, SROLL(i)->y,
                       SROLL(i)->align, SROLL(i)->col);
        } else {
            SROLL(i)->str = 0;
        }
    }

    return count;
}

static int staffRollNameOut(void)
{
    StaffRollEntry *e;
    char **s;
    int i;

    if (rollOffset / (float)(font_GetHeight() + 1) >= (float)rollNameIdx) {
        for (i = 0; i < 300; i++) {
            if (rollLines[i].str == 0)
                goto found;
        }
        /* staff roll: out of area */
        debug_StdPrintfDummy("staff roll 領域不足\n");
        debug_assert(__FILE__, 192);
        __assert(__FILE__, 192, "0");
    found:

        e = &rollLines[i];
        s = &staffRollNameData[rollNameIdx++];
        if (*s != 0)
            e->str = s;

        e->y = (float)(font_GetHeight() + 449);
        e->align = font_CheckAlign(&e->col, *e->str);
    }
    return rollNameIdx >= staffRollNameDataNum;
}

/* the roll's colour; only its alpha byte is read and written, fading toward
   staffRollAlpha.  A 4-byte array is 8-aligned, which leaves the zero word
   after the assert text. */
static unsigned char rollColour[4] = {0}; /* derived name */

int staffRollAlpha = 0;

void staffRollMain(void)
{
    int a;
    int n;

    if (closing != 0) {
        n = 0;
        staffRollArea[0] = (int)((float)staffRollArea[0] - areaStep);
        if (staffRollArea[0] < -5120) {
            staffRollArea[0] = -5120;
            n = 1;
        }
        rollWidth = (int)((float)rollWidth - widthStep);
        if (rollWidth < 640) {
            rollWidth = 640;
            n++;
        }
        if (n == 2) {
            closing = 0;
        }
    }

    a = rollColour[3];
    if (a < staffRollAlpha) {
        a += 2;
        if (staffRollAlpha < a) {
            a = staffRollAlpha;
        }
    } else {
        a -= 2;
        if (a < staffRollAlpha) {
            a = staffRollAlpha;
        }
    }
    /* the roll colour's alpha, a volatile store */
    *(volatile unsigned char *)&rollColour[3] = a;

    if (staffRollCenterOffsetXDest > staffRollCenterOffsetX) {
        staffRollCenterOffsetX += 0.5f;
        if (staffRollCenterOffsetXDest < staffRollCenterOffsetX) {
            staffRollCenterOffsetX = staffRollCenterOffsetXDest;
        }
    } else {
        staffRollCenterOffsetX -= 0.5f;
        if (staffRollCenterOffsetX < staffRollCenterOffsetXDest) {
            staffRollCenterOffsetX = staffRollCenterOffsetXDest;
        }
    }

    switch (rollStep) {
    case 0:
        font_Init();
        rollColour[3] = 0;
        rollStep++;
        /* fallthrough */
    case 1:
        staffRollCenterOffsetXDest = 0.0f;
        if (rollColour[3] == staffRollAlpha &&
            staffRollCenterOffsetX == staffRollCenterOffsetXDest) {
            rollStep++;
        }
        break;
    case 2:
        staffRollScroll();
        if (staffRollNameOut() != 0) {
            rollStep++;
        }
        break;
    case 3:
        if (staffRollScroll() == 0) {
            rollStep++;
        }
        break;
    case 4:
        staffRollAlpha = 0;
        staffRollCenterOffsetXDest = 0.0f;
        if (rollColour[3] == 0) {
            rollStep++;
        }
        break;
    case 5:
        if (staffRollCenterOffsetX == 0.0f) {
            rollStep++;
        }
        break;
    case 6:
        staffRollStartFlag = 0;
        break;
    }
}

void staffRollWide(void)
{
    closing = 1;
    areaStep = (float)((staffRollArea[0] + 5120) / 30);
    widthStep = (float)((rollWidth - 640) / 30);
    staffRollAlpha = 255;
}
