/*
 * port/platform/android/main_android.c
 *
 * The Android entry point. SDL's Java side (SDLActivity, android/) loads
 * libSDL3.so and libmain.so and calls SDL_main on a Java thread whose stack
 * is about 1 MB; returning from it finishes the activity. SDL_main.h
 * renames main to SDL_main and exports it.
 *
 * SDL_main sets the hints first, then runs the host program's main
 * (main_host.c, compiled as ico_host_main on Android) on an SDL thread with
 * a 32 MB stack, as the desktop main thread has (the replay, the present and
 * the movie decoder run on the host stack), waits for it and returns its
 * status.
 */
#ifdef __ANDROID__

#include <android/log.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "host_android.h"

#define ICO_MAIN_STACK_BYTES (32 * 1024 * 1024)

typedef struct MainArgs {
    int argc;
    char **argv;
    int status;
} MainArgs;

static int SDLCALL main_thread(void *user)
{
    MainArgs *a = (MainArgs *)user;

    a->status = ico_host_main(a->argc, a->argv);
    return a->status;
}

int main(int argc, char *argv[])
{
    MainArgs a;
    SDL_PropertiesID props;
    SDL_Thread *t;

    /* before anything reads them: landscape either way up; Back arrives as
       a key (the pause menu) instead of finishing the activity; touches are
       not mice and mice not touches (the touch overlay reads fingers);
       quitting ends the process, so the next start is a fresh one (the EE
       RAM arena must load at the same address, arena.c); the app's name.
       SDL_HINT_VIDEO_ALLOW_SCREENSAVER stays unset: SDL then keeps the
       screen on while the game runs. */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "0");
    SDL_SetHint(SDL_HINT_APP_NAME, "ICO");

    a.argc = argc;
    a.argv = argv;
    a.status = 1;
    props = SDL_CreateProperties();
    if (props == 0) {
        __android_log_print(ANDROID_LOG_ERROR, ICO_ANDROID_LOG_TAG, "SDL_CreateProperties: %s",
                            SDL_GetError());
        return 1;
    }
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                           (void *)main_thread);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "ico-main");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, ICO_MAIN_STACK_BYTES);
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, &a);
    t = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (t == NULL) {
        /* without the thread there is no stack big enough to run on */
        __android_log_print(ANDROID_LOG_ERROR, ICO_ANDROID_LOG_TAG,
                            "cannot start the game thread: %s", SDL_GetError());
        ico_android_message_box("ICO could not start (no memory for its main thread).", 1);
        return 1;
    }
    SDL_WaitThread(t, NULL);
    return a.status;
}

#endif
