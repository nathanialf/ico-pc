/*
 * port/math/ico_math.h
 *
 * The host's replacement for what the game kept in VU0 registers between
 * calls, and the C routines that game sources call from their ICO_HOST
 * branches. ico2/common/include/typedef.h includes it (and ps2float.h) for
 * the host build, so every game TU sees it.
 *
 * docs/port/MATH.md describes each routine against the assembly it replaces.
 */
#ifndef ICO_MATH_ICO_MATH_H
#define ICO_MATH_ICO_MATH_H

#include <stdint.h>

#include "ps2float.h"
#include "vector_inline.h"

/* The "current matrix": seki/src/Matrix.c kept it in VU0 registers
   vf4-vf7 (one row each) and every _*CurrentMatrix routine works on it.
   port/math/matrix_stack.c. */
extern float ico_current_matrix[4][4];

/* The host halves of seki/src/Matrix.c's _PushVu0Registers and
   _PopVu0Registers: save and restore the current matrix. matrix_stack.c. */
void ico_vu0_registers_push(void);
void ico_vu0_registers_pop(void);

/* out = m applied to v: m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3],
   summed left to right, all four fields (VU0's vmulax/vmadday/vmaddaz/
   vmaddw sequence). out may alias v. v[3] is read through ps2_operand: an
   exponent-255 w (a stale lane) is +-Fmax as on VU0, not Inf or NaN. */
void ico_apply_matrix(float *out, const float (*m)[4], const float *v);

/* As ico_apply_matrix with v[3] taken as 1 (the assembly multiplies row 3
   by vf0.w). */
void ico_apply_matrix_w1(float *out, const float (*m)[4], const float *v);

/* Rows 0-2 of the rotation matrix of quaternion q (w column 0); row 3 is
   left alone. sugipon/src/quaternion.c's GetMatrixFromQuaternion* bodies.
   port/math/quaternion.c. */
void ico_quaternion_rotation_rows(float (*m)[4], const float *q);
/* x*x + y*y + z*z + w*w, in that order (RegularizeQuaternion). */
float ico_quaternion_norm2(const float *q);

/* VU0's R register (seki/src/Matrix.c _InitRandom/_GetRandom):
   ito/src/lightning.c writes it directly (ctc2 to $vi20); this is the host
   form of that write. The low 23 bits of `bits` are kept. */
void ico_vu0_random_set(uint32_t bits);
uint32_t ico_vu0_random_get(void);

#endif /* ICO_MATH_ICO_MATH_H */
