#include "typedef.h"
#include "StageAnimation.h"
#include "itou_sub.h"
#include "Matrix.h"
#include <libvu0.h>

inline void lw_pos_to_ico_pos(float *dst, float *src)
{
    dst[0] = -src[0];
    dst[1] = -src[1];
    dst[2] = -src[2];
    dst[3] = src[3];
}

/* out = m applied to in, with w = 1; the VU0 code reads the three arguments
   from their registers ($4 out, $5 m, $6 in) */
inline void apply_matrix_w1(void *out, void *m, void *in)
{
#ifdef ICO_HOST
    float mm[4][4];

    __builtin_memcpy(mm, m, sizeof mm);
    ico_apply_matrix_w1((float *)out, (const float (*)[4])mm, (const float *)in);
#else
    VU0_LSV(lqc2, 4, 0x0, 5);
    VU0_LSV(lqc2, 5, 0x10, 5);
    VU0_LSV(lqc2, 6, 0x20, 5);
    VU0_LSV(lqc2, 7, 0x30, 5);
    VU0_LSV(lqc2, 8, 0x0, 6);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 8, z);
    VU0_V3OP_BC(vmaddw.xyzw, 9, 7, 0, w);
    VU0_LSV(sqc2, 9, 0x0, 4);
#endif
}

static void m33_to_quat(float *q, float (*m)[4])
{
    float tr;
    float s;
    float t;
    int i;
    int j;
    int k;

    tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        s = _Sqrt(tr + 1.0f);
        q[3] = s * 0.5f;
        t = 0.5f / s;
        q[0] = (m[1][2] - m[2][1]) * t;
        q[1] = (m[2][0] - m[0][2]) * t;
        q[2] = (m[0][1] - m[1][0]) * t;
    } else {
        int nxt[3] = {1, 2, 0};

        i = 0;
        if (m[1][1] > m[0][0]) {
            i = 1;
        }
        if (m[2][2] > m[i][i]) {
            i = 2;
        }
        j = nxt[i];
        k = nxt[j];
        s = _Sqrt(m[i][i] - (m[j][j] + m[k][k]) + 1.0f);
        q[i] = s * 0.5f;
        t = (s != 0.0f) ? 0.5f / s : 0.0f;
        q[3] = (m[j][k] - m[k][j]) * t;
        q[j] = (m[i][j] + m[j][i]) * t;
        q[k] = (m[i][k] + m[k][i]) * t;
    }
}

inline void ico_m33_to_quat(void *q, void *m)
{
    float buf[4][4];
    sceVu0TransposeMatrix(buf, m);
    m33_to_quat(q, buf);
}

inline void pbga_start(BgaPlayNode **slot, int key)
{
    if (*slot != 0) {
        stage_KillPlayBgAnimation(slot);
    }
    *slot = stage_MakePlayBgAnimation(key);
}
