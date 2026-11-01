/*
 * port/platform/state_reloc.c
 *
 * Moving saved host addresses (state_reloc.h): the word functions, the
 * saving process's two values (thread pointer, stack guard) and the
 * conservative scan over a state's regions.  The image range and the
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

int ico_state_reloc_word(const IcoStateReloc *r, uint64_t *w)
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

            if (ico_state_reloc_word(r, &w)) {
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
