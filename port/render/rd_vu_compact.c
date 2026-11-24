/* rd_vu_compact.c: the index compaction's plan, its keep stage's pipeline,
 * the class's decision, the counters' readback and the lines
 * (rd_vu_compact.h says what and why). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "attempt_ledger.h"
#include "rd_vu_audit.h" /* rd__vu_audit_flush */
#include "rd_vu_compact.h"
#include "rd_vu_compact_own.h" /* the Mali-G52's form's own test */
#include "host_fs.h"           /* ico_fopen, ico_remove: the mark */
#include "rd_device_rows.h"    /* rd__device_value */
#include "rd_vu_rec_early.h"   /* rd__vu_rec_release */
#include "rd_vu_record.h"
#include "rd_vu_skin_check.h" /* rd__vu_prepass_selftest_running */

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

/* ------------------------------------------- the Mali-G52's form (pure) */

void rd_vu_cmp_fill(uint32_t *dst, const uint32_t *src, uint32_t count, uint32_t raise)
{
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = src[i] + raise;
    }
}

void rd_vu_cmp_check_reset(RdVuCmpCheck *r)
{
    memset(r, 0, sizeof(*r));
    r->first = UINT32_MAX;
}

bool rd_vu_cmp_check(RdVuCmpCheck *r, const uint32_t *got, const uint32_t *src, uint32_t count,
                     uint32_t raise, const uint32_t *texels, int expect)
{
    const uint32_t tris = count / 3u;
    uint32_t first = UINT32_MAX;
    for (uint32_t w = 0; w * RD_VU_CMP_GROUP < tris; w++) {
        const uint32_t t0 = w * RD_VU_CMP_GROUP;
        const uint32_t t1 = t0 + RD_VU_CMP_GROUP < tris ? t0 + RD_VU_CMP_GROUP : tris;
        uint64_t fill = 0;
        for (uint32_t t = t0; t < t1; t++) {
            const uint32_t *g = got + 3u * t;
            const uint32_t s[3] = {src[3u * t] + raise, src[3u * t + 1u] + raise,
                                   src[3u * t + 2u] + raise};
            const uint32_t f = s[1]; /* the filler: corner 1 three times */
            int asW = 1, isF = 1, ok = 1;
            for (int k = 0; k < 3; k++) {
                asW &= g[k] == s[k];
                isF &= g[k] == f;
                ok &= g[k] == s[k] || g[k] == f;
            }
            bool bad = false;
            if (asW) {
                r->asWritten++;
                bad = expect == RD_VU_CMP_EXPECT_LEFT;
            } else if (isF) {
                r->fillers++;
                fill++;
                bad = expect == RD_VU_CMP_EXPECT_KEPT;
            } else if (ok) {
                r->mixed++;
                bad = true;
            } else {
                r->wrong++;
                bad = true;
            }
            if (bad && (asW || isF)) {
                r->unexpected++;
            }
            if (bad && first == UINT32_MAX) {
                first = t;
            }
        }
        const uint32_t *x = texels + 4u * w;
        const uint64_t left = (uint64_t)x[0] + x[1];
        r->leftOut += left;
        if (left + x[2] != (uint64_t)(t1 - t0)) {
            r->badCounts++;
            first = first == UINT32_MAX ? t0 : first;
        }
        if (fill < left) {
            r->missing += left - fill;
            first = first == UINT32_MAX ? t0 : first;
        } else if (fill > left) {
            r->extra += fill - left;
            first = first == UINT32_MAX ? t0 : first;
        }
    }
    r->tris += tris;
    if (first != UINT32_MAX && r->first == UINT32_MAX) {
        r->first = first;
    }
    return first == UINT32_MAX;
}

bool rd_vu_cmp_check_ok(const RdVuCmpCheck *r)
{
    return r->mixed == 0u && r->wrong == 0u && r->missing == 0u && r->extra == 0u &&
           r->badCounts == 0u && r->unexpected == 0u;
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
    if (c->cpuPictures != 0u) {
        cmpPut(out, cap, &at,
               "; the lists written by the CPU first (the Mali-G52's form): %.0f KB and %.2f ms a "
               "picture over %u pictures",
               (double)c->cpuBytes / 1024.0 / (double)c->cpuPictures,
               (double)c->cpuNs / 1e6 / (double)c->cpuPictures, c->cpuPictures);
    }
    return at;
}

size_t rd_vu_cmp_check_text(char *out, size_t cap, const RdVuCmpCheck *r)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    size_t at = 0;
    cmpPut(out, cap, &at,
           "%llu triangles read back: %llu as the CPU wrote them, %llu fillers where the GPU "
           "counted %llu left out",
           (unsigned long long)r->tris, (unsigned long long)r->asWritten,
           (unsigned long long)r->fillers, (unsigned long long)r->leftOut);
    if (r->mixed) {
        cmpPut(out, cap, &at, "; %llu mixed (a filler store lost in part)",
               (unsigned long long)r->mixed);
    }
    if (r->missing) {
        cmpPut(out, cap, &at,
               "; %llu filler writes missing (an unwritten filler leaves its triangle as it was)",
               (unsigned long long)r->missing);
    }
    if (r->extra) {
        cmpPut(out, cap, &at, "; %llu fillers past the GPU's count", (unsigned long long)r->extra);
    }
    if (r->wrong) {
        cmpPut(out, cap, &at,
               "; %llu triangles with a value neither the CPU's nor a filler (a stale or lost "
               "list)",
               (unsigned long long)r->wrong);
    }
    if (r->badCounts) {
        cmpPut(out, cap, &at, "; %llu workgroups whose counts are not their triangles",
               (unsigned long long)r->badCounts);
    }
    if (r->unexpected) {
        cmpPut(out, cap, &at, "; %llu triangles not as the test's geometry says",
               (unsigned long long)r->unexpected);
    }
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
    /* the device table's COMPACTION_CLASS on the arguments' identity (its
       row mali-g52 reads no backend, as this rule never did); the tests'
       switch is the table's fake; the DXT stays the caller's (the PowerVR
       class and its measured name) */
    RhiDeviceId id;
    RhiDeviceFakes fk;
    RhiDeviceChoices c;
    memset(&id, 0, sizeof id);
    memset(&fk, 0, sizeof fk);
    id.backend = RHI_DT_VULKAN;
    id.os = RHI_DT_OS_BUILD;
    id.moltenvk = moltenvk ? 1u : 0u;
    id.vendorId = vendor;
    id.deviceName = name;
    fk.compactArm = byTests ? 1u : 0u;
    rhi_device_table_eval(&id, &fk, &c);
    if (c.value[RHI_DC_COMPACTION_CLASS] == RHI_DT_CMP_G52) {
        return ICO_ATT_CMP_G52;
    }
    return dxt ? ICO_ATT_CMP_DXT : ICO_ATT_CMP_OTHER;
}

/* the class defaults, from the device table's rows (their values are
   RD_VU_COMPACT_ARM_DEFAULT and RD_VU_COMPACT_DEFAULT, rhi_device_table.h) */
static int armDefault(void)
{
    return (int)rhi_device_table_row_value("mali-g52", RHI_DC_COMPACTION_DEFAULT,
                                           RD_VU_COMPACT_ARM_DEFAULT);
}

static int dxtDefault(void)
{
    return (int)rhi_device_table_row_value("powervr-dxt-48-1536", RHI_DC_COMPACTION_DEFAULT,
                                           RD_VU_COMPACT_DEFAULT);
}

/* ------------------------------------------------------------ the state */

static struct {
    bool plane;        /* the record's fourth plane (from init; dropped with a verdict of off) */
    int cls;           /* ICO_ATT_CMP_*: the device's class */
    uint32_t vendor;   /* RhiLimits.vendorId, for the line */
    char name[96];     /* the Vulkan device name, for the line */
    bool armByTests;   /* ICO_RD_VU_COMPACT_ARM=1: the Mali-G52's class on this device */
    bool on;           /* to be tried at the start (the keep stage made) */
    bool runs;         /* the decider's verdict (on outside the self-test's replays) */
    bool refused;      /* the self-test refused it for the run */
    int env;           /* ICO_RD_VU_COMPACT */
    int selftest;      /* ICO_ATT_CMP_ST_*: -1 not run, 0 refused, 1 passed, ... */
    bool sabotage;     /* ICO_RD_VU_COMPACT_SABOTAGE=1: every triangle a filler */
    bool sabLists;     /* =lists: the CPU's lists not written (the Mali-G52's form) */
    bool sabReadback;  /* =readback: one filler read back as its corner 0 */
    bool sabUnchecked; /* =unchecked: its own test's lists never copied back */
    bool far; /* the self-test's lists from the plane's middle (rd__vu_compact_selftest_far) */
    uint32_t farAt; /* the value they began at (0 none) */
    /* the Mali-G52's form: the CPU writes the lists first */
    bool cpuLists;
    const uint32_t **srcs; /* the planned calls' source lists (cap of them) */
    bool ownRun, ownOn;    /* its own test replaying; the compaction on in it */
    uint32_t ownShift;     /* the lists' first value moved in the test's replays */
    int ownArm;            /* the replay whose lists are copied next (-1 none) */
    RdVuCmpOwnCopy own[RD_VU_CMP_OWN_REPLAYS];
    bool markBlocked;     /* ICO_COMPACT_BLOCK: kept off on this install */
    char markPath[1024];  /* ICO_COMPACT_MARK: the platform's mark file, "" none */
    char version[64];     /* ICO_COMPACT_VERSION */
    char blockWords[320]; /* ICO_COMPACT_BLOCK's words */
    uint32_t pictures;    /* pictures read back with it since its test (the probation) */
    bool markedOn;        /* "on" written */
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

/* the Mali-G52's form's mark (port/platform/compact_mark.h): the stage
   word and the running version, synced where asked (trying, counting,
   failed); NULL removes it (not checked: tried again at the next start) */
static bool writeMark(const char *word, bool sync)
{
    if (s_cmp.markPath[0] == '\0') {
        return true; /* no platform's mark (the replay tool, the tests without one) */
    }
    if (word == NULL) {
        (void)ico_remove(s_cmp.markPath);
        return true;
    }
    FILE *f = ico_fopen(s_cmp.markPath, "wb");
    if (f == NULL) {
        rd__log("VU index compaction: its mark %s could not be written (%s)", s_cmp.markPath, word);
        return false;
    }
    bool ok = (s_cmp.version[0] != '\0' ? fprintf(f, "%s %s\n", word, s_cmp.version)
                                        : fprintf(f, "%s\n", word)) > 0;
    ok = fflush(f) == 0 && ok;
    if (sync) {
        (void)ico_fsync(f);
    }
    return fclose(f) == 0 && ok;
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
    rd__vu_cmp_own_free();
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
                                         dxtDefault(), armDefault(), s_cmp.selftest) != 0;
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
                 armDefault());
    } else if (s_cmp.cls == ICO_ATT_CMP_DXT) {
        snprintf(cls, sizeof cls, "the PowerVR D-Series DXT-48-1536 (%s): RD_VU_COMPACT_DEFAULT %d",
                 s_cmp.name, dxtDefault());
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
       any device; the PowerVR D-Series DXT-48-1536 by the device table's
       row powervr-dxt-48-1536 */
    const char *armEnv = getenv("ICO_RD_VU_COMPACT_ARM");
    s_cmp.armByTests = armEnv != NULL && strcmp(armEnv, "1") == 0;
    s_cmp.vendor = lim != NULL ? lim->vendorId : 0u;
    const char *name = lim != NULL ? rhi_adapter_name() : NULL;
    snprintf(s_cmp.name, sizeof s_cmp.name, "%s", name != NULL ? name : "no device name");
    /* the DXT: the device's COMPACTION_CLASS (the row powervr-dxt-48-1536:
       the PowerVR class and its measured name; the evaluation carries no
       ICO_RD_VU_COMPACT_ARM, read above for this rule alone) */
    const bool dxt = lim != NULL && rd__device_value(RHI_DC_COMPACTION_CLASS) == RHI_DT_CMP_DXT;
    s_cmp.cls = lim != NULL
                    ? rd_vu_cmp_class(name, lim->vendorId, lim->moltenvk, dxt, s_cmp.armByTests)
                    : ICO_ATT_CMP_OTHER;
    s_cmp.env = rd_vu_cmp_env();
    {
        /* the tests' words: lists, readback (the Mali-G52's form's own
           test), anything else but 0 every triangle a filler */
        const char *sb = getenv("ICO_RD_VU_COMPACT_SABOTAGE");
        const bool any = sb != NULL && sb[0] != '\0' && sb[0] != '0';
        s_cmp.sabLists = any && strcmp(sb, "lists") == 0;
        s_cmp.sabReadback = any && strcmp(sb, "readback") == 0;
        s_cmp.sabUnchecked = any && strcmp(sb, "unchecked") == 0;
        s_cmp.sabotage = any && !s_cmp.sabLists && !s_cmp.sabReadback && !s_cmp.sabUnchecked;
        if (any) {
            rd__log("VU index compaction: ICO_RD_VU_COMPACT_SABOTAGE: %s",
                    s_cmp.sabLists ? "the lists the CPU writes first written one triangle off (the "
                                     "Mali-G52's form: its own test must refuse it)"
                    : s_cmp.sabReadback  ? "one filler of its own test's lists read back as its "
                                           "corner 0 (the test must refuse it)"
                    : s_cmp.sabUnchecked ? "its own test's lists never copied back (the test "
                                           "must say not checked)"
                                         : "every triangle written as a filler (the self-test "
                                           "must refuse it)");
        }
    }
    /* the install's mark (port/platform/compact_mark.h): its file, the
       running version, a block */
    {
        const char *mark = getenv("ICO_COMPACT_MARK");
        snprintf(s_cmp.markPath, sizeof s_cmp.markPath, "%s", mark != NULL ? mark : "");
        const char *ver = getenv("ICO_COMPACT_VERSION");
        snprintf(s_cmp.version, sizeof s_cmp.version, "%.*s", 63,
                 ver != NULL && strchr(ver, '\n') == NULL ? ver : "");
        const char *block = getenv("ICO_COMPACT_BLOCK");
        s_cmp.markBlocked = block != NULL && block[0] != '\0';
        snprintf(s_cmp.blockWords, sizeof s_cmp.blockWords, "%.300s",
                 s_cmp.markBlocked ? block : "");
        s_cmp.pictures = 0;
        s_cmp.markedOn = false;
    }
    s_cmp.selftest = ICO_ATT_CMP_ST_NOT_RUN;
    s_cmp.refused = false;
    s_cmp.ownRun = s_cmp.ownOn = false;
    s_cmp.ownArm = -1;
    s_cmp.ownShift = 0;
    /* the row before the self-test (off: not run yet, but under the tests'
       switch); tried by the self-test where it would run were the test to
       pass */
    s_cmp.runs = lim != NULL && decide();
    s_cmp.on = lim != NULL && ico_attempt_compaction_candidate(
                                  g_rd.vuPrepass && rd__vu_record_compute(), s_cmp.cls, s_cmp.env,
                                  dxtDefault(), armDefault()) != 0;
    s_cmp.plane = false;
    /* the Mali-G52's form (rd_vu_compact.h): the lists written by the CPU
       first, tried by its own start-up test; kept off on this install by
       its mark after a bad end while it was tried or on */
    s_cmp.cpuLists = s_cmp.on && s_cmp.cls == ICO_ATT_CMP_G52;
    if (s_cmp.cpuLists && s_cmp.markBlocked) {
        s_cmp.on = false;
        s_cmp.cpuLists = false;
        s_cmp.selftest = ICO_ATT_CMP_ST_MARKED;
        s_cmp.runs = decide();
        rd__log("VU index compaction: off on this install (%s)", s_cmp.blockWords);
    }
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
            s_cmp.cpuLists = false;
        }
    }
    s_cmp.plane = s_cmp.on;
    startLine(s_cmp.runs ? "on by the tests' switch: the compute record's mesh triangles that "
                           "draw nothing (the game's drops, wholly outside the target or the "
                           "depth range) written as a clipped filler in each draw's list, a "
                           "fourth record plane"
              : s_cmp.cpuLists
                  ? "to be tried by its own start-up test, on where it passes: the compute "
                    "record's mesh triangles that draw nothing (the game's drops, wholly "
                    "outside the target or the depth range) written as a clipped filler in "
                    "each draw's list, a fourth record plane in memory the CPU writes; the "
                    "Mali-G52's form: every planned list written whole by the CPU first, only "
                    "the fillers by the GPU"
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
    free((void *)s_cmp.srcs);
    s_cmp.srcs = NULL;
    s_cmp.cap = 0;
    s_cmp.on = false;
    s_cmp.runs = false;
    s_cmp.plane = false;
    s_cmp.cpuLists = false;
}

bool rd__vu_compact_plane(void)
{
    return s_cmp.plane;
}

bool rd__vu_compact_active(void)
{
    /* tried inside the record's self-test, run outside it only on its
       verdict (a self-test that never ran: off, but under the tests'
       switch); the Mali-G52's form inside its own test alone (the record's
       test runs without it), and there only while the test holds it on */
    const bool tried =
        s_cmp.cpuLists ? s_cmp.ownRun && s_cmp.ownOn : rd__vu_prepass_selftest_running();
    return s_cmp.on && !s_cmp.refused && (s_cmp.runs ? !s_cmp.ownRun || s_cmp.ownOn : tried) &&
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
    if ((s_cmp.far && rd__vu_prepass_selftest_running()) || s_cmp.ownRun) {
        /* the self-test's lists from the plane's middle (a whole texel):
           the draws' first index there, as a busy picture's last lists
           are; the counts after them as always.  The Mali-G52's form's own
           test moves them by its shift every replay (no list where the
           replay before left one) */
        s_cmp.plan.used = ((cap / 2u) & ~3u) + (s_cmp.ownRun ? s_cmp.ownShift : 0u);
        s_cmp.farAt = s_cmp.plan.used;
    }
    return &s_cmp.plan;
}

bool rd__vu_compact_cpu_lists(void)
{
    return s_cmp.cpuLists && s_cmp.plane && rd__vu_record_compute();
}

bool rd__vu_compact_host_record(void)
{
    /* from its own test on (the record's test, before it, keeps device
       memory: a record that falls back from the compute record there takes
       texture copies, whose barriers are a device buffer's) */
    return rd__vu_compact_cpu_lists() && (s_cmp.ownRun || s_cmp.runs);
}

uint32_t rd__vu_compact_aux_flags(void)
{
    return (s_cmp.sabotage ? 1u : 0u) | (rd__vu_compact_cpu_lists() ? 2u : 0u);
}

bool rd__vu_compact_lists_first(RhiBuffer rec, uint32_t head)
{
    if (!rd__vu_compact_cpu_lists()) {
        return true;
    }
    uint32_t *m = rhi_map_buffer(rec);
    if (m == NULL) {
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("VU index compaction: the record buffer could not be mapped for the lists the "
                    "CPU writes first: every draw reads its own index list");
        }
        return false;
    }
    const double t0 = rd__now_ms();
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < s_cmp.plan.calls; i++) {
        const RdVuCmpCall *c = &s_cmp.calls[i];
        uint32_t *dst = m + head + c->at;
        const uint32_t *src = s_cmp.srcs[i] + c->first;
        if (!s_cmp.sabLists) {
            rd_vu_cmp_fill(dst, src, c->count, c->raise);
        } else {
            /* the tests': each triangle written as the next one's (a list
               other than the draw's, its values in range, as a stale plane
               would hold one) */
            for (uint32_t k = 0; k < c->count; k++) {
                dst[k] = src[(k + 3u) % c->count] + c->raise;
            }
        }
        bytes += (uint64_t)c->count * 4u;
    }
    rhi_unmap_buffer(rec);
    const double ms = rd__now_ms() - t0;
    if (!rd__vu_prepass_selftest_running()) {
        blockLock();
        s_cmp.block.cpuBytes += bytes;
        s_cmp.block.cpuNs += (uint64_t)(ms * 1e6);
        s_cmp.block.cpuPictures++;
        blockUnlock();
    }
    return true;
}

void rd__vu_compact_selftest_far(bool on)
{
    s_cmp.far = on;
    if (on) {
        s_cmp.farAt = 0;
    }
}

uint32_t rd__vu_compact_far_at(void)
{
    return s_cmp.farAt;
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
        /* the calls' sources beside them (the Mali-G52's form writes them) */
        const uint32_t **srcs = realloc((void *)s_cmp.srcs, (size_t)cap * sizeof(*srcs));
        if (!srcs) {
            return UINT32_MAX;
        }
        s_cmp.srcs = srcs;
        s_cmp.cap = cap;
    }
    const uint32_t i = s_cmp.plan.calls;
    if (!rd_vu_cmp_plan_call(&s_cmp.plan, src, first, count, pass, list0, &s_cmp.calls[i])) {
        return UINT32_MAX;
    }
    s_cmp.srcs[i] = src;
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
    if (s_cmp.cpuLists && s_cmp.runs && !s_cmp.markedOn && !rd__vu_prepass_selftest_running() &&
        ++s_cmp.pictures >= RD_VU_CMP_PROBATION) {
        /* the Mali-G52's form's probation (its mark, compact_mark.h) */
        s_cmp.markedOn = true;
        (void)writeMark("on", false);
        rd__log("VU index compaction: %u pictures drawn with it since its own start-up test: its "
                "mark says on",
                (unsigned)RD_VU_CMP_PROBATION);
    }
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
    rd__vu_audit_flush(); /* the tests' bounds audit's run line (rd_vu_audit.h) */
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
    if (s_cmp.cpuLists) {
        /* the Mali-G52's form: its own test's verdict (rd__vu_cmp_own_verdict;
           not run where the record did not end on the compute record) */
        if (!compute) {
            s_cmp.selftest = ICO_ATT_CMP_ST_NOT_RUN;
        }
        const bool runs = decide() && !s_cmp.refused && compute;
        s_cmp.runs = runs;
        if (!runs) {
            dropPlane(); /* nothing reads it for the run: the record made again without it */
            s_cmp.cpuLists = false;
        }
        startLine(runs ? "on: passed its own start-up test" : "off after the start-up self-tests");
        return;
    }
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

/* ------------------------------------- the Mali-G52's form's own test's */

void rd__vu_cmp_own_begin(void)
{
    s_cmp.ownRun = true;
    s_cmp.ownOn = false;
    s_cmp.ownArm = -1;
    s_cmp.ownShift = 0;
}

void rd__vu_cmp_own_end(void)
{
    s_cmp.ownRun = false;
    s_cmp.ownOn = false;
    s_cmp.ownArm = -1;
    s_cmp.ownShift = 0;
}

void rd__vu_cmp_own_hold(bool on)
{
    s_cmp.ownOn = on;
}

void rd__vu_cmp_own_shift(uint32_t shift)
{
    s_cmp.ownShift = shift;
}

void rd__vu_cmp_own_arm(int k)
{
    s_cmp.ownArm = k >= 0 && k < RD_VU_CMP_OWN_REPLAYS ? k : -1;
    if (s_cmp.ownArm >= 0) {
        s_cmp.own[k].copied = false;
        s_cmp.own[k].n = 0;
    }
}

const RdVuCmpOwnCopy *rd__vu_cmp_own_copy(int k)
{
    return k >= 0 && k < RD_VU_CMP_OWN_REPLAYS ? &s_cmp.own[k] : NULL;
}

void rd__vu_cmp_own_free(void)
{
    for (int k = 0; k < RD_VU_CMP_OWN_REPLAYS; k++) {
        if (s_cmp.own[k].rb.id) {
            rhi_destroy_buffer(s_cmp.own[k].rb);
        }
        memset(&s_cmp.own[k], 0, sizeof(s_cmp.own[k]));
    }
}

bool rd__vu_cmp_own_sabotage_readback(void)
{
    return s_cmp.sabReadback;
}

bool rd__vu_cmp_own_wanted(void)
{
    return s_cmp.on && s_cmp.cpuLists && s_cmp.plane && !s_cmp.refused && s_cmp.pipe.id != 0 &&
           g_rd.vuPrepass && rd__vu_record_compute();
}

bool rd__vu_cmp_own_trying(void)
{
    return writeMark("trying", true);
}

void rd__vu_cmp_own_verdict(int result, const char *why)
{
    if (result == 1) {
        s_cmp.selftest = ICO_ATT_CMP_ST_PASSED;
        s_cmp.pictures = 0;
        s_cmp.markedOn = false;
        (void)writeMark("counting", true);
        return;
    }
    s_cmp.refused = true;
    snprintf(s_cmp.why, sizeof(s_cmp.why), "%s", why ? why : "");
    if (result == 0) {
        s_cmp.selftest = ICO_ATT_CMP_ST_OWN_REFUSED;
        (void)writeMark("failed", true);
    } else {
        s_cmp.selftest = ICO_ATT_CMP_ST_UNCHECKED;
        (void)writeMark(NULL, false); /* no verdict: tried again at the next start */
    }
}

void rd__vu_compact_check_copy(RhiCommandList cl, RhiBuffer rec, uint32_t head, uint32_t counts)
{
    const int k = s_cmp.ownArm;
    if (!s_cmp.ownRun || k < 0 || !rec.id) {
        return;
    }
    s_cmp.ownArm = -1; /* one replay's */
    RdVuCmpOwnCopy *o = &s_cmp.own[k];
    const RdVuCmpPlan *p = &s_cmp.plan;
    o->n = p->calls;
    o->copied = false;
    if (p->calls == 0u || p->calls > RD_VU_CMP_OWN_DRAWS) {
        return; /* not the test's four draws: the check says so */
    }
    /* the lists from the first call's (a whole texel down) to the counts'
       last texel */
    o->from = p->calls ? s_cmp.calls[0].at & ~3u : 0u;
    o->counts = counts - head / 4u;
    const uint64_t end = ((uint64_t)o->counts + p->groups) * 16u;
    const uint64_t bytes = end - (uint64_t)o->from * 4u;
    if (end <= (uint64_t)o->from * 4u) {
        return;
    }
    if (o->rb.id && o->bytes < bytes) {
        rhi_destroy_buffer(o->rb);
        o->rb = (RhiBuffer){0};
    }
    if (!o->rb.id) {
        o->rb = rhi_create_buffer(&(RhiBufferDesc){bytes, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                                   RHI_MEM_READBACK,
                                                   "rd vu index compaction own test"});
        o->bytes = o->rb.id ? bytes : 0u;
    }
    if (!o->rb.id) {
        return;
    }
    for (uint32_t i = 0; i < p->calls; i++) {
        o->calls[i] = s_cmp.calls[i];
        o->src[i] = s_cmp.srcs[i];
    }
    if (s_cmp.sabUnchecked) {
        return; /* the tests': nothing copied, the check cannot judge */
    }
    rhi_cmd_copy_buffer(cl, rec, ((uint64_t)head + o->from) * 4u, o->rb, 0, bytes);
    o->copied = true;
}
