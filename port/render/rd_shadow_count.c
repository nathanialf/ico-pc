/* rd_shadow_count.c: the shadow count at its full size above 1x, which of
 * its three forms a device draws (rd_shadow_count.h; issue 123, v0.11.8;
 * docs/port/RENDER_POST.md "The count from the stencil").
 *
 * The forms give the first level's step the same bytes by the code
 * (rd_shadowres.c, rd_shadow_one.c say why); the device's compile of the
 * stencil step and its seven draws are checked once a run before either
 * is drawn for a picture.  Until then every count above 1x takes the
 * full-size pass (v0.11.7's form).  The check: the first presented replay
 * with a count above 1x (a self-test's frame of its own is never
 * presented, nor is rd_replay_tool's --pass-dump) draws its picture with
 * the full-size pass and, besides, the first level again from the stencil
 * and from the seven draws' full-size count into scratch textures
 * (rd_shadowres.c checkDraw); the three levels are copied into one
 * readback buffer in that replay's list and compared byte for byte when
 * its frame slot is recycled (rd__shadow_count_collect, from rd_perf.c
 * after the slot's own fence wait: no wait of its own).  At the tablet's
 * 3x, SHADOW0's level is 864x768: 3 x 2.6 MB read back once, with 2 x 2.6
 * MB of scratch levels and the 1728x1344 seven-draw count (8.9 MB) made for
 * that replay and freed at the compare.  The stencil step differing keeps
 * the full-size pass for the run (logged with the differing texels, the
 * ledger's row refused); the seven draws differing leave them out of the
 * trial.
 *
 * Then the trial, cost alone: each presented replay the perf lines time
 * pass by pass (rd_perf.c: one every RD_PERF_PASS_EVERY_MS) takes the next
 * form (rd_sc_trial_next: the stencil step, the seven draws, the full-size
 * pass in turn: neighbouring samples of the forms 250 ms apart, the same
 * scene), and, when its pieces come back (rd__shadow_count_trial), its
 * list 3 GPU time is that form's sample (rd_sc_list_ms: a piece without
 * its two stamps leaves the sample out and is counted).  The replays
 * between take the stencil step.  With RD_SC_TRIAL_N samples of each
 * rd_sc_choose keeps the stencil step unless another form's median is
 * lower by more than the margin: the larger of RD_SC_MARGIN_SHARE of the
 * stencil step's median and the widest spread of any form's samples (a
 * difference inside the samples' own noise never flips the choice; the
 * forms differ by a whole pass, which the issue 123 log put at 3.6-11.4 ms
 * against list 3's 8.6-17.3 ms, so a real difference clears the margin).
 * Where the device has no GPU timestamps (or no pass timing: iOS) the
 * stencil step is kept untimed; where the trial cannot finish
 * (RD_SC_TRIAL_MAX timed replays scheduled) likewise, said.  Every form's
 * picture is the same bytes, so a player sees nothing of the check or the
 * trial; their cost is the full-size pass's (what v0.11.7 drew in every
 * picture) on the replays before the check passes and on the trial's 3
 * full-size and 3 seven-draw replays, and on the check replay its extra
 * passes (a level step from the stencil, the seven draws at the full size,
 * a level step from them, three copies).
 *
 * The lines (rd__log):
 *   shadow count: above 1x the full-size pass until the check at the first
 *     shadowed scene ... passes; then the device times ...
 *   shadow count: the check at the first shadowed scene: the first level
 *     from the stencil equal to the full-size pass's at all 663552 texels
 *     of 864x768; the seven draws' equal too (K KB read back at its
 *     slot's recycle, no wait); the forms timed next
 *   shadow count: the check at the first shadowed scene: the first level
 *     from the stencil differs from the full-size pass's at N of M texels
 *     of WxH, the first (X,Y): ... ; above 1x the full-size pass for this run
 *   shadow count: above 1x the stencil step, timed at the first shadowed
 *     scene (list 3 a replay, the median of 3 timed replays each, taken in
 *     turn): the stencil step 4.12 ms, the full-size pass 9.80 ms, the
 *     seven draws 7.33 ms; another form only below the stencil step's by
 *     more than 0.62 ms (...) (replay 412; ...)
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

int rd_sc_choose(const float ms[RD_SC_FORMS][RD_SC_TRIAL_N], const uint8_t n[RD_SC_FORMS],
                 float med[RD_SC_FORMS], float *margin)
{
    float spread = 0.0f;
    for (int f = 0; f < RD_SC_FORMS; f++) {
        med[f] = 0.0f;
        if (n[f] < RD_SC_TRIAL_N) {
            continue;
        }
        med[f] = rd_sc_median(ms[f], RD_SC_TRIAL_N);
        float lo = ms[f][0], hi = ms[f][0];
        for (int i = 1; i < RD_SC_TRIAL_N; i++) {
            lo = ms[f][i] < lo ? ms[f][i] : lo;
            hi = ms[f][i] > hi ? ms[f][i] : hi;
        }
        spread = hi - lo > spread ? hi - lo : spread;
    }
    *margin = 0.0f;
    if (n[RD_SC_STENCIL] < RD_SC_TRIAL_N) {
        return -1;
    }
    const float floorMs = RD_SC_MARGIN_SHARE * med[RD_SC_STENCIL];
    *margin = spread > floorMs ? spread : floorMs;
    /* another form only where its median is under the stencil step's by
     * more than the margin; of two, the lower (equal: the full-size pass) */
    static const int k_alt[2] = {RD_SC_FULL, RD_SC_SEVEN};
    int best = RD_SC_STENCIL;
    for (int i = 0; i < 2; i++) {
        const int f = k_alt[i];
        if (n[f] >= RD_SC_TRIAL_N && med[f] < med[RD_SC_STENCIL] - *margin &&
            (best == RD_SC_STENCIL || med[f] < med[best])) {
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

/* the check (rd_shadow_count.h): its state, the replay that draws it, the
 * slot and replay counter it was recorded in, its readback and textures */
enum { CHK_NONE = 0, CHK_ARMED, CHK_RECORDED, CHK_PASSED, CHK_FAILED };

static struct {
    int state;
    uint32_t replay;  /* RdPerfRecord.replay of the check replay (CHK_ARMED) */
    uint32_t counter; /* g_rd.replayCounter at the record */
    uint32_t slot;    /* rhi_frame_slot() at the record */
    uint32_t tries;   /* check replays that could not draw it */
    RhiBuffer rb;
    RhiTexture tex[RD_SC_CHECK_TEXTURES];
    uint32_t w, h, pitch;
    bool sevenBad; /* the seven draws' level differed: left out of the trial */
} s_chk;

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

/* the trial's figures in, the form kept (rd_sc_choose) */
static void finish(void)
{
    float med[RD_SC_FORMS], margin = 0.0f;
    uint8_t n[RD_SC_FORMS];
    for (int f = 0; f < RD_SC_FORMS; f++) {
        n[f] = s_sc.got[f];
    }
    if (s_chk.sevenBad) {
        n[RD_SC_SEVEN] = 0; /* its samples were never taken */
    }
    const int best = rd_sc_choose(s_sc.ms, n, med, &margin);
    char seven[64];
    if (s_chk.sevenBad) {
        snprintf(seven, sizeof seven, "left out (the check)");
    } else {
        snprintf(seven, sizeof seven, "%.2f ms", (double)med[RD_SC_SEVEN]);
    }
    char how[1536];
    snprintf(
        how, sizeof how,
        "timed at the first shadowed scene (list 3 a replay, the median of %d timed replays "
        "each, taken in turn): the stencil step %.2f ms, the full-size pass %.2f ms, the seven "
        "draws %s; another form only below the stencil step's by more than %.2f ms (the "
        "larger of %.0f %% of it and the samples' widest spread) (replay %u; %u timed "
        "replays' stamps unwritten; the same bytes in each form)",
        RD_SC_TRIAL_N, (double)med[RD_SC_STENCIL], (double)med[RD_SC_FULL], seven, (double)margin,
        (double)(RD_SC_MARGIN_SHARE * 100.0f), (unsigned)g_rd.replayCounter,
        (unsigned)s_sc.unwritten);
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
    } else if (s_chk.state != CHK_PASSED) {
        /* the full-size pass until the check passed (for the run where it
         * failed); the first presented replay draws the check besides */
        form = RD_SC_FULL;
        if (s_chk.state == CHK_NONE && g_rdPerf.presented && g_rd.hasDevice) {
            s_chk.state = CHK_ARMED;
            s_chk.replay = replay;
        }
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

/* ------------------------------------------------------------- the check */

bool rd__shadow_count_check_now(void)
{
    return s_chk.state == CHK_ARMED && s_chk.replay == g_rdPerf.replay && g_rdPerf.replay != 0u;
}

static void checkFreeObjects(void)
{
    if (g_rd.hasDevice) {
        for (int i = 0; i < RD_SC_CHECK_TEXTURES; i++) {
            if (s_chk.tex[i].id) {
                rhi_destroy_texture(s_chk.tex[i]);
            }
        }
        if (s_chk.rb.id) {
            rhi_destroy_buffer(s_chk.rb);
        }
    }
    memset(s_chk.tex, 0, sizeof(s_chk.tex));
    s_chk.rb = (RhiBuffer){0};
}

static void checkFail(const char *what)
{
    s_chk.state = CHK_FAILED;
    ico_attempt_state(ICO_ATT_SHADOW_COUNT, ICO_ATT_REFUSED,
                      "the check at the first shadowed scene refused the step from the stencil: "
                      "the full-size pass");
    rd__log("shadow count: the check at the first shadowed scene: %s; above 1x the full-size "
            "pass for this run",
            what);
}

void rd__shadow_count_check_lost(const char *why, bool counted)
{
    if (s_chk.state != CHK_ARMED) {
        return;
    }
    s_chk.state = CHK_NONE; /* the next presented replay above 1x draws it */
    if (!counted) {
        return; /* a picture with no first level step (no volumes): not a try */
    }
    s_chk.tries++;
    if (s_chk.tries >= RD_SC_CHECK_TRIES) {
        char what[192];
        snprintf(what, sizeof what, "not drawn in %u shadowed pictures (the last: %s)",
                 (unsigned)s_chk.tries, why);
        checkFail(what);
    }
}

void rd__shadow_count_check_refuse(const char *why)
{
    if (s_chk.state == CHK_ARMED) {
        checkFail(why);
    }
}

void rd__shadow_count_check_recorded(RhiBuffer rb, const RhiTexture tex[RD_SC_CHECK_TEXTURES],
                                     uint32_t w, uint32_t h, uint32_t pitch)
{
    s_chk.state = CHK_RECORDED;
    s_chk.rb = rb;
    for (int i = 0; i < RD_SC_CHECK_TEXTURES; i++) {
        s_chk.tex[i] = tex[i];
    }
    s_chk.w = w;
    s_chk.h = h;
    s_chk.pitch = pitch;
    s_chk.slot = rhi_frame_slot();
    s_chk.counter = g_rd.replayCounter;
}

uint32_t rd_sc_check_differ(const uint8_t *ref, const uint8_t *got, uint32_t w, uint32_t h,
                            uint32_t pitch, uint32_t *firstX, uint32_t *firstY)
{
    uint32_t n = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const size_t o = (size_t)y * pitch + (size_t)x * 4u;
            if (memcmp(ref + o, got + o, 4) != 0) {
                if (n == 0u) {
                    *firstX = x;
                    *firstY = y;
                }
                n++;
            }
        }
    }
    return n;
}

void rd__shadow_count_collect(uint32_t slot)
{
    if (s_chk.state != CHK_RECORDED || slot != s_chk.slot || g_rd.replayCounter == s_chk.counter) {
        return;
    }
    const uint8_t *map = s_chk.rb.id ? rhi_map_buffer(s_chk.rb) : NULL;
    if (!map) {
        checkFreeObjects();
        checkFail("its readback could not be mapped");
        return;
    }
    const size_t region = (size_t)s_chk.pitch * s_chk.h;
    const uint8_t *full = map, *stencil = map + region, *seven = map + 2u * region;
    uint32_t sx = 0, sy = 0, vx = 0, vy = 0;
    const uint32_t ds = rd_sc_check_differ(full, stencil, s_chk.w, s_chk.h, s_chk.pitch, &sx, &sy);
    const uint32_t dv = rd_sc_check_differ(full, seven, s_chk.w, s_chk.h, s_chk.pitch, &vx, &vy);
    const uint32_t all = s_chk.w * s_chk.h;
    char what[320];
    if (ds) {
        const uint8_t *a = full + (size_t)sy * s_chk.pitch + sx * 4u;
        const uint8_t *b = stencil + (size_t)sy * s_chk.pitch + sx * 4u;
        snprintf(what, sizeof what,
                 "the first level from the stencil differs from the full-size pass's at %u of %u "
                 "texels of %ux%u, the first (%u,%u): %02x%02x%02x%02x for %02x%02x%02x%02x (the "
                 "seven draws' at %u)",
                 (unsigned)ds, (unsigned)all, (unsigned)s_chk.w, (unsigned)s_chk.h, (unsigned)sx,
                 (unsigned)sy, b[0], b[1], b[2], b[3], a[0], a[1], a[2], a[3], (unsigned)dv);
        checkFreeObjects();
        checkFail(what);
        return;
    }
    s_chk.sevenBad = dv != 0u;
    if (s_chk.sevenBad) {
        s_sc.got[RD_SC_SEVEN] = RD_SC_TRIAL_N; /* not scheduled; left out of the choice */
    }
    rd__log("shadow count: the check at the first shadowed scene: the first level from the "
            "stencil equal to the full-size pass's at all %u texels of %ux%u; the seven draws' %s "
            "(%u KB read back at its slot's recycle, no wait); the forms timed next",
            (unsigned)all, (unsigned)s_chk.w, (unsigned)s_chk.h,
            dv ? "differs: left out of the trial" : "equal too", (unsigned)(3u * region / 1024u));
    if (dv) {
        rd__log("shadow count: the check: the seven draws' first level differs at %u of %u "
                "texels, the first (%u,%u)",
                (unsigned)dv, (unsigned)all, (unsigned)vx, (unsigned)vy);
    }
    checkFreeObjects();
    s_chk.state = CHK_PASSED;
}

bool rd__shadow_count_sabotaged(void)
{
    static int s_on = -1;
    if (s_on < 0) {
        const char *e = getenv("ICO_RD_SHADOW_COUNT_SABOTAGE");
        s_on = e && strcmp(e, "1") == 0;
    }
    return s_on == 1;
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
    rd__log("shadow count: above 1x the full-size pass until the check at the first shadowed "
            "scene (the first level from the stencil and from the seven draws compared with it "
            "byte for byte, read back at its frame slot's recycle) passes; then the device times "
            "the stencil step, the full-size pass and the seven draws (%d timed replays each, in "
            "turn) and keeps the stencil step unless another is cheaper by more than the larger "
            "of %.0f %% and the samples' spread (the same bytes)",
            RD_SC_TRIAL_N, (double)(RD_SC_MARGIN_SHARE * 100.0f));
}

void rd__shadow_count_reset(void)
{
    checkFreeObjects();
    memset(&s_chk, 0, sizeof(s_chk));
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
