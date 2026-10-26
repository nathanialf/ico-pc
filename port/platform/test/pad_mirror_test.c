/*
 * port/platform/test/pad_mirror_test.c
 *
 * Issue 144: in Mirror mode the d-pad's Left walked Ico right.  The game turns
 * the d-pad into Ico's stick vector in iosPadGetStick (fumi/ios/pad.c, the
 * `simulate` branch, both the European and the 2001-disc copy) when the stick
 * is centred; the port mirrors the sticks but not that vector.  This feeds the
 * game's own iosPadGetStick a centred stick and the d-pad bits and checks the
 * sign of the synthesised x on both copies (ico_region pal and ntsc):
 *
 *   mirror off: LEFT pulls x below the centre, RIGHT above it (as retail)
 *   mirror on:  LEFT above, RIGHT below
 *   y (Up/Down) and a neutral d-pad are the same either way
 *   a deflected stick is not touched by the d-pad branch
 */
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "ios.h"
#include "main.h"
#include "memory.h"
#include "pad.h"
#include "region.h"

#include "../../game/options.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

int odd_even;
int frame_count;
char *matrixptr;
int debug_hand_camera;
int systemStatus[12];
PadState pad[16];
int IosPadLock;
IosMemPart *ios_partition_root;
IosMemPart *ios_partition_event;

float FSqrt(float x);
void sceVu0ApplyMatrix(void *dst, void *m, void *v);
void sceVu0Normalize(void *dst, void *src);
void sceVu0TransposeMatrix(void *dst, void *src);

void debug_StdPrintfDummy(const char *fmt, ...);
void debug_assertMessage(const char *file, int line, const char *mes);
void debug_assert(const char *file, int line);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("FAIL debug_assertMessage %s:%d: %s\n", file, line, mes);
    fails++;
}

void debug_assert(const char *file, int line)
{
    printf("FAIL debug_assert %s:%d\n", file, line);
    fails++;
}

int odd_even;
int frame_count;
char *matrixptr;
int debug_hand_camera;
int systemStatus[12];
PadState pad[16];
int IosPadLock;
IosMemPart *ios_partition_root;
IosMemPart *ios_partition_event;
const PadActDef shockList[1] = {{0}};

/* the camera and stick arithmetic pad.c links against (port/math and the game
   in the program): not read by the Start poll, so inert here */
float FSqrt(float x);
void sceVu0ApplyMatrix(void *dst, void *m, void *v);
void sceVu0Normalize(void *dst, void *src);
void sceVu0TransposeMatrix(void *dst, void *src);

float FSqrt(float x)
{
    float r = x > 1.0f ? x : 1.0f;
    int i;

    for (i = 0; x > 0.0f && i < 16; i++) {
        r = 0.5f * (r + x / r);
    }
    return x > 0.0f ? r : 0.0f;
}

void sceVu0ApplyMatrix(void *dst, void *m, void *v)
{
    (void)m;
    memcpy(dst, v, 16);
}

void sceVu0Normalize(void *dst, void *src)
{
    memcpy(dst, src, 16);
}

void sceVu0TransposeMatrix(void *dst, void *src)
{
    memcpy(dst, src, 64);
}

float _GetDirection(float *dir);
void _PushVu0Registers(void);
void _PopVu0Registers(void);

float _GetDirection(float *dir)
{
    (void)dir;
    return 0.0f;
}

void _PushVu0Registers(void) {}

void _PopVu0Registers(void) {}

#define DPAD_UP 0x1000
#define DPAD_RIGHT 0x2000
#define DPAD_DOWN 0x4000
#define DPAD_LEFT 0x8000

static IosPadDevRec s_rec;
static IosPadCtx s_ctx;

/* one centred-stick read with the d-pad word `bits`, the stick starting at the
   idle (127, 127) the actor keeps */
static void read_stick(unsigned int bits, int lx, int ly, IosPadStick *st)
{
    memset(&s_rec, 0, sizeof s_rec);
    memset(&s_ctx, 0, sizeof s_ctx);
    s_rec.buf[0].lx = (unsigned char)lx;
    s_rec.buf[0].ly = (unsigned char)ly;
    s_ctx.dev = &s_rec;
    s_ctx.now = bits;
    memset(st, 0, sizeof *st);
    st->x = 127;
    st->y = 127;
    CHECK(iosPadGetStick(&s_ctx, st, 0, 0, 0, 1) == 0);
}

static void check_region(int region)
{
    IosPadStick st;
    int mirror;

    ico_region = region;
    for (mirror = 0; mirror <= 1; mirror++) {
        ico_opt_set_mirror(mirror);
        CHECK(ico_opt_mirror() == mirror);

        read_stick(DPAD_LEFT, 127, 127, &st);
        CHECK(mirror ? st.x > 127 : st.x < 127);
        read_stick(DPAD_RIGHT, 127, 127, &st);
        CHECK(mirror ? st.x < 127 : st.x > 127);
        /* the two cases are exact mirrors of each other */
        {
            IosPadStick l, r;

            read_stick(DPAD_LEFT, 127, 127, &l);
            read_stick(DPAD_RIGHT, 127, 127, &r);
            CHECK(l.x + r.x == 254);
        }
        /* Up and Down are the same in both modes */
        read_stick(DPAD_UP, 127, 127, &st);
        CHECK(st.y < 127 && st.x == 127);
        read_stick(DPAD_DOWN, 127, 127, &st);
        CHECK(st.y > 127 && st.x == 127);
        /* nothing pressed: no push either way */
        read_stick(0, 127, 127, &st);
        CHECK(st.x == 127 && st.y == 127);
        /* a deflected stick wins over the d-pad (the port flips it in vpad.c) */
        read_stick(DPAD_LEFT, 255, 127, &st);
        CHECK(st.x == 255);
    }
    ico_opt_set_mirror(0);
}

int main(void)
{
    check_region(ICO_REGION_PAL);
    check_region(ICO_REGION_NTSC);
    if (fails) {
        printf("pad_mirror_test: %d failures\n", fails);
        return 1;
    }
    printf("pad_mirror_test: ok\n");
    return 0;
}
