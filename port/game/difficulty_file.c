/*
 * port/game/difficulty_file.c
 *
 * The difficulty sidecar (difficulty.h "the sidecar"): difficulty.toml beside
 * achievements.toml, one entry per save keyed by the disc, the card and the
 * slot, and the list of ended journeys.  In ico_options with difficulty.c, so
 * it may use only what that library and the host_config.c every one of its
 * programs compiles provide (the config store, ico_host_pref_dir, the
 * host_config.h file and toml calls): no ico_platform-only symbol beyond the
 * state registry difficulty.c already names.
 *
 * Minimal bodies until the sidecar is written: a save names the run's slot,
 * a load takes the one-time carry-over of the retired config keys (written
 * nowhere yet), no save belongs to an ended journey.
 */
#include "difficulty.h"

#include <stdio.h>

static int s_carry_logged;

void ico_difficulty_file_init(const char *dir)
{
    (void)dir;
    s_carry_logged = 0;
}

void ico_difficulty_slot_saved(int port, int slot, uint32_t sum, int stage, uint32_t play, int sofa)
{
    (void)stage;
    (void)play;
    (void)sofa;
    ico_difficulty_set_run(port, slot, sum);
}

void ico_difficulty_slot_loaded(int port, int slot, uint32_t sum)
{
    IcoDifficulty d;

    ico_difficulty_from_config(&d);
    d.run_port = port;
    d.run_slot = slot;
    d.run_has_slot = 1;
    d.run_sum = sum;
    if (!s_carry_logged) {
        s_carry_logged = 1;
        fprintf(stderr,
                "difficulty: no difficulty.toml yet: card %d slot %d takes the carry-over\n", port,
                slot);
    }
    ico_difficulty_set_active(&d);
}

int ico_difficulty_slot_finished(int port, int slot, int stage, uint32_t play, int sofa)
{
    (void)port;
    (void)slot;
    (void)stage;
    (void)play;
    (void)sofa;
    return 0;
}

int ico_difficulty_sum_finished(uint32_t sum)
{
    (void)sum;
    return 0;
}

void ico_difficulty_file_journey_ended(void) {}

void ico_difficulty_file_knocks_reset(void) {}
