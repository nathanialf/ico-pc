/* rd_cpu_phase.h: the render thread's CPU phases a picture (rd_cpu_phase.c;
 * docs/port/RENDER_PERF.md "The CPU phases").  The interpolation, the
 * upload and the walk are timed whole by the record (RdPerfRecord.interpMs,
 * uploadMs, walkMs); these split them by phase, a clock read at each
 * phase's end (never a draw's), and count the per-draw look-ups. */
#ifndef ICO_RD_CPU_PHASE_H
#define ICO_RD_CPU_PHASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the phases, in the order a picture runs them */
enum {
    /* rd_interp.c rd__interp_frame */
    RD_CPU_I_COPY = 0,  /* copyFrame, the snap test, the keyed draws' count */
    RD_CPU_I_INDEX,     /* buildIndex (prev) and indexCur: the pairing's tables */
    RD_CPU_I_BLEND,     /* the cameras, then every draw: matchOf and the blends */
    RD_CPU_I_MORPH,     /* the other mesh draws' streams (morphDraw) */
    RD_CPU_I_UNMATCHED, /* unmatchedPass */
    RD_CPU_I_HASH,      /* the still picture's hash (outHash) */
    RD_CPU_I_REST,      /* feedback, the held glow, the velocity's attach */
    /* rd_replay.c replayFrame, the upload */
    RD_CPU_U_CULL,    /* rd__vis_frame */
    RD_CPU_U_RING,    /* the overlay's collection, the ring's size */
    RD_CPU_U_BEGIN,   /* the acquire, the command list, the late uploads' reserve */
    RD_CPU_U_JOIN,    /* rd__join_parts */
    RD_CPU_U_MESHES,  /* rd__upload_meshes */
    RD_CPU_U_LAYER,   /* rd_store_plan.c: rd__layer_scan */
    RD_CPU_U_FADES,   /* rd__fades_fold_scan */
    RD_CPU_U_SCANS,   /* the store plan's one walk and its consumers */
    RD_CPU_U_PRESENT, /* rd__present_scene_begin */
    RD_CPU_U_CLEARS,  /* rd__clear_new_targets */
    /* the walk */
    RD_CPU_W_PREPASS, /* rd__vu_prepass (its recording) */
    RD_CPU_W_LATE,    /* rd__upload_late (the textures) */
    RD_CPU_W_LISTS,   /* the lists, to rd__store_plan_end */
    RD_CPU_PHASES
};

/* the per-draw look-ups, counted on the render thread (folded a picture) */
typedef struct RdCpuCounts {
    uint32_t pipeLookups, pipeHits;       /* rd_pipeline.c rd__get_pipeline */
    uint32_t uniformLookups, uniformHits; /* rd_replay.c uniformOffset, blocks of 256 B or less */
} RdCpuCounts;

extern RdCpuCounts g_rdCpu;

/* a phase group begins: the clock read */
void rd_cpu_start(void);
/* the time since the last start or mark charged to phase */
void rd_cpu_mark(int phase);
/* RD_CPU_I_HASH's mark, and whether the still picture's hash was taken */
void rd_cpu_mark_hash(bool hashed);
/* the walk's end: RD_CPU_W_LISTS's mark, and the picture folded into the
 * 10 s sums (frame: the frame replayed, the ticks' count) */
void rd_cpu_picture(uint32_t frame);

/* the 10 s sums, taken (and emptied) by rd_cpu_phase_line */
typedef struct RdCpuWindow {
    uint64_t ns[RD_CPU_PHASES];
    uint64_t pictures, ticks, hashed, reads;
    uint64_t pipeLookups, pipeHits, uniformLookups, uniformHits;
} RdCpuWindow;

void rd_cpu_phase_take(RdCpuWindow *w);
/* "CPU phases ms a picture ..." (no "window: "), false with no picture */
bool rd_cpu_phase_format(const RdCpuWindow *w, char *out, size_t size);
/* rd.h rd_cpu_phase_line: the window's 10 s line */

#endif
