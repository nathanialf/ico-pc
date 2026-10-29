/*
 * port/platform/android/gpu_driver_android.c
 *
 * The player's graphics driver on Android (gpu_driver_android.h).
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
#include "diag_host.h"
#include "gpu_driver.h"
#include "host_android.h"
#include "host_fs.h"
#include "iso_import.h"
#include "rhi.h"
#include "rhi_driver_version.h"
#include "strings.h" /* port/ui: the hang box's driver route (UI_STR_GPU_HANG_TURNIP) */

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

/* the driver zips in the game's drivers folder (the way in when the file
   picker does not open) */
#define ZIP_MAX 16
static char s_zips[ZIP_MAX][160];
static int s_zipCount;
static int s_zipsListed;

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

/* <files>/drivers, made if missing so a USB cable finds it; relisted */
static void zips_refresh(void)
{
    const IcoAndroidPaths *p = ico_android_paths();
    s_zipCount = 0;
    if (p != NULL && p->drivers[0] != '\0') {
        ico_mkdir(p->drivers);
        s_zipCount = ico_gpu_driver_list_zips(p->drivers, s_zips, ZIP_MAX);
    }
    s_zipsListed = 1;
}

static void zips_ensure(void)
{
    if (!s_zipsListed) {
        zips_refresh();
    }
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
    rhi_set_vulkan_loader(NULL);
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

    rhi_set_vulkan_loader(NULL);
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
        /* without the marker a crash inside the driver would go unseen at
           the next start, and every start would load it again: the phone's
           own driver this time, the choice kept for a start that can write
           it */
        fprintf(stderr,
                "gpu driver: cannot write the start marker in %s; using the phone's own driver "
                "this time\n",
                root);
        return 0;
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
    rhi_set_vulkan_loader(gipa);
    s_trial = 1;
    return 1;
}

void ico_gpu_driver_android_init_failed(void)
{
    if (s_active[0] != '\0') {
        driver_failed(s_active, "the renderer could not start on it");
    }
    rhi_set_vulkan_loader(NULL);
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

void ico_gpu_driver_android_end(void)
{
    if (!s_trial) {
        return;
    }
    s_trial = 0;
    if (driver_root() != NULL) {
        ico_gpu_driver_trial_ok(s_root);
    }
    fprintf(stderr, "gpu driver: %s ran until a normal end (%u frames); it is kept\n", s_active,
            s_presents);
}

/* --- adding a driver: the picker, then a thread -------------------------------- */

enum { INSTALL_IDLE = 0, INSTALL_PICKING, INSTALL_COPYING, INSTALL_DONE };

static SDL_AtomicInt s_installState;
static SDL_AtomicInt s_installResult; /* UI_GPU_INSTALL_* once DONE */
static SDL_AtomicInt s_pickGen;
/* the file to copy: the picker's content URI, or a zip's path in the drivers
   folder (s_fromFolder, deleted once it is added) */
static char s_uri[2048];
static int s_fromFolder;
/* the folder the last install made; written before install_done, read once
   the state says DONE */
static char s_installed[160];

/* The picker watch (issue 135): some phones' pickers close without any
   answer (the activity comes back to the front with no onActivityResult) or
   never open at all, and the wait would not end. The pick ends as CANCELLED
   once the activity has come back to the front PICK_RESUME_GRACE_MS ago
   with no answer (an answer arrives before the resume), or after
   PICK_TIMEOUT_MS whatever happened. Read and written on the game's thread
   only (host_install_begin, pick_watch). */
#define PICK_RESUME_GRACE_MS 2000u
#define PICK_TIMEOUT_MS 120000u
static Uint64 s_pickStartMs;
static Uint64 s_pickResumedMs;
static int s_pickResumed; /* the resume was seen (s_pickResumedMs is its time) */
static int s_pickResumeBase;

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
        install_done(UI_GPU_INSTALL_UNREADABLE);
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
        install_done(now >= 0 && now < (1ll << 20) ? UI_GPU_INSTALL_NOSPACE
                                                   : UI_GPU_INSTALL_UNREADABLE);
        return 0;
    }
    const int r = ico_gpu_driver_install_zip(root, zip, SDL_GetAndroidSDKVersion(), folder,
                                             sizeof(folder), why, sizeof(why));
    ico_remove(zip);
    if (r == ICO_GPU_DRIVER_OK) {
        fprintf(stderr, "gpu driver: installed %s\n", folder);
        snprintf(s_installed, sizeof(s_installed), "%s", folder);
        if (s_fromFolder) {
            /* added: the zip is not wanted twice (as the disc copy goes) */
            if (ico_remove(s_uri) != 0) {
                fprintf(stderr, "gpu driver: could not delete %s\n", s_uri);
            }
        }
        install_done(UI_GPU_INSTALL_ADDED);
    } else {
        fprintf(stderr, "gpu driver: the chosen file was not installed: %s\n", why);
        install_done(r == ICO_GPU_DRIVER_NOSPACE ? UI_GPU_INSTALL_NOSPACE : UI_GPU_INSTALL_BAD);
    }
    return 0;
}

/* the picker's answer for the log: the content URI's authority only (the
   rest names the player's file), or what kind of answer it was */
static void log_answer(int gen, const char *file)
{
    const char *auth = strncmp(file, "content://", 10) == 0 ? file + 10 : NULL;
    if (auth != NULL) {
        const char *end = strchr(auth, '/');
        fprintf(stderr, "gpu driver: the file picker answered (pick %d): content://%.*s/...\n", gen,
                (int)(end != NULL ? (size_t)(end - auth) : strlen(auth)), auth);
    } else {
        fprintf(stderr, "gpu driver: the file picker answered (pick %d): a file path\n", gen);
    }
}

/* the copy thread for s_uri; the state is COPYING already */
static void start_install_thread(void)
{
    SDL_Thread *t = SDL_CreateThread(install_thread, "ico-gpu-driver", NULL);
    if (t == NULL) {
        fprintf(stderr, "gpu driver: SDL_CreateThread: %s\n", SDL_GetError());
        install_done(UI_GPU_INSTALL_UNREADABLE);
        return;
    }
    SDL_DetachThread(t);
}

/* the picker's answer (host_android.c ico_android_pick_file), on the
   activity's thread: atomics, s_uri (read by the copy thread made after it)
   and SDL_CreateThread only, as SDL's own answer callback did before */
static void pick_done(void *user, int gen, const char *uri, const char *why)
{
    (void)user;
    if (gen != SDL_GetAtomicInt(&s_pickGen) ||
        SDL_GetAtomicInt(&s_installState) != INSTALL_PICKING) {
        fprintf(stderr, "gpu driver: a late answer for pick %d (now %d) ignored\n", gen,
                SDL_GetAtomicInt(&s_pickGen));
        return;
    }
    if (uri != NULL) {
        /* the file: the state moves on only if the watch has not ended the
           pick in the same moment */
        if (!SDL_CompareAndSwapAtomicInt(&s_installState, INSTALL_PICKING, INSTALL_COPYING)) {
            fprintf(stderr, "gpu driver: a late answer for pick %d (the wait had ended) ignored\n",
                    gen);
            return;
        }
        log_answer(gen, uri);
        snprintf(s_uri, sizeof(s_uri), "%s", uri);
        s_fromFolder = 0;
        start_install_thread();
        return;
    }
    if (why != NULL) {
        fprintf(stderr, "gpu driver: the file picker failed: %s\n", why);
        install_done(UI_GPU_INSTALL_NO_PICKER);
        return;
    }
    fprintf(stderr,
            "gpu driver: the file picker answered (pick %d): closed without a file, %llu ms after "
            "it opened\n",
            gen, (unsigned long long)(SDL_GetTicks() - s_pickStartMs));
    install_done(UI_GPU_INSTALL_CANCELLED);
}

/* ends a pick the picker never answered (see PICK_RESUME_GRACE_MS) as
   CANCELLED, with the reason in the log; the late answer, if any, finds the
   state moved on and is dropped */
static void pick_watch(void)
{
    if (SDL_GetAtomicInt(&s_installState) != INSTALL_PICKING) {
        return;
    }
    const Uint64 now = SDL_GetTicks();
    const char *why = NULL;
    if (ico_android_resume_count() != s_pickResumeBase) {
        if (!s_pickResumed) {
            s_pickResumed = 1;
            s_pickResumedMs = now;
            fprintf(stderr,
                    "gpu driver: the app came back to the front %llu ms after the picker opened "
                    "(pick %d); no answer yet\n",
                    (unsigned long long)(now - s_pickStartMs), SDL_GetAtomicInt(&s_pickGen));
        }
        if (now - s_pickResumedMs >= PICK_RESUME_GRACE_MS) {
            why = "the app came back to the front with no answer";
        }
    }
    if (why == NULL && now - s_pickStartMs >= PICK_TIMEOUT_MS) {
        why = "no answer within 120 s";
    }
    if (why == NULL) {
        return;
    }
    SDL_SetAtomicInt(&s_installResult, UI_GPU_INSTALL_CANCELLED);
    if (SDL_CompareAndSwapAtomicInt(&s_installState, INSTALL_PICKING, INSTALL_DONE)) {
        fprintf(stderr,
                "gpu driver: the file picker (pick %d) gave no answer: %s (%llu ms); the "
                "wait is over\n",
                SDL_GetAtomicInt(&s_pickGen), why, (unsigned long long)(now - s_pickStartMs));
    }
}

/* --- the Settings page's host ---------------------------------------------------- */

/* the drivers folder as a file manager shows it: the path without the
   internal storage's root ("/storage/emulated/0/"), which the phone's file
   apps leave out ("Android/data/com.defnf.icopc/files/drivers"); another
   root (an SD card) stays whole. Empty before the paths are known. */
const char *ico_gpu_driver_android_folder_text(void)
{
    static char text[ICO_ANDROID_PATH_MAX];
    const IcoAndroidPaths *p = ico_android_paths();

    if (p == NULL || p->drivers[0] == '\0') {
        return "";
    }
    const char *path = p->drivers;
    if (strncmp(path, "/storage/emulated/", 18) == 0) {
        const char *slash = strchr(path + 18, '/');
        if (slash != NULL) {
            path = slash + 1;
        }
    }
    snprintf(text, sizeof(text), "%s", path);
    return text;
}

/* the previous start lost the graphics device on the phone's own Qualcomm
   driver (window_host.c, once the device is up): the Graphics driver row's
   note then names the driver package route */
static int s_lostOnStock;
/* this start runs on the phone's own Qualcomm driver (no driver package:
   ico_gpu_driver_android_device_up); the boxes after the driver stopped
   responding then name the driver package route (issue 123) */
static int s_onStock;

void ico_gpu_driver_android_note_loss_on_stock(void)
{
    if (!s_lostOnStock) {
        s_lostOnStock = 1;
        fprintf(stderr,
                "gpu driver: the previous start lost the graphics device on this phone's own "
                "Qualcomm driver; a Turnip driver package (Options > Graphics driver > Add a "
                "driver, or a zip file in %s) is loaded by the game itself, without the "
                "system's driver\n",
                ico_gpu_driver_android_folder_text());
    }
}

/* rd_init made a device (window_host.c): the previous start lost the graphics
   device and this one runs on the phone's own Qualcomm driver (no driver
   package loaded; Turnip reports Qualcomm's vendor id too, hence
   customDriver) */
void ico_gpu_driver_android_device_up(int customDriver)
{
    s_onStock =
        !customDriver && rhi_limits() != NULL && rhi_limits()->vendorId == RHI_VENDOR_QUALCOMM;
    if (s_onStock && ico_diag_startup_end_count(ICO_DIAG_END_LOSS) > 0) {
        ico_gpu_driver_android_note_loss_on_stock();
    }
    /* the GPU hang's box (the watchdog's end) ends with it too */
    ico_diag_set_gpu_hang_note(ico_gpu_driver_android_hang_note);
}

const char *ico_gpu_driver_android_hang_note(void)
{
    /* English, as the boxes it ends are (one language in a box); the
       Options rows keep the menu's language */
    return s_onStock ? ui_str_in(UI_LANG_EN, UI_STR_GPU_HANG_TURNIP) : NULL;
}

static int host_lost_on_stock(void)
{
    return s_lostOnStock;
}

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
    const char *name = rhi_adapter_name();
    return name != NULL && (strstr(name, "Adreno") != NULL || strstr(name, "adreno") != NULL ||
                            strstr(name, "ADRENO") != NULL);
}

static int host_files_count(void)
{
    zips_ensure();
    return s_zipCount;
}

static const char *host_file_name(int i)
{
    zips_ensure();
    return i >= 0 && i < s_zipCount ? s_zips[i] : "";
}

static int install_busy(void)
{
    pick_watch();
    const int state = SDL_GetAtomicInt(&s_installState);
    return state == INSTALL_PICKING || state == INSTALL_COPYING;
}

static int host_install_begin(void)
{
    if (install_busy()) {
        return -1;
    }
    const int gen = SDL_AddAtomicInt(&s_pickGen, 1) + 1;
    fprintf(stderr, "gpu driver: Add a driver: opening the file picker (pick %d)\n", gen);
    SDL_SetAtomicInt(&s_installResult, UI_GPU_INSTALL_PENDING);
    s_installed[0] = '\0';
    if (driver_root() == NULL) {
        /* the page always gets an answer: the poll hands it over */
        install_done(UI_GPU_INSTALL_NO_PICKER);
        return 0;
    }
    s_pickStartMs = SDL_GetTicks();
    s_pickResumed = 0;
    s_pickResumeBase = ico_android_resume_count();
    SDL_SetAtomicInt(&s_installState, INSTALL_PICKING);
    /* any type: the package's own check says whether the file is one */
    char why[512];
    int noApp = 0;
    if (ico_android_pick_file(gen, pick_done, NULL, &noApp, why, sizeof(why)) != 0) {
        fprintf(stderr, "gpu driver: the file picker could not open: %s\n", why);
        install_done(noApp ? UI_GPU_INSTALL_NO_PICKER_APP : UI_GPU_INSTALL_NO_PICKER);
    }
    return 0;
}

/* add the zip i of the drivers folder: the same copy thread, no picker */
static int host_install_file_begin(int i)
{
    const IcoAndroidPaths *p = ico_android_paths();
    zips_ensure();
    if (install_busy() || i < 0 || i >= s_zipCount) {
        return -1;
    }
    const int gen = SDL_AddAtomicInt(&s_pickGen, 1) + 1;
    fprintf(stderr, "gpu driver: Add from the drivers folder: %s (pick %d)\n", s_zips[i], gen);
    SDL_SetAtomicInt(&s_installResult, UI_GPU_INSTALL_PENDING);
    s_installed[0] = '\0';
    if (p == NULL || driver_root() == NULL ||
        snprintf(s_uri, sizeof(s_uri), "%s/%s", p->drivers, s_zips[i]) >= (int)sizeof(s_uri)) {
        install_done(UI_GPU_INSTALL_UNREADABLE);
        return 0;
    }
    s_fromFolder = 1;
    SDL_SetAtomicInt(&s_installState, INSTALL_COPYING);
    start_install_thread();
    return 0;
}

static const char *result_text(int r)
{
    switch (r) {
    case UI_GPU_INSTALL_ADDED:
        return "ADDED";
    case UI_GPU_INSTALL_BAD:
        return "NOT A PACKAGE";
    case UI_GPU_INSTALL_NOSPACE:
        return "NO SPACE";
    case UI_GPU_INSTALL_NO_PICKER:
        return "NO PICKER";
    case UI_GPU_INSTALL_NO_PICKER_APP:
        return "NO PICKER APP";
    case UI_GPU_INSTALL_UNREADABLE:
        return "UNREADABLE";
    default:
        return "CANCELLED";
    }
}

static int host_install_poll(void)
{
    pick_watch();
    const int state = SDL_GetAtomicInt(&s_installState);
    if (state == INSTALL_PICKING || state == INSTALL_COPYING) {
        return UI_GPU_INSTALL_PENDING;
    }
    if (state != INSTALL_DONE) {
        return UI_GPU_INSTALL_CANCELLED;
    }
    const int r = SDL_GetAtomicInt(&s_installResult);
    SDL_SetAtomicInt(&s_installState, INSTALL_IDLE);
    fprintf(stderr, "gpu driver: Add a driver ended: %s\n", result_text(r));
    zips_refresh(); /* an added zip is gone from the folder */
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
    .fileCount = host_files_count,
    .fileName = host_file_name,
    .installFileBegin = host_install_file_begin,
    .folderPath = ico_gpu_driver_android_folder_text,
    .lostOnStock = host_lost_on_stock,
};

const UiGpuDriverHost *ico_gpu_driver_android_host(void)
{
    return &k_host;
}

/* --- the start-up choice, before there is a device ------------------------------ */

/* The start-up box (window_host.c choose_driver): why the game cannot start,
   the driver package route, and the folder for a zip when the picker does
   not open. */
const char *ico_gpu_driver_android_box_text(void)
{
    static char box[768];
    const char *where = ico_gpu_driver_android_folder_text();

    snprintf(box, sizeof(box),
             "ICO could not start the game's graphics with this phone's driver.\n\n"
             "On a phone with an Adreno (Qualcomm) chip you can choose a graphics driver "
             "package instead (a .zip file, for example Turnip). The game loads a package "
             "itself, without the phone's own Vulkan loader, so it can work where the phone's "
             "driver does not.\n\n"
             "If the file picker does not appear, copy the zip file into this folder and "
             "choose again:\n%s",
             where[0] != '\0' ? where : "drivers");
    return box;
}

/* no device could be made (window_host.c, before the box): the log says
   which way past the system's loader there is */
void ico_gpu_driver_android_no_device_note(void)
{
    fprintf(stderr,
            "window: a graphics driver package (chosen in the box, in Options > Graphics driver, "
            "or a .zip file in %s) is loaded by the game itself, without the system's Vulkan "
            "loader\n",
            ico_gpu_driver_android_folder_text());
}

/* the folder's words, "drivers" when the paths are not known */
static const char *p_drivers_text(void)
{
    const char *t = ico_gpu_driver_android_folder_text();
    return t[0] != '\0' ? t : "drivers";
}

int ico_gpu_driver_android_choose(char *folder, size_t n, char *why, size_t whyn)
{
    int r;

    folder[0] = '\0';
    why[0] = '\0';
    if (host_install_begin() != 0) {
        snprintf(why, whyn, "The file picker could not be opened.");
        return 0;
    }
    /* nothing can be drawn yet: SDL's events are pumped (the picker's
       answer and the app coming back to the front) and left in the queue */
    while ((r = host_install_poll()) == UI_GPU_INSTALL_PENDING) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    if ((r == UI_GPU_INSTALL_NO_PICKER || r == UI_GPU_INSTALL_NO_PICKER_APP ||
         r == UI_GPU_INSTALL_CANCELLED) &&
        host_files_count() > 0) {
        /* no picker, or one that closed without a file (some phones' pickers
           do at once, issue 135): the first zip in the drivers folder, if
           the player put one there */
        fprintf(stderr, "gpu driver: %s at the start; trying %s from the drivers folder (%s)\n",
                r == UI_GPU_INSTALL_CANCELLED ? "no file from the picker" : "no file picker",
                host_file_name(0), p_drivers_text());
        if (host_install_file_begin(0) == 0) {
            while ((r = host_install_poll()) == UI_GPU_INSTALL_PENDING) {
                SDL_PumpEvents();
                SDL_Delay(20);
            }
        }
    }
    if (r != UI_GPU_INSTALL_ADDED) {
        snprintf(why, whyn, "%s",
                 r == UI_GPU_INSTALL_NOSPACE      ? "Not enough free space."
                 : r == UI_GPU_INSTALL_UNREADABLE ? "The chosen file could not be read."
                 : r == UI_GPU_INSTALL_CANCELLED  ? "No file was chosen."
                 : r == UI_GPU_INSTALL_NO_PICKER  ? "The file picker could not be opened."
                 : r == UI_GPU_INSTALL_NO_PICKER_APP
                     ? "No app on this device can choose a file."
                     : "That file is not a graphics driver package.");
        if (r == UI_GPU_INSTALL_CANCELLED || r == UI_GPU_INSTALL_NO_PICKER ||
            r == UI_GPU_INSTALL_NO_PICKER_APP) {
            /* the way that needs no picker */
            const size_t n0 = strlen(why);
            snprintf(why + n0, whyn - n0,
                     " You can copy the driver's zip file into this folder instead, then "
                     "choose again:\n%s",
                     p_drivers_text());
        }
        return 0;
    }
    const int i = find_folder(s_installed); /* host_install_poll refreshed the list */
    if (i < 0) {
        fprintf(stderr, "gpu driver: %s was installed but is not in the list\n", s_installed);
        snprintf(why, whyn, "That file is not a graphics driver package.");
        return 0;
    }
    host_select(i);
    if (ico_config_save() != 0) {
        fprintf(stderr, "gpu driver: could not save the settings\n");
    }
    /* the next start loads it on trial and writes its own marker: none
       is left from this run */
    s_trial = 0;
    if (driver_root() != NULL) {
        ico_gpu_driver_trial_ok(s_root);
    }
    snprintf(folder, n, "%s", s_list[i].folder);
    fprintf(stderr, "gpu driver: %s chosen at the start, for the next start\n", folder);
    return 1;
}
