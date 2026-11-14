/* rd_vu_audit.h: the VU record path's bounds audit, a debug check run only
 * under the tests' ICO_RD_VU_AUDIT=1 (rd_vu_audit.c; issue 179: the
 * Mali-G52's job faults on v0.11.6 with robust buffer access off).
 *
 * Lavapipe bounds-checks every shader buffer access and reads past a bound
 * index range as nothing, so a replay there cannot show an access that a
 * phone's driver without robust buffer access turns into a GPU fault.  The
 * audit checks, on the CPU, every place the record path's GPU work reads or
 * writes by an address the port computes:
 *   entries   each recorded mesh command's slots against the record's
 *             plane (pass V's base + its vertex slots, pass T's slots A, B
 *             and C + its kicks), and each index value its draws read
 *             (kick * 4 + corner): corner at most 2, the kick inside the
 *             command's recorded vertices (vu_main_vs and the keep stage
 *             read the record at rec.x + kick - 2 + corner and slot
 *             tri.x + kick), at most the device's largest index value
 *   lists     the index compaction's fourth plane as the keep stage wrote
 *             it (read back after the frame slot's fence): every triangle
 *             of every planned draw its source triangle raised by the
 *             draw's raise, or the filler (corner 1 three times), nothing
 *             else (an uninitialised or misplaced value is a vertex fetch
 *             anywhere)
 *   draws     every indexed draw that reads the record as its index
 *             buffer: the bound offset at the fourth plane, 4-aligned, and
 *             (first + count) * 4 + offset within the buffer and the plane;
 *             every indexed VU draw's values at most the device's largest
 * Each violation is one line ("rd: VU audit: violation: ..."), the first 32
 * of a run, then counted; the run's line ("rd: VU audit: ... N
 * violations") at rd_vu_compact_counts_now (the replay tool's --stats,
 * after a full wait).  It also says the index values the draws reached
 * (the compacted lists' raised values and the draws' own lists'), the
 * figures a driver that sizes a draw's vertex work by its index range
 * would see.
 *
 * The Arm estimate: on Mali "memory is allocated for all vertex indices
 * between the min and max index referenced by a draw call", in an
 * intermediate geometry region "fixed to 180 MB on current Mali GPUs";
 * "Exceeding the size of this region may result in a VK_ERROR_DEVICE_LOST"
 * (Khronos Vulkan-Samples docs/memory_limits.adoc,
 * https://github.com/KhronosGroup/Vulkan-Samples/blob/main/docs/memory_limits.adoc);
 * on Bifrost a draw whose indices differ by a large value fails, only the
 * difference counting (Arm community thread 52444,
 * https://community.arm.com/support-forums/f/graphics-gaming-and-vr-forum/52444/drawcall-will-fail-on-mali-gpu-when-there-is-large-index-value-diff-in-index-buffer).
 * So for each render pass the audit sums its indexed VU draws' ranges
 * (largest - smallest + 1, in vertices) and prints the sum times the bytes
 * one vertex's outputs take on this path (164: vu_main_vs's VuVSOut, the
 * position and 148 bytes of varyings) and times Arm's example 64, against
 * 180 MB, a line a pass and the run's worst pass; with the largest single
 * draw span, the draws whose smallest index is 0 and the largest span over
 * a draw's indices.  The non-indexed VU draws (the particles) and the 2D
 * draws are not in the sums.  Nothing changes the picture: the audit only
 * reads. */
#ifndef PORT_RENDER_RD_VU_AUDIT_H
#define PORT_RENDER_RD_VU_AUDIT_H

#include <stdbool.h>
#include <stdint.h>

/* ICO_RD_VU_AUDIT=1 (read once) */
bool rd_vu_audit_on(void);
/* the run's violations so far (the tests) */
uint32_t rd_vu_audit_violations(void);

#ifdef PORT_RENDER_RD_INTERNAL_H
/* One recorded mesh command of the pre-pass (rd_replay_vu.c VuPre) */
typedef struct RdVuAuditEntry {
    uint32_t index;   /* its place in the pre-pass's entries */
    uint32_t list;    /* the list it is in */
    const char *mesh; /* its mesh's name (may be NULL) */
    uint32_t first;   /* its recorded vertices [first, first + count) */
    uint32_t count;
    uint32_t base;                /* pass V's slot of vertex first */
    uint32_t vSlots;              /* pass V's slots (count, twice with the velocity's) */
    uint32_t slotA, slotB, slotC; /* pass T's slots of kick first + 2; slotC ~0u: none */
    uint64_t planeSlots;          /* the slots of a record plane (rows * 1024) */
    uint32_t maxValue;            /* the device's largest index value */
} RdVuAuditEntry;

/* the frame slot's earlier replay read back and judged (its fence passed:
   the slot came round), then the slot's tables emptied for this replay */
void rd__vu_audit_slot_begin(uint32_t slot);
/* entry e's index values values[0 .. n) as its draws read them */
void rd__vu_audit_entry(const RdVuAuditEntry *e, const uint32_t *values, uint32_t n);
/* a planned compacted draw of this replay in slot: its list's first value
   in the plane (at), its values (count), its raise, its source values */
void rd__vu_audit_call(uint32_t slot, uint32_t at, uint32_t count, uint32_t raise,
                       const uint32_t *src);
/* after pass K: the fourth plane's first values values (from byte head of
   rec) copied for the readback */
void rd__vu_audit_copy(RhiCommandList cl, uint32_t slot, RhiBuffer rec, uint64_t head,
                       uint32_t values);

enum { RD_VU_AUDIT_DRAW_PLANE = 0, RD_VU_AUDIT_DRAW_OWN, RD_VU_AUDIT_DRAW_RING };

/* an indexed VU draw: its kind (the fourth plane, its own list, a merged
   run's list in the ring), its first index and count, its values' least
   and greatest; for the plane the bound offset, the buffer's bytes and the
   plane's bytes */
typedef struct RdVuAuditDraw {
    int kind;
    uint32_t first, count;
    uint32_t lo, hi;
    uint64_t ibOff, ibBytes, planeBytes;
    uint32_t maxValue;
    uint32_t pass; /* the render pass it is drawn in (the replay's pass serial) */
    int list;      /* the list being walked */
} RdVuAuditDraw;

void rd__vu_audit_draw(const RdVuAuditDraw *d);
/* every slot's copy read back after a full wait, and the run's line */
void rd__vu_audit_flush(void);
#endif

#endif /* PORT_RENDER_RD_VU_AUDIT_H */
