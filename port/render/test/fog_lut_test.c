/* fog_lut_test.c: the depth fog's game side, the same on every compiler.
 *
 * fog_MakeFogClut and fog_DrawFog (ico2/seki/src/ZFog.c) as the window
 * build has them (ICO_HOST, ICO_RD), alone: the renderer calls the host path
 * makes are stubbed here and record what they are given.  No disc data.
 *
 *   d  the stage parameters of three of the player dumps in dist/dumps
 *      (v2277, the bench area: colour 232,248,227, alpha 108, near 0, far
 *      192; v13836, the dark hall: 160,153,154, 160, 0, 255; v21889, the
 *      pool: 208,208,202, 160, 96, 255; each the only parameter set that
 *      gives the dump's table) give the 256 RGBA words the dump's fog
 *      record carries, byte for byte (their hash), and the dump's fog
 *      sprite: RGBAQ 128,128,128 with the strength as alpha, Z 0xFFFFFF, corners
 *      (28672,29184)-(36864,36352) and UVs (8,8)-(8200,7176) on 512 x 448;
 *   s  every fog alpha 0..255 over a grid of near and far planes (near
 *      past far and equal included) gives the table of the integer formula
 *      written out here, and the fogOffsetA sprite carries the fog colour
 *      with fogOffsetA as alpha at Z 0xFFFFFFFF.
 * With an argument the dumps' tables and sprites are printed instead, one
 * line each, and the sweep as a hash of its tables and sprites per fog
 * alpha, for tools/fog_arm64_diff.sh to compare the gcc, clang and arm64
 * builds of the same code byte for byte. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rd.h"
#include "typedef.h"
#include "ZFog.h"

/* ------------------------------------------------- what ZFog.c imports */
int ScreenWidth = 512, ScreenHeight = 448;
int screenOffsetX, screenOffsetY;
int debug_font_flag;
int debug_fullscreen_effect = 1;
StageSetting GlobalStageSetting;
PadState pad[16];

int ico_video_effect_fog(void)
{
    return 1;
}

void FlushCache(int operation)
{
    (void)operation;
}

void debug_Printf(int a, int b, unsigned int c, const char *fmt, ...)
{
    (void)a, (void)b, (void)c, (void)fmt;
}

void debug_PrintfDummy(int x, int y, unsigned int col, const char *fmt, ...)
{
    (void)x, (void)y, (void)col, (void)fmt;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

int tex_AllocVramAuto(int kind, int size)
{
    (void)kind, (void)size;
    return 0x3000;
}

void tex_ResetVramPri(int pri)
{
    (void)pri;
}

/* ----------------------------------------- the renderer calls, recorded */
static int s_posts;
static RdPostParams s_pp;
static uint8_t s_lut[256 * 4];
static int s_screens;
static RdScreenVtx s_scr[2];

void rd_post(RdPostKind kind, const RdPostParams *params)
{
    if (kind != RD_POST_FOG) {
        return;
    }
    s_posts++;
    s_pp = *params;
    memcpy(s_lut, params->lut, sizeof(s_lut));
}

void rd_screen_prims(RdPrim type, const RdScreenVtx *v, uint32_t count, RdSpace space, int uvFixed,
                     RdKey key)
{
    (void)type, (void)space, (void)uvFixed, (void)key;
    s_screens++;
    if (count == 2) {
        memcpy(s_scr, v, sizeof(s_scr));
    }
}

RdTarget rd_target(RdTargetId id)
{
    RdTarget t;
    memset(&t, 0, sizeof(t));
    (void)id;
    return t;
}

RdTex rd_target_texture(RdTarget t, RdTexView view)
{
    RdTex x;
    memset(&x, 0, sizeof(x));
    (void)t, (void)view;
    return x;
}

void rd_set_target(RdTarget color, RdTarget depth, uint32_t gsW, uint32_t gsH, int useOffset)
{
    (void)color, (void)depth, (void)gsW, (void)gsH, (void)useOffset;
}

void rd_pabe(int on)
{
    (void)on;
}

void rd_blend_func(RdBlend eq, uint8_t fix)
{
    (void)eq, (void)fix;
}

void rd_texture(RdTex tex, RdTexFn fn, RdTcc tcc)
{
    (void)tex, (void)fn, (void)tcc;
}

void rd_texture_off(void) {}

void rd_z_write(int on)
{
    (void)on;
}

void rd_test_gs(uint64_t gsTestWord)
{
    (void)gsTestWord;
}

void rd_sampler_filter(RdFilter mag, RdFilter min)
{
    (void)mag, (void)min;
}

void rd_abe(int abe)
{
    (void)abe;
}

void rd_gouraud(int iip)
{
    (void)iip;
}

void rd_tex_a(RdTexA mode)
{
    (void)mode;
}

/* ------------------------------------------------------------ the checks */
static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (failures < 40) {                                                                   \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the dumps' tables: FNV-1a (64-bit) of the 1024 bytes of each dump's fog
   record table (256 RGBA words in index order: index n is the GS Z's bits
   16..23, entry n holds f(255 - n)), hashed from the .rddump files */

typedef struct FogDumpCase {
    const char *dump;
    int rgb[3];
    int alpha, offsetA, nearZ, farZ, strength;
    unsigned long long lutHash;
} FogDumpCase;

static const FogDumpCase kDumps[] = {
    {"v2277", {232, 248, 227}, 108, 0, 0, 192, 128, 0xa7058e1185af0f6dull},
    {"v13836", {160, 153, 154}, 160, 0, 0, 255, 128, 0x0bfc19336f3f461cull},
    {"v21889", {208, 208, 202}, 160, 0, 96, 255, 128, 0x46ed8ecde93ac774ull},
};

static void setFog(const int rgb[3], int alpha, int offsetA, int nearZ, int farZ, int strength)
{
    memset(&GlobalStageSetting, 0, sizeof(GlobalStageSetting));
    GlobalStageSetting.fogOn = 1;
    GlobalStageSetting.fogColR = rgb[0];
    GlobalStageSetting.fogColG = rgb[1];
    GlobalStageSetting.fogColB = rgb[2];
    GlobalStageSetting.fogColA = alpha;
    GlobalStageSetting.fogOffsetA = offsetA;
    GlobalStageSetting.fogNear = nearZ;
    GlobalStageSetting.fogFar = farZ;
    GlobalStageSetting.fogStrength = strength;
}

static void runFog(void)
{
    s_posts = 0;
    s_screens = 0;
    memset(s_lut, 0, sizeof(s_lut));
    fog_MakeFogClut();
    fog_DrawFog();
}

/* fog_MakeFogClut's alpha for i = 255 - n, as integers: zero to the near
   plane, alpha / 2 past the far plane, the line between, each division
   truncating */
static int refAlpha(int alpha, int nearZ, int farZ, int n)
{
    const int i = 255 - n;

    if (i <= nearZ) {
        return 0;
    }
    if (i > farZ) {
        return alpha / 2;
    }
    return alpha * (i - nearZ) / (farZ - nearZ) / 2;
}

static void printRun(const char *tag)
{
    int n;

    printf("%s posts %d screens %d rgba %d %d %d %d z %u rect %.1f %.1f %.1f %.1f uv %.1f %.1f "
           "%.1f %.1f lut",
           tag, s_posts, s_screens, s_pp.rgba[0], s_pp.rgba[1], s_pp.rgba[2], s_pp.rgba[3],
           (unsigned)s_pp.z, s_pp.rect[0], s_pp.rect[1], s_pp.rect[2], s_pp.rect[3], s_pp.uv[0],
           s_pp.uv[1], s_pp.uv[2], s_pp.uv[3]);
    for (n = 0; n < 256 * 4; n++) {
        printf("%s%02x", (n & 3) ? "" : " ", s_lut[n]);
    }
    if (s_screens) {
        printf(" off %d %d %u %d %d %d %d", (int)s_scr[0].x, (int)s_scr[1].y, (unsigned)s_scr[0].z,
               s_scr[0].rgba[0], s_scr[0].rgba[1], s_scr[0].rgba[2], s_scr[0].rgba[3]);
    }
    printf("\n");
}

/* FNV-1a over what printRun prints */
static void hashBytes(uint64_t *h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    size_t i;

    for (i = 0; i < n; i++) {
        *h = (*h ^ b[i]) * 0x100000001b3ull;
    }
}

static void hashRun(uint64_t *h)
{
    hashBytes(h, &s_posts, sizeof(s_posts));
    hashBytes(h, &s_screens, sizeof(s_screens));
    hashBytes(h, s_pp.rgba, sizeof(s_pp.rgba));
    hashBytes(h, &s_pp.z, sizeof(s_pp.z));
    hashBytes(h, s_pp.rect, sizeof(s_pp.rect));
    hashBytes(h, s_pp.uv, sizeof(s_pp.uv));
    hashBytes(h, s_lut, sizeof(s_lut));
    if (s_screens) {
        hashBytes(h, &s_scr[0].x, sizeof(s_scr[0].x));
        hashBytes(h, &s_scr[1].y, sizeof(s_scr[1].y));
        hashBytes(h, &s_scr[0].z, sizeof(s_scr[0].z));
        hashBytes(h, s_scr[0].rgba, sizeof(s_scr[0].rgba));
    }
}

static const int kPlanes[] = {0,   1,   2,   3,   7,   11,  31,  64,  96,
                              127, 128, 150, 187, 192, 200, 246, 254, 255};

int main(int argc, char **argv)
{
    const int print = argc > 1;
    const int nPlanes = (int)(sizeof(kPlanes) / sizeof(kPlanes[0]));
    unsigned k;
    int a, i, j, n;

    (void)argv;

    /* d: the dumps */
    for (k = 0; k < sizeof(kDumps) / sizeof(kDumps[0]); k++) {
        const FogDumpCase *c = &kDumps[k];

        setFog(c->rgb, c->alpha, c->offsetA, c->nearZ, c->farZ, c->strength);
        runFog();
        if (print) {
            printRun(c->dump);
            continue;
        }
        CHECK(s_posts == 1, "%s: %d fog records", c->dump, s_posts);
        {
            uint64_t h = 0xcbf29ce484222325ull;

            hashBytes(&h, s_lut, sizeof(s_lut));
            CHECK(h == c->lutHash, "%s: table hash %016llx, the dump's %016llx", c->dump,
                  (unsigned long long)h, c->lutHash);
        }
        for (n = 0; n < 256; n++) {
            CHECK(s_lut[n * 4 + 0] == c->rgb[0] && s_lut[n * 4 + 1] == c->rgb[1] &&
                      s_lut[n * 4 + 2] == c->rgb[2] &&
                      s_lut[n * 4 + 3] == refAlpha(c->alpha, c->nearZ, c->farZ, n),
                  "%s: entry %d is %d,%d,%d,%d", c->dump, n, s_lut[n * 4 + 0], s_lut[n * 4 + 1],
                  s_lut[n * 4 + 2], s_lut[n * 4 + 3]);
        }
        CHECK(s_pp.rgba[0] == 128 && s_pp.rgba[1] == 128 && s_pp.rgba[2] == 128 &&
                  s_pp.rgba[3] == c->strength && s_pp.z == 0xFFFFFFu,
              "%s: sprite RGBAQ %d,%d,%d,%d Z %u", c->dump, s_pp.rgba[0], s_pp.rgba[1],
              s_pp.rgba[2], s_pp.rgba[3], (unsigned)s_pp.z);
        CHECK(s_pp.rect[0] == 28672.0f && s_pp.rect[1] == 29184.0f && s_pp.rect[2] == 36864.0f &&
                  s_pp.rect[3] == 36352.0f,
              "%s: sprite corners (%g,%g)-(%g,%g)", c->dump, s_pp.rect[0], s_pp.rect[1],
              s_pp.rect[2], s_pp.rect[3]);
        CHECK(s_pp.uv[0] == 8.0f && s_pp.uv[1] == 8.0f && s_pp.uv[2] == 8200.0f &&
                  s_pp.uv[3] == 7176.0f,
              "%s: sprite UVs (%g,%g)-(%g,%g)", c->dump, s_pp.uv[0], s_pp.uv[1], s_pp.uv[2],
              s_pp.uv[3]);
        CHECK(s_screens == 0, "%s: fogOffsetA 0 drew %d flat sprites", c->dump, s_screens);
    }

    /* s: the sweep (printed as one hash per fog alpha) */
    for (a = 0; a < 256; a++) {
        uint64_t h = 0xcbf29ce484222325ull;

        for (i = 0; i < nPlanes; i++) {
            for (j = 0; j < nPlanes; j++) {
                static const int rgb[3] = {201, 7, 133};
                const int nearZ = kPlanes[i], farZ = kPlanes[j];
                const int offsetA = (a * 7) & 0xFF;

                setFog(rgb, a, offsetA, nearZ, farZ, 255 - a);
                runFog();
                if (print) {
                    hashRun(&h);
                    continue;
                }
                for (n = 0; n < 256; n++) {
                    CHECK(s_lut[n * 4 + 0] == 201 && s_lut[n * 4 + 1] == 7 &&
                              s_lut[n * 4 + 2] == 133 &&
                              s_lut[n * 4 + 3] == refAlpha(a, nearZ, farZ, n),
                          "alpha %d near %d far %d: entry %d alpha %d, not %d", a, nearZ, farZ, n,
                          s_lut[n * 4 + 3], refAlpha(a, nearZ, farZ, n));
                }
                CHECK(s_pp.rgba[3] == 255 - a, "alpha %d: strength %d", a, s_pp.rgba[3]);
                if (offsetA > 0) {
                    CHECK(s_screens == 1 && s_scr[0].rgba[0] == 201 && s_scr[0].rgba[1] == 7 &&
                              s_scr[0].rgba[2] == 133 && s_scr[0].rgba[3] == offsetA &&
                              s_scr[0].z == 0xFFFFFFFFu && s_scr[1].z == 0xFFFFFFFFu,
                          "fogOffsetA %d: %d sprites, %d,%d,%d,%d Z %u", offsetA, s_screens,
                          s_scr[0].rgba[0], s_scr[0].rgba[1], s_scr[0].rgba[2], s_scr[0].rgba[3],
                          (unsigned)s_scr[0].z);
                } else {
                    CHECK(s_screens == 0, "fogOffsetA 0: %d sprites", s_screens);
                }
            }
        }
        if (print) {
            printf("sweep a%d %016llx\n", a, (unsigned long long)h);
        }
    }
    if (print) {
        return 0;
    }
    if (failures) {
        printf("fog_lut_test: %d failures\n", failures);
        return 1;
    }
    printf("fog_lut_test: ok (3 dumps, %d parameter sets)\n", 256 * nPlanes * nPlanes);
    return 0;
}
