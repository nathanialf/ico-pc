/*
 * common/include/typedef.h, the `common` programmer's shared header.
 *
 * PROVENANCE.  Dev path `common/include/typedef.h`, reached from `fumi/` as
 * `../common/include/typedef.h`.  `baserom/pal/SRCFILE.TXT` attributes
 * instructions to exactly one of its lines (census:
 * docs/pal_source_tree.md, section `fumi/../common/include/typedef.h`):
 *
 *   line 74, avoid_obstacle2 (src/way_sys, 0x0017DA50), 3 rows / 2 expansions
 *
 * so the header holds at least one `static` helper besides whatever typedefs
 * its name implies.  Instructions from a MACRO expansion would be attributed
 * to the caller's line, not to line 74, so line 74 is a function.
 *
 * WHAT IS IN HERE AND ON WHAT EVIDENCE.  Three classes, marked individually:
 *
 *   (a) the line-74 helper, fixed by the listing: the float absolute value
 *       avoid_obstacle2 inlines twice; see section (a) below.
 *
 *   (b) the cross-programmer engine object shapes (GObj, PObjGObj, Sub15C,
 *       Act) and the GOBJ_SUB and GOBJ_ACT accessors.  PLACEMENT BY INCLUDE
 *       PATTERN, NOT BY LISTING ROWS: a struct declaration emits no
 *       instructions, so no listing row can name its home.  They are used by
 *       37 TUs spanning common, fumi, ito, seki, sugipon and fumi/sound, and
 *       this is the only header the disc attests that every one of those
 *       trees reaches (`-I../common/include` is in every ico2 TU's search
 *       order).  The shapes themselves are mechanical, recovered from load
 *       offsets by tools/dump_all_struct_shapes.py; only add a field with a
 *       verified access.
 *
 *   (c) the R5900 and VU0 macro-mode opcode wrappers.  PLACEMENT BY INCLUDE
 *       PATTERN, NOT BY LISTING ROWS: every one of their expansions is
 *       attributed by SRCFILE.TXT to the .c line that invokes it, which is
 *       what a macro expansion looks like and is therefore consistent with
 *       any header home, including one the listing cannot see.  They are
 *       used across seki (Matrix, BgAnimation, GifPacket), sugipon
 *       (clothAnimation, matrixDrive, quaternion, motionManager2, stormTest,
 *       sugiCommon.h) and ito (itou_sub, lightning, mpeg/mv_disp,
 *       mpeg/mv_vobuf), so no single programmer's header can own them and
 *       this one can.  The macro NAMES are ours; none is a disc fact.
 *       Wrappers used by exactly one TU are NOT here, they live in that TU:
 *       QCOPY64_PARALLEL in seki/src/Matrix.c, and QCOPY64_SERIAL /
 *       LQ16_FROM / SQ16_TO / MAP_A0_TO_SPR in sugipon/src/matrixDrive.c.
 *       The sce/ archives get their own per-member definitions: their uses
 *       stand for Sony-internal headers this tree cannot name.
 *
 * Nothing here is a typedef lifted out of a leaked or SDK header.
 */
#ifndef TYPEDEF_H
#define TYPEDEF_H

/* ------------------------------------------------------------------ *
 * (a) the line-74 helper, a float absolute value.
 *
 * Both of its expansions in the ROM, avoid_obstacle2's |dx| and |dz|
 * tests (the listing's rows inside way_sys.c 633 and 634), are
 * `mtc1 $zero,$fN; c.lt.s $f1,$fN; bc1tl <skip>; neg.s $f1,$f1`, i.e.
 * `x < 0.0f ? -x : x`, and SRCFILE.TXT attributes their three rows to
 * this file's line 74 alone.  A macro expansion would carry the caller's
 * line, so the helper is a function whose whole body is on that line.
 * It is never emitted out of line, so it has no symbol in
 * baserom/pal/MAIN.MAP and no disc name: `absf` is ours.  Unreferenced,
 * a static inline function emits nothing, so every other includer's
 * object is unchanged (each compared by whole object when it landed,
 * chain 1 pass 130).
 *
 * One host uses it (twice); the listing, not a second host, is what
 * places it here.
 *
 * The helper below is line 74.  DO NOT REFLOW ANYTHING ABOVE IT, and
 * keep it on one line: the listing puts every instruction of both
 * expansions on that line.
 * ------------------------------------------------------------------ */
static inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

/* ------------------------------------------------------------------ *
 * Host-port seams (ICO_HOST is defined by the native build; the PS2
 * build leaves it undefined and gets the original arithmetic).
 *
 * ICO_QW / ICO_UQW   the 128-bit quadword type.  ee-gcc spells it
 *                    mode(TI); the host has no such mode under -m32, so
 *                    it is a 16-byte-aligned pair of 64-bit halves.
 * ICO_ADDR(p)        a pointer as the EE code held it, an int address
 *                    (a cast there, the pointer itself on the host).
 * ICO_PHYS(p)        the physical address of a cached or uncached EE
 *                    address: `p & 0x0FFFFFFF`.
 * ICO_UNCACHED(p)    the uncached alias of an address: `p | 0x20000000`.
 * ICO_UNCACHED_ACCEL(p)  the uncached-accelerated alias: `p | 0x30000000`.
 *                    The host has a flat address space, so the three
 *                    address macros leave their operand unchanged, and its type, so a
 *                    pointer stays a pointer.  The EE forms take an int
 *                    operand; pass pointers through ICO_ADDR.
 * ICO_INVALID_PTR    the all-ones pointer the scripts use as "none".
 * ICO_POSTINC(T, p)  `((T)(p))++`, the cast-as-lvalue post-increment.
 * ICO_BREAK()       the EE debug trap (`break`); __builtin_trap() on the host.
 * ICO_WORD           a struct field the EE code holds an address in as an int
 *                    ("held as a word"): `int` on the EE, whose code
 *                    generation depends on the int type, and a pointer-wide
 *                    integer (intptr_t) on the host, so the address survives
 *                    a 64-bit build and the integer arithmetic the code does
 *                    on it (byte offsets, masks) means the same.
 * ICO_WORD_PTR(T)    the same for a word only ever converted to a pointer:
 *                    `int` on the EE, the pointer type T on the host.
 * ICO_SPR_ADDR(off)  the address `off` bytes into the 16 KB scratchpad
 *                    (0x70000000 on the EE); the host points it at
 *                    ico_scratchpad, defined in seki/src/Basic.c.
 * ------------------------------------------------------------------ */
#ifdef ICO_HOST
typedef struct ICO_QW {
    unsigned long long lo, hi;
} __attribute__((aligned(16))) ICO_QW;
typedef ICO_QW ICO_UQW;
#define ICO_ADDR(p) (p)
#define ICO_PHYS(p) (p)
#define ICO_UNCACHED(p) (p)
#define ICO_UNCACHED_ACCEL(p) (p)
#define ICO_INVALID_PTR ((void *)(__UINTPTR_TYPE__)-1)
extern char ico_scratchpad[16 * 1024] __attribute__((aligned(16)));
#define ICO_SPR_ADDR(off) ((__UINTPTR_TYPE__)ico_scratchpad + (off))
#define ICO_BREAK() __builtin_trap()
#define ICO_WORD __INTPTR_TYPE__
#define ICO_WORD_PTR(T) T
/* ICO_POSTINC(T, p): `((T)(p))++`, a cast used as an lvalue that ee-gcc 2.9
 * allows and the host compiler does not.  Yields (T)p and advances the byte
 * pointer p by sizeof(*(T)p). */
#define ICO_POSTINC(T, p)                                                                          \
    ({                                                                                             \
        __typeof__((T)0) ico_old_ = (T)(p);                                                                     \
        (p) = (void *)((char *)(p) + sizeof(*ico_old_));                                           \
        ico_old_;                                                                                  \
    })
#else
typedef int ICO_QW __attribute__((mode(TI)));
typedef unsigned int ICO_UQW __attribute__((mode(TI)));
#define ICO_ADDR(p) ((int)(p))
#define ICO_PHYS(p) ((p) & 0x0FFFFFFF)
#define ICO_UNCACHED(p) ((p) | 0x20000000)
#define ICO_UNCACHED_ACCEL(p) ((p) | 0x30000000)
#define ICO_INVALID_PTR ((void *)0xFFFFFFFF)
#define ICO_SPR_ADDR(off) (0x70000000 | (off))
#define ICO_BREAK() __asm__ __volatile__("break")
#define ICO_WORD int
#define ICO_WORD_PTR(T) int
#define ICO_POSTINC(T, p) ((T)(p))++
#endif

/*
 * RECONSTRUCTION.  Every shape below was read back out of the binary's own
 * memory accesses (offsets and widths from the load and store mnemonics, via
 * tools/dump_all_struct_shapes.py); no disc artefact declares any of them.
 * The names are this repository's, not the developers': the disc's maps name
 * functions and objects, never a struct, a field or a typedef.  And this file
 * is where they sit BY INCLUDE PATTERN, NOT BY LISTING ROWS: a declaration
 * emits no instructions, so no row of SRCFILE.TXT can name its home, and
 * ico2/common/include/typedef.h is the only attested header every one of the
 * 37 TUs that use these shapes reaches.
 *
 * `GObj` = the game object passed as `self` to per-object functions. The
 * name is the engine's own term, re-derived from the public PAL ICO-decomp's
 * Char/GObj list API (GetCharGObjList / MakeCharGObjList), a reference, not
 * a verbatim copy. Every field is named from what its readers and writers do
 * with it; a word whose readers do not fix its meaning is named by its type
 * and offset (word13C), and a word nothing in the tree reads is padding.
 * Padding is explicit so every field sits at its exact recovered offset; a
 * wrong offset is caught by the byte gate.
 *
 * Structs grow as TUs are typed; only add a field with a verified access.
 */

/* The 0x15C sub-object slot is an INT handle the engine casts to a pointer at
 * use, not a clean Sub15C*. Reading it int-typed reproduces the developer's
 * TBAA: it may-alias adjacent int writes, so the load reloads (not hoisted),
 * matching byte-for-byte WITHOUT the per-function int-typed-reload hacks
 * (COOKBOOK section 8.22). Pointer-chain users still match (no aliasing trigger).
 * Use this accessor for 0x15C; keep dobj in the struct for layout only. */
#ifdef ICO_HOST
#define GOBJ_SUB(o) (((GObj *)(o))->dobj)
#else
#define GOBJ_SUB(o) ((Sub15C *)*(int *)&((GObj *)(o))->dobj)
#endif
/* The 0x164 actor slot, the companion of GOBJ_SUB: the action-state object the
 * per-object functions run their state machines out of. */
#define GOBJ_ACT(o) ((Act *)((GObj *)(o))->act)

typedef struct GObj GObj; /* derived name */

typedef struct Sub15C Sub15C; /* *(GObj + 0x15C), the object's display object (DObj.c) */ /* derived name */

/* The 80-byte record Sub15C + 0x870 points at, one per display node; the
 * buffer is reallocated with the 0xC matrices and 0x10 vectors for the node
 * count.  The display (RegistPacket.c) draws a node with alpha
 * 1 - (1 - fade) * alpha, fading when bit 0 of the flag word is set, turns it
 * by the Z angle at 0x3A and places it at pos. */
struct DObjNode {   /* field names derived */
    int rot[4];     /* 0x00, the angles, read back as shorts */
    int word10[4];  /* 0x10 */
    float scale[4]; /* 0x20 */
    float fade;     /* 0x30 */
    float alpha;    /* 0x34 */

    union {
        int i;
        long long ll; /* bit 0 fade, bits 1 and 2 */
    } flags;          /* 0x38; its 0x3A half is also stored as a short, the Z angle; stored
                         through a union member, worm.o and enemyParts.o move (measured) */

    float pos[4]; /* 0x40, reset to 0 0 0 1 */
};

/* One mail in a game object's mail box (obj_manager.c): the mail type and
 * what it carries, the object that sent it or the record the type names. */
typedef struct IosMail { /* field names derived */
    int type;
    void *arg;
} IosMail; /* derived name */

/* A game object's mail box, the 0x108 bytes at GObj + 0x54: obj_manager.c
 * queues and runs the mails through this record. */
typedef struct IosMailBox { /* field names derived */
    int queue;              /* never read */
    int num;                /* the count of queued mails */
    IosMail mail[32];
} IosMailBox; /* derived name */

/* GObj and PObjGObj below are two views of ONE record: the game object.  GObj
 * types the run-list links, the display object pointer and the run function;
 * PObjGObj carries them as words.  The names are this repository's.  On the
 * host PObjGObj is GObj itself (docs/port/LOADERS.md). */
struct GObj {   /* field names derived */
    GObj *self; /* 0x0, the object itself while its table entry is
                               in use, 0 when free (gobj.c) */
    int labelType; /* 0x4, the label type the object was created under, 1 for a stage layout object, -1 for none */
    int labelId; /* 0x8, the layout or label number within that type (objLayout's row for a layout object) */
    int kind;             /* 0xC, the object-kind id, -1 when the object has
                                 none; it indexes the ObjKindEnt table */
    GObj *next;           /* 0x10, next object on its run list */
    GObj *prev;           /* 0x14, previous object on its run list */
    unsigned char linkId; /* 0x18, which of the eight run lists */
    char pad19[3];
    unsigned int key; /* 0x1C, the run list's sort key */
    char pad20[4];
    int word24; /* 0x24, cleared by StageAnimation.c once its object's display list is linked; nothing reads it */
    void (*fn)(GObj *);     /* 0x28, the object's per-frame function */
    struct GProc *procHead; /* 0x2C, head of the object's process list */
    struct GProc *procTail; /* 0x30, tail of the same list */
    struct GObj *dlNext;    /* 0x34, next object on its display list (gobj_dl.c, gobj_cam_dl.c) */
    struct GObj *dlPrev;    /* 0x38, previous object on its display list */
    GObj *kindNext;         /* 0x3C, next object of the same kind */
    unsigned char dlLinkId; /* 0x40, the display list the object is linked into (gobj_dl.c) */
    char pad41[3];
    int dlKey;          /* 0x44, the display list's sort key */
    void (*dl)(GObj *); /* 0x48, the object's display function, which the object manager calls */
    int kindMask; /* 0x4C, a camera's object kinds, one bit per gobj_dl_link_head list */
    int drawMask; /* 0x50, ANDed with a camera's mask to pick the cameras that draw it; all ones while shown */
    IosMailBox mailBox; /* 0x54, the mail box obj_manager.c queues and runs */
    Sub15C *dobj;     /* 0x15C, the display object (CSVSYSTEM_InitDObj) */
    char pad160[4];
    ICO_WORD_PTR(void *) act; /* 0x164, the actor/action-state object, held as a word like
                0x15C: the actor and script translation units read it as Act
                through GOBJ_ACT below, other translation units hang their own
                record there */
    char pad168[4];
    int active;      /* 0x16C, nonzero while the object is active: the object
                  manager runs only active objects' functions and processes */
    int pauseExempt; /* 0x170, nonzero when the object keeps running while the
                  game is paused (systemStatus[5]) */
};

/* a float quadword with its doubleword view, 16-byte aligned */
typedef union Vec16 { /* field names derived */
    float f[4];
    long long ll[2];
} __attribute__((aligned(16))) Vec16; /* derived name */

/* An object and one of its nodes: the motion work opens with the parent
   it is linked to, the rootUpdates return the one they stand on, and the
   wall-hit record at ClipWork+0x80 is one followed by the hit count.  GetPureVerticalPlane reads it as its `int *cfg` argument (see
   getVerticalElementOfWallNormal in src/motionManager2) and the character
   record keeps a copy at +0xE0.  The object/node pair is its own member: the
   ROM copies it as an eight-byte block and the count as a separate word. */
typedef struct ObjNode { /* field names derived */
    GObj *obj;          /* the object */
    int node;    /* the node index in its geometry */
} ObjNode;       /* derived name */

typedef struct { /* field names derived */
    ObjNode o;
    void *elem; /* the element of o's collision: a wall (FcWallEnt) or a
                   floor (FcFloorEnt), 0 for none */
} WallCfg; /* derived name */

/* One half of an orient request, the 12 bytes the request copies move: the
 * point the motion turns to, or the wall (an object, its node and the wall
 * element) or the object the mail that asked for it names. */
typedef union { /* field names derived */
    float pos[3];
    WallCfg wall;
    struct GObj *obj;
} MotOriTarget; /* derived name */

/* The 32-byte orient record an actor hands to SetMotionRequest by value: two
 * targets, each with a fourth word. */
typedef struct MotOriReq { /* field names derived */
    MotOriTarget a; /* 0x00 */
    int aw;         /* 0x0C */
    MotOriTarget b; /* 0x10 */
    int bw;         /* 0x1C */
} MotOriReq;        /* derived name */

/* RECONSTRUCTION, the type and enumerator names are ours: the 0x360 word of the
 * root block (MotRoot handIK) holds the table's 2-bit mode (bits 26-27 of the 0x188 word).
 * The ROM stores it ahead of the int store to the motion control while every int store to
 * the block stays behind that one, so its lvalue has an alias set of its own:
 * an enumerated mode, as motionOrientManager.c's debug_bar_flag. */
enum MotOriShiftMode { MOTORI_SHIFT_0, MOTORI_SHIFT_1, MOTORI_SHIFT_2, MOTORI_SHIFT_3 };

/* One hand of the root block, 0x60 bytes: the mode RequestChangeHandMode
   sets, the object node and point the hand reaches for, and the turn IK
   HandManager runs toward ikDir (motMan_getFinalMatrix reads hand 0's rate,
   reached and flag words; hand 1's are kept by the template only). */
typedef struct HandRec { /* field names derived */
    int mode;          /* 0x0, the mode flag */
    struct GObj *obj;  /* 0x4, the object the hand reaches for */
    int node;          /* 0x8, the node the hand reaches for */
    char pad0C[4];
    float pos[4];      /* 0x10, the hand target */
    int ikMode;        /* 0x20, the turn IK mode, 0 for off */
    int ikLock;        /* 0x24 */
    char pad28[8];
    float ikDir[4];    /* 0x30, the turn target */
    float ikQuat[4];   /* 0x40 */
    float ikRate;      /* 0x50, the slerp rate toward the target */
    int ikReached;     /* 0x54, 1 once the target is reached */
    int ikFlag;        /* 0x58 */
    char pad5C[4];
} HandRec; /* derived name */

/* RECONSTRUCTION, names ours: the motion work's root block (the motion work
   + 0xA0, up to its motion-control block at + 0x470), the record skelRoot
   points at.  The position, translation, rotation and the last position are
   what every rootUpdate reads and writes; the wall records are the ones the
   hit tests keep; the lift pair and the turn at + 0x300 are _getFinalMatrix's.
   Fields from the access widths and offsets in motionManager.c and from the
   display-object readers across the game (Sub15C root below). */
struct MotRoot {       /* field names derived */
    float pos[4];      /* 0x0 */
    float trans[4];    /* 0x10 */
    float baseQuat[4]; /* 0x20, the base turn SetRootBaseQuaternion sets */
    float quat[4];     /* 0x30 */
    float
        motionQuat[4]; /* 0x40, the root turn the motion gives over the base (getMotionGeometry) */
    short twist;       /* 0x50 */
    char _pad52[2];
    float twistRate; /* 0x54 */
    char _pad58[8];
    float up[4];      /* 0x60 */
    float savePos[4]; /* 0x70 */
    ObjNode hitObj;   /* 0x80 */
    char _pad88[8];
    float move[4]; /* 0x90 */
    float
        step[4]; /* 0xA0, the root step of the frame (GetGeometryOfMotion copies it to rootStep) */
    float itemQuat
        [4]; /* 0xB0, the held item's turn (item.c resets it on a pickup, weapon.c turns the blade by it) */
    float height; /* 0xC0 */
    char _padC4[12];
    float delta[4];     /* 0xD0 */
    WallCfg wall;       /* 0xE0 */
    int wallCount;      /* 0xEC */
    WallCfg cliffWall;  /* 0xF0, the wall found under the cliff edge (checkCliffState) */
    int cliffWallCount; /* 0xFC, its hit count, -1 once copied */
    WallCfg aheadWall;  /* 0x100, the wall clipWallAhead hit */
    char _pad10C[20];
    WallCfg filter; /* 0x120 */
    char _pad12C[4];
    Vec16 plane;      /* 0x130 */
    int lastField;    /* 0x140, the field GetCollisionOfLastActiveField returns */
    void *cliffFloor; /* 0x144, the floor under the cliff edge, 0 for none */
    char _pad148[8];
    float last[4];     /* 0x150 */
    float clipFrom[4]; /* 0x160, where the root's wall clip starts */
    float stepMove
        [4]; /* 0x170, the step the motion moves the root by, rotated by the root quaternion */
    int standNode; /* 0x180, the skeleton node the root stands on, -1 for none */
    char _pad184[12];
    float focusPos[4];   /* 0x190, the focus node's position */
    float focusLocal[4]; /* 0x1A0, the focus node's position in the root's frame */
    float footPos[4];    /* 0x1B0, the foot position fitted to the floor */
    float reservePos[4]; /* 0x1C0, the reserved position, in world space */
    float
        projHeight; /* 0x1D0, the height above the floor the root keeps (GetRootProjectionPosOfGObj adds it) */
    char _pad1D4[44];
    int liftOn; /* 0x200, 1 (the default): the root update lifts the foot pair onto a step node (kind 0x30) */
    int lifting; /* 0x204, set while that lift is applied; _getFinalMatrix bends the leg nodes by it */
    float lift[2]; /* 0x208 */
    HandRec hand1; /* 0x210, hand record 1 (RequestChangeHandMode mode 1) */
    HandRec hand0; /* 0x270, hand record 0 (RequestChangeHandMode mode 0) */
    float armTwist[4]; /* 0x2D0, the arm turn eased toward the hand targets */
    int lookMode;      /* 0x2E0, the look-target mode, 2 to turn the head fully */
    char _pad2E4[12];
    float lookPos[4]; /* 0x2F0, the look target */
    short h;          /* 0x300 */
    short p;          /* 0x302 */
    short b;          /* 0x304 */
    char _pad306[2];
    int noStepSearch; /* 0x308, the motion forbids the stand-node search */
    int gravity;      /* 0x30C, the motion falls under gravity */
    int slopeIK;      /* 0x310, the motion runs the slope foot IK */
    int stairStep;    /* 0x314, the motion steps the root by 50-unit stairs */
    int lookIK;       /* 0x318, the motion turns the head to lookPos */
    int handTurnIK;   /* 0x31C, the motion turns toward the hand targets (1) */
    int fieldWall;    /* 0x320, the motion clips against field walls */
    int fuchiMode;    /* 0x324, the edge reaction mode */
    int cylinder; /* 0x328, the motion record's cylinder flag: the object takes part in cylinder collision */
    int avgWallPlane; /* 0x32C, the motion averages four wall planes */
    int flag330;      /* 0x330, the motion drops node 4's own turn */
    int flag334;      /* 0x334, the motion drops node 6's own turn */
    float radius;     /* 0x338, the clip radius */
    float radiusTo;   /* 0x33C, the clip radius a shift eases to */
    float radiusFrom; /* 0x340, the clip radius it eases from */
    char _pad344[12];
    float cliffPlane[4];         /* 0x350, the plane at the cliff floor's height */
    enum MotOriShiftMode handIK; /* 0x360, nonzero while HandManager runs the hand IK */
    int stepNode;                /* 0x364, the focus node the step solution walks on */
    char _pad368[8];
    float holdPoint[4]; /* 0x370, the point the hang hold is measured from */
    int ropeState;      /* 0x380, 0, or -1 and 1 by the hold height on the chain */
    ICO_WORD fixObj; /* 0x384, the object SetMotionNodeFixModeParameter fixes the node to; held as a word: stored as GObj * (or char *, P4-xcut), SetMotionNodeFixModeParameter's code changes (measured) */
    int fixNode; /* 0x388, the focus node on that object */
    char _pad38C[4];
    float fixQuat[4];     /* 0x390, the fixed node's turn */
    float fixPos[4];      /* 0x3A0, the fixed node's offset */
    float fixWeight;      /* 0x3B0 */
    unsigned int fixMode; /* 0x3B4 */
    float footIKRate;     /* 0x3B8, the slope IK's blend */
    float ikRate0;        /* 0x3BC, the look IK's blend rate */
    float handRate;       /* 0x3C0, the hand IK's blend rate */
    float ikRate1;        /* 0x3C4 */
    float ikRate2;        /* 0x3C8 */
    char _pad3CC[4];
};

/* the 0x08C counter (cleared by shiftMotionData, stepped by
   UpdateFrameCounter) is not an int to the scheduler: in shiftMotionData its
   store does not precede the inlined int table read the other int stores do,
   so its lvalue has an alias set of its own, which a 32-bit enumerated type
   gives (motionOrientManager.c). */
enum MotOriStep { MOTORI_STEP_0 };

/* RECONSTRUCTION, names ours: the motion work's motion-control block (the
   motion work + 0x470), the record skelMotCtrl points at: the stream, the
   status flags, the motion number and the root-update mode the assert in
   _getGeometryOfMotion prints ("ID", "rootUpdateMode"), the wall and cliff
   distances and normals the hit tests leave.  Sub15C embeds it as ctrl. */
struct MotCtrl {           /* field names derived */
    int stream;            /* 0x0 */
    int oriFrom;           /* 0x4, the first motionOrient row the request searches */
    int oriTo;             /* 0x8, one past the last */
    int shifted;           /* 0xC, 1 after a motion shift */
    int ctrlFlags;         /* 0x10 */
    unsigned int flags;    /* 0x14 */
    int shiftStop;         /* 0x18, nonzero stops the motion shift (ExecMotionOrient reports it) */
    int *shiftReq;         /* 0x1C, the request table searchMotionShift walks, -1 terminated */
    int *shiftNext;        /* 0x20, the motion each request shifts to */
    int shiftFrom;         /* 0x24 */
    int shiftMode;         /* 0x28 */
    int request;           /* 0x2C, the requested motion */
    int motion;            /* 0x30 */
    int noAlt;             /* 0x34, 1 when the mirror table had no alternative */
    int shiftReady;        /* 0x38, the frame lies in the motion's shift range */
    float animFrame;       /* 0x3C, the current animation frame */
    float lastFrame;       /* 0x40 */
    float playTime;        /* 0x44 */
    float speedRatio;      /* 0x48 */
    float playRate;        /* 0x4C */
    float frameRatio;      /* 0x50, the frame, 0 to 1, of a mode 4 motion */
    int waterDrag;         /* 0x54, the play slows in water */
    int justShifted;       /* 0x58, 1 for the frame of a shift */
    int frameEnd;          /* 0x5C, the motion reached its end (its loop kind) */
    int keepUpdateMode;    /* 0x60, the shift keeps rootUpdateMode */
    int updateModeChanged; /* 0x64 */
    int rootUpdateMode;    /* 0x68 */
    int parallelEnded;     /* 0x6C */
    int parallel;          /* 0x70, the motion is a parallel one */
    int orientUpdateOff;   /* 0x74, 1 while the motion orient update is disabled */
    int noStand; /* 0x78, 1 drops the object the root stands on after the geometry update (st99a's explosion) */
    int posReserve;       /* 0x7C, 1 while a position reservation is pending */
    int loopFlag;         /* 0x80 */
    int reserveBlend;     /* 0x84, frames left of the reservation blend */
    int reserveMoved;     /* 0x88 */
    enum MotOriStep step; /* 0x8C, frames since the shift */
    int orientReq;        /* 0x90 */
    int lastMotion;       /* 0x94 */
    int lastNoAlt;        /* 0x98 */
    int shiftFrame;       /* 0x9C, the frame the last motion was left at */
    int blendCount;       /* 0xA0 */
    int blendFrames;      /* 0xA4 */
    char _padA8[8];
    float dir[4];     /* 0xB0, the motion direction */
    float lastDir[4]; /* 0xC0 */
    int orientKind;   /* 0xD0 */
    int floorFit;       /* 0xD4, nonzero fits the root to the floor after a direct move */
    int wallReact;      /* 0xD8, nonzero runs the low wall clip, the wave force and the step wall reaction */
    int cliffWallCheck; /* 0xDC, the cliff and wall checks: 1 every frame, 2 alternating on variation */
    int catchBoy;     /* 0xE0, 1 while the enemy holds the boy */
    int sideWallCheck; /* 0xE4, nonzero runs checkWallSideState after the cliff and wall checks (InitBoyGeo sets it) */
    int variation; /* 0xE8, the enemy's variation counter, 0 to 9: its parity alternates the cliff and the wall check */
    float fallHeight;   /* 0xEC, the fall height the death checks compare */
    float groundHeight; /* 0xF0, the root's height above the ground */
    int wallHit;        /* 0xF4, a wall was hit this frame */
    int cliffEdge;      /* 0xF8, a cliff edge was found (flag 0x10) */
    int cliffWallHit;   /* 0xFC, a wall under the edge was found */
    int cliffBack;      /* 0x100, the wall behind the edge was found */
    int fieldWallHit;   /* 0x104, a field wall was hit */
    int upperWall;      /* 0x108, a wall above was found (flag 0x1000) */
    int sideWall;       /* 0x10C, a side wall was found */
    float cliffHeight;  /* 0x110, the floor above the edge */
    float cliffDist;    /* 0x114, the distance to the edge */
    char _pad118[8];
    float cliffNormal[4];  /* 0x120 */
    float wallFloorHeight; /* 0x130, the floor beyond the wall */
    float wallTopHeight;   /* 0x134 */
    float wallDist;        /* 0x138 */
    char _pad13C[4];
    float wallDir[4];        /* 0x140 */
    float wallNormal[4];     /* 0x150 */
    float sideWallNormal[4]; /* 0x160 */
    float upperWallDist;     /* 0x170 */
    float sideWallDist;      /* 0x174 */
    float cliffDepth;        /* 0x178, the cliff's depth below the edge */
    int pureWallAttr;        /* 0x17C */
    int pureCliffAttr;       /* 0x180 */
    int wallAttr;            /* 0x184, the attribute of the wall the root touches */
    int floorAttr;           /* 0x188, the attribute of the floor the root stands on */
    char _pad18C[4];
    int frameFlag1;   /* 0x190 */
    int frameFlag2;   /* 0x194 */
    int trigger1;     /* 0x198, the first frame trigger fired this frame */
    int trigger1Done; /* 0x19C */
    int trigger2;     /* 0x1A0, the second frame trigger fired this frame */
    int trigger2Done; /* 0x1A4 */
    float
        ropeHangPos; /* 0x1A8, where the boy hangs on the rope (the rope's chain collision, GetRopeHangablePos) */
    int seGroup
        [2]; /* 0x1AC, the two SE groups InitMotionOrient takes (soundSeGroupGet); shiftMotionOrientEndFunc asserts on -1 */
    int slipFlags;     /* 0x1B4 */
    int lastSlipFlags; /* 0x1B8 */
    int slipOn;        /* 0x1BC, the floor slip attribute bits take effect */
    GObj *pickedWeapon; /* 0x1C0, the weapon PickupWeapon picked up */
    int keepWall;      /* 0x1C4, nonzero to keep the wall contact over getGeometryOfMotion */
    int keepStand;     /* 0x1C8, nonzero to keep the stand object over getGeometryOfMotion */
    int landed;        /* 0x1CC, 1 for the frame the root comes to stand on a node */
    float waterY;      /* 0x1D0, the water surface height */
    float waterDepth;  /* 0x1D4, the depth under the pool surface */
    GObj *pool;        /* 0x1D8, the pool the object stands in */
    int contactFlags;  /* 0x1DC, the field contact bits CheckFieldContact sets */
    int mailDelay; /* 0x1E0, counts the first frame up before the motion orient sends its state mail */
    int noFieldClip; /* 0x1E4, nonzero skips the flying root's wall and field collision (rootUpdateEnemyFly) */
    int seMute; /* 0x1E8, nonzero mutes the motion SEs (playSE in frameDependSequence.c); the ending (end.c) sets it on its two layout objects 2793 and 2794 */
    char _pad1EC[4];
};

/* One 64-byte node of an object's skeleton, the array Sub15C skel points at:
   the node its motion mirrors with (MakeMirrorMotion swaps the two nodes'
   motions, or mirrors a node paired with itself, up to a -1), the node's
   kind (the act-point kinds the stand search and the stair step test: 6 and
   11 the hand-1 side, 22 and 27 the hand-0 side, 0x30), its rest position
   and rotation, its first child, its next sibling and its parent, -1 where
   there is none. */
typedef struct SkelNode { /* field names derived */
    int mirror; /* 0x0 */
    int kind; /* 0x4 */
    char pad08[8];
    float pos[4];  /* 0x10 */
    float quat[4]; /* 0x20 */
    int child;     /* 0x30 */
    int sibling;   /* 0x34 */
    int parent;    /* 0x38 */
    int pad3C;
} SkelNode; /* derived name */

/* One 64-byte IK state of a skeleton node, the array Sub15C nodeRotElem
   points at: the blend rate _getFinalMatrix eases toward its target, the
   previous heading the look turn's limit is checked against (only the
   initial copies write it, with 0), the eased pitch and its per-frame speed,
   the node's IK rotation, the per-frame step slerped toward it and its
   offset from the motion's own rotation */
typedef struct MotIk { /* field names derived */
    float rate;       /* 0x00 */
    short prevH;      /* 0x04 */
    short pad06;
    short pitch;      /* 0x08 */
    short pad0A[2];
    short pitchSpeed; /* 0x0E */
    float q[4];       /* 0x10 */
    float step[4];    /* 0x20 */
    float offset[4];  /* 0x30 */
} MotIk;              /* derived name */

/* The 192-byte motion record at Sub15C + 0x680: initGeometryState clears
   it and GetMotionPointer returns it; no reader of its words is left. */
typedef struct { /* field names derived */
    long long pad[24];
} DObjMotion; /* derived name */

struct Sub15C { /* field names derived */
    ObjNode
        parent; /* 0x0, the object and node this one hangs from (LinkParentOfDObj), obj 0 for none */
    int nodeNum; /* 0x8, the count of node matrices and quaternions at 0xC and 0x10 */
    ICO_WORD nodeMtx; /* 0xC, one 64-byte matrix a node; held as a word: typed float (*)[4][4] or char *, attackhit.o, act-game.o, commonact.o, fieldCollision.o and girl_act.o move, where the ROM adds a byte offset to it (measured) */
    ICO_WORD nodeQuat; /* 0x10, one quaternion a node; held as a word: typed float (*)[4], GetMatrixOfMotion's int-typed read of it moves (measured, P4-xcut) */
    char pad14[12];
    float matrix[4][4]; /* 0x20, the object's own matrix (initMatrixDObj) */
    float quat
        [4]; /* 0x60, the object's turn: weapon.c copies the root's into it, SetParticleEffect takes it */
    ICO_WORD colData; /* 0x70, the collision data; its wall list hangs at 0x10 */
    int disp;      /* 0x74, nonzero while the stage animation draws the object */
    int colRotate; /* 0x78, nonzero when the collision follows the node's rotation */
    int cylinderOn; /* 0x7C, nonzero, with the word at 0x3C8, when the object takes part in cylinder collision */
    int colPerNode;  /* 0x80, nonzero when the collision has one entry a node */
    int modelId;     /* 0x84, the model id the enemy setup loads (GetPObjAddress) */
    int skelNodeNum; /* 0x88, the count of skeleton nodes */
    SkelNode *skel;  /* 0x8C, the skeleton node records; node 0's pos[1] is the root height */
    char *
        clusterMtx; /* 0x90, one matrix a node, applied before the node's own for a cluster shadow */
    char pad94[12];
    struct MotRoot root; /* 0xA0, the motion root block */
    struct MotCtrl ctrl; /* 0x470, the motion control block */
    int streamScale;     /* 0x660, nonzero to scale the stream motion by the node scale */
    char pad664[12];
    float streamOfs[3]; /* 0x670, the stream motion offset */
    char pad67C[4];
    DObjMotion motion; /* 0x680, the motion record GetMotionPointer returns */
    char fdsFlags
        [116]; /* 0x740, the frame-depend sequence's fired-slot flags (frameDependSequence.c's FDSFlags) */
    void *motionBuf; /* 0x7B4, the current motion's rotation elements, 32 bytes a skeleton node */
    char pad7B8[8];
    float motionPos[4]; /* 0x7C0, the position GetMatrixOfMotion places the root at */
    void *blendBuf;     /* 0x7D0, the blend source's rotation elements */
    char pad7D4[12];
    float localPos[4]; /* 0x7E0, the position relative to the object at 0x800 */
    float localMove
        [4]; /* 0x7F0, the root movement the local position steps by while no object holds it */
    ObjNode local;     /* 0x800, the object and node the position at 0x7E0 is relative to, obj 0 for none */
    float localHeight; /* 0x808, the height added to the local position */
    MotIk *nodeRotElem; /* 0x80C, one IK state a skeleton node */
    int *nodeLimit;    /* 0x810, the address of each skeleton node's rotation limit record */
    float (*nodeVec)[4]; /* 0x814, one vector a skeleton node */
    float (*blendRot)[4][4]; /* 0x818, four quaternions a skeleton node, the blend's rotations */
    int (*rideFunc)(
        ObjNode *on,
        GObj *
            rider); /* 0x81C, called for an object that comes to stand on this one (CageRideFunc, poolRideFunc) */
    char *blendless;  /* 0x820, one byte a node, set where the motion blend leaves the node alone */
    float scaleRatio; /* 0x824, the geometry scale ratio initGeometryScaleRatio sets */
    char pad828[8];
    void *work;   /* 0x830, the actor's own work record; each actor TU casts it to its own shape */
    int morphNum; /* 0x834, the count of morph weights at 0x838 */
    float *morphWeight; /* 0x838, one weight a morph target of the model's parts */
    int lightId;        /* 0x83C, the light the object carries (0 for none), Light.c */
    char *
        focusNodes; /* 0x840, the skeleton node for each focus point GetSkeltonFocusNode looks up */
    int accessary;  /* 0x844, the object's row in the accessary table */
    void (*lodFunc)(int lv); /* 0x848, called with the level SetLodLevel sets, 0 for none (girl.c's cloth setting) */
    unsigned short
        dispType; /* 0x84C, 2 when the object draws its node list, 1 for a cluster model */
    char pad84E[2];
    char *lineHdr;            /* 0x850, read by RegistPacket.c's point and line display */
    struct PObjModel *model;  /* 0x854, DisplayP2O.h */
    struct PObjModel *shadow; /* 0x858 */
    char pad85C[4];
    float shadowDir[4];           /* 0x860, the direction the shadow is cast in */
    struct DObjNode *nodes;       /* 0x870, the node records, nodeNum of them */
    struct LightMatrix *lightMtx; /* 0x874, the object's light matrices (Light.h) */
};

/* the 16-byte aligned float quadword the VU0 entry points take and return
 * (its copies are lq/sq quadword moves) */
typedef struct { /* field names derived */
    float x, y, z, w;
} __attribute__((aligned(16))) VECTOR; /* derived name */

/* RECONSTRUCTION, PUBLIC SDK NAMING RUNG.  The 16-byte aligned integer
 * quadword; the name is the one the public PS2 SDK documentation gives
 * libvu0's integer vector.  It is declared here rather than in
 * sce/libvu0/libvu0.h because StageSetting below uses it and 64 of the TUs
 * that include this header do not include libvu0.h.
 */
typedef int sceVu0IVECTOR[4] __attribute__((aligned(16)));

#ifdef ICO_HOST
/* The host build: the VU0 and R5900 wrappers below have no host meaning.
 * The VU0 maths is C in port/math (docs/port/MATH.md); the game sources that
 * used these wrappers keep the assembly under `#ifndef ICO_HOST` and call
 * port/math instead. ico_math.h is included here so every game TU sees the
 * current matrix, the PS2 float helpers (ps2float.h) and the small vector
 * routines (vector_inline.h). */
#include "../../../port/math/ico_math.h"
/* Memory barrier: a full fence. */
#define SYNC() __atomic_thread_fence(__ATOMIC_SEQ_CST)
/* No interrupts to mask: the game's threads are cooperative on the host. */
#define DI() ((void)0)
#define EI() ((void)0)
/* Any other use is a compile error: assembly that reached the host build
 * has not been rewritten in C yet. */
#define ICO_ASM_NOT_PORTED()                                                                      \
    _Static_assert(0, "R5900/VU0 inline assembly in the host build: rewrite it in C over "       \
                      "port/math (docs/port/MATH.md)")
#define QCOPY16(scratch) ICO_ASM_NOT_PORTED()
#define VU0_MEM(insn) ICO_ASM_NOT_PORTED()
#define VU0_REG(insn) ICO_ASM_NOT_PORTED()
#define VU0_LSV(mnem, vf, off, base) ICO_ASM_NOT_PORTED()
#define VU0_LSGP(mnem, gp, off, base) ICO_ASM_NOT_PORTED()
#define VU0_LSV_R(mnem, vf, off, base) ICO_ASM_NOT_PORTED()
#define VU0_V2OP(mnem, d, a) ICO_ASM_NOT_PORTED()
#define VU0_V3OP(mnem, d, a, b) ICO_ASM_NOT_PORTED()
#define VU0_V3OP_BC(mnem, d, a, b, bc) ICO_ASM_NOT_PORTED()
#define VU0_V3OP_ACC(mnem, a, b) ICO_ASM_NOT_PORTED()
#define VU0_V3OP_ACC_BC(mnem, a, b, bc) ICO_ASM_NOT_PORTED()
#define VU0_MFC1(gp, fp) ICO_ASM_NOT_PORTED()
#define VU0_MTC1(gp, fp) ICO_ASM_NOT_PORTED()
#define VU0_QMFC2_NI(gp, vf) ICO_ASM_NOT_PORTED()
#define VU0_QMTC2_NI(gp, vf) ICO_ASM_NOT_PORTED()
#define VU0_CFC2_NI(gp, vi) ICO_ASM_NOT_PORTED()
#define VU0_WAIT() ICO_ASM_NOT_PORTED()
#define VU0_WORD(w) ICO_ASM_NOT_PORTED()
#define VU0_NOREORDER_BEGIN() ICO_ASM_NOT_PORTED()
#define VU0_NOREORDER_END() ICO_ASM_NOT_PORTED()
#else /* !ICO_HOST */
/* ------------------------------------------------------------------ *
 * (c) R5900 opcodes with no C spelling.
 *
 * Each emits exactly one instruction inside a volatile inline-asm block.
 * ------------------------------------------------------------------ */

/* Memory sync barrier, stalls the CPU until pending stores commit.
 * Used as a fence between a write and an external observer (GS, IPU,
 * VU0/1, DMAC).  ico2 sites: ito/mpeg/mv_disp, ito/mpeg/mv_vobuf. */
#define SYNC() __asm__ __volatile__("sync" : : : "memory")
/* Disable / enable interrupts (COP0 DI, EI).  Encodings 0x42000039 and
 * 0x42000038; the period ee-as has no mnemonic for either.  ico2 sites:
 * seki/src/Matrix (the VU0 register push/pop pair), ito/mpeg/mv_disp,
 * ito/mpeg/mv_vobuf. */
#define DI() __asm__ __volatile__(".word 0x42000039" : : : "memory")
#define EI() __asm__ __volatile__(".word 0x42000038" : : : "memory")
/* Quadword copy, one 128-bit lq/sq pair through a scratch GPR (the nop in
 * the return slot after it is the toolchain's own).  The scratch register
 * differs per call site ($8 in seki/src/Matrix, $6 in
 * sugipon/src/matrixDrive), so it is a macro argument.  dst/src are
 * implicit in $4/$5: the macro is the BODY of a two-pointer wrapper. */
#define QCOPY16(scratch)                                                                           \
    __asm__ __volatile__("lq " scratch ", 0($5)" : : : "memory");                                  \
    __asm__ __volatile__("sq " scratch ", 0($4)" : : : "memory")

/* ------------------------------------------------------------------ *
 * (c) VU0 / COP2 macro-mode opcodes.
 *
 * The R5900's VU0 macro mode exposes ~120 distinct instruction variants
 * (broadcast + dest-mask combinations).  A unique macro per variant would
 * multiply the surface without buying any analysis power, since the
 * assembler already parses the asm text embedded in each one.  Instead
 * there is ONE macro per side-effect class.  Each emits exactly one
 * instruction inside its own __asm__ block, with a "memory" clobber only
 * on the loads and stores that observe caller-visible state.
 * ------------------------------------------------------------------ */

/* ===========================================================
 *  TYPED MACROS, preferred form.  Pass operands as tokens; the
 *  macro builds the asm string via stringify-and-paste.
 * ===========================================================
 *
 *  Naming: VU0_<SHAPE>(<mnem-with-mask>, <operands...>)
 *
 *  The mnemonic (including mask, e.g. `vmul.xyzw`, and any
 *  broadcast prefix in the mnemonic, e.g. `vaddz`) is passed as
 *  a SINGLE token; operands are unprefixed register numbers
 *  (e.g. `13` for `$vf13`, `8` for `$8`, `f12` for `$f12`).
 *
 *  Memory loads/stores
 *  -------------------
 *    VU0_LSV(mnem, vf, off, base)   -> "<mnem> $vf<vf>, <off>($<base>)"
 *      Use for: lqc2, sqc2, vlqd, vsqi.
 *    VU0_LSGP(mnem, gp, off, base)  -> "<mnem> $<gp>, <off>($<base>)"
 *      Use for: lq, sq, ld, sd.
 *
 *  VU compute (register-only)
 *  --------------------------
 *    VU0_V2OP(mnem, d, a)         -> "<mnem> $vfd, $vfa"
 *      Use for: vmove, vmr32, vftoi0/4/12/15, vsqrt-style 2-arg.
 *    VU0_V3OP(mnem, d, a, b)      -> "<mnem> $vfd, $vfa, $vfb"
 *      Use for: vmul.MASK / vsub.MASK / vadd.MASK with no broadcast.
 *    VU0_V3OP_BC(mnem, d, a, b, bc)
 *                                 -> "<mnem> $vfd, $vfa, $vfb<bc>"
 *      Use for: vmulx.MASK / vaddy.MASK / vmaddw.MASK style ops where
 *      the broadcast letter is part of the mnemonic AND appears as a
 *      register suffix on the b operand.
 *    VU0_V3OP_ACC(mnem, a, b)     -> "<mnem> ACC, $vfa, $vfb"
 *      Use for: vopmula.MASK, vopmsub.MASK destination=ACC.
 *    VU0_V3OP_ACC_BC(mnem, a, b, bc)
 *                                 -> "<mnem> ACC, $vfa, $vfb<bc>"
 *      Use for: vmulax/vmadday/vmaddaz.MASK style.
 *
 *  EE<->VU transfer
 *  ----------------
 *    VU0_MFC1(gp, fp)        -> "mfc1 $<gp>, $f<fp>"
 *    VU0_MTC1(gp, fp)        -> "mtc1 $<gp>, $f<fp>"
 *    VU0_QMFC2_NI(gp, vf)    -> "qmfc2.ni $<gp>, $vf<vf>"
 *    VU0_QMTC2_NI(gp, vf)    -> "qmtc2.ni $<gp>, $vf<vf>"
 *    VU0_CFC2_NI(gp, vi)     -> "cfc2.ni $<gp>, $vi<vi>"
 *
 *  Escape hatches (pass full asm string)
 *  -------------------------------------
 *    VU0_MEM(insn), caller-visible load/store; "memory" clobber.
 *    VU0_REG(insn), register-only; no clobber.
 *
 *  Use the escape hatches only when no typed macro fits, e.g. for
 *  `vrnext`, `vrxor`, `vrsqrt`, `vdiv`, `viaddi`, `vmulq`, or any
 *  rare opcode without a typed shape above.  When the same shape
 *  shows up in 3+ functions, lift it into a new typed macro here.
 */

/* Escape-hatch macros (raw asm string).  Use sparingly.
 *
 * RECONSTRUCTION, like the whole R5900 and VU0 wrapper set below and above it:
 * the opcodes are what the ROM's instructions decode to, the wrapper around
 * them is this repository's reconstruction of how the source reached them, and
 * every macro name here is ours rather than the developers', a macro leaves
 * no symbol and the disc's maps name none of them.  They sit in this header BY
 * INCLUDE PATTERN, NOT BY LISTING ROWS: SRCFILE.TXT attributes each expansion
 * to the .c line that invokes it, which is consistent with any header home, so
 * what places them here is that seki, sugipon and ito all use them and this is
 * the one attested header all three trees reach.
 *
 * Both bodies assemble with reordering off.  The game tree's VU0 opcodes are
 * hand-scheduled against the COP2 pipeline, so the assembler must leave them
 * where they are written; the visible consequence is the return of a VU0 leaf,
 * where ee-as would otherwise swap the closing `sqc2` into the `jr $31` delay
 * slot and the ROM has a `nop` there instead (69 sites in six objects).  The
 * SDK's own copy of these macros in sce/libvu0/libvu0.c is deliberately not
 * spelled this way: the ROM carries `sqc2` in 26 of that archive's return
 * slots, so the two trees were built from differently spelled templates. */
#define VU0_MEM(insn)                                                                              \
    __asm__ __volatile__(".set noreorder\n\t" insn "\n\t.set reorder" : : : "memory")
#define VU0_REG(insn) __asm__ __volatile__(".set noreorder\n\t" insn "\n\t.set reorder")
/* Memory load/store: typed forms. */
#define VU0_LSV(mnem, vf, off, base) VU0_MEM(#mnem " $vf" #vf ", " #off "($" #base ")")
#define VU0_LSGP(mnem, gp, off, base) VU0_MEM(#mnem " $" #gp ", " #off "($" #base ")")
/* Like VU0_LSV but the base address is a C expression bound via an "r"
 * constraint, so gcc sees the data dependency on `base`. Use when the
 * base is a function argument/local that must stay in a callee-saved reg
 * across calls: the explicit dependency makes the scheduler emit the
 * base-setup move just before the load (filling the prologue's ra-save
 * gap) instead of greedily up front. */
#define VU0_LSV_R(mnem, vf, off, base)                                                             \
    __asm__ __volatile__(#mnem " $vf" #vf ", " #off "(%0)" : : "r"(base) : "memory")
/* VU compute: 2-operand register-to-register (vmove, vmr32, vftoi*). */
#define VU0_V2OP(mnem, d, a) VU0_REG(#mnem " $vf" #d ", $vf" #a)
/* VU compute: 3-operand register-to-register. */
#define VU0_V3OP(mnem, d, a, b) VU0_REG(#mnem " $vf" #d ", $vf" #a ", $vf" #b)
/* Same with broadcast on b operand (mnemonic has broadcast letter,
 * b operand has matching register suffix). */
#define VU0_V3OP_BC(mnem, d, a, b, bc) VU0_REG(#mnem " $vf" #d ", $vf" #a ", $vf" #b #bc)
/* VU compute to ACC. */
#define VU0_V3OP_ACC(mnem, a, b) VU0_REG(#mnem " ACC, $vf" #a ", $vf" #b)
#define VU0_V3OP_ACC_BC(mnem, a, b, bc) VU0_REG(#mnem " ACC, $vf" #a ", $vf" #b #bc)
/* EE<->VU transfer ops. */
#define VU0_MFC1(gp, fp) VU0_REG("mfc1 $" #gp ", $f" #fp)
#define VU0_MTC1(gp, fp) VU0_REG("mtc1 $" #gp ", $f" #fp)
#define VU0_QMFC2_NI(gp, vf) VU0_REG("qmfc2.ni $" #gp ", $vf" #vf)
#define VU0_QMTC2_NI(gp, vf) VU0_REG("qmtc2.ni $" #gp ", $vf" #vf)
#define VU0_CFC2_NI(gp, vi) VU0_REG("cfc2.ni $" #gp ", $vi" #vi)

/* VU0_NOP() (an explicit `nop` before a VU0 leaf's return) was retired 2026-09-05:
   the return-slot nop after an inline-asm block is the assembler's, and
   tools/compile_c.sh reproduces it (docs/NOTES.md "Return-slot padding"). */

/* Wait-for-Q-pipeline barrier (vwaitq).  No memory effect but
 * sequences subsequent VU0 ops with prior compute. */
#define VU0_WAIT() __asm__ __volatile__("vwaitq")
/* Raw 32-bit word emission for COP2 ops without a gas mnemonic
 * (e.g., `vsqrt Q, $vfNx` -> .word 0x4A0X03BD). */
#define VU0_WORD(w) __asm__ __volatile__(".word " #w)
/* Hazard-pair scheduler barriers.
 *
 * Several R5900 COP2 transfer pairs have intrinsic load-delay or
 * Q-pipeline interlocks that gas's default `.set reorder` mode
 * "fixes" by inserting a `nop` between them.  The original ICO
 * codegen does NOT have those nops -- the bytes are tight.  Wrap
 * the affected pair in VU0_NOREORDER_BEGIN/END to suppress gas's
 * auto-fill.
 *
 * Example: `mfc1 $tN, $fM` followed by `qmtc2.ni $tN, $vfK` has a
 * 1-cycle GPR load-delay.  In `.set reorder` gas inserts a nop
 * between them; in `.set noreorder` gas leaves the bytes untouched
 * (the EE pipeline is forwarding-correct already).
 */
#define VU0_NOREORDER_BEGIN() __asm__ __volatile__(".set noreorder")
#define VU0_NOREORDER_END() __asm__ __volatile__(".set reorder")
#endif /* !ICO_HOST */

/* the stage's display setting: the lights, fog, shadow, post effects and camera limits */
typedef struct StageSetting { /* field names derived */
    float flatLightDir[3][4]; /* 0x000 */
    float flatLightCol[3][4]; /* 0x030 */
    float ambientCol[4];      /* 0x060 */
    float bgCol[4];           /* 0x070, the clear colour (sceneManager) */
    /* the fog words, as ico2/seki/src/ZFog.c reads and edits them */
    int fogOn;       /* 0x080 */
    char pad084[12]; /* 0x084 */
    int fogColR;     /* 0x090 */
    int fogColG;     /* 0x094 */
    int fogColB;     /* 0x098 */
    int fogColA;     /* 0x09C */
    int fogOffsetA;  /* 0x0A0 */
    int fogNear;     /* 0x0A4 */
    int fogFar;      /* 0x0A8 */
    /* the shadow words, as ico2/seki/src/Shadow.c's tool labels them */
    int shadowDepth;    /* 0x0AC */
    int shadowBlend[4]; /* 0x0B0, the 1/1, 1/4, 1/16 and 1/64 blends */
    int shadowColR;     /* 0x0C0 */
    int shadowColG;     /* 0x0C4 */
    int shadowColB;     /* 0x0C8 */
    char pad0CC[4];     /* 0x0CC */
    /* RECONSTRUCTION: the reduction tint used while no sub target is current,
       and the per sub target row whose fourth word is the film grain tint
       ico2/seki/src/GsBase.c reads at 0x13C. */
    int reductionCol[3]; /* 0x0D0 */
    char pad0DC[4];      /* 0x0DC */
    /* RECONSTRUCTION: the stage's view scale, a percentage that
       ico2/seki/src/GsBase.c's gsb_SetVSMatrix divides by 100 into the zoom
       and again into the mip map level. */
    int viewScale; /* 0x0E0 */
    /* 0x0E4 to 0x110: named after the labels ico2/seki/src/GsBase.c's stage
       setting menu prints for them */
    int texSampleMode;   /* 0x0E4, "Def Tex Sample Mode" */
    int postEffect;      /* 0x0E8, "Post Effect" */
    int depthFieldStart; /* 0x0EC, "DepthField Start" */
    int depthFieldWidth; /* 0x0F0, "DepthField Width" */
    int motionBlur;      /* 0x0F4, "Motion Blur" */
    int depthFieldLevel; /* 0x0F8, "DepthField Level" */
    int antiLevel0;      /* 0x0FC, "AntiLevel0" */
    int antiLevel1;      /* 0x100, "AntiLevel1" */
    int feedbackEffect;  /* 0x104, "Feedback Effect" */
    char pad108[8];      /* 0x108 */
    int feedbackCol[4];  /* 0x110, "Feedback Effect R, G, B, A" */
    int fogStrength;     /* 0x120 */
    char pad124[12];     /* 0x124 */

    /* RECONSTRUCTION: each target row is a 16 byte aligned quadword (red,
       green, blue, then the film grain tint), the ROM's own proof being
       gsb_Reduction's tint reads in ico2/seki/src/GsBase.c.  expr.c folds
       a member's constant offset onto the record's base before adding the
       variable index only while the reference's alignment is exactly the
       field's; with the row aligned to 16 it adds base and index first, cse
       puts the index first, and the 0x130 stays the load's displacement,
       which is the ROM's `addu index, index, base` / `lw 0x130`.  The row
       is the typed quadword; it replaced a bare aligned(16) on an r, g, b, a
       struct (re-audit, completeness pass 57), with every user's object
       byte-identical. */
    sceVu0IVECTOR targetCol[4]; /* 0x130 */

    /* RECONSTRUCTION: the film grain's UV step, which
       ico2/seki/src/GsBase.c's gsb_filmNoise passes as raw bits. */
    float grainScale; /* 0x170 */
    char pad174[12];  /* 0x174 */
    /* the camera limits ico2/omori/src/camera-root.c loads per stage, named
       after GsBase.c's menu labels */
    int handCameraLimitP; /* 0x180, "HandCamera Limit P" */
    int handCameraLimitV; /* 0x184, "HandCamera Limit V" */
    char pad188[8];       /* 0x188 */
    int zoomMaxInDemo;    /* 0x190, "ZOOM MAX IN DEMO" */
    char pad194[8];       /* 0x194 */

    struct {
        int a;
        int b;
    } antiLevel[4]; /* 0x19C */

    int subMotionBlur[5]; /* 0x1BC */
} StageSetting; /* derived name */

/* an RGBA colour as four words, 16-byte aligned */
typedef union { /* field names derived */
    int c[4];
    long long ll[2];
} __attribute__((aligned(16))) Col4; /* derived name */

/* one target the girl's brain weighs: the object and its attention level */
typedef struct { /* field names derived */
    GObj *gobj;
    float level;
    float levelCap; /* 0x08, the most the level climbs to */
    float capStep;  /* 0x0C, what brainClsTargetLevel takes off the cap */
    float rate;     /* 0x10, the level's rise and decay rate */
    int timer;
    unsigned char lookOnly;   /* 0x18, the girl only looks at the target; nonzero holds the target type at 1 */
    unsigned char alwaysSeen; /* 0x19, nonzero skips the view check */
    unsigned char detail;     /* 0x1A, bit 0 set by brainAddLevelGirlDetail */
    char pad1B[1];
} BrainTarget; /* derived name */

/* the GIF packet builder's double buffer and its write pointers */
typedef struct { /* field names derived */
    int cur;
    int *buf[2];
    char *dma;
    unsigned long long *ptr;
    char *tail;
    char *gif;
    char *end;
} GifDpk; /* derived name */

/* a packet doubleword with its two word halves */
typedef union { /* field names derived */
    long long d;
    int w[2];
} GifPkWord; /* derived name */

/* a gamesys object-info record as debug.c reads it: the kind word */
typedef struct { /* field names derived */
    char pad0[32];
    int kind; /* 0x20, debug.c's SE test reads it as the SE kind */
    char pad24[24];
} GsysObjInfo; /* derived name */

typedef union { /* field names derived */
    int i;
    float f;
} IntFloat; /* derived name */

/* one memory-card directory entry: the file size and name */
typedef struct { /* field names derived */
    char pad0[16];
    int size; /* 0x10 */
    char pad14[12];
    char name[32]; /* 0x20 */
} McDirEnt; /* derived name */

/* a quadword as four floats or four words */
typedef union { /* field names derived */
    float f[4];
    int i[4];
} Vec4u; /* derived name */

/* one pad's per-frame state as keyInput.c keeps it */
typedef struct PadState { /* field names derived */
    int now;               /* 0x00, the buttons held this frame */
    int flags;             /* 0x04, the trigger bits */
    int rel;               /* 0x08 */
    int rep;               /* 0x0C, the auto-repeat bits */
    int old;               /* 0x10, last frame's buttons (keyInput.c fills it) */
    unsigned int hist[16]; /* 0x14, per-button held-frame counts */
    unsigned char ana[4];  /* 0x54, the two analog sticks */
} PadState; /* derived name */

struct GamesysObjInfo;

struct SObjSimpleSetting;

/* The 70-entry object-kind table at objKindData, one 0x64-byte row per kind,
 * indexed by the kind id a GObj carries at +0xC.  The row's leading 0x24 bytes
 * are the kind's name ("BOY", "GIRL", "GIRLDEMOCTRL", ...), which debug.c and
 * debug_menu.c hand straight to the menu as a string; the offsets that hold a
 * text address in every row are spelled as function pointers. */
typedef struct {        /* field names derived */
    char name[36];      /* 0x00 */
    float targetTime;   /* 0x24, brain's target timer, -1.0 = none */
    float brainCapStep; /* 0x28, the girl brain's cap step for the kind (BrainTarget) */
    float brainRate;    /* 0x2C, the girl brain's rate for the kind */
    int brainLevel;     /* 0x30, the brain level the kind starts with, 0 for none */
    void (*infoLoad)(
        GObj *, struct GamesysObjInfo *); /* 0x34, applies the saved object info to the new GObj */
    void (*infoInit)(
        struct SObjSimpleSetting *,
        struct GamesysObjInfo *); /* 0x38, builds the create arguments from the saved object info */
    void (*uniqDataSet)(int *, GObj *); /* 0x3C, gamesys' per-kind unique-data writer */
    void (*start)(GObj *);              /* 0x40, the actor's start process (actBoyStart) */
    int layouted;       /* 0x44, nonzero when the kind is created from the stage layout */
    void (*dl)(GObj *); /* 0x48, the display function (BoyDL) */
    void (*afterGeo)(
        GObj *);            /* 0x4C, the process CreateGObjByFuncSet adds after the geometry one */
    void (*geo)(GObj *);    /* 0x50, the geometry process (BoyGeo) */
    void (*hotInit)(GObj *); /* 0x54, sceneManager's hot-init hook */
    void *(*create)(GObj *, void *); /* 0x58, the kind's GObj constructor */
    void (*ai)(GObj *);            /* 0x5C, the brain process (GirlAI, EnemyAI) */
    void (*before)(GObj *); /* 0x60, the object's per-frame function (BeforeFunc); nonzero means
                           the kind takes mail 47 */
} ObjKindEnt; /* derived name */

/* the 0x194-byte per-stage preset record at stageData */
typedef struct {   /* field names derived */
    char key[32];  /* 0x00, the stage's short name ("gate"); no C reader */
    char name[32]; /* 0x20, the stage name icoMisc prints, "st04a (GATE_1ST)" */
    char key2
        [32]; /* 0x40, the stage-setting file name GsBase builds its .ssb and .lock paths from */
    float fog[8];           /* 0x60, the fog switch, colour, offset, near and far
                                sceneManager casts into the stage setting record */
    char dataFile[32];      /* 0x80, the data file name access.c builds a path from */
    short ent[16];          /* 0xA0, the exit-data entries; StageManager reads the first 15 */
    float windDir[3];       /* 0xC0, the wind direction InitWindManager passes */
    float windPos[3];       /* 0xCC, the wind field's origin */
    float bgCol[3];         /* 0xD8 */
    float ambientCol[3];    /* 0xE4 */
    float flatLightCol[3];  /* 0xF0 */
    float flatLightDir[3];  /* 0xFC */
    int seSegFirst;         /* 0x108, sound data segment range */
    int seSegLast;          /* 0x10C */
    int seEnvFirst;         /* 0x110, sound SE environment range */
    int seEnvLast;          /* 0x114 */
    int camSetId;           /* 0x118, the camera set the stage opens with */
    int word11C;            /* 0x11C, no C reader */
    int animLayoutFirst;    /* 0x120, the stage-animation layout range StageAnimation walks */
    int animLayoutLast;     /* 0x124 */
    int labelTop;           /* 0x128, generator label range */
    int labelEnd;           /* 0x12C */
    int layoutFirst;        /* 0x130, layout range */
    int layoutLast;         /* 0x134 */
    int mdl[4];             /* 0x138, the four stage model ids GetRealModelId picks from */
    int word148;            /* 0x148, no C reader */
    int mot;                /* 0x14C, the motion-set id */
    void (*endproc)(void);  /* 0x150 */
    void (*initproc)(void); /* 0x154, the per-stage init hook */
    float windAmp;          /* 0x158, the wind speed's variance */
    float windSpeed;        /* 0x15C, the wind's base speed */
    int word160;            /* 0x160, no C reader */
    int wayGroupEnd;        /* 0x164 */
    int seSegData1;         /* 0x168, the sound data segment 1 keeps into the stage */
    int seSegData2;         /* 0x16C, the same for segment 2 */
    int wayGroupStart;      /* 0x170 */
    int word174;            /* 0x174, no C reader */
    int word178;            /* 0x178, no C reader */
    int word17C;            /* 0x17C, no C reader */
    float ledgeRange; /* 0x180, how near the girl must be across a ledge (act-env.c squares it) */
    float handCameraRate; /* 0x184 */
    short shadowDepth;    /* 0x188, copied into the stage setting's shadow depth */
    unsigned char pad18A[2];
    /* 0x18C, flag word whose low half is the reverb depth (soundManager reads
     * that half at this offset) and whose bits 16-24 carry a count (10 to 300
     * in the shipped rows: backStage's way-kidnap time in seconds, 0 for no way
     * kidnap).  RECONSTRUCTION: bits 25-30 are a bitfield,
     * the stage's movie number StageManager copies into mpegPlay, because the
     * ROM reads it as one: a whole-word field makes expand_expr fold the 0x18C
     * into a constant table base (expr.c 6464-6482), where StageManager's three
     * reads keep the table base, add the index product and load at 0x18C,
     * with the const table's unchanging flag on the load. */
    unsigned int reverbDepth : 16;
    unsigned int kidnapSeconds : 9;
    unsigned int mpegNo : 6;
    unsigned int attrTop : 1;
    /* 0x190, a word of one-bit stage switches.  They are bitfields because the
     * ROM reads them as bitfields: a read off the record itself keeps the
     * const table's unchanging flag on the load and emits the index product
     * as the first operand of the address add (actCommonFall, actCommonEdgeHang). */
    unsigned int flag0 : 1; /* no C reader */
    unsigned int flag1 : 1; /* GeneratorGeo: the stage keeps the boy out; initSceneGObj tests it */
    unsigned int flag2 : 1; /* actCommonEdgeHang: re-clip the hang to the floor */
    unsigned int flag3 : 1; /* actCommonFall: print and keep the low nibble of 0x5F8 */
} StgPre; /* derived name */

/* the per-stage preset table, the stage-all data member in the ELF's .rodata
 * run: const, so a load through it is unchanging (deja.c, op.c and s_init.c
 * rely on that for their schedules) */
extern const StgPre stageData[];

/* a float quadword with its doubleword view */
typedef union { /* field names derived */
    float f[4];
    long long ll[2];
} Vec4; /* derived name */

/* the collision query ClipWall, ClipFloor and ClipCollision take: the
   segment and the clipped point, the reflection GetReflectionElement builds
   from them, the radius, the element the search skips, the wall and floor it
   hit and the plane of the hit.  The block is quadword aligned, the
   alignment of the points it opens with. */
typedef struct ClipWork { /* field names derived */
    float pt[3][4]; /* 0x00, the start, end and clipped points */
    struct {             /* GetReflectionElement's, from the hit plane */
        float bounce[4]; /* 0x30, the motion along the plane normal, scaled */
        float slide[4];  /* 0x40, the motion along the plane, scaled */
        float pos[4];    /* 0x50, the point the reflected motion reaches */
        float dir[4];    /* 0x60, the reflected motion, slide plus bounce */
    } reflect;
    float radius;        /* 0x70 clip radius */
    WallCfg filter;      /* 0x74, the element the search skips; node -1 skips
                            the whole object */
    WallCfg wall;        /* 0x80, the wall the search hit */
    WallCfg floor;       /* 0x8C, the floor the search hit */
    int attr;            /* 0x98, the attribute of the element hit */
    char pad9C[4];
    Vec16 normal;    /* 0xA0, the hit plane */
    int slideCount;  /* 0xB0, times clip_wall_1 ran the ray along a wall's end */
    char padB4[12];
} __attribute__((aligned(16))) ClipWork; /* derived name */

/* the girl's brain: the targets it weighs and the one it chose */
typedef struct { /* field names derived */
    GObj *girl;
    BrainTarget *cur;
    int lock;           /* 0x08, brainLockGirl: every level held at 0 */
    int spMode;         /* 0x0C, brainSetSpMode's one-frame request */
    int spCount;        /* 0x10, frames the special mode was asked for, clamped */
    float threshold;    /* 0x14, the level a target must pass, decaying to 0x18 */
    float minThreshold; /* 0x18 */
    short targetType;   /* 0x1C, 1 to 3 by the chosen target's level */
    char pad1E[2];
    float targetLevel; /* 0x20, the chosen target's level over the threshold, 0 to 1 */
    short idx;
    char pad26[2];
    BrainTarget tgt[40];
} Brain; /* derived name */

/* a 64-bit actor status word with its two word halves */
typedef union ActStatus { /* field names derived */
    unsigned long long ll;
    int i[2];
} ActStatus; /* derived name */

/* one time stamp pair of the video stream and the data it covers */
typedef struct ViTs { /* field names derived */
    long long pts;    /* 0x00 -1 when the pack carried none */
    long long dts;    /* 0x08 */
    int pos;          /* 0x10 byte position in the data ring */
    int len;          /* 0x14 bytes the pair covers, 0 when the slot is free */
} ViTs; /* derived name */

/* the head of a pad record: the held and triggered buttons and the analog bytes */
typedef struct Pad {      /* field names derived */
    int now;              /* 0x00, the buttons held */
    int trg;              /* 0x04 */
    char pad08[76];       /* 0x08 */
    unsigned char ana[4]; /* 0x54 */
} Pad; /* derived name */

/* one stage exit: its position and rotation and the stage it leads to */
typedef struct { /* field names derived */
    float pos[3];
    float rot[3];
    int firstWalk0; /* 0x18, the three words test_nextstage_firstwalk_set takes */
    int firstWalk1;
    int firstWalk2;
    int nextStage; /* 0x24, the stage the exit leads to */
} ExitData; /* derived name */

extern const ExitData exitData[]; /* exit-data, in .rodata */

/* One row of an actor's mail table (Act.mail, Act.mainMail): the message id
   the actor listens for and the three entry points it starts.  429 ends a
   table.  act2.c's BeforeFunc2 walks it. */
typedef struct ActMail {        /* field names derived */
    unsigned short mail;        /* 0x00, the message id */
    void (*func)(GObj *);       /* 0x04, the main the actor changes to */
    void (*motion)();           /* 0x08, the motion thread it starts */
    void (*sub)();              /* 0x0C, the sub thread it starts */
} ActMail; /* derived name */

/* a float quadword constant with its doubleword view, 16-byte aligned */
typedef union { /* field names derived */
    float f[4];
    long long d[2];
} __attribute__((aligned(16))) ConstVec; /* derived name */

/* the object record (GObj) as a view of plain words.  No game code reads
 * this view (it is kept for the EE build's type set); on the host the
 * record has one definition, GObj, and PObjGObj names it (docs/port/LOADERS.md). */
#ifdef ICO_HOST
typedef struct GObj PObjGObj;
#else
typedef struct PObjGObj { /* field names derived */
    ICO_WORD self;             /* 0x000, the object itself while in use */
    int labelType;        /* 0x004, 1 for a stage layout object */
    int labelId;          /* 0x008, the layout row */
    int kind;             /* 0x00C, the object-kind id, -1 when the object has none */
    ICO_WORD next;             /* 0x010 */
    ICO_WORD prev;             /* 0x014 */
    unsigned char linkId; /* 0x018 */
    char pad19[3];
    unsigned int key; /* 0x01C */
    char pad20[8];
    ICO_WORD fn;                 /* 0x028 */
    struct GProc *procHead; /* 0x02C, head of the object's process list */
    struct GProc *procTail; /* 0x030, tail of the same list */
    char pad34[8];
    ICO_WORD kindNext; /* 0x03C */
    int dlLinkId; /* 0x040, the display list the object is linked into */
    char pad44[4];
    ICO_WORD dl;        /* 0x048, the display function */
    int word4C;    /* 0x04C, read only by GobjProc.c's CreateGObj, through an ObjKindEnt row */
    int drawMask;  /* 0x050, ANDed with a camera's mask */
    int mailQueue; /* 0x054, the mail box (GObj's view) */
    int mailNum;   /* 0x058 */
    int mailType;  /* 0x05C */
    ICO_WORD mailArg;   /* 0x060 */
    char pad64[248];
    ICO_WORD sub; /* 0x15C, the sub-object GObj's own view types as Sub15C * */
    char pad160[4];
    ICO_WORD act; /* 0x164, the actor/action-state object */
    char pad168[4];
    int active;      /* 0x16C */
    int pauseExempt; /* 0x170 */
} PObjGObj; /* derived name */
#endif

/* Act + 0x438: the way-state word, one 64-bit flag word whose low two bytes
   are also the way walker's two status bytes; bit 16 asks for the detailed
   way search (act-way.c, girl_act.c's attract state) */
typedef union { /* field names derived */
    long long flags;
    unsigned char st[2];

    struct {
        unsigned long long : 16;
        unsigned long long wayDetail : 1;
    } bits;
} WayState; /* derived name */

/* one of the actor's 64-bit wish words (Act + 0x478 .. 0x49F): act-wish.c sets
   and tests their bits, other readers take the low or high word */
typedef union { /* field names derived */
    unsigned long long ll;
    unsigned int w[2];
} ActWishWord; /* derived name */

#include "motionOrientManager.h"
#include "pad.h"                 /* IosPadStick, which Act carries at 0x338 */

/* The pad configuration record iosPadConnect hands a pad (pad.c's
   iosPadConfDefault and iosPadConfCustom, and the copy each actor keeps), 60
   words: two byte tables and a pair table for the pressure and repeat
   handling (no reader in this build) and, at 0xB0, the sixteen button bits
   iosPadRead ORs. */
typedef struct PadConf { /* field names derived */
    unsigned char press[12][2];
    unsigned char pressRate[24];
    int repeat[16][2];
    int bit[16];
} PadConf; /* derived name */

struct WayPoint;

/* The three way points set_check_wp fills: the current one and the two ends
   of the crossing between a group and a bridge. */
typedef struct CheckWp { /* field names derived */
    struct WayPoint *cur;
    struct WayPoint *start;
    struct WayPoint *cross;
} CheckWp; /* derived name */

/* The guide-way work block way_sys.c allocates and threads through every
 * member: 0x80 bytes (the actor record embeds one at +0x360 and
 * waySystemManager's request one at +0x20), and only the words the TU reads
 * or writes are named.  act-way.c copies the actor's with 64-bit moves, so the
 * record is 8-byte aligned. */
typedef struct WVTObj { /* field names derived */
    char pad00[16];     /* 0x00 */
    int pos[4];         /* 0x10 the current target position */
    CheckWp chk;        /* 0x20 the way point being walked to (cur), the start
                          of the walk (start) and the far end of a crossing (cross),
                          the record set_check_wp fills */
    struct WayPoint *nearWp; /* 0x2C */
    int stampFrame;     /* 0x30 */
    int direction;      /* 0x34 */
    int avoiding;       /* 0x38 */
    int flag3C;         /* 0x3C */
    char pad40[4];
    int reached;      /* 0x44 */
    char pad48[8];    /* 0x48 */
    float nrm[4];     /* 0x50 the unit direction to the current way point */
    int group;        /* 0x60 */
    int guideFirst;   /* 0x64 the first point of the guide way avoid_obstacle2
                          lays round an obstacle, -1 when none */
    int escapeFound;  /* 0x68 */
    int flag6C;       /* 0x6C */
    int pathKind;     /* 0x70 */
    struct WayPoint *fromWp; /* 0x74 */
    char pad78[8];    /* 0x78 */
} __attribute__((aligned(8))) WVTObj; /* derived name */

/* A way search handed to the way system manager's sub-thread (act-way.c's
   RequestWayBegin, waySystemManager.c's actWaySystemCore): the search runs
   _FUNC_GetWay_begin from `from` to `goal` over `way`, stores the first way
   point and then sets done. */
typedef struct WayRequest {   /* field names derived */
    int done;                 /* 0x00, set once the search has run */
    struct WayPoint *result;  /* 0x04, the way point the search returned */
    char pad08[8];
    float from[4];       /* 0x10 */
    WVTObj way;          /* 0x20 */
    float goal[4];       /* 0xA0 */
    struct GProc *proc;  /* 0xB0, the sub-thread running the search, 0 when none */
} WayRequest; /* derived name */

/* The actor's environment (Act + 0x4B0), the 0x1D0 bytes ACTGetEnvironment
 * fills in and ACTEnvGetTest clears every frame, keeping the turn direction
 * and the three orient requests: the orientations and positions of the
 * wall, cliff, ditch, edge, torch, sofa and lever the actor can act on, the
 * objects it found, and the requests SetMotionRequest takes. */
typedef struct ActEnv {      /* field names derived */
    float wallOrient[4];     /* 0x0, the wall orientation */
    float cliffOrient[4];    /* 0x10, the cliff orientation */
    float torchOrient[4];    /* 0x20, the direction to the torch to light (actCommonCatchFire) */
    float torchRevOrient[4]; /* 0x30, the direction to the torch to put out (actCommonPutFire) */
    float cliffEdgePos[4];   /* 0x40 */
    float cliffStepPos[4];   /* 0x50 */
    float ditchPos[4];       /* 0x60, the ditch position */
    float ditchDir[4];       /* 0x70 */
    unsigned char ditchCarry; /* 0x80, nonzero when ditchPos is stage 8's carry-mode ditch (getDitchDistTbl) */
    char pad81[15];
    float cliffBackPos[4]; /* 0x90 */
    float edgeOrient[4];   /* 0xA0 */
    float sofaOrient[4];   /* 0xB0, the sofa seat orientation (GetSofaPosition) */
    float edgePos[4];      /* 0xC0, the edge position, [3] nonzero while it is to be taken */
    char padD0[16];
    float wallPos[4]; /* 0xE0, the point on the wall the actor touches (wallContactPos) */
    float pullPos[4]; /* 0xF0, the pull position */
    float sofaPos[4]; /* 0x100, the sofa seat position (GetSofaPosition) */
    Vec4 turnDir;     /* 0x110, the direction the enemy turns from, kept over the clear */
    char pad120[16];
    int wallWord;         /* 0x130, the wall's attribute word */
    int cliffSel;         /* 0x134, the cliff selection */
    float cliffHeight;    /* 0x138, the cliff height */
    GObj *wallObj;        /* 0x13C, the object whose wall the root hit */
    GObj *boxObj;         /* 0x140, the box (kind 17) in reach */
    GObj *holdBoxObj;     /* 0x144, the box the actor can hold */
    GObj *barObj;         /* 0x148, the bar or turning object (kind 18) */
    GObj *pullObj;        /* 0x14C, the pull lever */
    GObj *pullParent;     /* 0x150, the object the actor is linked to (Sub15C parent)
                             when it finds the lever (kind 23) */
    char pad154[4];
    GObj *swapWeapon;     /* 0x158, the weapon the actor can swap to */
    GObj *frontObj;       /* 0x15C */
    union {
        ICO_WORD i;
        GObj *obj;
    } cageObj; /* 0x160, the cage: stored as the object ACTGetEnvironment
                  found, read as an int handle (commonact.c) */
    GObj *bombObj;        /* 0x164, the bomb */
    GObj *torchRevObj;    /* 0x168, the torch */
    union {
        ICO_WORD i;
        GObj *obj;
    } sofaObj; /* 0x16C, the sofa: stored as an int (ACTGetEnvironment), read
                  as the object */
    MotOriReq motOriReq;  /* 0x170, the motion orient request SetMotionRequest
                             takes, copied from the root's at Sub15C + 0x180 */
    MotOriReq cliffContact; /* 0x190, the same copy, taken where the cliff is
                               found; mails 378 to 384 read it (381 takes the
                               boy's) */
    MotOriReq supportReq;   /* 0x1B0, the wall _SCPBoySupportGirl sets (the
                               girl's as a, the boy's as b), the request mails
                               385 and 386 copy to motOriReq */
} ActEnv; /* derived name */

typedef struct Act { /* field names derived */
    struct GProc *brainProc; /* 0x0, the actor's brain process (actChangeActBrain) */
    struct GProc *actProc;  /* 0x4, the actor's action process (actInitialize, actChangeActMain) */
    struct GProc *motProc;  /* 0x8, the motion thread an interrupt's first function runs in */
    struct GProc *motProc2; /* 0xC, the motion thread of its second function */
    int frame;              /* 0x10, the actor's frame count */
    void *after;            /* 0x14, the actor's after function (enemy_act.c's name) */

    union {
        unsigned long long ll;
#if !defined(ICO_HOST) || __SIZEOF_POINTER__ == 4
        void (*afterProc)(GObj *);
#endif
    } flags18; /* 0x18, a 64-bit word: the after-proc in the low word
                  (commonact.c stores actAfterForceRope, afterCommonRopeCliff,
                  actAfterDown and actAfterRopeJump there), the state flags
                  in the high word, which every reader tests as bits 32-63
                  of the doubleword.  On a host with 8-byte pointers the
                  after-proc lives in afterProcHost at the end of the record
                  (an 8-byte pointer here would overwrite the flags); reach
                  it through ACT_AFTER_PROC below */

    ActStatus flags20;    /* 0x20 */
    int handFreeFrame;    /* 0x28, frames since the boy and girl let go of each other's hands */
    GObj *intrArg;        /* 0x2C, the argument of the mail that interrupted this frame */
    ICO_WORD_PTR(void *) intrData; /* 0x30, the mail additional data of that mail */
    int actMode;          /* 0x34, the current action mode; it indexes actModeTbl */
    unsigned int pushDir; /* 0x38, 1 while a box or bar is pushed, -1 while pulled, 0 otherwise */
    int intrMot;          /* 0x3C, the motion the interrupting mail chose */
    int curMot;           /* 0x40, the motion number copied from the display object each frame */
    int orientMot;        /* 0x44, the motion the pull-up mails orient to */
    int actKind;          /* 0x48, the actor's column in actModeTbl */
    int modeFrame;        /* 0x4C, frames since the action mode changed */
    int msgBlockTimer;    /* 0x50, while nonzero, mail 205 is turned away */
    GObj *mother; /* 0x54, the generator the enemy was called from (actEnemyRestart), 0 for none */
    long long bits58; /* 0x58, a 64-bit flag word (ACTParaStatus's bit set) */
    char pad60[8];
    float statusWait8; /* 0x68, the value char status bit 8 carries (a wait) */
    float statusWait5; /* 0x6C, the value char status bit 5 carries (a wait) */
    float statusVal17; /* 0x70, the value char status bit 17 carries */
    int statusVal18;   /* 0x74, the value char status bit 18 carries */
    ICO_WORD_PTR(GObj *) statusTarget; /* 0x78, the target char status bit 10 carries */
    ICO_WORD_PTR(GObj *) statusOther; /* 0x7C, the object char status bit 2 carries (the nearest active enemy) */
    GObj *gobj80;      /* 0x80 */
    GObj *gobj84;      /* 0x84 */
    ICO_WORD_PTR(GObj *) statusObj; /* 0x88, the object char status bit 11 carries (bomb, gondola, box) */
    char pad8C[4];
    long long paraStatus;              /* 0x90, the parallel status bits ACTParaStatus sets */
    unsigned long long lastParaStatus; /* 0x98, the parallel status bits as last seen */
    long long flags;                   /* 0xA0 */
    GObj *lookTarget; /* 0xA8, the object the actor looks at, 0 for the position at 0xC0 */
    int lookPri;      /* 0xAC, the priority of the current look request */
    int lookMode;     /* 0xB0, the look mode passed to the display object */
    char padB4[12];
    float lookPosX; /* 0xC0, the look position, x */
    float lookPosY; /* 0xC4, the look position, y */
    float lookPosZ; /* 0xC8, the look position, z */
    char padCC[4];
    ActMail *mainMail; /* 0xD0 */
    ActMail *mail;     /* 0xD4 */
    int intrKind;      /* 0xD8, the kind of the mail that last interrupted */
    int attack; /* 0xDC, nonzero when the actor's kind attacks (act_a_p_1.c copies spiderDef's attack) */
    int readyFlags; /* 0xE0, the hand-in-hand handshake bits: 1 ready begin, 2 ready end, 8 exec end, 0x10 error */
    char padE4[12];
    float jump[4]; /* 0xF0, the jump vector of the actor's kind (act_a_p_1.c copies spiderDef's jump; AP1JumpReq) */
    char pad100[16];
    float camRootX; /* 0x110, the root position the camera follows, x */
    float camRootY; /* 0x114, the root position the camera follows, y */
    float camRootZ; /* 0x118, the root position the camera follows, z */
    char pad11C[4];
    float dir[4]; /* 0x120, the facing direction: enemy_act.c's views
                     name it dir, every actor TU writes the three
                     components */
    void *motReq; /* 0x130: the motion record SetMotionRequest returns */
    int soundMot; /* 0x134, the motion a sound mail asks for (ACTGame_SendSoundMail) */
    unsigned char soundFlag; /* 0x138, bit 0: the sound mail's wait-skip flag (the EE code reads and
                                writes it as a whole word and doubleword over soundWait) */
    char pad139[1];
    short soundWait;  /* 0x13A, frames before the next sound mail is taken */
    ICO_WORD_PTR(GObj *) reserved; /* 0x13C, the actor that reserved this one as a target (ACTReserveTarget) */
    int reservedMail; /* 0x140, the mail the reservation waits for */
    struct GObj *carrier; /* 0x144, the enemy carrying the girl */
    struct GObj *carried; /* 0x148, the object the actor holds (the carried girl) */
    GObj *brainTarget;    /* 0x14C, the enemy brain's target */
    GObj *weapon;         /* 0x150, the weapon the actor holds */
    GObj *curItem;        /* 0x154, the held item as last published (SetBoyInfo, stage change) */
    void *box;            /* 0x158, the box/truck GObj the actor is holding: commonact.c
                  stores it here in actCommonBox and reads it back through
                  `*(void **)(s + 0x158)` in the boxbar helpers */
    ICO_WORD_PTR(GObj *) barObj; /* 0x15C, the bar the actor holds (actCommonBar) */
    char *sofa; /* 0x160, the sofa the actor sits on */
    char pad164[12];
    float restartPosX; /* 0x170, the enemy restart position, x */
    float restartPosY; /* 0x174, the enemy restart position, y */
    float restartPosZ; /* 0x178, the enemy restart position, z */
    char pad17C[4];

    /* 0x180 the held item, 0x184 the item about to be taken (act-game.c's
       ItemHold): the item object, which HoldItem, ThrowItem and GetItemKind
       take as an int and the release and compare paths read as a pointer */
    union {
        ICO_WORD i;
        GObj *p;
    } heldItem;

    union {
        ICO_WORD i;
        GObj *p;
    } nextItem;

    ICO_WORD attackTurn; /* 0x188, the target the attack turns toward (ACTSearchEnemy stores the object here), 0 for none */
    char pad18C[4];
    GObj *chain;     /* 0x190, the chain the actor hangs on */
    GObj *lastChain; /* 0x194, the chain the actor last hung on */
    char pad198[8];
    float ropeSwingX; /* 0x1A0, the rope swing direction, x */
    float ropeSwingY; /* 0x1A4, the rope swing direction, y */
    float ropeSwingZ; /* 0x1A8, the rope swing direction, z */
    char pad1AC[4];
    GObj *attacker; /* 0x1B0, the object whose attack hit the actor */
    char pad1B4[12];
    float attackDir[4]; /* 0x1C0, the attack direction */
    int damage;         /* 0x1D0, the damage of the attack that hit the actor */
    int hitGroup;       /* 0x1D4, the attack group that hit the actor */
    char downHit;       /* 0x1D8, nonzero when the hit knocks the actor down */
    char stoneHit;      /* 0x1D9, nonzero when the hit turns the actor to stone */
    char hit;           /* 0x1DA, set when the actor goes down, cleared each frame */
    char unguardable;   /* 0x1DB, nonzero when the hit cannot be guarded */
    char pad1DC[4];
    float life;      /* 0x1E0, the actor's life */
    float maxLife;   /* 0x1E4, the life the enemy restarts with */
    PadConf padConf; /* 0x1E8, the pad configuration the actor's pad record
                        at 0x2D8 is connected to (actInitialize copies
                        iosPadConfDefault into it) */
    IosPadCtx pad;   /* 0x2D8, the pad handle: the buttons held, pressed and
                        released this frame at 0x2E0, 0x2E4 and 0x2E8 */
    IosPadStick stick; /* 0x338, the stick reading iosPadGetStick fills in */
    int wayMode;    /* 0x350, the way follow state: 0 none, 1 search, 2 follow */
    char pad354[12];
    WVTObj way;     /* 0x360, the guide-way work block way_sys.c walks */
    float wayNodeX; /* 0x3E0, the next way node, x */
    float wayNodeY; /* 0x3E4, the next way node, y */
    float wayNodeZ; /* 0x3E8, the next way node, z */
    char pad3EC[4];
    long long wayFlags;  /* 0x3F0, the way walk flags */
    float wayGoalDist;   /* 0x3F8, the distance to the goal across the floor */
    float wayGoalHeight; /* 0x3FC, the goal's height over the actor */
    struct WayPoint *wayLast; /* 0x400, the way point GetWay_next last returned */
    void *wayTarget; /* 0x404, the object the detailed way walk heads for (ACTWayMove_Begin) */
    char pad408[8];
    float wayFromX; /* 0x410, where that walk began, x */
    float wayFromY; /* 0x414 */
    float wayFromZ; /* 0x418 */
    char pad41C[4];
    float wayDetailX; /* 0x420, the detailed way point, x */
    float wayDetailY; /* 0x424, the detailed way point, y */
    float wayDetailZ; /* 0x428, the detailed way point, z */
    char pad42C[4];
    int wayDetailFlag; /* 0x430, cleared when the detailed walk starts */
    float wayGoalY;    /* 0x434, the goal's height the walk was started with */
    WayState wayState; /* 0x438 */
    int brainAim;      /* 0x440, the enemy brain's aim: 1 the girl, 2 the boy */
    int infoPos;       /* 0x444, the object info position the enemy is saved at */
    int brainStatus;   /* 0x448, the brain status the actor starts with */
    char pad44C[4];
    int attackGroup;  /* 0x450, the actor's attack group */
    int doorFlag;     /* 0x454, the game flag of the door script */
    float doorDist;   /* 0x458, the door's travel */
    float doorStep;   /* 0x45C, the door's step */
    ActMail *doorMail; /* 0x460, the door's main mail list */
    int doorCamWait;  /* 0x464, frames to wait after the camera move */
    int doorEndWait;  /* 0x468, frames to wait after the move */
    GObj *doorCamera; /* 0x46C, the camera target of the door move, 0 for none */
    char pad470[8];
    ActWishWord wish0; /* 0x478 */
    ActWishWord wish1; /* 0x480 */
    ActWishWord wish2; /* 0x488 */
    ActWishWord wish3; /* 0x490 */
    ActWishWord wish4; /* 0x498 */
    char pad4A0[16];
    ActEnv env; /* 0x4B0, the environment ACTGetEnvironment fills in every frame */
    struct EnemyBattleWork *enemy;          /* 0x680, the enemy work (enemy_act.c) */
    struct MailAdditionalData *mailAddData; /* 0x684, the mail additional data table */
    ICO_WORD_PTR(void *) work; /* 0x688, the actor's extended work block (act-game.h's ActWork) */
    ICO_WORD_PTR(void *) addData; /* 0x68C, the flyer's mail additional data (act-game.c's mail 298) */
    char flyClip[0x1C0] __attribute__((aligned(16))); /* 0x690, the flyer's ClipColReq (commonact.c); the
                                  record is 0x850 bytes in all */
#if defined(ICO_HOST) && __SIZEOF_POINTER__ > 4
    void (*afterProcHost)(GObj *); /* host only: flags18's after-proc (docs/port/LOADERS.md) */
#endif
} Act; /* derived name */

/* ACT_AFTER_PROC(a): the actor's after-proc as an lvalue, the low word of
 * flags18 on the EE and on 32-bit hosts, afterProcHost on hosts with 8-byte
 * pointers.  The flag bits stay in flags18.ll on every build. */
#if defined(ICO_HOST) && __SIZEOF_POINTER__ > 4
#define ACT_AFTER_PROC(a) (((Act *)(a))->afterProcHost)
#else
#define ACT_AFTER_PROC(a) (((Act *)(a))->flags18.afterProc)
#endif

/* obj-layout: one placed object of a stage, 0x4C bytes, indexed by the
 * object's GObj labelId.  sceneManager.c creates the object from it; the
 * generator (ico2/omori/src/generator.c) reads a generator's and its enemies'
 * rows, objact.c the action row, enemy_act.c and ebrain.c the flag word. */
typedef struct GenGeo { /* field names derived */
    float scale[3];     /* 0x00 */
    float rot[3];       /* 0x0C, degrees */
    float pos[3];       /* 0x18 */
    void (*proc)(
        GObj *);    /* 0x24, the object's own act process (scpDeamon), 0 for the kind's start */
    char **outGObj; /* 0x28, where the created GObj is stored (scpDummyGObj), 0 for nowhere */
    int mdl;        /* 0x2C */
    int accessary;  /* 0x30, the accessary table row; for a generator the enemy kind it calls */
    int action;     /* 0x34, the object's row in obj-action's objAction, 0 for none */
    int initArg;    /* 0x38, the last word of the create arguments */
    int word3C;     /* 0x3C, no C reader */
    unsigned short procPri; /* 0x40, the process priority, shifted by 10 */
    short reviveCount;      /* 0x42, enemies left to revive, -1 for unlimited */
    unsigned short parent;  /* 0x44, the label of the object it belongs to (an enemy's generator) */
    unsigned char kind;     /* 0x46, the object kind (33 for a generator) */
    unsigned char light;    /* 0x47, low five bits the light id */
    unsigned int
        flags; /* 0x48, display list in bits 14-16, dead bit 18, the generator display bit 21 */
} GenGeo; /* derived name */

/* one stage-animation mode record: the base mode, the second animation and the held mode */
typedef struct { /* field names derived */
    int baseMode; /* 0x00, the mode the record returns to, also its BG animation: the first stage-animation id */
    int anim2; /* 0x04, the second stage-animation id, 972 (no animation) in most rows */
    int word8; /* 0x08, no C reader */
    int mode;  /* 0x0C, 972 while held */
    int flags; /* 0x10, bit 0 holds the mode at 972 */
} OaRecB; /* derived name */

typedef struct { /* field names derived */
    float m[16];
} Mtx44 __attribute__((aligned(16)));

/* RECONSTRUCTION, names ours: one memory card's product file, 0x1F0 bytes,
 * the record mcard.c's product_write and product_read move whole: the twenty
 * save files' previews, then the options and the last save's place; 16-byte
 * aligned, a SIF DMA buffer for the card code.
 * Writers: ico2/fumi/ios/mcard.c; readers: common/src/layout_action.c,
 * common/src/kanbanBoot.c. */
typedef struct {        /* field names derived */
    unsigned int stage; /* 0x00, the saved stage, 0xFFFFFFFF for an empty file */
    int cleared;        /* 0x04, gFlagGameClear at the save */
    int playTime;       /* 0x08, in frames */
    int sofa;           /* 0x0C, the save sofa's layout id (GetSaveSofaLayoutID) */
    int word10;         /* 0x10, no C reader */
} McFileInfo;           /* derived name */

typedef struct {                              /* field names derived */
    McFileInfo file[20];                      /* 0x000 */
    int soundMode;                            /* 0x190, systemStatus[11] */
    int outputMode;                           /* 0x194, soundOutputModeGet's */
    int vibration;                            /* 0x198, iosPadActRequestEnable */
    int controlType;                          /* 0x19C, optionControlType */
    char padConf[64];                         /* 0x1A0, iosPadConfCustom's bits */
    int fileNo;                               /* 0x1E0, the card's current file */
    int serial;                               /* 0x1E4, the serial of the save it holds */
    int cameraMove;                           /* 0x1E8, NonLinearCameraMove */
    int palMode;                              /* 0x1EC, systemStatus[0] */
} McProductFile __attribute__((aligned(16))); /* derived name */

/* mcard.c's two product files, one per port */
extern McProductFile IosMcProductFile[];

#include <libmc.h> /* sceMcTblGetDir, which McMgr carries at 0x4C0 */

/* the manager's status word: flag bits in the low word, the command
   iosMcManager runs in the high word */
typedef union { /* field names derived */
    long long ll;

    struct {
        int bits;
        int command;
    } w;
} McFlags; /* derived name */

/* RECONSTRUCTION, names ours: the memory-card manager block iosMcManager
 * runs a request on, on the 64-byte alignment of a DMA transfer buffer
 * (0xA00 bytes with its padding).  mcard.c works on the one it is handed;
 * kanbanBoot.c owns one for the boot check. */
typedef struct McMgr {       /* field names derived */
    McFlags flags;           /* 0x00 -- bit 0 idle, bit 1 saving; the command in the high word */
    int port;                /* 0x08 */
    int slot;                /* 0x0C */
    int result;              /* 0x10 -- the last sceMcSync result */
    int type;                /* 0x14 -- sceMcGetInfo's card type */
    int free;                /* 0x18 -- sceMcGetInfo's free clusters */
    int cardState;           /* 0x1C -- the last card-state result, -1 formatted, -2 unformatted */
    int format;              /* 0x20 -- sceMcGetInfo's format flag */
    int segment;             /* 0x24 -- the save segment (iOSMcSaveSeg) being read or written */
    int fd;                  /* 0x28 */
    int openMode;            /* 0x2C */
    int cmd;                 /* 0x30 -- the function sceMcSync reports as finished */
    int size;                /* 0x34 */
    int pos;                 /* 0x38 */
    int end;                 /* 0x3C */
    int fileNo;              /* 0x40 -- the number in the save file's name */
    int dirCount;            /* 0x44 -- entries filled in by sceMcGetDir */
    const void *segArg;      /* 0x48 -- what the segment's save and load handlers are given */
    int sum;                 /* 0x4C */
    int readSum;             /* 0x50 -- the checksum read back from the card */
    unsigned char buf[1024]; /* 0x54 -- the one-sector staging cache */
    char dirName[20];        /* 0x454 */
    char pwd[20];            /* 0x468 */
    char path[68];           /* 0x47C */
    sceMcTblGetDir dir[20];  /* 0x4C0 */
    long long mask;          /* 0x9C0 */
} __attribute__((aligned(64))) McMgr; /* derived name */

#endif /* TYPEDEF_H */
