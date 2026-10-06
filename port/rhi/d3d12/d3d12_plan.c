/* d3d12_plan.c: the D3D12 backend's header-free bookkeeping (d3d12_plan.h). */
#include "d3d12_plan.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------- resource states */
uint32_t d3dp_TextureState(RhiState s, bool depthFormat)
{
    switch (s) {
    case RHI_STATE_RENDER_TARGET:
        return D3DP_STATE_RENDER_TARGET;
    case RHI_STATE_DEPTH_WRITE:
        return D3DP_STATE_DEPTH_WRITE;
    case RHI_STATE_DEPTH_READ:
        return D3DP_STATE_DEPTH_READ | D3DP_STATE_SHADER_RESOURCE;
    case RHI_STATE_SHADER_READ:
        return depthFormat ? (D3DP_STATE_DEPTH_READ | D3DP_STATE_SHADER_RESOURCE)
                           : D3DP_STATE_SHADER_RESOURCE;
    case RHI_STATE_COPY_SRC:
        return D3DP_STATE_COPY_SOURCE;
    case RHI_STATE_COPY_DST:
        return D3DP_STATE_COPY_DEST;
    case RHI_STATE_PRESENT:
        return D3DP_STATE_PRESENT;
    case RHI_STATE_UNDEFINED:
    case RHI_STATE_COUNT:
    default:
        return D3DP_STATE_COMMON;
    }
}

bool d3dp_PlanTextureBarrier(RhiState before, RhiState after, bool depthFormat, uint32_t *tracked,
                             uint32_t *outBefore, uint32_t *outAfter, bool *mismatch)
{
    uint32_t b = *tracked;
    if (mismatch) {
        *mismatch = false;
    }
    if (before != RHI_STATE_UNDEFINED) {
        b = d3dp_TextureState(before, depthFormat);
        if (b != *tracked && mismatch) {
            *mismatch = true;
        }
    }
    uint32_t a = d3dp_TextureState(after, depthFormat);
    *tracked = a;
    *outBefore = b;
    *outAfter = a;
    return b != a;
}

/* ------------------------------------------------------------------ buffers */
uint32_t d3dp_BufferState(const D3dpBufferTrack *t, uint64_t listSerial)
{
    return t->listSerial == listSerial ? t->state : D3DP_STATE_COMMON;
}

bool d3dp_BufferBeginCopyDst(D3dpBufferTrack *t, uint64_t listSerial, uint32_t *outBefore,
                             uint32_t *outAfter)
{
    uint32_t cur = d3dp_BufferState(t, listSerial);
    t->listSerial = listSerial;
    t->state = D3DP_STATE_COPY_DEST;
    *outBefore = cur;
    *outAfter = D3DP_STATE_COPY_DEST;
    return cur != D3DP_STATE_COPY_DEST;
}

void d3dp_BufferEndCopyDst(D3dpBufferTrack *t, uint64_t listSerial, uint32_t *outBefore,
                           uint32_t *outAfter)
{
    t->listSerial = listSerial;
    t->state = D3DP_STATE_GENERIC_READ;
    *outBefore = D3DP_STATE_COPY_DEST;
    *outAfter = D3DP_STATE_GENERIC_READ;
}

bool d3dp_CopyNeedsSync(uint64_t *texSerial, uint64_t listSerial)
{
    bool sync = *texSerial == listSerial && listSerial != 0;
    *texSerial = listSerial;
    return sync;
}

void d3dp_CopyMarkBarrier(uint64_t *texSerial)
{
    *texSerial = 0;
}

/* ---------------------------------------------------------- descriptor rings */
void d3dp_RingInit(D3dpRing *r, uint32_t total, uint32_t persistent, uint32_t frames,
                   uint32_t frame)
{
    uint32_t avail = total > persistent ? total - persistent : 0u;
    uint32_t each = frames ? avail / frames : 0u;
    r->base = persistent + each * frame;
    r->size = frame < frames ? each : 0u;
    r->used = 0;
}

bool d3dp_RingAlloc(D3dpRing *r, uint32_t count, uint32_t *outIndex)
{
    if (count > r->size - r->used) {
        return false;
    }
    *outIndex = r->base + r->used;
    r->used += count;
    return true;
}

void d3dp_RingReset(D3dpRing *r)
{
    r->used = 0;
}

/* ---------------------------------------------------------------- layouts */
D3dpRegClass d3dp_RegClass(RhiBindType t)
{
    switch (t) {
    case RHI_BIND_UNIFORM_BUFFER:
    case RHI_BIND_UNIFORM_BUFFER_DYNAMIC:
        return D3DP_REG_B;
    case RHI_BIND_SAMPLER:
        return D3DP_REG_S;
    case RHI_BIND_STORAGE_BUFFER:
    case RHI_BIND_SAMPLED_TEXTURE:
    default:
        return D3DP_REG_T;
    }
}

bool d3dp_LayoutBuild(const RhiBindSlot *slots, uint32_t count, D3dpLayout *out)
{
    memset(out, 0, sizeof(*out));
    if (count > D3DP_MAX_SLOTS || (count && !slots)) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const RhiBindSlot *s = &slots[i];
        if ((uint32_t)s->type >= RHI_BIND_COUNT) {
            return false;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (slots[j].slot == s->slot &&
                d3dp_RegClass(slots[j].type) == d3dp_RegClass(s->type)) {
                return false; /* one register, two bindings */
            }
        }
        uint32_t stages =
            s->stages ? s->stages : ((1u << RHI_STAGE_VERTEX) | (1u << RHI_STAGE_FRAGMENT));
        out->slots[i] = *s;
        if (s->type == RHI_BIND_UNIFORM_BUFFER_DYNAMIC) {
            if (out->dynCount == RHI_MAX_DYNAMIC_OFFSETS) {
                return false;
            }
            out->table[i] = D3DP_ROOT_CBV;
            out->dynSlot[out->dynCount++] = (uint8_t)i;
        } else if (s->type == RHI_BIND_SAMPLER) {
            out->table[i] = D3DP_TABLE_SAMPLER;
            out->offset[i] = (uint8_t)out->smpCount++;
            out->smpStages |= stages;
        } else {
            out->table[i] = D3DP_TABLE_RESOURCE;
            out->offset[i] = (uint8_t)out->resCount++;
            out->resStages |= stages;
        }
    }
    out->slotCount = count;
    /* root CBVs in ascending slot order, the order of the bind-time offsets
     * (rhi_CmdSetBindGroupOffsets); offset[] is the rank */
    for (uint32_t a = 1; a < out->dynCount; a++) {
        for (uint32_t b = a;
             b > 0 && out->slots[out->dynSlot[b - 1]].slot > out->slots[out->dynSlot[b]].slot;
             b--) {
            const uint8_t t = out->dynSlot[b];
            out->dynSlot[b] = out->dynSlot[b - 1];
            out->dynSlot[b - 1] = t;
        }
    }
    for (uint32_t k = 0; k < out->dynCount; k++) {
        out->offset[out->dynSlot[k]] = (uint8_t)k;
    }
    return true;
}

int d3dp_LayoutFind(const D3dpLayout *l, uint32_t slot, RhiBindType type)
{
    for (uint32_t i = 0; i < l->slotCount; i++) {
        if (l->slots[i].slot == slot && l->slots[i].type == type) {
            return (int)i;
        }
    }
    return -1;
}

uint32_t d3dp_RootParams(const D3dpLayout *const *layouts, uint32_t count,
                         int8_t resParam[RHI_MAX_BIND_SLOTS], int8_t smpParam[RHI_MAX_BIND_SLOTS],
                         int8_t dynParam[RHI_MAX_BIND_SLOTS])
{
    uint32_t n = 0;
    for (uint32_t g = 0; g < RHI_MAX_BIND_SLOTS; g++) {
        resParam[g] = -1;
        smpParam[g] = -1;
        dynParam[g] = -1;
        if (g >= count || !layouts[g]) {
            continue;
        }
        if (layouts[g]->resCount) {
            resParam[g] = (int8_t)n++;
        }
        if (layouts[g]->smpCount) {
            smpParam[g] = (int8_t)n++;
        }
        if (layouts[g]->dynCount) {
            dynParam[g] = (int8_t)n;
            n += layouts[g]->dynCount;
        }
    }
    return n;
}

/* ------------------------------------------------------------- small maths */
uint32_t d3dp_CbvSize(uint64_t offset, uint64_t size, uint64_t bufSize)
{
    if ((offset & 255u) != 0 || offset >= bufSize) {
        return 0;
    }
    if (size == 0) {
        size = bufSize - offset;
        if (size > 65536u) {
            size = 65536u;
        }
        size &= ~(uint64_t)255u; /* "to the end": the whole 256-byte blocks only */
        return (uint32_t)size;
    }
    uint64_t v = d3dp_AlignUp(size, 256u);
    if (v > 65536u || offset + v > bufSize) {
        return 0;
    }
    return (uint32_t)v;
}

float d3dp_IntClearValue(float v)
{
    return v <= 0.0f ? 0.0f : floorf(v + 0.5f);
}

/* ------------------------------------------------- DXBC input signatures */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* DXBC container: "DXBC", 16-byte hash, u32 version, u32 total size, u32
 * chunk count, u32 chunk offsets; each chunk is a fourcc, a u32 size and
 * the data. */
bool d3dp_IsDxbc(const void *blob, size_t size)
{
    const uint8_t *b = blob;
    if (!b || size < 32 || memcmp(b, "DXBC", 4) != 0) {
        return false;
    }
    uint32_t total = rd32(b + 24), chunks = rd32(b + 28);
    if (total > size || chunks > 64 || 32u + 4u * chunks > total) {
        return false;
    }
    for (uint32_t i = 0; i < chunks; i++) {
        uint32_t off = rd32(b + 32 + 4 * i);
        if (off > total || total - off < 8 || rd32(b + off + 4) > total - off - 8) {
            return false;
        }
    }
    return true;
}

int d3dp_ReadInputSignature(const void *blob, size_t size, D3dpSigElem *out, int max)
{
    if (!d3dp_IsDxbc(blob, size)) {
        return -1;
    }
    const uint8_t *b = blob;
    uint32_t chunks = rd32(b + 28);
    for (uint32_t i = 0; i < chunks; i++) {
        uint32_t off = rd32(b + 32 + 4 * i);
        bool isg1 = memcmp(b + off, "ISG1", 4) == 0;
        bool isgn = memcmp(b + off, "ISGN", 4) == 0;
        if (!isg1 && !isgn) {
            continue;
        }
        const uint8_t *c = b + off + 8;
        uint32_t csize = rd32(b + off + 4);
        if (csize < 8) {
            return -1;
        }
        uint32_t n = rd32(c);
        /* ISG1: stream, name, index, system value, component type,
         * register, mask, rw mask, 2 pad, min precision (32 bytes);
         * ISGN: the same without stream and min precision (24 bytes) */
        uint32_t stride = isg1 ? 32u : 24u;
        if (n > 64 || 8u + n * stride > csize) {
            return -1;
        }
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *e = c + 8 + k * stride + (isg1 ? 4u : 0u);
            uint32_t nameOff = rd32(e);
            if (nameOff >= csize) {
                return -1;
            }
            if ((int)k < max) {
                D3dpSigElem *o = &out[k];
                memset(o, 0, sizeof(*o));
                size_t j = 0;
                while (nameOff + j < csize && c[nameOff + j] && j + 1 < sizeof(o->name)) {
                    o->name[j] = (char)c[nameOff + j];
                    j++;
                }
                o->semanticIndex = rd32(e + 4);
                o->systemValue = rd32(e + 8);
                o->componentType = rd32(e + 12);
                o->reg = rd32(e + 16);
                o->mask = e[20];
            }
        }
        return (int)n;
    }
    return -1;
}

int d3dp_LocationElement(const D3dpSigElem *elems, int count, uint32_t loc)
{
    /* the element whose register has exactly `loc` plain inputs below it */
    for (int i = 0; i < count; i++) {
        if (elems[i].systemValue != 0) {
            continue;
        }
        uint32_t below = 0;
        for (int j = 0; j < count; j++) {
            if (elems[j].systemValue == 0 && elems[j].reg < elems[i].reg) {
                below++;
            }
        }
        if (below == loc) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------ handle pools */
bool d3dp_PoolInit(D3dpPool *p, const char *name, uint32_t cap, uint32_t elemSize)
{
    memset(p, 0, sizeof(*p));
    p->name = name;
    p->cap = cap;
    p->elemSize = elemSize;
    p->data = calloc(cap, elemSize);
    p->gen = calloc(cap, sizeof(uint16_t));
    p->live = calloc(cap, 1);
    p->freeList = calloc(cap, sizeof(uint32_t));
    return p->data && p->gen && p->live && p->freeList;
}

void d3dp_PoolFree(D3dpPool *p)
{
    free(p->data);
    free(p->gen);
    free(p->live);
    free(p->freeList);
    memset(p, 0, sizeof(*p));
}

uint32_t d3dp_PoolAlloc(D3dpPool *p, void **out)
{
    uint32_t idx;
    if (p->freeCount > 0) {
        idx = p->freeList[--p->freeCount];
    } else if (p->next < p->cap) {
        idx = p->next++;
    } else {
        return 0;
    }
    p->live[idx] = 1;
    void *e = p->data + (size_t)idx * p->elemSize;
    memset(e, 0, p->elemSize);
    if (out) {
        *out = e;
    }
    return ((uint32_t)(p->gen[idx] & D3DP_GEN_MASK) << D3DP_GEN_SHIFT) | (idx + 1u);
}

void *d3dp_PoolGet(const D3dpPool *p, uint32_t id)
{
    uint32_t idx = id & D3DP_INDEX_MASK;
    if (!p->data || idx == 0 || idx > p->cap) {
        return NULL;
    }
    idx -= 1u;
    if (!p->live[idx] || (p->gen[idx] & D3DP_GEN_MASK) != (id >> D3DP_GEN_SHIFT)) {
        return NULL;
    }
    return p->data + (size_t)idx * p->elemSize;
}

void d3dp_PoolRelease(D3dpPool *p, uint32_t id)
{
    if (!d3dp_PoolGet(p, id)) {
        return;
    }
    uint32_t idx = (id & D3DP_INDEX_MASK) - 1u;
    p->live[idx] = 0;
    p->gen[idx] = (uint16_t)((p->gen[idx] + 1u) & D3DP_GEN_MASK);
    p->freeList[p->freeCount++] = idx;
}

void *d3dp_PoolAt(const D3dpPool *p, uint32_t index)
{
    if (!p->data || index >= p->next || !p->live[index]) {
        return NULL;
    }
    return p->data + (size_t)index * p->elemSize;
}

bool d3dp_SlotsInit(D3dpSlots *s, uint32_t cap)
{
    memset(s, 0, sizeof(*s));
    s->cap = cap;
    s->freeList = calloc(cap ? cap : 1, sizeof(uint32_t));
    return s->freeList != NULL;
}

void d3dp_SlotsFree(D3dpSlots *s)
{
    free(s->freeList);
    memset(s, 0, sizeof(*s));
}

bool d3dp_SlotsAlloc(D3dpSlots *s, uint32_t *out)
{
    if (s->freeCount) {
        *out = s->freeList[--s->freeCount];
        return true;
    }
    if (s->next < s->cap) {
        *out = s->next++;
        return true;
    }
    return false;
}

void d3dp_SlotsRelease(D3dpSlots *s, uint32_t index)
{
    if (index < s->next && s->freeCount < s->cap) {
        s->freeList[s->freeCount++] = index;
    }
}
