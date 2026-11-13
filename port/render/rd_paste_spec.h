/* rd_paste_spec.h: the glow's paste drawn by its own entry (port/render/rd_paste_spec.c;
 * docs/port/RENDER_POST.md, "The glow's paste by its own entry").
 *
 * staticBlur.c pasteFullScreenFlare adds WORK3 (the glow, 424 x 128 on the
 * PowerVR phone) over the whole of SCENE through FIX 128.  The fx plan draws
 * it in the open pass by the premultiplied add (rd_fx_plan.c, FX_ADDITIVE):
 * fx_frame_ps writes |(A - B) C >> 7| and the pipeline adds Cd.  fx_frame_ps
 * is fx_sprite_ps's frame-size build: one module for every post sprite, with
 * the above-1x kernel's two loops, the alpha-only and glow-mask paths, the
 * alpha test, DATE and the destination read in it, all chosen at run time
 * by DrawCB words.  On the phone that one full-size draw is about 3 ms of a
 * lamps picture (build-host design LAMPS3.md section 2).
 *
 * fx_paste_ps is the same source built with ICO_FX_PASTE
 * (port/shaders/fx_sprite.hlsl): the words the paste can never change are
 * constants (TEXTURED and OUT_PREMUL set; ABOVE1X, ALPHA_ONLY, GLOW_MASK,
 * FOLD, ABE, PABE, DATE, DATM, DST clear; TEXFMT RGBA32; no alpha test;
 * the source at scale 1 without a crop), the paste's own words (TFX, TCC,
 * LINEAR, CLAMP_S, CLAMP_T, FBA, the colour, the ALPHA register and FIX,
 * the rect, the UVs, the frame's scale, origin and crop) stay run-time
 * values.  So the module holds the paste's path and no other: no loops,
 * 5 texel loads in it instead of 15 (4 a texel on the paste's bilinear
 * path in both), 3430 SPIR-V words instead of 17985.
 *
 * Exact: the draw goes through fx_paste_ps only when its DrawCB holds the
 * constants' values and its key is the start-up set's (rd__paste_spec_why:
 * every condition, pure), so each value the module computes is the one
 * fx_frame_ps computes from the same words, in the same functions in the
 * same order.  A driver could still lower the two modules' float
 * arithmetic apart, so a start-up self-test (rd__paste_spec_selftest)
 * draws pastes both ways on random sources over random destinations and
 * compares every byte: at the frame's own scale, origin and Full pixel
 * crop on a SCENE-sized target, and at 1x, 2x, a fractional scale and the
 * half-line offset, with each premultiplied add form, nearest and
 * bilinear, CLAMP and REPEAT, TCC and FBA both ways and every TFX.  Any
 * difference keeps fx_frame_ps for the run.
 *
 * Where: every class under Vulkan where the self-test passes (the same draw
 * with a leaner shader: no new kind of GPU work); the module is made on
 * Vulkan alone (OpenGL and Direct3D keep fx_frame_ps, said once).  The
 * MSAA epoch never draws the paste by the fx plan (rd_fx_plan.c), so the
 * paste has no multisampled form.  One pipeline key, in the start-up set
 * wherever the module is made (rd__paste_spec_keys).
 *
 * ICO_RD_PASTE_SPEC=0|1 (the tests' switch, no config key): 0 off, 1 on
 * whatever RD_PASTE_SPEC_DEFAULT (still behind the module and the
 * self-test); =sabotage (the tests): the self-test's own draw of
 * fx_paste_ps given another FIX, so it must refuse. */
#ifndef PORT_RENDER_RD_PASTE_SPEC_H
#define PORT_RENDER_RD_PASTE_SPEC_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

/* shader_consts.h's DrawCB (port/shaders: not on every includer's path) */
struct IcoDrawCB;

#ifdef __cplusplus
extern "C" {
#endif

/* 1: on wherever the module and its self-test allow; 0 (a build's choice):
 * by ICO_RD_PASTE_SPEC=1 alone */
#ifndef RD_PASTE_SPEC_DEFAULT
#define RD_PASTE_SPEC_DEFAULT 1
#endif

/* the module's name in the shader table (port/shaders/CMakeLists.txt) */
#define RD_PASTE_SPEC_NAME "fx_paste_ps"

/* fx_sprite.hlsl FX_PASTE_FIXED, FX_PASTE_FREE: the flags fx_paste_ps holds
 * set, and those it reads from the DrawCB (every other bit clear) */
#define RD_PASTE_SPEC_FIXED ((uint32_t)RD_FXF_TEXTURED | (uint32_t)RD_FXF_OUT_PREMUL)
#define RD_PASTE_SPEC_FREE                                                                         \
    ((3u << RD_FXF_TFX_SHIFT) | (uint32_t)RD_FXF_TCC | (uint32_t)RD_FXF_LINEAR |                   \
     (uint32_t)RD_FXF_CLAMP_S | (uint32_t)RD_FXF_CLAMP_T | (uint32_t)RD_FXF_FBA)

/* why a paste kept fx_frame_ps (the 10 s line, --stats, the log once) */
enum {
    RD_PASTE_SPEC_WHY_NONE = 0, /* drawn by fx_paste_ps */
    RD_PASTE_SPEC_WHY_OFF,      /* ICO_RD_PASTE_SPEC=0, or the build's default 0 */
    RD_PASTE_SPEC_WHY_MODULE,   /* no module: not Vulkan, or the table has none */
    RD_PASTE_SPEC_WHY_TEST,     /* its self-test failed or has not run */
    RD_PASTE_SPEC_WHY_KEY,      /* not the start-up set's key (depth, format, blend, samples) */
    RD_PASTE_SPEC_WHY_FLAGS,    /* a flag fx_paste_ps holds otherwise */
    RD_PASTE_SPEC_WHY_TEXFMT,   /* the source not RGBA32 */
    RD_PASTE_SPEC_WHY_ATEST,    /* an alpha test */
    RD_PASTE_SPEC_WHY_SOURCE,   /* the source scaled or cropped */
    RD_PASTE_SPEC_WHY_PIPE,     /* its pipeline not made */
    RD_PASTE_SPEC_WHY_COUNT
};

const char *rd__paste_spec_why_text(int why);

/* ICO_RD_PASTE_SPEC: -1 unset, 0, 1, 2 sabotage (the tests); env NULL reads
 * the environment, else parses env */
int rd__paste_spec_env(const char *env);

/* The paste's DrawCB and key against fx_paste_ps's constants, pure:
 * RD_PASTE_SPEC_WHY_NONE when fx_paste_ps computes from them exactly what
 * fx_frame_ps computes (key: the draw's, fs RD_FS_FX_FRAME; start: the
 * start-up set's key, rd__paste_spec_general_key) */
int rd__paste_spec_why(const struct IcoDrawCB *cb, const RdPipeKeyInt *key,
                       const RdPipeKeyInt *start);

/* the start-up set's fx_frame_ps key of the paste (the premultiplied add,
 * Z ALWAYS, no Z write, no depth, RGBA8, the colour mask whole) and its
 * fx_paste_ps twin */
RdPipeKeyInt rd__paste_spec_general_key(void);
RdPipeKeyInt rd__paste_spec_key(void);

/* the start-up set (rd_fx_plan.c rd__fx_enumerate): fx_paste_ps's key where
 * the module is made and the switch does not say 0 */
uint32_t rd__paste_spec_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n);

/* rd_fx_plan.c fxDraw, the premultiplied add's draw: the pipeline and key
 * to draw with (fx_paste_ps's where taken, else general and key), counted */
RhiPipeline rd__paste_spec_pipe(const struct IcoDrawCB *cb, const RdPipeKeyInt *key,
                                RhiPipeline general, RdPipeKeyInt *spec, const RdPipeKeyInt **use);

/* in force: the module, the switch, the self-test passed */
bool rd__paste_spec_on(void);

/* rd_lamps.c rd__lamps_init / rd__lamps_shutdown: the module made (Vulkan)
 * and the run's state; the module destroyed */
void rd__paste_spec_init(void);
void rd__paste_spec_shutdown(void);

/* rd_lamps.c rd__lamps_selftest (rd_crack_fill.c rd__crack_swap_selftest:
 * rd_fog_selftest's first run per renderer, rd_replay_tool after rd_init):
 * true when passed or not asked here */
bool rd__paste_spec_selftest(void);
/* 0 not run, 1 passed, 2 failed or not run with a reason; words its line's */
int rd__paste_spec_test_state(const char **words);

/* the 10 s line (rd_lamps.c rd_lamps_line) */
void rd__paste_spec_line(void);
/* the run's counts since rd_init (the tool's --stats): pastes drawn by
 * fx_paste_ps, pastes seen, and the last reason one kept fx_frame_ps */
void rd__paste_spec_counts(uint64_t *taken, uint64_t *seen, int *lastWhy);
/* the last replay's (the A/B): pastes drawn by fx_paste_ps, pastes seen */
void rd__paste_spec_replay(uint32_t *taken, uint32_t *seen);

#ifdef __cplusplus
}
#endif

#endif
