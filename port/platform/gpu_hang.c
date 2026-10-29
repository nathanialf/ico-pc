/* port/platform/gpu_hang.c: the GPU hang limit (gpu_hang.h). */
#include "gpu_hang.h"

double ico_gpu_hang_limit_s(unsigned long watchdog_s)
{
    double s;

    if (watchdog_s == 0) {
        return 0.0;
    }
    s = (double)watchdog_s / 2.0;
    return s < ICO_GPU_HANG_MIN_S ? ICO_GPU_HANG_MIN_S : s;
}

int ico_gpu_hang_step(IcoGpuHangWatch *w, double limit_s, unsigned long long waiting_for,
                      unsigned long long finished, double dt_s, int count)
{
    if (limit_s <= 0.0) {
        return 0;
    }
    if (waiting_for == 0 || waiting_for <= finished) {
        /* no wait, or its submission finished: nothing is hung */
        w->waiting_for = 0;
        w->counted_s = 0.0;
        return 0;
    }
    if (waiting_for != w->waiting_for) {
        /* a wait for another submission: its time counts from this poll */
        w->waiting_for = waiting_for;
        w->counted_s = 0.0;
        return 0;
    }
    if (count && dt_s > 0.0) {
        w->counted_s += dt_s;
    }
    if (!w->fired && w->counted_s >= limit_s) {
        w->fired = 1;
        return 1;
    }
    return 0;
}
