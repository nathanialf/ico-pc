/*
 * port/fmv/rd_video.h
 *
 * The FMV's display, implemented in port/render/rd_video.c (window build
 * only; the headless build has no renderer and movie.c does not call it).
 *
 * The PS2 player bypassed the game's display entirely: it set its own
 * display environment (720 wide, the full 576/480 interlaced lines) and
 * uploaded each picture through the IPU's colour conversion (mv_disp.c).
 * The port does the same at the presenter's level: a frame goes straight to
 * the output in the presenter's 4:3 box (pillarboxed in a wider window),
 * converted from the decoder's 4:2:0 planes by yuv.hlsl with the IPU's
 * integer CSC.  The picture sits in the PS2
 * display area as mv_videodec.c placed it: centred, (dispW - w) / 2 and
 * (dispH - h) / 2, the display area filling the 4:3 box.  The box stays
 * 4:3 whatever the display options: a widescreen presentation pillarboxes
 * the movies.
 *
 * Mirror: drawn unmirrored unless the renderer's mirror mode is on
 * (rd.h rd_set_mirror); the films always follow it.
 */
#ifndef ICO_PORT_FMV_RD_VIDEO_H
#define ICO_PORT_FMV_RD_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus

extern "C" {
#endif

/* The PS2 display area the pictures are placed in (movie_init's imageW x
   imageH: 720 x 576 PAL, 720 x 480 NTSC). */
void rd_video_set_display(uint32_t dispW, uint32_t dispH);
/* Shows one decoded picture: 8-bit Y (w x h), Cb and Cr ((w+1)/2 x (h+1)/2),
   pitch[0..2] bytes per row.  Uploads, converts and presents at once.
   Returns 0, or -1 without a device. */
int rd_video_frame(const uint8_t *y, const uint8_t *u, const uint8_t *v, const uint32_t pitch[3],
                   uint32_t w, uint32_t h);

/* One decoded picture's planes, as rd_video_frame takes them. */
typedef struct RdVideoPicture {
    const uint8_t *y, *u, *v;
    uint32_t pitch[3];
} RdVideoPicture;

/* How rd_video_field fills the rows of the other field.  The player uses
   RD_VIDEO_DEINTERLACE; the weave is rd_video_test's reference, the combed
   picture the deinterlacer has to clean up. */
typedef enum RdVideoFill {
    RD_VIDEO_DEINTERLACE = 0, /* from the field itself and the pictures around it */
    RD_VIDEO_WEAVE_CUR = 1    /* the picture's own rows: the frame as decoded */
} RdVideoFill;

/* Shows one field of an interlaced picture, cur, as a whole picture:
   field 0 is the field that comes first in time (the top field when
   top_first), 1 the other; its own rows are cur's, the other rows are
   filled as fill says (RD_VIDEO_DEINTERLACE: yuv.hlsl field_sample, which
   weaves cur's own rows exactly where the three pictures do not change).
   prev and next are the pictures shown before and after cur (cur itself
   at the stream's ends), all three w x h.  Placed, converted and presented
   as rd_video_frame does, and counted with its presents. */
int rd_video_field(const RdVideoPicture *prev, const RdVideoPicture *cur,
                   const RdVideoPicture *next, uint32_t w, uint32_t h, int field, int top_first,
                   RdVideoFill fill);
/* The pictures rd_video_frame and rd_video_field put on screen since the
   program started, and in *failed (may be NULL) the ones they could not
   (no device, no swapchain image).  The window's 10 s statistics line and movie_proc's summary take
   differences: the only record in a player's log that a movie reached the
   screen, since its presents are not the game's frames. */
uint32_t rd_video_presents(uint32_t *failed);
/* Shows the display area filled with one colour (mv_disp.c's dispClear;
   GS RGBA, alpha ignored). */
int rd_video_clear(const uint8_t rgba[4]);
/* Releases the FMV's GPU objects; call before rd_shutdown (tests; the
   program leaves them to the device's teardown at exit). */
void rd_video_shutdown(void);

#ifdef __cplusplus
}

#endif
#endif /* ICO_PORT_FMV_RD_VIDEO_H */
