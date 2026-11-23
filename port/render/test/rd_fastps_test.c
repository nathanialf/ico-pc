/* rd_fastps_test.c: the fast pixel build's pure part (rd_fastps.c,
 * rd_fastps.h; issue 179), CPU only.  argv[1]: port/shaders/common.hlsli.
 *   table    the eligibility of a pass at its DrawCB: the set's forms through
 *            it (opaque, the two-pass lerp's colour and alpha passes, the
 *            premultiplied forms, FIX, FBA, PABE, C1_DST, the bits vu_ps never
 *            reads, a discard-free twin's key, TEXA's mode in mode.y's low
 *            byte, any atst, split and AREF with the test off, scale.z -0);
 *            every refusal reason in its order (texture: untextured, DECAL,
 *            TCC off; texel; levels: GS levels, nearest, the grid; DATE:
 *            DATE, DATM, DATE_DST; plane; alpha; crop); the tests' sabotage
 *            letting DECAL through
 *   none     the passes the build does not exist for (vu_texa_ps, vu_np_ps,
 *            sprite_ps, a destination read by storage and by attachment,
 *            nodual 0, a fast key already, the discard-free bit alone): -1
 *   mask     RD_FPS_FIXED_MASK and RD_FPS_FIXED against the shader's own
 *            VU_FAST_FIXED_MASK and VU_FAST_FIXED lines (common.hlsli): the
 *            bits the build holds are the bits the CPU checks
 *   names    every reason's words, the modules' names, the key bit
 *   use      the in-play judge's use of a replay
 *   state    the run's state without a device (rd_fastps_run.c): held until
 *            the self-test (no 10 s clause, the stats line's words), on once
 *            it passed, =0 off and a passed self-test does not turn it on,
 *            =1 on, the loss ladder and the install's mark over =1, a failed
 *            self-test off for the run with its words, a short buffer cut */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_fastps.h"
#include "rd_internal.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the boat's opaque pass: vu_ps's *_nodual build, TEXTURED | TCC */
static RdFpsPass base(void)
{
    RdFpsPass p;
    memset(&p, 0, sizeof(p));
    p.fs = RD_FS_VU;
    p.nodual = RD_NODUAL_ON;
    p.dst = RHI_DST_READ_NONE;
    p.mode[0] = (uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA;
    return p;
}

static void table(void)
{
    RdFpsPass p = base();
    CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    /* the free bits: the two-pass lerp, the premultiplied forms, FIX, FBA,
       PABE, C1_DST, and bits vu_ps never reads */
    static const uint32_t kFree[] = {ICO_DF_NODUAL_FACTOR,
                                     ICO_DF_NODUAL_ALPHA_PASS,
                                     ICO_DF_PREMUL,
                                     (uint32_t)ICO_DF_FIX_FACTOR | (uint32_t)ICO_DF_PREMUL,
                                     ICO_DF_FBA,
                                     ICO_DF_PABE,
                                     ICO_DF_C1_DST,
                                     ICO_DF_AA1_FULL,
                                     ICO_DF_TEXA_MAG_LINEAR,
                                     ICO_DF_TEXA_CLAMP_S,
                                     ICO_DF_ABOVE1X,
                                     ICO_DF_FOLD};
    for (size_t i = 0; i < sizeof(kFree) / sizeof(kFree[0]); i++) {
        p = base();
        p.mode[0] |= kFree[i];
        CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    }
    /* a pass the discard-free twins' rule gave its twin */
    p = base();
    p.nodual = RD_NODUAL_ON | RD_NODUAL_NODISCARD;
    CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    /* TEXA's mode with an RGBA32 texel; the test's words with it off; -0 */
    p = base();
    p.mode[1] = 0xFFu;
    p.mode[2] = 0xFFu | (2u << 16);
    p.mode[3] = 0x80u;
    p.scaleZ = -0.0f;
    CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    /* every refusal, in its order */
    p = base();
    p.mode[0] = ICO_DF_TCC_RGBA;
    CHECK(rd_fps_eligible(&p) == RD_FPS_TEXTURE);
    p = base();
    p.mode[0] |= ICO_DF_DECAL;
    CHECK(rd_fps_eligible(&p) == RD_FPS_TEXTURE);
    p.skipDecal = true; /* the tests' sabotage */
    CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    p = base();
    p.mode[0] = ICO_DF_TEXTURED;
    CHECK(rd_fps_eligible(&p) == RD_FPS_TEXTURE);
    p = base();
    p.mode[1] = 1u << 8;
    CHECK(rd_fps_eligible(&p) == RD_FPS_TEXEL);
    p.mode[1] = 2u << 8;
    CHECK(rd_fps_eligible(&p) == RD_FPS_TEXEL);
    static const uint32_t kLevels[] = {ICO_DF_GS_LOD, ICO_DF_NEAREST, ICO_DF_GS_BILINEAR_GRID};
    for (size_t i = 0; i < 3; i++) {
        p = base();
        p.mode[0] |= kLevels[i];
        CHECK(rd_fps_eligible(&p) == RD_FPS_LEVELS);
    }
    static const uint32_t kDate[] = {ICO_DF_DATE, ICO_DF_DATM, ICO_DF_DATE_DST};
    for (size_t i = 0; i < 3; i++) {
        p = base();
        p.mode[0] |= kDate[i];
        CHECK(rd_fps_eligible(&p) == RD_FPS_DATE);
    }
    p = base();
    p.mode[0] |= ICO_DF_PLANE;
    CHECK(rd_fps_eligible(&p) == RD_FPS_PLANE);
    p = base();
    p.mode[2] = 0x100u | 6u;
    CHECK(rd_fps_eligible(&p) == RD_FPS_ALPHA);
    p = base();
    p.scaleZ = 0.25f;
    CHECK(rd_fps_eligible(&p) == RD_FPS_CROP);
    /* the order: the first reason that applies */
    p = base();
    p.mode[0] |= ICO_DF_DECAL | ICO_DF_PLANE;
    p.mode[2] = 0x100u;
    CHECK(rd_fps_eligible(&p) == RD_FPS_TEXTURE);
    p = base();
    p.mode[0] |= ICO_DF_PLANE;
    p.mode[2] = 0x100u;
    CHECK(rd_fps_eligible(&p) == RD_FPS_PLANE);
}

static void none(void)
{
    static const uint8_t kFs[] = {RD_FS_VU_TEXA, RD_FS_VU_NP, RD_FS_VU_TEXA_NP, RD_FS_SPRITE};
    for (size_t i = 0; i < sizeof(kFs); i++) {
        RdFpsPass p = base();
        p.fs = kFs[i];
        CHECK(rd_fps_eligible(&p) == -1);
    }
    RdFpsPass p = base();
    p.dst = RHI_DST_READ_STORAGE;
    CHECK(rd_fps_eligible(&p) == -1);
    p.dst = RHI_DST_READ_ATTACHMENT;
    CHECK(rd_fps_eligible(&p) == -1);
    p = base();
    p.nodual = 0;
    CHECK(rd_fps_eligible(&p) == -1);
    p.nodual = RD_NODUAL_ON | RD_NODUAL_FAST;
    CHECK(rd_fps_eligible(&p) == -1);
    p.nodual = RD_NODUAL_NODISCARD;
    CHECK(rd_fps_eligible(&p) == -1);
}

/* the value of the shader's "#define NAME" line, its continuation joined: the
   OR of its decimal literals */
static bool shaderBits(const char *text, const char *name, uint32_t *out)
{
    char head[64];
    snprintf(head, sizeof(head), "#define %s", name);
    const char *at = text;
    for (;;) {
        at = strstr(at, head);
        if (!at) {
            return false;
        }
        at += strlen(head);
        if (*at == ' ' || *at == '\\') {
            break; /* this name, not a longer one it begins */
        }
    }
    uint32_t v = 0;
    bool any = false;
    for (const char *c = at; *c; c++) {
        if (*c == '\n' && c > at && c[-1] != '\\') {
            break;
        }
        if (*c == '/' && c[1] == '/') {
            break;
        }
        if (*c >= '0' && *c <= '9') {
            v |= (uint32_t)strtoul(c, (char **)&c, 10);
            any = true;
            c--;
        }
    }
    *out = v;
    return any;
}

static void mask(const char *path)
{
    if (!path) {
        printf("FAIL no common.hlsli given\n");
        failures++;
        return;
    }
    FILE *f = fopen(path, "rb");
    static char text[1 << 17];
    size_t n = 0;
    if (f) {
        n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
    }
    text[n] = '\0';
    uint32_t m = 0, v = 0;
    CHECK(n > 0);
    CHECK(shaderBits(text, "VU_FAST_FIXED_MASK", &m));
    CHECK(shaderBits(text, "VU_FAST_FIXED", &v));
    CHECK(m == RD_FPS_FIXED_MASK);
    CHECK(v == RD_FPS_FIXED);
    printf("  the shader's held bits 0x%08x (values 0x%08x), the CPU's 0x%08x (0x%08x)\n", m, v,
           (unsigned)RD_FPS_FIXED_MASK, (unsigned)RD_FPS_FIXED);
    /* the CPU's rule checks exactly the held bits: every one of them refuses
       a pass when it differs from its value, every other leaves it fast */
    for (int b = 0; b < 32; b++) {
        RdFpsPass p = base();
        p.mode[0] ^= 1u << b;
        const bool held = (RD_FPS_FIXED_MASK & (1u << b)) != 0u;
        CHECK((rd_fps_eligible(&p) != RD_FPS_FAST) == held);
    }
}

static void names(void)
{
    static const char *const kWords[RD_FPS_REASONS] = {"through it",
                                                       "texture",
                                                       "texel",
                                                       "levels",
                                                       "DATE",
                                                       "plane",
                                                       "alpha",
                                                       "crop",
                                                       "not made yet",
                                                       "timed without it",
                                                       "module not tested"};
    for (int i = 0; i < RD_FPS_REASONS; i++) {
        CHECK(strcmp(rd_fps_reason_name(i), kWords[i]) == 0);
    }
    CHECK(strcmp(rd_fps_reason_name(RD_FPS_REASONS), "?") == 0);
    CHECK(strcmp(RD_FPS_MODULE_NAME, "vu_ps_nodual_fast") == 0);
    CHECK(strcmp(RD_FPS_MODULE_NAME_ND, "vu_ps_nodual_fast_nd") == 0);
    CHECK((RD_NODUAL_FAST & (RD_NODUAL_ON | RD_NODUAL_NODISCARD)) == 0u);
}

static void use(void)
{
    RdFpsUse u;
    memset(&u, 0, sizeof(u));
    CHECK(!rd_fps_use_counts(&u));
    rd_fps_use_note(&u, RD_FPS_TEXTURE);
    CHECK(!rd_fps_use_counts(&u));
    rd_fps_use_note(&u, RD_FPS_FAST);
    CHECK(rd_fps_use_counts(&u));
    rd_fps_use_note(&u, RD_FPS_UNMADE);
    CHECK(!rd_fps_use_counts(&u));
    memset(&u, 0, sizeof(u));
    rd_fps_use_note(&u, RD_FPS_TRIAL);
    CHECK(rd_fps_use_counts(&u));
}

static void env(const char *name, const char *value)
{
    if (value) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
}

static void state(void)
{
    char buf[512], st[1024];
    env("ICO_RD_VU_FASTPS", NULL);
    env("ICO_LOSS_ROW", NULL);
    env("ICO_FASTPS_BLOCK", NULL);
    env("ICO_FASTPS_MARK", NULL);
    rd__fps_init();
    CHECK(strcmp(rd__fps_state_text(), "held") == 0);
    CHECK(rd_fps_text(buf, sizeof(buf), 1) == 0u && buf[0] == '\0');
    (void)rd__fps_stats_text(st, sizeof(st));
    CHECK(strstr(st, "fast pixel build held (until its start-up self-test passes): 0 passes "
                     "through it, 0 triangles; refused: texture 0, texel 0, levels 0, DATE 0, "
                     "plane 0, alpha 0, crop 0, not made yet 0, timed without it 0, module not "
                     "tested 0; 0 modules of it made, 0 of its pipelines alive") != NULL);
    rd__fps_verdict(true, "");
    CHECK(rd__fps_on());
    CHECK(!rd__fps_triable()); /* no module without a device */
    (void)rd_fps_text(buf, sizeof(buf), 2);
    CHECK(strcmp(buf, "; fast pixel build: 0 passes a picture through it, 0 triangles (0 refused "
                      "a picture: texture 0, texel 0, levels 0, DATE 0, plane 0, alpha 0, crop 0, "
                      "not made yet 0, timed without it 0, module not tested 0)") == 0);
    char small[24];
    const size_t n = rd_fps_text(small, sizeof(small), 1);
    CHECK(n == sizeof(small) - 1u && strlen(small) == n);
    env("ICO_RD_VU_FASTPS", "0");
    rd__fps_init();
    rd__fps_verdict(true, "");
    CHECK(strcmp(rd__fps_state_text(), "off") == 0);
    env("ICO_RD_VU_FASTPS", "1");
    rd__fps_init();
    CHECK(rd__fps_on());
    env("ICO_LOSS_ROW", "1");
    rd__fps_init();
    CHECK(strcmp(rd__fps_state_text(), "off") == 0);
    (void)rd__fps_stats_text(st, sizeof(st));
    CHECK(strstr(st, "fast pixel build off (for this start (the loss ladder: 1 start in a row") !=
          NULL);
    env("ICO_LOSS_ROW", NULL);
    env("ICO_FASTPS_BLOCK", "was being tried when it ended (a crash)");
    rd__fps_init();
    (void)rd__fps_stats_text(st, sizeof(st));
    CHECK(strstr(st, "fast pixel build off (on this install (was being tried when it ended (a "
                     "crash))") != NULL);
    env("ICO_FASTPS_BLOCK", NULL);
    env("ICO_RD_VU_FASTPS", NULL);
    rd__fps_init();
    rd__fps_verdict(false, "the pictures differ with it, the first at texel 1,2");
    rd__fps_init(); /* kept off for the run */
    (void)rd__fps_stats_text(st, sizeof(st));
    CHECK(strstr(st, "fast pixel build off (for the run (its self-test: the pictures differ with "
                     "it, the first at texel 1,2))") != NULL);
}

int main(int argc, char **argv)
{
    table();
    none();
    mask(argc > 1 ? argv[1] : NULL);
    names();
    use();
    state();
    printf("rd_fastps_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
