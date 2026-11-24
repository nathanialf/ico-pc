/* rd_onepass.c: the one-pass blend's pure part (rd_onepass.h): the proof
 * of a pair, the class of a draw, the classes from the check's verdicts,
 * the blender model, the words, the judge's use.  No renderer state:
 * port/render/test/rd_onepass_test.c calls it alone. */
#include "rd_onepass.h"

#include <math.h>

#include "rd_internal.h" /* RdDrawPass, rd__blend_path */
#include "shader_consts.h"

int rd_op_why(const RdDrawPass *colour, const RdDrawPass *alpha, uint8_t hi, uint32_t samples)
{
    const RdPipeKeyInt *k = &colour->key;
    /* the two-pass blend's pair as rd__expand_no_dual makes it: a colour
       pass (the factor in c0.a, RGB) and its alpha pass (A alone, blending
       off), the colour pass not taken already */
    if ((colour->flags & (uint32_t)ICO_DF_NODUAL_FACTOR) == 0u ||
        (alpha->flags & (uint32_t)ICO_DF_NODUAL_ALPHA_PASS) == 0u || (k->gs.colorMask & 8u) != 0u ||
        alpha->key.gs.colorMask != 8u || (k->gs.nodual & RD_NODUAL_ONEPASS) != 0u) {
        return RD_OP_OTHER;
    }
    /* the LERP (Cd*FIX + Cs stores Cs's alpha through ONE / SRC_ALPHA: not
       taken), not Z GREATER with Z write (its alpha pass's GEQUAL admits a
       fragment the colour pass did not), an RGBA8 UNORM target (the
       integer copies blend in a shader), no destination read, one sample
       (a multisampled pass's planes may be evaluated outside its triangle,
       past the alpha plan's range) */
    if (rd__blend_path(k->gs.blend) != RD_BP_LERP ||
        (k->gs.ztst == RD_ZTST_GREATER && k->gs.zwrite == RD_ZWRITE_ON) ||
        k->colorFmt != RHI_FMT_RGBA8_UNORM || k->dst != RHI_DST_READ_NONE || samples > 1u) {
        return RD_OP_OTHER;
    }
    if (colour->flags & (uint32_t)ICO_DF_FIX_FACTOR) {
        return RD_OP_FIX;
    }
    if (colour->flags & (uint32_t)ICO_DF_PABE) {
        return RD_OP_PABE; /* an unblended pixel's c0.a is 1.0, its alpha As */
    }
    if (colour->flags & (uint32_t)ICO_DF_FBA) {
        return RD_OP_FBA; /* the stored alpha As | 0x80 is not As/128 times a constant */
    }
    if (hi > RD_OP_ALPHA_MAX) {
        return RD_OP_ALPHA; /* the source factor clamps at 1.0 past 128 */
    }
    return RD_OP_ONE;
}

uint32_t rd_op_class_of(uint32_t vuBits, bool cmdFused)
{
    if ((vuBits & (uint32_t)ICO_VU_FUSED) != 0u) {
        return 0u; /* a fused kick: its pixels unblended through the factor 1.0 */
    }
    if ((vuBits & (uint32_t)(ICO_VU_CUT_ONLY | ICO_VU_KICK_ONLY)) != 0u) {
        /* the cut of a fused command draws its two passes for its kick's
           too (vuNoDualJoin): kept with it */
        return cmdFused && (vuBits & (uint32_t)ICO_VU_CUT_ONLY) != 0u ? 0u : RD_OP_CUT;
    }
    return RD_OP_PLAIN;
}

uint32_t rd_op_classes(int a, int b, int c)
{
    if (a != RD_OP_V_PASS) {
        return 0u; /* the plain class is C's too: nothing without it */
    }
    const bool bOk = b == RD_OP_V_PASS || b == RD_OP_V_NOTHING;
    return RD_OP_PLAIN | (c == RD_OP_V_PASS && bOk ? RD_OP_CUT : 0u);
}

/* ------------------------------------------------------------ the model */

/* x in [0, 1] quantised: bits 8..24 an unsigned normalised fixed point,
   116 a binary16, 132 a binary32; mode 0 nearest, 1 down, 2 up */
static double quant(double x, int bits, int mode)
{
    if (x <= 0.0) {
        return 0.0;
    }
    if (bits == 132) {
        float v = (float)x;
        if (mode == 1 && (double)v > x) {
            v = nextafterf(v, 0.0f);
        } else if (mode == 2 && (double)v < x) {
            v = nextafterf(v, 2.0f);
        }
        return (double)v;
    }
    if (bits == 116) {
        int e;
        (void)frexp(x, &e);                   /* x = m 2^e, m in [0.5, 1) */
        int ulpExp = e - 11;                  /* 11 significant bits */
        ulpExp = ulpExp < -24 ? -24 : ulpExp; /* the subnormals' step */
        const double ulp = ldexp(1.0, ulpExp);
        const double r = x / ulp;
        const double k = mode == 0 ? floor(r + 0.5) : (mode == 1 ? floor(r) : ceil(r));
        return k * ulp;
    }
    const double m = (double)((1u << bits) - 1u);
    const double r = x * m;
    double k = mode == 0 ? floor(r + 0.5) : (mode == 1 ? floor(r) : ceil(r));
    k = k > m ? m : k;
    return k / m;
}

static int storeByte(double x, int mode)
{
    const double r = (x > 1.0 ? 1.0 : x) * 255.0;
    return (int)(mode == 0 ? floor(r + 0.5) : (mode == 1 ? floor(r) : ceil(r)));
}

int rd_op_alpha_model(int as, int bs, int bk, int bp, int round, int store, int *two)
{
    /* the alpha pass: vu_dual_out_f's float4(col.rgb, a) * (1.0 / 255.0),
       blending off */
    const float c0 = (float)as * (float)(1.0 / 255.0);
    *two = storeByte(quant((double)c0, bs, round), store);
    /* the carry: c0.a = f * (1.0 / 128.0) (exact), clamped to 1 as the
       blender's source; times the constant's alpha 128/255 */
    double s = (double)((float)as * (float)(1.0 / 128.0));
    s = s > 1.0 ? 1.0 : s;
    const double k = quant((double)(float)(128.0 / 255.0), bk, round);
    return storeByte(quant(quant(s, bs, round) * k, bp, round), store);
}

/* ------------------------------------------------------------ the words */

static const char *const kReason[RD_OP_REASONS] = {
    [RD_OP_ONE] = "drawn in one pass",
    [RD_OP_ALPHA] = "alpha above 128",
    [RD_OP_FBA] = "FBA",
    [RD_OP_PABE] = "PABE",
    [RD_OP_FIX] = "a FIX factor",
    [RD_OP_OTHER] = "other",
    [RD_OP_FUSED] = "fused",
    [RD_OP_CLASS] = "class not in use",
    [RD_OP_UNMADE] = "not made yet",
    [RD_OP_TRIAL] = "timed without it",
};

const char *rd_op_reason_name(int why)
{
    return why >= 0 && why < RD_OP_REASONS ? kReason[why] : "?";
}

static const char *const kVerdict[RD_OP_VERDICTS] = {
    [RD_OP_V_NONE] = "not run",
    [RD_OP_V_PASS] = "the same bytes",
    [RD_OP_V_DIFFERS] = "differs",
    [RD_OP_V_UNCHECKED] = "not checked",
    [RD_OP_V_UNTESTED] = "not tested",
    [RD_OP_V_NOTHING] = "nothing fused on this device",
    [RD_OP_V_BLIND] = "its control did not differ",
    [RD_OP_V_DEVICE] = "the device failed",
};

const char *rd_op_verdict_name(int v)
{
    return v >= 0 && v < RD_OP_VERDICTS ? kVerdict[v] : "?";
}

const char *rd_op_classes_name(uint32_t classes)
{
    switch (classes & (RD_OP_PLAIN | RD_OP_CUT)) {
    case RD_OP_PLAIN:
        return "the pairs outside code 36";
    case RD_OP_CUT:
        return "code 36's cut and kick pairs drawn apart";
    case RD_OP_PLAIN | RD_OP_CUT:
        return "the pairs outside code 36 and code 36's cut and kick pairs drawn apart";
    default:
        return "none";
    }
}

/* ------------------------------------------------------------ the judge */

void rd_op_use_note(RdOpUse *u, int why)
{
    if (why == RD_OP_ONE || why == RD_OP_TRIAL) {
        u->one = true; /* drawn through it, or would have been at the off arm */
    } else if (why == RD_OP_UNMADE) {
        u->unmade = true;
    }
}

bool rd_op_use_counts(const RdOpUse *u)
{
    return u->one && !u->unmade;
}
