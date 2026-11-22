/*
 * port/platform/vmstat_read.c
 *
 * The kernel's memory events a block (vmstat_read.h): pure.
 */
#include "vmstat_read.h"

#include <stdio.h>
#include <string.h>

/* the counts' names: exact, or every line beginning with the name
   (prefix) summed */
static const struct {
    const char *name;
    bool prefix;
    const char *say;
} k_field[VMSTAT_FIELDS] = {{"allocstall", true, "direct reclaim stalls"},
                            {"compact_stall", false, "compaction stalls"},
                            {"pgmajfault", false, "major page faults"},
                            {"pswpin", false, "swap-ins"},
                            {"pswpout", false, "swap-outs"},
                            {"workingset_refault", true, "refaults"}};

bool vmstat_read_parse(const char *buf, size_t len, VmStat *out)
{
    memset(out, 0, sizeof(*out));
    size_t i = 0;
    bool any = false;
    while (i < len) {
        size_t e = i;
        while (e < len && buf[e] != '\n') {
            e++;
        }
        /* "name value" */
        size_t sp = i;
        while (sp < e && buf[sp] != ' ') {
            sp++;
        }
        const size_t nameLen = sp - i;
        size_t d = sp;
        while (d < e && buf[d] == ' ') {
            d++;
        }
        unsigned long long v = 0;
        size_t j = d;
        while (j < e && buf[j] >= '0' && buf[j] <= '9') {
            v = v * 10u + (unsigned long long)(buf[j] - '0');
            j++;
        }
        const bool num = j > d && nameLen > 0;
        for (int f = 0; f < VMSTAT_FIELDS && num; f++) {
            const size_t n = strlen(k_field[f].name);
            const bool match = k_field[f].prefix
                                   ? nameLen >= n && memcmp(buf + i, k_field[f].name, n) == 0
                                   : nameLen == n && memcmp(buf + i, k_field[f].name, n) == 0;
            if (match) {
                out->have[f] = true;
                out->v[f] += v;
                any = true;
                break;
            }
        }
        i = e + 1;
    }
    return any;
}

size_t vmstat_delta_text(const VmStat *before, const VmStat *now, char *out, size_t cap)
{
    size_t at = 0;
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    int w = snprintf(out, cap, "memory events in the block (/proc/vmstat):");
    at = w > 0 ? ((size_t)w < cap ? (size_t)w : cap - 1u) : 0u;
    for (int f = 0; f < VMSTAT_FIELDS && at + 1u < cap; f++) {
        if (!now->have[f] || !before->have[f]) {
            w = snprintf(out + at, cap - at, "%s %s not in the file", f ? "," : "", k_field[f].say);
        } else if (now->v[f] < before->v[f]) {
            w = snprintf(out + at, cap - at, "%s %s reset", f ? "," : "", k_field[f].say);
        } else {
            w = snprintf(out + at, cap - at, "%s %s %llu", f ? "," : "", k_field[f].say,
                         now->v[f] - before->v[f]);
        }
        if (w > 0) {
            at = at + (size_t)w < cap ? at + (size_t)w : cap - 1u;
        }
    }
    return at;
}
