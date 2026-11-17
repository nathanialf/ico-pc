/*
 * port/platform/state_reloc.c
 *
 * Moving saved host addresses (state_reloc.h): the word functions, the
 * translation of another build's MODULE words, the saving process's two
 * values (thread pointer, stack guard) and the conservative scan over a
 * state's regions.  The image range and the
 * self-checks are state_check.c's (they name the scheduler and the kernel,
 * which this file, linked by every fiber user, must not pull in).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "state_reloc.h"

static int in_range(const IcoStateRange *g, uint64_t w)
{
    return g->lo < g->hi && w >= g->lo && w <= g->hi;
}

static int find_point(const IcoStateSpan *p, size_t n, uint64_t old, uint64_t *now)
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (p[mid].lo < old) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < n && p[lo].lo == old) {
        *now = p[lo].now;
        return 1;
    }
    return 0;
}

int ico_state_xlate(const IcoStateXlate *x, uint64_t old, uint64_t *now)
{
    size_t lo = 0, hi = x->n_point;

    if (x->fiber > 0 && (size_t)(x->fiber - 1) < x->n_fiber && x->fiber_point != NULL &&
        x->fiber_point[x->fiber - 1] != NULL &&
        find_point(x->fiber_point[x->fiber - 1], x->fiber_n_point[x->fiber - 1], old, now)) {
        return 1;
    }
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (x->point[mid].lo < old) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < x->n_point && x->point[lo].lo == old) {
        *now = x->point[lo].now;
        return 1;
    }
    lo = 0;
    hi = x->n_span;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (x->span[mid].lo <= old) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo > 0 && old <= x->span[lo - 1].hi) {
        *now = x->span[lo - 1].now + (old - x->span[lo - 1].lo);
        return 1;
    }
    return 0;
}

size_t ico_state_stack_dead_bytes(uint64_t base, size_t n, uint64_t sp)
{
    const uint64_t live = sp >= ICO_STATE_RED_ZONE ? sp - ICO_STATE_RED_ZONE : 0;

    if (sp == 0 || live <= base) {
        return 0;
    }
    if (live >= base + n) {
        return n;
    }
    return (size_t)(live - base) & ~(size_t)7;
}

void ico_state_miss_at(IcoStateMiss *m, const char *where, uint64_t base, int fiber, int sp_rel)
{
    if (m == NULL) {
        return;
    }
    m->where = where;
    m->base = base;
    m->fiber = fiber;
    m->sp_rel = sp_rel;
    m->dead_lo = 0;
    m->dead_hi = 0;
}

/* a word without a translation, read from `at`: dead or live by its place */
static void note_miss(IcoStateMiss *m, uint64_t w, uint64_t at)
{
    if (m->dead_lo < m->dead_hi && at >= m->dead_lo && at < m->dead_hi) {
        if (m->dead == 0) {
            m->first_dead = w;
        }
        m->dead++;
        return;
    }
    if (m->live == 0) {
        m->first_live = w;
        m->live_where = m->where != NULL ? m->where : "an unnamed region";
        m->live_fiber = m->fiber;
        m->live_sp_rel = m->sp_rel;
        m->live_off = (int64_t)(at - m->base);
    }
    m->live++;
}

void ico_state_miss_live_text(const IcoStateMiss *m, uint64_t image_lo, char *out, size_t n)
{
    char place[128];
    const uint64_t off = m->first_live >= image_lo ? m->first_live - image_lo : 0;
    const uint64_t mag =
        m->live_off < 0 ? (uint64_t)0 - (uint64_t)m->live_off : (uint64_t)m->live_off;

    if (n == 0) {
        return;
    }
    if (m->live == 0) {
        out[0] = '\0';
        return;
    }
    if (m->live_fiber > 0 && m->live_sp_rel) {
        if (m->live_off == 0) {
            snprintf(place, sizeof place, "fiber slot %d's %s, at its saved stack pointer,",
                     m->live_fiber - 1, m->live_where);
        } else {
            snprintf(place, sizeof place,
                     "fiber slot %d's %s, %llu bytes %s its saved stack pointer,",
                     m->live_fiber - 1, m->live_where, (unsigned long long)mag,
                     m->live_off < 0 ? "below" : "above");
        }
    } else if (m->live_fiber > 0) {
        snprintf(place, sizeof place, "fiber slot %d's %s at offset 0x%llx", m->live_fiber - 1,
                 m->live_where, (unsigned long long)mag);
    } else {
        snprintf(place, sizeof place, "%s at offset 0x%llx", m->live_where,
                 (unsigned long long)mag);
    }
    snprintf(out, n, "%s holds 0x%llx (offset 0x%llx in the saving build's image)", place,
             (unsigned long long)m->first_live, (unsigned long long)off);
}

int ico_state_miss_refuses(const IcoStateMiss *m, uint64_t image_lo, char *out, size_t n)
{
    char first[224];

    if (n > 0) {
        out[0] = '\0';
    }
    if (m == NULL || m->live == 0) {
        return 0;
    }
    ico_state_miss_live_text(m, image_lo, first, sizeof first);
    if (n > 0) {
        snprintf(out, n, "%llu program words in live memory had no translation, the first: %s",
                 (unsigned long long)m->live, first);
    }
    return 1;
}

void ico_state_miss_counts(const IcoStateMiss *m, char *out, size_t n)
{
    if (n == 0) {
        return;
    }
    snprintf(out, n,
             "%llu program words in dead stack frames had no translation and were left as they "
             "were (first 0x%llx); %llu in live memory",
             (unsigned long long)m->dead, (unsigned long long)m->first_dead,
             (unsigned long long)m->live);
}

/* a MODULE word through the translation: 1 when it changed */
static int xlate_word(const IcoStateXlate *x, uint64_t *w, uint64_t at)
{
    uint64_t v;

    if (ico_state_xlate(x, *w, &v)) {
        if (v == *w) {
            return 0;
        }
        *w = v;
        return 1;
    }
    if (x->missed != NULL) {
        if (*x->missed == 0 && x->first_missed != NULL) {
            *x->first_missed = *w;
        }
        (*x->missed)++;
    }
    if (x->miss != NULL) {
        note_miss(x->miss, *w, at);
    }
    return 0;
}

int ico_state_reloc_word(const IcoStateReloc *r, uint64_t *w)
{
    return ico_state_reloc_word_at(r, w, (uint64_t)(uintptr_t)w);
}

int ico_state_reloc_word_at(const IcoStateReloc *r, uint64_t *w, uint64_t at)
{
    int i;
    for (i = 0; i < ICO_RELOC_VALUES; i++) {
        const IcoStateValue *v = &r->value[i];
        if (v->old != 0 && v->old != v->now && *w == v->old) {
            *w = v->now;
            if (r->value_words != NULL) {
                r->value_words[i]++;
            }
            return 1;
        }
    }
    for (i = 0; i < ICO_RELOC_RANGES; i++) {
        const IcoStateRange *g = &r->range[i];
        if (in_range(g, *w)) {
            if (i == ICO_RELOC_MODULE && r->xlate != NULL) {
                return xlate_word(r->xlate, w, at);
            }
            if (g->delta == 0) {
                return 0;
            }
            *w += (uint64_t)g->delta;
            return 1;
        }
    }
    return 0;
}

int ico_state_reloc_typed(const IcoStateReloc *r, int which, uint64_t *w)
{
    const IcoStateRange *g;
    if (which < 0 || which >= ICO_RELOC_RANGES) {
        return -1;
    }
    g = &r->range[which];
    if (*w == 0) {
        return 0;
    }
    if (g->lo < g->hi && !in_range(g, *w)) {
        return -1;
    }
    if (which == ICO_RELOC_MODULE && r->xlate != NULL) {
        uint64_t v;

        if (!ico_state_xlate(r->xlate, *w, &v)) {
            return -1;
        }
        *w = v;
        return 0;
    }
    *w += (uint64_t)g->delta;
    return 0;
}

static void set_value(IcoStateValue *v, uint64_t old, uint64_t now)
{
    v->old = old != 0 && now != 0 && old != now ? old : 0;
    v->now = now;
}

void ico_state_reloc_set_values(IcoStateReloc *r, uint64_t tls_old, uint64_t tls_now,
                                uint64_t guard_old, uint64_t guard_now, uint64_t *words)
{
    set_value(&r->value[ICO_RELOC_TLS], tls_old, tls_now);
    set_value(&r->value[ICO_RELOC_GUARD], guard_old, guard_now);
    r->value_words = words;
    if (words != NULL) {
        memset(words, 0, ICO_RELOC_VALUES * sizeof words[0]);
    }
}

int ico_state_reloc_values_on(const IcoStateReloc *r)
{
    int i;
    for (i = 0; i < ICO_RELOC_VALUES; i++) {
        if (r->value[i].old != 0 && r->value[i].old != r->value[i].now) {
            return 1;
        }
    }
    return 0;
}

/* --- the process's values ---------------------------------------------------- */

#if defined(__APPLE__)
/* libSystem's guard (Libc's stack_protector.c: an array whose first word
   the compiler's frames hold), random per process; no header declares it */
extern uintptr_t __stack_chk_guard;
#endif

void ico_state_proc_read(IcoStateProc *out)
{
    memset(out, 0, sizeof *out);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        uintptr_t tp;

        __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
        out->tls_base = (uint64_t)tp;
#if defined(__ANDROID__)
        /* bionic's TLS_SLOT_STACK_GUARD (slot 5 on arm64): what the
           protector's prologue loads from [TPIDR_EL0, #0x28] */
        if (tp != 0) {
            out->stack_guard = (uint64_t)((const volatile uintptr_t *)tp)[5];
        }
#endif
    }
#elif defined(__APPLE__)
    out->stack_guard = (uint64_t)__stack_chk_guard;
#endif
}

unsigned ico_state_proc_needs(const char *tag)
{
    if (tag == NULL) {
        return 0;
    }
    if (strcmp(tag, "android-arm64") == 0) {
        return ICO_STATE_PROC_TLS | ICO_STATE_PROC_GUARD;
    }
    if (strcmp(tag, "linux-arm64") == 0) {
        return ICO_STATE_PROC_TLS;
    }
    if (strncmp(tag, "macos-", 6) == 0 || strncmp(tag, "ios-", 4) == 0) {
        return ICO_STATE_PROC_GUARD;
    }
    return 0;
}

int ico_state_proc_check(const char *tag, uint64_t tls_base, uint64_t stack_guard)
{
    const unsigned need = ico_state_proc_needs(tag);

    if ((need & ICO_STATE_PROC_TLS) != 0 && tls_base == 0) {
        return -1;
    }
    if ((need & ICO_STATE_PROC_GUARD) != 0 && stack_guard == 0) {
        return -1;
    }
    return 0;
}

/* --- the scan -------------------------------------------------------------- */

static int log_on;

void ico_state_reloc_set_log(int on)
{
    log_on = on != 0;
}

size_t ico_state_reloc_scan(const IcoStateReloc *r, void *region, size_t n, uint64_t *rewritten)
{
    const uintptr_t base = (uintptr_t)region;
    uintptr_t a = (base + 7u) & ~(uintptr_t)7u;
    size_t moved = 0;

    if (region == NULL) {
        return 0;
    }
    for (; a >= base && a - base + 8u <= n; a += 8u) {
        uint64_t w;

        memcpy(&w, (const void *)a, sizeof w);
        {
            const uint64_t old = w;

            if (ico_state_reloc_word_at(r, &w, (uint64_t)a)) {
                memcpy((void *)a, &w, sizeof w);
                moved++;
                if (log_on) {
                    fprintf(stderr, "state: moved the word at %p: %#llx -> %#llx\n", (void *)a,
                            (unsigned long long)old, (unsigned long long)w);
                }
            }
        }
    }
    if (rewritten != NULL) {
        *rewritten += moved;
    }
    return moved;
}

/* --- what the scan may move (issue 218) ------------------------------------- */

int ico_state_reloc_ambiguous(const IcoStateReloc *r)
{
    static const int judged[] = {ICO_RELOC_MODULE, ICO_RELOC_ARENA, ICO_RELOC_POOL};
    size_t i;

    for (i = 0; i < sizeof judged / sizeof judged[0]; i++) {
        const IcoStateRange *g = &r->range[judged[i]];

        if (g->lo >= g->hi || g->delta == 0) {
            continue;
        }
        if (judged[i] == ICO_RELOC_MODULE && r->xlate != NULL) {
            continue;
        }
        if ((g->lo >> 32) < ICO_STATE_SCAN_HIGH_MIN || (g->hi >> 32) < ICO_STATE_SCAN_HIGH_MIN) {
            return judged[i];
        }
    }
    return -1;
}

size_t ico_state_reloc_count(const IcoStateReloc *r, int which, const void *region, size_t n)
{
    const uintptr_t base = (uintptr_t)region;
    uintptr_t a = (base + 7u) & ~(uintptr_t)7u;
    size_t k = 0;

    if (region == NULL || which < 0 || which >= ICO_RELOC_RANGES) {
        return 0;
    }
    for (; a >= base && a - base + 8u <= n; a += 8u) {
        uint64_t w;

        memcpy(&w, (const void *)a, sizeof w);
        k += (size_t)in_range(&r->range[which], w);
    }
    return k;
}

/* 1 when some span of x starts exactly at w */
static int span_start(const IcoStateXlate *x, uint64_t w)
{
    size_t lo = 0, hi = x->n_span;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (x->span[mid].lo < w) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < x->n_span && x->span[lo].lo == w;
}

/* inserts v into the sorted distinct out[0..*k) (cap entries); 1 when new */
static int add_distinct(uint64_t *out, size_t cap, size_t *k, size_t *found, uint64_t v)
{
    size_t lo = 0, hi = *k;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (out[mid] < v) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < *k && out[lo] == v) {
        return 0;
    }
    (*found)++;
    if (*k < cap) {
        memmove(out + lo + 1, out + lo, (*k - lo) * sizeof out[0]);
        out[lo] = v;
        (*k)++;
    }
    return 1;
}

size_t ico_state_code_numbers(const IcoStateXlate *x, uint64_t text_lo, uint64_t text_hi,
                              const void *region, size_t n, uint64_t *out, size_t cap, size_t *k)
{
    const uintptr_t base = (uintptr_t)region;
    uintptr_t a = (base + 7u) & ~(uintptr_t)7u;
    size_t found = 0;

    if (x == NULL || region == NULL || text_lo >= text_hi) {
        return 0;
    }
    for (; a >= base && a - base + 8u <= n; a += 8u) {
        uint64_t w, v;

        memcpy(&w, (const void *)a, sizeof w);
        if (w < text_lo || w >= text_hi || span_start(x, w)) {
            continue;
        }
        if (find_point(x->point, x->n_point, w, &v)) {
            continue;
        }
        (void)add_distinct(out, cap, k, &found, w);
    }
    return found;
}

int ico_state_region_holds(const void *region, size_t n, const uint64_t *vals, size_t nv,
                           uint64_t *which)
{
    const uintptr_t base = (uintptr_t)region;
    uintptr_t a = (base + 7u) & ~(uintptr_t)7u;

    if (region == NULL || vals == NULL || nv == 0) {
        return 0;
    }
    for (; a >= base && a - base + 8u <= n; a += 8u) {
        uint64_t w;
        size_t lo = 0, hi = nv;

        memcpy(&w, (const void *)a, sizeof w);
        if (w < vals[0] || w > vals[nv - 1]) {
            continue;
        }
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;

            if (vals[mid] < w) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo < nv && vals[lo] == w) {
            if (which != NULL) {
                *which = w;
            }
            return 1;
        }
    }
    return 0;
}

int ico_state_move_check(const IcoStateMoveIn *in, uint64_t *nums, size_t cap, size_t *nk,
                         uint64_t *held_value)
{
    const int amb = ico_state_reloc_ambiguous(in->rel);
    size_t found = 0;
    int i;

    *nk = 0;
    if (amb >= 0) {
        return amb;
    }
    if (in->rel->xlate == NULL || (in->saved_image_lo >> 32) >= ICO_STATE_SCAN_HIGH_MIN) {
        return ICO_STATE_MOVE_OK;
    }
    for (i = 0; i < in->n_data; i++) {
        found += ico_state_code_numbers(in->rel->xlate, in->text_lo, in->text_hi, in->data[i],
                                        in->data_n[i], nums, cap, nk);
    }
    if (found > *nk) {
        return ICO_STATE_MOVE_TOO_MANY;
    }
    for (i = 0; i < in->n_held && *nk > 0; i++) {
        if (ico_state_region_holds(in->held[i], in->held_n[i], nums, *nk, held_value)) {
            return ICO_STATE_MOVE_HELD;
        }
    }
    return ICO_STATE_MOVE_OK;
}
