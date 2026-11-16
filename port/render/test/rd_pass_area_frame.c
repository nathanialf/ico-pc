/* rd_pass_area_frame: a small frame recorded through the rd_* calls whose
 * last pass on SCENE takes a render area (rd_pass_area.c; docs/port/
 * WORK_BUFFERS.md, "The pass area"), dumped for the fixture fx-pass-area
 * (docs/port/TESTING_FIXTURES.md, "Hand-writing a fixture").  No device:
 * the renderer records only (rd__init_record_only), nothing is replayed.
 *
 * No frame of the game measured takes an area at the tests' settings (the
 * stage 6 frames' passes on SCENE draw the frame's rect; the others hold a
 * 3D draw, the fog, a clear as their load or a post sprite), so
 * rd_pass_area_ab needs this one.  The frame, by rd_pass_area.c's rule (a
 * pass begun on a target with its own depth at a 2D draw with a known box,
 * without a CLEAR load, whose 2D draws up to the next draw on another target
 * or the next clear, copy or command the walk does not replay leave at least
 * a 32 x 32 tile column or row of the target untouched):
 *   list 0  SCENE's colour and depth cleared (a pending clear)
 *   list 1  a flat sprite over the whole of SCENE: its pass takes the clear
 *           as its load, so it is whole ("a clear as its load")
 *   list 2  a flat sprite on WORK0: a draw on another target, which ends
 *           SCENE's pass
 *   list 3  two flat sprites on SCENE, (64,64)-(160,128) and (96,96)-
 *           (224,176) GS pixels: a pass begun at a 2D draw with a box, no
 *           clear left to take: its area is their union aligned out to
 *           whole tiles, narrower than SCENE both ways
 *   list 4  a flat sprite on WORK0 again, then one on SCENE at
 *           (320,256)-(384,320): a second pass with an area of its own
 * Flat colours, no texture: the frame holds no art.
 *
 * usage: rd_pass_area_frame DIR   (writes DIR/rd_pass_area_frame.rddump)
 * exit 0 written, 1 not. */
#include <stdio.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_state.h"

#define GS_W 512u
#define GS_H 448u

/* GS 12.4 window coordinates of GS pixel (x, y) of a gsW x gsH target drawn
 * with XYOFFSET at its centre (rd_set_target) */
static RdScreenVtx vtx(uint32_t gsW, uint32_t gsH, int x, int y, const uint8_t c[4])
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (int32_t)((2048 - (int)gsW / 2 + x) * 16);
    v.y = (int32_t)((2048 - (int)gsH / 2 + y) * 16);
    v.z = 0;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

/* a flat sprite (no texture, Z GEQUAL against the cleared 0, no Z write) */
static void sprite(uint32_t gsW, uint32_t gsH, int x0, int y0, int x1, int y1, const uint8_t c[4])
{
    rd_texture_off();
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(0);
    RdScreenVtx v[2] = {vtx(gsW, gsH, x0, y0, c), vtx(gsW, gsH, x1, y1, c)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

int main(int argc, char **argv)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80}, grey[4] = {0x40, 0x40, 0x40, 0x80};
    static const uint8_t red[4] = {0x80, 0x20, 0x20, 0x80}, blue[4] = {0x20, 0x20, 0x80, 0x80};
    static const uint8_t green[4] = {0x20, 0x80, 0x20, 0x80};
    const char *dir = argc > 1 ? argv[1] : ".";
    const RdTarget scene = rd_target(RD_TARGET_SCENE), work0 = rd_target(RD_TARGET_WORK0);
    char path[1024];

    if (!rd__init_record_only(GS_W, GS_H)) {
        printf("rd_pass_area_frame: no recording context\n");
        return 1;
    }
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(scene, black, 1, 0);
    rd_set_target(scene, scene, GS_W, GS_H, 1);
    rd_select_list(1);
    rd_set_target(scene, scene, GS_W, GS_H, 1);
    sprite(GS_W, GS_H, 0, 0, (int)GS_W, (int)GS_H, grey);
    rd_select_list(2);
    rd_set_target(work0, (RdTarget){0}, 256, 128, 0);
    sprite(256, 128, 0, 0, 64, 32, blue);
    rd_select_list(3);
    rd_set_target(scene, scene, GS_W, GS_H, 1);
    sprite(GS_W, GS_H, 64, 64, 160, 128, red);
    sprite(GS_W, GS_H, 96, 96, 224, 176, green);
    rd_select_list(4);
    rd_set_target(work0, (RdTarget){0}, 256, 128, 0);
    sprite(256, 128, 64, 32, 128, 64, blue);
    rd_set_target(scene, scene, GS_W, GS_H, 1);
    sprite(GS_W, GS_H, 320, 256, 384, 320, green);
    rd_end_frame(0);
    snprintf(path, sizeof(path), "%s/rd_pass_area_frame.rddump", dir);
    const bool ok = rd_dump_frame(path);
    rd_shutdown();
    printf("rd_pass_area_frame: %s %s\n", path, ok ? "written" : "NOT written");
    return ok ? 0 : 1;
}
