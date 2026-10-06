/* rd_filter_test.c: the draw filter (package MV; rd.h rd_SetDrawFilter) on synthetic keys, recorded without a
 * device (rd__InitRecordOnly):
 *   - off (the default and after rd_SetDrawFilter(false, ...)): every draw
 *     is recorded;
 *   - on: a world draw (screen prims in RD_SPACE_WORLD, also through a
 *     space override, world prims, shadow strips and triangles, grids) is
 *     recorded only when its key's object is in the set, whatever its part
 *     and ordinal; key 0 is not; UI and full-screen prims always are;
 *   - open: every world draw is recorded and its object joins the set, so
 *     the same object's draws pass once the window closes; key 0 is
 *     recorded but learns nothing;
 *   - rd_DrawFilterKeeps agrees and learns nothing.
 * Exit 0, 1 on a mismatch. */
#include <stdio.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char kObjA, kObjB, kObjC, kObjD, kObjE;

static void screen(RdSpace space, RdKey key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[1].x = v[1].y = 160;
    v[0].q = v[1].q = 1.0f;
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, space, 0, key);
}

static void worldPrims(RdKey key)
{
    RdWorldVtx v[2];
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memset(v, 0, sizeof(v));
    rd_WorldPrims(RD_PRIM_LINES, v, 2, m, key);
}

static void shadowStrip(RdKey key)
{
    float v[3][4];
    memset(v, 0, sizeof(v));
    rd_ShadowStrip((const float (*)[4])v, 3, 1.0f, key);
}

static void shadowTris(RdKey key)
{
    RdScreenVtx v[3];
    int8_t sign[1] = {1};
    memset(v, 0, sizeof(v));
    rd_ShadowTris(v, sign, 1, key);
}

static void grid(RdKey key)
{
    static float qw[3 * RD_VU_QW_GRID + 4][4];
    RdVuGridDraw d;
    memset(&d, 0, sizeof(d));
    d.qw = (const float (*)[4])qw;
    d.strips = 1;
    d.stripLen = 3;
    d.code = 20;
    rd_DrawVuGrid(&d, key);
}

/* the draws of the open frame's list 3 with this key */
static int count(RdKey key)
{
    const RdFrame *f = rd__RecFrame();
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[3].count; i++) {
        const RdCmd *c = &f->lists[3].cmds[i];
        if (!rd__CmdIsState(c->type) && c->keyLo == (uint32_t)key &&
            c->keyHi == (uint32_t)(key >> 32)) {
            n++;
        }
    }
    return n;
}

/* every kind of world draw once with key: how many were recorded */
static int worldDraws(RdKey key)
{
    const int before = count(key);
    screen(RD_SPACE_WORLD, key);
    worldPrims(key);
    shadowStrip(key);
    shadowTris(key);
    grid(key);
    return count(key) - before;
}

int main(void)
{
    if (!rd__InitRecordOnly(512, 512)) {
        printf("rd_filter_test: no context\n");
        return 1;
    }
    const RdKey a0 = RD_KEY(&kObjA, 0, 0), a1 = RD_KEY(&kObjA, 3, 17);
    const RdKey b0 = RD_KEY(&kObjB, 0, 0), c0 = RD_KEY(&kObjC, 2, 5), c1 = RD_KEY(&kObjC, 7, 1);

    rd_BeginFrame();
    rd_SelectList(3);
    CHECK(worldDraws(b0) == 5, "off: every world draw recorded");
    CHECK(rd_DrawFilterKeeps(b0) && rd_DrawFilterKeeps(0), "off: everything kept");

    const void *own[1] = {&kObjA};
    rd_SetDrawFilter(true, own, 1);
    CHECK(worldDraws(a0) == 5, "on: the object's draws recorded");
    CHECK(worldDraws(a1) == 5, "on: another part and ordinal of the object recorded");
    CHECK(worldDraws(b0) == 0, "on: another object's world draws left out");
    CHECK(worldDraws(0) == 0, "on: key 0's world draws left out");
    const RdKey d0 = RD_KEY(&kObjD, 0, 0), e0 = RD_KEY(&kObjE, 0, 0);
    screen(RD_SPACE_UI, d0);
    screen(RD_SPACE_FULLSCREEN, e0);
    CHECK(count(d0) == 1 && count(e0) == 1, "on: UI and full-screen prims recorded");
    int prev = rd_SetSpaceOverride(RD_SPACE_WORLD);
    screen(RD_SPACE_UI, d0);
    rd_SetSpaceOverride(prev);
    CHECK(count(d0) == 1, "on: a UI prim drawn as world space left out");
    CHECK(rd_DrawFilterKeeps(a1) && !rd_DrawFilterKeeps(b0) && !rd_DrawFilterKeeps(c0),
          "on: rd_DrawFilterKeeps");

    rd_DrawFilterOpen(true);
    CHECK(rd_DrawFilterKeeps(c0), "open: everything kept");
    CHECK(worldDraws(c0) == 5, "open: an unknown object recorded");
    CHECK(worldDraws(0) == 5, "open: key 0 recorded");
    rd_DrawFilterOpen(false);
    CHECK(worldDraws(c1) == 5, "closed: the learned object's draws recorded");
    CHECK(worldDraws(0) == 0, "closed: key 0 learned nothing");
    CHECK(worldDraws(b0) == 0, "closed: another object still left out");

    rd_SetDrawFilter(true, NULL, 0);
    CHECK(worldDraws(a0) == 0 && worldDraws(c0) == 0, "on again: the set emptied");
    rd_DrawFilterOpen(true);
    rd_SetDrawFilter(false, NULL, 0);
    CHECK(worldDraws(b0) == 5, "off: every world draw recorded again");
    rd_DrawFilterOpen(true);
    rd_SetDrawFilter(true, own, 1);
    CHECK(worldDraws(c0) == 0, "rd_SetDrawFilter closes the window");
    rd_SetDrawFilter(false, NULL, 0);
    rd_EndFrame(0);
    rd_Shutdown();
    if (failures == 0) {
        printf("rd_filter_test: ok\n");
    }
    return failures ? 1 : 0;
}
