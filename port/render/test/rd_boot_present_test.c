/* rd_boot_present_test: a saved start-up through the presenter, present by
 * present, as the window shows it.
 *
 * The frames of a start-up dumped with dump_every=1 (rd-NNNNN.rddump; the
 * boot's keep frames, then stage 1 under a full fade, then the first sign)
 * go through the window's present path in order: each frame takes the
 * frame ring's next slot as rd_end_frame leaves it (the slot two frames
 * back freed first, so the previous frame stays the interpolation's pair),
 * and the window's presents of that tick follow, rd_present at alpha 0,
 * 0.25, 0.5 and 0.75 (framerate uncapped: several presents a tick, the
 * first keeping the feedback inputs, the later ones starting from them
 * again).  Each present is read back from the headless output (the picture
 * the swapchain would get: DISPLAY through the present's box, line doubling
 * and filters) and printed with its maximum and mean (the largest of R, G,
 * B per pixel, 0..255) and its count of pixels above 16, with DISPLAY's
 * five probe points (rd_display_probe, the window's start-up log).
 *
 * Twice: the scene at 4x, aspect 32:9, full height, anisotropic filtering
 * (the Custom preset of the report this was written for), and at 1x 16:9.
 *
 * A dump does not keep a frame's fade (RdFrame.fade: rd_post's record of
 * gsb_fade's alpha, which the presenter snaps on): it is found again here
 * from the fade's sprite (list 11, keyed, untextured, ALPHA 0x44 with ABE,
 * covering the whole screen), as rd_post records it.  Not in a dump either:
 * the VU common block (RdFrame.vu, which a blended pair lerps; a dump's
 * frames have none) and the window's overlay and deferred text.
 *
 * The check: every present whose frame is a keep frame or fully faded
 * (fade 1 + 0x7F or more), and whose pair (the frame before) is too, is
 * black: maximum 2 at most (an alpha of 127 leaks 1/128 of the scene).
 *
 * Input: the dump folder as the first argument (the cmake cache variable
 * ICO_BOOT_DUMP_DIR) or the environment variable ICO_BOOT_DUMPS; the frames
 * from ICO_BOOT_FRAMES ("FIRST-LAST", default 115-123).  ICO_BOOT_PNGS names
 * a folder to write every present to as boot-<config>-<frame>-<alpha>.png.
 *
 * Exit 77 (skipped) without the folder or its dumps, or without a Vulkan
 * device. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define OUT_W 1280
#define OUT_H 720
/* a fully faded frame: rd_post's 1 + alpha for an alpha of 127 or more */
#define FADE_FULL 0x80u

/* per loaded frame: a sign is drawn over its fade (filled by installFrame) */
static bool s_signOver[1024]; /* ICO_BOOT_FRAMES spans at most this many frames */
#define BLACK_MAX 2u

typedef struct BootConfig {
    const char *name, *tag;
    float scale, aspect;
    uint8_t fullHeight, filter;
} BootConfig;

static const BootConfig kConfigs[] = {
    {"4x, 32:9, full height, anisotropic", "4x329", 4.0f, 32.0f / 9.0f, 1,
     RD_FILTER_UPGRADE_ANISOTROPIC},
    {"1x, 16:9", "1x169", 1.0f, 16.0f / 9.0f, 0, RD_FILTER_UPGRADE_OFF},
};

/* the window's presents of a tick, uncapped: the first at the tick's start */
static const float kAlphas[] = {0.0f, 0.25f, 0.5f, 0.75f};

static char s_dir[1024];
static uint32_t s_first = 115, s_last = 123;

static void dumpPath(uint32_t n, char *out, size_t size)
{
    snprintf(out, size, "%s/rd-%05u.rddump", s_dir, n);
}

static int fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* the fade rd_post(RD_POST_FADE) recorded: its sprite in list 11, keyed,
 * untextured, LERP_AS with ABE, covering the whole GS screen */
typedef struct FadeScan {
    const RdFrame *f;
    uint32_t fade;
    uint32_t fadeIndex; /* the fade sprite's index in list 11 */
    uint32_t after;     /* screen draws in list 11 after the fade: a sign's backdrop and words */
} FadeScan;

static void fadeWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    FadeScan *s = (FadeScan *)user;
    if (list == 11 && c->type == RDC_SCREEN && s->fade && index > s->fadeIndex) {
        s->after++;
    }
    if (list != 11 || c->type != RDC_SCREEN || !(c->keyLo | c->keyHi) || st->ds.texEnabled ||
        !st->ds.abe || st->ds.blend != RD_BLEND_LERP_AS || c->u[1] < 2 ||
        (uint64_t)c->u[0] + (uint64_t)c->u[1] * sizeof(RdScreenVtx) > s->f->payloadSize) {
        return;
    }
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    uint32_t alpha = 0;
    for (uint32_t i = 0; i < c->u[1]; i++) {
        RdScreenVtx v;
        memcpy(&v, s->f->payload + c->u[0] + (size_t)i * sizeof(v), sizeof(v));
        x0 = v.x < x0 ? v.x : x0;
        y0 = v.y < y0 ? v.y : y0;
        x1 = v.x > x1 ? v.x : x1;
        y1 = v.y > y1 ? v.y : y1;
        alpha = v.rgba[3] > alpha ? v.rgba[3] : alpha;
    }
    /* 12.4 units: the whole screen, a pixel's slack */
    if (x1 - x0 < (int32_t)s->f->gsW * 16 - 16 || y1 - y0 < (int32_t)s->f->gsH * 16 - 16) {
        return;
    }
    if (s->fade < 1u + alpha) {
        s->fade = 1u + alpha;
        s->fadeIndex = index;
        s->after = 0;
    }
}

/* the frame's fade, and whether a sign (its backdrop and words) is drawn
 * over it: those frames show the sign, so they need not be black */
static uint32_t findFade(const RdFrame *f, bool *signOver)
{
    FadeScan s = {f, 0, 0, 0};
    RdStateBlock st = f->startState;
    rd__walk(f, (int)f->keep, &st, fadeWalk, &s);
    *signOver = s.after != 0;
    return s.fade;
}

/* dump n into the ring slot after the last closed frame, as rd_begin_frame
 * and rd_end_frame leave it: the slot's frame (two back) freed first */
static RdFrame *installFrame(uint32_t n)
{
    char path[1100];
    dumpPath(n, path, sizeof(path));
    const int idx = g_rd.lastIndex < 0 ? 0 : (g_rd.lastIndex + 1) % RD_FRAME_RING;
    RdFrame *slot = &g_rd.frames[idx];
    rd__frame_free(slot);
    if (!rd__load_frame(path, slot)) {
        CHECK(0, "%s: rd__load_frame", path);
        return NULL;
    }
    slot->fade = findFade(slot, &s_signOver[n - s_first]);
    slot->closed = 1;
    g_rd.lastIndex = idx;
    g_rd.frameCounter = slot->number;
    g_rd.persistent = slot->endState;
    g_rd.videoShown = 0;
    return slot;
}

typedef struct Lum {
    uint32_t max, bright;
    double mean;
} Lum;

static Lum measure(const uint8_t *px, uint32_t w, uint32_t h)
{
    Lum r = {0, 0, 0.0};
    uint64_t sum = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *p = px + i * 4;
        uint32_t m = p[0] > p[1] ? p[0] : p[1];
        m = p[2] > m ? p[2] : m;
        r.max = m > r.max ? m : r.max;
        r.bright += m > 16;
        sum += m;
    }
    r.mean = w && h ? (double)sum / ((double)w * h) : 0.0;
    return r;
}

static const char *snapName(int snap)
{
    static const char *const kNames[RD_SNAP_COUNT] = {
        "blended", "no previous", "gap", "keep", "cut", "camera", "fade", "history", "size"};
    return snap >= 0 && snap < RD_SNAP_COUNT ? kNames[snap] : "?";
}

/* one configuration's run; false without a device */
static bool runConfig(const BootConfig *cfg, uint32_t gw, uint32_t gh, uint8_t *px)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = cfg->aspect;
    s.sceneScale = cfg->scale;
    s.fullHeightScene = cfg->fullHeight;
    s.filterUpgrade = cfg->filter;
    s.vsync = 1;
    s.interpolate = 1; /* framerate uncapped: the host presents (rd_present) */
    if (!rd_init(gw, gh, &s, NULL)) {
        return false;
    }
    rd__set_not_implemented_fatal(false);
    rd_precreate_pipelines(); /* as the window does after rd_init */
    g_rd.interpFloor = 0;
    const char *pngDir = getenv("ICO_BOOT_PNGS");
    printf("%s: scene %gx%g, output %ux%u\n", cfg->name, (double)g_rd.sceneSx, (double)g_rd.sceneSy,
           OUT_W, OUT_H);
    uint32_t firstBright = 0;
    for (uint32_t n = s_first; n <= s_last; n++) {
        const RdFrame *cur = installFrame(n);
        if (!cur) {
            break;
        }
        const RdFrame *prev = rd__prev_frame();
        const int snap = rd__interp_snap(prev, cur);
        /* a present that must be black: this frame and its pair kept or
         * fully faded */
        const bool dark = (cur->keep || cur->fade >= FADE_FULL) && !s_signOver[n - s_first] &&
                          (!prev || prev->keep || prev->fade >= FADE_FULL);
        printf("  frame %u: keep %u, fade %u (alpha %u), snap %s, %u texts%s\n", cur->number,
               cur->keep, cur->fade, cur->fade ? cur->fade - 1 : 0, snapName(snap), cur->textItems,
               dark ? "; must be black" : "");
        for (size_t k = 0; k < sizeof(kAlphas) / sizeof(kAlphas[0]); k++) {
            const float a = kAlphas[k];
            const bool ok = rd_present(a);
            CHECK(ok, "%s: frame %u: rd_present(%.2f)", cfg->tag, n, (double)a);
            if (!ok) {
                continue;
            }
            RdPresentInfo pi;
            CHECK(rd_last_present_info(&pi) && pi.frame == n && pi.firstOfTick == (k == 0),
                  "%s: frame %u: rd_last_present_info", cfg->tag, n);
            uint8_t probe[RD_DISPLAY_PROBE_POINTS][4];
            char pts[RD_DISPLAY_PROBE_POINTS * 20] = " unread";
            if (rd_display_probe(probe)) {
                size_t o = 0;
                for (int p = 0; p < RD_DISPLAY_PROBE_POINTS && o < sizeof(pts); p++) {
                    const int w = snprintf(pts + o, sizeof(pts) - o, " (%u,%u,%u)", probe[p][0],
                                           probe[p][1], probe[p][2]);
                    o += w > 0 ? (size_t)w : 0;
                }
            }
            uint32_t w = 0, h = 0;
            if (!rd_read_presented(px, &w, &h) || w != OUT_W || h != OUT_H) {
                CHECK(0, "%s: frame %u: rd_read_presented", cfg->tag, n);
                continue;
            }
            const Lum l = measure(px, w, h);
            printf("    t %.2f: max %u, mean %.2f, %u pixels over 16; display%s\n", (double)a,
                   l.max, l.mean, l.bright, pts);
            if (!firstBright && l.bright) {
                firstBright = n;
                printf("    the first present with a pixel over 16: frame %u at t %.2f\n", n,
                       (double)a);
            }
            if (dark) {
                CHECK(l.max <= BLACK_MAX,
                      "%s: frame %u at t %.2f: a present before the first sign is not black "
                      "(max %u, mean %.2f, %u pixels over 16)",
                      cfg->tag, n, (double)a, l.max, l.mean, l.bright);
            }
            if (pngDir && pngDir[0]) {
                char png[1200];
                snprintf(png, sizeof(png), "%s/boot-%s-%05u-%03d.png", pngDir, cfg->tag, n,
                         (int)(a * 100.0f + 0.5f));
                CHECK(rd_write_png(png, px, w, h, w * 4, 0), "%s: write", png);
            }
        }
    }
    rd_shutdown();
    return true;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 && argv[1][0] ? argv[1] : getenv("ICO_BOOT_DUMPS");
    if (!dir || !dir[0]) {
        printf("SKIP: no dump folder (ICO_BOOT_DUMP_DIR, or ICO_BOOT_DUMPS)\n");
        return 77;
    }
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    const char *range = getenv("ICO_BOOT_FRAMES");
    if (range && range[0]) {
        unsigned a = 0, b = 0;
        if (sscanf(range, "%u-%u", &a, &b) != 2 || !a || b < a || b - a > 1000) {
            printf("bad ICO_BOOT_FRAMES \"%s\" (FIRST-LAST)\n", range);
            return 1;
        }
        s_first = a;
        s_last = b;
    }
    char path[1100];
    for (uint32_t n = s_first; n <= s_last; n++) {
        dumpPath(n, path, sizeof(path));
        if (!fileExists(path)) {
            printf("SKIP: %s is missing\n", path);
            return 77;
        }
    }
    /* the dumps' GS size (bytes 24..31 of the header, rd_replay_tool's peek) */
    dumpPath(s_first, path, sizeof(path));
    uint8_t hdr[32];
    FILE *probe = fopen(path, "rb");
    const bool read = probe && fread(hdr, 1, sizeof(hdr), probe) == sizeof(hdr);
    if (probe) {
        fclose(probe);
    }
    if (!read || memcmp(hdr, RD_DUMP_MAGIC, 8) != 0) {
        printf("%s: not an rd dump\n", path);
        return 1;
    }
    const uint32_t gw = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8) | ((uint32_t)hdr[26] << 16) |
                        ((uint32_t)hdr[27] << 24);
    const uint32_t gh = (uint32_t)hdr[28] | ((uint32_t)hdr[29] << 8) | ((uint32_t)hdr[30] << 16) |
                        ((uint32_t)hdr[31] << 24);
    if (!gw || !gh || gw > 4096 || gh > 4096) {
        printf("%s: bad GS size %ux%u\n", path, gw, gh);
        return 1;
    }
    uint8_t *px = malloc((size_t)OUT_W * OUT_H * 4);
    if (!px) {
        return 1;
    }
    printf("frames %u to %u from %s, presents at alpha 0, 0.25, 0.5 and 0.75 a tick\n", s_first,
           s_last, s_dir);
    for (size_t c = 0; c < sizeof(kConfigs) / sizeof(kConfigs[0]); c++) {
        if (!runConfig(&kConfigs[c], gw, gh, px)) {
            free(px);
            printf("no Vulkan device: skipped\n");
            return 77;
        }
    }
    free(px);
    if (s_fail) {
        printf("%d failure(s)\n", s_fail);
        return 1;
    }
    printf("ok\n");
    return 0;
}
