/* port/platform/gpu_hang.c: the GPU hang limit (gpu_hang.h). */
#include "gpu_hang.h"
#include <string.h>

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

int ico_render_hang_step(IcoRenderHangWatch *r, double limit_s, unsigned long long seq,
                         unsigned long long done, unsigned mark, int creating, double dt_s,
                         int count)
{
    if (!r->seen || mark != r->mark || creating) {
        /* the driver made progress, or is inside a creation: the picture's
           time counts from this poll (the watch's fired mark kept) */
        r->seen = 1;
        r->mark = mark;
        r->w.waiting_for = 0;
        r->w.counted_s = 0.0;
        (void)ico_gpu_hang_step(&r->w, limit_s, seq, done, 0.0, 0);
        return 0;
    }
    return ico_gpu_hang_step(&r->w, limit_s, seq, done, dt_s, count);
}

int ico_pipe_note_creating(const char *note, unsigned bg_left)
{
    if (note == NULL || strncmp(note, "creating pipeline", 17) != 0) {
        return 0;
    }
    /* the background set's note: a creation only while its compiles run
       (a batch whose compiles have all returned waits for the next
       presented picture to take it, and nothing is inside the driver) */
    if (strstr(note, " in the background: ") != NULL) {
        return bg_left > 0;
    }
    return 1;
}

double ico_pipe_stall_step(IcoPipeStallWatch *w, double now, int creating, int counted, int on,
                           unsigned seq, unsigned done, unsigned bg_done)
{
    if (!creating || !on || seq != w->seq || done != w->done || bg_done != w->bg_done) {
        w->held_s = 0.0; /* another creation, or none: the memory rule's hold anew */
    }
    if (!creating || !counted || !on || seq != w->seq || done != w->done || bg_done != w->bg_done) {
        w->seq = seq;
        w->done = done;
        w->bg_done = bg_done;
        w->since = now;
        return 0.0;
    }
    return now - w->since;
}

double ico_pipe_stall_limit_s(int in_play, int serial_compiler)
{
    if (in_play) {
        return ICO_PIPE_STALL_PLAY_S;
    }
    return serial_compiler ? ICO_PIPE_STALL_START_SERIAL_S : ICO_PIPE_STALL_START_S;
}

int ico_pipe_mem_moving(long prev_kb, long now_kb)
{
    return prev_kb >= 0 && now_kb >= 0 && now_kb - prev_kb >= ICO_PIPE_MEM_MOVING_KB;
}

int ico_pipe_mem_hold(IcoPipeStallWatch *w, double now, int creating, int moving, int on,
                      int memory_low, double limit_s)
{
    const double dt = w->last > 0.0 && now > w->last ? now - w->last : 0.0;
    int hold;

    w->last = now;
    hold =
        creating && moving && on && !memory_low && w->held_s < ICO_PIPE_MEM_HOLD_LIMITS * limit_s;
    if (hold) {
        w->held_s += dt;
    }
    return hold;
}

int ico_pipe_stall_next_differs(int in_selftest, int lazy_run, int skip_family, int vu_note,
                                int vu_plain_already)
{
    if (skip_family || (vu_note && !vu_plain_already)) {
        return 1;
    }
    return !in_selftest && !lazy_run;
}
