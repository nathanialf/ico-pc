/* rhi_bind_cache.c: the bind cache's off switch (rhi_bind_cache.h). */
#include "rhi_bind_cache.h"

#include <stdlib.h>
#include <string.h>

/* -1 unread; the first read is the backend's start-up line, before any
 * command list, so the later reads (one per bind or state set) see one
 * value for the run */
static int s_enabled = -1;

bool rhi_bind_cache_enabled(void)
{
    if (s_enabled < 0) {
        const char *v = getenv("ICO_RHI_BIND_CACHE");
        const bool off = v && (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 ||
                               strcmp(v, "off") == 0 || strcmp(v, "no") == 0);
        s_enabled = off ? 0 : 1;
    }
    return s_enabled != 0;
}

const char *rhi_bind_cache_words(void)
{
    return rhi_bind_cache_enabled() ? "on" : "off ([dev] bind_cache = false, ICO_RHI_BIND_CACHE=0)";
}
