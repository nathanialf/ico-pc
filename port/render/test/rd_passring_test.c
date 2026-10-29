/* rd_passring_test.c: the pass ring (port/render/rd_passring.c), pure.
 *
 *   order     entries come back oldest first with rising sequence numbers,
 *             the replay, frame, keep flag and list in force when pushed
 *   wrap      past RD_PASSRING_SIZE entries the ring holds the newest
 *             ones; the replay table still names the replay and frame of a
 *             sequence number the ring moved past, until it too moves on
 *   print     the report prints the last RD_PASSRING_PRINT_REPLAYS replays'
 *             entries (whole at 430 entries a replay), never more than
 *             the ring holds
 *   markers   the marker push returns is the entry's sequence number
 *   pipeline  only a render pass, exact blend or VU pre-pass takes the pipeline asked
 *             for; an upload batch takes its bytes
 *   format    a pass, a copy and the present as the loss report prints
 *             them; the VU vertex pre-pass with its vertices and record
 *             and the pipeline it asked for
 *   bisect    the loss bisect's line for a pass, a copy and a pass of
 *             unknown count, in the same words
 *   hold      the shadow reduce's order (rd_replay.c shadowReduce): its
 *             pipeline looked up under the hold leaves the seven draws'
 *             entry alone; the reduce's own entry, pushed next, takes it
 *             when asked again; reset lets the hold go */
#include "rd_passring.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char *postName(int post)
{
    return post == 3 ? "fog" : "other";
}

static void testOrder(void)
{
    rd__passring_reset();
    CHECK(rd_passring_count() == 0);
    CHECK(rd_passring_get(0) == NULL);
    rd__passring_frame(316, 121, 0);
    const void *m0 = rd__passring_push(RD_PASSK_UPLOAD, 0, 0, 0, 0, 0, 0, 0, -1);
    rd__passring_bytes(2610u * 1024u);
    rd__passring_list(0);
    const void *m1 = rd__passring_push(RD_PASSK_PASS, 1, 1922, 1080, 2, 1922, 1080, 0, 3);
    rd__passring_pipeline(77, 4, 9, 0);
    rd__passring_list(12);
    rd__passring_push(RD_PASSK_PASS, 5, 1922, 1080, 0, 0, 0, 1, -1);
    CHECK((uintptr_t)m0 == 1 && (uintptr_t)m1 == 2);
    CHECK(rd_passring_count() == 3);
    const RdPassRec *a = rd_passring_get(0), *b = rd_passring_get(1), *c = rd_passring_get(2);
    CHECK(a && b && c);
    if (!a || !b || !c) {
        return;
    }
    CHECK(a->seq == 1 && a->kind == RD_PASSK_UPLOAD && a->bytes == 2610u * 1024u);
    CHECK(a->list == 255 && a->replay == 316 && a->frame == 121 && !a->keep);
    CHECK(a->pipeline == 0); /* an upload batch takes no pipeline */
    CHECK(b->seq == 2 && b->list == 0 && b->color == 1 && b->cw == 1922 && b->ch == 1080);
    CHECK(b->depth == 2 && b->pipeline == 77 && b->vs == 4 && b->fs == 9 && b->post == 3);
    CHECK(c->seq == 3 && c->list == 12 && c->storage == 1 && c->depth == 0 && c->pipeline == 0);
    /* the pipeline goes to the newest entry only while it is a pass */
    rd__passring_push(RD_PASSK_COPY, 1, 10, 10, 2, 10, 10, 0, -1);
    rd__passring_pipeline(99, 1, 1, 0);
    CHECK(rd_passring_get(3)->pipeline == 0);
    rd__passring_bytes(5); /* not an upload batch: ignored */
    CHECK(rd_passring_get(3)->bytes == 0);
    CHECK(rd_passring_replay_count() == 1);
    const RdPassReplay *r = rd_passring_replay_get(0);
    CHECK(r && r->replay == 316 && r->frame == 121 && r->first == 1 && r->last == 4);
}

static void testWrap(void)
{
    rd__passring_reset();

    /* 12 replays of 200 passes each (2400 entries, past the ring), every
     * third a keep frame */
    enum { PER = 200, REPS = 12, TOTAL = PER * REPS };

    for (uint32_t rep = 1; rep <= REPS; rep++) {
        rd__passring_frame(rep, 100 + rep, rep % 3 == 0);
        for (int p = 0; p < PER; p++) {
            rd__passring_list(p % 13);
            rd__passring_push(RD_PASSK_PASS, (uint32_t)p + 1, 64, 64, 0, 0, 0, 0, -1);
        }
    }
    CHECK(rd_passring_count() == RD_PASSRING_SIZE);
    const RdPassRec *first = rd_passring_get(0);
    const RdPassRec *last = rd_passring_get(RD_PASSRING_SIZE - 1);
    CHECK(first && last && last->seq == TOTAL && first->seq == TOTAL - RD_PASSRING_SIZE + 1);
    CHECK(last && last->replay == 12 && last->frame == 112 && last->keep == 1 &&
          last->list == (PER - 1) % 13);
    for (uint32_t i = 1; i < rd_passring_count(); i++) {
        CHECK(rd_passring_get(i)->seq == rd_passring_get(i - 1)->seq + 1);
    }
    CHECK(rd_passring_get(RD_PASSRING_SIZE) == NULL);
    /* the replay table: the last RD_PASSRING_REPLAYS replays (5..12) */
    CHECK(rd_passring_replay_count() == RD_PASSRING_REPLAYS);
    const RdPassReplay *r = rd_passring_replay_of(5 * PER); /* replay 5's passes 801..1000 */
    CHECK(r && r->replay == 5 && r->frame == 105 && r->first == 4 * PER + 1 && r->last == 5 * PER);
    CHECK(rd_passring_replay_of(4 * PER) == NULL); /* replay 4: past the table */
    CHECK(rd_passring_replay_of(TOTAL + 1) == NULL);
}

/* the report prints the last RD_PASSRING_PRINT_REPLAYS replays' entries,
 * each one whole while the ring holds it (a replay of about 430 entries:
 * the in-flight replays fit), and only those */
static void testPrintStart(void)
{
    rd__passring_reset();
    CHECK(rd_passring_print_start() == 0);

    enum { PER = 430 };

    for (uint32_t rep = 1; rep <= 6; rep++) {
        rd__passring_frame(rep, rep, 0);
        for (int p = 0; p < PER; p++) {
            rd__passring_push(RD_PASSK_PASS, 1, 64, 64, 0, 0, 0, 0, -1);
        }
    }
    CHECK(RD_PASSRING_SIZE >= RD_PASSRING_PRINT_REPLAYS * PER);
    const uint32_t s = rd_passring_print_start();
    const RdPassRec *e = rd_passring_get(s);
    CHECK(e && e->replay == 6 - RD_PASSRING_PRINT_REPLAYS + 1 &&
          e->seq == (uint64_t)(6 - RD_PASSRING_PRINT_REPLAYS) * PER + 1);
    CHECK(rd_passring_count() - s == RD_PASSRING_PRINT_REPLAYS * PER);
    /* a replay too big for the ring: the printing starts at the oldest
     * entry held and stays within the ring */
    rd__passring_frame(7, 7, 0);
    for (int p = 0; p < RD_PASSRING_SIZE + 10; p++) {
        rd__passring_push(RD_PASSK_PASS, 1, 64, 64, 0, 0, 0, 0, -1);
    }
    CHECK(rd_passring_print_start() == 0);
    /* fewer replays than RD_PASSRING_PRINT_REPLAYS: all of them */
    rd__passring_reset();
    rd__passring_frame(1, 1, 0);
    rd__passring_push(RD_PASSK_UPLOAD, 0, 0, 0, 0, 0, 0, 0, -1);
    CHECK(rd_passring_print_start() == 0);
}

static void testFormat(void)
{
    rd__passring_reset();
    rd__passring_frame(318, 122, 1);
    rd__passring_list(11);
    rd__passring_push(RD_PASSK_PASS, 4, 1922, 1080, 0, 0, 0, 1, 3);
    rd__passring_pipeline(12, 2, 30, 1);
    rd__passring_push(RD_PASSK_COPY, 7, 688, 448, 8, 688, 448, 0, -1);
    rd__passring_push(RD_PASSK_PRESENT, 2, 1922, 1080, 0, 1920, 1080, 0, -1);
    char line[320];
    rd_passring_format(rd_passring_get(0), postName, line, sizeof(line));
    CHECK(strcmp(line, "#1 replay 318 frame 122 (keep) list 11: pass (storage), colour 4 "
                       "(1922x1080), effect fog, pipeline 12 (vs 2 fs 30 dst 1)") == 0);
    if (strstr(line, "#1 replay") == NULL) {
        fprintf(stderr, "got: %s\n", line);
    }
    rd_passring_format(rd_passring_get(1), NULL, line, sizeof(line));
    CHECK(strcmp(line, "#2 replay 318 frame 122 (keep) list 11: copy, target 7 (688x448) -> "
                       "target 8 (688x448)") == 0);
    rd_passring_format(rd_passring_get(2), NULL, line, sizeof(line));
    CHECK(strcmp(line, "#3 replay 318 frame 122 (keep) list 11: present, from target 2 "
                       "(1922x1080) to 1920x1080") == 0);
    /* a short buffer is cut, as snprintf cuts */
    char small[8];
    CHECK(rd_passring_format(rd_passring_get(0), NULL, small, sizeof(small)) > 7);
    CHECK(strlen(small) == 7);
}

static void testVuPre(void)
{
    /* before list 0: no list in force */
    rd__passring_reset();
    rd__passring_frame(9, 40, 0);
    rd__passring_push(RD_PASSK_VU_PRE, 0, 1024, 2, 0, 1200, 0, 0, -1);
    rd__passring_pipeline(3, 30, 26, 0);
    char line[320];
    rd_passring_format(rd_passring_get(0), NULL, line, sizeof(line));
    CHECK(strcmp(line, "#1 replay 9 frame 40 list -: VU pre-pass, 1200 vertices into the record "
                       "(1024x2), pipeline 3 (vs 30 fs 26 dst 0)") == 0);
    if (strstr(line, "VU pre-pass, 1200") == NULL) {
        fprintf(stderr, "got: %s\n", line);
    }
}

static void testBisect(void)
{
    rd__passring_reset();
    rd__passring_frame(5, 121, 0);
    rd__passring_list(3);
    rd__passring_push(RD_PASSK_PASS, 4, 640, 448, 5, 640, 448, 0, -1);
    rd__passring_pipeline(409, 7, 5, 0);
    rd__passring_push(RD_PASSK_COPY, 7, 688, 448, 8, 688, 448, 0, 3);
    char line[320];
    rd_passring_format_bisect(rd_passring_get(0), 12, 61, 14, postName, line, sizeof(line));
    CHECK(strcmp(line, "pass 12 of 61, list 3, pipeline 409 (vs 7 fs 5 dst 0), pass colour 4 "
                       "(640x448), depth 5 (640x448), 14 draws") == 0);
    if (strstr(line, "pass 12 of 61") == NULL) {
        fprintf(stderr, "got: %s\n", line);
    }
    rd_passring_format_bisect(rd_passring_get(1), 13, 0, 1, postName, line, sizeof(line));
    CHECK(strcmp(line, "pass 13, list 3, no pipeline, copy target 7 (688x448) -> target 8 "
                       "(688x448), effect fog, 1 draw") == 0);
    /* the loss report's own line is unchanged by the shared parts */
    rd_passring_format(rd_passring_get(0), NULL, line, sizeof(line));
    CHECK(strcmp(line, "#1 replay 5 frame 121 list 3: pass, colour 4 (640x448), depth 5 "
                       "(640x448), pipeline 409 (vs 7 fs 5 dst 0)") == 0);
}

static void testHold(void)
{
    rd__passring_reset();
    rd__passring_frame(7, 90, 0);
    rd__passring_list(3);
    /* the seven draws' pass on the count target (scaled), its last pipeline */
    rd__passring_push(RD_PASSK_PASS, 45482004u, 1728, 1344, 1, 1728, 1344, 0, -1);
    rd__passring_pipeline(371, 2, 21, 0);
    /* the reduce looks its pipeline up before its pass begins */
    rd__passring_pipeline_hold(1);
    rd__passring_pipeline(366, 2, 13, 0);
    rd__passring_pipeline_hold(0);
    CHECK(rd_passring_get(0)->pipeline == 371 && rd_passring_get(0)->fs == 21);
    /* its own entry at the GS size, then the pipeline asked again */
    rd__passring_push(RD_PASSK_PASS, 45482004u, 576, 448, 0, 0, 0, 0, -1);
    rd__passring_pipeline(366, 2, 13, 0);
    CHECK(rd_passring_count() == 2);
    CHECK(rd_passring_get(1)->pipeline == 366 && rd_passring_get(1)->fs == 13);
    CHECK(rd_passring_get(1)->cw == 576 && rd_passring_get(1)->ch == 448);
    CHECK(rd_passring_get(0)->pipeline == 371);
    /* a hold left on is let go by reset */
    rd__passring_pipeline_hold(1);
    rd__passring_reset();
    rd__passring_push(RD_PASSK_PASS, 1, 8, 8, 0, 0, 0, 0, -1);
    rd__passring_pipeline(5, 1, 1, 0);
    CHECK(rd_passring_get(0)->pipeline == 5);
}

int main(void)
{
    testOrder();
    testWrap();
    testPrintStart();
    testFormat();
    testVuPre();
    testBisect();
    testHold();
    printf("rd_passring_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
