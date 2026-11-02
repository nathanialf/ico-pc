/*
 * port/game/yorda_trip.c
 *
 * Yorda trips: the rule (yorda_trip.h), the main gate scene's own
 * (ico2/script/src/st04a.c, actSt04aGirlSitChk).
 */
#include "yorda_trip.h"

#include "../platform/state_port.h"
#include "../platform/state_registry.h"

/* run-pulled ticks counted since the run began or the last mail (the
   scene's n), and the stamp of the last one */
static int s_run_ticks;
static uint32_t s_last_tick;
/* the count the last trip ended (the log's; not in a state) */
static int s_last_run;

/* The save state's blocks (state_port.h).  FRESH: a state an earlier
   version saved without them still loads, the count going on from this
   run's (a stale stamp only starts it again from 0) */
int ico_yorda_trip_state_register(void)
{
    int rc = 0;

    rc |= ico_state_register("yorda_trip.run_ticks", &s_run_ticks, sizeof s_run_ticks,
                             ICO_STATE_FRESH);
    rc |= ico_state_register("yorda_trip.last_tick", &s_last_tick, sizeof s_last_tick,
                             ICO_STATE_FRESH);
    return rc != 0 ? -1 : 0;
}

void ico_yorda_trip_reset(void)
{
    s_run_ticks = 0;
    s_last_tick = 0;
    s_last_run = 0;
}

int ico_yorda_trip_step(uint32_t tick, int tps, int ready)
{
    const uint32_t elapsed = tick - s_last_tick;

    if (tps <= 0) {
        return 0;
    }
    /* this tick is counted (a stamp of 0 with nothing counted is the first) */
    if (elapsed == 0 && (s_run_ticks > 0 || s_last_tick != 0)) {
        return 0;
    }
    /* the scene's count: consecutive ticks only, a tick without the bit
       (a missed stamp) starts it again; after a mail it starts from 0 */
    s_run_ticks = s_run_ticks > 0 && elapsed == 1 ? s_run_ticks + 1 : 1;
    s_last_tick = tick;
    /* the scene's ((60 - systemStatus[0] * 10) / systemStatus[1]) * 3 < n */
    if (s_run_ticks <= ICO_YT_RUN_S * tps) {
        return 0;
    }
    /* the scene sends its mail here and starts the count again whether the
       mail takes effect or not; the mail goes only where every guard holds */
    s_last_run = s_run_ticks;
    s_run_ticks = 0;
    return ready ? 1 : 0;
}

int ico_yorda_trip_run_ticks(void)
{
    return s_run_ticks;
}

int ico_yorda_trip_last_run(void)
{
    return s_last_run;
}
