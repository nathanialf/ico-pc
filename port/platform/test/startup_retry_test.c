/*
 * port/platform/test/startup_retry_test.c
 *
 * The steps after a graphics driver crash while building a pipeline or a
 * device loss (startup_retry.h):
 *   - startup_retry_next over both reasons, every step, the platform able
 *     to start the game again or not, cache loaded or not, D3D12 in the
 *     build or not, the backend and the blending named by the player or
 *     not, the run's blend already fixed or not, the crash's note naming
 *     a destination-read pipeline or not, naming a pipeline of the VU
 *     vertex pre-pass or not, the run drawing with the pre-pass or not, on
 *     Vulkan and on D3D12, against the crash order cache, noprepass,
 *     fixedblend, d3d12, lazy, none and the loss order lossnoprepass,
 *     robust (the run's robust access off by the platform's default),
 *     blend, backend, none (the note never changes it); the reason is
 *     never empty and a step only grows (every chain ends);
 *   - the robust step: its place before blend, its value, that it draws
 *     with robust access and without the pre-pass, not with the fixed
 *     blend, and that no run without robustOffDefault takes it;
 *   - the chains with the pre-pass: a crash in one of its pipelines
 *     (cache, noprepass, then the rest without it) and a loss in a run
 *     drawing with it (lossnoprepass, blend, backend), each value written
 *     and read back, and which values draw without the pre-pass or with
 *     the fixed blend;
 *   - a crash chain from the first run to the box, as the handler walks
 *     it, with and without a destination-read pipeline in the note, and
 *     the player's blend= skipping fixedblend;
 *   - the note's dst field read from the renderer's note text, and its
 *     pre-pass keys and modules (the record path's vu_main_vs and
 *     vu_particle_main_vs among them; an extended program entry's note
 *     not);
 *   - the loss chains on Windows from Vulkan and from D3D12, on Linux and
 *     macOS, on Android and iOS, with blend= or backend= in the settings,
 *     and a loss after a crash restart;
 *   - the variable's value written and read back, and bad values refused;
 *   - the relaunch environment: the POSIX pointer list and the Windows
 *     block, each without an inherited ICO_STARTUP_RETRY and with the new
 *     one (a crash step and a loss step), and too-small buffers refused.
 */
#include <stdio.h>
#include <string.h>
#include "startup_retry.h"
#include "../../test/ico_check.h"

/* the crash order, written out independently of startup_retry.c */
static StartupRetry expected_crash(StartupRetry step, bool cache, bool d3d12, bool expl,
                                   bool blendExpl, bool wasFixed, bool dst, bool pre,
                                   const char *backend)
{
    const bool onD3d12 = strcmp(backend, "d3d12") == 0;
    if (step == STARTUP_RETRY_LOSS_NO_PREPASS || step == STARTUP_RETRY_LOSS_ROBUST ||
        step == STARTUP_RETRY_LOSS_BLEND || step == STARTUP_RETRY_LOSS_BACKEND) {
        return STARTUP_RETRY_NONE; /* a crash after a loss restart: the box */
    }
    if (step == STARTUP_RETRY_NONE && cache) {
        return STARTUP_RETRY_CACHE;
    }
    if ((step == STARTUP_RETRY_NONE || step == STARTUP_RETRY_CACHE) && pre) {
        return STARTUP_RETRY_NO_PREPASS;
    }
    if ((step == STARTUP_RETRY_NONE || step == STARTUP_RETRY_CACHE ||
         step == STARTUP_RETRY_NO_PREPASS) &&
        dst && !blendExpl && !wasFixed) {
        return STARTUP_RETRY_CRASH_BLEND;
    }
    if ((step == STARTUP_RETRY_NONE || step == STARTUP_RETRY_CACHE ||
         step == STARTUP_RETRY_NO_PREPASS || step == STARTUP_RETRY_CRASH_BLEND) &&
        d3d12 && !expl && !onD3d12) {
        return STARTUP_RETRY_D3D12;
    }
    if (step != STARTUP_RETRY_LAZY) {
        return STARTUP_RETRY_LAZY;
    }
    return STARTUP_RETRY_NONE;
}

/* the loss order: lossnoprepass (same backend, no pre-pass) when the run
   drew with the pre-pass; robust (same backend, robust access on) when the
   run drew with it off by the platform's default; blend (same backend,
   fixed blend) unless the run was already fixed or the player chose the
   blending; then backend (the other one, fixed blend) in a two-backend
   build when the player chose neither the backend nor a blending other
   than fixed; then the box */
static StartupRetry expected_loss(StartupRetry step, bool d3d12, bool expl, bool blendExpl,
                                  bool wasFixed, bool preOn, bool robustOff)
{
    const bool beforeBlend = step != STARTUP_RETRY_LOSS_BLEND && step != STARTUP_RETRY_LOSS_BACKEND;
    const bool beforeRobust = beforeBlend && step != STARTUP_RETRY_LOSS_ROBUST;
    if (beforeRobust && step != STARTUP_RETRY_LOSS_NO_PREPASS && preOn) {
        return STARTUP_RETRY_LOSS_NO_PREPASS;
    }
    if (beforeRobust && robustOff) {
        return STARTUP_RETRY_LOSS_ROBUST;
    }
    if (beforeBlend && !wasFixed && !blendExpl) {
        return STARTUP_RETRY_LOSS_BLEND;
    }
    if (step != STARTUP_RETRY_LOSS_BACKEND && d3d12 && !expl && (!blendExpl || wasFixed)) {
        return STARTUP_RETRY_LOSS_BACKEND;
    }
    return STARTUP_RETRY_NONE;
}

/* every reason, step, platform (relaunch or not), cache, D3D12 in the
   build, backend and blend named or not, blend already fixed or not, a
   destination-read pipeline in the note or not, a pre-pass pipeline in
   the note or not, the run drawing with the pre-pass or not, on Vulkan and
   D3D12 */
static void test_table(void)
{
    static const char *const backends[] = {"vulkan", "d3d12"};
    int combos = 0;
    for (int why = STARTUP_RETRY_CRASH; why <= STARTUP_RETRY_LOSS; why++) {
        for (int step = STARTUP_RETRY_NONE; step <= STARTUP_RETRY_LOSS_BACKEND; step++) {
            for (int bits = 0; bits < 1024; bits++) {
                for (int b = 0; b < 2; b++) {
                    const bool cache = bits & 1, d3d12 = bits & 2, expl = bits & 4,
                               blendExpl = bits & 8, wasFixed = bits & 16, relaunch = bits & 32,
                               dst = bits & 64, pre = bits & 128, preOn = bits & 256,
                               robustOff = bits & 512;
                    const StartupRetryRun run = {
                        .why = (StartupRetryWhy)why,
                        .step = (StartupRetry)step,
                        .canRelaunch = relaunch,
                        .cacheWasLoaded = cache,
                        .haveD3d12 = d3d12,
                        .backendExplicit = expl,
                        .blendExplicit = blendExpl,
                        .blendWasFixed = wasFixed,
                        .dstPipeline = dst,
                        .prepassPipeline = pre,
                        .prepassWasOn = preOn,
                        .robustOffDefault = robustOff,
                        .backendName = backends[b],
                    };
                    char reason[200];
                    reason[0] = '\0';
                    const StartupRetry got = startup_retry_next(&run, reason, sizeof reason);
                    StartupRetry want = STARTUP_RETRY_NONE;
                    if (relaunch) {
                        want = why == STARTUP_RETRY_CRASH
                                   ? expected_crash((StartupRetry)step, cache, d3d12, expl,
                                                    blendExpl, wasFixed, dst, pre, backends[b])
                                   : expected_loss((StartupRetry)step, d3d12, expl, blendExpl,
                                                   wasFixed, preOn, robustOff);
                    }
                    CHECK(got == want,
                          "why %d step %d cache %d d3d12 %d explicit %d blend explicit %d "
                          "fixed %d relaunch %d dst %d pre %d pre on %d robust off %d %s: got %d, "
                          "want %d",
                          why, step, cache, d3d12, expl, blendExpl, wasFixed, relaunch, dst, pre,
                          preOn, robustOff, backends[b], (int)got, (int)want);
                    CHECK(reason[0] != '\0', "why %d step %d: empty reason", why, step);
                    /* a loss step only grows, and never comes from a crash */
                    if (got != STARTUP_RETRY_NONE) {
                        CHECK(got > (StartupRetry)step, "step %d went back to %d", step, (int)got);
                        CHECK(startup_retry_is_loss(got) == (why == STARTUP_RETRY_LOSS));
                    }
                    combos++;
                }
            }
        }
    }
    CHECK(combos == 2 * 10 * 1024 * 2);
    /* no name means the default backend; a NULL reason is allowed */
    {
        StartupRetryRun run = {.canRelaunch = true, .haveD3d12 = true};
        CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_D3D12);
        /* names in any case */
        run.backendName = "D3D12";
        CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LAZY);
        run.why = STARTUP_RETRY_LOSS;
        run.blendWasFixed = true;
        char v[32];
        CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LOSS_BACKEND);
        CHECK(startup_retry_value(STARTUP_RETRY_LOSS_BACKEND, "D3D12", v, sizeof v) == 0);
        CHECK_STR(v, "backend:vulkan");
    }
}

/* one crash-chain step from the run described by the arguments */
static StartupRetry crash_step(StartupRetry step, bool cache, bool d3d12, bool expl,
                               const char *backend)
{
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_CRASH,
        .step = step,
        .canRelaunch = true,
        .cacheWasLoaded = cache,
        .haveD3d12 = d3d12,
        .backendExplicit = expl,
        .backendName = backend,
    };
    return startup_retry_next(&run, NULL, 0);
}

/* one crash-chain step with the note's destination read and the player's
   blending */
static StartupRetry crash_step_dst(StartupRetry step, bool cache, bool d3d12, bool expl,
                                   bool blendExpl, bool dst, const char *backend)
{
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_CRASH,
        .step = step,
        .canRelaunch = true,
        .cacheWasLoaded = cache,
        .haveD3d12 = d3d12,
        .backendExplicit = expl,
        .blendExplicit = blendExpl,
        .dstPipeline = dst,
        .backendName = backend,
    };
    char reason[200];
    reason[0] = '\0';
    const StartupRetry next = startup_retry_next(&run, reason, sizeof reason);
    CHECK(reason[0] != '\0');
    if (next == STARTUP_RETRY_CRASH_BLEND) {
        CHECK(strstr(reason, "simpler blending") != NULL, "reason: %s", reason);
    }
    return next;
}

/* the handler's walk on a Windows build with a loaded cache: cache,
 * d3d12, lazy, then nothing; the reporter's case (no cache) starts at
 * d3d12; an explicit backend goes straight to lazy */
static void test_chain(void)
{
    char v[32], be[16];
    StartupRetry s = STARTUP_RETRY_NONE, p;
    const char *backend = "vulkan";
    static const StartupRetry walk[] = {STARTUP_RETRY_CACHE, STARTUP_RETRY_D3D12,
                                        STARTUP_RETRY_LAZY, STARTUP_RETRY_NONE};
    static const char *const values[] = {"cache:vulkan", "d3d12:d3d12", "lazy:d3d12"};
    bool cache = true;
    for (int i = 0; i < 4; i++) {
        s = crash_step(s, cache, true, false, backend);
        CHECK(s == walk[i], "chain step %d: %d", i, (int)s);
        if (s == STARTUP_RETRY_NONE) {
            CHECK(startup_retry_value(s, backend, v, sizeof v) == -1);
            break;
        }
        CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
        CHECK_STR(v, values[i]);
        /* the relaunched run */
        CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0);
        CHECK(p == s);
        backend = be[0] == 'd' ? "d3d12" : "vulkan";
        cache = false; /* deleted, or another backend */
    }
    CHECK(crash_step(STARTUP_RETRY_NONE, false, true, false, "vulkan") == STARTUP_RETRY_D3D12);
    CHECK(crash_step(STARTUP_RETRY_NONE, false, true, true, "vulkan") == STARTUP_RETRY_LAZY);
    CHECK(crash_step(STARTUP_RETRY_NONE, false, false, false, "vulkan") == STARTUP_RETRY_LAZY);
}

static StartupRetry loss_step(StartupRetry step, bool d3d12, bool expl, bool blendExpl,
                              bool wasFixed, const char *backend, bool relaunch);

/* the walk after a crash in a destination-read pipeline (issue 92: an
 * exact-blend pass's interlock entry on a Windows Vulkan driver), each
 * value written and read back as the relaunched run reads it: cache,
 * fixedblend, d3d12, lazy, then the box; the note on each run names the
 * same pipeline, except that the fixedblend run creates no dst pipeline, so
 * its crash (any other pipeline) and the later runs' notes say dst 0 */
static void test_dst_chain(void)
{
    static const StartupRetry walk[] = {STARTUP_RETRY_CACHE, STARTUP_RETRY_CRASH_BLEND,
                                        STARTUP_RETRY_D3D12, STARTUP_RETRY_LAZY,
                                        STARTUP_RETRY_NONE};
    static const char *const values[] = {"cache:vulkan", "fixedblend:vulkan", "d3d12:d3d12",
                                         "lazy:d3d12"};
    char v[32], be[16];
    StartupRetry s = STARTUP_RETRY_NONE, p;
    const char *backend = "vulkan";
    bool cache = true, dst = true;
    for (int i = 0; i < 5; i++) {
        s = crash_step_dst(s, cache, true, false, false, dst, backend);
        CHECK(s == walk[i], "dst chain step %d: %d", i, (int)s);
        if (s == STARTUP_RETRY_NONE) {
            break;
        }
        CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
        CHECK_STR(v, values[i]);
        CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0 && p == s);
        /* the fixed blend reaches only the fixedblend run (host_config.c),
           and the window does not count it as the player's */
        CHECK(startup_retry_value_draws_fixed(v) == (s == STARTUP_RETRY_CRASH_BLEND), "%s", v);
        CHECK(!startup_retry_value_is_loss(v));
        backend = be[0] == 'd' ? "d3d12" : "vulkan";
        cache = false;
        dst = s != STARTUP_RETRY_CRASH_BLEND && dst; /* still the same dst pipeline before */
    }
    /* the reporter's case: no cache file, a dst pipeline on Vulkan:
       fixedblend first, then Direct3D 12 even when that run crashes in a
       dst 0 pipeline */
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, true, false, false, true, "vulkan") ==
          STARTUP_RETRY_CRASH_BLEND);
    CHECK(crash_step_dst(STARTUP_RETRY_CRASH_BLEND, false, true, false, false, false, "vulkan") ==
          STARTUP_RETRY_D3D12);
    CHECK(crash_step_dst(STARTUP_RETRY_CRASH_BLEND, false, true, false, false, true, "vulkan") ==
          STARTUP_RETRY_D3D12);
    /* a crash in a dst 0 pipeline: no fixedblend, as before */
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, true, false, false, false, "vulkan") ==
          STARTUP_RETRY_D3D12);
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, true, true, false, false, false, "vulkan") ==
          STARTUP_RETRY_CACHE);
    CHECK(crash_step_dst(STARTUP_RETRY_CACHE, false, true, false, false, false, "vulkan") ==
          STARTUP_RETRY_D3D12);
    /* the player's blend= (or ICO_RD_BLEND): no fixedblend */
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, true, false, true, true, "vulkan") ==
          STARTUP_RETRY_D3D12);
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, false, false, true, true, "vulkan") ==
          STARTUP_RETRY_LAZY);
    /* on Direct3D 12 (its rasterizer-ordered view entries) and without
       D3D12 in the build (Linux, macOS): fixedblend, then lazy */
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, true, false, false, true, "d3d12") ==
          STARTUP_RETRY_CRASH_BLEND);
    CHECK(crash_step_dst(STARTUP_RETRY_CRASH_BLEND, false, true, false, false, false, "d3d12") ==
          STARTUP_RETRY_LAZY);
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, false, false, false, true, "vulkan") ==
          STARTUP_RETRY_CRASH_BLEND);
    CHECK(crash_step_dst(STARTUP_RETRY_CRASH_BLEND, false, false, false, false, true, "vulkan") ==
          STARTUP_RETRY_LAZY);
    /* backend= named: fixedblend, then lazy */
    CHECK(crash_step_dst(STARTUP_RETRY_NONE, false, true, true, false, true, "vulkan") ==
          STARTUP_RETRY_CRASH_BLEND);
    CHECK(crash_step_dst(STARTUP_RETRY_CRASH_BLEND, false, true, true, false, true, "vulkan") ==
          STARTUP_RETRY_LAZY);
    /* after a loss restart a crash still gets the box */
    CHECK(crash_step_dst(STARTUP_RETRY_LOSS_BLEND, false, true, false, false, true, "vulkan") ==
          STARTUP_RETRY_NONE);
    /* a loss in the fixedblend run (already fixed): straight to the other
       backend, as the loss order says */
    CHECK(loss_step(STARTUP_RETRY_CRASH_BLEND, true, false, false, true, "vulkan", true) ==
          STARTUP_RETRY_LOSS_BACKEND);
    CHECK(loss_step(STARTUP_RETRY_CRASH_BLEND, false, false, false, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
}

/* the renderer's note (port/render/rd_pipeline.c activityCreating) read
   for its dst field */
static void test_note(void)
{
    static const struct {
        const char *note;
        bool dst;
    } notes[] = {
        {"creating pipeline program 3 blend 12 vs 4 fs 19 fmt 1/9 msaa 1 dst 1 (start-up set, 12 "
         "of 400)",
         true},
        {"creating pipeline program 3 blend 12 vs 4 fs 5 fmt 1/9 msaa 1 dst 2", true},
        {"creating pipeline program 3 blend 12 vs 4 fs 5 fmt 1/9 msaa 4 dst 10 (start-up set, 1 "
         "of 2)",
         true},
        {"creating pipeline program 3 blend 12 vs 4 fs 19 fmt 1/9 msaa 1 dst 0 (start-up set, 12 "
         "of 400)",
         false},
        {"creating pipeline program 3 blend 2 vs 4 fs 19 fmt 1/9 msaa 1 dst 0", false},
        {"creating pipeline program 3 blend 12 vs 4 fs 19 fmt 1/9 msaa 1 dst 00", false},
        /* the issue's quote, without the field */
        {"creating pipeline program 3 blend 12 vs 4 fs 19", false},
        {"creating the start-up pipelines (12 of 400)", false},
        /* a batch of the start-up set: its first key's field */
        {"creating pipelines 321-352 of 702: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 1 "
         "to program 3 blend 12 vs 7 fs 19 fmt 1/8 msaa 0 dst 1",
         true},
        {"creating pipelines 321-352 of 702: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 "
         "to program 3 blend 12 vs 7 fs 19 fmt 1/8 msaa 0 dst 0",
         false},
        {"creating pipelines 9 of 9: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 2", true},
        {"creating pipelinesX dst 1", false},
        {"creating pipelinex dst 1", false},
        {"creating pipeline fog depth copy", false},
        {"creating pipeline program 3 dst ", false},
        {"creating pipeline program 3 dst x", false},
        {"creating pipeline program 3 dst 1x", false},
        {"creating pipeline program 3 dst -1", false},
        {"loading the title dst 1", false},
        /* the batch's head is a pipeline note's too */
        {"creating pipelines dst 1", true},
        {"", false},
    };

    for (size_t i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        CHECK(startup_retry_note_has_dst(notes[i].note) == notes[i].dst, "\"%s\"", notes[i].note);
    }
    CHECK(!startup_retry_note_has_dst(NULL));
}

/* the same notes read for the VU vertex pre-pass's pipelines: a " pre"
   key (rd_pipeline.c keyWords), a vu_X_pre_vs module, a record stage's
   vu_tri_*_pre_vs or vu_particle_pre_vs, the record path's drawing shaders
   vu_main_vs and vu_particle_main_vs (made only with the pre-pass: the
   retired extended VU kind folds into this one), the old triangle stage's
   vu_tri_vs or vu_tri_plain_vs (keyModules, batchModules); an older
   version's extended program entry (" ext 1", vu_prelit_vs) is not one */
static void test_note_vu_pre(void)
{
    static const struct {
        const char *note;
        bool pre;
    } notes[] = {
        {"creating pipeline program 0 blend 0 vs 31 fs 41 fmt 3/0 msaa 0 dst 0 pre "
         "(vu_prelit_pre_vs 1820 words + vu_pre_ps 300 words)",
         true},
        {"creating pipeline program 0 blend 0 vs 31 fs 41 fmt 3/0 msaa 0 dst 0 pre", true},
        {"creating pipeline program 0 blend 12 vs 30 fs 5 fmt 1/8 msaa 0 dst 0 ext 0 "
         "(vu_tri_plain_vs 19519 words + vu_ps 9000 words)",
         true},
        {"creating pipeline program 0 blend 12 vs 30 fs 5 fmt 1/8 msaa 0 dst 0 ext 1 "
         "(vu_tri_vs 47637 words + vu_ps 9000 words)",
         true},
        {"creating pipelines 300-331 of 702: program 0 blend 0 vs 31 fs 41 fmt 3/0 msaa 0 dst 0 "
         "pre to program 0 blend 12 vs 30 fs 19 fmt 1/8 msaa 0 dst 0 ext 0; vertex "
         "vu_prelit_pre_vs 1820 words (400 instructions), vu_tri_plain_vs 19519 words (4318 "
         "instructions); largest fragment vu_texa_ps 11666 words",
         true},
        {"creating pipelines 301-332 of 702: program 0 blend 12 vs 30 fs 5 fmt 1/8 msaa 0 dst 0 "
         "ext 0 to program 0 blend 4 vs 30 fs 19 fmt 1/8 msaa 0 dst 0 ext 0; vertex "
         "vu_tri_plain_vs 19519 words",
         true},
        /* the record path's drawing shaders and stages */
        {"creating pipeline program 0 blend 12 vs 41 fs 5 fmt 1/8 msaa 0 dst 0 "
         "(vu_main_vs 20000 words + vu_ps 9000 words)",
         true},
        {"creating pipelines 300-307 of 220: program 0 blend 12 vs 41 fs 5 fmt 1/8 msaa 0 dst 0 "
         "to program 0 blend 4 vs 42 fs 19 fmt 1/8 msaa 0 dst 0; vertex vu_main_vs 20000 words "
         "(4000 instructions), vu_particle_main_vs 9000 words (2000 instructions); largest "
         "fragment vu_texa_ps 11666 words; compiler E031.41.03.47",
         true},
        {"creating pipeline program 0 blend 0 vs 43 fs 41 fmt 3/0 msaa 0 dst 0 pre "
         "(vu_tri_rule_pre_vs 3000 words + vu_pre_ps 300 words)",
         true},
        {"creating pipeline program 0 blend 0 vs 46 fs 41 fmt 3/0 msaa 0 dst 0 "
         "(vu_particle_pre_vs 2000 words + vu_pre_ps 300 words)",
         true},
        {"loading vu_main_vs", false},
        /* the programs' own entries, a 2D batch: none of the pre-pass */
        {"creating pipeline program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 ext 0 "
         "(vu_prelit_plain_vs 22392 words + vu_ps 9000 words)",
         false},
        {"creating pipelines 297-328 of 702: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 "
         "ext 1 to program 0 blend 4 vs 5 fs 19 fmt 1/8 msaa 0 dst 0 ext 1; vertex vu_prelit_vs "
         "50325 words",
         false},
        {"creating pipelines 1-32 of 702: program 5 blend 1 vs 0 fs 0 fmt 1/8 msaa 0 dst 0 to "
         "program 6 blend 1 vs 1 fs 0 fmt 1/8 msaa 0 dst 0; vertex sprite_ui_vs 900 words",
         false},
        {"creating pipeline program 0 dst 0 prepared", false},
        {"creating pipeline program 0 dst 0 pre1", false},
        {"loading the title pre", false},
        {"loading vu_tri_vs", false},
        {"creating pipelinesX pre", false},
        {"creating the start-up pipelines (12 of 400)", false},
        {"", false},
    };

    for (size_t i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        CHECK(startup_retry_note_has_vu_pre(notes[i].note) == notes[i].pre, "\"%s\"",
              notes[i].note);
    }
    CHECK(!startup_retry_note_has_vu_pre(NULL));
}

/* one loss-chain step */
static StartupRetry loss_step(StartupRetry step, bool d3d12, bool expl, bool blendExpl,
                              bool wasFixed, const char *backend, bool relaunch)
{
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_LOSS,
        .step = step,
        .canRelaunch = relaunch,
        .haveD3d12 = d3d12,
        .backendExplicit = expl,
        .blendExplicit = blendExpl,
        .blendWasFixed = wasFixed,
        .backendName = backend,
    };
    char reason[200];
    reason[0] = '\0';
    const StartupRetry next = startup_retry_next(&run, reason, sizeof reason);
    CHECK(reason[0] != '\0');
    return next;
}

/* the walks after a device loss, as window_host.c and diag_host.c take
   them, each value written and read back as the relaunched run reads it:
   Windows on Vulkan (the reporter's v0.7.2 run): blend:vulkan, then
   backend:d3d12, then the box; Windows on D3D12 (the v0.7.4 run): then
   backend:vulkan; Linux and macOS (no D3D12): blend:vulkan, then the box;
   Android and iOS: the box at once */
static void test_loss_chain(void)
{
    static const struct {
        bool d3d12;
        const char *first;
        const char *values[2];
    } walks[] = {
        {true, "vulkan", {"blend:vulkan", "backend:d3d12"}},
        {true, "d3d12", {"blend:d3d12", "backend:vulkan"}},
        {false, "vulkan", {"blend:vulkan", NULL}},
    };

    for (size_t w = 0; w < sizeof walks / sizeof walks[0]; w++) {
        StartupRetry s = STARTUP_RETRY_NONE, p;
        char v[32], be[16];
        const char *backend = walks[w].first;
        bool fixed = false;
        int i = 0;
        for (;; i++) {
            s = loss_step(s, walks[w].d3d12, false, false, fixed, backend, true);
            if (s == STARTUP_RETRY_NONE) {
                break;
            }
            CHECK(i < 2 && walks[w].values[i] != NULL, "walk %zu: a step too many", w);
            if (i >= 2 || walks[w].values[i] == NULL) {
                break;
            }
            CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
            CHECK_STR(v, walks[w].values[i]);
            CHECK(startup_retry_value_is_loss(v));
            CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0 && p == s);
            backend = strcmp(be, "d3d12") == 0 ? "d3d12" : "vulkan";
            fixed = true; /* the loss steps draw with the fixed-function blend */
        }
        CHECK(i == (walks[w].values[1] != NULL ? 2 : 1), "walk %zu: %d steps", w, i);
    }
    /* Android and iOS: no relaunch, the box */
    CHECK(loss_step(STARTUP_RETRY_NONE, false, false, false, false, "vulkan", false) ==
          STARTUP_RETRY_NONE);
    /* blend=fixed in the ini (the run already drew with it): straight to
       the other backend; without D3D12, the box */
    CHECK(loss_step(STARTUP_RETRY_NONE, true, false, true, true, "vulkan", true) ==
          STARTUP_RETRY_LOSS_BACKEND);
    CHECK(loss_step(STARTUP_RETRY_NONE, false, false, true, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* the device had no exact blend (already fixed): no blend step */
    CHECK(loss_step(STARTUP_RETRY_NONE, false, false, false, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* a blending other than fixed named by the player: neither step */
    CHECK(loss_step(STARTUP_RETRY_NONE, true, false, true, false, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* backend= named: the blend step, never the other backend */
    CHECK(loss_step(STARTUP_RETRY_NONE, true, true, false, false, "vulkan", true) ==
          STARTUP_RETRY_LOSS_BLEND);
    CHECK(loss_step(STARTUP_RETRY_LOSS_BLEND, true, true, false, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* a loss in a run started after a driver crash (lazy on D3D12) still
       gets the loss steps; a crash in a loss step's run gets the box */
    CHECK(loss_step(STARTUP_RETRY_LAZY, true, false, false, false, "d3d12", true) ==
          STARTUP_RETRY_LOSS_BLEND);
    CHECK(crash_step(STARTUP_RETRY_LOSS_BLEND, true, true, false, "vulkan") == STARTUP_RETRY_NONE);
    CHECK(crash_step(STARTUP_RETRY_LOSS_BACKEND, false, false, false, "d3d12") ==
          STARTUP_RETRY_NONE);
}

/* one step of a chain with the pre-pass: a crash whose note names one of
   its pipelines (pre), or a loss in a run that drew with it (preOn) */
static StartupRetry prepass_step(StartupRetryWhy why, StartupRetry step, bool cache, bool d3d12,
                                 bool pre, bool preOn, bool wasFixed, const char *backend)
{
    const StartupRetryRun run = {
        .why = why,
        .step = step,
        .canRelaunch = true,
        .cacheWasLoaded = cache,
        .haveD3d12 = d3d12,
        .blendWasFixed = wasFixed,
        .prepassPipeline = pre,
        .prepassWasOn = preOn,
        .backendName = backend,
    };
    char reason[200];
    reason[0] = '\0';
    const StartupRetry next = startup_retry_next(&run, reason, sizeof reason);
    CHECK(reason[0] != '\0');
    if (next == STARTUP_RETRY_NO_PREPASS || next == STARTUP_RETRY_LOSS_NO_PREPASS) {
        CHECK(strstr(reason, "pre-pass") != NULL, "reason: %s", reason);
    }
    return next;
}

/* a crash in a pre-pass pipeline on a Windows build with a loaded cache:
 * cache, noprepass, then (that run makes none of its pipelines, so a crash
 * names another) d3d12, lazy, the box, every run from noprepass on without
 * the pre-pass; without a cache noprepass first; a loss in a run drawing
 * with the pre-pass: lossnoprepass (not the fixed blend), blend, backend,
 * the box, every loss run without it; a loss in a run without it (the
 * noprepass run, or the self-test turned it off): blend first */
static void test_prepass_chain(void)
{
    static const StartupRetry crash[] = {STARTUP_RETRY_CACHE, STARTUP_RETRY_NO_PREPASS,
                                         STARTUP_RETRY_D3D12, STARTUP_RETRY_LAZY,
                                         STARTUP_RETRY_NONE};
    static const char *const crashValues[] = {"cache:vulkan", "noprepass:vulkan", "d3d12:d3d12",
                                              "lazy:d3d12"};
    static const StartupRetry loss[] = {STARTUP_RETRY_LOSS_NO_PREPASS, STARTUP_RETRY_LOSS_BLEND,
                                        STARTUP_RETRY_LOSS_BACKEND, STARTUP_RETRY_NONE};
    static const char *const lossValues[] = {"lossnoprepass:vulkan", "blend:vulkan",
                                             "backend:d3d12"};
    char v[32], be[16];
    StartupRetry s = STARTUP_RETRY_NONE, p;
    const char *backend = "vulkan";
    bool cache = true, pre = true;
    for (int i = 0; i < 5; i++) {
        s = prepass_step(STARTUP_RETRY_CRASH, s, cache, true, pre, false, false, backend);
        CHECK(s == crash[i], "pre-pass crash chain step %d: %d", i, (int)s);
        if (s == STARTUP_RETRY_NONE) {
            break;
        }
        CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
        CHECK_STR(v, crashValues[i]);
        CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0 && p == s);
        CHECK(startup_retry_value_no_prepass(v) == (s != STARTUP_RETRY_CACHE), "%s", v);
        CHECK(startup_retry_draws_no_prepass(s) == (s != STARTUP_RETRY_CACHE), "%s", v);
        CHECK(!startup_retry_value_draws_fixed(v) && !startup_retry_value_is_loss(v), "%s", v);
        backend = be[0] == 'd' ? "d3d12" : "vulkan";
        cache = false;
        pre = s == STARTUP_RETRY_CACHE; /* the noprepass run makes none of them */
    }
    CHECK(prepass_step(STARTUP_RETRY_CRASH, STARTUP_RETRY_NONE, false, true, true, true, false,
                       "vulkan") == STARTUP_RETRY_NO_PREPASS);
    CHECK(prepass_step(STARTUP_RETRY_CRASH, STARTUP_RETRY_NONE, false, false, true, true, false,
                       "vulkan") == STARTUP_RETRY_NO_PREPASS);
    /* a note naming the pre-pass in a run at noprepass or later: the order
       goes on (a step only grows) */
    CHECK(prepass_step(STARTUP_RETRY_CRASH, STARTUP_RETRY_NO_PREPASS, false, false, true, false,
                       false, "vulkan") == STARTUP_RETRY_LAZY);
    s = STARTUP_RETRY_NONE;
    backend = "vulkan";
    bool preOn = true, fixed = false;
    for (int i = 0; i < 4; i++) {
        s = prepass_step(STARTUP_RETRY_LOSS, s, false, true, false, preOn, fixed, backend);
        CHECK(s == loss[i], "pre-pass loss chain step %d: %d", i, (int)s);
        if (s == STARTUP_RETRY_NONE) {
            break;
        }
        CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
        CHECK_STR(v, lossValues[i]);
        CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0 && p == s);
        CHECK(startup_retry_value_is_loss(v) && startup_retry_is_loss(s), "%s", v);
        CHECK(startup_retry_value_no_prepass(v) && startup_retry_draws_no_prepass(s), "%s", v);
        CHECK(startup_retry_value_draws_fixed(v) == (s != STARTUP_RETRY_LOSS_NO_PREPASS), "%s", v);
        CHECK(startup_retry_draws_fixed(s) == (s != STARTUP_RETRY_LOSS_NO_PREPASS), "%s", v);
        backend = strcmp(be, "d3d12") == 0 ? "d3d12" : "vulkan";
        preOn = false; /* every loss step's run draws without it */
        fixed = s != STARTUP_RETRY_LOSS_NO_PREPASS;
    }
    /* the noprepass crash run (without the pre-pass) losing the device:
       blend; a crash in a lossnoprepass run: the box */
    CHECK(prepass_step(STARTUP_RETRY_LOSS, STARTUP_RETRY_NO_PREPASS, false, true, false, false,
                       false, "vulkan") == STARTUP_RETRY_LOSS_BLEND);
    CHECK(prepass_step(STARTUP_RETRY_CRASH, STARTUP_RETRY_LOSS_NO_PREPASS, false, true, true, false,
                       false, "vulkan") == STARTUP_RETRY_NONE);
    /* a device without an exact blend (already fixed) and without D3D12,
       drawing with the pre-pass: lossnoprepass, then the box */
    CHECK(prepass_step(STARTUP_RETRY_LOSS, STARTUP_RETRY_NONE, false, false, false, true, true,
                       "vulkan") == STARTUP_RETRY_LOSS_NO_PREPASS);
    CHECK(prepass_step(STARTUP_RETRY_LOSS, STARTUP_RETRY_LOSS_NO_PREPASS, false, false, false,
                       false, true, "vulkan") == STARTUP_RETRY_NONE);
}

static void test_values(void)
{
    char v[32], be[16];
    StartupRetry s;
    CHECK(startup_retry_value(STARTUP_RETRY_LAZY, NULL, v, sizeof v) == 0);
    CHECK_STR(v, "lazy:vulkan");
    CHECK(startup_retry_value(STARTUP_RETRY_CACHE, "vulkan", v, 8) == -1);
    CHECK(startup_retry_parse("lazy:vulkan", &s, be, sizeof be) == 0 && s == STARTUP_RETRY_LAZY);
    CHECK_STR(be, "vulkan");
    CHECK(startup_retry_parse("cache:vulkan", &s, be, sizeof be) == 0 && s == STARTUP_RETRY_CACHE);
    CHECK(startup_retry_parse("fixedblend:vulkan", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_CRASH_BLEND);
    CHECK(startup_retry_value(STARTUP_RETRY_CRASH_BLEND, "d3d12", v, sizeof v) == 0);
    CHECK_STR(v, "fixedblend:d3d12");
    /* the steps drawing with the fixed blend: fixedblend and the loss ones */
    CHECK(startup_retry_draws_fixed(STARTUP_RETRY_CRASH_BLEND));
    CHECK(startup_retry_draws_fixed(STARTUP_RETRY_LOSS_BLEND));
    CHECK(startup_retry_draws_fixed(STARTUP_RETRY_LOSS_BACKEND));
    CHECK(!startup_retry_draws_fixed(STARTUP_RETRY_NONE));
    CHECK(!startup_retry_draws_fixed(STARTUP_RETRY_CACHE));
    CHECK(!startup_retry_draws_fixed(STARTUP_RETRY_D3D12));
    CHECK(!startup_retry_draws_fixed(STARTUP_RETRY_LAZY));
    CHECK(startup_retry_value_draws_fixed("fixedblend:vulkan"));
    CHECK(startup_retry_value_draws_fixed("blend:vulkan"));
    CHECK(startup_retry_value_draws_fixed("backend:d3d12"));
    CHECK(!startup_retry_value_draws_fixed("fixedblen:vulkan"));
    CHECK(!startup_retry_value_draws_fixed("fixedblend"));
    CHECK(!startup_retry_value_draws_fixed("lazy:vulkan"));
    CHECK(!startup_retry_value_draws_fixed(NULL));
    CHECK(!startup_retry_value_is_loss("fixedblend:vulkan"));
    CHECK(startup_retry_parse("blend:vulkan", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_LOSS_BLEND);
    CHECK(startup_retry_parse("backend:d3d12", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_LOSS_BACKEND);
    CHECK_STR(be, "d3d12");
    CHECK(startup_retry_value(STARTUP_RETRY_LOSS_BLEND, "d3d12", v, sizeof v) == 0);
    CHECK_STR(v, "blend:d3d12");
    CHECK(startup_retry_value(STARTUP_RETRY_LOSS_BACKEND, NULL, v, sizeof v) == 0);
    CHECK_STR(v, "backend:d3d12");
    /* the loss values, and only they (host_config.c's blend) */
    CHECK(startup_retry_value_is_loss("blend:vulkan"));
    CHECK(startup_retry_value_is_loss("backend:vulkan"));
    CHECK(!startup_retry_value_is_loss("lazy:vulkan"));
    CHECK(!startup_retry_value_is_loss("d3d12:d3d12"));
    CHECK(!startup_retry_value_is_loss("blend"));
    CHECK(!startup_retry_value_is_loss("backen:d3d12"));
    CHECK(!startup_retry_value_is_loss(""));
    CHECK(!startup_retry_value_is_loss(NULL));
    static const char *const bad[] = {"",
                                      "lazy",
                                      "lazy:",
                                      ":vulkan",
                                      "fast:vulkan",
                                      "lazyx:vulkan",
                                      "laz:vulkan",
                                      "blendx:vulkan",
                                      "Blend:vulkan",
                                      "lazy:Vulkan",
                                      "lazy:vul kan",
                                      "lazy:aaaaaaaaaaaaaaaaaaaaaaaa"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        s = STARTUP_RETRY_LAZY;
        CHECK(startup_retry_parse(bad[i], &s, be, sizeof be) == -1, "accepted \"%s\"", bad[i]);
        CHECK(s == STARTUP_RETRY_NONE);
    }
    CHECK(startup_retry_parse(NULL, &s, be, sizeof be) == -1);
    CHECK_STR(startup_retry_name(STARTUP_RETRY_NONE), "");
    CHECK(startup_retry_what(STARTUP_RETRY_CACHE)[0] != '\0');
    CHECK(startup_retry_what(STARTUP_RETRY_D3D12)[0] != '\0');
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_CRASH_BLEND), "simpler blending") != NULL);
    CHECK_STR(startup_retry_name(STARTUP_RETRY_CRASH_BLEND), "fixedblend");
    CHECK(startup_retry_what(STARTUP_RETRY_LAZY)[0] != '\0');
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_BLEND), "fresh launch") != NULL);
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_BACKEND), "fresh launch") != NULL);
    CHECK_STR(startup_retry_name(STARTUP_RETRY_LOSS_BLEND), "blend");
    CHECK_STR(startup_retry_name(STARTUP_RETRY_LOSS_BACKEND), "backend");
    /* the pre-pass's steps */
    CHECK_STR(startup_retry_name(STARTUP_RETRY_NO_PREPASS), "noprepass");
    CHECK_STR(startup_retry_name(STARTUP_RETRY_LOSS_NO_PREPASS), "lossnoprepass");
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_NO_PREPASS), "without the 3D vertex pre-pass") !=
          NULL);
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_NO_PREPASS), "fresh launch") != NULL);
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LAZY), "without the 3D vertex pre-pass") != NULL);
    CHECK(startup_retry_parse("noprepass:vulkan", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_NO_PREPASS);
    CHECK(startup_retry_parse("lossnoprepass:d3d12", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_LOSS_NO_PREPASS);
    CHECK(startup_retry_parse("noprepas:vulkan", &s, be, sizeof be) == -1);
    CHECK(!startup_retry_draws_no_prepass(STARTUP_RETRY_NONE));
    CHECK(!startup_retry_draws_no_prepass(STARTUP_RETRY_CACHE));
    CHECK(!startup_retry_value_no_prepass("cache:vulkan"));
    CHECK(!startup_retry_value_no_prepass("noprepass"));
    CHECK(!startup_retry_value_no_prepass("noprepassx:vulkan"));
    CHECK(!startup_retry_value_no_prepass(NULL));
    CHECK(!startup_retry_value_draws_fixed("lossnoprepass:vulkan"));
    CHECK(!startup_retry_draws_fixed(STARTUP_RETRY_NO_PREPASS));
}

/* the robust step (startup_retry.h): taken after lossnoprepass and before
 * blend when the losing run drew with robust access off by the platform's
 * default; its run draws with robust access on (host_config.c exports
 * ICO_GPU_ROBUST=1 for "robust:...") and without the pre-pass, with the
 * blend as before; the default run (robustOffDefault false, as every
 * caller leaves it) never takes it */
static void test_robust_step(void)
{
    char v[32], be[16];
    StartupRetry s;
    StartupRetryRun run = {
        .why = STARTUP_RETRY_LOSS,
        .canRelaunch = true,
        .blendWasFixed = true, /* the default blend */
        .prepassWasOn = true,
        .robustOffDefault = true,
        .backendName = "vulkan",
    };
    char reason[200];
    static const StartupRetry chain[] = {STARTUP_RETRY_LOSS_NO_PREPASS, STARTUP_RETRY_LOSS_ROBUST,
                                         STARTUP_RETRY_NONE};
    s = STARTUP_RETRY_NONE;
    for (int i = 0; i < 3; i++) {
        run.step = s;
        reason[0] = '\0';
        s = startup_retry_next(&run, reason, sizeof reason);
        CHECK(s == chain[i], "robust chain step %d: %d (%s)", i, (int)s, reason);
        CHECK(reason[0] != '\0');
        if (s == STARTUP_RETRY_LOSS_ROBUST) {
            CHECK(strstr(reason, "robust access") != NULL, "reason: %s", reason);
        }
        run.prepassWasOn = false;
    }
    /* with the player's exact blend: robust, then blend */
    run = (StartupRetryRun){.why = STARTUP_RETRY_LOSS,
                            .step = STARTUP_RETRY_LOSS_ROBUST,
                            .canRelaunch = true,
                            .robustOffDefault = true,
                            .backendName = "vulkan"};
    CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LOSS_BLEND);
    run.step = STARTUP_RETRY_NONE;
    CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LOSS_ROBUST);
    /* no caller's default: never taken */
    run.robustOffDefault = false;
    CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LOSS_BLEND);
    run.blendWasFixed = true;
    CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_NONE);
    /* Android cannot start again: the box, whatever the run */
    run.canRelaunch = false;
    run.robustOffDefault = true;
    CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_NONE);
    /* a crash in a robust run: the box */
    run = (StartupRetryRun){.why = STARTUP_RETRY_CRASH,
                            .step = STARTUP_RETRY_LOSS_ROBUST,
                            .canRelaunch = true,
                            .backendName = "vulkan"};
    CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_NONE);
    /* the value and what it means */
    CHECK(startup_retry_value(STARTUP_RETRY_LOSS_ROBUST, "vulkan", v, sizeof v) == 0);
    CHECK_STR(v, "robust:vulkan");
    CHECK(startup_retry_parse(v, &s, be, sizeof be) == 0 && s == STARTUP_RETRY_LOSS_ROBUST);
    CHECK_STR(startup_retry_name(STARTUP_RETRY_LOSS_ROBUST), "robust");
    CHECK(startup_retry_is_loss(STARTUP_RETRY_LOSS_ROBUST) && startup_retry_value_is_loss(v));
    CHECK(startup_retry_value_robust(v));
    CHECK(!startup_retry_value_robust("blend:vulkan") && !startup_retry_value_robust("robust"));
    CHECK(!startup_retry_value_robust(NULL));
    CHECK(startup_retry_value_no_prepass(v) && startup_retry_draws_no_prepass(s));
    CHECK(!startup_retry_value_draws_fixed(v) && !startup_retry_draws_fixed(s));
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_ROBUST), "robust access") != NULL);
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_ROBUST), "fresh launch") != NULL);
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_BLEND), "simpler blending") != NULL);
    /* the step only grows: robust sits between lossnoprepass and blend */
    CHECK(STARTUP_RETRY_LOSS_NO_PREPASS < STARTUP_RETRY_LOSS_ROBUST &&
          STARTUP_RETRY_LOSS_ROBUST < STARTUP_RETRY_LOSS_BLEND);
}

static void test_envp(void)
{
    char *env[] = {"HOME=/home/x",
                   "ICO_STARTUP_RETRY=lazy:vulkan",
                   "PATH=/bin",
                   "ico_startup_retry=cache:vulkan",
                   "ICO_STARTUP_RETRYX=1",
                   NULL};
    const char *out[8];
    int n = startup_retry_envp(env, "ICO_STARTUP_RETRY=d3d12:d3d12", out, 8);
    CHECK(n == 4);
    CHECK_STR(out[0], "HOME=/home/x");
    CHECK_STR(out[1], "PATH=/bin");
    CHECK_STR(out[2], "ICO_STARTUP_RETRYX=1");
    CHECK_STR(out[3], "ICO_STARTUP_RETRY=d3d12:d3d12");
    CHECK(out[4] == NULL);
    CHECK(startup_retry_envp(env, NULL, out, 8) == 3 && out[3] == NULL);
    CHECK(startup_retry_envp(env, "X=1", out, 4) == -1);
    CHECK(startup_retry_envp(env, "X=1", out, 5) == 4);
    CHECK(startup_retry_envp(NULL, NULL, out, 1) == 0 && out[0] == NULL);
    /* the relaunch after a loss: the inherited step replaced by the loss
       step, the rest (an ICO_RD_BLEND the player set among it) kept */
    char *env2[] = {"ICO_RD_BLEND=fixed", "ICO_STARTUP_RETRY=blend:vulkan", "HOME=/h", NULL};
    n = startup_retry_envp(env2, "ICO_STARTUP_RETRY=backend:d3d12", out, 8);
    CHECK(n == 3);
    CHECK_STR(out[0], "ICO_RD_BLEND=fixed");
    CHECK_STR(out[1], "HOME=/h");
    CHECK_STR(out[2], "ICO_STARTUP_RETRY=backend:d3d12");
    CHECK(out[3] == NULL);
}

static void test_block(void)
{
    static const wchar_t in[] = L"=C:=C:\\x\0A=1\0Ico_Startup_Retry=lazy:vulkan\0B=2\0";
    wchar_t out[64];
    long used = startup_retry_block_copy(in, out, 64);
    static const wchar_t want[] = L"=C:=C:\\x\0A=1\0B=2\0ICO_STARTUP_RETRY=lazy:d3d12\0";
    CHECK(used == 17, "used %ld", used);
    CHECK(startup_retry_block_finish(out, (size_t)used, 64, "lazy:d3d12") == 0);
    CHECK(memcmp(out, want, sizeof want) == 0); /* sizeof: the final NUL too */
    CHECK(startup_retry_block_copy(in, out, 10) == -1);
    CHECK(startup_retry_block_finish(out, (size_t)used, (size_t)used + 20, "lazy:d3d12") == -1);
    CHECK(startup_retry_block_copy(L"", out, 4) == 0);
    /* the loss value in the Windows block */
    static const wchar_t in2[] = L"A=1\0ICO_STARTUP_RETRY=lazy:d3d12\0";
    static const wchar_t want2[] = L"A=1\0ICO_STARTUP_RETRY=blend:d3d12\0";
    used = startup_retry_block_copy(in2, out, 64);
    CHECK(used == 4, "used %ld", used);
    CHECK(startup_retry_block_finish(out, (size_t)used, 64, "blend:d3d12") == 0);
    CHECK(memcmp(out, want2, sizeof want2) == 0);
}

int main(void)
{
    test_table();
    test_chain();
    test_loss_chain();
    test_dst_chain();
    test_note();
    test_note_vu_pre();
    test_prepass_chain();
    test_values();
    test_robust_step();
    test_envp();
    test_block();
    return ico_check_summary("startup_retry");
}
