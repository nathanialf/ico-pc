/* rd_inames_live.c: rd_interp.c's side of the draws named (rd_inames.h), on
 * the render thread. */
#include "rd_inames.h"

#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"

static RdInameTally s_tally;
static RdInameStairs s_stairs;
/* the tick before's view carried to the tick's, worked out once a pair */
static RdInameCarry s_carry;
/* the present being blended is its frame's first (rd__inames_first), and
   the frame being blended (its commands' payload) */
static int s_first = 1;
static const RdFrame *s_out;

static const char *draw_name(const RdCmd *c)
{
    switch (c->type) {
    case RDC_MESH:
    case RDC_SKINNED: {
        const RdMeshRec *m = rd__mesh_rec(c->u[0]);

        return m != NULL && m->name[0] != '\0' ? m->name : "a mesh";
    }
    case RDC_GRID:
        return "a grid";
    case RDC_PARTICLES:
        return "particles";
    case RDC_SCREEN:
        return "a screen prim";
    case RDC_SHADOW_STRIP:
        return "a shadow volume";
    case RDC_OVERLAY_TEXT:
        return "text";
    default:
        return "a draw";
    }
}

void rd__inames_first(int firstOfTick, const RdFrame *out)
{
    s_first = firstOfTick;
    s_out = out;
}

/* an unmatched mesh draw's VU block scrolls its texture: qw 2's x or y not
   0 (rd_interp.c lerpVuBlock's UV scroll; such a draw keeps its tick's
   scroll, camCurDraw) */
static int scrolls(const RdCmd *c)
{
    const uint32_t need = (uint32_t)(sizeof(RdVuPayload) + 3u * 16u);
    float qw2[4];

    if ((c->type != RDC_MESH && c->type != RDC_SKINNED) || s_out == NULL || c->u[2] < need ||
        c->u[1] > s_out->payloadSize || c->u[2] > s_out->payloadSize - c->u[1]) {
        return 0;
    }
    memcpy(qw2, s_out->payload + c->u[1] + sizeof(RdVuPayload) + 2u * 16u, sizeof(qw2));
    return qw2[0] != 0.0f || qw2[1] != 0.0f;
}

void rd__inames_note(const RdCmd *c, int why)
{
    if (!s_first) {
        return; /* a later present of the frame: counted once, at its first */
    }
    /* an unkeyed command that draws no geometry (a clear, a copy, a pass of
       the effects) is no object left behind */
    if (why == RD_INAME_UNKEYED && c->type != RDC_SCREEN && c->type != RDC_MESH &&
        c->type != RDC_SKINNED && c->type != RDC_GRID && c->type != RDC_PARTICLES &&
        c->type != RDC_SHADOW_STRIP) {
        return;
    }
    rd_inames_add(&s_tally, draw_name(c), why, 1u);
    if (why == RD_INAME_UNMATCHED && scrolls(c)) {
        rd_inames_scroll(&s_tally, draw_name(c), 1u);
    }
}

void rd__inames_step(const RdFrame *prev, const RdCmd *pc, const uint8_t *op, const RdCmd *c,
                     uint32_t curNumber, int firstOfTick)
{
    const uint32_t need = (uint32_t)(sizeof(RdVuPayload) + sizeof(RdVuBlock));

    /* the model's place in the world: a rigid mesh drawn through the
       frame's view (rd_interp.c vuView's RD_VU_VIEW_WORLD) */
    if (!firstOfTick || c->type != RDC_MESH || pc->type != RDC_MESH ||
        c->b[4] != RD_VU_VIEW_WORLD || pc->b[4] != RD_VU_VIEW_WORLD || op == NULL ||
        c->u[2] < need || pc->u[2] < need || pc->u[1] > prev->payloadSize ||
        pc->u[2] > prev->payloadSize - pc->u[1]) {
        return;
    }
    float mc[36][4], mp[36][4];

    memcpy(mc, op + sizeof(RdVuPayload), sizeof(mc));
    memcpy(mp, prev->payload + pc->u[1] + sizeof(RdVuPayload), sizeof(mp));
    if (!rd_inames_carry(&s_carry, (const float (*)[4])mp, (const float (*)[4])mc)) {
        return;
    }
    const uint64_t key = ((uint64_t)c->keyHi << 32) | c->keyLo;
    const char *name = draw_name(c);
    const unsigned v =
        rd_inames_stair(&s_stairs, key, rd_inames_name_hash(name), curNumber, prev->number,
                        rd_inames_moved(s_carry.t, (const float (*)[4])mp, (const float (*)[4])mc));

    if (v & RD_INAMES_V_STEP) {
        rd_inames_add(&s_tally, name, RD_INAME_STAIR, 1u);
    }
}

void rd__inames_log(uint32_t frames, uint32_t presents)
{
    char line[1536];

    (void)rd_inames_text(&s_tally, frames, presents, &s_stairs, line, sizeof(line));
    rd__log("%s", line);
    rd_inames_reset(&s_tally);
    rd_inames_stairs_window(&s_stairs);
}
