/*
 * ico2/sugipon/include/flag.h
 *
 * The declarations of what flag.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef FLAG_H
#define FLAG_H

#include "clothAnimation.h"

struct GObj;

void SetFlag4PointFixID(struct GObj *self, int turns, int id);

typedef struct { /* field names derived */
    float m[4];
} Vec4Flag; /* derived name */

/* The flag's work record, InitFlagGeo's 20-byte allocation: the cloth
   type (the layout kind's low four bits), the clothes InitClothes builds,
   their config, whether the object collides, and SetFlag4PointFixID's
   quarter turns. */
typedef struct { /* field names derived */
    int type;
    ClothSet *clothes;
    ClothCfg *cfg;
    int collide;
    int turns;
} FlagWork; /* derived name */

/* one word of a record, read and written through a union member: the
   node flags' bit clears and InitFlagGeo's float stores */
typedef union { /* field names derived */
    long long ll;
    int i;
    float f;
    short h;
} FlagNodeWord; /* derived name */

/* InitFlagGeo's stores through a record viewed as words: the EE's 4-byte
   offsets, the host's fields */
#ifdef ICO_HOST
#define FLAG_SET_ANCHORS(cl, mesh) ((cl)->anchors = (mesh))
#define FLAG_SET_POSW(self) (GOBJ_SUB(self)->root.pos[3] = 1.0f)
#else
#define FLAG_SET_ANCHORS(cl, mesh) (((FlagNodeWord *)&(cl)->anchors)->i = (int)(mesh))
#define FLAG_SET_POSW(self) (((FlagNodeWord *)((char *)GOBJ_SUB(self) + 0xAC))->f = 1.0f)
#endif
/* The sub record's display-list buffers (0xC the matrices, 0x10 the
   vectors, 0x870 the 80-byte nodes) freed and reallocated for n nodes, and
   the nodes reset; worm.c and boy.c carry the same block.  The block has its
   own counter, and each node's 0.0f words are stored from one float set at
   the top of the loop body.  FLAG_NODE_WORD is the word at the EE offset
   off, on the host the field it holds (Sub15C's prefix is wider on a 64-bit
   host: package 2I). */
#define FLAG_NODE_WORD(o, off, field) ICO_RAW(char *, o, off, *(char **)&(o)->field)
#define FLAG_ALLOC_NODES(o, num) /* derived name */                                                \
    {                                                                                              \
        int n;                                                                                     \
        if ((o)->nodeMtx != 0) {                                                                   \
            iosFree((void *)ICO_PHYS((o)->nodeMtx));                                               \
        }                                                                                          \
        if ((o)->nodeQuat != 0) {                                                                  \
            iosFree((void *)ICO_PHYS((o)->nodeQuat));                                              \
        }                                                                                          \
        FLAG_NODE_WORD(o, 0xC, nodeMtx) = 0;                                                       \
        FLAG_NODE_WORD(o, 0x10, nodeQuat) = 0;                                                     \
        FLAG_NODE_WORD(o, 0xC, nodeMtx) =                                                          \
            iosMallocDebug(ios_partition_seki, (num) * 64, __FILE__, __LINE__);                    \
        FLAG_NODE_WORD(o, 0x10, nodeQuat) =                                                        \
            iosMallocDebug(ios_partition_seki, (num) * 16, __FILE__, __LINE__);                    \
        (o)->nodeNum = (num);                                                                      \
        if ((o)->nodes != 0) {                                                                     \
            iosFree((void *)ICO_PHYS(ICO_ADDR((o)->nodes)));                                       \
        }                                                                                          \
        (o)->nodes = iosMallocDebug(ios_partition_seki, (num) * 80, __FILE__, __LINE__);           \
        for (n = 0; n < (num); n++) {                                                              \
            float zero = 0.0f;                                                                     \
            (o)->nodes[n].flags.ll &= ~1;                                                          \
            (o)->nodes[n].flags.ll &= ~2;                                                          \
            (o)->nodes[n].pos[0] = zero;                                                           \
            (o)->nodes[n].pos[1] = zero;                                                           \
            (o)->nodes[n].pos[2] = zero;                                                           \
            (o)->nodes[n].pos[3] = 1.0f;                                                           \
            (o)->nodes[n].flags.ll &= ~4;                                                          \
            (o)->nodes[n].fade = zero;                                                             \
            (o)->nodes[n].alpha = 1.0f;                                                            \
            *(short *)((char *)&(o)->nodes[n] + 0x3A) = 0;                                         \
            (o)->nodes[n].scale[0] = 1.0f;                                                         \
            (o)->nodes[n].scale[1] = 1.0f;                                                         \
            (o)->nodes[n].scale[2] = 1.0f;                                                         \
        }                                                                                          \
        (o)->dispType = 2;                                                                         \
    }
/* the flag's root position, Sub15C + 0xA0 (SetFlag4PointFixID); a raw view on
   the EE, the field on the host (ee_view.h) */
#define FLAG_ROOT_POS(o) ICO_RAWP(char *, GOBJ_SUB(o), 0xA0, (char *)GOBJ_SUB(o)->root.pos)
#endif /* FLAG_H */
