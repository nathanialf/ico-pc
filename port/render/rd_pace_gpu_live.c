/* rd_pace_gpu_live.c: the GPU queue's model in the renderer (rd_pace_gpu.h).
 *
 * The replaying thread hands each handed-over picture's measurement over
 * a single-producer ring (rd_pace_gpu_measure, from rd_perf.c at the
 * record's collection); the poster's thread takes them before it places
 * the next picture (rd_pace_gpu_shift, from rd_thread.c presentRebase) and
 * owns the model.  The attempt ledger's row is decided here at each change
 * and the start line written then (the first 8 changes, then every 100th). */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_pace_gpu.h"

#define QCAP 32u

static RdPaceGpuSample s_q[QCAP];
static uint32_t s_head, s_tail; /* __atomic: the producer's, the consumer's */

void rd_pace_gpu_measure(const RdPaceGpuSample *s)
{
    if (!(s->handMs > 0.0)) {
        return;
    }
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= QCAP) {
        rd_pace_gpu_tally_drop();
        return;
    }
    s_q[head % QCAP] = *s;
    __atomic_store_n(&s_head, head + 1u, __ATOMIC_RELEASE);
}

/* the poster's */
static RdPaceGpu s_m;
static bool s_clock;       /* a span and a first submit came back this run */
static uint32_t s_noClock; /* pictures as made before one did */
static int s_state = -1;   /* the row as last decided: -1 none, 0 paced, 1 on, 2 no clock */
static uint32_t s_changes; /* for the start line's limit */

static void drain(void)
{
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    while (tail != head) {
        const RdPaceGpuSample smp = s_q[tail % QCAP];
        tail++;
        RdPaceGpuNoted nd;
        rd_pace_gpu_model_note(&s_m, &smp, &nd);
        if (nd.errValid) {
            rd_pace_gpu_tally_err(nd.errMs);
        }
        if (smp.spanMs > 0.0 && smp.submitMs > 0.0) {
            s_clock = true;
        }
    }
    __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
}

static void decide(bool asMade)
{
    const bool waited = s_noClock >= RD_PACE_GPU_NO_CLOCK;
    const int st = !asMade ? 0 : (s_clock ? 1 : (waited ? 2 : -1));
    if (st < 0 || st == s_state) {
        return;
    }
    s_state = st;
    ico_attempt_decide_pace_gpu(asMade ? 1 : 0, s_clock ? 1 : 0, waited ? 1 : 0);
    const uint32_t k = s_changes++;
    if (k < 8u || k % 100u == 0u) {
        char line[1024];
        (void)rd_pace_gpu_start_text(st == 1, ico_attempt_why_of(ICO_ATT_PACE_GPU), line,
                                     sizeof line);
        fprintf(stderr, "%s\n", line);
    }
}

double rd_pace_gpu_shift(double handMs, uint32_t frame)
{
    drain();
    if (!s_clock && s_noClock < RD_PACE_GPU_NO_CLOCK) {
        s_noClock++;
    }
    decide(true);
    RdPaceGpuPlace p;
    rd_pace_gpu_model_place(&s_m, handMs, frame, &p);
    rd_pace_gpu_tally(&p);
    if (p.how == RD_PACE_GPU_PLACED) {
        ico_attempt_fire(ICO_ATT_PACE_GPU, 1u);
        if (p.atBound) {
            ico_attempt_fire2(ICO_ATT_PACE_GPU, 1u);
        }
    }
    return p.shiftMs;
}

void rd_pace_gpu_paced(void)
{
    /* the measurements dropped unread: the model starts again when the
       pictures are made as fast as the GPU allows again */
    __atomic_store_n(&s_tail, __atomic_load_n(&s_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    if (s_m.have || s_m.spanN != 0u || s_m.leadN != 0u || s_m.latN != 0u) {
        rd_pace_gpu_model_reset(&s_m);
    }
    decide(false);
}
