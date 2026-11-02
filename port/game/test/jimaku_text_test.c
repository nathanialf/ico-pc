/*
 * port/game/test/jimaku_text_test.c
 *
 * jimakuDisp's community-translation hook (ico2/fumi/src/jimaku.c, compiled
 * in below so its statics are in reach; port/ui/subtitle_text.h), on the CPU,
 * with the rest of the game stubbed and the hook's two port calls recorded
 * (ui_subtitle_words answers as the test sets it, ui_subtitle_draw counts):
 *   - a disc language (the hook's words NULL): the record's two strips are
 *     drawn as before, display_texture's two packets (tex_TransTexture of the
 *     record's texture, one sprite each), texProperty 434 and 435 named, and
 *     no text; the hook is asked once, with the shown group's block and
 *     gFlagGameClear;
 *   - a community translation (words given): no strip and no texture
 *     transfer, one packet in display_texture's state (list 11, ALPHA
 *     (1, 7, 0), register 74, no Z) around one ui_subtitle_draw of the words
 *     with the block and the strips' colour (~reductionCol, alpha 0x80);
 *     the rows' texNo still set as the game sets them;
 *   - the subtitle's timing is the game's either way: the shown and the
 *     per-frame semaphores are signalled alike, jimakuDispOn is cleared at
 *     the display time in both;
 *   - photo mode: neither strips nor text; subtitles off (jimakuOn 0), a
 *     record without its picture (tex -1) or no subtitle (systemStatus[10]
 *     0): the hook is not asked and nothing is drawn;
 *   - the Japanese and Korean discs run the same arm (rows 410 / 411 and
 *     404 / 405), so the hook is asked there too (port/ui's answer is NULL
 *     on every disc but the European one: ctest subtitles_ru); the North
 *     American disc, the 2001 previews and the Asian disc have their own
 *     arms, which never ask it and draw their strips.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../core/region.h" /* ico_region_select: the other discs' runs */
/* the subtitle display, as the game's own source; stubs below satisfy it */
#include "../../../ico2/fumi/src/jimaku.c"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "jimaku_text: FAIL %s:%d: ", __FILE__, __LINE__);                      \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the game's globals jimaku.c reads ----------------------------------- */
int systemStatus[12];
int lock_execIcoMisc;
int GlobalTimer;
int NonLinearCameraMove = 2;
int gFlagGameClear;
StageSetting GlobalStageSetting;
LtProperty texProperty[512];
const JimakuFileName jimakuFileName[10];

/* --- what jimakuDisp calls, counted --------------------------------------- */
static struct {
    int trans, transNo, packets, open, ends, sprites, alpha, alphaMode, reg74, zOff;
    int signals, asked, askedBlock, askedClear, draws, drawBlock;
    const char *drawWords;
    unsigned char drawRgba[4];
    int photo;
    const char *words; /* ui_subtitle_words' answer */
} s;

static void resetCounts(void)
{
    const int photo = s.photo;
    const char *words = s.words;
    memset(&s, 0, sizeof(s));
    s.photo = photo;
    s.words = words;
}

int ico_photo_active(void)
{
    return s.photo;
}

const char *ui_subtitle_words(int block, int gameClear)
{
    s.asked++;
    s.askedBlock = block;
    s.askedClear = gameClear;
    return s.words;
}

void ui_subtitle_draw(const char *words, int block, const unsigned char rgba[4])
{
    s.draws++;
    s.drawWords = words;
    s.drawBlock = block;
    memcpy(s.drawRgba, rgba, 4);
}

int tex_TransTexture(int no, int pri)
{
    (void)pri;
    s.trans++;
    s.transNo = no;
    return 0;
}

void gif_StartPacketPri(int pri)
{
    s.packets += pri == 11;
    s.open++;
}

void gif_SetAlpha(long long alpha, long long mode, long long fix)
{
    s.alpha += alpha == 1 && fix == 0;
    s.alphaMode = (int)mode;
}

void gif_SetGsReg(long long reg, long long data)
{
    s.reg74 += reg == 74 && data == 0;
}

void gif_SetZWrite(int on)
{
    s.zOff += on == 0;
}

void gif_SetZTest(int on)
{
    (void)on;
}

void gif_SpriteSensitiveOffset(int *r, long long z, int *uv, JimCol *col, int prim)
{
    (void)r;
    (void)z;
    (void)uv;
    (void)col;
    (void)prim;
    s.sprites++;
}

void gif_EndPacket(void)
{
    s.ends++;
}

#ifdef ICO_RD
/* the window build's draw keys (JIM_HOST_KEY) */
void gif_HostDrawKey(const void *obj, int part, int ordinal)
{
    (void)obj;
    (void)part;
    (void)ordinal;
}
#endif

int iosSemaReferStatus(IosSema *self)
{
    self->status.numWaitThreads = 1;
    return 0;
}

int iosSemaSignal(IosSema *self)
{
    (void)self;
    s.signals++;
    return 0;
}

/* never reached by jimakuDisp (the manager's thread and its reads) */
int iosSemaCreate(IosSema *self, int initCount, int maxCount, int option)
{
    (void)self;
    (void)initCount;
    (void)maxCount;
    (void)option;
    return 0;
}

int iosSemaDelete(IosSema *self)
{
    (void)self;
    return 0;
}

int iosSemaWait(IosSema *self)
{
    (void)self;
    return 0;
}

void iosMsgQueueCreate(IosMsgQueue *q, IosMsgWord *buf, int size)
{
    (void)q;
    (void)buf;
    (void)size;
}

int iosMsgRecv(IosMsgQueue *q, IosMsgWord *out, int mode)
{
    (void)q;
    (void)out;
    (void)mode;
    return 0;
}

int iosMsgSend(IosMsgQueue *q, IosMsgWord val, int mode)
{
    (void)q;
    (void)val;
    (void)mode;
    return 0;
}

CdvdBgReq *iosCdvdBackGroundMgrAdd(const char *name, void *readFunc, void *readArg, void *readyFunc,
                                   void *resumeFunc, void *cbArg, void *closeFunc, void *closeArg)
{
    (void)name;
    (void)readFunc;
    (void)readArg;
    (void)readyFunc;
    (void)resumeFunc;
    (void)cbArg;
    (void)closeFunc;
    (void)closeArg;
    return NULL;
}

void iosCdvdBackGroundMgrDelete(CdvdBgReq *self)
{
    (void)self;
}

void iosCdvdBackGroundMgrSeek(CdvdBgReq *self, int val)
{
    (void)self;
    (void)val;
}

int iosCdvdBackGroundRead(CdvdBgReq *self, void *buf, int size)
{
    (void)self;
    (void)buf;
    (void)size;
    return 0;
}

TexData *tex_GetTextureData(int idx)
{
    (void)idx;
    return NULL;
}

int tex_InitTexture(char *name, void *pkt)
{
    (void)name;
    (void)pkt;
    return -1;
}

int tex_FreeTexture(int id)
{
    (void)id;
    return 0;
}

void tex_SetSamplingType(TexData *tex, int mag, int min)
{
    (void)tex;
    (void)mag;
    (void)min;
}

void debug_assert(const char *file, int line)
{
    (void)file;
    (void)line;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void ico_assert(const char *file, int line, const char *failedexpr)
{
    fprintf(stderr, "jimaku_text: assert %s:%d %s\n", file, line, failedexpr);
    failures++;
}

/* --- the runs ------------------------------------------------------------- */

static JimakuArg s_msg;

/* group 0 shown: record 7's picture loaded as texture 3, made at tick 1000;
   the tick 10 later (past the five frames, inside the display time) */
static void showRecord(void)
{
    memset(&s_msg, 0, sizeof(s_msg));
    s_msg.sub.n = 0;
    jimakuRing[0].block = 7;
    jimakuRing[0].tex = 3;
    jimakuRing[0].stamp = 1000;
    lock_execIcoMisc = GlobalTimer = 1010;
    systemStatus[0] = 0;
    systemStatus[1] = 1;
    systemStatus[10] = 1;
    jimakuDispOn = 1;
    jimakuDispTime = 120;
    jimakuOn = 1;
    GlobalStageSetting.reductionCol[0] = 0x80;
    GlobalStageSetting.reductionCol[1] = 0x40;
    GlobalStageSetting.reductionCol[2] = 0x20;
    for (int i = 0; i < 512; i++) {
        texProperty[i].texNo = -1;
        texProperty[i].texW = 128;
        texProperty[i].texH = 24;
    }
}

static void testDiscLanguage(const char *slug, int row)
{
    showRecord();
    s.words = NULL;
    resetCounts();
    gFlagGameClear = 1;
    jimakuDisp(&s_msg);
    CHECK(s.asked == 1 && s.askedBlock == 7 && s.askedClear == 1,
          "%s: the hook is asked about record 7 on a cleared game (%d, %d, %d)", slug, s.asked,
          s.askedBlock, s.askedClear);
    CHECK(s.sprites == 2 && s.trans == 2 && s.transNo == 3 && s.packets == 2 && s.ends == 2 &&
              s.draws == 0,
          "%s: the two strips and no text (sprites %d, transfers %d, packets %d, draws %d)", slug,
          s.sprites, s.trans, s.packets, s.draws);
    CHECK(texProperty[row].texNo == 3 && texProperty[row + 1].texNo == 3,
          "%s: rows %d and %d name the record's texture", slug, row, row + 1);
    CHECK(s.signals == 2, "%s: the shown and the per-frame semaphores (%d)", slug, s.signals);
    gFlagGameClear = 0;
}

static void testCommunity(void)
{
    static const char kWords[] = "\xD0\xA1\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xBE\nline 2";
    showRecord();
    s.words = kWords;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.asked == 1 && s.askedBlock == 7 && s.askedClear == 0,
          "community: the hook is asked about record 7 on a first play");
    CHECK(s.sprites == 0 && s.trans == 0, "community: no strip, no transfer (%d, %d)", s.sprites,
          s.trans);
    CHECK(s.packets == 1 && s.open == 1 && s.ends == 1 && s.alpha == 1 && s.alphaMode == 7 &&
              s.reg74 == 1 && s.zOff == 1,
          "community: one packet in display_texture's state (packets %d, ends %d, alpha %d/%d, "
          "74 %d, Z %d)",
          s.packets, s.ends, s.alpha, s.alphaMode, s.reg74, s.zOff);
    CHECK(s.draws == 1 && s.drawWords == kWords && s.drawBlock == 7,
          "community: the words drawn once for record 7 (%d)", s.draws);
    CHECK(s.drawRgba[0] == (unsigned char)~0x80 && s.drawRgba[1] == (unsigned char)~0x40 &&
              s.drawRgba[2] == (unsigned char)~0x20 && s.drawRgba[3] == 128,
          "community: the strips' colour (%d %d %d %d)", s.drawRgba[0], s.drawRgba[1],
          s.drawRgba[2], s.drawRgba[3]);
    CHECK(texProperty[434].texNo == 3 && texProperty[435].texNo == 3,
          "community: the rows still name the texture, as the game sets them");
    CHECK(s.signals == 2, "community: the semaphores as with the strips (%d)", s.signals);
    CHECK(jimakuDispOn == 1, "community: still shown inside the display time");

    /* the display time ends the text as it ends the strips */
    lock_execIcoMisc = 1000 + 121;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(jimakuDispOn == 0 && s.draws == 0 && s.sprites == 0 && s.asked == 0,
          "community: hidden after the display time (%d, %d)", s.draws, s.asked);
}

static void testHidden(void)
{
    static const char kWords[] = "text";
    s.words = kWords;
    /* photo mode */
    showRecord();
    s.photo = 1;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.draws == 0 && s.sprites == 0 && s.open == 0, "photo mode, text: nothing drawn");
    s.words = NULL;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.draws == 0 && s.sprites == 0 && s.open == 0, "photo mode, strips: nothing drawn");
    s.photo = 0;
    s.words = kWords;
    /* subtitles off, no picture, no subtitle */
    showRecord();
    jimakuOn = 0;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.asked == 0 && s.draws == 0 && s.sprites == 0, "subtitles off: the hook not asked");
    showRecord();
    jimakuRing[0].tex = -1;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.asked == 0 && s.draws == 0 && s.sprites == 0, "no picture: the hook not asked");
    showRecord();
    systemStatus[10] = 0;
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.asked == 0 && s.draws == 0 && s.sprites == 0, "no subtitle: the hook not asked");
}

/* the discs with their own arms: never asked, their strips drawn */
static void testOwnArm(const char *slug, int row)
{
    CHECK(ico_region_select(slug) == 0, "select %s", slug);
    showRecord();
    s.words = "text";
    resetCounts();
    jimakuDisp(&s_msg);
    CHECK(s.asked == 0 && s.draws == 0 && s.sprites == 2 && texProperty[row].texNo == 3,
          "%s: its own arm, the strips (asked %d, sprites %d)", slug, s.asked, s.sprites);
}

int main(void)
{
    CHECK(ico_region_select("pal") == 0, "the European disc");
    testDiscLanguage("pal", 434);
    testCommunity();
    testHidden();
    CHECK(ico_region_select("jpn") == 0, "the Japanese disc");
    testDiscLanguage("jpn", 410);
    CHECK(ico_region_select("kor") == 0, "the Korean disc");
    testDiscLanguage("kor", 404);
    testOwnArm("ntsc", 382);
    testOwnArm("aug6", 382);
    testOwnArm("jun26", 382);
    testOwnArm("cn", 410);
    ico_region_select("pal");
    printf("jimaku_text: %s\n", failures ? "failed" : "ok");
    return failures ? 1 : 0;
}
