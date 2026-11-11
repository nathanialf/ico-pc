/*
 * port/render/rd_shot_late.h
 *
 * The state picture (the pause menu's, rd.h rd_capture_shot) read at its
 * frame slot's recycle instead of with a wait of its own (issue 179: the
 * read-back's one-shot submission and queue wait held the render thread
 * 77-79 ms at each pause on the Mali-G52, and the GPU then idled 56.9 ms).
 *
 * On Vulkan on a tile-based GPU (rhi_limits()->tiler; ICO_RD_SHOT_LATE=1
 * or 0 forces it, for the tests) the present's list copies the presentation box straight into a
 * readback buffer (rhi_cmd_copy_texture_to_buffer, where it copied the
 * output into the capture texture before) and the picture is read when
 * that frame slot's fence has been waited for by the replay that reuses it
 * (rd_replay.c, after rd__wait_frame: rd__perf_collect_gpu), as the skin
 * check reads its readbacks (rd_vu_skin_check.c).  No new wait of any kind:
 * the slot's ordinary one.  The picture is the presented frame's, so it is
 * the same bytes as the old read's.  A photo (rd_capture_presented), a
 * second request while one is pending, or another backend keep the old
 * path.  A request cancelled or replaced while pending is answered with no
 * picture; the device's end answers a pending one the same way.
 * Lines: "state picture: WxH from the WxH output (the box at X,Y), read at
 * its slot's recycle N pictures later, no wait (M ms after its present)".
 */
#ifndef RD_SHOT_LATE_H
#define RD_SHOT_LATE_H

#include <stdint.h>

/* the replay that reuses frame slot `slot` has waited for its fence (the
   render thread): a state picture copied in that slot's last use is read */
void rd__shot_late_collect(uint32_t slot);

#endif /* RD_SHOT_LATE_H */
