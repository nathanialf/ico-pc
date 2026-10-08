/* appearance_test.c: the characters' colours (port/game/appearance.c,
 * v0.4.2 package K).  CPU only; every CLUT here is synthetic (no colour
 * of the disc, docs/LEGAL.md).
 *
 *   identity    every part Original: recolour returns 0, out untouched
 *   covers      the 14 character textures, not the *_l / *_ref layers,
 *               the stone variants, the hair, sekika_boy or the tapes
 *   masks       b_mantle's four groups change disjoint sets of entries
 *               that cover all 16
 *   alpha       16- and 256-entry CLUTs: the alpha bytes stay
 *   formats     not PSMCT32, 0 or 257 colours, NULLs: 0
 *   anchor      each part's anchor lands on every target within 1, the
 *               swatch is that colour (the anchor for Original), and a
 *               darker and a lighter entry keep their order
 *   grey        a neutral target gives R = G = B
 *   weights     fuku22: S 0.04 follows the dress only, S 0.13 the skin
 *               only, and the two weights of an entry between sum to 1
 *   config      set -> [characters] string -> reload, the values'
 *               spelling, unknown values -> Original, saved and read back
 *   randomize   the same seed the same colours, never Original, the
 *               poncho's groups different, one serial step
 *   reset       every part Original, the keys "original"
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appearance.h"
#include "config.h"
#include "rd_tex.h"

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

static char s_dir[900];

static void writeConfig(const char *name, const char *text)
{
    char p[1024];
    FILE *f;

    snprintf(p, sizeof(p), "%s/%s", s_dir, name);
    f = fopen(p, "w");
    CHECK(f != NULL, "%s can be written", p);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
    ico_config_reset(p, "/nonexistent/appearance_test.ini");
    ico_appearance_reload();
}

/* the test's own HSL (degrees, 0..1) to 8-bit RGB */
static double hue(double p, double q, double t)
{
    if (t < 0.0) {
        t += 1.0;
    }
    if (t > 1.0) {
        t -= 1.0;
    }
    if (t < 1.0 / 6.0) {
        return p + (q - p) * 6.0 * t;
    }
    if (t < 0.5) {
        return q;
    }
    if (t < 2.0 / 3.0) {
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    }
    return p;
}

static void hslRgb(double h, double s, double l, int rgb[3])
{
    double v[3];
    h /= 360.0;
    if (s <= 0.0) {
        v[0] = v[1] = v[2] = l;
    } else {
        double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        double p = 2.0 * l - q;
        v[0] = hue(p, q, h + 1.0 / 3.0);
        v[1] = hue(p, q, h);
        v[2] = hue(p, q, h - 1.0 / 3.0);
    }
    for (int i = 0; i < 3; i++) {
        rgb[i] = (int)floor(v[i] * 255.0 + 0.5);
    }
}

static double lightness(const unsigned char *c)
{
    int mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
    int mn = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
    return (mx + mn) / 510.0;
}

/* the palette and the ramp, as appearance.h gives them */
static const struct {
    const char *name;
    short h, s, l;
} kPal[ICO_APP_COLOURS] = {
    {"red", 0, 70, 45},      {"crimson", 350, 65, 35}, {"rose", 340, 55, 65},
    {"pink", 330, 60, 75},   {"magenta", 310, 55, 45}, {"plum", 290, 35, 30},
    {"violet", 270, 45, 50}, {"indigo", 245, 45, 30},  {"navy", 225, 45, 20},
    {"blue", 215, 65, 45},   {"sky", 200, 60, 70},     {"teal", 180, 50, 32},
    {"cyan", 185, 55, 55},   {"green", 130, 45, 35},   {"moss", 90, 35, 32},
    {"olive", 65, 40, 35},   {"gold", 45, 70, 55},     {"orange", 28, 80, 52},
    {"rust", 15, 60, 35},    {"brown", 25, 40, 25},    {"sand", 35, 35, 70},
    {"white", 30, 10, 92},   {"grey", 0, 0, 55},       {"black", 0, 0, 8},
};

static void targetRgb(IcoAppPart p, int v, int rgb[3])
{
    if (ico_appearance_is_skin(p)) {
        double k = (v - 1) / 11.0;
        hslRgb(30.0 - 14.0 * k, (45.0 - 11.0 * k) / 100.0, (85.0 - 65.0 * k) / 100.0, rgb);
    } else {
        hslRgb(kPal[v - 1].h, kPal[v - 1].s / 100.0, kPal[v - 1].l / 100.0, rgb);
    }
}

static void allOriginal(void)
{
    for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
        ico_appearance_set((IcoAppPart)p, 0);
    }
}

static void put(unsigned char *c, int i, unsigned int rgb, int a)
{
    c[i * 4 + 0] = (unsigned char)(rgb >> 16);
    c[i * 4 + 1] = (unsigned char)(rgb >> 8);
    c[i * 4 + 2] = (unsigned char)rgb;
    c[i * 4 + 3] = (unsigned char)a;
}

/* a 16-entry CLUT of distinct, somewhat saturated colours */
static void synth16(unsigned char *c)
{
    for (int i = 0; i < 16; i++) {
        put(c, i,
            (unsigned)(60 + i * 11) << 16 | (unsigned)(40 + i * 9) << 8 | (unsigned)(30 + i * 7),
            0x80);
    }
}

static void testIdentity(void)
{
    unsigned char in[64], out[64];
    writeConfig("appearance_id.toml", "version = 1\n");
    synth16(in);
    memset(out, 0xAB, sizeof(out));
    for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
        CHECK(ico_appearance_get((IcoAppPart)p) == 0, "part %d Original by default", p);
    }
    CHECK(ico_appearance_recolour("b_mantle", in, 16, RDTEX_PSMCT32, out) == 0, "identity: 0");
    CHECK(ico_appearance_recolour("fuku22", in, 16, RDTEX_PSMCT32, out) == 0, "identity: 0");
    int same = 1;
    for (int i = 0; i < 64; i++) {
        same &= out[i] == 0xAB;
    }
    CHECK(same, "identity: out untouched");
    CHECK(ico_appearance_choices(ICO_APP_ICO_SKIN) == 13 &&
              ico_appearance_choices(ICO_APP_YORDA_SKIN) == 13 &&
              ico_appearance_choices(ICO_APP_ICO_TUNIC) == 25 &&
              ico_appearance_choices(ICO_APP_YORDA_DRESS) == 25,
          "choices");
    CHECK(ico_appearance_character(ICO_APP_ICO_SHORTS) == 0 &&
              ico_appearance_character(ICO_APP_YORDA_SKIN) == 1 &&
              ico_appearance_character(ICO_APP_YORDA_DRESS) == 1,
          "character");
}

static void testCovers(void)
{
    static const char *yes[] = {"b_arm",   "b_face2",  "b_head_top", "b_mantle",   "b_suit",
                                "b_pants", "hada_red", "hada_red2",  "facft05_36", "fuku22",
                                "fuku03",  "fuku04",   "fuku06",     "poncho005"};
    static const char *no[] = {
        "b_mantle_l", "b_face2_l",    "b_arm_l", "b_suit_l", "eye02_ref", "horn_ref", "sg_fuku03",
        "sg_fuku22",  "sg_poncho005", "g_hair7", "g_hair9",  "g_hair9_d", "sg_hair7", "sekika_boy",
        "tape_b",     "tape_boro",    "b_shoes", "b_hair02", "",          "b_mant"};
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); i++) {
        CHECK(ico_appearance_covers(yes[i]), "covers %s", yes[i]);
    }
    for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
        CHECK(!ico_appearance_covers(no[i]), "does not cover %s", no[i]);
    }
    CHECK(!ico_appearance_covers(NULL), "NULL");
}

static void testMasks(void)
{
    static const IcoAppPart fam[4] = {ICO_APP_ICO_PONCHO_NAVY, ICO_APP_ICO_PONCHO_PINK,
                                      ICO_APP_ICO_PONCHO_LIGHT, ICO_APP_ICO_PONCHO_DARK};
    unsigned char in[64], out[64];
    unsigned int seen = 0;
    synth16(in);
    writeConfig("appearance_masks.toml", "version = 1\n");
    for (int f = 0; f < 4; f++) {
        unsigned int set = 0;
        allOriginal();
        ico_appearance_set(fam[f], 14); /* green */
        CHECK(ico_appearance_recolour("b_mantle", in, 16, RDTEX_PSMCT32, out) == 1, "family %d", f);
        for (int i = 0; i < 16; i++) {
            if (memcmp(in + i * 4, out + i * 4, 3) != 0) {
                set |= 1u << i;
            }
        }
        CHECK(set != 0, "family %d changes entries", f);
        CHECK((set & seen) == 0, "family %d disjoint (0x%04x vs 0x%04x)", f, set, seen);
        seen |= set;
    }
    CHECK(seen == 0xFFFF, "the four families cover the 16 entries (0x%04x)", seen);
    allOriginal();
    ico_appearance_set(ICO_APP_ICO_PONCHO_NAVY, 14);
    CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT32, out) == 0,
          "a texture whose parts are all Original: 0");
}

static void testAlpha(void)
{
    unsigned char in[1024], out[1024];
    unsigned int x = 12345;
    for (int i = 0; i < 1024; i++) {
        x = x * 1103515245u + 12345u;
        in[i] = (unsigned char)(x >> 16);
    }
    writeConfig("appearance_alpha.toml", "version = 1\n");
    ico_appearance_set(ICO_APP_ICO_TUNIC, 1);
    ico_appearance_set(ICO_APP_YORDA_SKIN, 7);
    ico_appearance_set(ICO_APP_YORDA_DRESS, 10);
    CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT32, out) == 1, "16 colours");
    int ok = 1, changed = 0;
    for (int i = 0; i < 16; i++) {
        ok &= out[i * 4 + 3] == in[i * 4 + 3];
        changed += memcmp(in + i * 4, out + i * 4, 3) != 0;
    }
    CHECK(ok && changed > 8, "16 colours: alpha kept (%d changed)", changed);
    CHECK(ico_appearance_recolour("fuku22", in, 256, RDTEX_PSMCT32, out) == 1, "256 colours");
    ok = 1;
    changed = 0;
    for (int i = 0; i < 256; i++) {
        ok &= out[i * 4 + 3] == in[i * 4 + 3];
        changed += memcmp(in + i * 4, out + i * 4, 3) != 0;
    }
    CHECK(ok && changed > 128, "256 colours: alpha kept (%d changed)", changed);
}

static void testFormats(void)
{
    unsigned char in[1024], out[1024];
    memset(in, 0x40, sizeof(in));
    writeConfig("appearance_fmt.toml", "version = 1\n[characters]\nico_tunic = \"red\"\n");
    CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT32, out) == 1, "CT32");
    CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT16, out) == 0, "CT16");
    CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT24, out) == 0, "CT24");
    CHECK(ico_appearance_recolour("b_suit", in, 0, RDTEX_PSMCT32, out) == 0, "0 colours");
    CHECK(ico_appearance_recolour("b_suit", in, 257, RDTEX_PSMCT32, out) == 0, "257 colours");
    CHECK(ico_appearance_recolour("b_suit", NULL, 16, RDTEX_PSMCT32, out) == 0, "no CLUT");
    CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT32, NULL) == 0, "no out");
    CHECK(ico_appearance_recolour(NULL, in, 16, RDTEX_PSMCT32, out) == 0, "no name");
    CHECK(ico_appearance_recolour("b_shoes", in, 16, RDTEX_PSMCT32, out) == 0, "not covered");
}

/* a texture and entry where the part's weight is 1 */
static const struct {
    IcoAppPart part;
    const char *tex;
    int entry;
} kFull[ICO_APP_PART_COUNT] = {
    {ICO_APP_ICO_SKIN, "b_face2", 0},          {ICO_APP_ICO_PONCHO_NAVY, "b_mantle", 2},
    {ICO_APP_ICO_PONCHO_PINK, "b_mantle", 11}, {ICO_APP_ICO_PONCHO_LIGHT, "b_mantle", 13},
    {ICO_APP_ICO_PONCHO_DARK, "b_mantle", 1},  {ICO_APP_ICO_TUNIC, "b_suit", 0},
    {ICO_APP_ICO_SHORTS, "b_pants", 0},        {ICO_APP_YORDA_SKIN, "hada_red", 0},
    {ICO_APP_YORDA_DRESS, "fuku03", 0},
};

static unsigned int scale(unsigned int rgb, double f)
{
    unsigned int o = 0;
    for (int s = 16; s >= 0; s -= 8) {
        double v = ((rgb >> s) & 0xFF) * f;
        o |= (unsigned int)(v > 255.0 ? 255.0 : v) << s;
    }
    return o;
}

static void testAnchor(void)
{
    writeConfig("appearance_anchor.toml", "version = 1\n");
    for (int k = 0; k < ICO_APP_PART_COUNT; k++) {
        IcoAppPart p = kFull[k].part;
        unsigned int anchor;
        unsigned char in[64], out[64];
        int n = ico_appearance_choices(p);

        allOriginal();
        anchor = ico_appearance_swatch(p);
        synth16(in);
        put(in, kFull[k].entry, anchor, 0x80);
        /* the darker and lighter neighbours in the same family (b_mantle:
           the entry's own group only holds it, so the order is checked on
           the textures whose rule covers every entry) */
        int dark = (kFull[k].entry + 1) % 16, light = (kFull[k].entry + 3) % 16;
        int ordered = strcmp(kFull[k].tex, "b_mantle") != 0;
        put(in, dark, scale(anchor, 0.85), 0x80);
        put(in, light, scale(anchor, 1.15), 0x80);
        for (int v = 1; v < n; v++) {
            int rgb[3];
            unsigned int sw;

            ico_appearance_set(p, v);
            CHECK(ico_appearance_recolour(kFull[k].tex, in, 16, RDTEX_PSMCT32, out) == 1,
                  "part %d value %d", (int)p, v);
            targetRgb(p, v, rgb);
            const unsigned char *o = out + kFull[k].entry * 4;
            CHECK(abs(o[0] - rgb[0]) <= 1 && abs(o[1] - rgb[1]) <= 1 && abs(o[2] - rgb[2]) <= 1,
                  "part %d value %d: the anchor lands on %d,%d,%d (got %d,%d,%d)", (int)p, v,
                  rgb[0], rgb[1], rgb[2], o[0], o[1], o[2]);
            sw = ico_appearance_swatch(p);
            CHECK(abs((int)(sw >> 16) - rgb[0]) <= 1 &&
                      abs((int)((sw >> 8) & 0xFF) - rgb[1]) <= 1 &&
                      abs((int)(sw & 0xFF) - rgb[2]) <= 1,
                  "part %d value %d: swatch %06x", (int)p, v, sw);
            if (ordered) {
                double ld = lightness(out + dark * 4), la = lightness(o),
                       ll = lightness(out + light * 4);
                CHECK(ld <= la + 1e-9 && la <= ll + 1e-9,
                      "part %d value %d: lightness order %.3f %.3f %.3f", (int)p, v, ld, la, ll);
            }
        }
        ico_appearance_set(p, 0);
        CHECK(ico_appearance_swatch(p) == anchor, "part %d: Original swatch is the anchor", (int)p);
    }
}

static void testGrey(void)
{
    unsigned char in[64], out[64];
    synth16(in);
    writeConfig("appearance_grey.toml", "version = 1\n");
    for (int v = 23; v <= 24; v++) { /* grey, black */
        ico_appearance_set(ICO_APP_ICO_TUNIC, v);
        CHECK(ico_appearance_recolour("b_suit", in, 16, RDTEX_PSMCT32, out) == 1, "grey");
        for (int i = 0; i < 16; i++) {
            CHECK(out[i * 4] == out[i * 4 + 1] && out[i * 4 + 1] == out[i * 4 + 2],
                  "value %d entry %d: R = G = B (%d %d %d)", v, i, out[i * 4], out[i * 4 + 1],
                  out[i * 4 + 2]);
        }
    }
}

/* an entry of hue 25, lightness 0.5 and saturation s */
static unsigned int entryS(double s)
{
    int rgb[3];
    hslRgb(25.0, s, 0.5, rgb);
    return (unsigned int)rgb[0] << 16 | (unsigned int)rgb[1] << 8 | (unsigned int)rgb[2];
}

static void testWeights(void)
{
    unsigned char in[64], full[64], dress[64], skin[64], both[64];
    synth16(in);
    put(in, 0, entryS(0.04), 0x80); /* dress only */
    put(in, 1, entryS(0.13), 0x80); /* skin only */
    put(in, 2, entryS(0.08), 0x80); /* between */
    put(in, 3, entryS(0.07), 0x80);
    put(in, 4, entryS(0.09), 0x80);
    writeConfig("appearance_weights.toml", "version = 1\n");

    /* the dress alone: fuku03 (all dress) gives the full change */
    ico_appearance_set(ICO_APP_YORDA_DRESS, 24); /* black: far from the cloth */
    CHECK(ico_appearance_recolour("fuku03", in, 16, RDTEX_PSMCT32, full) == 1, "fuku03");
    CHECK(ico_appearance_recolour("fuku22", in, 16, RDTEX_PSMCT32, dress) == 1, "fuku22 dress");
    CHECK(memcmp(dress, full, 4) == 0, "S 0.04: the dress's full change");
    CHECK(memcmp(dress + 4, in + 4, 4) == 0, "S 0.13: no dress");
    double wd[3];
    for (int e = 2; e <= 4; e++) {
        double d = (double)full[e * 4] - in[e * 4];
        wd[e - 2] = ((double)dress[e * 4] - in[e * 4]) / d;
    }

    /* the skin alone: hada_red (all skin) gives the full change */
    ico_appearance_set(ICO_APP_YORDA_DRESS, 0);
    ico_appearance_set(ICO_APP_YORDA_SKIN, 1); /* Tone 1: far lighter */
    CHECK(ico_appearance_recolour("hada_red", in, 16, RDTEX_PSMCT32, full) == 1, "hada_red");
    CHECK(ico_appearance_recolour("fuku22", in, 16, RDTEX_PSMCT32, skin) == 1, "fuku22 skin");
    CHECK(memcmp(skin + 4, full + 4, 4) == 0, "S 0.13: the skin's full change");
    CHECK(memcmp(skin, in, 4) == 0, "S 0.04: no skin");
    for (int e = 2; e <= 4; e++) {
        double d = (double)full[e * 4] - in[e * 4];
        double ws = ((double)skin[e * 4] - in[e * 4]) / d;
        CHECK(fabs(ws + wd[e - 2] - 1.0) < 0.04, "entry %d: the weights sum to 1 (%.3f + %.3f)", e,
              ws, wd[e - 2]);
        CHECK(ws > 0.02 && wd[e - 2] > 0.02, "entry %d: both parts weigh (%.3f, %.3f)", e, ws,
              wd[e - 2]);
    }
    /* both on: an entry between moves by both */
    ico_appearance_set(ICO_APP_YORDA_DRESS, 24);
    CHECK(ico_appearance_recolour("fuku22", in, 16, RDTEX_PSMCT32, both) == 1, "fuku22 both");
    CHECK(memcmp(both, dress, 4) == 0 && memcmp(both + 4, skin + 4, 4) == 0,
          "both: the pure entries as each alone");
}

static const char *key(const char *k)
{
    return ico_config_get_string(k, "(absent)");
}

static void testConfig(void)
{
    char p[1024];
    writeConfig("appearance_cfg.toml", "version = 1\n");
    ico_appearance_set(ICO_APP_ICO_TUNIC, 3);
    ico_appearance_set(ICO_APP_ICO_SKIN, 5);
    ico_appearance_set(ICO_APP_YORDA_DRESS, 24);
    ico_appearance_set(ICO_APP_ICO_SHORTS, 99); /* out of range: Original */
    CHECK(strcmp(key("characters.ico_tunic"), "rose") == 0, "tunic: %s",
          key("characters.ico_tunic"));
    CHECK(strcmp(key("characters.ico_skin"), "tone5") == 0, "skin: %s", key("characters.ico_skin"));
    CHECK(strcmp(key("characters.yorda_dress"), "black") == 0, "dress: %s",
          key("characters.yorda_dress"));
    CHECK(strcmp(key("characters.ico_shorts"), "original") == 0 &&
              ico_appearance_get(ICO_APP_ICO_SHORTS) == 0,
          "out of range: original");
    ico_appearance_reload();
    CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 3 && ico_appearance_get(ICO_APP_ICO_SKIN) == 5 &&
              ico_appearance_get(ICO_APP_YORDA_DRESS) == 24,
          "reload: the values from the strings");
    for (int v = 1; v <= ICO_APP_COLOURS; v++) {
        ico_appearance_set(ICO_APP_ICO_PONCHO_PINK, v);
        CHECK(strcmp(key("characters.ico_poncho_pink"), kPal[v - 1].name) == 0, "value %d: %s", v,
              key("characters.ico_poncho_pink"));
    }
    /* saved and read back from the file */
    CHECK(ico_config_save() == 0, "save");
    snprintf(p, sizeof(p), "%s/appearance_cfg.toml", s_dir);
    ico_config_reset(p, "/nonexistent/appearance_test.ini");
    ico_appearance_reload();
    CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 3 && ico_appearance_get(ICO_APP_ICO_SKIN) == 5 &&
              ico_appearance_get(ICO_APP_YORDA_DRESS) == 24 &&
              ico_appearance_get(ICO_APP_ICO_PONCHO_PINK) == 24,
          "the saved file read back");

    /* unknown values: Original; spelling is case-blind */
    writeConfig("appearance_bad.toml", "version = 1\n[characters]\n"
                                       "ico_tunic = \"chartreuse\"\n"
                                       "ico_skin = \"red\"\n"
                                       "yorda_dress = \"tone3\"\n"
                                       "yorda_skin = \"tone13\"\n"
                                       "ico_shorts = \"Navy\"\n"
                                       "ico_poncho_dark = \"TONE2\"\n"
                                       "ico_poncho_navy = \"Original\"\n");
    CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 0 && ico_appearance_get(ICO_APP_ICO_SKIN) == 0 &&
              ico_appearance_get(ICO_APP_YORDA_DRESS) == 0 &&
              ico_appearance_get(ICO_APP_YORDA_SKIN) == 0 &&
              ico_appearance_get(ICO_APP_ICO_PONCHO_DARK) == 0 &&
              ico_appearance_get(ICO_APP_ICO_PONCHO_NAVY) == 0,
          "unknown values: Original");
    CHECK(ico_appearance_get(ICO_APP_ICO_SHORTS) == 9, "\"Navy\": navy (%d)",
          ico_appearance_get(ICO_APP_ICO_SHORTS));
    writeConfig("appearance_tone.toml",
                "version = 1\n[characters]\nyorda_skin = \"tone12\"\nico_skin = \"tone1\"\n");
    CHECK(ico_appearance_get(ICO_APP_YORDA_SKIN) == 12 && ico_appearance_get(ICO_APP_ICO_SKIN) == 1,
          "tones");
}

static void testRandomize(void)
{
    int a[ICO_APP_PART_COUNT];
    writeConfig("appearance_rand.toml", "version = 1\n");
    ico_appearance_randomize(1234);
    for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
        a[p] = ico_appearance_get((IcoAppPart)p);
    }
    ico_appearance_reset();
    ico_appearance_randomize(1234);
    int same = 1;
    for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
        same &= ico_appearance_get((IcoAppPart)p) == a[p];
    }
    CHECK(same, "the same seed, the same colours");
    ico_appearance_reload();
    same = 1;
    for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
        same &= ico_appearance_get((IcoAppPart)p) == a[p];
    }
    CHECK(same, "randomize wrote the keys");
    int differ = 0;
    for (unsigned int seed = 0; seed < 300; seed++) {
        unsigned int s0 = ico_appearance_serial();
        ico_appearance_randomize(seed);
        CHECK(ico_appearance_serial() == s0 + 1, "seed %u: one serial step", seed);
        for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
            int v = ico_appearance_get((IcoAppPart)p);
            CHECK(v >= 1 && v < ico_appearance_choices((IcoAppPart)p), "seed %u part %d: %d", seed,
                  p, v);
            differ |= seed == 7 && v != a[p];
        }
        for (int p = ICO_APP_ICO_PONCHO_NAVY; p <= ICO_APP_ICO_PONCHO_DARK; p++) {
            for (int q = p + 1; q <= ICO_APP_ICO_PONCHO_DARK; q++) {
                CHECK(ico_appearance_get((IcoAppPart)p) != ico_appearance_get((IcoAppPart)q),
                      "seed %u: poncho groups %d and %d differ", seed, p, q);
            }
        }
    }
    CHECK(differ, "another seed, other colours");
}

static void testReset(void)
{
    writeConfig("appearance_reset.toml", "version = 1\n");
    ico_appearance_randomize(99);
    unsigned int s0 = ico_appearance_serial();
    ico_appearance_reset();
    CHECK(ico_appearance_serial() == s0 + 1, "reset: one serial step");
    for (int p = 0; p < ICO_APP_PART_COUNT; p++) {
        CHECK(ico_appearance_get((IcoAppPart)p) == 0, "reset: part %d Original", p);
    }
    CHECK(strcmp(key("characters.ico_poncho_light"), "original") == 0 &&
              strcmp(key("characters.yorda_skin"), "original") == 0,
          "reset: the keys");
    s0 = ico_appearance_serial();
    ico_appearance_reset();
    CHECK(ico_appearance_serial() == s0, "reset again: no change, no step");
    ico_appearance_set(ICO_APP_ICO_TUNIC, 0);
    CHECK(ico_appearance_serial() == s0, "set to the same value: no step");
    ico_appearance_set(ICO_APP_ICO_TUNIC, 2);
    CHECK(ico_appearance_serial() == s0 + 1, "set: one step");
    s0 = ico_appearance_serial();
    ico_appearance_reload();
    CHECK(ico_appearance_serial() != s0, "reload: a step");
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    testIdentity();
    testCovers();
    testMasks();
    testAlpha();
    testFormats();
    testAnchor();
    testGrey();
    testWeights();
    testConfig();
    testRandomize();
    testReset();
    if (failures != 0) {
        printf("appearance_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("appearance_test: ok\n");
    return 0;
}
