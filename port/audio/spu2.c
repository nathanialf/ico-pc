/*
 * port/audio/spu2.c
 *
 * The software SPU2 (spu2.h).  One call to frame() renders one 48 kHz
 * output frame:
 *
 *   1. apply the queued register writes and transfers due at this frame;
 *   2. for core 0, then core 1: run the 24 voices (interpolate or noise,
 *      envelope, volume), sum them into the dry and wet buses by
 *      VMIXL/VMIXR/VMIXEL/VMIXER, add the external input (core 1: core 0's
 *      output through AVOL) and the AutoDMA input (through BVOL) as MMIX
 *      selects, run the reverb (every second frame, at 24 kHz, behind the
 *      39-tap resampling filters), add its output through EVOL, apply the
 *      master volume;
 *   3. store the write-back areas, output core 1's result.
 *
 * That is the reference renderer.  spu2_render gets the same bits faster by
 * running stretches of up to 256 frames voice by voice ("Chunked
 * rendering" below; AUDIO.md, "Render cost").
 *
 * Every behaviour is from psx-spx's SPU description unless a comment says
 * otherwise; docs/port/AUDIO.md lists the sources and the approximations
 * (DIVERGENCES.md rows A1..).
 */
#include "spu2.h"

#include <stddef.h>
#include <string.h>

#include "adpcm.h"
#include "spu2_internal.h"

/* --- State ------------------------------------------------------------------ */

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct vol_state {
    uint16_t reg; /* the last value written */
    int32_t level;
    uint32_t counter;
} vol_state;

typedef struct voice {
    vol_state vol[2];
    uint32_t ssa;
    uint32_t lsax;
    uint32_t cur; /* address of the current ADPCM block */
    adpcm_hist hist;
    int16_t buf[ADPCM_BLOCK_SAMPLES];
    int pos;           /* next sample of buf to feed the interpolator */
    int flags;         /* flag byte of the current block */
    int16_t interp[4]; /* oldest, older, old, new */
    uint32_t counter;  /* pitch counter, bits 12+ whole samples */
    int phase;
    int32_t env;
    uint32_t env_counter;
    int32_t out; /* this frame's enveloped sample (PMON, write-back) */
} voice;

#define RV_HIST 64 /* power of two >= 39 */

typedef struct core {
    uint16_t regs[0x400 / 2]; /* offsets 0x000-0x3FF */
    uint16_t xregs[0x28 / 2]; /* the 0x760 block */
    voice v[SPU2_VOICES];
    uint32_t endx;
    vol_state mvol[2];
    /* noise generator */
    uint16_t noise;
    int32_t noise_timer;
    /* reverb */
    uint32_t rv_cur;
    int16_t rv_in[2][RV_HIST];  /* wet input at 48 kHz */
    int16_t rv_out[2][RV_HIST]; /* reverb output, zero-stuffed to 48 kHz */
    /* AutoDMA input */
    const uint8_t *memin;
    spu2_memin_cb memin_cb;
    void *memin_user;
    int memin_pos; /* 0..511 */
    /* IRQ */
    int irq;
    /* this frame's outputs (core 0 feeds core 1) */
    int32_t out[2];
} core;

typedef struct event {
    uint64_t time;
    int kind;
    int core;
    unsigned reg;
    uint16_t value;
    int chan;
    uint32_t addr;
    void *buf;
    uint32_t len;
} event;

enum { EV_REG, EV_TRANS_WRITE, EV_TRANS_READ };

#define EV_CAP 16384

typedef struct dma_chan {
    int queued;
    int active;
    uint64_t done_at;
    spu2_trans_cb cb;
    void *user;
} dma_chan;

static struct {
    uint8_t ram[SPU2_RAM_SIZE];
    core c[SPU2_CORES];
    /* the most recent value queued per register, for helpers that need
       the value the caller has written (spu2_reverb_apply_preset's EEA) */
    uint16_t shadow[SPU2_CORES][0x400 / 2];
    uint64_t now;
    event ev[EV_CAP];
    int ev_head;
    int ev_count;
    dma_chan dma[2];
    uint32_t dma_rate;
    spu2_irq_cb irq_cb;
    void *irq_user;
    spu2_reverb_preset presets[SPU2_REVERB_MODES];
} S;

/* --- Small helpers ------------------------------------------------------------ */

static int32_t clamp16(int32_t x)
{
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return x;
}

static int16_t ram_rd(uint32_t addr)
{
    uint32_t b = (addr & SPU2_ADDR_MASK) * 2u;
    return (int16_t)(uint16_t)(S.ram[b] | S.ram[b + 1] << 8);
}

static void ram_wr(uint32_t addr, int32_t v)
{
    uint32_t b = (addr & SPU2_ADDR_MASK) * 2u;
    S.ram[b] = (uint8_t)(v & 0xFF);
    S.ram[b + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint32_t reg_pair(const core *c, unsigned reg)
{
    return ((uint32_t)(c->regs[reg / 2] & 0xF) << 16 | c->regs[reg / 2 + 1]) & SPU2_ADDR_MASK;
}

static uint32_t voice_bits(const core *c, unsigned reg)
{
    return ((uint32_t)c->regs[reg / 2] | (uint32_t)(c->regs[reg / 2 + 1] & 0xFF) << 16);
}

static void raise_irq(int ci)
{
    if (!S.c[ci].irq) {
        S.c[ci].irq = 1;
        if (S.irq_cb)
            S.irq_cb(ci, S.irq_user);
    }
}

/* An access to SPU addresses [a, a + n). */
static void irq_check(uint32_t a, uint32_t n)
{
    int ci;

    for (ci = 0; ci < SPU2_CORES; ci++) {
        core *c = &S.c[ci];
        uint32_t irqa;

        if (!(c->regs[SPU2_R_ATTR / 2] & SPU2_ATTR_IRQ))
            continue;
        irqa = reg_pair(c, SPU2_R_IRQA);
        if (((irqa - a) & SPU2_ADDR_MASK) < n)
            raise_irq(ci);
    }
}

/* --- Envelope generator --------------------------------------------------- */

static inline void env_tick(int32_t *level, uint32_t *counter, int rate, int exponential,
                            int decrease, int negative, int never_step)
{
    int shift = (rate >> 2) & 0x1F;
    int32_t step = 7 - (rate & 3);
    uint32_t inc;
    int32_t l = *level;

    if (never_step)
        return;
    if (decrease ^ negative)
        step = ~step; /* +7,+6,+5,+4 -> -8,-7,-6,-5 */
    step *= (int32_t)1 << (shift < 11 ? 11 - shift : 0);
    inc = 0x8000u >> (shift > 11 ? shift - 11 : 0);
    if (exponential && !decrease && l > 0x6000) {
        if (shift < 10) {
            step >>= 2;
        } else if (shift >= 11) {
            inc >>= 2;
        } else {
            step >>= 1;
            inc >>= 1;
        }
    } else if (exponential && decrease) {
        step = (step * l) >> 15;
    }
    if (inc < 1)
        inc = 1;

    *counter += inc;
    if (!(*counter & 0x8000u))
        return;
    *counter &= 0x7FFFu;

    l += step;
    if (!decrease) {
        if (l > 0x7FFF)
            l = 0x7FFF;
        else if (l < -0x8000)
            l = -0x8000;
    } else if (negative) {
        if (l > 0)
            l = 0;
        else if (l < -0x8000)
            l = -0x8000;
    } else if (l < 0) {
        l = 0;
    }
    *level = l;
}

void spu2_env_tick(int32_t *level, uint32_t *counter, int rate, int exponential, int decrease,
                   int negative, int never_step)
{
    env_tick(level, counter, rate, exponential, decrease, negative, never_step);
}

static inline void adsr_run(int *phase, int32_t *env, uint32_t *counter, uint16_t adsr1,
                            uint16_t adsr2)
{
    int rate;

    switch (*phase) {
    case ENV_ATTACK:
        rate = (adsr1 >> 8) & 0x7F;
        env_tick(env, counter, rate, adsr1 >> 15, 0, 0, rate == 0x7F);
        if (*env >= 0x7FFF) {
            *phase = ENV_DECAY;
            *counter = 0;
        }
        break;
    case ENV_DECAY:
        /* decay runs while the level is above the sustain level, (SL + 1)
           * 0x800; at SL 15 (0x8000) it ends at once */
        if (*env > (((adsr1 & 0xF) + 1) << 11))
            env_tick(env, counter, ((adsr1 >> 4) & 0xF) << 2, 1, 1, 0, 0);
        if (*env <= (((adsr1 & 0xF) + 1) << 11)) {
            *phase = ENV_SUSTAIN;
            *counter = 0;
        }
        break;
    case ENV_SUSTAIN:
        rate = (adsr2 >> 6) & 0x7F;
        env_tick(env, counter, rate, adsr2 >> 15, (adsr2 >> 14) & 1, 0, rate == 0x7F);
        break;
    case ENV_RELEASE:
        env_tick(env, counter, (adsr2 & 0x1F) << 2, (adsr2 >> 5) & 1, 1, 0, (adsr2 & 0x1F) == 0x1F);
        if (*env <= 0) {
            *env = 0;
            *phase = ENV_OFF;
        }
        break;
    default:
        break;
    }
}

static inline void adsr_tick(voice *v, uint16_t adsr1, uint16_t adsr2)
{
    adsr_run(&v->phase, &v->env, &v->env_counter, adsr1, adsr2);
}

/* env_tick's counter increment for a rate (never_step: 0). */
static inline uint32_t env_inc(int rate, int exponential, int decrease, int32_t level,
                               int never_step)
{
    int shift = (rate >> 2) & 0x1F;
    uint32_t inc = 0x8000u >> (shift > 11 ? shift - 11 : 0);

    if (never_step)
        return 0;
    if (exponential && !decrease && level > 0x6000) {
        if (shift >= 11)
            inc >>= 2;
        else if (shift == 10)
            inc >>= 1;
    }
    return inc < 1 ? 1 : inc;
}

/* The envelope between steps: while counter + inc stays below 0x8000, an
   adsr_run in the current phase and level only adds inc to the counter (no
   step, no phase change).  Returns that inc, or 0x8000 (always "steps")
   when the next adsr_run may do more: a phase about to end, or a level
   about to move it. */
static uint32_t adsr_quiet_inc(int phase, int32_t env, uint16_t adsr1, uint16_t adsr2)
{
    int rate;

    switch (phase) {
    case ENV_ATTACK:
        rate = (adsr1 >> 8) & 0x7F;
        if (env >= 0x7FFF)
            return 0x8000;
        return env_inc(rate, adsr1 >> 15, 0, env, rate == 0x7F);
    case ENV_DECAY:
        if (env <= (((adsr1 & 0xF) + 1) << 11))
            return 0x8000;
        return env_inc(((adsr1 >> 4) & 0xF) << 2, 1, 1, env, 0);
    case ENV_SUSTAIN:
        rate = (adsr2 >> 6) & 0x7F;
        return env_inc(rate, adsr2 >> 15, (adsr2 >> 14) & 1, env, rate == 0x7F);
    case ENV_RELEASE:
        if (env <= 0)
            return 0x8000;
        return env_inc((adsr2 & 0x1F) << 2, (adsr2 >> 5) & 1, 1, env, (adsr2 & 0x1F) == 0x1F);
    default:
        return 0;
    }
}

/* Volume registers (voice VOLL/VOLR, MVOLL/MVOLR): bit 15 clear = fixed
   level (bits 0-14 are the volume / 2), set = sweep from the current
   level (bit 14 exponential, 13 decrease, 12 negative phase, 0-6 rate). */
static void vol_write(vol_state *vs, uint16_t v)
{
    vs->reg = v;
    vs->counter = 0;
    if (!(v & 0x8000))
        vs->level = (int16_t)(uint16_t)(v << 1);
}

static inline void vol_tick(vol_state *vs)
{
    uint16_t v = vs->reg;

    if (!(v & 0x8000))
        return;
    env_tick(&vs->level, &vs->counter, v & 0x7F, (v >> 14) & 1, (v >> 13) & 1, (v >> 12) & 1,
             (v & 0x7F) == 0x7F);
}

/* --- Voices ------------------------------------------------------------------ */

static void voice_load_block(voice *v)
{
    uint32_t b = (v->cur & SPU2_ADDR_MASK) * 2u;

    SPU2_LAP(SPU2_PROF_PITCH);
    irq_check(v->cur, 8);
    v->flags = adpcm_decode_block(&S.ram[b], v->buf, &v->hist);
    SPU2_LAP(SPU2_PROF_DECODE);
    if (v->flags & ADPCM_FLAG_LOOP_START)
        v->lsax = v->cur;
    v->pos = 0;
}

static int16_t voice_fetch(core *c, int vi)
{
    voice *v = &c->v[vi];

    if (v->pos >= ADPCM_BLOCK_SAMPLES) {
        if (v->flags & ADPCM_FLAG_LOOP_END) {
            c->endx |= 1u << vi;
            v->cur = v->lsax;
            if (!(v->flags & ADPCM_FLAG_LOOP_REPEAT)) {
                /* code 1: end + mute */
                v->phase = ENV_RELEASE;
                v->env = 0;
                v->env_counter = 0;
            }
        } else {
            v->cur = (v->cur + 8) & SPU2_ADDR_MASK;
        }
        voice_load_block(v);
    }
    return v->buf[v->pos++];
}

static uint32_t voice_nax(const voice *v)
{
    int p = v->pos < ADPCM_BLOCK_SAMPLES ? v->pos : ADPCM_BLOCK_SAMPLES - 1;

    return (v->cur + 1 + (uint32_t)(p >> 2)) & SPU2_ADDR_MASK;
}

static void key_on(core *c, int vi)
{
    voice *v = &c->v[vi];
    unsigned va = SPU2_R_VA(vi);

    v->ssa = reg_pair(c, va + SPU2_VA_SSA);
    v->cur = v->ssa;
    memset(&v->hist, 0, sizeof v->hist);
    memset(v->interp, 0, sizeof v->interp);
    v->counter = 0;
    v->phase = ENV_ATTACK;
    v->env = 0;
    v->env_counter = 0;
    c->endx &= ~(1u << vi);
    voice_load_block(v);
}

static void key_off(core *c, int vi)
{
    voice *v = &c->v[vi];

    if (v->phase != ENV_OFF) {
        v->phase = ENV_RELEASE;
        v->env_counter = 0;
    }
}

static void noise_tick(core *c)
{
    int clk = (c->regs[SPU2_R_ATTR / 2] >> SPU2_ATTR_NOISE_SHIFT) & 0x3F;
    int shift = clk >> 2;
    int step = (clk & 3) + 4;
    unsigned n = c->noise;
    unsigned parity = ((n >> 15) ^ (n >> 12) ^ (n >> 11) ^ (n >> 10) ^ 1u) & 1u;

    c->noise_timer -= step;
    if (c->noise_timer < 0) {
        c->noise = (uint16_t)(n << 1 | parity);
        c->noise_timer += 0x20000 >> shift;
        if (c->noise_timer < 0)
            c->noise_timer += 0x20000 >> shift;
    }
}

static int32_t interpolate(const voice *v)
{
    int i = (v->counter >> 4) & 0xFF;
    int32_t s;

    s = (spu2_gauss[0x0FF - i] * v->interp[0]) >> 15;
    s += (spu2_gauss[0x1FF - i] * v->interp[1]) >> 15;
    s += (spu2_gauss[0x100 + i] * v->interp[2]) >> 15;
    s += (spu2_gauss[0x000 + i] * v->interp[3]) >> 15;
    return clamp16(s);
}

/* Run voice vi for one frame; adds into the four bus accumulators. */
static void voice_frame(core *c, int vi, uint32_t non, uint32_t pmon, int32_t acc[4],
                        uint32_t mix[4])
{
    voice *v = &c->v[vi];
    unsigned vp = SPU2_R_VP(vi);
    uint32_t bit = 1u << vi;
    int32_t s;
    int32_t l;
    int32_t r;
    uint32_t step;
    int n;

    s = (non & bit) ? (int16_t)c->noise : interpolate(v);
    SPU2_LAP(SPU2_PROF_INTERP);
    s = (s * v->env) >> 15;
    v->out = s;

    l = (s * v->vol[0].level) >> 15;
    r = (s * v->vol[1].level) >> 15;
    if (mix[0] & bit)
        acc[0] += l;
    if (mix[1] & bit)
        acc[1] += r;
    if (mix[2] & bit)
        acc[2] += l;
    if (mix[3] & bit)
        acc[3] += r;
    SPU2_LAP(SPU2_PROF_MIX);

    adsr_tick(v, c->regs[(vp + SPU2_VP_ADSR1) / 2], c->regs[(vp + SPU2_VP_ADSR2) / 2]);
    vol_tick(&v->vol[0]);
    vol_tick(&v->vol[1]);
    SPU2_LAP(SPU2_PROF_ENVELOPE);

    /* pitch counter (psx-spx "Pitch Counter") */
    step = c->regs[(vp + SPU2_VP_PITCH) / 2];
    if (vi > 0 && (pmon & bit)) {
        int32_t factor = c->v[vi - 1].out + 0x8000;
        step = (uint32_t)(((int32_t)(int16_t)(uint16_t)step * factor) >> 15) & 0xFFFFu;
    }
    if (step > 0x3FFF)
        step = 0x4000;
    v->counter += step;
    for (n = (int)(v->counter >> 12); n > 0; n--) {
        v->interp[0] = v->interp[1];
        v->interp[1] = v->interp[2];
        v->interp[2] = v->interp[3];
        v->interp[3] = voice_fetch(c, vi);
    }
    v->counter &= 0xFFFu;
    SPU2_LAP(SPU2_PROF_PITCH);
}

/* --- Reverb ------------------------------------------------------------------ */

/* The reverb address registers, in the order of the 0x2E4 block. */
enum {
    RV_APF1_SIZE,
    RV_APF2_SIZE,
    RV_SAME_L_DST,
    RV_SAME_R_DST,
    RV_COMB1_L,
    RV_COMB1_R,
    RV_COMB2_L,
    RV_COMB2_R,
    RV_SAME_L_SRC,
    RV_SAME_R_SRC,
    RV_DIFF_L_DST,
    RV_DIFF_R_DST,
    RV_COMB3_L,
    RV_COMB3_R,
    RV_COMB4_L,
    RV_COMB4_R,
    RV_DIFF_L_SRC,
    RV_DIFF_R_SRC,
    RV_APF1_L_DST,
    RV_APF1_R_DST,
    RV_APF2_L_DST,
    RV_APF2_R_DST
};

/* A core's reverb set-up, read from its registers: every address offset
   the formula uses, reduced modulo the work area size, so a step needs no
   division.  Built once per chunk (render_chunk) or per step (frame by
   frame); the registers cannot change in between. */
enum {
    RO_SAME_DST, /* [ch]: mSAME, mSAME - 1, dSAME */
    RO_SAME_PREV = 2,
    RO_SAME_SRC = 4,
    RO_DIFF_DST = 6, /* mDIFF, mDIFF - 1, dDIFF (crossed) */
    RO_DIFF_PREV = 8,
    RO_DIFF_SRC = 10,
    RO_COMB = 12,     /* [comb * 2 + ch] */
    RO_APF1_DST = 20, /* [ch]: mAPF1, mAPF1 - dAPF1, mAPF2, mAPF2 - dAPF2 */
    RO_APF1_SRC = 22,
    RO_APF2_DST = 24,
    RO_APF2_SRC = 26,
    RO_COUNT = 28
};

typedef struct rv_plan {
    int on;    /* ESA <= end */
    int write; /* ATTR bit 7 */
    uint32_t esa;
    uint32_t end;
    uint32_t size; /* halfwords in [esa, end] */
    uint32_t off[RO_COUNT];
    int32_t vIIR, vWALL, vAPF1, vAPF2, vC[4], vLIN, vRIN;
} rv_plan;

static uint32_t rv_reg(const core *c, int i)
{
    return reg_pair(c, SPU2_R_APF1_SIZE + 4u * (unsigned)i);
}

static int32_t rv_vol(const core *c, unsigned reg)
{
    return (int16_t)c->xregs[(reg - 0x760) / 2];
}

static uint32_t rv_mod(int64_t off, uint32_t size)
{
    int64_t m = (int64_t)size;

    off %= m;
    if (off < 0)
        off += m;
    return (uint32_t)off;
}

static void rv_plan_build(const core *c, rv_plan *p)
{
    int ch;

    p->end = ((uint32_t)(c->regs[SPU2_R_EEA / 2] & 0xF) << 16) | 0xFFFFu;
    p->esa = reg_pair(c, SPU2_R_ESA);
    p->on = p->esa <= p->end;
    p->write = (c->regs[SPU2_R_ATTR / 2] & SPU2_ATTR_EFFECT) != 0;
    if (!p->on)
        return;
    p->size = p->end - p->esa + 1;
    for (ch = 0; ch < 2; ch++) {
        int64_t m = rv_reg(c, RV_SAME_L_DST + ch);

        p->off[RO_SAME_DST + ch] = rv_mod(m, p->size);
        p->off[RO_SAME_PREV + ch] = rv_mod(m - 1, p->size);
        p->off[RO_SAME_SRC + ch] = rv_mod(rv_reg(c, RV_SAME_L_SRC + ch), p->size);
        m = rv_reg(c, RV_DIFF_L_DST + ch);
        p->off[RO_DIFF_DST + ch] = rv_mod(m, p->size);
        p->off[RO_DIFF_PREV + ch] = rv_mod(m - 1, p->size);
        p->off[RO_DIFF_SRC + ch] = rv_mod(rv_reg(c, ch ? RV_DIFF_L_SRC : RV_DIFF_R_SRC), p->size);
        p->off[RO_COMB + 0 + ch] = rv_mod(rv_reg(c, RV_COMB1_L + ch), p->size);
        p->off[RO_COMB + 2 + ch] = rv_mod(rv_reg(c, RV_COMB2_L + ch), p->size);
        p->off[RO_COMB + 4 + ch] = rv_mod(rv_reg(c, RV_COMB3_L + ch), p->size);
        p->off[RO_COMB + 6 + ch] = rv_mod(rv_reg(c, RV_COMB4_L + ch), p->size);
        m = rv_reg(c, RV_APF1_L_DST + ch);
        p->off[RO_APF1_DST + ch] = rv_mod(m, p->size);
        p->off[RO_APF1_SRC + ch] = rv_mod(m - (int64_t)rv_reg(c, RV_APF1_SIZE), p->size);
        m = rv_reg(c, RV_APF2_L_DST + ch);
        p->off[RO_APF2_DST + ch] = rv_mod(m, p->size);
        p->off[RO_APF2_SRC + ch] = rv_mod(m - (int64_t)rv_reg(c, RV_APF2_SIZE), p->size);
    }
    p->vIIR = rv_vol(c, SPU2_R_IIR_VOL);
    p->vWALL = rv_vol(c, SPU2_R_WALL_VOL);
    p->vAPF1 = rv_vol(c, SPU2_R_APF1_VOL);
    p->vAPF2 = rv_vol(c, SPU2_R_APF2_VOL);
    p->vC[0] = rv_vol(c, SPU2_R_COMB1_VOL);
    p->vC[1] = rv_vol(c, SPU2_R_COMB2_VOL);
    p->vC[2] = rv_vol(c, SPU2_R_COMB3_VOL);
    p->vC[3] = rv_vol(c, SPU2_R_COMB4_VOL);
    p->vLIN = rv_vol(c, SPU2_R_IN_COEF_L);
    p->vRIN = rv_vol(c, SPU2_R_IN_COEF_R);
}

/* The halfword address `off` (reduced) from the buffer position `rel`
   (both < size): the work area wraps. */
static inline uint32_t rv_addr(const rv_plan *p, uint32_t rel, int i)
{
    uint32_t a = rel + p->off[i];

    if (a >= p->size)
        a -= p->size;
    return (p->esa + a) & SPU2_ADDR_MASK;
}

static inline int32_t mul15(int32_t a, int32_t b)
{
    return (a * b) >> 15;
}

/* One 24 kHz step of the reverb unit (psx-spx "Reverb Formula"):
   in[2] the resampled wet input, out[2] the result before EVOL. */
static void reverb_step(core *c, const rv_plan *p, const int32_t in[2], int32_t out[2])
{
    uint32_t rel;
    int32_t Lin;
    int32_t Rin;
    int ch;

    if (!p->on) {
        out[0] = out[1] = 0;
        return;
    }
    if (c->rv_cur < p->esa || c->rv_cur > p->end)
        c->rv_cur = p->esa;
    rel = c->rv_cur - p->esa;

    Lin = mul15(p->vLIN, in[0]);
    Rin = mul15(p->vRIN, in[1]);

    if (p->write) {
        /* same side and different side reflections */
        for (ch = 0; ch < 2; ch++) {
            int32_t inp = ch ? Rin : Lin;
            int32_t prev = ram_rd(rv_addr(p, rel, RO_SAME_PREV + ch));
            int32_t t =
                clamp16(inp + mul15(ram_rd(rv_addr(p, rel, RO_SAME_SRC + ch)), p->vWALL) - prev);

            ram_wr(rv_addr(p, rel, RO_SAME_DST + ch), clamp16(mul15(t, p->vIIR) + prev));

            prev = ram_rd(rv_addr(p, rel, RO_DIFF_PREV + ch));
            t = clamp16(inp + mul15(ram_rd(rv_addr(p, rel, RO_DIFF_SRC + ch)), p->vWALL) - prev);
            ram_wr(rv_addr(p, rel, RO_DIFF_DST + ch), clamp16(mul15(t, p->vIIR) + prev));
        }
    }

    for (ch = 0; ch < 2; ch++) {
        int32_t o;
        int32_t a;

        /* early echo: the four combs */
        o = mul15(p->vC[0], ram_rd(rv_addr(p, rel, RO_COMB + 0 + ch)));
        o += mul15(p->vC[1], ram_rd(rv_addr(p, rel, RO_COMB + 2 + ch)));
        o += mul15(p->vC[2], ram_rd(rv_addr(p, rel, RO_COMB + 4 + ch)));
        o += mul15(p->vC[3], ram_rd(rv_addr(p, rel, RO_COMB + 6 + ch)));
        o = clamp16(o);

        /* late reverb: two all-pass filters */
        a = ram_rd(rv_addr(p, rel, RO_APF1_SRC + ch));
        o = clamp16(o - mul15(p->vAPF1, a));
        if (p->write)
            ram_wr(rv_addr(p, rel, RO_APF1_DST + ch), o);
        o = clamp16(mul15(o, p->vAPF1) + a);

        a = ram_rd(rv_addr(p, rel, RO_APF2_SRC + ch));
        o = clamp16(o - mul15(p->vAPF2, a));
        if (p->write)
            ram_wr(rv_addr(p, rel, RO_APF2_DST + ch), o);
        o = clamp16(mul15(o, p->vAPF2) + a);

        out[ch] = o;
    }

    c->rv_cur = c->rv_cur + 1 > p->end ? p->esa : c->rv_cur + 1;
}

/* The 39-tap resampling filter is a half-band filter: the odd taps are
   zero except the centre (0x4000 at 19), and it is symmetric.  Summing
   only the non-zero products, in pairs, gives the same integer. */
static inline int32_t fir_even(const int16_t *h, unsigned p)
{
    int32_t acc = 0;
    int k;

    for (k = 0; k < 19; k += 2)
        acc += spu2_reverb_fir[k] * ((int32_t)h[(p - (unsigned)k) & (RV_HIST - 1)] +
                                     h[(p - 38u + (unsigned)k) & (RV_HIST - 1)]);
    return acc;
}

/* Feed this frame's wet input; return this frame's reverb output (before
   EVOL) in out[2].  `p` is the core's plan, or NULL to build one. */
static void reverb_frame(core *c, const rv_plan *p, const int32_t wet[2], int32_t out[2])
{
    unsigned pos = (unsigned)(S.now & (RV_HIST - 1));
    int ch;

    c->rv_in[0][pos] = (int16_t)wet[0];
    c->rv_in[1][pos] = (int16_t)wet[1];

    if (S.now & 1) {
        rv_plan local;
        int32_t in[2];
        int32_t o[2];

        /* downsample: the 39-tap filter over the last 39 inputs */
        for (ch = 0; ch < 2; ch++) {
            int32_t acc =
                fir_even(c->rv_in[ch], pos) + 0x4000 * c->rv_in[ch][(pos - 19u) & (RV_HIST - 1)];

            in[ch] = clamp16(acc >> 15);
        }
        if (p == NULL) {
            rv_plan_build(c, &local);
            p = &local;
        }
        reverb_step(c, p, in, o);
        c->rv_out[0][pos] = (int16_t)o[0];
        c->rv_out[1][pos] = (int16_t)o[1];
        /* upsample: the same filter over the zero-stuffed output, gain 2.
           Every second rv_out entry is a stuffed zero (written on the even
           frames), so this frame meets only the even taps... */
        for (ch = 0; ch < 2; ch++)
            out[ch] = clamp16(fir_even(c->rv_out[ch], pos) >> 14);
    } else {
        c->rv_out[0][pos] = 0;
        c->rv_out[1][pos] = 0;
        /* ...and an even frame only the centre: (0x4000 * x) >> 14 = x */
        for (ch = 0; ch < 2; ch++)
            out[ch] = c->rv_out[ch][(pos - 19u) & (RV_HIST - 1)];
    }
}

/* --- Cores ------------------------------------------------------------------- */

static int32_t memin_sample(core *c, int ch)
{
    int half = c->memin_pos >> 8;
    int i = c->memin_pos & 0xFF;
    const uint8_t *p;

    if (!c->memin)
        return 0;
    p = c->memin + half * 0x400 + ch * 0x200 + i * 2;
    return (int16_t)(uint16_t)(p[0] | p[1] << 8);
}

static void memin_advance(int ci)
{
    core *c = &S.c[ci];
    int finished;

    if (!c->memin)
        return;
    c->memin_pos = (c->memin_pos + 1) & 0x1FF;
    if ((c->memin_pos & 0xFF) == 0) {
        finished = c->memin_pos ? 0 : 1;
        if (c->memin_cb)
            c->memin_cb(ci, finished, c->memin_user);
    }
}

static void wb(uint32_t base, int32_t v, int irq)
{
    uint32_t a = base + (uint32_t)(S.now & 0x1FF);

    ram_wr(a, clamp16(v));
    if (irq)
        irq_check(a, 1);
}

/* Steps 3-6 of a frame for core ci (AUDIO.md, "One frame"), after its
   voices: `bus` the four voice buses (dry L, dry R, wet L, wet R) before
   saturation, v1 and v3 voice 1's and 3's outputs for the write-back, `p`
   the reverb plan (NULL: built here), `irq` whether a write-back can raise
   an IRQ (render_chunk runs only when none can). */
static void core_finish(int ci, const int32_t bus[4], int32_t v1, int32_t v3, const rv_plan *p,
                        int irq)
{
    core *c = &S.c[ci];
    int32_t acc[4];
    uint16_t mmix = c->regs[SPU2_R_MMIX / 2];
    int32_t sin[2] = {0, 0};
    int32_t mem[2];
    int32_t dry[2];
    int32_t wet[2];
    int32_t rv[2];
    int ch;

    for (ch = 0; ch < 4; ch++)
        acc[ch] = clamp16(bus[ch]);

    if (ci == 1) {
        sin[0] = mul15(S.c[0].out[0], (int16_t)c->xregs[(SPU2_R_AVOLL - 0x760) / 2]);
        sin[1] = mul15(S.c[0].out[1], (int16_t)c->xregs[(SPU2_R_AVOLR - 0x760) / 2]);
    }
    mem[0] = mul15(memin_sample(c, 0), (int16_t)c->xregs[(SPU2_R_BVOLL - 0x760) / 2]);
    mem[1] = mul15(memin_sample(c, 1), (int16_t)c->xregs[(SPU2_R_BVOLR - 0x760) / 2]);

    dry[0] = (mmix & SPU2_MMIX_VOICE_DRY_L ? acc[0] : 0) +
             (mmix & SPU2_MMIX_SIN_DRY_L ? sin[0] : 0) +
             (mmix & SPU2_MMIX_MEMIN_DRY_L ? mem[0] : 0);
    dry[1] = (mmix & SPU2_MMIX_VOICE_DRY_R ? acc[1] : 0) +
             (mmix & SPU2_MMIX_SIN_DRY_R ? sin[1] : 0) +
             (mmix & SPU2_MMIX_MEMIN_DRY_R ? mem[1] : 0);
    wet[0] = (mmix & SPU2_MMIX_VOICE_WET_L ? acc[2] : 0) +
             (mmix & SPU2_MMIX_SIN_WET_L ? sin[0] : 0) +
             (mmix & SPU2_MMIX_MEMIN_WET_L ? mem[0] : 0);
    wet[1] = (mmix & SPU2_MMIX_VOICE_WET_R ? acc[3] : 0) +
             (mmix & SPU2_MMIX_SIN_WET_R ? sin[1] : 0) +
             (mmix & SPU2_MMIX_MEMIN_WET_R ? mem[1] : 0);
    wet[0] = clamp16(wet[0]);
    wet[1] = clamp16(wet[1]);

    SPU2_LAP(SPU2_PROF_BUS);
    reverb_frame(c, p, wet, rv);
    SPU2_LAP(SPU2_PROF_REVERB);
    for (ch = 0; ch < 2; ch++) {
        int32_t o =
            clamp16(dry[ch]) + mul15(rv[ch], (int16_t)c->xregs[(SPU2_R_EVOLL - 0x760) / 2 + ch]);
        o = clamp16(o);
        c->out[ch] = clamp16(mul15(o, c->mvol[ch].level));
        vol_tick(&c->mvol[ch]);
    }

    /* write-back areas (PCSX2 wiki "SPU2 is more than just sound!"),
       halfword addresses, 0x200 samples each */
    {
        uint32_t vbase = ci ? 0xC00 : 0x400;
        uint32_t mbase = ci ? 0x1800 : 0x1000;

        wb(vbase, v1, irq);
        wb(vbase + 0x200, v3, irq);
        if (ci == 0) {
            wb(0x800, c->out[0], irq);
            wb(0xA00, c->out[1], irq);
        }
        wb(mbase, acc[0], irq);
        wb(mbase + 0x200, acc[1], irq);
        wb(mbase + 0x400, acc[2], irq);
        wb(mbase + 0x600, acc[3], irq);
    }
    SPU2_LAP(SPU2_PROF_OUTPUT);
}

/* The reference renderer's frame for core ci: every voice in turn, the
   noise step, then core_finish. */
static void core_frame(int ci)
{
    core *c = &S.c[ci];
    uint32_t non = voice_bits(c, SPU2_R_NON);
    uint32_t pmon = voice_bits(c, SPU2_R_PMON);
    uint32_t mix[4];
    int32_t acc[4] = {0, 0, 0, 0}; /* dry L, dry R, wet L, wet R */
    int vi;

    mix[0] = voice_bits(c, SPU2_R_VMIXL);
    mix[1] = voice_bits(c, SPU2_R_VMIXR);
    mix[2] = voice_bits(c, SPU2_R_VMIXEL);
    mix[3] = voice_bits(c, SPU2_R_VMIXER);

    for (vi = 0; vi < SPU2_VOICES; vi++)
        voice_frame(c, vi, non, pmon, acc, mix);
    noise_tick(c);
    core_finish(ci, acc, c->v[1].out, c->v[3].out, NULL, 1);
}

/* --- Register writes ---------------------------------------------------------- */

static void reg_apply(int ci, unsigned reg, uint16_t value)
{
    core *c = &S.c[ci];
    int vi;

    reg &= ~1u;
    if (reg >= 0x760 && reg < 0x788) {
        unsigned i = (reg - 0x760) / 2;

        c->xregs[i] = value;
        if (reg == SPU2_R_MVOLL || reg == SPU2_R_MVOLR)
            vol_write(&c->mvol[(reg - SPU2_R_MVOLL) / 2], value);
        return;
    }
    if (reg >= 0x400)
        return;

    if (reg < 0x180) {
        vi = (int)(reg / 0x10);
        switch (reg & 0xF) {
        case SPU2_VP_VOLL:
        case SPU2_VP_VOLR:
            vol_write(&c->v[vi].vol[(reg & 0xF) / 2], value);
            break;
        case SPU2_VP_ENVX:
            c->v[vi].env = (int16_t)value;
            break;
        default:
            break;
        }
        c->regs[reg / 2] = value;
        return;
    }

    if (reg >= SPU2_R_VA(0) && reg < SPU2_R_VA(SPU2_VOICES)) {
        unsigned off = (reg - SPU2_R_VA(0)) % 0xC;
        voice *v;

        vi = (int)((reg - SPU2_R_VA(0)) / 0xC);
        v = &c->v[vi];
        c->regs[reg / 2] = value;
        if (off == SPU2_VA_LSAX || off == SPU2_VA_LSAX + 2) {
            v->lsax = reg_pair(c, SPU2_R_VA(vi) + SPU2_VA_LSAX);
        } else if (off == SPU2_VA_NAX || off == SPU2_VA_NAX + 2) {
            v->cur = reg_pair(c, SPU2_R_VA(vi) + SPU2_VA_NAX);
            voice_load_block(v);
        }
        return;
    }

    switch (reg) {
    case SPU2_R_KON:
    case SPU2_R_KON + 2:
    case SPU2_R_KOFF:
    case SPU2_R_KOFF + 2: {
        int first = (reg & 2) ? 16 : 0;
        int n = (reg & 2) ? 8 : 16;
        int on = reg < SPU2_R_KOFF;

        for (vi = 0; vi < n; vi++)
            if (value & (1u << vi)) {
                if (on)
                    key_on(c, first + vi);
                else
                    key_off(c, first + vi);
            }
        c->regs[reg / 2] = value;
        return;
    }
    case SPU2_R_ENDX:
        c->endx = (c->endx & 0xFF0000u) | value;
        return;
    case SPU2_R_ENDX + 2:
        c->endx = (c->endx & 0xFFFFu) | (uint32_t)(value & 0xFF) << 16;
        return;
    case SPU2_R_ESA:
    case SPU2_R_ESA + 2:
        c->regs[reg / 2] = value;
        c->rv_cur = reg_pair(c, SPU2_R_ESA);
        return;
    case SPU2_R_DATA: {
        /* manual transfer: store at TSA, advance TSA */
        uint32_t tsa = reg_pair(c, SPU2_R_TSA);

        ram_wr(tsa, (int16_t)value);
        irq_check(tsa, 1);
        tsa = (tsa + 1) & SPU2_ADDR_MASK;
        c->regs[SPU2_R_TSA / 2] = (uint16_t)(tsa >> 16);
        c->regs[SPU2_R_TSA / 2 + 1] = (uint16_t)tsa;
        return;
    }
    default:
        c->regs[reg / 2] = value;
        return;
    }
}

/* --- Event queue ---------------------------------------------------------------- */

static void ev_compact(void)
{
    if (S.ev_head > 0) {
        memmove(&S.ev[0], &S.ev[S.ev_head], (size_t)S.ev_count * sizeof(event));
        S.ev_head = 0;
    }
}

static void ev_run(const event *e);

static void ev_push(const event *e)
{
    int i;

    if (S.ev_head + S.ev_count >= EV_CAP)
        ev_compact();
    if (S.ev_count >= EV_CAP) {
        /* full: the oldest goes now (deterministic, never expected) */
        event first = S.ev[S.ev_head];

        S.ev_head++;
        S.ev_count--;
        ev_run(&first);
        ev_compact();
    }
    /* insert after every event with time <= e->time */
    i = S.ev_head + S.ev_count;
    while (i > S.ev_head && S.ev[i - 1].time > e->time) {
        S.ev[i] = S.ev[i - 1];
        i--;
    }
    S.ev[i] = *e;
    S.ev_count++;
}

static uint64_t dma_duration(uint32_t len)
{
    if (S.dma_rate == 0)
        return 1;
    return ((uint64_t)len + S.dma_rate - 1) / S.dma_rate;
}

static void ev_run(const event *e)
{
    switch (e->kind) {
    case EV_REG:
        reg_apply(e->core, e->reg, e->value);
        break;
    case EV_TRANS_WRITE:
    case EV_TRANS_READ: {
        dma_chan *d = &S.dma[e->chan];

        if (e->kind == EV_TRANS_WRITE)
            spu2_dma_write(e->addr, e->buf, e->len);
        else
            spu2_dma_read(e->addr, e->buf, e->len);
        d->queued = 0;
        d->active = 1;
        d->done_at = S.now + dma_duration(e->len);
        break;
    }
    default:
        break;
    }
}

static void ev_run_due(uint64_t t)
{
    while (S.ev_count > 0 && S.ev[S.ev_head].time <= t) {
        event e = S.ev[S.ev_head];

        S.ev_head++;
        S.ev_count--;
        ev_run(&e);
    }
    if (S.ev_count == 0)
        S.ev_head = 0;
}

/* --- Public API ----------------------------------------------------------------- */

void spu2_reset(void)
{
    int i;

    memset(&S, 0, sizeof S);
    for (i = 0; i < SPU2_REVERB_MODES; i++)
        S.presets[i] = spu2_reverb_presets[i];
}

uint8_t *spu2_ram(void)
{
    return S.ram;
}

uint64_t spu2_time(void)
{
    return S.now;
}

void spu2_write_reg(int core_idx, unsigned reg, uint16_t value, uint64_t time)
{
    event e;

    memset(&e, 0, sizeof e);
    e.kind = EV_REG;
    e.core = core_idx & 1;
    e.reg = reg;
    e.value = value;
    e.time = time < S.now ? S.now : time;
    if ((reg & ~1u) < 0x400)
        S.shadow[e.core][(reg & 0x3FF) / 2] = value;
    ev_push(&e);
}

void spu2_apply_pending(void)
{
    ev_run_due(UINT64_MAX);
}

uint16_t spu2_read_reg(int core_idx, unsigned reg)
{
    core *c = &S.c[core_idx & 1];
    int vi;

    reg &= ~1u;
    if (reg >= 0x760 && reg < 0x788) {
        if (reg == SPU2_R_MVOLXL || reg == SPU2_R_MVOLXR)
            return (uint16_t)c->mvol[(reg - SPU2_R_MVOLXL) / 2].level;
        return c->xregs[(reg - 0x760) / 2];
    }
    if (reg >= 0x400)
        return 0;
    if (reg < 0x180) {
        vi = (int)(reg / 0x10);
        switch (reg & 0xF) {
        case SPU2_VP_ENVX:
            return (uint16_t)c->v[vi].env;
        case SPU2_VP_VOLXL:
        case SPU2_VP_VOLXR:
            return (uint16_t)c->v[vi].vol[((reg & 0xF) - SPU2_VP_VOLXL) / 2].level;
        default:
            return c->regs[reg / 2];
        }
    }
    if (reg >= SPU2_R_VA(0) && reg < SPU2_R_VA(SPU2_VOICES)) {
        unsigned off = (reg - SPU2_R_VA(0)) % 0xC;
        uint32_t a;

        vi = (int)((reg - SPU2_R_VA(0)) / 0xC);
        if (off >= SPU2_VA_NAX)
            a = voice_nax(&c->v[vi]);
        else if (off >= SPU2_VA_LSAX)
            a = c->v[vi].lsax;
        else
            return c->regs[reg / 2];
        return (off & 2) ? (uint16_t)(a & 0xFFFF) : (uint16_t)(a >> 16);
    }
    switch (reg) {
    case SPU2_R_ENDX:
        return (uint16_t)(c->endx & 0xFFFF);
    case SPU2_R_ENDX + 2:
        return (uint16_t)(c->endx >> 16);
    case SPU2_R_STATX: {
        int ci = (int)(c - S.c);
        uint16_t st = 0;

        if (S.dma[ci].queued || S.dma[ci].active)
            st |= 0x0400; /* transfer busy */
        if (c->irq)
            st |= 0x0040;
        return st;
    }
    default:
        return c->regs[reg / 2];
    }
}

/* The start of the frame at S.now: the writes and transfers due, the
   transfers that have finished (their callbacks), the writes those
   queued. */
static void frame_begin(void)
{
    int ch;

    ev_run_due(S.now);
    for (ch = 0; ch < 2; ch++) {
        dma_chan *d = &S.dma[ch];

        if (d->active && S.now >= d->done_at) {
            d->active = 0;
            if (d->cb)
                d->cb(ch, d->user);
        }
    }
    /* callbacks may have queued writes for this frame */
    ev_run_due(S.now);
    SPU2_LAP(SPU2_PROF_EVENTS);
}

static void frame_end(int16_t *out)
{
    out[0] = (int16_t)S.c[1].out[0];
    out[1] = (int16_t)S.c[1].out[1];
    memin_advance(0);
    memin_advance(1);
    S.now++;
    SPU2_LAP(SPU2_PROF_OUTPUT);
}

/* The reference renderer: one frame, both cores, every voice. */
static void frame(int16_t *out)
{
    frame_begin();
    core_frame(0);
    core_frame(1);
    frame_end(out);
}

/* --- Chunked rendering ----------------------------------------------------------- */

/*
 * render_chunk renders the frames [S.now, S.now + n) voice by voice
 * instead of frame by frame, with the same result bit for bit.  It runs
 * only over a stretch where nothing outside the SPU2 can look or change
 * anything (chunk_frames): no queued write or transfer falls due, no
 * transfer finishes, the AutoDMA input's half-done callback can come only
 * after the last frame, and no IRQ can be raised (each core's IRQ is
 * disabled or already pending, so every IRQ check is a no-op).  Then the
 * registers are constant, voices of one core interact only through PMON
 * (voice v reads voice v - 1's output of the same frame, which is kept per
 * frame), the noise level of each frame can be computed first, and the
 * frame-by-frame order matters only where the voices read sound RAM that
 * the frames write: the write-back areas and the reverb work areas.  A
 * voice that loads a block from one of those flags a hazard; the voices
 * are then put back as they were and the chunk renders frame by frame.
 */

#define CHUNK 256

static struct {
    int32_t bus[SPU2_CORES][4][CHUNK];
    int32_t vout[2][CHUNK];            /* scratch outputs, alternating */
    int32_t wbv[SPU2_CORES][2][CHUNK]; /* voice 1 and 3 outputs (write-back) */
    int16_t noise[CHUNK];
    rv_plan plan[SPU2_CORES];
    /* halfword ranges [lo, hi] the frames write */
    uint32_t wr_lo[1 + SPU2_CORES];
    uint32_t wr_hi[1 + SPU2_CORES];
    int wr_count;
    int hazard;
    /* the voices as they were, for a hazard */
    voice snap[SPU2_CORES][SPU2_VOICES];
    uint32_t snap_endx[SPU2_CORES];
    uint16_t snap_noise[SPU2_CORES];
    int32_t snap_noise_timer[SPU2_CORES];
} F;

static int exact_only;
static spu2_stats stats;

void spu2_set_exact(int on)
{
    exact_only = on;
}

/* The frames from S.now (after frame_begin) that render_chunk may take. */
static int chunk_frames(int frames)
{
    uint64_t n = frames < CHUNK ? (uint64_t)frames : CHUNK;
    int i;

    if (S.ev_count > 0 && S.ev[S.ev_head].time - S.now < n)
        n = S.ev[S.ev_head].time - S.now;
    for (i = 0; i < 2; i++)
        if (S.dma[i].active && S.dma[i].done_at - S.now < n)
            n = S.dma[i].done_at - S.now;
    for (i = 0; i < SPU2_CORES; i++)
        if (S.c[i].memin && 256u - (unsigned)(S.c[i].memin_pos & 0xFF) < n)
            n = 256u - (unsigned)(S.c[i].memin_pos & 0xFF);
    return (int)n;
}

static int chunk_allowed(void)
{
    int ci;

    if (exact_only)
        return 0;
    for (ci = 0; ci < SPU2_CORES; ci++)
        if ((S.c[ci].regs[SPU2_R_ATTR / 2] & SPU2_ATTR_IRQ) && !S.c[ci].irq)
            return 0;
    return 1;
}

static void chunk_load_block(voice *v)
{
    uint32_t a = v->cur & SPU2_ADDR_MASK;
    int i;

    for (i = 0; i < F.wr_count; i++)
        if (a <= F.wr_hi[i] && a + 7u >= F.wr_lo[i])
            F.hazard = 1;
    SPU2_LAP(SPU2_PROF_VOICE);
    v->flags = adpcm_decode_block(&S.ram[a * 2u], v->buf, &v->hist);
    SPU2_LAP(SPU2_PROF_DECODE);
    if (v->flags & ADPCM_FLAG_LOOP_START)
        v->lsax = v->cur;
    v->pos = 0;
}

/* Shift `count` samples from buf[] into the interpolator. */
static inline void interp_feed(voice *v, const int16_t *buf, int count)
{
    if (count >= 4) {
        v->interp[0] = buf[count - 4];
        v->interp[1] = buf[count - 3];
        v->interp[2] = buf[count - 2];
        v->interp[3] = buf[count - 1];
        return;
    }
    while (count-- > 0) {
        v->interp[0] = v->interp[1];
        v->interp[1] = v->interp[2];
        v->interp[2] = v->interp[3];
        v->interp[3] = *buf++;
    }
}

/* Fetch `count` samples as voice_fetch would (without the IRQ check), a
   block at a time.  With stop_at_mute, return right after a fetch that
   ended a sound with end + mute (the voice is no longer silent from the
   next frame).  Returns the number of samples fetched. */
static uint32_t chunk_skip(core *c, voice *v, uint32_t bit, uint32_t count, int stop_at_mute)
{
    uint32_t done = 0;

    while (done < count) {
        uint32_t take;

        if (v->pos >= ADPCM_BLOCK_SAMPLES) {
            int mute = 0;

            if (v->flags & ADPCM_FLAG_LOOP_END) {
                c->endx |= bit;
                v->cur = v->lsax;
                if (!(v->flags & ADPCM_FLAG_LOOP_REPEAT)) {
                    v->phase = ENV_RELEASE;
                    v->env = 0;
                    v->env_counter = 0;
                    mute = 1;
                }
            } else {
                v->cur = (v->cur + 8) & SPU2_ADDR_MASK;
            }
            chunk_load_block(v);
            if (mute && stop_at_mute) {
                interp_feed(v, v->buf, 1);
                v->pos = 1;
                return done + 1;
            }
        }
        take = (uint32_t)(ADPCM_BLOCK_SAMPLES - v->pos);
        if (take > count - done)
            take = count - done;
        interp_feed(v, v->buf + v->pos, (int)take);
        v->pos += (int)take;
        done += take;
    }
    return done;
}

/* Frames [f, n) of a voice whose envelope is off at level 0 and whose
   pitch is constant (no PMON): every output is 0 and the buses get
   nothing, so only the pitch counter, the decoder and the volume sweeps
   move.  The counter's total over m frames is counter + m * step, of which
   the whole samples are fetched; if a fetch ends the sound with end + mute
   the voice sounds (a release at level 0) from the next frame, so the skip
   stops at the end of that frame.  Returns the next frame to render. */
static int chunk_silent(core *c, voice *v, uint32_t bit, int f, int n, uint32_t step, int sweep0,
                        int sweep1, int32_t *outs)
{
    uint32_t c0 = v->counter;
    uint32_t m = (uint32_t)(n - f);
    uint32_t fetched = chunk_skip(c, v, bit, (c0 + m * step) >> 12, 1);
    uint32_t i;

    if (v->phase != ENV_OFF) {
        /* the mute came with fetch `fetched`, in frame r of the m: the
           first r with c0 + (r + 1) * step >= fetched << 12; the rest of
           that frame's fetches follow */
        m = (((fetched << 12) - c0) + step - 1) / step;
        chunk_skip(c, v, bit, ((c0 + m * step) >> 12) - fetched, 0);
    }
    v->counter = (c0 + m * step) & 0xFFFu;
    for (i = 0; i < m; i++)
        outs[f + (int)i] = 0;
    if (sweep0)
        for (i = 0; i < m; i++)
            vol_tick(&v->vol[0]);
    if (sweep1)
        for (i = 0; i < m; i++)
            vol_tick(&v->vol[1]);
    return f + (int)m;
}

/* The chunked voice loop keeps the interpolator as a window over the
   samples still to come: win[idx..idx + 3] are the four last fetched
   (voice.interp), win[idx + 4..len - 1] the rest of the current block
   (voice.buf[pos..27]); fetching k samples is idx += k while the block
   lasts.  win_load builds it from the voice, win_store writes it back. */
typedef struct win {
    int16_t s[4 + ADPCM_BLOCK_SAMPLES];
    int idx;
    int len;
} win;

static inline void win_load(win *w, const voice *v)
{
    int i;

    w->s[0] = v->interp[0];
    w->s[1] = v->interp[1];
    w->s[2] = v->interp[2];
    w->s[3] = v->interp[3];
    w->len = 4;
    for (i = v->pos; i < ADPCM_BLOCK_SAMPLES; i++)
        w->s[w->len++] = v->buf[i];
    w->idx = 0;
}

static inline void win_store(const win *w, voice *v)
{
    v->interp[0] = w->s[w->idx];
    v->interp[1] = w->s[w->idx + 1];
    v->interp[2] = w->s[w->idx + 2];
    v->interp[3] = w->s[w->idx + 3];
    v->pos = ADPCM_BLOCK_SAMPLES - (w->len - 4 - w->idx);
}

/* The live state voice_chunk keeps in locals (the buses it stores to may
   alias anything under -fno-strict-aliasing, so fields read through the
   voice would be reloaded after every store). */
typedef struct vlocal {
    int32_t env;
    uint32_t env_counter;
    int phase;
    vol_state vol[2];
    uint32_t counter;
} vlocal;

static inline void vl_load(vlocal *l, const voice *v)
{
    l->env = v->env;
    l->env_counter = v->env_counter;
    l->phase = v->phase;
    l->vol[0] = v->vol[0];
    l->vol[1] = v->vol[1];
    l->counter = v->counter;
}

static inline void vl_store(const vlocal *l, voice *v)
{
    v->env = l->env;
    v->env_counter = l->env_counter;
    v->phase = l->phase;
    v->vol[0] = l->vol[0];
    v->vol[1] = l->vol[1];
    v->counter = l->counter;
}

/* voice_frame over n frames: the output of each frame into outs[], its
   share into the buses; prev[] is voice vi - 1's outputs (PMON). */
static void voice_chunk(core *c, int vi, int n, uint32_t non, uint32_t pmon, const uint32_t mix[4],
                        int32_t (*bus)[CHUNK], const int32_t *prev, int32_t *outs)
{
    voice *v = &c->v[vi];
    unsigned vp = SPU2_R_VP(vi);
    uint32_t bit = 1u << vi;
    uint16_t adsr1 = c->regs[(vp + SPU2_VP_ADSR1) / 2];
    uint16_t adsr2 = c->regs[(vp + SPU2_VP_ADSR2) / 2];
    uint32_t pitch = c->regs[(vp + SPU2_VP_PITCH) / 2];
    int noise = (non & bit) != 0;
    int pm = vi > 0 && (pmon & bit);
    int sweep0 = (v->vol[0].reg & 0x8000) != 0;
    int sweep1 = (v->vol[1].reg & 0x8000) != 0;
    int32_t *b0 = (mix[0] & bit) ? bus[0] : NULL;
    int32_t *b1 = (mix[1] & bit) ? bus[1] : NULL;
    int32_t *b2 = (mix[2] & bit) ? bus[2] : NULL;
    int32_t *b3 = (mix[3] & bit) ? bus[3] : NULL;
    vlocal l;
    win w;
    uint32_t einc;
    int f;

    if (pitch > 0x3FFF && !pm)
        pitch = 0x4000;
    vl_load(&l, v);
    win_load(&w, v);
    einc = adsr_quiet_inc(l.phase, l.env, adsr1, adsr2);
    for (f = 0; f < n; f++) {
        int32_t s = 0;
        uint32_t step = pitch;
        int k;

        if (l.env == 0 && l.phase == ENV_OFF && !pm) {
            win_store(&w, v);
            vl_store(&l, v);
            f = chunk_silent(c, v, bit, f, n, step, sweep0, sweep1, outs) - 1;
            vl_load(&l, v);
            win_load(&w, v);
            einc = adsr_quiet_inc(l.phase, l.env, adsr1, adsr2);
            continue;
        }

        /* a zero envelope makes the sample 0 whatever the interpolator
           holds, and 0 adds nothing to the buses */
        if (l.env != 0) {
            if (noise) {
                s = F.noise[f];
            } else {
                const int16_t *x = &w.s[w.idx];
                int i = (l.counter >> 4) & 0xFF;

                s = (spu2_gauss[0x0FF - i] * x[0]) >> 15;
                s += (spu2_gauss[0x1FF - i] * x[1]) >> 15;
                s += (spu2_gauss[0x100 + i] * x[2]) >> 15;
                s += (spu2_gauss[0x000 + i] * x[3]) >> 15;
                s = clamp16(s);
            }
            s = (s * l.env) >> 15;
            if (s != 0) {
                int32_t sl = (s * l.vol[0].level) >> 15;
                int32_t sr = (s * l.vol[1].level) >> 15;

                if (b0)
                    b0[f] += sl;
                if (b1)
                    b1[f] += sr;
                if (b2)
                    b2[f] += sl;
                if (b3)
                    b3[f] += sr;
            }
        }
        outs[f] = s;
        if (l.phase != ENV_OFF) {
            if (l.env_counter + einc < 0x8000u) {
                l.env_counter += einc;
            } else {
                adsr_run(&l.phase, &l.env, &l.env_counter, adsr1, adsr2);
                einc = adsr_quiet_inc(l.phase, l.env, adsr1, adsr2);
            }
        }
        if (sweep0)
            vol_tick(&l.vol[0]);
        if (sweep1)
            vol_tick(&l.vol[1]);

        if (pm) {
            int32_t factor = prev[f] + 0x8000;

            step = (uint32_t)(((int32_t)(int16_t)(uint16_t)step * factor) >> 15) & 0xFFFFu;
            if (step > 0x3FFF)
                step = 0x4000;
        }
        l.counter += step;
        k = (int)(l.counter >> 12);
        l.counter &= 0xFFFu;
        if (w.idx + 4 + k <= w.len) {
            w.idx += k;
        } else {
            /* the block ends within these k (k <= 4 < 28: one block end):
               take what is left, then voice_fetch's block end, then the
               rest from the new block */
            int i;

            k -= w.len - 4 - w.idx;
            for (i = 0; i < 4; i++)
                w.s[i] = w.s[w.len - 4 + i];
            if (v->flags & ADPCM_FLAG_LOOP_END) {
                c->endx |= bit;
                v->cur = v->lsax;
                if (!(v->flags & ADPCM_FLAG_LOOP_REPEAT)) {
                    /* code 1: end + mute */
                    l.phase = ENV_RELEASE;
                    l.env = 0;
                    l.env_counter = 0;
                    einc = adsr_quiet_inc(l.phase, l.env, adsr1, adsr2);
                }
            } else {
                v->cur = (v->cur + 8) & SPU2_ADDR_MASK;
            }
            chunk_load_block(v);
            memcpy(&w.s[4], v->buf, sizeof v->buf);
            w.len = 4 + ADPCM_BLOCK_SAMPLES;
            w.idx = k;
        }
    }
    win_store(&w, v);
    vl_store(&l, v);
    v->out = outs[n - 1];
    SPU2_LAP(SPU2_PROF_VOICE);
}

/* Render n frames (1 < n <= CHUNK, from chunk_frames) voice by voice.
   Returns 0, with nothing changed, on a hazard. */
static int render_chunk(int16_t *out, int n)
{
    int32_t *outs[SPU2_VOICES];
    int ci;
    int vi;
    int f;

    F.wr_count = 0;
    F.wr_lo[F.wr_count] = 0x400;
    F.wr_hi[F.wr_count++] = 0x1FFF;
    for (ci = 0; ci < SPU2_CORES; ci++) {
        core *c = &S.c[ci];

        rv_plan_build(c, &F.plan[ci]);
        if (F.plan[ci].on && F.plan[ci].write) {
            F.wr_lo[F.wr_count] = F.plan[ci].esa;
            F.wr_hi[F.wr_count++] = F.plan[ci].end;
        }
        memcpy(F.snap[ci], c->v, sizeof c->v);
        F.snap_endx[ci] = c->endx;
        F.snap_noise[ci] = c->noise;
        F.snap_noise_timer[ci] = c->noise_timer;
    }
    F.hazard = 0;
    SPU2_LAP(SPU2_PROF_EVENTS);

    for (ci = 0; ci < SPU2_CORES; ci++) {
        core *c = &S.c[ci];
        uint32_t non = voice_bits(c, SPU2_R_NON);
        uint32_t pmon = voice_bits(c, SPU2_R_PMON);
        uint32_t mix[4];
        const int32_t *prev = NULL;
        int k;

        mix[0] = voice_bits(c, SPU2_R_VMIXL);
        mix[1] = voice_bits(c, SPU2_R_VMIXR);
        mix[2] = voice_bits(c, SPU2_R_VMIXEL);
        mix[3] = voice_bits(c, SPU2_R_VMIXER);
        for (k = 0; k < 4; k++)
            memset(F.bus[ci][k], 0, (size_t)n * sizeof(int32_t));
        /* the noise level each frame's voices see, then the step */
        for (f = 0; f < n; f++) {
            F.noise[f] = (int16_t)c->noise;
            noise_tick(c);
        }
        SPU2_LAP(SPU2_PROF_INTERP);

        for (vi = 0; vi < SPU2_VOICES; vi++) {
            outs[vi] = vi == 1 ? F.wbv[ci][0] : vi == 3 ? F.wbv[ci][1] : F.vout[vi & 1];
            voice_chunk(c, vi, n, non, pmon, mix, F.bus[ci], prev, outs[vi]);
            if (F.hazard)
                goto hazard;
            prev = outs[vi];
        }
    }

    for (f = 0; f < n; f++) {
        for (ci = 0; ci < SPU2_CORES; ci++) {
            int32_t bus[4];

            bus[0] = F.bus[ci][0][f];
            bus[1] = F.bus[ci][1][f];
            bus[2] = F.bus[ci][2][f];
            bus[3] = F.bus[ci][3][f];
            core_finish(ci, bus, F.wbv[ci][0][f], F.wbv[ci][1][f], &F.plan[ci], 0);
        }
        frame_end(out + 2 * f);
    }
    return 1;

hazard:
    stats.hazards++;
    for (ci = 0; ci < SPU2_CORES; ci++) {
        core *c = &S.c[ci];

        memcpy(c->v, F.snap[ci], sizeof c->v);
        c->endx = F.snap_endx[ci];
        c->noise = F.snap_noise[ci];
        c->noise_timer = F.snap_noise_timer[ci];
    }
    return 0;
}

void spu2_render(int16_t *out, int frames)
{
#ifdef SPU2_PROFILE
    spu2_prof.last = spu2_prof_clock();
    spu2_prof.vsyncs++;
#endif
    while (frames > 0) {
        int n;
        int i;

        frame_begin();
        n = chunk_frames(frames);
        if (n >= 2 && chunk_allowed() && render_chunk(out, n)) {
            stats.chunks++;
            stats.chunked_frames += (uint64_t)n;
        } else {
            /* frame by frame (frame_begin is idempotent) */
            for (i = 0; i < n; i++)
                frame(out + 2 * i);
            stats.exact_frames += (uint64_t)n;
        }
        out += 2 * n;
        frames -= n;
    }
}

void spu2_get_stats(spu2_stats *st)
{
    *st = stats;
}

void spu2_dma_write(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0; i < len; i++)
        S.ram[(addr + i) & (SPU2_RAM_SIZE - 1)] = s[i];
    if (len)
        irq_check((addr >> 1) & SPU2_ADDR_MASK, (len + 1 + (addr & 1)) >> 1);
}

void spu2_dma_read(uint32_t addr, void *dst, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0; i < len; i++)
        d[i] = S.ram[(addr + i) & (SPU2_RAM_SIZE - 1)];
}

int spu2_voice_trans(int chan, int dir, uint32_t addr, void *buf, uint32_t len, uint64_t time)
{
    event e;
    dma_chan *d = &S.dma[chan & 1];

    if (d->queued || d->active)
        return -1;
    memset(&e, 0, sizeof e);
    e.kind = dir == SPU2_TRANS_READ ? EV_TRANS_READ : EV_TRANS_WRITE;
    e.chan = chan & 1;
    e.addr = addr;
    e.buf = buf;
    e.len = len;
    e.time = time < S.now ? S.now : time;
    d->queued = 1;
    ev_push(&e);
    return 0;
}

int spu2_voice_trans_busy(int chan)
{
    return S.dma[chan & 1].queued || S.dma[chan & 1].active;
}

void spu2_voice_trans_wait(int chan)
{
    dma_chan *d = &S.dma[chan & 1];
    int i;

    if (d->queued) {
        for (i = S.ev_head; i < S.ev_head + S.ev_count; i++) {
            event *e = &S.ev[i];

            if ((e->kind == EV_TRANS_WRITE || e->kind == EV_TRANS_READ) && e->chan == (chan & 1)) {
                event copy = *e;

                memmove(e, e + 1, (size_t)(S.ev_head + S.ev_count - i - 1) * sizeof(event));
                S.ev_count--;
                ev_run(&copy);
                break;
            }
        }
    }
    if (d->active) {
        d->active = 0;
        if (d->cb)
            d->cb(chan & 1, d->user);
    }
}

uint16_t spu2_shadow(int core_idx, unsigned reg)
{
    return reg < 0x400 ? S.shadow[core_idx & 1][reg / 2] : 0;
}

void spu2_set_trans_callback(int chan, spu2_trans_cb cb, void *user)
{
    S.dma[chan & 1].cb = cb;
    S.dma[chan & 1].user = user;
}

void spu2_set_dma_rate(uint32_t bytes_per_frame)
{
    S.dma_rate = bytes_per_frame;
}

void spu2_memin_start(int core_idx, const uint8_t *ring, spu2_memin_cb cb, void *user)
{
    core *c = &S.c[core_idx & 1];

    c->memin = ring;
    c->memin_cb = cb;
    c->memin_user = user;
    c->memin_pos = 0;
}

void spu2_memin_stop(int core_idx)
{
    core *c = &S.c[core_idx & 1];

    c->memin = NULL;
    c->memin_cb = NULL;
    c->memin_user = NULL;
    c->memin_pos = 0;
}

int spu2_memin_half(int core_idx)
{
    return S.c[core_idx & 1].memin_pos >> 8;
}

int spu2_irq_pending(int core_idx)
{
    return S.c[core_idx & 1].irq;
}

void spu2_irq_clear(int core_idx)
{
    S.c[core_idx & 1].irq = 0;
}

void spu2_set_irq_callback(spu2_irq_cb cb, void *user)
{
    S.irq_cb = cb;
    S.irq_user = user;
}

const spu2_reverb_preset *spu2_reverb_get_preset(int mode)
{
    if (mode < 0 || mode >= SPU2_REVERB_MODES)
        return NULL;
    return &S.presets[mode];
}

void spu2_reverb_set_preset(int mode, const spu2_reverb_preset *p)
{
    if (mode >= 0 && mode < SPU2_REVERB_MODES && p)
        S.presets[mode] = *p;
}

static void write_pair(int ci, unsigned reg, uint32_t a, uint64_t time)
{
    spu2_write_reg(ci, reg, (uint16_t)((a >> 16) & 0xF), time);
    spu2_write_reg(ci, reg + 2, (uint16_t)(a & 0xFFFF), time);
}

void spu2_reverb_apply_preset(int ci, const spu2_reverb_preset *p, uint64_t time)
{
    /* the PS1 register order of the preset -> SPU2 registers */
    static const unsigned vol_regs[8] = {SPU2_R_IIR_VOL,   SPU2_R_COMB1_VOL, SPU2_R_COMB2_VOL,
                                         SPU2_R_COMB3_VOL, SPU2_R_COMB4_VOL, SPU2_R_WALL_VOL,
                                         SPU2_R_APF1_VOL,  SPU2_R_APF2_VOL};
    uint32_t end = ((uint32_t)(S.shadow[ci & 1][SPU2_R_EEA / 2] & 0xF) << 16) | 0xFFFFu;
    uint32_t esa = (end + 1u - p->size / 2u) & SPU2_ADDR_MASK;
    int i;

    ci &= 1;
    write_pair(ci, SPU2_R_APF1_SIZE, (uint32_t)p->regs[0] * 4u, time);
    write_pair(ci, SPU2_R_APF2_SIZE, (uint32_t)p->regs[1] * 4u, time);
    for (i = 0; i < 8; i++)
        spu2_write_reg(ci, vol_regs[i], p->regs[2 + i], time);
    for (i = 0; i < 20; i++)
        write_pair(ci, SPU2_R_SAME_L_DST + 4u * (unsigned)i, (uint32_t)p->regs[10 + i] * 4u, time);
    spu2_write_reg(ci, SPU2_R_IN_COEF_L, p->regs[30], time);
    spu2_write_reg(ci, SPU2_R_IN_COEF_R, p->regs[31], time);
    write_pair(ci, SPU2_R_ESA, esa, time);
}

/* --- Stage profile (SPU2_PROFILE builds) ------------------------------------------ */

#ifdef SPU2_PROFILE
spu2_prof_t spu2_prof;

void spu2_prof_reset(void)
{
    memset(&spu2_prof, 0, sizeof spu2_prof);
}

double spu2_prof_lap_cost(void)
{
    spu2_prof_t save = spu2_prof;
    uint64_t t0 = spu2_prof_clock();
    int i;

    spu2_prof.last = t0;
    for (i = 0; i < 1000000; i++)
        SPU2_LAP(0);
    t0 = spu2_prof.last - t0;
    spu2_prof = save;
    return (double)t0 / 1e6;
}

const char *spu2_prof_stage_name(int stage)
{
    static const char *const names[SPU2_PROF_STAGES] = {"events", "decode", "interp", "envelope",
                                                        "mix",    "pitch",  "voices", "bus",
                                                        "reverb", "output"};

    return stage >= 0 && stage < SPU2_PROF_STAGES ? names[stage] : "?";
}
#endif
