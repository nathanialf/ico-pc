/* tools/region_blocks.py --selftest input: one block per class. The self test
 * copies it to ico2/common/src/fixture.c in a scratch tree with this
 * directory's regions.txt (six rows: pal, jpn, kor, ntsc, aug6, jun26) and
 * region_twins.txt, converts it, and compares the result with expected.c,
 * the report with expected_report.tsv and `report --bits` with
 * expected_bits.txt. */
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
    alloc(64, LN(560, 557, 557, 560, 561, 562));
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
    /* three targets: the selector over the rows that differ (PAL_JPN_NTSC,
       PAL_NTSC, PAL_JPN), and an else-if chain */
#ifdef VERSION_NTSC
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
#ifdef VERSION_NTSC
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

/* the Korean build is the Japanese program: arms shared by two rows
   (an `#if` naming both macros joined by ||, the set JPNKOR), a KOR
   block nested in a shared arm (its #else is the Japanese text), a KOR
   block beside PAL text (its #else serves every other row), the negated
   pair, an #elif with ||, and a JPN-only block the other rows skip */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
void DummyIcoKor(void);
#endif
#if defined(VERSION_JPN) || defined(VERSION_KOR)
int saveSeg[3] = {0, 0x1E8, 4};
#else
int saveSeg[3] = {0, 0x1F8, 4};
#endif
extern int viewScale;

void fx_shared(MotionRec *row, int video, int lsn)
{
    /* shared token and span */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    stage_SetAnimation(621);
#else
    stage_SetAnimation(625);
#endif
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    movie_init(720, 480, 36);
#else
    movie_init(720, video ? 576 : 480, 36);
#endif
    /* a bit position in a shared arm: one --bits row per row of the set */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    draw((row->modeBits.word >> 16) & 7, 0);
#else
    draw((row->modeBits.word >> 19) & 7, 0);
#endif
    /* shared statements, shared one-region, the negated pair */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    set_mode(4);
#else
    if (video) {
        set_mode(5);
    }
#endif
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    draw(0, 0);
#endif
#if !defined(VERSION_JPN) && !defined(VERSION_KOR)
    gsb_ResetSnap();
#endif
    /* KOR nested in a shared arm: the three texts, flat already */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
#ifdef VERSION_KOR
    strcpy_((char *)work, "SCPS_560.01");
#else
    strcpy_((char *)work, "SCPS_110.03");
#endif
#else
    strcpy_((char *)work, "SCES_507.60");
#endif
    /* KOR nested beside shared lines: rebuilt flat, the shared lines in
       both arms; a value two rows share takes one argument */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    draw(1, 2);
#ifdef VERSION_KOR
    set_mode(5);
#else
    set_mode(6);
#endif
#else
    draw(3, 4);
    set_mode(7);
#endif
    /* the same as statements: an else-if chain over the three texts */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    draw(1, 2);
#ifdef VERSION_KOR
    gsb_ResetSnap();
#endif
#else
    set_mode(7);
#endif
    /* KOR beside PAL: the #else serves every other row */
#ifdef VERSION_KOR
    set_mode(5);
#else
    set_mode(11);
#endif
#ifdef VERSION_KOR
    lsn += 153600;
#endif
    /* an #elif with || */
#ifdef VERSION_NTSC
    draw(25, 7);
#elif defined(VERSION_JPN) || defined(VERSION_KOR)
    draw(26, 9);
#else
    draw(29, 9);
#endif
    /* JPN only: the other rows take PAL's (empty) arm */
#ifdef VERSION_JPN
    viewScale = 150;
#endif
    draw(lsn, 0);
}

int fx_shared_switch(int stage)
{
    /* per-row deltas: KOR's ids 6 lower than JPN's (case-shift per row) */
    switch (lt_current_property_item()) {
#if defined(VERSION_JPN) || defined(VERSION_KOR)
#ifdef VERSION_KOR
    case 8:
#else
    case 14:
#endif
#else
    case 44:
#endif
        set_mode(1);
        break;
#if defined(VERSION_JPN) || defined(VERSION_KOR)
#ifdef VERSION_KOR
    case 9:
#else
    case 15:
#endif
#else
    case 45:
#endif
        set_mode(0);
        break;
    }
    /* one delta for the shared arm: the set's selector */
    switch (stage) {
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    case 0x14:
#else
    case 0x31:
#endif
        set_mode(2);
        break;
    }
    return 0;
}

/* the three later targets (upstream's docs/TARGETS.md shapes): blocks
   nested to four deep, each line's rows the intersection of the conditions
   around it, a row no condition names taking the PAL row's arm */
extern void lt_switch_layout(int id);
extern void gflagOn(int id);
extern int enable_game_pause;

void fx_nested(int video)
{
    /* NTSC nested in the PAL arm of a shared Japanese block: one token over
       three texts */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    lt_switch_layout(51);
#else
#ifdef VERSION_NTSC
    lt_switch_layout(50);
#else
    lt_switch_layout(54);
#endif
#endif
    /* AUG6 nested in the arm of the Japanese lines it shares */
#if defined(VERSION_JPN) || defined(VERSION_KOR) || defined(VERSION_AUG6)
#ifdef VERSION_AUG6
    draw(38, 1);
#else
    draw(40, 1);
#endif
#else
    draw(36, 1);
#endif
    /* AUG6 nested twice in a USA arm beside shared lines (st22a.c): rebuilt
       flat, the shared line in each arm, the story flags and the animation
       per row (the animation deltas are review lines) */
#if defined(VERSION_NTSC) || defined(VERSION_AUG6)
#ifdef VERSION_AUG6
    gflagOn(293);
#else
    gflagOn(294);
#endif
    lt_switch_layout(51);
#ifdef VERSION_AUG6
    gflagOn(294);
    stage_SetAnimation(683);
#else
    gflagOn(295);
    stage_SetAnimation(684);
#endif
#else
    gflagOn(323);
    lt_switch_layout(51);
    gflagOn(324);
    stage_SetAnimation(758);
#endif
    /* two deep: the demo inside the prototype inside the USA arm (a mail id
       per row) */
#if defined(VERSION_NTSC) || defined(VERSION_AUG6) || defined(VERSION_JUN26)
#if defined(VERSION_AUG6) || defined(VERSION_JUN26)
#ifdef VERSION_JUN26
    draw(377, 0);
#else
    draw(393, 0);
#endif
#else
    draw(397, 0);
#endif
#else
    draw(430, 0);
#endif
    /* three deep, whole statements: an else-if chain over five texts, the
       demo's and the prototype's sharing a line */
#if defined(VERSION_JPN) || defined(VERSION_KOR)
    set_mode(1);
#else
#if defined(VERSION_NTSC) || defined(VERSION_AUG6) || defined(VERSION_JUN26)
#if defined(VERSION_AUG6) || defined(VERSION_JUN26)
#ifdef VERSION_JUN26
    enable_game_pause = 0;
#endif
    gsb_ResetSnap();
#else
    set_mode(2);
#endif
#else
    if (video) {
        set_mode(3);
    }
#endif
#endif
    /* PAL-only lines: the three-name condition with the prototype's
       exclusion nested (a four-name line would pass the column limit), then
       with the demo's too */
#if !defined(VERSION_JPN) && !defined(VERSION_KOR) && !defined(VERSION_NTSC)
#ifndef VERSION_AUG6
    draw(7, 7);
#endif
#endif
#if !defined(VERSION_JPN) && !defined(VERSION_KOR) && !defined(VERSION_NTSC)
#if !defined(VERSION_AUG6) && !defined(VERSION_JUN26)
    draw(8, 8);
#endif
#endif
    /* the demo's own block beside the four-name condition (e3.c): the two
       texts are one, so one set of five rows */
#ifdef VERSION_JUN26
    set_mode(12);
#else
#if defined(VERSION_JPN) || defined(VERSION_KOR) || defined(VERSION_NTSC) || defined(VERSION_AUG6)
    set_mode(12);
#else
    gsb_ResetSnap();
#endif
#endif
    /* the same over a nested #elif */
#if defined(VERSION_NTSC) || defined(VERSION_AUG6) || defined(VERSION_JUN26)
#ifdef VERSION_JUN26
    draw(13, 3);
#elif defined(VERSION_AUG6)
    draw(13, 2);
#else
    draw(13, 1);
#endif
#else
    draw(13, 0);
#endif
}

/* a whole function body in the demo's arm, past LARGE_LINES (large-variant) */
void fx_large(void)
{
#ifdef VERSION_JUN26
    draw(1, 0);
    draw(2, 0);
    draw(3, 0);
    draw(4, 0);
    draw(5, 0);
    draw(6, 0);
    draw(7, 0);
    draw(8, 0);
    draw(9, 0);
    draw(10, 0);
    draw(11, 0);
    draw(12, 0);
    draw(13, 0);
    draw(14, 0);
    draw(15, 0);
    draw(16, 0);
    draw(17, 0);
    draw(18, 0);
    draw(19, 0);
    draw(20, 0);
    draw(21, 0);
    draw(22, 0);
    draw(23, 0);
    draw(24, 0);
    draw(25, 0);
    draw(26, 0);
    draw(27, 0);
    draw(28, 0);
    draw(29, 0);
    draw(30, 0);
    draw(31, 0);
    draw(32, 0);
    draw(33, 0);
    draw(34, 0);
    draw(35, 0);
    draw(36, 0);
    draw(37, 0);
    draw(38, 0);
    draw(39, 0);
    draw(40, 0);
    draw(41, 0);
    draw(42, 0);
#else
    set_mode(0);
#endif
}

/* a block that closes a statement, opens a switch and holds its first label
   (layout_action.c): the label is no place for a selector, and case-shift
   owns no such switch, so the block is hand work (unbalanced) */
int fx_label(int item)
{
    if (item != -1) {
#ifdef VERSION_NTSC
        draw(233, 234);
    }
    switch (item) {
    case 233:
#else
        draw(264, 265);
    }
    switch (item) {
    case 264:
#endif
        set_mode(1);
        break;
    case 7:
        set_mode(2);
    }
    return 0;
}

int fx_use(void)
{
    TexRec t;
    Layout l = {MAX_CHARS};

    demoEnd();
    DummyIcoKor();
    return mcCheck() + (int)sizeof(t) + l.curItem + stageOrientIdx[0] + systemStatus[0] +
           iosCdvdMediaType + NonLinearCameraMove + saveSeg[1];
}
