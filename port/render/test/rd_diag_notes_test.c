/* rd_diag_notes_test.c: the display line's width note and the upload
 * ring's grow words (port/render/rd_diag_notes.c; issue 179), CPU only.
 *
 *   rd_diag_notes_test
 *
 *   t1  the width: the line at the start (written anyway), the width's
 *       first change alone written once more with its words (issue 179's
 *       512 then 688), a second change alone not, a change with another
 *       reason written without the words, the same width never
 *   t2  the ring: none before, made and never used, made and used (its
 *       age, its last use and need), a new ring's note, a short buffer */
#include <stdio.h>
#include <string.h>

#include "rd_diag_notes.h"

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void t1(void)
{
    RdWidthNote n = {0};
    char b[128];
    /* the start: the line's own reason */
    CHECK(rd_width_note(&n, 512u, true), "t1: the start");
    CHECK(rd_width_note_text(&n, true, b, sizeof b) == 0 && b[0] == '\0', "t1: no words");
    CHECK(!rd_width_note(&n, 512u, false), "t1: the same width, nothing else: no line");
    /* the game's frame set to 688 after it: once more, with the words */
    CHECK(rd_width_note(&n, 688u, false), "t1: the first change alone");
    CHECK(rd_width_note_text(&n, false, b, sizeof b) == strlen(b) &&
              strcmp(b, " (written again: the frame's width changed from 512)") == 0,
          "t1: \"%s\"", b);
    CHECK(!rd_width_note(&n, 688u, false), "t1: 688 again: no line");
    /* later changes alone: no line (once only) */
    CHECK(!rd_width_note(&n, 512u, false), "t1: a second change alone");
    CHECK(!rd_width_note(&n, 688u, false), "t1: a third");
    /* a change with another reason: written, no words */
    CHECK(rd_width_note(&n, 640u, true), "t1: an option changed");
    CHECK(rd_width_note_text(&n, true, b, sizeof b) == 0, "t1: no words with a reason");
    CHECK(n.shown == 640u, "t1: the width kept: %u", (unsigned)n.shown);
    /* the first change came with another reason: no line for later ones */
    RdWidthNote m = {0};
    CHECK(rd_width_note(&m, 512u, true) && rd_width_note(&m, 688u, true), "t1: both written");
    CHECK(!rd_width_note(&m, 512u, false), "t1: the change was named: no line");
    char s[8];
    n = (RdWidthNote){512u, 0u, false};
    (void)rd_width_note(&n, 688u, false);
    CHECK(rd_width_note_text(&n, false, s, sizeof s) == 7u && strlen(s) == 7u, "t1: cut");
}

static void t2(void)
{
    RdRingNote r = {0};
    char b[160];
    CHECK(rd_ring_note_text(&r, 0, 1000.0, b, sizeof b) == strlen(b) &&
              strcmp(b, "; no ring before") == 0,
          "t2: \"%s\"", b);
    rd_ring_note_made(&r, 1000.0);
    (void)rd_ring_note_text(&r, 32ull << 20, 3500.0, b, sizeof b);
    CHECK(strcmp(b, "; the 32 MB ring it replaces was made 2.5 s before, never used before") == 0,
          "t2: \"%s\"", b);
    rd_ring_note_use(&r, 41000.0, (31ull << 20) + 5u);
    rd_ring_note_use(&r, 42120.0, (31ull << 20) + 77u);
    (void)rd_ring_note_text(&r, 32ull << 20, 42200.0, b, sizeof b);
    CHECK(strcmp(b, "; the 32 MB ring it replaces was made 41.2 s before, last used 0.08 s before "
                    "for 31 MB") == 0,
          "t2: \"%s\"", b);
    /* a new ring on the slot: made now, its uses its own */
    rd_ring_note_made(&r, 42200.0);
    CHECK(r.made && !r.used && r.madeMs == 42200.0, "t2: a new ring's note");
    char s[10];
    CHECK(rd_ring_note_text(&r, 64ull << 20, 43000.0, s, sizeof s) == 9u && strlen(s) == 9u,
          "t2: cut");
}

int main(void)
{
    t1();
    t2();
    if (fails) {
        printf("rd_diag_notes_test: %d failures\n", fails);
        return 1;
    }
    printf("rd_diag_notes_test: ok\n");
    return 0;
}
