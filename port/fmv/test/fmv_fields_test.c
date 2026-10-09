/*
 * port/fmv/test/fmv_fields_test.c
 *
 * What the disc's movies are made of, field by field: with the PAL image
 * (argv[1]) present, every .pss in DATA.DF's directory is demuxed whole
 * and
 *
 *   - every access unit's headers are scanned (ico_m2v_scan_unit): the
 *     sequence's frame_rate_code and progressive_sequence, and per picture
 *     picture_structure, top_field_first, repeat_first_field and
 *     progressive_frame, counted over the whole stream, with the first
 *     pictures' top_field_first / repeat_first_field / progressive_frame
 *     as strings (a 3:2 pulldown shows as a repeating rff cadence);
 *   - the first pictures (120, or argv[2]) are decoded and measured on the
 *     luma: d1 the mean absolute difference between adjacent lines, d2
 *     between lines two apart (one field's neighbours), their ratio (a
 *     smooth progressive picture has d1 < d2; the two fields of a moving
 *     interlaced one differ, so d1 > d2), and "comb" the share of pixels
 *     whose line differs from both its neighbours by more than 10 in the
 *     same direction (the teeth of a comb).  "TP" and "PT" are the same
 *     comb share for the picture woven from this picture's top field and
 *     the previous picture's bottom field, and the other way round: a
 *     telecined film has pictures that are combed as coded but clean in
 *     one of these.  "mot" is the mean top-field difference from the
 *     previous picture (motion).
 *
 * Before the disc, without it: field_match.c on a synthetic telecined
 * film (a bar moving a step a film frame; a picture carrying one frame's
 * top field and the next frame's bottom field is matched with the
 * picture whose bottom field completes it; a whole frame stays as it is).
 *
 * The output is a report for the deinterlacer's design (which movies are
 * interlaced, telecined or progressive); the checks are only that the
 * movies decode without error and that the display-order pairing of the
 * scanned headers matches the decoder's own picture types.  Exit 77 without
 * the image.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "field_match.h"
#include "m2v.h"
#include "pss.h"

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

#define DF_LSN 19771u /* DATA.DF on the PAL disc (fmv_test.c test_disc) */
#define MAX_MOVIES 16
#define CADENCE 60 /* pictures shown in the flag strings */
#define COMB_T 10  /* a line's step from each neighbour, for "comb" */

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
} Buf;

static int put(Buf *b, const void *src, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 1u << 20;
        uint8_t *np;
        while (cap < b->n + n) {
            cap *= 2;
        }
        np = realloc(b->p, cap);
        if (np == NULL) {
            return -1;
        }
        b->p = np;
        b->cap = cap;
    }
    memcpy(b->p + b->n, src, n);
    b->n += n;
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const char *rate_name(unsigned code)
{
    static const char *const names[] = {"forbidden", "23.976", "24",    "25", "29.97",
                                        "30",        "50",     "59.94", "60"};
    return code < sizeof(names) / sizeof(names[0]) ? names[code] : "reserved";
}

static char type_char(unsigned t)
{
    return t == 1 ? 'I' : t == 2 ? 'P' : t == 3 ? 'B' : t == 4 ? 'D' : '?';
}

static char struct_char(unsigned s)
{
    return s == ICO_M2V_TOP_FIELD ? 'T' : s == ICO_M2V_BOTTOM_FIELD ? 'B' : 'F';
}

/* --- the luma measures -------------------------------------------------------- */

/* Row y of the picture woven from a's even rows and b's odd rows (a == b:
   the picture itself). */
static const uint8_t *row_of(const uint8_t *a, const uint8_t *b, uint32_t pitch, uint32_t y)
{
    return ((y & 1) ? b : a) + (size_t)y * pitch;
}

static void line_diffs(const uint8_t *y, uint32_t pitch, uint32_t w, uint32_t h, double *d1,
                       double *d2)
{
    uint64_t s1 = 0, s2 = 0;
    uint32_t r, x;

    for (r = 0; r + 2 < h; r++) {
        const uint8_t *a = y + (size_t)r * pitch, *b = a + pitch, *c = b + pitch;
        for (x = 0; x < w; x++) {
            s1 += (uint64_t)abs((int)a[x] - (int)b[x]);
            s2 += (uint64_t)abs((int)a[x] - (int)c[x]);
        }
    }
    *d1 = h > 2 ? (double)s1 / ((double)(h - 2) * w) : 0.0;
    *d2 = h > 2 ? (double)s2 / ((double)(h - 2) * w) : 0.0;
}

/* the share (percent) of pixels whose line steps away from both
   neighbours by more than COMB_T in the same direction */
static double comb(const uint8_t *a, const uint8_t *b, uint32_t pitch, uint32_t w, uint32_t h)
{
    uint64_t n = 0;
    uint32_t r, x;

    for (r = 1; r + 1 < h; r++) {
        const uint8_t *up = row_of(a, b, pitch, r - 1), *mid = row_of(a, b, pitch, r),
                      *dn = row_of(a, b, pitch, r + 1);
        for (x = 0; x < w; x++) {
            const int e1 = (int)mid[x] - (int)up[x], e2 = (int)mid[x] - (int)dn[x];
            if ((e1 > COMB_T && e2 > COMB_T) || (e1 < -COMB_T && e2 < -COMB_T)) {
                n++;
            }
        }
    }
    return h > 2 ? 100.0 * (double)n / ((double)(h - 2) * w) : 0.0;
}

static double field_motion(const uint8_t *a, const uint8_t *b, uint32_t pitch, uint32_t w,
                           uint32_t h)
{
    uint64_t s = 0;
    uint32_t r, x;

    for (r = 0; r < h; r += 2) {
        for (x = 0; x < w; x++) {
            s += (uint64_t)abs((int)a[(size_t)r * pitch + x] - (int)b[(size_t)r * pitch + x]);
        }
    }
    return h > 0 ? (double)s / ((double)((h + 1) / 2) * w) : 0.0;
}

/* --- one movie ---------------------------------------------------------------- */

static void movie(FILE *f, const char *name, uint32_t lsn, uint32_t size, long want)
{
    uint8_t *buf = malloc(size);
    size_t n, pos = 0;
    Buf es = {0};
    IcoM2v *d;
    IcoM2vScan scan;
    uint32_t w = 0, h = 0;
    uint8_t *prev = NULL;
    /* the whole stream's headers */
    long units = 0, frames = 0, fields = 0, pf = 0, tff = 0, rff = 0, ext = 0;
    char sTff[CADENCE + 1], sRff[CADENCE + 1], sPf[CADENCE + 1], sType[CADENCE + 1];
    int seqProg = -1, seqRate = -1, seqMixed = 0;
    /* the decoded pictures */
    long decoded = 0, interlaced = 0, combed = 0, mismatches = 0;
    double sumRatio = 0.0, sumComb = 0.0;

    printf("\n== %s: %u bytes at sector %u\n", name, size, lsn);
    if (buf == NULL) {
        CHECK(0, "%s: no memory for %u bytes", name, size);
        return;
    }
    fseek(f, (long)lsn * 2048, SEEK_SET);
    n = fread(buf, 1, size, f);
    CHECK(n == size, "%s: read %zu of %u", name, n, size);
    while (pos < n) {
        IcoPssPacket pkt;
        long r = ico_pss_next(buf + pos, n - pos, &pkt);
        if (r <= 0) {
            CHECK(0, "%s: demux stopped at %zu (%ld)", name, pos, r);
            break;
        }
        if (pkt.kind == ICO_PSS_VIDEO && put(&es, pkt.data, pkt.len) != 0) {
            CHECK(0, "%s: no memory for the video", name);
            break;
        }
        pos += (size_t)r;
        if (pkt.kind == ICO_PSS_END) {
            break;
        }
    }
    free(buf);

    /* the headers of every unit */
    memset(&scan, 0, sizeof(scan));
    memset(sTff, 0, sizeof(sTff));
    memset(sRff, 0, sizeof(sRff));
    memset(sPf, 0, sizeof(sPf));
    memset(sType, 0, sizeof(sType));
    pos = 0;
    while (pos < es.n) {
        long s0 = ico_pss_es_find_start(es.p + pos, es.n - pos), e;
        if (s0 < 0) {
            break;
        }
        pos += (size_t)s0;
        if (es.p[pos + 3] == 0xB7) {
            pos += 4;
            continue;
        }
        e = ico_pss_es_unit_end(es.p + pos, es.n - pos);
        if (e < 0) {
            e = (long)(es.n - pos);
        }
        if (ico_m2v_scan_unit(es.p + pos, (size_t)e, &scan)) {
            if (seqProg < 0) {
                seqProg = scan.progressive_sequence;
                seqRate = scan.frame_rate_code;
            } else if (seqProg != scan.progressive_sequence || seqRate != scan.frame_rate_code) {
                seqMixed = 1;
            }
            if (units < CADENCE) {
                sTff[units] = (char)('0' + scan.top_field_first);
                sRff[units] = (char)('0' + scan.repeat_first_field);
                sPf[units] = (char)('0' + scan.progressive_frame);
                sType[units] = type_char(scan.coding_type);
            }
            units++;
            if (scan.picture_structure == ICO_M2V_FRAME_PICTURE) {
                frames++;
            } else {
                fields++;
            }
            pf += scan.progressive_frame;
            tff += scan.top_field_first;
            rff += scan.repeat_first_field;
            ext += scan.extensions;
        }
        pos += (size_t)e;
    }
    printf("%s: frame_rate_code %d (%s Hz), progressive_sequence %d%s\n", name, seqRate,
           rate_name((unsigned)seqRate), seqProg, seqMixed ? " (changes within the stream)" : "");
    printf("%s: %ld picture units (coded order): %ld frame pictures, %ld field pictures, "
           "%ld with a picture coding extension, progressive_frame 1 on %ld, top_field_first 1 "
           "on %ld, repeat_first_field 1 on %ld\n",
           name, units, frames, fields, ext, pf, tff, rff);
    printf("%s: first %d in coded order: type %s\n", name, CADENCE, sType);
    printf("%s:                          tff  %s\n", name, sTff);
    printf("%s:                          rff  %s\n", name, sRff);
    printf("%s:                          pf   %s\n", name, sPf);

    /* the first pictures decoded and measured */
    d = ico_m2v_create();
    CHECK(d != NULL, "%s: decoder", name);
    pos = 0;
    while (d != NULL && pos < es.n && decoded < want) {
        long s0 = ico_pss_es_find_start(es.p + pos, es.n - pos), e;
        IcoM2vFrame fr;
        int r;
        if (s0 < 0) {
            break;
        }
        pos += (size_t)s0;
        if (es.p[pos + 3] == 0xB7) {
            pos += 4;
            continue;
        }
        e = ico_pss_es_unit_end(es.p + pos, es.n - pos);
        if (e < 0) {
            e = (long)(es.n - pos);
        }
        r = ico_m2v_decode(d, es.p + pos, (size_t)e, &fr);
        pos += (size_t)e;
        if (r != 1) {
            continue;
        }
        if (prev == NULL || w != fr.w || h != fr.h) {
            free(prev);
            w = fr.w;
            h = fr.h;
            prev = malloc((size_t)w * h);
            if (prev == NULL) {
                break;
            }
            if (decoded == 0) {
                printf("%s: decoded %u x %u\n", name, w, h);
            } else {
                printf("%s: size changes to %u x %u at picture %ld\n", name, w, h, decoded);
            }
            for (uint32_t y = 0; y < h; y++) {
                memcpy(prev + (size_t)y * w, fr.y + (size_t)y * fr.pitch[0], w);
            }
        }
        {
            /* the current luma packed at pitch w beside the previous one */
            static uint8_t cur[ICO_M2V_MAX_W * ICO_M2V_MAX_H];
            double d1, d2, ratio, c, tp, pt, mot;

            for (uint32_t y = 0; y < h; y++) {
                memcpy(cur + (size_t)y * w, fr.y + (size_t)y * fr.pitch[0], w);
            }
            line_diffs(cur, w, w, h, &d1, &d2);
            ratio = d2 > 0.0 ? d1 / d2 : 0.0;
            c = comb(cur, cur, w, w, h);
            tp = comb(cur, prev, w, w, h);
            pt = comb(prev, cur, w, w, h);
            mot = field_motion(cur, prev, w, w, h);
            printf("%s #%3ld %c(%c) st %c ps %u pf %u tff %u rff %u int %u  d1 %6.2f d2 %6.2f "
                   "d1/d2 %5.2f  comb %6.2f%%  TP %6.2f%%  PT %6.2f%%  mot %6.2f\n",
                   name, decoded, type_char(fr.scan.coding_type), type_char(fr.out_type),
                   struct_char(fr.scan.picture_structure), fr.scan.progressive_sequence,
                   fr.scan.progressive_frame, fr.scan.top_field_first, fr.scan.repeat_first_field,
                   fr.interlaced, d1, d2, ratio, c, tp, pt, mot);
            if (fr.out_type != 0 && fr.out_type != fr.scan.coding_type) {
                mismatches++;
            }
            interlaced += fr.interlaced;
            combed += c > 1.0;
            sumRatio += ratio;
            sumComb += c;
            memcpy(prev, cur, (size_t)w * h);
        }
        decoded++;
    }
    printf("%s: SUMMARY %u x %u, frame_rate_code %d, progressive_sequence %d; %ld of %ld units "
           "progressive_frame, %ld rff; first %ld decoded: %ld flagged interlaced, %ld combed "
           "(comb > 1%%), mean d1/d2 %.2f, mean comb %.2f%%, %ld display-order type mismatches, "
           "%u decoder errors\n",
           name, w, h, seqRate, seqProg, pf, units, rff, decoded, interlaced, combed,
           decoded ? sumRatio / (double)decoded : 0.0, decoded ? sumComb / (double)decoded : 0.0,
           mismatches, ico_m2v_errors(d));
    CHECK(decoded > 0, "%s: nothing decoded", name);
    CHECK(ico_m2v_errors(d) == 0, "%s: %u decoder errors", name, ico_m2v_errors(d));
    CHECK(mismatches == 0, "%s: %ld pictures whose scanned type is not the decoder's", name,
          mismatches);
    ico_m2v_destroy(d);
    free(prev);
    free(es.p);
}

/* --- field matching on a synthetic film -------------------------------------- */

#define SW 64
#define SH 48

/* film frame t: a bright vertical bar 8 wide at x = 8 + 6 t on grey */
static void film_frame(uint8_t *p, int t)
{
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            p[y * SW + x] = (uint8_t)(x >= 8 + 6 * t && x < 16 + 6 * t ? 220 : 60);
        }
    }
}

/* rows of one parity from a, the other from b */
static void weave(uint8_t *dst, const uint8_t *a, const uint8_t *b, int odd_from_b)
{
    for (int y = 0; y < SH; y++) {
        const uint8_t *src = ((y & 1) == odd_from_b) ? b : a;
        memcpy(dst + y * SW, src + y * SW, SW);
    }
}

static void test_match(void)
{
    static uint8_t f0[SW * SH], f1[SW * SH], f2[SW * SH], mixed[SW * SH];

    film_frame(f0, 0);
    film_frame(f1, 1);
    film_frame(f2, 2);
    /* even rows (the top field) from frame 1, odd rows from frame 0 */
    weave(mixed, f1, f0, 1);
    CHECK(ico_field_comb(mixed, mixed, SW, SW, SH, 0) > 0, "match: the mixed picture combs");
    CHECK(ico_field_comb(f1, f1, SW, SW, SH, 0) == 0, "match: a film frame does not comb");
    /* keeping the top field (frame 1): the next picture, frame 1 whole,
       completes it */
    CHECK(ico_field_match(f0, mixed, f1, SW, SW, SH, 0) == ICO_FIELD_MATCH_NEXT,
          "match: top field of frame 1 + next picture's bottom field (got %d)",
          ico_field_match(f0, mixed, f1, SW, SW, SH, 0));
    /* keeping the bottom field (frame 0): the previous picture completes it */
    CHECK(ico_field_match(f0, mixed, f2, SW, SW, SH, 1) == ICO_FIELD_MATCH_PREV,
          "match: bottom field of frame 0 + previous picture's top field (got %d)",
          ico_field_match(f0, mixed, f2, SW, SW, SH, 1));
    CHECK(ico_field_match(f0, f1, f2, SW, SW, SH, 0) == ICO_FIELD_MATCH_CUR,
          "match: a whole frame is kept as decoded");
    CHECK(ico_field_match(NULL, mixed, NULL, SW, SW, SH, 0) == ICO_FIELD_MATCH_CUR,
          "match: nothing to match with");
    printf("match: %s\n", failures == 0 ? "ok" : "FAILED");
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    long want = argc > 2 ? strtol(argv[2], NULL, 10) : 120;
    uint8_t hdr[4];
    uint32_t count, i, movies = 0;

    struct {
        char name[33];
        uint32_t lsn, size;
    } list[MAX_MOVIES];

    test_match();
    if (f == NULL) {
        printf("SKIP fmv_fields_test: no disc image\n");
        return failures != 0 ? 1 : 77;
    }
    if (want <= 0) {
        want = 120;
    }
    fseek(f, (long)DF_LSN * 2048, SEEK_SET);
    if (fread(hdr, 1, 4, f) != 4 || (count = le32(hdr)) == 0 || count > 4096) {
        printf("SKIP fmv_fields_test: no DATA.DF directory at sector %u (not the PAL disc)\n",
               DF_LSN);
        fclose(f);
        return 77;
    }
    for (i = 0; i < count; i++) {
        uint8_t e[40];
        size_t k;
        if (fread(e, 1, 40, f) != 40) {
            break;
        }
        e[31] = 0;
        k = strlen((const char *)e);
        if (k > 4 && (strcmp((const char *)e + k - 4, ".pss") == 0 ||
                      strcmp((const char *)e + k - 4, ".PSS") == 0)) {
            printf("DATA.DF: %s, %u bytes\n", (const char *)e, le32(e + 36));
            if (movies < MAX_MOVIES) {
                memcpy(list[movies].name, e, k + 1);
                list[movies].lsn = DF_LSN + le32(e + 32) / 2048;
                list[movies].size = le32(e + 36);
                movies++;
            }
        }
    }
    if (movies == 0) {
        printf("SKIP fmv_fields_test: no .pss in DATA.DF (not the PAL disc)\n");
        fclose(f);
        return 77;
    }
    for (i = 0; i < movies; i++) {
        movie(f, list[i].name, list[i].lsn, list[i].size, want);
    }
    fclose(f);
    if (failures != 0) {
        printf("fmv_fields_test: %d failures\n", failures);
        return 1;
    }
    printf("fmv_fields_test: done\n");
    return 0;
}
