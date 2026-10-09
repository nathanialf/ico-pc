/*
 * port/platform/android/gpu_driver_android.c
 *
 * The player's graphics driver on Android (gpu_driver_android.h, v0.4.3
 * package AN-22a).
 */
#include "gpu_driver_android.h"
#include <SDL3/SDL.h>
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "gpu_driver.h"
#include "host_android.h"
#include "host_fs.h"
#include "iso_import.h"
#include "rhi.h"

#define KEY_DRIVER "video.gpu_driver"
#define KEY_FAILED "video.gpu_driver_failed"
/* presents before the trial marker goes */
#define TRIAL_PRESENTS 300u

static const char k_failed_box[] =
    "The graphics driver you chose did not start. The game uses the phone's own driver.";

/* --- the drivers folder and the list ----------------------------------------- */

static char s_root[1024];
static IcoGpuDriver s_list[ICO_GPU_DRIVER_LIST_MAX];
static int s_count;
static int s_listed;
static char s_name[256];

/* <internal files>/drivers, made if missing; NULL when unknown */
static const char *driver_root(void)
{
    if (s_root[0] == '\0') {
        const char *internal = SDL_GetAndroidInternalStoragePath();
        if (internal == NULL || internal[0] == '\0') {
            fprintf(stderr, "gpu driver: no internal files folder: %s\n", SDL_GetError());
            return NULL;
        }
        const int w = snprintf(s_root, sizeof(s_root), "%s/drivers", internal);
        if (w < 0 || (size_t)w >= sizeof(s_root)) {
            s_root[0] = '\0';
            return NULL;
        }
    }
    ico_mkdir(s_root); /* EEXIST is fine */
    return s_root;
}

static void list_refresh(void)
{
    const char *root = driver_root();
    s_count = root != NULL ? ico_gpu_driver_list(root, s_list, ICO_GPU_DRIVER_LIST_MAX) : 0;
    s_listed = 1;
}

static void list_ensure(void)
{
    if (!s_listed) {
        list_refresh();
    }
}

static int find_folder(const char *folder)
{
    list_ensure();
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_list[i].folder, folder) == 0) {
            return i;
        }
    }
    return -1;
}

/* --- the start ---------------------------------------------------------------- */

static int s_trial;         /* the player's driver is on trial (the marker is written) */
static unsigned s_presents; /* presents since the start, while on trial */
static char s_active[160];  /* its folder */

/* a Java exception from the last call, cleared: 1 when there was one */
static int clear_exception(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 1;
    }
    return 0;
}

/* the folder the app's own libraries were unpacked into
   (getApplicationInfo().nativeLibraryDir, with a trailing '/'), where the
   build put libadrenotools' hook libraries; else the folder of libmain.so */
static int native_lib_dir(char *out, size_t n)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = env != NULL ? (jobject)SDL_GetAndroidActivity() : NULL;
    int ok = 0;

    out[0] = '\0';
    if (env != NULL && act != NULL) {
        jclass cls = (*env)->GetObjectClass(env, act);
        jmethodID get = cls != NULL ? (*env)->GetMethodID(env, cls, "getApplicationInfo",
                                                          "()Landroid/content/pm/ApplicationInfo;")
                                    : NULL;
        if (clear_exception(env)) {
            get = NULL;
        }
        jobject info = get != NULL ? (*env)->CallObjectMethod(env, act, get) : NULL;
        if (clear_exception(env)) {
            info = NULL;
        }
        jclass infoCls = info != NULL ? (*env)->GetObjectClass(env, info) : NULL;
        jfieldID field = infoCls != NULL ? (*env)->GetFieldID(env, infoCls, "nativeLibraryDir",
                                                              "Ljava/lang/String;")
                                         : NULL;
        if (clear_exception(env)) {
            field = NULL;
        }
        jstring dir = field != NULL ? (jstring)(*env)->GetObjectField(env, info, field) : NULL;
        if (dir != NULL) {
            const char *s = (*env)->GetStringUTFChars(env, dir, NULL);
            if (s != NULL) {
                const int w = snprintf(out, n, "%s", s);
                ok = w > 0 && (size_t)w < n;
                (*env)->ReleaseStringUTFChars(env, dir, s);
            }
            (*env)->DeleteLocalRef(env, dir);
        }
        clear_exception(env);
        if (infoCls != NULL) {
            (*env)->DeleteLocalRef(env, infoCls);
        }
        if (info != NULL) {
            (*env)->DeleteLocalRef(env, info);
        }
        if (cls != NULL) {
            (*env)->DeleteLocalRef(env, cls);
        }
    }
    if (env != NULL && act != NULL) {
        (*env)->DeleteLocalRef(env, act);
    }
    if (!ok) {
        Dl_info di;
        if (dladdr((void *)native_lib_dir, &di) != 0 && di.dli_fname != NULL) {
            const char *slash = strrchr(di.dli_fname, '/');
            if (slash != NULL && (size_t)(slash - di.dli_fname) < n) {
                snprintf(out, n, "%.*s", (int)(slash - di.dli_fname), di.dli_fname);
                ok = 1;
                fprintf(stderr, "gpu driver: the app's library folder from libmain.so: %s\n", out);
            }
        }
    }
    if (ok) {
        /* adrenotools joins names to it directly */
        const size_t len = strlen(out);
        if (len + 1 < n && (len == 0 || out[len - 1] != '/')) {
            out[len] = '/';
            out[len + 1] = '\0';
        }
    }
    return ok ? 0 : -1;
}

/* the player's driver did not start: remembered, cleared, the box */
static void driver_failed(const char *folder, const char *why)
{
    const char *root = driver_root();

    fprintf(stderr, "gpu driver: %s did not start (%s); using the phone's own driver\n", folder,
            why);
    rhi_SetVulkanLoader(NULL);
    ico_config_set_string(KEY_FAILED, folder);
    ico_config_set_string(KEY_DRIVER, "");
    if (ico_config_save() != 0) {
        fprintf(stderr, "gpu driver: could not save the settings\n");
    }
    if (root != NULL) {
        ico_gpu_driver_trial_ok(root);
    }
    s_trial = 0;
    ico_android_message_box(k_failed_box, 1);
}

int ico_gpu_driver_android_start(void)
{
    char hookDir[1024], driverDir[1200];
    const char *want = ico_config_get_string(KEY_DRIVER, "");
    const char *root = driver_root();

    rhi_SetVulkanLoader(NULL);
    s_trial = 0;
    s_presents = 0;
    if (want == NULL || want[0] == '\0') {
        if (root != NULL) {
            ico_gpu_driver_trial_ok(root); /* a marker left from an older choice */
        }
        return 0;
    }
    snprintf(s_active, sizeof(s_active), "%s", want);
    if (root == NULL) {
        fprintf(stderr, "gpu driver: no drivers folder; using the phone's own driver\n");
        return 0;
    }
    if (ico_gpu_driver_trial_crashed(root, s_active)) {
        driver_failed(s_active, "the last start with it ended before it drew");
        return 0;
    }
    const int i = find_folder(s_active);
    if (i < 0) {
        /* removed by hand: nothing to warn about */
        fprintf(stderr, "gpu driver: %s is not installed; using the phone's own driver\n",
                s_active);
        ico_config_set_string(KEY_DRIVER, "");
        ico_config_save();
        return 0;
    }
    if (native_lib_dir(hookDir, sizeof(hookDir)) != 0) {
        driver_failed(s_active, "the app's library folder is unknown");
        return 0;
    }
    snprintf(driverDir, sizeof(driverDir), "%s/%s/", root, s_active);
    if (ico_gpu_driver_trial_begin(root, s_active) != 0) {
        fprintf(stderr, "gpu driver: cannot write the start marker in %s\n", root);
    }
    fprintf(stderr, "gpu driver: opening %s (%s, %s) from %s, hooks in %s\n", s_active,
            s_list[i].meta.name, s_list[i].meta.libraryName, driverDir, hookDir);
    void *lib = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, NULL, hookDir,
                                           driverDir, s_list[i].meta.libraryName, NULL, NULL);
    if (lib == NULL) {
        const char *e = dlerror();
        fprintf(stderr, "gpu driver: adrenotools_open_libvulkan failed%s%s\n", e ? ": " : "",
                e ? e : "");
        driver_failed(s_active, "it could not be loaded");
        return 0;
    }
    /* the handle stays open for the program's life */
    void *gipa = dlsym(lib, "vkGetInstanceProcAddr");
    if (gipa == NULL) {
        driver_failed(s_active, "it has no vkGetInstanceProcAddr");
        return 0;
    }
    rhi_SetVulkanLoader(gipa);
    s_trial = 1;
    return 1;
}

void ico_gpu_driver_android_init_failed(void)
{
    if (s_active[0] != '\0') {
        driver_failed(s_active, "the renderer could not start on it");
    }
    rhi_SetVulkanLoader(NULL);
}

void ico_gpu_driver_android_presented(void)
{
    if (!s_trial || ++s_presents < TRIAL_PRESENTS) {
        return;
    }
    s_trial = 0;
    if (driver_root() != NULL) {
        ico_gpu_driver_trial_ok(s_root);
    }
    fprintf(stderr, "gpu driver: %s drew %u frames; it is kept\n", s_active, TRIAL_PRESENTS);
}

/* --- adding a driver: the picker, then a thread -------------------------------- */

enum { INSTALL_IDLE = 0, INSTALL_PICKING, INSTALL_COPYING, INSTALL_DONE };

static SDL_AtomicInt s_installState;
static SDL_AtomicInt s_installResult; /* UI_GPU_INSTALL_* once DONE */
static SDL_AtomicInt s_pickGen;
static char s_uri[2048];

static void install_done(int result)
{
    SDL_SetAtomicInt(&s_installResult, result);
    SDL_SetAtomicInt(&s_installState, INSTALL_DONE);
}

/* the copy and the unpacking, off the game's thread */
static int SDLCALL install_thread(void *user)
{
    (void)user;
    char tmp[1200], zip[1200], folder[160], why[256];
    const char *root = s_root;

    snprintf(tmp, sizeof(tmp), "%s/incoming.zip.tmp", root);
    snprintf(zip, sizeof(zip), "%s/incoming.zip", root);
    SDL_IOStream *src = SDL_IOFromFile(s_uri, "rb");
    if (src == NULL) {
        fprintf(stderr, "gpu driver: cannot open the chosen file: %s\n", SDL_GetError());
        install_done(UI_GPU_INSTALL_BAD);
        return 0;
    }
    const Sint64 size = SDL_GetIOSize(src);
    const long long freeBytes = ico_android_free_bytes(root);
    /* the zip and its files unpacked, with a margin */
    if (size > 0 && freeBytes >= 0 &&
        (unsigned long long)freeBytes < (unsigned long long)size * 3ull + (16ull << 20)) {
        fprintf(stderr, "gpu driver: %lld bytes free for a %lld byte package\n", freeBytes,
                (long long)size);
        SDL_CloseIO(src);
        install_done(UI_GPU_INSTALL_NOSPACE);
        return 0;
    }
    const int c = ico_iso_copy(src, tmp, NULL, NULL, why, sizeof(why));
    SDL_CloseIO(src);
    if (c != 0) {
        const long long now = ico_android_free_bytes(root);
        fprintf(stderr, "gpu driver: copying the chosen file failed: %s\n", why);
        install_done(now >= 0 && now < (1ll << 20) ? UI_GPU_INSTALL_NOSPACE : UI_GPU_INSTALL_BAD);
        return 0;
    }
    const int r = ico_gpu_driver_install_zip(root, zip, SDL_GetAndroidSDKVersion(), folder,
                                             sizeof(folder), why, sizeof(why));
    ico_remove(zip);
    if (r == ICO_GPU_DRIVER_OK) {
        fprintf(stderr, "gpu driver: installed %s\n", folder);
        install_done(UI_GPU_INSTALL_ADDED);
    } else {
        fprintf(stderr, "gpu driver: the chosen file was not installed: %s\n", why);
        install_done(r == ICO_GPU_DRIVER_NOSPACE ? UI_GPU_INSTALL_NOSPACE : UI_GPU_INSTALL_BAD);
    }
    return 0;
}

static void SDLCALL pick_done(void *user, const char *const *files, int filter)
{
    (void)filter;
    if ((int)(intptr_t)user != SDL_GetAtomicInt(&s_pickGen) ||
        SDL_GetAtomicInt(&s_installState) != INSTALL_PICKING) {
        return;
    }
    if (files == NULL) {
        fprintf(stderr, "gpu driver: the file picker failed: %s\n", SDL_GetError());
        install_done(UI_GPU_INSTALL_BAD);
        return;
    }
    if (files[0] == NULL) {
        install_done(UI_GPU_INSTALL_CANCELLED);
        return;
    }
    snprintf(s_uri, sizeof(s_uri), "%s", files[0]);
    SDL_SetAtomicInt(&s_installState, INSTALL_COPYING);
    SDL_Thread *t = SDL_CreateThread(install_thread, "ico-gpu-driver", NULL);
    if (t == NULL) {
        fprintf(stderr, "gpu driver: SDL_CreateThread: %s\n", SDL_GetError());
        install_done(UI_GPU_INSTALL_BAD);
        return;
    }
    SDL_DetachThread(t);
}

/* --- the Settings page's host ---------------------------------------------------- */

static int host_count(void)
{
    list_ensure();
    return s_count;
}

static const char *host_name(int i)
{
    list_ensure();
    if (i < 0 || i >= s_count) {
        return "";
    }
    ico_gpu_driver_display_name(&s_list[i].meta, s_name, sizeof(s_name));
    return s_name;
}

static int host_selected(void)
{
    const char *want = ico_config_get_string(KEY_DRIVER, "");
    return want != NULL && want[0] != '\0' ? find_folder(want) : -1;
}

static void host_select(int i)
{
    list_ensure();
    ico_config_set_string(KEY_DRIVER, i >= 0 && i < s_count ? s_list[i].folder : "");
    /* a new choice: the earlier failure is not shown any more */
    ico_config_set_string(KEY_FAILED, "");
}

static int host_last_failed(int i)
{
    const char *failed = ico_config_get_string(KEY_FAILED, "");
    list_ensure();
    return i >= 0 && i < s_count && failed != NULL && failed[0] != '\0' &&
           strcmp(failed, s_list[i].folder) == 0;
}

static int host_adreno(void)
{
    const char *name = rhi_AdapterName();
    return name != NULL && (strstr(name, "Adreno") != NULL || strstr(name, "adreno") != NULL ||
                            strstr(name, "ADRENO") != NULL);
}

static int host_install_begin(void)
{
    const int state = SDL_GetAtomicInt(&s_installState);
    if (state == INSTALL_PICKING || state == INSTALL_COPYING || driver_root() == NULL) {
        return -1;
    }
    SDL_SetAtomicInt(&s_installResult, UI_GPU_INSTALL_PENDING);
    SDL_SetAtomicInt(&s_installState, INSTALL_PICKING);
    const int gen = SDL_AddAtomicInt(&s_pickGen, 1) + 1;
    /* no filters: Android's picker matches MIME types, and the package's
       own check says whether the file is one */
    SDL_ShowOpenFileDialog(pick_done, (void *)(intptr_t)gen, NULL, NULL, 0, NULL, false);
    return 0;
}

static int host_install_poll(void)
{
    const int state = SDL_GetAtomicInt(&s_installState);
    if (state == INSTALL_PICKING || state == INSTALL_COPYING) {
        return UI_GPU_INSTALL_PENDING;
    }
    if (state != INSTALL_DONE) {
        return UI_GPU_INSTALL_CANCELLED;
    }
    const int r = SDL_GetAtomicInt(&s_installResult);
    SDL_SetAtomicInt(&s_installState, INSTALL_IDLE);
    if (r == UI_GPU_INSTALL_ADDED) {
        list_refresh();
    }
    return r;
}

static void host_remove(int i)
{
    const char *root = driver_root();
    list_ensure();
    if (root == NULL || i < 0 || i >= s_count) {
        return;
    }
    char folder[160];
    snprintf(folder, sizeof(folder), "%s", s_list[i].folder);
    if (ico_gpu_driver_remove(root, folder) != 0) {
        fprintf(stderr, "gpu driver: could not delete everything in %s/%s\n", root, folder);
    } else {
        fprintf(stderr, "gpu driver: removed %s\n", folder);
    }
    const char *failed = ico_config_get_string(KEY_FAILED, "");
    if (failed != NULL && strcmp(failed, folder) == 0) {
        ico_config_set_string(KEY_FAILED, "");
    }
    const char *want = ico_config_get_string(KEY_DRIVER, "");
    if (want != NULL && strcmp(want, folder) == 0) {
        ico_config_set_string(KEY_DRIVER, "");
    }
    list_refresh();
}

static const UiGpuDriverHost k_host = {
    .count = host_count,
    .name = host_name,
    .selected = host_selected,
    .select = host_select,
    .lastFailed = host_last_failed,
    .adreno = host_adreno,
    .installBegin = host_install_begin,
    .installPoll = host_install_poll,
    .remove = host_remove,
};

const UiGpuDriverHost *ico_gpu_driver_android_host(void)
{
    return &k_host;
}
