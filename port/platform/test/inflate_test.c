/*
 * port/platform/test/inflate_test.c
 *
 * Issue 151: the game's inflate (fumi/ios/inflate.c) over its allocator
 * (fumi/ios/memory.c) and its code-table blocks (fumi/ios/mblock.c), all
 * three compiled as the game compiles them, on the host's EE RAM arena.
 *
 * A deflate block of the fixed code is decoded with tables inflate_fixed
 * allocates with iosMallocDebug and keeps until close_inflate_handler frees
 * them by walking each table's link entry.  The walk spelled the EE's 8-byte
 * struct huft (p -= 8, the link at p + 4); on the host the huft is 16 bytes,
 * so the close freed a pointer 8 bytes into the block and IOSFREE()'s magic
 * check stopped the game (the Asian disc's STGST13C.DF, stage 4, has one
 * fixed-code block among 295).
 *
 *   inflate_test synth
 *       a stream of fixed-code blocks, and one of dynamic blocks, an empty
 *       stored block and fixed-code blocks (deflated by miniz's tdefl with
 *       TDEFL_FORCE_ALL_STATIC_BLOCKS and TDEFL_FULL_FLUSH; each part's first
 *       block type checked): the game's inflate gives the text back byte for byte
 *       and the close frees every table without an assert (the fixed-only
 *       stream leaves the partition with 0 bytes in use); then a pointer 8
 *       bytes into a block handed to iosFree asserts with a message that
 *       names the block's allocation ("8 bytes into a block of ... allocated
 *       by owner/t.c line 151").
 *   inflate_test disc IMAGE PACK BYTES MEMBERS
 *       the disc's DATA.DF entry PACK through the game's inflate to its end:
 *       BYTES inflated, a header counting MEMBERS, a close without an
 *       assert.  Exits 77 (skipped) when the image cannot be opened.
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inflate.h"
#include "ios.h"
#include "memory.h"

#include "../arena.h"
#include "df_pack.h"
#include "miniz.h"
#include "vfs.h"

#define SKIP 77

static int fails;
static int asserts;
static char lastAssert[1024];
static jmp_buf *assertJump;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* inflate.c's partitions (fumi/ios/ios.c's in the game) */
struct IosMemPart *ios_partition_oomori;
struct IosMemPart *ios_partition_inflate;

/* The debug layer memory.c calls (common/src/debug*.c in the game).  An
   assert is recorded and, while a check expects one, left by longjmp before
   memory.c's __assert aborts. */
void debug_StdPrintfDummy(const char *fmt, ...);
void debug_assertMessage(const char *file, int line, const char *mes);
void debug_assert(const char *file, int line);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("debug_assertMessage %s:%d: %s\n", file, line, mes);
    snprintf(lastAssert, sizeof(lastAssert), "%s", mes);
    asserts++;
    if (assertJump != NULL) {
        longjmp(*assertJump, 1);
    }
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    asserts++;
    if (assertJump != NULL) {
        longjmp(*assertJump, 1);
    }
}

/* the streams' text: lines "line NNNN: the fixed-code tables\n", a..b-1 */
static size_t text(int a, int b, char *out)
{
    size_t n = 0;
    int i;
    for (i = a; i < b; i++) {
        n += (size_t)sprintf(out + n, "line %04d: the fixed-code tables\n", i);
    }
    return n;
}

/* n bytes of in deflated raw (no zlib header) by miniz's tdefl into out:
   TDEFL_FORCE_ALL_STATIC_BLOCKS for fixed-code blocks, TDEFL_FULL_FLUSH for
   a stream that goes on (an empty stored block ends it on a byte), or
   TDEFL_FINISH; the bytes written, 0 on an error */
static size_t deflateRaw(const char *in, size_t n, unsigned char *out, size_t cap, int flags,
                         tdefl_flush flush)
{
    tdefl_compressor *d = malloc(sizeof(*d));
    size_t inN = n;
    size_t outN = cap;
    tdefl_status st;

    if (d == NULL) {
        return 0;
    }
    tdefl_init(d, NULL, NULL, TDEFL_DEFAULT_MAX_PROBES | flags);
    st = tdefl_compress(d, in, &inN, out, &outN, flush);
    free(d);
    if (st < TDEFL_STATUS_OKAY || inN != n || outN == cap) {
        return 0;
    }
    return outN;
}

/* the type of the deflate block whose header starts byte p's bit 0 (a
   stream's first block, or one after a full flush): 0 stored, 1 fixed code,
   2 dynamic code */
static int blockType(const unsigned char *p)
{
    return (p[0] >> 1) & 3;
}

/* the read callback inflate.c's fill_inbuf calls: a buffer's bytes in turn,
   or a DATA.DF entry's */
typedef struct Source {
    const unsigned char *data; /* NULL: the disc entry */
    IcoVfs *vfs;
    const char *name;
    long long size;
    long long pos;
} Source;

static long long sourceRead(void *buf, long long size, void *handle)
{
    Source *s = handle;
    long long n = s->size - s->pos;
    if (n > size) {
        n = size;
    }
    if (n <= 0) {
        return 0;
    }
    if (s->data != NULL) {
        memcpy(buf, s->data + s->pos, (size_t)n);
    } else if (ico_df_read(s->vfs, s->name, (uint64_t)s->pos, buf, (size_t)n) != n) {
        return -1;
    }
    s->pos += n;
    return n;
}

/* the game's inflate over src into out (cap bytes), as cdvd.c's
   iosCdvdHandlerReadInflate loops, then the close; the bytes inflated, or
   -1 when an assert stopped the inflate or the close */
static long long inflateAll(Source *src, unsigned char *out, long long cap)
{
    jmp_buf jump;
    InflateHandler *volatile h = NULL;
    volatile long long total = 0;
    long long len;

    assertJump = &jump;
    if (setjmp(jump) != 0) {
        assertJump = NULL;
        return -1;
    }
    h = open_inflate_handler(sourceRead, src);
    while (total < cap && (len = inflate(h, out + total, cap - total)) > 0) {
        total += len;
    }
    close_inflate_handler(h);
    assertJump = NULL;
    return total;
}

static IosMemPart *makePartition(void)
{
    static IosMemPart *root;
    if (root == NULL) {
        CHECK(ico_arena_init() == 0);
        root = iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
        CHECK(root != NULL);
        /* the game's oomori partition's size (fumi/ios/ios.c) */
        ios_partition_oomori = iosMallocSetPartition(root, 4227072, 16);
        CHECK(ios_partition_oomori != NULL);
    }
    return ios_partition_oomori;
}

static int runSynth(void)
{
    static char want[192 * 40];
    static unsigned char got[192 * 40];
    static unsigned char fixedOnly[192 * 40];
    static unsigned char dynamicThenFixed[192 * 40];
    size_t fixedLen;
    size_t firstLen;
    size_t secondLen;
    size_t half;
    Source src;
    size_t n;
    long long total;
    IosMemPart *part = makePartition();
    char *block;
    jmp_buf jump;

    /* the streams: text(0, 64) in fixed-code blocks; text(0, 96) in dynamic
       ones and a full flush, then text(96, 192) in fixed-code blocks */
    n = text(0, 64, want);
    fixedLen = deflateRaw(want, n, fixedOnly, sizeof(fixedOnly), TDEFL_FORCE_ALL_STATIC_BLOCKS,
                          TDEFL_FINISH);
    CHECK(fixedLen != 0 && blockType(fixedOnly) == 1);
    half = text(0, 96, want);
    n = text(0, 192, want);
    firstLen =
        deflateRaw(want, half, dynamicThenFixed, sizeof(dynamicThenFixed), 0, TDEFL_FULL_FLUSH);
    CHECK(firstLen != 0 && blockType(dynamicThenFixed) == 2);
    secondLen = 0;
    if (firstLen != 0) {
        secondLen = deflateRaw(want + half, n - half, dynamicThenFixed + firstLen,
                               sizeof(dynamicThenFixed) - firstLen, TDEFL_FORCE_ALL_STATIC_BLOCKS,
                               TDEFL_FINISH);
    }
    CHECK(secondLen != 0 && blockType(dynamicThenFixed + firstLen) == 1);
    if (fixedLen == 0 || firstLen == 0 || secondLen == 0) {
        return 1;
    }

    /* one fixed-code block: the tables are the partition's only blocks
       besides the handle, and the close frees all of them */
    n = text(0, 64, want);
    memset(&src, 0, sizeof(src));
    src.data = fixedOnly;
    src.size = (long long)fixedLen;
    asserts = 0;
    total = inflateAll(&src, got, (long long)sizeof(got));
    CHECK(total == (long long)n && memcmp(got, want, n) == 0);
    CHECK(asserts == 0);
    CHECK(ico_heap_stats_used(part) == 0);
    printf("fixed-code block: %lld bytes inflated, %u bytes left in the partition, %d asserts\n",
           total, ico_heap_stats_used(part), asserts);

    /* a dynamic block's tables (the mblock's) beside the fixed code's */
    n = text(0, 192, want);
    memset(&src, 0, sizeof(src));
    src.data = dynamicThenFixed;
    src.size = (long long)(firstLen + secondLen);
    asserts = 0;
    total = inflateAll(&src, got, (long long)sizeof(got));
    CHECK(total == (long long)n && memcmp(got, want, n) == 0);
    CHECK(asserts == 0);
    printf("dynamic, stored and fixed-code blocks: %lld bytes inflated, %d asserts\n", total,
           asserts);

    /* a pointer 8 bytes into a block (what the EE's walk handed iosFree on
       the host): the assert names the block's allocation */
    block = iosMallocDebug(part, 528, "owner/t.c", 151);
    CHECK(block != NULL);
    asserts = 0;
    lastAssert[0] = '\0';
    assertJump = &jump;
    if (setjmp(jump) == 0) {
        iosFree(block + 8);
    }
    assertJump = NULL;
    CHECK(asserts == 1);
    CHECK(strstr(lastAssert, "IOSFREE():") != NULL);
    CHECK(strstr(
              lastAssert,
              "the pointer is 8 bytes into a block of 528 bytes allocated by owner/t.c line 151") !=
          NULL);
    iosFree(block);
    return 0;
}

static int runDisc(const char *image, const char *pack, long long bytes, int members)
{
    FILE *probe = image != NULL && image[0] != '\0' ? fopen(image, "rb") : NULL;
    IcoVfs *vfs;
    Source src;
    unsigned char *out;
    long long total;
    int64_t size;
    unsigned int first;

    if (probe == NULL) {
        printf("inflate_test: SKIP (no disc image%s%s)\n", image ? " at " : "", image ? image : "");
        return SKIP;
    }
    fclose(probe);
    vfs = ico_vfs_mount(&ico_vfs_iso9660, image);
    if (vfs == NULL) {
        printf("inflate_test: SKIP (%s is not a readable disc image)\n", image);
        return SKIP;
    }
    size = ico_df_size(vfs, pack);
    CHECK(size > 0);
    out = malloc((size_t)bytes + 1);
    CHECK(out != NULL);
    if (size <= 0 || out == NULL) {
        ico_vfs_unmount(vfs);
        free(out);
        return 1;
    }
    makePartition();
    memset(&src, 0, sizeof(src));
    src.vfs = vfs;
    src.name = pack;
    src.size = size;
    asserts = 0;
    /* one byte more than the pack holds: the inflate must end on its own */
    total = inflateAll(&src, out, bytes + 1);
    memcpy(&first, out, 4);
    printf("%s: %lld bytes on the disc, %lld inflated, %u members, %d asserts\n", pack,
           (long long)size, total, first, asserts);
    CHECK(total == bytes);
    CHECK(total < 4 || first == (unsigned int)members);
    CHECK(asserts == 0);
    free(out);
    ico_vfs_unmount(vfs);
    return 0;
}

int main(int argc, char **argv)
{
    int r;

    if (argc >= 2 && strcmp(argv[1], "synth") == 0) {
        r = runSynth();
    } else if (argc >= 6 && strcmp(argv[1], "disc") == 0) {
        r = runDisc(argv[2], argv[3], atoll(argv[4]), atoi(argv[5]));
    } else {
        printf("usage: inflate_test synth | disc IMAGE PACK BYTES MEMBERS\n");
        return 2;
    }
    if (r == SKIP) {
        return SKIP;
    }
    if (fails != 0 || r != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
