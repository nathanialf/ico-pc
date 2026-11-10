/* rhi_vk_pass_crumb_test.c: the render pass breadcrumb the crash report
 * prints (port/rhi/rhi_pass_crumb.c), pure: none before a pass; a pass
 * stored is inside the driver's begin until marked begun, then ended; the
 * line names the pass number, the pass ring's marker, the path, each colour
 * format and size, a missing view, the depth's format, layout, stencil and
 * read-only flags, the area and the framebuffer; a short buffer is cut and
 * terminated; the reset forgets it.
 * Exit 0 on success, 1 on a failure. */
#include "rhi_pass_crumb.h"
#include "../../test/ico_check.h"

int main(void)
{
    char line[512];
    rhi_pass_crumb_reset();
    CHECK(rhi_pass_crumb_last()->seq == 0);
    CHECK(rhi_pass_crumb_last()->phase == RHI_PASS_CRUMB_NONE);
    rhi_pass_crumb_phase(RHI_PASS_CRUMB_BEGUN); /* nothing stored: stays none */
    CHECK(rhi_pass_crumb_last()->phase == RHI_PASS_CRUMB_NONE);

    /* the crack fill's shape at 2x: SCENE's colour alone (RGBA8 37) */
    RhiPassCrumb c;
    memset(&c, 0, sizeof(c));
    c.seq = 4711;
    c.marker = 90210;
    c.colorCount = 1;
    c.colorFormat[0] = 37;
    c.colorW[0] = 1024;
    c.colorH[0] = 896;
    c.areaW = 1024;
    c.areaH = 896;
    c.fbW = 1024;
    c.fbH = 896;
    c.phase = RHI_PASS_CRUMB_ENDED; /* overwritten by the begin */
    rhi_pass_crumb_begin(&c);
    const RhiPassCrumb *l = rhi_pass_crumb_last();
    CHECK(l->seq == 4711 && l->phase == RHI_PASS_CRUMB_BEGINNING);
    rhi_pass_crumb_format(l, line, sizeof line);
    CHECK_STR(line, "the last render pass begun: #4711 (the renderer's pass ring 90210), dynamic "
                    "rendering (vkCmdBeginRendering), 1 colour (format 37 1024x896), no depth, "
                    "area 0,0 1024x896, framebuffer 1024x896; inside the driver's begin call (a "
                    "crash here is in the begin)");
    rhi_pass_crumb_phase(RHI_PASS_CRUMB_BEGUN);
    CHECK(rhi_pass_crumb_last()->phase == RHI_PASS_CRUMB_BEGUN);
    rhi_pass_crumb_format(rhi_pass_crumb_last(), line, sizeof line);
    CHECK(strstr(line, "; begun (its draws were being recorded)") != NULL, "%s", line);
    rhi_pass_crumb_phase(RHI_PASS_CRUMB_ENDED);
    rhi_pass_crumb_format(rhi_pass_crumb_last(), line, sizeof line);
    CHECK(strstr(line, "; ended") != NULL, "%s", line);

    /* two colours, one without a view, a read-only D32S8 (130) on a
       VkRenderPass */
    memset(&c, 0, sizeof(c));
    c.seq = 2;
    c.marker = 0;
    c.legacy = 1;
    c.colorCount = 2;
    c.colorFormat[0] = 37;
    c.colorFormat[1] = 9;
    c.colorW[0] = c.colorW[1] = 512;
    c.colorH[0] = c.colorH[1] = 448;
    c.viewMissing = 0x2u | 0x80u;
    c.depthFormat = 130;
    c.depthLayout = 1000117000u;
    c.hasStencil = 1;
    c.readOnlyDepth = 1;
    c.areaX = 8;
    c.areaY = 16;
    c.areaW = 100;
    c.areaH = 50;
    c.fbW = 512;
    c.fbH = 448;
    rhi_pass_crumb_begin(&c);
    const int len = rhi_pass_crumb_format(rhi_pass_crumb_last(), line, sizeof line);
    CHECK_STR(line, "the last render pass begun: #2 (the renderer's pass ring 0), a VkRenderPass "
                    "(vkCmdBeginRenderPass), 2 colour (format 37 512x448, format 9 512x448 NO "
                    "VIEW), depth format 130 (layout 1000117000, stencil, read-only, NO VIEW), "
                    "area 8,16 100x50, framebuffer 512x448; inside the driver's begin call (a "
                    "crash here is in the begin)");
    CHECK(len == (int)strlen(line), "length %d, the line's %zu", len, strlen(line));

    /* the record-stores pass before v0.11.3: no attachment */
    memset(&c, 0, sizeof(c));
    c.seq = 3;
    c.areaW = 64;
    c.areaH = 1;
    c.fbW = 64;
    c.fbH = 1;
    rhi_pass_crumb_begin(&c);
    rhi_pass_crumb_format(rhi_pass_crumb_last(), line, sizeof line);
    CHECK(strstr(line, "dynamic rendering (vkCmdBeginRendering), 0 colour, no depth, area 0,0 "
                       "64x1") != NULL,
          "%s", line);

    /* a short buffer: cut and terminated, the full length returned */
    {
        char small[24];
        memset(small, 'x', sizeof small);
        const int full = rhi_pass_crumb_format(rhi_pass_crumb_last(), small, sizeof small);
        CHECK(strlen(small) == sizeof small - 1u, "%zu", strlen(small));
        CHECK(strncmp(small, "the last render pass be", sizeof small - 1u) == 0, "%s", small);
        CHECK(full > (int)sizeof small);
        CHECK(rhi_pass_crumb_format(rhi_pass_crumb_last(), small, 0) == 0);
        CHECK(rhi_pass_crumb_format(NULL, small, sizeof small) == 0);
    }
    rhi_pass_crumb_reset();
    CHECK(rhi_pass_crumb_last()->seq == 0);
    return ico_check_summary("rhi_vk_pass_crumb");
}
