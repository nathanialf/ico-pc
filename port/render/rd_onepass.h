/* rd_onepass.h: a LERP As VU draw of the two-pass blend drawn in one pass where
 * the CPU proves the same bytes (port/render/rd_onepass.c; docs/port/RENDER_POST.md,
 * "The two-pass blend in one pass"; issue 179).
 *
 * Without dual-source blending (g_rd.noDual) a (Cs - Cd) As + Cd draw whose
 * mask has R, G or B and A is two passes (rd_pipeline.c rd__expand_no_dual):
 * a colour pass whose c0.a is the factor As/128 under SRC_ALPHA /
 * ONE_MINUS_SRC_ALPHA, then an alpha pass writing As/255 with blending off.
 *
 * The carry: where this rule is in force the colour pass also writes A
 * (RdPipelineKey.nodual RD_NODUAL_CARRY, mask with A), its alpha blended as
 * c0.a times the blend constant's alpha 128/255 (rd_pipeline.c blendState,
 * rd__onepass_bind) and nothing of Ad: (As/128) (128/255) = As/255.  The
 * alpha pass after it admits every fragment the colour pass wrote and
 * overwrites that alpha, so a draw that keeps its alpha pass stores what it
 * stored before, whatever the carried alpha was.  The colour pass's key
 * changes one for one (mask and nodual): no pipeline is added.
 *
 * The one pass: a pair whose stored alpha the carry gives exactly drops its
 * alpha pass (rd__onepass_take, rd_replay_vu.c vuDraw).  Proven per draw
 * (rd_onepass_why): the LERP with the As factor (not FIX), no PABE (an
 * unblended pixel's c0.a is 1.0), no FBA (its alpha is As | 0x80), As at
 * most 128 at every pixel (the alpha plan's range, rd_alpha_plan.c
 * rd__alpha_range_of_draw: hi <= 128, so the factor is not clamped), not Z
 * GREATER with Z write (where the alpha pass's GEQUAL admits a fragment the
 * colour pass did not), an RGBA8 UNORM target, no destination read, not a
 * code 36 fused kick (its pixels are unblended; a cut whose pair goes to one
 * pass is not fused: rd__onepass_unfuse).  The
 * stored alpha through the constant equals the alpha pass's for every As
 * 0..128 under round-to-nearest at any blender precision of 8 bits or more
 * (rd_onepass_alpha_model; the unit test's exhaustive table).
 *
 * A start-up self-test (rd__onepass_selftest, after the VU pre-pass's and
 * before the start-up set, and again at each new SCENE size: the play's)
 * replays a frame of qualifying draws over the whole of SCENE with the
 * pairs kept and with them in one pass, compares every byte and checks the
 * pictures' stamps; a difference keeps the two passes (and the colour pass
 * without A) for the run.
 *
 * ICO_RD_ONEPASS=0|1|sabotage (the tests' switch, no config key): 0 off, 1
 * on whatever RD_ONEPASS_DEFAULT (the two-pass blend and the self-test still
 * decide), sabotage: the self-test's one-pass replay with the constant's
 * alpha 127/255, so it must refuse.  The replay tool runs the self-test only
 * where the variable is set. */
#ifndef PORT_RENDER_RD_ONEPASS_H
#define PORT_RENDER_RD_ONEPASS_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RD_ONEPASS_DEFAULT
#define RD_ONEPASS_DEFAULT 1
#endif

/* RdPipelineKey.nodual of a colour pass carrying A (1: the plain two-pass
 * colour pass) */
#define RD_NODUAL_CARRY 2u

/* the largest As the proof takes (the factor As/128 at most 1.0) */
#define RD_ONEPASS_ALPHA_MAX 128u

enum {
    RD_ONEPASS_WHY_NONE = 0, /* drawn in one pass */
    RD_ONEPASS_WHY_ALPHA,    /* As may pass 128 (or the range is not known) */
    RD_ONEPASS_WHY_FBA,
    RD_ONEPASS_WHY_PABE,
    RD_ONEPASS_WHY_FIX,   /* the factor is FIX (LERP FIX), not As */
    RD_ONEPASS_WHY_OTHER, /* Cd*FIX + Cs, Z GREATER with Z write, the target, a cut or kick */
    RD_ONEPASS_WHY_OFF,   /* proven, but the rule is off, refused or not tested */
    RD_ONEPASS_WHY_COUNT
};

const char *rd__onepass_why_text(int why);

/* the proof for the pair colour (ICO_DF_NODUAL_FACTOR) and its alpha pass
 * (ICO_DF_NODUAL_ALPHA_PASS), pure: RD_ONEPASS_WHY_NONE when one pass
 * stores the same bytes; hi the alpha plan's largest As of the draw; vuBits
 * the draw's IcoVuCB draw[2] (its cut, kick and fused bits) */
int rd_onepass_why(const RdDrawPass *colour, const RdDrawPass *alpha, uint8_t hi, uint32_t vuBits);

/* The stored alpha of the carry and of the alpha pass under a blender
 * model, pure (the unit test's exhaustive table): each value quantised to
 * an unsigned normalised fixed point of bits (8..24) or to a float of 16 or
 * 32 bits (bits 116, 132), rounding mode 0 to nearest, 1 down, 2 up; the
 * source's precision bs, the constant's bk, the product's bp; store the
 * conversion to 8 bits (0 nearest, 1 down, 2 up).  *two the alpha pass's
 * byte for As, the return the carry's. */
int rd_onepass_alpha_model(int as, int bs, int bk, int bp, int round, int store, int *two);

/* rd_pipeline.c rd__expand_no_dual: whether a LERP's colour pass carries A
 * (the rule in force, or the self-test's replays) */
bool rd__onepass_carry(void);

/* rd_pipeline.c rd__set_pipeline: the blend constant (alpha 128/255) for a
 * colour pass carrying A; nothing for any other key (NULL included) */
void rd__onepass_bind(RhiCommandList cl, const RdPipeKeyInt *k);

/* rd_replay_vu.c vuDraw: each pair of dp[0 .. *np) classified and counted;
 * the alpha pass of each proven pair dropped where the rule is in force (the
 * passes after it moved down, *np lowered); count the draw's index count
 * (its vertex invocations a pass), hi the alpha plan's largest As */
void rd__onepass_take(RdDrawPass *dp, int *np, uint8_t hi, uint32_t vuBits, uint32_t count);

/* the tests' hook (rd_onepass_test): -1 none, 0 the pairs kept with the
 * colour passes carrying A, 1 the proven pairs in one pass, as the
 * self-test's two replays draw (counted as the run's, not the test's) */
void rd__onepass_force_arm(int arm);

/* rd_replay_vu.c vuFuseVerdict: a code 36 cut whose pair (cut[0..nc)) goes
 * to one pass is not fused with its kick (the fused kick's pixels are
 * unblended, so a fused pair keeps both passes); unfused, the cut draws in
 * one pass and the kick alone under its own state: the same bytes, the
 * unfused draws being what the fusion stands for */
bool rd__onepass_unfuse(const RdDrawPass *cut, int nc, uint8_t hi);

/* in force: wanted, the two-pass blend, the self-test passed */
bool rd__onepass_on(void);

/* rd_lamps.c rd__lamps_init: the run's state */
void rd__onepass_init(void);

/* rd_vuprepass_selftest.c rd_vuprepass_selftest's end (the start and a
 * switch of the graphics API, before the start-up set), and the replay tool
 * after rd_init where ICO_RD_ONEPASS is set: true when passed or not asked */
bool rd__onepass_selftest(void);
/* rd_fog_path.c rd__fog_scene_remade: again at a new SCENE size once run */
void rd__onepass_scene_remade(void);
/* 0 not run, 1 passed, 2 failed or not run with a reason; words its line's */
int rd__onepass_test_state(const char **words);

/* the 10 s line (rd_lamps.c rd_lamps_line) */
void rd__onepass_line(void);

/* the counts since rd_init, the self-test's replays left out (the tool's
 * --stats): pairs, pairs in one pass, pairs kept by why (RD_ONEPASS_WHY_*,
 * [0] unused), vertex invocations saved */
typedef struct RdOnepassCounts {
    uint64_t pairs, onePass, kept[RD_ONEPASS_WHY_COUNT], saved;
} RdOnepassCounts;

void rd__onepass_counts(RdOnepassCounts *out);
/* the last replay's pairs and pairs in one pass (the A/B) */
void rd__onepass_replay(uint32_t *onePass, uint32_t *pairs);

#ifdef __cplusplus
}
#endif

#endif
