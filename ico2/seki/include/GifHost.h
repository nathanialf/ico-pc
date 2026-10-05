/*
 * ico2/seki/include/GifHost.h
 *
 * PC port only (ICO_RD, the windowed build; renderer wave 2, package R2a).
 * What GifPacket.c's host path offers the rest of the seki layer besides
 * the gif_* entry points of GifPacket.h:
 *
 *   gif_HostSetTex0Resolver  the texture seam.  A TEX0 write (Texture.c's
 *                            tex_setTexReg packet, GsBase.c's work-buffer
 *                            reads, any raw gif_SetGsReg) binds the RdTex the
 *                            resolver returns for it; 0 falls through to the
 *                            named render targets (TBP 0, 0x800, 0x2800,
 *                            0x2C00, 0x3000, 0x3F00) and then to a
 *                            placeholder checker, one colour per TBP.  Until
 *                            R2b (Texture.c on rd) registers a resolver,
 *                            every game texture is a placeholder.
 *   gif_HostFlush            emits what the register decoder still holds
 *                            (a pending target change, batched primitives)
 *                            into the current list; dl_SetDLPriority and
 *                            dl_Swap call it before the list changes.
 *   gif_HostScreenPrims      rd_ScreenPrims in order with the decoder's own
 *                            output (DisplayFont.c's glyph sprites).
 *   gif_HostUndecoded*       the GS registers written through the packet
 *                            layer that the decoder does not turn into rd
 *                            state (each is also logged once).
 *
 * The decoder itself (the plan's rd_gs_shim.c) is GifPacket.c's host path:
 * docs/port/RENDER_API.md "GS register decoding (wave 2)".
 */
#ifndef GIFHOST_H
#define GIFHOST_H

#ifdef ICO_RD

#include "rd.h"

typedef RdTex (*GifTex0Resolver)(unsigned long long tex0, int list);

void gif_HostSetTex0Resolver(GifTex0Resolver fn);
void gif_HostFlush(void);
void gif_HostScreenPrims(RdPrim type, const RdScreenVtx *v, unsigned int n, RdSpace space,
                         int uvFixed);
/* writes to GS register reg (0..255) that were counted, not decoded */
unsigned int gif_HostUndecodedCount(int reg);
unsigned int gif_HostUndecodedTotal(void);
/* the placeholder bound for a game texture at tbp (R2b replaces it) */
RdTex gif_HostPlaceholder(unsigned int tbp);
/* rd was shut down and started again (tests): drop the placeholder ids */
void gif_HostForgetTextures(void);
/* the frame boundary: forget what was emitted (DisplayList.c, dl_Clear) */
void gif_HostFrameReset(void);

#endif /* ICO_RD */

#endif /* GIFHOST_H */
