/*
 * port/input/input_record.c
 *
 * The pad recording (input_record.h).
 */
#include "input_record.h"
#include <stdio.h>
#include <string.h>
#include "host_fs.h"

/* flush the file after this many polls (vsyncs) with something written */
#define FLUSH_POLLS 60

static FILE *s_file;

static IcoPadFrame s_last; /* the value the script holds after the lines written */

static int s_havePending;

static unsigned int s_pendingTick;

static IcoPadFrame s_pending;

static unsigned int s_lines, s_conflicts, s_dirtyPolls, s_lastTick;

static int s_dirty;

static int s_width; /* the frame width last noted, 0 before the first */

static int same(const IcoPadFrame *a, const IcoPadFrame *b)
{
    return a->buttons == b->buttons && a->lx == b->lx && a->ly == b->ly && a->rx == b->rx &&
           a->ry == b->ry;
}

int ico_input_record_format(char *out, size_t size, unsigned int tick, const IcoPadFrame *f)
{
    return snprintf(out, size, "%u %04x %u %u %u %u\n", tick, f->buttons & 0xFFFFu, (unsigned)f->lx,
                    (unsigned)f->ly, (unsigned)f->rx, (unsigned)f->ry);
}

int ico_input_record_open(const char *path, const char *header)
{
    ico_input_record_close();
    s_file = ico_fopen(path, "wb");
    if (s_file == NULL) {
        fprintf(stderr, "input: cannot write the pad recording %s\n", path);
        return -1;
    }
    if (header != NULL) {
        fputs(header, s_file);
    }
    fflush(s_file);
    memset(&s_last, 0, sizeof(s_last));
    s_last.lx = s_last.ly = s_last.rx = s_last.ry = ICO_PAD_STICK_CENTRE;
    s_havePending = 0;
    s_lines = s_conflicts = s_dirtyPolls = s_lastTick = 0;
    s_dirty = 0;
    s_width = 0;
    return 0;
}

int ico_input_record_active(void)
{
    return s_file != NULL;
}

static void commit(void)
{
    char line[64];

    if (!s_havePending) {
        return;
    }
    s_havePending = 0;
    if (same(&s_pending, &s_last)) {
        return;
    }
    s_last = s_pending;
    ico_input_record_format(line, sizeof(line), s_pendingTick, &s_pending);
    fputs(line, s_file);
    s_lines++;
    s_dirty = 1;
}

void ico_input_record_sample(unsigned int tick, const IcoPadFrame *f)
{
    if (s_file == NULL) {
        return;
    }
    if (s_havePending && tick == s_pendingTick) {
        /* a second read in the same tick: the script holds one value a
           tick, so the last read's is kept and the case counted */
        if (!same(f, &s_pending)) {
            s_conflicts++;
            s_pending = *f;
        }
        return;
    }
    if (s_havePending && tick < s_pendingTick) {
        return; /* not after the tick already held: cannot be scripted */
    }
    commit();
    s_havePending = 1;
    s_pendingTick = tick;
    s_pending = *f;
    s_lastTick = tick;
}

void ico_input_record_poll(unsigned int current_tick)
{
    if (s_file == NULL) {
        return;
    }
    if (s_havePending && s_pendingTick < current_tick) {
        commit();
    }
    if (s_dirty && ++s_dirtyPolls >= FLUSH_POLLS) {
        fflush(s_file);
        s_dirty = 0;
        s_dirtyPolls = 0;
    }
}

void ico_input_record_flush(void)
{
    if (s_file != NULL) {
        fflush(s_file);
        s_dirty = 0;
        s_dirtyPolls = 0;
    }
}

void ico_input_record_close(void)
{
    if (s_file == NULL) {
        return;
    }
    commit();
    fprintf(s_file, "# end at tick %u: %u lines, %u ticks read twice with different values\n",
            s_lastTick, s_lines, s_conflicts);
    fclose(s_file);
    s_file = NULL;
}

void ico_input_record_state_loaded(int slot, unsigned int tick)
{
    if (s_file == NULL) {
        return;
    }
    commit();
    fprintf(s_file, "# state loaded slot %d (Main tick %u)\n", slot, tick);
    s_lastTick = tick;
    s_dirty = 1;
}

unsigned int ico_input_record_lines(void)
{
    return s_lines;
}

int ico_input_record_frame_lines(char *out, size_t size, int gs_width, float aspect)
{
    return snprintf(out, size, "# effective gs_width = %d\n# effective aspect = %.4f\n", gs_width,
                    (double)aspect);
}

int ico_input_record_frame_diff(const char *line, int gs_width, float aspect, char *msg,
                                size_t size)
{
    int w;
    char a[32], b[32];

    if (sscanf(line, "# effective gs_width = %d", &w) == 1) {
        if (w != gs_width) {
            snprintf(msg, size, "gs_width = %d; this run has %d", w, gs_width);
            return 1;
        }
    } else if (sscanf(line, "# effective aspect = %31s", a) == 1) {
        snprintf(b, sizeof(b), "%.4f", (double)aspect);
        if (strcmp(a, b) != 0) {
            snprintf(msg, size, "aspect = %s; this run has %s", a, b);
            return 1;
        }
    }
    return 0;
}

void ico_input_record_note_width(unsigned int tick, int gs_width)
{
    if (s_file == NULL) {
        return;
    }
    if (s_width != 0 && gs_width != s_width) {
        fprintf(s_file, "# tick %u gs_width %d\n", tick, gs_width);
        s_dirty = 1;
    }
    s_width = gs_width;
}
