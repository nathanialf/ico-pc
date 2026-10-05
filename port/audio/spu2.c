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

void spu2_env_tick(int32_t *level, uint32_t *counter, int rate, int exponential, int decrease,
                   int negative, int never_step)
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

static void adsr_tick(voice *v, uint16_t adsr1, uint16_t adsr2)
{
    int rate;

    switch (v->phase) {
    case ENV_ATTACK:
        rate = (adsr1 >> 8) & 0x7F;
        spu2_env_tick(&v->env, &v->env_counter, rate, adsr1 >> 15, 0, 0, rate == 0x7F);
        if (v->env >= 0x7FFF) {
            v->phase = ENV_DECAY;
            v->env_counter = 0;
        }
        break;
    case ENV_DECAY:
        /* decay runs while the level is above the sustain level, (SL + 1)
           * 0x800; at SL 15 (0x8000) it ends at once */
        if (v->env > (((adsr1 & 0xF) + 1) << 11))
            spu2_env_tick(&v->env, &v->env_counter, ((adsr1 >> 4) & 0xF) << 2, 1, 1, 0, 0);
        if (v->env <= (((adsr1 & 0xF) + 1) << 11)) {
            v->phase = ENV_SUSTAIN;
            v->env_counter = 0;
        }
        break;
    case ENV_SUSTAIN:
        rate = (adsr2 >> 6) & 0x7F;
        spu2_env_tick(&v->env, &v->env_counter, rate, adsr2 >> 15, (adsr2 >> 14) & 1, 0,
                      rate == 0x7F);
        break;
    case ENV_RELEASE:
        spu2_env_tick(&v->env, &v->env_counter, (adsr2 & 0x1F) << 2, (adsr2 >> 5) & 1, 1, 0,
                      (adsr2 & 0x1F) == 0x1F);
        if (v->env <= 0) {
            v->env = 0;
            v->phase = ENV_OFF;
        }
        break;
    default:
        break;
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

static void vol_tick(vol_state *vs)
{
    uint16_t v = vs->reg;

    if (!(v & 0x8000))
        return;
    spu2_env_tick(&vs->level, &vs->counter, v & 0x7F, (v >> 14) & 1, (v >> 13) & 1, (v >> 12) & 1,
                  (v & 0x7F) == 0x7F);
}

/* --- Voices ------------------------------------------------------------------ */

static void voice_load_block(voice *v)
{
    uint32_t b = (v->cur & SPU2_ADDR_MASK) * 2u;

    irq_check(v->cur, 8);
    v->flags = adpcm_decode_block(&S.ram[b], v->buf, &v->hist);
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

    adsr_tick(v, c->regs[(vp + SPU2_VP_ADSR1) / 2], c->regs[(vp + SPU2_VP_ADSR2) / 2]);
    vol_tick(&v->vol[0]);
    vol_tick(&v->vol[1]);

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

typedef struct rv_ctx {
    const core *c;
    uint32_t esa;
    uint32_t size; /* halfwords in [esa, end] */
    int write;
} rv_ctx;

static uint32_t rv_reg(const core *c, int i)
{
    return reg_pair(c, SPU2_R_APF1_SIZE + 4u * (unsigned)i);
}

static int32_t rv_vol(const core *c, unsigned reg)
{
    return (int16_t)c->xregs[(reg - 0x760) / 2];
}

/* Address `off` halfwords from the buffer position, wrapped into the work
   area. */
static uint32_t rv_addr(const rv_ctx *x, int64_t off)
{
    int64_t a = (int64_t)(x->c->rv_cur - x->esa) + off;
    int64_t m = (int64_t)x->size;

    a %= m;
    if (a < 0)
        a += m;
    return (x->esa + (uint32_t)a) & SPU2_ADDR_MASK;
}

static int32_t rv_rd(const rv_ctx *x, int64_t off)
{
    return ram_rd(rv_addr(x, off));
}

static void rv_wr(const rv_ctx *x, int64_t off, int32_t v)
{
    if (x->write)
        ram_wr(rv_addr(x, off), clamp16(v));
}

static int32_t mul15(int32_t a, int32_t b)
{
    return (a * b) >> 15;
}

/* One 24 kHz step of the reverb unit (psx-spx "Reverb Formula"):
   in[2] the resampled wet input, out[2] the result before EVOL. */
static void reverb_step(core *c, const int32_t in[2], int32_t out[2])
{
    rv_ctx x;
    uint32_t end = ((uint32_t)(c->regs[SPU2_R_EEA / 2] & 0xF) << 16) | 0xFFFFu;
    int32_t vIIR = rv_vol(c, SPU2_R_IIR_VOL);
    int32_t vWALL = rv_vol(c, SPU2_R_WALL_VOL);
    int32_t vAPF1 = rv_vol(c, SPU2_R_APF1_VOL);
    int32_t vAPF2 = rv_vol(c, SPU2_R_APF2_VOL);
    int32_t vC[4];
    int32_t Lin;
    int32_t Rin;
    int ch;

    x.c = c;
    x.esa = reg_pair(c, SPU2_R_ESA);
    if (x.esa > end) {
        out[0] = out[1] = 0;
        return;
    }
    x.size = end - x.esa + 1;
    x.write = (c->regs[SPU2_R_ATTR / 2] & SPU2_ATTR_EFFECT) != 0;
    if (c->rv_cur < x.esa || c->rv_cur > end)
        c->rv_cur = x.esa;

    vC[0] = rv_vol(c, SPU2_R_COMB1_VOL);
    vC[1] = rv_vol(c, SPU2_R_COMB2_VOL);
    vC[2] = rv_vol(c, SPU2_R_COMB3_VOL);
    vC[3] = rv_vol(c, SPU2_R_COMB4_VOL);
    Lin = mul15(rv_vol(c, SPU2_R_IN_COEF_L), in[0]);
    Rin = mul15(rv_vol(c, SPU2_R_IN_COEF_R), in[1]);

    if (x.write) {
        /* same side and different side reflections */
        static const int same_dst[2] = {RV_SAME_L_DST, RV_SAME_R_DST};
        static const int same_src[2] = {RV_SAME_L_SRC, RV_SAME_R_SRC};
        static const int diff_dst[2] = {RV_DIFF_L_DST, RV_DIFF_R_DST};
        static const int diff_src[2] = {RV_DIFF_R_SRC, RV_DIFF_L_SRC}; /* crossed */

        for (ch = 0; ch < 2; ch++) {
            int32_t inp = ch ? Rin : Lin;
            int64_t m = rv_reg(c, same_dst[ch]);
            int32_t prev = rv_rd(&x, m - 1);
            int32_t t = clamp16(inp + mul15(rv_rd(&x, rv_reg(c, same_src[ch])), vWALL) - prev);

            rv_wr(&x, m, mul15(t, vIIR) + prev);

            m = rv_reg(c, diff_dst[ch]);
            prev = rv_rd(&x, m - 1);
            t = clamp16(inp + mul15(rv_rd(&x, rv_reg(c, diff_src[ch])), vWALL) - prev);
            rv_wr(&x, m, mul15(t, vIIR) + prev);
        }
    }

    for (ch = 0; ch < 2; ch++) {
        int base = ch ? 1 : 0;
        int32_t o;
        int64_t m;
        int32_t a;

        /* early echo: the four combs */
        o = mul15(vC[0], rv_rd(&x, rv_reg(c, RV_COMB1_L + base)));
        o += mul15(vC[1], rv_rd(&x, rv_reg(c, RV_COMB2_L + base)));
        o += mul15(vC[2], rv_rd(&x, rv_reg(c, RV_COMB3_L + base)));
        o += mul15(vC[3], rv_rd(&x, rv_reg(c, RV_COMB4_L + base)));
        o = clamp16(o);

        /* late reverb: two all-pass filters */
        m = rv_reg(c, RV_APF1_L_DST + base);
        a = rv_rd(&x, m - (int64_t)rv_reg(c, RV_APF1_SIZE));
        o = clamp16(o - mul15(vAPF1, a));
        rv_wr(&x, m, o);
        o = clamp16(mul15(o, vAPF1) + a);

        m = rv_reg(c, RV_APF2_L_DST + base);
        a = rv_rd(&x, m - (int64_t)rv_reg(c, RV_APF2_SIZE));
        o = clamp16(o - mul15(vAPF2, a));
        rv_wr(&x, m, o);
        o = clamp16(mul15(o, vAPF2) + a);

        out[ch] = o;
    }

    c->rv_cur = c->rv_cur + 1 > end ? x.esa : c->rv_cur + 1;
}

/* Feed this frame's wet input; return this frame's reverb output (before
   EVOL) in out[2]. */
static void reverb_frame(core *c, const int32_t wet[2], int32_t out[2])
{
    unsigned p = (unsigned)(S.now & (RV_HIST - 1));
    int ch;
    int k;

    c->rv_in[0][p] = (int16_t)wet[0];
    c->rv_in[1][p] = (int16_t)wet[1];

    if (S.now & 1) {
        int32_t in[2];
        int32_t o[2];

        /* downsample: the 39-tap filter over the last 39 inputs */
        for (ch = 0; ch < 2; ch++) {
            int32_t acc = 0;

            for (k = 0; k < 39; k++)
                acc += spu2_reverb_fir[k] * c->rv_in[ch][(p - (unsigned)k) & (RV_HIST - 1)];
            in[ch] = clamp16(acc >> 15);
        }
        reverb_step(c, in, o);
        c->rv_out[0][p] = (int16_t)o[0];
        c->rv_out[1][p] = (int16_t)o[1];
    } else {
        c->rv_out[0][p] = 0;
        c->rv_out[1][p] = 0;
    }

    /* upsample: the same filter over the zero-stuffed output, gain 2 */
    for (ch = 0; ch < 2; ch++) {
        int32_t acc = 0;

        for (k = 0; k < 39; k++)
            acc += spu2_reverb_fir[k] * c->rv_out[ch][(p - (unsigned)k) & (RV_HIST - 1)];
        out[ch] = clamp16(acc >> 14);
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

static void wb(uint32_t base, int32_t v)
{
    uint32_t a = base + (uint32_t)(S.now & 0x1FF);

    ram_wr(a, clamp16(v));
    irq_check(a, 1);
}

static void core_frame(int ci)
{
    core *c = &S.c[ci];
    uint32_t non = voice_bits(c, SPU2_R_NON);
    uint32_t pmon = voice_bits(c, SPU2_R_PMON);
    uint32_t mix[4];
    int32_t acc[4] = {0, 0, 0, 0}; /* dry L, dry R, wet L, wet R */
    uint16_t mmix = c->regs[SPU2_R_MMIX / 2];
    int32_t sin[2] = {0, 0};
    int32_t mem[2];
    int32_t dry[2];
    int32_t wet[2];
    int32_t rv[2];
    int ch;
    int vi;

    mix[0] = voice_bits(c, SPU2_R_VMIXL);
    mix[1] = voice_bits(c, SPU2_R_VMIXR);
    mix[2] = voice_bits(c, SPU2_R_VMIXEL);
    mix[3] = voice_bits(c, SPU2_R_VMIXER);

    for (vi = 0; vi < SPU2_VOICES; vi++)
        voice_frame(c, vi, non, pmon, acc, mix);
    noise_tick(c);
    for (ch = 0; ch < 4; ch++)
        acc[ch] = clamp16(acc[ch]);

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

    reverb_frame(c, wet, rv);
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

        wb(vbase, c->v[1].out);
        wb(vbase + 0x200, c->v[3].out);
        if (ci == 0) {
            wb(0x800, c->out[0]);
            wb(0xA00, c->out[1]);
        }
        wb(mbase, acc[0]);
        wb(mbase + 0x200, acc[1]);
        wb(mbase + 0x400, acc[2]);
        wb(mbase + 0x600, acc[3]);
    }
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

static void frame(int16_t *out)
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

    core_frame(0);
    core_frame(1);
    out[0] = (int16_t)S.c[1].out[0];
    out[1] = (int16_t)S.c[1].out[1];
    memin_advance(0);
    memin_advance(1);
    S.now++;
}

void spu2_render(int16_t *out, int frames)
{
    int i;

    for (i = 0; i < frames; i++)
        frame(out + 2 * i);
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
