/*
 * ico2/seki/include/GifHost.h
 *
 * PC port only (ICO_RD, the windowed build).
 * What GifPacket.c's host path offers the rest of the seki layer besides
 * the gif_* entry points of GifPacket.h:
 *
 *   gif_HostSetTex0Resolver  the texture seam.  A TEX0 write (Texture.c's
 *                            tex_setTexReg packet, GsBase.c's work-buffer
 *                            reads, any raw gif_SetGsReg) binds the RdTex the
 *                            resolver returns for it; 0 falls through to the
 *                            named render targets (TBP 0, 0x800, 0x2800,
 *                            0x2840, 0x2C00, 0x3000, 0x3F00), a block drawn
 *                            this frame (gif_HostWriteRegs) and then to a
 *                            placeholder checker, one colour per TBP.
 *                            Texture.c's tex_Init registers texHostResolve,
 *                            so the placeholder is left for a TBP no texture
 *                            and no target holds.
 *   gif_HostFlush            emits what the register decoder still holds
 *                            (a pending target change, batched primitives)
 *                            into the current list; dl_SetDLPriority and
 *                            dl_Swap call it before the list changes.
 *   gif_HostScreenPrims      rd_screen_prims in order with the decoder's own
 *                            output (DisplayFont.c's glyph sprites).
 *   gif_HostDrawKey          the RdKey the decoder's primitives carry
 *                            from here: a change emits the batch first, so
 *                            a batch has one key.
 *   gif_HostUndecoded*       the GS registers written through the packet
 *                            layer that the decoder does not turn into rd
 *                            state (each is also logged once).
 *
 * The decoder itself is GifPacket.c's host path.
 */
#ifndef GIFHOST_H
#define GIFHOST_H

#ifdef ICO_RD

#include "rd.h"

typedef RdTex (*GifTex0Resolver)(unsigned long long tex0, int list);

void gif_HostSetTex0Resolver(GifTex0Resolver fn);
void gif_HostFlush(void);
/* the native frame width the decoder's field widths and work-buffer
   classes read: gif_HostSetFrameWidth / gif_HostFrameWidth, GifPacket.h
   (every build has them) */
void gif_HostScreenPrims(RdPrim type, const RdScreenVtx *v, unsigned int n, RdSpace space,
                         int uvFixed);
/* writes to GS register reg (0..255) that were counted, not decoded */
unsigned int gif_HostUndecodedCount(int reg);
unsigned int gif_HostUndecodedTotal(void);
/* the placeholder bound for a TBP the resolver, the named targets and the
   frame's drawn blocks do not hold */
RdTex gif_HostPlaceholder(unsigned int tbp);
/* rd was shut down and started again (tests): drop the placeholder ids */
void gif_HostForgetTextures(void);
/* the frame boundary: forget what was emitted (DisplayList.c, dl_Clear) */
void gif_HostFrameReset(void);
/* n A+D pairs (data, register: two doublewords each, as in a PACKED A+D
   GIF packet) that reach the GS by another path than the open packet (VU1
   SET_GSREGISTER, a mesh batch tag's PRIM), decoded in order after what the
   open packet holds.  A FRAME.FBP that names no fixed buffer draws into a
   temporary target for the rest of the frame, and a TEX0 with that block's
   TBP samples it. */
void gif_HostWriteRegs(const unsigned long long *ad, unsigned int n);
/* The RdKey of the 2D primitives decoded from here, RD_KEY(obj, part,
   ordinal), so the presenter can blend a layout row, a subtitle or a font
   string between two ticks; (0, 0, 0) ends it (unkeyed).  A different key emits
   what the decoder batches first.  gif_HostDrawKeyText keys by a hash of
   the string s (DisplayFont.c's font_Print). */
void gif_HostDrawKey(const void *obj, int part, int ordinal);
void gif_HostDrawKeyText(const char *s, int part);
/* PC port: on != 0 until called with 0, the primitives decoded were
   projected on the CPU through the frame camera (+0x80 then +0xC0, or
   +0x100), with their real projected Z: rd_screen_prims marks them
   (rd.h rd_frame_projected) unless a reflection camera is pushed, and the
   presenter sees them through the camera it blends between ticks, as it
   does the VU draws around them.  The GIF_SP_WORLD helpers of GifPacket.c
   (rotTransPers) are marked without it. */
void gif_HostFrameProjected(int on);
/* PC port: until called with (0, 0), the frame-projected primitives decoded
   have STQs that follow their screen position: S / Q grows by dsx and T / Q
   by dsy per 12.4 unit of GS X and Y (puddle.c drawRipples, whose STs into
   the reflection block are its projected points scaled), so the presenter
   moves each vertex's S / Q and T / Q with the vertex where it re-projects
   it (rd.h rd_frame_projected_st) */
void gif_HostFrameProjectedSt(float dsx, float dsy);

#endif /* ICO_RD */

#endif /* GIFHOST_H */
