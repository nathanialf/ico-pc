/* rd_present_info.c: rd_last_present_info's record (rd.h RdPresentInfo):
 * what the last present drew, noted by rd_present (rd_interp.c) and by
 * rd_end_frame's present with interpolation off (rd__note_frame_present). */
#include <string.h>
#include "rd_internal.h"

/* valid once a present ran */
static RdPresentInfo s_info;
static bool s_infoValid;

void rd__present_info_reset(void)
{
    memset(&s_info, 0, sizeof(s_info));
    s_infoValid = false;
}

void rd__present_info_note(const RdFrame *cur, uint32_t firstOfTick, float alpha)
{
    s_info.frame = cur->number;
    s_info.keep = cur->keep;
    s_info.fade = cur->fade;
    s_info.snap = (uint32_t)rd__interp_snap(rd__prev_frame(), cur);
    s_info.firstOfTick = firstOfTick;
    s_info.alpha = alpha;
    s_infoValid = true;
}

/* with interpolation off rd_end_frame presents each frame once, whole: the
 * tick's own picture */
void rd__note_frame_present(const RdFrame *f)
{
    rd__present_info_note(f, 1, 1.0f);
}

bool rd_last_present_info(RdPresentInfo *out)
{
    if (!out || !s_infoValid) {
        return false;
    }
    *out = s_info;
    return true;
}
