/*
 * port/platform/android/main_android.c
 *
 * The Android entry point. SDL's Java side (SDLActivity, android/) loads
 * libSDL3.so and libmain.so and calls SDL_main on its own thread; returning
 * from it finishes the activity. SDL_main.h renames main to SDL_main and
 * exports it. It runs the host program's main (main_host.c, compiled as
 * ico_host_main on Android).
 */
#ifdef __ANDROID__

#include <SDL3/SDL_main.h>

int ico_host_main(int argc, char **argv);

int main(int argc, char *argv[])
{
    return ico_host_main(argc, argv);
}

#endif
