/*
 * port/platform/slow_judge.c
 *
 * What slow pictures and slow steps are judged against (slow_judge.h).
 */
#include "slow_judge.h"

#include <stdio.h>

static size_t written(int w, size_t cap)
{
    if (w < 0 || cap == 0) {
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

uint64_t slow_judge_interval_ns(uint64_t refreshNs, uint64_t pictureNs)
{
    return pictureNs > refreshNs ? pictureNs : refreshNs;
}

double slow_judge_replay_ms(uint64_t refreshNs, uint64_t pictureNs)
{
    return (double)slow_judge_interval_ns(refreshNs, pictureNs) / 1e6;
}

double slow_judge_step_ms(double keyMs, uint64_t refreshNs, uint64_t pictureNs)
{
    if (!(keyMs > 0.0) || refreshNs == 0) {
        return keyMs;
    }
    return keyMs * (double)slow_judge_interval_ns(refreshNs, pictureNs) / (double)refreshNs;
}

double slow_judge_target_hz(uint64_t refreshNs, uint64_t pictureNs)
{
    const uint64_t i = slow_judge_interval_ns(refreshNs, pictureNs);

    return i ? 1e9 / (double)i : 0.0;
}

size_t slow_judge_step_cpu_text(char *out, size_t cap, double wallMs, double cpuMs)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!(cpuMs > 0.0)) {
        return 0;
    }
    const double off = wallMs > cpuMs ? wallMs - cpuMs : 0.0;

    return written(
        snprintf(out, cap, " (CPU %.1f ms of it; %.1f ms off the CPU: %s)", cpuMs, off,
                 off > cpuMs ? "the thread waited or was not scheduled" : "the thread's own work"),
        cap);
}

double slow_judge_game_fps(unsigned frames, uint64_t firstNs, uint64_t closeNs, double wallS)
{
    if (firstNs != 0 && closeNs > firstNs && frames > 0) {
        return (double)frames * 1e9 / (double)(closeNs - firstNs);
    }
    return wallS > 0.0 ? (double)frames / wallS : 0.0;
}

size_t slow_judge_text(char *out, size_t cap, uint64_t refreshNs, uint64_t pictureNs, double keyMs)
{
    if (cap == 0) {
        return 0;
    }
    const double judge = slow_judge_replay_ms(refreshNs, pictureNs);
    const double r = (double)refreshNs / 1e6, p = (double)pictureNs / 1e6;
    char step[96];

    if (!(keyMs > 0.0)) {
        snprintf(step, sizeof(step), "slow steps off ([dev] slow_step_ms 0)");
    } else if (refreshNs == 0) {
        snprintf(step, sizeof(step), "slow steps against %.1f ms ([dev] slow_step_ms)", keyMs);
    } else {
        snprintf(step, sizeof(step),
                 "slow steps against %.1f ms ([dev] slow_step_ms %.1f scaled by %.1f / %.1f)",
                 slow_judge_step_ms(keyMs, refreshNs, pictureNs), keyMs, judge, r);
    }
    return written(snprintf(out, cap,
                            "slow pictures judged against %.1f ms (the larger of the display's "
                            "refresh %.1f ms and the picture interval in force %.1f ms), %s",
                            judge, r, p, step),
                   cap);
}
