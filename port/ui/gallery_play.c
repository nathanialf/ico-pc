/*
 * port/ui/gallery_play.c
 *
 * The music gallery's engine (gallery.h GalleryEngine; docs/port/MUSIC.md,
 * "Playback"): the game's own stream and effect calls, made from the
 * gallery page's layout proc on the simulation thread, while the title is
 * up.
 *
 * Streams.  The game has two stream records and two IOP rings
 * (fumi/sound/adpcm_init.c).  The title theme (op.c actTitleShortCut,
 * stream 56 in titleAdpcm) holds one; the gallery fades it out
 * (scpAdpcmFadeCloseFunc, as op.c's demo step does) and requests its stream
 * once the theme has closed, through scpAdpcmPlayRequestFunc and the
 * script daemon's thread (scpSubAdpcmPlay), as every scene does.  The
 * theme's handle is cleared when it has closed, as op.c clears it after its
 * own fade: actTitleShortCut waits while titleAdpcm is set.  On leaving, a
 * theme that was playing is requested again (from its start).
 *
 * Effects.  soundSeDefPlay(def, owner -1, no position, play mode 0) with
 * the effect's bank resident and seKind pointing the effect's kind at the
 * bank's row; the title theme is faded first, as for a stream, so the
 * effect is heard alone.  A bank that is not resident is read from a stage
 * pack on the disc (port/data/df_pack.h) into sound RAM as ReadSoundHdFile
 * and ReadSoundBdFile do (soundBDDataSet, soundHDDataSet).  On the title
 * the SPU buffer holds the six common banks (segment 0, up to 0x154A10)
 * and the logo stage's st27a_sys (segment 2, 407,552 bytes), which leaves
 * 134,672 bytes below segment 1's top (0x1D9020): too little for most
 * stage banks.  So the first such load closes the banks of segments 1 and
 * 2 (every slot playing from them stopped first) and the gallery's bank
 * takes segment 1, where up to 542,224 bytes are free (the largest bank,
 * st13b_obj.bd4, is 507,264); one gallery bank is resident at a time.
 * Leaving closes it, reads the closed banks back into their segments in
 * their order (the headers kept where the game put them; each must come
 * back at its old address) and rebuilds seKind (soundSeKindBuild).
 *
 * Pause.  A stream pauses as the pause menu pauses the game's streams
 * (layout_action.c la_game_pause): adpcmPauseRequest(1) sets every stream
 * voice's pitch to 0 and stops the disc reader's accounting until
 * adpcmPauseRequest(0).  An effect cannot pause (the page stops it).
 *
 * Position.  A stream's elapsed time is what its record has consumed
 * (AdpcmStream dataSize - remain: adpcmTickProc moves remain on by the IOP
 * read offset's progress, which the driver moves by half the SPU ring per
 * fill) less the 1.5 halves the SPU ring holds ahead of the voice on
 * average, in seconds at the stream's rate (adpcmFile pitch, Hz; 16 bytes
 * of SPU ADPCM are 28 samples per channel); its total is the file's
 * sectors.  An effect's total is its sample: the voices keyed after the
 * request that were silent before it and start inside the bank's SPU
 * buffer, each read in sound RAM from its start (SSA) to the block with
 * the end flag, at its voice's pitch; a sample whose last block loops
 * repeats, and its elapsed time is then counted within the loop.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm_init.h"
#include "df_pack.h"
#include "gallery.h"
#include "s_init.h"
#include "spu2.h"
#include "spu2_sd.h"
#include "typedef.h"
#include "vfs.h"

/* the game's side, declared here as the callers in the game do */
extern SeDef seDef[];
extern struct SqEntry *titleAdpcm; /* op.c */
extern void scpAdpcmPlayRequestFunc(int kind, struct SqEntry **id, int ch, int loopNum, int play);
extern int scpAdpcmPlayRequestNum(void);
extern int scpAdpcmFadeCloseFunc(struct SqEntry **h, short fade);
extern int scpAdpcmCloseChkFunc(struct SqEntry **h);
extern void scpAdpcmCloseFunc(struct SqEntry **h);
struct IosMemPart;
extern struct IosMemPart *ios_partition_sound;
extern struct IosMemPart *ios_partition_seki;
extern void *iosMallocDebugNoAssert(struct IosMemPart *part, int size, const char *file, int line);
extern void iosFree(void *p);

#define TITLE_THEME 56
#define FADE_STEP 1024       /* op.c's step when the demo leaves the title */
#define SE_BANK 11           /* SqEntry.bank of an effect bank */
#define STREAM_BANK 17       /* of a stream */
#define SPU_SEG_TOP 0x1D9020 /* s_init.c: the top of segment 1 */
#define MAX_EVICT 8

#define N_ADPCM 105
#define N_SEFILE 104
#define N_SEDEF 1426
#define N_SELIST 3837
#define N_SEENV 425
#define N_STAGE 106

static const GalleryItem *s_cur;
/* streams */
static struct SqEntry *s_stream;
static int s_streamNo, s_wantStream, s_streamPending;
static int s_titleState; /* 0 untouched, 1 fading, 2 closed by the gallery */
static int s_titleWas;
/* effects */
static int s_seId = -1;
static struct SqEntry *s_bank;
static void *s_bankHd;

static struct {
    int num, seg, addr;
    void *hd;
} s_evict[MAX_EVICT];

static int s_nEvict, s_evicted;

/* pause and position */
static int s_pausedStream;
static unsigned long long s_seFrame; /* the effect's request, audio frames */
static unsigned long long s_seBusy;  /* the voices sounding before it */
static float s_seTotal;              /* its sample's seconds, 0 until found */
static int s_seLoops;

#define SPU_RATE 48000.0
#define STREAM_SPU_HALF 0x2000 /* a stream voice's SPU ring (adpcmDataSet's 0x4000) halved */
#define SE_FIND_FRAMES 96000u  /* how long the effect's voices are looked for (2 s) */

static unsigned long long audioFrame(void)
{
    return (unsigned long long)spu2_time();
}

/* --- the tables ---------------------------------------------------------------- */

static int onDisc(int no)
{
    IcoVfs *vfs = ico_vfs_disc();
    if (vfs == NULL) {
        return 1;
    }
    const char *p = strrchr(adpcmFile[no].path, '/');
    return ico_df_has(vfs, p ? p + 1 : adpcmFile[no].path) == 1;
}

static int tables(GalleryTables *t)
{
    if (adpcmFile[1].channels == 0) {
        return -1; /* the loader has not filled them */
    }
    t->adpcm = adpcmFile;
    t->adpcmCount = N_ADPCM;
    t->seFile = seFile;
    t->seFileCount = N_SEFILE;
    t->seDef = seDef;
    t->seDefCount = N_SEDEF;
    t->seList = seList;
    t->seListCount = N_SELIST;
    t->seEnv = seEnv;
    t->seEnvCount = N_SEENV;
    t->stage = stageData;
    t->stageCount = N_STAGE;
    t->streamOnDisc = onDisc;
    return 0;
}

/* --- streams ------------------------------------------------------------------- */

static int entryIs(const struct SqEntry *e, int no, int bank)
{
    return e != NULL && e->num == no && e->bank == bank;
}

/* our stream's handle still names an open stream of ours */
static int streamAlive(void)
{
    return s_stream != NULL && entryIs(s_stream, s_streamNo, STREAM_BANK) &&
           scpAdpcmCloseChkFunc(&s_stream) != 0;
}

static void titleTick(void)
{
    if (s_titleState == 1 && (titleAdpcm == NULL || scpAdpcmCloseChkFunc(&titleAdpcm) == 0)) {
        titleAdpcm = NULL; /* as op.c does after its fade: actTitleShortCut waits on it */
        s_titleState = 2;
        fprintf(stderr, "gallery: the title theme has faded out\n");
    }
}

static void titleFade(void)
{
    if (s_titleState != 0) {
        return;
    }
    if (titleAdpcm != NULL && entryIs(titleAdpcm, TITLE_THEME, STREAM_BANK) &&
        scpAdpcmCloseChkFunc(&titleAdpcm) != 0) {
        s_titleWas = 1;
        scpAdpcmFadeCloseFunc(&titleAdpcm, FADE_STEP);
        s_titleState = 1;
    } else {
        s_titleState = 2;
    }
}

/* --- effects -------------------------------------------------------------------- */

static struct SqEntry *resident(int no)
{
    int key = (no & 0xFFFF) | SE_BANK << 16;
    return soundDataAreaSearch(&key);
}

/* the end of segment 0 (the common banks) in the SPU buffer */
static int seg0End(void)
{
    int end = 0x5010;
    for (int b = 0; b < N_SEFILE; b++) {
        struct SqEntry *e = resident(b);
        if (e && e->mode == 0 && e->seg == 0 && e->spu.buf.addr + e->spu.buf.size > end) {
            end = e->spu.buf.addr + e->spu.buf.size;
        }
    }
    return end;
}

static void *readMember(const char *name, uint32_t *size, struct IosMemPart *part)
{
    IcoDfMember m;
    IcoVfs *vfs = ico_vfs_disc();
    if (vfs == NULL || ico_df_find_member(vfs, name, &m) != 0) {
        fprintf(stderr, "gallery: failed: %s is in no pack on the disc\n", name);
        return NULL;
    }
    void *buf = iosMallocDebugNoAssert(part, (int)m.size, __FILE__, __LINE__);
    if (buf == NULL) {
        fprintf(stderr, "gallery: failed: no memory for %s (%u bytes)\n", name, m.size);
        return NULL;
    }
    if (ico_df_read_member(vfs, &m, buf) != 0) {
        fprintf(stderr, "gallery: failed: cannot read %s\n", name);
        iosFree(buf);
        return NULL;
    }
    *size = m.size;
    return buf;
}

/* reads a bank's body into the SPU buffer, then its header (as
   ReadSoundBdFile and ReadSoundHdFile); hd NULL: read it into the sound
   partition */
static struct SqEntry *loadBank(int no, int seg, void **hdInOut)
{
    uint32_t bdSize, hdSize;
    void *hd = *hdInOut;
    if (hd == NULL) {
        hd = readMember(seFile[no].hdPath, &hdSize, ios_partition_sound);
        if (hd == NULL) {
            return NULL;
        }
    }
    void *bd = readMember(seFile[no].bdPath, &bdSize, ios_partition_seki);
    if (bd == NULL) {
        if (*hdInOut == NULL) {
            iosFree(hd);
        }
        return NULL;
    }
    soundBDDataSet(bd, no, SE_BANK, 0, seg, (int)bdSize);
    iosFree(bd);
    struct SqEntry *e = soundHDDataSet(hd, no, SE_BANK, 0, seg);
    *hdInOut = hd;
    return e;
}

static void evictTitleBanks(void)
{
    s_nEvict = 0;
    for (int b = 0; b < N_SEFILE; b++) {
        struct SqEntry *e = resident(b);
        if (e) {
            fprintf(stderr, "gallery: resident bank %d (%s): segment %d, SPU 0x%X, %d bytes\n", b,
                    seFile[b].bdPath, e->seg, e->spu.buf.addr, e->spu.buf.size);
        }
        if (e && e->mode == 0 && e->seg != 0 && s_nEvict < MAX_EVICT) {
            s_evict[s_nEvict].num = b;
            s_evict[s_nEvict].seg = e->seg;
            s_evict[s_nEvict].addr = e->spu.buf.addr;
            s_evict[s_nEvict].hd = e->hd;
            s_nEvict++;
            soundSeReqStop(e);
        }
    }
    soundDataSegAllClose(1, 0);
    soundDataSegAllClose(2, 0);
    s_evicted = 1;
    fprintf(stderr, "gallery: closed the title's %d stage banks for the gallery's\n", s_nEvict);
}

static void closeGalleryBank(void)
{
    if (s_bank == NULL) {
        return;
    }
    soundSeReqStop(s_bank);
    soundDataSegAllClose(1, 0);
    iosFree(s_bankHd);
    s_bank = NULL;
    s_bankHd = NULL;
}

static int ensureBank(int no)
{
    if (resident(no) != NULL) {
        return 0;
    }
    if (ico_vfs_disc() == NULL) {
        fprintf(stderr, "gallery: failed: bank %d needs the disc\n", no);
        return -1;
    }
    IcoDfMember m;
    if (ico_df_find_member(ico_vfs_disc(), seFile[no].bdPath, &m) != 0) {
        fprintf(stderr, "gallery: failed: %s is in no pack on the disc\n", seFile[no].bdPath);
        return -1;
    }
    if (!s_evicted) {
        evictTitleBanks();
    }
    closeGalleryBank();
    int size = ((int)(m.size - 1) / 64 + 1) * 64;
    if (SPU_SEG_TOP - size < seg0End()) {
        fprintf(stderr,
                "gallery: failed: bank %s (%d bytes) does not fit beside the common banks "
                "(%d bytes free)\n",
                seFile[no].bdPath, size, SPU_SEG_TOP - seg0End());
        return -1;
    }
    void *hd = NULL;
    s_bank = loadBank(no, 1, &hd);
    if (s_bank == NULL) {
        return -1;
    }
    s_bankHd = hd;
    soundSeKindBuild();
    fprintf(stderr, "gallery: bank %d (%s) loaded at SPU 0x%X, %d bytes\n", no, seFile[no].bdPath,
            s_bank->spu.buf.addr, s_bank->spu.buf.size);
    return 0;
}

static void restoreTitleBanks(void)
{
    closeGalleryBank();
    if (!s_evicted) {
        return;
    }
    /* each segment in its allocation order: segment 1 grows down from its
       top, segment 2 up */
    for (int pass = 1; pass <= 2; pass++) {
        for (;;) {
            int pick = -1;
            for (int i = 0; i < s_nEvict; i++) {
                if (s_evict[i].seg != pass || s_evict[i].num < 0) {
                    continue;
                }
                if (pick < 0 || (pass == 1 ? s_evict[i].addr > s_evict[pick].addr
                                           : s_evict[i].addr < s_evict[pick].addr)) {
                    pick = i;
                }
            }
            if (pick < 0) {
                break;
            }
            void *hd = s_evict[pick].hd;
            struct SqEntry *e = loadBank(s_evict[pick].num, s_evict[pick].seg, &hd);
            if (e == NULL || e->spu.buf.addr != s_evict[pick].addr) {
                fprintf(stderr,
                        "gallery: failed: the title's bank %d came back at 0x%X, not 0x%X\n",
                        s_evict[pick].num, e ? e->spu.buf.addr : 0, s_evict[pick].addr);
            }
            s_evict[pick].num = -1;
        }
    }
    s_evicted = 0;
    s_nEvict = 0;
    fprintf(stderr, "gallery: the title's stage banks are back\n");
}

/* --- where the item is --------------------------------------------------------- */

static uint16_t voiceEntry(int slot, uint16_t param)
{
    return (uint16_t)(param | SPU2_SD_VOICE(slot / 24, slot % 24));
}

static int voiceSounding(int slot)
{
    return spu2_sd_get_param(voiceEntry(slot, SPU2_SD_VPARAM_ENVX)) != 0;
}

static unsigned long long soundingVoices(void)
{
    unsigned long long m = 0;
    for (int v = 0; v < 48; v++) {
        if (voiceSounding(v)) {
            m |= 1ull << v;
        }
    }
    return m;
}

/* a sample's 16-byte blocks from addr to the one with the end flag; *loops
   when that block also repeats */
static int sampleBlocks(uint32_t addr, int *loops)
{
    const uint8_t *ram = spu2_ram();
    int n = 0;
    *loops = 0;
    for (uint32_t a = addr & ~15u; a + 16 <= 0x200000u; a += 16) {
        n++;
        if (ram[a + 1] & 1) {
            *loops = (ram[a + 1] & 2) != 0;
            break;
        }
    }
    return n;
}

/* the effect's voices, once they are keyed: its sample's length */
static void findEffectVoices(void)
{
    struct SqEntry *b = resident(s_cur->bank);
    if (b == NULL || s_seTotal > 0.0f) {
        return;
    }
    uint32_t lo = (uint32_t)b->spu.buf.addr, hi = lo + (uint32_t)b->spu.buf.size;
    for (int v = 0; v < 48; v++) {
        if ((s_seBusy >> v) & 1 || !voiceSounding(v)) {
            continue;
        }
        uint32_t ssa = spu2_sd_get_addr(voiceEntry(v, SPU2_SD_VADDR_SSA));
        unsigned pitch = spu2_sd_get_param(voiceEntry(v, SPU2_SD_VPARAM_PITCH));
        if (ssa < lo || ssa >= hi || pitch == 0) {
            continue;
        }
        int loops;
        int blocks = sampleBlocks(ssa, &loops);
        float t = (float)((double)blocks * 28.0 * 4096.0 / ((double)pitch * SPU_RATE));
        if (t > s_seTotal) {
            s_seTotal = t;
            s_seLoops = loops;
        }
    }
}

static double streamSeconds(int no, double bytes)
{
    int ch = adpcmFile[no].channels > 0 ? adpcmFile[no].channels : 1;
    int hz = adpcmFile[no].pitch > 0 ? adpcmFile[no].pitch : 44100;
    return bytes / ch / 16.0 * 28.0 / hz;
}

static int playSe(const GalleryItem *it)
{
    int def = it->key, bank = it->bank;
    if (ensureBank(bank) != 0) {
        return -1;
    }
    /* the kind's row in this bank, whatever else is loaded */
    int idx = seDef[def].kind;
    int row = -1;
    for (int j = 0; j < N_SELIST; j++) {
        if (seList[j].num == bank && seList[j].idx == idx) {
            row = j;
            break;
        }
    }
    if (row < 0) {
        fprintf(stderr, "gallery: failed: bank %d has no row for effect %d\n", bank, def);
        return -1;
    }
    seKind[idx] = (unsigned short)row;
    s_seBusy = soundingVoices();
    s_seFrame = audioFrame();
    s_seTotal = 0.0f;
    s_seLoops = 0;
    s_seId = soundSeDefPlay(def, 0xFFFFFFFFu, NULL, 0);
    if (s_seId < 0) {
        fprintf(stderr, "gallery: failed: soundSeDefPlay(%d) returned %d\n", def, s_seId);
        s_seId = -1;
        return -1;
    }
    fprintf(stderr,
            "gallery: effect %d from bank %d (program %d, tone %d) keyed at audio frame %llu\n",
            def, bank, seList[row].prog, seList[row].tone, audioFrame());
    return 0;
}

/* --- the engine ------------------------------------------------------------------ */

static void stopAll(void)
{
    if (s_pausedStream) {
        adpcmPauseRequest(0);
        s_pausedStream = 0;
    }
    s_seTotal = 0.0f;
    s_seLoops = 0;
    if (s_wantStream) {
        s_wantStream = 0;
    }
    if (s_streamPending && s_stream == NULL) {
        scpAdpcmCloseFunc(&s_stream); /* cancels the request the daemon holds */
    } else if (streamAlive()) {
        scpAdpcmCloseFunc(&s_stream);
    }
    s_streamPending = 0;
    s_stream = NULL;
    if (s_seId >= 0) {
        soundSeDefStop(s_seId);
        s_seId = -1;
    }
    s_cur = NULL;
}

static void enter(void)
{
    s_titleState = 0;
    s_titleWas = 0;
}

static void leave(void)
{
    stopAll();
    restoreTitleBanks();
    soundSeKindBuild(); /* playSe pointed kinds at the gallery's rows */
    if (s_titleWas) {
        scpAdpcmPlayRequestFunc(TITLE_THEME, &titleAdpcm, 0, 0, 1);
        fprintf(stderr, "gallery: the title theme is requested again\n");
    }
    s_titleState = 0;
    s_titleWas = 0;
}

static int play(const GalleryItem *it)
{
    stopAll();
    if (it->kind == GAL_K_STREAM) {
        titleFade();
        s_wantStream = it->key;
        s_cur = it;
        return 0;
    }
    if (it->kind == GAL_K_SE) {
        titleFade(); /* the effect alone, as a stream is */
        if (playSe(it) != 0) {
            return -1;
        }
        s_cur = it;
        return 0;
    }
    return -1;
}

static void stop(void)
{
    stopAll();
}

static void tick(void)
{
    titleTick();
    if (s_wantStream && s_titleState == 2 && scpAdpcmPlayRequestNum() == 0 &&
        AdpcmFreeAreaGet() > 0) {
        s_streamNo = s_wantStream;
        s_wantStream = 0;
        s_stream = NULL;
        scpAdpcmPlayRequestFunc(s_streamNo, &s_stream, 1, 1, 1);
        s_streamPending = 1;
    }
    if (s_streamPending && s_stream != NULL) {
        s_streamPending = 0;
        fprintf(
            stderr, "gallery: stream %d (%s) opened on SPU voices %d and %d at audio frame %llu\n",
            s_streamNo, adpcmFile[s_streamNo].path, s_stream->stream ? s_stream->stream->ch[0] : -1,
            s_stream->stream && s_stream->stream->n > 1 ? s_stream->stream->ch[1] : -1,
            audioFrame());
    }
}

static const GalleryItem *playing(void)
{
    if (s_cur == NULL) {
        return NULL;
    }
    if (s_cur->kind == GAL_K_STREAM) {
        return s_wantStream || s_streamPending || streamAlive() ? s_cur : NULL;
    }
    return s_seId >= 0 && soundSeDefVolumeRateGet(s_seId) > 0.0f ? s_cur : NULL;
}

static int pauseItem(int on)
{
    if (on) {
        if (s_cur == NULL || s_cur->kind != GAL_K_STREAM || s_streamPending || s_wantStream ||
            !streamAlive()) {
            return -1;
        }
        adpcmPauseRequest(1);
        s_pausedStream = 1;
        return 0;
    }
    if (!s_pausedStream) {
        return -1;
    }
    adpcmPauseRequest(0);
    s_pausedStream = 0;
    return 0;
}

static int positionOf(float *elapsed, float *total)
{
    *elapsed = *total = 0.0f;
    if (playing() == NULL) {
        return -1;
    }
    if (s_cur->kind == GAL_K_STREAM) {
        int no = s_cur->key;
        const double size = (double)adpcmFile[no].sectors * 2048.0;
        *total = (float)streamSeconds(no, size);
        if (s_stream != NULL && !s_streamPending && streamAlive() && s_stream->stream != NULL) {
            const AdpcmStream *st = s_stream->stream;
            double played = (double)st->dataSize - (double)st->remain -
                            1.5 * STREAM_SPU_HALF * (st->n > 0 ? st->n : 1);
            played = played < 0.0 ? 0.0 : played > size ? size : played;
            *elapsed = (float)streamSeconds(no, played);
        }
        return 0;
    }
    if (audioFrame() - s_seFrame < SE_FIND_FRAMES) {
        findEffectVoices();
    }
    double t = (double)(audioFrame() - s_seFrame) / SPU_RATE;
    if (s_seLoops && s_seTotal > 0.0f) {
        t = t - (double)s_seTotal * (double)(long long)(t / (double)s_seTotal);
    }
    *elapsed = (float)t;
    *total = s_seTotal;
    if (*total > 0.0f && *elapsed > *total) {
        *elapsed = *total;
    }
    return 0;
}

static const GalleryEngine kEngine = {tables, enter,   leave,     play,      stop,
                                      tick,   playing, pauseItem, positionOf};

void gallery_EngineInstall(void)
{
    gallery_SetEngine(&kEngine);
}
