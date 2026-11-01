/*
 * port/save/state_recover.c
 *
 * A memory-card save from an old save state's progress (state_recover.h):
 * the side file, this program's copies of the globals the card code and the
 * save image need, what the image holds, and the card write through the
 * game's own ico2/fumi/ios/mcard.c on the fiber scheduler.
 *
 * The card code runs as in save_test (port/save/test/save_test.c, part B):
 * the manager thread iosMcManager at the game's priority, a driver thread
 * issuing the requests as layout_action.c does and polling iosMcSync once a
 * vsync, the vsyncs simulated here.  The icon files are never written (they
 * come from the disc, which this program has no access to): a card whose
 * directory lacks them gets them at the game's next save, which writes the
 * icon block first (la_save_processing step 2).
 */
#include "state_recover.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <eekernel.h>
#include <eeregs.h>
#include <libmc.h>
#include "debug.h"
#include "ios.h"
#include "main.h"
#include "cdvd.h"
#include "gamesys.h"
#include "mcard.h"
#include "mcdata.h"
#include "memory.h"
#include "message.h"
#include "pad.h"
#include "region.h" /* the game's: ICO_<SET>, PAL_<SET> */
#include "s_init.h"
#include "thread.h"
#include "typedef.h"
/* the region table (port/core/region.h, by its path: the game's include
   directories hold the game's own region.h) */
#include "../core/region.h"
#include "arena.h"
#include "host_loop.h"
#include "kernel_host.h"
#include "mc_host.h"
#include "sched.h"

/* --- what the rest of the game would define ---------------------------------- */

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    fprintf(stderr, "state_recover: the card code asserted at %s:%d: %s\n", file, line, mes);
}

void debug_assert(const char *file, int line)
{
    fprintf(stderr, "state_recover: the card code asserted at %s:%d\n", file, line);
}

int odd_even;

IosMemPart *ios_partition_root;

IosMemPart *ios_partition_event;

IosMemPart *ios_partition_sound;

/* the globals product_write and gameblock_write read, and the setters
   product_read and gameblock_read call (save_test.c keeps the same set) */
int systemStatus[12];

int NonLinearCameraMove;

int optionControlType;

int optionScreenMode;

int girlControlMode;

int iosPadActRequestEnable;

PadConf iosPadConfCustom;

static int s_sound_output;

int soundOutputModeGet(void)
{
    return s_sound_output;
}

void soundOutputModeSet(int mode)
{
    s_sound_output = mode;
}

/* the save image (gamesys.c's, which this program does not link) */
char gameSysMainSaveBuff[25596];

/* mcard.c's preview words (its definition: int IosMcPreviewInfo[6]) */
extern int IosMcPreviewInfo[6];

/* mcdata.c names these for the icon block, which this program never asks
   for: the disc is not here.  A call is a failure the card code reports. */
const IconFile iconFile[] = {{"", 0},
                             {"icon.sys", 964},
                             {"boy_blk.ico", 95624},
                             {"boy_blk.ico", 95624},
                             {"boy_blk.ico", 95624}};

int IosCdvdMgrSleep;

static CdvdBgReq s_no_disc;

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
    fprintf(stderr, "state_recover: the card code asked for the disc's icon files\n");
    return &s_no_disc;
}

int iosCdvdBackGroundRead(CdvdBgReq *self, void *buf, int size)
{
    (void)self;
    memset(buf, 0, (size_t)size);
    return 1;
}

void iosCdvdBackGroundMgrDelete(CdvdBgReq *self)
{
    (void)self;
}

/* --- the side file ------------------------------------------------------------ */

static void set_why(char *why, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (why == NULL || n == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(why, n, fmt, ap);
    va_end(ap);
}

void ico_recover_side_free(IcoRecoverSide *s)
{
    int i;

    for (i = 0; i < s->n_blobs; i++) {
        free(s->blobs[i].data);
    }
    memset(s, 0, sizeof *s);
}

int ico_recover_side_add(IcoRecoverSide *s, const char *name, const void *data, uint32_t size)
{
    IcoRecoverBlob *b;

    if (s->n_blobs >= ICO_RECOVER_MAX_BLOBS || size > ICO_RECOVER_BLOB_MAX ||
        strlen(name) >= ICO_RECOVER_NAME_BYTES) {
        return -1;
    }
    b = &s->blobs[s->n_blobs];
    b->data = (unsigned char *)malloc(size > 0 ? size : 1);
    if (b->data == NULL) {
        return -1;
    }
    memcpy(b->data, data, size);
    b->size = size;
    snprintf(b->name, sizeof b->name, "%s", name);
    s->n_blobs++;
    return 0;
}

const IcoRecoverBlob *ico_recover_side_blob(const IcoRecoverSide *s, const char *name)
{
    int i;

    for (i = 0; i < s->n_blobs; i++) {
        if (strcmp(s->blobs[i].name, name) == 0) {
            return &s->blobs[i];
        }
    }
    return NULL;
}

/* the text after "key " on a line, into out */
static int line_value(const char *line, const char *key, char *out, size_t n)
{
    size_t k = strlen(key);
    size_t len;

    if (strncmp(line, key, k) != 0 || line[k] != ' ') {
        return 0;
    }
    len = strlen(line + k + 1);
    if (len >= n) {
        len = n - 1; /* cut to the field */
    }
    memcpy(out, line + k + 1, len);
    out[len] = '\0';
    return 1;
}

int ico_recover_side_read(const char *path, IcoRecoverSide *out, char *why, size_t n)
{
    FILE *f;
    unsigned char *buf = NULL;
    long size;
    size_t pos = 0;
    int magic = 0, ended = 0;
    int rc = -1;

    memset(out, 0, sizeof *out);
    f = fopen(path, "rb");
    if (f == NULL) {
        set_why(why, n, "cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        size > (long)(ICO_RECOVER_MAX_BLOBS * (ICO_RECOVER_BLOB_MAX + 128u) + 4096u)) {
        set_why(why, n, "%s is not a side file (its size)", path);
        fclose(f);
        return -1;
    }
    buf = (unsigned char *)malloc((size_t)size + 1);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        set_why(why, n, "cannot read %s", path);
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);
    buf[size] = '\0';
    while (pos < (size_t)size && !ended) {
        char line[512];
        size_t e = pos;
        size_t len;
        char name[ICO_RECOVER_NAME_BYTES];
        unsigned long bytes;
        char tmp[64];

        while (e < (size_t)size && buf[e] != '\n') {
            e++;
        }
        if (e == (size_t)size) {
            break; /* no newline: a cut file */
        }
        len = e - pos;
        if (len >= sizeof line) {
            set_why(why, n, "%s: a line is too long", path);
            goto out;
        }
        memcpy(line, buf + pos, len);
        line[len] = '\0';
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }
        pos = e + 1;
        if (!magic) {
            if (strcmp(line, ICO_RECOVER_MAGIC) != 0) {
                set_why(why, n, "%s is not a side file (no \"%s\" line)", path, ICO_RECOVER_MAGIC);
                goto out;
            }
            magic = 1;
            continue;
        }
        if (strcmp(line, "end") == 0) {
            ended = 1;
        } else if (sscanf(line, "blob %47s %lu", name, &bytes) == 2) {
            if (bytes > ICO_RECOVER_BLOB_MAX || bytes > (size_t)size - pos ||
                pos + bytes >= (size_t)size || buf[pos + bytes] != '\n') {
                set_why(why, n, "%s: the blob %s is cut short or too large", path, name);
                goto out;
            }
            if (ico_recover_side_blob(out, name) != NULL ||
                ico_recover_side_add(out, name, buf + pos, (uint32_t)bytes) != 0) {
                set_why(why, n, "%s: the blob %s repeats, or too many blobs", path, name);
                goto out;
            }
            pos += bytes + 1;
        } else if (line_value(line, "disc", out->disc, sizeof out->disc) ||
                   line_value(line, "commit", out->commit, sizeof out->commit) ||
                   line_value(line, "platform", out->platform, sizeof out->platform) ||
                   line_value(line, "source", out->source, sizeof out->source)) {
        } else if (line_value(line, "format", tmp, sizeof tmp)) {
            out->format = atoi(tmp);
        } else if (line_value(line, "state_stage", tmp, sizeof tmp)) {
            out->state_stage = atoi(tmp);
        } else if (line_value(line, "play_frames", tmp, sizeof tmp)) {
            out->play_frames = (uint32_t)strtoul(tmp, NULL, 10);
        } else if (line_value(line, "main_tick", tmp, sizeof tmp)) {
            out->main_tick = (uint32_t)strtoul(tmp, NULL, 10);
        } else if (line_value(line, "saved_at", tmp, sizeof tmp)) {
            out->saved_at = (int64_t)strtoll(tmp, NULL, 10);
        }
    }
    if (!magic) {
        set_why(why, n, "%s is empty", path);
        goto out;
    }
    if (!ended) {
        set_why(why, n, "%s is cut short (no \"end\" line)", path);
        goto out;
    }
    if (out->disc[0] == '\0') {
        set_why(why, n, "%s names no disc", path);
        goto out;
    }
    rc = 0;
out:
    free(buf);
    return rc;
}

int ico_recover_side_write(const char *path, const IcoRecoverSide *s)
{
    FILE *f = fopen(path, "wb");
    int i;
    int ok;

    if (f == NULL) {
        return -1;
    }
    fprintf(f, "%s\ndisc %s\ncommit %s\nplatform %s\nformat %d\nstate_stage %d\n",
            ICO_RECOVER_MAGIC, s->disc, s->commit, s->platform, s->format, s->state_stage);
    fprintf(f, "play_frames %lu\nmain_tick %lu\nsaved_at %lld\nsource %s\n",
            (unsigned long)s->play_frames, (unsigned long)s->main_tick, (long long)s->saved_at,
            s->source);
    for (i = 0; i < s->n_blobs; i++) {
        fprintf(f, "blob %s %lu\n", s->blobs[i].name, (unsigned long)s->blobs[i].size);
        fwrite(s->blobs[i].data, 1, s->blobs[i].size, f);
        fputc('\n', f);
    }
    fputs("end\n", f);
    ok = ferror(f) == 0;
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* --- the globals ---------------------------------------------------------------- */

enum { NEED_ALWAYS = 1, NEED_OPTION_WORDS = 2, NEED_NEVER = 0 };

typedef struct RecoverGlobal {
    const char *name;
    void *p;
    size_t size;
    int need;
} RecoverGlobal;

/* the state's IosMcProductFile, kept apart: the card's own record is read
   into the live one before the write */
static McProductFile s_state_product[2];
static int s_have_state_product;

static const RecoverGlobal s_globals[] = {
    {"gameSysMainSaveBuff", gameSysMainSaveBuff, sizeof gameSysMainSaveBuff, NEED_ALWAYS},
    {"systemStatus", systemStatus, sizeof systemStatus, NEED_ALWAYS},
    {"IosMcPreviewInfo", IosMcPreviewInfo, 6 * sizeof(int), NEED_ALWAYS},
    {"optionScreenMode", &optionScreenMode, sizeof optionScreenMode, NEED_OPTION_WORDS},
    {"girlControlMode", &girlControlMode, sizeof girlControlMode, NEED_OPTION_WORDS},
    {"IosMcProductFile", s_state_product, sizeof s_state_product, NEED_NEVER},
};

/* the version stamp gamesysVersionSave writes (gamesys.c stamp_str_*), 18
   bytes with the NUL */
static const char *disc_stamp(void)
{
    return PAL_JPN_KORCN_NTSC_AUG6_JUN26("12/12/01 17:53:37", "10/23/01 02:02:43",
                                         "10/23/01 17:47:13", "08/18/01 22:22:20",
                                         "08/07/01 14:51:53", "06/26/01 20:00:06");
}

#define STAMP_BYTES 18

int ico_recover_apply(const IcoRecoverSide *s, char *why, size_t n)
{
    const int option_words = !ICO_NTSCAUG6JUN26;
    size_t i;
    int k;

    if (ico_region_select(s->disc) != 0) {
        set_why(why, n, "the state's disc \"%s\" is not one this program plays", s->disc);
        return -1;
    }
    s_have_state_product = 0;
    for (i = 0; i < sizeof s_globals / sizeof s_globals[0]; i++) {
        const RecoverGlobal *g = &s_globals[i];
        const IcoRecoverBlob *b = ico_recover_side_blob(s, g->name);
        const int need = g->need == NEED_ALWAYS || (g->need == NEED_OPTION_WORDS && option_words);

        if (b == NULL) {
            if (need) {
                set_why(why, n, "the side file has no %s", g->name);
                return -1;
            }
            continue;
        }
        if (b->size != g->size) {
            set_why(why, n, "the state's %s is %lu bytes, this program's %lu", g->name,
                    (unsigned long)b->size, (unsigned long)g->size);
            return -1;
        }
        memcpy(g->p, b->data, g->size);
        if (g->p == (void *)s_state_product) {
            s_have_state_product = 1;
        }
    }
    for (k = 0; k < s->n_blobs; k++) {
        int known = 0;

        for (i = 0; i < sizeof s_globals / sizeof s_globals[0]; i++) {
            known |= strcmp(s->blobs[k].name, s_globals[i].name) == 0;
        }
        if (!known) {
            fprintf(stderr, "state_recover: the side file's %s is not used\n", s->blobs[k].name);
        }
    }
    if (systemStatus[3] == 0) {
        set_why(why, n,
                "the state holds no checkpoint since the game was started or loaded "
                "(systemStatus[3] is 0): there is no progress to save");
        return -1;
    }
    {
        static const char zeros[STAMP_BYTES];

        if (memcmp(gameSysMainSaveBuff, disc_stamp(), STAMP_BYTES) != 0 &&
            memcmp(gameSysMainSaveBuff, zeros, STAMP_BYTES) != 0) {
            set_why(why, n,
                    "the save image does not start with the %s disc's version stamp \"%s\" "
                    "(not this disc's, or the symbol table is not the state's build)",
                    ico_region_cur()->slug, disc_stamp());
            return -1;
        }
    }
    return 0;
}

/* --- what the image holds ------------------------------------------------------ */

static int32_t rd32(const unsigned char *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                     (uint32_t)p[3] << 24);
}

static int rd16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

void ico_recover_info(IcoRecoverInfo *out)
{
    const unsigned char *img = (const unsigned char *)gameSysMainSaveBuff;
    /* the records in gameSysMemoryFuncList's order, each the bytes its save
       function writes: the stamp (gamesysVersionSave); gflagSave's stage,
       then gFlagGameClear and gflags[50] or the 2001 discs' gFlag[46];
       gamesysObjInfoSave's time, records and exit times; the generators
       (generator.c GetsizeGeneratorPacket); the hints (lws_kyomi.c
       hintWork: 8 flag bytes and 28 timers; none on the 2001 discs); the
       character record (boyact.c GetsizeCharacterPacket); backStageSave's
       nine words; itouGflagSave writes nothing */
    const int gflag = 4 + (ICO_NTSCAUG6JUN26 ? 46 : 4 + 50);
    const int objs = PAL_JUN26(0x2D80, 7808);
    const int objinfo = 4 + objs + PAL_JPNKORNTSCAUG6CN_JUN26(0x1A8, 0x1A0, 412);
    const int generator = PAL_JPNKORCN_NTSC_AUG6_JUN26(11277, 11208, 10365, 10251, 9744);
    const int hint = ICO_NTSCAUG6JUN26 ? 0 : 8 + 28 * 4;
    const int character = PAL_NTSCAUG6JUN26(32, 24);
    const int at_gflag = STAMP_BYTES;
    const int at_obj = at_gflag + gflag;
    const int at_char = at_obj + objinfo + generator + hint;
    const unsigned char *flags = img + at_gflag + (ICO_NTSCAUG6JUN26 ? 4 : 8);
    const int flag_bytes = ICO_NTSCAUG6JUN26 ? 46 : 50;
    const unsigned char *rec = img + at_obj + 4;
    const unsigned char *pkt = img + at_char;
    int i;

    memset(out, 0, sizeof *out);
    out->block_bytes = PAL_NTSCAUG6_JUN26(25588, 25596, 20480);
    out->option_words = !ICO_NTSCAUG6JUN26;
    out->image_bytes = at_char + character + 36;
    out->stamp = memcmp(img, disc_stamp(), STAMP_BYTES) == 0;
    out->stage = rd32(img + at_gflag);
    out->cleared = ICO_NTSCAUG6JUN26 ? -1 : rd32(img + at_gflag + 4);
    for (i = 0; i < flag_bytes; i++) {
        unsigned v = flags[i];

        while (v != 0) {
            out->flags_set += (int)(v & 1u);
            v >>= 1;
        }
    }
    out->time_count = (uint32_t)rd32(img + at_obj);
    /* GamesysObjInfo (backStage.h): no at 2, stage at 4, 64 bytes; record 0
       the boy's, 1 Yorda's (gamesysObjInfoBaseSet's kinds 1 and 2) */
    out->boy_stage = rd16(rec + 2) != 0 ? rd16(rec + 4) : 0xFFFF;
    out->girl_stage = rd16(rec + 64 + 2) != 0 ? rd16(rec + 64 + 4) : 0xFFFF;
    /* MakeCharacterPacket: the weapon's layout id at 8, the held object's
       at 0xC, the sofa's at 0x10 when the boy sat on one */
    out->weapon = rd32(pkt + 8);
    out->held = rd32(pkt + 0xC);
    out->sofa = rd32(pkt + 0x10);
}

/* --- the card, on the scheduler ------------------------------------------------- */

static McMgr s_req;
static IOSThread s_mc_th;
static IOSThread s_driver_th;
static IOSThread s_idle_th;
static char s_mc_stack[0x4000] __attribute__((aligned(16)));
static char s_driver_stack[0x8000] __attribute__((aligned(16)));
static char s_idle_stack[0x1800] __attribute__((aligned(16)));
static int s_done;
static int s_failed_wait;
static void (*s_job)(void);

static void idle(void)
{
    iosThreadSetPri(0, 0x20);
    for (;;) {
        ico_sched_spin_vsync();
    }
}

/* a request is finished when iosMcSync says so; polled once a vsync, as
   the game's Main does each tick */
static int wait_request(void)
{
    int guard = 0;

    while (iosMcSync(&s_req) == 0) {
        ico_sched_spin_vsync();
        if (++guard > 4000) {
            s_failed_wait = 1;
            return -1;
        }
    }
    return 0;
}

static void driver(void *arg)
{
    (void)arg;
    s_job();
    s_done = 1;
    for (;;) {
        ico_sched_spin_vsync();
    }
}

static void boot(void *arg)
{
    (void)arg;
    ChangeThreadPriority(GetThreadId(), 14);
    iosThreadInit();
    ios_partition_root =
        iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    ios_partition_event = iosMallocSetPartition(ios_partition_root, 262144, 16);
    ios_partition_sound = ios_partition_event;
    iosMsgInit();
    iosThreadCreate(&s_idle_th, 1, idle, 0, s_idle_stack, sizeof s_idle_stack, 0x1B);
    iosThreadStart(&s_idle_th);
    /* the game's priorities: the manager 27, the caller lower */
    iosThreadCreate(&s_mc_th, 1, iosMcManager, 0, s_mc_stack, sizeof s_mc_stack, 27);
    iosThreadStart(&s_mc_th);
    iosThreadCreate(&s_driver_th, 1, driver, 0, s_driver_stack, sizeof s_driver_stack, 0x1C);
    iosThreadStart(&s_driver_th);
    iosThreadSleep();
}

static void vsync(int field)
{
    ico_sched_vsync_advance();
    if (field) {
        *GS_CSR |= 1ull << 13;
    } else {
        *GS_CSR &= ~(1ull << 13);
    }
    ico_kernel_raise_intc(2);
    ico_host_run_vsync_hooks();
    ico_sched_run();
}

/* runs job on the driver thread over the card folder: 0, or -1 when the
   scheduler could not start or the job did not finish */
static int run_job(const char *card, void (*job)(void))
{
    int vsyncs = 0;

    if (ico_arena_init() != 0) {
        return -1;
    }
    ico_mc_host_set_root(card);
    s_job = job;
    s_done = 0;
    s_failed_wait = 0;
    memset(&s_req, 0, sizeof s_req);
    ico_sched_reset();
    ico_kernel_reset();
    if (ico_sched_boot(boot, NULL, 1) != 0) {
        return -1;
    }
    ico_sched_run();
    while (!s_done && vsyncs < 200000) {
        vsync(vsyncs & 1);
        vsyncs++;
    }
    ico_sched_reset();
    return s_done && !s_failed_wait ? 0 : -1;
}

/* --- the write --------------------------------------------------------------- */

static int s_slot_arg, s_overwrite;
static IcoRecoverResult *s_res;
static int s_rc;

static void refuse(int rc, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s_res->why, sizeof s_res->why, fmt, ap);
    va_end(ap);
    s_rc = rc;
}

static int dir_has(const char *name)
{
    int i;

    for (i = 0; i < s_req.dirCount && i < 20; i++) {
        if (strcmp((const char *)s_req.dir[i].EntryName, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* mcMakeSerial's number (layout_action.c) from this computer's clock */
static int clock_serial(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    int v = 1;

    if (tm != NULL) {
        v = ((tm->tm_year & 7) << 26) + (tm->tm_mday << 20) + (tm->tm_hour << 14) +
            (tm->tm_min << 7) + tm->tm_sec;
    }
    return v != 0 ? v : 1;
}

/* the globals product_write takes the record's option words from, set
   from the record so the write keeps them */
static void options_from_record(const McProductFile *p)
{
    systemStatus[11] = p->soundMode;
    s_sound_output = p->outputMode;
    iosPadActRequestEnable = p->vibration;
    optionControlType = p->controlType;
    memcpy(iosPadConfCustom.bit, p->padConf, sizeof p->padConf);
    if (!ICO_JPNKORCN && !ICO_NTSCAUG6JUN26) {
        NonLinearCameraMove = p->cameraMove;
        systemStatus[0] = p->palMode;
    }
}

/* the card's own reader: name's bytes (path from the card's root) into buf */
static int card_read_file(char *name, unsigned char *buf, int max)
{
    int cmd = 0, result = 0, fd, n, guard = 0;

    sceMcOpen(0, 0, name, SCE_RDONLY);
    while (sceMcSync(1, &cmd, &result) == 0 && ++guard < 100) {
        ico_sched_spin_vsync();
    }
    if (result < 0) {
        return -1;
    }
    fd = result;
    sceMcRead(fd, buf, max);
    guard = 0;
    while (sceMcSync(1, &cmd, &result) == 0 && ++guard < 100) {
        ico_sched_spin_vsync();
    }
    n = result;
    sceMcClose(fd);
    guard = 0;
    while (sceMcSync(1, &cmd, &result) == 0 && ++guard < 100) {
        ico_sched_spin_vsync();
    }
    return n;
}

static void write_job(void)
{
    const char *dir = ico_region_cur()->mc_save_dir;
    const int is2001 = ICO_NTSCAUG6JUN26;
    IcoRecoverInfo info;
    McProductFile *prod = &IosMcProductFile[0];
    int prev[6];
    int slot = s_slot_arg;
    int i;
    char name[64];
    static unsigned char file[26000];
    int n;
    uint32_t sum = 0;

    ico_recover_info(&info);
    s_req.port = 0;
    s_req.slot = 0;

    /* the directory: -14 when the card has none yet (kanbanBoot.c's check) */
    s_req.flags.ll &= ~2;
    iosMcChdirProduct(&s_req);
    if (wait_request() != 0) {
        refuse(-1, "the card did not answer");
        return;
    }
    if (s_req.result != 0 && s_req.result != -14) {
        refuse(-1, "the card folder cannot be used (card result %d)", s_req.result);
        return;
    }
    if (s_req.result == 0) {
        strcpy(s_req.path, "*");
        iosMcGetDir(&s_req);
        if (wait_request() != 0) {
            refuse(-1, "the card did not answer");
            return;
        }
        s_res->icons = dir_has("icon.sys") && dir_has("boy_blk.ico");
        /* the product file: the card's when it reads (-4: there is none) */
        iosMcLoadProductBlock(&s_req);
        if (wait_request() != 0) {
            refuse(-1, "the card did not answer");
            return;
        }
        if (s_req.result >= 0) {
            s_res->product_existed = 1;
        } else if (s_req.result != -4) {
            refuse(1,
                   "the card's product file %s/%s cannot be read (card result %d): it is "
                   "damaged; nothing was written",
                   dir, dir, s_req.result);
            return;
        }
    }

    /* the slots that hold a save (layout_action.c la_save_processing step 0) */
    strcpy(s_req.path, "game.");
    s_req.fileNo = slot >= 0 ? slot : 0;
    iosMcGetBlockSaveInfo(&s_req);
    if (wait_request() != 0 || s_req.result < 0) {
        refuse(-1, "the card's save list cannot be read (card result %d)", s_req.result);
        return;
    }
    s_res->used_mask = (uint32_t)s_req.mask & 0x3FFu;
    if (slot < 0) {
        for (i = 0; i < 10 && ((s_res->used_mask >> i) & 1u) != 0; i++) {}
        if (i == 10) {
            refuse(1,
                   "the card holds a save in all ten slots: name one with --slot and --overwrite");
            return;
        }
        slot = i;
    } else if (((s_res->used_mask >> slot) & 1u) != 0 && !s_overwrite) {
        refuse(1, "slot %d holds a save already (game.%03d): choose another, or add --overwrite",
               slot, slot);
        return;
    }
    s_res->slot = slot;

    /* no product file: a first save's (step 4 of la_system_save_processing:
       ten empty previews and a serial) */
    if (!s_res->product_existed) {
        if (s_have_state_product) {
            *prod = s_state_product[0];
        } else {
            memset(prod, 0, sizeof *prod);
        }
        for (i = 0; i < 10; i++) {
            prod->file[i].stage = 0xFFFFFFFFu;
        }
        if (!is2001 && prod->serial == 0) {
            prod->serial = clock_serial();
        }
    }
    memset(&IosMcProductFile[1], 0, sizeof IosMcProductFile[1]);
    options_from_record(prod);

    /* the slot's preview as la_save_processing step 4 sets it: the stage,
       the sofa (GetSaveSofaLayoutID: -1 off a sofa), the cleared flag; the
       play time is the state's own word */
    memcpy(prev, IosMcPreviewInfo, sizeof prev);
    prev[0] = info.stage;
    if (is2001) {
        prev[2] = info.sofa != 0 ? info.sofa : -1;
    } else {
        prev[3] = info.sofa != 0 ? info.sofa : -1;
        prev[1] = info.cleared;
    }
    memcpy(&prod->file[slot], prev, sizeof(McFileInfo));
    memcpy(IosMcPreviewInfo, prev, sizeof prev);
    if (!is2001) {
        prod->fileNo = slot;
        if (prod->serial == 0) {
            prod->serial = clock_serial();
        }
    }

    s_req.fileNo = slot;
    iosMcSaveProductBlock(&s_req);
    if (wait_request() != 0 || s_req.result != 0) {
        refuse(-1, "the product file was not written (card result %d)", s_req.result);
        return;
    }
    iosMcSaveGameBlock(&s_req, gameSysMainSaveBuff);
    if (wait_request() != 0 || s_req.result != 0) {
        refuse(-1, "game.%03d was not written (card result %d)", slot, s_req.result);
        return;
    }
    s_res->sum = (uint32_t)s_req.sum;

    /* read back through the card: the block, the option words, the sum */
    snprintf(name, sizeof name, "/%s/game.%03d", dir, slot);
    n = card_read_file(name, file, (int)sizeof file);
    s_res->file_bytes = n;
    if (n != info.block_bytes + (info.option_words ? 8 : 0) + 4) {
        refuse(-1, "game.%03d reads back as %d bytes", slot, n);
        return;
    }
    for (i = 0; i < n - 4; i++) {
        sum += file[i];
    }
    if (memcmp(file, gameSysMainSaveBuff, (size_t)info.block_bytes) != 0 ||
        (uint32_t)rd32(file + n - 4) != sum || sum != s_res->sum) {
        refuse(-1, "game.%03d does not read back as written", slot);
        return;
    }
    snprintf(s_res->path, sizeof s_res->path, "%s/%s/game.%03d", ico_mc_host_root(), dir, slot);
    s_rc = 0;
}

int ico_recover_write_card(const char *card, int slot, int overwrite, IcoRecoverResult *res)
{
    memset(res, 0, sizeof *res);
    res->slot = -1;
    if (slot < -1 || slot > 9) {
        snprintf(res->why, sizeof res->why, "slot %d is not one of 0 to 9", slot);
        return 1;
    }
    s_slot_arg = slot;
    s_overwrite = overwrite;
    s_res = res;
    s_rc = -1;
    snprintf(res->why, sizeof res->why, "the card code did not finish");
    if (run_job(card, write_job) != 0) {
        if (s_rc == 0) {
            s_rc = -1;
        }
        if (res->why[0] == '\0') {
            snprintf(res->why, sizeof res->why, "the card code did not finish");
        }
        return s_rc;
    }
    if (s_rc == 0) {
        res->why[0] = '\0';
    }
    return s_rc;
}

/* --- the read (the tests) ------------------------------------------------------ */

static int s_read_slot;
static unsigned char *s_read_block;
static size_t s_read_block_size;
static void *s_read_product;
static size_t s_read_product_size;
static int s_read_result;

static void read_job(void)
{
    static unsigned char buf[25600];

    s_req.port = 0;
    s_req.slot = 0;
    strcpy(s_req.path, "game.");
    s_req.fileNo = s_read_slot;
    iosMcGetBlockSaveInfo(&s_req);
    if (wait_request() != 0 || s_req.result < 0) {
        s_read_result = s_req.result != 0 ? s_req.result : -100;
        s_rc = -1;
        return;
    }
    iosMcLoadProductBlock(&s_req);
    if (wait_request() != 0 || s_req.result < 0) {
        s_read_result = s_req.result;
        s_rc = -1;
        return;
    }
    if (s_read_product != NULL) {
        memcpy(s_read_product, &IosMcProductFile[0],
               s_read_product_size < sizeof(McProductFile) ? s_read_product_size
                                                           : sizeof(McProductFile));
    }
    memset(buf, 0, sizeof buf);
    iosMcLoadGameBlock(&s_req, buf);
    if (wait_request() != 0 || s_req.result < 0) {
        s_read_result = s_req.result;
        s_rc = -1;
        return;
    }
    s_read_result = s_req.result;
    if (s_read_block != NULL) {
        unsigned char *p = s_read_block;
        size_t want = sizeof buf;

        if (want > s_read_block_size) {
            want = s_read_block_size;
        }
        memcpy(p, buf, want);
        /* the option words gameblock_read put back, after the block */
        if (!ICO_NTSCAUG6JUN26 && s_read_block_size >= 25588 + 8) {
            memcpy(p + 25588, &optionScreenMode, 4);
            memcpy(p + 25592, &girlControlMode, 4);
        }
    }
    s_rc = 0;
}

int ico_recover_read_card(const char *card, int slot, unsigned char *block, size_t block_size,
                          void *product, size_t product_size, int *result)
{
    s_read_slot = slot;
    s_read_block = block;
    s_read_block_size = block_size;
    s_read_product = product;
    s_read_product_size = product_size;
    s_read_result = 0;
    s_rc = -1;
    if (run_job(card, read_job) != 0) {
        s_rc = -1;
    }
    if (result != NULL) {
        *result = s_read_result;
    }
    return s_rc;
}
