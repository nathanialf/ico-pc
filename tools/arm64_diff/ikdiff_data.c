/*
 * tools/arm64_diff/ikdiff_data.c
 *
 * The game's globals the arm IK reads, for the arm64 differential harness
 * (tools/arm64_diff.sh): defined here, filled by ikdiff.c.  A header's
 * `extern const` declaration of a table is renamed around the #includes,
 * as port/data/gen/table_defs.c does, so the tables can be written.
 */
#define motionLimitDef motionLimitDef_header_decl
#define motionKind motionKind_header_decl
#define motionIKEffKind motionIKEffKind_header_decl
#include "typedef.h"
#include "main.h"
#include "motionOrientManager.h"
#include "handManager.h"
#undef motionLimitDef
#undef motionKind
#undef motionIKEffKind

MotOriLimit motionLimitDef[48];
MotionDef motionKind[4];
HandModeRow motionIKEffKind[7];
GenGeo objLayout[4];
int systemStatus[12] = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
PadState pad[16];
GObj *boyGObj;
/* ropeInterRate: motionOrientManager.c, linked for SetNodeRotationLimitDataTable */

int debug_now_motion_viewer;
int debug_face_rot_w_ratio;
int debug_font_flag;
int debug_actnode_flag;
int debug_skel_flag;
int debug_wallcheck_flag;
int debug_wallhitcoldisp;
int debug_col_old_proc;
int debug_mot_slope_interp;

/* the tables as ikdiff.c writes them (its headers declare them const) */
MotOriLimit *ikdiff_limits(void)
{
    return motionLimitDef;
}

MotionDef *ikdiff_motion_kind(void)
{
    return motionKind;
}

GenGeo *ikdiff_obj_layout(void)
{
    return objLayout;
}
