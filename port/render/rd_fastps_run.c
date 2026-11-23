/* rd_fastps_run.c: the fast pixel build in the renderer (rd_fastps.h): its
 * state for the run, the install's mark, the two modules, a pass's choice,
 * the counts and the lines.  The discard-free twins' form
 * (rd_nodiscard_run.c), one subject.
 *
 * The state: off (ICO_RD_VU_FASTPS=0, the loss ladder, the install's mark,
 * a failed self-test, a pipeline the background could not make), held
 * (until the start-up self-test passes: nothing of it exists on the device,
 * every pass draws through vu_ps_nodual) or on.  ICO_RD_VU_FASTPS=1 (tests)
 * starts on, the modules made at the first pass that wants one.
 *
 *   rd: fast pixel build: off for this start (the loss ladder: ...)
 *   rd: fast pixel build: trying it: vu_ps_nodual_fast and
 *     vu_ps_nodual_fast_nd made (2 modules; their pipelines with the
 *     self-test's, then in the background as passes ask for them)
 *   rd: fast pixel build: off for the run: a pipeline of it could not be
 *     made in the background (...)
 *   rd: fast pixel build: 250 pictures drawn with it since its self-test:
 *     its mark says on
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
#include "rd_fastps.h"
#include "host_fs.h" /* ico_fopen: UTF-8 paths on Windows */
#include "rd_internal.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

enum { F_HELD, F_ON, F_OFF };

/* ICO_RD_VU_FASTPS_SABOTAGE (tests) */
enum { FS_NONE, FS_DECAL, FS_MODULE };

static struct {
    int state;
    bool forced; /* ICO_RD_VU_FASTPS=1 */
    bool held;   /* rd__fps_hold: the self-test's frames */
    int heldState;
    int sabotage;
    char why[384];
    bool tried;          /* rd__fps_try_begin made the modules, not released */
    RhiShader mod[2];    /* vu_ps_nodual_fast, vu_ps_nodual_fast_nd */
    uint32_t made;       /* modules made this process (the tests) */
    uint32_t pictures;   /* pictures drawn with it since the self-test */
    bool markedOn;       /* "on" written */
    bool wasOn;          /* on at some point of the run (the 10 s clause when off) */
    bool drewNow;        /* the replay being made drew through it */
    char markPath[1024]; /* ICO_FASTPS_MARK: the platform's mark file, "" none */
    char version[64];    /* ICO_FASTPS_VERSION: the running version, "" none */
    uint64_t took, tookRefused[RD_FPS_REASONS]; /* since rd__fps_take */
    uint64_t tookEntry[2];                      /* of took, by module */
    bool entryOk[2]; /* the module drew in the self-test (play uses it then) */
    RdFpsUse use;    /* the replay being made, for the in-play judge */
} s_f;

/* off for the run: kept through a renderer made again in the same process */
static char s_runOff[384];

/* the counts: the block's (the window's 10 s line) and the run's (the
   replay tool's stats); written on the replaying thread, read from the
   window's (atomics) */
typedef struct FCounts {
    uint64_t passes, tris, refused[RD_FPS_REASONS];
    uint64_t passesNd; /* of passes, through vu_ps_nodual_fast_nd */
} FCounts;

static FCounts s_block, s_run, s_heldBlock, s_heldRun;

static void add(uint64_t *a, uint64_t *b, uint64_t v)
{
    __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
    __atomic_fetch_add(b, v, __ATOMIC_RELAXED);
}

static void setOff(const char *why)
{
    s_f.state = F_OFF;
    snprintf(s_f.why, sizeof(s_f.why), "%s", why);
}

static void setRunOff(const char *why)
{
    setOff(why);
    snprintf(s_runOff, sizeof(s_runOff), "%s", s_f.why);
}

/* the mark (port/platform/fastps_mark.h): the stage word and the running
   version, synced where asked (trying, counting, failed) */
static bool writeMark(const char *word, bool sync)
{
    if (s_f.markPath[0] == '\0') {
        return true; /* no platform's mark (the replay tool, the tests) */
    }
    FILE *f = ico_fopen(s_f.markPath, "wb");
    if (f == NULL) {
        rd__log("fast pixel build: its mark %s could not be written (%s)", s_f.markPath, word);
        return false;
    }
    bool ok = (s_f.version[0] != '\0' ? fprintf(f, "%s %s\n", word, s_f.version)
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

void rd__fps_init(void)
{
    memset(s_f.mod, 0, sizeof(s_f.mod));
    s_f.tried = false;
    s_f.pictures = 0;
    s_f.markedOn = false;
    s_f.wasOn = false;
    s_f.drewNow = false;
    s_f.took = 0;
    memset(s_f.tookRefused, 0, sizeof(s_f.tookRefused));
    memset(s_f.tookEntry, 0, sizeof(s_f.tookEntry));
    memset(s_f.entryOk, 0, sizeof(s_f.entryOk));
    memset(&s_f.use, 0, sizeof(s_f.use));
    const char *mark = getenv("ICO_FASTPS_MARK");
    snprintf(s_f.markPath, sizeof(s_f.markPath), "%s", mark != NULL ? mark : "");
    const char *ver = getenv("ICO_FASTPS_VERSION");
    snprintf(s_f.version, sizeof(s_f.version), "%.*s", 63,
             ver != NULL && strchr(ver, '\n') == NULL ? ver : "");
    const char *block = getenv("ICO_FASTPS_BLOCK");
    s_f.held = false;
    s_f.forced = false;
    s_f.state = F_HELD;
    snprintf(s_f.why, sizeof(s_f.why), "until its start-up self-test passes");
    const char *sb = getenv("ICO_RD_VU_FASTPS_SABOTAGE");
    s_f.sabotage = sb != NULL && strcmp(sb, "decal") == 0    ? FS_DECAL
                   : sb != NULL && strcmp(sb, "module") == 0 ? FS_MODULE
                                                             : FS_NONE;
    if (s_f.sabotage != FS_NONE) {
        rd__log("fast pixel build: ICO_RD_VU_FASTPS_SABOTAGE: %s",
                s_f.sabotage == FS_DECAL ? "the CPU's rule does not look at DECAL (a DECAL pass "
                                           "draws through the build, which reads MODULATE)"
                                         : "its modules not made");
    }
    const char *env = getenv("ICO_RD_VU_FASTPS");
    const char *row = getenv("ICO_LOSS_ROW");
    const long lost = row != NULL ? strtol(row, NULL, 10) : 0;
    if (s_runOff[0] != '\0') {
        setOff(s_runOff);
    } else if (block != NULL && block[0] != '\0') {
        char why[320];
        snprintf(why, sizeof(why), "on this install (%.280s)", block);
        setOff(why);
        rd__log("fast pixel build: off %s", s_f.why);
    } else if (env != NULL && strcmp(env, "0") == 0) {
        setOff("ICO_RD_VU_FASTPS=0");
    } else if (lost > 0) {
        char why[256];
        snprintf(why, sizeof(why),
                 "for this start (the loss ladder: %ld start%s in a row before this one lost the "
                 "graphics device or hung its driver, ICO_LOSS_ROW %ld; nothing written to the "
                 "settings)",
                 lost, lost == 1 ? "" : "s", lost);
        setOff(why);
        rd__log("fast pixel build: off %s", s_f.why);
    } else if (env != NULL && strcmp(env, "1") == 0) {
        s_f.state = F_ON;
        s_f.forced = true;
        s_f.wasOn = true;
        snprintf(s_f.why, sizeof(s_f.why), "ICO_RD_VU_FASTPS=1");
        rd__log("fast pixel build: on without its self-test (ICO_RD_VU_FASTPS=1): its pipelines "
                "made at the start of the replay after the one that asked for them, every one "
                "asked for, waited for");
    }
}

static void release(void)
{
    if (g_rd.hasDevice && (s_f.mod[0].id || s_f.mod[1].id)) {
        rhi_wait_idle();
        (void)rd__pipeline_drop_fast();
        for (int i = 0; i < 2; i++) {
            if (s_f.mod[i].id) {
                rhi_destroy_shader(s_f.mod[i]);
            }
        }
    }
    memset(s_f.mod, 0, sizeof(s_f.mod));
    if (s_f.tried) {
        s_f.tried = false;
        rd__log("fast pixel build: its modules and pipelines released");
    }
}

void rd__fps_shutdown(void)
{
    /* the pipeline cache was cleared before (rd__gpu_shutdown) */
    if (g_rd.hasDevice) {
        for (int i = 0; i < 2; i++) {
            if (s_f.mod[i].id) {
                rhi_destroy_shader(s_f.mod[i]);
            }
        }
    }
    memset(s_f.mod, 0, sizeof(s_f.mod));
    s_f.tried = false;
}

RhiShader rd__fps_module(bool nd)
{
    return s_f.mod[nd ? 1 : 0];
}

bool rd__fps_try_begin(void)
{
    if (s_f.state == F_OFF || !g_rd.hasDevice) {
        return false;
    }
    if (s_f.tried) {
        return s_f.mod[0].id && s_f.mod[1].id;
    }
    /* the mark first: a crash or a hang inside the compile or the
       self-test leaves it for the next start */
    if (!writeMark("trying", true)) {
        return false;
    }
    s_f.tried = true;
    bool ok = true;
    static const char *const kName[2] = {RD_FPS_MODULE_NAME, RD_FPS_MODULE_NAME_ND};
    for (int i = 0; i < 2 && ok; i++) {
        s_f.mod[i] = s_f.sabotage == FS_MODULE ? (RhiShader){0} : rd__make_shader(kName[i]);
        s_f.made++;
        if (!s_f.mod[i].id) {
            rd__log("fast pixel build: %s could not be made%s", kName[i],
                    s_f.sabotage == FS_MODULE ? " (ICO_RD_VU_FASTPS_SABOTAGE=module)" : "");
            ok = false;
        }
    }
    if (!ok) {
        (void)writeMark("failed", true);
        return false;
    }
    rd__log("fast pixel build: trying it: vu_ps_nodual_fast and vu_ps_nodual_fast_nd made (2 "
            "modules; their pipelines with the self-test's, then in the background as passes "
            "ask for them)");
    return true;
}

static bool ready(void)
{
    if (s_f.forced && !s_f.tried && s_f.state == F_ON) {
        (void)rd__fps_try_begin();
    }
    return s_f.mod[0].id != 0 && s_f.mod[1].id != 0;
}

static void count(int why, uint32_t tris, int entry)
{
    if (why == RD_FPS_FAST) {
        add(&s_block.passes, &s_run.passes, 1u);
        add(&s_block.tris, &s_run.tris, tris);
        if (entry == 1) {
            add(&s_block.passesNd, &s_run.passesNd, 1u);
        }
        s_f.took++;
        s_f.tookEntry[entry]++;
    } else if (why > 0 && why < RD_FPS_REASONS) {
        add(&s_block.refused[why], &s_run.refused[why], 1u);
        s_f.tookRefused[why]++;
    }
}

const char *rd__fps_not_applicable_text(void)
{
    if (!g_rd.hasDevice || rd__nd_applicable()) {
        return NULL; /* the twins' test: no dual-source blending by the device's own features */
    }
    return g_rd.noDual ? "not applicable here: the device has dual-source blending (the two-pass "
                         "blend is its fallback): the build is for a device without it"
                       : "not applicable here: the device blends with two outputs, so no pass "
                         "draws through vu_ps_nodual";
}

void rd__fps_pass(RdPipeKeyInt *key, RhiPipeline *pipe, const IcoDrawCB *cb, uint32_t tris)
{
    if (s_f.state != F_ON || !rd__nd_applicable()) {
        return; /* held, off or not applicable here: nothing asked, nothing counted */
    }
    const bool gathering = rd__pipeline_gathering();
    if ((!pipe->id && !gathering) || !ready()) {
        return;
    }
    RdFpsPass in;
    memset(&in, 0, sizeof(in));
    in.fs = key->fs;
    in.nodual = key->gs.nodual;
    in.dst = key->dst;
    memcpy(in.mode, cb->mode, sizeof(in.mode));
    in.scaleZ = cb->scale[2];
    in.skipDecal = s_f.sabotage == FS_DECAL;
    const int v = rd_fps_eligible(&in);
    if (v < 0) {
        return;
    }
    if (v != RD_FPS_FAST) {
        count(v, 0, 0);
        return;
    }
    const int entry = (key->gs.nodual & RD_NODUAL_NODISCARD) ? 1 : 0;
    if (!s_f.held && !s_f.forced && !s_f.entryOk[entry]) {
        count(RD_FPS_UNTESTED, 0, 0); /* its module drew nothing in the self-test */
        return;
    }
    RdPipeKeyInt t = *key;
    t.gs.nodual = (uint8_t)(t.gs.nodual | RD_NODUAL_FAST);
    const RhiPipeline tp = rd__pipeline_twin(&t); /* made, or asked for in the background */
    const int w = !tp.id || gathering                     ? RD_FPS_UNMADE
                  : rd__ab_off(RD_AB_FASTPS) && !s_f.held ? RD_FPS_TRIAL
                                                          : RD_FPS_FAST;
    if (!s_f.held) {
        /* the judge times a replay only when every eligible pass of it had
           its fast pipeline (one not made yet takes the replay out) */
        rd_fps_use_note(&s_f.use, w);
        if (rd_fps_use_counts(&s_f.use)) {
            rd__ab_used(RD_AB_FASTPS);
        } else {
            rd__ab_unused(RD_AB_FASTPS);
        }
        if (w == RD_FPS_UNMADE) {
            /* a pass drew through vu_ps_nodual only because its fast pipeline
               is not made yet (the twins' off arm asks for ON|FAST, which
               play had never asked for): no subject is timed on this replay,
               the twins' trial included (REV4R S1) */
            rd__ab_unmade();
        }
    }
    if (w != RD_FPS_FAST) {
        count(w, 0, 0);
        return;
    }
    *key = t;
    *pipe = tp;
    count(RD_FPS_FAST, tris, entry);
    s_f.drewNow = true;
}

void rd__fps_replay_begin(void)
{
    memset(&s_f.use, 0, sizeof(s_f.use));
    if (s_f.drewNow) {
        s_f.drewNow = false;
        if (s_f.state == F_ON && !s_f.held && !s_f.markedOn && ++s_f.pictures >= RD_FPS_PROBATION) {
            s_f.markedOn = true;
            (void)writeMark("on", false);
            rd__log("fast pixel build: %u pictures drawn with it since its self-test: its mark "
                    "says on",
                    (unsigned)RD_FPS_PROBATION);
        }
    }
    if (s_f.forced && s_f.state == F_ON && g_rd.hasDevice) {
        rd__pipeline_twin_tick(true); /* the tests' =1: made now, waited for */
    }
}

void rd__fps_fault(const char *what)
{
    if (s_f.state == F_OFF) {
        return;
    }
    char why[384];
    snprintf(why, sizeof(why), "for the run: %.360s", what);
    setRunOff(why);
    rd__log("fast pixel build: off %s (the mark is not changed: a background compile that failed "
            "once is this run's; the next start tests it again)",
            s_f.why);
}

/* --------------------------------------------------- the self-test's hold */

void rd__fps_hold(bool on)
{
    if (!s_f.held) {
        s_f.heldState = s_f.state;
        s_heldBlock = s_block;
        s_heldRun = s_run;
    }
    s_f.held = true;
    s_f.state = on ? F_ON : F_HELD;
}

void rd__fps_hold_end(void)
{
    if (s_f.held) {
        rd__pipeline_twin_forget_queued(); /* its frames' keys */
        s_f.state = s_f.heldState;
        s_block = s_heldBlock;
        s_run = s_heldRun;
    }
    s_f.held = false;
    s_f.drewNow = false; /* the self-test's pictures are not the probation's */
}

void rd__fps_take(uint64_t *fast, uint64_t refused[RD_FPS_REASONS])
{
    *fast = s_f.took;
    memcpy(refused, s_f.tookRefused, sizeof(s_f.tookRefused));
    s_f.took = 0;
    memset(s_f.tookRefused, 0, sizeof(s_f.tookRefused));
}

void rd__fps_take_entries(uint64_t out[2])
{
    out[0] = s_f.tookEntry[0];
    out[1] = s_f.tookEntry[1];
    s_f.tookEntry[0] = s_f.tookEntry[1] = 0;
}

void rd__fps_entry_tested(int entry, bool tested)
{
    if (entry >= 0 && entry < 2) {
        s_f.entryOk[entry] = tested;
    }
}

bool rd__fps_sabotage_decal(void)
{
    return s_f.sabotage == FS_DECAL;
}

void rd__fps_verdict(bool pass, const char *why)
{
    if (s_f.state == F_OFF || s_f.forced) {
        return;
    }
    if (pass) {
        s_f.state = F_ON;
        s_f.wasOn = true;
        s_f.why[0] = '\0';
        s_f.pictures = 0;
        if (s_f.tried) {
            (void)writeMark("counting", true);
        }
    } else {
        char w[200];
        snprintf(w, sizeof(w), "for the run (its self-test: %.150s)", why);
        setRunOff(w);
        if (s_f.tried) {
            (void)writeMark("failed", true);
        }
        release();
    }
}

void rd__fps_selftest_skip(const char *why)
{
    if (s_f.state == F_HELD) {
        char w[240];
        snprintf(w, sizeof(w), "for the run (self-test not run: %.190s)", why);
        setOff(w); /* not a fault: a renderer made again tests it */
        rd__log("fast pixel build: off %s", s_f.why);
    }
    rd__fps_start_line();
}

void rd__fps_start_line(void)
{
    rd__log("fast pixel build: %s, %u modules of it made, %u of its pipelines alive",
            rd__fps_state_text(), (unsigned)s_f.made, (unsigned)rd__pipeline_count_fast());
}

bool rd__fps_triable(void)
{
    return s_f.state == F_ON && !s_f.held && s_f.mod[0].id != 0 && s_f.mod[1].id != 0;
}

bool rd__fps_on(void)
{
    return s_f.state == F_ON;
}

const char *rd__fps_state_text(void)
{
    return s_f.state == F_ON ? "on" : s_f.state == F_HELD ? "held" : "off";
}

/* -------------------------------------------------------------- the lines */

static uint64_t takeN(uint64_t *v)
{
    return __atomic_exchange_n(v, 0u, __ATOMIC_RELAXED);
}

size_t rd_fps_text(char *out, size_t cap, unsigned n)
{
    if (cap == 0) {
        return 0;
    }
    FCounts c;
    c.passes = takeN(&s_block.passes);
    c.passesNd = takeN(&s_block.passesNd);
    c.tris = takeN(&s_block.tris);
    uint64_t refused = 0;
    for (int i = 0; i < RD_FPS_REASONS; i++) {
        c.refused[i] = takeN(&s_block.refused[i]);
        refused += c.refused[i];
    }
    out[0] = '\0';
    if (s_f.state != F_ON) {
        if (!s_f.wasOn) {
            return 0;
        }
        const int k =
            snprintf(out, cap, "; fast pixel build %s (%s)", rd__fps_state_text(), s_f.why);
        return k < 0 ? 0u : ((size_t)k < cap ? (size_t)k : cap - 1u);
    }
    const double d = n ? (double)n : 1.0;
    int at = snprintf(out, cap,
                      "; fast pixel build: %.0f passes a picture through it, %.0f triangles (%.0f "
                      "refused a picture:",
                      (double)c.passes / d, (double)c.tris / d, (double)refused / d);
    for (int i = 1; i < RD_FPS_REASONS && at > 0 && (size_t)at < cap; i++) {
        at += snprintf(out + at, cap - (size_t)at, "%s %s %.0f", i > 1 ? "," : "",
                       rd_fps_reason_name(i), (double)c.refused[i] / d);
    }
    if (at > 0 && (size_t)at < cap) {
        at += snprintf(out + at, cap - (size_t)at, ")");
    }
    return at < 0 ? 0u : ((size_t)at < cap ? (size_t)at : cap - 1u);
}

int rd__fps_stats_text(char *out, size_t cap)
{
    const FCounts *c = &s_run;
    char refused[320];
    int at = 0;
    refused[0] = '\0';
    for (int i = 1; i < RD_FPS_REASONS && at >= 0 && (size_t)at < sizeof(refused); i++) {
        at += snprintf(refused + at, sizeof(refused) - (size_t)at, "%s%s %llu", i > 1 ? ", " : "",
                       rd_fps_reason_name(i), (unsigned long long)c->refused[i]);
    }
    const char *na = rd__fps_not_applicable_text();
    return snprintf(out, cap,
                    "fast pixel build %s%s%s%s: %llu passes through it (%llu of them through "
                    "vu_ps_nodual_fast_nd), %llu triangles; refused: %s; %u modules of it made, "
                    "%u of its pipelines alive%s%s%s",
                    rd__fps_state_text(), s_f.why[0] ? " (" : "", s_f.why, s_f.why[0] ? ")" : "",
                    (unsigned long long)c->passes, (unsigned long long)c->passesNd,
                    (unsigned long long)c->tris, refused, (unsigned)s_f.made,
                    (unsigned)rd__pipeline_count_fast(), na ? " (" : "", na ? na : "",
                    na ? ")" : "");
}
