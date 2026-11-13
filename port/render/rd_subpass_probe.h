/* rd_subpass_probe.h: the subpass read's first-start probe (rd_subpass_probe.c;
 * port/rhi/rhi_subpass_verdict.h; docs/port/RENDER.md, "The subpass read's
 * probe").
 *
 * Where a Vulkan device made the subpass read's shape for this start's probe
 * (RhiLimits.subpassProbe: VkRenderPass objects without local read, GPU
 * timestamps, no verdict kept for this device and driver), DATE's self-test
 * (rd_date_selftest.c) first checks DATE through the read on the device;
 * then, if it passed, this probe times two things on SCENE at its real size
 * and on a target of its size, alternating the ways, the least GPU time of
 * each way from the replays' timestamps:
 *   the read      a frame of full-frame layers (Z GEQUAL, Z written, half
 *                 the cells alpha 0x80) and three DATE sprites (DATM 0, the
 *                 alpha masked: one set-up or one snapshot for the three, as
 *                 a game picture under one DATE set-up's sharing): DATE's
 *                 set-up inside the pass against the R8 snapshot;
 *   a plain pass  layers on a colour target made with the read's shape (the
 *                 input-attachment usage, GENERAL) against one made without
 *                 it (RHI_TEX_NO_SUBPASS_READ), four passes a replay, each
 *                 loading and storing the target: what the shape costs every
 *                 ordinary pass of the run.
 * The rule (rd__subpass_probe_keep): the read stays only when its least time
 * is not more than RD_SUBPASS_PROBE_TOLERANCE above the snapshot's and the
 * shaped plain pass's not more than the larger of that tolerance and the
 * plain twin's own spread (the probe's noise) above the twin's.  The verdict
 * (kept or refused, both times each way) is written to the device's verdict
 * file, so the next starts of this driver decide without a probe; refused,
 * DATE takes its R8 snapshot for the rest of this start ("DATE stencil off
 * (probe)") with the shape still made, and from the next start the device
 * makes no shape at all. */
#ifndef PORT_RENDER_RD_SUBPASS_PROBE_H
#define PORT_RENDER_RD_SUBPASS_PROBE_H

#include <stdbool.h>
#include <stddef.h>

/* the local-read probe's (RD_LOCAL_PROBE_TOLERANCE): 5 % */
#define RD_SUBPASS_PROBE_TOLERANCE 0.05

/* rd__subpass_probe_keep's answers */
typedef enum RdSubpassProbeRule {
    RD_SUBPASS_PROBE_KEEP = 0,     /* both within their bounds */
    RD_SUBPASS_PROBE_READ_SLOWER,  /* the read's frame slower than the snapshot's */
    RD_SUBPASS_PROBE_PLAIN_SLOWER, /* the shape made a plain pass slower */
    RD_SUBPASS_PROBE_UNTIMED,      /* a time missing, zero or not a number */
} RdSubpassProbeRule;

/* The rule on the least times (ms) of the four ways: readMs the read's
 * frame, snapMs the snapshot's, shapedMs the plain pass on the shaped
 * target, plainMs on its twin; plainSpread the twin's own spread, (its
 * largest - its least) / its least (0 with one time). */
RdSubpassProbeRule rd__subpass_probe_keep(double readMs, double snapMs, double shapedMs,
                                          double plainMs, double plainSpread);

/* The probe's name as a phase of the run (diag_host.h ico_diag_set_probe) */
#define RD_SUBPASS_PROBE_NAME "the subpass read's probe"

/* At the renderer's start (rd_profile.c rd__profile_init), where the device
 * made the read's shape for this start's probe: the window's phase hook
 * (rd.h rd_set_probe_fn) gets RD_SUBPASS_PROBE_NAME, so a loss, GPU hang or
 * end of the run before the verdict counts on no loss step; the hook gets
 * NULL once the verdict is written or the probe is given up. */
void rd__subpass_probe_begin(void);

/* At DATE's self-test's start (rd_date_selftest.c), once a renderer: the
 * attempt ledger's subpass read row from the device's decision (the row's
 * state where no probe runs at this start). */
void rd__subpass_probe_report(void);

/* After DATE's self-test (rd_date_selftest.c): ran 0 when it did not run
 * (the profile or a [dev] key draws DATE by the snapshot), else passed its
 * verdict on the stencil path and words its result ("stencil ok 5, snapshot
 * ok 5").  Runs the probe where the device asks for it
 * (RhiLimits.subpassProbe) and writes the verdict; nothing elsewhere. */
void rd__subpass_probe_after_selftest(int ran, bool passed, const char *words);

#endif
