/*
 * port/fmv/test/rd_video_test.c
 *
 * port/render/rd_video.c on rd's headless device (lavapipe in the
 * container): pictures in, the present target read back.
 *
 *   flat     a flat picture of a known colour: every pixel in the picture's
 *            rectangle is the IPU CSC of that colour, the bars around it are
 *            the clear colour
 *   ramp     a 64 x 48 picture shown 1:1 with varied luma and per-2x2 chroma:
 *            every pixel equals the CPU model of the IPU CSC (point-sampled
 *            chroma), bit for bit
 *   mirror   the same picture with the mirror mode on: flipped with the FMV
 *            toggle on, unflipped with it off
 *   crt      (package C1) a 720 x 576 film at an 800 x 480 output: with the
 *            CRT filter off no CRT pass is drawn and the box is the flat
 *            picture; on (scanlines), each frame and each clear frame is
 *            one CRT pass on the film's grid (512 triads, 288 field lines),
 *            the box shows the beam's lines, keeps the picture's light and
 *            stays black around it; off again, the plain picture
 *   fit      (v0.4.2 N1) a 720 x 576 picture in a 720 x 480 display area
 *            (a 576-line film under the 60 Hz video mode) at a 240 x 108
 *            output: the picture is fitted into the 144 x 108 box, its top
 *            band on the box's first rows and its bottom band on the last
 *            (not the 1.2x picture the box used to cut), black around it
 *
 * Exit 77 without a device.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fmv/rd_video.h"
#include "rd_internal.h"
#include "vk/rhi_vk.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* The IPU's CSC (yuv.hlsl's ipu_csc; PCSX2 yuv2rgb_reference's constants). */
static void ipu_csc(int Y, int Cb, int Cr, uint8_t rgb[3])
{
    int lum = (0x95 * (Y - 16 > 0 ? Y - 16 : 0)) >> 6;
    int cb = Cb - 128, cr = Cr - 128;
    int rcr = (0xCC * cr) >> 6, gcr = (-0x68 * cr) >> 6, gcb = (-0x32 * cb) >> 6,
        bcb = (0x102 * cb) >> 6;
    int c[3] = {(lum + rcr + 1) >> 1, (lum + gcr + gcb + 1) >> 1, (lum + bcb + 1) >> 1};
    for (int i = 0; i < 3; i++) {
        rgb[i] = (uint8_t)(c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i]);
    }
}

#define OW 64
#define OH 48

static uint8_t s_out[OW * OH * 4];

static int readOut(void)
{
    uint32_t w = 0, h = 0;
    if (!rd__ReadPresent(s_out, sizeof(s_out), &w, &h) || w != OW || h != OH) {
        printf("FAIL: readback (%u x %u)\n", w, h);
        failures++;
        return -1;
    }
    return 0;
}

static void testFlat(void)
{
    /* a 32 x 24 picture in a 64 x 48 display area: the box is the whole
       64 x 48 output, the picture its centre 32 x 24 at (16, 12), scaled
       1:1 */
    enum { W = 32, H = 24 };

    static uint8_t y[W * H], u[(W / 2) * (H / 2)], v[(W / 2) * (H / 2)];
    const uint32_t pitch[3] = {W, W / 2, W / 2};
    const uint8_t black[4] = {0, 0, 0, 0x80};
    uint8_t want[3];
    int bad = 0, badBar = 0;

    memset(y, 81, sizeof(y));
    memset(u, 90, sizeof(u));
    memset(v, 240, sizeof(v)); /* BT.601 red-ish */
    ipu_csc(81, 90, 240, want);
    rd_VideoSetDisplay(64, 48);
    CHECK(rd_VideoClear(black) == 0, "clear");
    CHECK(rd_VideoFrame(y, u, v, pitch, W, H) == 0, "frame");
    if (readOut() != 0) {
        return;
    }
    for (int py = 0; py < OH; py++) {
        for (int px = 0; px < OW; px++) {
            const uint8_t *p = s_out + (py * OW + px) * 4;
            int in = px >= 16 && px < 16 + W && py >= 12 && py < 12 + H;
            if (in && (p[0] != want[0] || p[1] != want[1] || p[2] != want[2])) {
                bad++;
            }
            if (!in && (p[0] | p[1] | p[2]) != 0) {
                badBar++;
            }
        }
    }
    printf("flat: Y 81 Cb 90 Cr 240 -> %u %u %u (pixel (20, 20) %u %u %u)\n", want[0], want[1],
           want[2], s_out[(20 * OW + 20) * 4], s_out[(20 * OW + 20) * 4 + 1],
           s_out[(20 * OW + 20) * 4 + 2]);
    CHECK(bad == 0, "flat: %d picture pixels differ", bad);
    CHECK(badBar == 0, "flat: %d pixels outside the picture are not the clear colour", badBar);
}

static uint8_t s_y[OW * OH], s_u[(OW / 2) * (OH / 2)], s_v[(OW / 2) * (OH / 2)];

static int checkRamp(int mirrored)
{
    int bad = 0;
    for (int py = 0; py < OH; py++) {
        for (int px = 0; px < OW; px++) {
            int sx = mirrored ? OW - 1 - px : px;
            uint8_t want[3];
            const uint8_t *p = s_out + (py * OW + px) * 4;
            ipu_csc(s_y[py * OW + sx], s_u[(py / 2) * (OW / 2) + sx / 2],
                    s_v[(py / 2) * (OW / 2) + sx / 2], want);
            if (p[0] != want[0] || p[1] != want[1] || p[2] != want[2]) {
                if (bad < 3) {
                    printf("  (%d, %d): got %u %u %u want %u %u %u\n", px, py, p[0], p[1], p[2],
                           want[0], want[1], want[2]);
                }
                bad++;
            }
        }
    }
    return bad;
}

static void testRamp(void)
{
    const uint32_t pitch[3] = {OW, OW / 2, OW / 2};

    for (int i = 0; i < OW * OH; i++) {
        s_y[i] = (uint8_t)((i * 37 + (i / OW) * 11) & 0xFF); /* includes Y < 16 and > 235 */
    }
    for (int i = 0; i < (OW / 2) * (OH / 2); i++) {
        s_u[i] = (uint8_t)((i * 53 + 7) & 0xFF);
        s_v[i] = (uint8_t)((i * 29 + 200) & 0xFF);
    }
    rd_VideoSetDisplay(OW, OH);
    CHECK(rd_VideoFrame(s_y, s_u, s_v, pitch, OW, OH) == 0, "ramp frame");
    if (readOut() == 0) {
        int bad = checkRamp(0);
        CHECK(bad == 0, "ramp: %d pixels differ from the IPU model", bad);
    }
}

static void setMirror(int on)
{
    RdSettings s = *rd_GetSettings();
    s.mirror = (uint8_t)on;
    rd_SetSettings(&s);
    rd_BeginFrame(); /* settings apply at the next frame */
    rd_DiscardFrame();
}

static void testMirror(void)
{
    const uint32_t pitch[3] = {OW, OW / 2, OW / 2};

    setMirror(1);
    CHECK(rd_VideoFrame(s_y, s_u, s_v, pitch, OW, OH) == 0, "mirror frame");
    if (readOut() == 0) {
        int bad = checkRamp(1);
        CHECK(bad == 0, "mirror on: %d pixels differ from the flipped model", bad);
    }
    setMirror(0);
    CHECK(rd_VideoFrame(s_y, s_u, s_v, pitch, OW, OH) == 0, "unmirrored frame");
    if (readOut() == 0) {
        int bad = checkRamp(0);
        CHECK(bad == 0, "mirror off: %d pixels differ from the unflipped model", bad);
    }
}

/* ------------------------------------------- the CRT filter (package C1) */

#define CW 800
#define CH 480

static uint8_t s_big[CW * CH * 4];

static void setCrt(RdCrtMode mode, uint32_t w, uint32_t h)
{
    RdSettings s = *rd_GetSettings();
    rd_CrtSettings(&s, mode, 1.0f);
    s.outputWidth = w;
    s.outputHeight = h;
    rd_SetSettings(&s);
    rd_BeginFrame(); /* settings apply at the next frame */
    rd_DiscardFrame();
}

/* the box's (640 x 480 at x 80) mean luma, its rows' spread (the largest
 * row mean less the smallest) and the lit pixels outside it */
static int measure(double *mean, double *spread, int *outside)
{
    uint32_t w = 0, h = 0;
    if (!rd__ReadPresent(s_big, sizeof(s_big), &w, &h) || w != CW || h != CH) {
        printf("FAIL: crt readback (%u x %u)\n", w, h);
        failures++;
        return -1;
    }
    double sum = 0.0, lo = 1e9, hi = -1.0;
    *outside = 0;
    for (int y = 0; y < CH; y++) {
        double row = 0.0;
        for (int x = 0; x < CW; x++) {
            const uint8_t *p = s_big + (y * CW + x) * 4;
            const double l = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
            if (x >= 80 && x < 720) {
                row += l;
            } else if (p[0] | p[1] | p[2]) {
                (*outside)++;
            }
        }
        row /= 640.0;
        sum += row;
        /* away from the tube's rounded corners and the edge's lines */
        if (y >= 40 && y < CH - 40) {
            lo = row < lo ? row : lo;
            hi = row > hi ? row : hi;
        }
    }
    *mean = sum / CH;
    *spread = hi - lo;
    return 0;
}

static void testCrt(void)
{
    enum { W = 720, H = 576 };

    static uint8_t y[W * H], u[(W / 2) * (H / 2)], v[(W / 2) * (H / 2)];
    const uint32_t pitch[3] = {W, W / 2, W / 2};
    const uint8_t black[4] = {0, 0, 0, 0x80};
    memset(y, 160, sizeof(y));
    memset(u, 128, sizeof(u));
    memset(v, 128, sizeof(v));
    rd_VideoSetDisplay(W, H);

    /* off: no pass, a flat box */
    setCrt(RD_CRT_OFF, CW, CH);
    const uint32_t n0 = rd__CrtLastPass(NULL, NULL);
    CHECK(rd_VideoFrame(y, u, v, pitch, W, H) == 0, "crt off: frame");
    double offMean = 0.0, spread = 0.0;
    int outside = 0;
    if (measure(&offMean, &spread, &outside) == 0) {
        printf("crt off: box luma %.1f, rows' spread %.2f, %d lit outside\n", offMean, spread,
               outside);
        CHECK(rd__CrtLastPass(NULL, NULL) == n0, "crt off: no CRT pass drawn");
        CHECK(spread < 0.01 && outside == 0, "crt off: the plain picture in the box");
    }

    /* on: one pass a frame, the film's grid, scanlines in the box */
    setCrt(RD_CRT_SCANLINES, CW, CH);
    CHECK(rd_VideoClear(black) == 0, "crt on: clear");
    uint32_t vw = 0, vh = 0;
    CHECK(rd__CrtLastPass(&vw, &vh) == n0 + 1, "crt on: the clear frame drawn through the filter");
    CHECK(rd_VideoFrame(y, u, v, pitch, W, H) == 0, "crt on: frame");
    CHECK(rd__CrtLastPass(&vw, &vh) == n0 + 2, "crt on: the film frame drawn through the filter");
    CHECK(vw == 512 && vh == H / 2, "crt on: the film's grid %u x %u (want 512 x %u)", vw, vh,
          H / 2);
    double crtMean = 0.0;
    if (measure(&crtMean, &spread, &outside) == 0) {
        printf("crt on: box luma %.1f, rows' spread %.2f, %d lit outside\n", crtMean, spread,
               outside);
        CHECK(spread > 8.0, "crt on: the beam's lines in the box (spread %.2f)", spread);
        CHECK(crtMean > offMean * 0.85 && crtMean < offMean * 1.05,
              "crt on: the picture keeps its light (%.1f against %.1f)", crtMean, offMean);
        CHECK(outside == 0, "crt on: black around the box (%d lit)", outside);
    }

    /* off again */
    setCrt(RD_CRT_OFF, CW, CH);
    const uint32_t n1 = rd__CrtLastPass(NULL, NULL);
    CHECK(rd_VideoFrame(y, u, v, pitch, W, H) == 0, "crt off again: frame");
    double again = 0.0;
    if (measure(&again, &spread, &outside) == 0) {
        CHECK(rd__CrtLastPass(NULL, NULL) == n1 && spread < 0.01 && fabs(again - offMean) < 0.01,
              "crt off again: the plain picture, no pass");
    }
    setCrt(RD_CRT_OFF, OW, OH);
}

/* ------------------------------- a picture taller than the area (N1) */

#define FW 240
#define FH 108

static uint8_t s_fit[FW * FH * 4];

static void testFit(void)
{
    enum { W = 720, H = 576, BAND = 16 };

    static uint8_t y[W * H], u[(W / 2) * (H / 2)], v[(W / 2) * (H / 2)];
    const uint32_t pitch[3] = {W, W / 2, W / 2};
    /* three bands of luma by picture row: the top 16 rows, the middle, the
       bottom 16 rows; neutral chroma */
    for (int r = 0; r < H; r++) {
        const uint8_t l = r < BAND ? 235 : r >= H - BAND ? 60 : 140;
        memset(y + (size_t)r * W, l, W);
    }
    memset(u, 128, sizeof(u));
    memset(v, 128, sizeof(v));
    uint8_t top[3], mid[3], bot[3];
    ipu_csc(235, 128, 128, top);
    ipu_csc(140, 128, 128, mid);
    ipu_csc(60, 128, 128, bot);

    rd_VideoSetDisplay(720, 480);
    setCrt(RD_CRT_OFF, FW, FH);
    CHECK(rd_VideoFrame(y, u, v, pitch, W, H) == 0, "fit: frame");
    uint32_t ow = 0, oh = 0;
    if (!rd__ReadPresent(s_fit, sizeof(s_fit), &ow, &oh) || ow != FW || oh != FH) {
        printf("FAIL: fit readback (%u x %u)\n", ow, oh);
        failures++;
    } else {
        /* the box: 144 x 108 at x 48 (rd__PresentBox 4:3); the picture's
           576 rows at 108/576 = 0.1875 output rows each: the top band on
           rows 0-2, the middle on 3-104, the bottom band on 105-107 (at the
           old 108/480 scale the top band sat above the box, cut away) */
        int badTop = 0, badMid = 0, badBot = 0, badBar = 0;
        for (int py = 0; py < FH; py++) {
            const uint8_t *want = py < 3 ? top : py >= FH - 3 ? bot : mid;
            for (int px = 0; px < FW; px++) {
                const uint8_t *p = s_fit + (py * FW + px) * 4;
                if (px < 48 || px >= 48 + 144) {
                    badBar += (p[0] | p[1] | p[2]) != 0;
                } else if (p[0] != want[0] || p[1] != want[1] || p[2] != want[2]) {
                    if (py < 3) {
                        badTop++;
                    } else if (py >= FH - 3) {
                        badBot++;
                    } else {
                        badMid++;
                    }
                }
            }
        }
        const uint8_t *r0 = s_fit + (0 * FW + 120) * 4, *rl = s_fit + ((FH - 1) * FW + 120) * 4;
        printf("fit: row 0 %u %u %u (want %u), row %d %u %u %u (want %u)\n", r0[0], r0[1], r0[2],
               top[0], FH - 1, rl[0], rl[1], rl[2], bot[0]);
        CHECK(badTop == 0, "fit: the picture's top rows are not on the box's first rows (%d px)",
              badTop);
        CHECK(badBot == 0, "fit: the picture's bottom rows are not on the box's last rows (%d px)",
              badBot);
        CHECK(badMid == 0, "fit: %d middle pixels differ (the picture is not scaled 108/576)",
              badMid);
        CHECK(badBar == 0, "fit: %d pixels lit outside the 4:3 box", badBar);
    }
    setCrt(RD_CRT_OFF, OW, OH);
}

int main(void)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = OW;
    s.outputHeight = OH;
    s.aspect = 4.0f / 3.0f;
    if (!rd_Init(512, 512, &s, NULL)) {
        printf("SKIP rd_video_test: no usable Vulkan device\n");
        return 77;
    }
    printf("rd_video_test: adapter %s\n", rhi_AdapterName());
    testFlat();
    testRamp();
    testMirror();
    testCrt();
    testFit();
    rd_VideoShutdown();
    const uint32_t verr = rhi_vk_ValidationErrorCount();
    CHECK(verr == 0, "%u validation errors", verr);
    rd_Shutdown();
    if (failures) {
        printf("rd_video_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_video_test: all passed\n");
    return 0;
}
