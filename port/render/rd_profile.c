/* rd_profile.c: the device profile (rd_internal.h RdProfile).
 *
 * One renderer, one table: after rhi_init the profile is decided once from
 * RhiLimits (tiler, localRead, vendorId, the dynamic state, the UINT view),
 * the backend and the platform, and every vendor-dependent choice in the
 * renderer reads a flag of it.  The flags here are the defaults; a [dev]
 * key overrides one at a time (the blend-policy package wires them) and a
 * self-test that fails turns its flag off and says so in its own line.
 * This file is the scaffold's: every flag of the v0.7.10 mechanisms starts
 * false (today's behaviour) and its package turns it on where its design
 * says (docs/port/RENDER.md, the device profile table).
 */
#include <stdio.h>
#include <string.h>
#include "rd_internal.h"

void rd__profile_init(void)
{
    RdProfile *p = &g_rd.profile;
    const RhiLimits *lim = rhi_limits();
    memset(p, 0, sizeof(*p));
    p->tiler = lim->tiler;
    p->localRead = lim->localRead;
    /* fogLocal, dateStencil, storePlan, blurFold, uintViewWork,
     * uintViewScene, carefulSet: false until their packages land */
    snprintf(p->text, sizeof p->text, "%s GPU%s", p->tiler ? "tile-based" : "immediate-mode",
             p->localRead ? ", local read" : "");
    rd__log("profile: %s", p->text);
}

const char *rd__profile_text(void)
{
    return g_rd.profile.text;
}
