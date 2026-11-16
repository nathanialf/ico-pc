/* rd_shadow_count.c: the shadow count at its full size above 1x, which of
 * its three forms a device draws (rd_shadow_count.h; issue 123, v0.11.8;
 * docs/port/RENDER_POST.md "The count from the stencil").
 *
 * The forms give the first level's step the same bytes (rd_shadowres.c,
 * rd_shadow_one.c say why), so the choice is cost alone, measured on the
 * device: at the first shadowed scene above 1x each presented replay the
 * perf lines time pass by pass (rd_perf.c: one replay every
 * RD_PERF_PASS_EVERY_MS; the self-tests' frames are never presented, nor
 * is rd_replay_tool's --pass-dump) takes the next form of the trial (rd_sc_trial_next: the stencil step, the
 * seven draws, the full-size pass in turn) and, when its pieces come back
 * (rd__shadow_count_trial), its list 3 GPU time is that form's sample
 * (rd_sc_list_ms: a piece without its two stamps leaves the sample out and
 * is counted).  The replays between them take the stencil step.  With
 * RD_SC_TRIAL_N samples of each the lowest median is kept for the run
 * (rd_sc_choose) and the line names the three.  Where the device has no
 * GPU timestamps (or no pass timing: iOS) the stencil step is kept
 * untimed; where the trial cannot finish (RD_SC_TRIAL_MAX timed replays
 * scheduled) likewise, said.  Every form's own picture is the same, so the
 * replays of the trial show nothing of it.
 *
 * The lines (rd__log):
 *   shadow count: above 1x the first level reads the full-size count from
 *     the stencil where nothing touched it, else the full-size pass; each
 *     device times the stencil step, the full-size pass and the seven draws
 *     at its first shadowed scene and keeps the cheapest (the same bytes)
 *   shadow count: above 1x the stencil step, timed at the first shadowed
 *     scene (list 3 a replay, the median of 3 timed replays each): the
 *     stencil step 4.12 ms, the full-size pass 9.80 ms, the seven draws
 *     7.33 ms (replay 412; the same bytes in each form)
 *   shadow count: above 1x the stencil step, untimed: no GPU timestamps on
 *     this device
 *   shadow count: the full-size pass drawn after all: the depth was touched
 *     before the level's step (replay 88; reported once a reason)
 * The 10 s clause's part (rd_shadowres_math.c rd__shadow_fine_text) and
 * --stats' line (rd__shadow_count_stats) count the forms a block drew. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_internal.h"
#include "rd_shadow_count.h"

/* ------------------------------------------------------------- the words */

const char *rd_sc_form_word(int form)
{
    switch (form) {
    case RD_SC_STENCIL:
        return "the stencil step";
    case RD_SC_FULL:
        return "the full-size pass";
    case RD_SC_SEVEN:
        return "the seven draws";
    default:
        return "none";
    }
}

const char *rd_sc_form_name(int form)
{
    switch (form) {
    case RD_SC_STENCIL:
        return "stencil";
    case RD_SC_FULL:
        return "full";
    case RD_SC_SEVEN:
        return "seven";
    default:
        return "none";
    }
}

const char *rd_sc_why_text(int why)
{
    switch (why) {
    case RD_SC_WHY_SIZE:
        return "the depth's texels are not the count's";
    case RD_SC_WHY_DEPTH:
        return "the depth was touched before the level's step";
    case RD_SC_WHY_COUNT:
        return "the count target was touched before the level's step";
    case RD_SC_WHY_LIST:
        return "the depth or the count touched in another command list (the shadow drawn at the "
               "PS2's size)";
    case RD_SC_WHY_PIPE:
        return "the stencil step's shader or pipeline could not be made";
    case RD_SC_WHY_AGAIN:
        return "a second first-level step of the chain after the depth or the count was touched "
               "(the shadow drawn at the PS2's size)";
    default:
        return "?";
    }
}

/* ------------------------------------------------------------- the pure */

float rd_sc_median(const float *v, int n)
{
    float s[RD_SC_TRIAL_N];
    if (n < 1) {
        return 0.0f;
    }
    if (n > RD_SC_TRIAL_N) {
        n = RD_SC_TRIAL_N;
    }
    for (int i = 0; i < n; i++) {
        s[i] = v[i];
    }
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && s[j] < s[j - 1]; j--) {
            const float t = s[j];
            s[j] = s[j - 1];
            s[j - 1] = t;
        }
    }
    return s[(n - 1) / 2];
}

int rd_sc_choose(const float med[RD_SC_FORMS], const bool have[RD_SC_FORMS])
{
    /* the preference between equal medians */
    static const int k_pref[RD_SC_FORMS] = {RD_SC_STENCIL, RD_SC_FULL, RD_SC_SEVEN};
    int best = -1;
    for (int i = 0; i < RD_SC_FORMS; i++) {
        const int f = k_pref[i];
        if (have[f] && (best < 0 || med[f] < med[best])) {
            best = f;
        }
    }
    return best;
}

int rd_sc_trial_next(uint32_t k, const uint8_t got[RD_SC_FORMS])
{
    static const int k_turn[RD_SC_FORMS] = {RD_SC_STENCIL, RD_SC_SEVEN, RD_SC_FULL};
    for (uint32_t i = 0; i < RD_SC_FORMS; i++) {
        const int f = k_turn[(k + i) % RD_SC_FORMS];
        if (got[f] < RD_SC_TRIAL_N) {
            return f;
        }
    }
    return -1;
}

double rd_sc_list_ms(const RdPerfPass *p, uint32_t n, int list, bool *written)
{
    double us = 0.0;
    uint32_t pieces = 0;
    bool ok = true;
    for (uint32_t i = 0; p && i < n; i++) {
        if (p[i].list != list) {
            continue;
        }
        pieces++;
        if (p[i].gpuUs < 0.0f) {
            ok = false;
            continue;
        }
        us += (double)p[i].gpuUs;
    }
    *written = ok && pieces > 0;
    return us / 1000.0;
}

/* ------------------------------------------------------------- the trial */

/* a trial replay waiting for its pieces: the replay (RdPerfRecord.replay)
 * and its form; dropped when the guard drew the pass in it (not the
 * stencil form's cost) */
typedef struct ScWait {
    uint32_t replay;
    int8_t form;
    uint8_t dropped;
} ScWait;

#define SC_WAITS 16

static struct {
    int env;    /* ICO_RD_SHADOW_COUNT_FORM's form, -1 none, -2 not read */
    int chosen; /* the run's form, -1 not chosen */
    float ms[RD_SC_FORMS][RD_SC_TRIAL_N];
    uint8_t got[RD_SC_FORMS];
    uint32_t turn;      /* the trial's timed replays scheduled */
    uint32_t unwritten; /* trial replays whose list 3 pieces came back unstamped */
    ScWait wait[SC_WAITS];
    uint32_t lastReplay; /* the replay of the last call (a second count takes its form) */
    int lastForm;
    uint32_t saidWhy; /* a bit a reason logged */
    bool keep;        /* rd__shadow_count_set_keep */
} s_sc = {-2, -1, {{0}}, {0}, 0, 0, {{0}}, 0, 0, 0, false};

static int envForm(void)
{
    if (s_sc.env == -2) {
        const char *e = getenv("ICO_RD_SHADOW_COUNT_FORM");
        s_sc.env = -1;
        for (int f = 0; e && f < RD_SC_FORMS; f++) {
            if (strcmp(e, rd_sc_form_name(f)) == 0) {
                s_sc.env = f;
            }
        }
    }
    return s_sc.env;
}

static bool timed(void)
{
#ifdef ICO_IOS
    return false; /* no pass timing on iOS (rd_perf.c) */
#else
    return g_rd.hasDevice && rhi_timestamps_supported();
#endif
}

static void decide(int form, const char *how)
{
    s_sc.chosen = form;
    if (form == RD_SC_STENCIL) {
        ico_attempt_state(ICO_ATT_SHADOW_COUNT, ICO_ATT_ON,
                          "above 1x the levels' step reads "
                          "the stencil: the device's cheapest");
    } else {
        ico_attempt_state(ICO_ATT_SHADOW_COUNT, ICO_ATT_REFUSED,
                          form == RD_SC_FULL ? "the device timed the full-size pass cheaper"
                                             : "the device timed the seven draws cheaper");
    }
    rd__log("shadow count: above 1x %s, %s", rd_sc_form_word(form), how);
}

/* the trial's figures in, the cheapest kept */
static void finish(void)
{
    float med[RD_SC_FORMS];
    bool have[RD_SC_FORMS];
    for (int f = 0; f < RD_SC_FORMS; f++) {
        have[f] = s_sc.got[f] >= RD_SC_TRIAL_N;
        med[f] = have[f] ? rd_sc_median(s_sc.ms[f], RD_SC_TRIAL_N) : 0.0f;
    }
    const int best = rd_sc_choose(med, have);
    char how[320];
    snprintf(how, sizeof how,
             "timed at the first shadowed scene (list 3 a replay, the median of %d timed replays "
             "each): the stencil step %.2f ms, the full-size pass %.2f ms, the seven draws %.2f ms "
             "(replay %u; %u timed replays' stamps unwritten; the same bytes in each form)",
             RD_SC_TRIAL_N, (double)med[RD_SC_STENCIL], (double)med[RD_SC_FULL],
             (double)med[RD_SC_SEVEN], (unsigned)g_rd.replayCounter, (unsigned)s_sc.unwritten);
    decide(best >= 0 ? best : RD_SC_STENCIL, how);
}

int rd__shadow_count_form(void)
{
    const uint32_t replay = g_rdPerf.replay;
    if (replay != 0u && replay == s_sc.lastReplay) {
        return s_sc.lastForm; /* a second count in the replay: the first's form */
    }
    int form = RD_SC_STENCIL;
    if (envForm() >= 0) {
        form = s_sc.env;
    } else if (s_sc.chosen >= 0) {
        form = s_sc.chosen;
    } else if (!timed()) {
        decide(RD_SC_STENCIL,
               g_rd.hasDevice ? "untimed: no GPU timestamps on this device" : "untimed: no device");
    } else if (s_sc.turn >= RD_SC_TRIAL_MAX) {
        char how[160];
        snprintf(how, sizeof how,
                 "untimed: the trial's %u timed replays brought too few figures back (%u with "
                 "stamps unwritten)",
                 (unsigned)s_sc.turn, (unsigned)s_sc.unwritten);
        decide(RD_SC_STENCIL, how);
    } else if (g_rdPerf.presented && rd__perf_pass_on()) {
        /* a presented replay (the game's pictures: a self-test's frame of
         * its own is never presented) timed pass by pass */
        const int f = rd_sc_trial_next(s_sc.turn, s_sc.got);
        if (f >= 0) {
            form = f;
            s_sc.wait[s_sc.turn % SC_WAITS] = (ScWait){replay, (int8_t)f, 0};
            s_sc.turn++;
        }
    }
    s_sc.lastReplay = replay;
    s_sc.lastForm = form;
    g_rdPerf.shadowCountForm = (uint8_t)(form + 1);
    return form;
}

void rd__shadow_count_guard(int why)
{
    if (g_rdPerf.shadowCountGuard < 0xFFFFu) {
        g_rdPerf.shadowCountGuard++;
    }
    ico_attempt_fire2(ICO_ATT_SHADOW_COUNT, 1);
    for (uint32_t i = 0; i < SC_WAITS; i++) {
        if (s_sc.wait[i].replay == g_rdPerf.replay && s_sc.wait[i].replay != 0u) {
            s_sc.wait[i].dropped = 1; /* not the stencil form's cost */
        }
    }
    if (why >= 0 && why < 32 && !(s_sc.saidWhy & (1u << why))) {
        s_sc.saidWhy |= 1u << why;
        rd__log("shadow count: %s: %s (replay %u; reported once a reason)",
                why == RD_SC_WHY_LIST || why == RD_SC_WHY_AGAIN
                    ? "the stencil step not drawn"
                    : "the full-size pass drawn after all",
                rd_sc_why_text(why), (unsigned)g_rd.replayCounter);
    }
}

void rd__shadow_count_trial(uint32_t replay, const RdPerfPass *p, uint32_t n)
{
    if (s_sc.chosen >= 0 || replay == 0u) {
        return;
    }
    for (uint32_t i = 0; i < SC_WAITS; i++) {
        ScWait *w = &s_sc.wait[i];
        if (w->replay != replay) {
            continue;
        }
        const int f = w->form;
        const bool dropped = w->dropped != 0;
        memset(w, 0, sizeof(*w));
        if (dropped || f < 0 || f >= RD_SC_FORMS || s_sc.got[f] >= RD_SC_TRIAL_N) {
            return;
        }
        bool written = false;
        const double ms = rd_sc_list_ms(p, n, 3, &written);
        if (!written) {
            s_sc.unwritten++;
            return;
        }
        s_sc.ms[f][s_sc.got[f]++] = (float)ms;
        if (rd_sc_trial_next(0, s_sc.got) < 0) {
            finish();
        }
        return;
    }
}

void rd__shadow_count_log(void)
{
    const RhiLimits *lim = g_rd.hasDevice ? rhi_limits() : NULL;
    const bool view = lim && lim->stencilView && g_rd.profile.shadowOne;
    ico_attempt_decide_shadow_count(view ? 1 : 0);
    if (!view) {
        rd__log("shadow count: above 1x the full-size count from the seven draws (no stencil view "
                "or no count in one draw here)");
        return;
    }
    if (envForm() >= 0) {
        rd__log("shadow count: above 1x %s (ICO_RD_SHADOW_COUNT_FORM=%s, the tests' switch)",
                rd_sc_form_word(s_sc.env), rd_sc_form_name(s_sc.env));
        return;
    }
    rd__log("shadow count: above 1x the first level reads the full-size count from the stencil "
            "where nothing touched it, else the full-size pass; the device times the stencil "
            "step, the full-size pass and the seven draws at its first shadowed scene (%d timed "
            "replays each) and keeps the cheapest (the same bytes)",
            RD_SC_TRIAL_N);
}

void rd__shadow_count_reset(void)
{
    const bool keep = s_sc.keep;
    memset(&s_sc, 0, sizeof(s_sc));
    s_sc.env = -2;
    s_sc.chosen = -1;
    s_sc.keep = keep;
}

void rd__shadow_count_set_keep(bool on)
{
    s_sc.keep = on;
}

bool rd__shadow_count_keep(void)
{
    return s_sc.keep;
}

bool rd__shadow_count_touch_test(void)
{
    static int s_on = -1;
    if (s_on < 0) {
        const char *e = getenv("ICO_RD_SHADOW_COUNT_TOUCH");
        s_on = e && strcmp(e, "1") == 0;
    }
    return s_on == 1;
}

void rd__shadow_count_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    const int form = (int)rec->shadowCountForm - 1;
    snprintf(out, size,
             "shadow count above 1x: %u from the stencil, %u full-size passes, %u seven draws, %u "
             "guard fallbacks (form %s)",
             rec->shadowCountStencil, rec->shadowCountFull, rec->shadowCountSeven,
             rec->shadowCountGuard, rd_sc_form_name(form));
}
