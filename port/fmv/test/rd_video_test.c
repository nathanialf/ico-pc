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
 *
 * Exit 77 without a device.
 */
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
    rd_VideoSetMirror(1);
    CHECK(rd_VideoFrame(s_y, s_u, s_v, pitch, OW, OH) == 0, "mirror frame");
    if (readOut() == 0) {
        int bad = checkRamp(1);
        CHECK(bad == 0, "mirror on, toggle on: %d pixels differ from the flipped model", bad);
    }
    rd_VideoSetMirror(0);
    CHECK(rd_VideoFrame(s_y, s_u, s_v, pitch, OW, OH) == 0, "mirror frame");
    if (readOut() == 0) {
        int bad = checkRamp(0);
        CHECK(bad == 0, "mirror on, toggle off: %d pixels differ from the unflipped model", bad);
    }
    setMirror(0);
    rd_VideoSetMirror(1);
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
