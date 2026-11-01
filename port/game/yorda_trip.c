/*
 * port/game/yorda_trip.c
 *
 * Yorda trips: the rule and its random numbers (yorda_trip.h).
 */
#include "yorda_trip.h"

#include "../platform/state_port.h"
#include "../platform/state_registry.h"

/* the generator (xorshift32), 0 until the first roll seeds it */
static uint32_t s_rng;
static int s_seeded;
/* run-pulled ticks counted (0 only before the first), the stamp of the
   last one, game ticks left before a trip can come again */
static int s_run_ticks;
static uint32_t s_last_tick;
static int s_refractory;
/* the count the last trip ended (the log's; not in a state) */
static int s_last_run;

/* The save state's blocks (state_port.h). */
int ico_yorda_trip_state_register(void)
{
    int rc = 0;

    rc |= ico_state_register("yorda_trip.rng", &s_rng, sizeof s_rng, 0);
    rc |= ico_state_register("yorda_trip.seeded", &s_seeded, sizeof s_seeded, 0);
    rc |= ico_state_register("yorda_trip.run_ticks", &s_run_ticks, sizeof s_run_ticks, 0);
    rc |= ico_state_register("yorda_trip.refractory", &s_refractory, sizeof s_refractory, 0);
    rc |= ico_state_register("yorda_trip.last_tick", &s_last_tick, sizeof s_last_tick, 0);
    return rc != 0 ? -1 : 0;
}

void ico_yorda_trip_reset(void)
{
    s_rng = 0;
    s_seeded = 0;
    s_run_ticks = 0;
    s_last_tick = 0;
    s_refractory = 0;
    s_last_run = 0;
}

static uint32_t next(void)
{
    uint32_t x = s_rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

int ico_yorda_trip_step(uint32_t tick, int tps, int ready, uint32_t seed)
{
    uint32_t elapsed = tick - s_last_tick;
    uint32_t span;

    if (tps <= 0) {
        return 0;
    }
    if (s_run_ticks > 0 && elapsed == 0) {
        return 0; /* this tick is counted */
    }
    /* game time passes for the refractory time whether she runs or not; a
       stamp behind the last (another run's frame count) leaves it */
    if (s_run_ticks > 0 && elapsed < 0x80000000u) {
        s_refractory = elapsed >= (uint32_t)s_refractory ? 0 : s_refractory - (int)elapsed;
    }
    /* consecutive ticks only: a gap starts the count again */
    s_run_ticks = s_run_ticks > 0 && elapsed == 1 ? s_run_ticks + 1 : 1;
    s_last_tick = tick;
    if (!ready || s_run_ticks <= ICO_YT_ELIGIBLE_S * tps || s_refractory > 0) {
        return 0;
    }
    if (!s_seeded) {
        s_rng = seed != 0 ? seed : 0x9E3779B9u;
        s_seeded = 1;
    }
    /* the chance 1 / span a tick, span the mean wait after the refractory
       time in ticks: with the refractory time added, one trip in
       ICO_YT_MEAN_S seconds of run-pulling */
    span = (uint32_t)(ICO_YT_MEAN_S - ICO_YT_REFRACTORY_S) * (uint32_t)tps;
    if (next() >= 0xFFFFFFFFu / span) {
        return 0;
    }
    s_last_run = s_run_ticks;
    s_refractory = ICO_YT_REFRACTORY_S * tps;
    return 1;
}

int ico_yorda_trip_run_ticks(void)
{
    return s_run_ticks;
}

int ico_yorda_trip_last_run(void)
{
    return s_last_run;
}

int ico_yorda_trip_refractory(void)
{
    return s_refractory;
}

uint32_t ico_yorda_trip_rng(void)
{
    return s_rng;
}
