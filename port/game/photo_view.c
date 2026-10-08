/*
 * port/game/photo_view.c
 *
 * Photo mode's camera in the game's matrices (photo_view.h).  Built with
 * the game's include path and record layout (it reads matrixptr, the pause
 * and the stage number), in the window and the headless build.
 */
#include <stdio.h>
#include <string.h>

#include "typedef.h"
#include "main.h"
#include "GsBase.h"

#include "photo_mode.h"
#include "photo_view.h"
#include "video_options.h"

static struct {
    int saved;
    int stage;      /* stage_no at enter */
    float d;        /* the game camera's distance (gsb_ViewFocus) */
    RdCamera game0; /* the game camera at enter */
} v;

int ico_photo_view_saved(void)
{
    return v.saved;
}

static void enter(void)
{
    gsb_PushView();
    memset(&v.game0, 0, sizeof(v.game0));
    memcpy(v.game0.view, matrixptr + 0x80, sizeof(v.game0.view));
    memcpy(v.game0.proj43, matrixptr + 0xC0, sizeof(v.game0.proj43));
    v.game0.aspect43 = 4.0f / 3.0f;
    v.d = gsb_ViewFocus();
    v.stage = stage_no;
    v.saved = 1;
    if (!ico_photo_set_game(&v.game0)) {
        fprintf(stderr, "photo: the game camera's view does not invert\n");
    }
}

static void leave(void)
{
    const int same = stage_no == v.stage && systemStatus[5] != 0;
    if (same) {
        gsb_PopView();
        gsb_MakeCommonMatrix();
    }
    v.saved = 0;
    ico_photo_set_game(NULL);
    /* the picture jumps back to the game camera: no blend into it */
    ico_video_camera_cut();
    if (!same) {
        fprintf(stderr, "photo: the stage changed or the game runs: its camera kept\n");
    }
}

void ico_photo_view_tick(void)
{
    const int on = ico_photo_active() && systemStatus[5] != 0;
    if (v.saved && (!on || stage_no != v.stage)) {
        leave();
        return;
    }
    if (!on) {
        return;
    }
    if (!v.saved) {
        enter();
    }
    RdCamera ov;
    if (!ico_photo_camera(&ov, &v.game0)) {
        ov = v.game0;
    }
    IcoPhotoState st;
    ico_photo_get(&st);
    const float k = st.zoom > 0.0f ? st.zoom : 1.0f;
    memcpy(matrixptr + 0x80, ov.view, sizeof(ov.view));
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, v.d * k);
    gsb_MakeCommonMatrix();
}
