/*
 * port/platform/android/host_android.c
 *
 * The Android side of the host layer (host_android.h).
 */
#ifdef __ANDROID__

#define _GNU_SOURCE 1 /* pipe2 */

#include "host_android.h"
#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <SDL3/SDL.h>

#define LOG_TAG "ico-pc"

/* --- folders -------------------------------------------------------------- */

static IcoAndroidPaths s_paths;
static int s_paths_state; /* 0 not tried, 1 ready, -1 unavailable */

static void copy_out(char *out, size_t size, const char *s)
{
    size_t n = strlen(s);

    if (size == 0) {
        return;
    }
    if (n >= size) {
        n = size - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

/* mkdir -p */
static int make_dirs(const char *path)
{
    char buf[ICO_ANDROID_PATH_MAX];
    size_t i, n = strlen(path);

    if (n == 0 || n >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];

            buf[i] = '\0';
            if (mkdir(buf, 0770) != 0 && errno != EEXIST) {
                return -1;
            }
            buf[i] = c;
        }
    }
    return 0;
}

const IcoAndroidPaths *ico_android_paths(void)
{
    const char *files = NULL;
    const char *cache;

    if (s_paths_state != 0) {
        return s_paths_state > 0 ? &s_paths : NULL;
    }
    s_paths_state = -1;
    if ((SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) != 0) {
        files = SDL_GetAndroidExternalStoragePath();
        if (files != NULL && make_dirs(files) != 0) {
            files = NULL;
        }
    }
    if (files == NULL) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "the shared storage is not writable; using the app's internal folder");
        files = SDL_GetAndroidInternalStoragePath();
        if (files != NULL && make_dirs(files) != 0) {
            files = NULL;
        }
    }
    cache = SDL_GetAndroidCachePath();
    if (files == NULL || cache == NULL || ico_android_layout(files, cache, &s_paths) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "no usable files folder (%s, %s): %s",
                            files ? files : "none", cache ? cache : "none", SDL_GetError());
        return NULL;
    }
    /* the texture pack's folder, so the player finds where a pack goes
       (the memory cards' and the log's are made when first written) */
    if (make_dirs(s_paths.textures) != 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "cannot create %s", s_paths.textures);
    }
    s_paths_state = 1;
    return &s_paths;
}

int ico_android_files_dir(char *out, size_t size)
{
    const IcoAndroidPaths *p = ico_android_paths();

    copy_out(out, size, p != NULL ? p->files : "");
    return p != NULL ? 0 : -1;
}

int ico_android_cache_dir(char *out, size_t size)
{
    const IcoAndroidPaths *p = ico_android_paths();

    copy_out(out, size, p != NULL ? p->cache : "");
    if (p != NULL) {
        make_dirs(p->cache);
    }
    return p != NULL ? 0 : -1;
}

long long ico_android_free_bytes(const char *dir)
{
    struct statvfs s;

    if (dir == NULL || statvfs(dir, &s) != 0) {
        return -1;
    }
    return (long long)s.f_bavail * (long long)s.f_frsize;
}

/* --- the log mirror ----------------------------------------------------- */

static int s_pipe_r = -1;
static int s_log_fd = -1;
static SDL_Mutex *s_mirror_lock;
static char s_line[1024];
static size_t s_line_len;

static void logcat_line(void)
{
    s_line[s_line_len] = '\0';
    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, s_line);
    s_line_len = 0;
}

/* one chunk read from the pipe: the file whole, logcat a line at a time */
static void mirror_chunk(const char *b, size_t n)
{
    size_t i, off = 0;

    while (off < n) {
        ssize_t w = write(s_log_fd, b + off, n - off);

        if (w <= 0) {
            break;
        }
        off += (size_t)w;
    }
    for (i = 0; i < n; i++) {
        if (b[i] == '\n') {
            logcat_line();
        } else if (b[i] != '\r') {
            s_line[s_line_len++] = b[i];
            if (s_line_len == sizeof(s_line) - 1) {
                logcat_line();
            }
        }
    }
}

/* reads what the pipe holds now (non-blocking), under the lock; 0 at its
   end (every writer closed) */
static int mirror_drain(void)
{
    char buf[4096];

    for (;;) {
        ssize_t n = read(s_pipe_r, buf, sizeof(buf));

        if (n > 0) {
            mirror_chunk(buf, (size_t)n);
            continue;
        }
        if (n == 0) {
            return 0;
        }
        return 1; /* EAGAIN (empty for now) or EINTR */
    }
}

static int SDLCALL mirror_main(void *user)
{
    struct pollfd pf;

    (void)user;
    pf.fd = s_pipe_r;
    pf.events = POLLIN;
    for (;;) {
        int more;

        pf.revents = 0;
        if (poll(&pf, 1, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        SDL_LockMutex(s_mirror_lock);
        more = mirror_drain();
        SDL_UnlockMutex(s_mirror_lock);
        if (!more || (pf.revents & (POLLERR | POLLNVAL)) != 0) {
            break;
        }
    }
    return 0;
}

int ico_android_log_mirror_start(const char *log_path)
{
    int fds[2];
    SDL_Thread *t;

    if (s_pipe_r >= 0) {
        return 0;
    }
    s_log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    if (s_log_fd < 0) {
        return -1;
    }
    s_mirror_lock = SDL_CreateMutex();
    if (s_mirror_lock == NULL || pipe2(fds, O_CLOEXEC) != 0) {
        goto fail;
    }
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    s_pipe_r = fds[0];
    fflush(stdout);
    fflush(stderr);
    if (dup2(fds[1], 1) < 0 || dup2(fds[1], 2) < 0) {
        close(fds[0]);
        close(fds[1]);
        s_pipe_r = -1;
        goto fail;
    }
    close(fds[1]);
    /* each call one write, as on Linux, so lines arrive whole and in order */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    t = SDL_CreateThread(mirror_main, "ico-log", NULL);
    if (t == NULL) {
        /* the pipe would fill and block the game: give the streams the
           file itself */
        dup2(s_log_fd, 1);
        dup2(s_log_fd, 2);
        close(s_pipe_r);
        s_pipe_r = -1;
        return 0;
    }
    SDL_DetachThread(t);
    return 0;
fail:
    if (s_mirror_lock != NULL) {
        SDL_DestroyMutex(s_mirror_lock);
        s_mirror_lock = NULL;
    }
    close(s_log_fd);
    s_log_fd = -1;
    return -1;
}

void ico_android_log_mirror_flush(void)
{
    if (s_pipe_r < 0 || s_mirror_lock == NULL) {
        return;
    }
    fflush(stdout);
    fflush(stderr);
    /* what the reader already took is written before it lets go of the
       lock; what is still in the pipe is drained here */
    SDL_LockMutex(s_mirror_lock);
    mirror_drain();
    if (s_line_len > 0) {
        logcat_line();
    }
    SDL_UnlockMutex(s_mirror_lock);
}

void ico_android_log_mirror_try_flush(void)
{
    int i;

    if (s_pipe_r < 0 || s_mirror_lock == NULL) {
        return;
    }
    for (i = 0; i < 50; i++) {
        if (SDL_TryLockMutex(s_mirror_lock)) {
            mirror_drain();
            if (s_line_len > 0) {
                logcat_line();
            }
            SDL_UnlockMutex(s_mirror_lock);
            return;
        }
        SDL_Delay(10);
    }
}

/* --- the message box and the version ------------------------------------ */

void ico_android_fatal_box(const char *text)
{
    ico_android_message_box(text, 1);
}

void ico_android_message_box(const char *text, int error)
{
    __android_log_write(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, LOG_TAG, text);
    if (!SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, "ICO",
                                  text, NULL)) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "message box: %s", SDL_GetError());
    }
}

void ico_android_log_version(void)
{
    SDL_IOStream *io = SDL_IOFromFile("VERSION.txt", "rb");
    char buf[512];
    char *line, *next;
    size_t n;

    if (io == NULL) {
        fprintf(stderr, "ico_pc: no VERSION.txt in the app: %s\n", SDL_GetError());
        return;
    }
    n = SDL_ReadIO(io, buf, sizeof(buf) - 1);
    SDL_CloseIO(io);
    buf[n] = '\0';
    /* "ico-pc <label>", "built <date>", "commit <hash>" */
    for (line = buf; line != NULL && *line != '\0'; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        line[strcspn(line, "\r")] = '\0';
        if (line[0] != '\0') {
            fprintf(stderr, "ico_pc: VERSION.txt: %s\n", line);
        }
    }
}

#endif
