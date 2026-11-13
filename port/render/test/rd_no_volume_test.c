/* rd_no_volume_test.c: the scan of a shadow window with no volume
 * (rd_no_volume.c rd_nv_scan_*), the gate, the 10 s clause and the --stats
 * words, CPU only.
 *
 *   1. the corpus title frame rd-00400's list 3 as the scan sees it (a reset
 *      at 6, the resolve at 13, levels 24/29/34 into SHADOW0-2, composites
 *      42/47/52 from SHADOW2, 1, 0, the fog after): taken, the levels and
 *      composites the walk leaves undrawn, nothing else;
 *   2. a strip between the reset and the resolve: a window with a volume;
 *   3. a foreign reader of SHADOW1 after the resolve, a sprite of the chain
 *      the box scan named neither a level nor a composite, a clear of
 *      SHADOW2, a draw into the count target: not taken (the chain);
 *   4. a draw inside the window, a second window, a resolve on another
 *      target, a window never resolved, a level writing a depth: not taken
 *      (the form); a composite whose DATE takes the R8 snapshot (DATE); a
 *      command the scan does not know after the resolve (unknown), and one
 *      before the window (no matter);
 *   5. SHADOW0 cleared before the window (a pending clear would land after
 *      the zero): not taken;
 *   6. no window;
 *   7. the gate: ICO_RD_NO_VOLUME=0 and =1, the class (its default off in
 *      this release lives in rd_no_volume.c classOn, the words checked
 *      here), MSAA, the pass window, the exact blend, GS state as shader
 *      data, DATE;
 *   8. the 10 s clause and the --stats words from made-up records (the
 *      stats line also carries the break mode's, the empty composites' and
 *      one DATE set-up's clauses: rd_shadow_box.c, rd_date_one.c, their
 *      switches unset: the break mode off, the other two on); the break
 *      mode's 10 s words (a window taken with SCENE's pass ended at the
 *      resolve, its zero clears and the targets holding zero). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_no_volume.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the targets (RdTarget ids as the scan compares them) */
enum { SCENE = 1, COUNT = 20, S0 = 10, S1 = 11, S2 = 12, OTHER = 30 };

static RdNvCmd cmd(uint8_t kind, uint32_t ord, uint32_t color, uint32_t depth)
{
    RdNvCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    c.ord = ord;
    c.color = color;
    c.depth = depth;
    return c;
}

static void step(RdNvScan *s, RdNvCmd c)
{
    rd_nv_scan_step(s, &c);
}

static RdNvCmd level(uint32_t ord, uint32_t dst, uint32_t src)
{
    RdNvCmd c = cmd(RD_NV_LEVEL, ord, dst, 0);
    c.src = src;
    c.names[0] = src;
    c.formOk = true;
    return c;
}

static RdNvCmd composite(uint32_t ord, uint32_t src)
{
    RdNvCmd c = cmd(RD_NV_COMPOSITE, ord, SCENE, SCENE);
    c.src = src;
    c.names[0] = src;
    c.formOk = true;
    return c;
}

static RdNvCmd clearOf(uint32_t ord, uint32_t target)
{
    RdNvCmd c = cmd(RD_NV_DRAW, ord, 0, 0);
    c.names[0] = target;
    c.clears = true;
    return c;
}

/* list 0's draws and clear, then rd-00400's window and its chain up to the
 * composites (upTo: 0 the window alone, 1 with the levels, 2 with the
 * composites) */
static void title(RdNvScan *s, int upTo)
{
    rd_nv_scan_begin(s);
    step(s, clearOf(10, SCENE));
    step(s, cmd(RD_NV_DRAW, 20, SCENE, SCENE));
    step(s, cmd(RD_NV_NONE, 700, 0, 0));
    step(s, cmd(RD_NV_RESET, 706, COUNT, SCENE));
    step(s, cmd(RD_NV_NONE, 707, 0, 0)); /* the state between */
    step(s, cmd(RD_NV_RESOLVE, 713, COUNT, SCENE));
    if (upTo >= 1) {
        step(s, level(724, S0, COUNT));
        step(s, level(729, S1, S0));
        step(s, level(734, S2, S1));
    }
    if (upTo >= 2) {
        step(s, composite(742, S2));
        step(s, composite(747, S1));
        step(s, composite(752, S0));
    }
}

static void testTitle(void)
{
    RdNvScan s;
    title(&s, 2);
    RdNvCmd fog = cmd(RD_NV_DRAW, 778, SCENE, SCENE);
    fog.names[0] = SCENE;
    fog.names[1] = SCENE;
    step(&s, fog);
    step(&s, cmd(RD_NV_DRAW, 900, SCENE, SCENE));
    const int r = rd_nv_scan_end(&s);
    CHECK(r == RD_NV_RES_EMPTY, "rd-00400's window: %d, taken expected", r);
    CHECK(s.resetOrd == 706 && s.resolveOrd == 713, "the window %u..%u", s.resetOrd, s.resolveOrd);
    CHECK(s.nChain == 4 && s.chain[0] == COUNT && s.chain[3] == S2, "the chain: %u targets",
          s.nChain);
    static const uint32_t kOrd[] = {724, 729, 734, 742, 747, 752, 706, 713, 778, 900, 20};
    static const int kKind[] = {RD_NV_LEVEL,     RD_NV_LEVEL,     RD_NV_LEVEL, RD_NV_COMPOSITE,
                                RD_NV_COMPOSITE, RD_NV_COMPOSITE, RD_NV_NONE,  RD_NV_NONE,
                                RD_NV_NONE,      RD_NV_NONE,      RD_NV_NONE};
    for (unsigned i = 0; i < sizeof(kOrd) / sizeof(kOrd[0]); i++) {
        const int k = rd_nv_skip_kind(&s, kOrd[i]);
        CHECK(k == kKind[i], "ordinal %u: skip kind %d, %d expected", kOrd[i], k, kKind[i]);
    }
}

static void testVolume(void)
{
    RdNvScan s;
    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_STRIP, 15, COUNT, SCENE));
    step(&s, cmd(RD_NV_STRIP, 19, COUNT, SCENE));
    step(&s, cmd(RD_NV_RESOLVE, 21, COUNT, SCENE));
    step(&s, level(32, S0, COUNT));
    int r = rd_nv_scan_end(&s);
    CHECK(r == RD_NV_RES_VOLUME, "rd-00200's window (two strips): %d, a volume expected", r);
    CHECK(rd_nv_skip_kind(&s, 32) == RD_NV_NONE, "a level of a window with a volume skipped");
    /* a strip, then the window never resolved: still a volume */
    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_STRIP, 15, COUNT, SCENE));
    r = rd_nv_scan_end(&s);
    CHECK(r == RD_NV_RES_VOLUME, "a strip in a window not resolved: %d", r);
}

static void expectRefused(RdNvScan *s, int why, const char *what)
{
    const int r = rd_nv_scan_end(s);
    CHECK(r == RD_NV_RES_REFUSED && s->why == why, "%s: result %d why %d, refused %d expected",
          what, r, s->why, why);
    CHECK(rd_nv_skip_kind(s, 724) == RD_NV_NONE && rd_nv_skip_kind(s, 742) == RD_NV_NONE,
          "%s: a refused window skips nothing", what);
}

static void testChain(void)
{
    RdNvScan s;
    title(&s, 1);
    RdNvCmd reader = cmd(RD_NV_DRAW, 740, SCENE, SCENE);
    reader.names[0] = S1; /* a foreign reader of SHADOW1 */
    step(&s, reader);
    step(&s, composite(742, S2));
    expectRefused(&s, RD_NV_WHY_CHAIN, "a foreign reader of SHADOW1");

    title(&s, 2);
    RdNvCmd whole = cmd(RD_NV_DRAW, 760, SCENE, SCENE);
    whole.names[0] = S2; /* the box scan's ROLE_WHOLE: a draw like any */
    step(&s, whole);
    expectRefused(&s, RD_NV_WHY_CHAIN, "a sprite of the chain neither a level nor a composite");

    title(&s, 2);
    step(&s, clearOf(800, S2));
    expectRefused(&s, RD_NV_WHY_CHAIN, "a clear of SHADOW2 after the resolve");

    title(&s, 2);
    step(&s, cmd(RD_NV_DRAW, 800, COUNT, 0));
    expectRefused(&s, RD_NV_WHY_CHAIN, "a draw into the count target");

    title(&s, 1);
    RdNvCmd intoLevel = level(740, S1, S0); /* a level into a target of the chain again */
    step(&s, intoLevel);
    expectRefused(&s, RD_NV_WHY_CHAIN, "a level into SHADOW1 twice");
}

static void testForm(void)
{
    RdNvScan s;
    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_DRAW, 8, SCENE, SCENE));
    step(&s, cmd(RD_NV_RESOLVE, 13, COUNT, SCENE));
    expectRefused(&s, RD_NV_WHY_FORM, "a draw inside the window");

    title(&s, 2);
    step(&s, cmd(RD_NV_RESET, 800, COUNT, SCENE));
    expectRefused(&s, RD_NV_WHY_FORM, "a second window");

    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_RESOLVE, 13, OTHER, SCENE));
    expectRefused(&s, RD_NV_WHY_FORM, "a resolve on another target");

    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    expectRefused(&s, RD_NV_WHY_FORM, "a window never resolved");

    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_RESOLVE, 13, COUNT, SCENE));
    expectRefused(&s, RD_NV_WHY_FORM, "a resolve with no reset");

    title(&s, 0);
    RdNvCmd z = level(724, S0, COUNT);
    z.formOk = false; /* a Z write into a depth */
    step(&s, z);
    expectRefused(&s, RD_NV_WHY_FORM, "a level writing a depth");

    title(&s, 1);
    RdNvCmd snap = composite(742, S2);
    snap.formOk = false;
    step(&s, snap);
    expectRefused(&s, RD_NV_WHY_DATE, "a composite's DATE by the R8 snapshot");

    title(&s, 2);
    step(&s, cmd(RD_NV_UNKNOWN, 800, SCENE, SCENE));
    expectRefused(&s, RD_NV_WHY_UNKNOWN, "a command the scan does not know");

    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_UNKNOWN, 3, SCENE, SCENE)); /* before the window: no matter */
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_RESOLVE, 13, COUNT, SCENE));
    step(&s, level(24, S0, COUNT));
    const int r = rd_nv_scan_end(&s);
    CHECK(r == RD_NV_RES_EMPTY && rd_nv_skip_kind(&s, 24) == RD_NV_LEVEL,
          "an unknown command before the window: %d", r);
}

static void testClearedBefore(void)
{
    RdNvScan s;
    rd_nv_scan_begin(&s);
    step(&s, clearOf(2, S0));
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_RESOLVE, 13, COUNT, SCENE));
    step(&s, level(24, S0, COUNT));
    expectRefused(&s, RD_NV_WHY_CHAIN, "SHADOW0 cleared before the window");

    rd_nv_scan_begin(&s);
    step(&s, clearOf(2, COUNT));
    step(&s, cmd(RD_NV_RESET, 6, COUNT, SCENE));
    step(&s, cmd(RD_NV_RESOLVE, 13, COUNT, SCENE));
    expectRefused(&s, RD_NV_WHY_CHAIN, "the count target cleared before the window");
}

static void testNone(void)
{
    RdNvScan s;
    rd_nv_scan_begin(&s);
    step(&s, cmd(RD_NV_DRAW, 1, SCENE, SCENE));
    const int r = rd_nv_scan_end(&s);
    CHECK(r == RD_NV_RES_NONE, "no window: %d", r);
    CHECK(rd_nv_skip_kind(&s, 1) == RD_NV_NONE, "no window: a skip");
}

/* the gate is pure: classOn is its argument.  The device default lives in
 * rd_no_volume.c classOn (RD_NO_VOLUME_DEFAULT 0 in this release: every
 * device's classOn is false, so unset gives RD_NV_WHY_CLASS everywhere) */
static void testGate(void)
{
    CHECK(rd_nv_gate(-1, true, false, false, false, false, true) == RD_NV_WHY_COUNT,
          "the class: on");
    CHECK(rd_nv_gate(-1, false, false, false, false, false, true) == RD_NV_WHY_CLASS,
          "another class: off");
    CHECK(rd_nv_gate(1, false, false, false, false, false, true) == RD_NV_WHY_COUNT,
          "=1 on another class: on");
    CHECK(rd_nv_gate(0, true, false, false, false, false, true) == RD_NV_WHY_OFF, "=0: off");
    CHECK(rd_nv_gate(1, true, true, false, false, false, true) == RD_NV_WHY_SETTING, "MSAA");
    CHECK(rd_nv_gate(1, true, false, true, false, false, true) == RD_NV_WHY_SETTING,
          "the pass window");
    CHECK(rd_nv_gate(1, true, false, false, true, false, true) == RD_NV_WHY_SETTING,
          "the exact blend");
    CHECK(rd_nv_gate(1, true, false, false, false, true, true) == RD_NV_WHY_SETTING,
          "GS state as shader data");
    CHECK(rd_nv_gate(1, true, false, false, false, false, false) == RD_NV_WHY_DATE,
          "DATE by the snapshot");
    CHECK(strcmp(rd_nv_why_words(RD_NV_WHY_CLASS),
                 "the default in this release: the PowerVR phone flickered black with it on, "
                 "cause not found") == 0,
          "the class words: %s", rd_nv_why_words(RD_NV_WHY_CLASS));
    CHECK(strcmp(rd_nv_why_words(RD_NV_WHY_DATE),
                 "the composites' DATE by the R8 snapshot or the shader's read") == 0,
          "the DATE words: %s", rd_nv_why_words(RD_NV_WHY_DATE));
}

static void testLines(void)
{
    static RdPerfRecord rec; /* large: static */
    memset(&rec, 0, sizeof(rec));
    rec.noVolumeKept = 1;
    rec.noVolumeOpen = 1;
    rec.noVolumePassW = 2364;
    rec.noVolumePassH = 1080;
    rec.noVolumePassDepth = 1;
    rec.noVolumeClears = 4;
    rec.noVolumeLevelTexels = 256u * 256u + 128u * 128u + 64u * 64u;
    rec.noVolumeSkipped = 6;
    char out[512];
    size_t at = 0;
    out[0] = '\0';
    rd__no_volume_pop(&rec);
    rd__no_volume_text(out, sizeof out, &at);
    /* 2364 x 1080 x (4 + 5) x 2 bytes = 45956160 = 43.83 MiB */
    const char *want = "; no volume: 1.0 windows a replay without a volume kept SCENE's pass open "
                       "(1.0 with it open, up to 43.8 MB not stored and loaded again), 0.0 with "
                       "volumes, 0.0 not taken";
    CHECK(strcmp(out, want) == 0, "the 10 s clause:\n  %s\nexpected\n  %s", out, want);
    CHECK(at == strlen(want), "the clause's length %zu", at);

    rd__no_volume_stats(&rec, out, sizeof out);
    want = "no volume off: 1 kept (1 with the pass open over 2364x1080, depth yes; 4 zero "
           "clears, 86016 level texels; 6 draws not drawn), 0 with volumes, 0 not taken; break "
           "off: 0 taken (0 zero clears, 0 targets holding zero); empty composites on: 0 skipped "
           "(0 with DATE); DATE one set-up on: 0 shared (0 texels), 0 kept their own";
    CHECK(strcmp(out, want) == 0, "the stats words:\n  %s\nexpected\n  %s", out, want);

    memset(&rec, 0, sizeof(rec));
    rec.noVolumeWhy[RD_NV_WHY_CLASS] = 1;
    static RdPerfRecord vol;
    memset(&vol, 0, sizeof(vol));
    vol.noVolumeWith = 1;
    rd__no_volume_pop(&rec);
    rd__no_volume_pop(&vol);
    at = 0;
    out[0] = '\0';
    rd__no_volume_text(out, sizeof out, &at);
    want = "; no volume: 0.0 windows a replay without a volume kept SCENE's pass open (0.0 with "
           "it open, up to 0.0 MB not stored and loaded again), 0.5 with volumes, 0.5 not taken "
           "(class 0.5)";
    CHECK(strcmp(out, want) == 0, "the 10 s clause, refused:\n  %s\nexpected\n  %s", out, want);

    rd__no_volume_stats(&rec, out, sizeof out);
    want = "no volume off: 0 kept (0 with the pass open over 0x0, depth no; 0 zero clears, 0 "
           "level texels; 0 draws not drawn), 0 with volumes, 1 not taken (class 1); break off: "
           "0 taken (0 zero clears, 0 targets holding zero); empty composites on: 0 skipped (0 "
           "with DATE); DATE one set-up on: 0 shared (0 texels), 0 kept their own";
    CHECK(strcmp(out, want) == 0, "the stats words, refused:\n  %s\nexpected\n  %s", out, want);

    /* the break mode: a window taken with SCENE's pass ended at the resolve,
     * four zero clears recorded (the reduced count's and three levels'), the
     * next replay's four found holding zero */
    memset(&rec, 0, sizeof(rec));
    rec.noVolumeBreak = 1;
    rec.noVolumeClears = 4;
    rec.noVolumeSkipped = 6;
    rd__no_volume_pop(&rec);
    memset(&rec, 0, sizeof(rec));
    rec.noVolumeBreak = 1;
    rec.noVolumeHeld = 4;
    rec.noVolumeSkipped = 6;
    rd__no_volume_pop(&rec);
    at = 0;
    out[0] = '\0';
    rd__no_volume_text(out, sizeof out, &at);
    want = "; no volume: 0.0 windows a replay without a volume kept SCENE's pass open (0.0 with "
           "it open, up to 0.0 MB not stored and loaded again), 0.0 with volumes, 0.0 not "
           "taken, 1.0 with SCENE's pass ended at the resolve (2.0 zero clears, 2.0 targets "
           "holding zero already)";
    CHECK(strcmp(out, want) == 0, "the 10 s clause, the break mode:\n  %s\nexpected\n  %s", out,
          want);
    rd__no_volume_stats(&rec, out, sizeof out);
    want = "no volume off: 0 kept (0 with the pass open over 0x0, depth no; 0 zero clears, 0 "
           "level texels; 6 draws not drawn), 0 with volumes, 0 not taken; break off: 1 taken (0 "
           "zero clears, 4 targets holding zero); empty composites on: 0 skipped (0 with DATE); "
           "DATE one set-up on: 0 shared (0 texels), 0 kept their own";
    CHECK(strcmp(out, want) == 0, "the stats words, the break mode:\n  %s\nexpected\n  %s", out,
          want);

    /* nothing counted: no clause */
    memset(&rec, 0, sizeof(rec));
    rd__no_volume_pop(&rec);
    at = 0;
    out[0] = '\0';
    rd__no_volume_text(out, sizeof out, &at);
    CHECK(at == 0 && out[0] == '\0', "a clause with nothing counted: %s", out);
}

int main(void)
{
    printf("rd_no_volume_test\n");
    testTitle();
    testVolume();
    testChain();
    testForm();
    testClearedBefore();
    testNone();
    testGate();
    testLines();
    if (failures) {
        printf("rd_no_volume_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_no_volume_test: ok\n");
    return 0;
}
