/* rd_vu_class.h: the VU record by compute dispatches on the PowerVR class
 * (rd_vu_class.c; docs/port/RENDER_POST.md "The VU record by compute on the
 * PowerVR class (off by default until a phone rc)").
 *
 * [dev] vu_record = auto takes the vertex shaders' stores wherever the
 * device offers them (rd_vu_record.c rd__vu_record_init), and the PowerVR
 * phone offers both kinds of store, so the record is written by two render
 * passes begun without an attachment.  This rule writes it by compute
 * dispatches instead on the PowerVR class under Vulkan (Imagination's
 * vendor id, not through MoltenVK: rd_profile.c classOf) where the device
 * offers both: the same entries, the same record bytes (rd_vu_record.h: all
 * three writes give the buffer the same bytes where any reader looks), no
 * render pass.  It changes how the record is made, so the gates are the
 * start-up self-test (rd_vuprepass_selftest.c judges the compute record as
 * it judges any start mode) and rd_vurecord_ab_compute; a compute record
 * the self-test refuses goes back to the vertex stores (rd_vu_record.c
 * rd__vu_record_fall_back), never down to the render targets first.
 *
 * On by default (RD_VU_COMPUTE_CLASS_DEFAULT 1) on the one PowerVR the
 * release candidates ran it on: the owner's phone's D-Series DXT-48-1536
 * (rd__vu_class_measured: its device name; v0.11.6-rc13, the fastest
 * opening measured there, the picture judged right).  The class is wider:
 * every Imagination device under Vulkan, the B-Series (no dualSrcBlend, no
 * GPU timestamps) and the older Rogue parts among them, none of which ever
 * ran the compute record.  Its start-up self-test judges the record's
 * bytes, but a device lost inside it (a new kind of GPU work on a driver
 * not tried) is not something a self-test can refuse, so the rest of the
 * class keeps the vertex stores, off by default with that reason.  The
 * tests' switch: ICO_RD_VU_COMPUTE_CLASS=1 takes the rule on any device
 * that offers both stores (lavapipe), =0 keeps it off on the class; no ini
 * key.  A [dev] vu_record asked for by name (stores, compute, targets)
 * always wins.  Logged once at the record's start ("VU record: the PowerVR
 * class's rule: ..."), counted in the attempt ledger's row (dispatches a
 * block). */
#ifndef PORT_RENDER_RD_VU_CLASS_H
#define PORT_RENDER_RD_VU_CLASS_H

#include <stdbool.h>

#ifndef RD_VU_COMPUTE_CLASS_DEFAULT
#define RD_VU_COMPUTE_CLASS_DEFAULT 1
#endif

/* the PowerVR the compute record was shown on (a substring of the Vulkan
   device name: "PowerVR D-Series DXT-48-1536 MC1" on the Pixel 10 Pro Fold) */
#define RD_VU_CLASS_MEASURED_NAME "PowerVR D-Series DXT-48-1536"

/* pure: name (NULL: unknown) is the measured PowerVR's */
bool rd__vu_class_measured(const char *name);

/* ICO_RD_VU_COMPUTE_CLASS: -1 unset, 0, 1 (anything else: -1, said once) */
int rd__vu_class_env(void);

/* the rule for one start (the attempt ledger's decider sets the row:
 * attempt_ledger.c ico_attempt_decide_vu_compute; its why then the row's):
 * asked the [dev] vu_record mode (RD_VU_RECORD_*), want auto's pick before
 * the rule (RD_VU_RECORD_STORES when the vertex stores are offered),
 * canCompute the device's compute stores, powervr the PowerVR class under
 * Vulkan, measured the device is the one it was shown on
 * (rd__vu_class_measured), env rd__vu_class_env's, defaultOn
 * RD_VU_COMPUTE_CLASS_DEFAULT.  True: the record by compute dispatches in
 * place of the stores */
bool rd__vu_class_pick(int asked, int want, bool canCompute, bool powervr, bool measured, int env,
                       bool defaultOn);

#endif
