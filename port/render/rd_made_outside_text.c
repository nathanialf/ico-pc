/* rd_made_outside_text.c: the made-in-play line's words and its limiter
 * (rd_made_outside.h), pure. */
#include <stdio.h>

#include "rd_made_outside.h"

bool rd_made_outside_due(uint64_t n)
{
    return n != 0u && (n <= 8u || n % 100u == 0u);
}

/* "1st", "2nd", "3rd", "11th", "102nd" */
static const char *ordSuffix(uint64_t n)
{
    const uint64_t t = n % 100u;
    if (t >= 11u && t <= 13u) {
        return "th";
    }
    switch (n % 10u) {
    case 1:
        return "st";
    case 2:
        return "nd";
    case 3:
        return "rd";
    default:
        return "th";
    }
}

size_t rd_made_outside_text(char *out, size_t size, int kind, const char *name, uint64_t bytes,
                            uint32_t replay, const char *where, uint64_t nth, uint64_t kindCount,
                            uint32_t kindNames)
{
    if (out == NULL || size == 0u) {
        return 0;
    }
    const bool pipe = kind == 1; /* RHI_CREATE_PIPELINE (rhi.h) */
    char sz[48] = "";
    if (!pipe) {
        snprintf(sz, sizeof(sz), " of %llu KB", (unsigned long long)((bytes + 1023u) / 1024u));
    }
    const int n = snprintf(
        out, size,
        "made in play outside the key cache in replay %u (%s): %s \"%s\"%s (its %llu%s; %s so far "
        "%llu, of %u names%s)",
        replay, where != NULL ? where : "?", pipe ? "pipeline" : "buffer",
        name != NULL && name[0] != '\0' ? name : "unnamed", sz, (unsigned long long)nth,
        ordSuffix(nth), pipe ? "pipelines" : "buffers", (unsigned long long)kindCount, kindNames,
        nth == 8u ? "; this name's lines from here every 100th" : "");
    return n < 0 ? 0u : (size_t)n < size ? (size_t)n : size - 1u;
}
