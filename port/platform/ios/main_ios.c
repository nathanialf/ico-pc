/* SDL owns UIKit's main thread; the engine needs more than its default stack. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "../fiber.h"
#include "../host_config.h"
#include <unistd.h>

int ico_host_main(int argc, char **argv);

typedef struct MainArgs {
    int argc;
    char **argv;
    int status;
} MainArgs;

static void run_host(void *user)
{
    MainArgs *args = user;
    args->status = ico_host_main(args->argc, args->argv);
}

int main(int argc, char **argv)
{
    MainArgs args = {argc, argv, 1};
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_APP_NAME, "ICO");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");

    char documents[ICO_PATH_MAX];
    if (ico_host_exe_dir(documents, sizeof(documents)) != 0 || chdir(documents) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO", "Cannot open the game data folder.",
                                 NULL);
        return 1;
    }

    /* Keep UIKit and rendering on this thread, with a guarded 32 MB host stack. */
    IcoFiber *host = ico_fiber_create(run_host, &args, 32 * 1024 * 1024);
    if (!host) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO", "Cannot allocate the game stack.",
                                 NULL);
        return 1;
    }
    if (ico_fiber_resume(host) != 0 || !ico_fiber_finished(host)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO", "The game stopped unexpectedly.",
                                 NULL);
        args.status = 1;
    }
    ico_fiber_destroy(host);
    return args.status;
}
