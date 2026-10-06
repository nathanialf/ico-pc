/*
 * port/audio/stream.c
 *
 * SNDN2DRV's two stream engines on the host (docs/research/sndn2drv.md,
 * "ADPCM streams" and "PCM streams"; the handler addresses in the comments
 * are R1's).  Both read their data where the game put it, in the IOP RAM
 * stand-in (port/data/iop_ram.h): the ADPCM `.int` rings the game fills
 * with sceCdReadIOPm through its background reader (fumi/sound/
 * adpcm_init.c, fumi/ios/cdvd.c), and the movie player's PCM buffer it
 * fills by SIF DMA (ito/mpeg/mv_audiodec.c).
 *
 * ADPCM: per stream voice, the IOP ring holds the file's interleave (n = 1,
 * 2 or 4 channels, 0x800 / n bytes of each per 0x800-byte sector); the SPU
 * ring (16 KB in the game) is played as one loop in two halves.  Every
 * tick the scheduler reads each voice's NAX and, when the voice has moved
 * into the other half, queues a FILL of the half it left; the queue runner
 * then does at most one transfer per tick on DMA channel 1: it
 * de-interleaves the voice's channel into the staging buffer, patches the
 * first and last block's loop flags so the ring loops and LSA re-latches
 * at its start, and sends it to the SPU2.  The IOP read offset the EE polls
 * moves on by half the SPU ring times n when the next tick finds the
 * channel idle.
 *
 * PCM: core 1's AutoDMA input reads the staging buffer as a 2 x 0x400 ring;
 * at each half-done interrupt the finished half is zeroed and the 16 PCM
 * channels are mixed into it with 16-bit wrapping adds.
 */
#include <stdio.h>
#include <string.h>

#include "iop_ram.h"
#include "sndn2_internal.h"
#include "spu2.h"
#include "spu2_sd.h"

static uint8_t staging[SNDN2_STAGING_SIZE]; /* the IRX's 0x5140 */

uint8_t *sndn2_staging_buf(void)
{
    return staging;
}

/* --- ADPCM ------------------------------------------------------------------- */

/* The 36-byte voice record at 0x7148 + 36 * slot. */
typedef struct StVoice {
    uint32_t active;    /* bit 0 set by the key-on event */
    uint32_t n;         /* channels in the IOP interleave */
    uint32_t spu_start; /* SPU ring (byte address) */
    uint32_t spu_size;
    uint32_t iop_start; /* this channel's first chunk in the IOP ring */
    uint32_t iop_size;
    uint32_t read_off; /* reported to the EE */
    uint32_t half;     /* the half NAX is in (1, 2) */
    uint32_t sched;    /* the half last scheduled */
} StVoice;

enum { EV_NONE = 0, EV_FILL = 1, EV_KEYON = 2 };

/* One 32-byte queue entry at 0x7808. */
typedef struct StEvent {
    uint32_t kind;
    uint32_t slot;
    uint32_t half;
    uint32_t n;
    uint32_t src; /* IOP address */
    uint32_t dst; /* SPU byte address */
    uint32_t len;
    uint32_t mask[2]; /* KEYON: core 0, core 1 */
} StEvent;

#define QUEUE 128

static StVoice st[SNDN2_SLOTS];
static StEvent queue[QUEUE];
static uint32_t q_write; /* 0x3D24 */
static uint32_t q_read;  /* 0x3D28 */
static uint32_t pending; /* 0x3D30: (core << 8 | voice) + 0x10000, 0 when none */

static int logged_overflow, logged_iop;

/* Every slot whose bit is set in the core 0 / core 1 masks. */
#define FOR_MASKED(slot, m0, m1)                                                                   \
    for ((slot) = 0; (slot) < SNDN2_SLOTS; (slot)++)                                               \
        if ((((slot) < 24 ? (m0) >> (slot) : (m1) >> ((slot) - 24)) & 1u) != 0)

/* 0x3334: a full queue (128 entries ahead of the reader) refuses the new
   event, which is lost; the IRX returns -1 and no caller checks it. */
static void enqueue(const StEvent *e)
{
    if (q_write - q_read >= QUEUE) {
        sndn2_log_once(&logged_overflow, "ADPCM stream event queue full: the new event is dropped");
        return;
    }
    queue[q_write & (QUEUE - 1)] = *e;
    q_write++;
}

/* 0x248C (docs/research/sndn2drv.md, "Stream cancel"), for the voice
   `core`, `voice`: a pending fill of that voice is forgotten, so the read
   offset does not advance for the transfer in flight; every queued FILL of
   the voice is zeroed; a queued KEYON keeps its place but loses the voice's
   bit.  For a core 0 voice the IRX clears bit `voice` in both masks (its
   switch on the core falls from case 0 into case 1), so the core 1 voice
   with the same number loses a queued key-on too.  The IRX walks all 128
   entries; consumed ones are zero. */
static void cancel(uint32_t slot)
{
    uint32_t core = slot / 24;
    uint32_t voice = slot % 24;
    uint32_t i;

    if (((pending >> 8) & 0xFF) == core && (pending & 0xFF) == voice) {
        pending = 0;
    }
    for (i = 0; i < QUEUE; i++) {
        StEvent *e = &queue[i];

        if (e->kind == EV_FILL && e->slot == slot) {
            memset(e, 0, sizeof(*e));
        } else if (e->kind == EV_KEYON) {
            if (core == 0) {
                e->mask[0] &= ~(1u << voice);
            }
            e->mask[1] &= ~(1u << voice);
        }
    }
}

void st_adpcm_reset(void)
{
    memset(st, 0, sizeof(st));
    memset(queue, 0, sizeof(queue));
    memset(staging, 0, sizeof(staging));
    q_write = q_read = 0;
    pending = 0;
    logged_overflow = logged_iop = 0;
    st_pcm_close(0xFFFFFFFFu);
}

void st_adpcm_init(void) /* 0x190C */
{
    memset(st, 0, sizeof(st));
    memset(queue, 0, sizeof(queue));
    st_pcm_close(0xFFFFFFFFu); /* and the 16 PCM channel records */
}

void st_adpcm_open(uint32_t w1, uint32_t w2, uint32_t w3) /* 0x1958 */
{
    uint32_t slot = w1 >> 24;
    StVoice *v;

    if (slot >= SNDN2_SLOTS) {
        return;
    }
    v = &st[slot];
    memset(v, 0, sizeof(*v));
    v->n = (w1 >> 16) & 0xFF;
    v->spu_size = w1 & 0xFF00; /* AdpcmChReq +0x14 (the decomp's `vol`) */
    v->spu_start = (w1 & 0xFF) << 16 | w2 >> 16;
    v->iop_size = (w2 & 0xFFFF) << 8 | w3 >> 24;
    v->iop_start = w3 & 0xFFFFFF;
    spu2_sd_set_addr((uint16_t)(vsel(slot, SPU2_SD_VADDR_SSA) | 0x40), v->spu_start);
    spu2_sd_set_param(vsel(slot, SPU2_SD_VPARAM_ADSR1), 0x8080);
    spu2_sd_set_param(vsel(slot, SPU2_SD_VPARAM_ADSR2), 0x808A);
}

void st_adpcm_close(uint32_t slot) /* 0x1CD0 */
{
    if (slot < SNDN2_SLOTS) {
        cancel(slot);
        memset(&st[slot], 0, sizeof(st[slot]));
    }
}

void st_adpcm_volume(uint32_t m0, uint32_t m1, uint32_t w3) /* 0x1DB4 */
{
    uint32_t slot;

    FOR_MASKED(slot, m0, m1)
    {
        spu2_sd_set_param(vsel(slot, SPU2_SD_VPARAM_VOLL), (uint16_t)(w3 >> 16));
        spu2_sd_set_param(vsel(slot, SPU2_SD_VPARAM_VOLR), (uint16_t)(w3 & 0xFFFF));
    }
}

void st_adpcm_pitch(uint32_t m0, uint32_t m1, uint32_t w3) /* 0x1EEC */
{
    uint32_t slot;
    int32_t p = (int32_t)(w3 << 12) / 48000;

    FOR_MASKED(slot, m0, m1)
    {
        spu2_sd_set_param(vsel(slot, SPU2_SD_VPARAM_PITCH), (uint16_t)p);
    }
}

static void fill_event(uint32_t slot, uint32_t half, uint32_t dst)
{
    StEvent e;
    StVoice *v = &st[slot];

    memset(&e, 0, sizeof(e));
    e.kind = EV_FILL;
    e.slot = slot;
    e.half = half;
    e.n = v->n;
    e.src = v->iop_start + v->read_off;
    e.dst = dst;
    e.len = v->spu_size / 2;
    enqueue(&e);
}

void st_adpcm_play(uint32_t m0, uint32_t m1) /* 0x2008 */
{
    StEvent e;
    uint32_t slot;

    FOR_MASKED(slot, m0, m1)
    {
        fill_event(slot, 2, st[slot].spu_start);
    }
    memset(&e, 0, sizeof(e));
    e.kind = EV_KEYON;
    e.mask[0] = m0 & 0xFFFFFF;
    e.mask[1] = m1 & 0xFFFFFF;
    enqueue(&e);
}

void st_adpcm_stop(uint32_t m0, uint32_t m1) /* 0x2250 */
{
    uint32_t slot;

    FOR_MASKED(slot, m0, m1)
    {
        cancel(slot);
        st[slot].active = 0;
        st[slot].read_off = 0;
        st[slot].half = 0;
        st[slot].sched = 0;
    }
    spu2_sd_set_switch(SPU2_SD_SWITCH_KOFF | 0, m0);
    spu2_sd_set_switch(SPU2_SD_SWITCH_KOFF | 1, m1);
}

/* 0x2778: queue a fill for every voice that has moved into the other half
   of its SPU ring.  NAX exactly on a boundary leaves the state alone. */
static void schedule(void)
{
    uint32_t slot;

    for (slot = 0; slot < SNDN2_SLOTS; slot++) {
        StVoice *v = &st[slot];
        uint32_t nax;
        uint32_t mid;
        uint32_t end;
        uint32_t cur;

        if ((v->active & 1) == 0) {
            continue;
        }
        nax = spu2_sd_get_addr((uint16_t)(vsel(slot, SPU2_SD_VADDR_NAX) | 0x40));
        mid = v->spu_start + v->spu_size / 2;
        end = v->spu_start + v->spu_size;
        cur = v->half;
        if (v->spu_start < nax && nax < mid) {
            cur = 1;
        } else if (mid < nax && nax < end) {
            cur = 2;
        }
        v->half = cur;
        if (cur != v->sched) {
            fill_event(slot, cur, cur == 1 ? mid : v->spu_start);
            v->sched = cur;
        }
    }
}

/* De-interleave one channel's half ring into the staging buffer, patch the
   loop flags (psx-spx: bit 0 end, bit 1 repeat, bit 2 loop start) and send
   it on DMA channel 1. */
static void run_fill(const StEvent *e)
{
    uint32_t chunk = e->n != 0 ? 0x800 / e->n : 0x800;
    uint32_t len = e->len > SNDN2_STAGING_SIZE ? SNDN2_STAGING_SIZE : e->len;
    uint32_t k;

    for (k = 0; k < len / chunk; k++) {
        uint32_t src = e->src + k * 0x800;

        if (ico_iop_range_ok(src, chunk)) {
            memcpy(staging + k * chunk, ico_iop_ptr(src), chunk);
        } else {
            sndn2_log_once(&logged_iop, "ADPCM fill reads IOP 0x%X outside IOP RAM: zeros", src);
            memset(staging + k * chunk, 0, chunk);
        }
    }
    if (len >= 16) {
        if (e->half == 1) {
            staging[1] = 2;
            staging[len - 16 + 1] = 3;
        } else {
            staging[1] = 6;
            staging[len - 16 + 1] = 2;
        }
    }
    spu2_sd_voice_trans(1, SPU2_SD_TRANS_WRITE, staging, e->dst, len);
    pending = ((e->slot / 24) << 8 | (e->slot % 24)) + 0x10000;
}

/* 0x2A54: once per tick while DMA channel 1 is idle. */
static void run_queue(void)
{
    if (spu2_sd_voice_trans_status(1, 0) != 1) {
        return;
    }
    if (pending != 0) {
        uint32_t slot = ((pending >> 8) & 0xFF) * 24 + (pending & 0xFF);
        StVoice *v = &st[slot % SNDN2_SLOTS];

        if (v->iop_size != 0) {
            v->read_off = (v->read_off + (v->spu_size / 2) * v->n) % v->iop_size;
        }
        pending = 0;
    }
    while (q_read != q_write) {
        StEvent *e = &queue[q_read & (QUEUE - 1)];
        StEvent ev = *e;
        uint32_t slot;

        memset(e, 0, sizeof(*e));
        q_read++;
        if (ev.kind == EV_FILL) {
            run_fill(&ev);
            return;
        }
        if (ev.kind == EV_KEYON) {
            spu2_sd_set_switch(SPU2_SD_SWITCH_KON | 0, ev.mask[0]);
            spu2_sd_set_switch(SPU2_SD_SWITCH_KON | 1, ev.mask[1]);
            FOR_MASKED(slot, ev.mask[0], ev.mask[1])
            {
                st[slot].active |= 1;
                st[slot].half = 0;
                st[slot].sched = 0;
            }
        }
    }
}

void st_adpcm_tick(void)
{
    schedule();
    run_queue();
}

uint32_t st_adpcm_read_off(int slot)
{
    return slot >= 0 && slot < SNDN2_SLOTS ? st[slot].read_off : 0;
}

/* --- PCM ----------------------------------------------------------------------- */

#define PCM_NO_STOP 0x200000u

/* The 32-byte channel record at 0x8808 + 32 * ch. */
typedef struct StPcm {
    uint32_t stop_off;
    uint32_t buf;  /* IOP address */
    uint32_t size; /* wrap point */
    uint32_t read_off;
    uint32_t ctl; /* low 16: bytes per callback; high 16: playing, the sample-step shift */
    uint32_t vol_l;
    uint32_t vol_r;
    uint32_t play_bits; /* the open's id & 0xFF0000 */
} StPcm;

static StPcm pcm[SNDN2_PCM_CHANNELS];

#define FOR_PCM(ch, mask)                                                                          \
    for ((ch) = 0; (ch) < SNDN2_PCM_CHANNELS; (ch)++)                                              \
        if ((((mask) >> (ch)) & 1u) != 0)

/* The mixer (0x3480) over one 0x400-byte half: 256 left samples, then 256
   right. */
static void pcm_mix(uint8_t *half)
{
    int ch;

    for (ch = 0; ch < SNDN2_PCM_CHANNELS; ch++) {
        StPcm *c = &pcm[ch];
        uint32_t shift = c->ctl >> 16;
        const uint8_t *src;
        int i;

        if (shift == 0) {
            continue;
        }
        if (c->read_off == c->stop_off) {
            c->ctl &= 0xFFFF;
            continue;
        }
        if (c->read_off == c->size) {
            c->read_off = 0;
        }
        src = ico_iop_range_ok(c->buf + c->read_off, 0x200) ? ico_iop_ptr(c->buf + c->read_off)
                                                            : NULL;
        for (i = 0; src != NULL && i < 256; i++) {
            /* srlv: the shift amount's low 5 bits */
            uint32_t at = (((uint32_t)i * 2) >> (shift & 31)) * 2;
            int32_t x = (int16_t)(src[at] | src[at + 1] << 8);
            uint8_t *l = half + i * 2;
            uint8_t *r = half + 0x200 + i * 2;
            uint16_t lv = (uint16_t)(l[0] | l[1] << 8);
            uint16_t rv = (uint16_t)(r[0] | r[1] << 8);

            /* mult (its low word), srl 15, add, sh: the low 16 bits,
               wrapping; unsigned, so a volume word the EE sent out of range
               wraps as on the IOP instead of overflowing */
            lv = (uint16_t)(lv + ((c->vol_l * (uint32_t)x) >> 15));
            rv = (uint16_t)(rv + ((c->vol_r * (uint32_t)x) >> 15));
            l[0] = (uint8_t)lv;
            l[1] = (uint8_t)(lv >> 8);
            r[0] = (uint8_t)rv;
            r[1] = (uint8_t)(rv >> 8);
        }
        c->read_off += c->ctl & 0xFFFF;
    }
}

/* Core 1's AutoDMA half-done interrupt (0x558): refill the half the input
   is not reading. */
static void pcm_half_done(int core, int half_finished, void *user)
{
    uint8_t *half = staging + (spu2_memin_half(core) ? 0 : 0x400);

    (void)half_finished;
    (void)user;
    memset(half, 0, 0x400);
    pcm_mix(half);
}

void st_pcm_init(void) /* 0x1360 */
{
    memset(staging, 0, sizeof(staging));
    memset(pcm, 0, sizeof(pcm));
    spu2_memin_start(1, staging, pcm_half_done, NULL);
    spu2_sd_set_param(SPU2_SD_PARAM_BVOLL | 1, 0x7FFF);
    spu2_sd_set_param(SPU2_SD_PARAM_BVOLR | 1, 0x7FFF);
}

void st_pcm_quit(void) /* 0x13D0 */
{
    spu2_memin_stop(1);
    spu2_sd_set_param(SPU2_SD_PARAM_BVOLL | 1, 0);
    spu2_sd_set_param(SPU2_SD_PARAM_BVOLR | 1, 0);
}

void st_pcm_open(uint32_t id, uint32_t w2, uint32_t w3) /* 0x1410 */
{
    StPcm *c = &pcm[(id >> 24) & 0xF];

    c->stop_off = PCM_NO_STOP;
    c->ctl = id & 0xFFFF;
    c->play_bits = id & 0xFF0000;
    c->buf = w2;
    c->size = w3; /* the read offset is not reset */
}

void st_pcm_close(uint32_t ch) /* 0x1500; all 16 for 0xFFFFFFFF (0x3C) */
{
    if (ch == 0xFFFFFFFFu) {
        memset(pcm, 0, sizeof(pcm));
    } else if (ch < SNDN2_PCM_CHANNELS) {
        memset(&pcm[ch], 0, sizeof(pcm[ch]));
    }
}

void st_pcm_volume(uint32_t mask, uint32_t l, uint32_t r) /* 0x178C */
{
    int ch;

    FOR_PCM(ch, mask)
    {
        pcm[ch].vol_l = l;
        pcm[ch].vol_r = r;
    }
}

void st_pcm_play(uint32_t mask) /* 0x15B8 */
{
    int ch;

    FOR_PCM(ch, mask)
    {
        pcm[ch].ctl |= pcm[ch].play_bits;
    }
}

void st_pcm_stop(uint32_t mask) /* 0x1678 */
{
    int ch;

    FOR_PCM(ch, mask)
    {
        pcm[ch].ctl &= ~pcm[ch].play_bits;
    }
}

void st_pcm_lseek(uint32_t ch, uint32_t off) /* 0x1754 */
{
    if (ch < SNDN2_PCM_CHANNELS) {
        pcm[ch].read_off = off;
    }
}

void st_pcm_effect(uint32_t id) /* 0x153C */
{
    if (id == 4) {
        spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 0, 0xFFF0);
        spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 1, 0xFFFC);
    } else if (id == 8) {
        spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 0, 0xFFC0);
        spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 1, 0xFFCC);
    }
}

void st_pcm_bufmode(uint32_t mask, uint32_t off, uint32_t mode) /* 0x1840 */
{
    int ch;

    FOR_PCM(ch, mask)
    {
        pcm[ch].stop_off = mode == 1 ? off : PCM_NO_STOP;
    }
}

uint32_t st_pcm_read_off(int ch)
{
    return ch >= 0 && ch < SNDN2_PCM_CHANNELS ? pcm[ch].read_off : 0;
}
