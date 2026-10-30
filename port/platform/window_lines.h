/*
 * port/platform/window_lines.h
 *
 * Log line writers of the window's 10 s block (window_host.c is at its file
 * size gate): the slow-replay line, the memory line and the late step's
 * held-sound line.  They read the renderer's records, the RHI's counters
 * and the step's profile only; no window state.
 */
#ifndef ICO_PLATFORM_WINDOW_LINES_H
#define ICO_PLATFORM_WINDOW_LINES_H

#include "rd.h"

/* A replay over a refresh: where its time went, with its own counters.
   `note` ends the line ("" or "; the block's longest, ...") */
void window_slow_replay_line(const RdPerfRecord *r, unsigned vsync, const char *note);

/* The block's "window: memory:" line: the graphics allocations, the
   process's and the device's memory, the pipeline pool */
void window_memory_line(void);

/* Once a block: writes the line "video memory N MB of M MB: the driver may
   be paging; ..." the first time the card's local memory use is at 90 % of
   its budget or more in the block (Direct3D 11 and 12; nothing elsewhere).
   Called from the perf drain and at the block's end */
void window_video_memory_watch(void);

/* Every step (window_host.c ico_window_pace): a step that ended more than
   HOLD_LATE_MS late (lateMs: its time past the refresh period) while the
   audio held sound back (heldMs: ico_audio_sdl_held_ms, rising since the
   last call) names what the game was doing in it, from the step's profile
   (host_loop.h) and the decode and pipeline counts since the step began
   (decodes0, pipes0); the first 8 such steps, then every 100th:
     window: the game step was late by 123 ms (loading: 1 disc reads of 64
     sectors, 1 texture decodes, 0 pipelines, achievements 0.1 ms): the
     sound was held back for 98 ms (1 such steps) */
void window_hold_line(double lateMs, unsigned long long heldMs, unsigned decodes0, unsigned pipes0);

/* At a 10 s block's end (window_host.c pace_log): the audio held sound back
   for over HOLD_BLOCK_MS in the block (heldMs, the block's own) while no
   step was over HOLD_LATE_MS late (stepMaxLateMs: the longest step against
   the period), so window_hold_line named none: a steady shortfall, the
   steps longer than their period on average (a picture slower than the
   step at Frame rate Original before its late skip).  Names the steps'
   average, the period, the frame rate (rateWords: "Original", or the
   option's name) and the replay's average; the first 8 such blocks, then
   every 30th:
     window: the game step was late (steady: steps average 31.2 ms against a
     period of 20.0 ms at Original (the picture takes 29.8 ms on this GPU)):
     the sound was held back for 640 ms in this block (1 such blocks) */
void window_hold_block_line(unsigned long long heldMs, double stepAvgMs, double stepMaxLateMs,
                            double periodMs, double pictureMs, const char *rateWords);

/* Frame rate Original's late skip (pace_policy.h pace_original_present),
   kept here so window_host.c stays under the repository's file size cap.
   window_orig_gate: the replay gate's answer (rd_set_replay_gate) for a
   frame of the step due at dueNs at the simulation's rate hz (50: 20 ms,
   else 16.683 ms), counting the frames it leaves out.  window_orig_step:
   after each Original pace, whether the step ran past its period and a
   whole frame's replay time (below 0: none replayed in the step).
   window_orig_words: the 10 s line's "; steps late N (frames not presented
   M)" (empty unless original; a static buffer).  window_hold_block: at the
   block's end, window_hold_block_line from the sound held back since the
   last block (the first: since the open), the steps' time (sum, count,
   longest, in ms), the picture's time (the Original replays' mean, else
   replaySumMs / replays) and the frame rate's words; then the counts start
   again. */
bool window_orig_gate(int keep, uint64_t nowNs, uint64_t dueNs, int hz);
void window_orig_step(bool late, double replayMs);
const char *window_orig_words(bool original);
void window_hold_block(double stepSumMs, unsigned steps, double stepMaxMs, int hz,
                       double replaySumMs, unsigned replays, bool original, const char *rateName);

#endif /* ICO_PLATFORM_WINDOW_LINES_H */
