/* rd_passring.h: the pass ring, what the GPU was last given to do.
 *
 * The replay (rd_replay.c) notes every render pass, copy, snapshot, exact
 * blend, upload batch, VU vertex and triangle pre-pass and present it records: the last RD_PASSRING_SIZE of
 * them, each with its sequence number, replay and frame numbers, keep flag,
 * list, targets and their sizes, storage flag, picture effect and the last
 * pipeline asked for while it was open; and per replay the sequence numbers
 * of its first and last entries (the last RD_PASSRING_REPLAYS replays).  A
 * few stores per pass, always on.  A replay records about 430 entries, so
 * the ring holds the last RD_PASSRING_PRINT_REPLAYS replays whole while
 * they stay under about 680 each: the replay that met the loss and the
 * RHI_FRAMES_IN_FLIGHT before it, the oldest of them the submission whose
 * wait failed.  The replay stops recording once the device is lost
 * (rd_replay.c replayFrame), so the ring ends at the last replay given to
 * the GPU.
 *
 * On a device loss the window prints it before the box
 * (port/platform/window_host.c device_lost_quit), after the backend's own
 * report (port/rhi/vk/vk_device.c vkr_lost_report): the sequence numbers are
 * the checkpoint markers the replay gives rhi_cmd_checkpoint, so a
 * checkpoint the GPU reached names its pass, or at least its replay and
 * frame once the ring has moved past it.  docs/port/RENDER.md, "The pass
 * ring".
 *
 * Pure: no device, no renderer state (rd_passring_test). */
#ifndef PORT_RENDER_RD_PASSRING_H
#define PORT_RENDER_RD_PASSRING_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define RD_PASSRING_SIZE 2048 /* 64 bytes an entry: 128 KiB */
#define RD_PASSRING_REPLAYS 8
#define RD_PASSRING_PRINT_REPLAYS 3 /* the replays whose entries the report prints */

typedef enum RdPassKind {
    RD_PASSK_PASS,    /* a render pass (beginPass) */
    RD_PASSK_COPY,    /* a target-to-target copy (doCopy) */
    RD_PASSK_SNAP,    /* a target into its snapshot (takeSnap) */
    RD_PASSK_EXACT,   /* the exact blend between two targets (doExact) */
    RD_PASSK_UPLOAD,  /* the replay's texture and mesh uploads */
    RD_PASSK_PRESENT, /* the present onto the window */
    /* the VU vertex pre-pass (rd_replay_vu.c rd__vu_prepass): cw x ch the record's
     * texels drawn, dw its vertices (no target id) */
    RD_PASSK_VU_PRE,
    /* the VU triangle pre-pass after it (the same): cw x ch the record's
     * texels drawn, dw the triangles' record slots */
    RD_PASSK_VU_TRI,
    RD_PASSK_COUNT
} RdPassKind;

typedef struct RdPassRec {
    uint64_t seq; /* 1, 2, ...: the checkpoint marker */
    uint32_t replay, frame;
    uint8_t kind;          /* RdPassKind */
    uint8_t keep;          /* the replay was a keep frame (lists 11-12 only) */
    uint8_t list;          /* the frame list being replayed (0..12), 255: none */
    uint8_t storage;       /* a storage pass (the exact blend's interlock path) */
    int8_t post;           /* the picture effect (RD_PERF_POST_*), -1: none */
    uint8_t vs, fs, dst;   /* the last pipeline's shaders and destination read */
    uint32_t color, depth; /* target ids (copies: source, destination), 0: none */
    uint32_t cw, ch, dw, dh;
    uint32_t pipeline; /* the last pipeline asked for while open, 0: none */
    uint64_t bytes;    /* uploads: the ring bytes written */
} RdPassRec;

typedef struct RdPassReplay {
    uint32_t replay, frame;
    uint8_t keep;
    uint64_t first, last; /* sequence numbers, 0: none yet */
} RdPassReplay;

/* the replay: a replay begins (its counter, frame number, keep flag); the
 * list being walked */
void rd__passring_frame(uint32_t replay, uint32_t frame, int keep);
void rd__passring_list(int list);
/* the list rd__passring_list last named in this replay, -1 none */
int rd__passring_list_now(void);
/* an entry; the marker returned is its sequence number as a pointer (what
 * rhi_cmd_checkpoint is given) */
const void *rd__passring_push(RdPassKind kind, uint32_t color, uint32_t cw, uint32_t ch,
                              uint32_t depth, uint32_t dw, uint32_t dh, int storage, int post);
/* the newest entry, while it is a render pass, exact blend or VU
 * pre-pass (either): the pipeline asked for (rd_pipeline.c rd__get_pipeline) */
void rd__passring_pipeline(uint32_t id, uint8_t vs, uint8_t fs, uint8_t dst);
/* on: rd__passring_pipeline notes nothing until turned off again (a pass
 * that looks its pipeline up before its own entry is pushed: rd_replay.c
 * shadowReduce, rd_shadow_one.c rd__shadow_one_draw; without it the
 * lookup lands on the entry before, the pass still open or just ended) */
void rd__passring_pipeline_hold(int on);
/* the newest entry, an upload batch: its bytes */
void rd__passring_bytes(uint64_t bytes);
/* empties the ring and the replay table (tests) */
void rd__passring_reset(void);

/* the entries held, oldest first (i < rd_passring_count()); the replays
 * held, oldest first */
uint32_t rd_passring_count(void);
const RdPassRec *rd_passring_get(uint32_t i);
uint32_t rd_passring_replay_count(void);
const RdPassReplay *rd_passring_replay_get(uint32_t i);
/* the replay holding sequence number seq, NULL when the table has moved
 * past it */
const RdPassReplay *rd_passring_replay_of(uint64_t seq);
/* one entry as a line (no newline), postName naming an effect (NULL:
 * numbers); the length written, as snprintf */
int rd_passring_format(const RdPassRec *e, const char *(*postName)(int), char *out, size_t n);
/* the loss bisect's line for the pass that lost the device (rd_dump.c
 * rd_loss_bisect): "pass <pass> of <of>, list <l>, pipeline <id> (vs <v>
 * fs <f> dst <d>), <kind> <targets>[, effect <e>], <draws> draws", the
 * parts in rd_passring_format's words (of 0: " of <of>" left out; no
 * pipeline: "no pipeline"); the length written, as snprintf */
int rd_passring_format_bisect(const RdPassRec *e, uint32_t pass, uint32_t of, uint32_t draws,
                              const char *(*postName)(int), char *out, size_t n);
/* the first entry the report prints (an index for rd_passring_get): the
 * first of the last RD_PASSRING_PRINT_REPLAYS replays, or the oldest entry
 * held when the ring has moved past it; rd_passring_count() when empty */
uint32_t rd_passring_print_start(void);
/* the replay table, then the entries from rd_passring_print_start, oldest
 * first, "rd: " lines (at most RD_PASSRING_SIZE of them) */
void rd_passring_print(FILE *f, const char *(*postName)(int));

#endif /* PORT_RENDER_RD_PASSRING_H */
