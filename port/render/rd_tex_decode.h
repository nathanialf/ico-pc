/* rd_tex_decode.h: the texture decode pool behind rdtex_store (rd_tex.c).
 *
 * A game texture's decode (rdtex_decode, the TEXA, the TIM2's further
 * levels and their authored test) is a pure function of the image's bytes,
 * so it can run off the game thread: rdtex_store makes or finds the cache
 * entry and its rd texture at once, as before, and hands the decode to a
 * job.  A job is queued to the pool's worker threads with a copy of the
 * image's bytes (the game may change or free them before the job runs: a
 * CLUT scroll, the stack copy of a recoloured CLUT), or, with the pool off
 * or RDTEX_DECODE_QUEUE_MAX jobs already waiting, decoded at once on the
 * game thread from the caller's bytes.
 *
 * rdtex_settle (rd_tex.h) puts every job's pixels into its texture in the
 * order the stores came (rdtex__job_finish), after decoding the queued
 * jobs no worker has taken on the calling thread and waiting for the ones
 * a worker is running.  Everything that reads a game texture's CPU texels
 * settles first: the replay's ring estimate and uploads (rd_upload.c), the
 * frame dump (rd_dump.c), rd_shutdown and rdtex_reset.  A job settled is
 * exactly what the synchronous store did: the same rd_update_texture or
 * the same pixels in the new texture, the same levels, the same log line.
 *
 * The pool's size: [dev] texture_decode_threads (ICO_RD_TEXTURE_DECODE_THREADS):
 * 0 the game thread decodes every texture as before, 1 to
 * RDTEX_DECODE_THREADS_MAX that many workers, auto (or unset in the window
 * build) 2, or 1 on a machine with 2 hardware threads or fewer.  Unset
 * without a window (the tests, rd_replay_tool) it is 0.
 * rdtex_set_decode_threads overrides both (the tests).
 */
#ifndef PORT_RENDER_RD_TEX_DECODE_H
#define PORT_RENDER_RD_TEX_DECODE_H

#include <stdint.h>
#include "rd_tex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the images a job holds: the base and up to 6 further TIM2 levels
   (Texture.c texHostTexture decodes at most 7) */
#define RDTEX_JOB_LEVELS 7
/* jobs waiting to be settled before a store decodes on the game thread */
#define RDTEX_DECODE_QUEUE_MAX 256
#define RDTEX_DECODE_THREADS_MAX 4

enum { RDTEX_JOB_QUEUED = 1, RDTEX_JOB_RUNNING, RDTEX_JOB_DONE };

typedef struct RdTexJob {
    struct RdTexJob *next; /* the settle order: the stores' */
    uint32_t texId;        /* the rd texture the pixels go to */
    int texa;              /* RDTEX_TEXA_REPLAY or an RdTexA mode */
    uint8_t update;        /* 1: re-expanded into it (rd_update_texture); 0: it was made for them */
    uint8_t state;         /* RDTEX_JOB_*, under the pool's lock once queued */
    uint8_t authored;      /* the further levels are pictures of their own */
    uint32_t pw, ph;       /* the texture's (padded) size */
    uint32_t n;            /* images in im: 1, or rdtex_store_levels' n */
    RdTexImage im[RDTEX_JOB_LEVELS];
    uint8_t *src;    /* the copy of the images' bytes im points into; NULL: the caller's */
    uint8_t *px;     /* pw x ph RGBA8, the decoded base */
    uint8_t *chain;  /* levels 1.. padded as the base (rdtex_store_levels), or NULL */
    uint32_t levels; /* the base and the further levels decoded into chain */
    double worst;    /* the largest brightness ratio of the levels (levelsAuthored) */
    char name[32];   /* the debug name (Texture.c's are 23 characters at most) */
} RdTexJob;

/* rd_tex.c: the decode itself, on any thread (reads only the job), and
   the result put into the texture, on the game thread; frees the job */
void rdtex__job_run(RdTexJob *j);
void rdtex__job_finish(RdTexJob *j);

/* rd_tex_decode.c */
/* rd_init and rd__init_record_only: the pool's size for this context
   (windowed: the window build's default, the pool on) */
void rdtex__decode_init(int windowed);
/* rd_shutdown: every job settled, the workers stopped */
void rdtex__decode_shutdown(void);
/* the workers the pool may use (0: off) */
int rdtex__decode_threads(void);
/* jobs added and not settled yet */
uint32_t rdtex__decode_pending(void);
/* j joins the settle order: queued for the workers (its bytes copied into
   j->src) or, queued 0, already run (rdtex__job_run) */
void rdtex__decode_add(RdTexJob *j, int queued);

#ifdef __cplusplus
}
#endif

#endif
