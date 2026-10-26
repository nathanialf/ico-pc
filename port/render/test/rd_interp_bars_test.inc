/* rd_interp_test.c's bars case (issue 139): included by rd_interp_test.c
 * after the unmatched draws' case, so it shares its helpers (sprite,
 * frameHead, built, build, alphaOf, drawsOf, endStateOf); a file of its own
 * keeps each under the repository's file size cap.
 *
 *   bars      the load screen's black bars at a change of layout
 *             (layout_texture.c: one pair of 640-wide sprites per layout,
 *             keyed by the layout's row, blend C = As, D = Cd, PABE off):
 *             each tick draws a matched backdrop and then a top and a bottom
 *             bar, the tick before's under one layout's keys and the tick's
 *             under the next layout's.  Identical bars (vertices, command
 *             and state) pair: at t 0.25 and 0.5 the tick's bars are drawn
 *             once each at full alpha, the tick before's not at all, the
 *             frame ends in the tick's state and the stats count 2 pairs.
 *             Controls: the tick's bars 4 px taller (geometry), or drawn
 *             with another blend FIX (state): no pair, both fade as before
 *             (each at 0x40 half way, 0 pairs). */
static const char kObjBar;

/* what: 0 the same bars, 1 taller bars in the tick, 2 another FIX in the
 * tick */
static void recordBars(int second, int what)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80}, sky[4] = {120, 110, 90, 0x80};
    rd_begin_frame();
    frameHead();
    sprite(0, 0, TW, TH, sky, RD_KEY(&kObjBar, 0, 0)); /* the backdrop, matched */
    rd_blend(RD_BLEND_LERP_AS, second && what == 2 ? 0x40 : 0x80, 1);
    const int grow = second && what == 1 ? 4 : 0;
    const int layout = second ? 2 : 1;
    sprite(0, 0, TW, 20 + grow, black, RD_KEY(&kObjBar, layout * 2, 0));
    sprite(0, TH - 20 - grow, TW, TH, black, RD_KEY(&kObjBar, layout * 2 + 1, 0));
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_end_frame(0);
}

static void testBars(void)
{
    const RdKey kTopP = RD_KEY(&kObjBar, 2, 0), kBotP = RD_KEY(&kObjBar, 3, 0);
    const RdKey kTopC = RD_KEY(&kObjBar, 4, 0), kBotC = RD_KEY(&kObjBar, 5, 0);
    for (int what = 0; what < 3; what++) {
        static const char *const kName[3] = {"the same bars", "taller bars", "another FIX"};
        recordBars(0, what);
        recordBars(1, what);
        const RdFrame *cur = rd__last_frame();
        RdStateBlock endCur, endOut;
        endStateOf(cur, &endCur);
        const float ts[2] = {0.25f, 0.5f};
        for (int k = 0; k < 2; k++) {
            const float t = ts[k];
            const RdInterpStats *st = build(t, 1);
            const uint32_t paired = st->screenPaired;
            const RdFrame *f = built(t);
            const int nTopC = drawsOf(f, 0, kTopC, RDC_SCREEN),
                      nBotC = drawsOf(f, 0, kBotC, RDC_SCREEN);
            const int nTopP = drawsOf(f, 0, kTopP, RDC_SCREEN),
                      nBotP = drawsOf(f, 0, kBotP, RDC_SCREEN);
            if (what == 0) {
                CHECK(paired == 2 && nTopC == 1 && nBotC == 1 && nTopP == 0 && nBotP == 0 &&
                          alphaOf(f, kTopC) == 0x80 && alphaOf(f, kBotC) == 0x80,
                      "bars, %s, t %.2f: the tick's bars once each, whole, the tick before's "
                      "dropped (paired %u, draws %d %d / %d %d, alpha %d %d)",
                      kName[what], (double)t, paired, nTopC, nBotC, nTopP, nBotP, alphaOf(f, kTopC),
                      alphaOf(f, kBotC));
                endStateOf(f, &endOut);
                CHECK(memcmp(&endOut, &endCur, sizeof(endCur)) == 0,
                      "bars, %s, t %.2f: the frame ends as the tick does", kName[what], (double)t);
            } else {
                const int in = (int)floorf(128.0f * t + 0.5f),
                          out = (int)floorf(128.0f * (1.0f - t) + 0.5f);
                CHECK(paired == 0 && nTopC == 1 && nBotC == 1 && nTopP == 1 && nBotP == 1 &&
                          alphaOf(f, kTopC) == in && alphaOf(f, kBotC) == in &&
                          alphaOf(f, kTopP) == out && alphaOf(f, kBotP) == out,
                      "bars, %s, t %.2f: no pair, both fade (paired %u, draws %d %d / %d %d, "
                      "alpha in %d %d (%d), out %d %d (%d))",
                      kName[what], (double)t, paired, nTopC, nBotC, nTopP, nBotP, alphaOf(f, kTopC),
                      alphaOf(f, kBotC), in, alphaOf(f, kTopP), alphaOf(f, kBotP), out);
            }
        }
    }
}
