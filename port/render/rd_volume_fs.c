/* rd_volume_fs.c: the shadow volumes drawn by a fragment entry with no
 * output and no discard (rd_volume_fs.h says what and why;
 * docs/port/RENDER_POST.md, "The shadow volumes' stencil-only entry").
 *
 *   rd__volume_fs_why    the gate, pure
 *   rd__volume_fs_turn   rd_pipeline.c rd__shadow_volume_key, the volume
 *                        draw's key: date_clear_ps's twin where taken,
 *                        counted per replay, run and 10 s block, and in the
 *                        attempt ledger
 *   rd__volume_fs_selftest  the shadow count's self-test frame replayed with
 *                        the volumes by both entries, the counts compared
 *                        byte for byte (the file's end)
 *
 * No module of its own: date_clear_ps (RD_FS_DATE_CLEAR) is in every
 * build's table (rd_replay.c's name table), so nothing is added to
 * rd_replay.c or to the shader build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_replay.h"
#include "rd_volume_fs.h"

static const char *const kWhy[RD_VOLUME_FS_WHY_COUNT] = {
    [RD_VOLUME_FS_WHY_NONE] = "drawn by date_clear_ps",
    [RD_VOLUME_FS_WHY_OFF] = "off (ICO_RD_VOLUME_FS=0 or the build's RD_VOLUME_FS_DEFAULT 0)",
    [RD_VOLUME_FS_WHY_BACKEND] = "no Vulkan device with date_clear_ps (Vulkan alone)",
    [RD_VOLUME_FS_WHY_TEST] = "its self-test has not passed",
    [RD_VOLUME_FS_WHY_MSAA] = "a multisampled pass (the MSAA epoch keeps sprite_ps)",
    [RD_VOLUME_FS_WHY_PIPE] = "its pipeline not made",
};

const char *rd__volume_fs_why_text(int why)
{
    return why >= 0 && why < RD_VOLUME_FS_WHY_COUNT ? kWhy[why] : "?";
}

static struct {
    int env; /* rd__volume_fs_env, read at init (or the first ask) */
    bool envRead;
    int force;    /* the bench's: -1 none, 0, 1 */
    int test;     /* 0 not run, 1 passed, 2 failed or not run with a reason */
    bool testing; /* inside the self-test's replays */
    int testArm;  /* there: 0 sprite_ps, 1 date_clear_ps */
    char words[224];
    uint32_t replay; /* g_rd.replayCounter of rTaken, rSeen */
    uint32_t rTaken, rSeen;
    uint64_t taken, seen; /* since rd_init, the self-test's replays left out */
    int lastWhy;
    bool said[RD_VOLUME_FS_WHY_COUNT], firstSaid;
    char stateSaid[384];
} s_vf = {.env = -1, .force = -1};

/* the 10 s block's counts (__atomic: the render thread adds, the window's
 * line takes) */
static struct {
    uint64_t taken, seen, why[RD_VOLUME_FS_WHY_COUNT];
} s_blk;

int rd__volume_fs_env(const char *env)
{
    const char *e = env ? env : getenv("ICO_RD_VOLUME_FS"); /* the tests only: no config key */
    if (!e || !e[0]) {
        return -1;
    }
    if (strcmp(e, "sabotage") == 0) {
        return 2;
    }
    return e[0] == '0' ? 0 : 1;
}

static void envRead(void)
{
    if (!s_vf.envRead) {
        s_vf.envRead = true;
        s_vf.env = rd__volume_fs_env(NULL);
    }
}

bool rd__volume_fs_wanted(void)
{
    envRead();
    if (s_vf.force >= 0) {
        return s_vf.force == 1;
    }
    return s_vf.env >= 1 || (s_vf.env < 0 && RD_VOLUME_FS_DEFAULT);
}

/* a Vulkan device with date_clear_ps's module */
static bool offered(void)
{
    return g_rd.hasDevice && rhi_backend() == RHI_BACKEND_VULKAN &&
           g_rd.fs[RD_FS_DATE_CLEAR].id != 0;
}

int rd__volume_fs_why(int wanted, int offer, int test, uint32_t samples)
{
    if (!wanted) {
        return RD_VOLUME_FS_WHY_OFF;
    }
    if (!offer) {
        return RD_VOLUME_FS_WHY_BACKEND;
    }
    if (test != 1) {
        return RD_VOLUME_FS_WHY_TEST;
    }
    if (samples > 1) {
        return RD_VOLUME_FS_WHY_MSAA;
    }
    return RD_VOLUME_FS_WHY_NONE;
}

RdPipeKeyInt rd__volume_fs_twin(const RdPipeKeyInt *general)
{
    RdPipeKeyInt k = *general;
    if (k.gs.program == RD_PROG_SHADOW_VOLUME && k.fs == RD_FS_SPRITE) {
        k.fs = RD_FS_DATE_CLEAR;
    }
    return k;
}

bool rd__volume_fs_on(void)
{
    return offered() && rd__volume_fs_wanted() && s_vf.test == 1;
}

static void blkAdd(uint64_t *v, uint64_t n)
{
    __atomic_fetch_add(v, n, __ATOMIC_RELAXED);
}

RdPipeKeyInt rd__volume_fs_turn(const RdPipeKeyInt *general, int decr)
{
    if (general->gs.program != RD_PROG_SHADOW_VOLUME || general->fs != RD_FS_SPRITE) {
        return *general;
    }
    if (s_vf.replay != g_rd.replayCounter) {
        s_vf.replay = g_rd.replayCounter;
        s_vf.rTaken = s_vf.rSeen = 0;
    }
    s_vf.rSeen++;
    if (s_vf.testing) {
        /* the self-test's replays: arm 0 sprite_ps, arm 1 date_clear_ps
         * (under the sabotage each increment drawn as a decrement and each
         * decrement as an increment); not counted for the run or the block */
        if (s_vf.testArm == 0) {
            return *general;
        }
        RdPipeKeyInt t = rd__volume_fs_twin(general);
        if (s_vf.env == 2) {
            t.gs.stencil = decr ? RD_STENCIL_INCR : RD_STENCIL_DECR;
        }
        if (rd__get_pipeline(&t).id) {
            s_vf.rTaken++;
            return t;
        }
        return *general;
    }
    s_vf.seen++;
    blkAdd(&s_blk.seen, 1);
    int why = rd__volume_fs_why(rd__volume_fs_wanted(), offered(), s_vf.test, g_rd.passSamples);
    if (why == RD_VOLUME_FS_WHY_NONE) {
        const RdPipeKeyInt t = rd__volume_fs_twin(general);
        if (rd__get_pipeline(&t).id) {
            s_vf.rTaken++;
            s_vf.taken++;
            blkAdd(&s_blk.taken, 1);
            ico_attempt_fire(ICO_ATT_VOLUME_FS, 1);
            if (!s_vf.firstSaid) {
                s_vf.firstSaid = true;
                rd__log("shadow volumes: drawn by date_clear_ps (no output, no discard: the "
                        "stencil written in the depth and stencil tests, before any shading), "
                        "frame %u (reported once)",
                        g_rd.replayCounter);
            }
            return t;
        }
        why = RD_VOLUME_FS_WHY_PIPE;
    }
    s_vf.lastWhy = why;
    blkAdd(&s_blk.why[why], 1);
    if (why != RD_VOLUME_FS_WHY_OFF && why != RD_VOLUME_FS_WHY_BACKEND) {
        ico_attempt_fire2(ICO_ATT_VOLUME_FS, 1);
    }
    if (why != RD_VOLUME_FS_WHY_OFF && !s_vf.said[why]) {
        s_vf.said[why] = true;
        rd__log("shadow volumes: a volume draw kept sprite_ps: %s (frame %u, the pass's "
                "samples %u; reported once for this reason)",
                kWhy[why], g_rd.replayCounter, g_rd.passSamples ? g_rd.passSamples : 1u);
    }
    return *general;
}

uint32_t rd__volume_fs_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n,
                            const RdPipeKeyInt *general, uint32_t n0)
{
    if (!offered() || !rd__volume_fs_wanted()) {
        return n;
    }
    for (uint32_t i = 0; i < n0; i++) {
        const RdPipeKeyInt k = rd__volume_fs_twin(&general[i]);
        n = rd__add_pipe_key(out, max, n, &k);
    }
    return n;
}

/* the state's line, when it changes, and the ledger's row */
static void stateNote(void)
{
    char line[sizeof(s_vf.stateSaid)];
    const char *how =
        s_vf.force == 0   ? "the bench's variant: off"
        : s_vf.force == 1 ? "the bench's variant: on"
        : s_vf.env == 0   ? "ICO_RD_VOLUME_FS=0"
        : s_vf.env == 1   ? "ICO_RD_VOLUME_FS=1"
        : s_vf.env == 2 ? "ICO_RD_VOLUME_FS=sabotage"
                        : (RD_VOLUME_FS_DEFAULT ? "the build's default" : "RD_VOLUME_FS_DEFAULT 0");
    if (!rd__volume_fs_wanted()) {
        snprintf(line, sizeof(line), "off (%s)", how);
    } else if (!offered()) {
        snprintf(line, sizeof(line), "off (%s): %s", how,
                 !g_rd.hasDevice ? "no device"
                 : rhi_backend() != RHI_BACKEND_VULKAN
                     ? "under Vulkan alone; sprite_ps draws the volumes"
                     : "no date_clear_ps module in the shader table");
    } else if (s_vf.test == 1) {
        snprintf(line, sizeof(line), "on (%s; self-test %s)", how, s_vf.words);
    } else {
        snprintf(line, sizeof(line), "off until its self-test passes (%s; self-test %s)", how,
                 s_vf.words[0] ? s_vf.words : "not run yet");
    }
    if (strcmp(line, s_vf.stateSaid) != 0) {
        snprintf(s_vf.stateSaid, sizeof(s_vf.stateSaid), "%s", line);
        rd__log("shadow volumes: the stencil-only entry (date_clear_ps in place of sprite_ps: no "
                "colour output, no discard): %s",
                line);
    }
    (void)ico_attempt_decide_volume_fs(offered() ? 1 : 0, s_vf.env < 0 ? -1 : (s_vf.env ? 1 : 0),
                                       RD_VOLUME_FS_DEFAULT,
                                       s_vf.test == 1 ? 1 : (s_vf.test == 2 ? 0 : -1));
}

void rd__volume_fs_init(void)
{
    memset(&s_vf, 0, sizeof(s_vf));
    s_vf.force = -1;
    s_vf.env = rd__volume_fs_env(NULL);
    s_vf.envRead = true;
    stateNote();
}

void rd__volume_fs_force(int v)
{
    s_vf.force = v < 0 ? -1 : (v ? 1 : 0);
    stateNote();
}

int rd__volume_fs_test_state(const char **words)
{
    if (words) {
        *words = s_vf.words[0] ? s_vf.words : "not run";
    }
    return s_vf.test;
}

void rd__volume_fs_counts(uint64_t *taken, uint64_t *seen, int *lastWhy)
{
    *taken = s_vf.taken;
    *seen = s_vf.seen;
    *lastWhy = s_vf.lastWhy;
}

void rd__volume_fs_replay(uint32_t *taken, uint32_t *seen)
{
    const bool now = s_vf.replay == g_rd.replayCounter;
    *taken = now ? s_vf.rTaken : 0u;
    *seen = now ? s_vf.rSeen : 0u;
}

void rd__volume_fs_line(void)
{
    const uint64_t taken = __atomic_exchange_n(&s_blk.taken, 0, __ATOMIC_RELAXED);
    const uint64_t seen = __atomic_exchange_n(&s_blk.seen, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_VOLUME_FS_WHY_COUNT];
    for (int i = 0; i < RD_VOLUME_FS_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_blk.why[i], 0, __ATOMIC_RELAXED);
    }
    if (seen == 0) {
        return;
    }
    char why[640] = "";
    size_t at = 0;
    for (int i = 1; i < RD_VOLUME_FS_WHY_COUNT; i++) {
        if (!w[i]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)w[i], kWhy[i]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    fprintf(stderr,
            "window: shadow volumes: %llu volume draws by the stencil-only entry of %llu, %llu "
            "kept on sprite_ps%s%s\n",
            (unsigned long long)taken, (unsigned long long)seen, (unsigned long long)(seen - taken),
            why, at ? ")" : "");
}

/* ------------------------------------------------------------ the self-test
 * The shadow count's self-test frame (rd_shadow_selftest.c buildFrame: 70
 * bands of volume rectangles over SCENE at sub-pixel edges, every count
 * 0..63 of both signs, resolved under the running disc's step) built twice,
 * under Z ALWAYS and under the game's Z GEQUAL (SCENE's depth cleared to a
 * middle value, the odd bands behind it), each replayed with the volumes by
 * sprite_ps (arm 0) and by date_clear_ps (arm 1), the count at its GS size
 * read back each time.  Passed: in both frames arm 1's count is arm 0's
 * byte for byte, arm 0 drew no volume by date_clear_ps and arm 1 drew every
 * one by it, and the two frames' counts differ (the Z test acted).  The
 * count is the stencil's only reader (the resolve, by whichever path the
 * profile has: the one draw or the seven draws), so its bytes are the
 * volumes' whole effect.  MSAA held off for the replays (SCENE's colour is
 * the count target), as rd__shadow_selftest does. */

/* where two counts differ: the count, the first texel */
static size_t diffCount(const uint8_t *a, const uint8_t *b, size_t texels, size_t *first)
{
    size_t n = 0;
    *first = 0;
    for (size_t i = 0; i < texels; i++) {
        if (memcmp(&a[i * 4u], &b[i * 4u], 4) != 0) {
            if (!n++) {
                *first = i;
            }
        }
    }
    return n;
}

bool rd__volume_fs_selftest(void)
{
    envRead();
    s_vf.test = 0;
    s_vf.words[0] = '\0';
    if (!g_rd.inited || !offered() || !rd__volume_fs_wanted()) {
        stateNote();
        return true; /* not asked here: nothing to check */
    }
    s_vf.test = 2;
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    RdTargetRec *t = scene ? rd__target_rec(scene) : NULL;
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM) {
        snprintf(s_vf.words, sizeof(s_vf.words), "not run: no SCENE target to run it on");
        rd__log("shadow volumes: self-test not run (no SCENE target to run it on): sprite_ps "
                "draws the volumes");
        stateNote();
        return false;
    }
    const uint32_t w = t->w, h = t->h;
    const size_t texels = (size_t)w * h, size = texels * 4u;
    uint8_t *px[2][2] = {{malloc(size), malloc(size)}, {malloc(size), malloc(size)}};
    static const uint64_t kTests[2] = {RD_TEST_Z_ALWAYS, RD_TEST_Z_GEQUAL};
    static const char *const kTestName[2] = {"Z ALWAYS", "Z GEQUAL"};
    const uint32_t step = rd__shadow_disc_step();
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true; /* SCENE's colour is this count's target */
    bool done[2][2] = {{false, false}, {false, false}};
    uint32_t took[2][2] = {{0, 0}, {0, 0}}, seen[2][2] = {{0, 0}, {0, 0}};
    const char *err = NULL;
    for (int ti = 0; ti < 2 && !err; ti++) {
        static RdFrame f;
        memset(&f, 0, sizeof(f)); /* the last frame was freed: nothing of it held */
        if (!px[ti][0] || !px[ti][1]) {
            err = "no memory for its readbacks";
            break;
        }
        if (!rd__shadow_selftest_frame(&f, scene, w, h, step, kTests[ti])) {
            err = "could not build its frame";
            rd__frame_free(&f);
            break;
        }
        for (int arm = 0; arm < 2; arm++) {
            s_vf.testing = true;
            s_vf.testArm = arm;
            done[ti][arm] = rd__shadow_selftest_replay(&f, scene, px[ti][arm], size, w, h);
            s_vf.testing = false;
            rd__volume_fs_replay(&took[ti][arm], &seen[ti][arm]);
            if (!done[ti][arm]) {
                err = "replay or readback error";
                break;
            }
        }
        rd__frame_free(&f);
    }
    s_vf.testing = false;
    g_rd.msaaBlocked = wasBlocked;
    rd__shadow_count_forget(); /* the count's target was SCENE */
    t->clearPending = 1;       /* the next replay starts SCENE from its clear */

    size_t nd[2] = {0, 0}, first[2] = {0, 0}, zFirst = 0;
    int bad = -1;
    for (int ti = 0; ti < 2 && !err; ti++) {
        nd[ti] = diffCount(px[ti][0], px[ti][1], texels, &first[ti]);
        if (bad < 0 && nd[ti]) {
            bad = ti;
        }
    }
    const size_t zDiff = err ? 0 : diffCount(px[0][0], px[1][0], texels, &zFirst);
    const bool tookAll = !err && seen[0][1] > 0 && took[0][1] == seen[0][1] && seen[1][1] > 0 &&
                         took[1][1] == seen[1][1] && took[0][0] == 0 && took[1][0] == 0;
    const bool sabotage = s_vf.env == 2;
    if (err) {
        snprintf(s_vf.words, sizeof(s_vf.words), "not run: %s", err);
        rd__log("shadow volumes: self-test not run (%s): sprite_ps draws the volumes", err);
    } else if (!tookAll) {
        snprintf(s_vf.words, sizeof(s_vf.words),
                 "not run: the volume draws not taken as asked, %u of %u and %u of %u by "
                 "date_clear_ps, %u and %u in the sprite_ps replays",
                 took[0][1], seen[0][1], took[1][1], seen[1][1], took[0][0], took[1][0]);
        rd__log("shadow volumes: self-test %s: sprite_ps draws the volumes", s_vf.words);
    } else if (bad >= 0) {
        const size_t at = first[bad];
        const uint8_t *a = &px[bad][1][at * 4u], *b = &px[bad][0][at * 4u];
        snprintf(s_vf.words, sizeof(s_vf.words), "failed at texel %zu of the %s frame", at,
                 kTestName[bad]);
        rd__log("shadow volumes: self-test failed: the %s frame's count differs at %zu of %zu "
                "texels, the first (%zu,%zu): %02X%02X%02X%02X for %02X%02X%02X%02X%s: sprite_ps "
                "draws the volumes",
                kTestName[bad], nd[bad], texels, at % w, at / w, a[0], a[1], a[2], a[3], b[0], b[1],
                b[2], b[3], sabotage ? " (ICO_RD_VOLUME_FS=sabotage)" : "");
    } else if (zDiff == 0) {
        snprintf(s_vf.words, sizeof(s_vf.words),
                 "not run: the Z GEQUAL frame counted as the Z ALWAYS frame, the depth test did "
                 "not act");
        rd__log("shadow volumes: self-test not run (the Z GEQUAL frame's count is the Z ALWAYS "
                "frame's: its depth test did not act): sprite_ps draws the volumes");
    } else {
        s_vf.test = 1;
        /* no brackets in the words: the tool's stats line holds them in its own */
        snprintf(s_vf.words, sizeof(s_vf.words),
                 "passed: the count the same at %zu texels of %ux%u under Z ALWAYS and Z GEQUAL, "
                 "%u and %u volume draws each way",
                 texels, w, h, seen[0][1], seen[1][1]);
        rd__log("shadow volumes: the stencil-only entry (self-test: the count the same at %zu "
                "texels of %ux%u at %ux%u texels under Z ALWAYS and Z GEQUAL, step %u; the Z "
                "GEQUAL frame differs from the Z ALWAYS one at %zu texels): the same bytes",
                texels, w, h, t->tw, t->th, step, zDiff);
    }
    for (int i = 0; i < 2; i++) {
        free(px[i][0]);
        free(px[i][1]);
    }
    stateNote();
    return s_vf.test == 1;
}
