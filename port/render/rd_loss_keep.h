/* rd_loss_keep.h: the memory copies of frames the GPU may not have finished
 * (rd_dump.c, "the lost frame"; rd_loss_pick.h says why and what they cost). */
#ifndef PORT_RENDER_RD_LOSS_KEEP_H
#define PORT_RENDER_RD_LOSS_KEEP_H

struct RdFrame;

/* rd_begin_frame, the game thread, before the ring's oldest slot is reset:
 * f (that slot's closed frame, may be NULL) is copied to memory when the
 * backend still has a submission open at or after its replay; the copies the
 * GPU has moved past are freed.  Cheap when nothing is open (one backend
 * progress read, a pass-ring table scan). */
void rd__loss_keep_recycle(const struct RdFrame *f);
/* the copies freed (rd_shutdown) */
void rd__loss_keep_shutdown(void);

#endif /* PORT_RENDER_RD_LOSS_KEEP_H */
