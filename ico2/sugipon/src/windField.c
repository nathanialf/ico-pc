#include "sugiCommon.h"
#include "windField.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "tableSin.h"
#include "debug.h"
#include <libvu0.h>
#include "main.h"
#include "GifPacket.h"

/* the field mode: 0 the radial cells, any other the parallel plane, -1
   before the first InitWindField */
static int windFieldMode = -1; /* derived name */

/* the wind field's geometry and tables */
static float windCenter[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

static float windDir[4] = {0.0f, 0.0f, 0.0f, 0.0f}; /* derived name */

static float windPlane[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

typedef struct { /* field names derived */
    float v[4];
    float str;
    float pad[3];
} WindCell; /* derived name */

static sceVu0FVECTOR windVector; /* derived name */

static float windStrength[256]; /* derived name */

static WindCell windCell[20][20]; /* derived name */

static float *dummyGetWindVector(float *power, float *pos);
static float *getParallelWindVector(float *power, float *pos);
static float *getRadiateWindVector(float *power, float *pos);

/* the wind field's sampler: none, the radial cells or the parallel plane */
static float *(*windVectorFunc)(float *power, float *pos) = dummyGetWindVector; /* derived name */

void InitWindField(int mode, float str, void *center, void *dir)
{
    int i;
    int j;

    windVectorFunc = dummyGetWindVector;
    windFieldMode = mode;

    for (i = 255; i >= 0; i--) {
        windStrength[i] = str;
    }

    if (mode == 0) {
        for (i = 0; i < 20; i++) {
            for (j = 0; j < 20; j++) {
                CopyVector(windCell[i][j].v, ZeroVector);
                windCell[i][j].str = 0.0f;
            }
        }
        CopyVector(windCenter, center);
        windVectorFunc = getRadiateWindVector;
    } else {
        CopyVector(windCenter, center);
        sceVu0Normalize(windDir, dir);
        windDir[3] = 0.0f;
        CopyVector(windPlane, windDir);
        windPlane[3] = -sceVu0InnerProduct(windPlane, windCenter);
        windVectorFunc = getParallelWindVector;
    }
}

static short fanAngle = 0; /* derived name */

/* RGBA of every line this file draws, in the int-per-channel form DrawLineG
   takes */
static int lineColor[4] = {64, 64, 128, 128}; /* derived name */

/* Vertex pairs, one line segment per two rows, terminated by a vertex whose
   x is the -10000 sentinel the draw loops test. */
static float haneLines[10][4] = {
    {0.0f, 0.0f, 0.0f, 1.0f},   {0.0f, 40.0f, 0.0f, 1.0f},  {0.0f, 40.0f, 0.0f, 1.0f},
    {15.0f, 40.0f, 0.0f, 1.0f}, {15.0f, 40.0f, 0.0f, 1.0f}, {40.0f, 20.0f, 0.0f, 1.0f},
    {40.0f, 20.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f, 1.0f},  {-10000.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
}; /* derived name */

/* The spoke drawSenpuukiHaneUnit sweeps sixteen times around Z. */
static float guardLines[12][4] = {
    {0.0f, 0.0f, 0.0f, 1.0f},      {0.0f, -10.0f, 0.0f, 1.0f},    {0.0f, -10.0f, 0.0f, 1.0f},
    {0.0f, -50.0f, 10.0f, 1.0f},   {0.0f, -50.0f, 10.0f, 1.0f},   {0.0f, -10.0f, 20.0f, 1.0f},
    {-10.0f, -50.0f, 10.0f, 1.0f}, {10.0f, -50.0f, 10.0f, 1.0f},  {-5.0f, -10.0f, 20.0f, 1.0f},
    {5.0f, -10.0f, 20.0f, 1.0f},   {-10000.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
}; /* derived name */

/* the body of drawLines, which the function below inlines twice and
   drawLines further down calls */
static inline void drawLinesInline(char *lines) /* derived name */
{
    char *cur = lines;

    if (-1000.0f < *(float *)cur) {
        do {
            DrawLineG(cur, lineColor, cur + 0x10, lineColor, -1);
            cur += 0x20;
        } while (-1000.0f < *(float *)cur);
    }
}

void drawSenpuukiHaneUnit(float scale)
{
    int i;

    MatrixDrive_PushMatrix();

    for (i = 0; i < 16; i++) {
        MatrixDrive_RotMatrixZ(4096);
        drawLinesInline((char *)guardLines);
    }

    MatrixDrive_PopMatrix();

    MatrixDrive_TransMatrix(0.0f, 0.0f, 10.0f);
    MatrixDrive_RotMatrixZ(fanAngle);

    for (i = 0; i < 3; i++) {
        MatrixDrive_RotMatrixZ(21845);
        drawLinesInline((char *)haneLines);
    }

    fanAngle = (short)(int)(fanAngle + scale * 4864.0f);
}

/* The motor housing: two 20 by 40 rectangles 20 apart in Y, joined at the
   corners. */
static float unitLines[26][4] = {
    {10.0f, 0.0f, 20.0f, 1.0f},     {10.0f, 0.0f, -20.0f, 1.0f},   {10.0f, 0.0f, -20.0f, 1.0f},
    {-10.0f, 0.0f, -20.0f, 1.0f},   {-10.0f, 0.0f, -20.0f, 1.0f},  {-10.0f, 0.0f, 20.0f, 1.0f},
    {-10.0f, 0.0f, 20.0f, 1.0f},    {10.0f, 0.0f, 20.0f, 1.0f},    {10.0f, -20.0f, 20.0f, 1.0f},
    {10.0f, -20.0f, -20.0f, 1.0f},  {10.0f, -20.0f, -20.0f, 1.0f}, {-10.0f, -20.0f, -20.0f, 1.0f},
    {-10.0f, -20.0f, -20.0f, 1.0f}, {-10.0f, -20.0f, 20.0f, 1.0f}, {-10.0f, -20.0f, 20.0f, 1.0f},
    {10.0f, -20.0f, 20.0f, 1.0f},   {10.0f, 0.0f, 20.0f, 1.0f},    {10.0f, -20.0f, 20.0f, 1.0f},
    {10.0f, 0.0f, -20.0f, 1.0f},    {10.0f, -20.0f, -20.0f, 1.0f}, {-10.0f, 0.0f, -20.0f, 1.0f},
    {-10.0f, -20.0f, -20.0f, 1.0f}, {-10.0f, 0.0f, 20.0f, 1.0f},   {-10.0f, -20.0f, 20.0f, 1.0f},
    {-10000.0f, 0.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 0.0f, 0.0f},
}; /* derived name */

/* The stand: a 100 by 100 square on the floor and the post above it. */
static float baseLines[12][4] = {
    {50.0f, 0.0f, 50.0f, 1.0f},   {50.0f, 0.0f, -50.0f, 1.0f},   {50.0f, 0.0f, -50.0f, 1.0f},
    {-50.0f, 0.0f, -50.0f, 1.0f}, {-50.0f, 0.0f, -50.0f, 1.0f},  {-50.0f, 0.0f, 50.0f, 1.0f},
    {-50.0f, 0.0f, 50.0f, 1.0f},  {50.0f, 0.0f, 50.0f, 1.0f},    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, -100.0f, 0.0f, 1.0f},  {-10000.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
}; /* derived name */

void drawSenpuuki(float scale)
{
    char *cur;

    gif_StartPacketPri(11);
    gif_SetAlpha(1, 5, 0);
    cur = (char *)baseLines;
    if (-1000.0f < *(float *)cur) {
        do {
            DrawLineG(cur, lineColor, cur + 0x10, lineColor, -1);
            cur += 0x20;
        } while (-1000.0f < *(float *)cur);
    }
    MatrixDrive_TransMatrix(0.0f, -100.0f, 0.0f);
    cur = (char *)unitLines;
    if (-1000.0f < *(float *)cur) {
        do {
            DrawLineG(cur, lineColor, cur + 0x10, lineColor, -1);
            cur += 0x20;
        } while (-1000.0f < *(float *)cur);
    }
    MatrixDrive_TransMatrix(0.0f, -10.0f, 20.0f);
    drawSenpuukiHaneUnit(scale);
    gif_EndPacket();
}

/* The cell centre ExecWindField samples, rewritten per cell; w stays 1. */
static float samplePos[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

void ExecWindField(float str)
{
    float d[4];
    float len;
    int i;
    int j;
    int n;
    int m;

    windStrength[0] = str;
    for (i = 255; i != 0; i--) {
        windStrength[i] = windStrength[i - 1];
    }
    if (windFieldMode == 0) {
        for (i = 0; i < 20; i++) {
            samplePos[2] = ((float)i - 10.0f) * 100.0f;
            for (j = 0; j < 20; j++) {
                samplePos[0] = ((float)j - 10.0f) * 100.0f;
                sceVu0SubVector(d, samplePos, windCenter);
                len = FSqrt(sceVu0InnerProduct(d, d));
                n = (int)(len * 0.1f *
                          ((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f));
                m = n < 256 ? n : 255;
                (windCell[i] + j)->str = windStrength[m];
                sceVu0ScaleVector(windCell[i][j].v, d,
                                  60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) *
                                      windStrength[m] / len);
            }
        }
    }
    if (debug_skel_flag != 0) {
        MatrixDrive_PushMatrix();
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV((char *)windCenter);
        MatrixDrive_RotMatrixY(GetTableArcTan2(windDir[0], windDir[2]));
        drawSenpuuki(str);
        MatrixDrive_PopMatrix();
    }
}

float *GetWindVector(float *power, float *pos)
{
    return windVectorFunc(power, pos);
}

static float *dummyGetWindVector(float *power, float *pos)
{
    if (power)
        *power = 0.0f;
    return ZeroVector;
}

static float *getParallelWindVector(float *power, float *pos)
{
    float d;
    float s;
    int i;
    int n;

    d = plane_distance(pos, windPlane);
    if (d < 0.0f)
        d = -d;

    n = (int)(d * 0.1f * ((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f));
    i = n < 256 ? n : 255;
    s = windStrength[i] * (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
    if (power)
        *power = s;
    sceVu0ScaleVector(windVector, windDir, s);
    return windVector;
}

static float *getRadiateWindVector(float *power, float *pos)
{
    int x;
    int z;

    z = (int)(pos[2] * 0.01f + 10.0f);
    x = (int)(pos[0] * 0.01f + 10.0f);
    z = z < 0 ? 0 : (z < 20 ? z : 19);
    x = x < 0 ? 0 : (x < 20 ? x : 19);
    if (power)
        *power = (windCell[0] + x + z * 20)->str;
    return windCell[z][x].v;
}

void StopWindField(void)
{
    windVectorFunc = dummyGetWindVector;
}

void drawLines(char *lines)
{
    drawLinesInline(lines);
}

void drawSenpuukiHane(void)
{
    float *p;
    for (p = (float *)haneLines; -1000.0f < *p; p += 8) {
        DrawLineG(p, lineColor, p + 4, lineColor, -1);
    }
}

void drawSenpuukiUnit(void)
{
    float *p;
    for (p = (float *)unitLines; -1000.0f < *p; p += 8) {
        DrawLineG(p, lineColor, p + 4, lineColor, -1);
    }
}

void drawSenpuukiBase(void)
{
    float *p;
    for (p = (float *)baseLines; -1000.0f < *p; p += 8) {
        DrawLineG(p, lineColor, p + 4, lineColor, -1);
    }
}
