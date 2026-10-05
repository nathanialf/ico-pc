/*
 * port/platform/trace_host.c
 *
 * The Main tick count and the --trace file (trace_host.h). Package 2A
 * replaces the trace; the tick hook stays.
 */
#include "trace_host.h"
#include <stdint.h>
#include <stdio.h>
#include "host_loop.h"
#include "pad_script.h"

/* The game's state the trace reads (each defined in the file named). */
extern int stage_no;               /* common/src/main.c:428 */
extern int systemStatus[];         /* common/src/main.c:37, int[12] */
extern int gameover_flag;          /* common/src/main.c:493 */
extern char gameSysMainSaveBuff[]; /* common/src/gamesys.c:79 */
int gflagChk(int bit_idx);         /* script/src/gflag.c:58 */

#define SAVE_BUFF_SIZE 25596 /* gameSysMainSaveBuff[25596], gamesys.c:79 */
#define GFLAG_COUNT 400      /* gflags[50], script/src/gflag.c:14 */
#define GFLAG_WORDS ((GFLAG_COUNT + 31) / 32)
#define FLUSH_EVERY 64 /* lines between flushes, so a killed run keeps most */

static unsigned int main_ticks;

static unsigned int traced_ticks;

static FILE *trace;

void ico_host_main_tick(void)
{
    main_ticks++;
    ico_pad_script_set_tick(main_ticks);
}

unsigned int ico_host_main_ticks(void)
{
    return main_ticks;
}

int ico_host_stage_no(void)
{
    return stage_no;
}

int ico_trace_open(const char *path)
{
    int i;

    trace = fopen(path, "w");
    if (trace == NULL) {
        fprintf(stderr, "trace: cannot create %s\n", path);
        return -1;
    }
    fprintf(trace, "# tick vsync stage sys0 sys1 gameover");
    for (i = 0; i < GFLAG_WORDS; i++) {
        fprintf(trace, " gf%d", i);
    }
    fprintf(trace, " save\n");
    traced_ticks = main_ticks;
    return 0;
}

static uint32_t fnv1a(const unsigned char *p, unsigned int n)
{
    uint32_t h = 0x811C9DC5u;
    unsigned int i;

    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

static void write_line(unsigned int tick)
{
    int w;

    fprintf(trace, "%u %u %d %d %d %d", tick, ico_host_vsync_count(), stage_no, systemStatus[0],
            systemStatus[1], gameover_flag);
    for (w = 0; w < GFLAG_WORDS; w++) {
        uint32_t word = 0;
        int b;

        for (b = 0; b < 32 && w * 32 + b < GFLAG_COUNT; b++) {
            if (gflagChk(w * 32 + b)) {
                word |= (uint32_t)1 << b;
            }
        }
        fprintf(trace, " %08x", (unsigned int)word);
    }
    fprintf(trace, " %08x\n",
            (unsigned int)fnv1a((const unsigned char *)gameSysMainSaveBuff, SAVE_BUFF_SIZE));
}

void ico_trace_poll(void)
{
    if (trace == NULL) {
        return;
    }
    /* Main ticks at most once per vsync, so this is one line or none; a
       backlog would be written with the state as it is now. */
    while (traced_ticks < main_ticks) {
        write_line(traced_ticks);
        traced_ticks++;
        if (traced_ticks % FLUSH_EVERY == 0) {
            fflush(trace);
        }
    }
}

void ico_trace_close(void)
{
    if (trace != NULL) {
        fclose(trace);
        trace = NULL;
    }
}
