/* rhi_adapter.c: the [video] adapter name match and the device-list text
 * (rhi_adapter.h). */
#include "rhi_adapter.h"
#include <stdio.h>
#include <string.h>

int rhi_adapter_match(const char *want, const char *const *names, int n)
{
    if (want == NULL || want[0] == '\0' || names == NULL) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (names[i] != NULL && strcmp(names[i], want) == 0) {
            return i;
        }
    }
    return -1;
}

char *rhi_adapter_list_text(const RhiAdapterInfo *list, int n, char *buf, size_t size)
{
    size_t at = 0;
    if (size == 0) {
        return buf;
    }
    buf[0] = '\0';
    for (int i = 0; i < n && at + 1 < size; i++) {
        const RhiAdapterInfo *a = &list[i];
        char score[24] = "";
        if (a->score > 0) {
            snprintf(score, sizeof(score), "%sscore %d", a->suitable ? ", " : "; ", a->score);
        }
        int w;
        if (a->suitable) {
            w = snprintf(buf + at, size - at, "%s%d %s (suitable%s)", i > 0 ? " " : "", i, a->name,
                         score);
        } else {
            w = snprintf(buf + at, size - at, "%s%d %s (unsuitable: %s%s)", i > 0 ? " " : "", i,
                         a->name, a->why != NULL ? a->why : "?", score);
        }
        if (w < 0) {
            break;
        }
        at += (size_t)w < size - at ? (size_t)w : size - at - 1;
    }
    return buf;
}
