/* rd_vu_audit.c: the VU record path's bounds audit under ICO_RD_VU_AUDIT=1
 * (rd_vu_audit.h says what is checked and why). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_vu_audit.h"

/* the violation lines printed (then only counted) */
#define AUDIT_LINES 32u

/* a planned compacted draw kept for the readback: its list's first value in
   the plane, its values, its raise, its source values' place in src */
typedef struct AuditCall {
    uint32_t at, count, raise, srcAt;
} AuditCall;

/* a frame slot's replay waiting for its readback */
typedef struct AuditSlot {
    uint32_t replay; /* g_rd.replayCounter of the tables (0: none) */
    AuditCall *calls;
    uint32_t nCalls, capCalls;
    uint32_t *src;
    uint32_t nSrc, capSrc;
    RhiBuffer rb;
    uint64_t rbBytes;
    uint32_t copied; /* values copied into rb (0: no copy pending) */
} AuditSlot;

static struct {
    int on; /* -1 not read, 0, 1 */
    AuditSlot slot[RHI_FRAMES_IN_FLIGHT];
    uint32_t violations, lines;
    /* the run's counts */
    uint64_t entries, values, calls, callValues, fillers, readbacks;
    uint64_t draws[3];
    uint32_t lo[3], hi[3];
    bool any[3];
    uint32_t maxPlaneEnd; /* the largest (first + count) of a plane draw */
} s_au = {.on = -1};

bool rd_vu_audit_on(void)
{
    if (s_au.on < 0) {
        const char *e = getenv("ICO_RD_VU_AUDIT");
        s_au.on = e != NULL && strcmp(e, "1") == 0 ? 1 : 0;
        if (s_au.on) {
            rd__log("VU audit: on (ICO_RD_VU_AUDIT=1, the tests): every recorded command's "
                    "slots and index values, the compaction's lists read back after their "
                    "fence and every draw that reads the record as its index buffer checked "
                    "on the CPU");
        }
    }
    return s_au.on == 1;
}

uint32_t rd_vu_audit_violations(void)
{
    return s_au.violations;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
static void violation(const char *fmt, ...)
{
    s_au.violations++;
    if (s_au.lines >= AUDIT_LINES) {
        return;
    }
    s_au.lines++;
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    rd__log("VU audit: violation: replay %u: %s", g_rd.replayCounter, text);
}

/* ------------------------------------------------------------ entries */

void rd__vu_audit_entry(const RdVuAuditEntry *e, const uint32_t *values, uint32_t n)
{
    if (!rd_vu_audit_on() || e == NULL) {
        return;
    }
    s_au.entries++;
    const char *mesh = e->mesh ? e->mesh : "?";
    if (e->count < 3u) {
        violation("entry %u (list %u, mesh %s): %u vertices recorded", e->index, e->list, mesh,
                  e->count);
        return;
    }
    const uint64_t kicks = (uint64_t)e->count - 2u;
    if ((uint64_t)e->base + e->vSlots > e->planeSlots) {
        violation("entry %u (list %u, mesh %s): pass V's slots %u .. %llu past the plane's %llu",
                  e->index, e->list, mesh, e->base, (unsigned long long)e->base + e->vSlots,
                  (unsigned long long)e->planeSlots);
    }
    const uint32_t t[3] = {e->slotA, e->slotB, e->slotC};
    for (int k = 0; k < 3; k++) {
        if (k == 2 && t[k] == ~0u) {
            continue;
        }
        if ((uint64_t)t[k] + kicks > e->planeSlots) {
            violation("entry %u (list %u, mesh %s): pass T's slot %c %u + %llu kicks past the "
                      "plane's %llu",
                      e->index, e->list, mesh, "ABC"[k], t[k], (unsigned long long)kicks,
                      (unsigned long long)e->planeSlots);
        }
    }
    if (values == NULL || n % 3u != 0u) {
        violation("entry %u (list %u, mesh %s): %u index values (not whole triangles)", e->index,
                  e->list, mesh, n);
    }
    for (uint32_t i = 0; values != NULL && i < n; i++) {
        const uint32_t v = values[i], kick = v >> 2, corner = v & 3u;
        s_au.values++;
        /* vu_main_vs and the keep stage: the record at rec.x + kick - 2 +
           corner (rec.x = base - first) and slot tri.x + kick (tri.x =
           slotA - first - 2) */
        if (corner > 2u || kick < e->first + 2u ||
            (uint64_t)kick >= (uint64_t)e->first + e->count || v > e->maxValue) {
            violation("entry %u (list %u, mesh %s): index value %u (kick %u, corner %u) at %u: "
                      "the recorded kicks are %u .. %u, the largest value %u",
                      e->index, e->list, mesh, v, kick, corner, i, e->first + 2u,
                      e->first + e->count - 1u, e->maxValue);
        }
    }
}

/* ------------------------------------------------------------ the lists */

static void freeSlot(AuditSlot *s)
{
    if (s->rb.id) {
        rhi_destroy_buffer(s->rb);
    }
    free(s->calls);
    free(s->src);
    memset(s, 0, sizeof(*s));
}

/* the slot's copy judged against its tables (its fence passed) */
static void judgeSlot(AuditSlot *s)
{
    if (s->copied == 0u || !s->rb.id) {
        s->copied = 0;
        return;
    }
    const uint32_t *m = rhi_map_buffer(s->rb);
    if (m == NULL) {
        violation("the readback of replay %u's lists could not be mapped", s->replay);
        s->copied = 0;
        return;
    }
    s_au.readbacks++;
    for (uint32_t c = 0; c < s->nCalls; c++) {
        const AuditCall *a = &s->calls[c];
        s_au.calls++;
        if ((uint64_t)a->at + a->count > s->copied || a->count % 3u != 0u) {
            violation("replay %u's list %u: values %u .. %llu past the %u copied", s->replay, c,
                      a->at, (unsigned long long)a->at + a->count, s->copied);
            continue;
        }
        for (uint32_t j = 0; j < a->count; j += 3u) {
            const uint32_t *o = m + a->at + j, *x = s->src + a->srcAt + j;
            const uint32_t r0 = o[0] - a->raise, r1 = o[1] - a->raise, r2 = o[2] - a->raise;
            s_au.callValues += 3u;
            if (r0 == x[0] && r1 == x[1] && r2 == x[2]) {
                continue;
            }
            if (r0 == x[1] && r1 == x[1] && r2 == x[1]) {
                s_au.fillers++;
                continue;
            }
            violation("replay %u's list %u (plane value %u, raise %u): triangle %u written %u %u "
                      "%u, its source %u %u %u (raised %u %u %u)",
                      s->replay, c, a->at + j, a->raise, j / 3u, o[0], o[1], o[2], x[0], x[1], x[2],
                      x[0] + a->raise, x[1] + a->raise, x[2] + a->raise);
        }
    }
    rhi_unmap_buffer(s->rb);
    s->copied = 0;
}

void rd__vu_audit_slot_begin(uint32_t slot)
{
    if (!rd_vu_audit_on() || slot >= RHI_FRAMES_IN_FLIGHT) {
        return;
    }
    AuditSlot *s = &s_au.slot[slot];
    judgeSlot(s);
    s->replay = g_rd.replayCounter;
    s->nCalls = 0;
    s->nSrc = 0;
}

void rd__vu_audit_call(uint32_t slot, uint32_t at, uint32_t count, uint32_t raise,
                       const uint32_t *src)
{
    if (!rd_vu_audit_on() || slot >= RHI_FRAMES_IN_FLIGHT || src == NULL) {
        return;
    }
    AuditSlot *s = &s_au.slot[slot];
    if (s->nCalls == s->capCalls) {
        const uint32_t cap = s->capCalls ? s->capCalls * 2u : 256u;
        AuditCall *g = realloc(s->calls, (size_t)cap * sizeof(*g));
        if (g == NULL) {
            violation("the audit's tables could not grow");
            return;
        }
        s->calls = g;
        s->capCalls = cap;
    }
    if ((uint64_t)s->nSrc + count > s->capSrc) {
        uint64_t cap = s->capSrc ? s->capSrc : 65536u;
        while (cap < (uint64_t)s->nSrc + count) {
            cap *= 2u;
        }
        uint32_t *g = cap <= UINT32_MAX ? realloc(s->src, (size_t)cap * sizeof(*g)) : NULL;
        if (g == NULL) {
            violation("the audit's tables could not grow");
            return;
        }
        s->src = g;
        s->capSrc = (uint32_t)cap;
    }
    s->calls[s->nCalls++] = (AuditCall){at, count, raise, s->nSrc};
    memcpy(s->src + s->nSrc, src, (size_t)count * sizeof(uint32_t));
    s->nSrc += count;
}

void rd__vu_audit_copy(RhiCommandList cl, uint32_t slot, RhiBuffer rec, uint64_t head,
                       uint32_t values)
{
    if (!rd_vu_audit_on() || slot >= RHI_FRAMES_IN_FLIGHT || !rec.id || values == 0u) {
        return;
    }
    AuditSlot *s = &s_au.slot[slot];
    const uint64_t bytes = (uint64_t)values * 4u;
    if (s->rbBytes < bytes) {
        if (s->rb.id) {
            rhi_destroy_buffer(s->rb);
        }
        uint64_t size = 65536u;
        while (size < bytes) {
            size *= 2u;
        }
        s->rb = rhi_create_buffer(&(RhiBufferDesc){size, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                                   RHI_MEM_READBACK, "rd vu audit lists"});
        s->rbBytes = s->rb.id ? size : 0u;
        if (!s->rb.id) {
            violation("the audit's readback buffer (%llu bytes) could not be made",
                      (unsigned long long)size);
            return;
        }
    }
    rhi_cmd_copy_buffer(cl, rec, head, s->rb, 0, bytes);
    s->copied = values;
}

/* ------------------------------------------------------------ draws */

void rd__vu_audit_draw(const RdVuAuditDraw *d)
{
    if (!rd_vu_audit_on() || d == NULL || d->kind < 0 || d->kind > 2) {
        return;
    }
    const int k = d->kind;
    s_au.draws[k]++;
    if (d->count > 0u) {
        s_au.lo[k] = !s_au.any[k] || d->lo < s_au.lo[k] ? d->lo : s_au.lo[k];
        s_au.hi[k] = !s_au.any[k] || d->hi > s_au.hi[k] ? d->hi : s_au.hi[k];
        s_au.any[k] = true;
    }
    static const char *const kName[3] = {"the fourth plane's list", "its own list",
                                         "a merged run's list in the ring"};
    if (d->count % 3u != 0u) {
        violation("a draw of %s: %u indices (not whole triangles)", kName[k], d->count);
    }
    if (d->count > 0u && (d->hi > d->maxValue || d->lo > d->hi)) {
        violation("a draw of %s: values %u .. %u, the device's largest %u", kName[k], d->lo, d->hi,
                  d->maxValue);
    }
    if (k != RD_VU_AUDIT_DRAW_PLANE) {
        return;
    }
    const uint64_t end = d->ibOff + ((uint64_t)d->first + d->count) * 4u;
    const uint32_t planeEnd = d->first + d->count;
    s_au.maxPlaneEnd = planeEnd > s_au.maxPlaneEnd ? planeEnd : s_au.maxPlaneEnd;
    if (d->ibOff % 4u != 0u || d->ibOff != 3u * d->planeBytes || end > d->ibBytes ||
        (uint64_t)planeEnd * 4u > d->planeBytes) {
        violation("a draw of the fourth plane's list: offset %llu (the plane at %llu), first %u, "
                  "count %u: its last byte %llu, the buffer's %llu, the plane's values %llu",
                  (unsigned long long)d->ibOff, (unsigned long long)(3u * d->planeBytes), d->first,
                  d->count, (unsigned long long)end, (unsigned long long)d->ibBytes,
                  (unsigned long long)(d->planeBytes / 4u));
    }
}

void rd__vu_audit_flush(void)
{
    if (!rd_vu_audit_on()) {
        return;
    }
    rhi_wait_idle();
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        judgeSlot(&s_au.slot[i]);
    }
    rd__log("VU audit: %llu entries (%llu index values), %llu compacted lists from %llu "
            "readbacks (%llu values, %llu filler triangles), draws: %llu of the fourth plane "
            "(values %u .. %u, the furthest index %u), %llu of their own lists (values %u .. "
            "%u), %llu merged runs in the ring (values %u .. %u); %u violations",
            (unsigned long long)s_au.entries, (unsigned long long)s_au.values,
            (unsigned long long)s_au.calls, (unsigned long long)s_au.readbacks,
            (unsigned long long)s_au.callValues, (unsigned long long)s_au.fillers,
            (unsigned long long)s_au.draws[0], s_au.lo[0], s_au.hi[0], s_au.maxPlaneEnd,
            (unsigned long long)s_au.draws[1], s_au.lo[1], s_au.hi[1],
            (unsigned long long)s_au.draws[2], s_au.lo[2], s_au.hi[2], s_au.violations);
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        freeSlot(&s_au.slot[i]);
    }
}
