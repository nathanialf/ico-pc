/* rd_glass.h: the content's speed on the glass, one 10 s line (rd_glass.c;
 * docs/port/PLATFORM_DIAGNOSTICS.md "The on-glass line").
 *
 * The evenness line (rd_even.h) puts the content against the GPU's ends; the
 * display shows a picture later (the present queue, the compositor's latch
 * at a refresh, a picture replaced before it is shown in mailbox mode).
 * Where the display's own times come back (rhi.h rhi_present_glass:
 * VK_GOOGLE_display_timing, read only), each shown picture's content is put
 * against the time the display showed it:
 *
 *   speed = (content of the picture - content of the shown picture before)
 *           / (its display time - that picture's display time)
 *
 * 1.00 is real time.  Per rule: the drawn one (whichever it is, the present
 * clock's in paced modes) and, with pictures as made, the three computed at
 * every picture (rd_pace_rules.h: the hand-over, the GPU's predicted end,
 * the own timer), so one run says which would be evenest on the glass;
 * beside each, the same step against the hand-overs and against the GPU's
 * ends.  No clock is read: the record's stamps and the display's times. */
#ifndef ICO_RD_GLASS_H
#define ICO_RD_GLASS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* a collected picture (rd_perf.c rd__perf_collect_gpu, the thread that
   replays) */
typedef struct RdGlassPic {
    uint32_t id;     /* its present's display-times id (0: none) */
    uint32_t frame;  /* the newer frame's number */
    float drawn;     /* the alpha drawn (RdPerfRecord.alpha; < 0: not a present) */
    float alpha[3];  /* by rule (RdPerfRecord.ruleAlpha; < 0 not computed) */
    uint8_t rule;    /* the rule drawn (rd_pace_rules.h, RD_RULE_CLOCK: paced) */
    double tickMs;   /* the tick (0: not known: the content's ms from 33.3667) */
    double handMs;   /* the hand-over (0 none) */
    double gpuEndMs; /* the GPU's end (0 none) */
    bool gpuExact;
} RdGlassPic;

/* the measures: the drawn rule, then the three computed */
#define RD_GLASS_DRAWN 0
#define RD_GLASS_RULES 4
/* the display's gaps in refreshes: under half, 1, 2, 3, 4 or more */
#define RD_GLASS_CLASSES 5
/* a pair further apart than this (or a frame number back, or on by more
   than 8) is not a step (a pause, a load) */
#define RD_GLASS_GAP_MAX_MS 1000.0
#define RD_GLASS_FRAME_JUMP 8
/* ratios kept in ten-thousandths */
#define RD_GLASS_UNITS 10000.0

typedef struct RdGlassStat {
    uint64_t n, sum2, above125, above15, below075;
    int64_t sum;
} RdGlassStat;

typedef struct RdGlassWindow {
    uint64_t pictures; /* collected with an id */
    uint64_t shown;    /* with a display time */
    uint64_t unseen;   /* presented with an id, no time came back */
    uint64_t pairs;
    uint32_t gaps[RD_GLASS_CLASSES];
    uint64_t refreshNs; /* the display's refresh at the last pair (0 not known) */
    uint8_t drawnRule;  /* at the last pair */
    RdGlassStat glass[RD_GLASS_RULES][RD_GLASS_CLASSES + 1]; /* [.][CLASSES]: every gap */
    RdGlassStat hand[RD_GLASS_RULES], gpu[RD_GLASS_RULES];
} RdGlassWindow;

/* pure: one pair of consecutive shown pictures (a shown at aNs, b at bNs on
   the display's clock) into w; false when it is not a step */
bool rd_glass_pair(RdGlassWindow *w, const RdGlassPic *a, uint64_t aNs, const RdGlassPic *b,
                   uint64_t bNs, uint64_t refreshNs);
/* "on the glass in 10 s: ..." (no "window: "); false with no picture */
bool rd_glass_format(const RdGlassWindow *w, char *out, size_t size);

/* the renderer's (rd_glass_live.c, the thread that collects the records):
   a collected picture, after the display's times read back since
   (rhi_present_glass) */
void rd_glass_picture(const RdGlassPic *p);

/* the join (rd_glass.c, one thread: the one that collects): a picture and
   a display time by id, in either order; a pair is made when both of a
   picture are in, in the presents' order */
void rd_glass_join(const RdGlassPic *p);
void rd_glass_time(uint32_t id, uint64_t actualNs, uint64_t refreshNs);
/* the 10 s sums taken (the window's thread) and the join emptied (tests) */
void rd_glass_take(RdGlassWindow *w);
void rd_glass_reset(void);
/* the window's 10 s line ("window: on the glass in 10 s: ..."; nothing
   where no picture carried an id), from rd_even_line */
void rd_glass_line(void);

#endif
