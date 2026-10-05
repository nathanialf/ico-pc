/*
 * port/data/extract.c
 *
 * The first-run extractor (extract.h): the user's disc image to `ico.o2r`
 * (archive.h). It mounts the image with the ISO9660 backend, walks the root
 * and DFDATAS directories, hashes the whole image, then streams each chosen
 * file into a ZIP written by miniz with no compression, hashing SCES_507.60
 * and taking the CRC-32 of DATA.DF's directory and members on the way, and
 * writes meta.json last, once the image has been accepted.
 */
#define _FILE_OFFSET_BITS 64

#include "extract.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "archive.h"
#include "host_config.h"
#include "miniz.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define DF_PATH "DFDATAS/DATA.DF"
#define ELF_PATH "SCES_507.60"
#define CNF_PATH "SYSTEM.CNF"
#define STORE_DIR "DFDATAS" /* stored whole, recursively */
#define ITEMS_MAX 1024
#define DEPTH_MAX 8
#define DIR_REC_MIN 34
#define DIR_BYTES_MAX (1u << 20)
#define HASH_CHUNK (1u << 20)
#define DF_MEMBERS_MAX 4096u /* a sanity bound; the PAL disc has 193 */

/* --- the DATA.DF manifest ------------------------------------------------------
 * The PAL disc's DATA.DF: its size, its directory's member count and the
 * CRC-32 (zlib) of the directory ({count, {name[32], offset, size} x count},
 * 4 + 40 * count bytes), then each member's offset, size and CRC-32 in
 * directory order. Hashes and structure only: no names and no bytes of the
 * file (docs/LEGAL.md). Regenerate with `archive_test manifest <iso>`, whose
 * output replaces the block between the BEGIN and END lines. */

typedef struct DfMember {
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
} DfMember;

/* BEGIN DATA.DF MANIFEST */
#define DF_SIZE 867184640u
#define DF_COUNT 193u
#define DF_DIR_CRC 0xfb82eea7u
static const DfMember dfMembers[] = {
    {8192u, 2365389u, 0xa7442fa6u},        {2373632u, 1256365u, 0xaaf763a3u},
    {3631104u, 4001634u, 0xe7b52d42u},     {7632896u, 51919u, 0x5f42c211u},
    {7686144u, 1928900u, 0x5cbd5400u},     {9615360u, 7487547u, 0x0280831au},
    {17104896u, 3010021u, 0x188be14cu},    {20115456u, 3403790u, 0x0691a3b8u},
    {23521280u, 4527299u, 0xfd10d6f2u},    {28049408u, 3539794u, 0x68f3d3e4u},
    {31590400u, 3786639u, 0x4d9de7b6u},    {35377152u, 3351457u, 0x6d4681b9u},
    {38729728u, 2385789u, 0xba823356u},    {41115648u, 4604019u, 0x915e5a24u},
    {45721600u, 2300650u, 0x06a090b3u},    {48023552u, 1861159u, 0xfc9f2615u},
    {49885184u, 1716793u, 0x95a7af30u},    {51603456u, 2363442u, 0x2dbf667cu},
    {53968896u, 4343953u, 0x58fe4e5du},    {58314752u, 2728365u, 0x26fbdb14u},
    {61044736u, 2789793u, 0xfacf318fu},    {63836160u, 3005291u, 0x4e457741u},
    {66842624u, 4908028u, 0xd081d8f3u},    {71751680u, 4146492u, 0x482524ccu},
    {75898880u, 3554485u, 0xbb873037u},    {79454208u, 770114u, 0x0c258f86u},
    {80226304u, 36699u, 0xaf8823bdu},      {80263168u, 3664632u, 0xea524198u},
    {83929088u, 3229179u, 0x61c42de7u},    {87158784u, 4074014u, 0xd6027944u},
    {91234304u, 4338131u, 0xddff44ecu},    {95574016u, 4908985u, 0x838e669bu},
    {100483072u, 3965297u, 0x71f1fd1cu},   {104450048u, 4342154u, 0x200842c0u},
    {108793856u, 4313146u, 0x893950b0u},   {113108992u, 4089014u, 0xf93f4330u},
    {117198848u, 3963119u, 0xf58f38d5u},   {121163776u, 4407795u, 0x289f146fu},
    {125573120u, 4003169u, 0x84e3572cu},   {129576960u, 4103202u, 0x73e43f03u},
    {133681152u, 4531007u, 0x8015ac33u},   {138213376u, 4597403u, 0xca9e701au},
    {142811136u, 5008097u, 0xa5f1d5b7u},   {147820544u, 4926587u, 0x873f1abau},
    {152748032u, 4610944u, 0x9184f9dfu},   {157360128u, 4686228u, 0x26102a8fu},
    {162048000u, 4591591u, 0x88055934u},   {166639616u, 3309224u, 0xc77f5ea9u},
    {169949184u, 3192736u, 0x7f5df611u},   {173142016u, 5037639u, 0xbd27a677u},
    {178180096u, 2855484u, 0xb20b56c7u},   {181037056u, 4384838u, 0x8aa36005u},
    {185423872u, 2818732u, 0xc3702263u},   {188243968u, 3796942u, 0xa472c950u},
    {192040960u, 4946719u, 0x97f86af3u},   {196988928u, 4074165u, 0xe73bbe82u},
    {201064448u, 4728586u, 0x26c09827u},   {205793280u, 4015760u, 0x0527ab61u},
    {209809408u, 3576896u, 0x7feb7869u},   {213387264u, 4291451u, 0x6acfc99cu},
    {217679872u, 4375432u, 0xbdf95004u},   {222056448u, 3408528u, 0x866a2a4eu},
    {225466368u, 3356859u, 0xf7f631c2u},   {228825088u, 4234068u, 0x67c873f8u},
    {233060352u, 3078221u, 0xafb0e023u},   {236140544u, 2786295u, 0x9bd81444u},
    {238927872u, 4528952u, 0x377874ceu},   {243458048u, 3177701u, 0x3eea6b4cu},
    {246636544u, 95624u, 0xcdb0a477u},     {246732800u, 1277952u, 0x0710d8feu},
    {248010752u, 4337664u, 0xec562dfcu},   {252348416u, 2375680u, 0xdb8bcedbu},
    {254724096u, 4538368u, 0x6e6c79b0u},   {259262464u, 1028096u, 0xa4020997u},
    {260290560u, 901120u, 0xa4223d4bu},    {261191680u, 901120u, 0x08660208u},
    {262092800u, 483328u, 0x9b3142f9u},    {262576128u, 1208320u, 0xe434981du},
    {263784448u, 3125248u, 0xfd8319cdu},   {266909696u, 921600u, 0x0ca4288au},
    {267831296u, 983040u, 0x52dbb45du},    {268814336u, 987136u, 0x82ffa512u},
    {269801472u, 1036288u, 0x24df6266u},   {270837760u, 1175552u, 0x51c61723u},
    {272013312u, 1724416u, 0x28241fd3u},   {273737728u, 1728512u, 0xcb802602u},
    {275466240u, 1269760u, 0x1a63ee1du},   {276736000u, 974848u, 0x104aa807u},
    {277710848u, 1007616u, 0x67b15bfdu},   {278718464u, 4206592u, 0x8de6edacu},
    {282925056u, 1437696u, 0x2beb372bu},   {284362752u, 905216u, 0xcfd8aee6u},
    {285267968u, 5005312u, 0x06a51956u},   {290273280u, 2277376u, 0x9260deb0u},
    {292550656u, 3289088u, 0x96839a46u},   {295839744u, 6283264u, 0x738fa681u},
    {302123008u, 3088384u, 0xd1eaa2f2u},   {305211392u, 1150976u, 0xadabf5d6u},
    {306362368u, 2539520u, 0xf3cce748u},   {308901888u, 13762560u, 0x68ab2d00u},
    {322664448u, 598016u, 0x10f5ecbeu},    {323262464u, 10067968u, 0xce38883fu},
    {333330432u, 1642496u, 0x23a7fef5u},   {334972928u, 847872u, 0x0e23ccdfu},
    {335820800u, 950272u, 0xfae89fd8u},    {336771072u, 3026944u, 0x8eee62adu},
    {339798016u, 1335296u, 0x401d91e6u},   {341133312u, 1355776u, 0xa4fef056u},
    {342489088u, 1200128u, 0xa2747422u},   {343689216u, 1040384u, 0xcc242a09u},
    {344729600u, 1310720u, 0x59256064u},   {346040320u, 757760u, 0xcc1d0d17u},
    {346798080u, 630784u, 0x32381d70u},    {347428864u, 802816u, 0xb0558cadu},
    {348231680u, 755712u, 0x8f8c0382u},    {348987392u, 831488u, 0x5c12a114u},
    {349818880u, 827392u, 0x18c61163u},    {350646272u, 847872u, 0xb0e04b4du},
    {351494144u, 1400832u, 0x4836d5c0u},   {352894976u, 1392640u, 0x2bede058u},
    {354287616u, 765952u, 0x42235eddu},    {355053568u, 1060864u, 0xabdcfc3au},
    {356114432u, 708608u, 0x34988651u},    {356823040u, 720896u, 0xa4791c6eu},
    {357543936u, 716800u, 0xaa4a7b6cu},    {358260736u, 872448u, 0x93c8dfdcu},
    {359133184u, 1171456u, 0xd8c6ce32u},   {360304640u, 913408u, 0xc721cf89u},
    {361218048u, 888832u, 0x33f47f19u},    {362106880u, 1232896u, 0x55da9176u},
    {363339776u, 942080u, 0xc38709c2u},    {364281856u, 655360u, 0x41704791u},
    {364937216u, 696320u, 0x3e551bdbu},    {365633536u, 712704u, 0x176e9058u},
    {366346240u, 630784u, 0xeb013728u},    {366977024u, 700416u, 0xb6a4a501u},
    {367677440u, 774144u, 0x3e071182u},    {368451584u, 798720u, 0x04c79688u},
    {369250304u, 786432u, 0xafbfa914u},    {370036736u, 827392u, 0x30cd6303u},
    {370864128u, 626688u, 0xc0ff345bu},    {371490816u, 634880u, 0xfd657b3au},
    {372125696u, 679936u, 0x1099d110u},    {372805632u, 2478080u, 0x70245be9u},
    {375283712u, 5013504u, 0xe8981923u},   {380297216u, 561152u, 0xad570c36u},
    {380858368u, 835584u, 0x04ed0debu},    {381693952u, 827392u, 0xd2cbdf1fu},
    {382521344u, 671744u, 0xc0b23345u},    {383193088u, 651264u, 0x6069ae9au},
    {383844352u, 4538368u, 0x071cc158u},   {388382720u, 454656u, 0xaa0f4915u},
    {388837376u, 503808u, 0xb852dda8u},    {389341184u, 471040u, 0x9080b7b4u},
    {389812224u, 458752u, 0x4f75e902u},    {390270976u, 130301956u, 0xd532ee7au},
    {520574976u, 130334724u, 0xc48f9011u}, {650911744u, 2815232u, 0x3aaa45e2u},
    {653727744u, 687232u, 0xa2e600cau},    {654415872u, 964u, 0xe5fa0c6bu},
    {654417920u, 2662400u, 0x34c81911u},   {657080320u, 3022848u, 0xfe2b76d4u},
    {660103168u, 3616768u, 0xacf3f715u},   {663719936u, 6213632u, 0x474ac58fu},
    {669933568u, 1789952u, 0x437d572au},   {671723520u, 4202496u, 0x17b056d2u},
    {675926016u, 1089536u, 0xe457c5a6u},   {677015552u, 876544u, 0x575cf3e7u},
    {677892096u, 1811680u, 0xf5e5792eu},   {679704576u, 3567616u, 0x848d1aefu},
    {683272192u, 2075320u, 0xe8882aa8u},   {685348864u, 2887680u, 0xc0396b7eu},
    {688236544u, 2688000u, 0x73e284f7u},   {690925568u, 4440064u, 0x00e7c349u},
    {695365632u, 725760u, 0x25bf4f45u},    {696092672u, 3567616u, 0x848d1aefu},
    {699660288u, 2961408u, 0x62da9193u},   {702621696u, 1467648u, 0x5ab03c76u},
    {704090112u, 4521984u, 0xc7f92f55u},   {708612096u, 2129792u, 0xb63d558eu},
    {710742016u, 4038656u, 0x5a030ffau},   {714780672u, 4038656u, 0x11a4d5d7u},
    {718819328u, 4038656u, 0xf94a9efau},   {722857984u, 4038656u, 0x19c7ed41u},
    {726896640u, 4038656u, 0x84d6f662u},   {730935296u, 4038656u, 0x0dad1b41u},
    {734973952u, 4038656u, 0xea152a5du},   {739012608u, 4038656u, 0x87c4355du},
    {743051264u, 4038656u, 0x480b6c1eu},   {747089920u, 4038656u, 0xed475180u},
    {751128576u, 6746112u, 0xa41b7661u},   {757874688u, 4907760u, 0xefaeee71u},
    {762783744u, 104398852u, 0x0b670315u},
};
/* END DATA.DF MANIFEST */

/* --- helpers ----------------------------------------------------------------- */

static void say(char *why, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (why == NULL || n == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(why, n, fmt, ap);
    va_end(ap);
}

static double now_s(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void sha1_hex(IcoSha1 *s, char hex[41])
{
    unsigned char d[20];
    int i;

    ico_sha1_final(s, d);
    for (i = 0; i < 20; i++) {
        snprintf(hex + 2 * i, 3, "%02x", d[i]);
    }
}

/* A growing text buffer for meta.json. */
typedef struct Buf {
    char *p;
    size_t n;
    size_t cap;
    int bad;
} Buf;

static void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int need;

    if (b->bad) {
        return;
    }
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        b->bad = 1;
        return;
    }
    if (b->n + (size_t)need + 1 > b->cap) {
        size_t cap = (b->cap ? b->cap * 2 : 4096) + (size_t)need;
        char *p = realloc(b->p, cap);

        if (p == NULL) {
            b->bad = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, (size_t)need + 1, fmt, ap);
    va_end(ap);
    b->n += (size_t)need;
}

/* a JSON string; disc names are ASCII, anything else is escaped */
static void buf_jstr(Buf *b, const char *s)
{
    buf_printf(b, "\"");
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\') {
            buf_printf(b, "\\%c", c);
        } else if (c < 0x20 || c >= 0x7F) {
            buf_printf(b, "\\u%04x", c);
        } else {
            buf_printf(b, "%c", c);
        }
    }
    buf_printf(b, "\"");
}

/* --- DATA.DF's directory and member CRCs ---------------------------------------- */

typedef struct DfState {
    int valid;     /* the directory parsed */
    uint32_t size; /* DATA.DF's size */
    uint32_t count;
    uint32_t dir_len; /* 4 + 40 * count */
    uint32_t dir_crc;
    DfMember *m;   /* offset and size from the image's directory, crc running */
    uint64_t next; /* the next byte expected (the CRCs need order) */
    int out_of_order;
} DfState;

static int df_begin(DfState *df, IcoVfs *iso, char *why, size_t whysize)
{
    IcoVfsFile f;
    unsigned char hdr[4];
    unsigned char *dir;
    uint32_t i;

    memset(df, 0, sizeof(*df));
    if (ico_vfs_open(iso, DF_PATH, &f) != 0) {
        say(why, whysize, "the image has no " DF_PATH);
        return -1;
    }
    df->size = f.entry.size;
    if (ico_vfs_read(&f, 0, hdr, 4) != 4) {
        say(why, whysize, DF_PATH ": cannot read its directory");
        return -1;
    }
    df->count = le32(hdr);
    if (df->count == 0 || df->count > DF_MEMBERS_MAX || 4u + 40u * (uint64_t)df->count > df->size) {
        say(why, whysize, DF_PATH ": not a member directory (count %u)", (unsigned)df->count);
        return -1;
    }
    df->dir_len = 4u + 40u * df->count;
    dir = malloc(df->dir_len);
    df->m = calloc(df->count, sizeof(*df->m));
    if (dir == NULL || df->m == NULL || ico_vfs_read(&f, 0, dir, df->dir_len) != df->dir_len) {
        free(dir);
        say(why, whysize, DF_PATH ": cannot read its directory");
        return -1;
    }
    for (i = 0; i < df->count; i++) {
        df->m[i].offset = le32(dir + 4 + 40 * i + 32);
        df->m[i].size = le32(dir + 4 + 40 * i + 36);
        if ((uint64_t)df->m[i].offset + df->m[i].size > df->size) {
            free(dir);
            say(why, whysize, DF_PATH ": member %u lies outside the file", (unsigned)i);
            return -1;
        }
        df->m[i].crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, NULL, 0);
    }
    free(dir);
    df->dir_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, NULL, 0);
    df->valid = 1;
    return 0;
}

static void df_update(DfState *df, uint64_t o, const unsigned char *p, size_t n)
{
    uint64_t end = o + n;
    uint32_t i;

    if (!df->valid) {
        return;
    }
    if (o != df->next) {
        df->out_of_order = 1;
        return;
    }
    df->next = end;
    if (o < df->dir_len) {
        uint64_t e = end < df->dir_len ? end : df->dir_len;

        df->dir_crc = (uint32_t)mz_crc32(df->dir_crc, p, (size_t)(e - o));
    }
    for (i = 0; i < df->count; i++) {
        uint64_t a = df->m[i].offset;
        uint64_t b = a + df->m[i].size;

        if (a < o) {
            a = o;
        }
        if (b > end) {
            b = end;
        }
        if (a < b) {
            df->m[i].crc = (uint32_t)mz_crc32(df->m[i].crc, p + (a - o), (size_t)(b - a));
        }
    }
}

/* Whether the image's DATA.DF is the manifest's. */
static int df_matches(const DfState *df, char *why, size_t whysize)
{
    uint32_t i;

    if (!df->valid) {
        say(why, whysize, "DATA.DF has no member directory");
        return 0;
    }
    if (df->out_of_order || df->next != df->size) {
        say(why, whysize, "DATA.DF was not read through in order");
        return 0;
    }
    if (DF_COUNT == 0) {
        say(why, whysize, "this build has no DATA.DF manifest");
        return 0;
    }
    if (df->size != DF_SIZE) {
        say(why, whysize, "DATA.DF is %u bytes, the PAL disc's is %u", (unsigned)df->size,
            (unsigned)DF_SIZE);
        return 0;
    }
    if (df->count != DF_COUNT) {
        say(why, whysize, "DATA.DF has %u members, the PAL disc's %u", (unsigned)df->count,
            (unsigned)DF_COUNT);
        return 0;
    }
    if (df->dir_crc != DF_DIR_CRC) {
        say(why, whysize, "DATA.DF's directory CRC-32 %08x, expected %08x", (unsigned)df->dir_crc,
            (unsigned)DF_DIR_CRC);
        return 0;
    }
    for (i = 0; i < df->count; i++) {
        const DfMember *w = &dfMembers[i];

        if (df->m[i].offset != w->offset || df->m[i].size != w->size || df->m[i].crc != w->crc) {
            say(why, whysize,
                "DATA.DF member %u: offset %u size %u CRC-32 %08x, expected %u %u %08x",
                (unsigned)i, (unsigned)df->m[i].offset, (unsigned)df->m[i].size,
                (unsigned)df->m[i].crc, (unsigned)w->offset, (unsigned)w->size, (unsigned)w->crc);
            return 0;
        }
    }
    return 1;
}

uint32_t ico_extract_datadf_manifest_count(void)
{
    return DF_COUNT;
}

int ico_extract_print_datadf_manifest(IcoVfs *iso, FILE *out)
{
    static unsigned char chunk[HASH_CHUNK];
    IcoVfsFile f;
    DfState df;
    char why[256];
    uint64_t o;
    uint32_t i;

    if (df_begin(&df, iso, why, sizeof(why)) != 0 || ico_vfs_open(iso, DF_PATH, &f) != 0) {
        fprintf(stderr, "manifest: %s\n", why);
        free(df.m);
        return -1;
    }
    for (o = 0; o < df.size;) {
        int64_t n = ico_vfs_read(&f, o, chunk, sizeof(chunk));

        if (n <= 0) {
            free(df.m);
            return -1;
        }
        df_update(&df, o, chunk, (size_t)n);
        o += (uint64_t)n;
    }
    fprintf(out,
            "/* BEGIN DATA.DF MANIFEST */\n"
            "#define DF_SIZE %uu\n#define DF_COUNT %uu\n#define DF_DIR_CRC 0x%08xu\n"
            "static const DfMember dfMembers[] = {\n",
            (unsigned)df.size, (unsigned)df.count, (unsigned)df.dir_crc);
    for (i = 0; i < df.count; i++) {
        fprintf(out, "    {%uu, %uu, 0x%08xu},\n", (unsigned)df.m[i].offset, (unsigned)df.m[i].size,
                (unsigned)df.m[i].crc);
    }
    fprintf(out, "};\n/* END DATA.DF MANIFEST */\n");
    free(df.m);
    return 0;
}

/* --- the walk ----------------------------------------------------------------- */

typedef struct Item {
    char path[ICO_VFS_PATH_MAX];
    IcoVfsEntry e;
    int store;
} Item;

typedef struct Ex {
    IcoVfs *iso;
    Item *items;
    int n;
    char boot[64]; /* BOOT2's file, normalized */

    IcoExtractProgressFn progress;
    void *ctx;
    uint64_t done;
    uint64_t total;
    int cancelled;

    FILE *out;
    uint64_t out_pos;
    int write_errno;

    /* the file being streamed */
    IcoVfsFile f;
    int is_elf;
    int is_df;
    uint64_t next;
    int read_err;
    IcoSha1 elf;
    int elf_seen;
    int elf_in_order;
    DfState df;
} Ex;

static int add_item(Ex *x, const char *path, int store, char *why, size_t whysize)
{
    Item *it;

    if (x->n >= ITEMS_MAX) {
        say(why, whysize, "more than %d files to extract", ITEMS_MAX);
        return -1;
    }
    it = &x->items[x->n];
    memset(it, 0, sizeof(*it));
    snprintf(it->path, sizeof(it->path), "%s", path);
    if (ico_vfs_stat(x->iso, path, &it->e) != 0) {
        say(why, whysize, "cannot look up %s on the image", path[0] ? path : "the root");
        return -1;
    }
    it->store = store && !it->e.is_dir;
    x->n++;
    return 0;
}

static int root_file_wanted(const Ex *x, const char *comp)
{
    size_t n = strlen(comp);

    /* DUMMY.TXT is the file after DATA.DF on the PAL disc (LSN 443201, where
       DATA.DF's last sector ends): kept so a read that runs over DATA.DF's
       end finds what the disc had there */
    return strcmp(comp, CNF_PATH) == 0 || strcmp(comp, ELF_PATH) == 0 ||
           strcmp(comp, "DUMMY.TXT") == 0 || (x->boot[0] != '\0' && strcmp(comp, x->boot) == 0) ||
           (n > 4 && strcmp(comp + n - 4, ".IRX") == 0);
}

static int walk(Ex *x, const char *dir, const IcoVfsEntry *d, int store_all, int depth, char *why,
                size_t whysize)
{
    unsigned char *buf;
    uint32_t nsec, s;
    int r = 0;

    if (depth > DEPTH_MAX || d->size > DIR_BYTES_MAX) {
        say(why, whysize, "the directory %s is too deep or too large", dir[0] ? dir : "/");
        return -1;
    }
    nsec = ico_vfs_size_to_sectors(d->size);
    buf = malloc((size_t)nsec * ICO_VFS_SECTOR);
    if (buf == NULL || ico_vfs_read_sectors(x->iso, d->lsn, nsec, buf) != 0) {
        free(buf);
        say(why, whysize, "cannot read the directory %s", dir[0] ? dir : "/");
        return -1;
    }
    for (s = 0; s < nsec && r == 0; s++) {
        unsigned o = 0;
        const unsigned char *sec = buf + (size_t)s * ICO_VFS_SECTOR;

        while (o + DIR_REC_MIN <= ICO_VFS_SECTOR && r == 0) {
            const unsigned char *rec = sec + o;
            unsigned rlen = rec[0];
            unsigned nlen;
            char comp[40];
            char path[ICO_VFS_PATH_MAX];
            unsigned i, k = 0;
            int is_dir;

            if (rlen == 0) {
                break;
            }
            nlen = rec[32];
            if (rlen < DIR_REC_MIN || o + rlen > ICO_VFS_SECTOR || 33u + nlen > rlen) {
                break;
            }
            o += rlen;
            if (nlen == 1 && rec[33] <= 1) {
                continue; /* "." and ".." */
            }
            for (i = 0; i < nlen && k + 1 < sizeof(comp) && rec[33 + i] != ';'; i++) {
                char c = (char)rec[33 + i];

                comp[k++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            }
            if (k > 0 && comp[k - 1] == '.') {
                k--;
            }
            comp[k] = '\0';
            if (k == 0) {
                continue;
            }
            snprintf(path, sizeof(path), "%s%s%s", dir, dir[0] ? "/" : "", comp);
            is_dir = (rec[25] & 2) != 0;
            if (is_dir && (store_all || (depth == 0 && strcmp(comp, STORE_DIR) == 0))) {
                IcoVfsEntry sub;

                r = add_item(x, path, 0, why, whysize);
                if (r == 0) {
                    sub = x->items[x->n - 1].e;
                    r = walk(x, path, &sub, 1, depth + 1, why, whysize);
                }
            } else if (!is_dir && (store_all || (depth == 0 && root_file_wanted(x, comp)))) {
                r = add_item(x, path, 1, why, whysize);
            }
        }
    }
    free(buf);
    return r;
}

/* SYSTEM.CNF's BOOT2 file, normalized ("SCES_507.60"); "" when absent. */
static void read_boot_name(Ex *x)
{
    char cnf[1024];
    IcoVfsFile f;
    int64_t n;
    char *p;

    x->boot[0] = '\0';
    if (ico_vfs_open(x->iso, CNF_PATH, &f) != 0) {
        return;
    }
    n = ico_vfs_read(&f, 0, cnf, sizeof(cnf) - 1);
    if (n <= 0) {
        return;
    }
    cnf[n] = '\0';
    p = strstr(cnf, "BOOT2");
    if (p == NULL || (p = strchr(p, '=')) == NULL) {
        return;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    p[strcspn(p, "\r\n")] = '\0';
    if (ico_vfs_normalize(p, x->boot, sizeof(x->boot)) != 0) {
        x->boot[0] = '\0';
    }
}

/* "SCES_507.60" -> "SCES-50760" */
static void disc_id_of(const char *boot, char *out, size_t size)
{
    size_t k = 0;

    for (; *boot != '\0' && k + 1 < size; boot++) {
        if (*boot == '_') {
            out[k++] = '-';
        } else if (*boot != '.') {
            out[k++] = *boot;
        }
    }
    out[k] = '\0';
}

/* --- the ZIP ------------------------------------------------------------------ */

static int tick(Ex *x, const char *phase, uint64_t n)
{
    x->done += n;
    if (x->progress != NULL && x->progress(x->ctx, phase, x->done, x->total) != 0) {
        x->cancelled = 1;
    }
    return x->cancelled;
}

static size_t zip_write(void *opaque, mz_uint64 ofs, const void *buf, size_t n)
{
    Ex *x = opaque;
    size_t w;

    if (ofs != x->out_pos) {
        if (ico_archive_seek(x->out, ofs) != 0) {
            x->write_errno = errno ? errno : EIO;
            return 0;
        }
        x->out_pos = ofs;
    }
    w = fwrite(buf, 1, n, x->out);
    x->out_pos += w;
    if (w != n) {
        x->write_errno = errno ? errno : ENOSPC;
    }
    return w;
}

static size_t zip_read_iso(void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
    Ex *x = opaque;
    int64_t got;

    if (x->cancelled) {
        return 0;
    }
    /* miniz asks for a full buffer each time and takes a short read as the
       end of the file and 0 as nothing more: store_item checks afterwards
       that the whole file went in */
    got = ico_vfs_read(&x->f, ofs, buf, n);
    if (got < 0) {
        x->read_err = 1;
        return 0;
    }
    if (got == 0) {
        return 0;
    }
    if (x->is_elf) {
        if (ofs != x->next) {
            x->elf_in_order = 0;
        }
        ico_sha1_update(&x->elf, buf, (size_t)got);
    }
    if (x->is_df) {
        df_update(&x->df, ofs, buf, (size_t)got);
    }
    x->next = ofs + (uint64_t)got;
    if (tick(x, "extract", (uint64_t)got)) {
        return 0;
    }
    return (size_t)got;
}

static int store_item(Ex *x, mz_zip_archive *zip, const Item *it, int *tail_out, char *why,
                      size_t whysize)
{
    char name[ICO_VFS_PATH_MAX + 8];
    unsigned char sec[ICO_VFS_SECTOR];
    uint32_t used = it->e.size % ICO_VFS_SECTOR;
    uint32_t i;

    *tail_out = 0;
    memset(&x->f, 0, sizeof(x->f));
    x->f.vfs = x->iso;
    x->f.entry = it->e;
    x->is_elf = strcmp(it->path, ELF_PATH) == 0;
    x->is_df = strcmp(it->path, DF_PATH) == 0;
    x->next = 0;
    x->read_err = 0;
    if (x->is_elf) {
        ico_sha1_init(&x->elf);
        x->elf_seen = 1;
        x->elf_in_order = 1;
    }
    snprintf(name, sizeof(name), "disc/%s", it->path);
    if (!mz_zip_writer_add_read_buf_callback(zip, name, zip_read_iso, x, it->e.size, NULL, NULL, 0,
                                             MZ_NO_COMPRESSION, NULL, 0, NULL, 0) ||
        x->cancelled || x->read_err || x->next != it->e.size) {
        if (x->cancelled) {
            say(why, whysize, "cancelled");
        } else if (x->read_err || x->next != it->e.size) {
            say(why, whysize, "cannot read %s from the image", it->path);
        } else if (x->write_errno != 0) {
            say(why, whysize, "cannot write the archive: %s", strerror(x->write_errno));
        } else {
            say(why, whysize, "cannot add %s to the archive: %s", it->path,
                mz_zip_get_error_string(mz_zip_get_last_error(zip)));
        }
        return -1;
    }
    if (used == 0) {
        return 0;
    }
    /* the bytes after the file's end in its last sector: kept when any is
       nonzero, so sector reads return what the disc held */
    if (ico_vfs_read_sectors(x->iso, it->e.lsn + it->e.size / ICO_VFS_SECTOR, 1, sec) != 0) {
        say(why, whysize, "cannot read the last sector of %s", it->path);
        return -1;
    }
    for (i = used; i < ICO_VFS_SECTOR && sec[i] == 0; i++) {}
    if (i == ICO_VFS_SECTOR) {
        return 0;
    }
    snprintf(name, sizeof(name), "tail/%s", it->path);
    if (!mz_zip_writer_add_mem_ex(zip, name, sec + used, ICO_VFS_SECTOR - used, NULL, 0,
                                  MZ_NO_COMPRESSION, 0, 0)) {
        say(why, whysize, "cannot add the tail of %s: %s", it->path,
            x->write_errno ? strerror(x->write_errno)
                           : mz_zip_get_error_string(mz_zip_get_last_error(zip)));
        return -1;
    }
    *tail_out = 1;
    return 0;
}

static void write_meta(Buf *b, const Ex *x, const IcoExtractResult *res, const int *tails)
{
    int i, k;

    buf_printf(b, "{\n  \"format\": ");
    buf_jstr(b, ICO_ARCHIVE_FORMAT);
    buf_printf(b, ",\n  \"version\": %d,\n  \"extractor\": ", ICO_ARCHIVE_VERSION);
    buf_jstr(b, ICO_ARCHIVE_EXTRACTOR);
    buf_printf(b, ",\n  \"disc_id\": ");
    buf_jstr(b, res->disc_id);
    buf_printf(b, ",\n  \"source\": {\n    \"sha1\": ");
    buf_jstr(b, res->iso_sha1);
    buf_printf(b,
               ",\n    \"size\": %llu,\n    \"accepted_by\": ", (unsigned long long)res->iso_size);
    buf_jstr(b, res->rule);
    buf_printf(b, ",\n    \"elf_sha1\": ");
    buf_jstr(b, res->elf_sha1);
    buf_printf(b, ",\n    \"datadf_manifest\": %s\n  },\n", res->datadf_ok ? "true" : "false");
    buf_printf(b, "  \"volume_sectors\": %u,\n  \"entries\": [\n",
               (unsigned)ico_vfs_volume_sectors(x->iso));
    for (i = 0; i < x->n; i++) {
        const Item *it = &x->items[i];

        buf_printf(b, "    {\"path\": ");
        buf_jstr(b, it->path);
        buf_printf(b, ", \"name\": ");
        buf_jstr(b, it->e.name);
        buf_printf(b, ", \"lsn\": %u, \"size\": %u", (unsigned)it->e.lsn, (unsigned)it->e.size);
        if (it->e.is_dir) {
            buf_printf(b, ", \"dir\": true");
        }
        buf_printf(b, ", \"date\": [");
        for (k = 0; k < 7; k++) {
            buf_printf(b, "%s%u", k ? ", " : "", (unsigned)it->e.date[k]);
        }
        buf_printf(b, "]");
        if (it->store) {
            buf_printf(b, ", \"data\": \"disc/%s\"", it->path);
            if (tails[i]) {
                buf_printf(b, ", \"tail\": \"tail/%s\"", it->path);
            }
        }
        buf_printf(b, "}%s\n", i + 1 < x->n ? "," : "");
    }
    buf_printf(b, "  ]\n}\n");
}

/* The image's SHA-1, with progress. */
static int hash_image(Ex *x, const char *iso_path, IcoExtractResult *res, char *why, size_t whysize)
{
    FILE *fp = ico_archive_fopen(iso_path, "rb");
    unsigned char *buf;
    IcoSha1 s;
    size_t got;
    int err;

    if (fp == NULL) {
        say(why, whysize, "cannot open %s: %s", iso_path, strerror(errno));
        return -1;
    }
    buf = malloc(HASH_CHUNK);
    if (buf == NULL) {
        fclose(fp);
        say(why, whysize, "out of memory");
        return -1;
    }
    ico_sha1_init(&s);
    while ((got = fread(buf, 1, HASH_CHUNK, fp)) != 0) {
        ico_sha1_update(&s, buf, got);
        if (tick(x, "hash", got)) {
            break;
        }
    }
    err = ferror(fp);
    fclose(fp);
    free(buf);
    if (x->cancelled) {
        say(why, whysize, "cancelled");
        return -1;
    }
    if (err) {
        say(why, whysize, "a read error in %s", iso_path);
        return -1;
    }
    res->iso_size = s.length;
    sha1_hex(&s, res->iso_sha1);
    return 0;
}

int ico_extract_archive(const char *iso_path, const char *out_path, unsigned flags,
                        IcoExtractProgressFn progress, void *ctx, IcoExtractResult *res, char *why,
                        size_t whysize)
{
    char tmp[ICO_PATH_MAX + 8];
    mz_zip_archive zip;
    int zip_open = 0;
    IcoVfsEntry root;
    IcoArchiveInfo info;
    Ex x;
    Buf meta;
    int *tails = NULL;
    int iso_ok, elf_ok;
    int i, ok = 0;
    double t0, t1;
    char dfwhy[256] = "";

    memset(res, 0, sizeof(*res));
    memset(&x, 0, sizeof(x));
    memset(&meta, 0, sizeof(meta));
    memset(&zip, 0, sizeof(zip));
    snprintf(tmp, sizeof(tmp), "%s.tmp", out_path);
    x.progress = progress;
    x.ctx = ctx;

    x.iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    if (x.iso == NULL) {
        say(why, whysize, "%s is not a readable ISO9660 disc image", iso_path);
        return -1;
    }
    x.items = calloc(ITEMS_MAX, sizeof(*x.items));
    tails = calloc(ITEMS_MAX, sizeof(*tails));
    if (x.items == NULL || tails == NULL) {
        say(why, whysize, "out of memory");
        goto done;
    }
    read_boot_name(&x);
    disc_id_of(x.boot, res->disc_id, sizeof(res->disc_id));
    if (add_item(&x, "", 0, why, whysize) != 0) {
        goto done;
    }
    root = x.items[0].e;
    if (walk(&x, "", &root, 0, 0, why, whysize) != 0) {
        goto done;
    }
    for (i = 0; i < x.n; i++) {
        if (x.items[i].store) {
            res->files++;
            res->bytes += x.items[i].e.size;
        }
    }
    if (res->files == 0) {
        say(why, whysize, "%s holds none of the game's files", iso_path);
        goto done;
    }
    if (df_begin(&x.df, x.iso, dfwhy, sizeof(dfwhy)) != 0) {
        x.df.valid = 0;
    }
    {
        FILE *fp = ico_archive_fopen(iso_path, "rb");
        int64_t sz = -1;

        if (fp != NULL && fseek(fp, 0, SEEK_END) == 0) {
            sz = ico_archive_tell(fp);
        }
        if (fp != NULL) {
            fclose(fp);
        }
        x.total = (sz > 0 ? (uint64_t)sz : 0) + res->bytes;
    }

    /* 1. the image's SHA-1 */
    t0 = now_s();
    if (hash_image(&x, iso_path, res, why, whysize) != 0) {
        res->cancelled = x.cancelled;
        goto done;
    }
    t1 = now_s();
    res->hash_seconds = t1 - t0;
    iso_ok = strcmp(res->iso_sha1, ICO_DISC_ISO_SHA1) == 0;

    /* 2. the files */
    x.out = ico_archive_fopen(tmp, "wb");
    if (x.out == NULL) {
        say(why, whysize, "cannot create %s: %s", tmp, strerror(errno));
        goto done;
    }
    zip.m_pWrite = zip_write;
    zip.m_pIO_opaque = &x;
    if (!mz_zip_writer_init_v2(&zip, 0, 0)) {
        say(why, whysize, "cannot start the archive: %s",
            mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        goto done;
    }
    zip_open = 1;
    for (i = 0; i < x.n; i++) {
        if (x.items[i].store && store_item(&x, &zip, &x.items[i], &tails[i], why, whysize) != 0) {
            res->cancelled = x.cancelled;
            goto done;
        }
    }
    res->extract_seconds = now_s() - t1;

    /* 3. the rule */
    if (x.elf_seen && x.elf_in_order) {
        sha1_hex(&x.elf, res->elf_sha1);
    }
    elf_ok = strcmp(res->elf_sha1, ICO_DISC_ELF_SHA1) == 0;
    res->datadf_ok = df_matches(&x.df, dfwhy[0] ? NULL : dfwhy, sizeof(dfwhy));
    snprintf(res->datadf_why, sizeof(res->datadf_why), "%s", res->datadf_ok ? "" : dfwhy);
    if (iso_ok) {
        snprintf(res->rule, sizeof(res->rule), "%s", ICO_RULE_ISO_SHA1);
    } else if (elf_ok && res->datadf_ok) {
        snprintf(res->rule, sizeof(res->rule), "%s", ICO_RULE_ELF_DATADF);
    } else if (flags & ICO_EXTRACT_NO_VERIFY) {
        snprintf(res->rule, sizeof(res->rule), "%s", ICO_RULE_UNVERIFIED);
    } else {
        say(why, whysize,
            "%s is not the ICO PAL disc (" ICO_DISC_ID ").\n"
            "Image SHA-1 %s (expected " ICO_DISC_ISO_SHA1 ");\n"
            "SCES_507.60 SHA-1 %s (expected " ICO_DISC_ELF_SHA1 ");\n"
            "DATA.DF: %s.",
            iso_path, res->iso_sha1, res->elf_sha1[0] ? res->elf_sha1 : "(absent)",
            res->datadf_ok ? "matches" : res->datadf_why);
        goto done;
    }

    /* 4. meta.json, the directory, and the move into place */
    write_meta(&meta, &x, res, tails);
    if (meta.bad) {
        say(why, whysize, "out of memory");
        goto done;
    }
    if (!mz_zip_writer_add_mem_ex(&zip, ICO_ARCHIVE_META, meta.p, meta.n, NULL, 0,
                                  MZ_NO_COMPRESSION, 0, 0) ||
        !mz_zip_writer_finalize_archive(&zip)) {
        say(why, whysize, "cannot finish the archive: %s",
            x.write_errno ? strerror(x.write_errno)
                          : mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        goto done;
    }
    mz_zip_writer_end(&zip);
    zip_open = 0;
    if (fflush(x.out) != 0 || fclose(x.out) != 0) {
        x.out = NULL;
        say(why, whysize, "cannot write %s: %s", tmp, strerror(errno));
        goto done;
    }
    x.out = NULL;
    if (ico_archive_read_info(tmp, &info, why, whysize) != 0) {
        goto done;
    }
    res->archive_bytes = info.file_bytes;
    if (ico_archive_replace(tmp, out_path) != 0) {
        say(why, whysize, "cannot move %s to %s: %s", tmp, out_path, strerror(errno));
        goto done;
    }
    ok = 1;

done:
    if (zip_open) {
        mz_zip_writer_end(&zip);
    }
    if (x.out != NULL) {
        fclose(x.out);
    }
    if (!ok) {
        ico_archive_remove(tmp);
    }
    ico_vfs_unmount(x.iso);
    free(x.items);
    free(x.df.m);
    free(tails);
    free(meta.p);
    return ok ? 0 : -1;
}
