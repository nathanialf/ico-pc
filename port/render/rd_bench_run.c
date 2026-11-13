/*
 * port/render/rd_bench_run.c
 *
 * The rc-only bench's loop over one kept frame (rd_bench_run.h).
 */
#include "rd_bench_run.h"

#include "rd_bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IcoBenchRun s_run;
static IcoBenchResult s_res[ICO_BENCH_VARIANTS];
static RdBenchVariant s_var[ICO_BENCH_VARIANTS];

/* the replays posted in the frame's run: the block and timed place of each
   (-1: untimed), and when it was posted */
#define SEQ_MAX (ICO_BENCH_BLOCKS * (ICO_BENCH_W + ICO_BENCH_K_MAX) + 4 * ICO_BENCH_VARIANTS + 64)

static struct {
    int16_t block;
    int8_t timed;
} s_seq[SEQ_MAX];

static double s_postMs[SEQ_MAX];
static int s_posted, s_popped, s_prevQ;
static double s_prevStart;

/* the bench's records (its frame's number) in replay order, each made the
   sample of its replay */
static void collect(const RdBenchFrame *b)
{
    RdPerfRecord r;
    while (rd_perf_pop(&r)) {
        if (r.frame != b->f.number || s_popped >= s_posted) {
            continue; /* the game's own replays before the bench */
        }
        const int q = s_popped++;
        double pic = -1.0;
        if (q > 0 && s_prevQ == q - 1 && s_prevStart > 0.0 && r.startMs > s_prevStart) {
            pic = r.startMs - s_prevStart;
        } else if (q > 0) {
            pic = s_postMs[q] - s_postMs[q - 1]; /* no start in the record: the posts' */
        }
        s_prevQ = q;
        s_prevStart = r.startMs;
        const int blk = s_seq[q].block, j = s_seq[q].timed;
        if (blk < 0 || j < 0 || blk >= s_run.nBlocks || j >= ICO_BENCH_K_MAX) {
            continue;
        }
        IcoBenchSample *s = &s_run.s[blk][j];
        memset(s, 0, sizeof(*s));
        s->pictureMs = (float)pic;
        s->gpuValid = r.gpuValid;
        s->gpuMs = (float)r.gpuMs;
        s->waitMs = (float)r.waitMs;
        s->passes = r.renderPasses;
        s->draws = r.draws;
        s->tris = r.vuTris;
        s->plan = (uint16_t)r.glowLight;
        s->noVolume = r.noVolumeKept;
        s->emptyComposites = (uint16_t)r.shadowBoxEmpty;
        s->merged = (uint16_t)r.shadowMerged;
        s->crackFills = (uint16_t)r.crackFillPasses;
        s->presentScene = (uint16_t)r.presentSceneShown;
        s->excluded = r.pipelineCreates > 0 || !r.presented || pic <= 0.0;
        s_run.excluded += s->excluded;
        if (s_run.ns[blk] < j + 1) {
            s_run.ns[blk] = (uint8_t)(j + 1);
        }
    }
}

static int stopNow(const RdBenchRunCfg *cfg, const RdBenchRunCb *cb)
{
    if (!g_rd.hasDevice || rhi_device_lost()) {
        return RD_BENCH_LOST;
    }
    if (cfg->capMs > 0.0 && rd__now_ms() - cfg->startMs > cfg->capMs) {
        return RD_BENCH_CAPPED;
    }
    return cb && cb->poll ? cb->poll(cb->user) : 0;
}

static bool post(RdBenchFrame *b, int block, int timed)
{
    if (s_posted >= SEQ_MAX) {
        return false;
    }
    s_seq[s_posted].block = (int16_t)block;
    s_seq[s_posted].timed = (int8_t)timed;
    s_postMs[s_posted] = rd__now_ms();
    s_posted++;
    if (!rd_bench_post(b)) {
        return false;
    }
    collect(b);
    return true;
}

static void say(const RdBenchRunCb *cb, const char *text)
{
    if (cb && cb->line) {
        cb->line(cb->user, text);
    }
}

const char *rd_bench_stop_words(int stopped)
{
    switch (stopped) {
    case RD_BENCH_DONE:
        return "done";
    case RD_BENCH_SKIPPED:
        return "skipped by a tap";
    case RD_BENCH_LEFT:
        return "stopped: the game left the screen";
    case RD_BENCH_LOST:
        return "stopped: the device was lost or a replay could not be posted";
    case RD_BENCH_CAPPED:
        return "stopped at the bench's time cap";
    default:
        return "not run (no variant or no memory)";
    }
}

static void emit(const RdBenchFrame *b, const RdBenchRunCfg *cfg, const RdBenchRunCb *cb)
{
    static char line[4096];
    IcoBenchDrift drift;
    ico_bench_results(&s_run, s_res, &drift);
    /* the GPU stamps as the baseline's timed replays hold them: none timed
     * (a run skipped or stopped before its first block) is "not known", not
     * "no" (the phone's pause bench: skipped 0.2 s in, "GPU stamps no") */
    const int stamps = s_res[0].gpu.n > 0 ? 1 : (s_res[0].pic.n > 0 ? 0 : -1);
    ico_bench_head_line(line, sizeof(line), &s_run, b->commands, (uint32_t)(b->bytes / 1024u),
                        cfg->presentMode, stamps);
    say(cb, line);
    for (int v = 0; v < s_run.nVariants; v++) {
        ico_bench_variant_line(line, sizeof(line), &s_run, v, &s_res[v]);
        say(cb, line);
    }
    ico_bench_drift_line(line, sizeof(line), &s_run, &drift);
    say(cb, line);
    int order[ICO_BENCH_VARIANTS];
    const int n = ico_bench_rank(&s_run, s_res, order, ICO_BENCH_VARIANTS);
    for (int from = 0; from < n; from += 6) {
        ico_bench_rank_line(line, sizeof(line), &s_run, s_res, order, n, from, 6);
        say(cb, line);
    }
    ico_bench_sum_line(line, sizeof(line), &s_run, s_res);
    say(cb, line);
    if (cb && cb->csv) {
        if (cfg->csvHeader) {
            ico_bench_csv_header(line, sizeof(line));
            cb->csv(cb->user, line);
        }
        for (int v = 0; v < s_run.nVariants; v++) {
            ico_bench_csv_row(line, sizeof(line), &s_run, v, &s_res[v], &drift);
            cb->csv(cb->user, line);
        }
    }
}

int rd_bench_run_frame(RdBenchFrame *b, int frame, int frames, int kind, const RdBenchRunCfg *cfg,
                       const RdBenchRunCb *cb, RdBenchRunOut *out)
{
    const double t0 = rd__now_ms();
    char text[192];
    memset(out, 0, sizeof(*out));
    memset(&s_run, 0, sizeof(s_run));
    for (int bl = 0; bl < ICO_BENCH_BLOCKS; bl++) {
        for (int j = 0; j < ICO_BENCH_K_MAX; j++) {
            s_run.s[bl][j].excluded = 1; /* a replay whose record never came counts for nothing */
        }
    }
    s_posted = s_popped = 0;
    s_prevQ = -2;
    s_prevStart = 0.0;
    s_run.frame = frame;
    s_run.frames = frames;
    s_run.kind = kind;
    s_run.number =
        b->f.number >= RD_BENCH_NUMBER_BASE ? b->f.number - RD_BENCH_NUMBER_BASE : b->f.number;
    s_run.w = cfg->w < 1 ? 1 : cfg->w;
    s_run.rounds = cfg->rounds;
    s_run.every = cfg->every;
    const int most = cfg->k < 1 ? 1 : (cfg->k > ICO_BENCH_K_MAX ? ICO_BENCH_K_MAX : cfg->k);
    s_run.nVariants = rd_bench_variants(b, s_var, ICO_BENCH_VARIANTS);
    for (int v = 0; v < s_run.nVariants; v++) {
        snprintf(s_run.info[v].name, sizeof(s_run.info[v].name), "%.*s",
                 (int)sizeof(s_run.info[v].name) - 1, s_var[v].name);
        s_run.info[v].knock = s_var[v].knock;
        s_run.info[v].sum = s_var[v].sum;
    }
    s_run.nBlocks = ico_bench_schedule(s_run.nVariants, s_run.rounds, s_run.every, s_run.block,
                                       ICO_BENCH_BLOCKS);
    out->variants = s_run.nVariants;
    if (s_run.nVariants < 1 || s_run.nBlocks < 1) {
        out->stopped = RD_BENCH_FAILED;
        return out->stopped;
    }
    /* the records before the bench are the game's */
    rd_bench_settle();
    while (rd_perf_pop(NULL)) {}
    int stopped = 0;
    if (cb && cb->status) {
        snprintf(text, sizeof(text), "Measuring the picture: frame %d of %d, getting ready", frame,
                 frames);
        cb->status(cb->user, text);
    }
    /* every variant once, untimed: what it makes is made now */
    for (int v = 0; v < s_run.nVariants && !stopped; v++) {
        s_run.info[v].removed = rd_bench_apply(b, &s_var[v]);
        if ((stopped = stopNow(cfg, cb)) == 0 && !post(b, -1, -1)) {
            stopped = RD_BENCH_LOST;
        }
    }
    /* the baseline's picture time, for K */
    double pictureMs = 0.0;
    if (!stopped) {
        (void)rd_bench_apply(b, &s_var[0]);
        for (int j = 0; j < 3 && !stopped; j++) {
            if ((stopped = stopNow(cfg, cb)) == 0 && !post(b, -1, -1)) {
                stopped = RD_BENCH_LOST;
            }
        }
        if (!stopped && s_posted >= 3) {
            pictureMs = (s_postMs[s_posted - 1] - s_postMs[s_posted - 3]) / 2.0;
        }
    }
    s_run.k = cfg->frameBudgetMs > 0.0
                  ? ico_bench_k(pictureMs, s_run.nBlocks, s_run.w, cfg->frameBudgetMs,
                                most < ICO_BENCH_K_LEAST ? most : ICO_BENCH_K_LEAST, most)
                  : most;
    for (int bl = 0; bl < s_run.nBlocks && !stopped; bl++) {
        const int v = s_run.block[bl].variant;
        (void)rd_bench_apply(b, &s_var[v]); /* the threads idle first */
        collect(b);
        if (cb && cb->status) {
            snprintf(text, sizeof(text), "Measuring the picture: frame %d of %d, %d %%", frame,
                     frames, (bl * 100) / s_run.nBlocks);
            cb->status(cb->user, text);
        }
        for (int j = 0; j < s_run.w + s_run.k && !stopped; j++) {
            if ((stopped = stopNow(cfg, cb)) != 0) {
                break;
            }
            if (!post(b, bl, j >= s_run.w ? j - s_run.w : -1)) {
                stopped = RD_BENCH_LOST;
            }
        }
    }
    /* the last records in: three baseline replays more (not where the app
       leaves or the device is gone) */
    (void)rd_bench_apply(b, &s_var[0]);
    if (stopped != RD_BENCH_LEFT && stopped != RD_BENCH_LOST) {
        for (int j = 0; j < 3; j++) {
            if (!post(b, -1, -1)) {
                break;
            }
        }
    }
    rd_bench_settle();
    collect(b);
    rd_bench_switches_restore();
    (void)rd_bench_mask(&b->f, b->orig, b->cls, 0, 0); /* the commands whole again */
    emit(b, cfg, cb);
    out->ran = ico_bench_variants_ran(s_res, s_run.nVariants);
    out->stopped = stopped;
    out->ms = rd__now_ms() - t0;
    return stopped;
}

/* ------------------------------------------------------ the window's run */

int rd_bench_run_kept(const RdBenchKept *kept, int n, const char *tmpPath, const RdBenchRunCfg *cfg,
                      const RdBenchRunCb *cb, RdBenchRunOut *out, int *framesRun)
{
    static RdBenchFrame b;
    char text[256];
    int stopped = RD_BENCH_DONE;
    memset(out, 0, sizeof(*out));
    *framesRun = 0;
    const double t0 = rd__now_ms();
    RdBenchRunCfg c = *cfg;
    if (!(c.startMs > 0.0)) {
        c.startMs = t0; /* the cap from here */
    }
    for (int i = 0; i < n && stopped == RD_BENCH_DONE; i++) {
        if (!rd_bench_frame_load(&b, kept[i].bytes, kept[i].len, tmpPath)) {
            snprintf(text, sizeof(text),
                     "bench: frame %d of %d: %s (frame %u, %zu bytes) could not be loaded again "
                     "(the lines above say why): passed over",
                     i + 1, n, ico_bench_kind_name(kept[i].kind), kept[i].number, kept[i].len);
            say(cb, text);
            continue;
        }
        b.f.number = RD_BENCH_NUMBER_BASE + kept[i].number;
        RdBenchRunOut one;
        stopped = rd_bench_run_frame(&b, i + 1, n, kept[i].kind, &c, cb, &one);
        c.csvHeader = false; /* once a run */
        out->variants += one.variants;
        out->ran += one.ran;
        (*framesRun)++;
        rd_bench_frame_free(&b);
        if (stopped == RD_BENCH_FAILED) {
            stopped = RD_BENCH_DONE; /* that frame had nothing to run: the next one */
        }
    }
    out->stopped = stopped;
    out->ms = rd__now_ms() - t0;
    return stopped;
}

/* ------------------------------------------------- rd_replay_tool --bench */

static void toolLine(void *user, const char *text)
{
    (void)user;
    printf("%s\n", text);
}

static void toolCsv(void *user, const char *text)
{
    (void)user;
    printf("bench csv: %s\n", text);
}

/* one baseline replay presented, the output read into *px (grown) */
static bool toolPicture(RdBenchFrame *b, uint8_t **px, size_t *cap, uint32_t *w, uint32_t *h)
{
    if (!rd_bench_post(b)) {
        return false;
    }
    rd_bench_settle();
    const size_t need = (size_t)g_rd.presentOutW * g_rd.presentOutH * 4u;
    if (need == 0) {
        return false;
    }
    if (need > *cap) {
        uint8_t *g = realloc(*px, need);
        if (!g) {
            return false;
        }
        *px = g;
        *cap = need;
    }
    return rd_read_presented(*px, w, h);
}

int rd_bench_tool(const RdFrame *f, int k, bool present)
{
    static RdBenchFrame b;
    if (!present || !f) {
        printf("bench: --bench needs a present (--present WxH): the picture is compared\n");
        return 1;
    }
    if (!rd_bench_frame_adopt(&b, f)) {
        printf("bench: the frame could not be prepared (no memory)\n");
        return 1;
    }
    b.f.number = RD_BENCH_NUMBER_BASE + f->number;
    uint8_t *px[4] = {NULL, NULL, NULL, NULL};
    size_t cap[4] = {0, 0, 0, 0};
    uint32_t w[4] = {0}, h[4] = {0};
    RdBenchVariant base;
    memset(&base, 0, sizeof(base));
    (void)rd_bench_apply(&b, &base);
    bool ok = toolPicture(&b, &px[0], &cap[0], &w[0], &h[0]) &&
              toolPicture(&b, &px[1], &cap[1], &w[1], &h[1]);
    RdBenchRunCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.w = 1;
    cfg.k = k;
    cfg.rounds = 1;
    cfg.every = ICO_BENCH_EVERY;
    cfg.startMs = rd__now_ms();
    cfg.presentMode = "headless";
    cfg.csvHeader = true;
    RdBenchRunCb cb = {NULL, NULL, toolLine, toolCsv, NULL};
    RdBenchRunOut out;
    memset(&out, 0, sizeof(out));
    if (ok) {
        (void)rd_bench_run_frame(&b, 1, 1, ICO_BENCH_PAUSE, &cfg, &cb, &out);
        ok = toolPicture(&b, &px[2], &cap[2], &w[2], &h[2]) &&
             toolPicture(&b, &px[3], &cap[3], &w[3], &h[3]);
    }
    int rc = 0;
    if (!ok) {
        printf("bench: a baseline picture could not be presented or read\n");
        rc = 1;
    } else {
        printf("bench: %s; every variant ran: %d of %d\n", rd_bench_stop_words(out.stopped),
               out.ran, out.variants);
        if (out.stopped != RD_BENCH_DONE || out.ran != out.variants) {
            rc = 1;
        }
        const size_t bytes = (size_t)w[1] * h[1] * 4u;
        const bool steady = w[0] == w[1] && h[0] == h[1] && memcmp(px[0], px[1], bytes) == 0;
        if (!steady) {
            printf("bench: the frame's picture differs from one baseline replay to the next (a "
                   "feedback effect): the pictures before and after the bench not compared\n");
        } else if (w[3] != w[1] || h[3] != h[1] || memcmp(px[3], px[1], bytes) != 0) {
            size_t diff = 0;
            if (w[3] == w[1] && h[3] == h[1]) {
                for (size_t i = 0; i < bytes; i++) {
                    diff += px[3][i] != px[1][i];
                }
            }
            printf("bench: the picture after the bench differs from the one before it (%zu bytes "
                   "of %zu; %ux%u against %ux%u)\n",
                   diff, bytes, w[3], h[3], w[1], h[1]);
            rc = 1;
        } else {
            printf("bench: the picture after the bench: the same bytes as before it (%ux%u)\n",
                   w[1], h[1]);
        }
    }
    for (int i = 0; i < 4; i++) {
        free(px[i]);
    }
    rd_bench_frame_free(&b);
    return rc;
}
