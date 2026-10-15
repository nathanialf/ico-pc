/* rd_content_hide.c: the content table, the stage geometry made for the
 * PS2's 4:3 picture that the native frame must not show.
 *
 * The renderer draws one camera on the native frame (vu_common.hlsli
 * vu_region_rule): a triangle that fails a VU program's region test is
 * drawn, clipped by the GPU, when it lies wholly beside the camera's
 * picture (GS X 2048 -+ 256; seen only on a frame wider than 4:3); one
 * that reaches into the picture is not drawn out of the box (the Original
 * preset at 4:3 and 1x), as the PS2 does not draw it, and in any other
 * state is drawn, clipped, when its vertices are in front of the eye and
 * it is not part of a card (a free-standing strip none of whose triangles
 * pass; rd_mesh.c markJoinedStrips).  A camera passing through scenery
 * (the opening's cliff st26a_p1, a vertex behind the eye, and tree card
 * st26a_near, a card) needs no entry.  A
 * few stage faces relied on the PS2's 4:3 picture in another way: closing
 * planes that seal a model where the 4:3 camera never looks, which pass
 * the region test beside the picture.  They are listed here as data, by
 * the stream bits of their corners (prelit and lit layouts), and rd_mesh.c
 * markContentHidden marks a triangle whose three vertices are black
 * (vertex colour 0, 0, 0, which MODULATE turns black whatever the texel)
 * and are corners of one listed entry.  Black alone does not tell such a
 * face: the stages use it for faces in full shade too (the bench's statue
 * pedestals, st17a; the pool, st09a), which every picture shows.  A model
 * pack's replacement or any other geometry never matches by accident.
 *
 * The verb (shader_consts.h ICO_VU_MARK_HIDE):
 *   RD_HIDE    drawn nowhere.  Listed only when a replay shows the
 *              triangles never reach the 4:3 picture
 *              (tools/content_table_check.py decide: the dumps at 4:3, in
 *              Original 1x and Enhanced 2x, with the entry hidden are the
 *              same, byte for byte, as without it); a face the 4:3 picture
 *              shows is left unlisted, drawn as geometry.
 * A new entry comes from a report: find the mesh (rd_replay_tool --list
 * and --mesh on the player's dump), list its black corners here, and run
 * tools/content_table_check.py decide on 4:3 dumps that draw it.
 */
#include "rd_internal.h"

/* Each row authored, decided on 4:3 dumps (the 4:3 picture never shows
   it) */
const RdContentHide rd__content_hide[] = {
    /* the long walkway (frame v31438 at 16:9): a black box 846
     * units wide through the arch at the walkway's end showed as a flat
     * grey block (the fog over black) beside the 4:3 picture; its top is a
     * four-vertex mesh of its own, its two sides the last eight vertices
     * of the next one, the textured stonework drawn behind it.  The 4:3
     * picture never shows its faces (content_table_check.py decide on
     * the 4:3 dumps v31438 and v9755: hidden, the same picture
     * byte for byte in Original 1x and Enhanced 2x): HIDE */
    {"st08a_p4: the box through the arch at the walkway's end",
     RD_HIDE,
     7,
     {{0xc518a521u, 0xc51a5cc4u, 0xc431a218u},   /* -2442.32 -2469.80  -710.53 */
      {0xc518a521u, 0xc5696b2bu, 0xc431a218u},   /* -2442.32 -3734.70  -710.53 */
      {0xc518a521u, 0xc51a5cc4u, 0xc4a092d2u},   /* -2442.32 -2469.80 -1284.59 */
      {0xc518a521u, 0xc5696b2bu, 0xc4a092d2u},   /* -2442.32 -3734.70 -1284.59 */
      {0xc4c78446u, 0xc51a5cc4u, 0xc42abb35u},   /* -1596.13 -2469.80  -682.93 */
      {0xc4c78446u, 0xc5696b2bu, 0xc42abb35u},   /* -1596.13 -3734.70  -682.93 */
      {0xc4c78446u, 0xc5696b2bu, 0xc45c93adu}}}, /* -1596.13 -3734.70  -882.31 */
};

#define RD_CONTENT_HIDE_COUNT (sizeof(rd__content_hide) / sizeof(rd__content_hide[0]))

_Static_assert(RD_CONTENT_HIDE_COUNT <= RD_CONTENT_HIDE_MAX, "the content table's size");

const int rd__content_hide_count = (int)RD_CONTENT_HIDE_COUNT;
