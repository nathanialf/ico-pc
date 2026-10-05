#include "camera-editor.h"
#include "debug.h"
#include "memory.h"
#include "camera-ico2.h"
#include "camera-root.h"
#include "gv.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "tableSin.h"
#include <stdio.h>
#include <sifdev.h>
#include <string.h>
#include "typedef.h"
#include "ios.h"
#include "main.h"
#include "debug_exception.h"
#include "GifPacket.h"
#include "poly-flat.h"
#include <libvu0.h>
#include <assert.h>

/* ios/thread.c's entry points as the menus call them, each with the menu's
   thread record.  thread.h is not included: the ROM loads a0 before every
   iosThreadSleep call here while thread.c defines it (void), so the
   developer's own declarations disagreed, and these calls were compiled
   against a one-argument declaration like this one. */
extern void iosThreadCreateS(void *th, int no, void (*func)(), void *arg, void *heap,
                             long long stackSize, int pri);
extern void iosThreadStart(void *th);
extern void iosThreadSleep(void *th);
extern void iosThreadDestroy(void *th);
extern void iosThreadWakeup(void *th);
extern void iosThreadMessage(int msg);

typedef struct CamMgr { /* field names derived */
    int count;          /* 0x00 */
    char *items;        /* 0x04 */
    char *pool;         /* 0x08 */
    char flags[100];    /* 0x0C, one per pool block, set while in use */
} CamMgr;               /* derived name */

ICO_WORD curmenu;

static void EnterMenu(void *func, int arg, void *parent)
{
    MenuThread *m = iosMallocDebug(ios_partition_oomori, sizeof(MenuThread), __FILE__, 217);
    iosThreadCreateS(m, 1, func, m, ios_partition_oomori, 4096, 23);
    m->arg = arg;
    m->parent = parent;
    iosThreadStart(m);
    curmenu = (ICO_WORD)m;
    if (parent != 0) {
        iosThreadSleep(parent);
    }
}

/* the 0x10-byte camera-set binary header */
typedef struct { /* field names derived */
    int magic;
    int version;
    int num;
    int pins;
} CamSetBinHdr; /* derived name */

inline void StickToTrans(int stickV, int stickH, int vertical, int heading, float *out, int speed)
{
    out[0] = out[1] = out[2] = 0.0f;
    if ((stickV < 0 ? -stickV : stickV) < 50 && (stickH < 0 ? -stickH : stickH) < 50) {
        return;
    }
    if (vertical != 0) {
        if (stickV > 0) {
            out[1] = (float)speed;
        }
        if (stickV < 0) {
            out[1] = (float)(-speed);
        }
    } else {
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_PushMatrix();
        {
            float vec[4] = {(float)stickH, 0.0f, (float)stickV, 0.0f};
            sceVu0UnitMatrix(MatrixDrive_GetMatrix());
            MatrixDrive_RotMatrixY((short)heading);
            sceVu0ApplyMatrix(vec, MatrixDrive_GetMatrix(), vec);
            sceVu0Normalize(out, vec);
        }
        MatrixDrive_PopMatrix();
        out[0] = out[0] * (float)(-speed);
        out[2] = out[2] * (float)speed;
    }
}

inline void debug_Arrow(float len, void *from, void *to, int r, int g, int b) {}

inline void debug_NMarker(float *pos, int r, int g, int b, float size)
{
    float buf[4];
    sceVu0ScaleVector(buf, pos, -1.0f);
    debug_Marker(buf, r, g, b, size, 0.0f);
}

inline void debug_Marker(float *pos, int r, int g, int b, float size, float pulse) {}

static inline void writeCameraSetFile(char *name, void *buf, int size) /* derived name */
{
    char path[128];

    debug_closeLog();
    debug_StdPrintfDummy("==== Save camera data start ========================\n");
    debug_StdPrintfDummy("\tfilename[%s]\n", name);
    debug_StdPrintfDummy("\t    size[%d]\n", size);
    sprintf(path, "ico2Data/%s", name);
    if (debugSceOpen(path, 0x202) < 0) {
        debug_StdPrintfDummy("Save Camera Data: host file open error.\n");
    } else {
        sceWrite(0, buf, size);
        debugSceClose(0);
        debug_StdPrintfDummy("==== Save camera data end ==========================\n");
    }
    debug_openLog();
}

static void saveEditedDataBinary(char *name, ICO_WORD boxes, int count)
{
    /* the header record: the body writes the four words straight into buf,
       and only the DEBUG build reads them back through it after the file
       write */
    CamSetBinHdr hdr;
    int size;
    int *buf;
    CamGroup *data;

    size = GetSizeOfCameraSetBinary((CamGroup *)boxes, count) + sizeof(CamSetBinHdr);
    buf = (int *)iosMallocDebug(ios_partition_oomori, size, __FILE__, 404);
    data = (CamGroup *)(buf + 4);
    buf[2] = count;
    buf[0] = 0x1234;
    buf[1] = 3;
    buf[3] = CameraEdit_PIN_NUMBER_ALL((CamGroup *)boxes, count);
    MakeCameraSetBinary((CamGroup *)boxes, count, data);
    writeCameraSetFile(name, buf, size);
#ifdef DEBUG
    hdr = *(CamSetBinHdr *)buf;
    scePrintf("camera set %x v%d num %d pins %d\n", hdr.magic, hdr.version, hdr.num, hdr.pins);
#endif
    iosFree(buf);
}

/* the camera set the stage plays and the copy the editor edits: two fixed
   buffers in the development kit's extra memory.  Each is a CamMgr (the count,
   then the groups' address) followed on the EE by the groups and the pin blocks
   (0xE27E0 bytes: 0x70 + 100 * 76 + 100 * 9200).  The addresses lie outside the
   32 MB the host simulates, so there the sets are static records and each box's
   pin block is a heap block (see docs/port/SWEEP_2F.md); CS_COUNT and CS_ITEMS
   read a set's two words. */
#ifdef ICO_HOST
#define CAMSET_T CamMgr
#define CS_COUNT(set) ((set)->count)
#define CS_ITEMS(set) ((ICO_WORD)(set)->items)

static CamMgr cameraSetOrgMgr; /* derived name */

static CamMgr cameraSetEditMgr; /* derived name */

static CamGroup cameraSetOrgGroups[100]; /* derived name */

static CamGroup cameraSetEditGroups[100]; /* derived name */

static CamMgr *cameraSetOrg = &cameraSetOrgMgr; /* derived name */

static CamMgr *cameraSetEdit = &cameraSetEditMgr; /* derived name */

#else
#define CAMSET_T int
#define CS_COUNT(set) ((set)[0])
#define CS_ITEMS(set) ((set)[1])

static int *cameraSetOrg = (int *)0x3000000; /* derived name */

static int *cameraSetEdit = (int *)0x30E27E0; /* derived name */

#endif

/* the line buffer every row of the dump is formatted into before it is
   written and echoed */
static char dumpLine[2048]; /* derived name */

static void saveEditedData(int *range)
{
    char path[112];
    int from = range[0];
    int to = range[1];
    int i;
    int j;
    int fd;

    sprintf(path, "a.txt");
    fd = debugSceOpen(path, 0x202);
    if (fd < 0) {
        debug_StdPrintfDummy("error---cannot open save camera data");
        debug_assert(__FILE__, 435);
        __assert(__FILE__, 435, "0");
    }
    for (i = from; i < to; i++) {
        CamGroup *b = (CamGroup *)(CS_ITEMS(cameraSetEdit) + i * 76);

        sprintf(dumpLine, "group[%s]\n%d\t\t%d\t%d\t%d\t\t\t%d\t%d\t%d\n", b->name, b->kind,
                (int)b->center[0], (int)b->center[1], (int)b->center[2], (int)b->range[0],
                (int)b->range[1], (int)((CamGroup *)(i * 76 + CS_ITEMS(cameraSetEdit)))->range[2]);
        sceWrite(fd, dumpLine, strlen(dumpLine));
        debug_StdPrintfDummy(dumpLine);
    }
    for (i = from; i < to; i++) {
        sprintf(dumpLine, "group[%s]'s pin\n",
                ((CamGroup *)(CS_ITEMS(cameraSetEdit) + i * 76))->name);
        sceWrite(fd, dumpLine, strlen(dumpLine));
        for (j = ((CamGroup *)(i * 76 + CS_ITEMS(cameraSetEdit)))->first;
             j < ((CamGroup *)(i * 76 + CS_ITEMS(cameraSetEdit)))->end; j++) {
            PinRec *p = CameraEdit_PIN(i, j);

            /* the pin flag prints as a maru when set and a batsu when clear */
            sprintf(dumpLine, "%s\t%d\t\t%d\t%d\t%d\t\t\t%d\t%d\t%d\n", p->on ? "○" : "×",
                    (int)p->fov, (int)p->pos[0], (int)p->pos[1], (int)p->pos[2], (int)p->look[0],
                    (int)p->look[1], (int)p->look[2]);
            sceWrite(fd, dumpLine, strlen(dumpLine));
        }
    }
    debugSceClose(fd);
    iosThreadMessage(2);
}

/* the report for a message the editor does not handle; nothing calls it */
static inline void illegalMessage(int msg) /* derived name */
{
    debug_StdPrintfDummy("illegal message %d\n", msg);
}

void gif_test(int *a, int *b, int *c, unsigned char *rgba)
{
    gif_SetGsReg(0, 3);
    gif_SetGsReg(1, (long long)rgba[0] | ((long long)rgba[1] << 8) | ((long long)rgba[2] << 16) |
                        ((long long)rgba[3] << 24));
    gif_SetGsReg(4, (long long)a[0] | ((long long)a[1] << 16) | ((long long)a[2] << 32));
    gif_SetGsReg(4, (long long)b[0] | ((long long)b[1] << 16) | ((long long)b[2] << 32));
    gif_SetGsReg(4, (long long)c[0] | ((long long)c[1] << 16) | ((long long)c[2] << 32));
}

static inline void dispPinRange(int box, int from, int to) /* derived name */
{
    sceVu0IVECTOR col = {255, 255, 255, 128};
    int i;
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    ((float (*)[4])MatrixDrive_GetMatrix())[0][0] = ((float (*)[4])MatrixDrive_GetMatrix())[1][1] =
        ((float (*)[4])MatrixDrive_GetMatrix())[2][2] = -1.0f;
    gif_StartPacketPri(11);
    for (i = from; i < to; i++) {
        float a[3] = {CameraEdit_PIN(box, i)->pos[0], CameraEdit_PIN(box, i)->pos[1],
                      CameraEdit_PIN(box, i)->pos[2]};
        float b[3] = {CameraEdit_PIN(box, i)->look[0], CameraEdit_PIN(box, i)->look[1],
                      CameraEdit_PIN(box, i)->look[2]};
        DrawLine(a, b, col, -1);
    }
    gif_EndPacket();
}

/* box corner quadword: _InterGV / DrawPolygon / do_DrawLine take 16-byte
   aligned vectors */
typedef struct { /* field names derived */
    float x, y, z, w;
} BoxVtx __attribute__((aligned(16))); /* derived name */

/* the box's two index tables and its line colour, initialised as whole
   objects here and in DispCameraGroup */
typedef struct { /* field names derived */
    int e[6][4];
} BoxIdx6; /* derived name */

typedef struct { /* field names derived */
    int e[12][2];
} BoxIdx12; /* derived name */

typedef struct { /* field names derived */
    unsigned char r, g, b, a;
} BoxCol; /* derived name */

typedef union { /* field names derived */
    unsigned int c[4];
    unsigned long long w[2];
} BoxCol4; /* derived name */

void DebugDispBox(float *c, float *s)
{
    int n;
    int j;
    int k;
    int i;
    BoxVtx v[8] = {{c[0] - s[0], c[1] - s[1], c[2] - s[2], 1.0f},
                   {c[0] - s[0], c[1] - s[1], c[2] + s[2], 1.0f},
                   {c[0] + s[0], c[1] - s[1], c[2] - s[2], 1.0f},
                   {c[0] + s[0], c[1] - s[1], c[2] + s[2], 1.0f},
                   {c[0] - s[0], c[1] + s[1], c[2] - s[2], 1.0f},
                   {c[0] - s[0], c[1] + s[1], c[2] + s[2], 1.0f},
                   {c[0] + s[0], c[1] + s[1], c[2] - s[2], 1.0f},
                   {c[0] + s[0], c[1] + s[1], c[2] + s[2], 1.0f}};
    BoxIdx6 idx6 = {
        {{0, 1, 2, 3}, {1, 3, 5, 7}, {2, 3, 6, 7}, {0, 2, 4, 6}, {5, 4, 7, 6}, {1, 0, 5, 4}}};
    BoxCol col;
    BoxCol4 col2;
    float m[4][4];
    float e0[4];
    float e1[4];
    float e2[4];
    float e3[4];
    float g0[4];
    float g1[4];
    float g2[4];
    float g3[4];
    BoxIdx12 idx12;
    float m2[4][4];

    sceVu0UnitMatrix(m);
    sceVu0MulMatrix(m, matrixptr + 0x80, m);
    sceVu0MulMatrix(m, matrixptr + 0xC0, m);
    before_DrawPolygon();
    for (n = 0; n < 6; n++) {
        col = (BoxCol){128, 64, 64, 64};
        col.r = 64;
        col.g = 64;
        col.b = 64;
        col.a = 32;
        for (j = 0; j < 3; j++) {
            _InterGV(e0, &v[idx6.e[n][0]].x, &v[idx6.e[n][1]].x, (float)j, (float)(3 - j));
            _InterGV(e1, &v[idx6.e[n][0]].x, &v[idx6.e[n][1]].x, (float)(j + 1), (float)(2 - j));
            _InterGV(e2, &v[idx6.e[n][2]].x, &v[idx6.e[n][3]].x, (float)j, (float)(3 - j));
            _InterGV(e3, &v[idx6.e[n][2]].x, &v[idx6.e[n][3]].x, (float)(j + 1), (float)(2 - j));
            for (k = 0; k < 3; k++) {
                _InterGV(g0, e0, e2, (float)k, (float)(3 - k));
                _InterGV(g1, e0, e2, (float)(k + 1), (float)(2 - k));
                _InterGV(g2, e1, e3, (float)k, (float)(3 - k));
                _InterGV(g3, e1, e3, (float)(k + 1), (float)(2 - k));
                DrawPolygon(g0, g1, g2, g3, (unsigned char *)&col, m);
            }
        }
    }
    after_DrawPolygon();
    idx12 = (BoxIdx12){{{0, 1},
                        {1, 3},
                        {3, 2},
                        {2, 0},
                        {0, 4},
                        {1, 5},
                        {2, 6},
                        {3, 7},
                        {4, 5},
                        {5, 7},
                        {7, 6},
                        {6, 4}}};
    col2 = (BoxCol4){{255, 255, 255, 255}};
    sceVu0UnitMatrix(m2);
    m2[0][0] = m2[1][1] = m2[2][2] = -1.0f;
    before_DrawLine(m);
    for (i = 0; i < 12; i++) {
        do_DrawLine(&v[idx12.e[i][0]], &v[idx12.e[i][1]], col2.c, -1);
    }
    after_DrawLine();
}

static void DispCameraGroup(int box, unsigned char sel)
{
    int n;
    int j;
    int k;
    int i;
    CamGroup *b = (CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76);
    BoxVtx v[8] = {
        {b->center[0] - b->range[0], b->center[1] - b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] - b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] - b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] - b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] + b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] + b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] + b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] + b->range[1], b->center[2] + b->range[2], 1.0f}};
    BoxIdx6 idx6 = {
        {{0, 1, 2, 3}, {1, 3, 5, 7}, {2, 3, 6, 7}, {0, 2, 4, 6}, {5, 4, 7, 6}, {1, 0, 5, 4}}};
    BoxCol col;
    BoxCol4 col2;
    float m[4][4];
    float e0[4];
    float e1[4];
    float e2[4];
    float e3[4];
    float g0[4];
    float g1[4];
    float g2[4];
    float g3[4];
    BoxIdx12 idx12;
    float m2[4][4];

    sceVu0UnitMatrix(m);
    m[0][0] = m[1][1] = m[2][2] = -1.0f;
    sceVu0MulMatrix(m, matrixptr + 0x80, m);
    sceVu0MulMatrix(m, matrixptr + 0xC0, m);
    before_DrawPolygon();
    for (n = 0; n < 6; n++) {
        col = (BoxCol){128, 64, 64, 64};
        if (sel == 0) {
            col.r = 64;
            col.g = 64;
            col.b = 64;
            col.a = 32;
        }
        for (j = 0; j < 3; j++) {
            _InterGV(e0, &v[idx6.e[n][0]].x, &v[idx6.e[n][1]].x, (float)j, (float)(3 - j));
            _InterGV(e1, &v[idx6.e[n][0]].x, &v[idx6.e[n][1]].x, (float)(j + 1), (float)(2 - j));
            _InterGV(e2, &v[idx6.e[n][2]].x, &v[idx6.e[n][3]].x, (float)j, (float)(3 - j));
            _InterGV(e3, &v[idx6.e[n][2]].x, &v[idx6.e[n][3]].x, (float)(j + 1), (float)(2 - j));
            for (k = 0; k < 3; k++) {
                _InterGV(g0, e0, e2, (float)k, (float)(3 - k));
                _InterGV(g1, e0, e2, (float)(k + 1), (float)(2 - k));
                _InterGV(g2, e1, e3, (float)k, (float)(3 - k));
                _InterGV(g3, e1, e3, (float)(k + 1), (float)(2 - k));
                DrawPolygon(g0, g1, g2, g3, (unsigned char *)&col, m);
            }
        }
    }
    after_DrawPolygon();
    idx12 = (BoxIdx12){{{0, 1},
                        {1, 3},
                        {3, 2},
                        {2, 0},
                        {0, 4},
                        {1, 5},
                        {2, 6},
                        {3, 7},
                        {4, 5},
                        {5, 7},
                        {7, 6},
                        {6, 4}}};
    col2 = (BoxCol4){{255, 255, 255, 255}};
    sceVu0UnitMatrix(m2);
    m2[0][0] = m2[1][1] = m2[2][2] = -1.0f;
    before_DrawLine(m);
    for (i = 0; i < 12; i++) {
        do_DrawLine(&v[idx12.e[i][0]], &v[idx12.e[i][1]], col2.c, -1);
    }
    after_DrawLine();
}

/* a VU0 quadword: DrawLineG takes 16-byte aligned vectors */
typedef struct { /* field names derived */
    float x, y, z, w;
} ArrowVtx __attribute__((aligned(16)));

/* The head, barb and shaft ends of the arrow drawXZArrow draws: the head runs from the origin out to +-50, the barbs join +-25 to
   the head, and the shaft runs from +-25 back to the caller's length. */
static ArrowVtx arrowHeadLeft = {-50.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static ArrowVtx arrowBarbLeft = {-25.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static ArrowVtx arrowShaftLeft = {-25.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static ArrowVtx arrowHeadRight = {50.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static ArrowVtx arrowBarbRight = {25.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static ArrowVtx arrowShaftRight = {25.0f, 0.0f, -50.0f, 1.0f}; /* derived name */

static void drawXZArrow(void *col, int f, float z)
{
    ArrowVtx v0 = {-25.0f, 0.0f, -z, 1.0f};
    ArrowVtx v1 = {25.0f, 0.0f, -z, 1.0f};

    DrawLineG(&ZeroVector, col, &arrowHeadRight, col, f);
    DrawLineG(&ZeroVector, col, &arrowHeadLeft, col, f);
    DrawLineG(&arrowBarbRight, col, &arrowHeadRight, col, f);
    DrawLineG(&arrowBarbLeft, col, &arrowHeadLeft, col, f);
    DrawLineG(&arrowShaftLeft, col, &v0, col, f);
    DrawLineG(&arrowShaftRight, col, &v1, col, f);
    DrawLineG(&v0, col, &v1, col, f);
}

/* one axis of the arrow table: the tip and tail vectors of the arrow */
typedef struct { /* field names derived */
    float tip[4];
    float tail[4];
} AxisPair; /* derived name */

/* the point the axis widget is drawn at, 2000 units down the view axis */
static ArrowVtx axisArrowOrigin = {0.0f, 0.0f, 2000.0f, 1.0f}; /* derived name */

/* the three axis arrows, each from -200 to +200 along one axis */
static AxisPair axisArrows[3] = {
    /* derived name */
    {{-200.0f, 0.0f, 0.0f, 1.0f}, {200.0f, 0.0f, 0.0f, 1.0f}},
    {{0.0f, -200.0f, 0.0f, 1.0f}, {0.0f, 200.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, -200.0f, 1.0f}, {0.0f, 0.0f, 200.0f, 1.0f}},
};

void DispAxisArrow(int mask, void *col)
{
    float v[4];
    float m0[4][4];
    float n[4];
    float m1[4][4];
    AxisPair *ax;
    int i;

    if (mask <= 0) {
        return;
    }
    if (mask >= 3) {
        if (mask >= 6) {
            return;
        }
        if (mask < 4) {
            return;
        }
    }
    {
        MatrixDrive_SetTransposeMatrix(m0, matrixptr + 0x80);
        sceVu0ApplyMatrix(v, m0, &axisArrowOrigin);

        gif_StartPacketPri(11);
        gif_SetAlpha(1, 5, 0);
        gif_SetZTest(0);

        ax = axisArrows;
        for (i = 0; i < 3; i++) {
            if ((mask >> i) & 1) {
                sceVu0UnitMatrix(MatrixDrive_GetMatrix());
                MatrixDrive_TransMatrixV(v);
                MatrixDrive_TurnObjectMatrix(-(ax->tip[0] - ax->tail[0]), ax->tip[1] - ax->tail[1],
                                             ax->tip[2] - ax->tail[2]);
                MatrixDrive_SetTransposeMatrix(m1, MatrixDrive_GetMatrix());
                sceVu0ApplyMatrix(n, matrixptr + 0x80, v);
                n[3] = 0.0f;
                sceVu0ApplyMatrix(n, m0, n);
                sceVu0ApplyMatrix(n, m1, n);
                sceVu0Normalize(n, n);
                n[2] = 0.0f;
                sceVu0Normalize(n, n);
                MatrixDrive_RotMatrixZ((short)-GetTableArcTan2(n[0], n[1]));
                MatrixDrive_TransMatrix(0.0f, 0.0f, 200.0f);
                drawXZArrow(col, -1, 400.0f);
            }
            ax++;
        }
        gif_EndPacket();
    }
}

/* the pin arrow colours: the first pair is drawn depth-tested (the part in
   front of the level), the second pair through it, and each pair has a colour
   for a typed pin and one for a plain one */
static int pinArrowColorTyped[4] = {255, 128, 128, 128}; /* derived name */

static int pinArrowColorPlain[4] = {64, 64, 64, 128}; /* derived name */

static int pinArrowHiddenColorTyped[4] = {64, 32, 32, 128}; /* derived name */

static int pinArrowHiddenColorPlain[4] = {16, 16, 16, 128}; /* derived name */

static void dispCameraPinType2(int box, int from, int to, int type)
{
    float m0[4][4];
    int *c1;
    int *c2;
    int i;

    c1 = type ? pinArrowColorTyped : pinArrowColorPlain;
    c2 = type ? pinArrowHiddenColorTyped : pinArrowHiddenColorPlain;
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_ScaleMatrix(-1.0f, -1.0f, -1.0f);
    MatrixDrive_SetTransposeMatrix(m0, matrixptr + 0x80);
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 0);
    gif_SetZWrite(1);
    for (i = from; i < to; i++) {
        int c1v[4];
        int c2v[4];
        ArrowVtx a = {CameraEdit_PIN(box, i)->pos[0], CameraEdit_PIN(box, i)->pos[1],
                      CameraEdit_PIN(box, i)->pos[2], 0.0f};
        ArrowVtx b = {CameraEdit_PIN(box, i)->look[0], CameraEdit_PIN(box, i)->look[1],
                      CameraEdit_PIN(box, i)->look[2], 0.0f};
        float n[4];
        float m1[4][4];
        float t;

        CopyIVector(c1v, c1);
        CopyIVector(c2v, c2);
        MatrixDrive_TransMatrixV(&b);
        MatrixDrive_TurnObjectMatrix(-(b.x - a.x), b.y - a.y, b.z - a.z);
        {
            ArrowVtx d = {-CameraEdit_PIN(box, i)->look[0], -CameraEdit_PIN(box, i)->look[1],
                          -CameraEdit_PIN(box, i)->look[2], 1.0f};

            MatrixDrive_SetTransposeMatrix(m1, MatrixDrive_GetMatrix());
            sceVu0ApplyMatrix(n, matrixptr + 0x80, &d);
            n[3] = 0.0f;
            sceVu0ApplyMatrix(n, m0, n);
            sceVu0ApplyMatrix(n, m1, n);
            sceVu0Normalize(n, n);
            t = (n[2] < 0.0f ? n[2] + 1.0f : 1.0f - n[2]) * 1000.0f;
            t = 1.0f < t ? 1.0f : t;
            c1v[0] = (int)((float)c1v[0] * t);
            c1v[1] = (int)((float)c1v[1] * t);
            c1v[2] = (int)((float)c1v[2] * t);
            c2v[0] = (int)((float)c2v[0] * t);
            c2v[1] = (int)((float)c2v[1] * t);
            c2v[2] = (int)((float)c2v[2] * t);
            n[2] = 0.0f;
            sceVu0Normalize(n, n);
            MatrixDrive_RotMatrixZ((short)-GetTableArcTan2(n[0], n[1]));
            gif_SetZTest(1);
            drawXZArrow(c1v, 0, GetPointDistance(&a, &b));
            gif_SetZTest(0);
            drawXZArrow(c2v, 0, GetPointDistance(&a, &b));
        }
    }
    gif_EndPacket();
}

void CameraEdit_DispPinType2(int box, int pin, int type)
{
    dispCameraPinType2(box, pin, pin + 1, type);
}

static unsigned char boxFaceColorSel[4] = {128, 64, 64, 64}; /* derived name */

static unsigned char boxFaceColor[4] = {32, 32, 32, 64}; /* derived name */

/* the eight box corners in face order: each row is the two corner pairs the
   face is interpolated between */
static int boxFaceCorner[6][4] = {
    /* derived name */
    {0, 1, 2, 3}, {1, 3, 5, 7}, {2, 3, 6, 7}, {0, 2, 4, 6}, {5, 4, 7, 6}, {1, 0, 5, 4},
};

/* the twelve box edges as corner pairs */
static int boxEdgeCorner[12][2] = {
    /* derived name */
    {0, 1}, {1, 3}, {3, 2}, {2, 0}, {0, 4}, {1, 5}, {2, 6}, {3, 7}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
};

/* the camera box edge colours, bright pair while the box is selected and dim
   pair while it is not; the second of each pair is the part behind geometry */
static unsigned int boxEdgeColorSel[4] = {224, 224, 224, 128}; /* derived name */

static unsigned int boxHiddenEdgeColorSel[4] = {32, 32, 32, 128}; /* derived name */

static unsigned int boxEdgeColor[4] = {64, 64, 64, 128}; /* derived name */

static unsigned int boxHiddenEdgeColor[4] = {16, 16, 16, 128}; /* derived name */

void dispCameraGroupType2(int box, unsigned char sel)
{
    int n;
    int j;
    int k;
    int i;
    unsigned char *col;
    unsigned int *c0;
    unsigned int *c1;
    CamGroup *b = (CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76);
    BoxVtx v[8] = {
        {b->center[0] - b->range[0], b->center[1] - b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] - b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] - b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] - b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] + b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] + b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] + b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] + b->range[1], b->center[2] + b->range[2], 1.0f}};
    float m[4][4];
    float e0[4];
    float e1[4];
    float e2[4];
    float e3[4];
    float g0[4];
    float g1[4];
    float g2[4];
    float g3[4];

    sceVu0UnitMatrix(m);
    m[0][0] = m[1][1] = m[2][2] = -1.0f;
    sceVu0MulMatrix(m, matrixptr + 0x80, m);
    sceVu0MulMatrix(m, matrixptr + 0xC0, m);
    before_DrawPolygon();
    gif_SetAlpha(1, 5, 0);
    gif_SetZWrite(0);
    gif_SetZTest(1);
    for (n = 0; n < 6; n++) {
        col = sel == 0 ? boxFaceColor : boxFaceColorSel;
        for (j = 0; j < 3; j++) {
            _InterGV(e0, &v[boxFaceCorner[n][0]].x, &v[boxFaceCorner[n][1]].x, (float)j,
                     (float)(3 - j));
            _InterGV(e1, &v[boxFaceCorner[n][0]].x, &v[boxFaceCorner[n][1]].x, (float)(j + 1),
                     (float)(2 - j));
            _InterGV(e2, &v[boxFaceCorner[n][2]].x, &v[boxFaceCorner[n][3]].x, (float)j,
                     (float)(3 - j));
            _InterGV(e3, &v[boxFaceCorner[n][2]].x, &v[boxFaceCorner[n][3]].x, (float)(j + 1),
                     (float)(2 - j));
            for (k = 0; k < 3; k++) {
                _InterGV(g0, e0, e2, (float)k, (float)(3 - k));
                _InterGV(g1, e0, e2, (float)(k + 1), (float)(2 - k));
                _InterGV(g2, e1, e3, (float)k, (float)(3 - k));
                _InterGV(g3, e1, e3, (float)(k + 1), (float)(2 - k));
                DrawPolygon(g0, g1, g2, g3, col, m);
            }
        }
    }
    after_DrawPolygon();
    c0 = boxEdgeColorSel;
    c1 = boxHiddenEdgeColorSel;
    if (sel == 0) {
        c0 = boxEdgeColor;
        c1 = boxHiddenEdgeColor;
    }
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_ScaleMatrix(-1.0f, -1.0f, -1.0f);
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 0);
    gif_SetZWrite(0);
    gif_SetZTest(0);
    for (i = 0; i < 12; i++) {
        DrawLineG(&v[boxEdgeCorner[i][0]], c1, &v[boxEdgeCorner[i][1]], c1, 0);
    }
    gif_SetZTest(1);
    for (i = 0; i < 12; i++) {
        DrawLineG(&v[boxEdgeCorner[i][0]], c0, &v[boxEdgeCorner[i][1]], c0, 0);
    }
    gif_EndPacket();
}

/* dispBox is defined as a nested function inside
 * CameraEdit_DispBoxType2_Plane below. */
/* Box corner, a VU0 quadword: _InterGV / DrawPolygon / DrawLineG all take
 * 16-byte aligned vectors. */
typedef struct { /* field names derived */
    float x, y, z, w;
} CamVtx __attribute__((aligned(16)));

/* the plane editor's own copy of the face and edge tables; the faces are in a
   different order from boxFaceCorner above */
static int planeFaceCorner[6][4] = {
    /* derived name */
    {2, 3, 6, 7}, {1, 0, 5, 4}, {1, 3, 5, 7}, {0, 2, 4, 6}, {5, 4, 7, 6}, {0, 1, 2, 3},
};

static int planeEdgeCorner[12][2] = {
    /* derived name */
    {0, 1}, {1, 3}, {3, 2}, {2, 0}, {0, 4}, {1, 5}, {2, 6}, {3, 7}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
};

/* the plane editor's edge colours.  It always draws the bright pair; the dim
   pair after it is the same pair as boxEdgeColor / boxHiddenEdgeColor above
   and nothing reads it. */
static unsigned int planeEdgeColorSel[4] = {224, 224, 224, 128}; /* derived name */

static unsigned int planeHiddenEdgeColorSel[4] = {32, 32, 32, 128}; /* derived name */

static unsigned int planeEdgeColor[4] = {64, 64, 64, 128}; /* derived name */

static unsigned int planeHiddenEdgeColor[4] = {16, 16, 16, 128};

static unsigned char planeFaceColorSel[4] = {128, 64, 64, 64}; /* derived name */

static unsigned char planeHiddenFaceColorSel[4] = {32, 8, 8, 64}; /* derived name */

static unsigned char planeFaceColor[4] = {32, 32, 32, 64}; /* derived name */

static unsigned char planeHiddenFaceColor[4] = {2, 2, 2, 64}; /* derived name */

/* dispBox was nested in CameraEdit_DispBoxType2_Plane; v, m and sel are passed in. */
static void dispBox(unsigned char *ca, unsigned char *cb, CamVtx *v, float m[4][4], int sel)
{
    int n;
    float e0[4], e1[4], e2[4], e3[4];
    float g0[4], g1[4], g2[4], g3[4];
    unsigned char *col;
    int j;
    int k;

    for (n = 0; n < 6; n++) {
        col = (n != sel) ? cb : ca;
        for (j = 0; j < 3; j++) {
            _InterGV(e0, &v[planeFaceCorner[n][0]].x, &v[planeFaceCorner[n][1]].x, (float)j,
                     (float)(3 - j));
            _InterGV(e1, &v[planeFaceCorner[n][0]].x, &v[planeFaceCorner[n][1]].x, (float)(j + 1),
                     (float)(2 - j));
            _InterGV(e2, &v[planeFaceCorner[n][2]].x, &v[planeFaceCorner[n][3]].x, (float)j,
                     (float)(3 - j));
            _InterGV(e3, &v[planeFaceCorner[n][2]].x, &v[planeFaceCorner[n][3]].x, (float)(j + 1),
                     (float)(2 - j));
            for (k = 0; k < 3; k++) {
                _InterGV(g0, e0, e2, (float)k, (float)(3 - k));
                _InterGV(g1, e0, e2, (float)(k + 1), (float)(2 - k));
                _InterGV(g2, e1, e3, (float)k, (float)(3 - k));
                _InterGV(g3, e1, e3, (float)(k + 1), (float)(2 - k));
                DrawPolygon(g0, g1, g2, g3, col, m);
            }
        }
    }
}

void CameraEdit_DispBoxType2_Plane(int box, int sel)
{
    int n;
    CamGroup *b = (CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76);
    CamVtx v[8] = {
        {b->center[0] - b->range[0], b->center[1] - b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] - b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] - b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] - b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] + b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] - b->range[0], b->center[1] + b->range[1], b->center[2] + b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] + b->range[1], b->center[2] - b->range[2], 1.0f},
        {b->center[0] + b->range[0], b->center[1] + b->range[1], b->center[2] + b->range[2], 1.0f}};
    {
        float m[4][4];
        unsigned int *c0;
        unsigned int *c1;
        int i;

        sceVu0UnitMatrix(m);
        m[0][0] = m[1][1] = m[2][2] = -1.0f;
        sceVu0MulMatrix(m, matrixptr + 0x80, m);
        sceVu0MulMatrix(m, matrixptr + 0xC0, m);
        before_DrawPolygon();
        gif_SetAlpha(1, 5, 0);
        gif_SetZWrite(0);
        gif_SetZTest(1);
        dispBox(planeFaceColorSel, planeFaceColor, v, m, sel);
        gif_SetZTest(0);
        dispBox(planeHiddenFaceColorSel, planeHiddenFaceColor, v, m, sel);
        after_DrawPolygon();
        c0 = planeEdgeColorSel;
        c1 = planeHiddenEdgeColorSel;
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_ScaleMatrix(-1.0f, -1.0f, -1.0f);
        gif_StartPacketPri(11);
        gif_SetAlpha(1, 5, 0);
        gif_SetZWrite(0);
        gif_SetZTest(0);
        for (i = 0; i < 12; i++) {
            DrawLineG(&v[planeEdgeCorner[i][0]], c1, &v[planeEdgeCorner[i][1]], c1, 0);
        }
        gif_SetZTest(1);
        for (i = 0; i < 12; i++) {
            DrawLineG(&v[planeEdgeCorner[i][0]], c0, &v[planeEdgeCorner[i][1]], c0, 0);
        }
        gif_EndPacket();
    }
}

void CameraEdit_DispBoxType2(int box, int sel)
{
    dispCameraGroupType2(box, sel & 0xFF);
}

int print_y;

unsigned char exit_f;

void menuGroupSelect(MenuThread *m)
{
    int *box = &m->arg;
    int i;
    int n;
    int start;
    int end;

    iosThreadSleep(m);

    while (1) {
        if (pad[1].flags & 0x1000) {
            m->arg--;
        }
        if (pad[1].flags & 0x4000) {
            m->arg++;
        }
        for (i = 0; i < CameraEdit_BOX_NUMBER(); i++) {
            DispCameraGroup(i, i == *box);
        }
        *box = (*box < 0) ? CameraEdit_BOX_NUMBER() - 1
                          : ((*box < CameraEdit_BOX_NUMBER()) ? *box : 0);
        n = *box - 5;
        start = (n < 0) ? 0 : ((CameraEdit_BOX_NUMBER() < n) ? CameraEdit_BOX_NUMBER() : n);
        n = start + 10;
        end = (n < 0) ? 0 : ((CameraEdit_BOX_NUMBER() < n) ? CameraEdit_BOX_NUMBER() : n);
        for (i = start; i < end; i++) {
            if (i == *box) {
                if (debug_font_flag & 1) {
                    print_y += 10;
                    debug_Printf(40, print_y, 0xFFFFFF00, ">> %s",
                                 ((CamGroup *)(CS_ITEMS(cameraSetEdit) + i * 76))->name);
                }
            } else {
                if (debug_font_flag & 1) {
                    print_y += 10;
                    debug_Printf(40, print_y, 0xFFFFFF00, "   %s",
                                 ((CamGroup *)(CS_ITEMS(cameraSetEdit) + i * 76))->name);
                }
            }
        }
        if (pad[1].flags & 0x20) {
            EnterMenu(menuGroupEdit, *box, m);
        } else if (pad[1].flags & 0x80) {
            EnterMenu(menuPinSelect, *box, m);
        } else if (pad[1].flags & 0x10) {
            exit_f = 1;
        }
        iosThreadSleep(m);
    }
}

/* one row of the group editor: the live value, the step the pad applies to it,
   and whether the row steps on the trigger edge or on the held level */
typedef struct { /* field names derived */
    int val;
    int step;
    int mode;
    char *name;
} EditItem; /* derived name */

void menuGroupEdit(MenuThread *m)
{
    CamGroup *rec = (CamGroup *)(CS_ITEMS(cameraSetEdit) + m->arg * 76);
    int cur = 0;
    int i;

    iosThreadSleep(m);

    while (1) {
        if (pad[1].flags & 0x1000) {
            cur--;
        }
        if (pad[1].flags & 0x4000) {
            cur++;
        }
        cur = (cur < 0) ? 7 : ((cur > 7) ? 0 : cur);

        for (i = 0; i < CameraEdit_BOX_NUMBER(); i++) {
            DispCameraGroup(i, i == m->arg);
        }
        {
            EditItem item[7] = {{rec->kind, 1, 1, "group"},
                                {(int)rec->center[0], 10, 0, "center-x"},
                                {(int)rec->center[1], 10, 0, "center-y"},
                                {(int)rec->center[2], 10, 0, "center-z"},
                                {(int)rec->range[0], 10, 0, "width-x"},
                                {(int)rec->range[1], 10, 0, "width-y"},
                                {(int)rec->range[2], 10, 0, "width-z"}};
            int d = 0;

            if (item[cur].mode) {
                if (pad[1].flags & 0x2000) {
                    d = 1;
                }
                if (pad[1].flags & 0x8000) {
                    d = -1;
                }
            } else {
                if (pad[1].now & 0x2000) {
                    d = 1;
                }
                if (pad[1].now & 0x8000) {
                    d = -1;
                }
            }
            d = d * item[cur].step;
            item[cur].val += d;
            for (i = 0; i < 7; i++) {
                if (i == cur) {
                    if (debug_font_flag & 1) {
                        print_y += 10;
                        debug_Printf(40, print_y, 0xFFFFFF00, ">>%8s = %d\n", item[i].name,
                                     item[i].val);
                    }
                } else {
                    if (debug_font_flag & 1) {
                        print_y += 10;
                        debug_Printf(40, print_y, 0xFFFFFF00, "  %8s = %d\n", item[i].name,
                                     item[i].val);
                    }
                }
            }
            rec->kind = item[0].val;

            rec->center[0] = (float)item[1].val;
            rec->center[1] = (float)item[2].val;
            rec->center[2] = (float)item[3].val;
            rec->range[0] = (float)item[4].val;
            rec->range[1] = (float)item[5].val;
            rec->range[2] = (float)item[6].val;
        }
        if (pad[1].flags & 0x10) {
            curmenu = (ICO_WORD)m->parent;
            iosThreadDestroy(m);
        }
        iosThreadSleep(m);
    }
}

/* the camera work SetWSMatrix converts: eye at 0x00, look-at at 0x10 and the
   field of view at 0x20, the same record camera-ico2.c hands it */
typedef struct CamWork {                /* field names derived */
    float eye[4];                       /* 0x00 */
    float at[4];                        /* 0x10 */
    float fov;                          /* 0x20 */
} __attribute__((aligned(16))) CamWork; /* derived name */

/* the pin the pin editor was opened on */
static int editPinNo; /* derived name */

/* The DEBUG build's trace of the pin window the list shows; retail builds it
   empty. */
#ifdef DEBUG
#define PIN_WINDOW_TRACE(from, to) scePrintf("pin window %d..%d\n", (from), (to))
#else
#define PIN_WINDOW_TRACE(from, to)
#endif

void menuPinSelect(MenuThread *m)
{
    int no = m->arg;
    int cur = ((CamGroup *)(CS_ITEMS(cameraSetEdit) + no * 76))->first;
    int min;
    int max;
    int i;
    int n;
    /* start and end are the window the list shows; the DEBUG build traces
       it at the top of every pass, so the first pass reads these zeros. */
    int start = 0;
    int end = 0;

    iosThreadSleep(m);

    while (1) {
        PIN_WINDOW_TRACE(start, end);
        min = ((CamGroup *)(CS_ITEMS(cameraSetEdit) + no * 76))->first;
        max = ((CamGroup *)(CS_ITEMS(cameraSetEdit) + no * 76))->end;
        if (pad[1].flags & 0x1000) {
            cur--;
        }
        if (pad[1].flags & 0x4000) {
            cur++;
        }
        cur = (cur < min) ? max - 1 : ((cur < max) ? cur : min);

        dispPinRange(no, min, max);

        n = cur - 5;
        start = (n < min) ? min : ((max < n) ? max : n);
        n = start + 10;
        end = (n < min) ? min : ((max < n) ? max : n);
        for (i = start; i < end; i++) {
            int k = i - min;

            if (i == cur) {
                if (debug_font_flag & 1) {
                    debug_Printf(40, print_y += 10, 0xFFFFFF00, ">>%s %d",
                                 CameraEdit_PIN(no, cur)->on ? "ON " : "OFF", k);
                }
            } else {
                if (debug_font_flag & 1) {
                    debug_Printf(40, print_y += 10, 0xFFFFFF00, "  %s %d",
                                 CameraEdit_PIN(no, i)->on ? "ON " : "OFF", k);
                }
            }
        }
        if (CameraEdit_PIN(no, cur)->range != 0.0f) {
            debug_Marker(CameraEdit_PIN(no, cur)->pos, 0, 0, 255, CameraEdit_PIN(no, cur)->range,
                         0.0f);
        } else {
            debug_Marker(CameraEdit_PIN(no, cur)->pos, 255, 0, 0, 100.0f, 0.0f);
        }
        {
            PinRec *p = CameraEdit_PIN(no, cur);
            CamWork cw = {
                {p->pos[0], p->pos[1], p->pos[2]}, {p->look[0], p->look[1], p->look[2]}, p->fov};

            sceVu0ScaleVector(&cw, &cw, -1.0f);
            sceVu0ScaleVector(cw.at, cw.at, -1.0f);
            SetWSMatrix(&cw);
        }
        if (pad[1].flags & 0x10) {
            curmenu = (ICO_WORD)m->parent;
            iosThreadDestroy(m);
        } else if (pad[1].flags & 0x20) {
            editPinNo = no;
            EnterMenu(menuPinEdit, cur, m);
        }
        iosThreadSleep(m);
    }
}

/* the heading from the camera's eye to its look-at point, which is the angle
   the pin editor turns the pad stick vector by */
static inline int camHeading(float *at, float *eye) /* derived name */
{
    float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float len;

    sceVu0SubVector(v, at, eye);
    sceVu0Normalize(v, v);
    len = FSqrt(v[0] * v[0] + v[2] * v[2]);
    return GetTableArcTan2(v[0], -v[2] / len);
}

void menuPinEdit(MenuThread *m)
{
    PinRec *pin = CameraEdit_PIN(editPinNo, m->arg);
    int cur = 0;
    int i;

    iosThreadSleep(m);

    while (1) {
        if (pad[1].flags & 0x1000) {
            cur--;
        }
        if (pad[1].flags & 0x4000) {
            cur++;
        }
        cur = (cur < 0) ? 7 : ((cur > 7) ? 0 : cur);
        {
            EditItem item[8] = {{pin->on, 1, 1, "onoff"},
                                {(int)pin->fov, 1, 1, "view"},
                                {(int)pin->pos[0], 10, 0, "camera-x"},
                                {(int)pin->pos[1], 10, 0, "camera-y"},
                                {(int)pin->pos[2], 10, 0, "camera-z"},
                                {(int)pin->look[0], 10, 0, "target-x"},
                                {(int)pin->look[1], 10, 0, "target-y"},
                                {(int)pin->look[2], 10, 0, "target-z"}};
            int d = 0;

            if (item[cur].mode) {
                if (pad[1].flags & 0x2000) {
                    d = 1;
                }
                if (pad[1].flags & 0x8000) {
                    d = -1;
                }
            } else {
                if (pad[1].now & 0x2000) {
                    d = 1;
                }
                if (pad[1].now & 0x8000) {
                    d = -1;
                }
            }
            d = d * item[cur].step;
            item[cur].val += d;
            for (i = 0; i < 8; i++) {
                if (i == cur) {
                    if (debug_font_flag & 1) {
                        print_y += 10;
                        debug_Printf(40, print_y, 0xFFFFFF00, ">>%8s = %d\n", item[i].name,
                                     item[i].val);
                    }
                } else {
                    if (debug_font_flag & 1) {
                        print_y += 10;
                        debug_Printf(40, print_y, 0xFFFFFF00, "  %8s = %d\n", item[i].name,
                                     item[i].val);
                    }
                }
            }
            pin->on = item[0].val;

            pin->fov = (float)item[1].val;
            pin->pos[0] = (float)item[2].val;
            pin->pos[1] = (float)item[3].val;
            pin->pos[2] = (float)item[4].val;
            pin->look[0] = (float)item[5].val;
            pin->look[1] = (float)item[6].val;
            pin->look[2] = (float)item[7].val;
            {
                CamWork cw = {{pin->pos[0], pin->pos[1], pin->pos[2]},
                              {pin->look[0], pin->look[1], pin->look[2]},
                              pin->fov};
                float out[4];

                sceVu0ScaleVector(&cw, &cw, -1.0f);
                sceVu0ScaleVector(cw.at, cw.at, -1.0f);
                SetWSMatrix(&cw);

                StickToTrans(pad[1].ana[1] - 128, pad[1].ana[0] - 128, pad[1].now & 2,
                             camHeading(cw.at, cw.eye), out, 20);
                sceVu0ScaleVector(out, out, -1.0f);
                pin->pos[0] = pin->pos[0] + out[0];
                pin->pos[1] = pin->pos[1] + out[1];
                pin->pos[2] = pin->pos[2] + out[2];

                StickToTrans(pad[1].ana[3] - 128, pad[1].ana[2] - 128, pad[1].now & 2,
                             camHeading(cw.at, cw.eye), out, 20);
                sceVu0ScaleVector(out, out, -1.0f);
                pin->look[0] = pin->look[0] + out[0];
                pin->look[1] = pin->look[1] + out[1];
                pin->look[2] = pin->look[2] + out[2];

                debug_Marker(pin->look, 255, 0, 0, 100.0f, 0.0f);
            }
        }
        if (pad[1].flags & 0x10) {
            curmenu = (ICO_WORD)m->parent;
            iosThreadDestroy(m);
        }
        iosThreadSleep(m);
    }
}

inline void menu_2(MenuThread *m)
{
    iosThreadSleep(m);

    while (1) {
        debug_StdPrintfDummy("menu_2, arg=%d\n", m->arg);
        if (pad[1].flags & 0x20) {
            curmenu = (ICO_WORD)m->parent;
            iosThreadDestroy(m);
        }
        iosThreadSleep(m);
    }
}

/* the global group_select; way_tool.c has a static of the same name, reached
   through its own menu table */
inline void group_select(MenuThread *m)
{
    iosThreadSleep(m);

    while (1) {
        debug_StdPrintfDummy("menu_1, arg=%d\n", m->arg);
        if (pad[1].flags & 0x20) {
            EnterMenu(menu_2, 3, m);
        }
        iosThreadSleep(m);
    }
}

void wakeup_cameraedit(void)
{
    print_y = 50;
    if (curmenu != 0) {
        iosThreadWakeup((void *)curmenu);
        if (pad[1].flags & 0x400) {
            saveEditedDataBinary(cameraSetList[stageData[stage_no].camSetId],
                                 CS_ITEMS(cameraSetEdit), CS_COUNT(cameraSetEdit));
        }
    }
}

void test_camedit(void)
{
    EnterMenu(menuGroupSelect, 0, 0);
}

inline CamGroup *_CameraEdit_BOX(CAMSET_T *set, int box)
{
    return (CamGroup *)(CS_ITEMS(set) + box * 76);
}

inline PinRec *_CameraEdit_PIN(CAMSET_T *set, int box, int pin)
{
    return &CAMGROUP_ITEMS((CamGroup *)(CS_ITEMS(set) + box * 76))[pin];
}

#ifdef ICO_HOST

/* a box's pin block: 100 pins, from the heap so CamGroup.items can hold its
   address as an EE word; 0 when the heap is out of room (the caller then
   reports the box as not added) */
static inline char *_CameraEdit_alloc_pool(CamMgr *mgr) /* derived name */
{
    return iosMallocDebugNoAssert(ios_partition_root, 9200, __FILE__, 1262);
}

#else

static inline char *_CameraEdit_alloc_pool(CamMgr *mgr) /* derived name */
{
    int i;
    for (i = 0; i < 100; i++) {
        if (mgr->flags[i] == 0) {
            mgr->flags[i] = 1;
            return mgr->pool + i * 9200;
        }
    }
    return 0;
}

#endif

inline int _CameraEdit_add_box(CamMgr *mgr, CamGroup *src)
{
    int result = -1;
    char *p;
    CamGroup *dst;
    if (mgr->count < 100) {
        p = _CameraEdit_alloc_pool(mgr);
        if (p != 0) {
            dst = (CamGroup *)(mgr->items + mgr->count * 76);
            result = mgr->count;
            *dst = *src;
            dst->first = 0;
            dst->end = 0;
            CAMGROUP_SET_ITEMS(dst, (PinRec *)p);
            mgr->count = mgr->count + 1;
        }
        return result;
    }
    /* "cannot add any more" */
    debug_StdPrintfDummy("これ以上追加できません");
    return -1;
}

inline int _CameraEdit_add_pin(CamMgr *mgr, int box, PinRec *src)
{
    ICO_WORD base = box * 76 + (ICO_WORD)mgr->items;
    int n = ((CamGroup *)base)->end;
    int result = -1;
    if (n < 100) {
        ICO_WORD base2;
        CAMGROUP_ITEMS((CamGroup *)base)[n] = *src;
        base2 = box * 76 + (ICO_WORD)mgr->items;
        result = ((CamGroup *)base2)->end;
        ((CamGroup *)base2)->end = result + 1;
    } else {
        /* "cannot add any more" */
        debug_StdPrintfDummy("これ以上追加できません");
    }
    return result;
}

#ifdef ICO_HOST

static inline void _CameraEdit_free_box_pool(CamMgr *mgr, int idx) /* derived name */
{
    CamGroup *box = (CamGroup *)(idx * 76 + (ICO_WORD)mgr->items);

    if (CAMGROUP_ITEMS(box) != 0) {
        iosFree(CAMGROUP_ITEMS(box));
    }
}

#else

static inline void _CameraEdit_free_box_pool(CamMgr *mgr, int idx) /* derived name */
{
    CamGroup *box = (CamGroup *)(idx * 76 + (int)mgr->items);
    char *p = mgr->pool;
    int i;
    for (i = 0; i < 100; i++) {
        if (p == (char *)CAMGROUP_ITEMS(box)) {
            mgr->flags[i] = 0;
        }
        p += 9200;
    }
}

#endif

void _CameraEdit_del_box(CamMgr *mgr, int idx)
{
    if (mgr->count <= 0) {
        /* "cannot delete any more" */
        debug_StdPrintfDummy("これ以上削除できません");
        return;
    }
    _CameraEdit_free_box_pool(mgr, idx);
    while (idx < mgr->count) {
        ((CamGroup *)mgr->items)[idx] = ((CamGroup *)mgr->items)[idx + 1];
        idx++;
    }
    mgr->count = mgr->count - 1;
}

static inline CamGroup *_CameraEdit_BOX_p(CamMgr *mgr, int i) /* derived name */
{
    return (CamGroup *)(i * 76 + (ICO_WORD)mgr->items);
}

static inline PinRec *_CameraEdit_PIN_p(CamMgr *mgr, int i, int j) /* derived name */
{
    return &CAMGROUP_ITEMS(_CameraEdit_BOX_p(mgr, i))[j];
}

void _CameraEdit_del_pin(CamMgr *mgr, int box, int pin)
{
    PinRec *p;
    if (_CameraEdit_BOX_p(mgr, box)->end <= 0) {
        /* "cannot delete any more" */
        debug_StdPrintfDummy("これ以上削除できません");
        return;
    }
    for (p = _CameraEdit_PIN_p(mgr, box, pin);
         p < _CameraEdit_PIN_p(mgr, box, _CameraEdit_BOX_p(mgr, box)->end); p++) {
        *p = p[1];
    }
    _CameraEdit_BOX_p(mgr, box)->end = _CameraEdit_BOX_p(mgr, box)->end - 1;
}

int CameraEdit_add_box(CamGroup *src)
{
    _CameraEdit_add_box((CamMgr *)cameraSetOrg, src);
    return _CameraEdit_add_box((CamMgr *)cameraSetEdit, src);
}

int CameraEdit_add_pin(int box, char *src)
{
    _CameraEdit_add_pin((CamMgr *)cameraSetOrg, box, (PinRec *)src);
    return _CameraEdit_add_pin((CamMgr *)cameraSetEdit, box, (PinRec *)src);
}

void CameraEdit_del_box(int box)
{
    _CameraEdit_del_box((CamMgr *)cameraSetOrg, box);
    _CameraEdit_del_box((CamMgr *)cameraSetEdit, box);
}

void CameraEdit_del_pin(int box, int pin)
{
    _CameraEdit_del_pin((CamMgr *)cameraSetOrg, box, pin);
    _CameraEdit_del_pin((CamMgr *)cameraSetEdit, box, pin);
}

void CameraEdit_DispBox(int box, unsigned char sel)
{
    DispCameraGroup(box, sel);
}

void CameraEdit_Reflect(void)
{
    CAMSET_T *p = cameraSetOrg;
    ReflectCameraSetBinary((CamGroup *)CS_ITEMS(p), CS_COUNT(p));
}

void CameraEdit_Save(char *name)
{
    CAMSET_T *p = cameraSetOrg;
    saveEditedDataBinary(name, CS_ITEMS(p), CS_COUNT(p));
}

inline void InitCameraEditor(void)
{
    curmenu = 0;
    exit_f = 0;
}

inline int debug_CameraEditor(void)
{
    debug_font_flag = 1;
    if (curmenu == 0) {
        test_camedit();
    }
    wakeup_cameraedit();
    CameraSetMode(1);
    if (exit_f == 0) {
        return 0;
    }
    exit_f = 0;
    CameraEdit_Reflect();
    return -1;
}

inline void CameraEdit_reset_box(int box)
{
    CamGroup *src;
    CamGroup *dst;
    PinRec *saved;
    int i;
    src = (CamGroup *)(CS_ITEMS(cameraSetOrg) + box * 76);
    dst = (CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76);
    saved = CAMGROUP_ITEMS(dst);
    *dst = *src;
    CAMGROUP_SET_ITEMS(dst, saved);
    i = 0;
    while (i < CameraEdit_BOX(box)->end - CameraEdit_BOX(box)->first) {
        CameraEdit_reset_pin(box, i);
        i++;
    }
}

inline void CameraEdit_reset_pin(int box, int pin)
{
    PinRec *dst = &CAMGROUP_ITEMS((CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76))[pin];
    PinRec *src = &CAMGROUP_ITEMS((CamGroup *)(CS_ITEMS(cameraSetOrg) + box * 76))[pin];
    *dst = *src;
}

inline void CameraEdit_reflect_box(int box)
{
    CamGroup *dst = (CamGroup *)(CS_ITEMS(cameraSetOrg) + box * 76);
    CamGroup *src = (CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76);
    PinRec *saved = CAMGROUP_ITEMS(dst);
    int i;
    *dst = *src;
    CAMGROUP_SET_ITEMS(dst, saved);
    i = 0;
    while (i < CameraEdit_BOX(box)->end - CameraEdit_BOX(box)->first) {
        CameraEdit_reflect_pin(box, i);
        i++;
    }
}

inline void CameraEdit_reflect_pin(int box, int pin)
{
    PinRec *dst = &CAMGROUP_ITEMS((CamGroup *)(CS_ITEMS(cameraSetOrg) + box * 76))[pin];
    PinRec *src = &CAMGROUP_ITEMS((CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76))[pin];
    *dst = *src;
}

inline int CameraEdit_BOX_NUMBER(void)
{
#ifdef ICO_HOST
    return cameraSetEdit->count;
#else
    return *cameraSetEdit;
#endif
}

inline int CameraEdit_PIN_NUMBER(int box)
{
    CamGroup *r1 = CameraEdit_BOX(box);
    CamGroup *r2 = CameraEdit_BOX(box);
    return r1->end - r2->first;
}

/* box is never advanced: the sum is n times the first group's pin count */
inline int CameraEdit_PIN_NUMBER_ALL(CamGroup *box, int n)
{
    int sum = 0;
    int i;
    for (i = 0; i < n; i++) {
        sum += box->end - box->first;
    }
    return sum;
}

inline CamGroup *CameraEdit_BOX(int box)
{
    return (CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76);
}

inline PinRec *CameraEdit_PIN(int box, int pin)
{
    return CAMGROUP_ITEMS((CamGroup *)(CS_ITEMS(cameraSetEdit) + box * 76)) + pin;
}

inline void CameraEdit_DispPin(int box, int pin)
{
    dispPinRange(box, pin, pin + 1);
}

/* sixteen bytes before the pin default; nothing reads them */
static float cameraEditVec[4] = {0.0f, -500.0f, 0.0f, 0.0f}; /* derived name */

/* the pin a new pin starts from: at (300, 300, 300) looking at the origin,
   on, a field of view of 60 and the hand camera's rates and angle limits;
   ConvertCameraSetBuffer gives it the stage's hand-camera rate */
PinRec cameraPinDefault = {{300.0f, 300.0f, 300.0f},
                           {0.0f, 0.0f, 0.0f},
                           {0.0f, 0.0f, 0.0f},
                           1,
                           60.0f,
                           0,
                           0.0f,
                           0,
                           10.0f,
                           10.0f,
                           120.0f,
                           80.0f};

/* the group a new group starts from: named "0", a 100-unit box at the
   origin */
CamGroup cameraGroupDefault = {"0", {0.0f, 0.0f, 0.0f}, {100.0f, 100.0f, 100.0f}};

/* a 128-byte record after the group default, -1 at 0x40, 50.0 at 0x64 and
   1 at 0x68; nothing reads it */
static struct { /* field names derived */
    int pad00[16];
    int word40;
    int pad44[8];
    float float64;
    int word68;
    int pad6C[5];
} cameraEditRec = {{0}, -1, {0}, 50.0f, 1}; /* derived name */

inline void ConvertCameraSetBuffer(int n, CamGroup *item, char *groups)
{
    CamMgr *m1;
    CamMgr *m2;
    int i;
    int j;
    int a;
    int b;
    char *f;
    cameraPinDefault.eyeRate = stageData[stage_no].handCameraRate;
#ifdef ICO_HOST
    /* give back the pin blocks of the sets this call replaces */
    for (i = 0; i < cameraSetOrgMgr.count; i++) {
        _CameraEdit_free_box_pool(&cameraSetOrgMgr, i);
    }
    for (i = 0; i < cameraSetEditMgr.count; i++) {
        _CameraEdit_free_box_pool(&cameraSetEditMgr, i);
    }
#endif
    m1 = (CamMgr *)cameraSetOrg;
#ifdef ICO_HOST
    m1->items = (char *)cameraSetOrgGroups;
    m1->pool = 0;
#else
    m1->items = (char *)m1 + sizeof(CamMgr);
    m1->pool = (char *)m1 + sizeof(CamMgr) + 100 * sizeof(CamGroup);
#endif
    m1->count = 0;
    f = &m1->flags[99];
    for (a = 99; a >= 0; a--) {
        *f-- = 0;
    }
    m2 = (CamMgr *)cameraSetEdit;
#ifdef ICO_HOST
    m2->items = (char *)cameraSetEditGroups;
    m2->pool = 0;
#else
    m2->items = (char *)m2 + sizeof(CamMgr);
    m2->pool = (char *)m2 + sizeof(CamMgr) + 100 * sizeof(CamGroup);
#endif
    m2->count = 0;
    f = &m2->flags[99];
    for (b = 99; b >= 0; b--) {
        *f-- = 0;
    }
    for (i = 0; i < n; i++) {
        CameraEdit_add_box(item);
        for (j = item->first; j < item->end; j++) {
            CameraEdit_add_pin(i, groups + j * 92);
        }
        item++;
    }
}

inline void CameraEdit_Enter(void) {}
