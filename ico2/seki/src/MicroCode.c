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

/* The five VU1 microprograms this table hands to the DMA, from cluster.o,
   mesh.o, normal_c.o, normal_l.o and particle.o. */
extern void ClusterMicroProgram();
extern void MeshMicroProgram();
extern void NormalCMicroProgram();
extern void NormalLMicroProgram();
extern void ParticleMicroProgram();

/* Indexed by the microprogram id the mesh and shadow paths pass around;
   slots 0 and 6 are unused. */
#ifdef ICO_HOST

/* The host has no VU1 microprograms (ico2/vusrc is assembled only by the PS2
   build) and a function address does not fit an int on 64-bit hosts; the
   addresses only reach DMA tags, which nothing consumes headless
   (port/null/gfx_null.c).  The renderer replaces the programs with shaders. */
int MicroCodeAddress[7] = {0};

#else

int MicroCodeAddress[7] = {
    0,
    (int)NormalCMicroProgram,
    (int)NormalLMicroProgram,
    (int)ClusterMicroProgram,
    (int)MeshMicroProgram,
    (int)ParticleMicroProgram,
    0,
};

#endif

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
#ifdef ICO_HOST
                /* no microprogram image on the host: the DMA tag keeps the
                   EE's address word, 0 (MicroCodeAddress above) */
                (void)q;
                dl_OpenDma(5, 0, 0);
#else
                dl_OpenDma(5, *q, 0);
#endif
                dl_CloseDma();
#ifdef ICO_RD
                /* R3ab: the program resident in this list from here on */
                rd_VuProgram(id);
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
 * layer builds for path 1 (docs/port/RENDER_API.md section 13).
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
 *   MSCAL / MSCALF other   rd_VuCall: SET_UVOFFSET, SET_*_MATRIX,
 *                          SET_*_LIGHT, the BEGIN codes
 *   MSCNT                  a batch: particle batches draw
 *                          (rd_DrawVuParticles); the mesh packets do not come
 *                          here (static meshes: RegistPacket.c; grids:
 *                          Primitive.c)
 *
 * The DMA chain forms: id 5 is a call into a chain of cnt tags whose upper
 * doubleword carries two VIF words (TTE), ending in ret; id 2 references
 * qwc quadwords of VIF codes. */

#define MC_HOST_WORDS 8192

static unsigned int mcHostWords[MC_HOST_WORDS];

static float mcHostTop[1024][4];

static unsigned int mcHostOnce;

static void mcHostOnceLog(int bit, const char *msg, unsigned int v)
{
    if ((mcHostOnce & (1u << bit)) == 0) {
        mcHostOnce |= 1u << bit;
        fprintf(stderr, "mc: %s (0x%x; reported once)\n", msg, v);
    }
}

/* SET_GSREGISTER (vu1_common.h:20-48): the GIF tag at TOP, then NLOOP
   quadwords to the GS. */
static void mcHostSetGsRegister(void)
{
    unsigned long long tag[2];
    unsigned int nloop, nreg;

    memcpy(tag, mcHostTop[0], 16);
    nloop = (unsigned int)(tag[0] & 0x7FFF);
    nreg = (unsigned int)((tag[0] >> 60) & 0xF);
    if (((tag[0] >> 58) & 3) != 0 || nreg != 1 || (tag[1] & 0xF) != 0xE) {
        mcHostOnceLog(0, "SET_GSREGISTER with a GIF tag that is not PACKED A+D: skipped",
                      (unsigned int)tag[1]);
        return;
    }
    if (nloop > 1023) {
        nloop = 1023;
    }
    gif_HostWriteRegs((const unsigned long long *)(const void *)mcHostTop[1], nloop);
}

/* MSCNT: a batch at TOP for the resident program. */
static void mcHostBatch(void)
{
    if (rd_VuCurrentProgram() == 5) {
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
        if (!rd_VuDrawFromState(&d)) {
            return;
        }
        memset(&pd, 0, sizeof(pd));
        pd.qw = (const float (*)[4])mcHostTop;
        pd.count = (uint32_t)(n > 80 ? 80 : n);
        pd.vu = d.vu;
        rd_DrawVuParticles(&pd, 0);
        return;
    }
    mcHostOnceLog(1, "a VU batch chained as a small packet is not drawn (program)",
                  (unsigned int)rd_VuCurrentProgram());
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
                rd_VuCall((int)imm, (const float (*)[4])mcHostTop, 1024);
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
            mcHostOnceLog(3, "VIF DIRECT in a VU1 chain: skipped", code);
            i += 4 * (imm ? imm : 65536);
            break;
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
