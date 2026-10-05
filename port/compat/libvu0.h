/*
 * port/compat/libvu0.h
 *
 * The host build's libvu0.h: the VU0 entry points the game calls, with the
 * signatures of sce/libvu0/libvu0.h (this project's own clean-room header,
 * MIT). The definitions are package 1A's (port/math/), reimplemented in C
 * behind these signatures. Only what the game uses is declared.
 */
#ifndef ICO_COMPAT_LIBVU0_H
#define ICO_COMPAT_LIBVU0_H

/* 16-byte aligned float quadword and 4x4 matrix. */
typedef float sceVu0FVECTOR[4] __attribute__((aligned(16)));

typedef float sceVu0FMATRIX[4][4] __attribute__((aligned(16)));

void sceVpu0Reset(void);
void sceVu0AddVector(void *dst, void *a, void *b);
void sceVu0ApplyMatrix(void *dst, void *m, void *v);
void sceVu0ClampVector(void *dst, void *src, float min, float max);
void sceVu0CopyMatrix(void *dst, void *src);
void sceVu0CopyVector(void *dst, void *src);
void sceVu0DivVector(void *dst, void *src, float q);
void sceVu0FTOI0Vector(void *dst, void *src);
void sceVu0FTOI4Vector(void *dst, void *src);
void sceVu0ITOF0Vector(void *dst, void *src);
float sceVu0InnerProduct(void *a, void *b);
void sceVu0InterVector(void *dst, void *a, void *b, float t);
void sceVu0InterVectorXYZ(void *dst, void *a, void *b, float t);
void sceVu0InversMatrix(void *dst, void *src);
void sceVu0MulMatrix(void *dst, void *m0, void *m1);
void sceVu0Normalize(void *dst, void *src);
void sceVu0OuterProduct(void *dst, void *a, void *b);
void sceVu0RotMatrixX(void *d, void *s, float a);
void sceVu0RotMatrixY(void *d, void *s, float a);
void sceVu0RotMatrixZ(void *d, void *s, float a);
void sceVu0RotTransPers(void *dst, void *m, void *src, int mode);
void sceVu0ScaleVector(void *dst, void *src, float scale);
void sceVu0ScaleVectorXYZ(void *dst, void *src, float scale);
void sceVu0SubVector(void *dst, void *a, void *b);
void sceVu0TransposeMatrix(void *dst, void *src);
void sceVu0UnitMatrix(void *m);

#endif /* ICO_COMPAT_LIBVU0_H */
