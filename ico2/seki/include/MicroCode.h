/*
 * ico2/seki/include/MicroCode.h
 *
 * The declarations of what MicroCode.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MICROCODE_H
#define MICROCODE_H

/* MicroCode.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void mc_TransMicroCode(int id, int mask);
void mc_Reset(void);
void mc_Init(void);
void mc_SetMicroCode(int mode, int light, int pass, int clip, int pri);
/* MicroCode.c's one .data object, read from DisplayP2O.c as well: the VU1
   microprogram address per microprogram id. */
extern int MicroCodeAddress[];

#ifdef ICO_RD
/* PC port (renderer wave 3, R3ab): what a DMA chain the seki layer chains for
   VU1 does to the current list's VU state and the GS, read on the host as
   the VIF would (MicroCode.c): id and qwc as dl_OpenDma takes them (5: a
   call into a cnt/ret chain, 2: qwc quadwords of VIF codes). */
void mc_HostDma(int id, const void *addr, int qwc);
/* Package I1: the emitter (prim_DispParticle's PrimParticle) whose particle
   batches the next mc_HostDma calls draw, keyed by it for the presenter's
   matching; 0 when its chain is done (a
   batch from elsewhere takes rd_mesh.c's list key). */
void mc_HostParticleKey(const void *emitter);
#endif

#endif /* MICROCODE_H */
