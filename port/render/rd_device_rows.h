/* rd_device_rows.h: the renderer's reads of the device table
 * (port/rhi/rhi_device_table.h; docs/port/DEVICE_TABLE.md "Who reads the
 * table").
 *
 * The renderer's identity-keyed choices (the profile's class and its tile
 * columns, the lamps', crack fill's and shadow tiles' classes, the stencil
 * cut, the VU record by compute, the no-volume window, the skin check, the
 * index compaction's DXT, the tile-based limits and orders) are the
 * device's evaluation of the table: rhi_device_table_current(), which every
 * backend made at its start with the tests' Vulkan fakes.  With no device
 * (before rhi_init, after rhi_shutdown, a run without a GPU: the backend's
 * limits are zero) the backend's defaults, no row: what the old rules gave
 * on the zeroed limits (a desktop class, no tile-based GPU, no phone).
 *
 * The pure predicates that keep their signatures (their tests call them
 * with ids of their own) build an identity from their arguments and read
 * the table on it; they live in rd_device_rows.c, pure (no rd__log, no
 * backend), which the equivalence test (port/rhi/test/
 * rhi_device_table_equiv_test.c) links and compares with its copies of the
 * old rules. */
#ifndef PORT_RENDER_RD_DEVICE_ROWS_H
#define PORT_RENDER_RD_DEVICE_ROWS_H

#include <stdbool.h>
#include <stdint.h>

#include "rhi.h"
#include "rhi_device_table.h"

/* The device's value of a choice (rd_profile.c): rd_device_rows_value with
   the table's current evaluation, the backend in use, and whether its limits
   are a device's (RhiLimits.maxRenderTargetSize non-zero) */
int32_t rd__device_value(RhiDeviceChoice choice);

/* pure: current (NULL: none yet) when the limits are live, else the
   backend's defaults (an identity of the backend alone: no row matches) */
int32_t rd_device_rows_value(const RhiDeviceChoices *current, int backend, bool liveLimits,
                             RhiDeviceChoice choice);

/* pure: the profile class (RHI_DT_CLASS_*) is one of the two Adreno ones */
bool rd_device_rows_adreno(int32_t profileClass);

/* pure: the reason the row id gives for choice (the turnip row's VU set
   words), NULL none */
const char *rd_device_rows_reason(const char *rowId, RhiDeviceChoice choice);

/* The pure predicates that keep their signatures (declared where they always
 * were too: rd_internal.h, rd_vu_skin_check.h, rd_vu_class.h):
 *   rd__driver_is_turnip       the table's Turnip driver test (driver id 18,
 *                              or "turnip" in RhiLimits.driverText, no vendor
 *                              test: the turnip row's match)
 *   rd__profile_vendor_adreno  PROFILE_CLASS one of the Adreno ones on the
 *                              Vulkan identity of a vendor id, MoltenVK or not
 *   rd__vu_class_measured      VU_COMPUTE_CLASS measured on the identity of an
 *                              Imagination GPU of that device name */
bool rd__driver_is_turnip(const RhiLimits *lim);
bool rd__profile_vendor_adreno(uint32_t vendorId, bool moltenvk);
bool rd__vu_class_measured(const char *name);

#endif /* PORT_RENDER_RD_DEVICE_ROWS_H */
