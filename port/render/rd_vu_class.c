/* rd_vu_class.c: the VU record by compute dispatches on the PowerVR class
 * (rd_vu_class.h). */
#include "rd_vu_class.h"

#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_vu_record.h"

int rd__vu_class_env(void)
{
    const char *e = getenv("ICO_RD_VU_COMPUTE_CLASS");
    if (e == NULL || e[0] == '\0') {
        return -1;
    }
    if (strcmp(e, "0") == 0 || strcmp(e, "off") == 0) {
        return 0;
    }
    if (strcmp(e, "1") == 0 || strcmp(e, "on") == 0) {
        return 1;
    }
    static bool s_said;
    if (!s_said) {
        s_said = true;
        rd__log("VU record: ICO_RD_VU_COMPUTE_CLASS=%s is none of 0, 1: unset", e);
    }
    return -1;
}

bool rd__vu_class_measured(const char *name)
{
    return name != NULL && strstr(name, RD_VU_CLASS_MEASURED_NAME) != NULL;
}

bool rd__vu_class_pick(int asked, int want, bool canCompute, bool powervr, bool measured, int env,
                       bool defaultOn)
{
    return ico_attempt_decide_vu_compute(asked != RD_VU_RECORD_AUTO, want == RD_VU_RECORD_STORES,
                                         canCompute, powervr, measured, env, defaultOn) != 0;
}
