/* vu_models.h: synthetic p2o-decoded models for the mesh tests
 * (rd_mesh_test.c; modelpack_test.c reuses them).  Header only, static.
 *
 * makeModel(m, o, cluster, shade) fills a Model (a PObjModel with one
 * PObjPart of STRIPS strips of SLEN vertices, its material, texture
 * definition, nodes, node and cluster matrices, light matrix) and the Sub15C
 * that draws it.  cluster 0 gives a normal_c (prelit) model, 1 a two-bone
 * cluster model whose bone lists go to the EE word arena.
 *
 * The includer provides, before including this file, the game headers the
 * types come from (typedef.h, DisplayP2O.h, Packet.h, Light.h, eeword.h)
 * and the EE word arena ico_arena_base() of at least
 * 2 * (NV + 1) * 16 bytes past offset 16 for every cluster model it makes
 *.  rnd() is a fixed LCG: set s_rng
 * before makeModel for reproducible models. */
#ifndef PORT_RENDER_TEST_VU_MODELS_H
#define PORT_RENDER_TEST_VU_MODELS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline void qw4(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

static inline void identity(float *m)
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static uint32_t s_rng = 1234567u;

static inline float rnd(float lo, float hi)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(s_rng >> 8) * (1.0f / 16777216.0f);
}

/* ------------------------------------------------------------- models */

#define STRIPS 4
#define SLEN 24
#define NV (STRIPS * SLEN)

typedef struct Model {
    PObjModel mdl __attribute__((aligned(16)));
    PObjPart part;
    PObjMatDef mat;
    PObjTexDef texDef;
    float vtx[NV][4] __attribute__((aligned(16)));
    float nrm[NV][4] __attribute__((aligned(16)));
    float uv[NV][4] __attribute__((aligned(16)));
    unsigned char col[NV][4];
    short strip[(STRIPS * (SLEN + 1) + 1) * 8];
    void *stripTbl[1];
    float boxes[8][4] __attribute__((aligned(16)));
    struct DObjNode nodes[2];
    float nodeMtx[2][16] __attribute__((aligned(16)));
    float clusterMtx[2][16] __attribute__((aligned(16)));
    LightMatrix light __attribute__((aligned(16)));
    ObjEnt polys[2];
    float w0[NV];
} Model;

/* A ribbon of STRIPS strips of SLEN vertices, x -14..14, rows of 6 units,
 * z 1.9..2.3: in view, and four strips of 72 quadwords make two VU batches
 * (pac_checkDivide's 192-quadword budget) with a restart inside each. */
static inline void makeModel(Model *m, Sub15C *o, int cluster, int shade)
{
    memset(m, 0, sizeof(*m));
    memset(o, 0, sizeof(*o));
    for (int s = 0, v = 0; s < STRIPS; s++) {
        for (int k = 0; k < SLEN; k++, v++) {
            float x = -14.0f + 28.0f * (float)(k >> 1) / (float)(SLEN / 2 - 1);
            float y = -12.0f + 6.0f * (float)s + 6.0f * (float)(k & 1);
            qw4(m->vtx[v], x + rnd(-0.3f, 0.3f), y + rnd(-0.3f, 0.3f), rnd(1.9f, 2.3f), 1.0f);
            qw4(m->nrm[v], rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), 1.0f);
            qw4(m->uv[v], rnd(0, 1), rnd(0, 1), 0, 0);
            m->col[v][0] = (unsigned char)rnd(20, 250);
            m->col[v][1] = (unsigned char)rnd(20, 250);
            m->col[v][2] = (unsigned char)rnd(20, 250);
            m->w0[v] = rnd(0.2f, 0.8f);
        }
    }
    /* the shape table: per strip a head record (count, the packet offset
     * pac_make*Strip fills) and a record per vertex (vertex, normal, uv,
     * colour indices, material, texture slot); count -1 ends it */
    short *p = m->strip;
    for (int s = 0; s < STRIPS; s++) {
        p[0] = SLEN;
        p += 8;
        for (int k = 0; k < SLEN; k++) {
            int v = s * SLEN + k;
            p[2] = (short)v;
            p[3] = (short)v;
            p[4] = (short)v;
            p[5] = (short)v;
            p[6] = 0;
            p[7] = 0;
            p += 8;
        }
    }
    p[0] = -1;
    m->stripTbl[0] = m->strip;
    m->mat.alpha = 1.0f;
    m->mat.wrap = 1;
    m->mat.fbaOff = 0;
    snprintf(m->texDef.name, sizeof(m->texDef.name), "testtex");
    m->texDef.scaleU = 1.0f;
    m->texDef.scaleV = 1.0f;
    m->part.vtx = (char *)m->vtx;
    m->part.vtxCount = NV;
    m->part.nrm = cluster ? (char *)m->nrm : NULL;
    m->part.nrmCount = cluster ? NV : 0;
    m->part.uv = (char *)m->uv;
    m->part.col = (char *)m->col;
    m->part.mats = &m->mat;
    m->part.matCount = 1;
    m->part.texDefs = &m->texDef;
    m->part.texCount = 1;
    m->part.strips = m->stripTbl;
    m->part.stripCount = 1;
    snprintf(m->mdl.name, sizeof(m->mdl.name), cluster ? "test_cluster" : "test_prelit");
    m->mdl.partCount = 1;
    m->mdl.disp = (signed char)(cluster ? 1 : 0);
    m->mdl.mode.s.shade = (unsigned short)shade;
    m->mdl.mode.s.lod = 0;
    m->mdl.parts = &m->part;
    m->mdl.boxes = (char *)m->boxes;
    for (int i = 0; i < 2; i++) {
        m->nodes[i].scale[0] = m->nodes[i].scale[1] = m->nodes[i].scale[2] = 1.0f;
        identity(m->nodeMtx[i]);
        identity(m->clusterMtx[i]);
    }
    /* node 0 moves the model half a unit right (the matrices the packets
     * carry are products, not copies) */
    m->nodeMtx[0][12] = 0.5f;
    m->nodeMtx[1][12] = 0.5f;
    m->clusterMtx[1][13] = 1.0f; /* bone 1 lifts its vertices a unit */
    o->model = &m->mdl;
    o->nodes = m->nodes;
    o->nodeNum = cluster ? 2 : 1;
    o->nodeMtx = (ICO_WORD)m->nodeMtx;
    o->clusterMtx = (char *)m->clusterMtx;
    o->lightMtx = &m->light;
    o->dispType = cluster ? 1 : 0;    /* 1: a skinned model (DObj.c) */
    o->skelNodeNum = cluster ? 2 : 0; /* clusterMtx holds one matrix per bone */
    if (cluster) {
        /* light: L1 = (n.z, n.x, n.y, n.w) as vu1_test.c, L2 colours with an
         * ambient column */
        float *l1 = &m->light.normal[0][0], *l2 = &m->light.color[0][0];
        qw4(l1 + 0, 0, 1, 0, 0);
        qw4(l1 + 4, 0, 0, 1, 0);
        qw4(l1 + 8, 1, 0, 0, 0);
        qw4(l1 + 12, 0, 0, 0, 1);
        qw4(l2 + 0, 0.5f, 0.5f, 0.5f, 0);
        qw4(l2 + 4, 0.25f, 0, 0, 0);
        qw4(l2 + 8, 0, 0.25f, 0, 0);
        qw4(l2 + 12, 0.125f, 0.125f, 0.25f, 0);
        m->light.mode = 1;
        /* cluster table: bone 0 and bone 1 each list every vertex with its
         * weight; the bone lists live in the EE word arena */
        static size_t arenaAt = 16;
        for (int j = 0; j < 2; j++) {
            unsigned char *list = ico_arena_base() + arenaAt;
            for (int v = 0; v < NV; v++) {
                int vi = v;
                float w = j == 0 ? m->w0[v] : 1.0f - m->w0[v];
                memcpy(list + v * 16, &vi, 4);
                memcpy(list + v * 16 + 4, &w, 4);
            }
            int end = -1;
            memcpy(list + NV * 16, &end, 4);
            arenaAt += (size_t)(NV + 1) * 16;
            m->polys[j].p = ico_eew(list);
            int bone = j;
            memcpy((char *)&m->polys[j] + 4, &bone, 4);
        }
        m->part.polys = m->polys;
        m->part.polyCount = 2;
    } else {
        m->light.mode = 0; /* normal_c, prelit */
    }
}

#endif
