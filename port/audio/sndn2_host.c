/*
 * port/audio/sndn2_host.c
 *
 * SNDN2DRV.IRX on the host (sndn2_host.h): the RPC entry points, the packet
 * dispatcher, the reply page and the pitch table.  Every command and its
 * libsd calls follow docs/research/sndn2drv.md (R1, read off the user's
 * own IRX; the handler addresses in the comments below are R1's), turned
 * into calls on the software SPU2's libsd front end (spu2_sd.h).  The
 * stream engines are in stream.c.  Clean-room: no code from the IRX, no
 * emulator code.
 */
#include "sndn2_host.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "iop_ram.h"
#include "libsd_irx.h"
#include "sif_host.h"
#include "sndn2_internal.h"
#include "spu2.h"
#include "spu2_sd.h"
#include "vfs.h"

/* SNDN2DRV.IRX on the PAL disc (R1, "What was examined"); the table's file
   offset and size (R1, "Pitch"). */
#define IRX_PATH "SNDN2DRV.IRX"
#define IRX_SIZE 20941u
#define IRX_PITCH_OFFSET 0x3900u
#define IRX_PITCH_BYTES (ICO_SNDN2_PITCH_ENTRIES * 2u)

static struct {
    int registered;
    unsigned int page_counter;              /* 0x3D20 */
    uint8_t pages[2][ICO_SNDN2_REPLY_SIZE]; /* 0x4D40 */
    uint32_t xfer_counter;                  /* 0x3D2C */
    uint8_t ret_page[ICO_SNDN2_REPLY_SIZE]; /* 0x7140 and zeros (a non-tick reply) */
    const uint8_t *last_reply;
    spu2_sd_effect_attr attr; /* the dispatcher frame's attr (R1 notes) */
    uint16_t pitch[ICO_SNDN2_PITCH_ENTRIES];
    int pitch_source;
    unsigned pitch_clamps;
} H;

static int logged_cmd, logged_iop, logged_clamp, logged_sub;

void sndn2_log_once(int *flag, const char *fmt, ...)
{
    va_list ap;

    if (*flag) {
        return;
    }
    *flag = 1;
    va_start(ap, fmt);
    fputs("sndn2: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* --- Pitch ----------------------------------------------------------------- */

uint16_t ico_sndn2_pitch_formula(int i)
{
    return (uint16_t)floor(4096.0 * exp2((double)(i - 208) / 192.0));
}

void ico_sndn2_pitch_set(const uint16_t *table)
{
    int i;

    if (table != NULL) {
        memcpy(H.pitch, table, sizeof(H.pitch));
        H.pitch_source = ICO_SNDN2_PITCH_IRX;
        return;
    }
    for (i = 0; i < ICO_SNDN2_PITCH_ENTRIES; i++) {
        H.pitch[i] = ico_sndn2_pitch_formula(i);
    }
    H.pitch_source = ICO_SNDN2_PITCH_FORMULA;
}

int ico_sndn2_pitch_load(void)
{
    IcoVfs *disc = ico_vfs_disc();
    IcoVfsFile f;
    uint8_t raw[IRX_PITCH_BYTES];
    uint16_t t[ICO_SNDN2_PITCH_ENTRIES];
    const char *why = NULL;
    int i;

    if (disc == NULL) {
        why = "no disc is mounted";
    } else if (ico_vfs_open(disc, IRX_PATH, &f) != 0) {
        why = "the disc has no " IRX_PATH;
    } else if (ico_vfs_size(&f) != IRX_SIZE) {
        why = IRX_PATH " is not the PAL disc's (size)";
    } else if (ico_vfs_read(&f, IRX_PITCH_OFFSET, raw, sizeof(raw)) != (int64_t)sizeof(raw)) {
        why = IRX_PATH " cannot be read";
    } else {
        /* not checked for order: the disc's table has two entries below
           their predecessors (64 and 172, docs/port/AUDIO.md) */
        for (i = 0; i < ICO_SNDN2_PITCH_ENTRIES; i++) {
            t[i] = (uint16_t)(raw[2 * i] | raw[2 * i + 1] << 8);
        }
        if (t[208] != 0x1000) {
            why = "the table read from " IRX_PATH " has no unity at 208";
        }
    }
    if (why != NULL) {
        ico_sndn2_pitch_set(NULL);
        fprintf(stderr,
                "sndn2: pitch table: %s; using the formula floor(4096 * 2^((i - 208) / 192)) "
                "(DIVERGENCES.md A15)\n",
                why);
        return H.pitch_source;
    }
    ico_sndn2_pitch_set(t);
    fprintf(stderr, "sndn2: pitch table: 608 entries from the disc's " IRX_PATH "\n");
    return H.pitch_source;
}

int ico_sndn2_pitch_source(void)
{
    return H.pitch_source;
}

const uint16_t *ico_sndn2_pitch_table(void)
{
    return H.pitch;
}

unsigned ico_sndn2_pitch_clamps(void)
{
    return H.pitch_clamps;
}

/* R1, "Pitch".  The arithmetic is the IRX's 32-bit unsigned arithmetic
   (the 441/480 step by multiply-high); the table index is not checked by
   the IRX, which then reads its neighbouring .data/.bss: here it is clamped
   and counted (R1 open question 4). */
uint16_t ico_sndn2_pitch_compute(uint32_t w2, uint32_t w3)
{
    int base = (int)(w2 >> 24);
    int note = (int)((w2 >> 16) & 0xFF);
    int fine = (int8_t)(uint8_t)(w2 >> 8);
    int bend = (int)(w2 & 0xFF);
    int range = (int)(w3 >> 24);
    uint32_t scale = w3 & 0xFFFFFF;
    int d;
    int idx;
    uint32_t p;

    if (H.pitch_source == 0) {
        ico_sndn2_pitch_set(NULL);
    }
    if (note >= base) {
        d = note - base;
        idx = (d % 12) * 16 + (((bend - 64) * range) >> 2) + 208 + fine;
    } else {
        d = base - note;
        idx = (12 - d % 12) * 16 + (((bend - 64) * range) >> 2) + 208 + fine;
    }
    if (idx < 0 || idx >= ICO_SNDN2_PITCH_ENTRIES) {
        H.pitch_clamps++;
        sndn2_log_once(&logged_clamp,
                       "pitch index %d outside the table (base %d note %d fine %d bend %d range "
                       "%d): clamped (R1 open question 4)",
                       idx, base, note, fine, bend, range);
        idx = idx < 0 ? 0 : ICO_SNDN2_PITCH_ENTRIES - 1;
    }
    if (note >= base) {
        p = (uint32_t)H.pitch[idx] << (d / 12);
    } else {
        p = (uint32_t)H.pitch[idx] >> (d / 12 + 1);
    }
    p = p * 441u / 480u;
    p = (scale * p) >> 12;
    return (uint16_t)(p & 0xFFFF);
}

/* --- Transfers ---------------------------------------------------------------- */

/* DMA channel 0 done (0x508): the counter of the last issued 0x20/0x21 goes
   into both reply pages. */
static void dma0_done(int chan, void *user)
{
    (void)chan;
    (void)user;
    wr32(&H.pages[0][ICO_SNDN2_REPLY_XFER], H.xfer_counter);
    wr32(&H.pages[1][ICO_SNDN2_REPLY_XFER], H.xfer_counter);
}

/* 0x20 write / 0x21 read (0x1190, 0x1268). */
static void voice_trans(uint32_t w1, uint32_t w2, uint32_t w3, int read)
{
    uint32_t iop = (w1 & 0xFF) << 16 | w2 >> 16;
    uint32_t spu = (w2 & 0xFFFF) << 8 | w3 >> 24;
    uint32_t size = w3 & 0xFFFFFF;
    void *buf;

    H.xfer_counter = w1 >> 8;
    if (!ico_iop_range_ok(iop, size)) {
        sndn2_log_once(&logged_iop, "transfer %s IOP 0x%X size 0x%X outside IOP RAM: dropped",
                       read ? "to" : "from", iop, size);
        return;
    }
    buf = ico_iop_ptr(iop);
    /* the IRX does not check the result: a busy channel drops it (R1) */
    (void)spu2_sd_voice_trans(0, read ? SPU2_SD_TRANS_READ : SPU2_SD_TRANS_WRITE, buf, spu, size);
}

/* --- The dispatcher (0x060C) ------------------------------------------------- */

static void set_masks(uint16_t sw0, uint16_t sw1, uint32_t m0, uint32_t m1)
{
    spu2_sd_set_switch((uint16_t)(sw0 | 0), m0);
    spu2_sd_set_switch((uint16_t)(sw1 | 1), m1);
}

static void dispatch(const uint8_t *pk)
{
    uint32_t cmd = rd32(pk);
    uint32_t id = rd32(pk + 4);
    uint32_t w2 = rd32(pk + 8);
    uint32_t w3 = rd32(pk + 12);
    int core = (int)(id & 1);

    switch (cmd) {
    /* voices */
    case 0x01:
        if (id < SNDN2_SLOTS) {
            spu2_sd_set_param(vsel(id, SPU2_SD_VPARAM_VOLL), (uint16_t)w2);
            spu2_sd_set_param(vsel(id, SPU2_SD_VPARAM_VOLR), (uint16_t)w3);
        }
        break;
    case 0x02:
        if (id < SNDN2_SLOTS) {
            spu2_sd_set_param(vsel(id, SPU2_SD_VPARAM_ADSR1), (uint16_t)w2);
            spu2_sd_set_param(vsel(id, SPU2_SD_VPARAM_ADSR2), (uint16_t)w3);
        }
        break;
    case 0x03:
        if (id < SNDN2_SLOTS) {
            spu2_sd_set_addr((uint16_t)(vsel(id, SPU2_SD_VADDR_SSA) | 0x40), w2);
        }
        break;
    case 0x04:
        if (id < SNDN2_SLOTS) {
            spu2_sd_set_param(vsel(id, SPU2_SD_VPARAM_PITCH), ico_sndn2_pitch_compute(w2, w3));
        }
        break;
    case 0x0A:
        set_masks(SPU2_SD_SWITCH_KON, SPU2_SD_SWITCH_KON, w2, w3);
        break;
    case 0x0B: /* the EE's id is ignored (SgQuit) */
        set_masks(SPU2_SD_SWITCH_KOFF, SPU2_SD_SWITCH_KOFF, w2, w3);
        break;
    case 0x0C:
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 0, w2);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | 0, w2);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 1, w3);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | 1, w3);
        break;
    case 0x0D:
        set_masks(SPU2_SD_SWITCH_NON, SPU2_SD_SWITCH_NON, w2, w3);
        break;

    /* core, reverb, output */
    case 0x14:
        spu2_sd_set_addr((uint16_t)(SPU2_SD_ADDR_EEA | core), w2);
        break;
    case 0x15:
        H.attr.mode = (int)(w2 & ~(uint32_t)SPU2_SD_EFFECT_MODE_CLEAR);
        H.attr.depth_L = 0;
        H.attr.depth_R = 0;
        spu2_sd_set_effect_attr(core, &H.attr);
        spu2_sd_set_core_attr((uint16_t)(SPU2_SD_CORE_EFFECT_ENABLE | core), 1);
        break;
    case 0x16:
        spu2_sd_set_param((uint16_t)(SPU2_SD_PARAM_EVOLL | core), (uint16_t)w2);
        spu2_sd_set_param((uint16_t)(SPU2_SD_PARAM_EVOLR | core), (uint16_t)w3);
        break;
    case 0x17: /* never sent by the game (R1): the last 0x15 attr, re-applied */
    case 0x18:
        if (cmd == 0x17) {
            H.attr.delay = (int)w2;
        } else {
            H.attr.feedback = (int)w2;
        }
        spu2_sd_set_effect_attr(core, &H.attr);
        spu2_sd_set_effect_attr(core, &H.attr);
        break;
    case 0x1E: /* 0x1100 */
        spu2_sd_init((int)(id & 1));
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 0, 0);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | 0, 0);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 1, 0);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | 1, 0);
        spu2_set_trans_callback(0, dma0_done, NULL);
        break;
    case 0x1F:
        spu2_set_trans_callback(0, NULL, NULL);
        break;
    case 0x28:
        spu2_sd_set_param((uint16_t)(SPU2_SD_PARAM_MVOLL | core), (uint16_t)w2);
        spu2_sd_set_param((uint16_t)(SPU2_SD_PARAM_MVOLR | core), (uint16_t)w3);
        break;
    case 0x32:
        if (id == 8) { /* noise clock of the slot's core */
            spu2_sd_set_core_attr((uint16_t)((w2 / 24) | SPU2_SD_CORE_NOISE_CLK), (uint16_t)w3);
        } else if (id == 10) { /* S/PDIF mode: nothing on the host */
            spu2_sd_set_core_attr(SPU2_SD_CORE_SPDIF_MODE, (uint16_t)w2);
        } else {
            sndn2_log_once(&logged_sub, "command 0x32 with sub-command %u ignored", id);
        }
        break;

    /* sample RAM */
    case 0x20:
        voice_trans(id, w2, w3, 0);
        break;
    case 0x21:
        voice_trans(id, w2, w3, 1);
        break;
    case 0x22: /* never sent by the EE (R1): the status into the return word */
        wr32(H.ret_page, (uint32_t)spu2_sd_voice_trans_status(0, (int16_t)id));
        break;

    /* ADPCM streams */
    case 0x3C:
        st_adpcm_init();
        break;
    case 0x3D:
        break;
    case 0x3E:
        st_adpcm_open(id, w2, w3);
        break;
    case 0x3F:
        st_adpcm_close(id);
        break;
    case 0x40:
        st_adpcm_volume(id, w2, w3);
        break;
    case 0x41:
        st_adpcm_pitch(id, w2, w3);
        break;
    case 0x42:
        st_adpcm_play(id, w2);
        break;
    case 0x43:
        st_adpcm_stop(id, w2);
        break;

    /* PCM streams */
    case 0x46:
        st_pcm_init();
        break;
    case 0x47:
        st_pcm_quit();
        break;
    case 0x48:
        st_pcm_open(id, w2, w3);
        break;
    case 0x49:
        st_pcm_close(id);
        break;
    case 0x4A:
        st_pcm_volume(id, w2, w3);
        break;
    case 0x4B:
        st_pcm_play(id);
        break;
    case 0x4C:
        st_pcm_stop(id);
        break;
    case 0x4D:
        st_pcm_lseek(id, w2);
        break;
    case 0x4E:
        st_pcm_effect(id);
        break;
    case 0x4F:
        st_pcm_bufmode(id, w2, w3);
        break;

    default: /* the jump table's other entries do nothing */
        sndn2_log_once(&logged_cmd, "command 0x%X ignored (not one the IRX handles)", cmd);
        break;
    }
}

/* --- The reply page ---------------------------------------------------------- */

static uint8_t *fill_page(void)
{
    uint8_t *page;
    int slot;
    int ch;

    H.page_counter++;
    page = H.pages[H.page_counter & 1];
    for (slot = 0; slot < SNDN2_SLOTS; slot++) {
        uint16_t envx = spu2_sd_get_param(vsel((uint32_t)slot, SPU2_SD_VPARAM_ENVX));

        wr32(page + ICO_SNDN2_REPLY_ENVX + 4 * slot, envx & 0x7FFFu);
        wr32(page + ICO_SNDN2_REPLY_STREAM + 4 * slot, st_adpcm_read_off(slot));
    }
    for (ch = 0; ch < SNDN2_PCM_CHANNELS; ch++) {
        wr32(page + ICO_SNDN2_REPLY_PCM + 4 * ch, st_pcm_read_off(ch));
    }
    /* +0x1C0 is the DMA callback's; +0x1C4..0x1FF are never written */
    return page;
}

void *ico_sndn2_host_serve(unsigned int rpc_number, void *send, int ssize, int rsize)
{
    const uint8_t *pk = send;
    int i;

    spu2_sd_set_time(spu2_time());
    if (rpc_number == ICO_SNDN2_RPC_TICK) {
        for (i = 0; pk != NULL && i + 16 <= ssize; i += 16) {
            dispatch(pk + i);
        }
        st_adpcm_tick();
        H.last_reply = fill_page();
        /* the host SIF copies `rsize` bytes from the reply: never past the
           page (the EE asks for exactly 0x200) */
        return rsize <= ICO_SNDN2_REPLY_SIZE ? (void *)H.last_reply : NULL;
    }
    /* any other number (the EE sends only 0x65): one packet, the return
       word; the host SIF copies `rsize` bytes from the reply */
    memset(H.ret_page, 0, sizeof(H.ret_page));
    if (pk != NULL && ssize >= 16) {
        dispatch(pk);
    }
    return rsize <= ICO_SNDN2_REPLY_SIZE ? H.ret_page : NULL;
}

const uint8_t *ico_sndn2_host_last_reply(void)
{
    return H.last_reply;
}

const uint8_t *ico_sndn2_staging(void)
{
    return sndn2_staging_buf();
}

void ico_sndn2_host_reset(void)
{
    spu2_reset();
    ico_libsd_apply(NULL); /* the idle block too, like the presets */
    memset(&H, 0, sizeof(H));
    st_adpcm_reset();
    logged_cmd = logged_iop = logged_clamp = logged_sub = 0;
}

void ico_sndn2_host_register(void)
{
    if (!H.registered) {
        ico_sndn2_host_reset();
        ico_sndn2_pitch_load();
        ico_libsd_load();
        H.registered = 1;
    }
    ico_sif_register_server(ICO_SNDN2_SERVER_ID, ico_sndn2_host_serve);
}
