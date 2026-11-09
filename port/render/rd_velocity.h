/*
 * port/render/rd_velocity.h
 *
 * Per-pixel motion vectors of the scene (docs/port/RENDER.md, "Velocity"):
 * the temporal upscaler's input, made only while the temporal upscaler is
 * on (FSR 3.1 in force, the test upscaler, rd_upscale.c syncVelocity) or
 * the developer switch asks ([dev] velocity_test, ICO_RD_VELOCITY=1,
 * rd_replay_tool --velocity).  Off, nothing here
 * runs: no payload is added to a frame, no texture or pipeline is made and
 * the pictures are byte for byte those of a build without it.
 *
 * The source rule (rd_velocity.c rd__velocity_attach).  Each VU mesh, skinned
 * mesh and grid draw of the frame about to be drawn carries the transform the
 * same object had in the picture drawn before it, its "VuPrevCB": a copy of
 * the fixed part of that draw's payload (RdVuPayload, the VU block, the
 * bones, a grid's stream; rd_mesh.h), appended to the frame's payload and
 * named by the draw's u[3] (RD_VU_PREV below).  The picture drawn before is
 * the last present's frame: with the interpolation the blended frame
 * rd_interp.c built for the present before (so the motion is that between
 * the two presents, blended(alpha now) - blended(alpha before), across a
 * tick's boundary too), without it the tick before's frame.  The draws are
 * paired as rd_interp.c pairs a draw with the tick before's (key, type,
 * list and call ordinal; the meshes and grids of a key drawn two or more
 * times by their model origin in the world first, rd_interp.c placeSlot) and
 * must have the same shape (mesh, program, batch range, bone, stream and
 * vertex counts: blendVu's header test).  A draw with no pair, a jump past
 * RD_INTERP_JUMP_WORLD, a particle batch and every draw of a frame after a
 * camera cut carry none: their velocity is 0 (the cut also asks the
 * upscaler to drop its history: RD_UPSCALE_NO_HISTORY).
 */
#ifndef RD_VELOCITY_H
#define RD_VELOCITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"
#include "rd_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A VU draw's u[3] (beside RD_VU_TICK_CAMERAS, bit 0): bit 1 set, the draw
 * carries its VuPrevCB at payload offset u[3] & RD_VU_PREV_OFF (payload
 * offsets are 8-aligned, rd__frame_payload, so bits 0..2 are free).  0 in
 * every frame recorded or blended with velocity off and in every dump from
 * before version 16. */
#define RD_VU_PREV 2u
#define RD_VU_PREV_OFF (~7u)

/* An RDC_UPSCALE_POINT's u[2] (rd__velocity_attach sets it, 0 in a frame
 * never attached and in every dump made before it was set): the frame
 * begins a new history (a camera cut, RdCamera.cut, FrameCB's eye w == 1;
 * the first frame; a frame after the switch went on or after
 * rd__velocity_forget), so the temporal upscaler drops its history at this
 * frame's dispatch (rd_upscale.c rd__upscale_point, which also takes the
 * frame's own cut). */
#define RD_UPSCALE_NO_HISTORY 1u

/* The bytes of a VU payload's fixed part that VuPrevCB copies: the header,
 * the VU block, the bones and the stream (not the materials and cards). */
static inline uint64_t rd__velocity_prev_size(const RdVuPayload *h)
{
    return (uint64_t)sizeof(RdVuPayload) + sizeof(RdVuBlock) +
           ((uint64_t)h->boneQw + h->streamQw) * 16u;
}

/* The VuPrevCB of draw c of frame f: its bytes, NULL when it has none or
 * they do not lie in the payload with the draw's own header (a dump's bytes
 * are checked again here). */
const uint8_t *rd__velocity_prev(const RdFrame *f, const RdCmd *c);

/* The temporal upscaler's switch (the plan's g_rd.upscaleTemporal; kept
 * here: rd_internal.h is at its size cap).  The first ask reads
 * ICO_RD_VELOCITY ([dev] velocity_test) unless rd__velocity_set came first. */
bool rd__velocity_on(void);
void rd__velocity_set(bool on);

/* [dev] velocity_sign (ICO_RD_VELOCITY_SIGN): 1, the shader's convention,
 * "current to previous" (the vector from a pixel to where its surface was
 * in the picture before, in pixels of the render target, +x right, +y
 * down: what FidelityFX's upscaler takes), or -1.  The shader never
 * changes; the upscaler multiplies its motion vector scale by this (the
 * desktop check with FSR's debug view). */
float rd__velocity_sign(void);

/* The source rule: every VU draw of f gets its VuPrevCB or none (above),
 * f's upscale point whether it begins a new history (RD_UPSCALE_NO_HISTORY),
 * then f's draws are kept as the picture before for the next call.
 * rd_interp.c calls it on each present's blended frame; rd_end_frame on the
 * tick's frame when the interpolation is off.  Nothing when the switch is
 * off. */
void rd__velocity_attach(RdFrame *f, int keep);

/* Drops the picture before (the next frame is a new history). */
void rd__velocity_forget(void);

/* The counters of the last rd__velocity_attach and of the replays since the
 * last 10 s line ("velocity: ...", written by rd__velocity_attach while the
 * switch is on), for the replay tool and the tests. */
typedef struct RdVelocityStats {
    uint32_t vuDraws;               /* the frame's mesh, skinned and grid draws */
    uint32_t paired;                /* with a VuPrevCB */
    uint32_t moved;                 /* of them, whose VuPrevCB differs from their own fixed part */
    uint32_t unpaired, shape, jump; /* without one: no pair, another shape, a jump */
    uint32_t reset;                 /* the frame began a new history (its marker's u[2]) */
    /* the replay (rd_replay_vu.c): draws drawn into VELOCITY, draws of list 0
       into SCENE with depth writes left out (drawn the old way, without the
       record, or multisampled), the passes */
    uint32_t drawn, noRecord, passes;
} RdVelocityStats;

const RdVelocityStats *rd__velocity_stats(void);

/* ----------------------------------------------------- the replay side */

/* The VELOCITY target (the plan's RD_TARGET_VELOCITY): RGBA16F at SCENE's
 * texture size, xy the motion in pixels, w 1 where a draw wrote it, cleared
 * to 0 each replay; made at the first replay that needs it, again when
 * SCENE's size changes.  False when it cannot be had (no device, the switch
 * off, no SCENE). */
bool rd__velocity_target(RhiTexture *tex, RhiState **state, uint32_t *w, uint32_t *h);

/* The velocity pass's pipeline: vu_velocity_main_vs + velocity_ps,
 * RGBA16F, SCENE's D32F_S8 read-only, depth EQUAL, no write; {0} when a
 * module is missing (logged once). */
RhiPipeline rd__velocity_pipeline(void);

/* The exact viewport's extent for a target of n texels (rd_replay.c
 * vpExtent's rule): the velocity pass's viewport is the scene passes'. */
uint32_t rd__velocity_vp_extent(uint32_t n);

/* VELOCITY read back as floats (RGBA, w * h * 4); false without it. */
bool rd__velocity_read(float *dst, size_t floats, uint32_t *w, uint32_t *h);

/* the replay's counters (rd_replay_vu.c), at the end of each velocity pass */
void rd__velocity_note_replay(uint32_t drawn, uint32_t noRecord);
/* a VU draw that gets no motion vector, by the gate that refused it: 0 outside list 0 (or a
 * particle draw), 1 with Z write off, 2 not into SCENE (the 10 s velocity line's counts) */
void rd__velocity_note_skip(int why);
/* The replay (g_rd.replayCounter) whose velocity pass last cleared and drew
 * VELOCITY, 0 before any: a replay with no VU work runs no pass, and the
 * upscaler then clears VELOCITY itself (rd_fsr3.c) rather than read the
 * motion of an older picture */
uint32_t rd__velocity_pass_replay(void);

void rd__velocity_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
