#include "memory.h"
#include "DisplayP2O.h"
#include "matrixDrive.h"
#include "tableSin.h"
#include "sugiTree.h"
#include <stdlib.h>
#include <libvu0.h>
#include "ios.h"

inline short *InitSugiLeafGeo(void)
{
    short *h = iosMallocDebug(ios_partition_sugipon, sizeof(short), "src/sugiTree.c", 12);
    int r = rand();
    *h = r % 65536;
    return h;
}

inline void SugiLeafGeo(GObj *gobj)
{
    Sub15C *p = GOBJ_SUB(gobj);
    short *ang = p->work;

    CopyMatrix(MatrixDrive_GetMatrix(), &p->matrix);
    MatrixDrive_RotMatrixY((short)(int)(GetTableSin(*ang) * 256.0f));
    MatrixDrive_RotMatrixX((short)(int)(GetTableSin(*ang * 2) * 256.0f));
    CopyMatrix((void *)p->nodeMtx, MatrixDrive_GetMatrix());
    *ang += 128;
}

inline short *InitSugiLeafGeo2(GObj *gobj)
{
    Sub15C *p = GOBJ_SUB(gobj);
    int n = p->model->partCount;
    short *buf = iosMallocDebug(ios_partition_sugipon, n * 2, "src/sugiTree.c", 35);
    int i;

    for (i = 0; i < n; i++) {
        buf[i] = rand() % 65536;
    }
    return buf;
}

void SugiLeafGeo2(GObj *gobj)
{
    Sub15C *p = GOBJ_SUB(gobj);
    int n = p->model->partCount;
    short *ang = p->work;
    int i;

    for (i = 0; i < n; i++) {
        if (i == n - 1) {
            CopyMatrix((char *)p->nodeMtx + i * 64, &p->matrix);
        } else {
            CopyMatrix((char *)p->nodeMtx + i * 64, &p->matrix);
            CopyMatrix(MatrixDrive_GetMatrix(), (char *)p->model->parts[i].mtx);
            p->nodes[i].rot[0] = (int)(GetTableCos((short)((ang[i / 3] * 9 + i) * 10)) * 768.0f);
            p->nodes[i].rot[1] = (int)(GetTableSin((short)((ang[i / 3] * 6 + i) * 16)) * 768.0f);
            MatrixDrive_RotMatrixY(*(short *)&p->nodes[i].rot[1]);
            MatrixDrive_RotMatrixX(*(short *)&p->nodes[i].rot[0]);
            sceVu0MulMatrix((char *)p->nodeMtx + i * 64, (char *)p->nodeMtx + i * 64,
                            MatrixDrive_GetMatrix());
            ang[i / 3]++;
        }
    }
}

void SugiLeafDL2(GObj *gobj)
{
    Sub15C *p = GOBJ_SUB(gobj);
    int n = p->model->partCount;
    char save[n][64];
    int i;

    for (i = 0; i < n; i++) {
        PObjPart *m = &p->model->parts[i];

        CopyMatrix(save[i], m->mtx);
    }
    p2o_DispVU1Default(gobj);
    for (i = 0; i < n; i++) {
        char *m = (char *)p->model->parts[i].mtx;

        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrix(0.0f, -0.5f, 0.0f);
        MatrixDrive_ScaleMatrix(1.0f, 0.0f, 1.0f);
        MatrixDrive_RotMatrixX(8192);
        sceVu0MulMatrix(m, MatrixDrive_GetMatrix(), m);
    }
    for (i = 0; i < n; i++) {
        CopyMatrix((char *)p->model->parts[i].mtx, save[i]);
    }
}
