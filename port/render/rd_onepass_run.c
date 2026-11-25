/* rd_onepass_run.c: the one-pass blend in the renderer (rd_onepass.h): its
 * state for the run, the install's mark, a pair's choice at its draw, a
 * code 36 command's unfuse, the blend constant, the counts and the lines.
 * The fast pixel build's form (rd_fastps_run.c), one subject.
 *
 * The state: off (ICO_RD_ONEPASS=0, the loss ladder, the install's mark,
 * a check that put nothing in use, a pipeline the background could not
 * make), held (until the check: every pair in two passes, nothing unfused)
 * or on with its classes.  ICO_RD_ONEPASS=plain|full (tests) starts on
 * without the check, its pipelines made in the draw.
 *
 *   rd: one-pass blend: off for this start (the loss ladder: ...)
 *   rd: one-pass blend: off for the run: a pipeline of it could not be made
 *     in the background (...)
 *   rd: one-pass blend: 250 pictures drawn with it since its check: its
 *     mark says on
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h> /* _commit */
#else
#include <unistd.h> /* fsync */
#endif

#include "host_fs.h" /* ico_fopen: UTF-8 paths on Windows */
#include "rd_ab_run.h"
#include "rd_internal.h"
#include "rd_nodiscard.h" /* rd__nd_applicable: the device's own features */
#include "rd_onepass.h"

enum { O_HELD, O_ON, O_OFF };

/* ICO_RD_ONEPASS_SABOTAGE (tests) */
enum { OS_NONE, OS_CONSTANT, OS_CUT, OS_UNMADE, OS_SLIVER, OS_READ };

static struct {
    int state;
    uint32_t classes; /* in use (RD_OP_PLAIN, RD_OP_CUT) */
    uint32_t forced;  /* ICO_RD_ONEPASS=plain|full: the classes, no check */
    int arm;          /* the check's arm drawn (RD_OP_ARM_*), -1 none */
    bool sabNow;      /* the check's taken pairs at 127/255 */
    int sabotage;
    bool cmdFused;       /* the code 36 command being drawn fused */
    uint32_t pictures;   /* pictures drawn with it since the check */
    bool markedOn;       /* "on" written */
    bool wasOn;          /* on at some point of the run (the 10 s clause when off) */
    bool drewNow;        /* the replay being made drew a pair in one pass */
    bool tried;          /* "trying" written */
    char why[384];       /* off's or held's words */
    char markPath[1024]; /* ICO_ONEPASS_MARK: the platform's mark file, "" none */
    char version[64];    /* ICO_ONEPASS_VERSION */
    uint64_t chkOne[2], chkUnfused, chkUnmade; /* the check's, since rd__op_check_take */
    RdOpUse use;                               /* the replay being made, for the judge */
} s_o = {.arm = -1};

/* off for the run: kept through a renderer made again in the same process */
static char s_runOff[384];

/* the counts: the block's (the window's 10 s line) and the run's (the
   replay tool's stats); written on the replaying thread, read from the
   window's (atomics) */
typedef struct OCounts {
    uint64_t pairs, one, oneCut, tris, unfused, refused[RD_OP_REASONS];
} OCounts;

static OCounts s_block, s_run;

static void add(uint64_t *a, uint64_t *b, uint64_t v)
{
    __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
    __atomic_fetch_add(b, v, __ATOMIC_RELAXED);
}

static void setOff(const char *why)
{
    s_o.state = O_OFF;
    s_o.classes = 0;
    snprintf(s_o.why, sizeof(s_o.why), "%s", why);
}

static void setRunOff(const char *why)
{
    setOff(why);
    snprintf(s_runOff, sizeof(s_runOff), "%s", s_o.why);
}

/* the mark (port/platform/onepass_mark.h): the stage word and the running
   version, synced where asked (trying, counting, failed) */
static bool writeMark(const char *word, bool sync)
{
    if (s_o.markPath[0] == '\0') {
        return true; /* no platform's mark (the replay tool, the tests) */
    }
    FILE *f = ico_fopen(s_o.markPath, "wb");
    if (f == NULL) {
        rd__log("one-pass blend: its mark %s could not be written (%s)", s_o.markPath, word);
        return false;
    }
    bool ok = (s_o.version[0] != '\0' ? fprintf(f, "%s %s\n", word, s_o.version)
                                      : fprintf(f, "%s\n", word)) > 0;
    ok = fflush(f) == 0 && ok;
    if (sync) {
#ifdef _WIN32
        (void)_commit(_fileno(f));
#else
        (void)fsync(fileno(f));
#endif
    }
    return fclose(f) == 0 && ok;
}

void rd__op_init(void)
{
    s_o.arm = -1;
    s_o.sabNow = false;
    s_o.cmdFused = false;
    s_o.pictures = 0;
    s_o.markedOn = false;
    s_o.wasOn = false;
    s_o.drewNow = false;
    s_o.tried = false;
    s_o.classes = 0;
    s_o.forced = 0;
    memset(s_o.chkOne, 0, sizeof(s_o.chkOne));
    s_o.chkUnfused = s_o.chkUnmade = 0;
    memset(&s_o.use, 0, sizeof(s_o.use));
    const char *mark = getenv("ICO_ONEPASS_MARK");
    snprintf(s_o.markPath, sizeof(s_o.markPath), "%s", mark != NULL ? mark : "");
    const char *ver = getenv("ICO_ONEPASS_VERSION");
    snprintf(s_o.version, sizeof(s_o.version), "%.*s", 63,
             ver != NULL && strchr(ver, '\n') == NULL ? ver : "");
    const char *block = getenv("ICO_ONEPASS_BLOCK");
    s_o.state = O_HELD;
    snprintf(s_o.why, sizeof(s_o.why), "until its device check");
    const char *sb = getenv("ICO_RD_ONEPASS_SABOTAGE");
    s_o.sabotage = sb == NULL                    ? OS_NONE
                   : strcmp(sb, "constant") == 0 ? OS_CONSTANT
                   : strcmp(sb, "cut") == 0      ? OS_CUT
                   : strcmp(sb, "unmade") == 0   ? OS_UNMADE
                   : strcmp(sb, "sliver") == 0   ? OS_SLIVER
                   : strcmp(sb, "read") == 0     ? OS_READ
                                                 : OS_NONE;
    if (s_o.sabotage != OS_NONE) {
        rd__log("one-pass blend: ICO_RD_ONEPASS_SABOTAGE: %s",
                s_o.sabotage == OS_CONSTANT ? "the constant's alpha 127/255 in the check's arms A "
                                              "and C (both must differ)"
                : s_o.sabotage == OS_CUT    ? "the constant's alpha 127/255 in the check's arm C "
                                              "on its code 36 frames (C must differ, A not)"
                : s_o.sabotage == OS_SLIVER ? "arm C's picture given a texel on the code 36 "
                                              "frames today's two passes drew nothing on (C must "
                                              "differ)"
                : s_o.sabotage == OS_READ   ? "the first frame's reference read taken as failed "
                                              "without a device loss (not checked, no verdict)"
                                            : "every one-pass pipeline taken as not made in the "
                                              "check (its arms not checked)");
    }
    const char *env = getenv("ICO_RD_ONEPASS");
    const char *row = getenv("ICO_LOSS_ROW");
    const long lost = row != NULL ? strtol(row, NULL, 10) : 0;
    if (s_runOff[0] != '\0') {
        setOff(s_runOff);
    } else if (block != NULL && block[0] != '\0') {
        char why[320];
        snprintf(why, sizeof(why), "on this install (%.280s)", block);
        setOff(why);
        rd__log("one-pass blend: off %s", s_o.why);
    } else if (env != NULL && strcmp(env, "0") == 0) {
        setOff("ICO_RD_ONEPASS=0");
    } else if (lost > 0) {
        char why[256];
        snprintf(why, sizeof(why),
                 "for this start (the loss ladder: %ld start%s in a row before this one lost the "
                 "graphics device or hung its driver, ICO_LOSS_ROW %ld; nothing written to the "
                 "settings)",
                 lost, lost == 1 ? "" : "s", lost);
        setOff(why);
        rd__log("one-pass blend: off %s", s_o.why);
    } else if (env != NULL && (strcmp(env, "plain") == 0 || strcmp(env, "full") == 0)) {
        s_o.forced = strcmp(env, "full") == 0 ? (RD_OP_PLAIN | RD_OP_CUT) : RD_OP_PLAIN;
        s_o.state = O_ON;
        s_o.classes = s_o.forced;
        s_o.wasOn = true;
        snprintf(s_o.why, sizeof(s_o.why), "ICO_RD_ONEPASS=%s", env);
        rd__log("one-pass blend: on without its check (ICO_RD_ONEPASS=%s: %s), its pipelines "
                "made in the draw",
                env, rd_op_classes_name(s_o.classes));
    }
}

bool rd__op_on(void)
{
    return s_o.state == O_ON || s_o.arm >= 0;
}

bool rd__op_triable(void)
{
    return s_o.state == O_ON && s_o.arm < 0 && s_o.classes != 0u;
}

bool rd__op_forced(void)
{
    return s_o.forced != 0u;
}

int rd__op_sabotage(void)
{
    return s_o.sabotage;
}

const char *rd__op_state_text(void)
{
    return s_o.state == O_ON ? "on" : s_o.state == O_HELD ? "held" : "off";
}

const char *rd__op_not_applicable_text(void)
{
    if (!g_rd.hasDevice) {
        return NULL;
    }
    if (rhi_backend() != RHI_BACKEND_VULKAN) {
        return "not applicable here: not the Vulkan backend (it is for a Vulkan device without "
               "dual-source blending)";
    }
    if (rd__nd_applicable()) {
        return NULL;
    }
    return g_rd.noDual ? "not applicable here: the device has dual-source blending (the two-pass "
                         "blend is its fallback): it is for a device without it"
                       : "not applicable here: the device blends with two outputs, so no pair is "
                         "drawn in two passes";
}

static void count(int why, uint32_t cls, uint32_t tris)
{
    if (s_o.arm >= 0) {
        /* the check's (its own counts) */
        if (why == RD_OP_ONE) {
            s_o.chkOne[cls == RD_OP_CUT ? 1 : 0]++;
        } else if (why == RD_OP_UNMADE) {
            s_o.chkUnmade++;
        }
        return;
    }
    add(&s_block.pairs, &s_run.pairs, 1u);
    if (why == RD_OP_ONE) {
        add(&s_block.one, &s_run.one, 1u);
        add(&s_block.tris, &s_run.tris, tris);
        if (cls == RD_OP_CUT) {
            add(&s_block.oneCut, &s_run.oneCut, 1u);
        }
    } else if (why > 0 && why < RD_OP_REASONS) {
        add(&s_block.refused[why], &s_run.refused[why], 1u);
    }
}

/* the classes the replay being made may take: the check's arm's, or play's */
static uint32_t useNow(void)
{
    switch (s_o.arm) {
    case RD_OP_ARM_A:
        return RD_OP_PLAIN;
    case RD_OP_ARM_C:
        return RD_OP_PLAIN | RD_OP_CUT;
    case RD_OP_ARM_REF:
    case RD_OP_ARM_B:
        return 0u;
    default:
        return s_o.state == O_ON ? s_o.classes : 0u;
    }
}

/* the one-pass key of a pair's colour pass: A in the mask, the bit */
static RdPipeKeyInt onepassKey(const RdPipeKeyInt *k)
{
    RdPipeKeyInt t = *k;
    t.gs.colorMask = (uint8_t)(t.gs.colorMask | 8u);
    t.gs.nodual = (uint8_t)(t.gs.nodual | RD_NODUAL_ONEPASS);
    return t;
}

/* whether key t's pipeline is made (asked for in the background when not;
   the forced tests' made in the draw by rd__get_pipeline, the gathering's
   named) */
static bool madeOrAsked(const RdPipeKeyInt *t)
{
    if (rd__pipeline_gathering() || s_o.forced) {
        return true;
    }
    if (s_o.arm >= 0 && s_o.sabotage == OS_UNMADE && (t->gs.nodual & RD_NODUAL_ONEPASS)) {
        (void)rd__pipeline_twin(t);
        return false; /* the tests' =unmade */
    }
    return rd__pipeline_twin(t).id != 0;
}

void rd__op_take(RdDrawPass *dp, int *np, uint8_t hi, uint32_t vuBits, uint32_t count3,
                 bool particle, bool proven)
{
    const bool check = s_o.arm >= 0;
    if (!check && s_o.state != O_ON) {
        return; /* held, off: nothing asked, nothing counted */
    }
    const uint32_t use = useNow();
    if (check && use == 0u) {
        return; /* the check's REF and B: every pair in two passes */
    }
    /* the in-play judge (the tests' forced state too: ICO_RD_AB=off puts
       every replay at its off arm, rd_onepass_ab) */
    const bool judged = !check;
    for (int i = 0; i + 1 < *np; i++) {
        if (!rd__no_dual_second(dp, i + 1)) {
            continue;
        }
        int why = rd_op_why(&dp[i], &dp[i + 1], hi, g_rd.passSamples);
        const uint32_t cls = rd_op_class_of(vuBits, s_o.cmdFused);
        if (why == RD_OP_ONE && cls == 0u) {
            why = RD_OP_FUSED;
        } else if (why == RD_OP_ONE && (use & cls) == 0u) {
            why = RD_OP_CLASS;
        } else if (why == RD_OP_ONE && cls == RD_OP_PLAIN &&
                   !rd_op_safe_draw(vuBits, particle, proven)) {
            why = RD_OP_EYE; /* the plain class, in play and in arm A alike */
        }
        const RdPipeKeyInt t = onepassKey(&dp[i].key);
        if (why == RD_OP_ONE && !madeOrAsked(&t)) {
            why = RD_OP_UNMADE;
        } else if (why == RD_OP_ONE && judged && rd__ab_off(RD_AB_ONEPASS)) {
            why = RD_OP_TRIAL; /* the in-play judge's off arm: two passes */
        }
        if (judged) {
            /* the judge times a replay only when every pair it would take
               had its pipeline (one not made yet takes the replay out) */
            rd_op_use_note(&s_o.use, why);
            if (rd_op_use_counts(&s_o.use)) {
                rd__ab_used(RD_AB_ONEPASS);
            } else if (why == RD_OP_UNMADE) {
                rd__ab_unused(RD_AB_ONEPASS);
                rd__ab_unmade();
            }
        }
        count(why, cls, count3 / 3u);
        if (why != RD_OP_ONE) {
            continue;
        }
        /* the colour pass carries A (the constant at bind, rd__op_bind), the
           alpha pass dropped: the passes after it moved down */
        dp[i].key = t;
        for (int j = i + 1; j + 1 < *np; j++) {
            dp[j] = dp[j + 1];
        }
        (*np)--;
        if (!check) {
            s_o.drewNow = true;
        }
    }
}

bool rd__op_unfuse(const RdDrawPass *cut, int nc, const RdDrawPass *kick, int nk, uint8_t hi)
{
    const bool check = s_o.arm >= 0;
    if (check ? (s_o.arm != RD_OP_ARM_B && s_o.arm != RD_OP_ARM_C)
              : (s_o.state != O_ON || (s_o.classes & RD_OP_CUT) == 0u)) {
        return false;
    }
    /* the cut's pair as one pass would take it, the kick one pass of its
       own (blending off) */
    if (nc != 2 || nk != 1 || !rd__no_dual_second(cut, 1) ||
        rd_op_why(&cut[0], &cut[1], hi, g_rd.passSamples) != RD_OP_ONE) {
        return false;
    }
    if (!check && rd__ab_off(RD_AB_ONEPASS)) {
        /* the judge's off arm: fused as today, the replay noted as one the
           rule would have changed (the on arm's unfused cut is noted by its
           take) */
        rd_op_use_note(&s_o.use, RD_OP_TRIAL);
        if (rd_op_use_counts(&s_o.use)) {
            rd__ab_used(RD_AB_ONEPASS);
        }
        return false;
    }
    /* both pipelines made (never made in the draw: a phone's compile is a
       second or more); B keeps the cut's two passes, its keys the fused
       ones' */
    const RdPipeKeyInt t = onepassKey(&cut[0].key);
    const bool cutMade = check && s_o.arm == RD_OP_ARM_B ? true : madeOrAsked(&t);
    const bool kickMade = cutMade && madeOrAsked(&kick[0].key);
    if (!cutMade || !kickMade) {
        if (check) {
            s_o.chkUnmade++;
        } else {
            add(&s_block.refused[RD_OP_UNMADE], &s_run.refused[RD_OP_UNMADE], 1u);
            rd__ab_unmade();
        }
        return false;
    }
    if (check) {
        s_o.chkUnfused++;
    } else {
        add(&s_block.unfused, &s_run.unfused, 1u);
    }
    return true;
}

void rd__op_cut_fused(bool fused)
{
    s_o.cmdFused = fused;
}

void rd__op_bind(RhiCommandList cl, const RdPipeKeyInt *k)
{
    if (k == NULL || (k->gs.nodual & RD_NODUAL_ONEPASS) == 0u || (k->gs.colorMask & 8u) == 0u) {
        return;
    }
    /* the alpha factor's constant (RHI_BF_CONSTANT reads the constant's
       alpha for the alpha channel); the check's control one lower */
    const float a = s_o.sabNow ? 127.0f / 255.0f : 128.0f / 255.0f;
    const float c[4] = {a, a, a, a};
    rhi_cmd_set_blend_constant(cl, c);
}

void rd__op_fault(const char *what)
{
    if (s_o.state == O_OFF) {
        return;
    }
    char why[384];
    snprintf(why, sizeof(why), "for the run: %.360s", what);
    setRunOff(why);
    rd__log("one-pass blend: off %s (the mark is not changed: a background compile that failed "
            "once is this run's; the next start checks it again)",
            s_o.why);
}

void rd__op_replay_begin(void)
{
    memset(&s_o.use, 0, sizeof(s_o.use));
    s_o.cmdFused = false;
    if (s_o.drewNow) {
        s_o.drewNow = false;
        if (s_o.state == O_ON && s_o.forced == 0u && !s_o.markedOn &&
            ++s_o.pictures >= RD_OP_PROBATION) {
            s_o.markedOn = true;
            (void)writeMark("on", false);
            rd__log("one-pass blend: %u pictures drawn with it since its check: its mark says on",
                    (unsigned)RD_OP_PROBATION);
        }
    }
}

/* ------------------------------------------------------ the check's part */

void rd__op_check_arm(int arm, bool sabotage)
{
    s_o.arm = arm >= 0 && arm < RD_OP_ARMS ? arm : -1;
    s_o.sabNow = s_o.arm >= 0 && sabotage;
}

void rd__op_check_take(uint64_t one[2], uint64_t *unfused, uint64_t *unmade)
{
    one[0] = s_o.chkOne[0];
    one[1] = s_o.chkOne[1];
    *unfused = s_o.chkUnfused;
    *unmade = s_o.chkUnmade;
    memset(s_o.chkOne, 0, sizeof(s_o.chkOne));
    s_o.chkUnfused = s_o.chkUnmade = 0;
}

bool rd__op_try_begin(void)
{
    if (s_o.state == O_OFF || !g_rd.hasDevice) {
        return false;
    }
    /* the mark first: a crash or a hang inside the check leaves it for the
       next start */
    if (!s_o.tried && !writeMark("trying", true)) {
        return false;
    }
    s_o.tried = true;
    return true;
}

void rd__op_verdict(uint32_t classes, bool measured, const char *why)
{
    if (s_o.state == O_OFF && s_o.tried && s_o.markPath[0] != '\0') {
        /* turned off during the check (a background fault): no verdict of
           the check's, never "trying" left for a later end to be blamed on */
        (void)ico_remove(s_o.markPath);
    }
    if (s_o.state == O_OFF || s_o.forced) {
        return;
    }
    classes &= RD_OP_PLAIN | RD_OP_CUT;
    if (classes != 0u) {
        s_o.state = O_ON;
        s_o.classes = classes;
        s_o.wasOn = true;
        s_o.why[0] = '\0';
        s_o.pictures = 0;
        if (s_o.tried) {
            (void)writeMark("counting", true);
        }
    } else {
        char w[300];
        snprintf(w, sizeof(w), "for the run (its check: %.250s)", why);
        setRunOff(w);
        if (s_o.tried && measured) {
            (void)writeMark("failed", true);
        } else if (s_o.tried && s_o.markPath[0] != '\0') {
            /* no verdict: not tried as far as the next start goes */
            (void)ico_remove(s_o.markPath);
        }
    }
}

void rd__op_selftest_skip(const char *why)
{
    if (s_o.state == O_HELD) {
        char w[300];
        snprintf(w, sizeof(w), "for the run (check not run: %.250s)", why);
        setOff(w); /* not a fault: a renderer made again checks it */
        rd__log("one-pass blend: off %s", s_o.why);
    }
    rd__op_start_line();
}

void rd__op_start_line(void)
{
    if (s_o.state == O_ON) {
        rd__log("one-pass blend: on (%s%s%s), %u of its pipelines alive",
                rd_op_classes_name(s_o.classes), s_o.forced ? ", " : "", s_o.forced ? s_o.why : "",
                (unsigned)rd__pipeline_count_onepass());
    } else {
        rd__log("one-pass blend: %s (%s), %u of its pipelines alive", rd__op_state_text(), s_o.why,
                (unsigned)rd__pipeline_count_onepass());
    }
}

/* -------------------------------------------------------------- the lines */

static uint64_t takeN(uint64_t *v)
{
    return __atomic_exchange_n(v, 0u, __ATOMIC_RELAXED);
}

size_t rd_op_text(char *out, size_t cap, unsigned n)
{
    if (cap == 0) {
        return 0;
    }
    OCounts c;
    c.pairs = takeN(&s_block.pairs);
    c.one = takeN(&s_block.one);
    c.oneCut = takeN(&s_block.oneCut);
    c.tris = takeN(&s_block.tris);
    c.unfused = takeN(&s_block.unfused);
    uint64_t refused = 0;
    for (int i = 0; i < RD_OP_REASONS; i++) {
        c.refused[i] = takeN(&s_block.refused[i]);
        refused += i > 0 ? c.refused[i] : 0u;
    }
    out[0] = '\0';
    if (s_o.state != O_ON) {
        if (!s_o.wasOn) {
            return 0;
        }
        const int k = snprintf(out, cap, "; one-pass blend %s (%s)", rd__op_state_text(), s_o.why);
        return k < 0 ? 0u : ((size_t)k < cap ? (size_t)k : cap - 1u);
    }
    const double d = n ? (double)n : 1.0;
    int at = snprintf(out, cap,
                      "; one-pass blend (%s): %.0f pairs a picture in one pass (%.0f of code 36), "
                      "%.0f triangles, %.0f code 36 commands unfused (%.0f pairs kept two-pass a "
                      "picture:",
                      s_o.classes == (RD_OP_PLAIN | RD_OP_CUT) ? "both classes"
                      : s_o.classes == RD_OP_PLAIN             ? "outside code 36"
                                                               : "code 36",
                      (double)c.one / d, (double)c.oneCut / d, (double)c.tris / d,
                      (double)c.unfused / d, (double)refused / d);
    for (int i = 1; i < RD_OP_REASONS && at > 0 && (size_t)at < cap; i++) {
        at += snprintf(out + at, cap - (size_t)at, "%s %s %.0f", i > 1 ? "," : "",
                       rd_op_reason_name(i), (double)c.refused[i] / d);
    }
    if (at > 0 && (size_t)at < cap) {
        at += snprintf(out + at, cap - (size_t)at, ")");
    }
    return at < 0 ? 0u : ((size_t)at < cap ? (size_t)at : cap - 1u);
}

int rd__op_stats_text(char *out, size_t cap)
{
    const OCounts *c = &s_run;
    char refused[320];
    int at = 0;
    refused[0] = '\0';
    for (int i = 1; i < RD_OP_REASONS && at >= 0 && (size_t)at < sizeof(refused); i++) {
        at += snprintf(refused + at, sizeof(refused) - (size_t)at, "%s%s %llu", i > 1 ? ", " : "",
                       rd_op_reason_name(i), (unsigned long long)c->refused[i]);
    }
    const char *na = rd__op_not_applicable_text();
    return snprintf(
        out, cap,
        "one-pass blend %s (%s): %llu pairs in one pass of %llu (%llu of code 36), "
        "%llu triangles, %llu code 36 commands unfused; kept two-pass: %s; %u of its "
        "pipelines alive%s%s%s",
        rd__op_state_text(), s_o.state == O_ON ? rd_op_classes_name(s_o.classes) : s_o.why,
        (unsigned long long)c->one, (unsigned long long)c->pairs, (unsigned long long)c->oneCut,
        (unsigned long long)c->tris, (unsigned long long)c->unfused, refused,
        (unsigned)rd__pipeline_count_onepass(), na ? " (" : "", na ? na : "", na ? ")" : "");
}
