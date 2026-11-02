/* rd_pixel_uber_test.inc: testUberSubtractOracle, included by rd_pixel_test.c
 * after testExactModes (rd_pixel_test.c stays under the 256 KiB file cap). */

/* ------------------------------------------ the premultiplied subtracts
 * Under GS state as shader data (rd_uber.c, ctest rd_pixel_uber) the
 * attachment read draws the premultiplied subtracts, ALPHA modes 1 (Cd -
 * Cs * FIX) and 6 (Cd - Cs * As), which every other mode leaves on the
 * blender (rd_dst_read.c premulForm; RENDER.md "What the exact blend draws
 * when opted into").  The GS's term is (0 - Cs) F >> 7, an arithmetic
 * shift that rounds the negative product down, so a channel is
 *   clamp(Cd + (((0 - Cs) * F) >> 7), 0, 255)         (the oracle, the GS)
 * where the blender subtracts the shader's truncated (Cs F) >> 7:
 *   clamp(Cd - min((Cs * F) >> 7, 255), 0, 255)      (the fixed-function arm)
 * one level above the GS exactly where Cs F is not a multiple of 128 (and
 * the GS's value is not clamped at 0), never below.  F is FIX (mode 1) or
 * As (mode 6) on the 0x80 = 1.0 scale, unclamped above it; COLCLAMP on,
 * PABE off; the alpha the GS writes is As, its MSB set under FBA.
 *
 * Ten 64 x 64 cells in SCENE: mode 1 with FIX 0x10, 0x55, 0x80 (every
 * product a multiple of 128: the two arms agree) and 0xC0 (past 1.0),
 * mode 6 (F = As, 0..0xFF), each with FBA off and on.  Cd is a noise
 * texture drawn 1:1 with the +8 UV nudge (DECAL: the sprite case's exact
 * texel copy), Cs and As a second one drawn the same way over the cell,
 * shifted 7 texels a cell, so every input is a texel the CPU holds.  Under
 * every mode the device has: through the colour read every pixel is the
 * oracle's; on the blender (the fixed-function blend, the storage path,
 * the attachment read without GS state as shader data, the two-pass
 * fallback) every pixel is the truncated form's, and against the oracle
 * each RGB channel is the same or one above, above only where Cs F is not
 * a multiple of 128, the alpha the same.  The oracle is written out and
 * checked against gs_blend_reg_ch (gs_math.hlsli, the shader's own
 * arithmetic). */

#define USO_CELL 64
#define USO_CELLS 10

static const struct {
    RdBlend eq;
    uint8_t fix, fba;
} kUso[USO_CELLS] = {
    {RD_BLEND_CD_SUB_CS_FIX, 0x10, 0}, {RD_BLEND_CD_SUB_CS_FIX, 0x10, 1},
    {RD_BLEND_CD_SUB_CS_FIX, 0x55, 0}, {RD_BLEND_CD_SUB_CS_FIX, 0x55, 1},
    {RD_BLEND_CD_SUB_CS_FIX, 0x80, 0}, {RD_BLEND_CD_SUB_CS_FIX, 0x80, 1},
    {RD_BLEND_CD_SUB_CS_FIX, 0xC0, 0}, {RD_BLEND_CD_SUB_CS_FIX, 0xC0, 1},
    {RD_BLEND_CD_SUB_CS_AS, 0x80, 0},  {RD_BLEND_CD_SUB_CS_AS, 0x80, 1},
};

static int usoClamp(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* the frame: the Cd noise over the whole target, then each cell's sprite
 * of the Cs noise under its blend (tintFrame's target and space) */
static void usoFrame(RdTex cd, RdTex cs)
{
    static const uint8_t clr[4] = {0, 0, 0, 0}, white[4] = {0x80, 0x80, 0x80, 0x80};
    rd_begin_frame();
    rd_select_list(5);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, 512, 512, 0);
    opaque2D();
    rd_color_mask(0);
    rd_col_clamp(1);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_texture(cd, RD_TEXFN_DECAL, RD_TCC_RGBA);
    sprite(512, 512, 0, 0, 512 * 16, 512 * 16, white, 8, 8, 512 * 16 + 8, 512 * 16 + 8);
    rd_texture(cs, RD_TEXFN_DECAL, RD_TCC_RGBA);
    for (int c = 0; c < USO_CELLS; c++) {
        const int32_t x0 = (c % 8) * USO_CELL, y0 = (c / 8) * USO_CELL, u0 = c * 7 * 16 + 8;
        rd_blend(kUso[c].eq, kUso[c].fix, 1);
        rd_fba(kUso[c].fba);
        sprite(512, 512, x0 * 16, y0 * 16, (x0 + USO_CELL) * 16, (y0 + USO_CELL) * 16, white, u0, 8,
               u0 + USO_CELL * 16, USO_CELL * 16 + 8);
    }
    rd_texture_off();
    rd_fba(0);
    rd_end_frame(0);
}

static void testUberSubtractOracle(void)
{
    static uint8_t cdTex[64 * 64 * 4], csTex[64 * 64 * 4];
    for (uint32_t i = 0; i < 64 * 64; i++) {
        uint32_t h = hash(i + 0x5EEDu);
        memcpy(&cdTex[i * 4], &h, 4);
        h = hash(i + 0xC5C5u);
        memcpy(&csTex[i * 4], &h, 4);
    }
    RdTex cd = rd_create_texture(64, 64, cdTex, RD_TEXA_80_80, "uber sub cd");
    RdTex cs = rd_create_texture(64, 64, csTex, RD_TEXA_80_80, "uber sub cs");
    const RdBlendExact was = rd_blend_exact();
    for (int m = 0; m < RD_BLEND_EXACT_COUNT; m++) {
        if (!rd_set_blend_exact((RdBlendExact)m)) {
            printf("  uber subtract oracle: %s not on this device\n", rd__blend_exact_name(m));
            continue;
        }
        /* the read draws the subtracts only under GS state as shader data
         * with the premultiplied forms on (rd__expand_dst) */
        const bool read = m == RD_BLEND_EXACT_ATTACHMENT && g_rd.uber && g_rd.uberPremul;
        const char *arm = read ? "the GS's floor" : "the blender's truncation";
        usoFrame(cd, cs);
        uint32_t w = 0, h = 0;
        uint8_t *img = readTarget(RD_TARGET_SCENE, &w, &h);
        if (!img || w != 512 || h != 512) {
            CHECK(0, "uber subtract oracle: SCENE readback %u x %u", w, h);
            continue;
        }
        int bad = 0, lifted = 0, liftBad = 0, apart = 0, refBad = 0, shown = 0;
        for (int c = 0; c < USO_CELLS; c++) {
            const uint32_t reg = rd__alpha_register((uint8_t)kUso[c].eq);
            for (int y = 0; y < USO_CELL; y++) {
                for (int x = 0; x < USO_CELL; x++) {
                    const int px = (c % 8) * USO_CELL + x, py = (c / 8) * USO_CELL + y;
                    const uint8_t *d = &cdTex[((py % 64) * 64 + (px % 64)) * 4];
                    const uint8_t *s = &csTex[((py % 64) * 64 + ((px + c * 7) % 64)) * 4];
                    const uint8_t *g = &img[((size_t)py * w + (size_t)px) * 4];
                    const int as = s[3], ad = d[3];
                    const int f = kUso[c].eq == RD_BLEND_CD_SUB_CS_FIX ? kUso[c].fix : as;
                    int gs[4], hw[4], off = 0, pixApart = 0, pixLift = 0;
                    for (int k = 0; k < 3; k++) {
                        gs[k] = usoClamp(d[k] + (((0 - s[k]) * f) >> 7));
                        refBad +=
                            gs_blend_reg_ch(reg, s[k], d[k], as, ad, kUso[c].fix, 1u) != gs[k];
                        const int p = (s[k] * f) >> 7;
                        hw[k] = usoClamp(d[k] - (p > 255 ? 255 : p));
                        pixApart += hw[k] != gs[k];
                        if (!read) {
                            /* the device's bytes over the GS: +1 where Cs F
                             * is not a multiple of 128, never below, never
                             * more */
                            const int lift = (int)g[k] - gs[k];
                            liftBad +=
                                lift < 0 || lift > 1 || (lift == 1 && ((s[k] * f) & 127) == 0);
                            pixLift += lift == 1;
                        }
                    }
                    gs[3] = hw[3] = kUso[c].fba ? (as | 0x80) : as;
                    const int *want = read ? gs : hw;
                    for (int k = 0; k < 4; k++) {
                        off += g[k] != want[k];
                    }
                    if (off && shown++ < 8) {
                        printf("FAIL uber subtract oracle (%s%s): cell %d mode %d FIX 0x%02X FBA "
                               "%d (%d,%d) Cd %u %u %u %u Cs %u %u %u %u: got %u %u %u %u, "
                               "expected %d %d %d %d (%s)\n",
                               rd__blend_exact_name(m), read ? ", colour read" : "", c,
                               (int)kUso[c].eq, kUso[c].fix, kUso[c].fba, px, py, d[0], d[1], d[2],
                               d[3], s[0], s[1], s[2], s[3], g[0], g[1], g[2], g[3], want[0],
                               want[1], want[2], want[3], arm);
                    }
                    bad += off != 0;
                    apart += pixApart != 0;
                    lifted += pixLift != 0;
                }
            }
        }
        printf("  uber subtract oracle (%s%s): %d cells x %d px, %d pixels off %s; the blender a "
               "level above the GS on %d (%d on the CPU's forms)\n",
               rd__blend_exact_name(m), read ? ", colour read" : "", USO_CELLS, USO_CELL * USO_CELL,
               bad, arm, lifted, apart);
        CHECK(refBad == 0,
              "uber subtract oracle: gs_blend_reg_ch is not the GS's floor on %d channels", refBad);
        CHECK(apart > 0, "uber subtract oracle: no pixel where Cs F is not a multiple of 128");
        if (read) {
            CHECK(bad == 0, "uber subtract oracle (colour read): %d pixels are not the GS's floor",
                  bad);
        } else {
            CHECK(bad == 0, "uber subtract oracle (%s): %d pixels are not the blender's truncation",
                  rd__blend_exact_name(m), bad);
            CHECK(liftBad == 0,
                  "uber subtract oracle (%s): %d channels are not the GS's floor or one level "
                  "above it where Cs F is not a multiple of 128",
                  rd__blend_exact_name(m), liftBad);
            CHECK(lifted > 0, "uber subtract oracle (%s): the blender never rounds up",
                  rd__blend_exact_name(m));
        }
    }
    rd_set_blend_exact(was);
    rd_destroy_texture(cd);
    rd_destroy_texture(cs);
    rhi_wait_idle();
    rd__pipeline_cache_clear(); /* outside the reachable set, as testExactModes' */
}
