/* rhi_pass_crumb.c: the render pass a backend last began (rhi_pass_crumb.h). */
#include "rhi_pass_crumb.h"
#include <stdio.h>
#include <string.h>

static RhiPassCrumb s_crumb;

void rhi_pass_crumb_begin(const RhiPassCrumb *c)
{
    if (c == NULL) {
        return;
    }
    s_crumb = *c;
    s_crumb.phase = RHI_PASS_CRUMB_BEGINNING;
}

void rhi_pass_crumb_phase(RhiPassCrumbPhase phase)
{
    if (s_crumb.seq != 0) {
        s_crumb.phase = (uint8_t)phase;
    }
}

const RhiPassCrumb *rhi_pass_crumb_last(void)
{
    return &s_crumb;
}

void rhi_pass_crumb_reset(void)
{
    memset(&s_crumb, 0, sizeof(s_crumb));
}

/* snprintf's length kept within the buffer: the place the next part goes */
static size_t at(int r, size_t used, size_t n)
{
    if (r < 0) {
        return used;
    }
    const size_t next = used + (size_t)r;
    return next < n ? next : (n > 0 ? n - 1u : 0u);
}

int rhi_pass_crumb_format(const RhiPassCrumb *c, char *out, size_t n)
{
    if (out == NULL || n == 0 || c == NULL) {
        return 0;
    }
    static const char *const phases[] = {
        "no pass begun",
        "inside the driver's begin call (a crash here is in the begin)",
        "begun (its draws were being recorded)",
        "ended",
    };
    const char *phase = c->phase < sizeof phases / sizeof phases[0] ? phases[c->phase] : "?";
    size_t u = 0;
    int total = 0;
    int r = snprintf(out, n,
                     "the last render pass begun: #%llu (the renderer's pass ring %llu), %s, "
                     "%u colour",
                     (unsigned long long)c->seq, (unsigned long long)c->marker,
                     c->legacy ? "a VkRenderPass (vkCmdBeginRenderPass)"
                               : "dynamic rendering (vkCmdBeginRendering)",
                     c->colorCount);
    total += r > 0 ? r : 0;
    u = at(r, u, n);
    const uint32_t colors =
        c->colorCount < RHI_PASS_CRUMB_COLORS ? c->colorCount : RHI_PASS_CRUMB_COLORS;
    for (uint32_t i = 0; i < colors; i++) {
        r = snprintf(out + u, n - u, "%sformat %u %ux%u%s", i == 0 ? " (" : ", ", c->colorFormat[i],
                     c->colorW[i], c->colorH[i], (c->viewMissing >> i) & 1u ? " NO VIEW" : "");
        total += r > 0 ? r : 0;
        u = at(r, u, n);
    }
    if (colors) {
        r = snprintf(out + u, n - u, ")");
        total += r > 0 ? r : 0;
        u = at(r, u, n);
    }
    if (c->depthFormat != 0) {
        r = snprintf(out + u, n - u, ", depth format %u (layout %u%s%s%s)", c->depthFormat,
                     c->depthLayout, c->hasStencil ? ", stencil" : "",
                     c->readOnlyDepth ? ", read-only" : "",
                     (c->viewMissing & 0x80u) ? ", NO VIEW" : "");
    } else {
        r = snprintf(out + u, n - u, ", no depth");
    }
    total += r > 0 ? r : 0;
    u = at(r, u, n);
    r = snprintf(out + u, n - u, ", area %u,%u %ux%u, framebuffer %ux%u; %s", c->areaX, c->areaY,
                 c->areaW, c->areaH, c->fbW, c->fbH, phase);
    total += r > 0 ? r : 0;
    (void)at(r, u, n);
    return total;
}
