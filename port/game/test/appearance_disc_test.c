/* appearance_disc_test.c: the characters' colours (port/game/appearance.c)
 * against the disc's own CLUTs.  argv[1] is the PAL image; without it the
 * test is skipped (77).  Nothing read here is written anywhere.
 *
 *   textures   each covered texture is a pack member, PSMT4 or PSMT8 with
 *              a 32-bit CLUT
 *   anchors    each part's anchor (its Original swatch) is within 3 of an
 *              entry of the texture it was taken from
 *   poncho     b_mantle's four groups, found by recolouring the disc's CLUT
 *              one group at a time, hold about 18 / 26 / 34 / 22 % of its
 *              texels (within 5 points), each entry in one group
 *   recolour   every covered texture recolours with every part set, the
 *              alpha bytes kept
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appearance.h"
#include "config.h"
#include "df_pack.h"
#include "rd_tex.h"
#include "vfs.h"

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

static const char *kTex[] = {"b_arm",   "b_face2",  "b_head_top", "b_mantle",   "b_suit",
                             "b_pants", "hada_red", "hada_red2",  "facft05_36", "fuku22",
                             "fuku03",  "fuku04",   "fuku06",     "poncho005"};
#define TEX_COUNT ((int)(sizeof(kTex) / sizeof(kTex[0])))

typedef struct Tim {
    uint8_t *file;
    const uint8_t *image;
    const uint8_t *clut;
    uint32_t colors;
    uint32_t type; /* 4 PSMT4, 5 PSMT8 */
    uint32_t clutType;
    uint32_t w, h;
} Tim;

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

static int lowerc(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* the member's base name without ".tm2" is name */
static int isTexture(const char *member, const char *name)
{
    const char *b = member;
    size_t n = strlen(name);
    for (const char *p = member; *p; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (lowerc((unsigned char)b[i]) != name[i]) {
            return 0;
        }
    }
    return lowerc((unsigned char)b[n]) == '.' && lowerc((unsigned char)b[n + 1]) == 't' &&
           lowerc((unsigned char)b[n + 2]) == 'm' && b[n + 3] == '2' && b[n + 4] == 0;
}

static int loadTim(IcoVfs *vfs, const char *name, Tim *t)
{
    int members = ico_df_index_members();
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < members; i++) {
        IcoDfMember m;
        const char *mn = ico_df_member_name(i);
        if (mn == NULL || !isTexture(mn, name) || ico_df_member(i, &m) != 0) {
            continue;
        }
        t->file = malloc(m.size ? m.size : 1);
        if (t->file == NULL || ico_df_read_member(vfs, &m, t->file) != 0 || m.size < 16 + 0x30 ||
            memcmp(t->file, "TIM2", 4) != 0) {
            free(t->file);
            t->file = NULL;
            return -1;
        }
        const uint8_t *pic = t->file + (t->file[5] != 0 ? 128 : 16);
        uint32_t clutSize = rd32(pic + 0x04);
        uint32_t imageSize = rd32(pic + 0x08);
        uint32_t headerSize = rd16(pic + 0x0C);
        t->colors = rd16(pic + 0x0E);
        t->clutType = pic[0x12];
        t->type = pic[0x13];
        t->w = rd16(pic + 0x14);
        t->h = rd16(pic + 0x16);
        t->image = pic + headerSize;
        t->clut = t->image + imageSize;
        if ((size_t)(t->clut + clutSize - t->file) > m.size || clutSize < t->colors * 4) {
            free(t->file);
            t->file = NULL;
            return -1;
        }
        return 0;
    }
    return -1;
}

/* texels per CLUT index (level 0) */
static void histogram(const Tim *t, double *share, uint32_t n)
{
    uint32_t texels = t->w * t->h;
    uint32_t count[256] = {0};
    for (uint32_t i = 0; i < texels; i++) {
        uint32_t v = t->type == 4 ? (t->image[i >> 1] >> ((i & 1) * 4)) & 15 : t->image[i];
        count[v & 255]++;
    }
    for (uint32_t i = 0; i < n; i++) {
        share[i] = texels ? (double)count[i] / texels : 0.0;
    }
}

static int within(const uint8_t *c, unsigned int rgb, int tol)
{
    return abs(c[0] - (int)(rgb >> 16)) <= tol && abs(c[1] - (int)((rgb >> 8) & 0xFF)) <= tol &&
           abs(c[2] - (int)(rgb & 0xFF)) <= tol;
}

int main(int argc, char **argv)
{
    static const struct {
        IcoAppPart part;
        const char *tex;
    } kAnchor[ICO_APP_PART_COUNT] = {
        {ICO_APP_ICO_SKIN, "b_face2"},         {ICO_APP_ICO_PONCHO_NAVY, "b_mantle"},
        {ICO_APP_ICO_PONCHO_PINK, "b_mantle"}, {ICO_APP_ICO_PONCHO_LIGHT, "b_mantle"},
        {ICO_APP_ICO_PONCHO_DARK, "b_mantle"}, {ICO_APP_ICO_TUNIC, "b_suit"},
        {ICO_APP_ICO_SHORTS, "b_pants"},       {ICO_APP_YORDA_SKIN, "hada_red"},
        {ICO_APP_YORDA_DRESS, "fuku04"},
    };

    static const IcoAppPart kFam[4] = {ICO_APP_ICO_PONCHO_NAVY, ICO_APP_ICO_PONCHO_PINK,
                                       ICO_APP_ICO_PONCHO_LIGHT, ICO_APP_ICO_PONCHO_DARK};
    static const double kShare[4] = {0.18, 0.26, 0.34, 0.22};
    const char *disc = argc > 1 ? argv[1] : NULL;
    FILE *probe = disc ? fopen(disc, "rb") : NULL;
    Tim tim[TEX_COUNT];
    unsigned char out[1024];

    if (probe == NULL) {
        printf("appearance_disc_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("appearance_disc_test: SKIP (%s is not a readable disc image)\n", disc);
        return 77;
    }
    ico_config_reset("/nonexistent/appearance_disc_test.toml",
                     "/nonexistent/appearance_disc_test.ini");
    ico_appearance_reload();
    IcoDfMember m;
    ico_df_find_member(vfs, "", &m); /* builds the index */
    CHECK(ico_df_index_members() > 0, "DATA.DF has members");

    for (int i = 0; i < TEX_COUNT; i++) {
        int ok = loadTim(vfs, kTex[i], &tim[i]) == 0;
        CHECK(ok, "%s is on the disc", kTex[i]);
        if (ok) {
            CHECK((tim[i].type == 4 && tim[i].colors == 16) ||
                      (tim[i].type == 5 && tim[i].colors == 256),
                  "%s: PSMT4/16 or PSMT8/256 (type %u, %u colours)", kTex[i], tim[i].type,
                  tim[i].colors);
            CHECK((tim[i].clutType & 0x3F) == 3, "%s: a 32-bit CLUT (type 0x%x)", kTex[i],
                  tim[i].clutType);
            CHECK(ico_appearance_covers(kTex[i]), "%s covered", kTex[i]);
        }
    }

    /* anchors */
    for (int k = 0; k < ICO_APP_PART_COUNT; k++) {
        unsigned int a = ico_appearance_swatch(kAnchor[k].part);
        int found = 0;
        for (int i = 0; i < TEX_COUNT; i++) {
            if (tim[i].file == NULL || strcmp(kTex[i], kAnchor[k].tex) != 0) {
                continue;
            }
            for (uint32_t e = 0; e < tim[i].colors; e++) {
                found |= within(tim[i].clut + e * 4, a, 3);
            }
        }
        CHECK(found, "part %d: the anchor %06x is in %s", (int)kAnchor[k].part, a, kAnchor[k].tex);
    }

    /* the poncho's groups */
    for (int i = 0; i < TEX_COUNT; i++) {
        if (tim[i].file == NULL || strcmp(kTex[i], "b_mantle") != 0 || tim[i].colors != 16) {
            continue;
        }
        double share[16];
        unsigned int seen = 0;
        histogram(&tim[i], share, 16);
        for (int f = 0; f < 4; f++) {
            unsigned int set = 0;
            double sum = 0.0;
            ico_appearance_reset();
            ico_appearance_set(kFam[f], 14); /* green */
            CHECK(ico_appearance_recolour("b_mantle", tim[i].clut, 16, RDTEX_PSMCT32, out) == 1,
                  "b_mantle recolours");
            for (int e = 0; e < 16; e++) {
                if (memcmp(out + e * 4, tim[i].clut + e * 4, 3) != 0) {
                    set |= 1u << e;
                    sum += share[e];
                }
            }
            printf("b_mantle group %d: entries 0x%04x, %.1f %% of the texels\n", f, set,
                   sum * 100.0);
            CHECK(sum > kShare[f] - 0.05 && sum < kShare[f] + 0.05,
                  "group %d: %.1f %% (about %.0f %%)", f, sum * 100.0, kShare[f] * 100.0);
            CHECK((set & seen) == 0, "group %d: its own entries", f);
            seen |= set;
        }
        CHECK(seen == 0xFFFF, "the groups cover b_mantle's 16 entries (0x%04x)", seen);
    }

    /* every texture with every part set: written, alpha kept */
    ico_appearance_randomize(4242);
    for (int i = 0; i < TEX_COUNT; i++) {
        if (tim[i].file == NULL) {
            continue;
        }
        int r = ico_appearance_recolour(kTex[i], tim[i].clut, tim[i].colors, RDTEX_PSMCT32, out);
        int alpha = 1;
        for (uint32_t e = 0; r && e < tim[i].colors; e++) {
            alpha &= out[e * 4 + 3] == tim[i].clut[e * 4 + 3];
        }
        CHECK(r == 1 && alpha, "%s recolours, alpha kept", kTex[i]);
    }
    ico_appearance_reset();

    for (int i = 0; i < TEX_COUNT; i++) {
        free(tim[i].file);
    }
    ico_vfs_unmount(vfs);
    if (failures != 0) {
        printf("appearance_disc_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("appearance_disc_test: ok\n");
    return 0;
}
