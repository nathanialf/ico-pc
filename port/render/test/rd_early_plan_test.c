/* rd_early_plan_test.c: what the renderer makes at start-up instead of in
 * the first 3D pictures (port/render/rd_early_plan.c), pure, with the
 * memory numbers of the two v0.11.3 loss reports.
 *
 *   mali       issue 179's Mali-G52 MC2 (compute record: two frame slots, 96
 *              KB a row): 815 MB available after the set (report/ico-pc.log's
 *              first heartbeat), floor 641 MB: half of the 174 MB above it is
 *              87 MB, so the record at 512 rows (48 MB) and the arena's 32 MB
 *              chunk (80 MB)
 *   adreno710  issue 211's Adreno 710 (vertex stores): 968 MB available
 *              (report/ico-pc.log:294), floor 908 MB: 30 MB, so the record at
 *              256 rows (24 MB, the first 3D scene's size there), no chunk
 *   desktop    the free memory not measured: the record at its largest (1024
 *              rows) and the chunk; half of Direct3D's free video memory as the
 *              bound; the targets' rows (their three textures too)
 *   floor      under the floor, at it, or after the system's signal: nothing
 *   off        the pre-pass off: the chunk alone; the chunk made already: the
 *              record alone; a record already as large: the chunk alone */
#include "rd_early_plan.h"
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

#define MB (1024ull * 1024ull)
#define ROW (1024ull * 48ull) /* a row of one frame slot's buffer */

static RdEarlyIn base(int memory, long avail, long floor, uint64_t rowBytes)
{
    RdEarlyIn in;
    memset(&in, 0, sizeof(in));
    in.memory = memory;
    in.availMb = avail;
    in.floorMb = floor;
    in.haveRows = 16;
    in.minRows = 16;
    in.maxRows = 1024;
    in.rowBytes = rowBytes;
    in.recordWanted = true;
    in.arenaBytes = 32 * MB;
    return in;
}

static void testPhones(void)
{
    RdEarlyPlan p;
    RdEarlyIn in = base(RD_EARLY_MEM_OK, 815, 641, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 512);
    CHECK(p.arena);
    CHECK(p.budget == 87 * MB);
    CHECK(p.bytes == 80 * MB);
    CHECK(strstr(p.why, "half of the 174 MB above the free-memory floor (815 MB available, floor "
                        "641 MB)") != NULL);

    in = base(RD_EARLY_MEM_OK, 968, 908, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 256);
    CHECK(!p.arena);
    CHECK(p.bytes == 24 * MB);
    CHECK(p.budget == 30 * MB);

    /* the 710 at the set's start (report/ico-pc.log:210: 978 MB): 35 MB */
    in = base(RD_EARLY_MEM_OK, 978, 908, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 256 && !p.arena);
}

static void testDesktop(void)
{
    RdEarlyPlan p;
    RdEarlyIn in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 1024);
    CHECK(p.arena);
    CHECK(p.budget == UINT64_MAX);
    CHECK(p.bytes == 128 * MB);
    CHECK(strstr(p.why, "not measured") != NULL);

    /* Direct3D: 200 MB of free video memory, half of it */
    in.gpuAvail = 200 * MB;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 1024 && !p.arena && p.budget == 100 * MB);
    CHECK(strstr(p.why, "adapter's 200 MB") != NULL);

    /* the targets: three textures and two buffers, 144 KB a row: 512 rows
       (72 MB), and the chunk no more within the 100 MB */
    in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 3 * ROW);
    in.gpuAvail = 200 * MB;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 512 && !p.arena && p.bytes == 72 * MB);
}

static void testFloor(void)
{
    RdEarlyPlan p;
    RdEarlyIn in = base(RD_EARLY_MEM_FLOOR, 882, 908, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && !p.arena && p.bytes == 0);
    CHECK(strstr(p.why, "882 MB available, under the free-memory floor of 908 MB") != NULL);

    in = base(RD_EARLY_MEM_OK, 908, 908, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && !p.arena);
    CHECK(strstr(p.why, "not above") != NULL);

    in = base(RD_EARLY_MEM_SIGNAL, 0, 0, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && !p.arena);
    CHECK(strstr(p.why, "low-memory signal") != NULL);

    /* 1 MB above the floor: half a MB, not a row more than 16 nor the chunk */
    in = base(RD_EARLY_MEM_OK, 642, 641, 2 * ROW);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && !p.arena);
}

static void testParts(void)
{
    RdEarlyPlan p;
    RdEarlyIn in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 2 * ROW);
    in.recordWanted = false;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && p.arena && p.bytes == 32 * MB);

    in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 2 * ROW);
    in.arenaHave = true;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 1024 && !p.arena && p.bytes == 96 * MB);

    in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 2 * ROW);
    in.haveRows = 1024;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && p.arena);

    /* no row size (no device): nothing for the record */
    in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 0);
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0);
}

/* the record's size from need (rd_early_start_rows): the largest recorded
   scene's rows (83 + 165 = 248; 330 with the tick cameras' third slot, 331
   with the velocity's second record, 413 with both), to the power of two
   the record grows by, doubled; and the plan held to it */
static void testNeed(void)
{
    CHECK(rd_early_start_rows(false, false, 16, 1024, 1024) == 512);
    CHECK(rd_early_start_rows(true, false, 16, 1024, 1024) == 1024);
    CHECK(rd_early_start_rows(false, true, 16, 1024, 1024) == 1024);
    CHECK(rd_early_start_rows(true, true, 16, 1024, 1024) == 1024);
    CHECK(rd_early_start_rows(false, false, 16, 256, 1024) == 256);
    CHECK(rd_early_start_rows(false, false, 0, 1024, 1024) == 0);
    CHECK(rd_early_start_rows(false, false, 16, 1024, 0) == 0);

    /* issue 179's Mali at v0.11.5's start (evidence/179 ico-pc.log:233:
       1175 MB available, floor 641 MB): the bound alone 1024 rows (half of
       534 MB is 267 MB, 96 MB of record), the need 512 rows: 48 MB of
       record and the arena's chunk, 80 MB; 48 MB less than v0.11.5 made */
    RdEarlyPlan p;
    RdEarlyIn in = base(RD_EARLY_MEM_OK, 1175, 641, 2 * ROW);
    in.startRows = 512;
    rd_early_plan(&in, &p);
    CHECK(p.boundRows == 1024);
    CHECK(p.rows == 512);
    CHECK(p.arena);
    CHECK(p.bytes == 80 * MB);
    in.startRows = 0; /* no need given: v0.11.5's plan */
    rd_early_plan(&in, &p);
    CHECK(p.rows == 1024 && p.boundRows == 1024 && p.bytes == 128 * MB);

    /* the bound under the need: the bound's (the v0.11.3 report's 815 MB) */
    in = base(RD_EARLY_MEM_OK, 815, 641, 2 * ROW);
    in.startRows = 1024;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 512 && p.boundRows == 512);

    /* a record already at the need: nothing for it */
    in = base(RD_EARLY_MEM_UNKNOWN, 0, 0, 2 * ROW);
    in.haveRows = 512;
    in.startRows = 512;
    rd_early_plan(&in, &p);
    CHECK(p.rows == 0 && p.boundRows == 1024 && p.arena);

    /* the window's line */
    RdVuRecMem m;
    memset(&m, 0, sizeof(m));
    m.heldRows = 512;
    m.heldBytes = 48 * MB;
    m.earlyRows = 512;
    m.blockNeedRows = 214;
    m.runNeedRows = 248;
    m.ringBytes = 128 * MB;
    char t[400];
    const size_t len = rd_vu_record_memory_text(t, sizeof t, &m);
    CHECK(len == strlen(t));
    CHECK(strcmp(t, "window: VU record memory: 512 rows held (48 MB over the frame slots; 512 rows "
                    "made at start-up); a picture of the block needed at most 214 rows (248 in the "
                    "run); 0 growths in play in the run; the upload rings 128 MB") == 0);
    m.earlyRows = 0;
    (void)rd_vu_record_memory_text(t, sizeof t, &m);
    CHECK(strstr(t, "(48 MB over the frame slots; nothing made at start-up)") != NULL);
    char s8[8];
    CHECK(rd_vu_record_memory_text(s8, sizeof s8, &m) == 7 && strlen(s8) == 7);
    CHECK(rd_vu_record_memory_text(s8, 0, &m) == 0);
}

int main(void)
{
    testNeed();
    testPhones();
    testDesktop();
    testFloor();
    testParts();
    if (failures) {
        fprintf(stderr, "rd_early_plan_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_early_plan_test: ok\n");
    return 0;
}
