/*
 * port/data/test/cdvd_host_test.c
 *
 * The disc reader of the libcdvd host layer (port/data/cdvd_host.c, issue
 * 126) over a fake VFS backend: a synthetic volume of FK_SECTORS sectors
 * (each byte a function of its sector and offset) whose read_sectors
 * sleeps a set time and counts the calls made on this thread and on any
 * other.  The vsync is the host loop's callback list, stood in for here as
 * vfs_test.c does.
 *   - ordering: with the thread, sceCdRead returns well before a 30 ms read
 *     ends; the command is busy until the vsync and never completes before
 *     it; a second read while busy is refused; at the vsync the buffer
 *     equals the sectors and the read was made on the other thread; a read
 *     finished before the vsync costs it no wait, one not finished is
 *     waited for there (IcoCdvdStats.wait_ns);
 *   - sceCdSync(0) from the host context completes at once, the buffer
 *     filled;
 *   - the errors: past the volume's end ICO_CD_ERR_END, a failing read
 *     ICO_CD_ERR_READ, final when busy clears;
 *   - sceCdBreak during a read: busy 0, ICO_CD_ERR_ABORT, the buffer
 *     written whole, and nothing written into it after the break;
 *   - the read-ahead: the sectors after a read copied from it (hits, no
 *     backend call), a read elsewhere read from the disc (a miss), the
 *     fill clamped at the volume's end (a hit up to it, END past it);
 *   - the stream: sceCdStRead's second read from the read-ahead;
 *   - the idle gate: not idle while a read is on the thread;
 *   - ico_cdvd_host_reset with a read on the thread: the read finished
 *     first, the thread gone;
 *   - the sync reader: the read on this thread, the buffer filled when
 *     sceCdRead returns, busy until the vsync, the same errors.
 * Run under the TSAN preset (build-host/linux-x64-tsan) where it is built.
 */
#include "cdvd_host.h"
#include "ico_thread.h"
#include "vfs.h"

#include <libcdvd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#else
#include <time.h>
#endif

#include "../../test/ico_check.h"

#define FK_SECTORS 400u
#define SEC 2048u

/* The host loop's vsync callback list (port/platform/host_loop.c) is not
   linked into this test: this stand-in records what sceCdInit registers,
   and vsync() runs it as one simulated vsync would. */
static void (*vsyncFn)(void *);
static void *vsyncUser;

int ico_host_on_vsync_register(void (*fn)(void *user), void *user)
{
    vsyncFn = fn;
    vsyncUser = user;
    return 0;
}

void ico_host_on_vsync_unregister(void (*fn)(void *user), void *user)
{
    if (vsyncFn == fn && vsyncUser == user) {
        vsyncFn = NULL;
        vsyncUser = NULL;
    }
}

static void vsync(void)
{
    if (vsyncFn != NULL) {
        vsyncFn(vsyncUser);
    }
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};

    nanosleep(&ts, NULL);
#endif
}

/* --- the fake backend ---------------------------------------------------- */

static _Thread_local int t_main; /* set on the test's own thread */

static struct {
    int sleepMs;      /* each read_sectors call sleeps this long first */
    uint32_t failLsn; /* a read covering this sector fails (0: none) */
    int calls, onMain, onOther;
} fk;

static unsigned char synth(uint32_t lsn, uint32_t off)
{
    return (unsigned char)(lsn * 7u + off * 13u + (off >> 8));
}

static int fk_mount(void **state, const char *location)
{
    (void)location;
    *state = &fk;
    return 0;
}

static void fk_unmount(void *state)
{
    (void)state;
}

static int fk_lookup(void *state, const char *path, IcoVfsEntry *out)
{
    (void)state;
    (void)path;
    (void)out;
    return -1; /* no files: the reader reads sectors only */
}

static int fk_read(void *state, uint32_t lsn, uint32_t count, void *dst)
{
    unsigned char *d = dst;
    const int ms = __atomic_load_n(&fk.sleepMs, __ATOMIC_RELAXED);
    const uint32_t fail = __atomic_load_n(&fk.failLsn, __ATOMIC_RELAXED);

    (void)state;
    __atomic_add_fetch(&fk.calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(t_main ? &fk.onMain : &fk.onOther, 1, __ATOMIC_RELAXED);
    if (lsn >= FK_SECTORS || count > FK_SECTORS - lsn) {
        return -1;
    }
    if (ms > 0) {
        sleep_ms(ms);
    }
    if (fail != 0 && fail >= lsn && fail - lsn < count) {
        return -1;
    }
    for (uint32_t s = 0; s < count; s++) {
        for (uint32_t o = 0; o < SEC; o++) {
            d[(size_t)s * SEC + o] = synth(lsn + s, o);
        }
    }
    return 0;
}

static uint32_t fk_volume(void *state)
{
    (void)state;
    return FK_SECTORS;
}

static const IcoVfsBackend fake = {
    "fake", fk_mount, fk_unmount, fk_lookup, fk_read, fk_volume,
};

static IcoVfs *fkVfs;

static int calls(void)
{
    return __atomic_load_n(&fk.calls, __ATOMIC_RELAXED);
}

static int on_other(void)
{
    return __atomic_load_n(&fk.onOther, __ATOMIC_RELAXED);
}

/* the buffer holds sectors [lsn, lsn + n) */
static int holds(const unsigned char *buf, uint32_t lsn, uint32_t n)
{
    for (uint32_t s = 0; s < n; s++) {
        for (uint32_t o = 0; o < SEC; o++) {
            if (buf[(size_t)s * SEC + o] != synth(lsn + s, o)) {
                return 0;
            }
        }
    }
    return 1;
}

/* a fresh drive with the fake disc and the reader asked for */
static void setup(int thread, int aheadKb)
{
    ico_cdvd_host_reset();
    memset(&fk, 0, sizeof(fk));
    ico_vfs_set_disc(fkVfs);
    CHECK(sceCdInit(0) == 1);
    ico_cdvd_host_set_reader(thread, aheadKb);
    CHECK(ico_cdvd_host_reader_thread() == thread);
}

static IcoCdvdStats stats(void)
{
    IcoCdvdStats s;

    ico_cdvd_host_stats(&s);
    return s;
}

static unsigned char buf[64 * SEC];

static void test_ordering(void)
{
    IcoCdvdStats s0, s1;
    double t;

    setup(1, 0);
    fk.sleepMs = 30;
    t = ico_thread_now_ms();
    CHECK(sceCdRead(20, 16, buf, NULL) == 1);
    t = ico_thread_now_ms() - t;
    CHECK(t < 15.0, "sceCdRead took %.1f ms with a 30 ms read on the thread", t);
    CHECK(sceCdSync(1) == 1 && ico_cdvd_host_busy());
    CHECK(sceCdRead(40, 1, buf + 32 * SEC, NULL) == 0); /* busy: refused */
    sleep_ms(60);                                       /* the read ends meanwhile */
    CHECK(sceCdSync(1) == 1);                           /* still busy: no vsync yet */
    s0 = stats();
    vsync();
    s1 = stats();
    CHECK(!ico_cdvd_host_busy() && sceCdSync(1) == 0);
    CHECK(sceCdGetError() == ICO_CD_ERR_NONE);
    CHECK(holds(buf, 20, 16));
    CHECK(calls() == 1 && on_other() == 1 && fk.onMain == 0);
    CHECK(s1.waits == s0.waits && s1.wait_ns == s0.wait_ns); /* finished: no wait */
    CHECK(s1.reads == s0.reads && s1.sectors == s0.sectors);
    /* the vsync right after: waited for there */
    s0 = stats();
    CHECK(sceCdRead(50, 16, buf, NULL) == 1);
    vsync();
    s1 = stats();
    CHECK(holds(buf, 50, 16) && sceCdGetError() == ICO_CD_ERR_NONE);
    CHECK(s1.waits == s0.waits + 1);
    CHECK(s1.wait_ns - s0.wait_ns >= 15000000ull, "waited %.1f ms",
          (double)(s1.wait_ns - s0.wait_ns) / 1e6);
    CHECK(s1.reads == s0.reads + 1 && s1.sectors == s0.sectors + 16);
    CHECK(s1.bytes == s0.bytes + 16ull * SEC);
}

static void test_host_sync(void)
{
    setup(1, 0);
    fk.sleepMs = 20;
    memset(buf, 0, 16 * SEC);
    CHECK(sceCdRead(60, 16, buf, NULL) == 1);
    CHECK(sceCdSync(0) == 0);
    CHECK(!ico_cdvd_host_busy() && sceCdGetError() == ICO_CD_ERR_NONE);
    CHECK(holds(buf, 60, 16));
}

static void test_errors(int thread)
{
    setup(thread, 0);
    CHECK(sceCdRead((int)FK_SECTORS - 4, 16, buf, NULL) == 1);
    vsync();
    CHECK(sceCdGetError() == ICO_CD_ERR_END);
    fk.failLsn = 77;
    CHECK(sceCdRead(70, 16, buf, NULL) == 1);
    vsync();
    CHECK(sceCdGetError() == ICO_CD_ERR_READ);
    fk.failLsn = 0;
    CHECK(sceCdRead(70, 16, buf, NULL) == 1);
    vsync();
    CHECK(sceCdGetError() == ICO_CD_ERR_NONE && holds(buf, 70, 16));
}

static void test_break(void)
{
    int n;

    setup(1, 0);
    fk.sleepMs = 30;
    memset(buf, 0, 16 * SEC);
    CHECK(sceCdRead(80, 16, buf, NULL) == 1);
    CHECK(sceCdBreak() == 1);
    CHECK(!ico_cdvd_host_busy() && sceCdGetError() == ICO_CD_ERR_ABORT);
    CHECK(holds(buf, 80, 16)); /* the read finished before the break returned */
    n = calls();
    memset(buf, 0xEE, 16 * SEC);
    sleep_ms(50);
    vsync();
    CHECK(calls() == n && buf[0] == 0xEE && buf[16 * SEC - 1] == 0xEE);
    CHECK(sceCdGetError() == ICO_CD_ERR_ABORT); /* the vsync after changes nothing */
}

static void test_read_ahead(void)
{
    IcoCdvdStats s0, s1;
    int n;

    setup(1, 128); /* 64 sectors */
    CHECK(sceCdRead(100, 16, buf, NULL) == 1);
    vsync();
    CHECK(holds(buf, 100, 16));
    sleep_ms(50); /* the fill from 116 */
    s0 = stats();
    n = calls();
    for (uint32_t lsn = 116; lsn < 180; lsn += 16) {
        memset(buf, 0, 16 * SEC);
        CHECK(sceCdRead((int)lsn, 16, buf, NULL) == 1);
        CHECK(holds(buf, lsn, 16)); /* copied at once */
        vsync();
        CHECK(sceCdGetError() == ICO_CD_ERR_NONE && holds(buf, lsn, 16));
    }
    s1 = stats();
    CHECK(s1.hits == s0.hits + 4, "%u hits", s1.hits - s0.hits);
    /* the last hit asks for the fill from 180: the hits themselves read nothing */
    CHECK(calls() <= n + (64 + 31) / 32, "%d backend calls", calls() - n);
    /* a read elsewhere: from the disc */
    sleep_ms(50);
    s0 = stats();
    CHECK(sceCdRead(10, 16, buf, NULL) == 1);
    vsync();
    s1 = stats();
    CHECK(s1.hits == s0.hits && holds(buf, 10, 16) && sceCdGetError() == ICO_CD_ERR_NONE);
    /* the fill clamped at the volume's end: the last 4 sectors only */
    CHECK(sceCdRead((int)FK_SECTORS - 20, 16, buf, NULL) == 1);
    vsync();
    sleep_ms(50);
    s0 = stats();
    CHECK(sceCdRead((int)FK_SECTORS - 4, 4, buf, NULL) == 1);
    vsync();
    s1 = stats();
    CHECK(s1.hits == s0.hits + 1 && holds(buf, FK_SECTORS - 4, 4));
    CHECK(sceCdGetError() == ICO_CD_ERR_NONE);
    CHECK(sceCdRead((int)FK_SECTORS - 4, 8, buf, NULL) == 1);
    vsync();
    CHECK(sceCdGetError() == ICO_CD_ERR_END);
}

static void test_stream(void)
{
    IcoCdvdStats s0, s1;
    int err = -1;

    setup(1, 128);
    CHECK(sceCdStInit(64, 4, NULL) == 1);
    CHECK(sceCdStStart(200, NULL) == 1);
    CHECK(sceCdStRead(16, buf, 1, &err) == 16 && err == ICO_CD_ERR_NONE);
    CHECK(holds(buf, 200, 16));
    sleep_ms(50);
    s0 = stats();
    CHECK(sceCdStRead(16, buf, 1, &err) == 16 && err == ICO_CD_ERR_NONE);
    s1 = stats();
    CHECK(holds(buf, 216, 16) && s1.hits == s0.hits + 1);
    CHECK(!ico_cdvd_host_idle()); /* a stream is started */
    CHECK(sceCdStStop() == 1);
}

static void test_idle(void)
{
    setup(1, 0);
    fk.sleepMs = 30;
    CHECK(ico_cdvd_host_idle());
    CHECK(sceCdRead(120, 16, buf, NULL) == 1);
    CHECK(!ico_cdvd_host_idle());
    vsync();
    CHECK(ico_cdvd_host_idle());
}

static void test_reset_in_flight(void)
{
    setup(1, 128);
    fk.sleepMs = 30;
    memset(buf, 0, 16 * SEC);
    CHECK(sceCdRead(140, 16, buf, NULL) == 1);
    ico_cdvd_host_reset();
    CHECK(holds(buf, 140, 16));
    CHECK(ico_cdvd_host_reader_thread() == 0 && !ico_cdvd_host_busy());
}

static void test_sync_reader(void)
{
    IcoCdvdStats s0, s1;

    setup(0, -1);
    fk.sleepMs = 5;
    memset(buf, 0, 16 * SEC);
    s0 = stats();
    CHECK(sceCdRead(160, 16, buf, NULL) == 1);
    CHECK(holds(buf, 160, 16)); /* filled when sceCdRead returns */
    CHECK(fk.onMain == 1 && on_other() == 0);
    CHECK(ico_cdvd_host_busy() && !ico_cdvd_host_idle());
    vsync();
    s1 = stats();
    CHECK(!ico_cdvd_host_busy() && sceCdGetError() == ICO_CD_ERR_NONE);
    CHECK(s1.waits == s0.waits + 1 && s1.hits == s0.hits); /* no read-ahead */
}

int main(void)
{
    t_main = 1;
    fkVfs = ico_vfs_mount(&fake, "fake disc");
    CHECK(fkVfs != NULL);
    if (fkVfs == NULL) {
        return ico_check_summary("cdvd_host_test");
    }
    test_ordering();
    test_host_sync();
    test_errors(1);
    test_errors(0);
    test_break();
    test_read_ahead();
    test_stream();
    test_idle();
    test_reset_in_flight();
    test_sync_reader();
    ico_cdvd_host_reset();
    ico_vfs_set_disc(NULL);
    ico_vfs_unmount(fkVfs);
    return ico_check_summary("cdvd_host_test");
}
