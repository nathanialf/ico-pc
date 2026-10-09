/*
 * port/game/appearance.c
 *
 * The characters' colours (appearance.h).
 *
 * The rule table names, per texture, the part it follows, the CLUT
 * entries that belong to the part (a 16-bit mask in index order) and
 * optional soft limits on the entry's saturation and lightness.  The
 * groupings were read from the disc's palettes (the character TIM2s of
 * COMMON.DF: dfdatas/common/object/sdf/{boy,girl}/texture); no colour or
 * pixel of the disc is kept here (docs/LEGAL.md): the comments name each
 * part's anchor (its most common colour, the one the target lands on) and
 * the families only.
 *
 * Recolour, per entry e of a rule whose part is not Original, in HSL with
 * the part's anchor a and target T:
 *   H' = H_T
 *   S' = clamp(S_T * (S_e + 0.10) / (S_a + 0.10))
 *   L' piecewise linear through (0, 0), (L_a, L_T), (1, 1)
 * weighted: out = orig + sum over rules of w * (hsl2rgb(H', S', L') - orig),
 * w = mask bit * smoothstep(sLo, sHi, S_e) * smoothstep(lLo, lHi, L_e)
 * (1 - the soft limits when inverted).  Alpha is never written.
 */
#include "appearance.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "rd_tex.h"

/* --------------------------------------------------------------- values */

typedef struct AppHsl {
    double h; /* degrees */
    double s; /* 0..1 */
    double l; /* 0..1 */
} AppHsl;

/* the clothing palette, value order (appearance.h); the names are the
   config's values */
static const struct {
    const char *name;
    short h, s, l;
} kPalette[ICO_APP_COLOURS] = {
    {"red", 0, 70, 45},      {"crimson", 350, 65, 35}, {"rose", 340, 55, 65},
    {"pink", 330, 60, 75},   {"magenta", 310, 55, 45}, {"plum", 290, 35, 30},
    {"violet", 270, 45, 50}, {"indigo", 245, 45, 30},  {"navy", 225, 45, 20},
    {"blue", 215, 65, 45},   {"sky", 200, 60, 70},     {"teal", 180, 50, 32},
    {"cyan", 185, 55, 55},   {"green", 130, 45, 35},   {"moss", 90, 35, 32},
    {"olive", 65, 40, 35},   {"gold", 45, 70, 55},     {"orange", 28, 80, 52},
    {"rust", 15, 60, 35},    {"brown", 25, 40, 25},    {"sand", 35, 35, 70},
    {"white", 30, 10, 92},   {"grey", 0, 0, 55},       {"black", 0, 0, 8},
};

/* the parts: config key, skin or not, character, anchor (0xRRGGBB) */
static const struct {
    const char *key;
    unsigned char skin;
    unsigned char character;
    unsigned int anchor;
} kParts[ICO_APP_PART_COUNT] = {
    /* Ico's skin: b_face2's main skin entry, b_arm's arm skin within 3 */
    {"characters.ico_skin", 1, 0, 0x976E41},
    /* b_mantle's four groups, each its own most common entry */
    {"characters.ico_poncho_navy", 0, 0, 0x282F41},
    {"characters.ico_poncho_pink", 0, 0, 0xD3A1BC},
    {"characters.ico_poncho_light", 0, 0, 0xD1C2B7},
    {"characters.ico_poncho_dark", 0, 0, 0x232323},
    /* b_suit's and b_pants's most common entries */
    {"characters.ico_tunic", 0, 0, 0x7C3723},
    {"characters.ico_shorts", 0, 0, 0xC7AC93},
    /* the skin of hada_red / hada_red2 / fuku22, the cloth of fuku03 /
       fuku04 / fuku22 / poncho005 */
    {"characters.yorda_skin", 1, 1, 0x8F7B6D},
    {"characters.yorda_dress", 0, 1, 0x9E9893},
};

/* -1: not read yet */
static signed char s_value[ICO_APP_PART_COUNT];
static int s_loaded;
static unsigned int s_serial;
static unsigned int s_warned; /* a bit per part: an unknown value logged */

static int partOk(IcoAppPart p)
{
    return (int)p >= 0 && (int)p < ICO_APP_PART_COUNT;
}

int ico_appearance_is_skin(IcoAppPart p)
{
    return partOk(p) && kParts[p].skin;
}

int ico_appearance_character(IcoAppPart p)
{
    return partOk(p) ? kParts[p].character : 0;
}

int ico_appearance_choices(IcoAppPart p)
{
    if (!partOk(p)) {
        return 0;
    }
    return 1 + (kParts[p].skin ? ICO_APP_TONES : 0) + ICO_APP_COLOURS;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int sameName(const char *a, const char *b)
{
    while (*a != 0 && *b != 0 && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

/* the palette index of a value, -1 for Original or a skin tone */
static int paletteIndex(IcoAppPart p, int v)
{
    const int first = kParts[p].skin ? 1 + ICO_APP_TONES : 1;

    return v >= first && v < ico_appearance_choices(p) ? v - first : -1;
}

/* the config string of a value */
static void valueName(IcoAppPart p, int v, char *buf, size_t n)
{
    if (v <= 0 || v >= ico_appearance_choices(p)) {
        snprintf(buf, n, "original");
    } else if (paletteIndex(p, v) >= 0) {
        snprintf(buf, n, "%s", kPalette[paletteIndex(p, v)].name);
    } else {
        snprintf(buf, n, "tone%d", v);
    }
}

/* a config string to a value, -1 when the part has no such value */
static int parseValue(IcoAppPart p, const char *s)
{
    int i;

    if (s == NULL || s[0] == 0 || sameName(s, "original")) {
        return 0;
    }
    if (kParts[p].skin) {
        if (lower((unsigned char)s[0]) == 't' && lower((unsigned char)s[1]) == 'o' &&
            lower((unsigned char)s[2]) == 'n' && lower((unsigned char)s[3]) == 'e') {
            int v = 0;

            for (i = 4; s[i] >= '0' && s[i] <= '9' && v < 100; i++) {
                v = v * 10 + (s[i] - '0');
            }
            if (i > 4 && s[i] == 0 && v >= 1 && v <= ICO_APP_TONES) {
                return v;
            }
            return -1;
        }
    }
    for (i = 0; i < ICO_APP_COLOURS; i++) {
        if (sameName(s, kPalette[i].name)) {
            return (kParts[p].skin ? 1 + ICO_APP_TONES : 1) + i;
        }
    }
    return -1;
}

static void load(void)
{
    int p;

    if (s_loaded) {
        return;
    }
    for (p = 0; p < ICO_APP_PART_COUNT; p++) {
        const char *s = ico_config_get_string(kParts[p].key, "original");
        int v = parseValue((IcoAppPart)p, s);

        if (v < 0) {
            if (!(s_warned & (1u << p))) {
                s_warned |= 1u << p;
                fprintf(stderr, "appearance: [characters] %s = \"%s\" is not a value; Original\n",
                        kParts[p].key + strlen("characters."), s);
            }
            v = 0;
        }
        s_value[p] = (signed char)v;
    }
    s_loaded = 1;
}

int ico_appearance_get(IcoAppPart p)
{
    if (!partOk(p)) {
        return 0;
    }
    load();
    return s_value[p];
}

static void store(IcoAppPart p, int v)
{
    char buf[16];

    s_value[p] = (signed char)v;
    valueName(p, v, buf, sizeof(buf));
    if (ico_config_set_string(kParts[p].key, buf) != 0) {
        fprintf(stderr, "appearance: cannot set [characters] %s\n",
                kParts[p].key + strlen("characters."));
    }
}

void ico_appearance_set(IcoAppPart p, int value)
{
    if (!partOk(p)) {
        return;
    }
    load();
    if (value < 0 || value >= ico_appearance_choices(p)) {
        value = 0;
    }
    if (s_value[p] != value) {
        s_serial++;
    }
    store(p, value);
}

/* xorshift32 (Marsaglia); never 0 from a non-zero state */
static unsigned int xorshift32(unsigned int *x)
{
    unsigned int v = *x;

    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    *x = v;
    return v;
}

/* randomize / reset over every part (character -1) or one character's */
static int inScope(int p, int character)
{
    return character < 0 || kParts[p].character == character;
}

static void randomizeScope(unsigned int seed, int character)
{
    unsigned int x = seed != 0 ? seed : 0x9E3779B9u;
    int v[ICO_APP_PART_COUNT];
    int p;

    load();
    for (p = 0; p < ICO_APP_PART_COUNT; p++) {
        int n = ico_appearance_choices((IcoAppPart)p) - 1;

        /* every part draws, in or out of the scope, so a part's colour
           from a seed is the same either way */
        for (;;) {
            int q;
            int clash = 0;

            v[p] = 1 + (int)(xorshift32(&x) % (unsigned int)n);
            /* the poncho's four groups all different, or the pattern goes */
            if (p > ICO_APP_ICO_PONCHO_NAVY && p <= ICO_APP_ICO_PONCHO_DARK) {
                for (q = ICO_APP_ICO_PONCHO_NAVY; q < p; q++) {
                    clash |= v[q] == v[p];
                }
            }
            if (!clash) {
                break;
            }
        }
    }
    for (p = 0; p < ICO_APP_PART_COUNT; p++) {
        if (inScope(p, character)) {
            store((IcoAppPart)p, v[p]);
        }
    }
    s_serial++;
    if (character < 0) {
        fprintf(stderr, "appearance: randomize (seed %u)\n", seed);
    } else {
        fprintf(stderr, "appearance: randomize %s (seed %u)\n", character == 1 ? "yorda" : "ico",
                seed);
    }
}

void ico_appearance_randomize(unsigned int seed)
{
    randomizeScope(seed, -1);
}

void ico_appearance_randomize_character(int character, unsigned int seed)
{
    if (character == 0 || character == 1) {
        randomizeScope(seed, character);
    }
}

static void resetScope(int character)
{
    int changed = 0;
    int p;

    load();
    for (p = 0; p < ICO_APP_PART_COUNT; p++) {
        if (inScope(p, character)) {
            changed |= s_value[p] != 0;
            store((IcoAppPart)p, 0);
        }
    }
    if (changed) {
        s_serial++;
    }
}

void ico_appearance_reset(void)
{
    resetScope(-1);
}

void ico_appearance_reset_character(int character)
{
    if (character == 0 || character == 1) {
        resetScope(character);
    }
}

void ico_appearance_reload(void)
{
    s_loaded = 0;
    s_serial++;
}

unsigned int ico_appearance_serial(void)
{
    return s_serial;
}

/* ---------------------------------------------------------------- rules */

#define ALL 0xFFFFu

typedef struct AppRule {
    const char *tex;      /* the game's trimmed texture name */
    unsigned char part;   /* IcoAppPart */
    unsigned char invert; /* w = mask * (1 - limits) */
    unsigned short mask;  /* entries in index order; ALL: every entry (256 too) */
    float sLo, sHi;       /* smoothstep on S; sLo == sHi == 0: none */
    float lLo, lHi;       /* smoothstep on L; none as above */
} AppRule;

static const AppRule kRules[] = {
    /* Ico's skin.  b_arm (arms and legs; the body object lists b_arm, not a
       leg texture): the skin is one entry (index 1, the bulk); a grey-green
       wrap (8-11) and tan stripe columns (2-5) fade out by saturation.
       Uncertain: 2-8 are partly skin by the limit (a seam is possible on
       the stripes). */
    {"b_arm", ICO_APP_ICO_SKIN, 0, ALL, 0.20f, 0.35f, 0.0f, 0.0f},
    /* b_face2: dark hair entries 0-5, skin 6-15, split by lightness.
       Uncertain: 4-5 (dark skin or light hair) take part weights. */
    {"b_face2", ICO_APP_ICO_SKIN, 0, ALL, 0.0f, 0.0f, 0.22f, 0.34f},
    /* b_head_top: the hair-to-skin gradient at the hairline, split as
       b_face2 so the seam matches */
    {"b_head_top", ICO_APP_ICO_SKIN, 0, ALL, 0.0f, 0.0f, 0.22f, 0.34f},
    /* b_mantle (the poncho pattern and the tape belt, one CLUT): the four
       masks cover the 16 entries once each.  Navy: the single dark blue
       entry (2).  Pink: the pinks and the dusty rose (5, 7, 10, 11).
       Light: the creams, peaches and light greys (6, 8, 9, 12-15).  Dark:
       the black, the near-black and the dark mauve greys (0, 1, 3, 4).
       Uncertain: 4 (a dark mauve grey between dark and pink). */
    {"b_mantle", ICO_APP_ICO_PONCHO_NAVY, 0, 0x0004u, 0.0f, 0.0f, 0.0f, 0.0f},
    {"b_mantle", ICO_APP_ICO_PONCHO_PINK, 0, 0x0CA0u, 0.0f, 0.0f, 0.0f, 0.0f},
    {"b_mantle", ICO_APP_ICO_PONCHO_LIGHT, 0, 0xF340u, 0.0f, 0.0f, 0.0f, 0.0f},
    {"b_mantle", ICO_APP_ICO_PONCHO_DARK, 0, 0x001Bu, 0.0f, 0.0f, 0.0f, 0.0f},
    /* the tunic: b_suit's browns, all entries */
    {"b_suit", ICO_APP_ICO_TUNIC, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    /* the shorts: b_pants's tans, all entries */
    {"b_pants", ICO_APP_ICO_SHORTS, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    /* Yorda's skin: hada_red and hada_red2 (256 colours) all skin */
    {"hada_red", ICO_APP_YORDA_SKIN, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    {"hada_red2", ICO_APP_YORDA_SKIN, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    /* facft05_36: the face's skin (S about 0.12) above the near-grey hair
       and shadow entries (S 0.03 and below) and the white (15).
       Uncertain: 4-5 (S 0.06-0.08) take part weights. */
    {"facft05_36", ICO_APP_YORDA_SKIN, 0, ALL, 0.06f, 0.10f, 0.0f, 0.0f},
    /* fuku22 (256 colours) holds skin (S 0.12-0.16) and the dress (S
       0.03-0.06) in one CLUT: the same limit, the dress's inverted, so the
       two weights sum to 1 */
    {"fuku22", ICO_APP_YORDA_SKIN, 0, ALL, 0.06f, 0.10f, 0.0f, 0.0f},
    {"fuku22", ICO_APP_YORDA_DRESS, 1, ALL, 0.06f, 0.10f, 0.0f, 0.0f},
    /* the dress: fuku03 (the skirt's cloth), fuku04, fuku06, all entries
       (the off-white cloth and, in fuku04, its shadow ramp) */
    {"fuku03", ICO_APP_YORDA_DRESS, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    {"fuku04", ICO_APP_YORDA_DRESS, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    {"fuku06", ICO_APP_YORDA_DRESS, 0, ALL, 0.0f, 0.0f, 0.0f, 0.0f},
    /* poncho005 (a cloth piece): the light cloth entries (L 0.45 and up),
       not the dark browns below.  Uncertain: the whole grouping (the dark
       entries may be cloth in shadow). */
    {"poncho005", ICO_APP_YORDA_DRESS, 0, ALL, 0.0f, 0.0f, 0.36f, 0.50f},
};

#define RULE_COUNT ((int)(sizeof(kRules) / sizeof(kRules[0])))

int ico_appearance_covers(const char *texName)
{
    int i;

    if (texName == NULL) {
        return 0;
    }
    for (i = 0; i < RULE_COUNT; i++) {
        if (sameName(texName, kRules[i].tex)) {
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- maths */

static double clamp01(double x)
{
    return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x;
}

static double smooth(float lo, float hi, double x)
{
    double t;

    if (hi <= lo) {
        return 1.0;
    }
    t = clamp01((x - lo) / (hi - lo));
    return t * t * (3.0 - 2.0 * t);
}

static AppHsl rgbToHsl(double r, double g, double b)
{
    AppHsl o;
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double d = mx - mn;

    o.l = (mx + mn) * 0.5;
    if (d <= 0.0) {
        o.h = 0.0;
        o.s = 0.0;
        return o;
    }
    o.s = o.l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == r) {
        o.h = (g - b) / d + (g < b ? 6.0 : 0.0);
    } else if (mx == g) {
        o.h = (b - r) / d + 2.0;
    } else {
        o.h = (r - g) / d + 4.0;
    }
    o.h *= 60.0;
    return o;
}

static double hueToRgb(double p, double q, double t)
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

static void hslToRgb(AppHsl c, double rgb[3])
{
    double h = c.h / 360.0;
    double q;
    double p;

    while (h < 0.0) {
        h += 1.0;
    }
    while (h >= 1.0) {
        h -= 1.0;
    }
    if (c.s <= 0.0) {
        rgb[0] = rgb[1] = rgb[2] = c.l;
        return;
    }
    q = c.l < 0.5 ? c.l * (1.0 + c.s) : c.l + c.s - c.l * c.s;
    p = 2.0 * c.l - q;
    rgb[0] = hueToRgb(p, q, h + 1.0 / 3.0);
    rgb[1] = hueToRgb(p, q, h);
    rgb[2] = hueToRgb(p, q, h - 1.0 / 3.0);
}

/* the part's target, or 0 for Original */
static int target(IcoAppPart p, AppHsl *t)
{
    int v = ico_appearance_get(p);

    if (v <= 0) {
        return 0;
    }
    if (paletteIndex(p, v) >= 0) {
        /* a named colour; for a skin part through the skin's rules (their
           soft limits keep the hair and the cloth beside it) as a tone */
        t->h = kPalette[paletteIndex(p, v)].h;
        t->s = kPalette[paletteIndex(p, v)].s / 100.0;
        t->l = kPalette[paletteIndex(p, v)].l / 100.0;
    } else {
        double k = (double)(v - 1) / (double)(ICO_APP_TONES - 1);

        t->h = 30.0 + (16.0 - 30.0) * k;
        t->s = (45.0 + (34.0 - 45.0) * k) / 100.0;
        t->l = (85.0 + (20.0 - 85.0) * k) / 100.0;
    }
    return 1;
}

static AppHsl anchorHsl(IcoAppPart p)
{
    unsigned int a = kParts[p].anchor;

    return rgbToHsl((double)((a >> 16) & 0xFF) / 255.0, (double)((a >> 8) & 0xFF) / 255.0,
                    (double)(a & 0xFF) / 255.0);
}

/* entry e (HSL) through the part's mapping: anchor a lands on target t */
static void mapEntry(AppHsl e, AppHsl a, AppHsl t, double rgb[3])
{
    AppHsl o;

    o.h = t.h;
    o.s = clamp01(t.s * (e.s + 0.10) / (a.s + 0.10));
    if (a.l <= 0.0) {
        o.l = t.l + e.l * (1.0 - t.l);
    } else if (a.l >= 1.0 || e.l <= a.l) {
        o.l = e.l * t.l / (a.l > 0.0 ? a.l : 1.0);
    } else {
        o.l = t.l + (e.l - a.l) * (1.0 - t.l) / (1.0 - a.l);
    }
    o.l = clamp01(o.l);
    hslToRgb(o, rgb);
}

static unsigned char toByte(double x)
{
    x = x * 255.0 + 0.5;
    return (unsigned char)(x < 0.0 ? 0 : x > 255.0 ? 255 : (int)x);
}

unsigned int ico_appearance_swatch(IcoAppPart p)
{
    AppHsl t;
    double rgb[3];

    if (!partOk(p)) {
        return 0;
    }
    if (!target(p, &t)) {
        return kParts[p].anchor;
    }
    mapEntry(anchorHsl(p), anchorHsl(p), t, rgb);
    return (unsigned int)toByte(rgb[0]) << 16 | (unsigned int)toByte(rgb[1]) << 8 |
           (unsigned int)toByte(rgb[2]);
}

int ico_appearance_recolour(const char *texName, const void *clut, unsigned int colors,
                            unsigned int cpsm, void *out)
{
    const unsigned char *in = clut;
    unsigned char *o = out;
    AppHsl tgt[ICO_APP_PART_COUNT];
    AppHsl anc[ICO_APP_PART_COUNT];
    int on[ICO_APP_PART_COUNT];
    int any = 0;
    int r0 = -1;
    int r1 = -1;
    int i;
    unsigned int pos;

    if (texName == NULL || clut == NULL || out == NULL || cpsm != RDTEX_PSMCT32 || colors < 1 ||
        colors > 256) {
        return 0;
    }
    for (i = 0; i < ICO_APP_PART_COUNT; i++) {
        on[i] = target((IcoAppPart)i, &tgt[i]);
        if (on[i]) {
            anc[i] = anchorHsl((IcoAppPart)i);
            any = 1;
        }
    }
    if (!any) {
        return 0;
    }
    /* the texture's rules, consecutive in the table; any of them on */
    any = 0;
    for (i = 0; i < RULE_COUNT; i++) {
        if (sameName(texName, kRules[i].tex)) {
            if (r0 < 0) {
                r0 = i;
            }
            r1 = i;
            any |= on[kRules[i].part];
        } else if (r0 >= 0) {
            break;
        }
    }
    if (!any) {
        return 0;
    }
    memcpy(o, in, (size_t)colors * 4);
    for (pos = 0; pos < colors; pos++) {
        /* rdtex_Csm1Index is its own inverse: memory position -> index */
        unsigned int idx = rdtex_Csm1Index(pos, colors);
        const unsigned char *c = in + pos * 4;
        double orig[3];
        double mix[3] = {0.0, 0.0, 0.0};
        double wsum = 0.0;
        AppHsl e;
        int k;

        orig[0] = c[0] / 255.0;
        orig[1] = c[1] / 255.0;
        orig[2] = c[2] / 255.0;
        e = rgbToHsl(orig[0], orig[1], orig[2]);
        for (k = r0; k <= r1; k++) {
            const AppRule *r = &kRules[k];
            double w;
            double rgb[3];
            int bit;

            if (!on[r->part]) {
                continue;
            }
            bit = r->mask == ALL || (idx < 16 && ((r->mask >> idx) & 1u));
            if (!bit) {
                continue;
            }
            w = smooth(r->sLo, r->sHi, e.s) * smooth(r->lLo, r->lHi, e.l);
            if (r->invert) {
                w = 1.0 - w;
            }
            if (w <= 0.0) {
                continue;
            }
            mapEntry(e, anc[r->part], tgt[r->part], rgb);
            mix[0] += w * rgb[0];
            mix[1] += w * rgb[1];
            mix[2] += w * rgb[2];
            wsum += w;
        }
        /* orig + sum w * (new - orig), spelled so that a single full
           weight gives the new colour exactly (a neutral target R = G = B) */
        o[pos * 4 + 0] = toByte(orig[0] * (1.0 - wsum) + mix[0]);
        o[pos * 4 + 1] = toByte(orig[1] * (1.0 - wsum) + mix[1]);
        o[pos * 4 + 2] = toByte(orig[2] * (1.0 - wsum) + mix[2]);
        /* o[pos * 4 + 3]: the alpha, copied */
    }
    return 1;
}
