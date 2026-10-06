/* d3d12_plan.h: the D3D12 backend's bookkeeping that needs no D3D12 header,
 * so it builds and is tested on every host (port/rhi/test/
 * rhi_d3d12_plan_test.c): the RhiState to D3D12_RESOURCE_STATES table and
 * the barrier plan, buffer state tracking for copies, same-state copy
 * ordering, the descriptor heap rings, bind group layouts as descriptor
 * tables and root parameters, constant buffer view sizes, the DXBC input
 * signature reader, and the handle pools.
 *
 * The D3DP_STATE_* values are D3D12_RESOURCE_STATES bit for bit;
 * d3d12_enums.h static-asserts that against d3d12.h. */
#ifndef PORT_RHI_D3D12_D3D12_PLAN_H
#define PORT_RHI_D3D12_D3D12_PLAN_H

#include "../rhi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------- resource states */
#define D3DP_STATE_COMMON 0x0u
#define D3DP_STATE_VERTEX_AND_CONSTANT_BUFFER 0x1u
#define D3DP_STATE_INDEX_BUFFER 0x2u
#define D3DP_STATE_RENDER_TARGET 0x4u
#define D3DP_STATE_DEPTH_WRITE 0x10u
#define D3DP_STATE_DEPTH_READ 0x20u
#define D3DP_STATE_NON_PIXEL_SHADER_RESOURCE 0x40u
#define D3DP_STATE_PIXEL_SHADER_RESOURCE 0x80u
#define D3DP_STATE_INDIRECT_ARGUMENT 0x200u
#define D3DP_STATE_COPY_DEST 0x400u
#define D3DP_STATE_COPY_SOURCE 0x800u
#define D3DP_STATE_GENERIC_READ 0xAC3u /* the read states of a buffer, D3D12's own constant */
#define D3DP_STATE_PRESENT 0x0u
#define D3DP_STATE_SHADER_RESOURCE                                                                 \
    (D3DP_STATE_NON_PIXEL_SHADER_RESOURCE | D3DP_STATE_PIXEL_SHADER_RESOURCE)

/* The D3D12 state of an RhiState.  UNDEFINED has none (the plan uses the
 * tracked state instead) and maps to COMMON.  Depth formats are sampled in
 * DEPTH_READ | shader resource, so SHADER_READ and DEPTH_READ are one state
 * for them, as in the Vulkan backend (one read-only layout). */
uint32_t d3dp_TextureState(RhiState s, bool depthFormat);

/* One texture barrier.  `tracked` is the backend's record of the texture's
 * current D3D12 state (updated in recording order).  before == UNDEFINED
 * takes the tracked state (D3D12 has no "discard the contents" state);
 * otherwise the caller's before is used, and *mismatch is set when it
 * differs from the tracked one.  Returns true when a transition must be
 * recorded (the states differ), with its two sides in *outBefore and
 * *outAfter; *tracked becomes the after state either way. */
bool d3dp_PlanTextureBarrier(RhiState before, RhiState after, bool depthFormat, uint32_t *tracked,
                             uint32_t *outBefore, uint32_t *outAfter, bool *mismatch);

/* Device-local buffers (RHI_MEM_DEVICE, default heap): buffers decay to
 * COMMON at the end of every ExecuteCommandLists, so a buffer's state is
 * known per command list: COMMON until this list transitions it.  A copy
 * into the buffer moves it to COPY_DEST, and after the copy to GENERIC_READ
 * (vertex, index, constant, shader resource and copy source at once: rhi.h's
 * "visible to every later read").  Upload and readback heap buffers never
 * transition (GENERIC_READ and COPY_DEST for their lifetime). */
typedef struct D3dpBufferTrack {
    uint64_t listSerial; /* the command list the state belongs to */
    uint32_t state;
} D3dpBufferTrack;

/* The state of `t` within command list `listSerial`. */
uint32_t d3dp_BufferState(const D3dpBufferTrack *t, uint64_t listSerial);
/* Before a copy into the buffer: returns true with the transition to record
 * when it is not in COPY_DEST yet. */
bool d3dp_BufferBeginCopyDst(D3dpBufferTrack *t, uint64_t listSerial, uint32_t *outBefore,
                             uint32_t *outAfter);
/* After the copy: always one transition, COPY_DEST to GENERIC_READ. */
void d3dp_BufferEndCopyDst(D3dpBufferTrack *t, uint64_t listSerial, uint32_t *outBefore,
                           uint32_t *outAfter);

/* Same-state copy ordering.  rhi.h orders two writes in one state (a copy
 * after a copy into one texture); D3D12 orders nothing without a barrier, so
 * a second copy into a texture in the same command list since its last
 * barrier is preceded by a COPY_DEST -> COMMON -> COPY_DEST round trip.
 * *texSerial is the texture's "copied into by list n since its last
 * barrier" mark (0 = none); barriers reset it (d3dp_CopyMarkBarrier).
 * Returns true when the round trip is needed; marks the texture. */
bool d3dp_CopyNeedsSync(uint64_t *texSerial, uint64_t listSerial);
void d3dp_CopyMarkBarrier(uint64_t *texSerial);

/* ---------------------------------------------------------- descriptor rings
 * One shader-visible heap per type (CBV/SRV/UAV and sampler).  The
 * CBV/SRV/UAV heap is split into RHI_FRAMES_IN_FLIGHT equal regions, one per
 * frame slot, each a linear allocator reset when the slot is recycled
 * (rhi_WaitFrame).  The sampler heap keeps `persistent` descriptors at its
 * start (one per RhiSampler, for single-sampler tables) and splits the rest
 * into per-frame regions for tables of several samplers. */
typedef struct D3dpRing {
    uint32_t base; /* first descriptor of the region in the heap */
    uint32_t size; /* descriptors in the region */
    uint32_t used;
} D3dpRing;

/* The region of frame slot `frame` of `frames` in a heap of `total`
 * descriptors whose first `persistent` are reserved.  Regions are equal; the
 * remainder after division is left unused. */
void d3dp_RingInit(D3dpRing *r, uint32_t total, uint32_t persistent, uint32_t frames,
                   uint32_t frame);
/* Allocates `count` contiguous descriptors; false when the region is full. */
bool d3dp_RingAlloc(D3dpRing *r, uint32_t count, uint32_t *outIndex);
void d3dp_RingReset(D3dpRing *r);

/* ---------------------------------------------------- layouts and root params
 * A bind group layout is up to two descriptor tables in register space
 * <group>: the resource table (b and t registers: CBVs, SRVs) and the
 * sampler table (s registers).  Each slot is one descriptor at a fixed
 * offset in its table, in the layout's slot order. */
#define D3DP_MAX_SLOTS 16
#define D3DP_TABLE_RESOURCE 0
#define D3DP_TABLE_SAMPLER 1
/* package PA: an RHI_BIND_UNIFORM_BUFFER_DYNAMIC slot is no table entry but
 * a root CBV of its own; its offset is its rank among the layout's dynamic
 * slots in ascending slot order (the order of the bind-time offsets) */
#define D3DP_ROOT_CBV 2

typedef enum D3dpRegClass { D3DP_REG_B = 0, D3DP_REG_T = 1, D3DP_REG_S = 2 } D3dpRegClass;

typedef struct D3dpLayout {
    uint32_t slotCount;
    RhiBindSlot slots[D3DP_MAX_SLOTS];
    uint8_t table[D3DP_MAX_SLOTS];  /* D3DP_TABLE_* */
    uint8_t offset[D3DP_MAX_SLOTS]; /* descriptor offset in that table */
    uint32_t resCount, smpCount;
    uint32_t resStages, smpStages;            /* union of the slots' (1 << RhiShaderStage) */
    uint32_t dynCount;                        /* root CBVs (package PA) */
    uint8_t dynSlot[RHI_MAX_DYNAMIC_OFFSETS]; /* the layout entry of root CBV k */
} D3dpLayout;

D3dpRegClass d3dp_RegClass(RhiBindType t);
/* False for an invalid layout: a bad type, more than D3DP_MAX_SLOTS, more
 * than RHI_MAX_DYNAMIC_OFFSETS dynamic uniforms, or two slots on one
 * register (t1 as a texture and a storage buffer). */
bool d3dp_LayoutBuild(const RhiBindSlot *slots, uint32_t count, D3dpLayout *out);
/* The layout entry for (slot, type), or -1. */
int d3dp_LayoutFind(const D3dpLayout *l, uint32_t slot, RhiBindType type);

/* Root parameters of a pipeline whose group g uses layouts[g] (NULL for a
 * group without a layout): in group order, the resource table, the sampler
 * table and the root CBVs (package PA) of each group that has them.
 * resParam[g] / smpParam[g] receive the parameter index or -1, dynParam[g]
 * the index of the group's first root CBV (the others follow it in
 * D3dpLayout.dynSlot order) or -1.  Returns the parameter count. */
uint32_t d3dp_RootParams(const D3dpLayout *const *layouts, uint32_t count,
                         int8_t resParam[RHI_MAX_BIND_SLOTS], int8_t smpParam[RHI_MAX_BIND_SLOTS],
                         int8_t dynParam[RHI_MAX_BIND_SLOTS]);

/* ------------------------------------------------------------- small maths */
static inline uint64_t d3dp_AlignUp(uint64_t v, uint64_t a)
{
    return a ? (v + a - 1u) / a * a : v;
}

/* A constant buffer view over [offset, offset + size) of a buffer of
 * bufSize bytes (size 0: to the end): D3D12 wants the size in multiples of
 * 256 and at most 65536.  Returns the view size, or 0 when the offset is
 * not 256-aligned or the rounded view would pass the end of the buffer. */
uint32_t d3dp_CbvSize(uint64_t offset, uint64_t size, uint64_t bufSize);

/* Integer render target clears: the RHI clear value (float, the integer
 * per channel) rounded to nearest, negatives to 0, as the Vulkan backend
 * does; D3D12 converts the float it is given to the integer format. */
float d3dp_IntClearValue(float v);

/* Subresource index of (mip, plane) in a texture of `mips` levels and one
 * array layer.  D32F_S8: plane 0 depth, plane 1 stencil. */
static inline uint32_t d3dp_Subresource(uint32_t mip, uint32_t plane, uint32_t mips)
{
    return mip + plane * mips;
}

/* ------------------------------------------------- DXBC input signatures
 * D3D12 matches vertex attributes by semantic, the RHI by location.  DXC
 * gives each vertex input its register in declaration order, which is the
 * location the SPIR-V build assigns (VK_LOC in common.hlsli follows the
 * same order), so location n is the input-signature element with the n-th
 * lowest register among those without a system value. */
typedef struct D3dpSigElem {
    char name[32];
    uint32_t semanticIndex;
    uint32_t systemValue; /* 0 for a plain input */
    uint32_t reg;
    uint32_t componentType; /* 1 uint, 2 sint, 3 float */
    uint8_t mask;
} D3dpSigElem;

/* True when blob is a DXBC container (magic, size, chunk table in bounds). */
bool d3dp_IsDxbc(const void *blob, size_t size);
/* Reads the input signature (ISG1, or ISGN) of a DXBC container.  Returns
 * the element count (up to max are stored), or -1 when there is none or it
 * is malformed. */
int d3dp_ReadInputSignature(const void *blob, size_t size, D3dpSigElem *out, int max);
/* The element for RHI location `loc`, or -1. */
int d3dp_LocationElement(const D3dpSigElem *elems, int count, uint32_t loc);

/* ------------------------------------------------------------ handle pools
 * As the Vulkan backend: id = (generation << 20) | (index + 1); a freed
 * slot bumps its generation so stale handles fail the lookup. */
#define D3DP_INDEX_BITS 20u
#define D3DP_INDEX_MASK ((1u << D3DP_INDEX_BITS) - 1u)
#define D3DP_GEN_SHIFT D3DP_INDEX_BITS
#define D3DP_GEN_MASK 0xFFFu

typedef struct D3dpPool {
    const char *name;
    uint32_t cap;
    uint32_t elemSize;
    uint8_t *data;
    uint16_t *gen;
    uint8_t *live;
    uint32_t *freeList;
    uint32_t freeCount;
    uint32_t next;
} D3dpPool;

bool d3dp_PoolInit(D3dpPool *p, const char *name, uint32_t cap, uint32_t elemSize);
void d3dp_PoolFree(D3dpPool *p);
uint32_t d3dp_PoolAlloc(D3dpPool *p, void **out);
void *d3dp_PoolGet(const D3dpPool *p, uint32_t id);
void d3dp_PoolRelease(D3dpPool *p, uint32_t id);
/* Element `index` when it is live (for teardown walks), else NULL. */
void *d3dp_PoolAt(const D3dpPool *p, uint32_t index);

/* A small index allocator with a free list (RTV/DSV heap slots). */
typedef struct D3dpSlots {
    uint32_t cap, next, freeCount;
    uint32_t *freeList;
} D3dpSlots;

bool d3dp_SlotsInit(D3dpSlots *s, uint32_t cap);
void d3dp_SlotsFree(D3dpSlots *s);
bool d3dp_SlotsAlloc(D3dpSlots *s, uint32_t *out);
void d3dp_SlotsRelease(D3dpSlots *s, uint32_t index);

#endif /* PORT_RHI_D3D12_D3D12_PLAN_H */
