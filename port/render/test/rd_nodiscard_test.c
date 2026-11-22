/* rd_nodiscard_test.c: the discard-free twins' pure part (rd_nodiscard.c,
 * rd_nodiscard.h; issue 179), CPU only.
 *   table    the eligibility of a VU pass: a pass the twins exist for
 *            (vu_ps or vu_texa_ps, the *_nodual build, no destination read)
 *            given its twin; every refusal reason and the order they are
 *            taken in (plane, DATE, band, alpha, clipped); the side band
 *            impossible under ICO_VU_NO_BESIDE, at clip mode none and for
 *            particles, possible at the region test (code 32) and the scissor
 *            (code 36) without the flag, as under [dev] region_rule = ps2 on
 *            a wide frame; DATE by the snapshot (ICO_DF_DATE, with DATM) and
 *            by the stencil bit; the alpha test off (mode.z bit 8 clear:
 *            the test off in the state, ATST ALWAYS, the AFAIL merge's one
 *            draw, the alpha plan's proof) against a test that can fail
 *            (AFAIL KEEP, the split's two passes); clip mode none alone on
 *            every device (the camera rule's region test with
 *            ICO_VU_NO_BESIDE, the scissor and particles refused as
 *            clipped: a corner may be behind the eye); the tests' sabotage
 *            letting the alpha test through
 *   none     the passes no twin exists for (another entry, the planes'
 *            twins at a fractional scale, no *_nodual build, a destination
 *            read by storage and by attachment): -1, never counted
 *   names    every reason's words, the two modules' names (none for
 *            another entry), the key bits
 *   use      the in-play judge's use of a replay: in use only when a pass
 *            drew through a twin and no pass found its twin not made yet
 *   state    the run's state without a device (rd_nodiscard_run.c): held
 *            until the self-test (no 10 s clause, the stats line's words), on
 *            once it passed (the clause with every reason at 0), =0 off and a
 *            passed self-test does not turn it on, =1 on, the loss ladder
 *            and the install's mark over =1 (off, said), a failed self-test
 *            off for the run with its words, a short buffer cut, no module
 *            or pipeline of the twins without a device */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* a pass the twins exist for, eligible: vu_ps's *_nodual build, clip mode
   none (code 34), the test off */
static RdNdPass base(void)
{
    RdNdPass p;
    memset(&p, 0, sizeof(p));
    p.fs = RD_FS_VU;
    p.nodual = RD_NODUAL_ON;
    p.dst = RHI_DST_READ_NONE;
    p.drawFlags = ICO_VU_CLIP_NONE;
    return p;
}

static void table(void)
{
    RdNdPass p = base();
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    p.fs = RD_FS_VU_TEXA;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);

    /* the clip plane: the draw's chain plane or the pass's flag */
    p = base();
    p.plane = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_PLANE);
    p = base();
    p.flags = ICO_DF_PLANE;
    CHECK(rd_nd_eligible(&p) == RD_ND_PLANE);

    /* DATE: the snapshot's (either DATM), the stencil bit's */
    p = base();
    p.flags = ICO_DF_DATE;
    CHECK(rd_nd_eligible(&p) == RD_ND_DATE);
    p.flags = ICO_DF_DATE | ICO_DF_DATM;
    CHECK(rd_nd_eligible(&p) == RD_ND_DATE);
    p = base();
    p.dateStencil = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_DATE);

    /* the side band: the region test and the scissor without the flag (the
       PS2 rule on a wide frame); clip mode none and particles never */
    p = base();
    p.drawFlags = ICO_VU_CLIP_REGION;
    CHECK(rd_nd_eligible(&p) == RD_ND_BAND);
    p.drawFlags = ICO_VU_CLIP_SCISSOR | ICO_VU_EXTENDED;
    CHECK(rd_nd_eligible(&p) == RD_ND_BAND);
    p.drawFlags = ICO_VU_CLIP_SCISSOR | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    p.drawFlags = ICO_VU_CLIP_NONE;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    p.drawFlags = ICO_VU_CLIP_REGION;
    p.particle = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);

    /* the alpha test: the ate bit (rd_pipeline.c: atst | 1 << 8 | split <<
       16) in the pass; without it (the test off, ALWAYS, the AFAIL merge,
       the alpha plan's proof) the ATST byte alone means nothing */
    p = base();
    p.modeZ = RD_ATST_GREATER | (1u << 8);
    CHECK(rd_nd_eligible(&p) == RD_ND_ALPHA);
    p.modeZ = RD_ATST_GREATER | (1u << 8) | (1u << 16);
    CHECK(rd_nd_eligible(&p) == RD_ND_ALPHA);
    p.modeZ = RD_ATST_GREATER | (1u << 8) | (2u << 16);
    CHECK(rd_nd_eligible(&p) == RD_ND_ALPHA);
    p.modeZ = RD_ATST_GREATER;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    p.modeZ = 0;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    /* the tests' sabotage: the test not looked at */
    p.modeZ = RD_ATST_GREATER | (1u << 8);
    p.skipAlpha = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);

    /* clip mode none alone, on every device: the camera rule's draws (the
       region test with ICO_VU_CAMERA and ICO_VU_NO_BESIDE, the boat's
       code 32 and 24), the scissor (code 36), the wrap's own flags kept */
    p = base();
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_NO_BESIDE | ICO_VU_EXTENDED;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    p.drawFlags = ICO_VU_CLIP_SCISSOR | ICO_VU_CAMERA | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    p.drawFlags = ICO_VU_CLIP_NONE | ICO_VU_MIRROR | ICO_VU_BEHIND_EYE;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    p.particle = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    /* a device whose clipper check passed: the camera rule's region test and
       the scissor with ICO_VU_NO_BESIDE take the twin; particles never, a
       draw without the flag still has its band, and every other reason
       still holds */
    p = base();
    p.clipperClean = true;
    p.prelit = true;
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    p.drawFlags = ICO_VU_CLIP_SCISSOR | ICO_VU_CAMERA | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
    p.particle = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    p.particle = false;
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_EXTENDED;
    CHECK(rd_nd_eligible(&p) == RD_ND_BAND);
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_NO_BESIDE;
    p.modeZ = RD_ATST_GREATER | (1u << 8);
    CHECK(rd_nd_eligible(&p) == RD_ND_ALPHA);
    p.modeZ = 0;
    p.plane = true;
    CHECK(rd_nd_eligible(&p) == RD_ND_PLANE);

    /* the order: the first reason that applies */
    p = base();
    p.plane = true;
    p.flags = ICO_DF_DATE;
    p.drawFlags = ICO_VU_CLIP_REGION;
    p.modeZ = 1u << 8;
    CHECK(rd_nd_eligible(&p) == RD_ND_PLANE);
    p.plane = false;
    CHECK(rd_nd_eligible(&p) == RD_ND_DATE);
    p.flags = 0;
    CHECK(rd_nd_eligible(&p) == RD_ND_BAND);
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_ALPHA);
    p.modeZ = 0;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    p.drawFlags = ICO_VU_CLIP_NONE;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
}

/* the forms a clean clipper check speaks for (rd_nd_checked_form, the
   third review's R3-1): the prelit program at codes 32 and 36 under the
   camera rule, its cut and kick draws, in an extended state or not; every
   other form refused as "form", whatever else it is; not clean, all of
   them "clipped"; clip mode none takes the twin in every form */
static void forms(void)
{
    static const struct {
        const char *name;
        bool prelit;
        uint32_t flags;
        bool checked;
    } kForm[] = {
        {"prelit 32, the camera rule", true, ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE,
         true},
        {"prelit 32, extended", true,
         ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_EXTENDED, true},
        {"prelit 36", true, ICO_VU_CLIP_SCISSOR | ICO_VU_CAMERA | ICO_VU_NO_BESIDE, true},
        {"prelit 36, the cut draw", true,
         ICO_VU_CLIP_SCISSOR | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_CUT_ONLY, true},
        {"prelit 36, the fused kick draw", true,
         ICO_VU_CLIP_SCISSOR | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_KICK_ONLY | ICO_VU_FUSED,
         true},
        {"lit, skinned or grid 32 (not prelit)", false,
         ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE, false},
        {"skinned 24 (not prelit)", false, ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE,
         false},
        {"lit 36 (not prelit)", false, ICO_VU_CLIP_SCISSOR | ICO_VU_CAMERA | ICO_VU_NO_BESIDE,
         false},
        {"prelit, the drops kept (no facing table)", true, ICO_VU_CLIP_REGION | ICO_VU_NO_BESIDE,
         false},
        {"prelit, two-sided", true,
         ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_TWO_SIDED, false},
        {"prelit, mirrored", true,
         ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_MIRROR, false},
        {"prelit, photo mode's free camera", true,
         ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_BEHIND_EYE, false},
        {"prelit, the ticks' cameras", true,
         ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE | ICO_VU_TICKS, false},
    };

    for (size_t i = 0; i < sizeof(kForm) / sizeof(kForm[0]); i++) {
        RdNdPass p = base();
        p.prelit = kForm[i].prelit;
        p.drawFlags = kForm[i].flags;
        p.clipperClean = true;
        const int clean = rd_nd_eligible(&p);
        CHECK(rd_nd_checked_form(&p) == kForm[i].checked);
        CHECK(clean == (kForm[i].checked ? RD_ND_TWIN : RD_ND_FORM));
        p.clipperClean = false;
        CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
        /* clip mode none: the proof's, every form */
        p.drawFlags = (kForm[i].flags & ~(uint32_t)ICO_VU_CLIP_MASK) | ICO_VU_CLIP_NONE;
        CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
        if (clean != (kForm[i].checked ? RD_ND_TWIN : RD_ND_FORM)) {
            printf("  form %s: %d\n", kForm[i].name, clean);
        }
    }
    /* particles: clipped before the form is asked */
    RdNdPass p = base();
    p.prelit = true;
    p.clipperClean = true;
    p.particle = true;
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_CAMERA | ICO_VU_NO_BESIDE;
    CHECK(rd_nd_eligible(&p) == RD_ND_CLIPPED);
    CHECK(strcmp(rd_nd_reason_name(RD_ND_FORM), "form") == 0);
}

static void none(void)
{
    RdNdPass p = base();
    p.fs = RD_FS_SPRITE;
    CHECK(rd_nd_eligible(&p) == -1);
    p.fs = RD_FS_VU_NP;
    CHECK(rd_nd_eligible(&p) == -1);
    p.fs = RD_FS_VU_TEXA_NP;
    CHECK(rd_nd_eligible(&p) == -1);
    p = base();
    p.nodual = 0;
    CHECK(rd_nd_eligible(&p) == -1);
    p = base();
    p.dst = RHI_DST_READ_STORAGE;
    CHECK(rd_nd_eligible(&p) == -1);
    p.dst = RHI_DST_READ_ATTACHMENT;
    CHECK(rd_nd_eligible(&p) == -1);
    /* a pass already turned (both bits) is still the twins' pass */
    p = base();
    p.nodual = RD_NODUAL_ON | RD_NODUAL_NODISCARD;
    CHECK(rd_nd_eligible(&p) == RD_ND_TWIN);
}

static void names(void)
{
    static const char *const kWant[RD_ND_REASONS] = {"through them",
                                                     "plane",
                                                     "DATE",
                                                     "band",
                                                     "alpha",
                                                     "clipped",
                                                     "form",
                                                     "not made yet",
                                                     "timed without them",
                                                     "entry not tested"};
    for (int i = 0; i < RD_ND_REASONS; i++) {
        CHECK(strcmp(rd_nd_reason_name(i), kWant[i]) == 0);
    }
    CHECK(strcmp(rd_nd_reason_name(-1), "?") == 0);
    CHECK(strcmp(rd_nd_reason_name(RD_ND_REASONS), "?") == 0);
    CHECK(strcmp(rd_nd_module_name(RD_FS_VU), "vu_ps_nodual_nd") == 0);
    CHECK(strcmp(rd_nd_module_name(RD_FS_VU_TEXA), "vu_texa_ps_nodual_nd") == 0);
    CHECK(rd_nd_module_name(RD_FS_SPRITE) == NULL);
    CHECK(rd_nd_module_name(RD_FS_VU_NP) == NULL);
    /* the key bits: the twin's bit beside the *_nodual one, both in the
       key's byte (rd_state.h RdPipelineKey.nodual) */
    CHECK(RD_NODUAL_ON == 1u && RD_NODUAL_NODISCARD == 2u);
    RdPipelineKey k;
    memset(&k, 0, sizeof(k));
    k.nodual = (uint8_t)(RD_NODUAL_ON | RD_NODUAL_NODISCARD);
    CHECK(k.nodual == 3u && sizeof(RdPipelineKey) == 16u);
}

/* the in-play judge's use of a replay: in use once a pass drew through a
   twin (or would have, at the off arm), never once one found its twin not
   made yet, in either order; nothing else changes it */
static void use(void)
{
    RdNdUse u;
    memset(&u, 0, sizeof(u));
    CHECK(!rd_nd_use_counts(&u));
    rd_nd_use_note(&u, RD_ND_ALPHA);
    rd_nd_use_note(&u, RD_ND_CLIPPED);
    CHECK(!rd_nd_use_counts(&u));
    rd_nd_use_note(&u, RD_ND_TWIN);
    CHECK(rd_nd_use_counts(&u));
    rd_nd_use_note(&u, RD_ND_PLANE);
    CHECK(rd_nd_use_counts(&u));
    rd_nd_use_note(&u, RD_ND_UNMADE);
    CHECK(!rd_nd_use_counts(&u));
    rd_nd_use_note(&u, RD_ND_TWIN);
    CHECK(!rd_nd_use_counts(&u)); /* a later pass's twin does not bring the share back */
    memset(&u, 0, sizeof(u));
    rd_nd_use_note(&u, RD_ND_UNMADE);
    rd_nd_use_note(&u, RD_ND_TWIN);
    CHECK(!rd_nd_use_counts(&u));
    memset(&u, 0, sizeof(u));
    rd_nd_use_note(&u, RD_ND_TRIAL);
    CHECK(rd_nd_use_counts(&u)); /* the off arm with every twin made: timed */
    rd_nd_use_note(&u, RD_ND_UNTESTED);
    CHECK(rd_nd_use_counts(&u));
}

static void putEnv(const char *name, const char *value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value ? value : "");
#else
    if (value) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
#endif
}

static void state(void)
{
    char buf[512], st[640];
    putEnv("ICO_RD_VU_NODISCARD", NULL);
    putEnv("ICO_RD_VU_NODISCARD_SABOTAGE", NULL);
    putEnv("ICO_LOSS_ROW", NULL);
    putEnv("ICO_NODISCARD_BLOCK", NULL);
    putEnv("ICO_NODISCARD_MARK", NULL);
    putEnv("ICO_RD_VU_NODISCARD_CLIPPER", NULL);
    /* held until the self-test: nothing in the 10 s line */
    rd__nd_init();
    CHECK(!rd__nd_on() && strcmp(rd__nd_state_text(), "held") == 0 && !rd__nd_triable());
    CHECK(rd_nd_text(buf, sizeof(buf), 1) == 0u && buf[0] == '\0');
    (void)rd__nd_stats_text(st, sizeof(st));
    CHECK(
        strcmp(
            st,
            "discard-free twins held (until their start-up self-test passes): 0 draws "
            "through them, 0 of them by the clipper check (not checked); refused: plane "
            "0, DATE 0, band 0, alpha 0, clipped 0, form 0, not made yet 0, timed "
            "without them 0, entry not tested 0; 0 modules of them made, 0 of their pipelines alive") ==
        0);
    if (strstr(st, "held (until") == NULL) {
        printf("  stats: %s\n", st);
    }
    /* the self-test passed (no module without a device: not triable) */
    rd__nd_verdict(true, "");
    CHECK(rd__nd_on() && !rd__nd_triable());
    (void)rd_nd_text(buf, sizeof(buf), 2);
    CHECK(strcmp(buf, "; discard-free twins: 0 draws a picture through them, 0 of them by the "
                      "clipper check (not checked) (0 refused a picture: plane 0, DATE 0, band 0, "
                      "alpha 0, clipped 0, form 0, not made yet 0, timed without them 0, entry "
                      "not tested 0)") == 0);
    /* the clipper check's verdicts and the tests' fake */
    rd__nd_clipper_verdict(false, "an edge on the centre view ray at code 32 differs");
    CHECK(rd__nd_clipper() == RD_ND_CLIP_DIRTY && strcmp(rd__nd_clipper_text(), "not clean") == 0);
    rd__nd_clipper_verdict(true, "");
    CHECK(rd__nd_clipper() == RD_ND_CLIP_CLEAN && strcmp(rd__nd_clipper_text(), "clean") == 0);
    putEnv("ICO_RD_VU_NODISCARD_CLIPPER", "clean");
    rd__nd_init();
    rd__nd_clipper_verdict(false, "x");
    CHECK(rd__nd_clipper() == RD_ND_CLIP_CLEAN);
    putEnv("ICO_RD_VU_NODISCARD_CLIPPER", NULL);
    rd__nd_init();
    CHECK(rd__nd_clipper() == RD_ND_CLIP_UNKNOWN);
    rd__nd_verdict(true, "");
    char small[24];
    const size_t n = rd_nd_text(small, sizeof(small), 1);
    CHECK(n == sizeof(small) - 1u && strlen(small) == sizeof(small) - 1u);
    /* =0: off, a passed self-test does not turn it on */
    putEnv("ICO_RD_VU_NODISCARD", "0");
    rd__nd_init();
    CHECK(!rd__nd_on());
    rd__nd_verdict(true, "");
    CHECK(!rd__nd_on());
    CHECK(rd_nd_text(buf, sizeof(buf), 1) == 0u);
    /* =1: on without the self-test */
    putEnv("ICO_RD_VU_NODISCARD", "1");
    rd__nd_init();
    CHECK(rd__nd_on() && strcmp(rd__nd_state_text(), "on") == 0);
    (void)rd__nd_stats_text(st, sizeof(st));
    static const char kOn[] = "discard-free twins on (ICO_RD_VU_NODISCARD=1): 0 draws through "
                              "them, 0 of them by the clipper check (not checked); ";
    CHECK(strncmp(st, kOn, sizeof(kOn) - 1u) == 0);
    putEnv("ICO_RD_VU_NODISCARD_CLIPPER", "clean");
    rd__nd_init();
    CHECK(rd__nd_on() && rd__nd_clipper() == RD_ND_CLIP_CLEAN);
    putEnv("ICO_RD_VU_NODISCARD_CLIPPER", NULL);
    /* the loss ladder over =1: off for this start, said in the clause once
       they were on */
    putEnv("ICO_LOSS_ROW", "1");
    rd__nd_init();
    CHECK(!rd__nd_on());
    (void)rd__nd_stats_text(st, sizeof(st));
    CHECK(strstr(st, "discard-free twins off (for this start (the loss ladder: 1 start in a row "
                     "before this one lost the graphics device or hung its driver, ICO_LOSS_ROW "
                     "1; nothing written to the settings)): ") == st);
    putEnv("ICO_LOSS_ROW", NULL);
    /* the install's mark over =1 */
    putEnv("ICO_NODISCARD_BLOCK", "were on when it ended (a crash)");
    rd__nd_init();
    CHECK(!rd__nd_on());
    (void)rd__nd_stats_text(st, sizeof(st));
    CHECK(strstr(st, "discard-free twins off (on this install (were on when it ended (a "
                     "crash))): ") == st);
    putEnv("ICO_NODISCARD_BLOCK", NULL);
    /* a failed self-test: off for the run, its words kept through a renderer
       made again */
    putEnv("ICO_RD_VU_NODISCARD", NULL);
    rd__nd_init();
    rd__nd_verdict(false, "the pictures differ with them");
    CHECK(!rd__nd_on());
    rd__nd_init();
    CHECK(!rd__nd_on());
    (void)rd__nd_stats_text(st, sizeof(st));
    CHECK(strstr(st, "discard-free twins off (for the run (their self-test: the pictures differ "
                     "with them)): ") == st);
    CHECK(rd__pipeline_count_twins() == 0u);
}

int main(void)
{
    table();
    forms();
    none();
    names();
    use();
    state();
    if (failures) {
        printf("rd_nodiscard_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_nodiscard_test: ok\n");
    return 0;
}
