#include "inflate.h"
#include "debug.h"
#include "mblock.h"
#include "memory.h"
#include <string.h>
#include "ios.h"

static inline int huft_free(char *p);

struct huft {
    unsigned char e; /* 0x0 number of extra bits or operation */
    unsigned char b; /* 0x1 number of bits in this code or subcode */

    union {
        unsigned short n; /* 0x4 literal, length base or distance base */
        struct huft *t;   /* 0x4 pointer to the next level of table */
    } v;
};

/* The inflate handle: open_inflate_handler allocates one, the decoder state
   lives after the window and the input buffer. */
struct InflateHandler {         /* field names derived */
    void *handle;               /* 0x00000 what the read callback is given */
    InflateReadFn read;         /* 0x00004 the read callback */
    unsigned char slide[65536]; /* 0x00008 sliding window */
    unsigned char inbuf[32768]; /* 0x10008 compressed input */
    char pad18008[64];
    int wp;              /* 0x18048 window position */
    unsigned int insize; /* 0x1804C bytes in inbuf */
    unsigned int inptr;  /* 0x18050 next byte of inbuf */
    struct huft *tl;     /* 0x18054 */
    struct huft *td;     /* 0x18058 */
    int bl;              /* 0x1805C */
    int bd;              /* 0x18060 */
    char pad18064[4];
    unsigned long long bb; /* 0x18068 bit buffer */
    unsigned long long bk; /* 0x18070 bits in buffer */
    int t;                 /* 0x18078 block type, -1 = need a new header */
    int last;              /* 0x1807C final block seen */
    int n;                 /* 0x18080 bytes left in the current block */
    int copy_src;          /* 0x18084 window read position of a running copy */
    struct huft *w_tl;     /* 0x18088 also the "a huffman block is in progress" flag */
    struct huft *w_td;     /* 0x1808C */
    int w_bl;              /* 0x18090 */
    int w_bd;              /* 0x18094 */
    MBlock mb;             /* 0x18098 the code tables' blocks */
    char pad180A0[8];
};

static int fill_inbuf(InflateHandler *h);

#define NEXTBYTE(w) /* derived name */ (w->inptr < w->insize ? w->inbuf[w->inptr++] : fill_inbuf(w))
#define NEEDBITS(w, j) /* derived name */                                                          \
    {                                                                                              \
        while (k < (j)) {                                                                          \
            b |= (unsigned long long)NEXTBYTE(w) << k;                                             \
            k += 8;                                                                                \
        }                                                                                          \
    }
#define MASKBITS(j) (((unsigned long long)1 << (j)) - 1) /* derived name */
#define DUMPBITS(j)                                      /* derived name */                        \
    {                                                                                              \
        b >>= (j);                                                                                 \
        k -= (j);                                                                                  \
    }

/* The deflate code tables: copy length and extra-bit count per literal code
   257..285, then copy offset and extra-bit count per distance code 0..29.
   99 marks an invalid code. */
static unsigned short cplens[31] = {3,  4,   5,   6,   7,   8,   9,   10, 11, 13, 15,
                                    17, 19,  23,  27,  31,  35,  43,  51, 59, 67, 83,
                                    99, 115, 131, 163, 195, 227, 258, 0,  0}; /* derived name */

static unsigned short cplext[31] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,  2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99}; /* derived name */

static unsigned short cpdist[30] = {1,    2,    3,    4,     5,     7,    9,    13,
                                    17,   25,   33,   49,    65,    97,   129,  193,
                                    257,  385,  513,  769,   1025,  1537, 2049, 3073,
                                    4097, 6145, 8193, 12289, 16385, 24577}; /* derived name */

static unsigned short cpdext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13}; /* derived name */

#define BMAX 16   /* derived name */
#define N_MAX 288 /* derived name */

static int huft_build(unsigned int *b, unsigned int n, unsigned int s, unsigned short *d,
                      unsigned short *e, struct huft **t, int *m, MBlock *mb)
{
    unsigned int a;           /* counter for codes of length k */
    unsigned int c[BMAX + 1]; /* bit length count table */
    unsigned int el;          /* length of EOB code (value 256) */
    unsigned int f;           /* i repeats in table every f entries */
    int g;                    /* maximum code length */
    int h;                    /* table level */
    register unsigned int i;  /* counter, current code */
    register unsigned int j;  /* counter */
    register int k;           /* number of bits in current code */
    int lx[BMAX + 1];         /* memory for l[-1..BMAX-1] */
    int *l = lx + 1;          /* stack of bits per table */
    register unsigned int *p; /* pointer into c[], b[], or v[] */
    register struct huft *q;  /* points to current table */
    struct huft r;            /* table entry for structure assignment */
    struct huft *u[BMAX];     /* table stack */
    unsigned int v[N_MAX];    /* values in order of bit length */
    register int w;           /* bits before this table == (l * h) */
    unsigned int x[BMAX + 1]; /* bit offsets, then code stack */
    unsigned int *xp;         /* pointer into x */
    int y;                    /* number of dummy codes added */
    unsigned int z;           /* number of entries in current table */

    /* Generate counts for each bit length */
    el = n > 256 ? b[256] : BMAX; /* set length of EOB code, if any */
    memset((char *)c, 0, sizeof(c));
    p = b;
    i = n;
    do {
        c[*p]++; /* assume all entries <= BMAX */
        p++;
    } while (--i);
    if (c[0] == n) { /* null input, all zero length codes */
        *t = (struct huft *)0;
        *m = 0;
        return 0;
    }

    /* Find minimum and maximum length, bound *m by those */
    for (j = 1; j <= BMAX; j++)
        if (c[j])
            break;
    k = j; /* minimum code length */
    if ((unsigned int)*m < j)
        *m = j;
    for (i = BMAX; i; i--)
        if (c[i])
            break;
    g = i; /* maximum code length */
    if ((unsigned int)*m > i)
        *m = i;

    /* Adjust last length count to fill out codes, if needed */
    for (y = 1 << j; j < i; j++, y <<= 1)
        if ((y -= c[j]) < 0)
            return 2; /* bad input: more codes than bits */
    if ((y -= c[i]) < 0)
        return 2;
    c[i] += y;

    /* Generate starting offsets into the value table for each length */
    x[1] = j = 0;
    p = c + 1;
    xp = x + 2;
    while (--i) { /* note that i == g from above */
        *xp++ = (j += *p++);
    }

    /* Make a table of values in order of bit lengths */
    memset((char *)v, 0, sizeof(v));
    p = b;
    i = 0;
    do {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);
    n = x[g]; /* set n to length of v */

    /* Generate the Huffman codes and for each, make the table entries */
    x[0] = i = 0;            /* first Huffman code is zero */
    p = v;                   /* grab values in bit order */
    h = -1;                  /* no tables yet, level -1 */
    w = l[-1] = 0;           /* no bits decoded yet */
    u[0] = (struct huft *)0; /* just to keep compilers happy */
    q = (struct huft *)0;    /* ditto */
    z = 0;                   /* ditto */

    /* go through the bit lengths (k already is bits in shortest code) */
    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            /* here i is the Huffman code of length k bits for value *p */
            /* make tables up to required level */
            while (k > w + l[h]) {
                w += l[h++]; /* add bits already decoded */

                /* compute minimum size table less than or equal to *m bits */
                z = (z = g - w) > (unsigned int)*m ? (unsigned int)*m : z;
                if ((f = 1 << (j = k - w)) > a + 1) { /* try a k-w bit table */
                    f -= a + 1;                       /* deduct codes from patterns left */
                    xp = c + k;
                    while (++j < z) { /* try smaller tables up to z bits */
                        if ((f <<= 1) <= *++xp)
                            break; /* enough codes to use up j bits */
                        f -= *xp;  /* else deduct codes from patterns */
                    }
                }
                if (w + j > el && w < el) /* here j == the table size in bits */
                    j = el - w;           /* make EOB code end at table */
                z = 1 << j;               /* table entries for j-bit table */
                l[h] = j;                 /* set table size in stack */

                /* allocate and link in new table */
                if (mb == 0)
                    q = (struct huft *)iosMallocDebug(ios_partition_inflate,
                                                      (z + 1) * sizeof(struct huft), __FILE__, 241);
                else
                    q = (struct huft *)new_segment(mb, (z + 1) * sizeof(struct huft));
                if (q == (struct huft *)0) {
                    if (h && mb == 0)
                        huft_free((char *)u[0]);
                    return 3; /* not enough memory */
                }
                *t = q + 1; /* link to list for huft_free() */
                *(t = &(q->v.t)) = (struct huft *)0;
                u[h] = ++q; /* table starts after link */

                /* connect to last table, if there is one */
                if (h) {
                    x[h] = i;                      /* save pattern for backing up */
                    r.b = (unsigned char)l[h - 1]; /* bits to dump before this table */
                    r.e = (unsigned char)(16 + j); /* bits in this table */
                    r.v.t = q;                     /* pointer to this table */
                    j = (i & ((1 << w) - 1)) >> (w - l[h - 1]);
                    u[h - 1][j] = r; /* connect to last table */
                }
            }

            /* set up table entry in r */
            r.b = (unsigned char)(k - w);
            if (p >= v + n)
                r.e = 99; /* out of values, invalid code */
            else if (*p < s) {
                r.e = (unsigned char)(*p < 256 ? 16 : 15); /* 256 is end-of-block code */
                r.v.n = (unsigned short)*p;                /* simple code is just the value */
                p++;
            } else {
                r.e = (unsigned char)e[*p - s]; /* non-simple, look up in lists */
                r.v.n = d[*p++ - s];
            }

            /* fill code-like entries with r */
            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;

            /* backwards increment the k-bit code i */
            for (j = 1 << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;

            /* backup over finished tables */
            while ((i & ((1 << w) - 1)) != x[h]) {
                h--; /* don't need to update q */
                w -= l[h];
            }
        }
    }

    /* return actual size of base table */
    *m = l[0];

    /* Return true (1) if we were given an incomplete table */
    return y != 0 && g != 1;
}

/* huft_free is an `inline` function, inlined into inflate_fixed and
 * inflate_dynamic, with its out-of-line copy at the end of the file. */
static inline int huft_free(char *p)
{
    char *next;
    if (p == (char *)0)
        goto end;
    /* PC port: the table's link entry is the huft in front of it (huft_build:
       the block starts there, its v.t holds the next table), which the EE's
       spelling (p -= 8, the link at p + 4) only finds with its 8-byte struct
       huft; on the host the huft is 16 bytes and v.t is at 8 (issue 151) */
    p = (char *)((struct huft *)p - 1);
    while (1) {
        next = (char *)((struct huft *)p)->v.t;
        iosFree(p);
        p = next;
        if (p == (char *)0)
            break;
        p = (char *)((struct huft *)p - 1);
    }
end:
    return 0;
}

static long long inflate_codes(InflateHandler *w, unsigned char *out, long long outlen)
{
    unsigned int e;
    unsigned int n;
    unsigned int d;
    int wp;
    struct huft *t;
    struct huft *tl;
    struct huft *td;
    int bl;
    int bd;
    int nout;
    unsigned char *slide;
    unsigned long long b;
    unsigned long long k;

    b = w->bb;
    k = w->bk;
    if (outlen == 0)
        return 0;
    slide = w->slide;
    tl = w->w_tl;
    td = w->w_td;
    bl = w->w_bl;
    bd = w->w_bd;
    wp = w->wp;
    nout = 0;
    for (;;) {
        NEEDBITS(w, (unsigned int)bl)
        t = tl + (unsigned int)(b & MASKBITS(bl));
        e = t->e;
        if (e > 16) {
            do {
                if (e == 99)
                    return -1;
                DUMPBITS(t->b)
                e -= 16;
                NEEDBITS(w, e)
                t = t->v.t + (unsigned int)(b & MASKBITS(e));
                e = t->e;
            } while (e > 16);
        }
        DUMPBITS(t->b)
        if (e == 16) {
            wp &= 0x7fff;
            out[nout++] = slide[wp++] = (unsigned char)t->v.n;
            if (nout == outlen) {
                w->wp = wp;
                w->bb = b;
                w->bk = k;
                return outlen;
            }
        } else {
            if (e == 15)
                break;
            NEEDBITS(w, e)
            n = t->v.n + (unsigned int)(b & MASKBITS(e));
            DUMPBITS(e)
            NEEDBITS(w, (unsigned int)bd)
            t = td + (unsigned int)(b & MASKBITS(bd));
            e = t->e;
            if (e > 16) {
                do {
                    if (e == 99)
                        return -1;
                    DUMPBITS(t->b)
                    e -= 16;
                    NEEDBITS(w, e)
                    t = t->v.t + (unsigned int)(b & MASKBITS(e));
                    e = t->e;
                } while (e > 16);
            }
            DUMPBITS(t->b)
            NEEDBITS(w, e)
            d = wp - t->v.n - (unsigned int)(b & MASKBITS(e));
            DUMPBITS(e)
            while (n != 0 && nout < outlen) {
                n--;
                d &= 0x7fff;
                wp &= 0x7fff;
                out[nout++] = slide[wp++] = slide[d++];
            }
            if (nout == outlen) {
                w->n = n;
                w->wp = wp;
                w->copy_src = d;
                w->bb = b;
                w->bk = k;
                return outlen;
            }
        }
    }
    w->wp = wp;
    w->t = -1;
    w->bb = b;
    w->bk = k;
    return nout;
}

static long long inflate_stored(InflateHandler *w, unsigned char *out, long long outlen)
{
    unsigned int n;
    int wp;
    int cnt;
    unsigned int m;
    unsigned long long b;
    unsigned long long k;

    b = w->bb;
    k = w->bk;

    n = k & 7;
    DUMPBITS(n)

    NEEDBITS(w, 16)
    n = b & 0xffff;
    DUMPBITS(16)
    NEEDBITS(w, 16)
    m = ~b & 0xffff;
    if (n != m) {
        w->bb = b;
        w->bk = k;
        return -1;
    }
    DUMPBITS(16)

    w->n = n;

    n = 0;
    cnt = m;
    wp = w->wp;
    while (cnt != 0 && n < outlen) {
        cnt--;
        wp &= 0x7fff;
        NEEDBITS(w, 8)
        out[n++] = w->slide[wp++] = (unsigned char)(b & 0xff);
        DUMPBITS(8)
    }
    if (cnt == 0)
        w->t = -1;
    w->n = cnt;
    w->wp = wp;
    w->bb = b;
    w->bk = k;
    return n;
}

static long long inflate_fixed(InflateHandler *w, unsigned char *out, long long outlen)
{
    int i;
    unsigned int l[288];

    if (w->tl == (struct huft *)0) {
        for (i = 0; i < 144; i++)
            l[i] = 8;
        for (i = 144; i < 256; i++)
            l[i] = 9;
        for (; i < 280; i++)
            l[i] = 7;
        for (; i < 288; i++)
            l[i] = 8;
        w->bl = 7;
        if ((i = huft_build(l, 288, 257, cplens, cplext, &w->tl, &w->bl, (void *)0)) != 0) {
            w->tl = (struct huft *)0;
            return -1;
        }
        for (i = 0; i < 30; i++)
            l[i] = 5;
        w->bd = 5;
        if (huft_build(l, 30, 0, cpdist, cpdext, &w->td, &w->bd, (void *)0) > 1) {
            huft_free((char *)w->tl);
            w->tl = (struct huft *)0;
            return -1;
        }
    }
    w->w_tl = w->tl;
    w->w_td = w->td;
    w->w_bl = w->bl;
    w->w_bd = w->bd;
    return inflate_codes(w, out, outlen);
}

/* .data, the last object of inflate.o's run: the order the bit length code
   lengths arrive in. */
static int border[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                         11, 4,  12, 3, 13, 2, 14, 1, 15}; /* derived name */

static int inflate_dynamic(InflateHandler *w, unsigned char *out, long long outlen)
{
    int i;
    unsigned int j;
    unsigned int l;
    unsigned int n;
    struct huft *tl;
    struct huft *td;
    int bl;
    int bd;
    unsigned int nb;
    unsigned int nl;
    unsigned int nd;
    unsigned int ll[286 + 30];
    unsigned long long k;
    unsigned long long b;

    b = w->bb;
    k = w->bk;
    reuse_mblock(&w->mb);

    NEEDBITS(w, 5)
    nl = 257 + (b & 0x1f);
    DUMPBITS(5)
    NEEDBITS(w, 5)
    nd = 1 + (b & 0x1f);
    DUMPBITS(5)
    NEEDBITS(w, 4)
    nb = 4 + (b & 0xf);
    DUMPBITS(4)
    if (nl > 286 || nd > 30) {
        w->bb = b;
        w->bk = k;
        return -1;
    }

    for (j = 0; j < nb; j++) {
        NEEDBITS(w, 3)
        ll[border[j]] = (unsigned int)(b & 7);
        DUMPBITS(3)
    }
    for (; j < 19; j++)
        ll[border[j]] = 0;

    bl = 7;
    if ((i = huft_build(ll, 19, 19, (unsigned short *)0, (unsigned short *)0, &tl, &bl, &w->mb)) !=
        0) {
        reuse_mblock(&w->mb);
        w->bb = b;
        w->bk = k;
        return -1;
    }

    n = nl + nd;
    i = l = 0;
    while ((unsigned int)i < n) {
        NEEDBITS(w, (unsigned int)bl)
        j = (td = tl + (unsigned int)(b & MASKBITS(bl)))->b;
        DUMPBITS(j)
        j = td->v.n;
        if (j < 16) {
            ll[i++] = l = j;
        } else if (j == 16) {
            NEEDBITS(w, 2)
            j = 3 + (b & 3);
            DUMPBITS(2)
            if ((unsigned int)i + j > n) {
                w->bb = b;
                w->bk = k;
                return -1;
            }
            while (j--)
                ll[i++] = l;
        } else if (j == 17) {
            NEEDBITS(w, 3)
            j = 3 + (b & 7);
            DUMPBITS(3)
            if ((unsigned int)i + j > n) {
                w->bb = b;
                w->bk = k;
                return -1;
            }
            while (j--)
                ll[i++] = 0;
            l = 0;
        } else {
            NEEDBITS(w, 7)
            j = 11 + (b & 0x7f);
            DUMPBITS(7)
            if ((unsigned int)i + j > n) {
                w->bb = b;
                w->bk = k;
                return -1;
            }
            while (j--)
                ll[i++] = 0;
            l = 0;
        }
    }

    w->bb = b;
    w->bk = k;
    reuse_mblock(&w->mb);

    bl = 9;
    i = huft_build(ll, nl, 257, cplens, cplext, &tl, &bl, &w->mb);
    if (bl == 0)
        i = 1;
    if (i != 0) {
        if (i == 1)
            debug_StdPrintfDummy(" incomplete literal tree\n");
        reuse_mblock(&w->mb);
        return -1;
    }

    bd = 6;
    i = huft_build(ll + nl, nd, 0, cpdist, cpdext, &td, &bd, &w->mb);
    if (bd == 0 && nl > 257) {
        debug_StdPrintfDummy(" incomplete distance tree\n");
        reuse_mblock(&w->mb);
        return -1;
    }
    if (i == 1)
        debug_StdPrintfDummy(" incomplete distance tree\n");
    if (i != 0) {
        reuse_mblock(&w->mb);
        return -1;
    }

    w->w_tl = tl;
    w->w_td = td;
    w->w_bl = bl;
    w->w_bd = bd;
    i = inflate_codes(w, out, outlen);
    if (i == -1) {
        reuse_mblock(&w->mb);
        return -1;
    }
    return i;
}

static void inflate_start(InflateHandler *h)
{
    h->t = -1;
    h->wp = 0;
    h->bb = 0;
    h->bk = 0;
    h->inptr = 0;
    h->insize = 0;
    h->tl = 0;
    h->td = 0;
    h->last = 0;
    h->copy_src = 0;
    h->n = 0;
    h->w_tl = 0;
    init_mblock(&h->mb);
}

void close_inflate_handler(InflateHandler *h)
{
    char *p;
    if (h->tl != 0) {
        /* PC port: the fixed-code tables (inflate_fixed: the only ones
           iosMallocDebug gives, the dynamic ones are the mblock's), walked
           from each table's link entry as huft_free walks them: the EE's
           p -= 8 and p + 4 freed 8 bytes into the block on the host, which
           IOSFREE's magic check stopped (issue 151: the Asian disc's
           STGST13C.DF has one fixed-code block) */
        p = (char *)h->td;
        if (p != 0) {
            p = (char *)((struct huft *)p - 1);
            for (;;) {
                char *node = p;
                p = (char *)((struct huft *)node)->v.t;
                iosFree(node);
                if (p == 0)
                    break;
                p = (char *)((struct huft *)p - 1);
            }
        }
        p = (char *)h->tl;
        if (p != 0) {
            p = (char *)((struct huft *)p - 1);
            for (;;) {
                char *node = p;
                p = (char *)((struct huft *)node)->v.t;
                iosFree(node);
                if (p == 0)
                    break;
                p = (char *)((struct huft *)p - 1);
            }
        }
        h->tl = 0;
        h->td = 0;
    }
    reuse_mblock(&h->mb);
    iosFree(h);
    iosMallocResetPartition(ios_partition_inflate);
}

long long inflate(InflateHandler *w, unsigned char *out, long long outlen)
{
    long long total;
    long long ret;
    int cnt;
    int wp;
    int src;

    total = 0;
    while (total < outlen) {
        if (w->last != 0 && w->t == -1)
            return total;

        if (w->n != 0) {
            cnt = w->n;
            wp = w->wp;
            if (w->t != 0) {
                src = w->copy_src;
                while (cnt != 0 && total < outlen) {
                    cnt--;
                    src &= 0x7fff;
                    wp &= 0x7fff;
                    out[total++] = w->slide[wp++] = w->slide[src++];
                }
                w->copy_src = src;
            } else {
                unsigned long long b;
                unsigned long long k;

                b = w->bb;
                k = w->bk;
                while (cnt != 0 && total < outlen) {
                    cnt--;
                    wp &= 0x7fff;
                    NEEDBITS(w, 8)
                    out[total++] = w->slide[wp++] = (unsigned char)(b & 0xff);
                    DUMPBITS(8)
                }
                w->bb = b;
                w->bk = k;
                if (cnt == 0)
                    w->t = -1;
            }
            w->n = cnt;
            w->wp = wp;
            if (total == outlen)
                return total;
        }

        if (w->t == -1) {
            unsigned long long k;
            unsigned long long b;

            b = w->bb;
            k = w->bk;
            if (w->last != 0)
                return total;
            NEEDBITS(w, 1)
            if (b & 1)
                w->last = 1;
            DUMPBITS(1)
            NEEDBITS(w, 2)
            w->t = b & 3;
            DUMPBITS(2)
            w->w_tl = (struct huft *)0;
            w->n = 0;
            w->bb = b;
            w->bk = k;
        }

        switch (w->t) {
        case 0:
            ret = inflate_stored(w, out + total, outlen - total);
            break;
        case 1:
            if (w->w_tl != (struct huft *)0)
                ret = inflate_codes(w, out + total, outlen - total);
            else
                ret = inflate_fixed(w, out + total, outlen - total);
            break;
        case 2:
            if (w->w_tl != (struct huft *)0)
                ret = inflate_codes(w, out + total, outlen - total);
            else
                ret = inflate_dynamic(w, out + total, outlen - total);
            break;
        default:
            ret = -1;
            break;
        }

        if (ret == -1) {
            if (w->last != 0)
                return 0;
            return -1;
        }
        total += ret;
    }
    return total;
}

InflateHandler *open_inflate_handler(InflateReadFn read, void *handle)
{
    struct IosMemPart *g = ios_partition_oomori;
    InflateHandler *h;
    ios_partition_inflate = g;
    free_mblock_list = 0;
    h = iosMallocDebug(g, sizeof(InflateHandler), __FILE__, 739);
    inflate_start(h);
    h->handle = handle;
    if (read == 0) {
        debug_StdPrintfDummy("read func not entry\n");
    } else {
        h->read = read;
    }
    return h;
}

/* Refill the input buffer through the handler's read callback: as many reads
 * as it takes to fill the buffer or reach the end of the data, then the first
 * byte.  */
static int fill_inbuf(InflateHandler *h)
{
    int len;

    h->insize = 0;
    do {
        len = h->read(h->inbuf + h->insize, 32768 - h->insize, h->handle);
        if (len == 0 || len == -1)
            break;
        h->insize += len;
    } while (h->insize < 32768);
    if (h->insize == 0)
        return -1;
    h->inptr = 1;
    return h->inbuf[0];
}
