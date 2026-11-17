/*
 * port/platform/test/last_run_test.c
 *
 * logs/last-run.txt (last_run.h, issue 215):
 *   layout   the slots' widths sum to LAST_RUN_BYTES, each value has room;
 *   trip     the text of a full set of values parsed back: every value and
 *            the facts (pid, seconds, phase with stage and tick, the
 *            background, the trim level, the resets, free memory with the
 *            lowest and the floor, the process's memory, the crash signal);
 *   cut      the file cut at every byte offset: the slots it holds whole
 *            read, the others marked cut, never a part of one;
 *   torn     a write stopped at every byte offset (the new text's first k
 *            bytes over the old): every slot is the new one's or the old
 *            one's whole, or marked torn, never a mix;
 *   lines    the previous run's lines word for word (a field, a run that
 *            ended without a word, a cut and a torn field);
 *   rules    the phase a step moves to, the stage a milestone names, the
 *            UTC text, the system's summary within 128 bytes, the paths;
 *   run      the run's own file through its calls (a step before the file,
 *            the start-up set and its own steps, a self-test, the game, the
 *            first picture, play with the stage and the beat's tick, the
 *            background, a trim, the resets, the GPU memory, the memory
 *            figures, the activity note between heartbeats, the system's
 *            summary handed over by the next beat only, once a phase
 *            change), the crash's
 *            end (the async-signal-safe write), the next start's lines and
 *            its copy to last-run-previous.txt, a run that ended without a
 *            word, the clean end after a loss, a run without the file
 *            (its first picture still told once).
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "host_fs.h"
#include "last_run.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

static const char *const kA[LAST_RUN_SLOTS] = {
    LAST_RUN_FORMAT " (a test)",
    "v0.11.7 (commit v0.11.6-183-g66d9d0dcba)",
    "4242",
    "2026-11-16 20:56:01 UTC",
    "14.8",
    "play: stage 1, Main tick 4 (since 14.2 s)",
    "in the background since 12.5 s",
    "level 15 (running critical) at 3.9 s, 1 in all",
    "47 at start, 66 now (+19)",
    ("1911 MB now of 11265 MB, lowest 1897 MB at 4.1 s, floor 1915 MB; file cache 2104, anon "
     "3012, kernel 640, GPU 4210, rest 980, swap used 1200 MB"),
    "251 MB now, peak 260 MB",
    "kernel 12 MB",
    "start-up set 290 of 684; 27 made as the frames needed them; the last pipeline note: none",
    "crash (signal 11)",
};

static const char *const kB[LAST_RUN_SLOTS] = {
    LAST_RUN_FORMAT " (another test)",
    "v0.11.8 (commit v0.11.7-12-gabcdef0123)",
    "77",
    "2026-11-16 21:00:00 UTC",
    "2.0",
    "the start-up set: pipeline 160 of 684 (since 0.9 s)",
    "in front since 0.0 s",
    "none",
    "66 at start, 66 now (+0)",
    "5059 MB now of 11265 MB, lowest 5059 MB at 2.0 s, floor 1915 MB",
    "232 MB now, peak 232 MB",
    "not read yet (the sampler starts with the window)",
    ("start-up set 157 of 684; 0 made as the frames needed them; the last pipeline note: "
     "creating pipeline 160 of 684 (in batch 158-161)"),
    "running (nothing written at an end: a run that stops here ended without a word)",
};

static char s_a[LAST_RUN_BYTES], s_b[LAST_RUN_BYTES], s_mix[LAST_RUN_BYTES];
static LastRunFacts s_f, s_g;

static void layout(void)
{
    size_t sum = 0;
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        const size_t w = last_run_slot_width(s);
        CHECK(last_run_slot_offset(s) == sum);
        /* room for the key, ": ", a value and the tail; the value fits the
           facts' buffer */
        CHECK(w > strlen(last_run_slot_key(s)) + 2 + LAST_RUN_TAIL + 8);
        CHECK(w - LAST_RUN_TAIL - strlen(last_run_slot_key(s)) - 2 < LAST_RUN_VALUE_MAX);
        sum += w;
    }
    CHECK(sum == LAST_RUN_BYTES);
    /* every test value fits its slot (none is cut) */
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        const size_t room =
            last_run_slot_width(s) - LAST_RUN_TAIL - strlen(last_run_slot_key(s)) - 2;
        CHECK(strlen(kA[s]) <= room && strlen(kB[s]) <= room);
    }
}

static void trip(void)
{
    last_run_text(kA, s_a);
    last_run_text(kB, s_b);
    /* each slot one line, its check at its end */
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        const char *p = s_a + last_run_slot_offset(s);
        const size_t w = last_run_slot_width(s);
        CHECK(p[w - 1] == '\n' && memchr(p, '\n', w - 1) == NULL);
        CHECK(p[w - LAST_RUN_TAIL] == ' ' && p[w - LAST_RUN_TAIL + 1] == '[' && p[w - 2] == ']');
    }
    CHECK(last_run_parse(s_a, sizeof s_a, &s_f) == LAST_RUN_SLOTS);
    CHECK(s_f.found);
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        CHECK(s_f.state[s] == LAST_RUN_SLOT_READ && text_is(s_f.value[s], kA[s]));
    }
    CHECK(s_f.pid == 4242 && s_f.seconds > 14.79 && s_f.seconds < 14.81);
    CHECK(s_f.phase == LAST_RUN_PHASE_PLAY && s_f.stage == 1 && s_f.tick == 4);
    CHECK(s_f.background == 1 && s_f.trimLevel == 15);
    CHECK(s_f.resetsStart == 47 && s_f.resetsNow == 66);
    CHECK(s_f.freeMb == 1911 && s_f.freeLowMb == 1897 && s_f.floorMb == 1915);
    CHECK(s_f.rssMb == 251 && s_f.peakMb == 260);
    CHECK(s_f.end == LAST_RUN_END_CRASH && s_f.endSignal == 11);
    CHECK(last_run_parse(s_b, sizeof s_b, &s_g) == LAST_RUN_SLOTS);
    CHECK(s_g.phase == LAST_RUN_PHASE_SET && s_g.stage == -1 && s_g.tick == -1);
    CHECK(s_g.background == 0 && s_g.trimLevel == -1 && s_g.end == LAST_RUN_END_RUNNING);
    CHECK(s_g.endSignal == -1 && s_g.resetsStart == 66 && s_g.resetsNow == 66);
    /* a value with a newline: one line still; a value too long: cut */
    {
        char slot[512], big[1000];
        memset(big, 'x', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        last_run_slot_build(LAST_RUN_END, "a\nb", slot);
        CHECK(memcmp(slot, "end: a b ", 9) == 0);
        last_run_slot_build(LAST_RUN_PHASE, big, slot);
        CHECK(slot[last_run_slot_width(LAST_RUN_PHASE) - 1] == '\n' &&
              slot[last_run_slot_width(LAST_RUN_PHASE) - LAST_RUN_TAIL - 1] == 'x');
    }
    /* another format's header: not found */
    {
        const char *v[LAST_RUN_SLOTS];
        memcpy(v, kA, sizeof v);
        v[LAST_RUN_HEADER] = "format 2";
        last_run_text(v, s_mix);
        (void)last_run_parse(s_mix, sizeof s_mix, &s_g);
        CHECK(!s_g.found);
    }
    /* nothing */
    CHECK(last_run_parse(NULL, 0, &s_g) == 0 && !s_g.found && s_g.state[0] == LAST_RUN_SLOT_CUT);
}

static void cut(void)
{
    for (size_t k = 0; k <= LAST_RUN_BYTES; k++) {
        int ok = 1;
        (void)last_run_parse(s_a, k, &s_g);
        for (int s = 0; s < LAST_RUN_SLOTS; s++) {
            const size_t end = last_run_slot_offset(s) + last_run_slot_width(s);
            if (end <= k) {
                ok &= s_g.state[s] == LAST_RUN_SLOT_READ && strcmp(s_g.value[s], kA[s]) == 0;
            } else {
                ok &= s_g.state[s] == LAST_RUN_SLOT_CUT && s_g.value[s][0] == '\0';
            }
        }
        if (!ok) {
            printf("cut at %zu\n", k);
        }
        CHECK(ok);
    }
}

static void torn(void)
{
    unsigned tornN = 0;
    for (size_t k = 0; k <= LAST_RUN_BYTES; k++) {
        int ok = 1;
        memcpy(s_mix, s_b, k);
        memcpy(s_mix + k, s_a + k, LAST_RUN_BYTES - k);
        (void)last_run_parse(s_mix, sizeof s_mix, &s_g);
        for (int s = 0; s < LAST_RUN_SLOTS; s++) {
            const size_t off = last_run_slot_offset(s), end = off + last_run_slot_width(s);
            if (end <= k) {
                ok &= s_g.state[s] == LAST_RUN_SLOT_READ && strcmp(s_g.value[s], kB[s]) == 0;
            } else if (off >= k) {
                ok &= s_g.state[s] == LAST_RUN_SLOT_READ && strcmp(s_g.value[s], kA[s]) == 0;
            } else if (s_g.state[s] == LAST_RUN_SLOT_READ) {
                /* the bytes of both agree up to k: one of them whole */
                ok &= strcmp(s_g.value[s], kA[s]) == 0 || strcmp(s_g.value[s], kB[s]) == 0;
            } else {
                ok &= s_g.state[s] == LAST_RUN_SLOT_TORN;
                tornN++;
            }
        }
        if (!ok) {
            printf("torn at %zu\n", k);
        }
        CHECK(ok);
    }
    /* the mixes were seen torn (every slot's values differ between A and
       B, so a write stopped inside a slot past its first differing byte is
       torn: most offsets) */
    CHECK(tornN > LAST_RUN_BYTES / 2);
}

static void lines(void)
{
    char line[1024];
    CHECK(last_run_previous_line(&s_f, LAST_RUN_HEADER, line, sizeof line) == 0);
    CHECK(last_run_previous_line(&s_f, LAST_RUN_PHASE, line, sizeof line) == 1);
    CHECK(text_is(line, "the previous run: phase: play: stage 1, Main tick 4 (since 14.2 s)"));
    (void)last_run_previous_line(&s_f, LAST_RUN_END, line, sizeof line);
    CHECK(text_is(line, "the previous run: end: crash (signal 11)"));
    (void)last_run_previous_line(&s_f, LAST_RUN_RESETS, line, sizeof line);
    CHECK(text_is(line, "the previous run: GPU resets: 47 at start, 66 now (+19)"));
    (void)last_run_parse(s_b, sizeof s_b, &s_g);
    (void)last_run_previous_line(&s_g, LAST_RUN_END, line, sizeof line);
    CHECK(text_is(line, "the previous run: end: none written: the run ended without a word (no "
                        "clean end, crash, device loss or watchdog end was written; the fields "
                        "above are its last write, 2.0 s after its start)"));
    /* cut inside the phase's slot */
    (void)last_run_parse(s_a, last_run_slot_offset(LAST_RUN_PHASE) + 10, &s_g);
    (void)last_run_previous_line(&s_g, LAST_RUN_PHASE, line, sizeof line);
    CHECK(text_is(line, "the previous run: phase: not readable (the file ends before it)"));
    /* torn inside it: B's first 10 bytes of the slot over A's */
    {
        const size_t k = last_run_slot_offset(LAST_RUN_PHASE) + 10;
        memcpy(s_mix, s_b, k);
        memcpy(s_mix + k, s_a + k, LAST_RUN_BYTES - k);
        (void)last_run_parse(s_mix, sizeof s_mix, &s_g);
        (void)last_run_previous_line(&s_g, LAST_RUN_PHASE, line, sizeof line);
        CHECK(text_is(line, "the previous run: phase: not readable (a write the run did not "
                            "finish)"));
        CHECK(s_g.phase == LAST_RUN_PHASE_UNKNOWN);
    }
}

static void rules(void)
{
    char t[160], cur[64], prev[64];
    CHECK(last_run_phase_for_step("window: start: the fog self-test", LAST_RUN_PHASE_STARTING, 0,
                                  0) == LAST_RUN_PHASE_SELFTEST);
    CHECK(last_run_phase_for_step("window: start: the pipeline set", LAST_RUN_PHASE_SELFTEST, 0,
                                  0) == LAST_RUN_PHASE_STARTING);
    CHECK(last_run_phase_for_step("window: start: x", LAST_RUN_PHASE_STARTING, 1, 0) ==
          LAST_RUN_PHASE_SELFTEST);
    CHECK(last_run_phase_for_step("window: shaders: 171 of 684 made in 1.0 s", LAST_RUN_PHASE_SET,
                                  0, 1) == LAST_RUN_PHASE_SET);
    CHECK(last_run_phase_for_step("window: shaders: the start-up set is over, 290 made in 4.3 s",
                                  LAST_RUN_PHASE_SET, 0, 0) == LAST_RUN_PHASE_STARTING);
    CHECK(last_run_phase_for_step("window: start: up in 12.3 s; the game runs",
                                  LAST_RUN_PHASE_SELFTEST, 0, 0) == LAST_RUN_PHASE_GAME);
    CHECK(last_run_phase_for_step("window: start: the fog self-test", LAST_RUN_PHASE_PLAY, 1, 0) ==
          LAST_RUN_PHASE_PLAY);
    CHECK(last_run_milestone_stage("first Main tick done (stage_no 1)") == 1);
    CHECK(last_run_milestone_stage("stage_no -1 -> 12") == 12);
    CHECK(last_run_milestone_stage("stage_no 3 -> -1") == -1);
    CHECK(last_run_milestone_stage("menu screen -1 -> 0") == -2);
    CHECK(last_run_milestone_stage("thread #14 created by #8 StageManager") == -2);
    last_run_utc_text(0, t, sizeof t);
    CHECK(text_is(t, "1970-01-01 00:00:00 UTC"));
    last_run_utc_text(1794862561ll, t, sizeof t);
    CHECK(text_is(t, "2026-11-16 20:56:01 UTC"));
    last_run_utc_text(951782400ll, t, sizeof t);
    CHECK(text_is(t, "2000-02-29 00:00:00 UTC"));
    last_run_utc_text(4102444799ll, t, sizeof t);
    CHECK(text_is(t, "2099-12-31 23:59:59 UTC"));
    {
        char sum[LAST_RUN_SUMMARY_MAX], big[400];
        last_run_summary_text("play: stage 1, Main tick 4 (since 14.2 s)", 47, 66, 1911, 1897, sum,
                              sizeof sum);
        CHECK(text_is(sum,
                      "ico-pc play: stage 1, Main tick 4 (since 14.2 s) | resets 47->66 | free "
                      "1911 MB, lowest 1897"));
        last_run_summary_text("starting", -1, -1, -1, -1, sum, sizeof sum);
        CHECK(text_is(sum, "ico-pc starting | resets unread | free unknown"));
        /* a phase longer than the room: cut, the figures kept, 128 bytes */
        memset(big, 'p', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        last_run_summary_text(big, 47, 66, 1911, 1897, sum, sizeof sum);
        CHECK(strlen(sum) == LAST_RUN_SUMMARY_MAX - 1);
        CHECK(strstr(sum, "pp | resets 47->66 | free 1911 MB, lowest 1897") != NULL);
        CHECK(strncmp(sum, "ico-pc pp", 9) == 0);
    }
    CHECK(last_run_paths("logs/startup-activity.txt", cur, prev, sizeof cur) == 0);
    CHECK(text_is(cur, "logs/last-run.txt") && text_is(prev, "logs/last-run-previous.txt"));
    CHECK(last_run_paths("C:\\g\\logs\\startup-activity.txt", cur, prev, sizeof cur) == 0);
    CHECK(text_is(cur, "C:\\g\\logs\\last-run.txt"));
    CHECK(last_run_paths("x.act", cur, prev, sizeof cur) == 0 && text_is(cur, "last-run.txt"));
    CHECK(last_run_paths("logs/startup-activity.txt", cur, prev, 20) == -1);
}

/* the log callback's lines, kept in order */
static char s_log[64 * 1024];

static void log_line(const char *line)
{
    const size_t n = strlen(s_log);
    snprintf(s_log + n, sizeof s_log - n, "%s\n", line);
}

/* the file now, read whole: its size, parsed into *f */
/* the system's summary hook: its calls and the last text */
static int s_sumCalls;
static char s_sumLast[LAST_RUN_SUMMARY_MAX];

static void sum_hook(const char *text)
{
    s_sumCalls++;
    snprintf(s_sumLast, sizeof s_sumLast, "%s", text);
}

static size_t read_file(const char *path, char *buf, size_t cap, LastRunFacts *f)
{
    FILE *fp = ico_fopen(path, "rb");
    size_t n = 0;
    if (fp != NULL) {
        n = fread(buf, 1, cap, fp);
        fclose(fp);
    }
    (void)last_run_parse(buf, n, f);
    return n;
}

static void run(const char *base)
{
    static char buf[LAST_RUN_BYTES + 64], before[LAST_RUN_BYTES + 64];
    char dir[512], act[600], cur[700], prev[700];
    LastRunBeat b;
    size_t n;

    snprintf(dir, sizeof dir, "%s/last_run_test_logs", base);
    (void)ico_remove_tree(dir);
    CHECK(ico_mkdir(dir) == 0);
    snprintf(act, sizeof act, "%s/startup-activity.txt", dir);
    snprintf(cur, sizeof cur, "%s/last-run.txt", dir);
    snprintf(prev, sizeof prev, "%s/last-run-previous.txt", dir);

    /* a step before the file: its first write has it */
    last_run_step("window: start: creating the vulkan window");
    s_log[0] = '\0';
    last_run_begin(act, "v0.11.8 (commit test)", log_line);
    CHECK(strcmp(last_run_path(), cur) == 0);
    CHECK(text_is(s_log, "the previous run: no last-run.txt (the first start with it, or the file "
                         "was deleted)\n"));
    n = read_file(cur, buf, sizeof buf, &s_f);
    CHECK(n == LAST_RUN_BYTES && s_f.found && s_f.slots == LAST_RUN_SLOTS);
    CHECK(s_f.phase == LAST_RUN_PHASE_STARTING &&
          strncmp(s_f.value[LAST_RUN_PHASE], "starting: creating the vulkan window (since ", 44) ==
              0);
    CHECK(text_is(s_f.value[LAST_RUN_VERSION], "v0.11.8 (commit test)"));
    CHECK(s_f.end == LAST_RUN_END_RUNNING && s_f.background == 0 && s_f.trimLevel == -1);
    CHECK(text_is(s_f.value[LAST_RUN_FREE], "unknown on this system"));
#ifndef _WIN32
    CHECK(s_f.pid == (long)getpid());
#endif
#ifndef __ANDROID__
    CHECK(text_is(s_f.value[LAST_RUN_RESETS],
                  "not available on this system (Android's kernel GPU driver only)"));
    CHECK(s_f.resetsStart == -1);
#endif
    CHECK(strncmp(s_f.value[LAST_RUN_STARTED], "20", 2) == 0 &&
          strstr(s_f.value[LAST_RUN_STARTED], " UTC") != NULL);

    /* the start-up set, its own step, its end, a self-test */
    last_run_set_pipelines(5, 684);
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_SET &&
          strncmp(s_f.value[LAST_RUN_PHASE], "the start-up set: pipeline 5 of 684 (since ", 43) ==
              0);
    last_run_step("window: shaders: 171 of 684 made in 1.0 s");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_SET &&
          strstr(s_f.value[LAST_RUN_PHASE], "; the last step: window: shaders: 171 of 684 made "
                                            "in 1.0 s") != NULL);
    last_run_set_pipelines(290, 0);
    last_run_step("window: start: the fog self-test");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_SELFTEST &&
          strncmp(s_f.value[LAST_RUN_PHASE], "a self-test: the fog self-test (since ", 38) == 0);
    CHECK(strstr(s_f.value[LAST_RUN_PHASE], "the last step") == NULL);

    /* the system's figures */
    last_run_set_gpu_resets_start(47, NULL);
    last_run_set_gpu_resets(66);
    last_run_set_gpu_memory("kernel 12 MB");
    last_run_trim(15, "running critical");
    last_run_background(1);
    memset(&b, 0, sizeof b);
    b.ticks = 4;
    b.pipesMade = 27;
    b.note = "creating pipeline a";
    b.rssKb = 251 * 1024;
    b.peakKb = 260 * 1024;
    b.availKb = 1911 * 1024;
    b.totalKb = 11265 * 1024;
    b.floorMb = 1915;
    last_run_beat(&b);
    b.availKb = 1897 * 1024;
    last_run_beat(&b);
    b.availKb = 1903 * 1024;
    /* the system's split after the free figure (mem_breakdown.h, issue 215) */
    snprintf(b.memText, sizeof b.memText,
             "file cache 2104, anon 3012, kernel 640, GPU ?, rest 5190, swap used 1200 MB");
    last_run_beat(&b);

    /* the system's summary: set at a phase change (on Android it can come
       on the game's fiber stack), handed to the hook by the next beat (the
       heartbeat's thread) only, once */
    last_run_set_summary_hook(sum_hook);
    CHECK(s_sumCalls == 0);
    last_run_beat(&b);
    CHECK(s_sumCalls == 1 && strncmp(s_sumLast, "ico-pc a self-test: the fog self-test", 37) == 0);
    s_sumCalls = 0;

    /* the game, its first picture, play */
    last_run_step("window: start: up in 12.3 s; the game runs");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_GAME);
    CHECK(s_sumCalls == 0);
    last_run_beat(&b);
    CHECK(s_sumCalls == 1 && strncmp(s_sumLast, "ico-pc the game", 15) == 0 &&
          strstr(s_sumLast, " | resets 47->66 | free 1903 MB, lowest 1897") != NULL);
    last_run_beat(&b);
    CHECK(s_sumCalls == 1);
    last_run_set_summary_hook(NULL);
    last_run_present();
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_FIRST_PICTURE);
    last_run_milestone("menu screen -1 -> 0"); /* not a stage: nothing */
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_FIRST_PICTURE);
    last_run_milestone("first Main tick done (stage_no 1)");
    last_run_present(); /* past the first picture: nothing */
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_PLAY && s_f.stage == 1 && s_f.tick == 4);
    CHECK(strncmp(s_f.value[LAST_RUN_PHASE], "play: stage 1, Main tick 4 (since ", 34) == 0);
    CHECK(s_f.background == 1 &&
          strncmp(s_f.value[LAST_RUN_APP], "in the background since ", 24) == 0);
    CHECK(s_f.trimLevel == 15 &&
          strncmp(s_f.value[LAST_RUN_TRIM], "level 15 (running critical) at ", 31) == 0 &&
          strstr(s_f.value[LAST_RUN_TRIM], " s, 1 in all") != NULL);
    CHECK(s_f.resetsStart == 47 && s_f.resetsNow == 66 &&
          text_is(s_f.value[LAST_RUN_RESETS], "47 at start, 66 now (+19)"));
    CHECK(s_f.freeMb == 1903 && s_f.freeLowMb == 1897 && s_f.floorMb == 1915);
    CHECK(strncmp(s_f.value[LAST_RUN_FREE], "1903 MB now of 11265 MB, lowest 1897 MB at ", 43) ==
              0 &&
          strstr(s_f.value[LAST_RUN_FREE],
                 " s, floor 1915 MB; file cache 2104, anon 3012, "
                 "kernel 640, GPU ?, rest 5190, swap used 1200 MB") != NULL);
    CHECK(s_f.rssMb == 251 && s_f.peakMb == 260);
    CHECK(text_is(s_f.value[LAST_RUN_PROCESS], "251 MB now, peak 260 MB"));
    CHECK(text_is(s_f.value[LAST_RUN_GPU_MEMORY], "kernel 12 MB"));
    CHECK(text_is(s_f.value[LAST_RUN_PIPELINES], "start-up set 290 of 684; 27 made as the frames "
                                                 "needed them; the last pipeline note: creating "
                                                 "pipeline a"));
    CHECK(s_f.end == LAST_RUN_END_RUNNING);
    /* a step in play: the phase stays, the step beside it */
    last_run_step("window: switch: the vulkan renderer");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_PLAY &&
          strstr(s_f.value[LAST_RUN_PHASE], "; the last step: window: switch: the vulkan "
                                            "renderer") != NULL);

    /* the activity note between two heartbeats: the next write has it */
    last_run_set_note("creating pipeline b");
    last_run_milestone("stage_no 1 -> 2");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.phase == LAST_RUN_PHASE_PLAY && s_f.stage == 2 &&
          strstr(s_f.value[LAST_RUN_PIPELINES], "the last pipeline note: creating pipeline b") !=
              NULL);
    CHECK(strstr(s_f.value[LAST_RUN_PHASE], "the last step") == NULL);

    /* the crash's end, then the next start */
    last_run_end_crash(11);
    n = read_file(cur, before, sizeof before, &s_f);
    CHECK(n == LAST_RUN_BYTES);
#ifndef _WIN32
    CHECK(s_f.end == LAST_RUN_END_CRASH && s_f.endSignal == 11 &&
          text_is(s_f.value[LAST_RUN_END], "crash (signal 11)"));
#else
    CHECK(s_f.end == LAST_RUN_END_CRASH &&
          text_is(s_f.value[LAST_RUN_END], "crash (exception 0xb)"));
#endif
    s_log[0] = '\0';
    last_run_begin(act, "v0.11.8 (commit test)", log_line);
    CHECK(strstr(s_log, "the previous run: version: v0.11.8 (commit test)\n") != NULL);
    CHECK(strstr(s_log, "the previous run: phase: play: stage 2, Main tick 4 (since ") != NULL);
    CHECK(strstr(s_log, "the previous run: GPU resets: 47 at start, 66 now (+19)\n") != NULL);
    CHECK(strstr(s_log, "the previous run: GPU memory: kernel 12 MB\n") != NULL);
#ifndef _WIN32
    CHECK(strstr(s_log, "the previous run: end: crash (signal 11)\n") != NULL);
#endif
    {
        /* one line a field (the header has none) */
        unsigned lines_ = 0;
        for (const char *p = s_log; (p = strchr(p, '\n')) != NULL; p++) {
            lines_++;
        }
        CHECK(lines_ == LAST_RUN_SLOTS - 1);
    }
    /* the copy is the file the crash left, byte for byte */
    {
        static char copy[LAST_RUN_BYTES + 64];
        FILE *fp = ico_fopen(prev, "rb");
        size_t m = 0;
        if (fp != NULL) {
            m = fread(copy, 1, sizeof copy, fp);
            fclose(fp);
        }
        CHECK(m == LAST_RUN_BYTES && memcmp(copy, before, LAST_RUN_BYTES) == 0);
    }
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.end == LAST_RUN_END_RUNNING); /* this run's end is still to come */

    /* a run that ends without a word: the next start says so */
    s_log[0] = '\0';
    last_run_begin(act, "v0.11.8 (commit test)", log_line);
    CHECK(strstr(s_log, "the previous run: end: none written: the run ended without a word (no "
                        "clean end, crash, device loss or watchdog end was written; the fields "
                        "above are its last write, ") != NULL);

    /* a loss, then the clean end; back in front */
    last_run_end_words("graphics device lost");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.end == LAST_RUN_END_LOSS);
    last_run_background(0);
    last_run_end_clean();
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.end == LAST_RUN_END_CLEAN &&
          text_is(s_f.value[LAST_RUN_END], "clean, after: graphics device lost"));
    CHECK(s_f.background == 0 && strncmp(s_f.value[LAST_RUN_APP], "in front since ", 15) == 0);
    last_run_end_clean();
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(text_is(s_f.value[LAST_RUN_END], "clean"));
    last_run_end_words("the watchdog: no Main tick for 60 s");
    (void)read_file(cur, buf, sizeof buf, &s_f);
    CHECK(s_f.end == LAST_RUN_END_WATCHDOG);

    /* no file: an older copy is not left for a report */
    (void)ico_remove(cur);
    s_log[0] = '\0';
    last_run_begin(act, "v0.11.8 (commit test)", log_line);
    CHECK(ico_path_kind(prev, NULL, NULL) < 0);
    CHECK(strstr(s_log, "the previous run: no last-run.txt") != NULL);

    /* a file that cannot be opened (no path): the first picture is still
       told once (set_size_first_picture's memory line) */
    last_run_begin(NULL, "v0.11.8 (commit test)", log_line);
    CHECK(last_run_path()[0] == '\0');
    last_run_step("window: start: up in 1.0 s; the game runs");
    CHECK(last_run_present() == 1);
    CHECK(last_run_present() == 0);
    (void)ico_remove_tree(dir);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: last_run_test <folder>\n");
        return 2;
    }
    layout();
    trip();
    cut();
    torn();
    lines();
    rules();
    run(argv[1]);
    if (fails != 0) {
        printf("last_run_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("last_run_test: ok\n");
    return 0;
}
