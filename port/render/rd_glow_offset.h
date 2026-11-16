/* rd_glow_offset.h: how far the held glow's camera is from the present's
 * (issue 123's right wall: the held glow is one of the things not tied to the
 * present's blended camera that can slide over a wall; build-host's WALL123
 * hand-back, candidate 2).
 *
 * Under the held glow (rd_glow_hold.c) a later present of a tick pastes the
 * glow its tick's first present drew, through that present's blended camera.
 * The offset said is the frame centre's direction: the direction the first
 * present's screen-view puts at GS (2048, 2048), projected through the later
 * present's, its distance from the centre in GS pixels.  A direction (a point
 * at infinity) moves with the camera's turn and not with its eye, so this is
 * the pan's share of the slide; a near wall slides more by the eye's move.
 * Each held present adds one offset; the glow's 250-frame line says their
 * median and largest.  Pure (port/render/test/rd_glow_offset_test.c). */
#ifndef ICO_RD_GLOW_OFFSET_H
#define ICO_RD_GLOW_OFFSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The frame centre's direction of the screen-view a (world to GS screen,
   column-major 4 x 4, RdFrame.vu.screenView) projected through b: its
   distance from GS (2048, 2048) in GS pixels into *px.  False when a puts
   no direction at the centre in front of the eye, or b puts it behind */
bool rd_glow_offset(const float a[16], const float b[16], double *px);

#define RD_GLOW_OFFSETS 512u /* the offsets kept for the median (the first of a window) */

typedef struct RdGlowOffsets {
    uint32_t n;       /* added */
    uint32_t refused; /* held presents whose offset could not be taken */
    double max;
    float v[RD_GLOW_OFFSETS];
} RdGlowOffsets;

void rd_glow_offsets_reset(RdGlowOffsets *o);
void rd_glow_offsets_add(RdGlowOffsets *o, double px);
/* "; the held glow's camera against the present's (the frame centre's
   direction): median 1.2, largest 4.5 GS pixels over 44 held presents" (2
   not taken: ", 2 not taken"); "" with none added or refused.  The values
   are sorted in place.  The length snprintf gives */
int rd_glow_offsets_text(RdGlowOffsets *o, char *out, size_t cap);

#endif
