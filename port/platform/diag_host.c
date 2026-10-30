/*
 * port/platform/diag_host.c
 *
 * Crash handlers, heartbeat, watchdog and milestone lines (diag_host.h).
 *
 * Every line is formatted into a static buffer and written with one OS call
 * to a handle of its own on the log file (opened for appending, as the
 * redirected stdout and stderr are), so nothing waits in a stdio buffer and
 * the crash and watchdog paths take no C library stream lock.
 *
 * The heartbeat and the watchdog run on their own OS thread: the game's
 * threads are fibers on the main thread, and one that spins without a
 * kernel call would stop anything run from the host loop. The thread reads
 * the scheduler's plain fields (sched.h, ico_sched_view) without locking;
 * a value may be a moment stale, which is fine for a report.
 *
 * A crash on Windows is reported from that thread too (the faulting thread
 * signals it and waits), so a stack overflow does not have to format the
 * report on the stack that overflowed. Before ico_diag_start, and on POSIX
 * (an alternate signal stack), the faulting thread reports itself.
 *
 * Android: the game is libmain.so, so addresses are given from its load
 * address; the lines also go to logcat; after a crash's block the handler
 * puts the system's handlers back and returns, so the fault repeats into
 * them and the system still writes its tombstone.
 */
#ifndef _WIN32
#define _GNU_SOURCE 1
#endif

#include "diag_host.h"
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fiber.h"
#include "gpu_hang.h"
#include "memory_floor.h"
#include "host_fs.h"
#include "rd_bisect_plan.h"
#include "report.h"
#include "sched.h"
#include "startup_retry.h"
#include "symmap.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#else

#include <errno.h>
#include <fcntl.h>

#define ICO_WANT_LIBC_SCHED 1 /* sched.h says why */

#include <pthread.h>

#undef ICO_WANT_LIBC_SCHED

#include <time.h>
#ifdef __APPLE__
#include <sys/ucontext.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#else
#include <ucontext.h>
#endif
#include <unistd.h>
#ifdef __ANDROID__

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>

#endif

#endif
/* marks a function the OS or the C runtime calls (a handler, a thread's entry) */
#define ICO_ENTRY
#define LINE_MAX_BYTES 1024
#define HEARTBEAT_S 2.0
#define NO_PROGRESS_BEATS 3
/* the watchdog polls every 0.25 s; a longer gap between two polls means
   the system or the process was suspended (watchdog_loop) */
#define SUSPEND_GAP_S 5.0
/* with an effects program loaded, the part of one present above
   PRESENT_FREE_NS is excused from the watchdog, for at most PRESENT_EXCUSE_NS */
#define PRESENT_FREE_NS (1000000000ull)
#define PRESENT_EXCUSE_NS (300ull * 1000000000ull)
/* one wait in the system (ico_diag_os_wait_enter) ends the run only after
   this many seconds (ico_diag_set_os_wait_limit) */
#define OS_WAIT_LIMIT_S 600u
#define STACK_SCAN_BYTES (64 * 1024)
#define STACK_HITS 24
#define FUNC_NAMES 64
#define THREAD_LINES_AFTER_BOOT 16
#define EXIT_CRASH 3
#define EXIT_WATCHDOG 4

/* --- State ----------------------------------------------------------------- */

static char log_path_copy[1024];

static int inited;

static IcoDiagStatusFn status_fn;

static unsigned int (*ticks_fn)(void);
static unsigned int (*vsyncs_fn)(void);
/* ico_diag_note_progress and ico_diag_set_movie: written by the game's
   thread, read by the watchdog's; a plain aligned word, as the tick count */
static volatile unsigned int progress;

static volatile int movie_playing;
/* ico_diag_set_activity: written by the one thread that builds pipelines,
   read by the crash report (on Windows from the watchdog thread while the
   faulting thread waits, so the text stands still) and by the heartbeat
   while the start-up set is created (a copy, so a note changing meanwhile
   gives at worst a mixed line, never a read past the buffer). A batch's
   note names its keys, its modules and their sizes (rd_pipeline.c
   createKeys): up to about 450 characters */
static char activity[640];
/* ico_diag_set_pipelines: the start-up set's count and its longest compile */
static volatile unsigned int pipes_done, pipes_total, pipes_longest_ms;
/* ico_diag_present_enter/leave (diag_host.h): when the main thread entered
   the present it is in (0 = not inside one), and the capped time of the
   presents that finished, both in ico_diag_now_ns */
static volatile unsigned long long present_since_ns;

static volatile unsigned long long present_done_ns;
/* ico_diag_os_wait_enter/leave (diag_host.h): when the main thread entered
   the system's wait it is in (0 = not inside one), what it is, the time of
   the waits that finished and the last one's, all in ico_diag_now_ns */
static volatile unsigned long long os_wait_since_ns;
static volatile unsigned long long os_wait_done_ns;
static volatile unsigned long long os_wait_last_ns;
static const char *volatile os_wait_what;
static volatile unsigned int os_wait_limit_s = OS_WAIT_LIMIT_S;
static void (*volatile os_wait_describe)(char *out, size_t size);
static double os_wait_seconds(double *current, unsigned long long *since_out);
static const char *os_wait_name(void);
/* ico_diag_set_effects_program: written by the host before the first present */
static volatile int effects_program;
/* ico_diag_set_gpu_hang: the graphics backend's progress and the window's
   loss dump, set once before the game */
static int (*volatile gpu_progress_fn)(IcoDiagGpu *out);
static void (*volatile gpu_dump_fn)(void);
/* ico_diag_set_gpu_hang_limit (gpu_hang.h): in milliseconds, 0 off; set
   before ico_diag_start */
static volatile unsigned int gpu_hang_limit_ms;
/* ico_diag_set_gpu_hang_note: the sentence the Android hang box ends with */
static const char *(*volatile gpu_hang_note_fn)(void);
static void (*exit_hook)(const char *reason);

#ifndef _WIN32
/* ico_diag_set_fatal_ui's box and flush (Android: both ends; Linux: the
   watchdog's box), and the requests an Android crashing thread leaves for
   the watchdog thread (fatal_ui_*) */
static void (*fatal_box)(const char *text);
static void (*fatal_flush)(void);
static volatile sig_atomic_t ui_flush_req, ui_flush_done, ui_box_req, ui_box_done;
static char ui_box_text[1536];
#endif

static char failure[512];

static char thread_names[ICO_SCHED_MAX_THREADS][32];

static void *thread_funcs[ICO_SCHED_MAX_THREADS];

static struct {
    void *fn;
    const char *name;
} func_names[FUNC_NAMES];

static int n_func_names;

static int kanban_seen;

static int kanban_boot = -1000;

static int kanban_mc = -1000;

/* the module the game is in */
static uintptr_t exe_lo;

static uintptr_t exe_hi;

static char exe_name[64] = "ico_pc";

/* the offsets a fatal report printed, in order, for its symbol lines
   (report_symbols); sym_collect while the report is written */
static uint64_t sym_offs[ICO_SYM_MAX];
static int sym_n;
static int sym_collect;

static void sym_note(uintptr_t a);

/* the report file's name (report.h), for the box ("" none) */
static char report_name[160];

/* one crash (or watchdog report) at a time */
static volatile int fatal_once;

/* the fault the report describes */
static struct {
    const char *what;   /* "EXCEPTION_ACCESS_VIOLATION", "SIGSEGV", "abort()" */
    unsigned long code; /* the exception code or signal number */
    uintptr_t pc;       /* 0 when unknown (abort) */
    uintptr_t sp;
    uintptr_t fault_addr;
    int fault_kind; /* 0 read, 1 write, 8 execute, -1 none */
    int thread;     /* the scheduler's current thread then */
    uintptr_t stack_lo, stack_hi;
} crash;

/* --- Platform: the log, time, locks ------------------------------------------ */

#ifdef _WIN32

/* kernel32's CreateThread, SuspendThread and ResumeThread share their names
   with the EE kernel calls kernel_host.c defines; linking them by name would
   pull kernel32's import stubs in beside those (a duplicate definition on
   x86-64), so they are looked up at run time. */
typedef HANDLE(WINAPI *CreateThreadFn)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
                                       LPVOID, DWORD, LPDWORD);
typedef DWORD(WINAPI *SuspendResumeFn)(HANDLE);
static CreateThreadFn w32_create_thread;
static SuspendResumeFn w32_suspend_thread;
static SuspendResumeFn w32_resume_thread;

static void w32_threads(void)
{
    HMODULE k = GetModuleHandleA("kernel32.dll");
    if (k == NULL) {
        return;
    }
    w32_create_thread = (CreateThreadFn)(void (*)(void))GetProcAddress(k, "CreateThread");
    w32_suspend_thread = (SuspendResumeFn)(void (*)(void))GetProcAddress(k, "SuspendThread");
    w32_resume_thread = (SuspendResumeFn)(void (*)(void))GetProcAddress(k, "ResumeThread");
}

static HANDLE log_h = INVALID_HANDLE_VALUE;

static CRITICAL_SECTION lock;

static ULONGLONG t0;

static HANDLE main_thread;

static DWORD main_tid;

static HANDLE crash_event;

static HANDLE crash_done;

static DWORD reporter_tid;

static int reporter_running;

static void raw_write(const char *s, size_t n)
{
    DWORD done;
    HANDLE h = log_h != INVALID_HANDLE_VALUE ? log_h : GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE && h != NULL) {
        WriteFile(h, s, (DWORD)n, &done, NULL);
    }
}

double ico_diag_uptime(void)
{
    return (double)(GetTickCount64() - t0) / 1000.0;
}

unsigned long long ico_diag_now_ns(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&c);
    return (unsigned long long)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
}

static void lock_take(void)
{
    EnterCriticalSection(&lock);
}

static void lock_give(void)
{
    LeaveCriticalSection(&lock);
}

/* stdout and stderr are fully buffered on Windows
   (host_config.c ico_host_redirect_output; msvcrt writes an unbuffered
   stream one character per OS call). Before a fatal report their content
   is written out by a helper thread, waited on for at most STDIO_FLUSH_MS:
   the stopped thread may hold a stream's lock, and a flush that faults
   only stops the helper (crash_from_exception parks a second faulting
   thread). */
#define STDIO_FLUSH_MS 500

ICO_ENTRY static DWORD WINAPI stdio_flush_main(LPVOID arg)
{
    (void)arg;
    fflush(stdout);
    fflush(stderr);
    return 0;
}

static void flush_stdio_bounded(void)
{
    DWORD tid;
    HANDLE h = w32_create_thread != NULL
                   ? w32_create_thread(NULL, 64 * 1024, stdio_flush_main, NULL, 0, &tid)
                   : NULL;

    if (h != NULL) {
        WaitForSingleObject(h, STDIO_FLUSH_MS);
        CloseHandle(h);
    }
}

/* a milestone on the main thread: what stderr holds goes first */
static void flush_stdio_if_main(void)
{
    if (GetCurrentThreadId() == main_tid) {
        fflush(stdout);
        fflush(stderr);
    }
}

#else

static int log_fd = -1;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct timespec t0;

static pthread_t main_thread;

static uintptr_t main_stack_lo, main_stack_hi;

static int reporter_running;

#ifdef __ANDROID__
/* the watchdog thread (ico_diag_start): the crash handler hands it the
   player's box */
static pthread_t reporter_thread;
#endif

static void raw_write(const char *s, size_t n)
{
    int fd = log_fd >= 0 ? log_fd : 2;
#ifdef __ANDROID__
    /* logcat too: written to the file, these lines bypass stderr and its
       mirror (one line, its newline dropped) */
    if (log_fd >= 0 && n > 0 && n < LINE_MAX_BYTES) {
        char line[LINE_MAX_BYTES];

        memcpy(line, s, n);
        line[s[n - 1] == '\n' ? n - 1 : n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "ico-pc", line);
    }
#endif
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w <= 0) {
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

double ico_diag_uptime(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0.tv_sec) + (double)(t.tv_nsec - t0.tv_nsec) / 1e9;
}

unsigned long long ico_diag_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}

static void lock_take(void)
{
    pthread_mutex_lock(&lock);
}

static void lock_give(void)
{
    pthread_mutex_unlock(&lock);
}

/* POSIX keeps stdout and stderr unbuffered (glibc formats a whole printf
   call before its one write): nothing waits in them */
static void flush_stdio_bounded(void) {}

static void flush_stdio_if_main(void) {}

#endif

/* Formats one line (adding the newline) into buf and writes it. */
static void vline(char *buf, size_t size, const char *fmt, va_list ap)
{
    int n = vsnprintf(buf, size - 1, fmt, ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > size - 2) {
        n = (int)size - 2;
    }
    buf[n++] = '\n';
    raw_write(buf, (size_t)n);
}

void ico_diag_log(const char *fmt, ...)
{
    static char buf[LINE_MAX_BYTES];
    va_list ap;
    if (inited) {
        lock_take();
    }
    va_start(ap, fmt);
    vline(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (inited) {
        lock_give();
    }
}

/* The fatal paths' writer: no lock (the lock's holder may be the thread
   that crashed or the main thread the watchdog interrupted). */
static void flog(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);

static void flog(const char *fmt, ...)
{
    static char buf[LINE_MAX_BYTES];
    va_list ap;
    va_start(ap, fmt);
    vline(buf, sizeof buf, fmt, ap);
    va_end(ap);
}

const char *ico_diag_log_path(void)
{
    return log_path_copy[0] != '\0' ? log_path_copy : "(the console)";
}

void ico_diag_set_failure(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(failure, sizeof failure, fmt, ap);
    va_end(ap);
}

void ico_diag_set_sources(IcoDiagStatusFn status, unsigned int (*main_ticks)(void),
                          unsigned int (*vsyncs)(void))
{
    status_fn = status;
    ticks_fn = main_ticks;
    vsyncs_fn = vsyncs;
}

void ico_diag_note_progress(void)
{
    progress++;
}

unsigned int ico_diag_progress(void)
{
    return progress;
}

void ico_diag_set_movie(int playing)
{
    movie_playing = playing;
}

void ico_diag_set_pipelines(unsigned int done, unsigned int total, unsigned int longest_ms)
{
    pipes_done = done;
    pipes_longest_ms = longest_ms;
    pipes_total = total;
}

void ico_diag_set_exit_hook(void (*fn)(const char *reason))
{
    exit_hook = fn;
}

/* --- Addresses and names ----------------------------------------------------- */

static int in_exe(uintptr_t a)
{
    return a >= exe_lo && a < exe_hi;
}

/* "ico_pc.exe+0x1234", "msvcrt.dll+0x56" or "0x...". */
static void describe(uintptr_t a, char *out, size_t size)
{
    if (a == 0) {
        snprintf(out, size, "(unknown)");
        return;
    }
    if (in_exe(a)) {
        sym_note(a);
        snprintf(out, size, "%s+0x%lx", exe_name, (unsigned long)(a - exe_lo));
        return;
    }
#ifdef _WIN32
    {
        HMODULE m = NULL;
        char path[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)a, &m) &&
            GetModuleFileNameA(m, path, sizeof path) > 0) {
            const char *b = strrchr(path, '\\');
            snprintf(out, size, "%s+0x%lx", b != NULL ? b + 1 : path,
                     (unsigned long)(a - (uintptr_t)m));
            return;
        }
    }
#endif
    snprintf(out, size, "0x%llx", (unsigned long long)a);
}

static const char *func_name(void *fn)
{
    int i;
    for (i = 0; i < n_func_names; i++) {
        if (func_names[i].fn == fn) {
            return func_names[i].name;
        }
    }
    return NULL;
}

void ico_diag_name_func(void *func, const char *name)
{
    int i;
    for (i = 0; i < n_func_names; i++) {
        if (func_names[i].fn == func) {
            func_names[i].name = name;
            return;
        }
    }
    if (n_func_names < FUNC_NAMES) {
        func_names[n_func_names].fn = func;
        func_names[n_func_names].name = name;
        n_func_names++;
    }
}

/* The thread's name: given, else its function's, else its function's
   address. */
static const char *thread_name(int id, char *tmp, size_t size)
{
    IcoSchedView v;
    void *fn;
    const char *n;
    if (id == 0) {
        return "host loop";
    }
    if (id < 0 || id >= ICO_SCHED_MAX_THREADS) {
        return "?";
    }
    if (thread_names[id][0] != '\0') {
        return thread_names[id];
    }
    fn = thread_funcs[id];
    if (fn == NULL && ico_sched_view(id, &v)) {
        fn = v.entry;
    }
    n = fn != NULL ? func_name(fn) : NULL;
    if (n != NULL) {
        return n;
    }
    if (fn == NULL) {
        return "?";
    }
    {
        char d[64];
        describe((uintptr_t)fn, d, sizeof d);
        snprintf(tmp, size, "fn %s", d);
    }
    return tmp;
}

/* --- Thread lines -------------------------------------------------------------- */

static const char *state_name(int s)
{
    switch (s) {
    case ICO_THS_RUN:
        return "RUN";
    case ICO_THS_READY:
        return "READY";
    case ICO_THS_WAIT:
        return "WAIT";
    case ICO_THS_SUSPEND:
        return "SUSPEND";
    case ICO_THS_WAITSUSPEND:
        return "WAITSUSPEND";
    case ICO_THS_DORMANT:
        return "DORMANT";
    }
    return "?";
}

/* One thread's line; returns 0 when id is not a thread. */
static int thread_line(int id, char *out, size_t size)
{
    IcoSchedView v;
    char tmp[80];
    char wait[96];
    char last[160];
    char at[64];
    if (!ico_sched_view(id, &v)) {
        return 0;
    }
    wait[0] = '\0';
    if (v.status == ICO_THS_WAIT || v.status == ICO_THS_WAITSUSPEND) {
        if (v.wait_type == ICO_TSW_SLEEP) {
            snprintf(wait, sizeof wait, " sleeping (SleepThread)");
        } else if (v.wait_type == ICO_TSW_SEMA) {
            int count = 0;
            int waiters = 0;
            ico_sched_sema_view(v.wait_id, &count, &waiters);
            snprintf(wait, sizeof wait, " on sema %d (count %d, %d waiting)", v.wait_id, count,
                     waiters);
        }
    } else if (v.spinning) {
        snprintf(wait, sizeof wait, " busy-waiting for vsync %u", v.spin_until);
    }
    if (v.note != NULL) {
        describe((uintptr_t)v.note_caller, at, sizeof at);
        snprintf(last, sizeof last, "; last call %s(%d) from %s at vsync %u", v.note, v.note_arg,
                 at, v.note_vsync);
    } else {
        last[0] = '\0';
    }
    snprintf(out, size, "%c#%-3d %-22s pri %3d (init %3d) %-7s%s%s%s",
             id == ico_sched_current() ? '*' : ' ', id, thread_name(id, tmp, sizeof tmp),
             v.priority, v.init_priority, state_name(v.status), wait,
             v.wakeup_count > 0 ? " (wakeup pending)" : "", last);
    return 1;
}

static void dump_threads(void)
{
    static char line[LINE_MAX_BYTES];
    int id;
    flog("ico_pc: threads (* = on the CPU; priority 0 is the highest):");
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        if (thread_line(id, line, sizeof line)) {
            flog("ico_pc:   %s", line);
        }
    }
}

/* --- Process memory and the fatal end's player box ------------------------------ */

void ico_diag_set_fatal_ui(void (*box)(const char *text), void (*flush)(void))
{
#ifdef _WIN32
    /* Windows has its own box (finish) */
    (void)box;
    (void)flush;
#else
    fatal_box = box;
    fatal_flush = flush;
#endif
}

#ifndef _WIN32

/* "Name:   1234 kB" in a /proc status text: the number, or -1 */
static long status_kb(const char *text, const char *name)
{
    const char *p = text;
    const size_t n = strlen(name);
    while (*p != '\0') {
        if (strncmp(p, name, n) == 0) {
            long v = 0;
            p += n;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p < '0' || *p > '9') {
                return -1;
            }
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p++ - '0');
            }
            return v;
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return -1;
}

#endif

int ico_diag_process_memory(long *rss_kb, long *peak_kb)
{
    *rss_kb = *peak_kb = -1;
#ifdef _WIN32
    return -1;
#else
    /* open and read only: callable from the crash handler */
    char buf[4096];
    ssize_t got = 0;
    const int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    while (got < (ssize_t)sizeof buf - 1) {
        const ssize_t r = read(fd, buf + got, sizeof buf - 1 - (size_t)got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    close(fd);
    buf[got] = '\0';
    *rss_kb = status_kb(buf, "VmRSS:");
    *peak_kb = status_kb(buf, "VmHWM:");
    return *rss_kb >= 0 ? 0 : -1;
#endif
}

int ico_diag_device_memory(long *avail_kb, long *total_kb)
{
    *avail_kb = *total_kb = -1;
#ifdef _WIN32
    return -1;
#else
    /* open and read only, as ico_diag_process_memory: the two lines are
       the file's first three */
    char buf[4096];
    ssize_t got = 0;
    const int fd = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    while (got < (ssize_t)sizeof buf - 1) {
        const ssize_t r = read(fd, buf + got, sizeof buf - 1 - (size_t)got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    close(fd);
    buf[got] = '\0';
    *avail_kb = status_kb(buf, "MemAvailable:");
    *total_kb = status_kb(buf, "MemTotal:");
    if (*avail_kb < 0 || *total_kb < 0) {
        *avail_kb = *total_kb = -1;
        return -1;
    }
    return 0;
#endif
}

void ico_diag_memory_text(char *out, size_t size)
{
    long rss, peak, avail, total;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (ico_diag_process_memory(&rss, &peak) != 0) {
        return;
    }
    if (ico_diag_device_memory(&avail, &total) == 0) {
        snprintf(out, size, "memory %ld MB (most %ld MB); device %ld MB available of %ld MB",
                 rss / 1024, (peak >= 0 ? peak : rss) / 1024, avail / 1024, total / 1024);
    } else {
        snprintf(out, size, "memory %ld MB (most %ld MB); device memory unknown", rss / 1024,
                 (peak >= 0 ? peak : rss) / 1024);
    }
}

/* " | memory N MB (most M MB); device A MB available of T MB", or "" where
   unknown */
static void memory_note(char *out, size_t size)
{
    char text[160];
    out[0] = '\0';
    ico_diag_memory_text(text, sizeof text);
    if (text[0] != '\0') {
        snprintf(out, size, " | %s", text);
    }
}

#ifdef __ANDROID__

static int on_reporter(void)
{
    return reporter_running && pthread_equal(pthread_self(), reporter_thread);
}

/* the crashing thread: waits up to ms for the watchdog thread to set *flag */
static int ui_wait(volatile sig_atomic_t *flag, unsigned int ms)
{
    struct timespec ts = {0, 20 * 1000 * 1000};
    unsigned int t;
    for (t = 0; t < ms && !*flag; t += 20) {
        nanosleep(&ts, NULL);
    }
    return *flag != 0;
}

/* What the process wrote to stderr just before the end (the C library's
   abort message) into the log, ahead of the report: on the watchdog thread
   at once, from another thread through it, waiting at most a second. */
static void fatal_ui_flush(void)
{
    if (fatal_flush == NULL) {
        return;
    }
    if (on_reporter()) {
        fatal_flush();
    } else if (reporter_running) {
        ui_flush_done = 0;
        ui_flush_req = 1;
        ui_wait(&ui_flush_done, 1000);
    }
}

/* The player's box: on the watchdog thread itself, else through it, the
   crashing thread waiting until it is dismissed (at most two minutes) */
static void fatal_ui_box(const char *text)
{
    size_t n;
    if (fatal_box == NULL) {
        return;
    }
    n = strlen(text);
    if (n >= sizeof ui_box_text) {
        n = sizeof ui_box_text - 1;
    }
    memcpy(ui_box_text, text, n);
    ui_box_text[n] = '\0';
    if (on_reporter()) {
        fatal_box(ui_box_text);
    } else if (reporter_running) {
        ui_box_done = 0;
        ui_box_req = 1;
        ui_wait(&ui_box_done, 120000);
    }
}

#endif

#ifndef _WIN32

/* the watchdog thread's side, at each poll */
static void fatal_ui_serve(void)
{
    if (ui_flush_req && !ui_flush_done) {
        if (fatal_flush != NULL) {
            fatal_flush();
        }
        ui_flush_done = 1;
    }
    if (ui_box_req && !ui_box_done) {
        if (fatal_box != NULL) {
            fatal_box(ui_box_text);
        }
        ui_box_done = 1;
    }
}

#endif

#ifdef __ANDROID__

/* the box's words: what happened, the saves, and the log to send */
static void fatal_text(char *out, size_t size, const char *what)
{
    if (report_name[0] != '\0') {
        snprintf(out, size,
                 "%s\n\nYour saves are kept. To report it, send the newest file in logs that "
                 "starts with report- (this time: %s) with a few words about what you did just "
                 "before.",
                 what, report_name);
    } else {
        snprintf(out, size,
                 "%s\n\nYour saves are kept. To report it, send the file %s with a few words "
                 "about what you did just before.",
                 what, log_path_copy[0] != '\0' ? log_path_copy : "logs/ico-pc.log");
    }
}

#endif

/* --- The GPU's progress (ico_diag_set_gpu_hang) ----------------------------------- */

void ico_diag_set_gpu_hang(int (*progress)(IcoDiagGpu *out), void (*dump)(void))
{
    gpu_progress_fn = progress;
    gpu_dump_fn = dump;
}

void ico_diag_set_gpu_hang_limit(double seconds)
{
    gpu_hang_limit_ms = seconds > 0.0 ? (unsigned int)(seconds * 1000.0 + 0.5) : 0u;
}

void ico_diag_set_gpu_hang_note(const char *(*note)(void))
{
    gpu_hang_note_fn = note;
}

/* the backend's progress into *g: 1 when it tracks it (no call into the
   driver: callable from the watchdog thread and the crash paths) */
static int gpu_state(IcoDiagGpu *g)
{
    int (*fn)(IcoDiagGpu *) = gpu_progress_fn;
    memset(g, 0, sizeof *g);
    return fn != NULL && fn(g) != 0;
}

/* a GPU hang: a wait for the GPU in progress whose work has not finished */
static int gpu_hung(const IcoDiagGpu *g)
{
    return g->waiting_for != 0 && g->waiting_for > g->finished;
}

/* --- Heartbeat ------------------------------------------------------------------ */

/* The heartbeat's content without the time. */
static void heartbeat_body(char *out, size_t size)
{
    char st[512];
    char tmp[80];
    int id;
    int ready = 0;
    int waiting = 0;
    int cur = ico_sched_current();
    IcoSchedView v;
    st[0] = '\0';
    if (status_fn != NULL) {
        status_fn(st, sizeof st);
    }
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        if (ico_sched_view(id, &v)) {
            if (v.status == ICO_THS_READY || v.status == ICO_THS_RUN) {
                ready++;
            } else if (v.status == ICO_THS_WAIT || v.status == ICO_THS_WAITSUSPEND) {
                waiting++;
            }
        }
    }
    if (cur != 0 && ico_sched_view(cur, &v)) {
        snprintf(out, size, "%s | on CPU #%d %s pri %d | ready %d, waiting %d | switches %lu", st,
                 cur, thread_name(cur, tmp, sizeof tmp), v.priority, ready, waiting,
                 ico_sched_switch_count());
    } else {
        snprintf(out, size, "%s | on CPU: host loop | ready %d, waiting %d | switches %lu", st,
                 ready, waiting, ico_sched_switch_count());
    }
    if (kanban_seen) {
        size_t n = strlen(out);
        snprintf(out + n, size - n, " | kanbanBoot step %d, card step %d", kanban_boot, kanban_mc);
    }
    {
        /* the second sign of life (ico_diag_note_progress): a movie's
           pictures move it while the tick stands still */
        size_t n = strlen(out);
        snprintf(out + n, size - n, " | progress %u%s", progress,
                 movie_playing ? " (a movie is playing)" : "");
    }
    {
        /* the start-up set while it is created (it runs before the first
           Main tick, and a phone takes minutes over it) */
        const unsigned int total = pipes_total;
        if (total > 0) {
            size_t n = strlen(out);
            snprintf(out + n, size - n, " | pipelines %u of %u, longest %.1f s", pipes_done, total,
                     (double)pipes_longest_ms / 1000.0);
            /* and what the driver is compiling now (the batch's keys,
               modules and sizes: issue 107, a log that stops mid-set
               names the shader) */
            {
                char note[sizeof activity];
                memcpy(note, activity, sizeof note);
                note[sizeof note - 1] = '\0';
                if (note[0] != '\0') {
                    n = strlen(out);
                    snprintf(out + n, size - n, " | %s", note);
                }
            }
        }
    }
}

static char last_body[1024];

static unsigned int same_beats;

static void heartbeat(int fatal_path)
{
    static char body[1024];
    static char mem[288];
    IcoDiagGpu gpu;
    heartbeat_body(body, sizeof body);
    if (strcmp(body, last_body) == 0) {
        same_beats++;
    } else {
        same_beats = 0;
        memcpy(last_body, body, sizeof body);
    }
    /* the process's memory (a phone ends a process that holds too
       much without a word), outside the comparison: it moves while the game
       stands still */
    memory_note(mem, sizeof mem);
    {
        /* a wait the system holds (ico_diag_os_wait_enter), also outside
           the comparison: the game stands still while it lasts */
        double cur;
        (void)os_wait_seconds(&cur, NULL);
        if (cur >= 1.0) {
            const size_t n = strlen(mem);
            snprintf(mem + n, sizeof mem - n, " | in %s %.0f s", os_wait_name(), cur);
        }
    }
    if (gpu_state(&gpu) && gpu.waiting_for != 0 && gpu.waiting_s >= 1.0) {
        /* a wait for the GPU (the watchdog's GPU hang), outside the
           comparison too */
        const size_t n = strlen(mem);
        snprintf(mem + n, sizeof mem - n,
                 " | waiting for the GPU %.0f s (submission %llu; the newest %llu, the newest "
                 "seen finished %llu)",
                 gpu.waiting_s, gpu.waiting_for, gpu.submitted, gpu.finished);
    }
    if (fatal_path) {
        flog("ico_pc: heartbeat %.1f s | %s%s", ico_diag_uptime(), body, mem);
    } else if (same_beats + 1 >= NO_PROGRESS_BEATS) {
        ico_diag_log("ico_pc: heartbeat %.1f s | %s%s | no progress for %.0f s", ico_diag_uptime(),
                     body, mem, (double)same_beats * HEARTBEAT_S);
    } else {
        ico_diag_log("ico_pc: heartbeat %.1f s | %s%s", ico_diag_uptime(), body, mem);
    }
}

/* --- Milestones and the game's hooks --------------------------------------------- */

static void prefix(char *out, size_t size)
{
    char tmp[80];
    int cur = ico_sched_current();
    snprintf(out, size, "[%.3f s, vsync %u, tick %u, #%d %s]", ico_diag_uptime(),
             vsyncs_fn != NULL ? vsyncs_fn() : 0u, ticks_fn != NULL ? ticks_fn() : 0u, cur,
             thread_name(cur, tmp, sizeof tmp));
}

void ico_diag_milestone(const char *fmt, ...)
{
    char p[160];
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    prefix(p, sizeof p);
    flush_stdio_if_main();
    ico_diag_log("ico_pc: %s %s", p, msg);
}

void ico_host_milestone(const char *what)
{
    ico_diag_milestone("%s", what);
}

void ico_host_name_func(void *func, const char *name)
{
    ico_diag_name_func(func, name);
}

void ico_host_thread_func(int id, void *func, int priority)
{
    char tmp[80];
    char tmp2[80];
    IcoSchedView v;
    if (id <= 0 || id >= ICO_SCHED_MAX_THREADS) {
        return;
    }
    static unsigned int logged;
    thread_funcs[id] = func;
    thread_names[id][0] = '\0';
    if (!inited) {
        return;
    }
    /* every one until the first Main tick, then a few (the game makes
       short-lived threads all the time) */
    if (ticks_fn != NULL && ticks_fn() > 0 && logged >= THREAD_LINES_AFTER_BOOT) {
        return;
    }
    if (ticks_fn != NULL && ticks_fn() > 0 && ++logged == THREAD_LINES_AFTER_BOOT) {
        ico_diag_milestone("(further thread creations are not logged)");
    }
    ico_sched_view(id, &v);
    ico_diag_milestone("thread #%d created by #%d %s: priority %d, runs %s", id, v.parent,
                       thread_name(v.parent, tmp2, sizeof tmp2), priority,
                       thread_name(id, tmp, sizeof tmp));
}

void ico_host_kanban_step(int boot_step, int mc_check_step)
{
    if (boot_step == kanban_boot && mc_check_step == kanban_mc) {
        return;
    }
    kanban_seen = 1;
    kanban_boot = boot_step;
    kanban_mc = mc_check_step;
    if (inited) {
        ico_diag_milestone("kanbanBoot: bootStep %d, mcCheckStep %d", boot_step, mc_check_step);
    }
}

void ico_host_gflag_range(int id, int write, int count, void *caller)
{
    static const char *const what[3] = {"read", "set", "cleared"};
    char d[96];

    if (!inited) {
        return;
    }
    describe((uintptr_t)caller, d, sizeof d);
    ico_log_limited("gflag: flag id past the disc's flags",
                    "ico_pc: gflag: flag %d %s past the disc's %d flags (from %s)", id,
                    what[write >= 0 && write <= 2 ? write : 0], count, d);
}

void ico_host_item_held(int kind, int motion, int girl)
{
    if (!inited) {
        return;
    }
    ico_log_limited("item: an item taken", "ico_pc: item: %s takes an item of kind %d (motion %d)",
                    girl ? "Yorda" : "Ico", kind, motion);
}

void ico_host_torch_out(int burn_time, int life, int flags)
{
    if (!inited) {
        return;
    }
    ico_log_limited("torch: a torch went out",
                    "ico_pc: torch: a lit torch went out after %d frames (life %d, type %d)",
                    burn_time, life, flags);
}

void ico_host_torch_caught(int flags)
{
    if (!inited) {
        return;
    }
    ico_log_limited("torch: a torch caught fire",
                    "ico_pc: torch: a torch caught fire from a burning one nearby (type %d)",
                    flags);
}

void ico_host_candle_flames(int asked, int placed)
{
    if (!inited) {
        return;
    }
    ico_log_limited("candle: flames placed", "ico_pc: candle: %d of %d flames placed", placed,
                    asked);
}

void ico_host_item_watch_null(int motion, float frame, float anim_frame)
{
    if (!inited) {
        return;
    }
    ico_log_limited("item watch: null item skipped",
                    "ico_pc: item watch: a mode-2 motion %d at frame %g (item frame %g) with no "
                    "item held: the null read skipped",
                    motion, (double)anim_frame, (double)frame);
}

void ico_host_stage_switch(int stage, float fade_in, float fade_out, int cmd, int queued,
                           int queued_stage)
{
    char p[160];
    char tmp[80];
    int cur;

    if (!inited) {
        return;
    }
    prefix(p, sizeof p);
    cur = ico_sched_current();
    ico_log_limited_n("stage switch request", 200u, 10u,
                      "ico_pc: %s stage switch request: stage %d, fade in %g out %g, cmd %d, by "
                      "thread #%d %s",
                      p, stage, (double)fade_in, (double)fade_out, cmd, cur,
                      thread_name(cur, tmp, sizeof tmp));
    if (queued > 0) {
        ico_log_limited("stage switch request while one is queued",
                        "ico_pc: %s a second stage switch request while one is queued (stage %d "
                        "then %d, %d queued)",
                        p, queued_stage, stage, queued);
    }
}

/* what an abort() is called in the crash report, on both systems */
#define ABORT_WHAT "abort() (an assertion or a fatal error; see the last failure message)"

/* --- Stacks ------------------------------------------------------------------------ */

/* Words in [lo, hi) that point into the executable: return-address
   candidates, innermost first. */
static void scan_stack(const unsigned char *lo, const unsigned char *hi)
{
    char line[LINE_MAX_BYTES];
    size_t n = 0;
    int hits = 0;
    const unsigned char *p;
    line[0] = '\0';
    for (p = lo; p + sizeof(uintptr_t) <= hi && hits < STACK_HITS; p += sizeof(uintptr_t)) {
        uintptr_t w;
        memcpy(&w, p, sizeof w);
        if (in_exe(w)) {
            sym_note(w);
            n +=
                (size_t)snprintf(line + n, sizeof line - n, " +0x%lx", (unsigned long)(w - exe_lo));
            hits++;
            if (n > sizeof line - 32) {
                break;
            }
        }
    }
    flog("ico_pc: stack, return-address candidates in %s (innermost first):%s", exe_name,
         hits > 0 ? line : " none found");
}

#ifdef _WIN32

/* [sp, end of its committed region), at most STACK_SCAN_BYTES */
static void stack_bounds(uintptr_t sp, uintptr_t *lo, uintptr_t *hi)
{
    MEMORY_BASIC_INFORMATION mbi;
    *lo = *hi = 0;
    if (sp == 0 || VirtualQuery((LPCVOID)sp, &mbi, sizeof mbi) == 0 || mbi.State != MEM_COMMIT) {
        return;
    }
    *lo = sp;
    *hi = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if (*hi - *lo > STACK_SCAN_BYTES) {
        *hi = *lo + STACK_SCAN_BYTES;
    }
}

#else

/* the running fiber's stack, else the main thread's (called on the thread
   whose stack it is) */
static void stack_bounds_here(uintptr_t sp, uintptr_t *lo, uintptr_t *hi)
{
    void *a;
    void *b;
    *lo = *hi = 0;
    if (ico_fiber_current_stack(&a, &b) == 0 && sp >= (uintptr_t)a && sp < (uintptr_t)b) {
        *lo = sp;
        *hi = (uintptr_t)b;
    } else if (sp >= main_stack_lo && sp < main_stack_hi) {
        *lo = sp;
        *hi = main_stack_hi;
    }
    if (*hi - *lo > STACK_SCAN_BYTES) {
        *hi = *lo + STACK_SCAN_BYTES;
    }
}

#endif

/* --- The activity note and the relaunch after a driver crash ------------------ */

/* activity (ico_diag_set_activity) is defined with the heartbeat's state
   at the top of the file */

/* ico_diag_startup_activity_record's mode (0 off) and the file's path */
static int act_mode;
static char act_path[1024];
#ifdef _WIN32
static wchar_t act_wpath[1024];
#endif

/* The note in the start-up activity file, replacing what it held. The
   file stays open while the run records (about 700 notes in a start-up
   set): each note is one write at the file's start, no stdio buffer, then
   the file is cut to it. A write shorter than the text before it is
   padded with NULs to that text's end, so the file reads as the new note
   (the reader stops at a NUL) even when the process is ended between the
   write and the cut, or the next moment without a word (a phone's system
   ends it, or a driver exits it). act_len: the file's length now. */
static char act_version[128];
/* a device loss in this run (ico_diag_note_device_loss): the file's third
   line, "loss dump: <zip>" or act_loss_none, kept through the run's end so
   the next start finds it; "" none */
static char act_loss[1100];
static const char act_loss_prefix[] = "loss dump: ";
/* this start's set is made under the careful policy
   (ico_diag_startup_activity_careful): the file's line after the note
   while it records; the previous run's, read by begin */
static int act_careful;
static const char act_set_careful[] = "set: careful";
static int act_prev_careful;
/* this start's set stopped at the system's low-memory signal
   (ico_diag_startup_activity_budget_stop after ico_diag_low_memory_stop):
   the file's line after the note for the rest of the run, with the level
   ("set: stopped at the system's low-memory signal (running critical)");
   the previous run's, read by begin, makes this start lazy
   (ICO_DIAG_PIPESET_LAZY_LOW_MEMORY) */
static char act_low_memory[160];
static const char act_set_low_memory[] = "set: stopped at the system's low-memory signal";
static int act_prev_low_memory;
static char act_prev_low_memory_line[sizeof act_low_memory];
static const char act_loss_none[] = "graphics device lost, no loss dump";
/* a GPU hang's loss (watchdog_fire, loss_hang set): its own line, counted
   as a loss (loss_count_prev) and its dump attached to a report, but never
   replayed by the next start's bisect (a replay of the frame the driver
   hung on can hang the same way, before the watchdog runs) */
static const char act_loss_hang_prefix[] = "loss dump (the driver hung): ";
static const char act_loss_hang_none[] = "graphics driver hung, no loss dump";
static volatile int loss_hang;
/* the previous run's GPU hang's dump, for this run's report only
   (report_file), "" none */
static char loss_prev_hang[1024];
/* the loss dump the previous run left for this one's bisect
   (ico_diag_startup_activity_begin), "" none */
static char loss_pending[1024];
/* the bisect's note: a run ended during it is not bisected again */
static const char act_bisect[] = "replaying the lost frame pass by pass (the loss bisect)";
/* the second level's note (ico_diag_loss_bisect2): the same; its state file
   says where it was */
static const char act_bisect2[] = "replaying the lost frame draw by draw (the loss bisect)";
#ifdef _WIN32
static HANDLE act_h = INVALID_HANDLE_VALUE;
#else
static int act_fd = -1;
#endif
static size_t act_len;

void ico_diag_set_version(const char *version)
{
    snprintf(act_version, sizeof act_version, "%s", version != NULL ? version : "");
}

/* 0 when the file is open for the writes */
static int act_open(void)
{
#ifdef _WIN32
    LARGE_INTEGER size;
    if (act_h != INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (act_wpath[0] == L'\0') {
        return -1;
    }
    act_h = CreateFileW(act_wpath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (act_h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    act_len = GetFileSizeEx(act_h, &size) && size.QuadPart > 0 ? (size_t)size.QuadPart : 0;
#else
    off_t end;
    if (act_fd >= 0) {
        return 0;
    }
    if (act_path[0] == '\0') {
        return -1;
    }
    act_fd = open(act_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (act_fd < 0) {
        return -1;
    }
    end = lseek(act_fd, 0, SEEK_END);
    act_len = end > 0 ? (size_t)end : 0;
#endif
    return 0;
}

/* the writes are done (the file keeps what it holds) */
static void act_close(void)
{
#ifdef _WIN32
    if (act_h != INVALID_HANDLE_VALUE) {
        CloseHandle(act_h);
        act_h = INVALID_HANDLE_VALUE;
    }
#else
    if (act_fd >= 0) {
        close(act_fd);
        act_fd = -1;
    }
#endif
}

/* Whether a note goes to the disc before the work it names starts (issue
   107: the Adreno 740 froze the whole device while its driver compiled a
   batch, and after the reboot a note still in the system's cache would be
   gone): a start-up batch's note ("creating pipelines ...", about 20 a
   cold start, a few ms each), any note naming a pipeline of the VU vertex
   pre-pass or the record path (startup_retry_note_has_vu_pre: its
   self-test's, before the set, or a lazy run's), and in a lazy run each
   pipeline's note (issues 93 and 107: the Adreno 740 makes every pipeline
   that way, and a freeze in one must still name it; each such creation
   takes far longer than the write). Not the single pipelines of the
   progress screen's frames. */
static int act_sync_note(const char *note)
{
#ifdef __ANDROID__
    /* issue 161: the key inside the driver now ("creating pipeline N of M
       (in batch A-B): ...", rd_pipeline.c noteKey), on a phone: a freeze
       inside it must still name it */
    if (strncmp(note, "creating pipeline ", 18) == 0 && strstr(note, " (in batch ") != NULL) {
        return 1;
    }
#endif
    return strncmp(note, "creating pipelines ", 19) == 0 || startup_retry_note_has_vu_pre(note) ||
           (act_mode == ICO_DIAG_ACTIVITY_LAZY && strncmp(note, "creating pipeline ", 18) == 0);
}

/* the start-up batches' compile times (ico_diag_startup_activity_batch,
   the file's "compiled: " lines) and the system's low-memory signals
   (ico_diag_note_low_memory, its "system: " lines), in order, the last
   ICO_DIAG_ACTIVITY_BATCHES, each kept with its prefix */
#define ACT_BATCH_BYTES 480
static const char act_batch_prefix[] = "compiled: ";
static const char act_system_prefix[] = "system: ";
static char act_batches[ICO_DIAG_ACTIVITY_BATCHES][ACT_BATCH_BYTES];
static unsigned int act_nbatches;
#define ACT_BATCHES_TEXT (ICO_DIAG_ACTIVITY_BATCHES * (ACT_BATCH_BYTES + 1))
/* a low-memory signal from another thread waiting for the main thread's
   next write (ico_diag_startup_activity_poll), under the log's lock */
static char act_system_pending[ACT_BATCH_BYTES];
static volatile int act_system_waiting;

/* a write of the batches' lines left in the system's cache
   (ico_diag_startup_activity_batch): the next write syncs it */
static int act_unsynced;

/* sync: the note goes to the disc (act_sync_note), or the write is a
   batch's line (0: it goes with the next note's sync, one sync a batch) */
static void act_write_sync(const char *note, int sync)
{
    /* the version's line, then the note, then the careful set's line
       while it records, then the low-memory stop's line while it records,
       then this run's loss when there was one, then the
       batches' lines; room for the NULs over a longer text before it (one
       past the longest text) */
    static char text[sizeof act_version + 1 + sizeof activity + 1 + sizeof act_set_careful + 1 +
                     sizeof act_low_memory + 1 + sizeof act_loss + 1 + ACT_BATCHES_TEXT + 1];
    const size_t cap = sizeof text - 1;
    int len = snprintf(text, cap, "%s\n%s%s%s%s%s%s%s", act_version, note,
                       act_careful && act_mode != 0 ? "\n" : "",
                       act_careful && act_mode != 0 ? act_set_careful : "",
                       act_low_memory[0] != '\0' && act_mode != 0 ? "\n" : "",
                       act_low_memory[0] != '\0' && act_mode != 0 ? act_low_memory : "",
                       act_loss[0] != '\0' ? "\n" : "", act_loss);
    {
        const unsigned int nb = act_nbatches;
        const unsigned int first =
            nb > ICO_DIAG_ACTIVITY_BATCHES ? nb - ICO_DIAG_ACTIVITY_BATCHES : 0;

        for (unsigned int i = first; i < nb && len >= 0 && (size_t)len < cap; i++) {
            const int k = snprintf(text + len, cap - (size_t)len, "\n%s",
                                   act_batches[i % ICO_DIAG_ACTIVITY_BATCHES]);
            len = k < 0 ? len : len + k;
        }
    }
    const size_t n = len < 0 ? 0 : (size_t)len < cap ? (size_t)len : cap - 1;
    size_t w = n;
    if (act_open() != 0) {
        return;
    }
    if (act_len > n) {
        w = act_len < sizeof text ? act_len : sizeof text;
        memset(text + n, 0, w - n);
    }
#ifdef _WIN32
    {
        DWORD done = 0;
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        LARGE_INTEGER at;
        at.QuadPart = (LONGLONG)n;
        if (SetFilePointerEx(act_h, zero, NULL, FILE_BEGIN) &&
            WriteFile(act_h, text, (DWORD)w, &done, NULL) && done == (DWORD)w &&
            SetFilePointerEx(act_h, at, NULL, FILE_BEGIN) && SetEndOfFile(act_h)) {
            act_len = n;
            if (sync && (act_unsynced || act_sync_note(note))) {
                (void)FlushFileBuffers(act_h);
                act_unsynced = 0;
            } else if (!sync) {
                act_unsynced = 1;
            }
            return;
        }
    }
#else
    if (pwrite(act_fd, text, w, 0) == (ssize_t)w && ftruncate(act_fd, (off_t)n) == 0) {
        act_len = n;
        if (sync && (act_unsynced || act_sync_note(note))) {
            (void)fsync(act_fd);
            act_unsynced = 0;
        } else if (!sync) {
            act_unsynced = 1;
        }
        return;
    }
#endif
    /* a failed step: the file may reach as far as this write */
    if (act_len < w) {
        act_len = w;
    }
}

static void act_write(const char *note)
{
    act_write_sync(note, 1);
}

/* a lazy run's file between two creations: the next start stays lazy
   when this run is ended outside one */
static const char act_between[] =
    "running with the pipelines created as the frames need them (none was being created)";
/* ... and after its clean exit: the next start is lazy too, and says it
   is no ended run */
static const char act_lazy_exit[] = "exited with the pipelines created as the frames needed them";
/* the same two of a start whose lazy run was for that start only (the
   first end while preparing the graphics, ico_diag_startup_pipeline_set):
   the next start tries the start-up set again */
static const char act_between_once[] = "running with the pipelines created as the frames need them "
                                       "for this start only (none was being created)";
static const char act_lazy_exit_once[] =
    "exited with the pipelines created as the frames needed them for that start only";
/* this start's lazy run is for this start only */
static int act_once;
/* this run built a pipeline or batch of the VU vertex pre-pass or the
   record path (startup_retry_note_has_vu_pre; its note while recording):
   its clean end clears the count of ends inside one (ends_clean) */
static int act_saw_vu_pre;

static const char *act_between_note(void)
{
    return act_once ? act_between_once : act_between;
}

/* the note a write of the batches' lines keeps: the run's own, else what
   the mode's file says between two creations */
static const char *act_current(void)
{
    if (activity[0] != '\0') {
        return activity;
    }
    return act_mode == ICO_DIAG_ACTIVITY_LAZY ? act_between_note()
                                              : "creating the start-up pipelines";
}

/* every note set or cleared (the watchdog's pipeline limit, below: a
   creation that has not returned keeps the same note and count) */
static volatile unsigned int activity_seq;

void ico_diag_set_activity(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(activity, sizeof activity, fmt, ap);
    va_end(ap);
    activity_seq++;
    /* the start-up set's own "(i of m)" notes between two pipelines are
       left out: the pipeline's note names its place in the set, and this
       halves the writes */
    if (act_mode == ICO_DIAG_ACTIVITY_LAZY ||
        (act_mode == ICO_DIAG_ACTIVITY_STARTUP &&
         strncmp(activity, "creating the start-up pipelines", 31) != 0)) {
        act_write(activity);
    }
    if (act_mode != 0 && !act_saw_vu_pre && startup_retry_note_has_vu_pre(activity)) {
        act_saw_vu_pre = 1;
    }
}

void ico_diag_clear_activity(void)
{
    activity[0] = '\0';
    activity_seq++;
    /* the start-up set's mode keeps the last note: a frame the progress
       screen presents between two of its pipelines clears it, and the
       next one follows at once */
    if (act_mode == ICO_DIAG_ACTIVITY_LAZY) {
        act_write(act_between_note());
    }
}

const char *ico_diag_activity(void)
{
    return activity;
}

void ico_diag_startup_activity_batch(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }
    snprintf(act_batches[act_nbatches % ICO_DIAG_ACTIVITY_BATCHES], ACT_BATCH_BYTES, "%s%s",
             act_batch_prefix, line);
    act_nbatches++;
    /* written at once with the batch's note still in the file, not synced:
       the next note's write syncs it with that note (a batch's note goes
       to the disc before its batch, act_sync_note), so each batch costs
       one sync and a device that freezes in the next batch still leaves
       this line */
    if (act_mode != 0) {
        act_write_sync(act_current(), 0);
    }
}

/* --- The ends while preparing the graphics: counted, and the background ---- */

/* ico_diag_set_pipelines_made: the pipelines made so far (a run without
   the start-up set) */
static volatile unsigned int pipes_made;

/* Beside the activity file (its name without ".txt", then the suffix):
   "-ends.txt" counts the starts in a row that ended without a word while
   preparing the graphics, by kind, and those that lost the graphics device
   (ico_diag_startup_end_count: "pipelines <n>", "vu_pre <n>", "loss <n>",
   and "loss_robust 1" while the start that ran with robust access on for a
   loss has not been followed by the next); "-background.txt" is the mark of a run in the
   background while it records (ico_diag_startup_activity_background), with
   the system's signals that came there. Both read as untrusted. */
static char act_ends_path[1024 + 16];
static char act_bg_path[1024 + 16];
/* the previous run's mark (ico_diag_startup_activity_begin) */
static int act_prev_background;
static char act_prev_bg_line[200];
/* the mark of this run: set, and its text (version, the line, then the
   system's lines), under the log's lock (any thread) */
static volatile int act_bg;
#define ACT_BG_TEXT (sizeof act_version + 200 + 4 * ACT_BATCH_BYTES)
static char act_bg_text[ACT_BG_TEXT];
static const char act_bg_prefix[] = "background: ";

static void act_sibling(char *out, size_t size, const char *suffix)
{
    size_t n = strlen(act_path);
    if (n >= 4 && strcmp(act_path + n - 4, ".txt") == 0) {
        n -= 4;
    }
    snprintf(out, size, "%.*s%s", (int)n, act_path, suffix);
}

#define ACT_END_KINDS 3
/* the counts file's slots: the kinds (ICO_DIAG_END_*), then the mark of a
   start that ran with robust access on for a loss
   (ico_diag_startup_end_persist), which the next start's loss_count_prev
   reads and clears */
#define ACT_END_ROBUST_ONCE 3
#define ACT_END_SLOTS 4

/* the counts by kind (ICO_DIAG_END_*), 0 when none or unreadable; an
   older version's "vu_ext <n>" (the retired extended VU kind) is dropped */
static void ends_read(unsigned int n[ACT_END_SLOTS])
{
    char line[64];
    FILE *f;
    n[0] = n[1] = n[2] = n[3] = 0;
    if (act_ends_path[0] == '\0' || (f = ico_fopen(act_ends_path, "rb")) == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        unsigned int v;
        if (sscanf(line, "pipelines %u", &v) == 1) {
            n[ICO_DIAG_END_PIPELINES] = v > 9 ? 9 : v;
        } else if (sscanf(line, "vu_pre %u", &v) == 1) {
            n[ICO_DIAG_END_VU_PRE] = v > 9 ? 9 : v;
        } else if (sscanf(line, "loss_robust %u", &v) == 1) {
            n[ACT_END_ROBUST_ONCE] = v != 0;
        } else if (sscanf(line, "loss %u", &v) == 1) {
            n[ICO_DIAG_END_LOSS] = v > 9 ? 9 : v;
        }
    }
    fclose(f);
}

/* the counts written (all 0: the file deleted); a write that fails is
   logged once a run */
static void ends_write(const unsigned int n[ACT_END_SLOTS])
{
    static int logged;
    FILE *f;
    int ok;
    if (act_ends_path[0] == '\0') {
        return;
    }
    if (n[0] == 0 && n[1] == 0 && n[2] == 0 && n[3] == 0) {
        (void)ico_remove(act_ends_path);
        return;
    }
    f = ico_fopen(act_ends_path, "wb");
    ok = f != NULL &&
         fprintf(f, "pipelines %u\nvu_pre %u\nloss %u\nloss_robust %u\n", n[ICO_DIAG_END_PIPELINES],
                 n[ICO_DIAG_END_VU_PRE], n[ICO_DIAG_END_LOSS], n[ACT_END_ROBUST_ONCE]) > 0;
    if (f != NULL && fclose(f) != 0) {
        ok = 0;
    }
    if (!ok && !logged) {
        logged = 1;
        ico_diag_log("ico_pc: the count of starts that ended while preparing the graphics could "
                     "not be written to %s (pipelines %u, vertex pre-pass %u, device losses %u)",
                     act_ends_path, n[ICO_DIAG_END_PIPELINES], n[ICO_DIAG_END_VU_PRE],
                     n[ICO_DIAG_END_LOSS]);
    }
}

/* one kind's count set to 0 in the file (its setting was written, or the
   device table already rules it) */
static void ends_clear_kind(int kind)
{
    unsigned int n[ACT_END_SLOTS];
    ends_read(n);
    if (n[kind] != 0) {
        n[kind] = 0;
        ends_write(n);
    }
}

/* one slot set to v in the file */
static void ends_set_slot(int slot, unsigned int v)
{
    unsigned int n[ACT_END_SLOTS];
    ends_read(n);
    if (n[slot] != v) {
        n[slot] = v;
        ends_write(n);
    }
}

/* Whether a note names a pipeline being made: one ("creating pipeline
   ..."), a batch of the start-up set ("creating pipelines ...") or the set
   between two ("creating the start-up pipelines") */
static int note_is_pipelines(const char *note)
{
    return note != NULL && (strncmp(note, "creating pipeline", 17) == 0 ||
                            strncmp(note, "creating the start-up pipelines", 31) == 0);
}

/* the count the previous run's end reached, by kind (0: that kind's end
   was not counted), set once by ico_diag_startup_activity_begin */
static unsigned int act_end_n[ACT_END_KINDS];

static const char *end_kind_name(int kind)
{
    return kind == ICO_DIAG_END_VU_PRE ? "the vertex pre-pass" : "the pipelines";
}

/* ico_diag_startup_activity_begin: the previous run's end into the counts
   (its note prev, read whatever its version). An end of a kind in the
   foreground counts one more; the second in a row is due, and the count
   stays until ico_diag_startup_end_persist writes the setting (or finds
   the device table rules it) and sets it to 0, so a start that fails
   before the device is made leaves it for the next end. An end of a kind
   in the background breaks the row of that kind. Any other note (the run ended outside a creation)
   breaks the row of ends while making the pipelines; the row of ends
   inside the vertex pre-pass stays across the runs that build none of its
   pipelines (the run without it its first end gives), and is cleared by a
   run that built one and finished (ends_clean). A restart after a crash
   report counts nothing: its own step decides. The losses are
   loss_count_prev's. */
static void ends_count_prev(const char *prev, int relaunched)
{
    unsigned int n[ACT_END_SLOTS], was[ACT_END_SLOTS];
    int kind = -1;
    act_end_n[ICO_DIAG_END_PIPELINES] = act_end_n[ICO_DIAG_END_VU_PRE] = 0;
    if (relaunched || act_ends_path[0] == '\0' || prev[0] == '\0') {
        return;
    }
    /* a pipeline of the vertex pre-pass or the record path first (the
       next start draws without them), then any other */
    if (startup_retry_note_has_vu_pre(prev)) {
        kind = ICO_DIAG_END_VU_PRE;
    } else if (note_is_pipelines(prev)) {
        kind = ICO_DIAG_END_PIPELINES;
    }
    ends_read(n);
    memcpy(was, n, sizeof was);
    if (kind < 0) {
        n[ICO_DIAG_END_PIPELINES] = 0;
    } else if (act_prev_background) {
        n[kind] = 0;
    } else {
        n[kind]++;
        act_end_n[kind] = n[kind];
        ico_diag_log("ico_pc: starts in a row that ended while preparing the graphics (%s): %u",
                     end_kind_name(kind), n[kind]);
    }
    if (memcmp(n, was, sizeof was) != 0) {
        ends_write(n);
    }
}

int ico_diag_startup_end_count(int kind)
{
    if (kind < 0 || kind >= ACT_END_KINDS) {
        return 0;
    }
    return act_end_n[kind] > 2 ? 2 : (int)act_end_n[kind];
}

/* a clean end of this run (the start-up set finished, a lazy run's normal
   exit): the row of ends while making the pipelines is broken, and the row
   of ends inside the vertex pre-pass's when this run built one of its */
static void ends_clean(void)
{
    unsigned int n[ACT_END_SLOTS];
    ends_read(n);
    if (n[ICO_DIAG_END_PIPELINES] != 0 || (act_saw_vu_pre && n[ICO_DIAG_END_VU_PRE] != 0)) {
        n[ICO_DIAG_END_PIPELINES] = 0;
        if (act_saw_vu_pre) {
            n[ICO_DIAG_END_VU_PRE] = 0;
        }
        ends_write(n);
    }
}

/* ico_diag_startup_activity_begin: the previous run's device loss
   (prev_loss: its file's loss line) into the row of starts that lost it.
   A loss counts one more, whatever the version or the note; a start
   without one breaks the row, except the start that ran with robust access
   on because of the row (its mark, ACT_END_ROBUST_ONCE, set by
   ico_diag_startup_end_persist), which leaves it: the start after that
   one, with robust access off again, losing the device is the second in a
   row. A restart after a crash report or a loss (relaunched: the desktop's
   loss order took its own step) counts nothing. */
static void loss_count_prev(int prev_loss, int relaunched)
{
    unsigned int n[ACT_END_SLOTS], was[ACT_END_SLOTS];
    act_end_n[ICO_DIAG_END_LOSS] = 0;
    if (relaunched || act_ends_path[0] == '\0') {
        return;
    }
    ends_read(n);
    memcpy(was, n, sizeof was);
    if (prev_loss) {
        n[ICO_DIAG_END_LOSS]++;
        act_end_n[ICO_DIAG_END_LOSS] = n[ICO_DIAG_END_LOSS];
        ico_diag_log("ico_pc: starts in a row that lost the graphics device: %u",
                     n[ICO_DIAG_END_LOSS]);
    } else if (!n[ACT_END_ROBUST_ONCE]) {
        n[ICO_DIAG_END_LOSS] = 0;
    }
    n[ACT_END_ROBUST_ONCE] = 0;
    if (memcmp(n, was, sizeof was) != 0) {
        ends_write(n);
    }
}

/* the mark's file written whole from act_bg_text (under the lock) */
static void bg_write_locked(void)
{
    FILE *f;
    if (act_bg_path[0] == '\0') {
        return;
    }
    f = ico_fopen(act_bg_path, "wb");
    if (f != NULL) {
        fputs(act_bg_text, f);
        fclose(f);
    }
}

/* the mark deleted (under the lock); 1 when there was one */
static int bg_remove(void)
{
    int had;
    if (inited) {
        lock_take();
    }
    had = act_bg;
    act_bg = 0;
    act_bg_text[0] = '\0';
    if (act_bg_path[0] != '\0') {
        (void)ico_remove(act_bg_path);
    }
    if (inited) {
        lock_give();
    }
    return had;
}

/* where the run is, for the mark and the low-memory line */
static void act_where(char *out, size_t size)
{
    const unsigned int total = pipes_total;
    if (total > 0) {
        snprintf(out, size, "at pipelines %u of %u of the start-up set", pipes_done, total);
    } else {
        snprintf(out, size, "with %u pipelines made", pipes_made);
    }
}

/* the app is in the background (its last signal said so), whether the
   activity file records or not: a start that goes there before the
   recording begins (Home during the renderer's start) is marked when it
   does (ico_diag_startup_activity_record) */
static volatile int act_in_bg;

/* The mark written while the file records, once: act_bg checked again
   under the lock, so of two threads that both see it unset (the lifecycle
   watch, onTrimMemory(20) on Android's Java thread) only the one that sets
   it writes the text and logs. before: the app was there before the
   recording began. */
static void bg_mark(int before)
{
    char where[96], what[200];
    int set = 0;
    act_where(where, sizeof where);
    if (before) {
        snprintf(what, sizeof what,
                 "the app was in the background before preparing the graphics began (%s)", where);
    } else {
        snprintf(what, sizeof what, "the app went to the background %s", where);
    }
    if (inited) {
        lock_take();
    }
    if (act_mode != 0 && !act_bg) {
        act_bg = 1;
        set = 1;
        snprintf(act_bg_text, sizeof act_bg_text, "%s\n%s%s\n", act_version, act_bg_prefix, what);
        bg_write_locked();
    }
    if (inited) {
        lock_give();
    }
    if (set) {
        ico_diag_log("ico_pc: %s; an end there is not counted as one while preparing the graphics",
                     what);
    }
}

void ico_diag_startup_activity_background(int background)
{
    if (!background) {
        act_in_bg = 0;
        if (act_bg && bg_remove()) {
            ico_diag_log("ico_pc: back in the foreground; the background mark is deleted");
        }
        return;
    }
    act_in_bg = 1;
    if (act_mode == 0 || act_bg) {
        return; /* not recording (the recording's start marks it), or marked already */
    }
    bg_mark(0);
}

/* a system line into the mark while the app is in the background (any
   thread: the main thread may be held in the paused pump) */
static void bg_add_system(const char *line)
{
    if (!act_bg) {
        return;
    }
    if (inited) {
        lock_take();
    }
    if (act_bg) {
        const size_t n = strlen(act_bg_text);
        snprintf(act_bg_text + n, sizeof act_bg_text - n, "%s\n", line);
        bg_write_locked();
    }
    if (inited) {
        lock_give();
    }
}

/* --- The system's low-memory signals (issue 93) ------------------------------ */

static volatile unsigned int low_memory_count;

void ico_diag_set_pipelines_made(unsigned int n)
{
    pipes_made = n;
}

/* Android's ComponentCallbacks2 TRIM_MEMORY_* levels: 5, 10 and 15 come
   while the app runs and the device is low on memory; 20 when its screen
   is hidden (the app left the foreground), 40, 60 and 80 while it is in
   the background, its place in the system's list of cached processes (80:
   among the next to be ended; Android 14 and later send 20 and 40 only) */
static const char *trim_name(int level)
{
    switch (level) {
    case 5:
        return "running moderate";
    case 10:
        return "running low";
    case 15:
        return "running critical";
    case 20:
        return "UI hidden";
    case 40:
        return "background";
    case 60:
        return "moderate, the middle of the cached list";
    case 80:
        return "complete, among the next to be ended";
    default:
        return "a level not named";
    }
}

/* what the signal says of the app: low memory while it runs, or that it
   left the foreground (not a shortage of its own) */
static const char *trim_kind(int level)
{
    if (level == 20) {
        return "the app's screen was hidden";
    }
    if (level == 40 || level == 60 || level == 80) {
        return "the app is in the background";
    }
    if (level < 0 || level == 5 || level == 10 || level == 15) {
        return "low memory";
    }
    return "a memory signal";
}

/* The start-up set stops at the system's low-memory signal (issue 128:
   the Adreno 740 handheld's set went on through four of them, the system
   took the process's pages back and then ended it): the level that asks
   the set to stop, 0 none, set from the signal's thread (Android's Java
   thread) and read by the set's loop between two batches
   (ico_diag_low_memory_stop through rd_set_low_memory_fn). 15 ("running
   critical") at once, whenever it comes before the set's end; 10
   ("running low") once the set is past its first batch (pipes_done: a
   "running low" while the app is opened can be the system making room
   for it, one batch later the shortage is the device's while the set
   runs); 5 ("running moderate") and a signal without a level never. */
static volatile int low_memory_stop_level;
/* the set stopped on it (ico_diag_low_memory_stop answered yes): the
   window's ico_diag_startup_activity_budget_stop persists it */
static volatile int low_memory_stopped;

static void low_memory_stop_note(int level)
{
    if (level == 15 || (level == 10 && pipes_total > 0 && pipes_done > 0)) {
        int cur = __atomic_load_n(&low_memory_stop_level, __ATOMIC_ACQUIRE);
        /* the strongest level asked so far (15 over 10) */
        while (level > cur && !__atomic_compare_exchange_n(&low_memory_stop_level, &cur, level, 0,
                                                           __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {}
    }
}

/* The free-memory floor (memory_floor.h, issue 161): on Android the set
   also stops once MemAvailable is under the floor ([dev]
   pipeline_memory_floor_mb, exported as ICO_PIPELINE_MEMORY_FLOOR_MB; the
   default the larger of 1536 MB and 17 % of MemTotal, 1915 MB on the
   reporter's 11265 MB), and is recorded as
   the low-memory signal's stop (the next start makes no set). The v0.9.5
   Retroid Pocket 6 start stalled in the driver at 1693 MB of 11265 MB
   available with no onTrimMemory signal. floor_why: the stop's words, ""
   until it stops (the set's thread only) */
static char floor_why[96];

static int memory_floor_stop(void)
{
#ifdef __ANDROID__
    static long setting = -3; /* -3: not read yet */
    long avail_kb, total_kb, avail, total, floor;
    if (floor_why[0] != '\0') {
        return 1;
    }
    if (setting == -3) {
        const char *v = getenv("ICO_PIPELINE_MEMORY_FLOOR_MB");
        setting = ico_memory_floor_setting(v);
        if (setting == -2) {
            ico_diag_log("ico_pc: [dev] pipeline_memory_floor_mb = \"%s\" is not a whole number of "
                         "MB; the default floor",
                         v);
            setting = -1;
        }
    }
    if (setting == 0 || ico_diag_device_memory(&avail_kb, &total_kb) != 0) {
        return 0;
    }
    avail = avail_kb / 1024;
    total = total_kb / 1024;
    if (!ico_memory_floor_stops(avail, total, setting)) {
        return 0;
    }
    floor = ico_memory_floor_mb(total, setting);
    snprintf(floor_why, sizeof floor_why, "%ld MB available of %ld MB, floor %ld MB", avail, total,
             floor);
    ico_diag_log(
        "ico_pc: pipelines: the start-up set stopped at %ld MB available of %ld MB (floor %ld "
        "MB): the rest made on first use",
        avail, total, floor);
    return 1;
#else
    return 0;
#endif
}

int ico_diag_low_memory_stop(char *why, size_t size)
{
    const int level = __atomic_load_n(&low_memory_stop_level, __ATOMIC_ACQUIRE);
    if (level == 0) {
        if (!memory_floor_stop()) {
            return 0;
        }
        low_memory_stopped = 1;
        if (why != NULL && size > 0) {
            snprintf(why, size, "%s", floor_why);
        }
        return 1;
    }
    low_memory_stopped = 1;
    if (why != NULL && size > 0) {
        snprintf(why, size, "%s", trim_name(level));
    }
    return 1;
}

void ico_diag_note_low_memory(int level)
{
    char line[ACT_BATCH_BYTES - sizeof act_system_prefix], pipes[96], mem[160], lv[128];
    const unsigned int nth = ++low_memory_count;
    /* before the line: the set reads the flag between two batches */
    low_memory_stop_note(level);
    act_where(pipes, sizeof pipes);
    ico_diag_memory_text(mem, sizeof mem);
    if (mem[0] == '\0') {
        snprintf(mem, sizeof mem, "memory unknown");
    }
    if (level >= 0) {
        snprintf(lv, sizeof lv, "onTrimMemory level %d, %s", level, trim_name(level));
    } else {
        snprintf(lv, sizeof lv, "no level given");
    }
    snprintf(line, sizeof line, "%s (%s; signal %u of this run) %s; %s", trim_kind(level), lv, nth,
             pipes, mem);
    ico_diag_log("ico_pc: the system asked the app to free memory: %s", line);
    /* 20 and above: the app left the foreground (the lifecycle's event may
       come later, or not reach a held main thread); the line goes into the
       mark at once, from this thread */
    if (level >= 20) {
        ico_diag_startup_activity_background(1);
    }
    {
        char sys[sizeof act_system_pending];
        snprintf(sys, sizeof sys, "%s%s", act_system_prefix, line);
        bg_add_system(sys);
    }
    if (inited) {
        lock_take();
    }
    snprintf(act_system_pending, sizeof act_system_pending, "%s%s", act_system_prefix, line);
    act_system_waiting = 1;
    if (inited) {
        lock_give();
    }
}

void ico_diag_startup_activity_poll(void)
{
    char line[sizeof act_system_pending];
    if (!act_system_waiting) {
        return;
    }
    if (inited) {
        lock_take();
    }
    memcpy(line, act_system_pending, sizeof line);
    act_system_waiting = 0;
    if (inited) {
        lock_give();
    }
    if (act_mode == 0) {
        return; /* not recording: the log's line only */
    }
    snprintf(act_batches[act_nbatches % ICO_DIAG_ACTIVITY_BATCHES], ACT_BATCH_BYTES, "%s", line);
    act_nbatches++;
    act_write(act_current());
}

static void act_remove(void)
{
    if (act_bg) {
        (void)bg_remove();
    }
    /* closed first: Windows keeps an open file's name until its handle
       goes */
    act_close();
#ifdef _WIN32
    if (act_wpath[0] != L'\0') {
        DeleteFileW(act_wpath);
    }
#else
    if (act_path[0] != '\0') {
        unlink(act_path);
    }
#endif
}

/* the note the previous run's file held (ico_diag_startup_activity_prev) */
static char act_prev[sizeof activity];

const char *ico_diag_startup_activity_prev(void)
{
    return act_prev;
}

int ico_diag_startup_activity_begin(const char *path, int relaunched)
{
    static char buf[sizeof act_version + 1 + sizeof activity + 1 + sizeof act_set_careful + 1 +
                    sizeof act_low_memory + 1 + sizeof act_loss + 1 + ACT_BATCHES_TEXT];
    const char *ver = "";
    char *prev = buf;
    char *loss = NULL;
    const char *lastBatch = NULL;
    char *nl;
    size_t n = 0;
    FILE *f;
    act_close();
    act_mode = 0;
    act_nbatches = 0;
    act_path[0] = '\0';
    act_loss[0] = '\0';
    act_prev[0] = '\0';
    loss_pending[0] = '\0';
    loss_prev_hang[0] = '\0';
    act_once = 0;
    act_careful = 0;
    act_prev_careful = 0;
    act_low_memory[0] = '\0';
    act_prev_low_memory = 0;
    act_prev_low_memory_line[0] = '\0';
    act_saw_vu_pre = 0;
    act_unsynced = 0;
    act_end_n[0] = act_end_n[1] = act_end_n[2] = 0;
    act_prev_background = 0;
    act_prev_bg_line[0] = '\0';
    act_ends_path[0] = '\0';
    act_bg_path[0] = '\0';
    if (path == NULL || strlen(path) >= sizeof act_path) {
        return 0;
    }
    memcpy(act_path, path, strlen(path) + 1);
    act_sibling(act_ends_path, sizeof act_ends_path, "-ends.txt");
    act_sibling(act_bg_path, sizeof act_bg_path, "-background.txt");
    /* the previous run's background mark: its line, and the system's lines
       that came there (logged below unless the activity file has them) */
    static char bgbuf[ACT_BG_TEXT];
    bgbuf[0] = '\0';
    f = ico_fopen(act_bg_path, "rb");
    if (f != NULL) {
        const size_t k = fread(bgbuf, 1, sizeof bgbuf - 1, f);
        fclose(f);
        bgbuf[k] = '\0';
        (void)ico_remove(act_bg_path);
        {
            const char *b = strstr(bgbuf, "\nbackground: ");
            if (b != NULL) {
                b += 1 + sizeof act_bg_prefix - 1;
                act_prev_background = 1;
                snprintf(act_prev_bg_line, sizeof act_prev_bg_line, "%.*s", (int)strcspn(b, "\r\n"),
                         b);
            }
        }
    }
#ifdef _WIN32
    act_wpath[0] = L'\0';
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, act_path, -1, act_wpath,
                            (int)(sizeof act_wpath / sizeof act_wpath[0])) == 0) {
        act_wpath[0] = L'\0';
    }
#endif
    f = ico_fopen(act_path, "rb");
    if (f == NULL) {
        /* no file: the previous run lost no device (a loss keeps it) */
        loss_count_prev(0, relaunched);
        return 0;
    }
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* the version's line, then the note's (a file without the version's
       line counts as another version's); a NUL ends the text (act_write's
       padding) */
    nl = strchr(buf, '\n');
    if (nl != NULL) {
        *nl = '\0';
        ver = buf;
        prev = nl + 1;
        if (nl > buf && nl[-1] == '\r') {
            nl[-1] = '\0';
        }
    }
    /* the note's line, then a device loss's (ico_diag_note_device_loss)
       and the batches' compile times ("compiled: ", in order) */
    nl = strchr(prev, '\n');
    while (nl != NULL) {
        char *line = nl + 1;

        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        line[strcspn(line, "\r")] = '\0';
        if (strncmp(line, act_batch_prefix, sizeof act_batch_prefix - 1) == 0) {
            /* issue 107: the sizes and times of the batches a start that
               froze did finish */
            lastBatch = line + sizeof act_batch_prefix - 1;
            ico_diag_log("ico_pc: the previous run (version %s) compiled: %s",
                         ver[0] != '\0' ? ver : "unknown", lastBatch);
        } else if (strncmp(line, act_system_prefix, sizeof act_system_prefix - 1) == 0) {
            /* issue 93: the system's low-memory signals, in their place
               among the batches */
            ico_diag_log("ico_pc: the previous run (version %s) was told by the system: %s",
                         ver[0] != '\0' ? ver : "unknown", line + sizeof act_system_prefix - 1);
            /* not again from the mark */
            {
                char *dup = strstr(bgbuf, line);
                if (dup != NULL) {
                    memset(dup, ' ', strlen(line));
                }
            }
        } else if (strcmp(line, act_set_careful) == 0) {
            /* the ended start's set was the careful one */
            act_prev_careful = 1;
        } else if (strncmp(line, act_set_low_memory, sizeof act_set_low_memory - 1) == 0) {
            /* issue 128: its set stopped at the system's low-memory
               signal (ico_diag_startup_pipeline_set) */
            act_prev_low_memory = 1;
            snprintf(act_prev_low_memory_line, sizeof act_prev_low_memory_line, "%s", line);
        } else if (loss == NULL && line[0] != '\0') {
            loss = line;
        }
    }
    /* one line: a cut write leaves no newline to strip, a hand-edited file
       may have one */
    prev[strcspn(prev, "\r\n")] = '\0';
    if (lastBatch != NULL) {
        /* issues 93 and 107: the last batch it finished, with the memory
           and the pipeline cache's size after it (the system ending a
           start shows as memory that grew batch by batch, or not) */
        ico_diag_log("ico_pc: the previous run's last finished batch: %s", lastBatch);
    }
    /* the mark's system lines the activity file did not get (the main
       thread was held in the background) */
    for (const char *l = bgbuf; act_prev_background && *l != '\0';) {
        const size_t len = strcspn(l, "\r\n");
        if (strncmp(l, act_system_prefix, sizeof act_system_prefix - 1) == 0) {
            ico_diag_log("ico_pc: the previous run was told by the system in the background: %.*s",
                         (int)(len - (sizeof act_system_prefix - 1)),
                         l + sizeof act_system_prefix - 1);
        }
        l += len;
        l += strspn(l, "\r\n");
    }
    snprintf(act_prev, sizeof act_prev, "%.*s", (int)(sizeof act_prev - 1), prev);
    act_remove();
    ends_count_prev(prev, relaunched);
    loss_count_prev(loss != NULL &&
                        (strcmp(loss, act_loss_none) == 0 ||
                         strncmp(loss, act_loss_prefix, sizeof act_loss_prefix - 1) == 0 ||
                         strcmp(loss, act_loss_hang_none) == 0 ||
                         strncmp(loss, act_loss_hang_prefix, sizeof act_loss_hang_prefix - 1) == 0),
                    relaunched);
    if (act_prev_background && (relaunched || !note_is_pipelines(prev))) {
        /* the mark of a run that ended in the background outside a
           creation (a lazy run between two, a bisect): said, nothing
           decided by it (an end inside one is logged below) */
        ico_diag_log("ico_pc: the previous run was in the background when it ended (%s)",
                     act_prev_bg_line);
    }
    if (loss != NULL && strcmp(loss, act_loss_none) == 0) {
        ico_diag_log("ico_pc: the previous run lost the graphics device (no loss dump was "
                     "written)");
    } else if (loss != NULL && strcmp(loss, act_loss_hang_none) == 0) {
        ico_diag_log("ico_pc: the previous run ended because the graphics driver stopped "
                     "responding (a GPU hang, counted as a device loss; no loss dump was "
                     "written)");
    } else if (loss != NULL &&
               strncmp(loss, act_loss_hang_prefix, sizeof act_loss_hang_prefix - 1) == 0) {
        /* counted above; the dump goes with a report, never to the bisect */
        const char *zip = loss + sizeof act_loss_hang_prefix - 1;
        snprintf(loss_prev_hang, sizeof loss_prev_hang, "%s", zip);
        ico_diag_log("ico_pc: the previous run ended because the graphics driver stopped "
                     "responding (a GPU hang, counted as a device loss); its loss dump %s is not "
                     "replayed (the replay could hang the same way)",
                     zip);
    } else if (loss != NULL && strncmp(loss, act_loss_prefix, sizeof act_loss_prefix - 1) == 0 &&
               loss[sizeof act_loss_prefix - 1] != '\0') {
        const char *zip = loss + sizeof act_loss_prefix - 1;
        if (strcmp(ver, act_version) != 0) {
            /* the bisect replays the dump on the renderer that lost it */
            ico_diag_log("ico_pc: the previous run (version %s) lost the graphics device; its "
                         "loss dump %s is not replayed on %s",
                         ver[0] != '\0' ? ver : "unknown", zip, act_version);
        } else if (strlen(zip) < sizeof loss_pending) {
            memcpy(loss_pending, zip, strlen(zip) + 1);
            ico_diag_log("ico_pc: the previous run lost the graphics device; its loss dump is %s",
                         zip);
        }
    }
    if (strcmp(prev, act_bisect2) == 0) {
        /* a second-level step that never finished: its state file says
           where (ico_diag_loss_bisect2 continues it); not a lazy start */
        ico_diag_log("ico_pc: the previous run ended while: %s; it continues", prev);
        return 0;
    }
    if (strcmp(prev, act_bisect) == 0) {
        /* a bisect that never finished: named, not run again, and nothing
           to do with the pipelines */
        ico_diag_log("ico_pc: the previous run ended while: %s; it is not run again", prev);
        return 0;
    }
    if (prev[0] == '\0') {
        if (ver[0] != '\0' && strcmp(ver, act_version) != 0) {
            /* another version's file with no note: only its loss line
               kept it (a clean end deletes the file); a Vulkan renderer
               drops that version's graphics cache (vk_pipeline.c
               vkr_cache_drop_others), Direct3D 12 has none */
            ico_diag_log("ico_pc: the previous start was version %s, this one is %s: a Vulkan "
                         "renderer drops the graphics cache of that version; this start prepares "
                         "the graphics from nothing",
                         ver, act_version);
        }
        return 0;
    }
    if (strcmp(prev, act_lazy_exit_once) == 0) {
        /* the lazy run after a first end while preparing the graphics was
           for that start only */
        ico_diag_log("ico_pc: the previous run made the pipelines as the frames needed them for "
                     "that start only; this one tries the start-up set again");
        return 0;
    }
    if (strcmp(prev, act_between_once) == 0) {
        /* ... and was ended between two creations, not by one */
        ico_diag_log("ico_pc: the previous run ended while: %s; that lazy run was for that start "
                     "only, so this one tries the start-up set again",
                     prev);
        return 0;
    }
    if (act_prev_background && !relaunched && note_is_pipelines(prev)) {
        /* the system ends apps in the background (memory, a swipe from
           the recent apps): not an end the graphics caused */
        ico_diag_log("ico_pc: the previous run ended while: %s, in the background (%s); the "
                     "system ends apps there, so it is not counted as an end while preparing the "
                     "graphics: the start-up set as usual",
                     prev, act_prev_bg_line);
        return 0;
    }
    if (strcmp(ver, act_version) != 0) {
        /* another version: its fixes may have removed the cause */
        if (strcmp(prev, act_lazy_exit) == 0) {
            ico_diag_log(
                "ico_pc: the previous run (version %s) created the pipelines as the "
                "frames needed them; trying the start-up set again on %s (a Vulkan renderer "
                "drops the graphics cache of that version; this start prepares the graphics "
                "from nothing)",
                ver[0] != '\0' ? ver : "unknown", act_version);
        } else if (act_end_n[ICO_DIAG_END_PIPELINES] != 0) {
            /* an end while making the pipelines counts whatever the
               version (the setting outlives it): the policy's line says
               what this start does (ico_diag_startup_end_persist) */
            ico_diag_log("ico_pc: the previous run ended while: %s (version %s); a Vulkan "
                         "renderer drops the graphics cache of that version",
                         prev, ver[0] != '\0' ? ver : "unknown");
        } else {
            ico_diag_log(
                "ico_pc: the previous run ended while: %s (version %s); trying the "
                "start-up set again on %s (a Vulkan renderer drops the graphics cache of that "
                "version; this start prepares the graphics from nothing)",
                prev, ver[0] != '\0' ? ver : "unknown", act_version);
        }
        return 0;
    }
    if (act_prev_low_memory && !relaunched &&
        (strcmp(prev, act_lazy_exit) == 0 || strcmp(prev, act_between) == 0)) {
        /* issue 128: the set stopped at the system's low-memory signal and
           the run went on lazily (exited, or was ended between two
           creations): this start makes no set
           (ICO_DIAG_PIPESET_LAZY_LOW_MEMORY) */
        ico_diag_log("ico_pc: the previous start stopped preparing the graphics when the system "
                     "ran low on memory (%s); this start prepares them as the game needs them",
                     prev);
        return 1;
    }
    if (act_prev_low_memory) {
        /* ... and was then ended inside a creation: the count of ends
           decides (ico_diag_startup_pipeline_set), the line says why its
           set was short */
        ico_diag_log("ico_pc: the previous start had stopped preparing the graphics when the "
                     "system ran low on memory (%s)",
                     act_prev_low_memory_line);
    }
    if (strcmp(prev, act_lazy_exit) == 0) {
        /* a clean exit of a lazy run: no ended run to report */
        ico_diag_log("ico_pc: the previous run created the pipelines as the frames needed them; "
                     "this one does too");
    } else {
        ico_diag_log("ico_pc: the previous run ended while: %s", prev);
    }
    /* a restart after a driver crash (startup_retry.h) takes its own step */
    return relaunched ? 0 : 1;
}

/* this start makes the set under the careful policy for this start only
   (ICO_DIAG_PIPESET_CAREFUL_ONCE): the first end's line says so */
static int act_careful_once;

int ico_diag_startup_pipeline_set(int relaunched, int file_lazy, const char *setting,
                                  const char *config_name)
{
    const int careful = setting != NULL && strcmp(setting, "careful") == 0;
    (void)config_name;
    act_careful_once = 0;
    if (setting != NULL && strcmp(setting, "lazy") == 0) {
        /* the setting already rules: no count kept towards it (a deleted
           setting starts the count again) */
        if (act_end_n[ICO_DIAG_END_PIPELINES] != 0) {
            ends_clear_kind(ICO_DIAG_END_PIPELINES);
        }
        return ICO_DIAG_PIPESET_LAZY_SETTING;
    }
    /* an end inside the vertex pre-pass's (or the record path's) pipelines
       makes a start without it the rule instead
       (ico_diag_startup_vu_prepass), and that start builds the set */
    if (startup_retry_note_has_vu_pre(act_prev)) {
        return careful ? ICO_DIAG_PIPESET_CAREFUL_SETTING : ICO_DIAG_PIPESET_FULL;
    }
    /* a restart after a crash report takes its own step (startup_retry.h);
       an end in the background is not counted (begin said so) */
    if (relaunched || act_end_n[ICO_DIAG_END_PIPELINES] == 0) {
        if (file_lazy && act_prev_low_memory && !relaunched) {
            /* issue 128: the previous start's set stopped at the system's
               low-memory signal; this start's file keeps the line, so an
               end inside its own creations steps from "no set" (LAZY_ONCE,
               then the setting), not back to the careful set */
            memcpy(act_low_memory, act_prev_low_memory_line, sizeof act_low_memory);
            return ICO_DIAG_PIPESET_LAZY_LOW_MEMORY;
        }
        return file_lazy ? ICO_DIAG_PIPESET_LAZY_FILE
               : careful ? ICO_DIAG_PIPESET_CAREFUL_SETTING
                         : ICO_DIAG_PIPESET_FULL;
    }
    if (act_end_n[ICO_DIAG_END_PIPELINES] >= 2) {
        /* the second in a row, whatever the ended start's set: written
           after the device is made (ico_diag_startup_end_persist) */
        return ICO_DIAG_PIPESET_LAZY_NOW;
    }
    /* the first: an end the user or the system may have caused (a swipe
       from the recent apps, Task Manager), so this start only; its line
       comes after the device is made (ico_diag_startup_end_persist). The
       ended start's own set says how far this one steps back: from the
       full set (or none, a lazy run or an older version's file without
       the line) to the careful set, from the careful set, or a set that
       stopped at the system's low-memory signal (issue 128), to none */
    if (!act_prev_careful && !act_prev_low_memory) {
        act_careful_once = 1;
        return ICO_DIAG_PIPESET_CAREFUL_ONCE;
    }
    act_once = 1;
    return ICO_DIAG_PIPESET_LAZY_ONCE;
}

int ico_diag_startup_vu_prepass(void)
{
    const unsigned int n = act_end_n[ICO_DIAG_END_VU_PRE];
    return n > 2 ? 2 : (int)n;
}

int ico_diag_startup_end_persist(int kind, const char *table, int (*persist)(void),
                                 const char *config_name)
{
    const char *cfg = config_name != NULL ? config_name : "config.toml";
    const int pre = kind == ICO_DIAG_END_VU_PRE;
    const int loss = kind == ICO_DIAG_END_LOSS;
    const char *setting = pre ? "[dev] vu_prepass = false" : "[dev] pipeline_set = \"lazy\"";
    const char *rule = pre ? "the device already draws without the 3D vertex pre-pass"
                           : "the device table already makes the pipelines as the game needs "
                             "them on this device";
    int saved;
    if (kind < 0 || kind >= ACT_END_KINDS || act_end_n[kind] == 0) {
        return 0;
    }
    if (table != NULL) {
        /* the rule already holds here: the setting would be redundant, and
           it would outlive a change of the rule; the row is cleared, and a
           lazy run follows the rule, not "this start only" */
        ends_clear_kind(kind);
        if (kind == ICO_DIAG_END_PIPELINES) {
            act_once = 0;
        }
        if (loss) {
            ico_diag_log("ico_pc: the previous start lost the graphics device; nothing is counted "
                         "or written to %s: robust access is as the settings say (%s)",
                         cfg, table);
        } else if (act_end_n[kind] >= 2) {
            ico_diag_log("ico_pc: the second start in a row ended while preparing the graphics "
                         "(%s); %s is not written to %s: %s (%s)",
                         act_prev, setting, cfg, rule, table);
        } else {
            ico_diag_log("ico_pc: the previous start ended while preparing the graphics (%s); "
                         "nothing is counted or written to %s: %s (%s)",
                         act_prev, cfg, rule, table);
        }
        return 0;
    }
    if (act_end_n[kind] == 1) {
        if (loss) {
            /* this start runs with robust access on: its end without a
               loss leaves the row (loss_count_prev) */
            ends_set_slot(ACT_END_ROBUST_ONCE, 1);
            ico_diag_log("ico_pc: the previous start lost the graphics device; this start only "
                         "checks the bounds of every buffer and image read (robust access; "
                         "nothing is written to %s); if a start loses it again before one runs "
                         "without a loss, [dev] gpu_robust = true is written for good",
                         cfg);
        } else if (pre) {
            ico_diag_log("ico_pc: the previous start ended while the graphics driver compiled the "
                         "3D vertex pre-pass's shaders (%s); this start only draws without it "
                         "(nothing is written to %s); if a start that compiles them ends there "
                         "again, [dev] vu_prepass = false is written for good",
                         act_prev, cfg);
        } else if (act_careful_once) {
            ico_diag_log("ico_pc: the previous start ended while preparing the graphics (%s); this "
                         "start only makes the start-up set the careful way (small batches on one "
                         "thread, the pipeline cache written after each; nothing is written to "
                         "%s); if this start also ends while preparing them, [dev] pipeline_set = "
                         "\"lazy\" is written for good",
                         act_prev, cfg);
        } else {
            ico_diag_log("ico_pc: the previous start ended while preparing the graphics (%s); this "
                         "start only makes the pipelines as the game needs them, with no start-up "
                         "set (nothing is written to %s); if this start also ends while preparing "
                         "them, [dev] pipeline_set = \"lazy\" is written for good",
                         act_prev, cfg);
        }
        return 0;
    }
    saved = persist != NULL && persist() == 0;
    if (saved) {
        /* the setting is the rule from now; a deleted setting starts the
           count again */
        ends_clear_kind(kind);
    } else if (kind == ICO_DIAG_END_PIPELINES) {
        /* not written: this start only, so its lazy run's notes say so
           and the next start tries the set again; the count stays, so a
           further end is due again */
        act_once = 1;
    } else if (loss) {
        /* not written: robust access for this start only, as at the first
           (the count stays) */
        ends_set_slot(ACT_END_ROBUST_ONCE, 1);
    }
    if (loss) {
        ico_diag_log("ico_pc: the graphics device was lost in two starts in a row; this start and "
                     "every start after it check the bounds of every buffer and image read ([dev] "
                     "gpu_robust = true in %s%s); delete that line to turn it off again",
                     cfg, saved ? "" : ", which could not be written: this start only");
    } else if (pre) {
        ico_diag_log("ico_pc: the previous start ended while the graphics driver compiled the 3D "
                     "vertex pre-pass's shaders (%s); this start and every start after it draw "
                     "without it ([dev] vu_prepass = false in %s%s); delete that line to try it "
                     "again",
                     act_prev, cfg, saved ? "" : ", which could not be written: this start only");
    } else {
        ico_diag_log("ico_pc: the previous start ended while preparing the graphics (%s); this "
                     "start and every start after it make the pipelines as the game needs them, "
                     "with no start-up set ([dev] pipeline_set = \"lazy\" in %s%s); delete that "
                     "line to try the start-up set again",
                     act_prev, cfg, saved ? "" : ", which could not be written: this start only");
    }
    return saved;
}

void ico_diag_startup_activity_exit(void)
{
    if (act_mode == ICO_DIAG_ACTIVITY_LAZY) {
        /* the file keeps a line saying the run exited lazy (for this
           start only: the next tries the start-up set again) */
        act_write(act_once ? act_lazy_exit_once : act_lazy_exit);
        act_close();
        act_mode = 0;
        if (act_bg) {
            (void)bg_remove();
        }
        ends_clean();
        return;
    }
    ico_diag_startup_activity_record(0);
}

void ico_diag_startup_activity_record(int mode)
{
    const int was = act_mode;
    act_mode = mode;
    if (mode == 0) {
        /* the set is done: its batches' times are no news, nor its policy */
        act_nbatches = 0;
        act_careful = 0;
        if (act_bg) {
            (void)bg_remove();
        }
        if (was == ICO_DIAG_ACTIVITY_STARTUP) {
            ends_clean();
        }
    }
    if (mode == 0 && act_loss[0] != '\0') {
        /* the run's device loss stays for the next start, with no note */
        act_write("");
        act_close();
    } else if (mode == 0) {
        act_remove();
    } else if (mode == ICO_DIAG_ACTIVITY_LAZY) {
        act_write(activity[0] != '\0' ? activity : act_between_note());
    } else {
        act_write(activity[0] != '\0' ? activity : "creating the start-up pipelines");
    }
    /* the app went to the background before the recording began */
    if (mode != 0 && was == 0 && act_in_bg) {
        bg_mark(1);
    }
}

void ico_diag_startup_activity_careful(int on)
{
    act_careful = on != 0;
    if (act_mode != 0) {
        act_write(act_current());
    }
}

void ico_diag_startup_activity_budget_stop(void)
{
    if (act_mode != ICO_DIAG_ACTIVITY_STARTUP) {
        return;
    }
    /* the set ended as it should (stopped by its policy): its row is
       broken as at its finish; the rest of the run is a lazy run's, for
       this start only (the careful line kept) */
    ends_clean();
    if (low_memory_stopped) {
        /* issue 128: stopped at the system's low-memory signal
           (ico_diag_low_memory_stop): the file's line says so for the rest
           of the run, and the lazy run is not for this start only, so the
           next start makes no set (ICO_DIAG_PIPESET_LAZY_LOW_MEMORY); the
           set's batches and the system's lines stay in the file (what the
           memory did up to the stop) */
        char lv[96];
        if (!ico_diag_low_memory_stop(lv, sizeof lv)) {
            snprintf(lv, sizeof lv, "a level not named");
        }
        snprintf(act_low_memory, sizeof act_low_memory, "%s (%s)", act_set_low_memory, lv);
        act_once = 0;
        ico_diag_log(
            "ico_pc: the start-up set stopped at %s (%s); the next start makes the "
            "pipelines as the game needs them, with no start-up set",
            floor_why[0] != '\0' ? "the free-memory floor" : "the system's low-memory signal", lv);
    } else {
        act_nbatches = 0;
        act_once = 1;
    }
    ico_diag_clear_activity();
    ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_LAZY);
}

void ico_diag_note_device_loss(const char *dump_zip)
{
    const int hang = loss_hang;
    if (dump_zip != NULL && dump_zip[0] != '\0') {
        snprintf(act_loss, sizeof act_loss, "%s%s", hang ? act_loss_hang_prefix : act_loss_prefix,
                 dump_zip);
    } else {
        snprintf(act_loss, sizeof act_loss, "%s", hang ? act_loss_hang_none : act_loss_none);
    }
    /* written now, the run's note kept (the process may be ended before
       its exit) */
    if (act_mode == ICO_DIAG_ACTIVITY_LAZY) {
        act_write(activity[0] != '\0' ? activity : act_between_note());
    } else if (act_mode == ICO_DIAG_ACTIVITY_STARTUP) {
        act_write(activity[0] != '\0' ? activity : "creating the start-up pipelines");
    } else {
        act_write("");
    }
}

const char *ico_diag_loss_dump(void)
{
    return loss_pending[0] != '\0' ? loss_pending : NULL;
}

int ico_diag_loss_bisect(int (*bisect)(const char *dump_zip, char *line, size_t line_size,
                                       void *user),
                         void *user)
{
    char zip[sizeof loss_pending];
    char line[640];
    int r;
    if (loss_pending[0] == '\0' || bisect == NULL) {
        return -1;
    }
    memcpy(zip, loss_pending, sizeof zip);
    loss_pending[0] = '\0';
    /* the file says the bisect is running (and holds no loss): a run ended
       inside it does not bisect again */
    act_write(act_bisect);
    ico_diag_log("ico_pc: loss bisect: replaying the lost frame of %s pass by pass before the "
                 "game starts",
                 zip);
    line[0] = '\0';
    r = bisect(zip, line, sizeof line, user);
    if (r == ICO_DIAG_BISECT_LOST) {
        ico_diag_log("ico_pc: loss bisect: %s", line[0] != '\0' ? line : "the device was lost");
    } else if (r == ICO_DIAG_BISECT_CLEAN) {
        ico_diag_log("ico_pc: loss bisect: %s",
                     line[0] != '\0' ? line : "the frame ran without losing the device");
    } else {
        r = ICO_DIAG_BISECT_NOT_RUN;
        ico_diag_log("ico_pc: loss bisect: not run: %s", line[0] != '\0' ? line : "no reason");
    }
    /* done: the file goes (the start that follows writes its own) */
    act_remove();
    ico_diag_log("ico_pc: loss bisect: done; the game starts");
    return r;
}

/* --- The loss bisect's second level ------------------------------------------ */

/* The state file (diag_host.h ico_diag_loss_bisect2), one record a line:
     ico-pc loss bisect 1
     version <the program's version>
     zip <the loss dump>
     pass <P> draws <N> passes <T>
     starts <k>
     step <i> <kind> <first> <last> <filter> <arg> <verdict> <ms> key <valid> <vu> <skinned>
          <vs> <clip> <tex> note <text>
   (one line a step; verdict lost, clean, killed or "-" while it runs).
   Read as untrusted: lines past B2_LINE bytes, numbers out of range, a step
   out of order, a kind or filter it does not know: the file is deleted;
   other unknown lines are ignored. */
#define B2_MAGIC "ico-pc loss bisect 1"
#define B2_LINE 1100
#define B2_NOTE 320

static char b2_path[1024];

typedef struct B2State {
    char version[128];
    char zip[1024];
    uint32_t pass, draws, passes, starts;
    RdBisectPlan plan;
    double ms[RD_BISECT_PLAN_STEPS];
    char note[RD_BISECT_PLAN_STEPS][B2_NOTE];
} B2State;

static B2State b2;

void ico_diag_loss_bisect2_path(const char *path)
{
    snprintf(b2_path, sizeof b2_path, "%s", path != NULL ? path : "");
}

static const char *b2_verdict_name(uint8_t v)
{
    return v == RD_BISECT_V_LOST     ? "lost"
           : v == RD_BISECT_V_CLEAN  ? "clean"
           : v == RD_BISECT_V_KILLED ? "killed"
                                     : "-";
}

/* the file written whole (a temporary file renamed over it) */
static int b2_save(void)
{
    char tmp[sizeof b2_path + 8];
    FILE *f;
    uint32_t i;
    int ok;
    if (b2_path[0] == '\0') {
        return -1;
    }
    snprintf(tmp, sizeof tmp, "%s.new", b2_path);
    f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    ok = fprintf(f, "%s\nversion %s\nzip %s\npass %u draws %u passes %u\nstarts %u\n", B2_MAGIC,
                 b2.version, b2.zip, (unsigned)b2.pass, (unsigned)b2.draws, (unsigned)b2.passes,
                 (unsigned)b2.starts) > 0;
    for (i = 0; ok && i < b2.plan.count; i++) {
        const RdBisectStep *s = &b2.plan.step[i];
        const RdBisectKey *k = &b2.plan.key[i];
        ok = fprintf(f, "step %u %s %u %u %s %u %s %.0f key %u %u %u %u %u %u note %s\n",
                     (unsigned)(i + 1), rd_bisect_kind_name(s->kind), (unsigned)s->drawFirst,
                     (unsigned)s->drawLast, rd_bisect_filter_name(s->filter), (unsigned)s->arg,
                     b2_verdict_name(b2.plan.verdict[i]), b2.ms[i], (unsigned)k->valid,
                     (unsigned)k->vu, (unsigned)k->skinned, (unsigned)k->vs, (unsigned)k->clip,
                     (unsigned)k->tex, b2.note[i]) > 0;
    }
    ok = fclose(f) == 0 && ok;
    if (!ok || ico_rename_replace(tmp, b2_path) != 0) {
        ico_remove(tmp);
        return -1;
    }
    return 0;
}

static void b2_delete(void)
{
    if (b2_path[0] != '\0') {
        ico_remove(b2_path);
    }
}

/* the next space-separated word of *s (at most size - 1 bytes), 0 when
   none or too long */
static int b2_word(const char **s, char *out, size_t size)
{
    size_t n = 0;
    while (**s == ' ') {
        (*s)++;
    }
    while ((*s)[n] != '\0' && (*s)[n] != ' ') {
        n++;
    }
    if (n == 0 || n >= size) {
        return 0;
    }
    memcpy(out, *s, n);
    out[n] = '\0';
    *s += n;
    return 1;
}

/* the next word as a number at most max */
static int b2_num(const char **s, uint32_t max, uint32_t *out)
{
    char w[16];
    char *end;
    unsigned long v;
    if (!b2_word(s, w, sizeof w) || w[0] < '0' || w[0] > '9') {
        return 0;
    }
    v = strtoul(w, &end, 10);
    if (*end != '\0' || v > max) {
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int b2_expect(const char **s, const char *word)
{
    char w[16];
    return b2_word(s, w, sizeof w) && strcmp(w, word) == 0;
}

/* one step line after "step " */
static int b2_parse_step(const char *s)
{
    char w[24];
    uint32_t i, first, last, arg, ms, valid, vu, sk, vs, clip, tex;
    int kind, filter;
    uint8_t v;
    RdBisectStep *st;
    RdBisectKey *k;
    if (!b2_num(&s, RD_BISECT_PLAN_STEPS, &i) || i != b2.plan.count + 1 ||
        !b2_word(&s, w, sizeof w) || (kind = rd_bisect_kind_parse(w)) < 0 ||
        !b2_num(&s, b2.draws, &first) || !b2_num(&s, b2.draws, &last) || first < 1 ||
        last < first || !b2_word(&s, w, sizeof w) || (filter = rd_bisect_filter_parse(w)) < 0 ||
        !b2_num(&s, 0xFFFFFFFFu, &arg) || !b2_word(&s, w, sizeof w)) {
        return 0;
    }
    if (strcmp(w, "lost") == 0) {
        v = RD_BISECT_V_LOST;
    } else if (strcmp(w, "clean") == 0) {
        v = RD_BISECT_V_CLEAN;
    } else if (strcmp(w, "killed") == 0 || strcmp(w, "-") == 0) {
        /* a step that never got its verdict: the run ended inside it */
        v = RD_BISECT_V_KILLED;
    } else {
        return 0;
    }
    if (!b2_num(&s, 3600000u, &ms) || !b2_expect(&s, "key") || !b2_num(&s, 1, &valid) ||
        !b2_num(&s, 1, &vu) || !b2_num(&s, 1, &sk) || !b2_num(&s, 255, &vs) ||
        !b2_num(&s, 3, &clip) || !b2_num(&s, 0xFFFFFFFFu, &tex) || !b2_expect(&s, "note")) {
        return 0;
    }
    st = &b2.plan.step[b2.plan.count];
    k = &b2.plan.key[b2.plan.count];
    st->kind = (uint8_t)kind;
    st->filter = (uint8_t)filter;
    st->drawFirst = first;
    st->drawLast = last;
    st->arg = arg;
    k->valid = (uint8_t)valid;
    k->vu = (uint8_t)vu;
    k->skinned = (uint8_t)sk;
    k->vs = (uint8_t)vs;
    k->clip = (uint8_t)clip;
    k->tex = tex;
    b2.plan.verdict[b2.plan.count] = v;
    b2.ms[b2.plan.count] = ms;
    while (*s == ' ') {
        s++;
    }
    snprintf(b2.note[b2.plan.count], B2_NOTE, "%s", s);
    b2.plan.count++;
    return 1;
}

/* the state file into b2: 1 read, 0 none, -1 a file that does not parse
   or another version's (deleted) */
static int b2_load(void)
{
    char line[B2_LINE + 2];
    FILE *f;
    int n = 0, ok = 1, have[4] = {0, 0, 0, 0};
    const char *s;
    memset(&b2, 0, sizeof b2);
    if (b2_path[0] == '\0' || (f = ico_fopen(b2_path, "rb")) == NULL) {
        return 0;
    }
    while (ok && fgets(line, sizeof line, f) != NULL) {
        size_t len = strlen(line);
        if (len > B2_LINE || (len > 0 && line[len - 1] != '\n' && !feof(f))) {
            ok = 0; /* past the line's bound */
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (n++ == 0) {
            ok = strcmp(line, B2_MAGIC) == 0;
            continue;
        }
        s = line;
        if (strncmp(line, "version ", 8) == 0) {
            const size_t n = strlen(line + 8);
            ok = n < sizeof b2.version;
            if (ok) {
                memcpy(b2.version, line + 8, n + 1);
            }
            have[0] = 1;
        } else if (strncmp(line, "zip ", 4) == 0) {
            const size_t n = strlen(line + 4);
            ok = n > 0 && n < sizeof b2.zip;
            if (ok) {
                memcpy(b2.zip, line + 4, n + 1);
            }
            have[1] = 1;
        } else if (strncmp(line, "pass ", 5) == 0) {
            s += 5;
            ok = b2_num(&s, 4096, &b2.pass) && b2.pass >= 1 && b2_expect(&s, "draws") &&
                 b2_num(&s, 1u << 20, &b2.draws) && b2.draws >= 1 && b2_expect(&s, "passes") &&
                 b2_num(&s, 4096, &b2.passes) && b2.pass <= b2.passes;
            have[2] = 1;
        } else if (strncmp(line, "starts ", 7) == 0) {
            s += 7;
            ok = b2_num(&s, ICO_DIAG_BISECT2_STARTS, &b2.starts);
            have[3] = 1;
        } else if (strncmp(line, "step ", 5) == 0) {
            ok = have[2] && b2.plan.count < RD_BISECT_PLAN_STEPS && b2_parse_step(line + 5);
        }
        /* any other line: ignored */
    }
    fclose(f);
    b2.plan.draws = b2.draws;
    if (!ok || n == 0 || !have[0] || !have[1] || !have[2] || !have[3]) {
        ico_diag_log("ico_pc: loss bisect: the state file %s does not read as one; deleted",
                     b2_path);
        b2_delete();
        memset(&b2, 0, sizeof b2);
        return -1;
    }
    if (strcmp(b2.version, act_version) != 0) {
        ico_diag_log("ico_pc: loss bisect: the state file is from version %s; deleted (this is "
                     "%s)",
                     b2.version, act_version);
        b2_delete();
        memset(&b2, 0, sizeof b2);
        return -1;
    }
    return 1;
}

int ico_diag_loss_bisect2_pending(void)
{
    return b2_load() == 1;
}

/* the final line: the draw, its note and the verdict */
static void b2_final(const RdBisectResult2 *r, const char *why)
{
    const char *note = "";
    const char *mask = rd_bisect_mask_text(r);
    uint32_t i;
    for (i = 0; r->draw != 0 && i < b2.plan.count; i++) {
        if (b2.plan.step[i].drawLast == r->draw && b2.note[i][0] != '\0') {
            note = b2.note[i];
            break;
        }
    }
    if (r->draw != 0) {
        ico_diag_log("ico_pc: loss bisect: pass %u, draw %u of %u%s%s%s: %s%s%s%s",
                     (unsigned)b2.pass, (unsigned)r->draw, (unsigned)b2.draws, note[0] ? " (" : "",
                     note, note[0] ? ")" : "", rd_bisect_alone_text(r), mask[0] ? "; " : "", mask,
                     why);
    } else {
        ico_diag_log("ico_pc: loss bisect: pass %u of %u draws: not finished%s%s%s",
                     (unsigned)b2.pass, (unsigned)b2.draws, mask[0] ? "; " : "", mask, why);
    }
}

int ico_diag_loss_bisect2(const char *dump_zip, uint32_t pass, uint32_t draws, uint32_t passes,
                          IcoBisect2StepFn stepfn, void *user, double budget_ms)
{
    RdBisectResult2 res;
    RdBisectStep step;
    double spent = 0.0;
    int last = ICO_DIAG_BISECT2_NOT_RUN, anyLost = 0;
    /* the lost steps of this start: with the first level's loss at most
       ICO_DIAG_BISECT2_LOSSES + 1 device losses (Windows restarts the
       whole system after 5 graphics resets in 60 s) */
    unsigned int losses = 0;
    if (stepfn == NULL || b2_path[0] == '\0') {
        return ICO_DIAG_BISECT2_NOT_RUN;
    }
    if (dump_zip != NULL) {
        if (draws == 0 || pass == 0 || strlen(dump_zip) >= sizeof b2.zip) {
            return ICO_DIAG_BISECT2_NOT_RUN;
        }
        memset(&b2, 0, sizeof b2);
        snprintf(b2.version, sizeof b2.version, "%s", act_version);
        snprintf(b2.zip, sizeof b2.zip, "%s", dump_zip);
        b2.pass = pass;
        b2.draws = draws > (1u << 20) ? (1u << 20) : draws;
        b2.passes = passes < pass ? pass : passes;
        b2.starts = 1;
        rd_bisect_plan_init(&b2.plan, b2.draws);
        ico_diag_log("ico_pc: loss bisect: narrowing pass %u (%u draws) draw by draw, a renderer "
                     "of its own a step",
                     (unsigned)b2.pass, (unsigned)b2.draws);
    } else {
        uint32_t killed = 0, i;
        if (b2_load() != 1) {
            return ICO_DIAG_BISECT2_NOT_RUN;
        }
        for (i = 0; i < b2.plan.count; i++) {
            killed = b2.plan.verdict[i] == RD_BISECT_V_KILLED ? i + 1 : killed;
        }
        if (b2.starts >= ICO_DIAG_BISECT2_STARTS) {
            char why[32];
            snprintf(why, sizeof why, " (%u starts)", (unsigned)ICO_DIAG_BISECT2_STARTS);
            rd_bisect_plan_result(&b2.plan, &res);
            b2_final(&res, why);
            b2_delete();
            return ICO_DIAG_BISECT2_NOT_RUN;
        }
        b2.starts++;
        if (killed != 0 && killed == b2.plan.count) {
            ico_diag_log("ico_pc: loss bisect: continuing at step %u (start %u of %u; step %u "
                         "ended the run)",
                         (unsigned)(b2.plan.count + 1), (unsigned)b2.starts,
                         (unsigned)ICO_DIAG_BISECT2_STARTS, (unsigned)killed);
        } else {
            ico_diag_log("ico_pc: loss bisect: continuing at step %u (start %u of %u)",
                         (unsigned)(b2.plan.count + 1), (unsigned)b2.starts,
                         (unsigned)ICO_DIAG_BISECT2_STARTS);
        }
    }
    if (b2_save() != 0) {
        ico_diag_log("ico_pc: loss bisect: the state file %s cannot be written; not run", b2_path);
        return ICO_DIAG_BISECT2_NOT_RUN;
    }
    act_write(act_bisect2);
    for (;;) {
        const RdBisectEnd end = rd_bisect_plan_next(&b2.plan, &step, &res);
        const uint32_t i = b2.plan.count - 1;
        char what[96], note[B2_NOTE];
        RdBisectKey key;
        double ms = 0.0;
        int r;
        if (end != RD_BISECT_END_NO) {
            b2_final(&res, end == RD_BISECT_END_NOT_MONOTONE ? " (the verdicts contradict each "
                                                               "other: something other than the "
                                                               "draws loses the device)"
                           : end == RD_BISECT_END_BOUND      ? " (16 steps)"
                                                             : "");
            b2_delete();
            break;
        }
        if (spent >= budget_ms || losses >= ICO_DIAG_BISECT2_LOSSES) {
            if (spent >= budget_ms) {
                ico_diag_log("ico_pc: loss bisect: %.0f s spent; step %u waits for the next start",
                             spent / 1000.0, (unsigned)(i + 1));
            } else {
                ico_diag_log("ico_pc: loss bisect: %u lost steps in this start; step %u waits "
                             "for the next start",
                             losses, (unsigned)(i + 1));
            }
            /* the planned step is planned again then */
            b2.plan.count--;
            b2_save();
            break;
        }
        b2.plan.verdict[i] = RD_BISECT_V_NONE;
        b2.note[i][0] = '\0';
        b2.ms[i] = 0.0;
        b2_save(); /* the step without its verdict: killed if the run ends in it */
        memset(&key, 0, sizeof key);
        note[0] = '\0';
        r = stepfn(b2.zip, b2.pass, i + 1, &step, &key, note, sizeof note, &ms, user);
        rd_bisect_step_text(&step, b2.draws, what, sizeof what);
        if (r == ICO_DIAG_BISECT2_NOINIT) {
            ico_diag_log("ico_pc: loss bisect: step %u of %u: pass %u, %s: the renderer did not "
                         "start; the step runs again at the next start",
                         (unsigned)(i + 1), (unsigned)RD_BISECT_PLAN_STEPS, (unsigned)b2.pass,
                         what);
            b2.plan.count--;
            b2_save();
            last = r;
            break;
        }
        if (r != ICO_DIAG_BISECT2_LOST && r != ICO_DIAG_BISECT2_CLEAN) {
            ico_diag_log("ico_pc: loss bisect: step %u of %u: pass %u, %s: not run (%s); the "
                         "bisect ends",
                         (unsigned)(i + 1), (unsigned)RD_BISECT_PLAN_STEPS, (unsigned)b2.pass, what,
                         note[0] ? note : "the dump cannot be replayed");
            b2_delete();
            last = ICO_DIAG_BISECT2_NOT_RUN;
            break;
        }
        for (char *c = note; *c != '\0'; c++) {
            if (*c == '\n' || *c == '\r') {
                *c = ' '; /* one line in the file */
            }
        }
        snprintf(b2.note[i], B2_NOTE, "%s", note);
        b2.ms[i] = ms < 0.0 ? 0.0 : ms > 3600000.0 ? 3600000.0 : ms;
        rd_bisect_plan_set(&b2.plan, i,
                           r == ICO_DIAG_BISECT2_LOST ? RD_BISECT_V_LOST : RD_BISECT_V_CLEAN, &key);
        spent += b2.ms[i];
        last = r;
        anyLost |= r == ICO_DIAG_BISECT2_LOST;
        losses += r == ICO_DIAG_BISECT2_LOST;
        ico_diag_log("ico_pc: loss bisect: step %u of %u: pass %u, %s: %s in %.1f s",
                     (unsigned)(i + 1), (unsigned)RD_BISECT_PLAN_STEPS, (unsigned)b2.pass, what,
                     r == ICO_DIAG_BISECT2_LOST ? "lost" : "clean", b2.ms[i] / 1000.0);
        b2_save();
    }
    act_remove();
    /* a step of this start lost the device: the caller's renderer starts
       after a loss */
    return last == ICO_DIAG_BISECT2_NOINIT ? last : anyLost ? ICO_DIAG_BISECT2_LOST : last;
}

/* the note says the graphics driver was building a pipeline */
static int driver_note(void)
{
    static const char a[] = "creating pipeline";
    static const char b[] = "creating the start-up pipelines";
    return strncmp(activity, a, sizeof a - 1) == 0 || strncmp(activity, b, sizeof b - 1) == 0;
}

/* ico_diag_set_startup_retry and ico_diag_set_pipeline_cache */
static int retry_step;
static int retry_have_d3d12;
static int retry_explicit;
static int retry_blend_explicit;
static char retry_backend[16] = "vulkan";
static int retry_cache_loaded;
static char retry_cache_path[1024];
/* ico_diag_set_vu_prepass: the run draws with the VU vertex pre-pass */
static int retry_prepass_on;
/* ico_diag_set_robust_off_default: robust access off by the device's
   default */
static int retry_robust_off;
#ifdef _WIN32
static wchar_t retry_cache_wpath[1024];
#endif

void ico_diag_set_startup_retry(int step, int have_d3d12, int backend_explicit, int blend_explicit,
                                const char *backend)
{
    retry_step = step;
    retry_have_d3d12 = have_d3d12 != 0;
    retry_explicit = backend_explicit != 0;
    retry_blend_explicit = blend_explicit != 0;
    snprintf(retry_backend, sizeof retry_backend, "%s",
             backend != NULL && backend[0] != '\0' ? backend : "vulkan");
}

void ico_diag_set_backend_choice(int backend_explicit, const char *backend)
{
    retry_explicit = backend_explicit != 0;
    snprintf(retry_backend, sizeof retry_backend, "%s",
             backend != NULL && backend[0] != '\0' ? backend : "vulkan");
}

/* ico_diag_set_backend_trial: the renderer on trial, "" none */
static char retry_trial[16];

void ico_diag_set_backend_trial(const char *backend)
{
    snprintf(retry_trial, sizeof retry_trial, "%s", backend != NULL ? backend : "");
}

void ico_diag_set_vu_prepass(int on)
{
    retry_prepass_on = on != 0;
}

void ico_diag_set_robust_off_default(int on)
{
    retry_robust_off = on != 0;
}

void ico_diag_forget_startup_retry_env(void)
{
#ifdef _WIN32
    /* the C library's copy (getenv) and the process's block
       (GetEnvironmentStringsW, CreateProcessW) */
    _putenv_s(STARTUP_RETRY_ENV, "");
    SetEnvironmentVariableW(L"" STARTUP_RETRY_ENV, NULL);
#else
    unsetenv(STARTUP_RETRY_ENV);
#endif
}

void ico_diag_set_pipeline_cache(int loaded, const char *path)
{
    if (path != NULL) {
        retry_cache_path[0] = '\0';
        if (strlen(path) < sizeof retry_cache_path) {
            memcpy(retry_cache_path, path, strlen(path) + 1);
        }
#ifdef _WIN32
        retry_cache_wpath[0] = L'\0';
        if (retry_cache_path[0] != '\0' &&
            MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, retry_cache_path, -1, retry_cache_wpath,
                (int)(sizeof retry_cache_wpath / sizeof retry_cache_wpath[0])) == 0) {
            retry_cache_wpath[0] = L'\0';
        }
#endif
    }
    retry_cache_loaded = loaded != 0 && retry_cache_path[0] != '\0';
}

/* ico_diag_prepare_relaunch: everything the relaunch needs, made at
   start-up, so the crash path neither allocates nor reads the environment
   while another thread may change it. relaunch_value: set by the crash path
   when the run is to start again (finish). */
static int relaunch_ready;
static char relaunch_value[48];
/* ico_diag_set_relaunch_stub (tests): called instead of starting the
   program again */
static int (*relaunch_stub)(const char *value);

void ico_diag_set_relaunch_stub(int (*fn)(const char *value))
{
    relaunch_stub = fn;
}

#if !defined(__ANDROID__) && !defined(ICO_IOS)
static void loss_relaunch_at_exit(void);
static int loss_at_exit;

/* the relaunch after a device loss runs at exit, after the end-of-run
   steps, which main_host.c registers later (atexit runs the last
   registered first) */
static void loss_relaunch_register(void)
{
    if (!loss_at_exit && atexit(loss_relaunch_at_exit) == 0) {
        loss_at_exit = 1;
    }
}
#endif

#if defined(_WIN32)

/* the executable, the command line (CreateProcessW may write into it) and
   the environment without ICO_STARTUP_RETRY, ready for its new value */
static wchar_t relaunch_exe[4096];
static wchar_t relaunch_cmd[32768];
static wchar_t relaunch_env[65536];
static size_t relaunch_env_used;

void ico_diag_prepare_relaunch(int argc, char **argv)
{
    const DWORD cap = (DWORD)(sizeof relaunch_exe / sizeof relaunch_exe[0]);
    const wchar_t *cmd = GetCommandLineW();
    wchar_t *env;
    long used = -1;
    (void)argc;
    (void)argv;
    relaunch_ready = 0;
    const DWORD n = GetModuleFileNameW(NULL, relaunch_exe, cap);
    if (n == 0 || n >= cap || cmd == NULL ||
        wcslen(cmd) >= sizeof relaunch_cmd / sizeof relaunch_cmd[0]) {
        return;
    }
    wcscpy(relaunch_cmd, cmd);
    env = GetEnvironmentStringsW();
    if (env != NULL) {
        /* room for the variable and the final NUL: startup_retry_block_finish */
        used = startup_retry_block_copy(env, relaunch_env,
                                        sizeof relaunch_env / sizeof relaunch_env[0] - 64);
        FreeEnvironmentStringsW(env);
    }
    if (used < 0) {
        return;
    }
    relaunch_env_used = (size_t)used;
    relaunch_ready = 1;
    loss_relaunch_register();
}

/* 0 when the new process was created */
static int relaunch_now(const char *value)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (relaunch_ready && relaunch_stub != NULL) {
        return relaunch_stub(value);
    }
    if (!relaunch_ready ||
        startup_retry_block_finish(relaunch_env, relaunch_env_used,
                                   sizeof relaunch_env / sizeof relaunch_env[0], value) != 0) {
        return -1;
    }
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    /* no handles inherited; the current folder stays the same (NULL) */
    if (!CreateProcessW(relaunch_exe, relaunch_cmd, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT,
                        relaunch_env, NULL, &si, &pi)) {
        return -1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

#elif !defined(__ANDROID__) && !defined(ICO_IOS)

extern char **environ;

/* the executable's path, main's argv (kept for the process's life) and the
   environment's pointers with relaunch_var as the last */
static char relaunch_exe[4096];
static char **relaunch_argv;
static const char *relaunch_envp[1024];
static char relaunch_var[96];

void ico_diag_prepare_relaunch(int argc, char **argv)
{
    relaunch_ready = 0;
    if (argc < 1 || argv == NULL || argv[0] == NULL) {
        return;
    }
#ifdef __APPLE__
    {
        uint32_t size = (uint32_t)sizeof relaunch_exe;
        if (_NSGetExecutablePath(relaunch_exe, &size) != 0) {
            return;
        }
    }
#else
    {
        const ssize_t n = readlink("/proc/self/exe", relaunch_exe, sizeof relaunch_exe - 1);
        if (n <= 0 || (size_t)n >= sizeof relaunch_exe - 1) {
            return;
        }
        relaunch_exe[n] = '\0';
    }
    {
        /* inside an AppImage (host_fs.h ico_appimage_file) the program
           lies in the runtime's mount, which ends once the runtime's
           keep-alive pipe has no reader: execve closes it (relaunch_now
           marks every descriptor close-on-exec), so the program in the
           mount could not load its libraries. The restart starts the
           AppImage file instead, which mounts it again; the link map stays
           the mounted program's (/proc/self/exe) */
        char app[sizeof relaunch_exe];
        if (ico_appimage_file(app, sizeof app, NULL)) {
            memcpy(relaunch_exe, app, strlen(app) + 1);
        }
    }
#endif
    relaunch_argv = argv;
    relaunch_var[0] = '\0';
    if (startup_retry_envp(environ, relaunch_var, relaunch_envp,
                           sizeof relaunch_envp / sizeof relaunch_envp[0]) < 0) {
        return;
    }
    relaunch_ready = 1;
    loss_relaunch_register();
}

/* Replaces this process with the program again (the same process id, so
   whatever started the game keeps waiting on it). Only async-signal-safe
   calls. Returns only when execve failed. */
static int relaunch_now(const char *value)
{
    static const char name[] = STARTUP_RETRY_ENV "=";
    size_t i = 0, j;
    sigset_t none;
    int fd;
    if (!relaunch_ready) {
        return -1;
    }
    if (relaunch_stub != NULL) {
        return relaunch_stub(value);
    }
    for (j = 0; name[j] != '\0'; j++) {
        relaunch_var[i++] = name[j];
    }
    for (j = 0; value[j] != '\0' && i + 1 < sizeof relaunch_var; j++) {
        relaunch_var[i++] = value[j];
    }
    relaunch_var[i] = '\0';
    /* the descriptors this run opened (the GPU's, the audio device's) are
       not handed on; the log's stay open should execve fail */
    for (fd = 3; fd < 1024; fd++) {
        const int fl = fcntl(fd, F_GETFD);
        if (fl >= 0) {
            fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
        }
    }
    /* the crash's signal is blocked inside its handler, and the mask is
       inherited across execve */
    sigemptyset(&none);
    pthread_sigmask(SIG_SETMASK, &none, NULL);
    execve(relaunch_exe, relaunch_argv, (char *const *)relaunch_envp);
    return -1;
}

#else

/* Android and iOS: the system owns the app's process; no relaunch */
void ico_diag_prepare_relaunch(int argc, char **argv)
{
    (void)argc;
    (void)argv;
}

#ifndef __ANDROID__ /* iOS's finish asks; Android's has its own end */
static int relaunch_now(const char *value)
{
    (void)value;
    return -1;
}
#endif

#endif
const char *ico_diag_relaunch_program(void)
{
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(ICO_IOS)
    return relaunch_ready ? relaunch_exe : NULL;
#else
    return NULL;
#endif
}

/* The crash box's words after a driver crash with nothing left to try */
static const char driver_box_text[] =
    "The graphics driver crashed while the game was getting its graphics ready, and the game "
    "could not work around it. Updating the graphics driver usually fixes this.";

#ifdef __ANDROID__
/* ... and after one while preparing the start-up set: the next start
   prepares them as the game draws (ico_diag_startup_activity_begin) */
static const char driver_next_start_text[] =
    "The graphics driver crashed while the game was getting its graphics ready. Start the game "
    "again: it will then get its graphics ready as you play instead.";
#endif

/* A crash while the note says a pipeline was being built: the step the
   next run takes (startup_retry.h), logged; relaunch_value set when there
   is one and this platform can start the game again. The cache step
   deletes the pipeline cache file here. The note says whether the
   pipeline read the screen for the exact blend (its dst field), which
   gives the fixedblend step. */
static void driver_crash_plan(void)
{
    char reason[200];
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_CRASH,
        .step = (StartupRetry)retry_step,
        .canRelaunch = relaunch_ready != 0,
        .cacheWasLoaded = retry_cache_loaded != 0,
        .haveD3d12 = retry_have_d3d12 != 0,
        .backendExplicit = retry_explicit != 0,
        .blendExplicit = retry_blend_explicit != 0,
        .dstPipeline = startup_retry_note_has_dst(activity),
        .prepassPipeline = startup_retry_note_has_vu_pre(activity),
        .backendName = retry_backend,
    };
    relaunch_value[0] = '\0';
    if (retry_trial[0] != '\0') {
        /* Apply's renderer, not kept yet: the file names the one before */
        flog("ico_pc: the graphics driver crashed while preparing the shaders of the %s renderer "
             "applied from the menu and not kept yet: the game does not start again; the next "
             "start uses the renderer the settings name",
             retry_trial);
        return;
    }
    const StartupRetry next = startup_retry_next(&run, reason, sizeof reason);
    if (next == STARTUP_RETRY_NONE) {
        flog("ico_pc: the graphics driver crashed while preparing the shaders: %s", reason);
        return;
    }
    if (startup_retry_value(next, retry_backend, relaunch_value, sizeof relaunch_value) != 0) {
        relaunch_value[0] = '\0';
        return;
    }
    if (next == STARTUP_RETRY_CACHE) {
#ifdef _WIN32
        const int gone = retry_cache_wpath[0] != L'\0' && DeleteFileW(retry_cache_wpath);
#else
        const int gone = unlink(retry_cache_path) == 0;
#endif
        flog("ico_pc: %s the pipeline cache file %s", gone ? "deleted" : "could not delete",
             retry_cache_path);
    }
    flog("ico_pc: the graphics driver crashed while preparing the shaders; the game starts "
         "again with %s=%s: %s",
         STARTUP_RETRY_ENV, relaunch_value, reason);
}

/* --- The relaunch after a device loss --------------------------------------------- */

/* ico_diag_device_lost_restart: the loss step's value, kept for the
   relaunch at exit (loss_relaunch_at_exit); "" when the run just ends.
   Apart from relaunch_value, so a crash or watchdog end during the
   shutdown still reports as one. */
static char loss_value[48];
#if defined(__ANDROID__) || defined(ICO_IOS)
static const int loss_at_exit = 0; /* no relaunch */
#endif

int ico_diag_device_lost_restart(int blend_was_fixed)
{
    char reason[200];
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_LOSS,
        .step = (StartupRetry)retry_step,
        .canRelaunch = relaunch_ready != 0 && loss_at_exit != 0,
        .cacheWasLoaded = retry_cache_loaded != 0,
        .haveD3d12 = retry_have_d3d12 != 0,
        .backendExplicit = retry_explicit != 0,
        .blendExplicit = retry_blend_explicit != 0,
        .blendWasFixed = blend_was_fixed != 0,
        .prepassWasOn = retry_prepass_on != 0,
        .robustOffDefault = retry_robust_off != 0,
        .backendName = retry_backend,
    };
    loss_value[0] = '\0';
    if (retry_trial[0] != '\0') {
        ico_diag_log("ico_pc: the graphics card stopped responding on the %s renderer applied "
                     "from the menu and not kept yet: the game does not start again; the next "
                     "start uses the renderer the settings name",
                     retry_trial);
        return 0;
    }
    const StartupRetry next = startup_retry_next(&run, reason, sizeof reason);
    if (next == STARTUP_RETRY_NONE) {
        ico_diag_log("ico_pc: the graphics card stopped responding; the game does not start "
                     "again: %s",
                     reason);
        return 0;
    }
    if (startup_retry_value(next, retry_backend, loss_value, sizeof loss_value) != 0) {
        loss_value[0] = '\0';
        return 0;
    }
    ico_diag_log("ico_pc: the graphics card stopped responding; the game starts again at the "
                 "title when this run has closed, with %s=%s: %s",
                 STARTUP_RETRY_ENV, loss_value, reason);
    return 1;
}

#if !defined(__ANDROID__) && !defined(ICO_IOS)

/* The box when the relaunch could not be made: the player is not left with
   a game that just went */
static const char loss_box_text[] =
    "The graphics card stopped responding, and the game could not start itself again.\n\n"
    "If the graphics card is overclocked, try its stock clocks first.";

/* Last of the end-of-run steps (registered before them, atexit runs it
   after them): the window, the audio and the files are closed; the
   program starts again with the loss step. The normal path: stdio and the
   heap are fine here. */
static void loss_relaunch_at_exit(void)
{
    if (loss_value[0] == '\0') {
        return;
    }
    ico_diag_log("ico_pc: starting the game again after the graphics card stopped responding");
    fflush(NULL);
#ifdef _WIN32
    if (log_h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(log_h);
    }
    if (relaunch_now(loss_value) == 0) {
        ico_diag_log("ico_pc: started the game again");
        return;
    }
    ico_diag_log("ico_pc: could not start the game again (error %lu)",
                 (unsigned long)GetLastError());
    {
        char box[2048]; /* the text (up to 1023) and a path (up to 1023) */
        snprintf(box, sizeof box, "%s\n\nThe log is:\n%s", loss_box_text, ico_diag_log_path());
        MessageBoxA(NULL, box, "ICO PC", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
#else
    /* returns only when it failed (or a test's stub stood in) */
    if (relaunch_now(loss_value) == 0) {
        ico_diag_log("ico_pc: started the game again");
        return;
    }
    ico_diag_log("ico_pc: could not start the game again");
    if (fatal_box != NULL) {
        char box[2048]; /* the text (up to 1023) and a path (up to 1023) */
        snprintf(box, sizeof box, "%s\n\nThe log is:\n%s", loss_box_text, ico_diag_log_path());
        fatal_box(box);
    }
#endif
}

#endif

/* --- Symbols, the summary line and the report file --------------------------- */

static void sym_note(uintptr_t a)
{
    uint64_t off;
    int i;

    if (!sym_collect || !in_exe(a)) {
        return;
    }
    off = (uint64_t)(a - exe_lo);
    for (i = 0; i < sym_n; i++) {
        if (sym_offs[i] == off) {
            return;
        }
    }
    /* the last slot is the check's (report_symbols) */
    if (sym_n < ICO_SYM_MAX - 1) {
        sym_offs[sym_n++] = off;
    }
}

/* The symbol file (symmap.h): the link map beside the program, opened at
   ico_diag_init (no reading until a report), or the one handed in
   (ico_diag_set_symbols_path, ico_diag_set_symbols_fd). */
static char sym_name[160];
static int sym_gz;
static long long sym_base, sym_len = -1, sym_pos;
static IcoSymHit sym_hits[ICO_SYM_MAX];
#ifdef _WIN32
static HANDLE sym_h = INVALID_HANDLE_VALUE;
#define SYM_OPEN (sym_h != INVALID_HANDLE_VALUE)
#else
static int sym_fd = -1;
#define SYM_OPEN (sym_fd >= 0)
#endif

static void sym_close(void)
{
#ifdef _WIN32
    if (sym_h != INVALID_HANDLE_VALUE) {
        CloseHandle(sym_h);
        sym_h = INVALID_HANDLE_VALUE;
    }
#else
    if (sym_fd >= 0) {
        close(sym_fd);
        sym_fd = -1;
    }
#endif
    sym_base = 0;
    sym_len = -1;
    sym_gz = 0;
}

static long sym_raw_read(void *user, char *buf, size_t size)
{
    (void)user;
    if (sym_len >= 0 && (long long)size > sym_len - sym_pos) {
        size = (size_t)(sym_len - sym_pos);
    }
    if (size == 0) {
        return 0;
    }
#ifdef _WIN32
    {
        DWORD got = 0;

        if (!ReadFile(sym_h, buf, (DWORD)size, &got, NULL)) {
            return -1;
        }
        sym_pos += got;
        return (long)got;
    }
#else
    {
        ssize_t r;

        do {
            r = read(sym_fd, buf, size);
        } while (r < 0 && errno == EINTR);
        if (r > 0) {
            sym_pos += r;
        }
        return (long)r;
    }
#endif
}

static int sym_raw_rewind(void *user)
{
    (void)user;
    sym_pos = 0;
#ifdef _WIN32
    {
        LARGE_INTEGER li;

        li.QuadPart = sym_base;
        return SetFilePointerEx(sym_h, li, NULL, FILE_BEGIN) ? 0 : -1;
    }
#else
    return lseek(sym_fd, (off_t)sym_base, SEEK_SET) == (off_t)sym_base ? 0 : -1;
#endif
}

static void sym_set_name(const char *path)
{
    const char *b = path + strlen(path);

    while (b > path && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    snprintf(sym_name, sizeof sym_name, "%s", b);
}

void ico_diag_set_symbols_path(const char *path)
{
    size_t n;

    sym_close();
    sym_name[0] = '\0';
    if (path == NULL || path[0] == '\0') {
        return;
    }
#ifdef _WIN32
    {
        wchar_t *w = ico_widen(path);

        if (w != NULL) {
            sym_h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            free(w);
        }
    }
#else
    sym_fd = open(path, O_RDONLY | O_CLOEXEC);
#endif
    sym_set_name(path);
    n = strlen(path);
    sym_gz = n > 3 && strcmp(path + n - 3, ".gz") == 0;
}

#ifndef _WIN32
void ico_diag_set_symbols_fd(int fd, long long offset, long long length, int gzip, const char *name)
{
    sym_close();
    sym_fd = fd;
    sym_base = offset;
    sym_len = length;
    sym_gz = gzip != 0;
    snprintf(sym_name, sizeof sym_name, "%s", name != NULL ? name : "the symbol table");
}
#endif

/* The map beside the program: its name with ".exe" (when there is one)
   replaced by ".map" (ico_pc_x64.exe: ico_pc_x64.map; ico_pc: ico_pc.map).
   Opened, not read. Android hands its table in (ico_diag_set_symbols_fd);
   macOS and iOS ship none. */
static void sym_open_default(void)
{
#if defined(_WIN32)
    wchar_t w[1024];
    const DWORD n = GetModuleFileNameW(NULL, w, (DWORD)(sizeof w / sizeof w[0]) - 8);
    char path[2048];

    if (n == 0 || n >= sizeof w / sizeof w[0] - 8) {
        return;
    }
    if (n > 4 && (w[n - 4] == L'.')) {
        w[n - 4] = L'\0';
    }
    wcscat(w, L".map");
    if (ico_narrow(w, path, sizeof path) == 0) {
        ico_diag_set_symbols_path(path);
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    char path[1024];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof path - 8);

    if (n <= 0) {
        return;
    }
    path[n] = '\0';
    strcat(path, ".map");
    ico_diag_set_symbols_path(path);
#endif
}

/* The symbol lines after a report's raw ones (a failure here loses
   nothing above): each offset the report printed, in order, as
   "+0x1b35e4 _CopyVector+0x4 (vu0.c.obj)". The file is checked against
   this build by a second function (ico_diag_log must come out at +0x0). */
static void report_symbols(void)
{
    IcoSymSource raw, src;
    char d[256];
    int i, r;
    const int n = sym_n;

    sym_collect = 0;
    if (n == 0) {
        return;
    }
    if (!SYM_OPEN) {
        flog("ico_pc: symbols: no %s beside the program (a build without one); the offsets above "
             "are looked up in the release's map",
             sym_name[0] != '\0' ? sym_name : "link map");
        return;
    }
    raw.read = sym_raw_read;
    raw.rewind = sym_raw_rewind;
    raw.user = NULL;
    if (sym_gz) {
        ico_sym_gz_source(&src, &raw);
    } else {
        src = raw;
    }
    for (i = 0; i < n; i++) {
        sym_hits[i].off = sym_offs[i];
    }
    sym_hits[n].off = (uint64_t)((uintptr_t)ico_diag_log - exe_lo);
    r = ico_sym_resolve(&src, "ico_diag_init", (uint64_t)((uintptr_t)ico_diag_init - exe_lo),
                        sym_hits, n + 1);
    if (r < 0) {
        flog("ico_pc: symbols: %s could not be read or does not name ico_diag_init", sym_name);
        return;
    }
    if (strcmp(sym_hits[n].name, "ico_diag_log") != 0 || sym_hits[n].delta != 0) {
        flog("ico_pc: symbols: %s is not this build's (ico_diag_log is not where it says); the "
             "offsets above stay as they are",
             sym_name);
        return;
    }
    flog("ico_pc: symbols from %s (each offset above in %s, in order):", sym_name, exe_name);
    for (i = 0; i < n; i++) {
        ico_sym_describe(&sym_hits[i], d, sizeof d);
        flog("ico_pc:   +0x%llx %s", (unsigned long long)sym_offs[i],
             d[0] != '\0' ? d : "(before the first function the map names)");
    }
}

/* The first screenful's summary line (ico_diag_set_summary) */
static char summary_text[LINE_MAX_BYTES - 32];

void ico_diag_set_summary(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(summary_text, sizeof summary_text, fmt, ap);
    va_end(ap);
    /* after the lines the C library still holds (Windows buffers stderr):
       the summary follows the device and window lines in the file */
    flush_stdio_if_main();
    ico_diag_log("ico_pc: summary: %s", summary_text);
}

static char disc_text[96];

void ico_diag_set_disc(const char *text)
{
    static const char unknown[] = "disc " ICO_DIAG_DISC_UNKNOWN;
    char *at;

    snprintf(disc_text, sizeof disc_text, "%s", text != NULL ? text : "");
    at = strstr(summary_text, unknown);
    if (at != NULL && disc_text[0] != '\0') {
        char rest[sizeof summary_text];

        snprintf(rest, sizeof rest, "%s", at + sizeof unknown - 1);
        snprintf(at, sizeof summary_text - (size_t)(at - summary_text), "disc %s%s", disc_text,
                 rest);
        ico_diag_log("ico_pc: summary: %s", summary_text);
    }
}

const char *ico_diag_disc(void)
{
    return disc_text;
}

const char *ico_diag_version(void)
{
    return act_version[0] != '\0' ? act_version : "unknown";
}

/* ico_diag_set_report_extra: the host's own part of report.txt */
static void (*volatile report_extra_fn)(char *out, size_t size);

void ico_diag_set_report_extra(void (*fn)(char *out, size_t size))
{
    report_extra_fn = fn;
}

static void report_file(const char *what, int fatal)
{
    IcoReportInputs in;
    char name[160];
    const size_t np = sizeof act_loss_prefix - 1;
    /* one report at a time (report.h): static, a crash handler's stack is
       small */
    static char extra[24 * 1024];
    void (*extra_fn)(char *, size_t) = report_extra_fn;

    memset(&in, 0, sizeof in);
    extra[0] = '\0';
    if (extra_fn != NULL) {
        extra_fn(extra, sizeof extra);
        in.extra = extra;
    }
    in.what = what;
    in.log_path = log_path_copy[0] != '\0' ? log_path_copy : NULL;
    in.summary = summary_text;
    in.disc = disc_text;
    {
        /* open and read only (ico_diag_memory_text): a crash handler's */
        static char mem[160];
        ico_diag_memory_text(mem, sizeof mem);
        in.memory = mem;
    }
    in.uptime_s = ico_diag_uptime();
    /* this run's loss dump, else the one the previous run left */
    if (strncmp(act_loss, act_loss_prefix, np) == 0) {
        in.loss_zip = act_loss + np;
    } else if (strncmp(act_loss, act_loss_hang_prefix, sizeof act_loss_hang_prefix - 1) == 0) {
        in.loss_zip = act_loss + sizeof act_loss_hang_prefix - 1;
    } else if (loss_pending[0] != '\0') {
        in.loss_zip = loss_pending;
    } else if (loss_prev_hang[0] != '\0') {
        in.loss_zip = loss_prev_hang;
    }
#ifdef _WIN32
    if (log_h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(log_h);
    }
#endif
    if (ico_report_write(&in, name, sizeof name) == 0) {
        snprintf(report_name, sizeof report_name, "%s", name);
        if (fatal) {
            flog("ico_pc: report: %s in the logs folder (this log, the previous one, the "
                 "settings and a summary): the file to attach to an issue",
                 name);
        } else {
            ico_diag_log("ico_pc: report: %s in the logs folder (this log, the previous one, the "
                         "settings and a summary): the file to attach to an issue",
                         name);
        }
    } else if (fatal) {
        flog("ico_pc: report: not written (%s)", name);
    } else {
        ico_diag_log("ico_pc: report: not written (%s)", name);
    }
}

const char *ico_diag_write_report(const char *what)
{
    if (!inited) {
        return NULL;
    }
    report_file(what, 0);
    if (report_name[0] != '\0') {
        const int pruned = ico_report_prune(ICO_REPORT_KEEP);

        if (pruned > 0) {
            ico_diag_log("ico_pc: report: %d older report%s deleted (the newest %d kept)", pruned,
                         pruned == 1 ? "" : "s", ICO_REPORT_KEEP);
        }
        return report_name;
    }
    return NULL;
}

const char *ico_diag_report_name(void)
{
    return report_name[0] != '\0' ? report_name : NULL;
}

/* --- Rate-limited lines (ico_log_limited) ----------------------------------------- */

static IcoLogLimit *limit_list;

void ico_log_limit_write(IcoLogLimit *l, const char *fmt, ...)
{
    char buf[LINE_MAX_BYTES - 96];
    const unsigned int c = __atomic_load_n(&l->count, __ATOMIC_RELAXED);
    va_list ap;

    if (__atomic_add_fetch(&l->written, 1, __ATOMIC_RELAXED) == 1) {
        /* the first line of this key: onto the end summary's list */
        IcoLogLimit *head = __atomic_load_n(&limit_list, __ATOMIC_ACQUIRE);

        do {
            l->next = head;
        } while (!__atomic_compare_exchange_n(&limit_list, &head, l, 1, __ATOMIC_RELEASE,
                                              __ATOMIC_ACQUIRE));
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (c == ico_log_limit_first(l)) {
        ico_diag_log("%s (%u of these so far; from here every %uth is written)", buf, c,
                     ico_log_limit_every(l));
    } else if (c > ico_log_limit_first(l)) {
        ico_diag_log("%s (number %u of these)", buf, c);
    } else {
        ico_diag_log("%s", buf);
    }
}

int ico_log_limit_summary(char *out, size_t size)
{
    const IcoLogLimit *l;
    size_t n = 0;
    int keys = 0;

    if (size > 0) {
        out[0] = '\0';
    }
    for (l = __atomic_load_n(&limit_list, __ATOMIC_ACQUIRE); l != NULL; l = l->next) {
        const unsigned int c = __atomic_load_n(&l->count, __ATOMIC_RELAXED);
        const unsigned int w = __atomic_load_n(&l->written, __ATOMIC_RELAXED);
        int k;

        if (n >= size) {
            break;
        }
        k = snprintf(out + n, size - n, "%s%s %u (%u written)", keys ? "; " : "", l->key, c, w);
        if (k < 0) {
            break;
        }
        n += (size_t)k;
        keys++;
    }
    return keys;
}

void ico_log_limit_log_summary(void)
{
    static char buf[LINE_MAX_BYTES - 64];

    if (ico_log_limit_summary(buf, sizeof buf) > 0) {
        ico_diag_log("ico_pc: limited lines (key, count, written): %s", buf);
    }
}

/* --- The fatal end ------------------------------------------------------------------- */

#if defined(__linux__) && !defined(__ANDROID__)
/* 1 once watchdog_fire (the watchdog thread) heads for finish: the end that
   may show a box on Linux; a crash's finish runs in a signal handler */
static int watchdog_ending;
#endif

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
/* The box's words: what happened, and the file to attach (the systems
   whose finish shows a box; Android's is fatal_text) */
static void report_box(char *box, size_t size, const char *box_text)
{
    if (report_name[0] != '\0') {
        snprintf(box, size,
                 "%s\n\nPlease report it at github.com/nathanialf/ico-pc/issues and attach the "
                 "newest file in logs that starts with report- (this time: %s). The log is:\n%s",
                 box_text, report_name, ico_diag_log_path());
    } else {
        snprintf(box, size,
                 "%s\n\nPlease report it at github.com/nathanialf/ico-pc/issues and attach "
                 "the logs folder, the one that holds the log. The log is:\n%s",
                 box_text, ico_diag_log_path());
    }
}
#endif

static void finish(const char *reason, const char *box_text, int code)
{
    flog("ico_pc: the run ended: %s", reason);
    if (exit_hook != NULL) {
        exit_hook(reason);
    }
#ifdef _WIN32
    if (relaunch_value[0] != '\0') {
        if (log_h != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(log_h);
        }
        /* the activity file's name is free for the new run (it keeps the
           note) */
        act_close();
        if (relaunch_now(relaunch_value) == 0) {
            flog("ico_pc: started the game again");
            if (log_h != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(log_h);
            }
            TerminateProcess(GetCurrentProcess(), (UINT)code);
        }
        flog("ico_pc: could not start the game again (error %lu)", (unsigned long)GetLastError());
    }
    {
        char box[1536];
        report_box(box, sizeof box, box_text);
        MessageBoxA(NULL, box, "ICO PC", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    if (log_h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(log_h);
    }
    TerminateProcess(GetCurrentProcess(), (UINT)code);
#else
#ifdef __ANDROID__
    {
        /* the watchdog's end (a crash keeps the system's handlers,
           crash_handler): the box, then the end */
        static char box[1536];
        fatal_ui_flush();
        fatal_text(box, sizeof box, box_text);
        fatal_ui_box(box);
    }
#else
    if (relaunch_value[0] != '\0') {
        relaunch_now(relaunch_value); /* returns only when it failed */
        flog("ico_pc: could not start the game again");
    }
#if defined(__linux__)
    /* Linux: the watchdog's end shows the box (ico_diag_set_fatal_ui, the
       window build) from the watchdog thread, after the log is written. A
       crash ends here inside the signal handler on the crashing thread,
       where showing a box is not safe: no box, the log only. */
    if (watchdog_ending && fatal_box != NULL) {
        static char box[1536];
        report_box(box, sizeof box, box_text);
        /* the box runs SDL's dialog from this thread while the main thread
           hangs; if that call blocks on the stuck window, SIGALRM's default
           action still ends the process (the log is already written) */
        alarm(30);
        fatal_box(box);
    }
#else
    /* macOS (and iOS): a box can only be shown on the main thread, which is
       the one that crashed or hung, so neither end shows one; the log has it */
    (void)box_text;
#endif
#endif
    _exit(code);
#endif
}

/* the crash block in the log, without the end */
static void report_crash_block(void)
{
    char where[96];
    char tmp[80];
    char line[LINE_MAX_BYTES];
    flush_stdio_bounded();
    sym_n = 0;
    sym_collect = 1;
    describe(crash.pc, where, sizeof where);
    flog("ico_pc: ======================================================================");
    flog("ico_pc: CRASH: %s (0x%lx) at %s", crash.what, crash.code, where);
    if (activity[0] != '\0') {
        flog("ico_pc: while: %s", activity);
    }
    if (crash.fault_kind >= 0) {
        flog("ico_pc: the faulting access: %s address 0x%llx",
             crash.fault_kind == 1   ? "writing"
             : crash.fault_kind == 8 ? "executing"
                                     : "reading",
             (unsigned long long)crash.fault_addr);
    }
    flog("ico_pc: the offsets are from %s's load address; the symbol lines after the threads "
         "name them",
         exe_name);
    flog("ico_pc: game thread then: #%d %s", crash.thread,
         thread_name(crash.thread, tmp, sizeof tmp));
    if (crash.thread != 0 && thread_line(crash.thread, line, sizeof line)) {
        flog("ico_pc:   %s", line);
    }
    if (failure[0] != '\0') {
        flog("ico_pc: last failure message: %s", failure);
    }
    if (crash.stack_hi > crash.stack_lo) {
        scan_stack((const unsigned char *)crash.stack_lo, (const unsigned char *)crash.stack_hi);
    }
    heartbeat(1);
    dump_threads();
    /* the names after the raw lines: a failure in the lookup loses none */
    report_symbols();
}

#ifndef __ANDROID__

static void report_crash(void)
{
    char box[256];

    report_crash_block();
    /* the player's box says what to do; what and where are in the log.
       A driver crash while building a pipeline starts the game again
       (finish) and shows the box only when nothing is left to try or the
       new process could not be started. */
    if (driver_note()) {
        driver_crash_plan();
        snprintf(box, sizeof box, "%s", driver_box_text);
    } else {
        snprintf(box, sizeof box, "ICO PC hit an error and had to close.");
    }
    report_file(crash.what, 1);
    finish(crash.what, box, EXIT_CRASH);
}

#endif

/* --- Watchdog ------------------------------------------------------------------------ */

static unsigned char sample_stack[32 * 1024];

/* 1 from ico_diag_init until ico_diag_main_thread_end: the thread that
   called ico_diag_init (the one the watchdog samples) still runs */
static volatile int main_alive;

static size_t sample_len;

static uintptr_t sample_pc;

static uintptr_t sample_sp;

static int sample_thread;

#ifdef _WIN32

static int sample_main(void)
{
    CONTEXT c;
    uintptr_t lo;
    uintptr_t hi;
    sample_len = 0;
    if (!main_alive || w32_suspend_thread == NULL || w32_resume_thread == NULL ||
        w32_suspend_thread(main_thread) == (DWORD)-1) {
        return -1;
    }
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(main_thread, &c)) {
#if defined(_M_X64) || defined(__x86_64__)
        sample_pc = (uintptr_t)c.Rip;
        sample_sp = (uintptr_t)c.Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
        sample_pc = (uintptr_t)c.Pc;
        sample_sp = (uintptr_t)c.Sp;
#endif
        stack_bounds(sample_sp, &lo, &hi);
        if (hi > lo) {
            sample_len = hi - lo > sizeof sample_stack ? sizeof sample_stack : hi - lo;
            memcpy(sample_stack, (const void *)lo, sample_len);
        }
    }
    sample_thread = ico_sched_current();
    w32_resume_thread(main_thread);
    return 0;
}

#else

static volatile sig_atomic_t sample_done;

/* main_alive (ico_diag_main_thread_end) is cleared under this lock and
   read under it around the signal, so a thread that has ended is never
   signalled */
static pthread_mutex_t main_alive_lock = PTHREAD_MUTEX_INITIALIZER;

ICO_ENTRY static void sample_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    uintptr_t lo;
    uintptr_t hi;
    (void)sig;
    (void)si;
#if defined(__APPLE__) && defined(__aarch64__)
    sample_pc = (uintptr_t)uc->uc_mcontext->__ss.__pc;
    sample_sp = (uintptr_t)uc->uc_mcontext->__ss.__sp;
#elif defined(__APPLE__) && defined(__x86_64__)
    sample_pc = (uintptr_t)uc->uc_mcontext->__ss.__rip;
    sample_sp = (uintptr_t)uc->uc_mcontext->__ss.__rsp;
#elif defined(__x86_64__)
    sample_pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    sample_sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
    sample_pc = (uintptr_t)uc->uc_mcontext.pc;
    sample_sp = (uintptr_t)uc->uc_mcontext.sp;
#else
    (void)uc;
#endif
    sample_len = 0;
    stack_bounds_here(sample_sp, &lo, &hi);
    if (hi > lo) {
        sample_len = hi - lo > sizeof sample_stack ? sizeof sample_stack : hi - lo;
        memcpy(sample_stack, (const void *)lo, sample_len);
    }
    sample_thread = ico_sched_current();
    sample_done = 1;
}

static int sample_main(void)
{
    struct timespec ts = {0, 10 * 1000 * 1000};
    int i;
    int sent;
    sample_done = 0;
    sample_len = 0;
    pthread_mutex_lock(&main_alive_lock);
    sent = main_alive && pthread_kill(main_thread, SIGUSR2) == 0;
    pthread_mutex_unlock(&main_alive_lock);
    if (!sent) {
        return -1;
    }
    for (i = 0; i < 50 && !sample_done; i++) {
        nanosleep(&ts, NULL);
    }
    return sample_done ? 0 : -1;
}

#endif

#ifdef _WIN32

/* size bytes at the image's base + off into out, through
   ReadProcessMemory on this process: the main thread runs again after the
   sample, and a module may unload before its header is read; an unmapped
   page fails the read instead of faulting this thread. 0 when read whole. */
static int image_read(uintptr_t base, uint32_t off, void *out, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(base + off), out, size, &got) &&
                   got == size
               ? 0
               : -1;
}

/* A code module's name from its own export table (the DLL's name as it
   was linked: "DINPUT8.dll", "SDL3.dll"), never through the loader: the
   held main thread may own the loader lock (a device scan loads drivers).
   The DOS header, the NT headers, the export directory and the name are
   copied (bounded) into locals by image_read, never dereferenced in place.
   0 with out set, -1 when the image has none or a read failed. */
static int image_export_name(uintptr_t base, char *out, size_t size)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    IMAGE_EXPORT_DIRECTORY ed;
    const IMAGE_DATA_DIRECTORY *d;
    char name[49];
    size_t k;
    if (size == 0 || image_read(base, 0, &dos, sizeof dos) != 0 ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096 ||
        image_read(base, (uint32_t)dos.e_lfanew, &nt, sizeof nt) != 0 ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
        return -1;
    }
    d = &nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (d->VirtualAddress == 0 || d->Size == 0 ||
        d->VirtualAddress >= nt.OptionalHeader.SizeOfImage ||
        image_read(base, d->VirtualAddress, &ed, sizeof ed) != 0 || ed.Name == 0 ||
        ed.Name >= nt.OptionalHeader.SizeOfImage) {
        return -1;
    }
    /* the name, at most 48 bytes (the line's width) and not past the
       image; when those cross into a page that is gone, up to the page's
       end */
    k = sizeof name - 1;
    if (k > nt.OptionalHeader.SizeOfImage - ed.Name) {
        k = nt.OptionalHeader.SizeOfImage - ed.Name;
    }
    if (image_read(base, ed.Name, name, k) != 0) {
        const size_t page = 4096 - (size_t)((base + ed.Name) & 4095u);
        if (page >= k || image_read(base, ed.Name, name, page) != 0) {
            return -1;
        }
        k = page;
    }
    name[k] = '\0';
    if (name[0] == '\0') {
        return -1;
    }
    snprintf(out, size, "%s", name);
    return 0;
}

/* "DINPUT8.dll+0x1234" when a is in executable image memory (a return
   address candidate), else 0 */
static int code_address(uintptr_t a, char *out, size_t size)
{
    MEMORY_BASIC_INFORMATION mbi;
    const DWORD exec =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    char name[49];
    if (a < 0x10000) {
        return 0;
    }
    if (in_exe(a)) {
        snprintf(out, size, "%s+0x%lx", exe_name, (unsigned long)(a - exe_lo));
        return 1;
    }
    if (VirtualQuery((LPCVOID)a, &mbi, sizeof mbi) == 0 || mbi.State != MEM_COMMIT ||
        mbi.Type != MEM_IMAGE || (mbi.Protect & exec) == 0 || mbi.AllocationBase == NULL) {
        return 0;
    }
    if (image_export_name((uintptr_t)mbi.AllocationBase, name, sizeof name) == 0) {
        snprintf(out, size, "%.48s+0x%lx", name,
                 (unsigned long)(a - (uintptr_t)mbi.AllocationBase));
    } else {
        snprintf(out, size, "module@0x%llx+0x%lx",
                 (unsigned long long)(uintptr_t)mbi.AllocationBase,
                 (unsigned long)(a - (uintptr_t)mbi.AllocationBase));
    }
    return 1;
}

/* A wait the system holds, sampled (issue 105, diag_host.h): where the
   main thread is and the code modules on its stack, innermost first (each
   word of the copied stack that points into executable image memory: the
   return addresses and a few stale ones) */
static void os_wait_sample(double held)
{
    static char line[LINE_MAX_BYTES - 200];
    char where[96], a[80];
    size_t n = 0;
    int hits = 0;
    if (sample_main() != 0) {
        ico_log_limited("ico_pc: pump sample",
                        "ico_pc: watchdog: %s has held the main thread for "
                        "%.0f s; it could not be sampled",
                        os_wait_name(), held);
        return;
    }
    if (!code_address(sample_pc, where, sizeof where)) {
        snprintf(where, sizeof where, "0x%llx", (unsigned long long)sample_pc);
    }
    line[0] = '\0';
    for (size_t at = 0; at + sizeof(uintptr_t) <= sample_len && hits < 24;
         at += sizeof(uintptr_t)) {
        uintptr_t w;
        memcpy(&w, sample_stack + at, sizeof w);
        if (code_address(w, a, sizeof a)) {
            const int k = snprintf(line + n, sizeof line - n, "%s%s", hits ? ", " : "", a);
            if (k < 0 || (size_t)k >= sizeof line - n) {
                break;
            }
            n += (size_t)k;
            hits++;
        }
    }
    ico_log_limited("ico_pc: pump sample",
                    "ico_pc: watchdog: %s has held the main thread for %.0f s: at %s; code on its "
                    "stack (innermost first): %s",
                    os_wait_name(), held, where, hits ? line : "none found");
}

#endif

void ico_diag_set_effects_program(int loaded)
{
    effects_program = loaded != 0;
}

/* The excused part of a present that has lasted d ns: nothing without an
   effects program; with one, what is above PRESENT_FREE_NS, capped. */
static unsigned long long present_excused(unsigned long long d)
{
    if (!effects_program || d <= PRESENT_FREE_NS) {
        return 0;
    }
    d -= PRESENT_FREE_NS;
    return d < PRESENT_EXCUSE_NS ? d : PRESENT_EXCUSE_NS;
}

void ico_diag_present_enter(void)
{
    present_since_ns = ico_diag_now_ns();
}

void ico_diag_present_leave(void)
{
    const unsigned long long since = present_since_ns;
    if (since != 0) {
        const unsigned long long d = ico_diag_now_ns() - since;
        present_done_ns += present_excused(d);
    }
    present_since_ns = 0;
}

/* Seconds of present time excused so far (finished ones plus the one in
   progress, see present_excused). *current is the one in progress, 0 if none. */
static double present_seconds(double *current)
{
    unsigned long long done, since, now, cur = 0;
    do {
        done = present_done_ns;
        since = present_since_ns;
        now = ico_diag_now_ns();
    } while (done != present_done_ns);
    if (since != 0 && now > since) {
        cur = now - since;
    }
    if (current != NULL) {
        *current = (double)cur / 1e9;
    }
    return (double)(done + present_excused(cur)) / 1e9;
}

void ico_diag_os_wait_enter(const char *what)
{
    os_wait_what = what;
    /* 0 means "not inside one": a start at 0 ns is moved by one */
    __atomic_store_n(&os_wait_since_ns, ico_diag_now_ns() | 1ull, __ATOMIC_RELEASE);
}

void ico_diag_os_wait_leave(void)
{
    const unsigned long long since = os_wait_since_ns;
    if (since != 0) {
        const unsigned long long now = ico_diag_now_ns();
        const unsigned long long d = now > since ? now - since : 0;
        os_wait_last_ns = d;
        __atomic_add_fetch(&os_wait_done_ns, d, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&os_wait_since_ns, 0ull, __ATOMIC_RELEASE);
}

void ico_diag_set_os_wait_limit(unsigned int seconds)
{
    os_wait_limit_s = seconds;
}

void ico_diag_set_os_wait_describe(void (*fn)(char *out, size_t size))
{
    os_wait_describe = fn;
}

/* the describe function's text, "" without one */
static void os_wait_detail(char *out, size_t size)
{
    void (*fn)(char *, size_t) = os_wait_describe;
    out[0] = '\0';
    if (fn != NULL) {
        fn(out, size);
    }
}

/* Seconds spent in the system's waits so far (finished ones plus the one
   in progress). *current: the one in progress, 0 if none; *since: its
   start (0 if none). */
static double os_wait_seconds(double *current, unsigned long long *since_out)
{
    unsigned long long done, since, now, cur = 0;
    do {
        done = __atomic_load_n(&os_wait_done_ns, __ATOMIC_ACQUIRE);
        since = __atomic_load_n(&os_wait_since_ns, __ATOMIC_ACQUIRE);
        now = ico_diag_now_ns();
    } while (done != __atomic_load_n(&os_wait_done_ns, __ATOMIC_ACQUIRE));
    if (since != 0 && now > since) {
        cur = now - since;
    }
    if (current != NULL) {
        *current = (double)cur / 1e9;
    }
    if (since_out != NULL) {
        *since_out = since;
    }
    return (double)(done + cur) / 1e9;
}

static const char *os_wait_name(void)
{
    const char *w = os_wait_what;
    return w != NULL ? w : "a call into the system";
}

#ifdef __ANDROID__
/* Issue 161: the graphics driver's pipeline creation that does not return.
   On the Retroid Pocket 6 (Adreno 740, compiler E031.41.03.47) a creation of
   the careful start-up set stayed inside the driver for 18 s while the
   device's free memory fell, then the system ended the app without a word
   (v0.9.5), or the whole console froze and a hard reset followed (v0.9.4);
   the watchdog's limits are not in force before the game starts, and the
   GPU hang limit counts waits for the GPU, not a driver call.  While a
   note names a pipeline being created (the set's batch, a lazy run's single
   one) and neither the note nor the set's count has moved for
   PIPE_STALL_S, with the GPU hang limit on ([dev] watchdog = 0 turns both
   off), the run ends here in order: the log, the report, the box, a
   normal exit, and the activity file keeps the note, so the next start
   counts the end and leaves the family out when the note names only one
   (window_host.c window_pipeline_skip).  The longest creation any log has
   shown is 3.4 s (a VU pipeline on a PowerVR phone). */
#define PIPE_STALL_S 10.0
static volatile int pipe_stall;

/* the creation's stall in seconds, or 0 while it moves */
static double pipe_stall_step(double now, int counted)
{
    static unsigned int last_seq, last_done;
    static double since;
    const unsigned int seq = activity_seq, done = pipes_done;
    const int creating = act_mode != 0 && strncmp(activity, "creating pipeline", 17) == 0;
    if (!creating || !counted || gpu_hang_limit_ms == 0 || seq != last_seq || done != last_done) {
        last_seq = seq;
        last_done = done;
        since = now;
        return 0.0;
    }
    return now - since;
}
#endif

static void watchdog_fire(const char *reason)
{
    char where[96];
    char tmp[80];
    char box[1024];
    IcoDiagGpu gpu;
    int tracked, hang;
    if (fatal_once++) {
        return;
    }
    flush_stdio_bounded();
    sym_n = 0;
    sym_collect = 1;
    /* a GPU hang (diag_host.h ico_diag_set_gpu_hang): the main thread waits
       for work the GPU has not finished; the end is a device loss's */
    tracked = gpu_state(&gpu);
    hang = tracked && gpu_hung(&gpu);
    flog("ico_pc: ======================================================================");
    flog("ico_pc: WATCHDOG: %s", reason);
    if (hang) {
        flog("ico_pc: the graphics driver did not finish the picture (submission %llu has not "
             "finished after a %.0f s wait)",
             gpu.waiting_for, gpu.waiting_s);
        flog("ico_pc: the GPU: the main thread has waited %.0f s for submission %llu; the newest "
             "is %llu (replay %u, frame %u), the newest seen finished %llu; this end counts as a "
             "device loss (the loss dump; the next start counts the loss and does not replay the "
             "frame)",
             gpu.waiting_s, gpu.waiting_for, gpu.submitted, gpu.replay, gpu.frame, gpu.finished);
    }
    {
        double cur;
        (void)present_seconds(&cur);
        if (cur > 0.0 && !hang) {
            flog("ico_pc: the game has waited %.0f s for a picture to be shown (an effects "
                 "program may be preparing its effects)",
                 cur);
            if (tracked && gpu.submitted != 0 && gpu.waiting_for == 0) {
                /* inside the present but not waiting for the GPU: the
                   driver's acquire or present call has not returned */
                flog("ico_pc: the GPU: no wait for it in progress; the newest submission is %llu "
                     "(replay %u, frame %u, made %.0f s ago), the newest seen finished %llu",
                     gpu.submitted, gpu.replay, gpu.frame, gpu.submitted_ago_s, gpu.finished);
            }
        }
        (void)os_wait_seconds(&cur, NULL);
        if (cur > 0.0) {
            static char detail[LINE_MAX_BYTES - 64];
            flog("ico_pc: the main thread has been inside %s for %.0f s (a call into the "
                 "system that has not returned)",
                 os_wait_name(), cur);
            os_wait_detail(detail, sizeof detail);
            if (detail[0] != '\0') {
                flog("ico_pc: %s", detail);
            }
        }
    }
    if (sample_main() == 0) {
        describe(sample_pc, where, sizeof where);
        flog("ico_pc: the main thread was at %s, running #%d %s", where, sample_thread,
             thread_name(sample_thread, tmp, sizeof tmp));
        flog("ico_pc: (a game thread that never reaches a kernel call keeps the host loop from "
             "running; the address shows where)");
        if (sample_len > 0) {
            scan_stack(sample_stack, sample_stack + sample_len);
        }
    } else {
        flog("ico_pc: could not sample the main thread");
    }
    if (activity[0] != '\0') {
        flog("ico_pc: while: %s", activity);
    }
    if (failure[0] != '\0') {
        flog("ico_pc: last failure message: %s", failure);
    }
    heartbeat(1);
    dump_threads();
    report_symbols();
    if (hang) {
        /* what a device loss writes (window_host.c device_lost_quit): the
           pass ring and the loss dump, from the frames the renderer holds
           (CPU data: the main thread that writes them is blocked in the
           driver), noted for the next start with the hang's own line
           (loss_hang: counted as a loss, never bisected); a loss without a
           dump when none was noted */
        void (*dump)(void) = gpu_dump_fn;
        loss_hang = 1;
        if (dump != NULL) {
            dump();
        }
#ifdef __ANDROID__
        fatal_ui_flush();
#else
        flush_stdio_bounded();
#endif
        if (act_loss[0] == '\0') {
            ico_diag_note_device_loss(NULL);
        }
    }
    {
        char what[LINE_MAX_BYTES - 64];

        if (hang) {
            snprintf(what, sizeof what,
                     "the graphics driver stopped responding (submission %llu has not finished "
                     "after a %.0f s wait)",
                     gpu.waiting_for, gpu.waiting_s);
        } else {
            snprintf(what, sizeof what, "the watchdog: %s", reason);
        }
        report_file(what, 1);
    }
    /* the reason is in the log; the box says what happened in plain words
       (a GPU hang: a device loss's, window_host.c device_lost_quit) */
#ifdef __ANDROID__
    {
        /* the device's own Qualcomm driver: the Options route to a Turnip
           driver package (the window's note, ico_diag_set_gpu_hang_note) */
        const char *(*notefn)(void) = gpu_hang_note_fn;
        const char *note = hang && notefn != NULL ? notefn() : NULL;

        snprintf(box, sizeof box, "%s%s%s",
                 pipe_stall ? "The graphics driver stopped while preparing the graphics, so the "
                              "game has to close.\n\nStart the game again: it prepares them "
                              "another way."
                 : hang     ? "The graphics driver stopped responding while the game was drawing, "
                              "so the game has to close.\n\nStart the game again. If this "
                              "happens every time at the same place, please report it at "
                              "github.com/nathanialf/ico-pc/issues."
                            : "ICO stopped responding, so it has to close.",
                 note != NULL && note[0] != '\0' ? "\n\n" : "", note != NULL ? note : "");
        if (note != NULL && note[0] != '\0') {
            flog("ico_pc: the box names the graphics driver route (the device's own Qualcomm "
                 "driver)");
        }
    }
#else
    snprintf(box, sizeof box, "%s",
             hang ? "The graphics card stopped responding while the game was drawing, so the "
                    "game has to close. Your last save is kept; the log says why.\n\nIf the "
                    "graphics card is overclocked, try its stock clocks first."
                  : "ICO PC stopped responding, so it was closed.");
#ifdef __linux__
    watchdog_ending = 1;
#endif
#endif
    finish(hang ? "the graphics driver stopped responding (watchdog)" : "watchdog", box,
           EXIT_WATCHDOG);
}

static unsigned int wd_first_s;

static unsigned int wd_later_s;

/* ico_diag_watchdog_pause: set by any thread, read by the watchdog's */
static volatile int wd_paused;

void ico_diag_watchdog_pause(int paused)
{
    if ((paused != 0) == (wd_paused != 0)) {
        return;
    }
    wd_paused = paused != 0;
    ico_diag_log("ico_pc: diagnostics: watchdog %s", paused ? "paused" : "running again");
}

void ico_diag_main_thread_end(void)
{
#ifndef _WIN32
    pthread_mutex_lock(&main_alive_lock);
#endif
    main_alive = 0;
#ifndef _WIN32
    pthread_mutex_unlock(&main_alive_lock);
#endif
}

/* ico_diag_start after ico_diag_start_reporter: bumped (release) once the
   limits are set, so the watchdog takes them and counts them from then */
static unsigned int wd_epoch;

static void watchdog_loop(void)
{
    double start = ico_diag_uptime();
    double last_beat = start;
    double last_alive = start;
    double last_poll = start;
    /* seconds inside presents when the clock last restarted: those since
       are not the game's idle time */
    double pres_start = present_seconds(NULL);
    double pres_alive = pres_start;
    /* the same for the system's waits (ico_diag_os_wait_enter): the time
       the main thread spent where the system held it */
    double os_start = os_wait_seconds(NULL, NULL);
    double os_alive = os_start;
    /* the wait the "waiting" line was written for (its start; 0: none),
       and the suspended time inside the wait in progress (not the
       system's holding: the machine slept) */
    unsigned long long os_logged = 0;
    unsigned long long os_gap_since = 0;
    double os_gap = 0.0;
    unsigned int last_ticks = 0;
    unsigned int last_progress = progress;
    unsigned int epoch = __atomic_load_n(&wd_epoch, __ATOMIC_ACQUIRE);
    /* this thread's copies, read again when the epoch moves (set before
       it, so they are seen with it) */
    unsigned int first_s = wd_first_s;
    unsigned int later_s = wd_later_s;
    char reason[200];
    /* the GPU hang limit's watch (gpu_hang.h): the wait it counts */
    IcoGpuHangWatch hang_watch;
    memset(&hang_watch, 0, sizeof hang_watch);
    for (;;) {
        double now;
        unsigned int ticks;
        unsigned int prog;
        double pres;
        double os;
        double os_cur;
        unsigned long long os_since;
        unsigned int lim;
        double poll_dt;
        int poll_counted;
        unsigned int hang_ms;
        if (epoch != __atomic_load_n(&wd_epoch, __ATOMIC_ACQUIRE)) {
            /* the limits were just set (ico_diag_start): both count from now */
            epoch = __atomic_load_n(&wd_epoch, __ATOMIC_ACQUIRE);
            first_s = wd_first_s;
            later_s = wd_later_s;
            start = last_alive = last_poll = ico_diag_uptime();
            pres_start = pres_alive = present_seconds(NULL);
            os_start = os_alive = os_wait_seconds(NULL, NULL);
            last_ticks = ticks_fn != NULL ? ticks_fn() : 0;
            last_progress = progress;
        }
#ifdef _WIN32
        if (WaitForSingleObject(crash_event, 250) == WAIT_OBJECT_0) {
            report_crash();
            SetEvent(crash_done);
            Sleep(INFINITE);
        }
#else
        struct timespec ts = {0, 250 * 1000 * 1000};
        nanosleep(&ts, NULL);
        /* a crashing thread's flush and box (fatal_ui_*) */
        fatal_ui_serve();
#endif
        if (fatal_once || !main_alive) {
            continue;
        }
        now = ico_diag_uptime();
        os = os_wait_seconds(&os_cur, &os_since);
        if (os_since != os_gap_since) {
            os_gap_since = os_since;
            os_gap = 0.0;
        }
        poll_dt = now - last_poll;
        poll_counted = poll_dt <= SUSPEND_GAP_S && !wd_paused;
        if (now - last_poll > SUSPEND_GAP_S || wd_paused) {
            /* the clock jumped between two polls: the system slept (Windows'
               GetTickCount64 counts sleep and hibernation) or the process was
               stopped (a debugger, SIGSTOP); or the watchdog is paused (the
               app in the background). That time is not the game's, so
               neither limit counts it. */
            start += now - last_poll;
            last_alive += now - last_poll;
            if (os_since != 0) {
                /* nor is it the system's holding of a wait: the bound
                   below does not count a night's sleep inside the pump */
                os_gap += now - last_poll;
                os_start += now - last_poll;
                os_alive += now - last_poll;
            }
        }
        last_poll = now;
        if (wd_paused) {
            last_beat = now;
            continue;
        }
        if (now - last_beat >= HEARTBEAT_S) {
            last_beat = now;
            heartbeat(0);
        }
        /* alive while either moves: a Main tick, or the progress a long
           stretch inside one Main tick reports (a movie plays whole inside
           one: the 137 s attract outlasted the later limit) */
        ticks = ticks_fn != NULL ? ticks_fn() : 0;
        prog = progress;
        pres = present_seconds(NULL);
        if (ticks != last_ticks || prog != last_progress) {
            last_ticks = ticks;
            last_progress = prog;
            last_alive = now;
            pres_alive = pres;
            os_alive = os;
        }
        /* The main thread inside a wait the system holds (the window's
           event pump): its time counts towards neither limit below, since
           the game's code is not running and cannot be hung. Once a wait
           outlasts the limit in force it is named, once; only past the
           bound for one wait does the run end, so a deadlock in the system
           is still reported. */
        lim = ticks == 0 ? first_s : later_s;
#ifdef _WIN32
        {
            /* a held wait sampled at 2, 10 and 30 s into it (issue 105:
               which code the system holds the pump in) */
            static const double steps[3] = {2.0, 10.0, 30.0};
            static unsigned long long sampled_since;
            static int sampled;
            if (os_since != sampled_since) {
                sampled_since = os_since;
                sampled = 0;
            }
            if (os_since != 0 && sampled < 3 && os_cur - os_gap >= steps[sampled]) {
                os_wait_sample(os_cur - os_gap);
                while (sampled < 3 && os_cur - os_gap >= steps[sampled]) {
                    sampled++;
                }
            }
        }
#endif
        if (os_logged != 0 && os_since != os_logged) {
            /* the wait named below came back (os_wait_last_ns: it, unless
               a later one already finished, which is short) */
            ico_diag_log("ico_pc: watchdog: %s returned after %.1f s; the game goes on",
                         os_wait_name(), (double)os_wait_last_ns / 1e9);
            os_logged = 0;
        }
        if (os_since != 0 && os_since != os_logged && lim != 0 && os_cur - os_gap >= lim) {
            static char detail[LINE_MAX_BYTES - 64];
            os_logged = os_since;
            ico_diag_log("ico_pc: watchdog: %s has not returned for %.0f s (the system is holding "
                         "it: the game is not hung); waiting",
                         os_wait_name(), os_cur - os_gap);
            os_wait_detail(detail, sizeof detail);
            if (detail[0] != '\0') {
                ico_diag_log("ico_pc: watchdog: %s", detail);
            }
        }
        /* The GPU hang limit (gpu_hang.h, issue 123): one wait for a
           submission the GPU has not finished, seen for the limit's time
           (counted as the limits below count), ends the run as the GPU
           hang now (watchdog_fire: the pass ring, the loss dump, the loss
           count, the box), not after the later limit's 2 x watchdog; only
           while a limit is in force, like the bound below. One end: this
           thread runs either, once (fatal_once). */
        hang_ms = gpu_hang_limit_ms;
        if (hang_ms != 0 && (first_s != 0 || later_s != 0)) {
            IcoDiagGpu g;
            if (gpu_state(&g) &&
                ico_gpu_hang_step(&hang_watch, (double)hang_ms / 1000.0, g.waiting_for, g.finished,
                                  poll_dt, poll_counted)) {
                snprintf(reason, sizeof reason,
                         "the GPU: submission %llu not finished after %.0f s (limit %.0f s); the "
                         "newest is %llu, the newest seen finished %llu",
                         g.waiting_for, g.waiting_s, (double)hang_ms / 1000.0, g.submitted,
                         g.finished);
                watchdog_fire(reason);
                continue;
            }
        }
#ifdef __ANDROID__
        {
            /* issue 161: a pipeline creation that has not returned
               (pipe_stall_step), in force before the game starts too */
            const double stalled = pipe_stall_step(now, poll_counted);
            if (stalled >= PIPE_STALL_S) {
                ico_diag_log("ico_pc: the graphics driver has not returned from creating a "
                             "pipeline for %.0f s (pipelines %u of %u): the run ends now, in "
                             "order, so the next start can prepare the graphics another way",
                             stalled, pipes_done, pipes_total);
                snprintf(reason, sizeof reason,
                         "a pipeline creation has not returned for %.0f s (limit %.0f s)", stalled,
                         PIPE_STALL_S);
                pipe_stall = 1;
                watchdog_fire(reason);
                continue;
            }
        }
#endif
        /* the bound only while a limit is in force: with the watchdog off
           ([dev] watchdog = 0), and on Android before the game starts
           (ico_diag_start_reporter, the start-up set's progress pump), a
           wait the system holds never ends the run */
        if (os_since != 0 && os_wait_limit_s != 0 && (first_s != 0 || later_s != 0) &&
            os_cur - os_gap >= os_wait_limit_s) {
            snprintf(reason, sizeof reason,
                     "%s has not returned for %u s (the system holds it; the limit is [dev] "
                     "watchdog_pump)",
                     os_wait_name(), os_wait_limit_s);
            watchdog_fire(reason);
        } else if (first_s != 0 && ticks == 0 &&
                   now - start - (pres - pres_start) - (os - os_start) >= first_s) {
            snprintf(reason, sizeof reason, "no Main tick %u s after boot started (watchdog=%u)",
                     first_s, first_s);
            watchdog_fire(reason);
        } else if (later_s != 0 && ticks > 0 &&
                   now - last_alive - (pres - pres_alive) - (os - os_alive) >= later_s) {
            if (movie_playing) {
                snprintf(reason, sizeof reason,
                         "a movie was playing and showed no new picture for %u s (the last "
                         "was tick %u, progress %u)",
                         later_s, ticks, prog);
            } else {
                snprintf(reason, sizeof reason,
                         "no new Main tick for %u s (the last was tick %u, progress %u)", later_s,
                         ticks, prog);
            }
            watchdog_fire(reason);
        }
    }
}

#ifdef _WIN32

ICO_ENTRY static DWORD WINAPI watchdog_main(LPVOID arg)
{
    (void)arg;
    watchdog_loop();
    return 0;
}

#else

ICO_ENTRY static void *watchdog_main(void *arg)
{
    (void)arg;
    watchdog_loop();
    return NULL;
}

#endif

/* the thread itself (ico_diag_start, ico_diag_start_reporter); 0 when it
   could not start */
static int reporter_create(void);

void ico_diag_start(unsigned int first_s, unsigned int later_s)
{
    wd_first_s = first_s;
    wd_later_s = later_s;
    if (!inited) {
        return;
    }
    if (reporter_running) {
        /* started early (ico_diag_start_reporter): the limits count from
           here, as they would from the thread's start */
        __atomic_add_fetch(&wd_epoch, 1, __ATOMIC_RELEASE);
    } else if (!reporter_create()) {
        return;
    }
    ico_diag_log("ico_pc: diagnostics: heartbeat every %.0f s; watchdog: first Main tick within "
                 "%u s, then a new one or a movie picture within %u s (0 = off); time the system "
                 "holds the window's event pump is not counted, up to %u s for one wait (0 = no "
                 "limit); GPU hang limit %.1f s for one wait for the GPU (0 = off)",
                 HEARTBEAT_S, first_s, later_s, os_wait_limit_s,
                 (double)gpu_hang_limit_ms / 1000.0);
}

void ico_diag_start_reporter(void)
{
    if (!inited || reporter_running) {
        return;
    }
    if (reporter_create()) {
        ico_diag_log("ico_pc: diagnostics: heartbeat every %.0f s; the watchdog starts with the "
                     "game",
                     HEARTBEAT_S);
    }
}

static int reporter_create(void)
{
#ifdef _WIN32
    {
        HANDLE h = w32_create_thread != NULL
                       ? w32_create_thread(NULL, 256 * 1024, watchdog_main, NULL, 0, &reporter_tid)
                       : NULL;
        if (h == NULL) {
            ico_diag_log("ico_pc: diagnostics: cannot start the watchdog thread");
            return 0;
        }
        CloseHandle(h);
    }
#else
    {
        pthread_t t;
        if (pthread_create(&t, NULL, watchdog_main, NULL) != 0) {
            ico_diag_log("ico_pc: diagnostics: cannot start the watchdog thread");
            return 0;
        }
#ifdef __ANDROID__
        reporter_thread = t;
#endif
        pthread_detach(t);
    }
#endif
    reporter_running = 1;
    return 1;
}

/* Exception codes that are messages to a debugger, never faults: the thread
   naming exception (MS_VC_EXCEPTION, raised by SDL for every thread it
   makes, and by drivers) and the two OutputDebugString codes. The Windows
   handlers ignore them; the table is portable so a test can read it. */
int ico_diag_benign_exception(unsigned int code)
{
    return code == 0x406D1388u || code == 0x40010006u || code == 0x4001000Au;
}

int ico_diag_fatal_started(void)
{
    return fatal_once != 0;
}

/* --- Crash handlers ------------------------------------------------------------------- */

#ifdef _WIN32

static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION (a trap: __builtin_trap / ICO_BREAK)";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_IN_PAGE_ERROR:
        return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT:
        return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_UNDERFLOW:
        return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_FLT_INEXACT_RESULT:
        return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_STACK_CHECK:
        return "EXCEPTION_FLT_STACK_CHECK";
    case 0xC00002B5:
        return "STATUS_FLOAT_MULTIPLE_TRAPS";
    case 0xC00002B4:
        return "STATUS_FLOAT_MULTIPLE_FAULTS";
    case 0xC0000409:
        return "STATUS_STACK_BUFFER_OVERRUN (fast fail)";
    }
    return "an exception";
}

static int fatal_code(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_STACK_CHECK:
    case 0xC00002B5:
    case 0xC00002B4:
        return 1;
    }
    return 0;
}

/* The faulting thread: record, hand the report to the watchdog thread (a
   fresh stack) and wait, or report here when there is none. */
static void crash_common(void)
{
    if (reporter_running && GetCurrentThreadId() != reporter_tid) {
        SetEvent(crash_event);
        WaitForSingleObject(crash_done, 60000);
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH);
    }
    report_crash();
}

static void crash_from_exception(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    if (InterlockedExchange((volatile LONG *)&fatal_once, 1) != 0) {
        /* a second fault while reporting: stop at once */
        if (GetCurrentThreadId() != reporter_tid) {
            Sleep(60000);
        }
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH);
    }
    crash.what = exception_name(r->ExceptionCode);
    crash.code = r->ExceptionCode;
    crash.pc = (uintptr_t)r->ExceptionAddress;
#if defined(_M_X64) || defined(__x86_64__)
    crash.sp = (uintptr_t)c->Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    crash.sp = (uintptr_t)c->Sp;
#endif
    crash.fault_kind = -1;
    if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        r->NumberParameters >= 2) {
        crash.fault_kind = (int)r->ExceptionInformation[0];
        crash.fault_addr = (uintptr_t)r->ExceptionInformation[1];
    }
    crash.thread = ico_sched_current();
    stack_bounds(crash.sp, &crash.stack_lo, &crash.stack_hi);
    crash_common();
}

/* A benign exception (ico_diag_benign_exception) was raised on this thread:
   log it once per raising module, at most 8 modules. The raiser is the first
   stack frame outside kernelbase.dll and ntdll.dll (the VEH runs on the
   raising thread). Nothing here allocates or takes the log lock. */
static void benign_note(DWORD code, const char *suffix)
{
    static volatile LONG seen_count;
    static HMODULE seen[8];
    void *frames[16];
    USHORT n, i;
    HMODULE who = NULL;
    char path[MAX_PATH];
    const char *name = "unknown module";
    LONG k;

    n = CaptureStackBackTrace(0, 16, frames, NULL);
    for (i = 0; i < n; i++) {
        HMODULE m = NULL;
        const char *b;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)frames[i], &m) ||
            GetModuleFileNameA(m, path, sizeof path) == 0) {
            continue;
        }
        b = strrchr(path, '\\');
        b = b != NULL ? b + 1 : path;
        if (_stricmp(b, "kernelbase.dll") == 0 || _stricmp(b, "ntdll.dll") == 0 ||
            _stricmp(b, "kernel32.dll") == 0) {
            continue;
        }
        who = m;
        name = b;
        break;
    }
    for (k = 0; k < seen_count && k < 8; k++) {
        if (seen[k] == who) {
            return;
        }
    }
    k = InterlockedIncrement(&seen_count) - 1;
    if (k >= 8) {
        return;
    }
    seen[k] = who;
    if (code == 0x406D1388u) {
        flog("diag: thread-name exception from %s ignored (thread %lu)%s", name,
             (unsigned long)GetCurrentThreadId(), suffix);
    } else {
        /* the two OutputDebugString codes (DBG_PRINTEXCEPTION_C / _WIDE_C) */
        flog("diag: debugger message 0x%08lx from %s ignored (thread %lu)%s", (unsigned long)code,
             name, (unsigned long)GetCurrentThreadId(), suffix);
    }
}

ICO_ENTRY static LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    uintptr_t a = (uintptr_t)r->ExceptionAddress;
    int ours = in_exe(a);
    if (!ours) {
        /* the C runtime the game calls (memcpy, strcmp, ...) */
        HMODULE m = NULL;
        char path[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)a, &m) &&
            GetModuleFileNameA(m, path, sizeof path) > 0) {
            const char *b = strrchr(path, '\\');
            b = b != NULL ? b + 1 : path;
            ours = _strnicmp(b, "msvcrt", 6) == 0 || _strnicmp(b, "ucrtbase", 8) == 0;
        }
    }
    if (ico_diag_benign_exception(r->ExceptionCode)) {
        /* what SDL's own temporary handler does: the raise returns */
        benign_note(r->ExceptionCode, "");
        /* a raise that is not continuable cannot return: leave it to the next handler */
        if (!(r->ExceptionFlags & EXCEPTION_NONCONTINUABLE)) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (fatal_code(r->ExceptionCode) && (ours || r->ExceptionCode == EXCEPTION_STACK_OVERFLOW)) {
        crash_from_exception(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

ICO_ENTRY static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep)
{
    if (ico_diag_benign_exception(ep->ExceptionRecord->ExceptionCode)) {
        benign_note(ep->ExceptionRecord->ExceptionCode, " (reached the unhandled filter)");
        if (!(ep->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        /* not continuable: it is reported as a crash below */
    }
    crash_from_exception(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

ICO_ENTRY static void abort_handler(int sig)
{
    volatile char here = 0;
    (void)sig;
    if (InterlockedExchange((volatile LONG *)&fatal_once, 1) != 0) {
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH);
    }
    crash.what = ABORT_WHAT;
    crash.code = SIGABRT;
    crash.pc = 0;
    crash.sp = (uintptr_t)&here;
    crash.fault_kind = -1;
    crash.thread = ico_sched_current();
    stack_bounds(crash.sp, &crash.stack_lo, &crash.stack_hi);
    crash_common();
}

static void install_handlers(void)
{
    SetUnhandledExceptionFilter(unhandled_filter);
    signal(SIGABRT, abort_handler);
    /* no "this program has stopped working" box on top of ours */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

static void exe_range(void)
{
    HMODULE m = GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)m;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)m + dos->e_lfanew);
    char path[MAX_PATH];
    exe_lo = (uintptr_t)m;
    exe_hi = exe_lo + nt->OptionalHeader.SizeOfImage;
    if (GetModuleFileNameA(m, path, sizeof path) > 0) {
        const char *b = strrchr(path, '\\');
        snprintf(exe_name, sizeof exe_name, "%.63s", b != NULL ? b + 1 : path);
    }
}

#else

static char alt_stack[64 * 1024];

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL (a trap: __builtin_trap / ICO_BREAK)";
    case SIGFPE:
        return "SIGFPE";
    case SIGTRAP:
        return "SIGTRAP";
    case SIGABRT:
        return ABORT_WHAT;
    }
    return "a signal";
}

#ifdef __ANDROID__

static void restore_handlers(void);

#endif

ICO_ENTRY static void crash_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    if (fatal_once++) {
        _exit(EXIT_CRASH);
    }
    crash.what = signal_name(sig);
    crash.code = (unsigned long)sig;
#if defined(__APPLE__) && defined(__aarch64__)
    crash.pc = (uintptr_t)uc->uc_mcontext->__ss.__pc;
    crash.sp = (uintptr_t)uc->uc_mcontext->__ss.__sp;
#elif defined(__APPLE__) && defined(__x86_64__)
    crash.pc = (uintptr_t)uc->uc_mcontext->__ss.__rip;
    crash.sp = (uintptr_t)uc->uc_mcontext->__ss.__rsp;
#elif defined(__x86_64__)
    crash.pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    crash.sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
    crash.pc = (uintptr_t)uc->uc_mcontext.pc;
    crash.sp = (uintptr_t)uc->uc_mcontext.sp;
#else
    (void)uc;
#endif
    crash.fault_kind = -1;
    if (sig == SIGSEGV || sig == SIGBUS) {
        crash.fault_kind = 0;
        crash.fault_addr = (uintptr_t)si->si_addr;
    }
    crash.thread = ico_sched_current();
    stack_bounds_here(crash.sp, &crash.stack_lo, &crash.stack_hi);
#ifdef __ANDROID__
    /* the C library's last words first (abort, FORTIFY and the
       stack protector write them to stderr, the log mirror's pipe) */
    fatal_ui_flush();
    report_crash_block();
    report_file(crash.what, 1);
    flog("ico_pc: the run ended: %s", crash.what);
    if (exit_hook != NULL) {
        exit_hook(crash.what);
    }
    {
        /* a box instead of the game just going (the system shows
           nothing), from the watchdog thread; this thread waits for it */
        static char box[1536];
        if (driver_note() && act_mode == ICO_DIAG_ACTIVITY_STARTUP) {
            /* the start-up activity file keeps the note: the next start
               creates the pipelines as the frames need them */
            flog("ico_pc: the graphics driver crashed while preparing the shaders; the next "
                 "start creates the pipelines as the frames need them");
            fatal_text(box, sizeof box, driver_next_start_text);
        } else if (driver_note()) {
            driver_crash_plan(); /* logged; no relaunch on Android */
            fatal_text(box, sizeof box, driver_box_text);
        } else {
            fatal_text(box, sizeof box, "ICO ran into a problem and has to close.");
        }
        fatal_ui_box(box);
    }
    flog("ico_pc: the system's crash report (tombstone) follows");
    /* the system's handlers back; a fault repeats when this returns, a
       signal sent by a call (abort, kill) is sent again, pending until
       then */
    restore_handlers();
    if (si->si_code <= 0) {
        raise(sig);
    }
#else
    report_crash();
#endif
}

#ifdef __ANDROID__

/* libmain.so's extent: its executable segments, from its load address */
static int module_phdr(struct dl_phdr_info *info, size_t size, void *user)
{
    uintptr_t base = *(const uintptr_t *)user;
    uintptr_t hi = 0;
    int i;

    (void)size;
    if ((uintptr_t)info->dlpi_addr != base) {
        return 0;
    }
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];

        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) != 0) {
            uintptr_t end = base + (uintptr_t)ph->p_vaddr + (uintptr_t)ph->p_memsz;

            if (end > hi) {
                hi = end;
            }
        }
    }
    exe_hi = hi;
    return 1;
}

static void exe_range(void)
{
    Dl_info info;
    uintptr_t base;

    if (dladdr((void *)ico_diag_init, &info) == 0 || info.dli_fbase == NULL) {
        return;
    }
    base = (uintptr_t)info.dli_fbase;
    exe_lo = base;
    dl_iterate_phdr(module_phdr, &base);
    if (info.dli_fname != NULL) {
        const char *b = strrchr(info.dli_fname, '/');

        snprintf(exe_name, sizeof exe_name, "%.63s", b != NULL ? b + 1 : info.dli_fname);
    }
}

#else

#ifdef __APPLE__
static void exe_range(void)
{
    Dl_info info;
    if (!dladdr((const void *)ico_diag_init, &info)) {
        return;
    }
    /* Locate this image, which LiveContainer loads alongside its own executable. */
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header_64 *h = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if ((const void *)h != info.dli_fbase || h->magic != MH_MAGIC_64) {
            continue;
        }
        const struct load_command *c = (const struct load_command *)(h + 1);
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        for (uint32_t j = 0; j < h->ncmds; j++) {
            if (c->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg = (const struct segment_command_64 *)c;
                if (strcmp(seg->segname, SEG_TEXT) == 0) {
                    exe_lo = (uintptr_t)(seg->vmaddr + slide);
                    exe_hi = exe_lo + (uintptr_t)seg->vmsize;
                    break;
                }
            }
            c = (const struct load_command *)((const char *)c + c->cmdsize);
        }
        break;
    }
    const char *name = strrchr(info.dli_fname, '/');
    snprintf(exe_name, sizeof(exe_name), "%.63s", name ? name + 1 : info.dli_fname);
}
#else
extern char __executable_start[] __attribute__((weak));
extern char etext[] __attribute__((weak));

static void exe_range(void)
{
    char path[512];
    ssize_t n;
    if (__executable_start != NULL && etext != NULL) {
        exe_lo = (uintptr_t)__executable_start;
        exe_hi = (uintptr_t)etext;
    }
    n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n > 0) {
        const char *b;
        path[n] = '\0';
        b = strrchr(path, '/');
        snprintf(exe_name, sizeof exe_name, "%.63s", b != NULL ? b + 1 : path);
    }
}

#endif

#endif

static const int crash_sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};

#ifdef __ANDROID__

/* the handlers in place before ours (the system's crash reporter) */
static struct sigaction old_actions[sizeof crash_sigs / sizeof crash_sigs[0]];

static void restore_handlers(void)
{
    size_t i;

    for (i = 0; i < sizeof crash_sigs / sizeof crash_sigs[0]; i++) {
        sigaction(crash_sigs[i], &old_actions[i], NULL);
    }
}

#endif

static void install_handlers(void)
{
    const int *sigs = crash_sigs;
    struct sigaction sa;
    stack_t st;
#ifndef __APPLE__
    pthread_attr_t attr;
#endif
    size_t i;

    main_thread = pthread_self();
#ifdef __APPLE__
    main_stack_hi = (uintptr_t)pthread_get_stackaddr_np(main_thread);
    main_stack_lo = main_stack_hi - pthread_get_stacksize_np(main_thread);
#else
    if (pthread_getattr_np(main_thread, &attr) == 0) {
        void *addr;
        size_t size;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) {
            main_stack_lo = (uintptr_t)addr;
            main_stack_hi = (uintptr_t)addr + size;
        }
        pthread_attr_destroy(&attr);
    }
#endif
    memset(&st, 0, sizeof st);
    st.ss_sp = alt_stack;
    st.ss_size = sizeof alt_stack;
    sigaltstack(&st, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof crash_sigs / sizeof crash_sigs[0]; i++) {
#ifdef __ANDROID__
        sigaction(sigs[i], &sa, &old_actions[i]);
#else
        sigaction(sigs[i], &sa, NULL);
#endif
    }
    sa.sa_sigaction = sample_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);
}

#endif

void ico_diag_init(const char *log_path)
{
    if (inited) {
        return;
    }
#ifdef _WIN32
    t0 = GetTickCount64();
    w32_threads();
    InitializeCriticalSection(&lock);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread, 0,
                    FALSE, DUPLICATE_SAME_ACCESS);
    main_tid = GetCurrentThreadId();
    crash_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    crash_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (log_path != NULL) {
        /* a UTF-8 path (host_fs.h) */
        wchar_t *wp = ico_widen(log_path);

        if (wp != NULL) {
            log_h = CreateFileW(wp, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            free(wp);
        } else {
            log_h = CreateFileA(log_path, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
    }
#else
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (log_path != NULL) {
        log_fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    }
#endif
    if (log_path != NULL) {
        snprintf(log_path_copy, sizeof log_path_copy, "%s", log_path);
    }
    exe_range();
    sym_open_default();
    install_handlers();
    main_alive = 1;
    inited = 1;
#ifdef __ANDROID__
    ico_diag_log("ico_pc: diagnostics: %s at 0x%lx; this thread's stack 0x%lx..0x%lx (%lu KB)",
                 exe_name, (unsigned long)exe_lo, (unsigned long)main_stack_lo,
                 (unsigned long)main_stack_hi,
                 (unsigned long)((main_stack_hi - main_stack_lo) / 1024));
#endif
}

#ifdef _WIN32

void ico_diag_arm_vectored(void)
{
    AddVectoredExceptionHandler(1, vectored_handler);
}

#endif

/* --- Float faults ------------------------------------------------------------- */

#define FLOAT_FAULT_SITES 32

static struct {
    const char *site;
    const void *caller;
} float_fault_seen[FLOAT_FAULT_SITES];

static int float_fault_sites;

static unsigned int float_fault_count;

int ico_diag_float_fault(const char *site, const void *caller)
{
    char where[160];
    int i;

    float_fault_count++;
    for (i = 0; i < float_fault_sites; i++) {
        if (float_fault_seen[i].caller == caller &&
            (float_fault_seen[i].site == site || strcmp(float_fault_seen[i].site, site) == 0)) {
            return 0;
        }
    }
    if (float_fault_sites >= FLOAT_FAULT_SITES) {
        return 0;
    }
    float_fault_seen[float_fault_sites].site = site;
    float_fault_seen[float_fault_sites].caller = caller;
    float_fault_sites++;
    describe((uintptr_t)caller, where, sizeof where);
    ico_diag_log("ico_pc: NaN in %s, called from %s (fault %u; first from this caller)", site,
                 where, float_fault_count);
    return 1;
}

unsigned int ico_diag_float_faults(void)
{
    return float_fault_count;
}
