/*
 * port/ui/pad_glyphs.c
 *
 * The button pictures of other controllers (pad_glyphs.h): the 24
 * contributed SVG files read by a reader of what they use, rasterised at
 * the size they are drawn and kept as one RGBA atlas per set and height.
 *
 * What a file may hold (anything else refuses the piece, and a set with a
 * refused piece can never be the style in force):
 *   <?xml ...?> and comments; one root <svg> (version, xmlns, width, height,
 *   id, and a viewBox, which is required); <g> (id only: no transform);
 *   <style> (type) holding one rule ".<class> { ... }" with fill, stroke
 *   (#rgb, #rrggbb or none), stroke-width, stroke-miterlimit and
 *   paint-order; exactly one <path> (id, class naming the rule, d,
 *   fill-rule nonzero or evenodd) whose data uses M L H V Q C Z in absolute
 *   and relative forms with implicit repeats, every subpath closed.
 *
 * How a piece is drawn, as a browser draws the file: the curves flattened
 * into segments at the output size; the fill by the file's rule; the stroke
 * (stroke-width wide, miter joins up to stroke-miterlimit and bevel joins
 * past it, SVG's stroke-linejoin default) built as outlines: a band along
 * every segment and a join piece at every vertex on the outer side of the
 * turn; paint-order "stroke" first puts the fill over the stroke; all of it
 * clipped to the viewBox (the root's overflow).  Both layers go through
 * stb_truetype's scanline rasteriser (font.c ui__raster_polys), which sums
 * each polygon's signed area per pixel and takes its magnitude: so the
 * stroke's pieces are all given one orientation (they unite), and an
 * evenodd fill's contours are oriented by how deeply each is nested inside
 * the others (even depths one way, odd ones the other), which makes a hole
 * inside an outline cancel to zero, the even-odd result for outlines that
 * do not cross (the letters' holes: A, B, R, a, b ...).
 *
 * What is drawn is the piece's ink, not its viewBox (pad_glyphs.h): the
 * files place their letters differently in boxes of three sizes, so each
 * piece's ink box is measured once from the outline (pgInk) and every
 * raster, cell and aspect is that box at the set's lettering height
 * (pgRefs).
 */
#include "pad_glyphs.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#ifdef ICO_RD
#include "rd.h"
#endif

/* the files, embedded at build time (pad_glyphs.cmake, embed_font.cmake):
   ui_pad_<set>_<button>_svg[], ui_pad_<set>_<button>_svg_size */
#define PG_SVG(set, b)                                                                             \
    extern const unsigned char ui_pad_##set##_##b##_svg[];                                         \
    extern const unsigned int ui_pad_##set##_##b##_svg_size;
#define PG_SET(set)                                                                                \
    PG_SVG(set, cross)                                                                             \
    PG_SVG(set, circle)                                                                            \
    PG_SVG(set, square)                                                                            \
    PG_SVG(set, triangle)                                                                          \
    PG_SVG(set, l1)                                                                                \
    PG_SVG(set, r1)                                                                                \
    PG_SVG(set, l2)                                                                                \
    PG_SVG(set, r2)
PG_SET(xbox)
PG_SET(nintendo)
PG_SET(wii_classic)

typedef struct PgSource {
    const unsigned char *data;
    const unsigned int *size;
} PgSource;

#define PG_REF(set, b) {ui_pad_##set##_##b##_svg, &ui_pad_##set##_##b##_svg_size}
#define PG_ROW(set)                                                                                \
    {PG_REF(set, cross), PG_REF(set, circle), PG_REF(set, square), PG_REF(set, triangle),          \
     PG_REF(set, l1),    PG_REF(set, r1),     PG_REF(set, l2),     PG_REF(set, r2)}

#define PG_SETS (ICO_PAD_GLYPHS_COUNT - 1) /* the styles with files: Xbox .. Wii Classic */

static const PgSource s_embedded[PG_SETS][PAD_GLYPHS_PIECES] = {PG_ROW(xbox), PG_ROW(nintendo),
                                                                PG_ROW(wii_classic)};

static const char *const kSetDir[PG_SETS] = {"xbox", "nintendo", "wii_classic"};
static const char *const kPieceName[PAD_GLYPHS_PIECES] = {"cross", "circle", "square", "triangle",
                                                          "l1",    "r1",     "l2",     "r2"};
static const char *const kStyleName[ICO_PAD_GLYPHS_COUNT] = {"PlayStation", "Xbox", "Nintendo",
                                                             "Wii Classic Controller"};

/* The reader's bounds: a file, its path commands, its nesting; a raster's
   flattened points and polygon points (the shipped files: under 900
   bytes, under 60 commands, a few hundred points at 256 rows) */
#define PG_MAX_SRC 8192
#define PG_MAX_OPS 512
#define PG_MAX_DEPTH 8
#define PG_MAX_FLAT 16384
#define PG_MAX_POLY (1 << 18)
#define PG_MAX_COORD 10000.0f
#define PG_MAX_PX 1024 /* rows a CPU raster may ask for */

typedef struct PgOp {
    char op; /* 'M' 'L' 'Q' 'C' 'Z', absolute user units */
    float p[6];
} PgOp;

typedef struct PgPiece {
    int valid;
    char why[160];
    float vbx, vby, vbw, vbh;
    uint8_t fill[3], stroke[3];
    int hasFill, hasStroke, evenodd, strokeFirst;
    float strokeW, miter;
    int nops;
    PgOp *ops; /* malloc'd, nops */
    /* the ink box (user units from the viewBox's corner), the outline's
       own top and height (no stroke), whether the piece is a capital or
       ascender letter (tall) and its lettering height in user units (ref:
       pgRefs, per set) */
    float ix0, iy0, ix1, iy1;
    float oy0, oh;
    int tall;
    float ref;
} PgPiece;

static struct {
    int inited;
    PgPiece piece[PG_SETS][PAD_GLYPHS_PIECES];
    int complete[PG_SETS];
    int style;
    unsigned char refusedLogged[ICO_PAD_GLYPHS_COUNT];
    unsigned stale[ICO_PAD_GLYPHS_COUNT]; /* bumped when a set's pieces change */
} s_pg;

#ifdef ICO_RD
static void atlasSweep(void);
#endif

/* ============================================================ the reader */

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static int pgFail(PgPiece *pc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pc->why, sizeof(pc->why), fmt, ap);
    va_end(ap);
    pc->valid = 0;
    return 0;
}

static int isWs(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int isDigit(char c)
{
    return c >= '0' && c <= '9';
}

static int isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int isNameCh(char c)
{
    return isAlpha(c) || isDigit(c) || c == '-' || c == '_' || c == ':' || c == '.';
}

static void skipWs(const char **p, const char *e)
{
    while (*p < e && isWs(**p)) {
        (*p)++;
    }
}

static int startsWith(const char *p, const char *e, const char *lit)
{
    const size_t n = strlen(lit);
    return (size_t)(e - p) >= n && memcmp(p, lit, n) == 0;
}

/* name [p, e) equals lit */
static int nameIs(const char *p, size_t n, const char *lit)
{
    return strlen(lit) == n && memcmp(p, lit, n) == 0;
}

/* an SVG number (sign, digits, fraction, exponent), after optional white
   space and commas; locale-free.  0 when there is none. */
static int pgNum(const char **pp, const char *e, float *out)
{
    const char *p = *pp;
    while (p < e && (isWs(*p) || *p == ',')) {
        p++;
    }
    double sign = 1.0, v = 0.0;
    int digits = 0;
    if (p < e && (*p == '+' || *p == '-')) {
        sign = *p == '-' ? -1.0 : 1.0;
        p++;
    }
    while (p < e && isDigit(*p)) {
        v = v * 10.0 + (double)(*p - '0');
        digits++;
        p++;
    }
    if (p < e && *p == '.') {
        double f = 0.1;
        p++;
        while (p < e && isDigit(*p)) {
            v += (double)(*p - '0') * f;
            f *= 0.1;
            digits++;
            p++;
        }
    }
    if (!digits) {
        return 0;
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int es = 1, ex = 0;
        if (q < e && (*q == '+' || *q == '-')) {
            es = *q == '-' ? -1 : 1;
            q++;
        }
        if (q < e && isDigit(*q)) {
            while (q < e && isDigit(*q)) {
                if (ex < 100) {
                    ex = ex * 10 + (*q - '0');
                }
                q++;
            }
            v *= pow(10.0, (double)(es * ex));
            p = q;
        }
    }
    v *= sign;
    if (!(fabs(v) <= (double)PG_MAX_COORD)) {
        return 0;
    }
    *out = (float)v;
    *pp = p;
    return 1;
}

static int hexVal(char c)
{
    if (isDigit(c)) {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* #rgb, #rrggbb or none: 1 with *on and rgb, 0 for anything else */
static int pgColour(const char *v, size_t n, int *on, uint8_t rgb[3])
{
    if (nameIs(v, n, "none")) {
        *on = 0;
        return 1;
    }
    if ((n != 4 && n != 7) || v[0] != '#') {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if (hexVal(v[i]) < 0) {
            return 0;
        }
    }
    for (int k = 0; k < 3; k++) {
        rgb[k] = n == 7 ? (uint8_t)(hexVal(v[1 + 2 * k]) * 16 + hexVal(v[2 + 2 * k]))
                        : (uint8_t)(hexVal(v[1 + k]) * 17);
    }
    *on = 1;
    return 1;
}

/* a plain number filling the whole value (an optional "px" after it) */
static int pgValueNum(const char *v, size_t n, float *out)
{
    const char *p = v, *e = v + n;
    if (n >= 2 && v[n - 2] == 'p' && v[n - 1] == 'x') {
        e -= 2;
    }
    if (!pgNum(&p, e, out)) {
        return 0;
    }
    return p == e;
}

/* paint-order: the listed tokens first, the rest in the default order
   (fill, stroke, markers); strokeFirst when stroke comes before fill */
static int pgPaintOrder(const char *v, size_t n, int *strokeFirst)
{
    const char *p = v, *e = v + n;
    int posFill = -1, posStroke = -1, pos = 0, normal = 0;
    while (p < e) {
        skipWs(&p, e);
        if (p >= e) {
            break;
        }
        const char *t = p;
        while (p < e && !isWs(*p)) {
            p++;
        }
        const size_t tn = (size_t)(p - t);
        if (nameIs(t, tn, "normal")) {
            normal = 1;
        } else if (nameIs(t, tn, "fill")) {
            posFill = pos;
        } else if (nameIs(t, tn, "stroke")) {
            posStroke = pos;
        } else if (!nameIs(t, tn, "markers")) {
            return 0;
        }
        pos++;
    }
    if (normal && pos != 1) {
        return 0;
    }
    if (posStroke < 0) {
        *strokeFirst = 0;
    } else if (posFill < 0) {
        *strokeFirst = 1;
    } else {
        *strokeFirst = posStroke < posFill;
    }
    return 1;
}

static void trim(const char **v, size_t *n)
{
    while (*n && isWs(**v)) {
        (*v)++;
        (*n)--;
    }
    while (*n && isWs((*v)[*n - 1])) {
        (*n)--;
    }
}

/* the <style> text: exactly one rule ".class { ... }" */
static int pgCss(PgPiece *pc, const char *p, const char *e, char *cls, size_t clsSize)
{
    skipWs(&p, e);
    if (p >= e || *p != '.') {
        return pgFail(pc, "the style is not one class rule");
    }
    p++;
    const char *c0 = p;
    while (p < e && (isAlpha(*p) || isDigit(*p) || *p == '-' || *p == '_')) {
        p++;
    }
    const size_t cn = (size_t)(p - c0);
    if (cn == 0 || cn >= clsSize) {
        return pgFail(pc, "the style's class name");
    }
    memcpy(cls, c0, cn);
    cls[cn] = '\0';
    skipWs(&p, e);
    if (p >= e || *p != '{') {
        return pgFail(pc, "the style is not one class rule");
    }
    p++;
    for (;;) {
        skipWs(&p, e);
        if (p >= e) {
            return pgFail(pc, "the style rule is not closed");
        }
        if (*p == '}') {
            p++;
            break;
        }
        const char *n0 = p;
        while (p < e && (isAlpha(*p) || *p == '-')) {
            p++;
        }
        const size_t nn = (size_t)(p - n0);
        skipWs(&p, e);
        if (nn == 0 || p >= e || *p != ':') {
            return pgFail(pc, "a style declaration that cannot be read");
        }
        p++;
        const char *v = p;
        while (p < e && *p != ';' && *p != '}') {
            p++;
        }
        size_t vn = (size_t)(p - v);
        trim(&v, &vn);
        if (p < e && *p == ';') {
            p++;
        }
        float f = 0.0f;
        if (nameIs(n0, nn, "fill")) {
            if (!pgColour(v, vn, &pc->hasFill, pc->fill)) {
                return pgFail(pc, "the fill colour is not #rgb, #rrggbb or none");
            }
        } else if (nameIs(n0, nn, "stroke")) {
            if (!pgColour(v, vn, &pc->hasStroke, pc->stroke)) {
                return pgFail(pc, "the stroke colour is not #rgb, #rrggbb or none");
            }
        } else if (nameIs(n0, nn, "stroke-width")) {
            if (!pgValueNum(v, vn, &f) || f < 0.0f || f > 64.0f) {
                return pgFail(pc, "the stroke width");
            }
            pc->strokeW = f;
        } else if (nameIs(n0, nn, "stroke-miterlimit")) {
            if (!pgValueNum(v, vn, &f) || f < 1.0f || f > 1000.0f) {
                return pgFail(pc, "the stroke miter limit");
            }
            pc->miter = f;
        } else if (nameIs(n0, nn, "paint-order")) {
            if (!pgPaintOrder(v, vn, &pc->strokeFirst)) {
                return pgFail(pc, "the paint order");
            }
        } else {
            return pgFail(pc, "the style property \"%.*s\"", (int)(nn > 40 ? 40 : nn), n0);
        }
    }
    skipWs(&p, e);
    if (p != e) {
        return pgFail(pc, "more than one style rule");
    }
    return 1;
}

static int pgEmit(PgPiece *pc, PgOp *ops, int *n, char op, const float *p, int np)
{
    if (*n >= PG_MAX_OPS) {
        return pgFail(pc, "more than %d path commands", PG_MAX_OPS);
    }
    PgOp *o = &ops[(*n)++];
    memset(o, 0, sizeof(*o));
    o->op = op;
    for (int i = 0; i < np; i++) {
        o->p[i] = p[i];
    }
    return 1;
}

/* the path data into absolute ops */
static int pgPath(PgPiece *pc, const char *d, size_t len, PgOp *ops, int *nops)
{
    const char *p = d, *e = d + len;
    float cx = 0.0f, cy = 0.0f, sx = 0.0f, sy = 0.0f;
    char cmd = 0;
    int haveSub = 0, segs = 0, closed = 0;

    *nops = 0;
    for (;;) {
        while (p < e && (isWs(*p) || *p == ',')) {
            p++;
        }
        if (p >= e) {
            break;
        }
        if (isAlpha(*p)) {
            const char c = *p++;
            if (!strchr("MmLlHhVvQqCcZz", c)) {
                return pgFail(pc, "the path command '%c'", c);
            }
            cmd = c;
            if (c == 'Z' || c == 'z') {
                if (!haveSub) {
                    return pgFail(pc, "a close before any subpath");
                }
                if (!pgEmit(pc, ops, nops, 'Z', NULL, 0)) {
                    return 0;
                }
                cx = sx;
                cy = sy;
                segs = 0;
                closed = 1;
                continue;
            }
        } else if (!cmd) {
            return pgFail(pc, "path data before a command");
        } else if (cmd == 'Z' || cmd == 'z') {
            return pgFail(pc, "numbers after a close");
        }
        const char lc = (char)(cmd | 0x20);
        const int rel = cmd == lc;
        const int argc = lc == 'h' || lc == 'v' ? 1 : lc == 'q' ? 4 : lc == 'c' ? 6 : 2;
        float a[6];
        for (int i = 0; i < argc; i++) {
            if (!pgNum(&p, e, &a[i])) {
                return pgFail(pc, "the path command '%c' lacks a number", cmd);
            }
        }
        if (lc == 'm') {
            if (haveSub && segs > 0 && !closed) {
                return pgFail(pc, "an open subpath (its line ends are not drawn)");
            }
            cx = rel ? cx + a[0] : a[0];
            cy = rel ? cy + a[1] : a[1];
            sx = cx;
            sy = cy;
            const float pt[2] = {cx, cy};
            if (!pgEmit(pc, ops, nops, 'M', pt, 2)) {
                return 0;
            }
            haveSub = 1;
            segs = 0;
            closed = 0;
            cmd = rel ? 'l' : 'L'; /* the implicit repeats of a moveto are linetos */
            continue;
        }
        if (!haveSub) {
            return pgFail(pc, "a drawing command before the first moveto");
        }
        if (closed) {
            /* after a close the next subpath starts at the same point */
            const float pt[2] = {sx, sy};
            if (!pgEmit(pc, ops, nops, 'M', pt, 2)) {
                return 0;
            }
            closed = 0;
            segs = 0;
        }
        const float ox = rel ? cx : 0.0f, oy = rel ? cy : 0.0f;
        float q[6];
        char op;
        int np;
        switch (lc) {
        case 'l':
            q[0] = ox + a[0];
            q[1] = oy + a[1];
            op = 'L';
            np = 2;
            break;
        case 'h':
            q[0] = ox + a[0];
            q[1] = cy;
            op = 'L';
            np = 2;
            break;
        case 'v':
            q[0] = cx;
            q[1] = oy + a[0];
            op = 'L';
            np = 2;
            break;
        case 'q':
            for (int i = 0; i < 4; i++) {
                q[i] = (i & 1 ? oy : ox) + a[i];
            }
            op = 'Q';
            np = 4;
            break;
        default: /* 'c' */
            for (int i = 0; i < 6; i++) {
                q[i] = (i & 1 ? oy : ox) + a[i];
            }
            op = 'C';
            np = 6;
            break;
        }
        for (int i = 0; i < np; i++) {
            if (!(fabsf(q[i]) <= PG_MAX_COORD)) {
                return pgFail(pc, "a path point out of range");
            }
        }
        if (!pgEmit(pc, ops, nops, op, q, np)) {
            return 0;
        }
        cx = q[np - 2];
        cy = q[np - 1];
        segs++;
    }
    if (haveSub && segs > 0 && !closed) {
        return pgFail(pc, "an open subpath (its line ends are not drawn)");
    }
    if (*nops == 0) {
        return pgFail(pc, "an empty path");
    }
    return 1;
}

/* one attribute: name, value (quotes off); 0 at the tag's end; -1 bad */
static int pgAttr(const char **pp, const char *e, const char **name, size_t *nameN,
                  const char **val, size_t *valN)
{
    const char *p = *pp;
    skipWs(&p, e);
    if (p >= e || *p == '>' || *p == '/') {
        *pp = p;
        return 0;
    }
    const char *n0 = p;
    while (p < e && isNameCh(*p)) {
        p++;
    }
    if (p == n0) {
        return -1;
    }
    *name = n0;
    *nameN = (size_t)(p - n0);
    skipWs(&p, e);
    if (p >= e || *p != '=') {
        return -1;
    }
    p++;
    skipWs(&p, e);
    if (p >= e || (*p != '"' && *p != '\'')) {
        return -1;
    }
    const char q = *p++;
    const char *v0 = p;
    while (p < e && *p != q) {
        if (*p == '&' || *p == '<') {
            return -1; /* no entities */
        }
        p++;
    }
    if (p >= e) {
        return -1;
    }
    *val = v0;
    *valN = (size_t)(p - v0);
    *pp = p + 1;
    return 1;
}

enum { EL_SVG = 1, EL_STYLE, EL_G, EL_PATH };

/* the whole file into pc (its ops malloc'd); 1, or 0 with pc->why */
static int pgParse(PgPiece *pc, const char *src, size_t n)
{
    memset(pc, 0, sizeof(*pc));
    /* SVG's initial values */
    pc->hasFill = 1;
    pc->strokeW = 1.0f;
    pc->miter = 4.0f;
    if (!src || n == 0) {
        return pgFail(pc, "an empty file");
    }
    if (n > PG_MAX_SRC) {
        return pgFail(pc, "larger than %d bytes", PG_MAX_SRC);
    }
    const char *p = src, *e = src + n;
    int stack[PG_MAX_DEPTH], depth = 0, sawSvg = 0, rootClosed = 0, paths = 0, styles = 0;
    int haveViewBox = 0;
    char cls[32] = "", pathCls[32] = "";
    int pathHasCls = 0;
    const char *d = NULL;
    size_t dn = 0;

    while (p < e) {
        if (*p != '<') {
            const char *t = p;
            while (p < e && *p != '<') {
                p++;
            }
            if (depth > 0 && stack[depth - 1] == EL_STYLE) {
                if (styles++) {
                    return pgFail(pc, "more than one style text");
                }
                if (!pgCss(pc, t, p, cls, sizeof(cls))) {
                    return 0;
                }
            } else {
                for (const char *c = t; c < p; c++) {
                    if (!isWs(*c)) {
                        return pgFail(pc, "text outside the style");
                    }
                }
            }
            continue;
        }
        if (startsWith(p, e, "<?")) {
            if (sawSvg) {
                return pgFail(pc, "a processing instruction inside the picture");
            }
            const char *q = p + 2;
            while (q < e && !startsWith(q, e, "?>")) {
                q++;
            }
            if (q >= e) {
                return pgFail(pc, "an unclosed processing instruction");
            }
            p = q + 2;
            continue;
        }
        if (startsWith(p, e, "<!--")) {
            const char *q = p + 4;
            while (q < e && !startsWith(q, e, "-->")) {
                q++;
            }
            if (q >= e) {
                return pgFail(pc, "an unclosed comment");
            }
            p = q + 3;
            continue;
        }
        if (startsWith(p, e, "<!")) {
            return pgFail(pc, "a DOCTYPE or CDATA section");
        }
        const int closing = startsWith(p, e, "</");
        p += closing ? 2 : 1;
        const char *n0 = p;
        while (p < e && isNameCh(*p)) {
            p++;
        }
        const size_t nn = (size_t)(p - n0);
        int el = 0;
        if (nameIs(n0, nn, "svg")) {
            el = EL_SVG;
        } else if (nameIs(n0, nn, "style")) {
            el = EL_STYLE;
        } else if (nameIs(n0, nn, "g")) {
            el = EL_G;
        } else if (nameIs(n0, nn, "path")) {
            el = EL_PATH;
        } else {
            return pgFail(pc, "the element <%.*s>", (int)(nn > 32 ? 32 : nn), n0);
        }
        if (closing) {
            skipWs(&p, e);
            if (p >= e || *p != '>' || depth == 0 || stack[depth - 1] != el) {
                return pgFail(pc, "a closing tag that does not match");
            }
            p++;
            depth--;
            if (depth == 0) {
                rootClosed = 1;
            }
            continue;
        }
        if (rootClosed) {
            return pgFail(pc, "content after the root");
        }
        if ((depth == 0) != (el == EL_SVG)) {
            return pgFail(pc, depth == 0 ? "the root is not <svg>" : "a nested <svg>");
        }
        if (depth > 0 && (stack[depth - 1] == EL_STYLE || stack[depth - 1] == EL_PATH)) {
            return pgFail(pc, "an element inside <style> or <path>");
        }
        if (el == EL_SVG) {
            sawSvg = 1;
        }
        if (el == EL_PATH && paths++) {
            return pgFail(pc, "more than one <path>");
        }
        for (;;) {
            const char *an = NULL, *av = NULL;
            size_t ann = 0, avn = 0;
            const int r = pgAttr(&p, e, &an, &ann, &av, &avn);
            if (r < 0) {
                return pgFail(pc, "an attribute that cannot be read");
            }
            if (r == 0) {
                break;
            }
            int ok = nameIs(an, ann, "id");
            if (el == EL_SVG) {
                if (nameIs(an, ann, "viewBox")) {
                    const char *vp = av, *ve = av + avn;
                    float v[4];
                    for (int i = 0; i < 4; i++) {
                        if (!pgNum(&vp, ve, &v[i])) {
                            return pgFail(pc, "the viewBox");
                        }
                    }
                    skipWs(&vp, ve);
                    if (vp != ve ||
                        !(v[2] >= 1.0f && v[3] >= 1.0f && v[2] <= 1000.0f && v[3] <= 1000.0f &&
                          v[2] <= 4.0f * v[3] && v[3] <= 4.0f * v[2] && fabsf(v[0]) <= 100000.0f &&
                          fabsf(v[1]) <= 100000.0f)) {
                        /* a size under 1 would scale the outline past what
                           the rasteriser's floats hold (a NaN there) */
                        return pgFail(pc, "the viewBox (four numbers, a size from 1 to 1000, "
                                          "an aspect within 4)");
                    }
                    pc->vbx = v[0];
                    pc->vby = v[1];
                    pc->vbw = v[2];
                    pc->vbh = v[3];
                    haveViewBox = 1;
                    ok = 1;
                } else {
                    ok = ok || nameIs(an, ann, "version") || nameIs(an, ann, "xmlns") ||
                         nameIs(an, ann, "width") || nameIs(an, ann, "height");
                }
            } else if (el == EL_STYLE) {
                ok = nameIs(an, ann, "type");
            } else if (el == EL_PATH) {
                if (nameIs(an, ann, "d")) {
                    d = av;
                    dn = avn;
                    ok = 1;
                } else if (nameIs(an, ann, "class")) {
                    const char *cv = av;
                    size_t cvn = avn;
                    trim(&cv, &cvn);
                    for (size_t i = 0; i < cvn; i++) {
                        if (isWs(cv[i])) {
                            return pgFail(pc, "more than one class on the path");
                        }
                    }
                    if (cvn == 0 || cvn >= sizeof(pathCls)) {
                        return pgFail(pc, "the path's class");
                    }
                    memcpy(pathCls, cv, cvn);
                    pathCls[cvn] = '\0';
                    pathHasCls = 1;
                    ok = 1;
                } else if (nameIs(an, ann, "fill-rule")) {
                    if (nameIs(av, avn, "evenodd")) {
                        pc->evenodd = 1;
                    } else if (!nameIs(av, avn, "nonzero")) {
                        return pgFail(pc, "the fill rule");
                    }
                    ok = 1;
                }
            }
            if (!ok) {
                return pgFail(pc, "the attribute %.*s on <%.*s>", (int)(ann > 32 ? 32 : ann), an,
                              (int)nn, n0);
            }
        }
        if (p >= e) {
            return pgFail(pc, "an unclosed tag");
        }
        if (*p == '/') {
            if (p + 1 >= e || p[1] != '>') {
                return pgFail(pc, "an unclosed tag");
            }
            p += 2;
            if (el == EL_SVG) {
                rootClosed = 1;
            }
            continue;
        }
        p++; /* '>' */
        if (depth >= PG_MAX_DEPTH) {
            return pgFail(pc, "nested deeper than %d", PG_MAX_DEPTH);
        }
        stack[depth++] = el;
    }
    if (!sawSvg || !rootClosed || depth != 0) {
        return pgFail(pc, "no closed <svg>");
    }
    if (!haveViewBox) {
        return pgFail(pc, "no viewBox");
    }
    if (paths != 1 || !d) {
        return pgFail(pc, "no <path> with data");
    }
    if (pathHasCls != (cls[0] != '\0') || (pathHasCls && strcmp(pathCls, cls) != 0)) {
        return pgFail(pc, "the path's class is not the style's rule");
    }
    PgOp *ops = malloc(sizeof(PgOp) * PG_MAX_OPS);
    if (!ops) {
        return pgFail(pc, "no memory");
    }
    int nops = 0;
    if (!pgPath(pc, d, dn, ops, &nops)) {
        free(ops);
        return 0;
    }
    pc->ops = malloc(sizeof(PgOp) * (size_t)nops);
    if (!pc->ops) {
        free(ops);
        return pgFail(pc, "no memory");
    }
    memcpy(pc->ops, ops, sizeof(PgOp) * (size_t)nops);
    free(ops);
    pc->nops = nops;
    pc->valid = 1;
    pc->why[0] = '\0';
    return 1;
}

/* ============================================================ geometry */

typedef struct PgPolys {
    float *xy; /* points, pairs */
    int *cnt;  /* each polygon's point count */
    int n, npts, capPts, capN;
} PgPolys;

static int polysReserve(PgPolys *ps, int morePts)
{
    if (ps->npts + morePts > PG_MAX_POLY) {
        return 0;
    }
    if (ps->npts + morePts > ps->capPts) {
        int cap = ps->capPts ? ps->capPts : 1024;
        while (cap < ps->npts + morePts) {
            cap *= 2;
        }
        if (cap > PG_MAX_POLY) {
            cap = PG_MAX_POLY;
        }
        float *xy = realloc(ps->xy, sizeof(float) * 2 * (size_t)cap);
        if (!xy) {
            return 0;
        }
        ps->xy = xy;
        ps->capPts = cap;
    }
    if (ps->n + 1 > ps->capN) {
        const int cap = ps->capN ? ps->capN * 2 : 256;
        int *c = realloc(ps->cnt, sizeof(int) * (size_t)cap);
        if (!c) {
            return 0;
        }
        ps->cnt = c;
        ps->capN = cap;
    }
    return 1;
}

static void polysFree(PgPolys *ps)
{
    free(ps->xy);
    free(ps->cnt);
    memset(ps, 0, sizeof(*ps));
}

static double signedArea(const float *xy, int n)
{
    double a = 0.0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        a += (double)xy[2 * j] * xy[2 * i + 1] - (double)xy[2 * i] * xy[2 * j + 1];
    }
    return a * 0.5;
}

static void reversePoly(float *xy, int n)
{
    for (int i = 0, j = n - 1; i < j; i++, j--) {
        const float x = xy[2 * i], y = xy[2 * i + 1];
        xy[2 * i] = xy[2 * j];
        xy[2 * i + 1] = xy[2 * j + 1];
        xy[2 * j] = x;
        xy[2 * j + 1] = y;
    }
}

/* Sutherland-Hodgman against one edge of the box (axis 0: x, 1: y; keep
   the side >= bound when ge, else <= bound); out holds up to 2 n points.
   On a polygon that is not convex the result keeps every winding number
   inside the box (the parts outside become segments along the edge). */
static int clipEdge(const float *in, int n, float *out, int axis, float bound, int ge)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const float *c = &in[2 * i], *pv = &in[2 * ((i + n - 1) % n)];
        const int inC = ge ? c[axis] >= bound : c[axis] <= bound;
        const int inP = ge ? pv[axis] >= bound : pv[axis] <= bound;
        if (inC != inP) {
            const float t = (bound - pv[axis]) / (c[axis] - pv[axis]);
            float *o = &out[2 * m++];
            o[0] = pv[0] + (c[0] - pv[0]) * t;
            o[1] = pv[1] + (c[1] - pv[1]) * t;
            o[axis] = bound;
        }
        if (inC) {
            out[2 * m] = c[0];
            out[2 * m + 1] = c[1];
            m++;
        }
    }
    return m;
}

/* adds polygon xy (n points, pixels) to ps clipped to [0, W] x [0, H];
   orient: +1 / -1 forces that sign of area, 0 keeps it.  0 on overflow. */
static int polysAdd(PgPolys *ps, const float *xy, int n, float W, float H, int orient)
{
    if (n < 3) {
        return 1;
    }
    float x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
    for (int i = 1; i < n; i++) {
        x0 = fminf(x0, xy[2 * i]);
        x1 = fmaxf(x1, xy[2 * i]);
        y0 = fminf(y0, xy[2 * i + 1]);
        y1 = fmaxf(y1, xy[2 * i + 1]);
    }
    if (x1 <= 0.0f || y1 <= 0.0f || x0 >= W || y0 >= H) {
        return 1; /* wholly outside the viewBox: nothing of it shows */
    }
    const float *src = xy;
    int m = n;
    float *a = NULL, *b = NULL;
    if (x0 < 0.0f || y0 < 0.0f || x1 > W || y1 > H) {
        const size_t cap = (size_t)n * 16 + 16;
        a = malloc(sizeof(float) * 2 * cap);
        b = malloc(sizeof(float) * 2 * cap);
        if (!a || !b) {
            free(a);
            free(b);
            return 0;
        }
        m = clipEdge(xy, n, a, 0, 0.0f, 1);
        m = clipEdge(a, m, b, 0, W, 0);
        m = clipEdge(b, m, a, 1, 0.0f, 1);
        m = clipEdge(a, m, b, 1, H, 0);
        src = b;
    }
    int ok = 1;
    if (m >= 3) {
        if (!polysReserve(ps, m)) {
            ok = 0;
        } else {
            float *dst = &ps->xy[2 * ps->npts];
            memcpy(dst, src, sizeof(float) * 2 * (size_t)m);
            const double ar = signedArea(dst, m);
            if (orient && ar * orient < 0.0) {
                reversePoly(dst, m);
            }
            ps->cnt[ps->n++] = m;
            ps->npts += m;
        }
    }
    free(a);
    free(b);
    return ok;
}

typedef struct PgFlat {
    float *xy; /* user units, viewBox-relative */
    int *start, *len;
    int ncont, npts;
} PgFlat;

static void flatFree(PgFlat *f)
{
    free(f->xy);
    free(f->start);
    free(f->len);
    memset(f, 0, sizeof(*f));
}

static int flatPoint(PgFlat *f, float x, float y)
{
    const int c = f->ncont - 1;
    if (c < 0) {
        return 0;
    }
    if (f->len[c] > 0) {
        const float *l = &f->xy[2 * (f->npts - 1)];
        if (fabsf(l[0] - x) < 1e-6f && fabsf(l[1] - y) < 1e-6f) {
            return 1;
        }
    }
    if (f->npts >= PG_MAX_FLAT) {
        return 0;
    }
    f->xy[2 * f->npts] = x;
    f->xy[2 * f->npts + 1] = y;
    f->npts++;
    f->len[c]++;
    return 1;
}

/* the last contour's closing point dropped when it repeats the first */
static void flatEnd(PgFlat *f)
{
    const int c = f->ncont - 1;
    if (c >= 0 && f->len[c] > 1) {
        const float *a = &f->xy[2 * f->start[c]], *l = &f->xy[2 * (f->npts - 1)];
        if (fabsf(l[0] - a[0]) < 1e-6f && fabsf(l[1] - a[1]) < 1e-6f) {
            f->npts--;
            f->len[c]--;
        }
    }
}

/* segments for a curve whose control polygon is L pixels long */
static int curveSegs(float L)
{
    if (!(L < 128.0f)) {
        return 64; /* long, or not a number: never cast */
    }
    int n = (int)ceilf(L / 2.0f);
    return n < 2 ? 2 : n;
}

/* the ops as closed contours of points (user units less the viewBox's
   origin); sx, sy: pixels per unit (the curves' segment counts) */
static int flatten(const PgPiece *pc, float sx, float sy, PgFlat *f)
{
    memset(f, 0, sizeof(*f));
    f->xy = malloc(sizeof(float) * 2 * PG_MAX_FLAT);
    f->start = malloc(sizeof(int) * (size_t)(pc->nops + 1));
    f->len = malloc(sizeof(int) * (size_t)(pc->nops + 1));
    if (!f->xy || !f->start || !f->len) {
        flatFree(f);
        return 0;
    }
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < pc->nops; i++) {
        const PgOp *o = &pc->ops[i];
        float q[6];
        for (int k = 0; k < 6; k++) {
            q[k] = o->p[k] - (k & 1 ? pc->vby : pc->vbx);
        }
        switch (o->op) {
        case 'M':
            flatEnd(f);
            f->start[f->ncont] = f->npts;
            f->len[f->ncont] = 0;
            f->ncont++;
            if (!flatPoint(f, q[0], q[1])) {
                flatFree(f);
                return 0;
            }
            cx = q[0];
            cy = q[1];
            break;
        case 'L':
            if (!flatPoint(f, q[0], q[1])) {
                flatFree(f);
                return 0;
            }
            cx = q[0];
            cy = q[1];
            break;
        case 'Q':
        case 'C': {
            const int cubic = o->op == 'C';
            const float ex = cubic ? q[4] : q[2], ey = cubic ? q[5] : q[3];
            float L = hypotf((q[0] - cx) * sx, (q[1] - cy) * sy);
            if (cubic) {
                L += hypotf((q[2] - q[0]) * sx, (q[3] - q[1]) * sy) +
                     hypotf((q[4] - q[2]) * sx, (q[5] - q[3]) * sy);
            } else {
                L += hypotf((q[2] - q[0]) * sx, (q[3] - q[1]) * sy);
            }
            const int n = curveSegs(L);
            for (int s = 1; s <= n; s++) {
                const float t = (float)s / (float)n, u = 1.0f - t;
                float x, y;
                if (cubic) {
                    x = u * u * u * cx + 3.0f * u * u * t * q[0] + 3.0f * u * t * t * q[2] +
                        t * t * t * q[4];
                    y = u * u * u * cy + 3.0f * u * u * t * q[1] + 3.0f * u * t * t * q[3] +
                        t * t * t * q[5];
                } else {
                    x = u * u * cx + 2.0f * u * t * q[0] + t * t * q[2];
                    y = u * u * cy + 2.0f * u * t * q[1] + t * t * q[3];
                }
                if (s == n) {
                    x = ex;
                    y = ey;
                }
                if (!flatPoint(f, x, y)) {
                    flatFree(f);
                    return 0;
                }
            }
            cx = ex;
            cy = ey;
            break;
        }
        default: /* 'Z' */
            flatEnd(f);
            break;
        }
    }
    flatEnd(f);
    return 1;
}

/* crossing-number test of (x, y) against contour c */
static int insideContour(const PgFlat *f, int c, float x, float y)
{
    const float *p = &f->xy[2 * f->start[c]];
    const int n = f->len[c];
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float yi = p[2 * i + 1], yj = p[2 * j + 1];
        if ((yi > y) != (yj > y)) {
            const float xc = p[2 * j] + (y - yj) * (p[2 * i] - p[2 * j]) / (yi - yj);
            if (x < xc) {
                in = !in;
            }
        }
    }
    return in;
}

/* the fill's polygons in pixels: the file's orientation (non-zero), or for
   evenodd each contour turned by its nesting depth */
static int fillPolys(const PgPiece *pc, const PgFlat *f, float sx, float sy, float W, float H,
                     PgPolys *ps)
{
    float *buf = malloc(sizeof(float) * 2 * (size_t)(f->npts + 1));
    if (!buf) {
        return 0;
    }
    int ok = 1;
    for (int c = 0; c < f->ncont && ok; c++) {
        const int n = f->len[c];
        if (n < 3) {
            continue;
        }
        const float *p = &f->xy[2 * f->start[c]];
        for (int i = 0; i < n; i++) {
            buf[2 * i] = p[2 * i] * sx;
            buf[2 * i + 1] = p[2 * i + 1] * sy;
        }
        int orient = 0;
        if (pc->evenodd) {
            int depth = 0;
            for (int o = 0; o < f->ncont; o++) {
                if (o != c && f->len[o] >= 3 && insideContour(f, o, p[0], p[1])) {
                    depth++;
                }
            }
            orient = depth & 1 ? -1 : 1;
        }
        ok = polysAdd(ps, buf, n, W, H, orient);
    }
    free(buf);
    return ok;
}

/* the stroke's outline pieces in pixels, all of one orientation: a band of
   half-width hw along each segment, at each vertex the outer side's miter
   (or bevel past the miter limit) */
static int strokePolys(const PgPiece *pc, const PgFlat *f, float sx, float sy, float W, float H,
                       PgPolys *ps)
{
    const float hw = pc->strokeW * 0.5f;
    for (int c = 0; c < f->ncont; c++) {
        const int n = f->len[c];
        const float *p = &f->xy[2 * f->start[c]];
        if (n < 2) {
            continue;
        }
        for (int i = 0; i < n; i++) {
            const float *a = &p[2 * i], *b = &p[2 * ((i + 1) % n)];
            const float dx = b[0] - a[0], dy = b[1] - a[1], len = hypotf(dx, dy);
            if (len < 1e-6f) {
                continue;
            }
            const float nx = -dy / len * hw, ny = dx / len * hw;
            const float q[8] = {(a[0] + nx) * sx, (a[1] + ny) * sy, (b[0] + nx) * sx,
                                (b[1] + ny) * sy, (b[0] - nx) * sx, (b[1] - ny) * sy,
                                (a[0] - nx) * sx, (a[1] - ny) * sy};
            if (!polysAdd(ps, q, 4, W, H, 1)) {
                return 0;
            }
        }
        for (int i = 0; i < n; i++) {
            const float *pv = &p[2 * ((i + n - 1) % n)], *cu = &p[2 * i],
                        *nv = &p[2 * ((i + 1) % n)];
            float d0x = cu[0] - pv[0], d0y = cu[1] - pv[1], d1x = nv[0] - cu[0],
                  d1y = nv[1] - cu[1];
            const float l0 = hypotf(d0x, d0y), l1 = hypotf(d1x, d1y);
            if (l0 < 1e-6f || l1 < 1e-6f) {
                continue;
            }
            d0x /= l0;
            d0y /= l0;
            d1x /= l1;
            d1y /= l1;
            const float cr = d0x * d1y - d0y * d1x, dt = d0x * d1x + d0y * d1y;
            if (fabsf(cr) < 1e-7f && dt > 0.0f) {
                continue; /* straight on: the bands meet */
            }
            /* the outer side: away from the turn (left normal (-dy, dx)) */
            const float sgn = cr > 0.0f ? -1.0f : 1.0f;
            const float o0x = -d0y * sgn, o0y = d0x * sgn, o1x = -d1y * sgn, o1y = d1x * sgn;
            const float ax = cu[0] + o0x * hw, ay = cu[1] + o0y * hw;
            const float bx = cu[0] + o1x * hw, by = cu[1] + o1y * hw;
            /* the miter's length over the stroke width is 1 / sin(theta / 2),
               theta the angle between the segments: 1 / cos(turn / 2) */
            const float half = sqrtf(fmaxf(0.0f, (1.0f + dt) * 0.5f));
            float q[8];
            int m = 0;
            q[m++] = cu[0] * sx;
            q[m++] = cu[1] * sy;
            q[m++] = ax * sx;
            q[m++] = ay * sy;
            if (half > 1e-6f && 1.0f / half <= pc->miter) {
                float mx = o0x + o1x, my = o0y + o1y;
                const float ml = hypotf(mx, my);
                if (ml > 1e-6f) {
                    mx /= ml;
                    my /= ml;
                    q[m++] = (cu[0] + mx * hw / half) * sx;
                    q[m++] = (cu[1] + my * hw / half) * sy;
                }
            }
            q[m++] = bx * sx;
            q[m++] = by * sy;
            if (!polysAdd(ps, q, m / 2, W, H, 1)) {
                return 0;
            }
        }
    }
    return 1;
}

/* points a user unit the curves are flattened at to measure the ink (a
   letter of 24 units at 384 rows: past every size the atlases are made at) */
#define PG_INK_SCALE 16.0f

/* The piece's ink box: the bounds of every polygon the raster draws (the
   fill, the stroke's bands and joins, all clipped to the viewBox), in user
   units from the viewBox's corner; and its outline's top and height (the
   flattened path itself, no stroke), which say whether it is a short
   letter (pgRefs).  1, or 0 with the refusal in pc->why. */
static int pgInk(PgPiece *pc)
{
    PgFlat f;
    if (!flatten(pc, PG_INK_SCALE, PG_INK_SCALE, &f)) {
        return pgFail(pc, "the outline is past the reader's bounds");
    }
    float oy0 = PG_MAX_COORD * 4.0f, oy1 = -PG_MAX_COORD * 4.0f;
    for (int i = 0; i < f.npts; i++) {
        oy0 = fminf(oy0, f.xy[2 * i + 1]);
        oy1 = fmaxf(oy1, f.xy[2 * i + 1]);
    }
    PgPolys ps;
    memset(&ps, 0, sizeof(ps));
    int ok = !pc->hasFill || fillPolys(pc, &f, 1.0f, 1.0f, pc->vbw, pc->vbh, &ps);
    if (ok && pc->hasStroke && pc->strokeW > 0.0f) {
        ok = strokePolys(pc, &f, 1.0f, 1.0f, pc->vbw, pc->vbh, &ps);
    }
    float x0 = pc->vbw, y0 = pc->vbh, x1 = 0.0f, y1 = 0.0f;
    for (int i = 0; ok && i < ps.npts; i++) {
        x0 = fminf(x0, ps.xy[2 * i]);
        x1 = fmaxf(x1, ps.xy[2 * i]);
        y0 = fminf(y0, ps.xy[2 * i + 1]);
        y1 = fmaxf(y1, ps.xy[2 * i + 1]);
    }
    polysFree(&ps);
    flatFree(&f);
    if (!ok) {
        return pgFail(pc, "the outline is past the reader's bounds");
    }
    /* a hundredth of a unit: a sliver no raster size shows */
    if (!(x1 - x0 >= 0.01f && y1 - y0 >= 0.01f)) {
        return pgFail(pc, "nothing drawn inside the viewBox");
    }
    pc->ix0 = x0;
    pc->iy0 = y0;
    pc->ix1 = x1;
    pc->iy1 = y1;
    pc->oy0 = oy0;
    pc->oh = oy1 - oy0;
    return 1;
}

/* the ink's size in texels at a lettering height of pixelH: each side
   rounded up (a capital's height, pixelH exactly, is not rounded past it
   by the float's last bit) */
static int pgSize(const PgPiece *pc, int pixelH, int *w, int *h)
{
    const float s = (float)pixelH / pc->ref;
    const float fw = ceilf((pc->ix1 - pc->ix0) * s - 1e-3f);
    const float fh = ceilf((pc->iy1 - pc->iy0) * s - 1e-3f);
    /* a short letter of a test file may be far taller than its set's
       capitals: the reader's raster bound */
    if (!(fw <= 4.0f * PG_MAX_PX && fh <= 4.0f * PG_MAX_PX)) {
        return -1;
    }
    *w = fw < 1.0f ? 1 : (int)fw;
    *h = fh < 1.0f ? 1 : (int)fh;
    return 0;
}

/* the piece at a lettering height of pixelH into rgba (W x H texels of
   pgSize, stride bytes a row), the ink box's corner at texel (0, 0):
   straight colour, the alpha at GS scale */
static int pgRaster(const PgPiece *pc, int pixelH, int W, int H, uint8_t *rgba, int stride)
{
    const float s = (float)pixelH / pc->ref;
    PgFlat f;
    if (!flatten(pc, s, s, &f)) {
        return -1;
    }
    /* the ink box's corner to the origin: clipping to the ink box is the
       viewBox's clip (the box is the bounds of what lies inside it) */
    for (int i = 0; i < f.npts; i++) {
        f.xy[2 * i] -= pc->ix0;
        f.xy[2 * i + 1] -= pc->iy0;
    }
    const float cw = (pc->ix1 - pc->ix0) * s, ch = (pc->iy1 - pc->iy0) * s;
    const size_t px = (size_t)W * (size_t)H;
    uint8_t *covF = calloc(px, 1), *covS = calloc(px, 1);
    PgPolys ps;
    memset(&ps, 0, sizeof(ps));
    int ok = covF && covS;
    if (ok && pc->hasFill) {
        ok = fillPolys(pc, &f, s, s, cw, ch, &ps) &&
             ui__raster_polys(covF, W, H, W, ps.xy, ps.cnt, ps.n) == 0;
    }
    const int stroke = pc->hasStroke && pc->strokeW > 0.0f;
    if (ok && stroke) {
        ps.n = ps.npts = 0;
        ok = strokePolys(pc, &f, s, s, cw, ch, &ps) &&
             ui__raster_polys(covS, W, H, W, ps.xy, ps.cnt, ps.n) == 0;
    }
    polysFree(&ps);
    flatFree(&f);
    if (ok) {
        /* paint order: the bottom layer, then the top one over it */
        const uint8_t *bot = pc->strokeFirst ? pc->stroke : pc->fill;
        const uint8_t *top = pc->strokeFirst ? pc->fill : pc->stroke;
        const uint8_t *bare = stroke ? pc->stroke : pc->fill;
        for (int y = 0; y < H; y++) {
            uint8_t *row = rgba + (size_t)y * (size_t)stride;
            for (int x = 0; x < W; x++) {
                const size_t i = (size_t)y * (size_t)W + (size_t)x;
                const float af = pc->hasFill ? (float)covF[i] / 255.0f : 0.0f;
                const float as = stroke ? (float)covS[i] / 255.0f : 0.0f;
                const float ab = pc->strokeFirst ? as : af, at = pc->strokeFirst ? af : as;
                const float a = at + ab * (1.0f - at);
                uint8_t *o = row + 4 * x;
                if (a <= 0.0f) {
                    o[0] = bare[0];
                    o[1] = bare[1];
                    o[2] = bare[2];
                    o[3] = 0;
                    continue;
                }
                for (int k = 0; k < 3; k++) {
                    const float c = ((float)top[k] * at + (float)bot[k] * ab * (1.0f - at)) / a;
                    const long v = lrintf(c);
                    o[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
                const long a255 = lrintf(a * 255.0f);
                const int ai = a255 < 0 ? 0 : a255 > 255 ? 255 : (int)a255;
                o[3] = (uint8_t)((ai * 128 + 127) / 255); /* GS alpha: 255 -> 0x80 */
            }
        }
    }
    free(covF);
    free(covS);
    return ok ? 0 : -1;
}

/* ============================================================ the sets */

/* Each valid piece's lettering height in user units.  A capital or
   ascender letter (tall: its outline's top within a quarter of its own
   height of its viewBox's top) is its own: its ink fills the lettering
   height, so every capital of the set comes out the same height (the
   files' capitals are 19.10, 19.54 and 20.00 units tall: one scale for the
   set would leave them 4 % apart).  A short letter takes the lettering of
   the set's first tall piece drawn on a viewBox of its size (the Wii
   Classic Controller's a, x and y take its b's: same font, same grid), or
   failing one the tallest capital's, so it keeps its size against them. */
static void pgRefs(int s)
{
    PgPiece *p = s_pg.piece[s];
    float tallest = 0.0f;
    for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
        if (!p[g].valid) {
            continue;
        }
        p[g].tall = !(p[g].oy0 > 0.25f * p[g].oh);
        p[g].ref = p[g].iy1 - p[g].iy0;
        if (p[g].tall && p[g].ref > tallest) {
            tallest = p[g].ref;
        }
    }
    for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
        if (!p[g].valid || p[g].tall) {
            continue;
        }
        float ref = 0.0f;
        for (int t = 0; t < PAD_GLYPHS_PIECES && ref <= 0.0f; t++) {
            if (p[t].valid && p[t].tall && p[t].vbw == p[g].vbw && p[t].vbh == p[g].vbh) {
                ref = p[t].iy1 - p[t].iy0;
            }
        }
        if (ref <= 0.0f) {
            ref = tallest;
        }
        if (ref > 0.0f) {
            p[g].ref = ref;
        }
    }
}

static void pgCheckSet(int s)
{
    int n = 0;
    for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
        n += s_pg.piece[s][g].valid;
    }
    s_pg.complete[s] = n == PAD_GLYPHS_PIECES;
    pgRefs(s);
}

static void pgLoad(int s, int g, const char *src, size_t n)
{
    PgPiece *pc = &s_pg.piece[s][g];
    free(pc->ops);
    if (pgParse(pc, src, n)) {
        pgInk(pc);
    }
}

void pad_glyphs_init(void)
{
    if (s_pg.inited) {
        return;
    }
    s_pg.inited = 1;
    char line[256];
    size_t at = 0;
    for (int s = 0; s < PG_SETS; s++) {
        for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
            const PgSource *e = &s_embedded[s][g];
            pgLoad(s, g, (const char *)e->data, *e->size);
            const PgPiece *pc = &s_pg.piece[s][g];
            if (!pc->valid) {
                fprintf(stderr, "ui: button pictures: %s/%s.svg refused: %s\n", kSetDir[s],
                        kPieceName[g], pc->why);
            }
        }
        pgCheckSet(s);
        int valid = 0;
        for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
            valid += s_pg.piece[s][g].valid;
        }
        const int k =
            snprintf(line + at, sizeof(line) - at, "%s%s %s", s ? ", " : "", kStyleName[s + 1],
                     s_pg.complete[s] ? "complete" : "incomplete, cannot be chosen");
        if (k > 0 && (size_t)k < sizeof(line) - at) {
            at += (size_t)k;
        }
        if (!s_pg.complete[s]) {
            const int k2 =
                snprintf(line + at, sizeof(line) - at, " (%d of %d)", valid, PAD_GLYPHS_PIECES);
            if (k2 > 0 && (size_t)k2 < sizeof(line) - at) {
                at += (size_t)k2;
            }
        }
    }
    fprintf(stderr, "ui: button pictures: %s; PlayStation: the disc's own\n", line);
}

int pad_glyphs_set_complete(int style)
{
    pad_glyphs_init();
    if (style == ICO_PAD_GLYPHS_PLAYSTATION) {
        return 1;
    }
    if (style <= 0 || style >= ICO_PAD_GLYPHS_COUNT) {
        return 0;
    }
    return s_pg.complete[style - 1];
}

void pad_glyphs_set_style(int style)
{
    pad_glyphs_init();
    if (style < 0 || style >= ICO_PAD_GLYPHS_COUNT || !pad_glyphs_set_complete(style)) {
        const int k = style >= 0 && style < ICO_PAD_GLYPHS_COUNT ? style : 0;
        if (!s_pg.refusedLogged[k]) {
            s_pg.refusedLogged[k] = 1;
            fprintf(stderr,
                    "ui: button pictures: %s (%d) asked for, not a complete set: PlayStation's "
                    "drawn\n",
                    pad_glyphs_style_name(style), style);
        }
        style = ICO_PAD_GLYPHS_PLAYSTATION;
    }
    s_pg.style = style;
#ifdef ICO_RD
    atlasSweep();
#endif
}

int pad_glyphs_style(void)
{
    pad_glyphs_init();
    return s_pg.style;
}

int pad_glyphs_active(void)
{
    pad_glyphs_init();
    return s_pg.style != ICO_PAD_GLYPHS_PLAYSTATION && pad_glyphs_set_complete(s_pg.style);
}

const char *pad_glyphs_style_name(int style)
{
    return style >= 0 && style < ICO_PAD_GLYPHS_COUNT ? kStyleName[style] : "?";
}

/* the piece of a style with files, or NULL */
static const PgPiece *pieceOf(int style, int glyph)
{
    pad_glyphs_init();
    if (style <= 0 || style >= ICO_PAD_GLYPHS_COUNT || glyph < 0 || glyph >= PAD_GLYPHS_PIECES) {
        return NULL;
    }
    return &s_pg.piece[style - 1][glyph];
}

float pad_glyphs_aspect(int style, int glyph)
{
    const PgPiece *pc = pieceOf(style, glyph);
    return pc && pc->valid ? (pc->ix1 - pc->ix0) / (pc->iy1 - pc->iy0) : 0.0f;
}

float pad_glyphs_height(int style, int glyph)
{
    const PgPiece *pc = pieceOf(style, glyph);
    return pc && pc->valid && pc->ref > 0.0f ? (pc->iy1 - pc->iy0) / pc->ref : 0.0f;
}

int pad_glyphs_ink_box(int style, int glyph, float box[4])
{
    const PgPiece *pc = pieceOf(style, glyph);
    if (!pc || !pc->valid || !box) {
        return 0;
    }
    box[0] = pc->ix0;
    box[1] = pc->iy0;
    box[2] = pc->ix1;
    box[3] = pc->iy1;
    return 1;
}

int pad_glyphs_bucket(int pixelH)
{
    static const int kSteps[] = {16, 24, 32, 48, 64, 96, 128, 192, 256};
    for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); i++) {
        if (pixelH <= kSteps[i]) {
            return kSteps[i];
        }
    }
    return 256;
}

int pad_glyphs_raster_size(int style, int glyph, int pixelH, int *w, int *h)
{
    const PgPiece *pc = pieceOf(style, glyph);
    if (!pc || !pc->valid || pixelH < 1 || pixelH > PG_MAX_PX || !w || !h) {
        return -1;
    }
    return pgSize(pc, pixelH, w, h);
}

int pad_glyphs_raster(int style, int glyph, int pixelH, uint8_t *rgba, int stride)
{
    int w = 0, h = 0;
    if (pad_glyphs_raster_size(style, glyph, pixelH, &w, &h) != 0 || !rgba || stride < 4 * w) {
        return -1;
    }
    return pgRaster(pieceOf(style, glyph), pixelH, w, h, rgba, stride);
}

int pad_glyphs_piece_valid(int style, int glyph)
{
    const PgPiece *pc = pieceOf(style, glyph);
    return pc && pc->valid;
}

const char *pad_glyphs_piece_error(int style, int glyph)
{
    const PgPiece *pc = pieceOf(style, glyph);
    if (!pc) {
        return "no such piece";
    }
    return pc->valid ? NULL : pc->why;
}

int pad_glyphs_test_source(int style, int glyph, const char *svg, size_t len)
{
    if (!pieceOf(style, glyph)) {
        return -1;
    }
    const int s = style - 1;
    if (svg) {
        pgLoad(s, glyph, svg, len);
    } else {
        const PgSource *e = &s_embedded[s][glyph];
        pgLoad(s, glyph, (const char *)e->data, *e->size);
    }
    pgCheckSet(s);
    s_pg.stale[style]++;
    if (s_pg.style == style && !s_pg.complete[s]) {
        s_pg.style = ICO_PAD_GLYPHS_PLAYSTATION;
    }
    return s_pg.piece[s][glyph].valid;
}

/* ============================================================ the atlases */

#ifdef ICO_RD
#define PG_ATLASES 6
#define PG_EVICT_FRAMES 4 /* as font.c's: a frame still replayed may name a texture */
#define PG_GUTTER 2

typedef struct PgAtlas {
    int style, h;   /* h 0: free */
    unsigned stale; /* s_pg.stale[style] when made */
    int failed;     /* the upload failed: not tried again until forgotten */
    uint32_t tex;
    int cellX[PAD_GLYPHS_PIECES], cellY[PAD_GLYPHS_PIECES];
    float inkW[PAD_GLYPHS_PIECES], inkH[PAD_GLYPHS_PIECES]; /* texels, unrounded */
    uint32_t lastFrame, lastUse;
} PgAtlas;

static PgAtlas s_atlas[PG_ATLASES];
static uint32_t s_useClock;
static int s_hooked, s_uploadLogged;

static void atlasFree(PgAtlas *a, int destroy)
{
    if (destroy && a->tex) {
        rd_destroy_texture((RdTex){a->tex});
    }
    memset(a, 0, sizeof(*a));
}

/* ui_font_shutdown: the textures go */
static void pgShutdown(void)
{
    for (int i = 0; i < PG_ATLASES; i++) {
        atlasFree(&s_atlas[i], 1);
    }
}

/* ui_font_forget_textures: rd started again, the ids are stale; the atlases
   are made again at their next draw */
static void pgForget(void)
{
    for (int i = 0; i < PG_ATLASES; i++) {
        atlasFree(&s_atlas[i], 0);
    }
}

static int evictable(const PgAtlas *a, uint32_t now)
{
    return now - a->lastFrame >= PG_EVICT_FRAMES;
}

/* the eight pictures of style at lettering height h in one texture: the
   face buttons on the first row, the shoulders on the second, each cell its
   ink rounded up to whole texels, PG_GUTTER transparent texels around each
   (bilinear sampling inside a cell reaches one texel past it); the rows as
   tall as their tallest cell, one more row when the height would be a
   power of two, so no mip chain averages cells together (rd_upload.c
   texLevels) */
static int atlasMake(PgAtlas *a, int style, int h)
{
    int w[PAD_GLYPHS_PIECES], hh[PAD_GLYPHS_PIECES], rowW[2] = {PG_GUTTER, PG_GUTTER};
    int rowH[2] = {0, 0};
    for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
        const PgPiece *pc = pieceOf(style, g);
        if (pad_glyphs_raster_size(style, g, h, &w[g], &hh[g]) != 0) {
            return 0;
        }
        const int r = g / 4;
        const float s = (float)h / pc->ref;
        a->cellX[g] = rowW[r];
        a->inkW[g] = (pc->ix1 - pc->ix0) * s;
        a->inkH[g] = (pc->iy1 - pc->iy0) * s;
        rowW[r] += w[g] + PG_GUTTER;
        rowH[r] = hh[g] > rowH[r] ? hh[g] : rowH[r];
    }
    for (int g = 0; g < PAD_GLYPHS_PIECES; g++) {
        a->cellY[g] = PG_GUTTER + (g / 4) * (rowH[0] + PG_GUTTER);
    }
    const int texW = rowW[0] > rowW[1] ? rowW[0] : rowW[1];
    int texH = rowH[0] + rowH[1] + 3 * PG_GUTTER;
    if ((texH & (texH - 1)) == 0) {
        texH++;
    }
    uint8_t *rgba = calloc((size_t)texW * (size_t)texH, 4);
    if (!rgba) {
        return 0;
    }
    int ok = 1;
    for (int g = 0; g < PAD_GLYPHS_PIECES && ok; g++) {
        uint8_t *cell = rgba + ((size_t)a->cellY[g] * (size_t)texW + (size_t)a->cellX[g]) * 4;
        ok = pad_glyphs_raster(style, g, h, cell, texW * 4) == 0;
    }
    if (ok) {
        char name[48];
        snprintf(name, sizeof(name), "ui pad glyphs %s %dpx", kSetDir[style - 1], h);
        a->tex = rd_create_texture((uint32_t)texW, (uint32_t)texH, rgba, RD_TEXA_80_80, name).id;
        if (!a->tex) {
            ok = 0;
            if (!s_uploadLogged) {
                s_uploadLogged = 1;
                fprintf(stderr,
                        "ui: button pictures: a texture could not be uploaded (no texture slot or "
                        "memory: \"%s\"); the disc's pictures are drawn instead\n",
                        name);
            }
        }
    }
    free(rgba);
    /* a failed one keeps its slot, so it is not rasterised again at every
       draw (until ui_font_forget_textures or the cache drops it) */
    a->failed = !ok;
    a->style = style;
    a->h = h;
    a->stale = s_pg.stale[style];
    if (!s_hooked) {
        s_hooked = 1;
        ui__set_pad_glyph_hooks(pgShutdown, pgForget);
    }
    return ok;
}

/* the atlases of another style than the one in force, or made before
   their set changed: gone once no frame can name them */
static void atlasSweep(void)
{
    const uint32_t now = rd_frame_number();
    for (int i = 0; i < PG_ATLASES; i++) {
        PgAtlas *a = &s_atlas[i];
        if (a->h && (a->style != s_pg.style || a->stale != s_pg.stale[a->style]) &&
            evictable(a, now)) {
            atlasFree(a, 1);
        }
    }
}

static PgAtlas *atlasFor(int style, int h)
{
    const uint32_t now = rd_frame_number();
    PgAtlas *freeSlot = NULL, *oldest = NULL, *nearest = NULL;
    int bestDist = 1 << 30;
    s_useClock++;
    atlasSweep();
    for (int i = 0; i < PG_ATLASES; i++) {
        PgAtlas *a = &s_atlas[i];
        if (!a->h) {
            if (!freeSlot) {
                freeSlot = a;
            }
            continue;
        }
        const int current = a->style == style && a->stale == s_pg.stale[style];
        if (current && a->h == h) {
            if (a->failed) {
                return NULL;
            }
            a->lastUse = s_useClock;
            a->lastFrame = now;
            return a;
        }
        if (current && !a->failed && abs(a->h - h) < bestDist) {
            bestDist = abs(a->h - h);
            nearest = a;
        }
        if (evictable(a, now) && (!oldest || a->lastUse < oldest->lastUse)) {
            oldest = a;
        }
    }
    if (!freeSlot && oldest) {
        atlasFree(oldest, 1);
        freeSlot = oldest;
    }
    if (!freeSlot) {
        if (nearest) {
            nearest->lastUse = s_useClock;
            nearest->lastFrame = now;
        }
        return nearest;
    }
    if (!atlasMake(freeSlot, style, h)) {
        freeSlot->lastUse = s_useClock;
        freeSlot->lastFrame = now;
        return NULL;
    }
    freeSlot->lastUse = s_useClock;
    freeSlot->lastFrame = now;
    return freeSlot;
}

int pad_glyphs_atlas_count(void)
{
    int n = 0;
    for (int i = 0; i < PG_ATLASES; i++) {
        n += s_atlas[i].h != 0 && s_atlas[i].tex != 0;
    }
    return n;
}
#else
int pad_glyphs_atlas_count(void)
{
    return 0;
}
#endif

int pad_glyphs_texture(int glyph, int pixelH, unsigned *tex, float uv[4], float *aspect)
{
    if (tex) {
        *tex = 0;
    }
    if (!tex || !uv || glyph < 0 || glyph >= PAD_GLYPHS_PIECES || !pad_glyphs_active()) {
        return 0;
    }
#ifdef ICO_RD
    const int style = s_pg.style;
    const PgAtlas *a = atlasFor(style, pad_glyphs_bucket(pixelH));
    if (!a || !a->tex) {
        return 0;
    }
    *tex = a->tex;
    uv[0] = (float)a->cellX[glyph];
    uv[1] = (float)a->cellY[glyph];
    uv[2] = (float)a->cellX[glyph] + a->inkW[glyph];
    uv[3] = (float)a->cellY[glyph] + a->inkH[glyph];
    if (aspect) {
        *aspect = pad_glyphs_aspect(style, glyph);
    }
    return 1;
#else
    (void)pixelH;
    (void)aspect;
    return 0;
#endif
}
