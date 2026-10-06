/*
 * port/ui/game_font_build.c
 *
 * The game face's builder (game_font.h): the
 * letters of the menu sheets' word rectangles, segmented, matched to the
 * transcribed words, measured and packed.  No game or disc code: the
 * sources come from the caller (game_font_disc.c, or a test's synthetic
 * sheet).
 *
 * Per rectangle:
 *   1. the ink.  A sheet is 4-bit with a 16-colour palette mixing the light
 *      letters, their dark (black or grey) rim and antialiasing.  A texel of
 *      luminance L and alpha A is read as ink over rim: the premultiplied
 *      light P = L A = c Lmax + R A (1 - c) gives the ink coverage
 *      c = (P - R A) / (Lmax - R), with Lmax the letters' luminance (the
 *      95th percentile where A > 0.9: 1 for white, 151 / 255 for the grey
 *      slot numbers) and R the rim's (the median luminance of the faint
 *      texels, 0 < A < 0.35, when the rectangle has a dark core: 0 for the
 *      black rims, about 62 / 255 for the French, Italian and Spanish
 *      sheets' grey rims).  Black letters (the white panel) are their alpha.
 *   2. the letters' shapes: texels of ink above 0.35 (0.5 over a grey rim,
 *      whose own light would join the letters) in 8-connected components.
 *      The rim cannot be segmented on: it is a soft glow over the whole word.
 *   3. the lines: a component belongs to the line whose capital middle
 *      (the table's, plus the pitch per line) is nearest its centre.
 *   4. the marks: a component above or below another within its columns
 *      (the dot of i and j, the accents, the two dots of : and the umlaut),
 *      or a small one inside its columns, joins it.
 *   5. the letters: the components, left to right, are matched to the line's
 *      characters (spaces left out) by dynamic programming, each component
 *      taking one to three characters, the cost the squared difference of
 *      its width from the characters' expected widths (in capital heights)
 *      plus a penalty per extra character.  The expected widths are the
 *      first pass's median per character (0.6 of the capitals for one not
 *      seen yet), so touching pairs ("AV", "To", "ff", "rt") are found where
 *      a component is about as wide as two letters.  A component holding
 *      several is cut at the column of least ink within two texels of the
 *      boundary its letters' widths put.
 *   6. per letter: its ink (the texels it owns: its component's and, within
 *      two texels, the antialiasing next to them), its box (the shape's
 *      texels), the baseline (the median bottom of the line's letters that
 *      sit on it), and the gap to the next letter of the word, or over a
 *      space to the next word.
 * Then, over all rectangles and languages:
 *   7. one bitmap per character: from the main size class (the 13.5-texel
 *      em of the menu rows) when the character appears there, else from the
 *      class it appears in most; within it the medoid (the instance nearest
 *      all the others, laid on its box and baseline), so a badly cut or
 *      mismatched instance is never the one kept;
 *   8. the spacing: each character's left and right side bearings fitted to
 *      every gap measured within words (gap(a, b) = right(a) + left(b), least
 *      squares, normalised to the main class), and a kerning pair where a
 *      pair's mean residual over at least two sightings sets it half a
 *      texel or more closer (whole texels); the space is the median of the word gaps less the bearings
 *      either side;
 *   9. the rim: the sheets' rim is a dark glow round the whole word, a
 *      dark edge against the letters and a lighter plateau beyond it.  Its
 *      texels within two of the letters' ink are kept as cut (step 10); the
 *      plateau beyond, which differs from row to row and sheet to sheet, is
 *      a fitted glow per glyph: 0.5 (1 - exp(-G(D(ink)))), D a dilation by a
 *      disc of 2 texels, G a Gaussian of 4 texels, at least the ink.  Fitted
 *      on the 17 black-rimmed rectangles' texels further than 2.5 from any
 *      ink, each word's glow the alpha blend of its letters' (as they are
 *      drawn: the transmittances multiply): squared error 0.0066 per texel;
 *  10. the atlas: ink and rim cells, shelf-packed 512 texels wide (ATLAS_W),
 *      two texels apart, each with a zero border (a cell wider than the
 *      atlas fails the build); its height is never a power of two (no mip
 *      chain, font.c's PAGE_TRIM).
 */
#include "game_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h" /* ui_Utf8Next */

#define MAX_LINES 8
#define MAX_CHARS 128 /* per line */
#define MEDOID_MAX 96
#define RIM_DILATE 2
#define RIM_SIGMA 4.0f
#define RIM_GAIN 1.0f
#define RIM_CAP 0.5f
#define ATLAS_W 512
#define CELL_GAP 2

typedef struct Src {
    UiGfSource s;
    float *L, *A; /* w x h, 0..1 */
    char *text;
} Src;

typedef struct Inst {
    uint32_t cp;
    float em, cap;
    int sheet, lang, split;
    int bw, bh;   /* the ink bitmap */
    float *bmp;   /* bw x bh */
    float bx, by; /* the bitmap's top-left from the box's left edge and the baseline */
    int boxW;     /* the shape's width */
    int boxH;     /* the shape's height */
    /* the sheet's own texels round the letter (its cell of the line: the
       columns nearer its box than its neighbours', the line's rows): the
       alpha and the premultiplied light, as the sprite has them */
    int hasRim;         /* the rectangle's letters carry the dark rim */
    int vw, vh;         /* the cell */
    float *va, *vp;     /* vw x vh */
    float *vn;          /* how much of each is the sheet's (sheetNear: near any letter's ink) */
    float *nmap;        /* sheetNear over a window round the letter (the glow's reach) */
    int nx, ny, nw, nh; /* the window: left column from the box's left, top row from the baseline */
    int vx;             /* its left column from the box's left edge */
    int vy;             /* its top row from the baseline */
    int openL, openR;   /* no letter of the same word on that side on the sheet */
    int tier;           /* how well its texels serve (lower is better): a rim at all,
                         a black one, a one-line rectangle (the cell's rows are its
                         own, not cut at half a pitch) */
} Inst;

typedef struct Pair {
    uint32_t a, b;
    float gap; /* main texels */
} Pair;

typedef struct Expect {
    uint32_t cp;
    float w; /* width / capitals */
} Expect;

struct UiGfBuilder {
    Src *src;
    int nsrc, capsrc;
    char (*sheets)[UI_GF_SHEET_NAME];
    int nsheets, capsheets;
    Inst *inst;
    int ninst, capinst;
    Pair *pairs;
    int npairs, cappairs;
    Pair *spaces; /* the gaps over a space, main texels */
    int nspaces, capspaces;
    Expect *expect;
    int nexpect;
    float mainEm;
    UiGfStats st;
};

/* ------------------------------------------------------------ helpers */

static int grow(void **p, int *cap, int need, size_t elem)
{
    if (need <= *cap) {
        return 0;
    }
    int n = *cap ? *cap * 2 : 64;
    while (n < need) {
        n *= 2;
    }
    void *q = realloc(*p, (size_t)n * elem);
    if (!q) {
        return -1;
    }
    *p = q;
    *cap = n;
    return 0;
}

static int cmpFloat(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* the median of n values (sorted in place) */
static float median(float *v, int n)
{
    if (n <= 0) {
        return 0.0f;
    }
    qsort(v, (size_t)n, sizeof(float), cmpFloat);
    return n & 1 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

static float expected(const UiGfBuilder *b, uint32_t cp)
{
    for (int i = 0; i < b->nexpect; i++) {
        if (b->expect[i].cp == cp) {
            return b->expect[i].w;
        }
    }
    return 0.6f;
}

/* letters that sit on the baseline (no descender, not a bracket) */
static int sitsOnBaseline(uint32_t cp)
{
    if (cp < 0x80) {
        return (cp >= 'A' && cp <= 'Z' && cp != 'Q' && cp != 'J') ||
               (cp >= 'a' && cp <= 'z' && !strchr("gjpqy", (int)cp)) || (cp >= '0' && cp <= '9');
    }
    return cp >= 0xC0 && cp <= 0x17F && cp != 0xC7 && cp != 0xE7; /* accented, not Ç ç */
}

/* --------------------------------------------------------------- API */

UiGfBuilder *ui_GfBuilderNew(void)
{
    return calloc(1, sizeof(UiGfBuilder));
}

static void clearInstances(UiGfBuilder *b)
{
    for (int i = 0; i < b->ninst; i++) {
        free(b->inst[i].bmp);
        free(b->inst[i].va);
        free(b->inst[i].vp);
        free(b->inst[i].vn);
        free(b->inst[i].nmap);
    }
    b->ninst = 0;
    b->npairs = 0;
    b->nspaces = 0;
}

void ui_GfBuilderFree(UiGfBuilder *b)
{
    if (!b) {
        return;
    }
    clearInstances(b);
    for (int i = 0; i < b->nsrc; i++) {
        free(b->src[i].L);
        free(b->src[i].A);
        free(b->src[i].text);
    }
    free(b->src);
    free(b->sheets);
    free(b->inst);
    free(b->pairs);
    free(b->spaces);
    free(b->expect);
    free(b);
}

int ui_GfBuilderSheet(UiGfBuilder *b, const char *name)
{
    for (int i = 0; i < b->nsheets; i++) {
        if (strncmp(b->sheets[i], name, UI_GF_SHEET_NAME - 1) == 0) {
            return i;
        }
    }
    if (grow((void **)&b->sheets, &b->capsheets, b->nsheets + 1, sizeof(b->sheets[0])) != 0) {
        return -1;
    }
    memset(b->sheets[b->nsheets], 0, UI_GF_SHEET_NAME);
    strncpy(b->sheets[b->nsheets], name, UI_GF_SHEET_NAME - 1);
    return b->nsheets++;
}

int ui_GfBuilderAdd(UiGfBuilder *b, const UiGfSource *s)
{
    if (!s->rgba || !s->text || s->w <= 0 || s->h <= 0 || s->u < 0 || s->v < 0 ||
        s->u + s->w > s->sheetW || s->v + s->h > s->sheetH || s->em <= 0.0f || s->pitch <= 0.0f) {
        return -1;
    }
    if (grow((void **)&b->src, &b->capsrc, b->nsrc + 1, sizeof(Src)) != 0) {
        return -1;
    }
    Src *d = &b->src[b->nsrc];
    memset(d, 0, sizeof(*d));
    d->s = *s;
    const size_t n = (size_t)s->w * (size_t)s->h;
    d->L = malloc(n * sizeof(float));
    d->A = malloc(n * sizeof(float));
    d->text = malloc(strlen(s->text) + 1);
    if (!d->L || !d->A || !d->text) {
        free(d->L);
        free(d->A);
        free(d->text);
        return -1;
    }
    strcpy(d->text, s->text);
    d->s.text = d->text;
    d->s.rgba = NULL;
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            const uint8_t *p =
                s->rgba + ((size_t)(s->v + y) * (size_t)s->sheetW + (size_t)(s->u + x)) * 4;
            d->L[(size_t)y * (size_t)s->w + (size_t)x] = (float)p[0] / 255.0f;
            d->A[(size_t)y * (size_t)s->w + (size_t)x] = (float)p[3] / 255.0f;
        }
    }
    b->nsrc++;
    b->st.sources++;
    return 0;
}

/* ------------------------------------------------------ segmentation */

typedef struct Comp {
    int x0, x1, y0, y1, n;
    int group; /* the component it was merged into (itself when a root) */
    int line;
} Comp;

static void inkOf(const Src *sr, float *ink, int *rimmed, int *hasRim)
{
    const int n = sr->s.w * sr->s.h;
    *rimmed = 0;
    *hasRim = 0;
    if (sr->s.dark) {
        memcpy(ink, sr->A, (size_t)n * sizeof(float));
        return;
    }
    float *tmp = malloc((size_t)n * sizeof(float));
    int k = 0, core = 0;
    float lmax = 1.0f, r = 0.0f;
    if (tmp) {
        for (int i = 0; i < n; i++) {
            if (sr->A[i] > 0.9f) {
                tmp[k++] = sr->L[i];
            }
            if (sr->A[i] > 0.5f && sr->L[i] < 0.45f) {
                core++;
            }
        }
        if (k > 3) {
            qsort(tmp, (size_t)k, sizeof(float), cmpFloat);
            lmax = tmp[(k * 95) / 100 < k ? (k * 95) / 100 : k - 1];
        }
        if (core > 8) {
            k = 0;
            for (int i = 0; i < n; i++) {
                if (sr->A[i] > 0.03f && sr->A[i] < 0.35f) {
                    tmp[k++] = sr->L[i];
                }
            }
            if (k > 8) {
                r = median(tmp, k);
                if (r > 0.45f) {
                    r = 0.0f;
                }
            }
        }
        free(tmp);
    }
    float den = lmax - r;
    if (den < 0.2f) {
        den = 0.2f;
    }
    for (int i = 0; i < n; i++) {
        float c = (sr->L[i] * sr->A[i] - r * sr->A[i]) / den;
        ink[i] = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
    }
    *rimmed = r > 0.0f;
    *hasRim = core > 8;
}

/* 8-connected components of ink > thr; lab gets 1-based ids */
static int label(const float *ink, int w, int h, float thr, int *lab, Comp **out)
{
    int *stack = malloc((size_t)w * (size_t)h * sizeof(int));
    Comp *cs = NULL;
    int n = 0, cap = 0;
    if (!stack) {
        return -1;
    }
    memset(lab, 0, (size_t)w * (size_t)h * sizeof(int));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i0 = y * w + x;
            if (ink[i0] <= thr || lab[i0]) {
                continue;
            }
            if (grow((void **)&cs, &cap, n + 1, sizeof(Comp)) != 0) {
                free(stack);
                free(cs);
                return -1;
            }
            Comp *c = &cs[n++];
            c->x0 = c->x1 = x;
            c->y0 = c->y1 = y;
            c->n = 0;
            c->group = n - 1;
            c->line = 0;
            int sp = 0;
            stack[sp++] = i0;
            lab[i0] = n;
            while (sp) {
                const int i = stack[--sp];
                const int cx = i % w, cy = i / w;
                c->n++;
                c->x0 = cx < c->x0 ? cx : c->x0;
                c->x1 = cx > c->x1 ? cx : c->x1;
                c->y0 = cy < c->y0 ? cy : c->y0;
                c->y1 = cy > c->y1 ? cy : c->y1;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int xx = cx + dx, yy = cy + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) {
                            continue;
                        }
                        const int j = yy * w + xx;
                        if (ink[j] > thr && !lab[j]) {
                            lab[j] = n;
                            stack[sp++] = j;
                        }
                    }
                }
            }
            c->x1++;
            c->y1++;
        }
    }
    free(stack);
    *out = cs;
    return n;
}

static int rootOf(Comp *cs, int i)
{
    while (cs[i].group != i) {
        i = cs[i].group;
    }
    return i;
}

/* step 4: marks join their letter; returns the roots of line `line`, left
   to right, in roots */
static int groupLine(Comp *cs, int n, int line, int *roots, int maxRoots)
{
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < n && !changed; i++) {
            if (cs[i].line != line || cs[i].group != i) {
                continue;
            }
            for (int j = i + 1; j < n; j++) {
                if (cs[j].line != line || cs[j].group != j) {
                    continue;
                }
                Comp *a = &cs[i], *c = &cs[j];
                const int ov = (a->x1 < c->x1 ? a->x1 : c->x1) - (a->x0 > c->x0 ? a->x0 : c->x0);
                const int wa = a->x1 - a->x0, wc = c->x1 - c->x0;
                const int small = wa < wc ? wa : wc;
                const int vsep = a->y1 <= c->y0 + 1 || c->y1 <= a->y0 + 1;
                const Comp *sm = a->n < c->n ? a : c, *bg = a->n < c->n ? c : a;
                const int mark = sm->n * 4 <= bg->n && (sm->y1 - sm->y0) * 2 <= (bg->y1 - bg->y0);
                if (2 * ov >= small && (vsep || mark)) {
                    a->x0 = a->x0 < c->x0 ? a->x0 : c->x0;
                    a->x1 = a->x1 > c->x1 ? a->x1 : c->x1;
                    a->y0 = a->y0 < c->y0 ? a->y0 : c->y0;
                    a->y1 = a->y1 > c->y1 ? a->y1 : c->y1;
                    a->n += c->n;
                    c->group = i;
                    changed = 1;
                    break;
                }
            }
        }
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (cs[i].line == line && cs[i].group == i && k < maxRoots) {
            /* specks: a single texel */
            if (cs[i].n < 2 && cs[i].x1 - cs[i].x0 <= 1) {
                continue;
            }
            roots[k++] = i;
        }
    }
    /* left to right */
    for (int i = 1; i < k; i++) {
        for (int j = i; j > 0 && cs[roots[j]].x0 < cs[roots[j - 1]].x0; j--) {
            const int t = roots[j];
            roots[j] = roots[j - 1];
            roots[j - 1] = t;
        }
    }
    return k;
}

static int addInst(UiGfBuilder *b, const Inst *in)
{
    if (grow((void **)&b->inst, &b->capinst, b->ninst + 1, sizeof(Inst)) != 0) {
        return -1;
    }
    b->inst[b->ninst++] = *in;
    return 0;
}

static void addPair(UiGfBuilder *b, uint32_t a, uint32_t c, float gap)
{
    if (grow((void **)&b->pairs, &b->cappairs, b->npairs + 1, sizeof(Pair)) == 0) {
        b->pairs[b->npairs].a = a;
        b->pairs[b->npairs].b = c;
        b->pairs[b->npairs].gap = gap;
        b->npairs++;
    }
}

static void addSpace(UiGfBuilder *b, uint32_t a, uint32_t c, float gap)
{
    if (grow((void **)&b->spaces, &b->capspaces, b->nspaces + 1, sizeof(Pair)) == 0) {
        b->spaces[b->nspaces].a = a;
        b->spaces[b->nspaces].b = c;
        b->spaces[b->nspaces].gap = gap;
        b->nspaces++;
    }
}

/* one glyph of a line while it is measured */
typedef struct Placed {
    uint32_t cp;
    int x0, x1; /* the shape's columns */
    int split;
} Placed;

/* how much of the texel (x, y) of a rectangle a cell keeps from the sheet:
   1 within NEAR_SHEET texels of any letter's ink (the letters, their
   antialiasing and the rim's dark edge next to them, a neighbour's too: in a
   word set by the face a letter has a neighbour there as well), 0 from
   NEAR_FADE texels further (the glow's plateau, which differs from row to
   row and sheet to sheet), linear between */
#define NEAR_SHEET 2.0f
#define NEAR_FADE 2.0f /* texels over which the sheet fades into the fitted glow */
#define NEAR_WIN 16    /* texels round a letter its near map covers */

static float sheetNear(const float *ink, int w, int h, int x, int y)
{
    float best = 1e9f;
    for (int v = -4; v <= 4; v++) {
        for (int u = -4; u <= 4; u++) {
            const int xx = x + u, yy = y + v;
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && ink[yy * w + xx] > 0.1f) {
                const float d = (float)(u * u + v * v);
                best = d < best ? d : best;
            }
        }
    }
    const float d = sqrtf(best);
    return d <= NEAR_SHEET               ? 1.0f
           : d >= NEAR_SHEET + NEAR_FADE ? 0.0f
                                         : (NEAR_SHEET + NEAR_FADE - d) / NEAR_FADE;
}

static void segment(UiGfBuilder *b, const Src *sr)
{
    const int w = sr->s.w, h = sr->s.h, n = w * h;
    float *ink = malloc((size_t)n * sizeof(float));
    int *lab = malloc((size_t)n * sizeof(int));
    int *owner = malloc((size_t)n * sizeof(int));
    int *owner2 = malloc((size_t)n * sizeof(int));
    Comp *cs = NULL;
    if (!ink || !lab || !owner || !owner2) {
        goto done;
    }
    int rimmed = 0, hasRim = 0;
    inkOf(sr, ink, &rimmed, &hasRim);
    const float thr = rimmed ? 0.5f : 0.35f;
    const int nc = label(ink, w, h, thr, lab, &cs);
    if (nc <= 0) {
        goto done;
    }

    /* the lines' characters */
    uint32_t chars[MAX_LINES][MAX_CHARS];
    int nchars[MAX_LINES], spaceAfter[MAX_LINES][MAX_CHARS];
    int nl = 0;
    {
        const char *s = sr->s.text;
        memset(nchars, 0, sizeof(nchars));
        memset(spaceAfter, 0, sizeof(spaceAfter));
        uint32_t cp;
        while ((cp = ui_Utf8Next(&s)) != 0) {
            if (cp == '\n') {
                if (++nl >= MAX_LINES) {
                    goto done;
                }
                continue;
            }
            if (cp == ' ') {
                if (nchars[nl] > 0) {
                    spaceAfter[nl][nchars[nl] - 1] = 1;
                }
                continue;
            }
            if (nchars[nl] < MAX_CHARS) {
                chars[nl][nchars[nl]++] = cp;
            }
        }
        nl++;
    }
    const float cap = sr->s.em * 0.688f + 0.7f;
    for (int i = 0; i < nc; i++) {
        const float cy = 0.5f * (float)(cs[i].y0 + cs[i].y1);
        int line = nl > 1 ? (int)lrintf((cy - sr->s.capMid) / sr->s.pitch) : 0;
        cs[i].line = line < 0 ? 0 : line >= nl ? nl - 1 : line;
    }
    for (int i = 0; i < n; i++) {
        owner[i] = -1;
    }

    Placed placed[MAX_LINES][MAX_CHARS];
    int nplaced[MAX_LINES];
    int glyphBase[MAX_LINES];
    int nglyph = 0;
    memset(nplaced, 0, sizeof(nplaced));
    for (int line = 0; line < nl; line++) {
        int roots[MAX_CHARS * 2];
        const int M = groupLine(cs, nc, line, roots, MAX_CHARS * 2);
        const int N = nchars[line];
        glyphBase[line] = nglyph;
        if (M == 0 || N == 0) {
            continue;
        }
        b->st.lines++;
        /* step 5: the alignment */
        static float best[MAX_CHARS * 2 + 1][MAX_CHARS + 1];
        static signed char back[MAX_CHARS * 2 + 1][MAX_CHARS + 1];
        for (int m = 0; m <= M; m++) {
            for (int k = 0; k <= N; k++) {
                best[m][k] = 1e30f;
                back[m][k] = 0;
            }
        }
        best[0][0] = 0.0f;
        for (int m = 1; m <= M; m++) {
            const float cw = (float)(cs[roots[m - 1]].x1 - cs[roots[m - 1]].x0);
            for (int k = 1; k <= N; k++) {
                for (int kk = 1; kk <= 3 && kk <= k; kk++) {
                    if (best[m - 1][k - kk] >= 1e29f) {
                        continue;
                    }
                    float e = 0.0f;
                    for (int q = k - kk; q < k; q++) {
                        e += expected(b, chars[line][q]) * cap;
                    }
                    const float d = (cw - e) / cap;
                    const float cost =
                        best[m - 1][k - kk] + d * d + (kk > 1 ? 0.3f * (float)kk : 0.0f);
                    if (cost < best[m][k]) {
                        best[m][k] = cost;
                        back[m][k] = (signed char)kk;
                    }
                }
            }
        }
        if (best[M][N] >= 1e29f) {
            b->st.unaligned++;
            continue;
        }
        int take[MAX_CHARS * 2];
        for (int m = M, k = N; m > 0; m--) {
            take[m - 1] = back[m][k];
            k -= back[m][k];
        }
        if (M == N) {
            b->st.exact++;
        } else {
            b->st.aligned++;
        }
        /* the owners: each glyph's shape texels, split at the cut columns */
        int ci = 0;
        for (int m = 0; m < M; m++) {
            const Comp *g = &cs[roots[m]];
            const int kk = take[m];
            int edges[5];
            edges[0] = g->x0;
            edges[kk] = g->x1;
            if (kk > 1) {
                b->st.splits++;
                float es[3], tot = 0.0f;
                for (int q = 0; q < kk; q++) {
                    es[q] = expected(b, chars[line][ci + q]);
                    tot += es[q];
                }
                float acc = 0.0f;
                for (int q = 0; q < kk - 1; q++) {
                    acc += es[q];
                    const float t = (float)g->x0 + (float)(g->x1 - g->x0) * acc / tot;
                    int lo = (int)lrintf(t) - 2, hi = (int)lrintf(t) + 2;
                    lo = lo < edges[q] + 1 ? edges[q] + 1 : lo;
                    hi = hi > g->x1 - 1 ? g->x1 - 1 : hi;
                    int cut = (int)lrintf(t);
                    float bestSum = 1e30f;
                    for (int x = lo; x <= hi; x++) {
                        float sum = 0.0f;
                        for (int y = g->y0; y < g->y1; y++) {
                            const int i = y * w + x;
                            if (lab[i] && rootOf(cs, lab[i] - 1) == roots[m]) {
                                sum += ink[i];
                            }
                        }
                        const float score = sum + 0.01f * fabsf((float)x - t);
                        if (score < bestSum) {
                            bestSum = score;
                            cut = x;
                        }
                    }
                    edges[q + 1] = cut;
                }
            }
            for (int q = 0; q < kk; q++) {
                Placed *p = &placed[line][nplaced[line]++];
                p->cp = chars[line][ci + q];
                p->split = kk > 1;
                p->x0 = w;
                p->x1 = 0;
                const int id = nglyph++;
                for (int y = g->y0; y < g->y1; y++) {
                    for (int x = edges[q]; x < edges[q + 1]; x++) {
                        const int i = y * w + x;
                        if (lab[i] && rootOf(cs, lab[i] - 1) == roots[m]) {
                            owner[i] = id;
                            p->x0 = x < p->x0 ? x : p->x0;
                            p->x1 = x + 1 > p->x1 ? x + 1 : p->x1;
                        }
                    }
                }
            }
            ci += kk;
        }
    }
    /* the antialiasing next to a shape (two texels) is its owner's */
    for (int round = 0; round < 2; round++) {
        memcpy(owner2, owner, (size_t)n * sizeof(int));
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int i = y * w + x;
                if (owner[i] >= 0 || ink[i] <= 0.02f) {
                    continue;
                }
                for (int dy = -1; dy <= 1 && owner2[i] < 0; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx >= 0 && yy >= 0 && xx < w && yy < h && owner[yy * w + xx] >= 0) {
                            owner2[i] = owner[yy * w + xx];
                            break;
                        }
                    }
                }
            }
        }
        memcpy(owner, owner2, (size_t)n * sizeof(int));
    }
    /* step 6: per glyph */
    const float norm = b->mainEm / sr->s.em;
    for (int line = 0; line < nl; line++) {
        const int np = nplaced[line];
        if (np == 0) {
            continue;
        }
        int bx0[MAX_CHARS], bx1[MAX_CHARS], by0[MAX_CHARS], by1[MAX_CHARS];
        int sy1[MAX_CHARS]; /* the shape's bottom */
        float bottoms[MAX_CHARS];
        int nb = 0;
        for (int g = 0; g < np; g++) {
            const int id = glyphBase[line] + g;
            bx0[g] = w;
            by0[g] = h;
            bx1[g] = by1[g] = sy1[g] = 0;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const int i = y * w + x;
                    if (owner[i] != id) {
                        continue;
                    }
                    bx0[g] = x < bx0[g] ? x : bx0[g];
                    bx1[g] = x + 1 > bx1[g] ? x + 1 : bx1[g];
                    by0[g] = y < by0[g] ? y : by0[g];
                    by1[g] = y + 1 > by1[g] ? y + 1 : by1[g];
                    if (ink[i] > thr) {
                        sy1[g] = y + 1 > sy1[g] ? y + 1 : sy1[g];
                    }
                }
            }
            if (sitsOnBaseline(placed[line][g].cp) && sy1[g] > 0) {
                bottoms[nb++] = (float)sy1[g];
            }
        }
        /* on a whole row, so the sheet's texels keep their rows */
        const float baseline = floorf(
            (nb > 0 ? median(bottoms, nb) : sr->s.capMid + (float)line * sr->s.pitch + 0.5f * cap) +
            0.5f);
        /* the line's rows: half a pitch either side of its capitals' middle
           (the whole rectangle for one line) */
        int band0 = 0, band1 = h;
        if (nl > 1) {
            const float mid = sr->s.capMid + (float)line * sr->s.pitch;
            band0 = (int)floorf(mid - 0.5f * sr->s.pitch);
            band1 = (int)ceilf(mid + 0.5f * sr->s.pitch);
            band0 = band0 < 0 ? 0 : band0;
            band1 = band1 > h ? h : band1;
        }
        for (int g = 0; g < np; g++) {
            const Placed *p = &placed[line][g];
            const int id = glyphBase[line] + g;
            if (bx1[g] <= bx0[g] || p->x1 <= p->x0) {
                continue;
            }
            Inst in;
            memset(&in, 0, sizeof(in));
            in.cp = p->cp;
            in.em = sr->s.em;
            in.cap = cap;
            in.sheet = sr->s.sheet;
            in.lang = sr->s.lang;
            in.split = p->split;
            in.bw = bx1[g] - bx0[g];
            in.bh = by1[g] - by0[g];
            in.bmp = calloc((size_t)in.bw * (size_t)in.bh, sizeof(float));
            if (!in.bmp) {
                continue;
            }
            int sy0 = h;
            for (int y = by0[g]; y < by1[g]; y++) {
                for (int x = bx0[g]; x < bx1[g]; x++) {
                    const int i = y * w + x;
                    if (owner[i] == id) {
                        in.bmp[(y - by0[g]) * in.bw + (x - bx0[g])] = ink[i];
                        if (ink[i] > thr && y < sy0) {
                            sy0 = y;
                        }
                    }
                }
            }
            in.bx = (float)(bx0[g] - p->x0);
            in.by = (float)by0[g] - baseline;
            in.boxW = p->x1 - p->x0;
            in.boxH = sy1[g] > sy0 ? sy1[g] - sy0 : 0;
            /* the sheet's texels of its cell: the columns from halfway to
               the previous letter's box to halfway to the next one's */
            {
                const int vl = g > 0 ? (placed[line][g - 1].x1 + p->x0 + 1) / 2 : 0;
                const int vr = g + 1 < np ? (p->x1 + placed[line][g + 1].x0 + 1) / 2 : w;
                in.hasRim = hasRim && !sr->s.dark;
                in.tier = (in.hasRim ? 0 : 4) + (in.hasRim && !rimmed ? 0 : 2) + (nl == 1 ? 0 : 1);
                in.openL = g == 0 || spaceAfter[line][g - 1];
                in.openR = g + 1 == np || spaceAfter[line][g];
                in.vx = vl - p->x0;
                in.vy = band0 - (int)baseline;
                in.vw = vr > vl ? vr - vl : 0;
                in.vh = band1 - band0;
                if (in.vw > 0 && in.vh > 0) {
                    in.va = calloc((size_t)in.vw * (size_t)in.vh, sizeof(float));
                    in.vp = calloc((size_t)in.vw * (size_t)in.vh, sizeof(float));
                    in.vn = calloc((size_t)in.vw * (size_t)in.vh, sizeof(float));
                    if (!in.va || !in.vp || !in.vn) {
                        /* all three or none: the users test va alone */
                        free(in.va);
                        free(in.vp);
                        free(in.vn);
                        in.va = in.vp = in.vn = NULL;
                    }
                    for (int y = 0; in.va && y < in.vh; y++) {
                        for (int x = 0; x < in.vw; x++) {
                            const int i = (band0 + y) * w + (vl + x);
                            in.va[y * in.vw + x] = sr->A[i];
                            in.vp[y * in.vw + x] = sr->L[i] * sr->A[i];
                            /* another letter's ink is not this letter's
                               surroundings: the glow stands in there */
                            in.vn[y * in.vw + x] = owner[i] >= 0 && owner[i] != id
                                                       ? 0.0f
                                                       : sheetNear(ink, w, h, vl + x, band0 + y);
                        }
                    }
                    in.nx = -NEAR_WIN;
                    in.ny = -NEAR_WIN - (int)ceilf(cap);
                    in.nw = in.boxW + 2 * NEAR_WIN;
                    in.nh = (int)ceilf(cap) + 2 * NEAR_WIN;
                    in.nmap = calloc((size_t)in.nw * (size_t)in.nh, sizeof(float));
                    for (int y = 0; in.nmap && y < in.nh; y++) {
                        for (int x = 0; x < in.nw; x++) {
                            const int rx = p->x0 + in.nx + x, ry = (int)baseline + in.ny + y;
                            in.nmap[y * in.nw + x] =
                                (rx >= 0 && ry >= 0 && rx < w && ry < h &&
                                 !(owner[ry * w + rx] >= 0 && owner[ry * w + rx] != id))
                                    ? sheetNear(ink, w, h, rx, ry)
                                    : 0.0f;
                        }
                    }
                }
            }
            if (addInst(b, &in) != 0) {
                free(in.bmp);
                free(in.va);
                free(in.vp);
                free(in.vn);
                free(in.nmap);
                continue;
            }
            b->st.instances++;
            if (g + 1 < np) {
                const Placed *q = &placed[line][g + 1];
                const float gap = (float)(q->x0 - p->x1) * norm;
                /* the character index of p in its line is g (one placed per
                   character, in order) */
                if (spaceAfter[line][g]) {
                    addSpace(b, p->cp, q->cp, gap);
                } else {
                    addPair(b, p->cp, q->cp, gap);
                }
            }
        }
    }
done:
    free(ink);
    free(lab);
    free(owner);
    free(owner2);
    free(cs);
}

/* ---------------------------------------------------------- finishing */

/* a glyph's cells: its ink (the letter's light fill, for text drawn without
   the rim), and the sheet's alpha and light of its own columns (main) and of
   the columns beyond them on either side (the caps, drawn at a word's ends) */
enum {
    GC_INK,
    GC_MAIN_A,
    GC_MAIN_P,
    GC_LEFT_A,
    GC_LEFT_P,
    GC_RIGHT_A,
    GC_RIGHT_P,
    GC_GLOW,     /* the fitted glow inside a word */
    GC_GLOW_L,   /* at a word's start: its left side open */
    GC_GLOW_R,   /* at a word's end */
    GC_GLOW_LR,  /* a word of one letter */
    GC_MAIN_A_L, /* the main cell at a word's start, its end, alone (as the glow's) */
    GC_MAIN_P_L,
    GC_MAIN_A_R,
    GC_MAIN_P_R,
    GC_MAIN_A_LR,
    GC_MAIN_P_LR,
    GC_COUNT
};

#define CAP_W 10 /* texels: the rim's reach beyond a letter's columns */

typedef struct Glyph {
    uint32_t cp;
    const Inst *in;
    const Inst
        *capIn[2]; /* the instances the left and right caps are cut from (NULL: the fitted glow) */
    int count;
    float scale;
    float lsb, rsb;
    int hasL, hasR;
    /* the cells: 0 the ink, then the main cell's, the left cap's and the
       right cap's alpha and light (GC_*) */
    uint8_t *cell[GC_COUNT];
    int cw[GC_COUNT], ch[GC_COUNT];
    int cx[GC_COUNT], cy[GC_COUNT]; /* in the atlas */
    int left, right;                /* the main cell's columns from the box's left edge */
    int top[3];                     /* each A / P pair's first inner row from the baseline */
    int glowX, glowY;               /* the glow cell's top-left from the box's left, the baseline */
} Glyph;

static int cmpU32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* step 7: the medoid of the instances of one character in one class */
static const Inst *medoid(const Inst **v, int n)
{
    enum { CW = 48, CH = 48, OX = 8, OY = 32 };

    if (n == 1) {
        return v[0];
    }
    if (n > MEDOID_MAX) {
        n = MEDOID_MAX;
    }
    float *canv = calloc((size_t)n * CW * CH, sizeof(float));
    if (!canv) {
        return v[0];
    }
    for (int k = 0; k < n; k++) {
        const Inst *in = v[k];
        const int ox = OX + (int)lrintf(in->bx), oy = OY + (int)lrintf(in->by);
        for (int y = 0; y < in->bh; y++) {
            for (int x = 0; x < in->bw; x++) {
                const int cx = ox + x, cy = oy + y;
                if (cx >= 0 && cy >= 0 && cx < CW && cy < CH) {
                    canv[(size_t)k * CW * CH + (size_t)cy * CW + (size_t)cx] =
                        in->bmp[y * in->bw + x];
                }
            }
        }
    }
    int bestK = 0;
    double bestD = 1e300;
    for (int a = 0; a < n; a++) {
        double d = 0.0;
        for (int c = 0; c < n && d < bestD; c++) {
            const float *pa = canv + (size_t)a * CW * CH, *pc = canv + (size_t)c * CW * CH;
            for (int i = 0; i < CW * CH; i++) {
                const double e = (double)(pa[i] - pc[i]);
                d += e * e;
            }
        }
        if (d < bestD) {
            bestD = d;
            bestK = a;
        }
    }
    free(canv);
    return v[bestK];
}

/* step 9: the rim of an ink bitmap, its origin -pad texels from the ink's */
static uint8_t *makeRim(const float *ink, int iw, int ih, int *rw, int *rh, int *pad)
{
    const float sigma = RIM_SIGMA;
    const int r = (int)ceilf(3.0f * sigma);
    const int p = r + 1 + RIM_DILATE;
    const int W = iw + 2 * p, H = ih + 2 * p;
    float k[32];
    float ks = 0.0f;
    for (int i = -r; i <= r; i++) {
        k[i + r] = expf(-(float)(i * i) / (2.0f * sigma * sigma));
        ks += k[i + r];
    }
    for (int i = 0; i <= 2 * r; i++) {
        k[i] /= ks;
    }
    float *a = calloc((size_t)W * (size_t)H, sizeof(float));
    float *t = calloc((size_t)W * (size_t)H, sizeof(float));
    uint8_t *out = malloc((size_t)W * (size_t)H);
    if (!a || !t || !out) {
        free(a);
        free(t);
        free(out);
        return NULL;
    }
    for (int y = 0; y < ih; y++) {
        for (int x = 0; x < iw; x++) {
            a[(y + p) * W + (x + p)] = ink[y * iw + x];
        }
    }
    /* the ink dilated by a disc of RIM_DILATE texels */
    float *dl = calloc((size_t)W * (size_t)H, sizeof(float));
    if (!dl) {
        free(a);
        free(t);
        free(out);
        return NULL;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float m = 0.0f;
            for (int v = -RIM_DILATE; v <= RIM_DILATE; v++) {
                for (int u = -RIM_DILATE; u <= RIM_DILATE; u++) {
                    const int xx = x + u, yy = y + v;
                    if (u * u + v * v <= RIM_DILATE * RIM_DILATE && xx >= 0 && yy >= 0 && xx < W &&
                        yy < H && a[yy * W + xx] > m) {
                        m = a[yy * W + xx];
                    }
                }
            }
            dl[y * W + x] = m;
        }
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float s = 0.0f;
            for (int i = -r; i <= r; i++) {
                const int xx = x + i;
                if (xx >= 0 && xx < W) {
                    s += dl[y * W + xx] * k[i + r];
                }
            }
            t[y * W + x] = s;
        }
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float s = 0.0f;
            for (int i = -r; i <= r; i++) {
                const int yy = y + i;
                if (yy >= 0 && yy < H) {
                    s += t[yy * W + x] * k[i + r];
                }
            }
            float v = RIM_CAP * (1.0f - expf(-RIM_GAIN * s));
            /* the rim is at least the letter: drawn under it, it is the
               letter's own alpha where the letter is */
            const float c = a[y * W + x];
            v = v > c ? v : c;
            /* the outermost ring is zero, so a bilinear quad fades out */
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1) {
                v = 0.0f;
            }
            out[y * W + x] = (uint8_t)lrintf(v * 255.0f);
        }
    }
    free(a);
    free(t);
    free(dl);
    *rw = W;
    *rh = H;
    *pad = p;
    return out;
}

typedef struct Cell {
    int glyph, rim, w, h;
} Cell;

static int cmpCellH(const void *a, const void *b)
{
    const Cell *x = a, *y = b;
    return y->h - x->h;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void putF(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    put32(p, v);
}

/* sheetNear for the letter's own ink alone, at (x, y) from its box's left
   edge and the baseline */
static float ownNear(const Inst *in, int x, int y)
{
    const int ox = (int)lrintf(in->bx), oy = (int)lrintf(in->by);
    float best = 1e9f;
    for (int v = 0; v < in->bh; v++) {
        for (int u = 0; u < in->bw; u++) {
            if (in->bmp[v * in->bw + u] > 0.1f) {
                const float dx = (float)(x - (ox + u)), dy = (float)(y - (oy + v));
                best = dx * dx + dy * dy < best ? dx * dx + dy * dy : best;
            }
        }
    }
    const float d = sqrtf(best);
    return d <= NEAR_SHEET               ? 1.0f
           : d >= NEAR_SHEET + NEAR_FADE ? 0.0f
                                         : (NEAR_SHEET + NEAR_FADE - d) / NEAR_FADE;
}

int ui_GfBuilderFinish(UiGfBuilder *b, float mainEm, uint8_t **blobOut, size_t *sizeOut,
                       UiGfStats *stats)
{
    int rc = -1;
    Glyph *gl = NULL;
    uint32_t *cps = NULL;
    const Inst **vec = NULL;
    Cell *cells = NULL;
    uint8_t *blob = NULL;
    int ng = 0;
    float *tmp = NULL;

    b->mainEm = mainEm;
    /* two passes: the second with the first's expected widths */
    for (int pass = 0; pass < 2; pass++) {
        clearInstances(b);
        b->st.lines = b->st.exact = b->st.aligned = b->st.splits = b->st.unaligned = 0;
        b->st.instances = 0;
        for (int i = 0; i < b->nsrc; i++) {
            segment(b, &b->src[i]);
        }
        if (pass == 0) {
            /* the expected widths: per character, the median width over its
               capitals */
            free(b->expect);
            b->expect = calloc((size_t)b->ninst + 1, sizeof(Expect));
            tmp = malloc(((size_t)b->ninst + 1) * sizeof(float));
            if (!b->expect || !tmp) {
                goto out;
            }
            b->nexpect = 0;
            for (int i = 0; i < b->ninst; i++) {
                int seen = 0;
                for (int e = 0; e < b->nexpect; e++) {
                    seen |= b->expect[e].cp == b->inst[i].cp;
                }
                if (seen) {
                    continue;
                }
                int k = 0;
                for (int j = i; j < b->ninst; j++) {
                    if (b->inst[j].cp == b->inst[i].cp) {
                        tmp[k++] = (float)b->inst[j].boxW / b->inst[j].cap;
                    }
                }
                b->expect[b->nexpect].cp = b->inst[i].cp;
                b->expect[b->nexpect].w = median(tmp, k);
                b->nexpect++;
            }
            free(tmp);
            tmp = NULL;
        }
    }
    if (b->ninst == 0) {
        goto out;
    }

    /* the characters */
    cps = malloc((size_t)b->ninst * sizeof(uint32_t));
    vec = malloc((size_t)b->ninst * 2 * sizeof(*vec));
    tmp = malloc(((size_t)b->ninst + (size_t)b->npairs + (size_t)b->nspaces + 1) * sizeof(float));
    if (!cps || !vec || !tmp) {
        goto out;
    }
    for (int i = 0; i < b->ninst; i++) {
        cps[i] = b->inst[i].cp;
    }
    qsort(cps, (size_t)b->ninst, sizeof(uint32_t), cmpU32);
    int ncp = 0;
    for (int i = 0; i < b->ninst; i++) {
        if (i == 0 || cps[i] != cps[i - 1]) {
            cps[ncp++] = cps[i];
        }
    }
    gl = calloc((size_t)ncp, sizeof(Glyph));
    if (!gl) {
        goto out;
    }
    /* step 7 */
    for (int c = 0; c < ncp; c++) {
        /* the class: the main one when present, else the most common */
        float classes[16];
        int counts[16], nclass = 0, total = 0;
        for (int i = 0; i < b->ninst; i++) {
            if (b->inst[i].cp != cps[c]) {
                continue;
            }
            total++;
            int k = 0;
            while (k < nclass && fabsf(classes[k] - b->inst[i].em) > 0.01f) {
                k++;
            }
            if (k == nclass && nclass < 16) {
                classes[nclass] = b->inst[i].em;
                counts[nclass++] = 0;
            }
            if (k < nclass) {
                counts[k]++;
            }
        }
        int pick = 0;
        for (int k = 0; k < nclass; k++) {
            if (fabsf(classes[k] - mainEm) < 0.01f) {
                pick = k;
                break;
            }
            if (counts[k] > counts[pick]) {
                pick = k;
            }
        }
        /* within the class: the main cell from the instances whose texels
           serve best (the lowest tier: a black rim, a one-line rectangle),
           not cut from a touching pair, and among them those inside a word (a neighbour either side, so their
           cell's sides hold the word's glow as any neighbour would); each
           cap from the instances that end a word on that side */
        int bestTier = 1 << 30;
        for (int i = 0; i < b->ninst; i++) {
            const Inst *in = &b->inst[i];
            if (in->cp == cps[c] && fabsf(in->em - classes[pick]) < 0.01f && in->va) {
                const int t = in->tier * 4 + in->split * 2 + (in->openL || in->openR);
                bestTier = t < bestTier ? t : bestTier;
            }
        }
        int n = 0;
        for (int i = 0; i < b->ninst; i++) {
            const Inst *in = &b->inst[i];
            if (in->cp == cps[c] && fabsf(in->em - classes[pick]) < 0.01f &&
                (bestTier == (1 << 30) ||
                 (in->va && in->tier * 4 + in->split * 2 + (in->openL || in->openR) == bestTier))) {
                vec[n++] = in;
            }
        }
        Glyph *g = &gl[ng];
        for (int side = 0; side < 2; side++) {
            int capTier = 1 << 30, m = 0;
            for (int i = 0; i < b->ninst; i++) {
                const Inst *in = &b->inst[i];
                if (in->cp == cps[c] && fabsf(in->em - classes[pick]) < 0.01f && in->va &&
                    in->hasRim && (side ? in->openR : in->openL)) {
                    const int t = in->tier * 2 + in->split;
                    capTier = t < capTier ? t : capTier;
                }
            }
            const Inst **cv = vec + n;
            for (int i = 0; i < b->ninst && capTier < (1 << 30); i++) {
                const Inst *in = &b->inst[i];
                if (in->cp == cps[c] && fabsf(in->em - classes[pick]) < 0.01f && in->va &&
                    in->hasRim && (side ? in->openR : in->openL) &&
                    in->tier * 2 + in->split == capTier) {
                    cv[m++] = in;
                }
            }
            g->capIn[side] = m ? medoid(cv, m) : NULL;
        }
        ng++;
        g->cp = cps[c];
        g->in = medoid(vec, n);
        g->count = total;
        g->scale = mainEm / g->in->em;
    }

    /* the capitals' height of the main class */
    float capMain = mainEm * 0.688f + 0.7f;
    {
        int k = 0;
        for (int i = 0; i < b->ninst; i++) {
            const Inst *in = &b->inst[i];
            if (fabsf(in->em - mainEm) < 0.01f && in->cp < 0x80 && strchr("HEILTNF", (int)in->cp) &&
                in->boxH > 0) {
                tmp[k++] = (float)in->boxH;
            }
        }
        if (k > 0) {
            capMain = median(tmp, k);
        }
    }

    /* step 8: the side bearings */
    for (int it = 0; it < 200; it++) {
        for (int pass = 0; pass < 2; pass++) {
            for (int c = 0; c < ng; c++) {
                double s = 0.0;
                int k = 0;
                for (int p = 0; p < b->npairs; p++) {
                    const Pair *pr = &b->pairs[p];
                    if (pass == 0 && pr->a == gl[c].cp) {
                        float other = 0.0f;
                        for (int d = 0; d < ng; d++) {
                            if (gl[d].cp == pr->b) {
                                other = gl[d].lsb;
                            }
                        }
                        s += pr->gap - other;
                        k++;
                    } else if (pass == 1 && pr->b == gl[c].cp) {
                        float other = 0.0f;
                        for (int d = 0; d < ng; d++) {
                            if (gl[d].cp == pr->a) {
                                other = gl[d].rsb;
                            }
                        }
                        s += pr->gap - other;
                        k++;
                    }
                }
                if (k > 0) {
                    if (pass == 0) {
                        gl[c].rsb = (float)(s / k);
                        gl[c].hasR = 1;
                    } else {
                        gl[c].lsb = (float)(s / k);
                        gl[c].hasL = 1;
                    }
                }
            }
        }
        /* the gauge: mean left = mean right */
        double sl = 0.0, sr = 0.0;
        int nl = 0, nr = 0;
        for (int c = 0; c < ng; c++) {
            if (gl[c].hasL) {
                sl += gl[c].lsb;
                nl++;
            }
            if (gl[c].hasR) {
                sr += gl[c].rsb;
                nr++;
            }
        }
        const float t = (float)(((nr ? sr / nr : 0.0) - (nl ? sl / nl : 0.0)) * 0.5);
        for (int c = 0; c < ng; c++) {
            gl[c].rsb -= t;
            gl[c].lsb += t;
        }
        if (it > 40 && fabsf(t) < 1e-4f) {
            break;
        }
    }
    {
        /* letters seen in no pair: the median bearings */
        int k = 0;
        for (int c = 0; c < ng; c++) {
            if (gl[c].hasL) {
                tmp[k++] = gl[c].lsb;
            }
        }
        const float ml = k ? median(tmp, k) : 0.5f;
        k = 0;
        for (int c = 0; c < ng; c++) {
            if (gl[c].hasR) {
                tmp[k++] = gl[c].rsb;
            }
        }
        const float mr = k ? median(tmp, k) : 0.5f;
        for (int c = 0; c < ng; c++) {
            if (!gl[c].hasL) {
                gl[c].lsb = ml;
            }
            if (!gl[c].hasR) {
                gl[c].rsb = mr;
            }
        }
    }

    /* the kerning pairs: mean residuals of half a texel or more inwards (whole
       texels), seen twice.  Only pairs set closer: a pair set apart would
       leave a column between the letters' cells that neither holds */
    typedef struct K {
        uint32_t a, b;
        float adj;
    } K;

    K *kerns = NULL;
    int nk = 0, capk = 0;
    {
        char *used = calloc((size_t)b->npairs + 1, 1);
        if (!used) {
            goto out;
        }
        for (int p = 0; p < b->npairs; p++) {
            if (used[p]) {
                continue;
            }
            const uint32_t pa = b->pairs[p].a, pb = b->pairs[p].b;
            float ra = 0.0f, lb = 0.0f;
            for (int d = 0; d < ng; d++) {
                if (gl[d].cp == pa) {
                    ra = gl[d].rsb;
                }
                if (gl[d].cp == pb) {
                    lb = gl[d].lsb;
                }
            }
            int k = 0;
            for (int q = p; q < b->npairs; q++) {
                if (b->pairs[q].a == pa && b->pairs[q].b == pb) {
                    used[q] = 1;
                    tmp[k++] = b->pairs[q].gap - ra - lb;
                }
            }
            double mean = 0.0;
            for (int q = 0; q < k; q++) {
                mean += tmp[q];
            }
            mean /= k;
            if (k >= 2 && mean <= -0.5 && lrint(mean) != 0 &&
                grow((void **)&kerns, &capk, nk + 1, sizeof(K)) == 0) {
                kerns[nk].a = pa;
                kerns[nk].b = pb;
                kerns[nk].adj = (float)lrint(mean); /* whole texels: cells tile */
                nk++;
            }
        }
        free(used);
    }
    float space = 0.35f * mainEm;
    {
        /* each word gap less the bearings either side of it */
        int k = 0;
        for (int s = 0; s < b->nspaces; s++) {
            float ra = 0.0f, lb = 0.0f;
            for (int d = 0; d < ng; d++) {
                if (gl[d].cp == b->spaces[s].a) {
                    ra = gl[d].rsb;
                }
                if (gl[d].cp == b->spaces[s].b) {
                    lb = gl[d].lsb;
                }
            }
            tmp[k++] = b->spaces[s].gap - ra - lb;
        }
        if (k > 0) {
            space = median(tmp, k);
        }
        space = floorf(space + 0.5f); /* whole texels */
    }

    /* step 9, 10: the cells.  Two layers of alpha, drawn black:
       - the sheet's texels, as cut, where they are near a letter's ink on
         the sheet (sheetNear: the letters, their antialiasing, the rim's dark
         edge; fading out from NEAR_SHEET to NEAR_SHEET + NEAR_FADE texels):
         the main cell is the letter's columns from its left bearing to its
         right one (whole texels, so a word's cells tile as the sheet's
         columns did; columns past the instance's own cell on the sheet repeat
         its edge), from the main instance (a letter inside a word on a
         black-rimmed one-line row where there is one); the caps are the
         CAP_W columns beyond either side, drawn at a word's ends, from an
         instance that ends a word on that side.  With them, light cells:
         the sheet's light where its alpha is kept, the ink elsewhere;
       - the fitted glow (step 9), the whole reach of each letter, less
         where the first layer keeps the sheet's texels, drawn overlapping so
         a word's glows add up as the sheet's word glow does; four variants
         (inside a word, at its start, at its end, alone), since past an open
         side only the letter's own ink holds the sheet's texels (the cap's).
       The far glow is the one place the sheets differ from row to row (its
       plateau, black or grey); cut per letter it would patch.  Each A / P
       cell has an apron of one texel of the neighbouring values all round,
       so a bilinear quad over its inner texels joins the next cell without a
       seam; the ink and glow cells have a zero border. */
    cells = calloc((size_t)ng * GC_COUNT, sizeof(Cell));
    if (!cells) {
        free(kerns);
        goto out;
    }
    int ncell = 0;
    for (int c = 0; c < ng; c++) {
        Glyph *g = &gl[c];
        const Inst *in = g->in;
        const float sc = g->scale;
        g->left = (int)lrintf(-g->lsb / sc);
        g->right = in->boxW + (int)lrintf(g->rsb / sc);
        if (g->right <= g->left) {
            g->right = g->left + 1;
        }
        /* the ink */
        g->cw[GC_INK] = in->bw + 2;
        g->ch[GC_INK] = in->bh + 2;
        g->cell[GC_INK] = calloc((size_t)g->cw[GC_INK] * (size_t)g->ch[GC_INK], 1);
        int pad = 0, mw = 0, mh = 0;
        uint8_t *model = makeRim(in->bmp, in->bw, in->bh, &mw, &mh, &pad);
        if (!g->cell[GC_INK] || !model) {
            free(model);
            free(kerns);
            goto out;
        }
        for (int y = 0; y < in->bh; y++) {
            for (int x = 0; x < in->bw; x++) {
                g->cell[GC_INK][(y + 1) * g->cw[GC_INK] + (x + 1)] =
                    (uint8_t)lrintf(in->bmp[y * in->bw + x] * 255.0f);
            }
        }
        const int mx0 = (int)lrintf(in->bx) - pad, my0 = (int)lrintf(in->by) - pad;
        for (int kind = 0; kind < 3; kind++) {
            /* the instance this pair is cut from: the main one, or the cap's
               (an instance ending a word on that side); none: the fitted glow */
            const Inst *src = kind == 0 ? in : g->capIn[kind - 1];
            if (src && !(src->hasRim && src->va)) {
                src = NULL;
            }
            const int inner = kind == 0 ? g->right - g->left : CAP_W;
            const int x0 = kind == 0 ? g->left : kind == 1 ? g->left - CAP_W : g->right;
            /* rows (from the baseline): the source's line, or the glow's */
            int top = my0, bot = my0 + mh;
            if (src) {
                top = src->vy < top ? src->vy : top;
                bot = src->vy + src->vh > bot ? src->vy + src->vh : bot;
            }
            const int rows = bot - top;
            g->top[kind] = top;
            /* the main cell in four variants: on a side open in the word set
               by the face (its start, its end) only the letter's own
               surroundings are the sheet's, not the edge of the neighbour it
               had on the sheet */
            for (int cell = 0; cell < (kind == 0 ? 8 : 2); cell++) {
                const int variant = cell / 2, light = cell & 1;
                const int k = variant == 0 ? GC_MAIN_A + kind * 2 + light
                                           : GC_MAIN_A_L + (variant - 1) * 2 + light;
                const int openL = variant & 1, openR = variant & 2;
                const int mid = (int)lrintf(in->bx) + in->bw / 2;
                g->cw[k] = inner + 2;
                g->ch[k] = rows + 2;
                g->cell[k] = calloc((size_t)g->cw[k] * (size_t)g->ch[k], 1);
                if (!g->cell[k]) {
                    free(model);
                    free(kerns);
                    goto out;
                }
                for (int y = -1; y <= rows; y++) {
                    for (int x = -1; x <= inner; x++) {
                        const int cx = x0 + x, cy = top + y; /* from the box's left, the baseline */
                        /* the alpha cells hold the sheet's texels alone (the
                           fitted glow is its own layer, GC_GLOW); the light
                           cells the letter's ink */
                        float v = 0.0f;
                        if (light) {
                            /* where the sheet's texels are not kept, the
                               letter's own ink (its owned texels) */
                            const Inst *li = src ? src : in;
                            const int lx = cx - (int)lrintf(li->bx), ly = cy - (int)lrintf(li->by);
                            v = (lx >= 0 && ly >= 0 && lx < li->bw && ly < li->bh)
                                    ? li->bmp[ly * li->bw + lx]
                                    : 0.0f;
                        }
                        if (src) {
                            int sx = cx - src->vx;
                            const int sy = cy - src->vy;
                            /* the main cell's columns past its cell on the sheet
                               (a neighbour nearer than the bearing) repeat its
                               edge column */
                            if (kind == 0) {
                                sx = sx < 0 ? 0 : sx >= src->vw ? src->vw - 1 : sx;
                            }
                            /* rows past the source's line band (the glow's
                               reach above and below it) have no sheet texels:
                               every array indexed by (sx, sy) is read inside */
                            const int inside = sy >= 0 && sy < src->vh && sx >= 0 && sx < src->vw;
                            float wgt = inside ? src->vn[sy * src->vw + sx] : 0.0f;
                            if (inside && kind == 0 &&
                                ((openL && cx < mid) || (openR && cx >= mid))) {
                                wgt = ownNear(in, cx, cy);
                            }
                            if (wgt > 0.0f) {
                                /* the sheet's alpha; its light (a grey rim's
                                   own light included), never a neighbour's
                                   letter (whose texels weigh 0) */
                                v = light ? wgt * src->vp[sy * src->vw + sx] + (1.0f - wgt) * v
                                          : wgt * src->va[sy * src->vw + sx];
                            }
                        }
                        g->cell[k][(y + 1) * g->cw[k] + (x + 1)] = (uint8_t)lrintf(v * 255.0f);
                    }
                }
            }
        }
        /* the fitted glow, the whole of its reach, less where the sheet's
           texels are kept (near any letter's ink on the main instance's
           sheet); drawn overlapping, so the glows of a word's letters add up
           as the sheet's word glow does */
        g->glowX = mx0;
        g->glowY = my0;
        for (int variant = 0; variant < 4; variant++) {
            const int k = GC_GLOW + variant;
            const int openL = variant & 1, openR = variant & 2;
            g->cw[k] = mw;
            g->ch[k] = mh;
            g->cell[k] = calloc((size_t)mw * (size_t)mh, 1);
            if (!g->cell[k]) {
                free(model);
                free(kerns);
                goto out;
            }
            for (int y = 0; y < mh; y++) {
                for (int x = 0; x < mw; x++) {
                    const int cx = mx0 + x, cy = my0 + y; /* from the box's left, the baseline */
                    float wgt = 0.0f;
                    if (in->hasRim && in->nmap) {
                        /* past an open side's bearing the sheet's texels kept
                           are the cap's, near the letter's own ink alone */
                        /* an open side: from the letter's middle outwards, as
                           the main cell's variants have it; there the sheet's
                           texels kept are near the letter's own ink, in the
                           main cell and in the cap (none without a cap cut
                           from the sheet: the glow alone) */
                        const int mid = (int)lrintf(in->bx) + in->bw / 2;
                        const int open = (openL && cx < mid) || (openR && cx >= mid);
                        const int inCap = cx < g->left || cx >= g->right;
                        const Inst *cap = cx < mid ? g->capIn[0] : g->capIn[1];
                        if (open) {
                            /* in the cap's columns, near the cap instance's own
                               ink: the cap's texels are placed by its box */
                            wgt = !inCap                          ? ownNear(in, cx, cy)
                                  : cap && cap->hasRim && cap->va ? ownNear(cap, cx, cy)
                                                                  : 0.0f;
                        } else {
                            const int nx = cx - in->nx, ny = cy - in->ny;
                            if (nx >= 0 && ny >= 0 && nx < in->nw && ny < in->nh) {
                                wgt = in->nmap[ny * in->nw + nx];
                            }
                        }
                    }
                    const float v = (float)model[y * mw + x] / 255.0f * (1.0f - wgt);
                    g->cell[k][y * mw + x] = (uint8_t)lrintf(v * 255.0f);
                }
            }
        }
        free(model);
        for (int k = 0; k < GC_COUNT; k++) {
            cells[ncell++] = (Cell){c, k, g->cw[k], g->ch[k]};
        }
    }
    qsort(cells, (size_t)ncell, sizeof(Cell), cmpCellH);
    for (int i = 0; i < ncell; i++) {
        if (cells[i].w + 2 * CELL_GAP > ATLAS_W) {
            /* a cell wider than a shelf: no packing places it */
            free(kerns);
            goto out;
        }
    }
    int sx = CELL_GAP, sy = CELL_GAP, shelf = 0;
    for (int i = 0; i < ncell; i++) {
        Cell *cl = &cells[i];
        if (sx + cl->w + CELL_GAP > ATLAS_W) {
            sy += shelf + CELL_GAP;
            sx = CELL_GAP;
            shelf = 0;
        }
        Glyph *g = &gl[cl->glyph];
        g->cx[cl->rim] = sx;
        g->cy[cl->rim] = sy;
        sx += cl->w + CELL_GAP;
        shelf = cl->h > shelf ? cl->h : shelf;
    }
    int atlasH = sy + shelf + CELL_GAP;
    atlasH = (atlasH + 3) & ~3;
    if ((atlasH & (atlasH - 1)) == 0) {
        atlasH += 4; /* never a power of two: no mip chain */
    }
    if (atlasH > 8192) {
        free(kerns);
        goto out;
    }

    /* the blob */
    const size_t size = UI_GF_HEADER + (size_t)ng * UI_GF_GLYPH + (size_t)nk * UI_GF_KERN +
                        (size_t)b->nsheets * UI_GF_SHEET_NAME + (size_t)ATLAS_W * (size_t)atlasH;
    blob = calloc(size, 1);
    if (!blob) {
        free(kerns);
        goto out;
    }
    memcpy(blob, UI_GF_MAGIC, 4);
    put32(blob + 4, UI_GF_VERSION);
    put16(blob + 8, ATLAS_W);
    put16(blob + 10, (unsigned)atlasH);
    putF(blob + 12, mainEm);
    putF(blob + 16, capMain);
    putF(blob + 20, space);
    put32(blob + 24, (uint32_t)ng);
    put32(blob + 28, (uint32_t)nk);
    put32(blob + 32, (uint32_t)b->nsheets);
    uint8_t *p = blob + UI_GF_HEADER;
    for (int c = 0; c < ng; c++) {
        const Glyph *g = &gl[c];
        const Inst *in = g->in;
        const float s = g->scale;
        /* the pen is the main cell's left edge; the ink cell's top-left is
           the bitmap's offset from it, less the border */
        const float idx = ((float)lrintf(in->bx) - (float)g->left - 1.0f) * s;
        const float idy = ((float)lrintf(in->by) - 1.0f) * s;
        put32(p, g->cp);
        putF(p + 4, s);
        put16(p + 8, (unsigned)g->cx[GC_INK]);
        put16(p + 10, (unsigned)g->cy[GC_INK]);
        put16(p + 12, (unsigned)g->cw[GC_INK]);
        put16(p + 14, (unsigned)g->ch[GC_INK]);
        for (int kind = 0; kind < 3; kind++) {
            const int ka = GC_MAIN_A + kind * 2, kp = ka + 1;
            uint8_t *q = p + 16 + kind * 16;
            put16(q, (unsigned)g->cx[ka]);
            put16(q + 2, (unsigned)g->cy[ka]);
            put16(q + 4, (unsigned)g->cx[kp]);
            put16(q + 6, (unsigned)g->cy[kp]);
            put16(q + 8, (unsigned)g->cw[ka]);
            put16(q + 10, (unsigned)g->ch[ka]);
            putF(q + 12, (float)g->top[kind] * s);
        }
        putF(p + 64, idx);
        putF(p + 68, idy);
        putF(p + 72, (float)(g->right - g->left) * s);
        put16(p + 76, (unsigned)in->sheet);
        p[78] = (uint8_t)in->lang;
        p[79] = (uint8_t)(g->count > 255 ? 255 : g->count);
        putF(p + 80, (float)(g->glowX - g->left) * s);
        putF(p + 84, (float)g->glowY * s);
        put16(p + 88, (unsigned)g->cw[GC_GLOW]);
        put16(p + 90, (unsigned)g->ch[GC_GLOW]);
        for (int v = 0; v < 4; v++) {
            put16(p + 92 + v * 4, (unsigned)g->cx[GC_GLOW + v]);
            put16(p + 94 + v * 4, (unsigned)g->cy[GC_GLOW + v]);
        }
        for (int v = 0; v < 3; v++) {
            put16(p + 108 + v * 8, (unsigned)g->cx[GC_MAIN_A_L + v * 2]);
            put16(p + 110 + v * 8, (unsigned)g->cy[GC_MAIN_A_L + v * 2]);
            put16(p + 112 + v * 8, (unsigned)g->cx[GC_MAIN_P_L + v * 2]);
            put16(p + 114 + v * 8, (unsigned)g->cy[GC_MAIN_P_L + v * 2]);
        }
        p += UI_GF_GLYPH;
    }
    for (int k = 0; k < nk; k++) {
        put32(p, kerns[k].a);
        put32(p + 4, kerns[k].b);
        putF(p + 8, kerns[k].adj);
        p += UI_GF_KERN;
    }
    for (int s = 0; s < b->nsheets; s++) {
        memcpy(p, b->sheets[s], UI_GF_SHEET_NAME);
        p += UI_GF_SHEET_NAME;
    }
    for (int c = 0; c < ng; c++) {
        const Glyph *g = &gl[c];
        for (int k = 0; k < GC_COUNT; k++) {
            for (int y = 0; y < g->ch[k]; y++) {
                memcpy(p + (size_t)(g->cy[k] + y) * ATLAS_W + (size_t)g->cx[k],
                       g->cell[k] + (size_t)y * (size_t)g->cw[k], (size_t)g->cw[k]);
            }
        }
    }
    free(kerns);
    b->st.glyphs = ng;
    b->st.kerns = nk;
    *blobOut = blob;
    *sizeOut = size;
    blob = NULL;
    rc = 0;
out:
    if (stats) {
        *stats = b->st;
    }
    if (gl) {
        for (int c = 0; c < ng; c++) {
            for (int k = 0; k < GC_COUNT; k++) {
                free(gl[c].cell[k]);
            }
        }
    }
    free(gl);
    free(cps);
    free(vec);
    free(cells);
    free(tmp);
    free(blob);
    return rc;
}
