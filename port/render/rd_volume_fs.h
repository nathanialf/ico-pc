/* rd_volume_fs.h: the shadow volumes drawn by a fragment entry with no
 * output and no discard (port/render/rd_volume_fs.c; docs/port/RENDER_POST.md,
 * "The shadow volumes' stencil-only entry").
 *
 * Shadow.c's volumes (RDC_SHADOW_STRIP, rd_replay.c doShadowStripIn) write
 * the stencil alone: INCR_WRAP or DECR_WRAP on both faces where the depth
 * test passes, under write mask RD_SHADOW_STENCIL_MASK, colour mask 0, no
 * depth write (rd_pipeline.c rd__shadow_volume_key, shadowStencil).  Their
 * pipeline is sprite_world_vs with sprite_ps, the screen prims' general
 * fragment entry (port/shaders/sprite.hlsl): two dual-source outputs, the
 * texture, fold and TEXA paths, and two discards (DATE and the alpha test),
 * none of them taken for a volume (its DrawCB is zero: g_mode.x without
 * DF_DATE or DF_TEXTURED, g_mode.z without the alpha test's enable bit), but
 * all of them in the module.  A fragment shader that can discard keeps the
 * depth and stencil tests and the stencil's write after the shader on every
 * GPU, and on a PowerVR makes each volume a punch-through object: every
 * covered fragment is shaded and its stencil written only after the
 * shader's answer, inside the scene's pass.  The phone's bench (v0.11.6
 * rc4, bench-20261113-065800.csv) priced the 8 and 20 volume draws of its
 * two shadow pictures at 5.50 and 7.19 ms of GPU time.
 *
 * date_clear_ps (sprite.hlsl: void, no output, no read, no discard; DATE's
 * stencil set-up already draws it under a colour mask of 0) is the
 * volumes' fragment entry here: the key's fs RD_FS_DATE_CLEAR in place of
 * RD_FS_SPRITE, every other field the same (rd__volume_fs_twin).  The
 * vertex entry, its positions and the rasterizer state are unchanged, so
 * the fragments covered are the same; the stencil op, its masks and the
 * depth test are fixed-function state, unchanged; sprite_ps writes nothing
 * a volume keeps (colour mask 0) and never discards one.  So the stencil
 * after the volumes is the same, bit for bit, by the Vulkan rules
 * (fragment operations; early fragment tests are allowed exactly when the
 * shader neither discards nor writes depth, and give the same results).
 *
 * Exactness is still checked on the device: a start-up self-test
 * (rd__volume_fs_selftest, from rd_lamps.c rd__lamps_selftest, the crack
 * swap's group: rd_fog_selftest's first run per renderer and rd_replay_tool
 * after rd_init) replays the shadow count's self-test frame
 * (rd_shadow_selftest.c: 70 bands of volume rectangles at sub-pixel edges,
 * every stencil value 0..63 of both signs) under Z ALWAYS and under the
 * game's Z GEQUAL, each with the volumes by sprite_ps and by
 * date_clear_ps, reads the count back at its GS size and compares every
 * byte; any difference keeps sprite_ps for the run.
 *
 * Where: every class under Vulkan whose self-test passes, single-sampled
 * passes alone (the MSAA epoch keeps sprite_ps: its count is not in the
 * self-test).  OpenGL and Direct3D keep sprite_ps (said once).  The two
 * keys of each Z test (INCR and DECR) are in the start-up set beside
 * sprite_ps's (rd__volume_fs_keys).
 *
 * ICO_RD_VOLUME_FS=0|1|sabotage (the tests' switch, no config key): 0 off,
 * 1 on whatever RD_VOLUME_FS_DEFAULT (still behind the backend, the
 * self-test and the pass's samples); sabotage: the self-test's own
 * date_clear_ps replay draws each increment as a decrement, so it must
 * refuse. */
#ifndef PORT_RENDER_RD_VOLUME_FS_H
#define PORT_RENDER_RD_VOLUME_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1: on wherever the backend, the self-test and the pass allow; 0 (a
 * build's choice): by ICO_RD_VOLUME_FS=1 alone */
#ifndef RD_VOLUME_FS_DEFAULT
#define RD_VOLUME_FS_DEFAULT 1
#endif

/* why a volume draw kept sprite_ps (the 10 s line, --stats, the log once) */
enum {
    RD_VOLUME_FS_WHY_NONE = 0, /* drawn by date_clear_ps */
    RD_VOLUME_FS_WHY_OFF,      /* ICO_RD_VOLUME_FS=0, or the build's default 0 */
    RD_VOLUME_FS_WHY_BACKEND,  /* not Vulkan, no device or no date_clear_ps module */
    RD_VOLUME_FS_WHY_TEST,     /* its self-test failed or has not run */
    RD_VOLUME_FS_WHY_MSAA,     /* a multisampled pass (the MSAA epoch) */
    RD_VOLUME_FS_WHY_PIPE,     /* its pipeline not made */
    RD_VOLUME_FS_WHY_COUNT
};

const char *rd__volume_fs_why_text(int why);

/* ICO_RD_VOLUME_FS: -1 unset, 0, 1, 2 sabotage (the tests); env NULL reads
 * the environment, else parses env */
int rd__volume_fs_env(const char *env);

/* The gate, pure: wanted (the switch or the default), offered (a Vulkan
 * device with date_clear_ps's module), test (0 not run, 1 passed, 2
 * failed), samples (the pass's, 0 or 1 single-sampled); the reason, NONE
 * when the volume draw may take date_clear_ps */
int rd__volume_fs_why(int wanted, int offered, int test, uint32_t samples);

/* The volume key's twin: fs RD_FS_DATE_CLEAR, every other field general's
 * (a key that is not a volume's comes back unchanged) */
RdPipeKeyInt rd__volume_fs_twin(const RdPipeKeyInt *general);

/* rd_pipeline.c rd__shadow_volume_key (the volume draw's key, rd_replay.c
 * doShadowStripIn): general's twin where the gate takes it and its
 * pipeline is made, else general; counted per replay, run and 10 s block
 * and in the attempt ledger (decr: the decrement's key, the sabotage's
 * turn) */
RdPipeKeyInt rd__volume_fs_turn(const RdPipeKeyInt *general, int decr);

/* the start-up set (rd_pipeline.c rd__enumerate_reachable_shadow): the twin
 * of each of the n0 general keys where the cut is offered and wanted */
uint32_t rd__volume_fs_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n,
                            const RdPipeKeyInt *general, uint32_t n0);

/* in force: offered, wanted, the self-test passed */
bool rd__volume_fs_on(void);

/* the bench's switch (rd_bench.c): 0 off, 1 on (still behind the backend,
 * the self-test and the pass), -1 the environment's and the default again */
void rd__volume_fs_force(int v);
/* wanted now (the force, the switch, the default) */
bool rd__volume_fs_wanted(void);

/* rd_lamps.c rd__lamps_init: this renderer's state (the self-test not run) */
void rd__volume_fs_init(void);
/* rd_lamps.c rd__lamps_selftest: true when passed or not asked here */
bool rd__volume_fs_selftest(void);
/* 0 not run, 1 passed, 2 failed or not run with a reason; words its line's */
int rd__volume_fs_test_state(const char **words);

/* the 10 s line (rd_lamps.c rd_lamps_line) */
void rd__volume_fs_line(void);
/* the run's counts since rd_init (the tool's --stats): volume draws by
 * date_clear_ps, volume draws seen, the last reason one kept sprite_ps */
void rd__volume_fs_counts(uint64_t *taken, uint64_t *seen, int *lastWhy);
/* the last replay's (the A/B): volume draws by date_clear_ps, seen */
void rd__volume_fs_replay(uint32_t *taken, uint32_t *seen);

/* rd_shadow_selftest.c, the shadow count's self-test frame for the
 * volumes' own self-test: built on SCENE (scene, its GS w x h) under the
 * running disc's step with the GS TEST word test (RD_TEST_Z_ALWAYS or
 * RD_TEST_Z_GEQUAL); replayed from SCENE's clear with the count at its GS
 * size read into px (w * h * 4 bytes) */
bool rd__shadow_selftest_frame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, uint32_t step,
                               uint64_t test);
bool rd__shadow_selftest_replay(const RdFrame *f, uint32_t scene, uint8_t *px, size_t size,
                                uint32_t w, uint32_t h);

#ifdef __cplusplus
}
#endif

#endif
