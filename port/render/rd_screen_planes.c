/* rd_screen_planes.c: the planes of the screen-prim triangles
 * (RENDER.md "Screen planes").
 *
 * The GPU interpolates a vertex's colour and ST from the pixel's place in
 * the target: its float rounding depends on how far the triangle sits from
 * the target's corner, so the Gouraud colours of a CPU (GIF) triangle in
 * the 4:3 columns came out a step apart on some pixels at each frame
 * width.  doScreen (rd_replay.c) has every triangle's three vertices here,
 * after expand: each triangle gets its colour and S, T, Q planes, per GS
 * pixel about its first corner, computed from the corners' GS positions
 * and values alone (the same numbers at every width), and carries them on
 * its three vertices (IcoSpritePlaneVertex, shader_consts.h).  The vertex
 * shader turns them per texel of the bound target and the pixel shader
 * evaluates them at the pixel's offset from the corner (sprite.hlsl
 * sprite_planes, sprite_plane_attrs): the same operations on the same
 * values at every width, as the VU programs' triangles (vu_common.hlsli
 * vu_attr_planes).  The GS's colours and STQ run linearly in screen space,
 * which the planes are.  Sprites (two corners, axis-aligned) keep their
 * own path: their colour is flat.
 *
 * At a scale that is not a whole number (rd_pixel_plan.c rd__planes_dropped)
 * the planes buy no exactness: a triangle command draws with the plain
 * sprite build there, the GPU's screen-space interpolation of its corners
 * (rd__screen_planes_want), but for the STQ triangles (the lightning's
 * strips), whose plain keys the start-up set does not hold beside the
 * planes ones (rd_pipeline.c rd__enumerate_reachable_screen).
 *
 * Without the sprite planes (issue 161: the Adreno 740's driver with the
 * compiler E031.41.03.47 stopped while creating a batch of them): the host's
 * record of that end ([dev] pipeline_skip, port/platform/pipeline_skip.h,
 * rd_set_sprite_planes_off) or ICO_RD_NO_SPRITE_PLANES=1 turns the family
 * off for the run (g_rd.spritePlanesOff): every screen-prim triangle command,
 * its STQ ones too, draws through the plain sprite build, the start-up set
 * holds the plain keys in their place, and no planes key is created on first
 * use.  The picture: the GPU's interpolation of the corners instead of the
 * planes (docs/port/WORK_BUFFERS.md "Without the sprite planes"). */
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "shader_consts.h"

/* rd_set_sprite_planes_off's reason ("" none) */
static char s_hostWhy[200];

void rd_set_sprite_planes_off(const char *reason)
{
    snprintf(s_hostWhy, sizeof(s_hostWhy), "%s", reason != NULL ? reason : "");
    if (g_rd.hasDevice) {
        /* after rd_init (the host knows the device's name and driver only
           then): in force from here, before the start-up set */
        rd__sprite_planes_init();
    }
}

bool rd_sprite_planes_off(void)
{
    return g_rd.spritePlanesOff;
}

void rd__sprite_planes_init(void)
{
    const char *env = getenv("ICO_RD_NO_SPRITE_PLANES");
    const bool byEnv = env != NULL && env[0] != '\0' && env[0] != '0';
    g_rd.spritePlanesOff = byEnv || s_hostWhy[0] != '\0';
    g_rd.spritePlanesWhy[0] = '\0';
    if (!g_rd.spritePlanesOff) {
        return;
    }
    snprintf(g_rd.spritePlanesWhy, sizeof(g_rd.spritePlanesWhy), "%s",
             s_hostWhy[0] != '\0' ? s_hostWhy : "by ICO_RD_NO_SPRITE_PLANES=1");
    rd__log("pipelines: the sprite planes builds are not made on this device (%s); their draws "
            "take the plain builds (the screen-prim triangles' colours and texture coordinates by "
            "the GPU's interpolation of their corners; the window's 10 s line counts them as "
            "sprite no-planes)",
            g_rd.spritePlanesWhy);
}

bool rd__key_is_sprite_planes(const RdPipeKeyInt *k)
{
    return k->vs == RD_VS_SPRITE_PLANE_UI || k->vs == RD_VS_SPRITE_PLANE_WORLD ||
           k->vs == RD_VS_SPRITE_STQ_PLANE_UI || k->vs == RD_VS_SPRITE_STQ_PLANE_WORLD;
}

static IcoSpritePlaneVertex *s_pl;

static uint32_t s_plCap;

/* the pass's vertex and fragment shaders with the planes: sprite.hlsl's
 * planes build (ICO_SPRITE_PLANES), whose SpriteVSOut carries them; the
 * plain sprite entries' outputs and inputs are the position, colour and
 * uv or stq alone */
int rd__plane_pass(RdDrawPass *dp)
{
    RdPipeKeyInt *k = &dp->key;
    if (g_rd.spritePlanesOff) {
        return 0; /* the family is off for the run (issue 161) */
    }
    if ((k->fs == RD_FS_SPRITE || k->fs == RD_FS_SPRITE_TEXA) &&
        (k->vs == RD_VS_SPRITE_UI || k->vs == RD_VS_SPRITE_WORLD)) {
        k->vs = k->vs == RD_VS_SPRITE_WORLD ? RD_VS_SPRITE_PLANE_WORLD : RD_VS_SPRITE_PLANE_UI;
        k->fs = k->fs == RD_FS_SPRITE_TEXA ? RD_FS_SPRITE_TEXA_PLANES : RD_FS_SPRITE_PLANES;
        return 1;
    }
    if (k->fs == RD_FS_SPRITE_STQ &&
        (k->vs == RD_VS_SPRITE_STQ_UI || k->vs == RD_VS_SPRITE_STQ_WORLD)) {
        k->vs = k->vs == RD_VS_SPRITE_STQ_WORLD ? RD_VS_SPRITE_STQ_PLANE_WORLD
                                                : RD_VS_SPRITE_STQ_PLANE_UI;
        k->fs = RD_FS_SPRITE_STQ_PLANES;
        return 1;
    }
    return 0;
}

int rd__screen_planes_want(const RdStateBlock *s, const RdTargetRec *tc, const RdDrawPass *dp)
{
    if (g_rd.spritePlanesOff) {
        /* issue 161: the plain build; a command whose pass would have
           taken the planes (rd__plane_pass's entries) counted for the 10 s
           line */
        if (dp && (dp->key.fs == RD_FS_SPRITE || dp->key.fs == RD_FS_SPRITE_TEXA ||
                   dp->key.fs == RD_FS_SPRITE_STQ)) {
            g_rdPerf.spriteNoPlanes++;
        }
        return 0;
    }
    if (dp && (dp->key.fs == RD_FS_SPRITE_STQ || dp->key.fs == RD_FS_SPRITE_STQ_PLANES)) {
        return 1;
    }
    return !rd__planes_dropped(s, tc);
}

/* the change per GS pixel across (gx) and down (gy) of values va, vb, vc at
 * the corners, from the basis of planes() */
static void grad(double va, double vb, double vc, const double g[4], float *gx, float *gy)
{
    *gx = (float)((vb - va) * g[0] + (vc - va) * g[1]);
    *gy = (float)((vb - va) * g[2] + (vc - va) * g[3]);
}

const IcoSpritePlaneVertex *rd__screen_planes(const void *vb, uint32_t stride, uint32_t n)
{
    if (n > s_plCap) {
        IcoSpritePlaneVertex *p = realloc(s_pl, (size_t)n * 2 * sizeof(*p));
        if (!p) {
            return NULL;
        }
        s_pl = p;
        s_plCap = n * 2;
    }
    IcoSpritePlaneVertex *o = s_pl;
    const uint8_t *in = vb;
    const int stq = stride == sizeof(IcoSpriteStqVertex);
    for (uint32_t t = 0; t + 2 < n; t += 3) {
        IcoSpriteStqVertex c[3];
        double px[3], py[3], at[3][7];
        for (int j = 0; j < 3; j++) {
            const uint8_t *p = in + (size_t)(t + (uint32_t)j) * stride;
            if (stq) {
                memcpy(&c[j], p, sizeof(c[j]));
            } else {
                memcpy(&c[j].v, p, sizeof(c[j].v));
                c[j].q = 1.0f;
            }
            px[j] = c[j].v.x / 16.0;
            py[j] = c[j].v.y / 16.0;
            for (int i = 0; i < 4; i++) {
                at[j][i] = c[j].v.rgba[i];
            }
            at[j][4] = c[j].v.u;
            at[j][5] = c[j].v.v;
            at[j][6] = c[j].q;
        }
        /* the basis: (vb - va) g0 + (vc - va) g1 across, g2, g3 down */
        const double d1x = px[1] - px[0], d1y = py[1] - py[0];
        const double d2x = px[2] - px[0], d2y = py[2] - py[0];
        const double det = d1x * d2y - d1y * d2x;
        double g[4] = {0.0, 0.0, 0.0, 0.0};
        if (det != 0.0) {
            g[0] = d2y / det;
            g[1] = -d1y / det;
            g[2] = -d2x / det;
            g[3] = d1x / det;
        }
        IcoSpritePlaneVertex pl;
        memset(&pl, 0, sizeof(pl));
        float gx[7], gy[7];
        for (int i = 0; i < 7; i++) {
            grad(at[0][i], at[1][i], at[2][i], g, &gx[i], &gy[i]);
        }
        for (int i = 0; i < 4; i++) {
            pl.c0[i] = (float)at[0][i];
            pl.cx[i] = gx[i];
            pl.cy[i] = gy[i];
        }
        for (int i = 0; i < 3; i++) {
            pl.s0[i] = (float)at[0][4 + i];
            pl.sx[i] = gx[4 + i];
            pl.sy[i] = gy[4 + i];
        }
        pl.s0[3] = (float)px[0];
        pl.sx[3] = (float)py[0];
        pl.sy[3] = 1.0f;
        for (int j = 0; j < 3; j++) {
            o[t + (uint32_t)j] = pl;
            o[t + (uint32_t)j].s = c[j];
        }
    }
    return o;
}
