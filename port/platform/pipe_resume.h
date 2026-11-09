/*
 * port/platform/pipe_resume.h
 *
 * The start-up set resumed after a low-memory stop (issue 123).  A start
 * whose set stopped when the system ran low on memory (the system's signal
 * or the free-memory floor, memory_floor.h) writes where it stopped into
 * the activity file's stop line (diag_host.c: "set: stopped at the
 * system's low-memory signal (<why>)" followed by this part):
 *
 *   "; resume at pipeline 312 of 1196, batches of 8, stop 1, 2026-11-09 20:45"
 *
 * the set's place in its own order (0-based: the first pipeline not made),
 * the set's size, the batch the stopped start made its pipelines in, how
 * many starts in a row stopped, and the stop's local date.  The next start
 * resumes the set there instead of making no set: in batches of half the
 * stopped start's (at least ICO_PIPE_RESUME_MIN_BATCH), so a stop that
 * repeats halves the batch again.  Before v0.11.2 the line had no such
 * part and a stop turned the set off for every later start of the version.
 *
 * Pure C: the unit test is port/platform/test/pipe_resume_test.c.
 */
#ifndef ICO_PIPE_RESUME_H
#define ICO_PIPE_RESUME_H

#include <stddef.h>

/* the part's words after the stop line's bracket */
#define ICO_PIPE_RESUME_WORDS "; resume at pipeline "
/* the smallest batch a resumed set is made in */
#define ICO_PIPE_RESUME_MIN_BATCH 4u

typedef struct IcoPipeResume {
    unsigned at;    /* the first pipeline not made, in the set's order (0-based) */
    unsigned total; /* the set's size at the stop */
    unsigned batch; /* the batch the stopped start made the set in */
    unsigned stops; /* the starts in a row whose set stopped (1: the first) */
    char date[24];  /* "2026-11-09 20:45", the stop's local time ("" unknown) */
} IcoPipeResume;

/* "; resume at pipeline <at> of <total>, batches of <batch>, stop <stops>,
   <date>" ("an unknown date" for an empty one) into out; the length
   written (cut at cap - 1) */
size_t ico_pipe_resume_part(char *out, size_t cap, const IcoPipeResume *r);
/* The part in a stop line: 1 with *out filled when the line holds one whose
   numbers read (at below total, total and batch above 0, stops above 0),
   0 otherwise (*out zeroed: an older version's line, or none) */
int ico_pipe_resume_parse(const char *line, IcoPipeResume *out);
/* The next start's batch after a stop in batches of stopped: half of it,
   at least ICO_PIPE_RESUME_MIN_BATCH; 0 for 0 (not known: the renderer
   halves its own) */
unsigned ico_pipe_resume_batch(unsigned stopped);
/* Where the next start resumes in a set of total pipelines: r->at when it
   is inside the set, else 0 (the set shrank under it: from the start) */
unsigned ico_pipe_resume_at(const IcoPipeResume *r, unsigned total);
/* The stop count of a start's own stop: the previous stop's count + 1 when
   this start was a resume (resumed != 0), else 1 */
unsigned ico_pipe_resume_stops(int resumed, const IcoPipeResume *prev);

#endif /* ICO_PIPE_RESUME_H */
