/* hlsl_shim.h: lets gs_math.hlsli, which is valid HLSL, compile as C. */
#ifndef PORT_SHADERS_TEST_HLSL_SHIM_H
#define PORT_SHADERS_TEST_HLSL_SHIM_H

#include <stdbool.h>
#include <stdint.h>

#define ICO_GS_MATH_C 1

typedef uint32_t uint;

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(x, lo, hi) (min(max((x), (lo)), (hi)))

#endif
