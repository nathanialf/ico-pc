/* vk_tsclock.c: the recycled frame slot's clock (rhi.h
 * rhi_read_timestamp_clock).
 *
 * Issue 123 (Adreno 740, Qualcomm's driver): presents that blocked about
 * 340 ms while the GPU finished nothing, the timestamp counter restarted
 * before the stalled replay's first stamp and no device was lost.  The
 * stamps alone cannot say when the GPU began that replay (the counter's
 * restart breaks the comparison with the replay before) nor whether the
 * GPU ran part of it, stopped, and ran it again (a driver's reset and
 * replay of the work in flight).  Three things here can:
 *
 *   - the slot's submit times (vkr_tsclock_note_submit, from
 *     vkr_submit_batch) and, where VK_KHR_calibrated_timestamps or
 *     VK_EXT_calibrated_timestamps is offered with the device's and the
 *     CPU's time domains, one calibrated pair of clocks read at the slot's
 *     recycle: a stamp t was then written at cpuNs - (gpuNs - t) on the
 *     CPU's clock (Vulkan spec, "Time Domains and Calibrated Timestamps");
 *     the renderer compares that with the submit;
 *   - while the recycle's wait for the slot blocks past
 *     RHI_TS_TRACE_AFTER_MS, the slot's query results polled every
 *     RHI_TS_TRACE_STEP_MS (vkGetQueryPoolResults without the wait bit:
 *     "may return VK_NOT_READY if there are queries in the unavailable
 *     state", spec "Query Operation"), one trace point per change: which
 *     stamps were available, whether they were still the previous use's (the
 *     slot's reset, the first command of its first list, not yet run), how
 *     far its submissions had finished;
 *   - the fall: the available count going down after the slot's own
 *     results had begun to come means its queries were reset again, which
 *     only a second run of its first list does.
 *
 * Nothing here changes what is drawn or when: the wait returns when the
 * same number finishes as before (the poll only splits it into timed
 * waits), and nothing is recorded into a command buffer.  Enabling the
 * calibrated-timestamps extension also extends the spec's ordering of
 * timestamps to every submission on the device ("If VK_KHR_calibrated_
 * timestamps or VK_EXT_calibrated_timestamps is enabled, this extends to
 * timestamp writes across all submissions on the same logical device";
 * without it only writes in the same submission are ordered).
 *
 * The single wait (VkrState.tile.polled false: a tile-based GPU by default
 * since v0.11.4, [dev] polled_wait = off, or two lost starts in a row;
 * port/rhi/rhi_tile_rule.h, issues 179 and 211) is v0.11.2's: one wait
 * without a time limit, no read of the slot's queries or of the counter
 * while its work runs, and the calibrated-timestamps extension not enabled
 * (vkr_tsclock_pick).  The submit times and the wait's CPU times are still
 * kept; the trace, the still stretch, the fall and the calibrated pair are
 * not (the device's "submission wait" line says so). */
#include "vk_internal.h"
#include "rhi_vk.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* why calibration is off, for the device's line */
static char s_why[96] = "not offered";

/* the time domain the CPU's clock (vkr_now_ns) reads */
static VkTimeDomainKHR cpu_domain(void)
{
#ifdef _WIN32
    return VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#else
    return VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
#endif
}

static const char *domain_name(VkTimeDomainKHR d)
{
    switch (d) {
    case VK_TIME_DOMAIN_DEVICE_KHR:
        return "device";
    case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
        return "CLOCK_MONOTONIC";
    case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
        return "CLOCK_MONOTONIC_RAW";
    case VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR:
        return "the performance counter";
    default:
        return "another";
    }
}

const char *vkr_tsclock_pick(bool hasKhr, bool hasExt)
{
    g_vkr.tsCalib = false;
    g_vkr.tsCalibExt = NULL;
    g_vkr.getCalibratedTimestamps = NULL;
    snprintf(s_why, sizeof s_why, "not offered");
    const char *env = getenv("ICO_VK_CALIBRATED_TIMESTAMPS");
    if (env && strcmp(env, "0") == 0) {
        snprintf(s_why, sizeof s_why, "off (ICO_VK_CALIBRATED_TIMESTAMPS=0)");
        return NULL;
    }
    if (!g_vkr.tile.polled) {
        /* v0.11.2's device: the extension not enabled with the single wait
           (rhi_tile_rule.h; the submission wait's line says why) */
        snprintf(s_why, sizeof s_why, "off (the single submission wait: %s)", g_vkr.tile.polledTag);
        return NULL;
    }
    if (!hasKhr && !hasExt) {
        return NULL;
    }
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR q =
        (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR)vkGetInstanceProcAddr(
            g_vkr.instance, hasKhr ? "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR"
                                   : "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
    uint32_t n = 0;
    if (!q || q(g_vkr.phys, &n, NULL) != VK_SUCCESS || n == 0) {
        snprintf(s_why, sizeof s_why, "offered, its time domains not readable");
        return NULL;
    }
    VkTimeDomainKHR d[16];
    if (n > 16u) {
        n = 16u;
    }
    const VkResult r = q(g_vkr.phys, &n, d);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        snprintf(s_why, sizeof s_why, "offered, its time domains not readable");
        return NULL;
    }
    bool dev = false, cpu = false;
    for (uint32_t i = 0; i < n; i++) {
        dev = dev || d[i] == VK_TIME_DOMAIN_DEVICE_KHR;
        cpu = cpu || d[i] == cpu_domain();
    }
    if (!dev || !cpu) {
        snprintf(s_why, sizeof s_why, "offered without the %s domain (%u domains)",
                 dev ? domain_name(cpu_domain()) : "device", n);
        return NULL;
    }
    g_vkr.tsCalibCpuDomain = (uint32_t)cpu_domain();
    g_vkr.tsCalibExt = hasKhr ? VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME
                              : VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    return g_vkr.tsCalibExt;
}

/* vkr_tsclock_recycled's calibrated reads still to make (1 at a device's
 * start: its first recycle) */
static uint32_t s_calibLeft = 1u;

void vkr_tsclock_init(uint32_t validBits)
{
    s_calibLeft = 1u;
    g_vkr.tsValidBits = validBits;
    if (g_vkr.tsCalibExt != NULL) {
        const bool khr = strcmp(g_vkr.tsCalibExt, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0;
        g_vkr.getCalibratedTimestamps = (PFN_vkGetCalibratedTimestampsKHR)vkGetDeviceProcAddr(
            g_vkr.device, khr ? "vkGetCalibratedTimestampsKHR" : "vkGetCalibratedTimestampsEXT");
        g_vkr.tsCalib = g_vkr.getCalibratedTimestamps != NULL && g_vkr.timestamps;
        if (!g_vkr.tsCalib) {
            snprintf(s_why, sizeof s_why, "enabled, %s",
                     g_vkr.timestamps ? "its entry point missing" : "no timestamps");
        }
    }
    if (!g_vkr.timestamps) {
        VKR_LOG("timestamps: none on the queue (timestampValidBits %u)", validBits);
        return;
    }
    if (g_vkr.tsCalib) {
        VKR_LOG("timestamps: %u valid bits, %.3f ns a tick; calibrated with the CPU's clock: on "
                "(%s, the device and %s): a slow replay's stamps are placed on the CPU's clock "
                "against its submit",
                validBits, (double)g_vkr.timestampPeriod, g_vkr.tsCalibExt,
                domain_name((VkTimeDomainKHR)g_vkr.tsCalibCpuDomain));
    } else {
        VKR_LOG("timestamps: %u valid bits, %.3f ns a tick; calibrated with the CPU's clock: %s "
                "(a slow replay's start is placed from its wait instead)",
                validBits, (double)g_vkr.timestampPeriod, s_why);
    }
}

void vkr_tsclock_note_submit(VkrFrame *f, uint64_t value)
{
    const uint64_t now = vkr_now_ns();
    if (f->tsSubmits == 0) {
        f->tsFirstSubmitNs = now;
        f->tsFirstValue = value;
    }
    f->tsLastSubmitNs = now;
    f->tsSubmits++;
}

static uint64_t tick_ns(uint64_t raw)
{
    return (uint64_t)((double)(raw & g_vkr.timestampMask) * g_vkr.timestampPeriod);
}

/* the poll's state over one wait */
static RhiTsTracePoint s_prev;
static bool s_havePrev;
static uint32_t s_lastChangeUs;

/* one look at the slot's results while its wait blocks (the wait's own
 * thread: the slot's lists were all submitted, nothing records into it) */
static void poll(VkrFrame *f, uint64_t t0)
{
    RhiTimestampClock *k = &g_vkr.tsClock;
    uint32_t hi = 0;
    for (uint32_t i = 0; i < RHI_MAX_TIMESTAMPS; i++) {
        if (rhi_ts_has(f->tsWritten, i)) {
            hi = i + 1u;
        }
    }
    if (hi == 0 || !f->queryPool) {
        return;
    }
    uint64_t data[RHI_MAX_TIMESTAMPS][2];
    memset(data, 0, sizeof(data[0]) * hi);
    const VkResult r = vkGetQueryPoolResults(
        g_vkr.device, f->queryPool, 0, hi, sizeof(data[0]) * hi, data, sizeof(data[0]),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r != VK_SUCCESS && r != VK_NOT_READY) {
        return;
    }
    RhiTsTracePoint p;
    memset(&p, 0, sizeof(p));
    const uint64_t at = (vkr_now_ns() - t0) / 1000u;
    p.atUs = at > UINT32_MAX ? UINT32_MAX : (uint32_t)at;
    bool first = true;
    uint64_t firstRaw = 0;
    for (uint32_t i = 0; i < hi; i++) {
        if (!rhi_ts_has(f->tsWritten, i)) {
            continue;
        }
        if (data[i][1]) {
            p.avail++;
            rhi_ts_add(p.bits, i);
            if (first) {
                firstRaw = data[i][0] & g_vkr.timestampMask;
            }
        }
        first = false;
    }
    p.firstNs = firstRaw ? tick_ns(firstRaw) : 0;
    p.stale = (uint8_t)(firstRaw != 0 && firstRaw == f->tsPrevFirst);
    uint64_t done = 0;
    if (vkr_sync_reached(&done)) {
        p.finished = done >= f->waitValue                         ? 2u
                     : f->tsFirstValue && done >= f->tsFirstValue ? 1u
                                                                  : 0u;
    }
    const bool changed = !s_havePrev || p.avail != s_prev.avail || p.stale != s_prev.stale ||
                         p.finished != s_prev.finished ||
                         memcmp(p.bits, s_prev.bits, sizeof(p.bits)) != 0;
    if (s_havePrev && !s_prev.stale && s_prev.avail > 0 && !p.stale && p.avail < s_prev.avail) {
        /* the slot's own results going away: its reset ran again */
        if (!k->again) {
            k->againAtUs = p.atUs;
            k->againFrom = s_prev.avail;
        }
        k->again = true;
    }
    if (changed) {
        s_lastChangeUs = p.atUs;
        if (k->traceN < RHI_TS_TRACE_MAX) {
            k->trace[k->traceN++] = p;
        } else {
            k->trace[RHI_TS_TRACE_MAX - 1] = p; /* the first changes and the latest */
            k->traceMore++;
        }
    } else if (p.atUs - s_lastChangeUs > k->stillUs) {
        k->stillUs = p.atUs - s_lastChangeUs;
        k->stillFromUs = s_lastChangeUs;
        k->stillAvail = p.avail;
        k->stillStale = p.stale;
    }
    s_prev = p;
    s_havePrev = true;
}

VkResult vkr_tsclock_wait(VkrFrame *f, uint64_t value)
{
    RhiTimestampClock *k = &g_vkr.tsClock;
    const uint64_t t0 = vkr_now_ns();
    k->waitBeginNs = t0;
    /* the single wait (VkrState.tile.polled false, rhi_tile_rule.h): one
       wait without a time limit and no read of the slot while its work is
       in flight, as v0.11.2; the CPU's times of the wait are kept */
    if (!g_vkr.timestamps || !f->queryPool || !g_vkr.tile.polled) {
        const VkResult r = vkr_sync_wait(value);
        k->waitEndNs = vkr_now_ns();
        return r;
    }
    s_havePrev = false;
    s_lastChangeUs = 0;
    VkResult r = vkr_sync_wait_for(value, (uint64_t)RHI_TS_TRACE_AFTER_MS * 1000000u);
    while (r == VK_TIMEOUT) {
        poll(f, t0);
        r = vkr_sync_wait_for(value, (uint64_t)RHI_TS_TRACE_STEP_MS * 1000000u);
    }
    k->waitEndNs = vkr_now_ns();
    return r;
}

static uint64_t cpu_ns(uint64_t v)
{
#ifdef _WIN32
    LARGE_INTEGER fr;
    QueryPerformanceFrequency(&fr);
    return (uint64_t)((double)v * 1e9 / (double)fr.QuadPart); /* as vkr_now_ns */
#else
    return v; /* CLOCK_MONOTONIC in ns */
#endif
}

/* The calibrated pair costs a driver call a recycle (on a phone perhaps a
 * kernel call), and a line wants it only where it places a replay's
 * stamps on the CPU's clock (port/render/rd_perf_clock.c: a blocked wait,
 * a second run, unwritten stamps, a counter restart).  So it is read at
 * the first recycle of a device (a reference), at a recycle whose wait
 * blocked past RHI_TS_TRACE_AFTER_MS, and at the RHI_FRAMES_IN_FLIGHT + 1
 * recycles after one: the replays that were in flight through the stall,
 * among them the one whose counter restarted (every restart in issue 123's
 * logs came with such a stall).  s_calibLeft (above vkr_tsclock_init):
 * the recycles still to read it at */
void vkr_tsclock_recycled(VkrFrame *f)
{
    RhiTimestampClock *k = &g_vkr.tsClock;
    k->validBits = g_vkr.tsValidBits;
    k->submits = f->tsSubmits;
    k->firstSubmitNs = f->tsFirstSubmitNs;
    k->lastSubmitNs = f->tsLastSubmitNs;
    const bool blocked =
        k->waitBeginNs != 0 && k->waitEndNs >= k->waitBeginNs &&
        k->waitEndNs - k->waitBeginNs >= (uint64_t)RHI_TS_TRACE_AFTER_MS * 1000000u;
    if (blocked) {
        s_calibLeft = RHI_FRAMES_IN_FLIGHT + 2u; /* this recycle and the ones after it */
    }
    const bool calib = s_calibLeft > 0u;
    if (calib) {
        s_calibLeft--;
    }
    if (calib && g_vkr.tsCalib && !g_vkr.deviceLost) {
        const VkCalibratedTimestampInfoKHR in[2] = {
            {.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
             .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR},
            {.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
             .timeDomain = (VkTimeDomainKHR)g_vkr.tsCalibCpuDomain},
        };
        uint64_t v[2] = {0, 0}, dev = 0;
        if (g_vkr.getCalibratedTimestamps(g_vkr.device, 2, in, v, &dev) == VK_SUCCESS) {
            k->calibrated = true;
            k->gpuNs = tick_ns(v[0]);
            k->cpuNs = cpu_ns(v[1]);
            k->deviationNs = dev;
        }
    }
    f->tsSubmits = 0;
    f->tsFirstSubmitNs = f->tsLastSubmitNs = f->tsFirstValue = 0;
}

bool rhi_read_timestamp_clock(RhiTimestampClock *out)
{
    if (!out) {
        return false;
    }
    if (!g_vkr.timestamps) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    *out = g_vkr.tsClock;
    return true;
}
