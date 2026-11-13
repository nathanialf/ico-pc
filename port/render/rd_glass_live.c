/* rd_glass_live.c: the display's times read back into the join (rd_glass.h,
 * rhi.h rhi_present_glass), on the thread that collects the records */
#include "rd_glass.h"
#include "rd_internal.h"
#include "rhi.h"

void rd_glass_picture(const RdGlassPic *p)
{
    if (!g_rd.hasDevice) {
        return;
    }
    RhiGlassTime t[64];
    uint64_t refresh = 0;
    uint32_t n;
    do {
        n = rhi_present_glass(NULL, &refresh, t, 64);
        for (uint32_t i = 0; i < n; i++) {
            rd_glass_time(t[i].id, t[i].actualNs, refresh);
        }
    } while (n == 64);
    rd_glass_join(p);
}
