/* rhi_adapter.h: the backends' shared rules for [video] adapter (rhi.h
 * rhi_set_adapter_wanted) and the device-list log line.  No graphics
 * header: built and tested on every host
 * (port/rhi/test/rhi_adapter_match_test.c). */
#ifndef PORT_RHI_RHI_ADAPTER_H
#define PORT_RHI_RHI_ADAPTER_H

#include <stddef.h>

#include "rhi.h"

/* The index of the first of names[0..n) equal to want byte for byte, -1
 * when none is (or want is NULL or ""); a NULL entry (a device that cannot
 * run the renderer) never matches. */
int rhi_adapter_match(const char *want, const char *const *names, int n);

/* "0 <name> (suitable, score 1065) 1 <name> (unsuitable: <why>)" for
 * list[0..n), into buf (cut at size); the score is left out where it is
 * 0 (a backend without one).  Returns buf. */
char *rhi_adapter_list_text(const RhiAdapterInfo *list, int n, char *buf, size_t size);

#endif /* PORT_RHI_RHI_ADAPTER_H */
