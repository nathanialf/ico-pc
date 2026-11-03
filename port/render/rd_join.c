/*
 * port/render/rd_join.c
 *
 * The region rule's card test joined over a part's
 * packets (rd_replay.c calls rd__join_parts once per replayed frame, before
 * the meshes are uploaded).  Split from rd_replay.c for the tracked-file cap.
 */
#include <stdlib.h>
#include <stdint.h>
#include "rd_internal.h"
#include "rd_vis.h"

/* The card test's joined strips over a part's meshes (RENDER.md "Strip
 * joining").
 * The region rule leaves out of the picture a failing triangle of a card
 * (vu_common.hlsli vu_region_rule, VU_RULE_STRIP: a free-standing strip
 * none of whose triangles passes the region test), and a strip is free-
 * standing when it shares no vertex with a strip of another shape
 * (rd_mesh.c markJoinedStrips).  Asked of the strip's mesh alone, one
 * packet of a part, that is not enough: Packet.c pac_makePacket makes a packet per
 * material and texture of the part, so a wall's strip whose material is
 * its own shares its edges with the rest of the wall only through the
 * part's other packets.  st18a_p1 part 9 has eight such strips, one
 * triangle each, in the wall planes above the doorways (from the lintel,
 * Y 80, to the wall's top, Y -775; part 6 holds the rest of the wall, part
 * 9 the top face along the same edge): at the courtyard's camera (eye
 * 1859.4, -65, -2560) the top corners lie above the GS window (GS y -56 to
 * -189), so with the mesh alone the triangles would be taken for cards and
 * left out, and the wall would show the far side of the courtyard through
 * them, opening and closing as the camera turned.  So the strips
 * are joined over the part: the VU draws of the frame whose keys name one
 * object and part (RD_KEY: the key without its last byte, the packet's
 * ordinal) are one group, and rd__mesh_join_part marks across their
 * meshes.  Every packet of a part the game registers is drawn
 * (RegistPacket.c reg_clipPacketBoundingBox clips, never drops), so the
 * group is the part's whole, live and in a dump's replay alike.  Over every
 * model on the disc 6,610 of 932,105 strips in 216 models are joined this
 * way that their packet alone left free; the opening's tree cards
 * (st26a_near part 5) stay free.  Out of the box the region rule never
 * reads the mark. */
typedef struct JoinDraw {
    uint64_t part; /* the key without the packet's ordinal */
    uint32_t mesh;
} JoinDraw;

static int joinDrawCmp(const void *a, const void *b)
{
    const JoinDraw *x = a, *y = b;
    if (x->part != y->part) {
        return x->part < y->part ? -1 : 1;
    }
    return x->mesh < y->mesh ? -1 : (x->mesh > y->mesh ? 1 : 0);
}

void rd__join_parts(const RdFrame *f, int keep)
{
    /* the frame as recorded (rd_vis.c): a packet the visibility test
     * skipped still joins its part's other packets' strips, so their marks,
     * and the card verdicts they decide, are those of every replay */
    f = rd__vis_source(f);
    static JoinDraw *draws;
    static RdMeshRec **ms;
    static uint32_t cap;
    uint32_t n = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            const uint64_t key = (uint64_t)c->keyHi << 32 | c->keyLo;
            if ((c->type != RDC_MESH && c->type != RDC_SKINNED) || key == 0) {
                continue;
            }
            if (n == cap) {
                const uint32_t nc = cap ? cap * 2u : 1024u;
                JoinDraw *p = realloc(draws, (size_t)nc * sizeof(JoinDraw));
                RdMeshRec **q = p ? realloc(ms, (size_t)nc * sizeof(RdMeshRec *)) : NULL;
                if (p) {
                    draws = p;
                }
                if (!q) {
                    return;
                }
                ms = q;
                cap = nc;
            }
            draws[n].part = key >> 8;
            draws[n].mesh = c->u[0];
            n++;
        }
    }
    if (n == 0) {
        return; /* no draws (a frame without meshes): qsort takes no null */
    }
    qsort(draws, n, sizeof(JoinDraw), joinDrawCmp);
    for (uint32_t a = 0; a < n;) {
        uint32_t b = a + 1;
        while (b < n && draws[b].part == draws[a].part) {
            b++;
        }
        uint32_t k = 0;
        uint64_t sig = 1469598103934665603ull; /* FNV-1a over the meshes' ids */
        for (uint32_t i = a; i < b; i++) {
            if (i > a && draws[i].mesh == draws[i - 1].mesh) {
                continue;
            }
            RdMeshRec *m = rd__mesh_rec(draws[i].mesh);
            if (!m || !m->vu || m->replaced) {
                continue;
            }
            ms[k++] = m;
            sig = (sig ^ draws[i].mesh) * 1099511628211ull;
        }
        if (k > 1) {
            rd__mesh_join_part(ms, k, sig);
        }
        a = b;
    }
}
