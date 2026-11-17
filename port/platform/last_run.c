/*
 * port/platform/last_run.c
 *
 * logs/last-run.txt (last_run.h): the slots, their check, the parse and
 * the previous run's lines (pure), then the run's own file.
 */
#ifndef _WIN32
#define _GNU_SOURCE 1
#endif

#include "last_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diag_host.h"
#include "host_fs.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* --- The slots ---------------------------------------------------------------- */

static const char *const s_key[LAST_RUN_SLOTS] = {
    "ico-pc last run",
    "version",
    "pid",
    "started",
    "seconds since start",
    "phase",
    "app",
    "last trim signal",
    "GPU resets",
    "free memory",
    "process memory",
    "GPU memory",
    "pipelines",
    "end",
};

/* the widths: LAST_RUN_BYTES in all (checked by last_run_test) */
static const unsigned short s_width[LAST_RUN_SLOTS] = {
    80, 160, 32, 64, 48, 320, 80, 112, 112, 208, 80, 192, 440, 192,
};

const char *last_run_slot_key(int slot)
{
    return slot >= 0 && slot < LAST_RUN_SLOTS ? s_key[slot] : "";
}

size_t last_run_slot_width(int slot)
{
    return slot >= 0 && slot < LAST_RUN_SLOTS ? s_width[slot] : 0;
}

size_t last_run_slot_offset(int slot)
{
    size_t off = 0;
    for (int i = 0; i < slot && i < LAST_RUN_SLOTS; i++) {
        off += s_width[i];
    }
    return off;
}

/* the check of a slot's bytes before its tail, its index mixed in (a slot
   moved to another place fails it): 32-bit FNV-1a */
static uint32_t slot_check(int slot, const char *p, size_t n)
{
    uint32_t h = 2166136261u ^ (uint32_t)slot;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 16777619u;
    }
    return h;
}

static const char s_hex[] = "0123456789abcdef";

void last_run_slot_build(int slot, const char *value, char *out)
{
    const size_t w = last_run_slot_width(slot);
    const char *key = last_run_slot_key(slot);
    size_t k = 0;
    uint32_t c;
    if (w == 0) {
        return;
    }
    for (size_t i = 0; key[i] != '\0' && k < w - LAST_RUN_TAIL; i++) {
        out[k++] = key[i];
    }
    if (k + 2 <= w - LAST_RUN_TAIL) {
        out[k++] = ':';
        out[k++] = ' ';
    }
    for (size_t i = 0; value != NULL && value[i] != '\0' && k < w - LAST_RUN_TAIL; i++) {
        const char ch = value[i];
        out[k++] = ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch;
    }
    while (k < w - LAST_RUN_TAIL) {
        out[k++] = ' ';
    }
    c = slot_check(slot, out, w - LAST_RUN_TAIL);
    out[k++] = ' ';
    out[k++] = '[';
    for (int sh = 28; sh >= 0; sh -= 4) {
        out[k++] = s_hex[(c >> sh) & 15u];
    }
    out[k++] = ']';
    out[k] = '\n';
}

void last_run_text(const char *const values[LAST_RUN_SLOTS], char *out)
{
    size_t off = 0;
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        last_run_slot_build(s, values[s], out + off);
        off += s_width[s];
    }
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

/* 1 when the slot's bytes at p are whole: tail, check and key */
static int slot_whole(int slot, const char *p)
{
    const size_t w = s_width[slot];
    const char *t = p + w - LAST_RUN_TAIL;
    const size_t klen = strlen(s_key[slot]);
    uint32_t c = 0;
    if (t[0] != ' ' || t[1] != '[' || t[10] != ']' || t[11] != '\n') {
        return 0;
    }
    for (int i = 2; i < 10; i++) {
        const int d = hex_digit(t[i]);
        if (d < 0) {
            return 0;
        }
        c = (c << 4) | (uint32_t)d;
    }
    return c == slot_check(slot, p, w - LAST_RUN_TAIL) && memcmp(p, s_key[slot], klen) == 0 &&
           p[klen] == ':' && p[klen + 1] == ' ';
}

/* the phases' and the ends' words, in their enums' order */
static const char *const s_phase_words[LAST_RUN_PHASE_UNKNOWN] = {
    "starting", "the start-up set", "a self-test", "the game starting", "the first picture", "play",
};

static const char *const s_end_words[LAST_RUN_END_UNKNOWN] = {
    "running",
    "clean",
    "crash",
    "graphics device lost",
    "the graphics driver stopped responding",
    "the watchdog",
};

const char *last_run_phase_words(int phase)
{
    return phase >= 0 && phase < LAST_RUN_PHASE_UNKNOWN ? s_phase_words[phase] : "unknown";
}

/* the table's entry the text starts with (whole word: the end or ':' ',' ' '
   '(' after it), else unknown */
static int by_prefix(const char *text, const char *const *words, int n)
{
    for (int i = 0; i < n; i++) {
        const size_t k = strlen(words[i]);
        if (strncmp(text, words[i], k) == 0 &&
            (text[k] == '\0' || text[k] == ':' || text[k] == ',' || text[k] == ' ' ||
             text[k] == '(')) {
            return i;
        }
    }
    return n;
}

static long num_after(const char *text, const char *what)
{
    const char *p = strstr(text, what);
    if (p == NULL) {
        return -1;
    }
    p += strlen(what);
    if (*p < '0' || *p > '9') {
        return -1;
    }
    return strtol(p, NULL, 10);
}

static void facts_numbers(LastRunFacts *f)
{
    const char *v;
    if (f->state[LAST_RUN_PID] == LAST_RUN_SLOT_READ) {
        f->pid = strtol(f->value[LAST_RUN_PID], NULL, 10);
    }
    if (f->state[LAST_RUN_SECONDS] == LAST_RUN_SLOT_READ) {
        f->seconds = strtod(f->value[LAST_RUN_SECONDS], NULL);
    }
    if (f->state[LAST_RUN_PHASE] == LAST_RUN_SLOT_READ) {
        v = f->value[LAST_RUN_PHASE];
        f->phase = by_prefix(v, s_phase_words, LAST_RUN_PHASE_UNKNOWN);
        if (f->phase == LAST_RUN_PHASE_PLAY) {
            const long st = num_after(v, "stage ");
            f->stage = st >= 0 ? (int)st : -1;
            f->tick = num_after(v, "Main tick ");
        }
    }
    if (f->state[LAST_RUN_APP] == LAST_RUN_SLOT_READ) {
        v = f->value[LAST_RUN_APP];
        f->background = strncmp(v, "in the background", 17) == 0 ? 1
                        : strncmp(v, "in front", 8) == 0         ? 0
                                                                 : -1;
    }
    if (f->state[LAST_RUN_TRIM] == LAST_RUN_SLOT_READ) {
        const long lv = num_after(f->value[LAST_RUN_TRIM], "level ");
        f->trimLevel = lv >= 0 ? (int)lv : -1;
    }
    if (f->state[LAST_RUN_RESETS] == LAST_RUN_SLOT_READ) {
        int a, b;
        if (sscanf(f->value[LAST_RUN_RESETS], "%d at start, %d now", &a, &b) == 2) {
            f->resetsStart = a;
            f->resetsNow = b;
        }
    }
    if (f->state[LAST_RUN_FREE] == LAST_RUN_SLOT_READ) {
        v = f->value[LAST_RUN_FREE];
        if (v[0] >= '0' && v[0] <= '9') {
            f->freeMb = strtol(v, NULL, 10);
        }
        f->freeLowMb = num_after(v, "lowest ");
        f->floorMb = num_after(v, "floor ");
    }
    if (f->state[LAST_RUN_PROCESS] == LAST_RUN_SLOT_READ) {
        v = f->value[LAST_RUN_PROCESS];
        if (v[0] >= '0' && v[0] <= '9') {
            f->rssMb = strtol(v, NULL, 10);
        }
        f->peakMb = num_after(v, "peak ");
    }
    if (f->state[LAST_RUN_END] == LAST_RUN_SLOT_READ) {
        v = f->value[LAST_RUN_END];
        f->end = by_prefix(v, s_end_words, LAST_RUN_END_UNKNOWN);
        if (f->end == LAST_RUN_END_CRASH) {
            const long sg = num_after(v, "(signal ");
            f->endSignal = sg >= 0 ? (int)sg : -1;
        }
    }
}

int last_run_parse(const char *buf, size_t n, LastRunFacts *out)
{
    size_t off = 0;
    memset(out, 0, sizeof *out);
    out->pid = out->tick = out->freeMb = out->freeLowMb = out->floorMb = -1;
    out->rssMb = out->peakMb = -1;
    out->seconds = -1.0;
    out->phase = LAST_RUN_PHASE_UNKNOWN;
    out->stage = out->background = out->trimLevel = out->resetsStart = out->resetsNow = -1;
    out->end = LAST_RUN_END_UNKNOWN;
    out->endSignal = -1;
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        const size_t w = s_width[s];
        if (buf == NULL || off + w > n) {
            out->state[s] = LAST_RUN_SLOT_CUT;
        } else if (!slot_whole(s, buf + off)) {
            out->state[s] = LAST_RUN_SLOT_TORN;
        } else {
            const size_t start = strlen(s_key[s]) + 2;
            size_t len = w - LAST_RUN_TAIL - start;
            while (len > 0 && buf[off + start + len - 1] == ' ') {
                len--;
            }
            if (len >= LAST_RUN_VALUE_MAX) {
                len = LAST_RUN_VALUE_MAX - 1;
            }
            memcpy(out->value[s], buf + off + start, len);
            out->value[s][len] = '\0';
            out->state[s] = LAST_RUN_SLOT_READ;
            out->slots++;
        }
        off += w;
    }
    out->found =
        out->state[LAST_RUN_HEADER] == LAST_RUN_SLOT_READ &&
        strncmp(out->value[LAST_RUN_HEADER], LAST_RUN_FORMAT, sizeof LAST_RUN_FORMAT - 1) == 0;
    facts_numbers(out);
    return out->slots;
}

int last_run_previous_line(const LastRunFacts *f, int slot, char *out, size_t size)
{
    if (size == 0 || slot <= LAST_RUN_HEADER || slot >= LAST_RUN_SLOTS) {
        return 0;
    }
    if (f->state[slot] != LAST_RUN_SLOT_READ) {
        snprintf(out, size, "the previous run: %s: not readable (%s)", s_key[slot],
                 f->state[slot] == LAST_RUN_SLOT_CUT ? "the file ends before it"
                                                     : "a write the run did not finish");
    } else if (slot == LAST_RUN_END && f->end == LAST_RUN_END_RUNNING) {
        snprintf(out, size,
                 "the previous run: end: none written: the run ended without a word (no clean "
                 "end, crash, device loss or watchdog end was written; the fields above are its "
                 "last write, %.1f s after its start)",
                 f->seconds);
    } else {
        snprintf(out, size, "the previous run: %s: %s", s_key[slot], f->value[slot]);
    }
    return 1;
}

void last_run_utc_text(long long t, char *out, size_t size)
{
    /* days to the civil date (H. Hinnant's days_from_civil, inverted) */
    long long days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    long long secs = t - days * 86400;
    long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const long long y = (long long)yoe + era * 400 + (m <= 2 ? 1 : 0);
    snprintf(out, size, "%04lld-%02u-%02u %02lld:%02lld:%02lld UTC", y, m, d, secs / 3600,
             (secs / 60) % 60, secs % 60);
}

int last_run_phase_for_step(const char *step, int current, int selftest, int setOpen)
{
    if (strstr(step, "the game runs") != NULL) {
        return LAST_RUN_PHASE_GAME;
    }
    if (current >= LAST_RUN_PHASE_GAME && current < LAST_RUN_PHASE_UNKNOWN) {
        return current; /* a step in play (a renderer switch): the phase stays */
    }
    if (current == LAST_RUN_PHASE_SET && setOpen) {
        return current; /* the set's own steps ("window: shaders: 171 of 684 made") */
    }
    if (selftest || strstr(step, "self-test") != NULL) {
        return LAST_RUN_PHASE_SELFTEST;
    }
    return LAST_RUN_PHASE_STARTING;
}

int last_run_milestone_stage(const char *m)
{
    const char *p;
    static const char first[] = "first Main tick done (stage_no ";
    if (strncmp(m, first, sizeof first - 1) == 0) {
        p = m + sizeof first - 1;
    } else if (strncmp(m, "stage_no ", 9) == 0 && (p = strstr(m, " -> ")) != NULL) {
        p += 4;
    } else {
        return LAST_RUN_NO_STAGE;
    }
    if (!((*p >= '0' && *p <= '9') || *p == '-')) {
        return LAST_RUN_NO_STAGE;
    }
    return (int)strtol(p, NULL, 10);
}

void last_run_summary_text(const char *phase, int resetsStart, int resetsNow, long freeMb,
                           long lowMb, char *out, size_t size)
{
    char resets[48], free_[64];
    if (size == 0) {
        return;
    }
    if (resetsStart >= 0) {
        snprintf(resets, sizeof resets, "resets %d->%d", resetsStart,
                 resetsNow >= 0 ? resetsNow : resetsStart);
    } else {
        snprintf(resets, sizeof resets, "resets unread");
    }
    if (freeMb >= 0) {
        snprintf(free_, sizeof free_, "free %ld MB, lowest %ld", freeMb,
                 lowMb >= 0 ? lowMb : freeMb);
    } else {
        snprintf(free_, sizeof free_, "free unknown");
    }
    {
        /* the figures first in the room: the phase is cut, never them.  The
           whole text is made first (the tail at most 6 + 47 + 63 bytes),
           then cut by hand to the system's 128 bytes: the cut is the
           phase's alone */
        char tail[sizeof resets + sizeof free_ + 8];
        const int t = snprintf(tail, sizeof tail, " | %s | %s", resets, free_);
        const size_t tlen = t <= 0 ? 0 : (size_t)t < sizeof tail ? (size_t)t : sizeof tail - 1;
        const size_t cap = size < LAST_RUN_SUMMARY_MAX ? size : LAST_RUN_SUMMARY_MAX;
        const char *ph = phase != NULL ? phase : "";
        const size_t plen = strlen(ph);
        const size_t room = 7 + tlen < cap ? cap - 7 - tlen - 1 : 0;
        const size_t pk = plen < room ? plen : room;
        size_t k = 0;
        memcpy(out, "ico-pc ", 7 < cap - 1 ? 7 : cap - 1);
        k = 7 < cap - 1 ? 7 : cap - 1;
        if (k + pk + tlen <= cap - 1) {
            memcpy(out + k, ph, pk);
            k += pk;
            memcpy(out + k, tail, tlen);
            k += tlen;
        }
        out[k] = '\0';
    }
}

int last_run_paths(const char *activityPath, char *cur, char *prev, size_t size)
{
    size_t dir = strlen(activityPath);
    int a, b;
    while (dir > 0 && activityPath[dir - 1] != '/' && activityPath[dir - 1] != '\\') {
        dir--;
    }
    a = snprintf(cur, size, "%.*slast-run.txt", (int)dir, activityPath);
    b = snprintf(prev, size, "%.*slast-run-previous.txt", (int)dir, activityPath);
    return a > 0 && (size_t)a < size && b > 0 && (size_t)b < size ? 0 : -1;
}

/* --- The run's file ------------------------------------------------------------- */

#define PATH_BYTES 1024

static char s_path[PATH_BYTES];
#ifdef _WIN32
static HANDLE s_h = INVALID_HANDLE_VALUE;
#else
static int s_fd = -1;
#endif
/* one writer at a time (the heartbeat's thread, the main thread, the
   system's callbacks); the crash handler waits a bounded time, then writes */
static volatile int s_busy;
static char s_img[LAST_RUN_BYTES];

static char s_version[160];
static long s_pid;
static char s_started[40];
static volatile int s_phase = LAST_RUN_PHASE_STARTING;
static double s_phaseSince;
static char s_phaseDetail[200];
static char s_playStep[200];
/* the start-up set's size decision (last_run_set_size_words), "" none */
static char s_sizeWords[112]; /* set_size.h SET_SIZE_WORDS_MAX */
static int s_stage = -1;
static volatile int s_selftest;
static unsigned s_setDone, s_setTotal;
static volatile int s_setOpen; /* the set in progress (its count's total not 0) */
static volatile int s_bg;
static double s_bgSince;
static int s_trimLevel = -1;
static unsigned s_trimN;
static double s_trimAt;
static char s_trimName[48];
#ifdef __ANDROID__
static char s_resetsWhy[96] =
    "not read yet (the kernel GPU driver's sampler starts with the window)";
static char s_gpuMem[2][176] = {"not read yet (the sampler starts with the window)", ""};
#else
static char s_resetsWhy[96] = "not available on this system (Android's kernel GPU driver only)";
static char s_gpuMem[2][176] = {"not available on this system (Android's kernel GPU driver only)",
                                ""};
#endif
static volatile int32_t s_resetsStart = -1, s_resetsNow = -1;
static volatile int s_gpuMemAt; /* the copy the next reader takes */
static LastRunBeat s_beat;
static char s_note[LAST_RUN_VALUE_MAX];
static long s_lowMb = -1;
static double s_lowAt;
static char s_end[192];
static void (*volatile s_summary)(const char *summary);
/* the summary's text from the last phase change, handed to the hook by the
   heartbeat's thread (last_run_beat): a phase change can come on the game's
   fiber stack, where a Java call is not made (Android: the hook is a JNI
   call); under the lock */
static char s_sumText[LAST_RUN_SUMMARY_MAX];
static unsigned s_sumGen, s_sumGiven;

static void lock_take(void)
{
    while (__atomic_exchange_n(&s_busy, 1, __ATOMIC_ACQUIRE) != 0) {
#ifdef _WIN32
        SwitchToThread();
#else
        /* 0.1 ms (sched.h in this tree is the game's scheduler, not the
           C library's sched_yield) */
        const struct timespec ts = {0, 100000};
        nanosleep(&ts, NULL);
#endif
    }
}

static int lock_try(void)
{
    return __atomic_exchange_n(&s_busy, 1, __ATOMIC_ACQUIRE) == 0;
}

static void lock_give(void)
{
    __atomic_store_n(&s_busy, 0, __ATOMIC_RELEASE);
}

static int file_open(void)
{
#ifdef _WIN32
    return s_h != INVALID_HANDLE_VALUE;
#else
    return s_fd >= 0;
#endif
}

/* bytes at off; sync: to the disc. Async-signal-safe on POSIX. */
static void file_put(const char *p, size_t n, size_t off, int sync)
{
#ifdef _WIN32
    OVERLAPPED ov;
    DWORD done = 0;
    if (s_h == INVALID_HANDLE_VALUE) {
        return;
    }
    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)off;
    if (WriteFile(s_h, p, (DWORD)n, &done, &ov) && sync) {
        (void)FlushFileBuffers(s_h);
    }
#else
    if (s_fd < 0) {
        return;
    }
    if (pwrite(s_fd, p, n, (off_t)off) == (ssize_t)n && sync) {
        (void)fsync(s_fd);
    }
#endif
}

static void phase_value(char *out, size_t size)
{
    const int ph = s_phase;
    const char *w = last_run_phase_words(ph);
    if (ph == LAST_RUN_PHASE_SET) {
        snprintf(out, size, "%s: pipeline %u of %u (since %.1f s)", w, s_setDone, s_setTotal,
                 s_phaseSince);
    } else if (ph == LAST_RUN_PHASE_PLAY) {
        snprintf(out, size, "%s: stage %d, Main tick %u (since %.1f s)", w, s_stage, s_beat.ticks,
                 s_phaseSince);
    } else if (s_phaseDetail[0] != '\0') {
        snprintf(out, size, "%s: %s (since %.1f s)", w, s_phaseDetail, s_phaseSince);
    } else {
        snprintf(out, size, "%s (since %.1f s)", w, s_phaseSince);
    }
    if (s_sizeWords[0] != '\0') {
        /* issue 215: the start-up set's size decision, in every phase after
           it (before the last step, which the slot may cut) */
        const size_t n = strlen(out);
        snprintf(out + n, size - n, "; %s", s_sizeWords);
    }
    if (s_playStep[0] != '\0') {
        /* a step while the phase stays (a renderer switch in play, the
           set's own steps) */
        const size_t n = strlen(out);
        snprintf(out + n, size - n, "; the last step: %s", s_playStep);
    }
}

/* the image from the state (under the lock) */
static void compose(void)
{
    /* each value whole before its slot cuts it to the slot's room
       (last_run_slot_build: the file's fixed layout is the one intended
       cut; the pipeline note alone, up to 439 bytes, can reach it) */
    static char v[LAST_RUN_SLOTS][LAST_RUN_VALUE_MAX + 256];
    const char *vals[LAST_RUN_SLOTS];
    const LastRunBeat *b = &s_beat;
    snprintf(v[LAST_RUN_HEADER], sizeof v[0], "%s (rewritten in place while the game runs)",
             LAST_RUN_FORMAT);
    snprintf(v[LAST_RUN_VERSION], sizeof v[0], "%s", s_version);
    snprintf(v[LAST_RUN_PID], sizeof v[0], "%ld", s_pid);
    snprintf(v[LAST_RUN_STARTED], sizeof v[0], "%s", s_started);
    snprintf(v[LAST_RUN_SECONDS], sizeof v[0], "%.1f", ico_diag_uptime());
    phase_value(v[LAST_RUN_PHASE], sizeof v[0]);
    snprintf(v[LAST_RUN_APP], sizeof v[0], "%s since %.1f s",
             s_bg ? "in the background" : "in front", s_bgSince);
    if (s_trimN == 0) {
        snprintf(v[LAST_RUN_TRIM], sizeof v[0], "none");
    } else {
        snprintf(v[LAST_RUN_TRIM], sizeof v[0], "level %d (%s) at %.1f s, %u in all", s_trimLevel,
                 s_trimName, s_trimAt, s_trimN);
    }
    {
        const int32_t a = s_resetsStart, n = s_resetsNow >= 0 ? s_resetsNow : s_resetsStart;
        if (a >= 0) {
            snprintf(v[LAST_RUN_RESETS], sizeof v[0], "%d at start, %d now (%+d)", (int)a, (int)n,
                     (int)(n - a));
        } else {
            snprintf(v[LAST_RUN_RESETS], sizeof v[0], "%s", s_resetsWhy);
        }
    }
    if (b->availKb >= 0) {
        char floor_[48];
        if (b->floorMb > 0) {
            snprintf(floor_, sizeof floor_, "floor %ld MB", b->floorMb);
        } else {
            snprintf(floor_, sizeof floor_, "%s",
                     b->floorMb == 0 ? "the floor off" : "no floor on this system");
        }
        /* then the system's split (mem_breakdown.h, issue 215): what holds
           the memory the free figure misses */
        snprintf(v[LAST_RUN_FREE], sizeof v[0],
                 "%ld MB now of %ld MB, lowest %ld MB at %.1f s, %s%s%s", b->availKb / 1024,
                 b->totalKb / 1024, s_lowMb, s_lowAt, floor_, b->memText[0] != '\0' ? "; " : "",
                 b->memText);
    } else {
        snprintf(v[LAST_RUN_FREE], sizeof v[0], "unknown on this system");
    }
    if (b->rssKb >= 0) {
        snprintf(v[LAST_RUN_PROCESS], sizeof v[0], "%ld MB now, peak %ld MB", b->rssKb / 1024,
                 (b->peakKb >= 0 ? b->peakKb : b->rssKb) / 1024);
    } else {
        snprintf(v[LAST_RUN_PROCESS], sizeof v[0], "unknown on this system");
    }
    {
        const int at = __atomic_load_n(&s_gpuMemAt, __ATOMIC_ACQUIRE);
        snprintf(v[LAST_RUN_GPU_MEMORY], sizeof v[0], "%s", s_gpuMem[at & 1]);
    }
    snprintf(v[LAST_RUN_PIPELINES], sizeof v[0],
             "start-up set %u of %u; %u made as the frames needed them; the last pipeline note: "
             "%s",
             s_setDone, s_setTotal, b->pipesMade, s_note[0] != '\0' ? s_note : "none");
    snprintf(v[LAST_RUN_END], sizeof v[0], "%s",
             s_end[0] != '\0' ? s_end
                              : "running (nothing written at an end: a run that stops here ended "
                                "without a word)");
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        vals[s] = v[s];
    }
    last_run_text(vals, s_img);
}

/* the file rewritten from the state; sync for a phase change or an end */
static void write_all(int sync)
{
    if (!file_open()) {
        return;
    }
    if (sync) {
        lock_take();
    } else if (!lock_try()) {
        return; /* a phase change is writing: the next beat comes */
    }
    compose();
    file_put(s_img, sizeof s_img, 0, sync);
    lock_give();
}

/* the phase change's summary, kept for the next beat's hand-over */
static void summary(void)
{
    char ph[LAST_RUN_VALUE_MAX], text[LAST_RUN_SUMMARY_MAX];
    if (s_summary == NULL) {
        return;
    }
    phase_value(ph, sizeof ph);
    last_run_summary_text(ph, (int)s_resetsStart, (int)s_resetsNow,
                          s_beat.availKb >= 0 ? s_beat.availKb / 1024 : -1, s_lowMb, text,
                          sizeof text);
    lock_take();
    memcpy(s_sumText, text, sizeof s_sumText);
    s_sumGen++;
    lock_give();
}

/* on the heartbeat's thread (a thread of the host's, its own stack): the
   summary kept since the last hand-over, if any; a write in flight: the
   next beat */
static void summary_hand_over(void)
{
    void (*fn)(const char *) = s_summary;
    char text[LAST_RUN_SUMMARY_MAX];
    if (fn == NULL || !lock_try()) {
        return;
    }
    if (s_sumGen == s_sumGiven) {
        lock_give();
        return;
    }
    s_sumGiven = s_sumGen;
    memcpy(text, s_sumText, sizeof text);
    lock_give();
    text[sizeof text - 1] = '\0';
    fn(text);
}

static void phase_to(int phase, const char *detail)
{
    s_phase = phase;
    s_phaseSince = ico_diag_uptime();
    snprintf(s_phaseDetail, sizeof s_phaseDetail, "%s", detail != NULL ? detail : "");
    write_all(1);
    summary();
}

void last_run_begin(const char *activityPath, const char *version, void (*log)(const char *line))
{
    static char buf[LAST_RUN_BYTES + 1];
    static LastRunFacts f;
    static char line[LAST_RUN_VALUE_MAX + 160];
    char prev[PATH_BYTES];
    size_t n = 0;
    FILE *fp;
#ifdef _WIN32
    if (s_h != INVALID_HANDLE_VALUE) {
        CloseHandle(s_h);
        s_h = INVALID_HANDLE_VALUE;
    }
#else
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
#endif
    s_path[0] = '\0';
    if (activityPath == NULL || last_run_paths(activityPath, s_path, prev, sizeof s_path) != 0) {
        s_path[0] = '\0';
        return;
    }
    snprintf(s_version, sizeof s_version, "%s", version != NULL ? version : "");
    s_end[0] = '\0'; /* this run's end is still to come */
    /* the previous run's, before this run's write */
    fp = ico_fopen(s_path, "rb");
    if (fp != NULL) {
        n = fread(buf, 1, sizeof buf - 1, fp);
        fclose(fp);
    }
    if (fp == NULL) {
        (void)ico_remove(prev); /* an older run's copy would be taken for it */
        if (log != NULL) {
            log("the previous run: no last-run.txt (the first start with it, or the file was "
                "deleted)");
        }
    } else {
        FILE *cp = ico_fopen(prev, "wb");
        if (cp != NULL) {
            (void)fwrite(buf, 1, n, cp);
            (void)fclose(cp);
        }
        (void)last_run_parse(buf, n, &f);
        if (log != NULL && !f.found) {
            snprintf(line, sizeof line,
                     "the previous run: last-run.txt (%zu bytes) is not in %s; kept as "
                     "last-run-previous.txt",
                     n, LAST_RUN_FORMAT);
            log(line);
        }
        for (int s = LAST_RUN_HEADER + 1; log != NULL && f.found && s < LAST_RUN_SLOTS; s++) {
            if (last_run_previous_line(&f, s, line, sizeof line)) {
                log(line);
            }
        }
    }
    /* this run's */
#ifdef _WIN32
    s_pid = (long)GetCurrentProcessId();
    {
        wchar_t *w = ico_widen(s_path);
        if (w != NULL) {
            s_h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            free(w);
        }
    }
#else
    s_pid = (long)getpid();
    s_fd = open(s_path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
    last_run_utc_text((long long)time(NULL), s_started, sizeof s_started);
    if (!file_open()) {
        if (log != NULL) {
            /* the path cut to what the line holds (a path may be longer than the line) */
            snprintf(line, sizeof line, "last-run.txt: %.*s could not be opened; not written",
                     (int)(sizeof line - 64), s_path);
            log(line);
        }
        s_path[0] = '\0';
        return;
    }
    s_beat.availKb = s_beat.totalKb = s_beat.rssKb = s_beat.peakKb = s_beat.floorMb = -1;
    /* the phase a step before the file set (last_run_step), and its time */
    write_all(1);
    summary();
}

const char *last_run_path(void)
{
    return s_path;
}

void last_run_beat(const LastRunBeat *b)
{
    const long avail = b->availKb >= 0 ? b->availKb / 1024 : -1;
    if (s_path[0] == '\0') {
        return;
    }
    s_beat = *b;
    s_beat.note = NULL;
    if (b->note != NULL) {
        last_run_set_note(b->note);
    }
    if (avail >= 0 && (s_lowMb < 0 || avail < s_lowMb)) {
        s_lowMb = avail;
        s_lowAt = ico_diag_uptime();
    }
    write_all(0);
    summary_hand_over();
}

void last_run_set_note(const char *note)
{
    snprintf(s_note, sizeof s_note, "%s", note != NULL ? note : "");
}

void last_run_step(const char *step)
{
    static const char start[] = "window: start: ";
    const int cur = s_phase;
    const int ph = last_run_phase_for_step(step, cur, s_selftest, s_setOpen);
    const char *detail =
        strncmp(step, start, sizeof start - 1) == 0 ? step + sizeof start - 1 : step;
    if (s_path[0] == '\0') {
        /* before the file: the phase kept for its first write */
        s_phase = ph;
        snprintf(s_phaseDetail, sizeof s_phaseDetail, "%s", detail);
        s_phaseSince = ico_diag_uptime();
        return;
    }
    if (ph == cur && (ph >= LAST_RUN_PHASE_FIRST_PICTURE || ph == LAST_RUN_PHASE_SET)) {
        /* the phase stays: the step is named beside it */
        snprintf(s_playStep, sizeof s_playStep, "%s", detail);
        write_all(1);
        return;
    }
    s_playStep[0] = '\0';
    phase_to(ph, detail);
}

void last_run_selftest(int on)
{
    s_selftest = on != 0;
}

void last_run_set_pipelines(unsigned done, unsigned total)
{
    if (total == 0) {
        /* the set over (window_host.c hands the last count with total 0):
           its figures stay */
        if (done > s_setDone) {
            s_setDone = done;
        }
        s_setOpen = 0;
        return;
    }
    s_setOpen = 1;
    s_setDone = done;
    s_setTotal = total;
    if (s_phase < LAST_RUN_PHASE_SET && s_path[0] != '\0') {
        s_playStep[0] = '\0';
        phase_to(LAST_RUN_PHASE_SET, "");
    }
}

int last_run_present(void)
{
    if (s_phase != LAST_RUN_PHASE_GAME) {
        return 0;
    }
    s_playStep[0] = '\0';
    if (s_path[0] == '\0') {
        /* no file (it could not be opened): the phase moves all the same,
           so the caller's first picture (set_size_first_picture) still
           comes once */
        s_phase = LAST_RUN_PHASE_FIRST_PICTURE;
        s_phaseSince = ico_diag_uptime();
        return 1;
    }
    phase_to(LAST_RUN_PHASE_FIRST_PICTURE, "");
    return 1;
}

void last_run_milestone(const char *milestone)
{
    const int stage = last_run_milestone_stage(milestone);
    if (stage == LAST_RUN_NO_STAGE || s_path[0] == '\0') {
        return;
    }
    s_stage = stage;
    s_playStep[0] = '\0';
    phase_to(LAST_RUN_PHASE_PLAY, "");
}

void last_run_background(int background)
{
    if ((background != 0) == (s_bg != 0)) {
        return;
    }
    s_bg = background != 0;
    s_bgSince = ico_diag_uptime();
    /* Android: the system's lifecycle callback on its UI thread, never made
       to wait on the disc: written without a sync and only if no other
       write is in flight (else the next beat's, 2 s, carries it); the
       write is in the system's cache, which a killed process keeps */
    write_all(0);
}

void last_run_trim(int level, const char *name)
{
    s_trimLevel = level;
    s_trimAt = ico_diag_uptime();
    s_trimN++;
    snprintf(s_trimName, sizeof s_trimName, "%s", name != NULL ? name : "");
    write_all(0); /* onTrimMemory on Android's UI thread: as last_run_background */
}

void last_run_set_gpu_resets_start(int32_t start, const char *why)
{
    if (why == NULL && start >= 0) {
        s_resetsNow = start;
        s_resetsStart = start;
    } else {
        snprintf(s_resetsWhy, sizeof s_resetsWhy, "not readable (%s)",
                 why != NULL ? why : "no value read");
    }
}

void last_run_set_gpu_resets(int32_t now)
{
    if (s_resetsStart < 0 && now >= 0) {
        s_resetsStart = now;
    }
    s_resetsNow = now;
}

const char *last_run_gpu_memory_text(void)
{
    return s_gpuMem[__atomic_load_n(&s_gpuMemAt, __ATOMIC_ACQUIRE) & 1];
}

void last_run_set_gpu_memory(const char *text)
{
    /* the copy no reader takes now, then the switch */
    const int next = (__atomic_load_n(&s_gpuMemAt, __ATOMIC_ACQUIRE) + 1) & 1;
    snprintf(s_gpuMem[next], sizeof s_gpuMem[next], "%s", text != NULL ? text : "");
    __atomic_store_n(&s_gpuMemAt, next, __ATOMIC_RELEASE);
}

void last_run_set_summary_hook(void (*fn)(const char *summary))
{
    s_summary = fn;
    if (fn != NULL && s_path[0] != '\0') {
        summary();
    }
}

void last_run_end_clean(void)
{
    char was[sizeof s_end];
    if (s_path[0] == '\0') {
        return;
    }
    snprintf(was, sizeof was, "%s", s_end);
    if (was[0] != '\0' && strncmp(was, "clean", 5) != 0) {
        snprintf(s_end, sizeof s_end, "clean, after: %.160s", was);
    } else {
        snprintf(s_end, sizeof s_end, "clean");
    }
    write_all(1);
}

void last_run_end_words(const char *words)
{
    if (s_path[0] == '\0') {
        return;
    }
    snprintf(s_end, sizeof s_end, "%s", words != NULL ? words : "");
    write_all(1);
}

void last_run_end_crash(unsigned long code)
{
    /* no library call that is not async-signal-safe: the words built by
       hand, the slot by last_run_slot_build, one positional write */
    static char words[64], slot[192];
    static const char sig[] = "crash (signal ";
#ifdef _WIN32
    static const char exc[] = "crash (exception 0x";
    const char *head = exc;
#else
    const char *head = sig;
#endif
    size_t k = 0;
    char digits[24];
    int nd = 0;
    unsigned long v = code;
    unsigned spins = 0;
    int locked;
    if (!file_open()) {
        return;
    }
    (void)sig;
    for (size_t i = 0; head[i] != '\0'; i++) {
        words[k++] = head[i];
    }
#ifdef _WIN32
    do {
        digits[nd++] = s_hex[v & 15u];
        v >>= 4;
    } while (v != 0 && nd < (int)sizeof digits);
#else
    do {
        digits[nd++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0 && nd < (int)sizeof digits);
#endif
    while (nd > 0) {
        words[k++] = digits[--nd];
    }
    words[k++] = ')';
    words[k] = '\0';
    last_run_slot_build(LAST_RUN_END, words, slot);
    /* a beat's write in flight finishes first (bounded: the crashing thread
       may hold the lock itself) */
    while (!(locked = lock_try()) && spins < 1000000u) {
        spins++;
    }
    file_put(slot, last_run_slot_width(LAST_RUN_END), last_run_slot_offset(LAST_RUN_END), 1);
    /* a later full write keeps it */
    for (size_t i = 0; i < sizeof s_end && i <= k; i++) {
        s_end[i] = words[i];
    }
    if (locked) {
        lock_give();
    }
}

void last_run_set_size_words(const char *words)
{
    snprintf(s_sizeWords, sizeof s_sizeWords, "%s", words != NULL ? words : "");
    write_all(1);
}

long last_run_lowest_mb(void)
{
    return s_lowMb;
}
