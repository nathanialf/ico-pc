#include "MicroCode.h"
#include "DisplayList.h"
#include "typedef.h"
#include "debug.h"
#include "DmaPacket.h"

#ifdef ICO_RD

#include <stdio.h>
#include <string.h>
#include "GifHost.h"
#include "rd_mesh.h"

#endif

/* Indexed by the microprogram id the mesh and shadow paths pass around;
   slots 0 and 6 are unused. */

/* The host has no VU1 microprograms (ico2/vusrc is assembled only by the PS2
   build) and a function address does not fit an int on 64-bit hosts; the
   addresses only reach DMA tags, which nothing consumes headless
   (port/null/gfx_null.c).  The renderer replaces the programs with shaders. */
int MicroCodeAddress[7] = {0};

/* The count of microprogram uploads
   this frame, and the program currently resident in each of the 13 VU1
   priority banks. */
static int mcUploadCount; /* derived name */

static int mcResident[16]; /* derived name */

/* The display-list packet builder state and one 64-bit packet slot; same
   objects src/GifPacket.c builds its packets in. */

static void mc_setBaseOffset(int base, int pri)
{
    char *c;
    char *q;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x10000000;
    PacketBufferStruct.ptr.c = c + 8;

    switch (base) {
    case 1:
    case 2: {
        char *p = PacketBufferStruct.ptr.c;

        ((GifPkWord *)p)->w[0] = 0x03000100;
        p += 4;
        PacketBufferStruct.ptr.d = (unsigned long long *)p;
        ((GifPkWord *)p)->w[0] = 0x02000180;
        PacketBufferStruct.ptr.c = p + 4;
    } break;
    case 3: {
        char *p = PacketBufferStruct.ptr.c;

        ((GifPkWord *)p)->w[0] = 0x03000100;
        p += 4;
        PacketBufferStruct.ptr.d = (unsigned long long *)p;
        ((GifPkWord *)p)->w[0] = 0x02000180;
        PacketBufferStruct.ptr.c = p + 4;
    } break;
    case 4: {
        char *p = PacketBufferStruct.ptr.c;

        ((GifPkWord *)p)->w[0] = 0x03000010;
        p += 4;
        PacketBufferStruct.ptr.d = (unsigned long long *)p;
        ((GifPkWord *)p)->w[0] = 0x020001F8;
        PacketBufferStruct.ptr.c = p + 4;
    } break;
    case 5: {
        char *p = PacketBufferStruct.ptr.c;

        ((GifPkWord *)p)->w[0] = 0x03000010;
        p += 4;
        PacketBufferStruct.ptr.d = (unsigned long long *)p;
        ((GifPkWord *)p)->w[0] = 0x0200016A;
        PacketBufferStruct.ptr.c = p + 4;
    } break;
    }

    q = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    PacketBufferStruct.ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = q + 0xC;
    ((GifPkWord *)(q + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = q + 0x10;
    dl_SetDLPriority(pri);
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
}

inline void mc_TransMicroCode(int id, int mask)
{
    int *q = &MicroCodeAddress[id];
    int i;
    for (i = 0; i < 13; i++) {
        if ((mask >> i) & 1) {
            if (id != mcResident[i]) {
                mcUploadCount++;
                mc_setBaseOffset(id, i);
                dl_SetDLPriority(i);
                /* no microprogram image on the host: the DMA tag keeps the
                   EE's address word, 0 (MicroCodeAddress above) */
                (void)q;
                dl_OpenDma(5, 0, 0);
                dl_CloseDma();
#ifdef ICO_RD
                /* R3ab: the program resident in this list from here on */
                rd_vu_program(id);
#endif
                mcResident[i] = id;
            }
        }
    }
}

/* The DEBUG build's trace of the microcode residency on the mode 1, light == 0
   path, switched by seki's debug-display bit; retail builds the switch as 0,
   jimaku.c's form.  It takes no argument: it reads the module's own state. */
#ifdef DEBUG
#define MC_DEBUG_TRACE (debug_font_flag & 1) /* derived name */
#else
#define MC_DEBUG_TRACE 0 /* derived name */
#endif

extern void mcTracePut(int uploads, int r0, int r1, int r2, int r3, int r4, int r5, int r6, int r7);

static inline void mcTrace(void) /* derived name */
{
    if (MC_DEBUG_TRACE) {
        mcTracePut(mcUploadCount, mcResident[0], mcResident[1], mcResident[2], mcResident[3],
                   mcResident[4], mcResident[5], mcResident[6], mcResident[7]);
    }
}

/* Every level of the selection is a switch except the light tests, and the
   three conditional codes are if/else pairs. */
void mc_SetMicroCode(int mode, int light, int pass, int clip, int pri)
{
    int code = 0xFFFF;
    char *c;

    switch (mode) {
    case 0:
        switch (light) {
        case 0:
            switch (clip) {
            case -1:
                code = 34;
                break;
            case 2:
                code = 36;
                break;
            default:
                code = 32;
                break;
            }
            break;
        case 3:
            code = 38;
            break;
        default:
            switch (pass) {
            case 0:
                if (clip == 2) {
                    code = 36;
                } else {
                    code = 32;
                }
                break;
            case 1:
                code = 34;
                break;
            case 2:
                code = 38;
                break;
            }
            break;
        }
        break;
    case 1:
        if (light == 0) {
            mcTrace();
            code = 20;
        } else {
            if (clip == -1) {
                switch (pass) {
                case 0:
                    if (debug_specular_flag != 1) {
                        code = 20;
                    } else {
                        code = 24;
                    }
                    break;
                case 1:
                    code = 22;
                    break;
                }
            } else {
                switch (pass) {
                case 0:
                    if (debug_specular_flag != 1) {
                        code = 20;
                    } else {
                        code = 24;
                    }
                    break;
                case 1:
                    code = 22;
                    break;
                }
            }
        }
        break;
    case 2:
        if (light == 0) {
            code = 20;
        } else {
            if (debug_specular_flag == 0) {
                code = 22;
            } else {
                code = 24;
            }
        }
        break;
    case 3:
        code = 18;
        break;
    }
    if (code == 0xFFFF) {
        return;
    }

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.tail.c = c;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.end.c = 0;
    ((GifPkWord *)c)->d = 0x10000000;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = code | 0x15000000;
    PacketBufferStruct.ptr.c = c + 0xC;
    ((GifPkWord *)(c + 0xC))->w[0] = 0x13000000;
    PacketBufferStruct.ptr.c = c + 0x10;
    PacketBufferStruct.tail.c = c + 0x10;
    ((GifPkWord *)(c + 0x10))->d = 0x60000000;
    PacketBufferStruct.ptr.c = c + 0x18;
    ((GifPkWord *)(c + 0x18))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0x1C;
    ((GifPkWord *)(c + 0x18))->w[1] = 0;
    PacketBufferStruct.ptr.c = c + 0x20;
    dl_SetDLPriority(pri);
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
#ifdef ICO_RD
    /* R3ab: the MSCALF code reaches the list's VU state */
    mc_HostDma(5, PacketBufferStruct.dma.c, 0);
#endif
}

inline void mc_Init(void)
{
    int *p = mcResident;
    int i = 0xC;
    mcUploadCount = 0;
    p += 0xC;
    do {
        *p = 0;
        i--;
        p--;
    } while (i >= 0);
}

inline void mc_Reset(void)
{
    int *p = mcResident;
    int i = 0xC;
    mcUploadCount = 0;
    p += 0xC;
    do {
        *p = 0;
        i--;
        p--;
    } while (i >= 0);
}

#ifdef ICO_RD

/* ===================================================================== *
 * PC port (renderer wave 3, R3ab): the VU1 side of the DMA chains the seki
 * layer builds for path 1.
 *
 * On the PS2 the VIF unpacks these chains into VU1 memory and starts the
 * resident microprogram at an MSCAL/MSCALF code.  The host reads the VIF
 * codes of the small packets (matrix, light, UV offset, material, texture,
 * dissolve, specular and reflection register packets, particle batches)
 * when they are chained, in list order:
 *
 *   UNPACK V4-32 with FLG  the quadwords into the TOP staging buffer
 *   MSCAL / MSCALF 0       SET_GSREGISTER: the GIF tag at TOP and its A+D
 *                          quadwords to the GS register decoder
 *                          (gif_HostWriteRegs), in order with the meshes
 *   MSCAL / MSCALF other   rd_vu_call: SET_UVOFFSET, SET_*_MATRIX,
 *                          SET_*_LIGHT, the BEGIN codes
 *   MSCNT                  a batch: particle batches draw
 *                          (rd_draw_vu_particles); the mesh packets do not come
 *                          here (static meshes: RegistPacket.c; grids:
 *                          Primitive.c)
 *   DIRECT / DIRECTHL      (wave 5, R5c) path 2: the GIF packets of the block
 *                          to the decoder (mcHostGif below; lightning.c)
 *
 * Since wave 5 (R5c) SET_GSREGISTER reads any GIF tag (mcHostGif), and the
 * raw packet builders outside seki chain theirs here too: darkVolume.c and
 * particleEffect.c (SET_GSREGISTER register packets), lightning.c (DIRECT).
 *
 * The DMA chain forms: id 5 is a call into a chain of cnt tags whose upper
 * doubleword carries two VIF words (TTE), ending in ret; id 2 references
 * qwc quadwords of VIF codes.  These are the only forms the seki layer and
 * the four raw builders chain (no ref/next/call: the host's DMA tags cannot
 * hold a 64-bit address, and nothing chained here needs one). */

#define MC_HOST_WORDS 8192

static unsigned int mcHostWords[MC_HOST_WORDS];

static float mcHostTop[1024][4];

static unsigned int mcHostOnce;

/* package I1: mc_HostParticleKey's emitter, 0 = none */
static const void *mcHostEmitter;

void mc_HostParticleKey(const void *emitter)
{
    mcHostEmitter = emitter;
}

static void mcHostOnceLog(int bit, const char *msg, unsigned int v)
{
    if ((mcHostOnce & (1u << bit)) == 0) {
        mcHostOnce |= 1u << bit;
        fprintf(stderr, "mc: %s (0x%x; reported once)\n", msg, v);
    }
}

/* ---------------------------------------------------------------------- *
 * Renderer wave 5 (R5c): GIF packets.  The register writes of a GIF packet
 * (a SET_GSREGISTER kick, a VIF DIRECT/DIRECTHL block on path 2) as the GIF
 * unpacks them, gathered as A+D pairs (data, register) and handed to the GS
 * register decoder (gif_HostWriteRegs) in packet order:
 *
 *   PACKED   per register descriptor, the 128-bit form of the GS manual's
 *            PACKED table: PRIM (bits 0..10), RGBAQ (R G B A in the low byte
 *            of each word, Q from the last PACKED ST), ST (S, T; Q kept for
 *            RGBAQ), UV (14 bits each in words 0 and 1), XYZF2 / XYZ2 (X, Y
 *            in words 0 and 1, Z in word 2 (bits 4..27 for XYZF2), F in word
 *            3 bits 4..11, ADC (word 3 bit 15) selects XYZF3 / XYZ3), FOG
 *            (F in word 3 bits 4..11), A+D (data in the low doubleword,
 *            register in bits 64..71), NOP; TEX0, CLAMP: the low doubleword.
 *            PRE writes the tag's PRIM field first.
 *   REGLIST  NLOOP x NREG doublewords, each the register's 64-bit value; a
 *            descriptor of A+D or NOP writes nothing; the block is padded to
 *            a whole quadword.
 *   IMAGE    NLOOP quadwords of HWREG data (an image transfer): skipped,
 *            logged once.
 *
 * FRAME_1 with PSM PSMCT24 (FRAME bits 24..29 = 1): the GS keeps the top byte
 * of every pixel (no alpha writes), which is FBMSK with the top byte set; the
 * decoder only reads FBP, FBW and FBMSK, so the pair passes on with
 * FBMSK |= 0xFF000000 (darkVolume.c's composite into the scene, FRAME
 * 0x1000040).  PSMCT16 frames would need more: logged once. */

#define MC_HOST_PAIRS 2048

static unsigned long long mcHostAd[2 * MC_HOST_PAIRS];

static unsigned int mcHostAdN;

/* the GIF's Q register: a PACKED ST sets it, a PACKED RGBAQ sends it */
static unsigned int mcHostPackedQ = 0x3F800000u;

static void mcHostAdFlush(void)
{
    if (mcHostAdN != 0) {
        gif_HostWriteRegs(mcHostAd, mcHostAdN);
        mcHostAdN = 0;
    }
}

static void mcHostAdPush(unsigned int reg, unsigned long long data)
{
    if (reg == 0x4C) {
        unsigned int psm = (unsigned int)((data >> 24) & 0x3F);

        if (psm == 1) {
            data |= 0xFF000000ULL << 32; /* PSMCT24: the alpha byte stays */
        } else if (psm != 0) {
            mcHostOnceLog(5, "FRAME_1 with a 16-bit PSM: drawn as PSMCT32", psm);
        }
    }
    if (mcHostAdN == MC_HOST_PAIRS) {
        mcHostAdFlush();
    }
    mcHostAd[2 * mcHostAdN] = data;
    mcHostAd[2 * mcHostAdN + 1] = reg;
    mcHostAdN++;
}

/* One PACKED quadword q[0..3] for register descriptor desc. */
static void mcHostPacked(unsigned int desc, const unsigned int *q)
{
    unsigned long long x = q[0] & 0xFFFF, y = q[1] & 0xFFFF;
    int adc = (q[3] >> 15) & 1;

    switch (desc) {
    case 0x0: /* PRIM */
        mcHostAdPush(0x00, q[0] & 0x7FF);
        break;
    case 0x1: /* RGBAQ */
        mcHostAdPush(0x01, (unsigned long long)(q[0] & 0xFF) |
                               ((unsigned long long)(q[1] & 0xFF) << 8) |
                               ((unsigned long long)(q[2] & 0xFF) << 16) |
                               ((unsigned long long)(q[3] & 0xFF) << 24) |
                               ((unsigned long long)mcHostPackedQ << 32));
        break;
    case 0x2: /* ST */
        mcHostAdPush(0x02, (unsigned long long)q[0] | ((unsigned long long)q[1] << 32));
        mcHostPackedQ = q[2];
        break;
    case 0x3: /* UV */
        mcHostAdPush(0x03, (unsigned long long)(q[0] & 0x3FFF) |
                               ((unsigned long long)(q[1] & 0x3FFF) << 16));
        break;
    case 0x4: /* XYZF2 */
    case 0xC: /* XYZF3 */
        mcHostAdPush(desc == 0x4 && !adc ? 0x04 : 0x0C,
                     x | (y << 16) | ((unsigned long long)((q[2] >> 4) & 0xFFFFFF) << 32) |
                         ((unsigned long long)((q[3] >> 4) & 0xFF) << 56));
        break;
    case 0x5: /* XYZ2 */
    case 0xD: /* XYZ3 */
        mcHostAdPush(desc == 0x5 && !adc ? 0x05 : 0x0D,
                     x | (y << 16) | ((unsigned long long)q[2] << 32));
        break;
    case 0xA: /* FOG */
        mcHostAdPush(0x0A, (unsigned long long)((q[3] >> 4) & 0xFF) << 56);
        break;
    case 0xE: /* A+D */
        mcHostAdPush(q[2] & 0xFF, (unsigned long long)q[0] | ((unsigned long long)q[1] << 32));
        break;
    case 0xF: /* NOP */
        break;
    default: /* TEX0_1/2, CLAMP_1/2 (and the reserved 0xB): the low doubleword */
        mcHostAdPush(desc, (unsigned long long)q[0] | ((unsigned long long)q[1] << 32));
        break;
    }
}

/* The GIF packets in w[0 .. 4 qwc).  oneTag: a SET_GSREGISTER kick sends
   one tag and its data (the EOP tag the VU code builds); a DIRECT block runs
   to its size.  Returns the quadwords read. */
static unsigned int mcHostGif(const unsigned int *w, unsigned int qwc, int oneTag)
{
    unsigned int at = 0;

    while (at < qwc) {
        const unsigned int *t = w + 4 * at;
        unsigned long long lo = t[0] | ((unsigned long long)t[1] << 32);
        unsigned long long hi = t[2] | ((unsigned long long)t[3] << 32);
        unsigned int nloop = (unsigned int)(lo & 0x7FFF);
        unsigned int eop = (unsigned int)((lo >> 15) & 1);
        unsigned int pre = (unsigned int)((lo >> 46) & 1);
        unsigned int flg = (unsigned int)((lo >> 58) & 3);
        unsigned int nreg = (unsigned int)((lo >> 60) & 0xF);
        unsigned int k, r;

        if (nreg == 0) {
            nreg = 16;
        }
        at++;
        if (flg == 0) { /* PACKED */
            if (pre) {
                mcHostAdPush(0x00, (lo >> 47) & 0x7FF);
            }
            for (k = 0; k < nloop; k++) {
                for (r = 0; r < nreg; r++) {
                    if (at >= qwc) {
                        goto out;
                    }
                    mcHostPacked((unsigned int)((hi >> (4 * r)) & 0xF), w + 4 * at);
                    at++;
                }
            }
        } else if (flg == 1) { /* REGLIST */
            unsigned int total = nloop * nreg, i;

            if (pre) {
                mcHostOnceLog(6, "GIF REGLIST tag with PRE: PRIM field not written",
                              (unsigned int)lo);
            }
            for (i = 0; i < total; i++) {
                const unsigned int *d;
                unsigned int desc = (unsigned int)((hi >> (4 * (i % nreg))) & 0xF);

                if (at + i / 2 >= qwc) {
                    goto out;
                }
                d = w + 4 * at + 2 * i; /* formed once it is known to be in range */
                if (desc != 0xE && desc != 0xF) {
                    mcHostAdPush(desc, (unsigned long long)d[0] | ((unsigned long long)d[1] << 32));
                }
            }
            at += (total + 1) / 2;
        } else { /* IMAGE */
            mcHostOnceLog(7, "GIF IMAGE data (HWREG transfer) on path 2: skipped", nloop);
            at += nloop;
        }
        if (oneTag) {
            if (!eop) {
                mcHostOnceLog(8, "SET_GSREGISTER GIF tag without EOP: the first tag only",
                              (unsigned int)lo);
            }
            break;
        }
    }
out:
    mcHostAdFlush();
    return at < qwc ? at : qwc;
}

/* SET_GSREGISTER (vu1_common.h:20-48): XGKICK of the GIF packet at TOP (R3ab
   read PACKED A+D tags only; since R5c any GIF tag, mcHostGif). */
static void mcHostSetGsRegister(void)
{
    mcHostGif((const unsigned int *)(const void *)mcHostTop, 1024, 1);
}

/* MSCNT: a batch at TOP for the resident program. */
static void mcHostBatch(void)
{
    if (rd_vu_current_program() == 5) {
        RdVuDraw d;
        RdVuParticleDraw pd;
        unsigned long long prim[2];
        int n;

        memcpy(&n, mcHostTop[0], 4);
        if (n <= 0) {
            return;
        }
        /* the sprites' GIF tag PRIM (0xD6: sprite, TME, ABE, AA1) as the
           GS keeps it */
        memcpy(&prim[0], mcHostTop[1], 8);
        prim[0] = (prim[0] >> 47) & 0x7FF;
        prim[1] = 0;
        gif_HostWriteRegs(prim, 1);
        if (!rd_vu_draw_from_state(&d)) {
            return;
        }
        memset(&pd, 0, sizeof(pd));
        pd.qw = (const float (*)[4])mcHostTop;
        pd.count = (uint32_t)(n > 80 ? 80 : n);
        pd.vu = d.vu;
        /* package I1: keyed by the emitter (prim_DispParticle), so a batch
           another emitter inserts ahead of it does not shift its match */
        rd_draw_vu_particles(&pd, mcHostEmitter ? RD_KEY(mcHostEmitter, 18, 0) : 0);
        return;
    }
    mcHostOnceLog(1, "a VU batch chained as a small packet is not drawn (program)",
                  (unsigned int)rd_vu_current_program());
}

/* The VIF code stream w[0..n). */
static void mcHostVif(const unsigned int *w, unsigned int n)
{
    unsigned int i = 0;

    while (i < n) {
        unsigned int code = w[i++];
        unsigned int cmd = (code >> 24) & 0x7F;
        unsigned int num = (code >> 16) & 0xFF;
        unsigned int imm = code & 0xFFFF;

        if (cmd >= 0x60) {
            /* UNPACK: vn = components - 1, vl = 32, 16, 8 or 5 bits */
            unsigned int vn = (cmd >> 2) & 3, vl = cmd & 3;
            unsigned int qn = num ? num : 256;
            unsigned int bits = (vl == 0 ? 32u : vl == 1 ? 16u : vl == 2 ? 8u : 16u);
            unsigned int words = vl == 3 ? (qn + 1) / 2 : (qn * (vn + 1) * bits + 31) / 32;

            if (vn == 3 && vl == 0 && (cmd & 0x10) == 0 && (imm & 0x8000)) {
                unsigned int at = imm & 0x3FF, k;

                for (k = 0; k < qn && i + 4 * k + 3 < n && at + k < 1024; k++) {
                    memcpy(mcHostTop[at + k], &w[i + 4 * k], 16);
                }
            } else {
                mcHostOnceLog(2, "VIF UNPACK other than V4-32 to TOP: skipped", code);
            }
            i += words;
            continue;
        }
        switch (cmd) {
        case 0x14: /* MSCAL */
        case 0x15: /* MSCALF */
            if (imm == 0) {
                mcHostSetGsRegister();
            } else {
                rd_vu_call((int)imm, (const float (*)[4])mcHostTop, 1024);
            }
            break;
        case 0x17: /* MSCNT */
            mcHostBatch();
            break;
        case 0x20: /* STMASK */
            i += 1;
            break;
        case 0x30: /* STROW */
        case 0x31: /* STCOL */
            i += 4;
            break;
        case 0x4A: /* MPG */
            i += 2 * (num ? num : 256);
            break;
        case 0x50: /* DIRECT */
        case 0x51: /* DIRECTHL */
        {
            /* R5c: path 2, the GIF packets of the next imm quadwords
               (lightning.c's strips) */
            unsigned int qn = imm ? imm : 65536;

            if (i + 4 * qn > n) {
                mcHostOnceLog(3, "VIF DIRECT past the end of its chain: cut", code);
                qn = (n - i) / 4;
            }
            mcHostGif(&w[i], qn, 0);
            i += 4 * qn;
            break;
        }
        default: /* NOP, STCYCL, OFFSET, BASE, ITOP, STMOD, MSKPATH3, MARK, FLUSH* */
            break;
        }
    }
}

void mc_HostDma(int id, const void *addr, int qwc)
{
    const unsigned char *p = (const unsigned char *)addr;
    unsigned int n = 0;

    if (p == 0) {
        return;
    }
    if (id == 2) {
        n = (unsigned int)qwc * 4;
        if (n > MC_HOST_WORDS) {
            n = MC_HOST_WORDS;
        }
        memcpy(mcHostWords, p, (size_t)n * 4);
    } else if (id == 5) {
        int guard;

        for (guard = 0; guard < 4096; guard++) {
            unsigned int tag[4], qn, tid;

            memcpy(tag, p, 16);
            qn = tag[0] & 0xFFFF;
            tid = (tag[0] >> 28) & 7;
            if (tid != 1 && tid != 6 && tid != 7) {
                mcHostOnceLog(4, "DMA tag other than cnt/ret/end in a VU1 chain: stopped", tag[0]);
                break;
            }
            if (n + 2 + qn * 4 > MC_HOST_WORDS) {
                break;
            }
            mcHostWords[n++] = tag[2];
            mcHostWords[n++] = tag[3];
            memcpy(&mcHostWords[n], p + 16, (size_t)qn * 16);
            n += qn * 4;
            p += 16 + (size_t)qn * 16;
            if (tid != 1) {
                break;
            }
        }
    } else {
        return;
    }
    mcHostVif(mcHostWords, n);
}

#endif /* ICO_RD */
