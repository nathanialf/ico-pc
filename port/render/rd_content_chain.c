/* rd_content_chain.c: the chains a stage animation lets down, drawn
 * with their links at their size under "Natural chains" (rd_chain.h).
 *
 * The table comes from the disc's stage animations, read with
 * tools/content_table_check.py chains (every .bga of every pack: a node
 * whose keyed scale changes along one axis over a link model's nodes).
 * Two do: the two below.  The others with a chain-like name are not
 * let down: st13c's cage chains (st13c_cage, st13c_cage_cam,
 * st13c_cagefall_chain) scale on all three axes at once from 0.01, the
 * cage's chains popping in; st13c_cagefall04's chain nodes are a falling
 * chain at the model's own scale 100, its chain_hide_Null a show/hide on
 * all three axes; st10r_chain's chain4 links are never scaled; st01b_chain
 * moves a gear.  The ropes (st04l, st04r, st10r_tower, st20a's bridge,
 * st47a) are ropes, not links: a rope piece's scale is the rope's length.
 *
 * chain4.p2o: one part, 96 vertices, x and z -6.38 .. 6.38, y -5.37 ..
 * 54.82: a link reaches 5.37 units above its node (reach) and 54.82 below
 * (fall), the next node 50 below. */
#include "rd_chain.h"

#include <stddef.h>

#include "../core/region.h" /* the running disc's groups */

const RdChain rd__chains[] = {
    /* the Stone Pillar (stage 17, st10l): actSt10lChainMove plays it
     * (stage_SetAnimation(384, 1, 0), 240 frames); Null 21 at (-1050,
     * -50, 500) scaled 0.1 on Y from frame 1 to 121, then linearly to 1
     * at 240; links 0..20 at Y 0 .. 1000; the drum 10l_chain_kas at
     * (-1025, -60, 500) turns -720 degrees over frames 120..240 */
    {"st10l_chain_down", 731, 21, 1, 0.0f, 1000.0f, 5.37f, 54.82f},
    /* the underground (stage 32, st19b): actSt19aChainDown's
     * actSt19aChainDownSub plays it (stage_SetAnimation(144, 1, 0), 240
     * frames, then held at 361); Null 22 at Y 1340 scaled 0.5 on Y to
     * frame 121, then linearly to Y 0 and scale 1 at 240 (the anchor,
     * 1340 + 2700 s, stays within 10 units of 2700); links 1..21 at Y
     * 2700 .. 3700 */
    {"st19a_chain", 636, 22, 1, 2700.0f, 3700.0f, 5.37f, 54.82f},
};

const int rd__chain_count = (int)(sizeof(rd__chains) / sizeof(rd__chains[0]));
_Static_assert(sizeof(rd__chains) / sizeof(rd__chains[0]) == 2,
               "chainGroup names each row's region.h field");

/* A row's group on the running disc (port/core/region.h: the European
   disc's are the table's, and the Japanese disc's are the same numbers) */
static int chainGroup(int i)
{
    const IcoRegionInfo *r = ico_region_cur();
    return i == 0 ? r->chain_group_st10l : i == 1 ? r->chain_group_st19a : rd__chains[i].group;
}

const RdChain *rd_chain_find(int group)
{
    for (int i = 0; i < rd__chain_count; i++) {
        if (chainGroup(i) == group) {
            return &rd__chains[i];
        }
    }
    return NULL;
}

void rd_chain_null(const RdChain *c, const float sc[3], float scaleOut[3], float trans[4])
{
    for (int i = 0; i < 3; i++) {
        scaleOut[i] = i == c->axis ? 1.0f : sc[i];
        trans[i] = 0.0f;
    }
    const float sign = c->end >= c->anchor ? 1.0f : -1.0f;
    trans[c->axis] = (sc[c->axis] - 1.0f) * (c->end + sign * c->fall);
    trans[3] = 1.0f;
}

uint8_t rd_chain_plane_code(int axis, int neg)
{
    return (uint8_t)((unsigned)(axis + 1) | (neg ? RD_CHAIN_PLANE_NEG : 0u));
}

void rd_chain_link(const RdChain *c, float s, float pos, uint8_t *code, float *offset)
{
    /* the chain runs from the anchor toward the end; sign is that way */
    const float sign = c->end >= c->anchor ? 1.0f : -1.0f;
    /* rd_chain_null's move */
    const float shift = (s - 1.0f) * (c->end + sign * c->fall);
    /* where the disc's squashed top link begins (at full length: the top
       link whole) */
    const float top = s * (c->anchor - sign * c->reach);
    *code = rd_chain_plane_code(c->axis, sign < 0.0f);
    *offset = sign * (pos + shift - top);
}

/* ------------------------------------------------ the frame's link planes */

#define RD_CHAIN_NOTES 64 /* two chains of 21 links at most in a stage */

typedef struct ChainNote {
    const void *obj;
    int node;
    uint8_t code;
    float offset;
} ChainNote;

static ChainNote s_notes[RD_CHAIN_NOTES];
static int s_noteCount;

void rd_chain_note(const void *obj, int node, uint8_t code, float offset)
{
    for (int i = 0; i < s_noteCount; i++) {
        if (s_notes[i].obj == obj && s_notes[i].node == node) {
            if (code == 0) {
                s_notes[i] = s_notes[--s_noteCount];
            } else {
                s_notes[i].code = code;
                s_notes[i].offset = offset;
            }
            return;
        }
    }
    if (code == 0 || s_noteCount >= RD_CHAIN_NOTES) {
        return;
    }
    s_notes[s_noteCount].obj = obj;
    s_notes[s_noteCount].node = node;
    s_notes[s_noteCount].code = code;
    s_notes[s_noteCount].offset = offset;
    s_noteCount++;
}

int rd_chain_plane(const void *obj, int node, uint8_t *code, float *offset)
{
    for (int i = 0; i < s_noteCount; i++) {
        if (s_notes[i].obj == obj && s_notes[i].node == node) {
            *code = s_notes[i].code;
            *offset = s_notes[i].offset;
            return 1;
        }
    }
    return 0;
}

void rd_chain_reset(void)
{
    s_noteCount = 0;
}
