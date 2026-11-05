/*
 * port/platform/test/present_fallback_test.c
 *
 * The FIFO fallback's decision and words (present_fallback.h):
 *   log      the stock Adreno 740 log's blocks of 380, 382, 376 and 380 ms
 *            at 0, 9, 14 and 21 s under mailbox: the third switches, the
 *            fourth and later ones never again; the switch line word for
 *            word; the block line's suffix before and after the switch;
 *            the mode suffix "taken after 3 blocks";
 *   apart    two blocks 40 s apart, then a third 25 s after the second:
 *            the first has left the window, no switch;
 *   short    blocks of 52 ms and 21 of 143 ms: none is a block, no switch;
 *   turnip   the same tablet under Turnip ("turnip Mesa driver"): not
 *            eligible, so the window's gate is NO: the counts rise, no
 *            switch, no "toward FIFO" suffix;
 *   kept     [dev] present_fallback=off (GATE_KEPT): no switch, the kept
 *            answer once and its line word for word;
 *   other    the Mali-G52 reporter's blocks of 204, 231 and 240 ms at 408, 412 and
 *            416 s under GATE_OTHER: the other answer once, no switch, the line
 *            word for word, no suffix;
 *   late     a count reached under GATE_NO (vsync on) switches at the next
 *            block once the gate is YES;
 *   driver   eligibility (and Qualcomm's own: only vendor 0x5143): Qualcomm's text yes, Mesa's and Turnip's no (any
 *            case), no device no;
 *   start    FIFO from the first swapchain: Qualcomm's own driver (vendor
 *            0x5143, text neither Mesa nor turnip) with auto and vsync off
 *            yes; off, fifo (its own path), vsync on, Mesa/turnip texts
 *            and other vendors no; the start line word for word and the
 *            mode suffix;
 *   mode     the [dev] value: unset/auto auto, off/0/false/no off, fifo
 *            FIFO; the mode suffix's armed and dev-fifo words.
 */
#include "../present_fallback.h"
#include "../../test/ico_check.h"

#define S(x) ((unsigned long long)((x) * 1e9))

static const char k_qcom[] = "Qualcomm Technologies Inc. Adreno Vulkan Driver Driver Build: "
                             "bad34dcf83, I1064ae0a1f, 1764326394\nDate: 11/28/25";

static void testLog(void)
{
    IcoPresentFallback f = {0};
    char line[512];
    const int yes = ICO_PRESENT_FALLBACK_GATE_YES;

    CHECK(ico_present_fallback_note(&f, S(100), 380.0, yes) == 0);
    ico_present_fallback_block_suffix(&f, S(105), line, sizeof line);
    CHECK_STR(line, "; 1 of 3 over 200 ms in 30 s toward FIFO");
    CHECK(ico_present_fallback_note(&f, S(109), 382.0, yes) == 0);
    ico_present_fallback_block_suffix(&f, S(110), line, sizeof line);
    CHECK_STR(line, "; 2 of 3 over 200 ms in 30 s toward FIFO");
    /* a present over 50 ms that is no block changes nothing */
    CHECK(ico_present_fallback_note(&f, S(111), 143.0, yes) == 0);
    CHECK(ico_present_fallback_note(&f, S(114), 376.0, yes) == ICO_PRESENT_FALLBACK_SWITCH);
    ico_present_fallback_switch_line(&f, "Adreno (TM) 740", k_qcom, 6, line, sizeof line);
    CHECK_STR(line, "window: present blocked 3 times (376-382 ms) in 14 s on Adreno (TM) 740 "
                    "(Qualcomm Technologies Inc. Adreno Vulkan Driver Driver Build: bad34dcf83, "
                    "I1064ae0a1f, 1764326394) under mailbox (vsync off, 6 images): presents now "
                    "FIFO for this run ([dev] present_fallback=off keeps mailbox)");
    ico_present_fallback_mode_suffix(&f, ICO_PRESENT_FALLBACK_AUTO, 1, 0, line, sizeof line);
    CHECK_STR(line, ", FIFO fallback taken after 3 blocks");
    /* never a second time, whatever follows */
    CHECK(ico_present_fallback_note(&f, S(121), 380.0, yes) == 0);
    for (int i = 0; i < 20; i++) {
        CHECK(ico_present_fallback_note(&f, S(122 + i), 400.0, yes) == 0);
    }
    CHECK(f.taken == 1);
    ico_present_fallback_block_suffix(&f, S(144), line, sizeof line);
    CHECK_STR(line, "; 21 since presents went FIFO 30 s ago");
    /* presents under 50 ms after the switch are not counted */
    CHECK(ico_present_fallback_note(&f, S(145), 40.0, yes) == 0);
    CHECK(f.slowSince == 21);
}

static void testApart(void)
{
    IcoPresentFallback f = {0};
    const int yes = ICO_PRESENT_FALLBACK_GATE_YES;
    char line[128];

    CHECK(ico_present_fallback_note(&f, S(10), 380.0, yes) == 0);
    CHECK(ico_present_fallback_note(&f, S(50), 380.0, yes) == 0);
    CHECK(ico_present_fallback_note(&f, S(75), 380.0, yes) == 0);
    CHECK(f.n == 2 && f.blocks == 3 && !f.taken);
    /* 0/16/31: the first is 31 s before the third */
    IcoPresentFallback g = {0};
    CHECK(ico_present_fallback_note(&g, S(0), 300.0, yes) == 0);
    CHECK(ico_present_fallback_note(&g, S(16), 300.0, yes) == 0);
    CHECK(ico_present_fallback_note(&g, S(31), 300.0, yes) == 0);
    CHECK(!g.taken);
    /* exactly 30 s is within */
    IcoPresentFallback h = {0};
    CHECK(ico_present_fallback_note(&h, S(0), 200.0, yes) == 0);
    CHECK(ico_present_fallback_note(&h, S(15), 200.0, yes) == 0);
    CHECK(ico_present_fallback_note(&h, S(30), 200.0, yes) == ICO_PRESENT_FALLBACK_SWITCH);
    /* the suffix counts only the blocks still within the window */
    ico_present_fallback_block_suffix(&f, S(106), line, sizeof line);
    CHECK_STR(line, "");
}

static void testShort(void)
{
    IcoPresentFallback f = {0};
    const int yes = ICO_PRESENT_FALLBACK_GATE_YES;
    char line[128];

    CHECK(ico_present_fallback_note(&f, S(1), 52.0, yes) == 0);
    for (int i = 0; i < 21; i++) {
        CHECK(ico_present_fallback_note(&f, S(2) + (unsigned long long)i * 400000000ull, 143.0,
                                        yes) == 0);
    }
    CHECK(f.blocks == 0 && !f.taken);
    ico_present_fallback_block_suffix(&f, S(11), line, sizeof line);
    CHECK_STR(line, "");
}

static void testTurnip(void)
{
    IcoPresentFallback f = {0};
    const int gate = ico_present_fallback_driver_eligible(0x5143u, "turnip Mesa driver")
                         ? ICO_PRESENT_FALLBACK_GATE_YES
                         : ICO_PRESENT_FALLBACK_GATE_NO;
    char line[128];

    CHECK(gate == ICO_PRESENT_FALLBACK_GATE_NO);
    for (int i = 0; i < 6; i++) {
        CHECK(ico_present_fallback_note(&f, S(5) + (unsigned long long)i * 2000000000ull, 230.0,
                                        gate) == 0);
        CHECK(f.blocks == (unsigned)(i + 1));
    }
    CHECK(!f.taken && !f.kept);
    ico_present_fallback_block_suffix(&f, S(16), line, sizeof line);
    CHECK_STR(line, "");
    ico_present_fallback_mode_suffix(&f, ICO_PRESENT_FALLBACK_AUTO, 0, 0, line, sizeof line);
    CHECK_STR(line, "");
}

static void testKept(void)
{
    IcoPresentFallback f = {0};
    const int kept = ICO_PRESENT_FALLBACK_GATE_KEPT;
    char line[256];

    CHECK(ico_present_fallback_note(&f, S(0), 380.0, kept) == 0);
    CHECK(ico_present_fallback_note(&f, S(9), 382.0, kept) == 0);
    CHECK(ico_present_fallback_note(&f, S(21), 376.0, kept) == ICO_PRESENT_FALLBACK_KEPT);
    ico_present_fallback_kept_line(&f, line, sizeof line);
    CHECK_STR(line, "window: present blocked 3 times over 200 ms in 21 s under mailbox: mailbox "
                    "kept ([dev] present_fallback=off)");
    for (int i = 0; i < 10; i++) {
        CHECK(ico_present_fallback_note(&f, S(22 + i), 380.0, kept) == 0);
    }
    CHECK(!f.taken);
    ico_present_fallback_block_suffix(&f, S(33), line, sizeof line);
    CHECK_STR(line, "");
}

static void testLate(void)
{
    IcoPresentFallback f = {0};

    CHECK(ico_present_fallback_note(&f, S(0), 380.0, ICO_PRESENT_FALLBACK_GATE_NO) == 0);
    CHECK(ico_present_fallback_note(&f, S(5), 380.0, ICO_PRESENT_FALLBACK_GATE_NO) == 0);
    CHECK(ico_present_fallback_note(&f, S(6), 380.0, ICO_PRESENT_FALLBACK_GATE_NO) == 0);
    CHECK(ico_present_fallback_note(&f, S(8), 380.0, ICO_PRESENT_FALLBACK_GATE_YES) ==
          ICO_PRESENT_FALLBACK_SWITCH);
    CHECK(f.takenBlocks == 4);
}

static const char k_mali[] = "device 13b5:74021000; driver Mali-G52 MC2 "
                             "v1.r32p1-01eac0.2a893c04ca0026c2e6802dbe7d7af5c5, version 32.1.0";

/* the Mali-G52 reporter's three blocks (issue 179): the switch is Qualcomm's own */
static void testOther(void)
{
    IcoPresentFallback f = {0};
    const int other = ICO_PRESENT_FALLBACK_GATE_OTHER;
    char line[512];

    CHECK(ico_present_fallback_note(&f, S(408), 204.0, other) == 0);
    ico_present_fallback_block_suffix(&f, S(409), line, sizeof line);
    CHECK_STR(line, "");
    CHECK(ico_present_fallback_note(&f, S(412), 231.0, other) == 0);
    CHECK(ico_present_fallback_note(&f, S(416), 240.0, other) == ICO_PRESENT_FALLBACK_OTHER);
    CHECK(!f.taken && f.kept);
    ico_present_fallback_other_line(&f, "Mali-G52 MC2", k_mali, line, sizeof line);
    CHECK_STR(
        line,
        "window: present blocked 3 times (204-240 ms) in 8 s on Mali-G52 MC2 (device "
        "13b5:74021000; driver Mali-G52 MC2 v1.r32p1-01eac0.2a893c04ca0026c2e6802dbe7d7af5c5, "
        "version 32.1.0) under mailbox: mailbox kept (presents go FIFO by themselves only on "
        "Qualcomm's own driver, where the blocks were measured; [dev] present_fallback = fifo "
        "asks for FIFO here)");
    /* once a run, whatever follows */
    for (int i = 0; i < 10; i++) {
        CHECK(ico_present_fallback_note(&f, S(417 + i), 300.0, other) == 0);
    }
    CHECK(!f.taken && f.slowSince == 0);
    ico_present_fallback_block_suffix(&f, S(430), line, sizeof line);
    CHECK_STR(line, "");
    ico_present_fallback_mode_suffix(&f, ICO_PRESENT_FALLBACK_AUTO, 0, 0, line, sizeof line);
    CHECK_STR(line, "");
}

static void testDriver(void)
{
    CHECK(ico_present_fallback_driver_eligible(0x5143u, k_qcom));
    CHECK(ico_present_fallback_driver_eligible(0x13b5u, "ARM Mali driver"));
    CHECK(ico_present_fallback_driver_eligible(0x5143u, NULL));
    /* the Mali-G52 stays eligible (the Mesa/Turnip exclusion) but is not Qualcomm's own: the
       blocks' switch needs the vendor too */
    CHECK(ico_present_fallback_driver_eligible(0x13b5u, k_mali));
    CHECK(!ico_present_fallback_qualcomm_own(0x13b5u, k_mali));
    CHECK(!ico_present_fallback_qualcomm_own(0x13b5u, "ARM Mali driver"));
    CHECK(!ico_present_fallback_qualcomm_own(0x1002u, "AMD proprietary"));
    CHECK(!ico_present_fallback_qualcomm_own(0x10deu, "NVIDIA 570"));
    CHECK(ico_present_fallback_qualcomm_own(0x5143u, k_qcom));
    CHECK(!ico_present_fallback_driver_eligible(0x5143u, "turnip Mesa driver"));
    CHECK(!ico_present_fallback_driver_eligible(0x5143u, "TURNIP"));
    CHECK(!ico_present_fallback_driver_eligible(0x1002u, "radv Mesa 25.1.0"));
    CHECK(!ico_present_fallback_driver_eligible(0x10005u, "llvmpipe MESA 24.0"));
    CHECK(!ico_present_fallback_driver_eligible(0u, k_qcom));
}

static void testMode(void)
{
    char line[96];

    CHECK(ico_present_fallback_mode(NULL) == ICO_PRESENT_FALLBACK_AUTO);
    CHECK(ico_present_fallback_mode("auto") == ICO_PRESENT_FALLBACK_AUTO);
    CHECK(ico_present_fallback_mode("off") == ICO_PRESENT_FALLBACK_OFF);
    CHECK(ico_present_fallback_mode("0") == ICO_PRESENT_FALLBACK_OFF);
    CHECK(ico_present_fallback_mode("false") == ICO_PRESENT_FALLBACK_OFF);
    CHECK(ico_present_fallback_mode("no") == ICO_PRESENT_FALLBACK_OFF);
    CHECK(ico_present_fallback_mode("fifo") == ICO_PRESENT_FALLBACK_FIFO);
    ico_present_fallback_mode_suffix(NULL, ICO_PRESENT_FALLBACK_AUTO, 1, 0, line, sizeof line);
    CHECK_STR(line, ", FIFO fallback armed");
    ico_present_fallback_mode_suffix(NULL, ICO_PRESENT_FALLBACK_FIFO, 0, 0, line, sizeof line);
    CHECK_STR(line, ", FIFO fallback set by [dev] present_fallback=fifo");
    ico_present_fallback_mode_suffix(NULL, ICO_PRESENT_FALLBACK_OFF, 0, 0, line, sizeof line);
    CHECK_STR(line, "");
    /* FIFO from the start wins over armed (the gate is NO under FIFO anyway) */
    ico_present_fallback_mode_suffix(NULL, ICO_PRESENT_FALLBACK_AUTO, 0, 1, line, sizeof line);
    CHECK_STR(line, ", FIFO from the start (Qualcomm's own driver)");
}

/* FIFO from the first swapchain on Qualcomm's own driver (issue 123) */
static void testStart(void)
{
    char line[160];
    const unsigned qcom = 0x5143u;

    /* eligible + auto + vsync off: FIFO from the start */
    CHECK(ico_present_fallback_qualcomm_own(qcom, k_qcom));
    CHECK(ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, qcom, k_qcom));
    /* the reporter's A/B: off keeps mailbox for the whole run */
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_OFF, 1, 0, qcom, k_qcom));
    /* fifo is FIFO from the start by its own path, not this one */
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_FIFO, 1, 0, qcom, k_qcom));
    /* vsync on: unchanged */
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 1, qcom, k_qcom));
    /* Mesa's and Turnip's drivers on the same vendor id keep mailbox */
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, qcom, "turnip Mesa driver"));
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, qcom, "Mesa 25.1.0"));
    CHECK(!ico_present_fallback_qualcomm_own(qcom, "TURNIP"));
    /* other vendors keep mailbox and the blocks' fallback */
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, 0x13b5u, "ARM Mali driver"));
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, 0x1002u, "AMD proprietary"));
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, 0x10deu, NULL));
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, 0u, k_qcom));
    /* a Qualcomm device with no driver text is the stock driver (the rule
       that exists: only Mesa's and Turnip's texts are excluded) */
    CHECK(ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 1, 0, qcom, NULL));
    /* phones only: Windows on Snapdragon (the same vendor id, Qualcomm's
       own Vulkan driver, a surface offering immediate) is never asked, so
       its Vsync Off stays immediate */
    CHECK(ico_present_fallback_start_asked(ICO_PRESENT_FALLBACK_AUTO, 1));
    CHECK(!ico_present_fallback_start_asked(ICO_PRESENT_FALLBACK_AUTO, 0));
    CHECK(!ico_present_fallback_start_asked(ICO_PRESENT_FALLBACK_OFF, 1));
    CHECK(!ico_present_fallback_start_asked(ICO_PRESENT_FALLBACK_FIFO, 1));
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 0, 0, qcom, k_qcom));
    CHECK(!ico_present_fallback_start(ICO_PRESENT_FALLBACK_AUTO, 0, 0, qcom, NULL));
    /* the build's own platform */
#ifdef __ANDROID__
    CHECK(ICO_PRESENT_FALLBACK_ANDROID == 1);
#else
    CHECK(ICO_PRESENT_FALLBACK_ANDROID == 0);
#endif
    ico_present_fallback_start_line(line, sizeof line);
    CHECK_STR(line, "window: present fifo (Qualcomm's own driver: FIFO from the start; [dev] "
                    "present_fallback = off keeps mailbox)");
}

int main(void)
{
    testLog();
    testApart();
    testShort();
    testTurnip();
    testKept();
    testOther();
    testLate();
    testDriver();
    testMode();
    testStart();
    return ico_check_summary("present_fallback_test");
}
