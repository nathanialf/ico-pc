/* rd_chain.h: "Natural chains" (Options > Gameplay, [gameplay]
 * natural_chains), the game's side and the draw's side.
 *
 * Two stage animations let a chain down for the player: st10l_chain_down
 * (stage 17, st10l, the Stone Pillar's save area: actSt10lChainMove,
 * ico2/script/src/st10l.c) and st19a_chain (stage 32, st19b, the
 * underground: actSt19aChainDown, ico2/script/src/st19a.c).  Each is 21
 * nodes of the link model chain4 (obj_com03/model/chain4.p2o), 50 units
 * apart along the parent's Y, under a Null (BGA node type 1) whose keyed
 * scale on Y grows to 1 while the chain comes down (0.1 to 1, 0.5 to 1).
 * bga_CalcObject applies a type-1 node's scale to its children
 * (_ScaleCurrentMatrix, BgAnimation.c), so the links are drawn squashed
 * until the chain is fully down.  The game means a chain paid out from
 * above (st10l's comes off a winding drum, 10l_chain_kas, turned -720
 * degrees over the same frames): the links keep their size and more of
 * the chain shows below the top as it comes down.
 *
 * With the option on (BgAnimation.c, under ICO_RD) the chain fills the
 * same stretch the disc's squashed one does at every frame, top to bottom,
 * with links of their own size:
 *   - the Null takes no scale along the chain: its children are moved
 *     along it instead (rd_chain_null), so that the last link's bottom is
 *     where the disc's squashed last link ends;
 *   - each link gets a clip plane in its own space (rd_chain_link) where
 *     the disc's squashed top link begins: what is above it is the chain
 *     still at the top (on the drum), not drawn.  RegistPacket.c's
 *     regHostMesh gives a draw of a noted node its plane (rd_chain_plane),
 *     rd_draw_vu_mesh records it (RDC_MESH b[5], f[0]) and the VU shaders
 *     discard the pixels on the far side (vu_common.hlsli vu_plane_*).
 * Nothing is drawn where the disc's chain is not.  Off: the disc's
 * matrices, no plane, the disc's picture byte for byte.  At full length
 * the two are the same matrices and the plane clips nothing.
 *
 * tools/content_table_check.py chains checks the table against the disc
 * (the node, its type and keyed scale, the links' places and the model's
 * reach). */
#ifndef PORT_RENDER_RD_CHAIN_H
#define PORT_RENDER_RD_CHAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RdChain {
    const char *bga; /* the stage animation's file name, for the reader */
    int group;       /* its BgaHeader.group (stageTable's group, unique to it): the
                        European disc's; rd_chain_find matches the running disc's
                        (port/core/region.h chain_group_*) */
    int null;        /* the Null's node number (BgaDObjEnt.num, type 1) */
    int axis;        /* the chain's length along the Null's 0 x, 1 y, 2 z */
    float anchor;    /* the top link's node along the axis (unscaled) */
    float end;       /* the last link's node along the axis (unscaled) */
    float reach;     /* how far a link's mesh reaches past its node toward the anchor */
    float fall;      /* how far it reaches past its node away from the anchor */
} RdChain;

extern const RdChain rd__chains[];
extern const int rd__chain_count;

/* The listed chain of a stage animation (its BgaHeader.group), or NULL. */
const RdChain *rd_chain_find(int group);

/* The Null's transform for its children with the option on, from the
 * keyed scale sc: scaleOut is sc with 1 along the chain, trans[4] the
 * move along it (w 1) that puts the last link's far side where the scaled
 * one's is, (s - 1) x (end + fall) for s = sc[axis] (fall negated for a
 * chain running toward minus). */
void rd_chain_null(const RdChain *c, const float sc[3], float scaleOut[3], float trans[4]);

/* A link's clip plane: code (RD_CHAIN_PLANE_*) and offset, the link
 * drawn where sign x its coordinate along the axis + offset >= 0, for the
 * Null's keyed scale s along the chain and the link node's own place pos
 * along it (unscaled; the link turned and scaled by nothing, as the
 * listed chains' are).  The plane lies at s x (anchor - reach), where the
 * disc's squashed top link begins. */
void rd_chain_link(const RdChain *c, float s, float pos, uint8_t *code, float *offset);

/* The plane code a draw carries (RdVuDraw.plane, RDC_MESH b[5],
 * VuCB vu_batch.z): 0 none, else the axis + 1 in bits 0..1 and bit 2 for
 * the minus side. */
#define RD_CHAIN_PLANE_AXIS(code) (((code) & 3u) - 1u)
#define RD_CHAIN_PLANE_NEG 4u
uint8_t rd_chain_plane_code(int axis, int neg);

/* The planes of the frame's link draws, by object (the Sub15C) and node:
 * set (code != 0) or cleared (code 0) by BgAnimation.c each time it
 * places a listed chain's link, read by RegistPacket.c at the node's
 * draws.  rd_chain_reset forgets them all (stage_Init: a new stage's
 * objects).  rd_chain_plane: 1 and *code, *offset for a noted node, else 0. */
void rd_chain_note(const void *obj, int node, uint8_t code, float offset);
int rd_chain_plane(const void *obj, int node, uint8_t *code, float *offset);
void rd_chain_reset(void);

#ifdef __cplusplus
}
#endif

#endif
