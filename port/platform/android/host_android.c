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
#include <jni.h>

#define LOG_TAG ICO_ANDROID_LOG_TAG

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

/* what is still in the pipe, then a line not yet ended; the caller holds
   s_mirror_lock */
static void mirror_flush_locked(void)
{
    mirror_drain();
    if (s_line_len > 0) {
        logcat_line();
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
    mirror_flush_locked();
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
            mirror_flush_locked();
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
    if (!SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
                                  "ICO PC", text, NULL)) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "message box: %s", SDL_GetError());
    }
}

int ico_android_message_box_buttons(void *window, const char *text, const char *const *labels,
                                    int count, int enter, int quit)
{
    SDL_MessageBoxButtonData buttons[4];
    SDL_MessageBoxData box;
    int chosen = quit;

    __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, text);
    if (count < 1 || count > (int)SDL_arraysize(buttons) || quit < 0 || quit >= count ||
        enter < 0 || enter >= count) {
        return quit;
    }
    for (int i = 0; i < count; i++) {
        buttons[i].flags = (i == enter ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) |
                           (i == quit ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
        buttons[i].buttonID = i;
        buttons[i].text = labels[i];
    }
    SDL_zero(box);
    box.flags = SDL_MESSAGEBOX_ERROR;
    box.window = (SDL_Window *)window;
    box.title = "ICO PC";
    box.message = text;
    box.numbuttons = count;
    box.buttons = buttons;
    if (!SDL_ShowMessageBox(&box, &chosen)) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "message box: %s", SDL_GetError());
        return quit;
    }
    return chosen >= 0 && chosen < count ? chosen : quit;
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

/* --- the phone's vibrator ------------------------------------------------- */

/* Calls the static Java method IcoActivity.vibrate(int ms, int amplitude).
   The JNI environment SDL_GetAndroidJNIEnv returns belongs to the calling
   thread, so this runs only on SDL's main thread (the host loop's, where
   the input layer's rumble is sent). The class comes from the activity
   object, the method id is looked up once (a failed lookup is remembered
   and the call then does nothing), and every local reference is deleted. */
void ico_host_vibrate(int amplitude, int ms)
{
    static jmethodID s_mid;
    static int s_failed;
    JNIEnv *env;
    jobject act;
    jclass cls;

    if (s_failed) {
        return;
    }
    env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    act = env != NULL ? (jobject)SDL_GetAndroidActivity() : NULL;
    if (env == NULL || act == NULL) {
        return;
    }
    cls = (*env)->GetObjectClass(env, act);
    if (cls != NULL) {
        if (s_mid == NULL) {
            s_mid = (*env)->GetStaticMethodID(env, cls, "vibrate", "(II)V");
            if (s_mid == NULL) {
                s_failed = 1;
            }
        }
        if (s_mid != NULL) {
            (*env)->CallStaticVoidMethod(env, cls, s_mid, (jint)ms, (jint)amplitude);
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, cls);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, act);
}

/* --- the screen's density and fold ---------------------------------------- */

/* Written by the activity's thread (the Java UI thread), read by SDL's main
   thread: the copy is guarded by the spin lock and the count goes up after
   it, so a reader that sees a new count finds the new values. */
static IcoAndroidScreen s_screen;
static SDL_SpinLock s_screenLock;
static SDL_AtomicInt s_screenGen;

int ico_android_screen(IcoAndroidScreen *out)
{
    int gen;

    if (out == NULL) {
        return SDL_GetAtomicInt(&s_screenGen);
    }
    SDL_LockSpinlock(&s_screenLock);
    gen = SDL_GetAtomicInt(&s_screenGen);
    if (gen > 0) {
        *out = s_screen;
    }
    SDL_UnlockSpinlock(&s_screenLock);
    return gen;
}

/* IcoActivity.nativeScreen (a static native method): the activity's report.
   It may come before SDL's main thread starts, or while it is paused; the
   values only wait here for the next layout. */
JNIEXPORT void JNICALL Java_com_defnf_icopc_IcoActivity_nativeScreen(JNIEnv *env, jclass cls,
                                                                     jint dpi, jint fold, jint half,
                                                                     jint sep, jint l, jint t,
                                                                     jint r, jint b, jint winW,
                                                                     jint winH)
{
    (void)env;
    (void)cls;
    SDL_LockSpinlock(&s_screenLock);
    s_screen.densityDpi = (int)dpi;
    s_screen.fold = (int)fold;
    s_screen.halfOpened = half != 0;
    s_screen.separating = sep != 0;
    s_screen.l = (int)l;
    s_screen.t = (int)t;
    s_screen.r = (int)r;
    s_screen.b = (int)b;
    s_screen.winW = (int)winW;
    s_screen.winH = (int)winH;
    SDL_AddAtomicInt(&s_screenGen, 1);
    SDL_UnlockSpinlock(&s_screenLock);
}

#endif /* __ANDROID__ */
