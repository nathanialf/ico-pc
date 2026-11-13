/* rd_comp_spec.h: the shadow composites and levels drawn by their own entry,
 * sprite_comp_ps (port/render/rd_comp_spec.c; docs/port/RENDER_POST.md, "The
 * shadow composites by their own entry").
 *
 * The shadow chain's screen sprites (shadow.c through the replay's doScreen:
 * the three levels SHADOW0, 1 and 2, each a sprite over its whole level, and
 * the three composites into SCENE, each a sprite over the frame under ATST
 * GREATER 0 and the LERP (Cs - Cd) As + Cd) are drawn by sprite_ps, the
 * screen prims' general entry (9942 SPIR-V words, three loops: the above-1x
 * kernels, the fades fold, the GS levels, DATE by the snapshot), of which
 * they take one path: the vertex colour, the sampler's texel, MODULATE with
 * TCC, the alpha test, the two outputs.  sprite_comp_ps (sprite.hlsl, built
 * with ICO_SPRITE_COMP=1: 1014 words, no loop) is that path alone, the same
 * statements in the same order.
 *
 * Exact: a draw goes through sprite_comp_ps only when its key is of the
 * composite's or the level's form (rd_comp_spec_key_form) on a dual-source
 * device of the plain build (no destination read, single-sampled), and its
 * DrawCB holds, of the flags sprite_ps reads, exactly RD_COMP_SPEC_FIXED
 * (rd_comp_spec_flags_why) and an RGBA32 texel.  The pipeline is the
 * general key's with the fragment entry alone changed, so every fixed-function
 * state (the blend, the depth and stencil tests, the mask) is the same; and
 * a start-up self-test draws the shadow chain's sprite forms both ways on
 * SCENE and compares every byte (rd__comp_spec_selftest), because the
 * driver compiles a second module and nothing promises it rounds the same
 * expressions the same way.
 *
 * Where: rd_pixel_plan.c rd__fill_draw_cb_extra (the DrawCB final, the key
 * the run draws with) notes a draw the gate takes (rd__comp_spec_note);
 * rd_pipeline.c rd__set_pipeline binds the twin's pipeline in its place when
 * the screen run that command opened is drawn (rd__comp_spec_turn: the
 * owner g_rdDrawOwner, the same key, the general pipeline).  The start-up set
 * holds every enumerated key of the two forms' twins (rd__comp_spec_keys).
 * rd_replay.c is not touched.  Vulkan alone (the module is made there; the
 * other backends keep sprite_ps, said once).
 *
 * Switches (the tests only, no config key): ICO_RD_COMP_SPEC=0|1|sabotage;
 * the sabotage draws the self-test's second arm with one band's vertex alpha
 * one lower, so it must fail.  RD_COMP_SPEC_DEFAULT 1 (on wherever the module
 * and the self-test allow); -DRD_COMP_SPEC_DEFAULT=0 takes it out of a build. */
#ifndef PORT_RENDER_RD_COMP_SPEC_H
#define PORT_RENDER_RD_COMP_SPEC_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

/* shader_consts.h's DrawCB (port/shaders: not on every includer's path) */
struct IcoDrawCB;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RD_COMP_SPEC_DEFAULT
#define RD_COMP_SPEC_DEFAULT 1
#endif

/* the module's name in the shader table (port/shaders/CMakeLists.txt) */
#define RD_COMP_SPEC_NAME "sprite_comp_ps"

/* DrawCB.mode[0]'s bits sprite_ps reads in its dual-source plain build
 * (sprite.hlsl sprite_ps, common.hlsli): DATE (the snapshot's test), TEXTURED,
 * ABOVE1X (sprite_above1x), GS_LOD and NEAREST (gs_sample; GS_BILINEAR_GRID
 * under GS_LOD alone), FOLD, DECAL and TCC_RGBA (gs_texture_function), FIX_FACTOR,
 * PABE, FBA, PREMUL and C1_DST (gs_dual_out).  Every other bit is read by
 * another entry alone (the TEXA, AA1, VU, two-pass and destination-read
 * builds) or under one of these (DATM under DATE, the TEXA clamps under
 * ABOVE1X) */
#define RD_COMP_SPEC_READ                                                                          \
    (0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x40u | 0x80u | 0x8000u | 0x40000u | 0x1000000u | \
     0x2000000u | 0x20000000u)
/* sprite.hlsl SPRITE_COMP_FLAGS: TEXTURED and TCC_RGBA (MODULATE) */
#define RD_COMP_SPEC_FIXED (0x1u | 0x4u)

/* the key's form (rd_comp_spec_key_form) */
enum {
    RD_COMP_SPEC_FORM_NONE = 0,
    RD_COMP_SPEC_FORM_COMPOSITE, /* a LERP, Z write off, mask RGBA (the alpha test is DrawCB words) */
    RD_COMP_SPEC_FORM_LEVEL,     /* no blend, no alpha test, no depth, mask RGBA */
};

/* why a draw of the forms kept sprite_ps (the 10 s line, --stats, the log once) */
enum {
    RD_COMP_SPEC_WHY_NONE = 0, /* drawn by sprite_comp_ps */
    RD_COMP_SPEC_WHY_OFF,      /* ICO_RD_COMP_SPEC=0, or the build's default 0 */
    RD_COMP_SPEC_WHY_MODULE,   /* no module: not Vulkan, or the table has none */
    RD_COMP_SPEC_WHY_TEST,     /* its self-test failed or has not run */
    RD_COMP_SPEC_WHY_DEVICE,   /* no dual-source blend, the exact blend, a multisampled pass */
    RD_COMP_SPEC_WHY_FLAGS,    /* a flag sprite_comp_ps holds otherwise (DATE by the snapshot,
                                  DECAL, above 1x, GS levels, nearest, FBA, PABE, ...) */
    RD_COMP_SPEC_WHY_TEXFMT,   /* the texel not RGBA32 (a TEXA expansion) */
    RD_COMP_SPEC_WHY_DRAW,     /* noted, but drawn outside a screen run of its own (the
                                  immediate path, or joined to another command's run, which
                                  draws with its owner's pipeline: none in the shadow chain,
                                  whose sprites read other textures or targets) */
    RD_COMP_SPEC_WHY_PIPE,     /* its pipeline not made */
    RD_COMP_SPEC_WHY_COUNT
};

const char *rd__comp_spec_why_text(int why);

/* ICO_RD_COMP_SPEC: -1 unset, 0, 1, 2 sabotage (the tests); env NULL reads
 * the environment */
int rd__comp_spec_env(const char *env);

/* The key's form, pure: RD_COMP_SPEC_FORM_* (NONE for any key that is not
 * sprite_ps's dual-source plain build of a triangle list into RGBA8 with one
 * of the two forms' fixed-function states) */
int rd_comp_spec_key_form(const RdPipeKeyInt *k);
/* The DrawCB's words against sprite_comp_ps's constants, pure:
 * RD_COMP_SPEC_WHY_NONE, _FLAGS or _TEXFMT */
int rd_comp_spec_flags_why(uint32_t mode0, uint32_t mode1);
/* the key's twin: the fragment entry sprite_comp_ps, every other field the same */
RdPipeKeyInt rd__comp_spec_twin(const RdPipeKeyInt *general);

/* the start-up set (rd_pipeline.c enumerateAll, after every family): the
 * twin of each of out[0 .. n0 - 1] of the two forms, where the module is
 * made and the switch wants it, on a dual-source device without the exact
 * blend */
uint32_t rd__comp_spec_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n, uint32_t n0);

/* rd_pixel_plan.c rd__fill_draw_cb_extra: a draw pass's final DrawCB and
 * key; a draw the gate takes is noted for its run (counted where it is of
 * the forms) */
void rd__comp_spec_note(const RdPipeKeyInt *key, const struct IcoDrawCB *cb);
/* rd_pipeline.c rd__set_pipeline: the pipeline to bind for key k, p the
 * general one; the twin's where a noted draw's screen run is drawn now */
RhiPipeline rd__comp_spec_turn(RhiPipeline p, const RdPipeKeyInt *k);

/* in force: the module, the switch, the self-test passed */
bool rd__comp_spec_on(void);
/* the bench's switch (rd_bench.c): 0 off, 1 on (still behind the module and
 * the self-test), -1 the switch's own */
void rd__comp_spec_force(int v);

/* rd_lamps.c rd__lamps_init / rd__lamps_shutdown: the module made (Vulkan)
 * and the state cleared for this renderer; the module destroyed */
void rd__comp_spec_init(void);
void rd__comp_spec_shutdown(void);

/* rd_fog_path.c rd_fog_selftest's first run a renderer (and rd_replay_tool
 * where ICO_RD_COMP_SPEC is set): the frame both ways, compared byte for
 * byte; true when passed or not asked */
bool rd__comp_spec_selftest(void);
/* 0 not run, 1 passed, 2 failed or not run with a reason; words its line's */
int rd__comp_spec_test_state(const char **words);

/* the 10 s line (rd_lamps.c rd_lamps_line) */
void rd__comp_spec_line(void);
/* the run's counts since rd_init (the tool's --stats; the self-test's
 * replays left out): draws by sprite_comp_ps, draws of the forms seen, the
 * last reason one kept sprite_ps */
void rd__comp_spec_counts(uint64_t *taken, uint64_t *seen, int *lastWhy);
/* the last replay's (the A/B): draws by sprite_comp_ps, draws of the forms */
void rd__comp_spec_replay(uint32_t *taken, uint32_t *seen);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RENDER_RD_COMP_SPEC_H */
