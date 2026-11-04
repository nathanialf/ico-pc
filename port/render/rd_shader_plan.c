/* rd_shader_plan.c: the size of rd__gpu_init's shader set, counted before
 * it is made (rd_internal.h rd__shader_plan).
 *
 * The device's start makes its shaders one after the other (rd_replay.c
 * rd__gpu_init: the vertex entries, the VU policy's plain modules, the
 * fragment entries with their _nodual and destination-read twins).  On
 * OpenGL that is where the time goes on a cold start (NVIDIA's GLSL
 * compiler: a long while for the set on an RTX 3090, the rc3 report),
 * and nothing of the renderer can draw yet, so the backend draws its own
 * progress screen from the count (rhi.h rhi_plan_shaders, port/rhi/gl/
 * gl_start_screen.c).  The count walks the same tests rd__gpu_init's loops
 * make, in the same order, and counts each name rd__make_shader would hand
 * to rhi_create_shader (in the table, with this backend's form: OpenGL's
 * GLSL); the VU policy's plain modules are added by rd__vu_policy_init when
 * it takes them (rd_pipeline.c), since only it decides.  Should the count
 * and the loops drift apart, the dispatcher's line at the first frame says
 * by how much (rhi_backend.c), and the screens only count wrong. */
#include <stdio.h>
#include <string.h>

#include "rd_internal.h"
#include "shader_consts.h"
#include "shaders_gen.h"

/* rd__make_shader calls rhi_create_shader for name */
static bool planMade(const char *name)
{
    const IcoShaderBlob *b = ico_find_shader(name);
    return b && (rhi_backend() != RHI_BACKEND_OPENGL || b->glsl);
}

uint32_t rd__shader_plan(const RhiLimits *lim, bool dstStorage, bool dstAttach)
{
    uint32_t n = 0;
    char twin[64];

    /* the optional entries where built (rd__gpu_init's optionalBuilt is
     * planMade's test), the rest asked for and made where planMade holds */
    for (int i = 0; i < RD_VS_COUNT; i++) {
        n += planMade(rd__vs_name((uint8_t)i)) ? 1u : 0u;
    }
    for (int i = 0; i < RD_FS_COUNT; i++) {
        const char *name = rd__fs_name((uint8_t)i);
        if (i == RD_FS_FOG_MS && !lim->sampleRateShading) {
            continue;
        }
        if (i >= RD_FS_OPTIONAL_FIRST && !planMade(name)) {
            continue;
        }
        /* rd__make_dst_twins (rd_uber.c) */
        if (rd__fs_has_dst((uint8_t)i)) {
            if (dstStorage) {
                snprintf(twin, sizeof(twin), "%s_dstrov", name);
                n += planMade(twin) ? 1u : 0u;
            }
            if (dstAttach) {
                snprintf(twin, sizeof(twin), "%s_dstatt", name);
                n += planMade(twin) ? 1u : 0u;
            }
        } else if (dstAttach && i == RD_FS_VU_NP && rd__uber_wanted(NULL)) {
            snprintf(twin, sizeof(twin), "%s_dstatt", name);
            n += planMade(twin) ? 1u : 0u;
        }
        if (rd__fs_has_no_dual((uint8_t)i)) {
            snprintf(twin, sizeof(twin), "%s_nodual", name);
            n += planMade(twin) ? 1u : 0u;
            if (!lim->dualSourceBlend) {
                continue;
            }
        }
        n += planMade(name) ? 1u : 0u;
    }
    return n;
}
