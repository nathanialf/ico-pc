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
 * integer CSC (docs/port/FMV.md, "Colour").  The picture sits in the PS2
 * display area as mv_videodec.c placed it: centred, (dispW - w) / 2 and
 * (dispH - h) / 2, the display area filling the 4:3 box.
 *
 * Mirror: drawn unmirrored unless the renderer's mirror mode is on and the
 * FMV toggle (rd_VideoSetMirror, port config "mirror_fmv", default on) is
 * set.
 */
#ifndef ICO_PORT_FMV_RD_VIDEO_H
#define ICO_PORT_FMV_RD_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus

extern "C" {
#endif

/* The PS2 display area the pictures are placed in (movie_init's imageW x
   imageH: 720 x 576 PAL, 720 x 480 NTSC). */
void rd_VideoSetDisplay(uint32_t dispW, uint32_t dispH);
/* The FMV mirror toggle (applies only while the mirror mode is on). */
void rd_VideoSetMirror(int on);
/* Shows one decoded picture: 8-bit Y (w x h), Cb and Cr ((w+1)/2 x (h+1)/2),
   pitch[0..2] bytes per row.  Uploads, converts and presents at once.
   Returns 0, or -1 without a device. */
int rd_VideoFrame(const uint8_t *y, const uint8_t *u, const uint8_t *v, const uint32_t pitch[3],
                  uint32_t w, uint32_t h);
/* Shows the display area filled with one colour (mv_disp.c's dispClear;
   GS RGBA, alpha ignored). */
int rd_VideoClear(const uint8_t rgba[4]);
/* Releases the FMV's GPU objects; call before rd_Shutdown (tests; the
   program leaves them to the device's teardown at exit). */
void rd_VideoShutdown(void);

#ifdef __cplusplus
}

#endif
#endif /* ICO_PORT_FMV_RD_VIDEO_H */
