/* rd_ratecard_test.c: the rate card's pure part (rd_ratecard.h), CPU only:
 * the median and spread of synthetic timings, the unit arithmetic (a
 * difference over a count, the spread from the extremes, too few times,
 * nothing to divide by, not finite), the ratio, the printed block word for
 * word (a golden text with a measured item, a ratio and "not measured"),
 * the block read back (rd_rc_parse, a reprint's suffix allowed), and the
 * result file's decision (no file, another key, done, measuring, blocked, a
 * text it does not know). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_ratecard.h"
#include "../../test/ico_check.h"

static void stats(void)
{
    const double t[5] = {2.0, 1.0, 5.0, 3.0, 4.0};
    RdRcStat s = rd_rc_stat(t, 5);
    CHECK(s.n == 5 && s.med == 3.0 && s.min == 1.0 && s.max == 5.0, "the median of 5: %u %g %g %g",
          (unsigned)s.n, s.med, s.min, s.max);
    const double e[6] = {1.0, 2.0, 3.0, 4.0, 0.0, NAN};
    s = rd_rc_stat(e, 6);
    CHECK(s.n == 4 && s.med == 2.5, "0 and NaN left out, an even count: %u %g", (unsigned)s.n,
          s.med);
    s = rd_rc_stat(NULL, 3);
    CHECK(s.n == 0 && s.med == 0.0, "no times");
}

static void units(void)
{
    const double a[5] = {10.0, 10.2, 10.1, 9.9, 10.0}, b[5] = {2.0, 2.1, 1.9, 2.0, 2.0};
    const RdRcStat sa = rd_rc_stat(a, 5), sb = rd_rc_stat(b, 5);
    /* (10.0 - 2.0) ms over 4e6 fragments: 2 ns; spread (9.9 - 2.1) .. (10.2 - 1.9) */
    RdRcUnit u = rd_rc_unit(sa, sb, 4.0e6, 1.0e6);
    CHECK(u.ok && fabs(u.med - 2.0) < 1e-9 && fabs(u.lo - 1.95) < 1e-9 && fabs(u.hi - 2.075) < 1e-9,
          "a unit: %d %g %g %g", u.ok, u.med, u.lo, u.hi);
    /* a alone (b.n 0): its median over the count, in ms */
    const RdRcStat none = rd_rc_stat(NULL, 0);
    u = rd_rc_unit(sb, none, 1.0, 1.0);
    CHECK(u.ok && u.med == 2.0 && u.lo == 1.9 && u.hi == 2.1, "a time alone: %g %g %g", u.med, u.lo,
          u.hi);
    const RdRcStat few = rd_rc_stat(a, 4);
    u = rd_rc_unit(few, sb, 1.0, 1.0);
    CHECK(!u.ok && strcmp(u.why, "4 and 5 timed replays, 5 wanted") == 0, "too few: %s", u.why);
    u = rd_rc_unit(sa, sb, 0.0, 1.0);
    CHECK(!u.ok && strcmp(u.why, "nothing to divide by") == 0, "no count: %s", u.why);
    /* the ratio */
    RdRcUnit x = rd_rc_unit(sa, sb, 2.0e6, 1.0e6), y = rd_rc_unit(sa, sb, 4.0e6, 1.0e6);
    RdRcUnit r = rd_rc_ratio(x, y);
    CHECK(r.ok && fabs(r.med - 2.0) < 1e-9, "a ratio: %g", r.med);
    r = rd_rc_ratio(x, rd_rc_none("off"));
    CHECK(!r.ok && strcmp(r.why, "off") == 0, "a ratio of a unit not measured: %s", r.why);
}

static RdRateCard sample(void)
{
    RdRateCard c;
    memset(&c, 0, sizeof(c));
    c.have = true;
    snprintf(c.device, sizeof(c.device), "Mali-G52 (Mali 32.1, vulkan)");
    c.w = 688;
    c.h = 512;
    c.tw = 688;
    c.th = 512;
    snprintf(c.sized, sizeof(c.sized), "2 full layers");
    c.totalMs = 612.5;
    static const char *const kLabel[RD_RC_ITEMS] = {"a shaded fragment, vu_ps_nodual",
                                                    "a shaded fragment, vu_texa_ps_nodual",
                                                    "a shaded fragment, the twin vu_ps_nodual_nd",
                                                    "a shaded fragment, the fast pixel build",
                                                    "a fragment hidden by depth, with discard",
                                                    "a fragment hidden by depth, through the twin",
                                                    "a vertex (a corner drawn off the target)",
                                                    "a triangle binned",
                                                    "the 2x2-quad factor",
                                                    "a pass with nothing drawn",
                                                    "a MB loaded or stored",
                                                    "a blended fragment, the two-pass blend",
                                                    "the blended fragment over the opaque one"};
    static const char *const kUnit[RD_RC_ITEMS] = {"ns", "ns", "ns", "ns", "ns", "ns", "ns",
                                                   "ns", "x",  "ms", "ms", "ns", "x"};
    for (int i = 0; i < RD_RC_ITEMS; i++) {
        snprintf(c.item[i].label, sizeof(c.item[i].label), "%s", kLabel[i]);
        snprintf(c.item[i].unit, sizeof(c.item[i].unit), "%s", kUnit[i]);
        c.item[i].u.ok = true;
        c.item[i].u.med = 10.0 + i;
        c.item[i].u.lo = 9.5 + i;
        c.item[i].u.hi = 11.25 + i;
    }
    c.item[RD_RC_SHADE_FAST].u = rd_rc_none("no such build in this release");
    c.item[RD_RC_PASS_MB].u = rd_rc_none("one target size: not separable from the pass");
    return c;
}

static void text(void)
{
    const RdRateCard c = sample();
    char out[4096];
    const size_t n = rd_rc_text(&c, out, sizeof(out));
    static const char kWant[] =
        "rate card: Mali-G52 (Mali 32.1, vulkan) on SCENE 688x512 at 688x512 texels, 2 full "
        "layers; the median of 5 timed replays a frame (min, max beside it); 612.5 ms in all\n"
        "rate card: a shaded fragment, vu_ps_nodual: 10.00 ns (min 9.50, max 11.25)\n"
        "rate card: a shaded fragment, vu_texa_ps_nodual: 11.00 ns (min 10.50, max 12.25)\n"
        "rate card: a shaded fragment, the twin vu_ps_nodual_nd: 12.00 ns (min 11.50, max 13.25)\n"
        "rate card: a shaded fragment, the fast pixel build: not measured (no such build in this "
        "release)\n"
        "rate card: a fragment hidden by depth, with discard: 14.00 ns (min 13.50, max 15.25)\n"
        "rate card: a fragment hidden by depth, through the twin: 15.00 ns (min 14.50, max 16.25)\n"
        "rate card: a vertex (a corner drawn off the target): 16.00 ns (min 15.50, max 17.25)\n"
        "rate card: a triangle binned: 17.00 ns (min 16.50, max 18.25)\n"
        "rate card: the 2x2-quad factor: 18.00 x (min 17.50, max 19.25)\n"
        "rate card: a pass with nothing drawn: 19.000 ms (min 18.500, max 20.250)\n"
        "rate card: a MB loaded or stored: not measured (one target size: not separable from the "
        "pass)\n"
        "rate card: a blended fragment, the two-pass blend: 21.00 ns (min 20.50, max 22.25)\n"
        "rate card: the blended fragment over the opaque one: 22.00 x (min 21.50, max 23.25)\n";
    CHECK(n == strlen(out) && strcmp(out, kWant) == 0, "the block:\n%s", out);
    /* read back, also with a reprint's suffix on each line */
    RdRateCard b;
    CHECK(rd_rc_parse(out, &b) && b.have && b.w == 688 && b.th == 512 && b.totalMs == 612.5,
          "the block read back");
    CHECK(strcmp(b.device, c.device) == 0, "the device read back: %s", b.device);
    for (int i = 0; i < RD_RC_ITEMS; i++) {
        CHECK(b.item[i].u.ok == c.item[i].u.ok && strcmp(b.item[i].label, c.item[i].label) == 0,
              "item %d read back: %s", i, b.item[i].label);
        CHECK(!c.item[i].u.ok || (fabs(b.item[i].u.med - c.item[i].u.med) < 1e-6 &&
                                  fabs(b.item[i].u.hi - c.item[i].u.hi) < 1e-6 &&
                                  strcmp(b.item[i].unit, c.item[i].unit) == 0),
              "item %d's numbers read back", i);
    }
    CHECK(strcmp(b.item[RD_RC_PASS_MB].u.why, "one target size: not separable from the pass") == 0,
          "a reason read back: %s", b.item[RD_RC_PASS_MB].u.why);
    CHECK(!rd_rc_parse("rate card: nothing\n", &b), "a block of another form read back");
    char small[40];
    const size_t m = rd_rc_text(&c, small, sizeof(small));
    CHECK(m == sizeof(small) - 1u && strlen(small) == m, "a short buffer: %u", (unsigned)m);
}

static void file(void)
{
    char date[32], buf[4096];
    const char *lines;
    const char key[] = "Mali-G52 | Mali 32.1 | vulkan | 688x512 | v0.11.15";
    CHECK(rd_rc_decide(NULL, key, date, sizeof(date), &lines) == RD_RC_MEASURE, "no file");
    (void)rd_rc_file_text("measuring", "2026-11-23 10:15", key, NULL, buf, sizeof(buf));
    CHECK(strcmp(buf, "measuring 2026-11-23 10:15\nkey Mali-G52 | Mali 32.1 | vulkan | 688x512 | "
                      "v0.11.15\n") == 0,
          "the measuring text: %s", buf);
    CHECK(rd_rc_decide(buf, key, date, sizeof(date), &lines) == RD_RC_SKIP &&
              strcmp(date, "2026-11-23 10:15") == 0,
          "a run that ended while measuring: skipped, its date %s", date);
    CHECK(rd_rc_decide(buf, "another | key", date, sizeof(date), &lines) == RD_RC_MEASURE,
          "measuring under another key: measured");
    (void)rd_rc_file_text("blocked", "2026-11-23 10:15", key, NULL, buf, sizeof(buf));
    CHECK(rd_rc_decide(buf, key, date, sizeof(date), &lines) == RD_RC_SKIP, "blocked: skipped");
    (void)rd_rc_file_text("done", "2026-11-23 10:16", key, "rate card: x\nrate card: y\n", buf,
                          sizeof(buf));
    CHECK(rd_rc_decide(buf, key, date, sizeof(date), &lines) == RD_RC_REPRINT && lines != NULL &&
              strcmp(lines, "rate card: x\nrate card: y\n") == 0 &&
              strcmp(date, "2026-11-23 10:16") == 0,
          "done: reprinted");
    char other[64];
    snprintf(other, sizeof(other), "%s.1", key);
    CHECK(rd_rc_decide(buf, other, date, sizeof(date), &lines) == RD_RC_MEASURE,
          "done under another key (a longer one): measured");
    CHECK(rd_rc_decide("onward\nkey x\n", key, date, sizeof(date), &lines) == RD_RC_MEASURE,
          "a text it does not know: measured");
    CHECK(rd_rc_decide("done 2026\n", key, date, sizeof(date), &lines) == RD_RC_MEASURE,
          "no key line: measured");
}

int main(void)
{
    stats();
    units();
    text();
    file();
    return ico_check_summary("rd_ratecard_test");
}
