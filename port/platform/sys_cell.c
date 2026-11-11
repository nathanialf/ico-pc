/*
 * port/platform/sys_cell.c
 *
 * A last-value cell (sys_cell.h).
 */
#include "sys_cell.h"

#include <string.h>

/* doubles through their bits, so the atomic builtins take them */
static void storeD(double *p, double v)
{
    uint64_t b;
    memcpy(&b, &v, sizeof b);
    __atomic_store_n((uint64_t *)(void *)p, b, __ATOMIC_RELEASE); /* after the odd sequence */
}

static double loadD(const double *p)
{
    const uint64_t b = __atomic_load_n((const uint64_t *)(const void *)p, __ATOMIC_ACQUIRE);
    double v;
    memcpy(&v, &b, sizeof v);
    return v;
}

void sys_cell_put(SysCell *c, double atMs, double tookMs, const double *v, uint32_t n)
{
    if (n > SYS_CELL_VALUES) {
        n = SYS_CELL_VALUES;
    }
    const uint32_t s = __atomic_load_n(&c->seq, __ATOMIC_RELAXED);
    /* no fences (ThreadSanitizer refuses them): every value stored with
       release after the odd sequence, every load with acquire before the
       sequence is read again */
    __atomic_store_n(&c->seq, s + 1u, __ATOMIC_RELAXED);
    storeD(&c->atMs, atMs);
    storeD(&c->tookMs, tookMs);
    for (uint32_t i = 0; i < n; i++) {
        storeD(&c->v[i], v[i]);
    }
    __atomic_store_n(&c->n, n, __ATOMIC_RELEASE);
    __atomic_store_n(&c->seq, s + 2u, __ATOMIC_RELEASE);
}

uint32_t sys_cell_get(const SysCell *c, double *v, uint32_t n, double *atMs, double *tookMs)
{
    for (int tries = 0; tries < 4; tries++) {
        const uint32_t s1 = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u) {
            continue; /* a write in progress */
        }
        uint32_t got = __atomic_load_n(&c->n, __ATOMIC_ACQUIRE);
        got = got < n ? got : n;
        const double at = loadD(&c->atMs), took = loadD(&c->tookMs);
        for (uint32_t i = 0; i < got; i++) {
            v[i] = loadD(&c->v[i]);
        }
        if (__atomic_load_n(&c->seq, __ATOMIC_RELAXED) == s1) {
            if (got == 0) {
                return 0;
            }
            if (atMs) {
                *atMs = at;
            }
            if (tookMs) {
                *tookMs = took;
            }
            return got;
        }
    }
    return 0;
}
