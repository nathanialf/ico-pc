/* rhi_pass_crumb.h: the render pass a backend last began, for the crash
 * report.  No graphics header: built into its own library (ico_pass_crumb),
 * which the Vulkan backend writes and the platform's crash report
 * (port/platform/diag_host.c report_crash_block) reads, and tested on every
 * host (port/rhi/test/rhi_vk_pass_crumb_test.c).
 *
 * Before the driver's begin call (vk_renderpass.c vkr_pass_begin) the
 * backend stores the pass's shape: its colour count, formats and views,
 * its depth and stencil, its area and framebuffer size, the path
 * (dynamic rendering or a VkRenderPass), the pass's number and the
 * renderer's last checkpoint marker (its pass ring's sequence number,
 * port/render/rd_passring.h); after the call returns it marks the pass
 * begun, at its end ended.  A crash inside the driver's begin
 * (issue 123's Intel HD 530 report: igvk64.dll in vkCmdBeginRendering) then
 * names the pass in the report: "inside the driver's begin call".
 *
 * Plain stores on the recording thread, read by the crash report on any
 * thread: a few bytes may be a moment stale, which is fine for a report. */
#ifndef PORT_RHI_RHI_PASS_CRUMB_H
#define PORT_RHI_RHI_PASS_CRUMB_H

#include <stddef.h>
#include <stdint.h>

#define RHI_PASS_CRUMB_COLORS 8u

typedef enum RhiPassCrumbPhase {
    RHI_PASS_CRUMB_NONE = 0,  /* no pass begun yet */
    RHI_PASS_CRUMB_BEGINNING, /* inside the driver's begin call */
    RHI_PASS_CRUMB_BEGUN,     /* begun: its draws are being recorded */
    RHI_PASS_CRUMB_ENDED      /* ended */
} RhiPassCrumbPhase;

typedef struct RhiPassCrumb {
    uint64_t seq;    /* passes begun on this device, 1, 2, ... */
    uint64_t marker; /* the last checkpoint marker before the begin, 0: none */
    uint32_t colorCount;
    uint32_t colorFormat[RHI_PASS_CRUMB_COLORS]; /* the backend's numbers (VkFormat) */
    uint32_t colorW[RHI_PASS_CRUMB_COLORS], colorH[RHI_PASS_CRUMB_COLORS];
    uint32_t depthFormat; /* 0: no depth attachment */
    uint32_t depthLayout; /* the backend's number (VkImageLayout) */
    uint32_t areaX, areaY, areaW, areaH;
    uint32_t fbW, fbH;
    uint8_t hasStencil, readOnlyDepth;
    uint8_t legacy;      /* 1: a VkRenderPass (vkCmdBeginRenderPass), 0: dynamic rendering */
    uint8_t viewMissing; /* bit i: colour i without a view; bit 7: the depth's */
    uint8_t phase;       /* RhiPassCrumbPhase */
} RhiPassCrumb;

/* the pass about to be begun (copied; its phase set to BEGINNING) */
void rhi_pass_crumb_begin(const RhiPassCrumb *c);
/* the last pass's phase */
void rhi_pass_crumb_phase(RhiPassCrumbPhase phase);
/* the last pass stored (seq 0: none), and the store forgotten (a new device) */
const RhiPassCrumb *rhi_pass_crumb_last(void);
void rhi_pass_crumb_reset(void);
/* the pass as one line (no newline): "the last render pass begun: #<seq>
 * (the renderer's pass ring <marker>), <path>, <n> colour (format <f>
 * <w>x<h>[ NO VIEW], ...), depth format <f> (layout <l>[, stencil][,
 * read-only][, NO VIEW]) | no depth, area <x>,<y> <w>x<h>, framebuffer
 * <w>x<h>; <phase>"; the length written, as snprintf */
int rhi_pass_crumb_format(const RhiPassCrumb *c, char *out, size_t n);

#endif /* PORT_RHI_RHI_PASS_CRUMB_H */
