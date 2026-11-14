/* rd_health.c: the picture-health report's rule (rd_health.h says what and
 * why).  Pure: no globals, no device; the unit test is
 * port/render/test/rd_health_test.c. */
#include "rd_health.h"

#include <string.h>

#include "rd_perf_clock.h" /* rd_perf_reset_counted */

static uint32_t popc(uint32_t v)
{
    uint32_t n = 0;
    for (; v != 0u; v &= v - 1u) {
        n++;
    }
    return n;
}

void rd_health_reset(RdHealth *h)
{
    memset(h, 0, sizeof(*h));
}

bool rd_health_pic(RdHealth *h, bool judged, int sign, uint32_t *fails, uint32_t *judgedN)
{
    if (!judged || h->met) {
        return false;
    }
    h->win = (h->win << 1) | (rd_perf_reset_counted(sign) ? 1u : 0u); /* the oldest drops out */
    h->winN = h->winN < RD_HEALTH_WINDOW ? h->winN + 1u : RD_HEALTH_WINDOW;
    const uint32_t n = popc(h->win);
    if (n < RD_HEALTH_TRIGGER) {
        return false;
    }
    h->met = true;
    if (fails != NULL) {
        *fails = n;
    }
    if (judgedN != NULL) {
        *judgedN = h->winN;
    }
    return true;
}
