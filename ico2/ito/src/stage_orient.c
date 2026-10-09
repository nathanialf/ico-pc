#include "typedef.h"
#include "Matrix.h"
#include "matrixDrive.h"
#include <libvu0.h>

typedef struct { /* field names derived */
    char name[16];
    float angle;
    float pos[3];
    float pos2[3];
} StageOrientDef; /* derived name */

static const StageOrientDef stageOrientDefs[41] = {
    /* derived name */
    {"st47a", 0.0f, {200.0f, -350.0f, -950.0f}, {0.0f, -1750.0f, 0.0f}},
    {"st18a", -3.14159f, {-4850.0f, -1650.0f, 3075.0f}, {-2600.0f, -950.0f, 1375.0f}},
    {"st01b", -1.5708f, {-4845.0f, -2175.0f, 6140.0f}, {-4950.0f, -1925.0f, 6225.0f}},
    {"st09a", 3.14159f, {-4425.0f, -1700.0f, 9300.0f}, {-4425.0f, -1700.0f, 9300.0f}},
    {"st03t", 1.5708f, {-3050.0f, -925.0f, 5100.0f}, {-3125.0f, -1350.0f, 5750.0f}},
    {"st10l", 3.14159f, {-1070.0f, 475.0f, 5660.0f}, {-1100.0f, 475.0f, 4750.0f}},
    {"st04a", 1.5708f, {4800.0f, -1750.0f, -1.04092e-13f}, {6500.0f, -1750.0f, 0.0f}},
    {"st04b", 1.5708f, {5725.0f, 205.0f, 3175.0f}, {6550.0f, -1600.0f, 200.0f}},
    {"st04c", 3.14159f, {6555.0f, 840.0f, 12095.0f}, {6550.0f, 350.0f, 9600.0f}},
    {"st04d", -3.14159f, {6550.0f, 1035.0f, 14165.0f}, {6550.0f, 350.0f, 9700.0f}},
    {"st04e", 3.14159f, {6055.0f, 1235.0f, 16190.0f}, {6550.0f, 350.0f, 9850.0f}},
    {"st05b", 1.5708f, {5145.0f, 180.0f, -2605.0f}, {6550.0f, -1600.0f, -50.0f}},
    {"st05c", 0.0f, {6550.0f, 1285.0f, -11885.0f}, {6550.0f, 700.0f, -9450.0f}},
    {"st05d", 0.0f, {6550.0f, 1845.0f, -13955.0f}, {6550.0f, 700.0f, -9450.0f}},
    {"st05e", 0.0f, {5845.0f, 1600.0f, -15810.0f}, {6550.0f, 700.0f, -9550.0f}},
    {"st02a", -1.5708f, {-4035.0f, -765.0f, -3180.0f}, {-3850.0f, -450.0f, -3000.0f}},
    {"st06a", -1.5708f, {-7050.0f, 290.0f, -2950.0f}, {-7050.0f, -450.0f, -2950.0f}},
    {"st22a", 0.0f, {7640.0f, -5670.0f, -2105.0f}, {10400.0f, -5850.0f, 1450.0f}},
    {"st19a", 0.0f, {-6445.0f, -2250.0f, -4785.0f}, {-5300.0f, -2250.0f, -2250.0f}},
    {"st20a", 0.0f, {-4380.0f, -3505.0f, -9725.0f}, {-5050.0f, -2600.0f, -8000.0f}},
    {"st10r", 0.0f, {-685.0f, 5.0f, -5550.0f}, {-1450.0f, 550.0f, -8050.0f}},
    {"st19b", 0.0f, {-5055.0f, -5165.0f, -4865.0f}, {-5300.0f, -2250.0f, -2250.0f}},
    {"st17b", 0.0f, {-12960.0f, -6075.0f, -3520.0f}, {-4950.0f, -7550.0f, -5650.0f}},
    {"st17a", 0.0f, {-14230.0f, -40.0f, 455.0f}, {-18250.0f, -2150.0f, -650.0f}},
    {"st08a", 0.0f, {-9490.0f, 680.0f, 2970.0f}, {-8350.0f, -2550.0f, 450.0f}},
    {"st07a", 3.14159f, {-4300.0f, -475.0f, 2.08167e-13f}, {-6000.0f, 0.0f, -950.0f}},
    {"st00a", -1.5708f, {-10560.0f, -150.0f, 50.0f}, {-9600.0f, -300.0f, 50.0f}},
    {"st13c", -3.14159f, {-18650.0f, -230.0f, 450.0f}, {-18650.0f, -500.0f, 450.0f}},
    {"st13d", 3.14159f, {-18620.0f, 2505.0f, 450.0f}, {-18650.0f, -500.0f, 450.0f}},
    {"st13b", 3.14159f, {-24850.0f, -430.0f, 450.0f}, {-24850.0f, -900.0f, 450.0f}},
    {"st13a", 0.0f, {-24005.0f, -7620.0f, 485.0f}, {-19250.0f, -6350.0f, -2050.0f}},
    {"st24a", -1.5708f, {-21330.0f, -10450.0f, 390.0f}, {-20550.0f, -10450.0f, -700.0f}},
    {"st25a", 0.0f, {-25000.0f, 4640.0f, 450.0f}, {-25000.0f, 5400.0f, 450.0f}},
    {"st24b", -1.5708f, {-20500.0f, -10450.0f, -600.0f}, {-20500.0f, -10450.0f, -600.0f}},
    {"st27a", 1.5708f, {27325.0f, -12310.0f, 77370.0f}, {29200.0f, -12400.0f, 77500.0f}},
    {"E3_st01b", 0.0f, {-14935.0f, 175.0f, 420.0f}, {-14850.0f, 425.0f, 525.0f}},
    {"E3_st09a", -1.5708f, {-11575.0f, 650.0f, 0.0f}, {-11575.0f, 650.0f, 0.0f}},
    {"E3_st04a", -1.5708f, {-16200.0f, 600.0f, -1400.0f}, {-17900.0f, 600.0f, -1400.0f}},
    {"E3_st13c", -3.14159f, {-18650.0f, -230.0f, 450.0f}, {-18650.0f, -500.0f, 450.0f}},
    {"E3_st13d", 3.14159f, {-18620.0f, 2505.0f, 450.0f}, {-18650.0f, -500.0f, 450.0f}},
    {"E3_st13b", 3.14159f, {-24850.0f, -430.0f, 450.0f}, {-24850.0f, -900.0f, 450.0f}},
};

/* the orient index each stage resolves to, -1 where the stage has none */
static short stageOrientIdx[128]; /* derived name */

#include "stage_orient.h"
#include <stdio.h>
#include <string.h>

inline void StageOrientInit(void)
{
    int i, j;
    char buf[64];

    for (i = 0; i < 106; i++) {
        sscanf(stageData[i].name, "%s", buf);
        stageOrientIdx[i] = -1;
        for (j = 0; j < sizeof(stageOrientDefs) / sizeof(stageOrientDefs[0]); j++) {
            if (strcmp(buf, stageOrientDefs[j].name) == 0) {
                stageOrientIdx[i] = j;
                break;
            }
        }
    }
}

/* the matrix that places a stage at its orient record's anchor and angle,
   for GetStageDifferenceMatrix and StageOrientGet2 */
static inline void MakeStageOrientMatrix(float *m, const StageOrientDef *p) /* derived name */
{
    /* The DEBUG build reports how far the stage's second anchor (pos2) sits
       from the one the matrix is built on, through ofs; retail keeps only the
       declaration. */
    VECTOR ofs;
    VECTOR v = {p->pos[0], p->pos[1], p->pos[2], 1.0f};

#ifdef DEBUG
    ofs.x = p->pos2[0] - p->pos[0];
    ofs.y = p->pos2[1] - p->pos[1];
    ofs.z = p->pos2[2] - p->pos[2];
    printf("%s: pos2 - pos = %f %f %f\n", p->name, ofs.x, ofs.y, ofs.z);
#endif

    MatrixDrive_PushMatrix();
    _UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrixV(&v);
    MatrixDrive_RotMatrixY((short)(int)(p->angle * 10430.37793f));
    CopyMatrix(m, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
}

static int GetStageDifferenceMatrix(float *out, int stA, int stB)
{
    int a = stageOrientIdx[stA];
    int b = stageOrientIdx[stB];
    const StageOrientDef *pa;
    const StageOrientDef *pb;
    float m1[16];
    float m2[16];
    float mt[16];

    if (a < 0 || b < 0) {
        _UnitMatrix(out);
        return 0;
    }

    pa = &stageOrientDefs[a];
    pb = &stageOrientDefs[b];

    MakeStageOrientMatrix(m1, pa);
    MakeStageOrientMatrix(m2, pb);

    MatrixDrive_SetTransposeMatrix(mt, m1);
    _MulMatrix(out, mt, m2);
    return 1;
}

inline int StageOrientGet(VECTOR *ret, int stA, int stB)
{
    int a = stageOrientIdx[stA];
    int b = stageOrientIdx[stB];

    if (a < 0 || b < 0) {
        ret->x = 1.0f;
        ret->y = 0.0f;
        ret->z = 0.0f;
        ret->w = 1.0f;
        return 0;
    }
    {
        const StageOrientDef *pa = &stageOrientDefs[a];
        float *va = pa->pos;
        float *vb = stageOrientDefs[b].pos;
        float m[16];
        VECTOR d = {vb[0] - va[0], vb[1] - va[1], vb[2] - va[2], 1.0f};

        sceVu0Normalize(ret, &d);

        sceVu0UnitMatrix(m);
        sceVu0RotMatrixY(m, m, pa->angle);
        sceVu0InversMatrix(m, m);
        sceVu0ApplyMatrix(ret, m, ret);
    }
    ret->y = -ret->y;
    return 1;
}

static int StageOrientGet2(VECTOR *ret, int stA, VECTOR *posA, int stB, VECTOR *posB)
{
    int a = stageOrientIdx[stA];
    int b = stageOrientIdx[stB];

    if (a < 0 || b < 0) {
        ret->x = 1.0f;
        ret->y = 0.0f;
        ret->z = 0.0f;
        ret->w = 1.0f;
        return 0;
    }
    {
        const StageOrientDef *pa = &stageOrientDefs[a];
        const StageOrientDef *pb = &stageOrientDefs[b];
        VECTOR va = {posA->x, -posA->y, posA->z, 1.0f};
        VECTOR vb = {posB->x, -posB->y, posB->z, 1.0f};
        VECTOR ta;
        float m1[16];
        float m2[16];
        VECTOR tb;
        float m3[16];

        sceVu0UnitMatrix(m1);
        sceVu0RotMatrixY(m1, m1, pa->angle);
        m1[12] = pa->pos2[0];
        m1[13] = pa->pos2[1];
        m1[14] = pa->pos2[2];
        m1[15] = 1.0f;
        sceVu0ApplyMatrix(&ta, m1, &va);

        sceVu0UnitMatrix(m2);
        sceVu0RotMatrixY(m2, m2, pb->angle);
        m2[12] = pb->pos2[0];
        m2[13] = pb->pos2[1];
        m2[14] = pb->pos2[2];
        m2[15] = 1.0f;
        sceVu0ApplyMatrix(&tb, m2, &vb);

        sceVu0SubVector(ret, &tb, &ta);

        sceVu0UnitMatrix(m3);
        sceVu0RotMatrixY(m3, m3, pa->angle);
        sceVu0InversMatrix(m3, m3);
        sceVu0ApplyMatrix(ret, m3, ret);
        sceVu0Normalize(ret, ret);
    }
    ret->y = -ret->y;
    return 1;
}

int OtherStagePositionGet(float *ret, int stA, int stB, float *pos)
{
    int a = stageOrientIdx[stA];
    int b = stageOrientIdx[stB];

    if (a < 0 || b < 0) {
        ret[0] = 0.0f;
        ret[1] = 0.0f;
        ret[2] = 0.0f;
        ret[3] = 1.0f;
        return 0;
    }
    {
        const StageOrientDef *pa = &stageOrientDefs[a];
        const StageOrientDef *pb = &stageOrientDefs[b];
        VECTOR v = {pos[0], -pos[1], pos[2], 1.0f};
        VECTOR t;
        float m1[16];
        float m2[16];
        float m3[16];

        sceVu0UnitMatrix(m1);
        sceVu0RotMatrixY(m1, m1, pb->angle);
        m1[12] = pb->pos2[0];
        m1[13] = pb->pos2[1];
        m1[14] = pb->pos2[2];
        m1[15] = 1.0f;
        sceVu0ApplyMatrix(&t, m1, &v);

        sceVu0UnitMatrix(m2);
        sceVu0RotMatrixY(m2, m2, pa->angle);
        m2[12] = pa->pos2[0];
        m2[13] = pa->pos2[1];
        m2[14] = pa->pos2[2];
        m2[15] = 1.0f;
        sceVu0InversMatrix(m3, m2);
        sceVu0ApplyMatrix(ret, m3, &t);
    }
    ret[1] = -ret[1];
    return 1;
}
