/*
 * port/game/pit_time.h
 *
 * Time to rescue Yorda ([gameplay] pit_time, options.h): the length of the
 * game's own pull-in on each disc, header-only so the disc test
 * (port/game/test/pit_dive_base_test.c) checks the same numbers the option
 * reads without the options library.
 *
 * The pull-in is the shadow's five SINK motions (EN1 SINK_A to SINK_E),
 * one after the other in its KIDNAP_END action (ico2/fumi/src/enemy_act.c
 * actEnemyKidnapEnd): the motion-orient rows of the dive's request kind
 * move A to B to C to D to E each at its motion's last frame, and the game
 * takes Yorda once SINK_E passes frame 50.  At one motion frame a game tick
 * (60 Hz: 30 ticks a second; 50 Hz: 25 ticks of 1.2 frames, the same
 * seconds) that is (A - 1) + (B - 1) + (C - 1) + (D - 1) + 51 ticks for the
 * motions' frame counts: 31, 128, 128, 128 on every released disc and the
 * August 2001 preview (462 ticks, 15.4 s), 25, 131, 130, 130 on the June
 * 2001 demo (463 ticks).  The motions play at the game's speed 1 on every
 * disc (playSpeedRatio and palSpeedRatio 1).
 */
#ifndef ICO_PORT_GAME_PIT_TIME_H
#define ICO_PORT_GAME_PIT_TIME_H

#include <string.h>

/* the ticks a second the pull-in's length is counted in */
#define ICO_PIT_TICK_HZ 30
/* SINK_E's frame the game takes Yorda after (enemy_act.c: 50.0f < animFrame) */
#define ICO_PIT_TAKE_FRAME 50

/* the pull-in's ticks on the disc of a region slug (config/regions.txt) */
static inline int ico_pit_chain_ticks_for(const char *slug)
{
    return slug != NULL && strcmp(slug, "jun26") == 0 ? 463 : 462;
}

#endif
