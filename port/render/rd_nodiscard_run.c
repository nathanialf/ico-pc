/* rd_nodiscard_run.c: the discard-free twins in the renderer
 * (rd_nodiscard.h): their state for the run, the install's mark, the two
 * modules, a pass's choice, the counts and the lines.
 *
 * The state: off (ICO_RD_VU_NODISCARD=0, the loss ladder, the install's
 * mark, a failed self-test, a twin pipeline the background could not make),
 * held (until the start-up self-test passes: nothing of the twins exists on
 * the device, every pass draws through the *_nodual build) or on.
 * ICO_RD_VU_NODISCARD=1 (tests) starts on, the modules made at the first
 * pass that wants one.
 *
 *   rd: discard-free twins: off for this start (the loss ladder: ...)
 *   rd: discard-free twins: trying them: vu_ps_nodual_nd and
 *     vu_texa_ps_nodual_nd made (2 modules; their pipelines with the
 *     self-test's, then in the background as passes ask for them)
 *   rd: discard-free twins: off for the run: a twin's pipeline could not be
 *     made in the background (...)
 *   rd: discard-free twins: 250 pictures drawn with them since their
 *     self-test: their mark says on
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h> /* _commit */
#else
#include <unistd.h> /* fsync */
#endif

#include "rd_ab_run.h"
#include "rd_internal.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

enum { N_HELD, N_ON, N_OFF };

/* ICO_RD_VU_NODISCARD_SABOTAGE (tests) */
enum { NS_NONE, NS_ALPHA, NS_MODULE, NS_CLIPPER, NS_EMPTY };

static struct {
    int state;
    bool forced; /* ICO_RD_VU_NODISCARD=1 */
    bool held;   /* rd__nd_hold: the self-test's frames */
    int heldState;
    int sabotage;
    char why[384];
    bool tried;          /* rd__nd_try_begin made the modules, not released */
    RhiShader mod[2];    /* vu_ps_nodual_nd, vu_texa_ps_nodual_nd */
    uint32_t made;       /* modules of the twins made this process (the tests) */
    uint32_t pictures;   /* pictures drawn with a twin since the self-test */
    bool markedOn;       /* "on" written */
    bool wasOn;          /* on at some point of the run (the 10 s clause when off) */
    bool drewNow;        /* the replay being made drew through a twin */
    char markPath[1024]; /* ICO_NODISCARD_MARK: the platform's mark file, "" none */
    char version[64];    /* ICO_NODISCARD_VERSION: the running version, "" none */
    uint64_t took, tookRefused[RD_ND_REASONS]; /* since rd__nd_take */
    uint64_t tookClip;                         /* of took, by the clipper check */
    uint64_t tookEntry[2];                     /* of took, by entry (vu_ps, vu_texa_ps) */
    uint64_t tookEntryClip[2];                 /* of tookClip, by entry */
    bool entryOk[2]; /* the entry's twin drew in the self-test's floor (play uses it then) */
    RdNdUse use;     /* the replay being made, for the in-play judge */
    /* the device's clipper (rd_nodiscard.h "The clipper check"): RD_ND_CLIP_*,
       its first differing case's words, the check running (its frames take
       the camera rule's passes as a clean clipper would), the tests' fake */
    int clipper;
    char clipWhy[320];
    bool clipCheck;
    int clipFake; /* ICO_RD_VU_NODISCARD_CLIPPER: -1 none, else RD_ND_CLIP_* */
} s_n;

/* off for the run (a failed self-test, a pipeline not made): kept through a
   renderer made again in the same process (rd__nd_init) */
static char s_runOff[384];

/* the counts: the block's (the window's 10 s line takes them) and the
   run's (the replay tool's stats); written on the replaying thread, read
   from the window's (atomics) */
typedef struct NCounts {
    uint64_t draws, refused[RD_ND_REASONS];
    uint64_t drawsClip; /* of draws, a clip mode other than none (the clipper check's class) */
} NCounts;

static NCounts s_block, s_run;
/* the counts when the self-test's hold began: its own draws (the floor's
   and the clipper check's) are not play, so the hold's end puts them back */
static NCounts s_heldBlock, s_heldRun;

static void add(uint64_t *a, uint64_t *b, uint64_t v)
{
    __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
    __atomic_fetch_add(b, v, __ATOMIC_RELAXED);
}

static void setOff(const char *why)
{
    s_n.state = N_OFF;
    snprintf(s_n.why, sizeof(s_n.why), "%s", why);
}

static void setRunOff(const char *why)
{
    setOff(why);
    snprintf(s_runOff, sizeof(s_runOff), "%s", s_n.why);
}

/* the mark (port/platform/nodiscard_mark.h): the stage word and the running
   version, written whole, synced where asked (trying, counting, failed:
   before the step they guard; on: not synced, a lost "on" leaves the
   "counting" the next start judges the same way) */
static bool writeMark(const char *word, bool sync)
{
    if (s_n.markPath[0] == '\0') {
        return true; /* no platform's mark (the replay tool, the tests) */
    }
    FILE *f = fopen(s_n.markPath, "wb");
    if (f == NULL) {
        rd__log("discard-free twins: their mark %s could not be written (%s)", s_n.markPath, word);
        return false;
    }
    bool ok = (s_n.version[0] != '\0' ? fprintf(f, "%s %s\n", word, s_n.version)
                                      : fprintf(f, "%s\n", word)) > 0;
    /* the clipper check's verdict with the version it was measured in, the
       second line (port/platform/nodiscard_mark.c logs it at the next start;
       a device property, never a reason to block) */
    if (ok && s_n.clipper != RD_ND_CLIP_UNKNOWN) {
        ok = fprintf(f, "clipper %s %s%s%s%s\n", rd__nd_clipper_text(),
                     s_n.version[0] != '\0' ? s_n.version : "?", s_n.clipWhy[0] ? " (" : "",
                     s_n.clipWhy, s_n.clipWhy[0] ? ")" : "") > 0;
    }
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

void rd__nd_init(void)
{
    /* a renderer made again (the Graphics API switched in the process): no
       handle of the device before is used again (rd__nd_shutdown destroyed
       them where it ran) */
    memset(s_n.mod, 0, sizeof(s_n.mod));
    s_n.tried = false;
    s_n.pictures = 0;
    s_n.markedOn = false;
    s_n.wasOn = false;
    s_n.drewNow = false;
    s_n.took = 0;
    s_n.tookClip = 0;
    memset(s_n.tookEntry, 0, sizeof(s_n.tookEntry));
    memset(s_n.tookEntryClip, 0, sizeof(s_n.tookEntryClip));
    memset(s_n.entryOk, 0, sizeof(s_n.entryOk));
    memset(&s_n.use, 0, sizeof(s_n.use));
    memset(s_n.tookRefused, 0, sizeof(s_n.tookRefused));
    s_n.clipper = RD_ND_CLIP_UNKNOWN;
    s_n.clipWhy[0] = '\0';
    s_n.clipCheck = false;
    const char *cf = getenv("ICO_RD_VU_NODISCARD_CLIPPER");
    /* (lavapipe measures not clean: only the clean verdict needs a fake) */
    s_n.clipFake = cf != NULL && strcmp(cf, "clean") == 0 ? RD_ND_CLIP_CLEAN : -1;
    if (s_n.clipFake >= 0) {
        rd__log("discard-free twins: ICO_RD_VU_NODISCARD_CLIPPER=clean: the clipper check's "
                "verdict taken as clean whatever it measures (tests)");
    }
    const char *mark = getenv("ICO_NODISCARD_MARK");
    snprintf(s_n.markPath, sizeof(s_n.markPath), "%s", mark != NULL ? mark : "");
    const char *ver = getenv("ICO_NODISCARD_VERSION");
    snprintf(s_n.version, sizeof(s_n.version), "%.*s", 63,
             ver != NULL && strchr(ver, '\n') == NULL ? ver : "");
    const char *block = getenv("ICO_NODISCARD_BLOCK");
    s_n.held = false;
    s_n.forced = false;
    s_n.state = N_HELD;
    snprintf(s_n.why, sizeof(s_n.why), "until their start-up self-test passes");
    const char *sb = getenv("ICO_RD_VU_NODISCARD_SABOTAGE");
    s_n.sabotage = NS_NONE;
    if (sb != NULL && strcmp(sb, "alpha") == 0) {
        s_n.sabotage = NS_ALPHA;
    } else if (sb != NULL && strcmp(sb, "module") == 0) {
        s_n.sabotage = NS_MODULE;
    } else if (sb != NULL && strcmp(sb, "clipper") == 0) {
        s_n.sabotage = NS_CLIPPER;
    } else if (sb != NULL && strcmp(sb, "empty") == 0) {
        s_n.sabotage = NS_EMPTY;
    }
    if (s_n.sabotage != NS_NONE) {
        rd__log("discard-free twins: ICO_RD_VU_NODISCARD_SABOTAGE: %s",
                s_n.sabotage == NS_ALPHA
                    ? "the CPU's proof does not look at the alpha test (a twin draws where the "
                      "*_nodual build discards)"
                : s_n.sabotage == NS_MODULE ? "their modules not made"
                : s_n.sabotage == NS_CLIPPER
                    ? "the clipper check's picture through the twins changed in one byte for "
                      "its first case (the check must say not clean)"
                    : "the clipper check's first case moved off the frame, so it draws "
                      "nothing (the check must say not clean: not tested)");
    }
    const char *env = getenv("ICO_RD_VU_NODISCARD");
    const char *row = getenv("ICO_LOSS_ROW");
    const long lost = row != NULL ? strtol(row, NULL, 10) : 0;
    if (s_runOff[0] != '\0') {
        setOff(s_runOff); /* said when they were turned off */
    } else if (block != NULL && block[0] != '\0') {
        /* the install's mark (port/platform/nodiscard_mark.h): a run of this
           version ended badly while they were tried or used, or their
           self-test failed */
        char why[320];
        snprintf(why, sizeof(why), "on this install (%.280s)", block);
        setOff(why);
        rd__log("discard-free twins: off %s", s_n.why);
    } else if (env != NULL && strcmp(env, "0") == 0) {
        setOff("ICO_RD_VU_NODISCARD=0");
    } else if (lost > 0) {
        /* the loss ladder (port/rhi/rhi_tile_rule.h): every device, this
           start only */
        char why[256];
        snprintf(why, sizeof(why),
                 "for this start (the loss ladder: %ld start%s in a row before this one lost the "
                 "graphics device or hung its driver, ICO_LOSS_ROW %ld; nothing written to the "
                 "settings)",
                 lost, lost == 1 ? "" : "s", lost);
        setOff(why);
        rd__log("discard-free twins: off %s", s_n.why);
    } else if (env != NULL && strcmp(env, "1") == 0) {
        s_n.state = N_ON;
        s_n.forced = true;
        s_n.wasOn = true;
        snprintf(s_n.why, sizeof(s_n.why), "ICO_RD_VU_NODISCARD=1");
        rd__log(
            "discard-free twins: on without their self-test (ICO_RD_VU_NODISCARD=1): their "
            "pipelines made at the start of the replay after the one that asked for them, every "
            "one asked for, waited for");
        /* no clipper check without the self-test: the floor (clip mode none)
           unless the tests fake a verdict */
        if (s_n.clipFake >= 0) {
            s_n.clipper = s_n.clipFake;
            snprintf(s_n.clipWhy, sizeof(s_n.clipWhy), "ICO_RD_VU_NODISCARD_CLIPPER, no check run");
        }
    }
}

/* the modules released (with the device: destroyed; the pipelines of them
   dropped first, the GPU idle) */
static void release(void)
{
    if (g_rd.hasDevice && (s_n.mod[0].id || s_n.mod[1].id)) {
        rhi_wait_idle();
        (void)rd__pipeline_drop_twins();
        for (int i = 0; i < 2; i++) {
            if (s_n.mod[i].id) {
                rhi_destroy_shader(s_n.mod[i]);
            }
        }
    }
    memset(s_n.mod, 0, sizeof(s_n.mod));
    if (s_n.tried) {
        s_n.tried = false;
        rd__log("discard-free twins: their modules and pipelines released");
    }
}

void rd__nd_shutdown(void)
{
    /* the pipeline cache was cleared before (rd__gpu_shutdown) */
    if (g_rd.hasDevice) {
        for (int i = 0; i < 2; i++) {
            if (s_n.mod[i].id) {
                rhi_destroy_shader(s_n.mod[i]);
            }
        }
    }
    memset(s_n.mod, 0, sizeof(s_n.mod));
    s_n.tried = false;
}

RhiShader rd__nd_module(uint8_t fs)
{
    return fs == RD_FS_VU ? s_n.mod[0] : fs == RD_FS_VU_TEXA ? s_n.mod[1] : (RhiShader){0};
}

bool rd__nd_try_begin(void)
{
    if (s_n.state == N_OFF || !g_rd.hasDevice) {
        return false;
    }
    if (s_n.tried) {
        return s_n.mod[0].id && s_n.mod[1].id;
    }
    /* the mark first: a crash or a hang inside the compile or the
       self-test leaves it for the next start */
    if (!writeMark("trying", true)) {
        return false; /* no way back after a crash without it: not tried */
    }
    s_n.tried = true;
    bool ok = true;
    for (int i = 0; i < 2 && ok; i++) {
        const char *name = rd_nd_module_name(i == 0 ? (uint8_t)RD_FS_VU : (uint8_t)RD_FS_VU_TEXA);
        s_n.mod[i] = s_n.sabotage == NS_MODULE ? (RhiShader){0} : rd__make_shader(name);
        s_n.made++;
        if (!s_n.mod[i].id) {
            rd__log("discard-free twins: %s could not be made%s", name,
                    s_n.sabotage == NS_MODULE ? " (ICO_RD_VU_NODISCARD_SABOTAGE=module)" : "");
            ok = false;
        }
    }
    if (!ok) {
        (void)writeMark("failed", true); /* the device refused it: off for the version */
        return false;
    }
    rd__log("discard-free twins: trying them: vu_ps_nodual_nd and vu_texa_ps_nodual_nd made (2 "
            "modules; their pipelines with the self-test's, then in the background as passes "
            "ask for them)");
    return true;
}

/* both modules exist (made, not released); the tests' =1 makes them at the
   first need */
static bool ready(void)
{
    if (s_n.forced && !s_n.tried && s_n.state == N_ON) {
        (void)rd__nd_try_begin();
    }
    return s_n.mod[0].id != 0 && s_n.mod[1].id != 0;
}

static void count(int why, bool byClipper, int entry)
{
    if (why == RD_ND_TWIN) {
        add(&s_block.draws, &s_run.draws, 1u);
        s_n.took++;
        s_n.tookEntry[entry]++;
        if (byClipper) {
            add(&s_block.drawsClip, &s_run.drawsClip, 1u);
            s_n.tookClip++;
            s_n.tookEntryClip[entry]++;
        }
    } else if (why > 0 && why < RD_ND_REASONS) {
        add(&s_block.refused[why], &s_run.refused[why], 1u);
        s_n.tookRefused[why]++;
    }
}

bool rd__nd_applicable(void)
{
    /* the device has no dual-source blending by its own features: one that
       has it and fell back to the two-pass blend after the blend's
       self-test (rd_blend_selftest.c, rd_set_no_dual) stays untouched */
    return g_rd.noDual && (!g_rd.hasDevice || !rhi_limits()->dualSourceBlend);
}

const char *rd__nd_not_applicable_text(void)
{
    if (!g_rd.hasDevice || rd__nd_applicable()) {
        return NULL;
    }
    return g_rd.noDual ? "not applicable here: the device has dual-source blending (the two-pass "
                         "blend is its fallback): the twins are for a device without it"
                       : "not applicable here: the device blends with two outputs, so no pass "
                         "draws through a *_nodual build";
}

void rd__nd_pass(RdPipeKeyInt *key, RhiPipeline *pipe, const RdDrawPass *p, uint32_t drawFlags,
                 bool plane, bool particle, bool dateStencil)
{
    if (s_n.state != N_ON || !rd__nd_applicable()) {
        return; /* held, off or not applicable here: nothing asked, nothing counted */
    }
    const bool gathering = rd__pipeline_gathering();
    if ((!pipe->id && !gathering) || !ready()) {
        return;
    }
    const bool clean = s_n.clipCheck || s_n.clipper == RD_ND_CLIP_CLEAN;
    const RdNdPass in = {key->fs,   key->gs.nodual, key->dst, p->flags,    p->modeZ,
                         drawFlags, plane,          particle, dateStencil, s_n.sabotage == NS_ALPHA,
                         clean};
    const int v = rd_nd_eligible(&in);
    if (v < 0) {
        return;
    }
    if (v != RD_ND_TWIN) {
        count(v, false, 0);
        return;
    }
    const int entry = key->fs == RD_FS_VU_TEXA ? 1 : 0;
    if (!s_n.held && !s_n.forced && !s_n.entryOk[entry]) {
        /* its start-up case did not draw through this entry's twin: never
           used in play (logged at the self-test) */
        count(RD_ND_UNTESTED, false, 0);
        return;
    }
    RdPipeKeyInt t = *key;
    t.gs.nodual = (uint8_t)(t.gs.nodual | RD_NODUAL_NODISCARD);
    const RhiPipeline tp = rd__pipeline_twin(&t); /* made, or asked for in the background */
    const int w = !tp.id || gathering                        ? RD_ND_UNMADE
                  : rd__ab_off(RD_AB_NODISCARD) && !s_n.held ? RD_ND_TRIAL
                                                             : RD_ND_TWIN;
    if (!s_n.held) {
        /* the in-play judge (rd_ab_run.h): a replay is timed for the twins
           only when every eligible pass of it had its twin (one found not
           made yet takes the replay out: the share would be partial) */
        rd_nd_use_note(&s_n.use, w);
        if (rd_nd_use_counts(&s_n.use)) {
            rd__ab_used(RD_AB_NODISCARD);
        } else {
            rd__ab_unused(RD_AB_NODISCARD);
        }
    }
    if (w != RD_ND_TWIN) {
        count(w, false, 0);
        return;
    }
    *key = t;
    *pipe = tp;
    /* the class: clip mode none (proven on every device) or another, taken
       on the device's clipper check */
    count(RD_ND_TWIN, (drawFlags & ICO_VU_CLIP_MASK) != ICO_VU_CLIP_NONE, entry);
    s_n.drewNow = true;
}

void rd__nd_replay_begin(void)
{
    memset(&s_n.use, 0, sizeof(s_n.use)); /* the judge's use: this replay's own */
    if (s_n.drewNow) {
        s_n.drewNow = false;
        if (s_n.state == N_ON && !s_n.held && !s_n.markedOn && ++s_n.pictures >= RD_ND_PROBATION) {
            s_n.markedOn = true;
            (void)writeMark("on", false); /* after it only a bad end blocks, as while counting */
            rd__log("discard-free twins: %u pictures drawn with them since their self-test: their "
                    "mark says on",
                    (unsigned)RD_ND_PROBATION);
        }
    }
    if (s_n.forced && s_n.state == N_ON && g_rd.hasDevice) {
        /* the tests' =1: every pipeline the replays before asked for, made
           now, a batch at a time, and waited for (in play:
           rd__pipe_finish_tick, one batch a picture, never waited) */
        rd__pipeline_twin_tick(true);
    }
}

void rd__nd_fault(const char *what)
{
    if (s_n.state == N_OFF) {
        return;
    }
    char why[384];
    snprintf(why, sizeof(why), "for the run: %.360s", what);
    setRunOff(why);
    rd__log("discard-free twins: off %s (the mark is not changed: a background compile that "
            "failed once is this run's; the next start tests them again)",
            s_n.why);
}

/* --------------------------------------------------- the self-test's hold */

void rd__nd_hold(bool on)
{
    if (!s_n.held) {
        s_n.heldState = s_n.state;
        s_heldBlock = s_block;
        s_heldRun = s_run;
    }
    s_n.held = true;
    s_n.state = on ? N_ON : N_HELD;
}

void rd__nd_hold_end(void)
{
    if (s_n.held) {
        s_n.state = s_n.heldState;
        s_block = s_heldBlock;
        s_run = s_heldRun;
    }
    s_n.held = false;
    s_n.drewNow = false; /* the self-test's pictures are not the probation's */
}

void rd__nd_take(uint64_t *twin, uint64_t refused[RD_ND_REASONS])
{
    *twin = s_n.took;
    memcpy(refused, s_n.tookRefused, sizeof(s_n.tookRefused));
    s_n.took = 0;
    s_n.tookClip = 0;
    memset(s_n.tookRefused, 0, sizeof(s_n.tookRefused));
}

bool rd__nd_sabotage_alpha(void)
{
    return s_n.sabotage == NS_ALPHA;
}

bool rd__nd_sabotage_clipper(void)
{
    return s_n.sabotage == NS_CLIPPER;
}

bool rd__nd_sabotage_empty(void)
{
    return s_n.sabotage == NS_EMPTY;
}

void rd__nd_take_entries(uint64_t out[2])
{
    out[0] = s_n.tookEntry[0];
    out[1] = s_n.tookEntry[1];
    s_n.tookEntry[0] = s_n.tookEntry[1] = 0;
}

void rd__nd_take_entries_clip(uint64_t out[2])
{
    out[0] = s_n.tookEntryClip[0];
    out[1] = s_n.tookEntryClip[1];
    s_n.tookEntryClip[0] = s_n.tookEntryClip[1] = 0;
}

void rd__nd_entry_tested(int entry, bool tested)
{
    if (entry >= 0 && entry < 2) {
        s_n.entryOk[entry] = tested;
    }
}

void rd__nd_floor_passed(void)
{
    if (s_n.tried && !s_n.forced) {
        /* past the compile and the floor's comparisons: the clipper check
           after it never writes "failed", so it is judged as play is */
        (void)writeMark("counting", true);
    }
}

uint64_t rd__nd_take_clip(void)
{
    const uint64_t n = s_n.tookClip;
    s_n.tookClip = 0;
    return n;
}

void rd__nd_clipper_check(bool on)
{
    s_n.clipCheck = on;
}

void rd__nd_clipper_verdict(bool clean, const char *why)
{
    s_n.clipper = clean ? RD_ND_CLIP_CLEAN : RD_ND_CLIP_DIRTY;
    snprintf(s_n.clipWhy, sizeof(s_n.clipWhy), "%s", why != NULL ? why : "");
    if (s_n.clipFake >= 0 && s_n.clipFake != s_n.clipper) {
        char measured[320];
        snprintf(measured, sizeof(measured), "%s", s_n.clipWhy);
        s_n.clipper = s_n.clipFake;
        snprintf(s_n.clipWhy, sizeof(s_n.clipWhy),
                 "ICO_RD_VU_NODISCARD_CLIPPER; measured %s%s%.200s", clean ? "clean" : "not clean",
                 measured[0] ? ": " : "", measured);
    }
}

int rd__nd_clipper(void)
{
    return s_n.clipper;
}

const char *rd__nd_clipper_text(void)
{
    return s_n.clipper == RD_ND_CLIP_CLEAN   ? "clean"
           : s_n.clipper == RD_ND_CLIP_DIRTY ? "not clean"
                                             : "not checked";
}

void rd__nd_verdict(bool pass, const char *why)
{
    if (s_n.state == N_OFF || s_n.forced) {
        return; /* off before it (the environment, the ladder, the mark), or the tests' own */
    }
    if (pass) {
        s_n.state = N_ON;
        s_n.wasOn = true;
        s_n.why[0] = '\0';
        s_n.pictures = 0;
        if (s_n.tried) {
            /* past the compile and the self-test: from here only an end the
               evidence calls bad blocks them (port/platform/nodiscard_mark.h) */
            (void)writeMark("counting", true);
        }
    } else {
        char w[200];
        snprintf(w, sizeof(w), "for the run (their self-test: %.150s)", why);
        setRunOff(w);
        if (s_n.tried) {
            (void)writeMark("failed", true); /* the install's: off until the version changes */
        }
        release();
    }
}

void rd__nd_selftest_skip(const char *why)
{
    if (s_n.state == N_HELD) {
        char w[240];
        snprintf(w, sizeof(w), "for the run (self-test not run: %.190s)", why);
        setOff(w); /* not a fault: a renderer made again tests them */
        rd__log("discard-free twins: off %s", s_n.why);
    }
    rd__nd_start_line();
}

void rd__nd_start_line(void)
{
    rd__log("discard-free twins: %s, %u modules of them made, %u of their pipelines alive",
            rd__nd_state_text(), (unsigned)s_n.made, (unsigned)rd__pipeline_count_twins());
}

bool rd__nd_triable(void)
{
    return s_n.state == N_ON && !s_n.held && s_n.mod[0].id != 0 && s_n.mod[1].id != 0;
}

bool rd__nd_on(void)
{
    return s_n.state == N_ON;
}

const char *rd__nd_state_text(void)
{
    return s_n.state == N_ON ? "on" : s_n.state == N_HELD ? "held" : "off";
}

/* -------------------------------------------------------------- the lines */

static uint64_t takeN(uint64_t *v)
{
    return __atomic_exchange_n(v, 0u, __ATOMIC_RELAXED);
}

size_t rd_nd_text(char *out, size_t cap, unsigned n)
{
    if (cap == 0) {
        return 0;
    }
    NCounts c;
    c.draws = takeN(&s_block.draws);
    c.drawsClip = takeN(&s_block.drawsClip);
    uint64_t refused = 0;
    for (int i = 0; i < RD_ND_REASONS; i++) {
        c.refused[i] = takeN(&s_block.refused[i]);
        refused += c.refused[i];
    }
    out[0] = '\0';
    if (s_n.state != N_ON) {
        /* while held or off from the start the start lines say why; once on,
           the turn off is said here too */
        if (!s_n.wasOn) {
            return 0;
        }
        const int k =
            snprintf(out, cap, "; discard-free twins %s (%s)", rd__nd_state_text(), s_n.why);
        return k < 0 ? 0u : ((size_t)k < cap ? (size_t)k : cap - 1u);
    }
    const double d = n ? (double)n : 1.0;
    int at = snprintf(out, cap,
                      "; discard-free twins: %.0f draws a picture through them, %.0f of them by "
                      "the clipper check (%s) (%.0f refused a picture:",
                      (double)c.draws / d, (double)c.drawsClip / d, rd__nd_clipper_text(),
                      (double)refused / d);
    for (int i = 1; i < RD_ND_REASONS && at > 0 && (size_t)at < cap; i++) {
        at += snprintf(out + at, cap - (size_t)at, "%s %s %.0f", i > 1 ? "," : "",
                       rd_nd_reason_name(i), (double)c.refused[i] / d);
    }
    if (at > 0 && (size_t)at < cap) {
        at += snprintf(out + at, cap - (size_t)at, ")");
    }
    /* the background set's cost in the block (new on a phone in play:
       batches of 8 at most, low priority, one at a time between pictures) */
    uint32_t made = 0, failed = 0, batches = 0;
    double ms = 0.0;
    rd__pipeline_twins_take(&made, &failed, &batches, &ms);
    if (batches != 0u && at > 0 && (size_t)at < cap) {
        at += snprintf(out + at, cap - (size_t)at,
                       "; their pipelines made in the background: %u in %u batches, %.0f ms "
                       "from begin to taken%s",
                       (unsigned)made, (unsigned)batches, ms, failed ? ", some failed" : "");
    }
    return at < 0 ? 0u : ((size_t)at < cap ? (size_t)at : cap - 1u);
}

int rd__nd_stats_text(char *out, size_t cap)
{
    const NCounts *c = &s_run;
    char refused[256];
    int at = 0;
    refused[0] = '\0';
    for (int i = 1; i < RD_ND_REASONS && at >= 0 && (size_t)at < sizeof(refused); i++) {
        at += snprintf(refused + at, sizeof(refused) - (size_t)at, "%s%s %llu", i > 1 ? ", " : "",
                       rd_nd_reason_name(i), (unsigned long long)c->refused[i]);
    }
    return snprintf(out, cap,
                    "discard-free twins %s%s%s%s: %llu draws through them, %llu of them by the "
                    "clipper check (%s); refused: %s; %u modules of them made, %u of their "
                    "pipelines alive%s%s%s",
                    rd__nd_state_text(), s_n.why[0] ? " (" : "", s_n.why, s_n.why[0] ? ")" : "",
                    (unsigned long long)c->draws, (unsigned long long)c->drawsClip,
                    rd__nd_clipper_text(), refused, (unsigned)s_n.made,
                    (unsigned)rd__pipeline_count_twins(),
                    rd__nd_not_applicable_text() != NULL ? " (" : "",
                    rd__nd_not_applicable_text() != NULL ? rd__nd_not_applicable_text() : "",
                    rd__nd_not_applicable_text() != NULL ? ")" : "");
}
