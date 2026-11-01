/*
 * port/platform/backup_host.c
 *
 * Options > Extras > Back up your data and Restore from a backup in the
 * window build (backup_host.h): the rows' host (settings.h UiBackupHost).
 * begin, proceed and cancel come from the menu (the game's threads) and
 * only set the state; pump, on the window's per-vsync step, opens the file
 * dialog, starts the work on a thread of its own and ends the run after a
 * restore; poll reads the state. The dialog's answer comes on whatever
 * thread the system gives (SDL's dialogs: any; Android: the activity's)
 * and touches only atomics and the answer's copy.
 *
 * The desktop asks SDL's save and open dialogs for a path and writes or
 * reads it directly. Android asks the app's own document screens for a
 * content URI: a backup is written into the cache folder, then copied
 * through the URI's descriptor (a provider may hand a pipe); a restore's
 * zip is copied into the cache folder first (iso_import.h ico_iso_copy),
 * since the zip reader seeks.
 */
#include "backup_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <unistd.h>
#endif

#include "../core/region.h"
#include "backup.h"
#include "cdvd_host.h"
#include "diag_host.h"
#include "host_config.h"
#include "host_fs.h"
#include "ico_build_commit.h" /* ICO_BUILD_COMMIT, generated */
#include "ico_version.h"      /* ICO_VERSION_LABEL, generated ("" in a development build) */
#include "mc_host.h"
#include "settings.h"
#include "state_file.h"
#ifdef __ANDROID__
#include "host_android.h"
#include "iso_import.h"
#endif

#ifndef ICO_IOS

/* the restore's popup shown before the run ends */
#define RESTART_DELAY_MS 5000u
/* Android: a document screen that closed without an answer (the activity
   came back to the front and nothing came within the grace), or none in
   the time (gpu_driver_android.c's picker watch, longer here: the save
   screen has a name to type) */
#define PICK_RESUME_GRACE_MS 2000u
#define PICK_TIMEOUT_MS 300000u

/* the UI's states (settings.h UI_BACKUP_*) and three of the host's own:
   a dialog asked for (shown as CHOOSING), a file chosen and a restore
   confirmed (both shown as WORKING), which pump moves on */
enum { ST_REQUESTED = 7, ST_CHOSEN = 8, ST_PROCEED = 9 };

static SDL_AtomicInt s_state, s_gen, s_done, s_total;
static int s_job;                /* UI_BACKUP_JOB_* */
static int s_why;                /* UI_BACKUP_WHY_* once FAILED */
static int s_restarts;           /* a DONE restore: the run starts itself again */
static char s_path[1200];        /* the dialog's answer: a path, or Android's content URI */
static char s_name[96];          /* the file's name as the player reads it */
static char s_zip[1200];         /* the zip a restore reads */
static char s_user[1200];        /* the user folder */
static char s_disc[16];          /* the running disc's slug */
static IcoBackupManifest s_man;  /* a restore's manifest */
static IcoBackupGates s_gates;   /* sampled on the game's side before the work starts */
static Uint64 s_askMs, s_doneMs; /* the dialog's start; a restore's DONE seen */
static int s_ended;              /* the run's end asked for */
#ifdef __ANDROID__
static int s_resumeBase, s_resumed;
static Uint64 s_resumedMs;
#endif

static const char *job_word(void)
{
    return s_job == UI_BACKUP_JOB_SAVE ? "Back up your data" : "Restore from a backup";
}

static IcoBackupGates gates_now(void)
{
    IcoBackupGates g;
    g.card_busy = ico_mc_host_pending();
    g.state_busy = ico_state_file_busy();
    g.disc_busy = !ico_cdvd_host_idle();
    return g;
}

static int why_of(IcoBackupResult r)
{
    switch (r) {
    case ICO_BACKUP_BUSY:
        return UI_BACKUP_WHY_BUSY;
    case ICO_BACKUP_NOTHING:
        return UI_BACKUP_WHY_NOTHING;
    case ICO_BACKUP_READ:
        return UI_BACKUP_WHY_READ;
    case ICO_BACKUP_NOT_BACKUP:
        return UI_BACKUP_WHY_NOT_BACKUP;
    case ICO_BACKUP_NEWER:
        return UI_BACKUP_WHY_NEWER;
    case ICO_BACKUP_DAMAGED:
        return UI_BACKUP_WHY_DAMAGED;
    case ICO_BACKUP_UNSAFE:
        return UI_BACKUP_WHY_UNSAFE;
    case ICO_BACKUP_TOO_BIG:
        return UI_BACKUP_WHY_TOO_BIG;
    default:
        return UI_BACKUP_WHY_WRITE;
    }
}

/* the job ends FAILED with why, its line in the log */
static void fail(int why, const char *what)
{
    s_why = why;
    fprintf(stderr, "backup: refused: %s: %s\n", job_word(), what);
    SDL_SetAtomicInt(&s_state, UI_BACKUP_FAILED);
}

/* a result that is not OK: refused (the data or the moment), or could not
   be written or read (the disk) */
static void fail_result(IcoBackupResult r, const IcoBackupReport *rep)
{
    s_why = why_of(r);
    if (r == ICO_BACKUP_READ || r == ICO_BACKUP_WRITE) {
        fprintf(stderr, "backup: %s: could not %s %s: %s\n", job_word(),
                r == ICO_BACKUP_READ ? "read" : "write", s_name, rep->why);
    } else {
        fprintf(stderr, "backup: refused: %s: %s (%s)\n", job_word(), ico_backup_result_text(r),
                rep->why);
    }
    SDL_SetAtomicInt(&s_state, UI_BACKUP_FAILED);
}

static void progress(void *user, int done, int total)
{
    (void)user;
    SDL_SetAtomicInt(&s_total, total);
    SDL_SetAtomicInt(&s_done, done);
}

#ifndef __ANDROID__
/* path's file name into s_name, cut at its size */
static void name_from_path(const char *path)
{
    const char *b = path + strlen(path);
    while (b > path && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    const size_t n = strlen(b) < sizeof(s_name) ? strlen(b) : sizeof(s_name) - 1;
    memcpy(s_name, b, n);
    s_name[n] = '\0';
}
#endif

#ifdef __ANDROID__

/* <cache folder>/<name>, or -1 */
static int cache_path(char *out, size_t size, const char *name)
{
    char dir[1024];
    return ico_android_cache_dir(dir, sizeof(dir)) == 0 &&
                   snprintf(out, size, "%s/%s", dir, name) < (int)size
               ? 0
               : -1;
}

/* the file at from, start to end, into the descriptor (closed here) */
static int copy_to_fd(const char *from, int fd, char *why, size_t n)
{
    FILE *in = ico_fopen(from, "rb");
    unsigned char *buf = malloc(1u << 20);
    int ok = in != NULL && buf != NULL;
    size_t got;

    while (ok && (got = fread(buf, 1, 1u << 20, in)) > 0) {
        for (size_t at = 0; ok && at < got;) {
            const ssize_t w = write(fd, buf + at, got - at);
            if (w < 0 && errno == EINTR) {
                continue;
            }
            if (w <= 0) {
                snprintf(why, n, "writing the chosen file: %s", strerror(errno));
                ok = 0;
                break;
            }
            at += (size_t)w;
        }
    }
    if (ok && in != NULL && ferror(in)) {
        snprintf(why, n, "reading %s back", from);
        ok = 0;
    }
    if (ok && fsync(fd) != 0 && errno != EINVAL && errno != EROFS) {
        snprintf(why, n, "saving the chosen file: %s", strerror(errno));
        ok = 0; /* a pipe takes no fsync (EINVAL): its far end saves */
    }
    if (close(fd) != 0 && ok) {
        snprintf(why, n, "closing the chosen file: %s", strerror(errno));
        ok = 0;
    }
    if (in != NULL) {
        fclose(in);
    }
    free(buf);
    return ok ? 0 : -1;
}

#endif

/* --- the work, on its own thread ------------------------------------------------- */

static int SDLCALL write_thread(void *u)
{
    const IcoBackupInfo info = {ICO_VERSION_LABEL, ICO_BUILD_COMMIT, ico_state_platform_tag(),
                                s_disc, (long long)time(NULL)};
    IcoBackupReport rep;
    const Uint64 t0 = SDL_GetTicks();
    const char *zip = s_path;
    IcoBackupResult r;
#ifdef __ANDROID__
    char tmp[1200];
    if (cache_path(tmp, sizeof(tmp), "backup-out.zip") != 0) {
        fail(UI_BACKUP_WHY_WRITE, "no cache folder");
        return 0;
    }
    zip = tmp;
#endif
    (void)u;
    r = ico_backup_write(s_user, zip, &info, &s_gates, progress, NULL, &rep);
#ifdef __ANDROID__
    if (r == ICO_BACKUP_OK) {
        int fd = ico_android_open_fd(s_path, "wt");
        if (fd < 0) {
            fd = ico_android_open_fd(s_path, "w");
        }
        if (fd < 0) {
            snprintf(rep.why, sizeof(rep.why), "the chosen file could not be opened for writing");
            r = ICO_BACKUP_WRITE;
        } else if (copy_to_fd(tmp, fd, rep.why, sizeof(rep.why)) != 0) {
            r = ICO_BACKUP_WRITE;
        }
    }
    ico_remove(tmp);
#endif
    if (r != ICO_BACKUP_OK) {
        fail_result(r, &rep);
        return 0;
    }
    fprintf(stderr, "backup: wrote %s (%d files, %llu KB) in %llu ms\n", s_name, rep.files,
            (rep.bytes + 1023) >> 10, (unsigned long long)(SDL_GetTicks() - t0));
    SDL_SetAtomicInt(&s_state, UI_BACKUP_DONE);
    return 0;
}

static int SDLCALL inspect_thread(void *u)
{
    IcoBackupReport rep;
    IcoBackupResult r;
    (void)u;
    memset(&rep, 0, sizeof(rep));
#ifdef __ANDROID__
    {
        char tmp[1200], why[256];
        SDL_IOStream *src = SDL_IOFromFile(s_path, "rb");
        if (cache_path(s_zip, sizeof(s_zip), "backup-in.zip") != 0 ||
            snprintf(tmp, sizeof(tmp), "%s.tmp", s_zip) >= (int)sizeof(tmp)) {
            if (src != NULL) {
                SDL_CloseIO(src);
            }
            fail(UI_BACKUP_WHY_READ, "no cache folder");
            return 0;
        }
        if (src == NULL) {
            snprintf(rep.why, sizeof(rep.why), "the chosen file cannot be opened: %s",
                     SDL_GetError());
            fail_result(ICO_BACKUP_READ, &rep);
            return 0;
        }
        const Sint64 size = SDL_GetIOSize(src);
        char cache[1024];
        const long long freeBytes =
            ico_android_cache_dir(cache, sizeof(cache)) == 0 ? ico_android_free_bytes(cache) : -1;
        if (size > 0 && freeBytes >= 0 && freeBytes < (long long)size + (16ll << 20)) {
            SDL_CloseIO(src);
            fprintf(stderr, "backup: %lld bytes free in the cache for a %lld byte backup\n",
                    freeBytes, (long long)size);
            fail(UI_BACKUP_WHY_NO_SPACE, "too little free space to read the backup");
            return 0;
        }
        const int c = ico_iso_copy(src, tmp, NULL, NULL, why, sizeof(why));
        SDL_CloseIO(src);
        if (c != 0) {
            snprintf(rep.why, sizeof(rep.why), "copying the chosen file: %s", why);
            fail_result(ICO_BACKUP_READ, &rep);
            return 0;
        }
    }
#else
    snprintf(s_zip, sizeof(s_zip), "%s", s_path);
#endif
    r = ico_backup_inspect(s_zip, s_disc, &s_man, &rep);
    if (r != ICO_BACKUP_OK) {
        fail_result(r, &rep);
        return 0;
    }
#ifdef __ANDROID__
    {
        const long long freeBytes = ico_android_free_bytes(s_user);
        if (freeBytes >= 0 && (unsigned long long)freeBytes < s_man.bytes + (32ull << 20)) {
            fprintf(stderr, "backup: %lld bytes free for a backup of %llu bytes\n", freeBytes,
                    s_man.bytes);
            fail(UI_BACKUP_WHY_NO_SPACE, "too little free space to unpack the backup");
            return 0;
        }
    }
#endif
    fprintf(stderr,
            "backup: read %s: version %s (commit %s), %s, disc %s, discs %s, made at %lld, %d "
            "files, %llu KB, holding %s%s\n",
            s_name, s_man.version, s_man.commit, s_man.platform, s_man.disc, s_man.discs,
            s_man.time, s_man.files, (s_man.bytes + 1023) >> 10, s_man.items,
            s_man.other_disc ? "; made while another disc was played" : "");
    SDL_SetAtomicInt(&s_total, s_man.files);
    SDL_SetAtomicInt(&s_state, UI_BACKUP_ASK);
    return 0;
}

static int SDLCALL restore_thread(void *u)
{
    IcoBackupReport rep;
    IcoBackupManifest m;
    (void)u;
    const IcoBackupResult r =
        ico_backup_restore(s_zip, s_name, s_user, s_disc, &s_gates, progress, NULL, &m, &rep);
#ifdef __ANDROID__
    ico_remove(s_zip); /* the cache's copy */
#endif
    if (r != ICO_BACKUP_OK) {
        fail_result(r, &rep);
        return 0;
    }
    s_restarts = ico_diag_can_restart();
    fprintf(stderr, "backup: restored from %s (version %s, discs %s): %d files ready; %s\n", s_name,
            m.version, m.discs, rep.files,
            s_restarts ? "the game restarts to put them in place"
                       : "the game closes, and its next start puts them in place");
    SDL_SetAtomicInt(&s_state, UI_BACKUP_DONE);
    return 0;
}

static void start(SDL_ThreadFunction fn, const char *name)
{
    SDL_SetAtomicInt(&s_done, 0);
    SDL_SetAtomicInt(&s_state, UI_BACKUP_WORKING);
    SDL_Thread *t = SDL_CreateThread(fn, name, NULL);
    if (t == NULL) {
        fprintf(stderr, "backup: SDL_CreateThread: %s\n", SDL_GetError());
        fail(UI_BACKUP_WHY_WRITE, "no thread for the work");
        return;
    }
    SDL_DetachThread(t);
}

/* --- the dialogs ---------------------------------------------------------------- */

/* an answer: file (NULL none), error (the dialog failed), for dialog gen */
static void answer(int gen, const char *file, const char *error)
{
    if (gen != SDL_GetAtomicInt(&s_gen) || SDL_GetAtomicInt(&s_state) != UI_BACKUP_CHOOSING) {
        fprintf(stderr, "backup: a late answer for dialog %d (now %d) ignored\n", gen,
                SDL_GetAtomicInt(&s_gen));
        return;
    }
    if (file != NULL && file[0] != '\0') {
        snprintf(s_path, sizeof(s_path), "%s", file);
        if (SDL_CompareAndSwapAtomicInt(&s_state, UI_BACKUP_CHOOSING, ST_CHOSEN)) {
            fprintf(stderr, "backup: the dialog answered (dialog %d): a file\n", gen);
        }
        return;
    }
    if (error != NULL) {
        fprintf(stderr, "backup: the file dialog failed (dialog %d): %s\n", gen, error);
        s_why = UI_BACKUP_WHY_NO_DIALOG;
        SDL_CompareAndSwapAtomicInt(&s_state, UI_BACKUP_CHOOSING, UI_BACKUP_FAILED);
        return;
    }
    fprintf(stderr,
            "backup: the dialog answered (dialog %d): closed without a file, %llu ms "
            "after it opened\n",
            gen, (unsigned long long)(SDL_GetTicks() - s_askMs));
    SDL_CompareAndSwapAtomicInt(&s_state, UI_BACKUP_CHOOSING, UI_BACKUP_CANCELLED);
}

#ifdef __ANDROID__

static void pick_answer(void *user, int gen, const char *uri, const char *why)
{
    (void)user;
    answer(gen, uri, uri == NULL ? why : NULL);
}

/* ends a document screen that never answered as CANCELLED (see
   PICK_RESUME_GRACE_MS) */
static void pick_watch(void)
{
    const Uint64 now = SDL_GetTicks();
    const char *why = NULL;
    if (ico_android_resume_count() != s_resumeBase) {
        if (!s_resumed) {
            s_resumed = 1;
            s_resumedMs = now;
        }
        if (now - s_resumedMs >= PICK_RESUME_GRACE_MS) {
            why = "the app came back to the front with no answer";
        }
    }
    if (why == NULL && now - s_askMs >= PICK_TIMEOUT_MS) {
        why = "no answer within 300 s";
    }
    if (why != NULL &&
        SDL_CompareAndSwapAtomicInt(&s_state, UI_BACKUP_CHOOSING, UI_BACKUP_CANCELLED)) {
        fprintf(stderr, "backup: the file screen (dialog %d) gave no answer: %s (%llu ms)\n",
                SDL_GetAtomicInt(&s_gen), why, (unsigned long long)(now - s_askMs));
    }
}

#else

static void SDLCALL dialog_answer(void *user, const char *const *files, int filter)
{
    (void)filter;
    if (files == NULL) {
        answer((int)(intptr_t)user, NULL, SDL_GetError());
    } else {
        answer((int)(intptr_t)user, files[0], NULL);
    }
}

static const SDL_DialogFileFilter k_filters[] = {{"Backup (*.zip)", "zip"}};

#endif

/* opens the dialog begin asked for (pump, outside the game's threads) */
static void open_dialog(void)
{
    const int gen = SDL_GetAtomicInt(&s_gen);
    char name[64];

    ico_backup_default_name(name, sizeof(name), (long long)time(NULL));
    s_askMs = SDL_GetTicks();
    SDL_SetAtomicInt(&s_state, UI_BACKUP_CHOOSING);
    fprintf(stderr, "backup: %s: opening the %s (dialog %d)\n", job_word(),
            s_job == UI_BACKUP_JOB_SAVE ? "save dialog" : "file dialog", gen);
#ifdef __ANDROID__
    {
        char why[512];
        int noApp = 0, rc;
        s_resumed = 0;
        s_resumeBase = ico_android_resume_count();
        rc = s_job == UI_BACKUP_JOB_SAVE
                 ? ico_android_create_file(gen, name, "application/zip", pick_answer, NULL, &noApp,
                                           why, sizeof(why))
                 : ico_android_pick_file(gen, pick_answer, NULL, &noApp, why, sizeof(why));
        if (rc != 0) {
            answer(gen, NULL, why);
        }
    }
#else
    {
        int count = 0;
        SDL_Window **wins = SDL_GetWindows(&count);
        SDL_Window *win = wins != NULL && count > 0 ? wins[0] : NULL;
        const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
        /* kept until the answer, as the filters are */
        static char where[1200];
        SDL_free(wins);
        if (docs != NULL && s_job == UI_BACKUP_JOB_SAVE) {
            snprintf(where, sizeof(where), "%s%s", docs, name);
        } else {
            snprintf(where, sizeof(where), "%s", docs != NULL ? docs : "");
        }
        if (s_job == UI_BACKUP_JOB_SAVE) {
            SDL_ShowSaveFileDialog(dialog_answer, (void *)(intptr_t)gen, win, k_filters, 1,
                                   where[0] != '\0' ? where : NULL);
        } else {
            SDL_ShowOpenFileDialog(dialog_answer, (void *)(intptr_t)gen, win, k_filters, 1,
                                   where[0] != '\0' ? where : NULL, false);
        }
    }
#endif
}

/* --- the host --------------------------------------------------------------------- */

static int under_way(int st)
{
    return st == UI_BACKUP_CHOOSING || st == UI_BACKUP_WORKING || st == UI_BACKUP_ASK ||
           st == ST_REQUESTED || st == ST_CHOSEN || st == ST_PROCEED;
}

static int host_begin(int job)
{
    const int st = SDL_GetAtomicInt(&s_state);
    if (under_way(st) || (st == UI_BACKUP_DONE && s_job == UI_BACKUP_JOB_RESTORE)) {
        return -1;
    }
    s_job = job == UI_BACKUP_JOB_RESTORE ? UI_BACKUP_JOB_RESTORE : UI_BACKUP_JOB_SAVE;
    s_why = UI_BACKUP_WHY_NONE;
    s_name[0] = '\0';
    SDL_SetAtomicInt(&s_done, 0);
    SDL_SetAtomicInt(&s_total, 0);
    SDL_AddAtomicInt(&s_gen, 1);
    snprintf(s_disc, sizeof(s_disc), "%s", ico_region_cur()->slug);
    s_gates = gates_now();
    if (s_gates.card_busy || s_gates.state_busy) {
        fail(UI_BACKUP_WHY_BUSY, s_gates.card_busy ? "the memory card is being written"
                                                   : "a save state is being written");
        return 0;
    }
    SDL_SetAtomicInt(&s_state, ST_REQUESTED);
    return 0;
}

static int host_poll(UiBackupStatus *out)
{
    int st = SDL_GetAtomicInt(&s_state);
    memset(out, 0, sizeof(*out));
    if (st == ST_REQUESTED) {
        st = UI_BACKUP_CHOOSING;
    } else if (st == ST_CHOSEN || st == ST_PROCEED) {
        st = UI_BACKUP_WORKING;
    }
    out->job = s_job;
    out->state = st;
    out->done = SDL_GetAtomicInt(&s_done);
    out->total = SDL_GetAtomicInt(&s_total);
    out->why = st == UI_BACKUP_FAILED ? s_why : UI_BACKUP_WHY_NONE;
    out->restarts = s_restarts;
    snprintf(out->name, sizeof(out->name), "%s", s_name);
    if (st == UI_BACKUP_ASK && s_job == UI_BACKUP_JOB_RESTORE) {
        const time_t t = (time_t)s_man.time;
        struct tm lt;
        out->otherDisc = s_man.other_disc;
        snprintf(out->version, sizeof(out->version), "%s", s_man.version);
#ifdef _WIN32
        if (localtime_s(&lt, &t) == 0) {
#else
        if (localtime_r(&t, &lt) != NULL) {
#endif
            strftime(out->date, sizeof(out->date), "%Y-%m-%d %H:%M", &lt);
        }
        /* the slugs as the player reads them: the discs' serials */
        for (const char *p = s_man.discs; *p != '\0';) {
            const char *sp = strchr(p, ' ');
            const size_t k = sp != NULL ? (size_t)(sp - p) : strlen(p);
            char slug[16];
            const IcoRegionInfo *ri;
            snprintf(slug, sizeof(slug), "%.*s", (int)(k < sizeof(slug) ? k : sizeof(slug) - 1), p);
            ri = ico_region_by_slug(slug);
            if (ri != NULL) {
                const size_t have = strlen(out->discs);
                snprintf(out->discs + have, sizeof(out->discs) - have, "%s%s", have > 0 ? ", " : "",
                         ri->disc_id);
            }
            p = sp != NULL ? sp + 1 : p + k;
        }
    }
    return st;
}

static void host_proceed(void)
{
    if (SDL_GetAtomicInt(&s_state) != UI_BACKUP_ASK) {
        return;
    }
    s_gates = gates_now();
    if (s_gates.card_busy || s_gates.state_busy || s_gates.disc_busy) {
        /* the work refuses with these (backup.h): the zip is read again
           then, so asking again works once the game is idle */
        fprintf(stderr, "backup: %s: the game is busy (card %d, save state %d, disc %d)\n",
                job_word(), s_gates.card_busy, s_gates.state_busy, s_gates.disc_busy);
    }
    SDL_SetAtomicInt(&s_state, ST_PROCEED);
}

static void host_cancel(void)
{
    if (!SDL_CompareAndSwapAtomicInt(&s_state, UI_BACKUP_ASK, UI_BACKUP_CANCELLED)) {
        return;
    }
    fprintf(stderr, "backup: the restore of %s was not confirmed; nothing changed\n", s_name);
#ifdef __ANDROID__
    ico_remove(s_zip);
#endif
}

static void host_pump(void)
{
    const int st = SDL_GetAtomicInt(&s_state);
    if (st == ST_REQUESTED) {
        open_dialog();
        return;
    }
#ifdef __ANDROID__
    if (st == UI_BACKUP_CHOOSING) {
        pick_watch();
        return;
    }
#endif
    if (st == ST_CHOSEN) {
#ifdef __ANDROID__
        if (ico_android_display_name(s_path, s_name, sizeof(s_name)) != 0) {
            snprintf(s_name, sizeof(s_name), "the chosen file");
        }
#else
        name_from_path(s_path);
        fprintf(stderr, "backup: %s: %s\n", job_word(), s_path);
#endif
        s_gates = gates_now();
        start(s_job == UI_BACKUP_JOB_SAVE ? write_thread : inspect_thread, "ico-backup");
        return;
    }
    if (st == ST_PROCEED) {
        start(restore_thread, "ico-backup");
        return;
    }
    if (st == UI_BACKUP_DONE && s_job == UI_BACKUP_JOB_RESTORE && !s_ended) {
        const Uint64 now = SDL_GetTicks();
        if (s_doneMs == 0) {
            s_doneMs = now;
        }
        if (now - s_doneMs >= RESTART_DELAY_MS) {
            SDL_Event e;
            s_ended = 1;
            fprintf(stderr,
                    "backup: ending the run so that the next start puts the backup in "
                    "place%s\n",
                    ico_diag_restart_at_exit() ? "; the game starts again by itself" : "");
            SDL_zero(e);
            e.type = SDL_EVENT_QUIT;
            if (!SDL_PushEvent(&e)) {
                fprintf(stderr, "backup: could not post the quit event: %s\n", SDL_GetError());
            }
        }
    }
}

void ico_backup_host_install(void)
{
    static const UiBackupHost host = {host_begin, host_poll, host_proceed, host_cancel, host_pump};
    if (ico_host_pref_dir(s_user, sizeof(s_user)) != 0 || s_user[0] == '\0') {
        fprintf(stderr, "backup: no user folder; Options > Extras has no backup rows\n");
        return;
    }
    {
        /* a card folder of the player's own is not in a backup (backup.h:
           the user folder's memcard folders only) */
        char card[1200];
        if (ico_host_saves_dir(card, sizeof(card)) == 0 &&
            strncmp(card, s_user, strlen(s_user)) != 0) {
            fprintf(stderr,
                    "backup: the memory card folder %s is outside the user folder %s: a backup "
                    "does not hold it\n",
                    card, s_user);
        }
    }
    ui_settings_set_backup_host(&host);
}

#else

void ico_backup_host_install(void) {}

#endif
