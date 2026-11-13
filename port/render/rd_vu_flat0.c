/* rd_vu_flat0.c: the main vertex shader's flat outputs at the provoking
 * corner, the tests' switch and the start line (rd_vu_flat0.h says what
 * and why). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_internal.h"
#include "rd_vu_flat0.h"

bool rd_vu_flat0_word(const char *e, bool *bad)
{
    if (bad) {
        *bad = false;
    }
    if (e == NULL || e[0] == '\0' || strcmp(e, "1") == 0) {
        return true;
    }
    if (strcmp(e, "0") == 0) {
        return false;
    }
    if (bad) {
        *bad = true;
    }
    return true;
}

int rd_vu_flat0_line(char *buf, size_t size, bool on, const char *missing)
{
    if (on) {
        return snprintf(
            buf, size,
            "VU flat outputs: provoking corner only (%u loads a corner-0 invocation, %u "
            "at the others: the planes read at the triangle's first corner alone, its "
            "provoking vertex on every backend; the later bias's own corner not read "
            "twice)%s%s%s",
            RD_VU_FLAT0_LOADS_FIRST, RD_VU_FLAT0_LOADS_OTHER,
            missing ? "; ICO_RD_VU_FLAT0=0 asked, but " : "", missing ? missing : "",
            missing ? " could not be made" : "");
    }
    return snprintf(buf, size,
                    "VU flat outputs: every corner (ICO_RD_VU_FLAT0=0, the tests' twins "
                    "vu_all_main_vs and vu_merge_all_main_vs: %u loads an invocation)",
                    RD_VU_FLAT0_LOADS_ALL);
}

void rd__vu_flat0_init(void)
{
    const char *e = getenv("ICO_RD_VU_FLAT0");
    bool bad = false;
    const bool on = rd_vu_flat0_word(e, &bad);
    if (bad) {
        rd__log("VU flat outputs: ICO_RD_VU_FLAT0=%s is not 0 or 1: 1", e);
    }
    const char *missing = NULL;
    if (!on) {
        RhiShader *mainVs = &g_rd.vs[RD_VS_VU_MAIN];
        RhiShader *mergeVs = &g_rd.vsMerge[RD_VS_VU_MAIN];
        const bool hasMain = mainVs->id != 0, hasMerge = mergeVs->id != 0;
        rhi_plan_shaders((hasMain ? 1u : 0u) + (hasMerge ? 1u : 0u));
        RhiShader all = {0}, mergeAll = {0};
        if (hasMain) {
            all = rd__make_shader("vu_all_main_vs");
            missing = all.id ? NULL : "vu_all_main_vs";
        }
        if (hasMerge && !missing) {
            mergeAll = rd__make_shader("vu_merge_all_main_vs");
            missing = mergeAll.id ? NULL : "vu_merge_all_main_vs";
        }
        if (missing) {
            /* the shipped ones kept (the line says so) */
            if (all.id) {
                rhi_destroy_shader(all);
            }
        } else {
            /* each twin in place of the shipped module it was made for
             * (rd_replay.c rd__gpu_shutdown and the merge's free destroy
             * them as theirs); before any pipeline is made */
            if (hasMain) {
                rhi_destroy_shader(*mainVs);
                *mainVs = all;
            }
            if (hasMerge) {
                rhi_destroy_shader(*mergeVs);
                *mergeVs = mergeAll;
            }
        }
    }
    char line[512];
    (void)rd_vu_flat0_line(line, sizeof(line), on || missing != NULL, missing);
    rd__log("%s", line);
    ico_attempt_decide_flat0(on || missing != NULL);
}
