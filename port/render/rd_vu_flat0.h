/* rd_vu_flat0.h: the main vertex shader's flat outputs at the provoking
 * corner (port/shaders/vu_tri.hlsl vu_main_vs, ICO_VU_FLAT0;
 * docs/port/PIPELINES.md, "Flat outputs at the provoking corner").
 *
 * Every mesh and grid draw through the record runs vu_main_vs three times
 * a triangle (index value kick * 4 + corner, no vertex reuse).  Its seven
 * flat outputs (VuVSOut a0, a1, x0, x1, y0, y1, r: the attribute planes)
 * are taken by the rasteriser from the triangle's provoking vertex, its
 * first index value, which is corner 0 in every list the replay draws and
 * on every backend; so the shipped build reads the planes (five record
 * loads) at corner 0 alone, and the later bias's own corner from the record
 * texels the corner's vertex was already read from.  The picture is the
 * same byte for byte (the test rd_vu_flat0_ab).
 *
 * The tests' switch ICO_RD_VU_FLAT0 (docs/port/TESTING_PROGRAMS_RENDER.md;
 * no setting): unset, empty or 1 the shipped build; 0 the twins built with
 * ICO_VU_FLAT0=0 (vu_all_main_vs, vu_merge_all_main_vs: the shader before,
 * every corner computing them) in place of vu_main_vs and
 * vu_merge_main_vs. */
#ifndef RD_VU_FLAT0_H
#define RD_VU_FLAT0_H

#include <stdbool.h>
#include <stddef.h>

/* The record loads of one vu_main_vs invocation (record texels: vu_rec_buf
 * of port/shaders/vu_common.hlsli) on a drawn triangle of a draw alone
 * (not merged, no tick cameras, no chain plane, no later bias) whose planes
 * hold (exact, under the exact viewport): the corner's vertex 3
 * (vu_rec_vertex: its three planes), the triangle's slot A 1 (its flags
 * and the anchor), the planes 5 (vu_rec_planes: slot A's planes 1 and 2,
 * slot B's three).  Corner 0 reads all 9; corners 1 and 2 read 4 in the
 * shipped build, 9 in the twins.  Draws that add more add the same at
 * every corner (the merge's table and constants, slot C under the tick
 * cameras, the chain plane's stream qword), except the later bias: 3 more
 * (plane 1 of each corner) in the twins, 2 in the shipped build. */
#define RD_VU_FLAT0_LOADS_FIRST 9u
#define RD_VU_FLAT0_LOADS_OTHER 4u
#define RD_VU_FLAT0_LOADS_ALL 9u

/* The switch's word: NULL, "" or "1" on, "0" off; anything else on with
 * *bad set (bad may be NULL). */
bool rd_vu_flat0_word(const char *e, bool *bad);

/* The start line's text after "rd: " into buf (size bytes; snprintf's
 * count back).  on: the shipped build; missing: the twin that could not be
 * made under ICO_RD_VU_FLAT0=0 (NULL: none; on is then true as well). */
int rd_vu_flat0_line(char *buf, size_t size, bool on, const char *missing);

/* At rd_init, after the draw merge's modules (rd_replay_vu.c
 * rd__vu_prepass_init): under ICO_RD_VU_FLAT0=0 the twins in place of
 * g_rd.vs[RD_VS_VU_MAIN] and g_rd.vsMerge[RD_VS_VU_MAIN] (each where the
 * shipped one was made; a twin that cannot be made keeps both shipped
 * ones); the line. */
void rd__vu_flat0_init(void);

#endif
