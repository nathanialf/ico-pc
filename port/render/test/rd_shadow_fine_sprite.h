/* rd_shadow_fine_sprite.h: a level sprite of rd_shadow_fine_vectors.h as
 * rd_sr_recognise's input (rd_shadow_fine_test.c and the dump harness).
 * What rd_replay_tool --list does not print is Shadow.c's own, by
 * construction: fixed UVs (rd_screen_prims' uvFixed 1), one colour on both
 * vertices (shadowHostSprite copies one col into each), no frame camera, ST
 * shift or AA1 (the shadow box's scan, which demands the same, takes these
 * chains: its "shadow box: on" line); the colour is the retail
 * (128, 128, 128, shadowDepth 80) and the UV offset 0 (no UVOFFSET command
 * before the chain in any of the dumps: the vector's uvOffset is 0). */
#ifndef ICO_RD_SHADOW_FINE_SPRITE_H
#define ICO_RD_SHADOW_FINE_SPRITE_H

#include <math.h>
#include <string.h>

#include "rd_shadowres.h"
#include "rd_state.h"

static RdSrSprite rd_sft_sprite(const RdSftVec *v)
{
    RdSrSprite sp;
    memset(&sp, 0, sizeof(sp));
    sp.prim = v->sprite ? (uint8_t)RD_PRIM_SPRITES : 0u;
    sp.space = (uint8_t)v->space;
    sp.uvFixed = 1;
    sp.n = (uint32_t)v->n;
    sp.tcc = (uint8_t)v->tcc;
    sp.texFn = (uint8_t)v->fn;
    sp.abe = (uint8_t)v->abe;
    sp.fba = (uint8_t)v->fba;
    sp.colorMask = (uint8_t)v->mask;
    sp.ate = (uint8_t)v->ate;
    sp.atst = (uint8_t)v->atst;
    sp.date = (uint8_t)v->date;
    sp.zte = (uint8_t)v->zte;
    sp.ztst = (uint8_t)v->ztst;
    sp.magF = (uint8_t)v->mag;
    sp.minF = (uint8_t)v->min;
    for (int i = 0; i < 2; i++) {
        sp.rgba[i][0] = sp.rgba[i][1] = sp.rgba[i][2] = 128;
        sp.rgba[i][3] = 80;
    }
    sp.x16[0] = (int32_t)lroundf(v->x0 * 16.0f);
    sp.x16[1] = (int32_t)lroundf(v->x1 * 16.0f);
    sp.y16[0] = (int32_t)lroundf(v->y0 * 16.0f);
    sp.y16[1] = (int32_t)lroundf(v->y1 * 16.0f);
    sp.s16[0] = v->u0 * 16.0f;
    sp.s16[1] = v->u1 * 16.0f;
    sp.t16[0] = v->v0 * 16.0f;
    sp.t16[1] = v->v1 * 16.0f;
    sp.gsW = (uint32_t)v->gsW;
    sp.gsH = (uint32_t)v->gsH;
    sp.dstW = (uint32_t)v->gsW;
    sp.dstH = (uint32_t)v->gsH;
    sp.srcW = (uint32_t)v->srcW;
    sp.srcH = (uint32_t)v->srcH;
    return sp;
}

#endif
