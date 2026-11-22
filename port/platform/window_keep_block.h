/*
 * port/platform/window_keep_block.h
 *
 * The loss keep's blocks (port/render/rd_loss_keep_block.h; issue 179):
 * the renderer's copies of a frame the GPU had not finished, made on the
 * game thread after a stalled slot wait, go into blocks the dump worker
 * made and touched (dump_auto.h ico_dump_auto_keep_*), given back while
 * the device's free memory is under the floor or after the system's
 * low-memory signal (diag_host.h ico_diag_background_memory, the
 * background finisher's reading).
 */
#ifndef ICO_PLATFORM_WINDOW_KEEP_BLOCK_H
#define ICO_PLATFORM_WINDOW_KEEP_BLOCK_H

/* the host handed to the renderer (rd_loss_keep_set_host), once at start */
void window_keep_block_start(void);

#endif /* ICO_PLATFORM_WINDOW_KEEP_BLOCK_H */
