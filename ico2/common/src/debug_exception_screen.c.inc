#include <string.h>
#include <stdio.h>
#include <libdma.h>
#include <libvu0.h>
#include <eekernel.h>

/* debug_exception_screen.c.inc -- the debug screen primitives (Vif1 direct packets, GS draw environments, the 26x256 text
 * screen, sprite/font blits). #included once at the top of debug_exception.c; debug.c calls them as externs.
 * The three Vif1/GIF helpers are inline (they inline into drawSprite/PutFont and their out-of-line copies close the
 * object); the rest are ordinary functions in this order. */
/* libpkt.h's entry points, which this TU does not include */
extern void sceVif1PkCloseDirectCode();
extern void sceVif1PkCloseGifTag();
extern void sceVif1PkEnd();
extern void sceVif1PkTerminate();

/* The four GS environment packets the screen sends with sceGsPutDrawEnv: a
   GIF tag (NLOOP registers in PACKED A+D, EOP) and the register writes after
   it, qword aligned for the DMA.  Each Set...Environment function fills its
   own packet's registers through the uncached mirror before sending it.
   A texture packet carries TEST_1, the four alpha pairs sceGsSetDefAlphaEnv
   fills and the four texture pairs sceGsSetDefTexEnv fills; the draw packet
   carries the eight pairs of a draw environment. */
typedef struct { /* field names derived */
    unsigned long long giftag[2];
    unsigned long long test;
    unsigned long long test_addr;
    long long alpha[8];
    sceGsTexEnv tex;
} DbgTexPacket; /* derived name */

typedef struct { /* field names derived */
    unsigned long long giftag[2];
    sceGsDrawEnv draw;
} DbgDrawPacket; /* derived name */

static DbgTexPacket texFrameEnv
    __attribute__((aligned(16))) = {{0x1000000000008009LL, 0xE}}; /* derived name */

static DbgDrawPacket texDrawEnv
    __attribute__((aligned(16))) = {{0x1000000000008008LL, 0xE}}; /* derived name */

static DbgTexPacket drawnTexEnv
    __attribute__((aligned(16))) = {{0x1000000000008009LL, 0xE}}; /* derived name */

static DbgTexPacket drawEnv
    __attribute__((aligned(16))) = {{0x1000000000008002LL, 0xE}}; /* derived name */

typedef ICO_UQW u128_dbgscr;

/* a GIF tag as the two 64-bit words the packet takes it in, on the
   quadword boundary the GIF reads it at */
typedef unsigned long long GifTag[2] __attribute__((aligned(16))); /* derived name */

/* sceVif1Packet: 0x20 bytes (two per drawSprite frame at sp+0x00 / sp+0x20).
 * The fields are the libpkt bodies' (sce/libpkt/libpkt.h): the next word,
 * the buffer start, the open DMA tag, DIRECT code and GIF tag; nothing
 * reads 0x10 or 0x18. */
typedef struct { /* field names derived */
    int *cur;
    int *base;
    int *dmaTag;
    int *directTag;
    int *pad10;
    int *gifTag;
    int pad18[2];
} Vif1Packet; /* derived name */

/* as in libpkt.h, which this TU does not include */
extern void sceVif1PkInit();
extern void sceVif1PkReset();
extern void sceVif1PkCnt();
extern void sceVif1PkOpenDirectCode();
/* this TU passes void * and u128_dbgscr; libpkt.h declares sceVif1Packet * and u_long128 */
extern void sceVif1PkOpenGifTag(void *packet, u128_dbgscr tag);
/* this TU passes void * and unsigned long long; libpkt.h declares int ** and long long */
extern void sceVif1PkAddGsData(void *packet, unsigned long long data);

/* The packet side OpenVif1DirectPacket builds into, flipped every packet, and
   the Q words SetPrimColor and SetPrimColorTex send with the colour (1.0f). */
static int vif1PacketSide = 0; /* derived name */

static unsigned int primQ = 0x3F800000; /* derived name */

static unsigned int primTexQ = 0x3F800000; /* derived name */

/* GIF tags for the sprite packets, PACKED with EOP and NLOOP left for
   sceVif1PkCloseGifTag: UV and XYZ2 for a textured sprite, XYZ2 alone for a
   flat one. */
static GifTag spriteTexTag = {0x2400000000008000LL, 0x43}; /* derived name */

static GifTag spriteTag = {0x1400000000008000LL, 0x4}; /* derived name */

/* The colour PutFont draws with, set per character from its screen cell. */
static int fontR = 255; /* derived name */

static int fontG = 255; /* derived name */

static int fontB = 255; /* derived name */

/* The font's tag: XYZ2 pairs, one sprite per lit pixel. */
static GifTag fontTag = {0x1400000000008000LL, 0x4}; /* derived name */

typedef struct { /* field names derived */
    int col;
    char ch;
} DbgChar; /* derived name */

/* The 26-line, 256-column character screen the report is printed into. */
static DbgChar charScreen[26][256]; /* derived name */

/* The blank a cleared or scrolled cell takes, the first line shown, and the
   print cursor. */
static DbgChar blankChar = {0, ' '}; /* derived name */

static int scrollTop = 0; /* derived name */

static int cursorX = 0; /* derived name */

static int cursorY = 0; /* derived name */

/* The cursor blink's phase, stepped every frame. */
static int blinkPhase = 0; /* derived name */

inline void OpenVif1DirectPacket(void *packet)
{
    sceVif1PkInit(packet, ICO_SPR_ADDR(vif1PacketSide << 13));
    sceVif1PkReset(packet);
    sceVif1PkCnt(packet, 0);
    sceVif1PkOpenDirectCode(packet, 0);
    vif1PacketSide = (vif1PacketSide + 1) & 1;
}

void CloseVif1DirectPacket(int pkt)
{
    sceVif1PkCloseGifTag(pkt);
    sceVif1PkCloseDirectCode(pkt);
    sceVif1PkEnd(pkt, 0);
    sceVif1PkTerminate(pkt);
}

void SendVif1DirectPacket(int *self)
{
    DmaChan *p;
    sceGsSyncPath(0, 0);
    p = sceDmaGetChan(1);
    p->chcr |= 0x40;
    sceDmaSend(p, (self[0x4 / 4] & 0x3FF0) | 0x80000000);
}

void RestoreNormalDrawEnvironment(sceGsDBuff *db, int id, int field)
{
    sceGsDBuff *u = (sceGsDBuff *)ICO_UNCACHED(ICO_ADDR(db));
    if (id != 0) {
        sceGsSetHalfOffset(&u->draw1, 0x800, 0x800, (short)field);
        sceGsPutDrawEnv(u->giftag1);
    } else {
        sceGsSetHalfOffset(&u->draw0, 0x800, 0x800, (short)field);
        sceGsPutDrawEnv(u->giftag0);
    }
}

void SetTextureWithFrameBuffer(int x)
{
    DbgTexPacket *env;

    env = (DbgTexPacket *)ICO_UNCACHED(ICO_ADDR(&texFrameEnv));
    sceGsSetDefTexEnv(&env->tex, 0, (short)x, (short)(ScreenWidth / 64), 1, 10, 8, 0, 0, 0, 0, 1);
    sceGsSetDefAlphaEnv(env->alpha, 0);
    env->alpha[0] = 0x44;
    env->test = 0x20000;
    env->test_addr = 0x47;
    env->tex.clamp = 0;
    env->tex.tex1 = 0x61;
    sceGsSyncPath(0, 0);
    sceGsPutDrawEnv(env);
}

void SetTexDrawEnvironment(int addr)
{
    DbgDrawPacket *env;

    env = (DbgDrawPacket *)ICO_UNCACHED(ICO_ADDR(&texDrawEnv));
    sceGsSetDefDrawEnv(&env->draw, 0, (short)ScreenWidth, (short)ScreenHeight, 1, 0x31);
    env->draw.frame = (env->draw.frame & ~0x1FF) | ((addr / 32) & 0x1FF);
    env->draw.zbuf |= 0x100000000;
    sceGsSyncPath(0, 0);
    sceGsPutDrawEnv(env);
}

void SetDrawnTextureEnvironment(int x)
{
    DbgTexPacket *env;

    env = (DbgTexPacket *)ICO_UNCACHED(ICO_ADDR(&drawnTexEnv));
    sceGsSetDefTexEnv(&env->tex, 0, (short)x, (short)(ScreenWidth / 64), 0, 10, 8, 0, 0, 0, 0, 1);
    sceGsSetDefAlphaEnv(env->alpha, 0);
    env->alpha[0] = 0x44;
    env->test = 0x20000;
    env->test_addr = 0x47;
    env->tex.clamp = 0;
    env->tex.tex1 = 0x61;
    env->alpha[6] = 0;
    sceGsSyncPath(0, 0);
    sceGsPutDrawEnv(env);
}

void SetDrawEnvironment(int mode)
{
    DbgTexPacket *env;

    env = (DbgTexPacket *)ICO_UNCACHED(ICO_ADDR(&drawEnv));
    sceGsSetDefAlphaEnv(env->alpha, 0);
    switch (mode) {
    case 0:
        env->alpha[0] = 0x48;
        break;
    case 1:
        env->alpha[0] = 0x44;
        break;
    case 2:
        env->alpha[0] = 0x42;
        break;
    case 3:
        env->alpha[0] = 0x0000002800000064ULL;
        break;
    case 4:
        env->alpha[0] = 0x68;
        break;
    }
    env->test = 0x20000;
    env->test_addr = 0x47;
    sceGsSyncPath(0, 0);
    sceGsPutDrawEnv(env);
}

inline void SetPrimColor(int prim, int r, int g, int b, int a)
{
    /* REGLIST with EOP, PRIM then RGBAQ, NLOOP left for sceVif1PkCloseGifTag */
    static const unsigned long long primTag[2]
        __attribute__((aligned(16))) = {0x2400000000008000LL, 0x10}; /* derived name */
    Vif1Packet packet;
    OpenVif1DirectPacket(&packet);
    sceVif1PkOpenGifTag(&packet, *(u128_dbgscr *)primTag);
    sceVif1PkAddGsData(&packet, prim | 0x40);
    sceVif1PkAddGsData(&packet, (unsigned long long)r | ((unsigned long long)g << 8) |
                                    ((unsigned long long)b << 16) | ((unsigned long long)a << 24) |
                                    ((unsigned long long)primQ << 32));
    CloseVif1DirectPacket((int)&packet);
    SendVif1DirectPacket((int *)&packet);
}

inline void SetPrimColorTex(int prim, int r, int g, int b, int a, int fst)
{
    /* the same tag, PRIM then RGBAQ */
    static const unsigned long long primTag[2]
        __attribute__((aligned(16))) = {0x2400000000008000LL, 0x10}; /* derived name */
    Vif1Packet packet;
    unsigned long long data;
    OpenVif1DirectPacket(&packet);
    sceVif1PkOpenGifTag(&packet, *(u128_dbgscr *)primTag);
    data = prim | 0x10;
    if (fst != 0) {
        data |= 0x100;
    }
    sceVif1PkAddGsData(&packet, data);
    sceVif1PkAddGsData(&packet, (unsigned long long)r | ((unsigned long long)g << 8) |
                                    ((unsigned long long)b << 16) | ((unsigned long long)a << 24) |
                                    ((unsigned long long)primTexQ << 32));
    CloseVif1DirectPacket((int)&packet);
    SendVif1DirectPacket((int *)&packet);
}

static void drawSprite(int r, int g, int b, int a, int x0, int y0, int x1, int y1, int tex)
{
    Vif1Packet packet;

    if (tex != 0) {
        SetPrimColorTex(6, r, g, b, a, 1);
        OpenVif1DirectPacket(&packet);
        sceVif1PkOpenGifTag(&packet, *(u128_dbgscr *)spriteTexTag);
        sceVif1PkAddGsData(&packet, 0);
        sceVif1PkAddGsData(&packet, (unsigned long long)((x0 + 0x800) << 4) |
                                        ((unsigned long long)((y0 + 0x800) << 4) << 16) |
                                        0x0080000000000000ULL);
        sceVif1PkAddGsData(&packet, (unsigned long long)(ScreenWidth << 4) |
                                        ((unsigned long long)(ScreenHeight << 4) << 16));
        sceVif1PkAddGsData(&packet, (unsigned long long)((x1 + 0x800) << 4) |
                                        ((unsigned long long)((y1 + 0x800) << 4) << 16) |
                                        0x0080000000000000ULL);
        CloseVif1DirectPacket((int)&packet);
        SendVif1DirectPacket((int *)&packet);
    } else {
        SetPrimColor(6, r, g, b, a);
        OpenVif1DirectPacket(&packet);
        sceVif1PkOpenGifTag(&packet, *(u128_dbgscr *)spriteTag);
        sceVif1PkAddGsData(&packet, (unsigned long long)((x0 + 0x800) << 4) |
                                        ((unsigned long long)((y0 + 0x800) << 4) << 16) |
                                        0x0080000000000000ULL);
        sceVif1PkAddGsData(&packet, (unsigned long long)((x1 + 0x800) << 4) |
                                        ((unsigned long long)((y1 + 0x800) << 4) << 16) |
                                        0x0080000000000000ULL);
        CloseVif1DirectPacket((int)&packet);
        SendVif1DirectPacket((int *)&packet);
    }
}

void PutFont(int c, int x, int y)
{
    Vif1Packet packet;
    int i, j;
    int y2 = y + 2;

    SetPrimColor(6, fontR >> 2, fontG >> 2, fontB >> 2, 0x40);
    OpenVif1DirectPacket(&packet);
    sceVif1PkOpenGifTag(&packet, *(u128_dbgscr *)fontTag);
    for (j = 0; j < 8; j++) {
        int oy = j * 2 + 0x800;
        for (i = 0; i < 8; i++) {
            if ((dbgFont[j + c * 8] >> i) & 1) {
                sceVif1PkAddGsData(&packet, (unsigned long long)((x + i - 2 + 0x800) << 4) |
                                                ((unsigned long long)((y + oy - 1) << 4) << 16) |
                                                0x0080000000000000ULL);
                sceVif1PkAddGsData(&packet, (unsigned long long)((x + i + 3 + 0x800) << 4) |
                                                ((unsigned long long)((y2 + oy) << 4) << 16) |
                                                0x0080000000000000ULL);
            }
        }
    }
    CloseVif1DirectPacket((int)&packet);
    SendVif1DirectPacket((int *)&packet);

    SetPrimColor(0, fontR, fontG, fontB, 0x80);
    OpenVif1DirectPacket(&packet);
    sceVif1PkOpenGifTag(&packet, *(u128_dbgscr *)fontTag);
    for (j = 0; j < 8; j++) {
        int oy = j * 2 + 0x800;
        for (i = 0; i < 8; i++) {
            if ((dbgFont[j + c * 8] >> i) & 1) {
                sceVif1PkAddGsData(&packet, (unsigned long long)((x + i + 0x800) << 4) |
                                                ((unsigned long long)((y + oy) << 4) << 16) |
                                                0x0080000000000000ULL);
            }
        }
    }
    CloseVif1DirectPacket((int)&packet);
    SendVif1DirectPacket((int *)&packet);
}

static void resetPath(void)
{
    sceDmaReset(1);
    sceDmaSync(sceDmaGetChan(0), 0, 100);
    sceDmaSync(sceDmaGetChan(1), 0, 100);
    sceDmaSync(sceDmaGetChan(2), 0, 100);
    *(int *)sceDmaGetChan(0) |= 0x40;
    *(int *)sceDmaGetChan(1) |= 0x40;
    *(int *)sceDmaGetChan(2) |= 0x40;
    sceGsResetPath();
}

static void resetGS(void)
{
    sceVpu0Reset();
    gsb_Init(&db);
}

static inline void scrollDbgScreen(void) /* derived name */
{
    int i, j;

    for (i = 1; i < 26; i++) {
        for (j = 0; j < 256; j++) {
            charScreen[i - 1][j] = charScreen[i][j];
        }
    }
    for (j = 0; j < 256; j++) {
        charScreen[25][j] = blankChar;
    }
}

static void putString(unsigned int color, char *fmt, ...)
{
    char buf[256];
    char *str = buf;
    unsigned int i;

    vsprintf(buf, fmt, (char *)__builtin_next_arg(fmt) - 0x30);
    for (i = 0; i < strlen(str); i++) {
        if (str[i] == '\t') {
            cursorX += 2;
        } else if (str[i] == '\n') {
            cursorY++;
            cursorX = 0;
        } else {
            charScreen[cursorY][cursorX].col = color;
            charScreen[cursorY][cursorX].ch = str[i];
            cursorX++;
        }
        if (cursorX >= 256) {
            cursorX = 255;
        }
        if (cursorY >= 26) {
            scrollDbgScreen();
            cursorY = 25;
        }
    }
}

static void drawWin(void)
{
    int i, j, k;
    int len = 50;
    int st;

    for (i = 0; i < 26; i++) {
        for (j = 255; charScreen[i][j].ch == ' ' && j >= 0; j--) {}
        j++;
        if (len < j)
            len = j;

        if (j < 50) {
            st = 0;
        } else if (j - scrollTop - 50 >= 0) {
            st = scrollTop;
        } else {
            st = j - 50;
        }
        for (k = 0; k < 50; k++) {
            DbgChar *p = &charScreen[i][st + k];
            if (p->ch != ' ') {
                fontR = ((p->col >> 24) & 0xFF) * 0.8f;
                fontG = ((p->col >> 16) & 0xFF) * 0.8f;
                fontB = ((p->col >> 8) & 0xFF) * 0.8f;
                PutFont(p->ch, k * 10 - 250, (i - 13) * 16);
            }
        }
    }
    if (len - scrollTop - 50 < 0) {
        scrollTop = len - 50;
    }
}

static void saveBack(void)
{
    SetDrawEnvironment(1);
    drawSprite(0x10, 0x20, 0x30, 0x40, -0x136, -0x6B, 0x136, 0x6B, 0);
    SetDrawEnvironment(1);
}

static inline void putBaseChar(int c) /* derived name */
{
    unsigned int col = 0xFFFFFF00;
    charScreen[cursorY][cursorX].col = col;
    charScreen[cursorY][cursorX].ch = c;
}

static inline void dispBase(void) /* derived name */
{
    int c;
    switch (blinkPhase) {
    case 0:
    default:
        c = '-';
        break;
    case 1:
        c = '/';
        break;
    case 2:
        c = '|';
        break;
    case 3:
        c = '\\';
        break;
    }
    putBaseChar(c);
    blinkPhase = (blinkPhase + 1) & 3;
}

static void baseFunc(void)
{
    if (pad[0].rep & 0x8000)
        if (--scrollTop < 0)
            scrollTop = 0;
    if (pad[0].rep & 0x2000)
        if (++scrollTop >= 50)
            scrollTop = 49;
    SetDrawEnvironment(1);
    dispBase();
    drawWin();
}

static void syncGS(void)
{
    FlushCache(0);
    sceGsSyncPath(1, 0);
    odd_even = sceGsSyncV(0);
    frame_count++;
    buffer_ID = frame_count & 1;
    gsb_Reduction();
    sceGsSwapDBuff(&db, buffer_ID);
    RestoreNormalDrawEnvironment(&db, buffer_ID, odd_even);
    SetDrawEnvironment(1);
}
