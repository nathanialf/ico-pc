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
int NonLinearCameraMove = 0;
void demoEnd(void) {}
void DummyIcoMisc(void);

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
    strcpy_(file, PAL_JPN("SCES_507.60", "SCPS_110.03"));
    stage_SetAnimation(PAL_JPN(625, 621));
    alloc(64, LN(560, 557, 557, 560, 561, 562));
    return framePlayed / PAL_JPN(2997.0f, 50.0f);
}

int fx_layout(MotionRec *row)
{
    int k = 0;

    /* deny-listed: a bit shift, a raw offset after a pointer cast, the
       ActWork shift of 16, a member name */
    if ((row->modeBits.word >> 19) & 7) {
        k = 1;
    }
    *(int *)((char *)work + 0x840) = 20;
    alloc(0x980, 907);
    row->a900[0] = 0;
    return k;
}

int fx_locals(void)
{
    /* an initialised against an uninitialised local (A1') */
    int n = 0;
    /* an automatic initialiser takes the selector; a static one cannot */
    int rect[4] = {80, PAL_JPN(11, 6), -160, PAL_JPN(5, 6)};
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
    switch (lt_current_property_item() + PAL_JPN(0, 29)) { /* JPN property ids 29 lower */
    case 0x31:
        set_mode(2);
        break;
        return PAL_JPN(0x14, 0x11);
    case 0x32:
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
    movie_init(720, PAL_JPN(video ? 576 : 480, 480), 36);
    /* whole statements (A3) */
    if (ICO_JPN) {
        set_mode(4);
        draw(1, 2);
    } else {
        if (video) {
            set_mode(5);
        }
        draw(3, 4);
    }
    /* one-region statements (A4) */
    if (!ICO_JPN) {
        gsb_ResetSnap();
    }

    if (ICO_JPN) {
        draw(0, 0);
    }

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
    draw(PAL_JPN_NTSC(29, 30, 31), PAL_NTSC(9, 7));
    stage_SetAnimation(PAL_JPN(700, 696));
    alloc(PAL_NTSC(4, 8), 1);
    if (ICO_NTSC) {
        set_mode(9);
    } else if (ICO_JPN) {
        if (stage_no) {
            set_mode(8);
        }
    } else {
        draw(stage_no, 0);
    }
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
    draw(5, PAL_JPN(7, 6));
}

/* the Korean build is the Japanese program: arms shared by two rows
   (an `#if` naming both macros joined by ||, the set JPNKOR), a KOR
   block nested in a shared arm (its #else is the Japanese text), a KOR
   block beside PAL text (its #else serves every other row), the negated
   pair, an #elif with ||, and a JPN-only block the other rows skip */
void DummyIcoKor(void);
#if defined(VERSION_JPN) || defined(VERSION_KOR)
int saveSeg[3] = {0, 0x1E8, 4};
#else
int saveSeg[3] = {0, 0x1F8, 4};
#endif
extern int viewScale;

void fx_shared(MotionRec *row, int video, int lsn)
{
    /* shared token and span */
    stage_SetAnimation(PAL_JPNKOR(625, 621));
    movie_init(720, PAL_JPNKOR(video ? 576 : 480, 480), 36);
    /* a bit position in a shared arm: one --bits row per row of the set */
    draw((row->modeBits.word >> 19) & 7, 0);
    /* shared statements, shared one-region, the negated pair */
    if (ICO_JPNKOR) {
        set_mode(4);
    } else {
        if (video) {
            set_mode(5);
        }
    }
    if (ICO_JPNKOR) {
        draw(0, 0);
    }
    if (!ICO_JPNKOR) {
        gsb_ResetSnap();
    }
    /* KOR nested in a shared arm: the three texts, flat already */
    strcpy_((char *)work, PAL_JPN_KOR("SCES_507.60", "SCPS_110.03", "SCPS_560.01"));
    /* KOR nested beside shared lines: rebuilt flat, the shared lines in
       both arms; a value two rows share takes one argument */
    draw(PAL_JPNKOR(3, 1), PAL_JPNKOR(4, 2));
    set_mode(PAL_JPN_KOR(7, 6, 5));
    /* the same as statements: an else-if chain over the three texts */
    if (ICO_JPN) {
        draw(1, 2);
    } else if (ICO_KOR) {
        draw(1, 2);
        gsb_ResetSnap();
    } else {
        set_mode(7);
    }
    /* KOR beside PAL: the #else serves every other row */
    set_mode(PAL_KOR(11, 5));
    if (ICO_KOR) {
        lsn += 153600;
    }
    /* an #elif with || */
    draw(PAL_JPNKOR_NTSC(29, 26, 25), PAL_NTSC(9, 7));
    /* JPN only: the other rows take PAL's (empty) arm */
    if (ICO_JPN) {
        viewScale = 150;
    }
    draw(lsn, 0);
}

int fx_shared_switch(int stage)
{
    /* per-row deltas: KOR's ids 6 lower than JPN's (case-shift per row) */
    switch (lt_current_property_item() + PAL_JPN_KOR(0, 30, 36)) { /* JPN property ids 30 lower, KOR property ids 36 lower */
    case 44:
        set_mode(1);
        break;
    case 45:
        set_mode(0);
        break;
    }
    /* one delta for the shared arm: the set's selector */
    switch (stage + PAL_JPNKOR(0, 29)) { /* JPNKOR stage ids 29 lower */
    case 0x31:
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
    lt_switch_layout(PAL_JPNKOR_NTSC(54, 51, 50));
    /* AUG6 nested in the arm of the Japanese lines it shares */
    draw(PAL_JPNKOR_AUG6(36, 40, 38), 1);
    /* AUG6 nested twice in a USA arm beside shared lines (st22a.c): rebuilt
       flat, the shared line in each arm, the story flags and the animation
       per row (the animation deltas are review lines) */
    gflagOn(PAL_NTSC_AUG6(323, 294, 293));
    lt_switch_layout(51);
    gflagOn(PAL_NTSC_AUG6(324, 295, 294));
    stage_SetAnimation(PAL_NTSC_AUG6(758, 684, 683));
    /* two deep: the demo inside the prototype inside the USA arm (a mail id
       per row) */
    draw(PAL_NTSC_AUG6_JUN26(430, 397, 393, 377), 0);
    /* three deep, whole statements: an else-if chain over five texts, the
       demo's and the prototype's sharing a line */
    if (ICO_JPNKOR) {
        set_mode(1);
    } else if (ICO_JUN26) {
        enable_game_pause = 0;
        gsb_ResetSnap();
    } else if (ICO_AUG6) {
        gsb_ResetSnap();
    } else if (ICO_NTSC) {
        set_mode(2);
    } else {
        if (video) {
            set_mode(3);
        }
    }
    /* PAL-only lines: the three-name condition with the prototype's
       exclusion nested (a four-name line would pass the column limit), then
       with the demo's too */
    if (!ICO_JPNKORNTSCAUG6) {
        draw(7, 7);
    }
    if (!ICO_JPNKORNTSCAUG6JUN26) {
        draw(8, 8);
    }
    /* the demo's own block beside the four-name condition (e3.c): the two
       texts are one, so one set of five rows */
    if (ICO_JPNKORNTSCAUG6JUN26) {
        set_mode(12);
    } else {
        gsb_ResetSnap();
    }
    /* the same over a nested #elif */
    draw(13, PAL_NTSC_AUG6_JUN26(0, 1, 2, 3));
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
