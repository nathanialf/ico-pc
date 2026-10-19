/* tools/region_blocks.py --selftest input: one block per class. The self test
 * copies it to ico2/common/src/fixture.c in a scratch tree with this
 * directory's regions.txt (three rows: pal, jpn, us) and region_twins.txt,
 * converts it, and compares the result with expected.c, the report with
 * expected_report.tsv and `report --bits` with expected_bits.txt. */
#include "region.h"

/* header typedef: the layouts differ (layout-typedef) */
#ifdef VERSION_JPN
typedef struct {
    char path[48];
} TexRec;
#else
typedef struct {
    char path[48];
    int cameraMove;
} TexRec;
#endif

typedef struct {
    unsigned int word;
} BitWord;

typedef struct {
    BitWord modeBits;
    BitWord flags;
    int a900[10];
} MotionRec;

typedef struct {
    int curItem;
} Layout;

/* a macro (define) */
#ifdef VERSION_JPN
#define MAX_CHARS 1621
#else
#define MAX_CHARS 1637
#endif

/* data initialisers (data-twin), a scalar (extern-init), a dimension
   (array-size) */
#ifdef VERSION_JPN
int systemStatus[4] = {0, 2, 0, 7};
#else
int systemStatus[4] = {1, 2, 0, 7};
#endif
#ifdef VERSION_JPN
int iosCdvdMediaType = 1;
#else
int iosCdvdMediaType = 2;
#endif
#ifdef VERSION_JPN
static short stageOrientIdx[104];
#else
static short stageOrientIdx[128];
#endif

/* one region at file scope: kept for every region (A5a) */
#ifndef VERSION_JPN
int NonLinearCameraMove = 0;
void demoEnd(void) {}
#endif
#ifdef VERSION_JPN
void DummyIcoMisc(void);
#endif

/* a renamed function (rename) */
#ifdef VERSION_JPN
static void actEndLogoFade(void);
#else
static void actEndLogoChk(void);
#endif

extern MotionRec motionKind[];
extern float framePlayed;
extern char *work;
extern int stage_no;
extern void movie_init(int w, int h, int d);
extern void *alloc(int size, int line);
extern void stage_SetAnimation(int id);
extern int lt_current_property_item(void);
extern void strcpy_(char *dst, const char *src);
extern void gsb_ResetSnap(void);
extern void draw(int x, int y);
extern void set_mode(int m);

/* a whole-function twin (function-twin) */
#ifdef VERSION_JPN
static int mcCheck(void)
{
    return stage_no + 1;
}
#else
static int mcCheck(void)
{
    int r = stage_no;

    return r + 2;
}
#endif

float fx_tokens(char *file)
{
    /* token substitution (A1): a timing constant (review), a string, an id */
#ifdef VERSION_JPN
    strcpy_(file, "SCPS_110.03");
#else
    strcpy_(file, "SCES_507.60");
#endif
#ifdef VERSION_JPN
    stage_SetAnimation(621);
#else
    stage_SetAnimation(625);
#endif
    alloc(64, LN(560, 557));
#ifdef VERSION_JPN
    return framePlayed / 50.0f;
#else
    return framePlayed / 2997.0f;
#endif
}

int fx_layout(MotionRec *row)
{
    int k = 0;

    /* deny-listed: a bit shift, a raw offset after a pointer cast, the
       ActWork shift of 16, a member name */
#ifdef VERSION_JPN
    if ((row->modeBits.word >> 16) & 7) {
#else
    if ((row->modeBits.word >> 19) & 7) {
#endif
        k = 1;
    }
#ifdef VERSION_JPN
    *(int *)((char *)work + 0x830) = 20;
#else
    *(int *)((char *)work + 0x840) = 20;
#endif
#ifdef VERSION_JPN
    alloc(0x970, 907);
#else
    alloc(0x980, 907);
#endif
#ifdef VERSION_JPN
    row->modeHist[0] = 0;
#else
    row->a900[0] = 0;
#endif
    return k;
}

int fx_locals(void)
{
    /* an initialised against an uninitialised local (A1') */
#ifdef VERSION_JPN
    int n = 0;
#else
    int n;
#endif
    /* an automatic initialiser takes the selector; a static one cannot */
#ifdef VERSION_JPN
    int rect[4] = {80, 6, -160, 6};
#else
    int rect[4] = {80, 11, -160, 5};
#endif
#ifdef VERSION_JPN
    static const int anims[2] = {754, 755};
#else
    static const int anims[2] = {758, 759};
#endif
    n = rect[1] + anims[0];
    return n;
}

int fx_switches(int stage)
{
    /* every label in a block with one delta (A2) */
    switch (lt_current_property_item()) {
#ifdef VERSION_JPN
    case 0x14:
#else
    case 0x31:
#endif
        set_mode(2);
        break;
#ifdef VERSION_JPN
        return 0x11;
    case 0x15:
#else
        return 0x14;
    case 0x32:
#endif
        set_mode(3);
        break;
    }
    /* a label outside the blocks too (case-mixed) */
    switch (stage) {
#ifdef VERSION_JPN
    case 99:
#else
    case 101:
#endif
        set_mode(1);
        break;
    case 15:
        set_mode(0);
        break;
    }
    return 0;
}

void fx_statements(int video)
{
    /* one balanced span (A3') */
#ifdef VERSION_JPN
    movie_init(720, 480,
               36);
#else
    movie_init(720, video ? 576 : 480, 36);
#endif
    /* whole statements (A3) */
#ifdef VERSION_JPN
    set_mode(4);
    draw(1, 2);
#else
    if (video) {
        set_mode(5);
    }
    draw(3, 4);
#endif
    /* one-region statements (A4) */
#ifndef VERSION_JPN
    gsb_ResetSnap();
#endif
#ifdef VERSION_JPN

    draw(0, 0);

#endif
    /* a declaration in one region's arm (decl) */
#ifdef VERSION_JPN
    int extra = 3;
    draw(extra, extra);
#endif
}

void fx_three(void)
{
    /* three targets: the selector over the rows that differ (PAL_JPN_US,
       PAL_US, PAL_JPN), and an else-if chain */
#ifdef VERSION_US
    draw(31, 7);
    stage_SetAnimation(700);
    alloc(8, 1);
#elif defined(VERSION_JPN)
    draw(30, 9);
    stage_SetAnimation(696);
    alloc(4, 1);
#else
    draw(29, 9);
    stage_SetAnimation(700);
    alloc(4, 1);
#endif
#ifdef VERSION_US
    set_mode(9);
#elif defined(VERSION_JPN)
    if (stage_no) {
        set_mode(8);
    }
#else
    draw(stage_no, 0);
#endif
}

void fx_unbalanced(int video)
{
    /* the arms differ in more than a span and open a block (unbalanced) */
#ifdef VERSION_JPN
    if (video == 1) {
        draw(1, 1);
#else
    if (video == 2 && stage_no) {
        draw(2, 2);
#endif
    }
}

/* a twin done by hand: config/region_twins.txt names the pair, so a block
   inside either body is a `twin` line */
static void fx_twin_jpn(void)
{
    draw(0, 1);
}

void fx_twin(void)
{
    if (ICO_JPN) {
        fx_twin_jpn();
        return;
    }
#ifdef VERSION_JPN
    draw(5, 6);
#else
    draw(5, 7);
#endif
}

int fx_use(void)
{
    TexRec t;
    Layout l = {MAX_CHARS};

    demoEnd();
    return mcCheck() + (int)sizeof(t) + l.curItem + stageOrientIdx[0] + systemStatus[0] +
           iosCdvdMediaType + NonLinearCameraMove;
}
