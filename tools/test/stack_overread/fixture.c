/* tools/test/stack_overread/fixture.c: tools/stack_overread_audit.py --selftest
   runs the audit on this file. Each line marked EXPECT: <kind> must give that
   hit and no other line may give one. Never compiled. */
#include <string.h>

typedef union {
    float f[4];
    long long ll[2];
} __attribute__((aligned(16))) QVec;

typedef struct {
    QVec x;
    QVec y;
    QVec z;
} QMat3;

typedef struct {
    QVec x;
    QVec y;
    QVec z;
    QVec w;
} QMat44;

typedef struct {
    QMat3 rot;
    QVec trans;
} QMat3T;

typedef struct {
    float v[4];
} LVec;

static inline void UnitMatrix33(QMat3 *m)
{
    m->x.f[0] = 1.0f;
    m->y.f[1] = 1.0f;
    m->z.f[2] = 1.0f;
}

/* issue 12's shape: a 3x3 local, the translation in the next local */
void barrierBefore(void *node, QVec *pos)
{
    QMat3 rot;
    QVec trans; /* EXPECT: write-only */

    UnitMatrix33(&rot);
    sceVu0CopyVector(&trans, pos);
    CopyMatrix(node, &rot); /* EXPECT: overread */
}

/* the fix: one 64-byte record */
void barrierAfter(void *node, QVec *pos)
{
    QMat3T rt;

    UnitMatrix33(&rt.rot);
    sceVu0CopyVector(&rt.trans, pos);
    CopyMatrix(node, &rt);
}

void rowsAndCasts(void *node, float *src)
{
    float m34[3][4];
    LVec rows[3];
    float m44[4][4];
    float v3[3];
    QMat44 full;
    QMat3 r3;
    float ms[2][4][4];
    int i;

    sceVu0CopyMatrix(m34, src); /* EXPECT: overread */
    CopyMatrix(node, m34);      /* EXPECT: overread */
    _CopyMatrix((void *)rows, node); /* EXPECT: overread */
    CopyMatrix(node, rows);     /* EXPECT: overread */
    sceVu0CopyMatrix(m44, src);
    CopyMatrix(node, m44);
    sceVu0MulMatrix(m44, m44, &full.x); /* the whole of full from x on */
    sceVu0MulMatrix(m44, m44, &full.y); /* EXPECT: overread */
    sceVu0CopyVector(v3, src);  /* EXPECT: overread */
    memcpy(&r3, src, 64);       /* EXPECT: overread */
    memcpy(&r3, src, sizeof(r3));
    memcpy(&r3, src, 48);
    for (i = 0; i < 2; i++) {
        CopyMatrix(node, ms[i]);
    }
    CopyMatrix(node, &full);
    sceVu0ApplyMatrix(v3, m44, src); /* EXPECT: overread */
    sceVu0ApplyMatrix(src, (float *)rows, src); /* EXPECT: overread */
    CopyMatrix(node, (char *)m44 + 0);
    if (v3[0] < r3.x.f[0] && full.w.f[0] > 0.0f && rows[0].v[0] > 0.0f && m34[0][0] > 0.0f) {
        CopyMatrix(node, ms);
    }
}

/* a block-scoped local shadows an outer one of another size */
void shadowed(void *node, float *src)
{
    float p[4];

    sceVu0CopyVector(p, src);
    {
        float p[4][4];

        sceVu0UnitMatrix(p);
        CopyMatrix(node, p);
    }
    {
        float p[3][4];

        CopyMatrix(node, p); /* EXPECT: overread */
    }
    src[0] = p[0];
}

/* written by a known writer and by an assignment, read nowhere */
void deadStores(float *src)
{
    float a[4]; /* EXPECT: write-only */
    float b[4];
    int n; /* EXPECT: write-only */

    sceVu0CopyVector(a, src);
    a[1] = 0.0f;
    sceVu0CopyVector(b, src);
    n = 3;
    if (b[0] == 1.0f) {
        src[1] = 2.0f;
    }
}
