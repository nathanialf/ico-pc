/*
 * port/platform/slow_judge.h
 *
 * What a slow picture and a slow step are judged against (issue 179), and
 * the game's rate counted over its own frames.  Pure: no SDL, no renderer
 * (port/platform/test/slow_judge_test.c).
 *
 * The slow-replay lines judged every replay against one display refresh
 * (16.7 ms at 60 Hz).  The Mali-G52 reporter's PAL disc draws one picture a
 * game frame, 40 ms apart, so every picture of play was "slow" (v0.11.7 log:
 * 18.7-21 ms replays at 25 pictures a second, five lines a block and the
 * longest), which hid the replays that were.  A picture is now judged against
 * the larger of the display's refresh and the picture interval in force:
 * Frame rate Original's one picture a game frame (the frame's steps times the
 * step period, window_present.c), a numeric cap's gap between presents, or
 * the refresh (Uncapped, the vsync lock).  The slow-step bound ([dev]
 * slow_step_ms, default 8: half a 60 Hz refresh) is scaled by the same
 * interval over the refresh, so at 60 pictures a second on 60 Hz it is the
 * key's value as before and at 25 it is 19.2 ms.
 *
 * The game's rate ("the game 25.1 of 25.0") counted a block's frames over the
 * block's wall time, whose ends fall anywhere between two frames: 251 frames
 * in a 10.0 s block at 25 a second.  It is now the frames over the span from
 * the frame close before the block to the block's last close (whole frame
 * intervals; the pace deadlines, the resyncs' dropped lag included).
 */
#ifndef ICO_PLATFORM_SLOW_JUDGE_H
#define ICO_PLATFORM_SLOW_JUDGE_H

#include <stddef.h>
#include <stdint.h>

/* The interval a picture is judged against: the larger of refreshNs and
   pictureNs (either 0: not known, the other taken; both 0: 0) */
uint64_t slow_judge_interval_ns(uint64_t refreshNs, uint64_t pictureNs);

/* The slow-replay threshold in ms (0: none known, never over) */
double slow_judge_replay_ms(uint64_t refreshNs, uint64_t pictureNs);

/* The slow-step threshold: keyMs scaled by the judging interval over the
   refresh (keyMs as it is when the refresh is not known, or keyMs 0 or
   less: off) */
double slow_judge_step_ms(double keyMs, uint64_t refreshNs, uint64_t pictureNs);

/* The pictures a second the presents aim at: 1e9 over the judging interval
   (0 none known) */
double slow_judge_target_hz(uint64_t refreshNs, uint64_t pictureNs);

/* The slow-step line's CPU part, " (CPU 3.2 ms of it; 7.8 ms off the CPU:
   the thread waited or was not scheduled)" when most of the step was off
   the CPU, " (CPU 9.1 ms of it; 1.9 ms off the CPU: the thread's own work)"
   when most was on it; "" with cpuMs 0 or less (not known).  The length
   written */
size_t slow_judge_step_cpu_text(char *out, size_t cap, double wallMs, double cpuMs);

/* The game's frames a second over its own frame intervals: frames closed in
   the block, closeNs the block's last close and firstNs the close before
   the block's first (the pace deadlines, ns); wallS the block's seconds,
   used when the span is not known (firstNs 0, or not before closeNs) */
double slow_judge_game_fps(unsigned frames, uint64_t firstNs, uint64_t closeNs, double wallS);

/* "slow pictures judged against 40.0 ms (the larger of the display's
   refresh 16.7 ms and the picture interval in force 40.0 ms), slow steps
   against 19.2 ms ([dev] slow_step_ms 8.0 scaled by 40.0 / 16.7)" (after
   "window: "); the length written */
size_t slow_judge_text(char *out, size_t cap, uint64_t refreshNs, uint64_t pictureNs, double keyMs);

#endif /* ICO_PLATFORM_SLOW_JUDGE_H */
