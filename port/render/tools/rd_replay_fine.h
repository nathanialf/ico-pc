/* rd_replay_fine.h: rd_replay_tool --fine-check (rd_replay_fine.c) */
#ifndef ICO_RD_REPLAY_FINE_H
#define ICO_RD_REPLAY_FINE_H

/* after a replay: the shadow's levels at the scene's scale against the CPU
 * reference, printed; 0 inside the rule, 1 outside it, 2 no chain to check
 * or a readback failed */
int rd_replay_fine_check(void);

#endif
