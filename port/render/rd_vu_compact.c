/* rd_vu_compact.c: the index compaction's plan, its keep stage's pipeline,
 * the class's decision, the counters' readback and the lines
 * (rd_vu_compact.h says what and why). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "attempt_ledger.h"
#include "rd_vu_compact.h"
#include "rd_vu_class.h"
#include "rd_stencil_selftest.h" /* rd__profile_powervr */
#include "rd_vu_rec_early.h"     /* rd__vu_rec_release */
#include "rd_vu_record.h"
#include "rd_vu_skin_check.h" /* rd__vu_prepass_selftest_running */

/* Arm's PCI vendor id (the Mali-G52 of issue 179; rd_profile.c VENDOR_ARM) */
#define CMP_VENDOR_ARM 0x13B5u

/* ------------------------------------------------------------ the plan */

void rd_vu_cmp_plan_reset(RdVuCmpPlan *p, uint32_t cap, uint32_t maxValue, bool raise)
{
    memset(p, 0, sizeof(*p));
    p->cap = cap;
    p->maxValue = maxValue;
    p->raise = raise;
}

bool rd_vu_cmp_plan_call(RdVuCmpPlan *p, const uint32_t *src, uint32_t first, uint32_t count,
                         uint8_t pass, uint8_t list0, RdVuCmpCall *out)
{
    /* its list after the plan's, the counts from the next whole texel: a
       texel each planned workgroup, this call's too */
    const uint32_t groups = (count / 3u + RD_VU_CMP_GROUP - 1u) / RD_VU_CMP_GROUP;
    const uint64_t end = ((uint64_t)p->used + count + 3u) / 4u + p->groups + groups;
    if (src == NULL || count < 3u || count % 3u != 0u || end * 4u > (uint64_t)p->cap) {
        p->refused++;
        return false;
    }
    uint32_t lo = UINT32_MAX, hi = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = src[first + i];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    /* raised: the call's values from top on, past every value planned
       before (the merge's runs read them in the plan's order); its last
       raised value within the device's */
    uint32_t raise = 0;
    if (p->raise) {
        const uint64_t last = p->top + (uint64_t)(hi - lo);
        if (last > (uint64_t)p->maxValue) {
            p->refused++;
            return false;
        }
        raise =
            (uint32_t)(p->top - (uint64_t)lo); /* mod 2^32: the shader adds, the merge subtracts */
        p->top = last + 1u;
    } else if (hi > p->maxValue) {
        p->refused++;
        return false;
    }
    out->first = first;
    out->count = count;
    out->at = p->used;
    out->group = p->groups;
    out->lo = lo;
    out->hi = hi;
    out->raise = raise;
    out->pass = pass;
    out->list0 = list0;
    p->used += count;
    p->groups += groups;
    p->calls++;
    return true;
}

uint32_t rd_vu_cmp_counts_base(const RdVuCmpPlan *p)
{
    return (uint32_t)(((uint64_t)p->used + 3u) / 4u);
}

/* ---------------------------------------------------------- the counts */

void rd_vu_cmp_counts_add(RdVuCmpCounts *acc, const uint32_t *texels, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t *t = texels + 4u * i;
        const int g = t[3] != 0u ? 0 : 1;
        acc->drop[g] += t[0];
        acc->outside[g] += t[1];
        acc->kept[g] += t[2];
    }
    acc->pictures++;
}

uint64_t rd_vu_cmp_left_out(const RdVuCmpCounts *c)
{
    return c->drop[0] + c->outside[0] + c->drop[1] + c->outside[1];
}

/* snprintf into out at *at (kept under cap) */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static void cmpPut(char *out, size_t cap, size_t *at, const char *fmt, ...)
{
    if (*at >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(out + *at, cap - *at, fmt, ap);
    va_end(ap);
    if (w < 0) {
        return;
    }
    *at = *at + (size_t)w < cap ? *at + (size_t)w : cap - 1u;
}

size_t rd_vu_cmp_block_text(char *out, size_t cap, const RdVuCmpCounts *c)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    size_t at = 0;
    const double n = c->pictures ? (double)c->pictures : 1.0;
    const uint64_t out0 = c->drop[0] + c->outside[0], all0 = out0 + c->kept[0];
    const uint64_t out1 = c->drop[1] + c->outside[1], all1 = out1 + c->kept[1];
    cmpPut(out, cap, &at,
           "window: index compaction: triangles left out as drawing nothing, a picture: %.0f of "
           "%.0f in list 0 (%.1f %%), by class: the game's drops %.0f, outside the target or "
           "the depth range %.0f; the other lists %.0f of %.0f (%.1f %%) (%u pictures read "
           "back, two frame slots behind)",
           (double)out0 / n, (double)all0 / n, all0 ? 100.0 * (double)out0 / (double)all0 : 0.0,
           (double)c->drop[0] / n, (double)c->outside[0] / n, (double)out1 / n, (double)all1 / n,
           all1 ? 100.0 * (double)out1 / (double)all1 : 0.0, c->pictures);
    return at;
}

int rd_vu_cmp_env(void)
{
    const char *e = getenv("ICO_RD_VU_COMPACT");
    if (e == NULL || e[0] == '\0') {
        return -1;
    }
    if (strcmp(e, "0") == 0 || strcmp(e, "off") == 0) {
        return 0;
    }
    if (strcmp(e, "1") == 0 || strcmp(e, "on") == 0) {
        return 1;
    }
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("VU index compaction: ICO_RD_VU_COMPACT=%s is none of 0, 1: unset", e);
    }
    return -1;
}

int rd_vu_cmp_class(const char *name, uint32_t vendor, bool moltenvk, bool dxt, bool byTests)
{
    if (byTests || (!moltenvk && vendor == CMP_VENDOR_ARM && name != NULL &&
                    strstr(name, RD_VU_CMP_G52_NAME) != NULL)) {
        return ICO_ATT_CMP_G52;
    }
    return dxt ? ICO_ATT_CMP_DXT : ICO_ATT_CMP_OTHER;
}

/* ------------------------------------------------------------ the state */

static struct {
    bool plane;      /* the record's fourth plane (from init; dropped with a verdict of off) */
    int cls;         /* ICO_ATT_CMP_*: the device's class */
    uint32_t vendor; /* RhiLimits.vendorId, for the line */
    char name[96];   /* the Vulkan device name, for the line */
    bool armByTests; /* ICO_RD_VU_COMPACT_ARM=1: the Mali-G52's class on this device */
    bool on;         /* to be tried at the start (the keep stage made) */
    bool runs;       /* the decider's verdict (on outside the self-test's replays) */
    bool refused;    /* the self-test refused it for the run */
    int env;         /* ICO_RD_VU_COMPACT */
    int selftest;    /* -1 not run, 0 refused, 1 passed */
    bool sabotage;   /* ICO_RD_VU_COMPACT_SABOTAGE */
    RhiShader cs;
    RhiPipeline pipe;
    /* the counts' readback, a buffer a frame slot (grown by powers of
       two), the texels its last copy holds, not read yet (0: none) */
    RhiBuffer rb[RHI_FRAMES_IN_FLIGHT];
    uint64_t rbBytes[RHI_FRAMES_IN_FLIGHT];
    uint32_t rbTexels[RHI_FRAMES_IN_FLIGHT];
    /* the plan */
    RdVuCmpPlan plan;
    RdVuCmpCall *calls;
    uint32_t cap;
    /* the block's counts (the render thread adds under the lock-free
       exchange below; the window thread takes): the readback's sums */
    RdVuCmpCounts block;
    uint32_t blockLock; /* __atomic: 1 while one side holds block */
    RdVuCmpCounts run;  /* the run's (the tool's) */
    uint32_t lastLeftOut;
    bool ever; /* a copy was recorded this run */
    char why[200];
} s_cmp;

static void blockLock(void)
{
    while (__atomic_exchange_n(&s_cmp.blockLock, 1u, __ATOMIC_ACQUIRE) != 0u) {}
}

static void blockUnlock(void)
{
    __atomic_store_n(&s_cmp.blockLock, 0u, __ATOMIC_RELEASE);
}

static void freeObjects(void)
{
    if (s_cmp.pipe.id) {
        rhi_destroy_pipeline(s_cmp.pipe);
    }
    s_cmp.pipe = (RhiPipeline){0};
    if (s_cmp.cs.id) {
        rhi_destroy_shader(s_cmp.cs);
    }
    s_cmp.cs = (RhiShader){0};
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_cmp.rb[i].id) {
            rhi_destroy_buffer(s_cmp.rb[i]);
        }
        s_cmp.rb[i] = (RhiBuffer){0};
        s_cmp.rbBytes[i] = 0;
        s_cmp.rbTexels[i] = 0;
    }
}

/* the ledger's row (attempt_ledger.c ico_attempt_decide_compaction) from
   the state now; true when the compaction is to run */
static bool decide(void)
{
    const bool compute = g_rd.vuPrepass && rd__vu_record_compute();
    const char *notWhy = !g_rd.vuPrepass             ? "the VU pre-pass is off"
                         : !rd__vu_record_buffered() ? "record path targets: render targets"
                         : !rd__vu_record_compute()  ? "record path stores: vertex shader "
                                                       "stores"
                                                     : NULL;
    return ico_attempt_decide_compaction(compute ? 1 : 0, notWhy, s_cmp.cls, s_cmp.env,
                                         RD_VU_COMPACT_DEFAULT, RD_VU_COMPACT_ARM_DEFAULT,
                                         s_cmp.selftest) != 0;
}

/* the class the decision was taken for, and the define that decided it
   (the reporter's log names the class on every device) */
static void startLine(const char *what)
{
    const char *why = ico_attempt_why_of(ICO_ATT_COMPACTION);
    char cls[256];
    if (s_cmp.cls == ICO_ATT_CMP_G52) {
        snprintf(cls, sizeof cls,
                 "the Mali-G52 (%s, vendor 0x%04X%s): RD_VU_COMPACT_ARM_DEFAULT %d", s_cmp.name,
                 (unsigned)s_cmp.vendor, s_cmp.armByTests ? ", ICO_RD_VU_COMPACT_ARM=1" : "",
                 RD_VU_COMPACT_ARM_DEFAULT);
    } else if (s_cmp.cls == ICO_ATT_CMP_DXT) {
        snprintf(cls, sizeof cls, "the PowerVR D-Series DXT-48-1536 (%s): RD_VU_COMPACT_DEFAULT %d",
                 s_cmp.name, RD_VU_COMPACT_DEFAULT);
    } else {
        snprintf(cls, sizeof cls,
                 "not the Mali-G52 or the PowerVR D-Series DXT-48-1536 (%s, vendor 0x%04X): off "
                 "by default",
                 s_cmp.name, (unsigned)s_cmp.vendor);
    }
    rd__log("VU index compaction: %s (%s; %s)", what, why ? why : "", cls);
}

/* the fourth plane given up (the verdict is off): the record made again
   without it at the next replay's growth (rd_replay_vu.c rd__vu_rec_release:
   the destroys deferred past the frames that read it) */
static void dropPlane(void)
{
    if (s_cmp.plane) {
        s_cmp.plane = false;
        rd__vu_rec_release();
    }
}

void rd__vu_compact_init(const RhiLimits *lim)
{
    freeObjects();
    /* the Mali-G52 by its Vulkan device name ("Mali-G52 MC2" in issue
       179's log) under Arm's vendor id as the device reports it
       (RhiLimits.vendorId; the tile rule's ICO_VK_FAKE_VENDOR changes that
       rule alone, vk_device.c), or the tests' ICO_RD_VU_COMPACT_ARM=1 on
       any device; the PowerVR D-Series DXT-48-1536 by the PowerVR rule's
       name (rd_vu_class.h) */
    const char *armEnv = getenv("ICO_RD_VU_COMPACT_ARM");
    s_cmp.armByTests = armEnv != NULL && strcmp(armEnv, "1") == 0;
    s_cmp.vendor = lim != NULL ? lim->vendorId : 0u;
    const char *name = lim != NULL ? rhi_adapter_name() : NULL;
    snprintf(s_cmp.name, sizeof s_cmp.name, "%s", name != NULL ? name : "no device name");
    const bool dxt = lim != NULL && rd__profile_powervr() && rd__vu_class_measured(name);
    s_cmp.cls = lim != NULL
                    ? rd_vu_cmp_class(name, lim->vendorId, lim->moltenvk, dxt, s_cmp.armByTests)
                    : ICO_ATT_CMP_OTHER;
    s_cmp.env = rd_vu_cmp_env();
    {
        const char *sb = getenv("ICO_RD_VU_COMPACT_SABOTAGE");
        s_cmp.sabotage = sb != NULL && sb[0] != '\0' && sb[0] != '0';
        if (s_cmp.sabotage) {
            rd__log("VU index compaction: ICO_RD_VU_COMPACT_SABOTAGE: every triangle written as "
                    "a filler (the self-test must refuse it)");
        }
    }
    s_cmp.selftest = -1;
    s_cmp.refused = false;
    /* the row before the self-test (off: not run yet, but under the tests'
       switch); tried by the self-test where it would run were the test to
       pass */
    s_cmp.runs = lim != NULL && decide();
    s_cmp.on = lim != NULL && ico_attempt_compaction_candidate(
                                  g_rd.vuPrepass && rd__vu_record_compute(), s_cmp.cls, s_cmp.env,
                                  RD_VU_COMPACT_DEFAULT, RD_VU_COMPACT_ARM_DEFAULT) != 0;
    s_cmp.plane = false;
    if (s_cmp.on) {
        /* group 0 FrameCB and group 1's store layout, as the record's
           compute builds (rd_pipeline.c) */
        s_cmp.cs = rd__make_shader("vu_tri_keep_compute_cs");
        RhiBindGroupLayout layouts[2] = {g_rd.layoutFrame, g_rdRec.layoutVuPre};
        if (s_cmp.cs.id && layouts[0].id && layouts[1].id) {
            RhiPipelineDesc d;
            memset(&d, 0, sizeof(d));
            d.compute = s_cmp.cs;
            d.layouts = layouts;
            d.layoutCount = 2;
            d.debugName = "rd vu index compaction (compute)";
            s_cmp.pipe = rhi_create_pipeline(&d);
        }
        if (!s_cmp.pipe.id) {
            ico_attempt_state(ICO_ATT_COMPACTION, ICO_ATT_NA,
                              s_cmp.cs.id ? "the keep stage's pipeline could not be made"
                                          : "the keep stage's module vu_tri_keep_compute_cs "
                                            "could not be made");
            freeObjects();
            s_cmp.on = false;
            s_cmp.runs = false;
        }
    }
    s_cmp.plane = s_cmp.on;
    startLine(s_cmp.runs ? "on by the tests' switch: the compute record's mesh triangles that "
                           "draw nothing (the game's drops, wholly outside the target or the "
                           "depth range) written as a clipped filler in each draw's list, a "
                           "fourth record plane"
              : s_cmp.on ? "to be tried by the start-up self-test, on where it passes: the "
                           "compute record's mesh triangles that draw nothing (the game's "
                           "drops, wholly outside the target or the depth range) written as a "
                           "clipped filler in each draw's list, a fourth record plane"
                         : "off");
}

void rd__vu_compact_shutdown(void)
{
    freeObjects();
    free(s_cmp.calls);
    s_cmp.calls = NULL;
    s_cmp.cap = 0;
    s_cmp.on = false;
    s_cmp.runs = false;
    s_cmp.plane = false;
}

bool rd__vu_compact_plane(void)
{
    return s_cmp.plane;
}

bool rd__vu_compact_active(void)
{
    /* tried inside the record's self-test, run outside it only on its
       verdict (a self-test that never ran: off, but under the tests'
       switch) */
    return s_cmp.on && !s_cmp.refused && (s_cmp.runs || rd__vu_prepass_selftest_running()) &&
           s_cmp.pipe.id != 0 && g_rd.vuPrepass && rd__vu_record_compute() && !g_rdWin.first;
}

RhiPipeline rd__vu_compact_pipeline(void)
{
    return s_cmp.pipe;
}

bool rd__vu_compact_sabotage(void)
{
    return s_cmp.sabotage;
}

const char *rd_vu_compact_state(void)
{
    return ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_COMPACTION));
}

RdVuCmpPlan *rd__vu_compact_plan_begin(uint32_t cap, bool raise)
{
    uint32_t m = rhi_limits()->maxDrawIndexedIndexValue;
    m = m == 0u ? 0xFFFFFFu : (m < 0xFFFFFFFEu ? m : 0xFFFFFFFEu); /* never the restart value */
    rd_vu_cmp_plan_reset(&s_cmp.plan, cap, m, raise);
    return &s_cmp.plan;
}

uint32_t rd__vu_compact_add(const uint32_t *src, uint32_t first, uint32_t count, uint8_t pass,
                            uint8_t list0)
{
    if (s_cmp.plan.calls == s_cmp.cap) {
        const uint32_t cap = s_cmp.cap ? s_cmp.cap * 2u : 256u;
        RdVuCmpCall *grown = realloc(s_cmp.calls, (size_t)cap * sizeof(*grown));
        if (!grown) {
            return UINT32_MAX;
        }
        s_cmp.calls = grown;
        s_cmp.cap = cap;
    }
    const uint32_t i = s_cmp.plan.calls;
    if (!rd_vu_cmp_plan_call(&s_cmp.plan, src, first, count, pass, list0, &s_cmp.calls[i])) {
        return UINT32_MAX;
    }
    return i;
}

const RdVuCmpCall *rd__vu_compact_call(uint32_t i)
{
    return i < s_cmp.plan.calls ? &s_cmp.calls[i] : NULL;
}

const RdVuCmpCall *rd__vu_compact_find(uint32_t c0, uint32_t n, uint8_t pass, uint32_t first,
                                       uint32_t count)
{
    for (uint32_t i = c0; i < c0 + n && i < s_cmp.plan.calls; i++) {
        const RdVuCmpCall *c = &s_cmp.calls[i];
        if (c->pass == pass && c->first == first && c->count == count) {
            return c;
        }
    }
    return NULL;
}

/* the slot's readback, when its copy's frame is done: into the block's and
   the run's counts, the ledger fired */
static void slotRead(uint32_t slot)
{
    const uint32_t n = s_cmp.rbTexels[slot];
    if (n == 0u || !s_cmp.rb[slot].id) {
        return;
    }
    s_cmp.rbTexels[slot] = 0;
    const uint32_t *m = rhi_map_buffer(s_cmp.rb[slot]);
    if (m == NULL) {
        return;
    }
    RdVuCmpCounts d;
    memset(&d, 0, sizeof(d));
    rd_vu_cmp_counts_add(&d, m, n);
    rhi_unmap_buffer(s_cmp.rb[slot]);
    const uint64_t left = rd_vu_cmp_left_out(&d);
    s_cmp.lastLeftOut = left > UINT32_MAX ? UINT32_MAX : (uint32_t)left;
    ico_attempt_fire(ICO_ATT_COMPACTION, left);
    blockLock();
    for (int g = 0; g < 2; g++) {
        s_cmp.block.drop[g] += d.drop[g];
        s_cmp.block.outside[g] += d.outside[g];
        s_cmp.block.kept[g] += d.kept[g];
        s_cmp.run.drop[g] += d.drop[g];
        s_cmp.run.outside[g] += d.outside[g];
        s_cmp.run.kept[g] += d.kept[g];
    }
    s_cmp.block.pictures++;
    s_cmp.run.pictures++;
    blockUnlock();
}

void rd__vu_compact_slot_begin(uint32_t slot)
{
    if (slot < RHI_FRAMES_IN_FLIGHT) {
        slotRead(slot);
    }
}

void rd__vu_compact_readback(RhiCommandList cl, uint32_t slot, RhiBuffer rec, uint64_t off,
                             uint32_t texels)
{
    if (slot >= RHI_FRAMES_IN_FLIGHT || !rec.id || texels == 0u) {
        return;
    }
    const uint64_t bytes = (uint64_t)texels * 16u;
    if (s_cmp.rbBytes[slot] < bytes) {
        /* grown: a copy not read yet is lost with it (its picture not
           counted) */
        if (s_cmp.rb[slot].id) {
            rhi_destroy_buffer(s_cmp.rb[slot]);
        }
        uint64_t size = 16384u;
        while (size < bytes) {
            size *= 2u;
        }
        s_cmp.rb[slot] =
            rhi_create_buffer(&(RhiBufferDesc){size, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                               RHI_MEM_READBACK, "rd vu index compaction counts"});
        s_cmp.rbBytes[slot] = s_cmp.rb[slot].id ? size : 0u;
        s_cmp.rbTexels[slot] = 0;
        if (!s_cmp.rb[slot].id) {
            return;
        }
    }
    rhi_cmd_copy_buffer(cl, rec, off, s_cmp.rb[slot], 0, bytes);
    s_cmp.rbTexels[slot] = texels;
    s_cmp.ever = true;
}

uint32_t rd__vu_compact_last_left_out(void)
{
    return rd__vu_compact_active() ? s_cmp.lastLeftOut : 0u;
}

void rd_vu_compact_block(RdVuCmpCounts *out)
{
    blockLock();
    *out = s_cmp.block;
    memset(&s_cmp.block, 0, sizeof(s_cmp.block));
    blockUnlock();
}

bool rd_vu_compact_counts_now(RdVuCmpCounts *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_cmp.ever) {
        return false;
    }
    rhi_wait_idle();
    for (uint32_t s = 0; s < RHI_FRAMES_IN_FLIGHT; s++) {
        slotRead(s);
    }
    blockLock();
    *out = s_cmp.run;
    blockUnlock();
    return true;
}

void rd__vu_compact_selftest_not_run(const char *why)
{
    if (!s_cmp.on) {
        return;
    }
    s_cmp.runs = decide() && !s_cmp.refused; /* the row: not run, but under the tests' switch */
    if (!s_cmp.runs) {
        dropPlane();
    }
    char what[200];
    snprintf(what, sizeof what, "%s: the start-up self-test did not run (%s)",
             s_cmp.runs ? "on by the tests' switch" : "off", why ? why : "");
    startLine(what);
}

void rd__vu_compact_refuse(const char *why)
{
    dropPlane(); /* the test again without it: the record without the plane */
    s_cmp.refused = true;
    s_cmp.selftest = 0;
    snprintf(s_cmp.why, sizeof(s_cmp.why), "%s", why ? why : "");
}

void rd__vu_compact_selftest_done(bool on)
{
    if (!s_cmp.on) {
        return;
    }
    /* the record's final mode with the pre-pass on: the compaction's
       verdict; a record that failed with and without it (fell back, or the
       pre-pass off) is the record's failure, not the compaction's */
    const bool compute = on && g_rd.vuPrepass && rd__vu_record_compute();
    if (!s_cmp.refused && compute) {
        s_cmp.selftest = 1;
    }
    if (s_cmp.refused && !compute) {
        s_cmp.selftest = -1;
    }
    const bool runs = decide() && !s_cmp.refused;
    s_cmp.runs = runs;
    if (!runs) {
        dropPlane(); /* nothing reads it for the run */
    }
    if (s_cmp.refused && compute) {
        rd__log("VU index compaction: refused by its start-up self-test (the record's test with "
                "it: %s; without it the test passed): every draw reads its own index list",
                s_cmp.why);
    } else {
        startLine(runs ? "on: passed its start-up self-test with the record's"
                       : "off after the start-up self-test");
    }
}
