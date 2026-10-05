#include "MicroCode.h"
#include "DisplayList.h"
#include "typedef.h"
#include "debug.h"
#include "DmaPacket.h"

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
                dl_OpenDma(5, *q, 0);
                dl_CloseDma();
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
