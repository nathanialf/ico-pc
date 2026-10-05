/*
 * port/platform/main_host.c
 *
 * The host program's entry point. The game's own main (common/src/main.c) is
 * compiled as ico_game_main (CMakeLists.txt renames it with a definition on
 * that one source), and this main calls it after putting the FPU in the
 * simulation's mode. The host loop, fibers and devices replace this in
 * Phase 1 (package 1B).
 */
#include "fpenv.h"

int ico_game_main(void);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_fpenv_sim_enter();
    return ico_game_main();
}
