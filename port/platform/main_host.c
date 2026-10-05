/*
 * port/platform/main_host.c
 *
 * The host program's entry point. The game's own main (common/src/main.c)
 * is compiled as ico_game_main (CMakeLists.txt renames it with a definition
 * on that one source) and runs on the boot fiber (host_loop.c). This loop
 * simulates vsyncs as fast as it can; pacing and presentation come later.
 */
#include "host_loop.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_host_init();
    for (;;) {
        ico_host_step();
    }
}
