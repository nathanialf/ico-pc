/* libsndn2.a(sound.o).  The _Sg family (the first 44 functions) and the 61
 * Sg entry points that follow are one member: sound.o's sections are .text
 * (0x56B0 in this revision), .rodata 0x1E0 (exactly the _Sg tick's and
 * _SgContParam's jump tables), .data 0x40 (the _Sg voice code's pan table)
 * and .bss 0x4B68 (the _Sg context blocks and the RPC client the Sg entry
 * points bind), and the member has no other sections for the entry points to
 * own. */
#include <eekernel.h>
#include <sifrpc.h>
#include <sifcmd.h>
#include <string.h>
#include "sound.h"

/* one 0x58-byte voice slot as SgSeStop walks them: the sequence or SE that
   keyed it and its state (0 free, 1 keyed off, 2 sounding) */
typedef struct { /* field names derived */
    char pad0[0x50];
    unsigned char owner; /* 0x50 */
    unsigned char state; /* 0x51 */
    char pad52[6];
} SgSlot; /* derived name */

/* the member's .bss, in address order: the 128 vab headers, the 48 voice
   slots, the 48 sequence contexts, the common context the IOP side polls,
   the two 4 KB packet pages, the EE side of the IOP to EE mailbox and the
   pointer the IOP is handed to it, the 128 SE volume words, the head block
   and the RPC client */
static unsigned char sgVabContext[128][12]; /* derived name */

static unsigned char sgSlotContext[48][88]; /* derived name */

static unsigned char sgSeqContext[48][84]; /* derived name */

static int sgComContext[20]; /* derived name */

static unsigned char sgPacketContext[2 * 4096] __attribute__((aligned(64))); /* derived name */

static unsigned char sgIop2EeBuf[512]; /* derived name */

static char *sgIop2EeContext; /* derived name */

static int sgSeContext[128] __attribute__((aligned(64))); /* derived name */

static int sgHeadContext[16]; /* derived name */

static sceSifRpcClientData sgClient; /* derived name */

void *_SgGetSlotContext(int slot)
{
    return sgSlotContext[slot];
}

void *_SgGetSeqContext(int seq)
{
    return sgSeqContext[seq];
}

void *_SgGetComContext(void)
{
    return sgComContext;
}

void *_SgGetVabContext(int vab)
{
    return sgVabContext[vab];
}

void _SgSetSeVolValue(int vab, int vol)
{
    sgSeContext[vab] = vol;
}

int _SgGetSeVolValue(int vab)
{
    return sgSeContext[vab];
}

void *_SgSetSeContext(void)
{
    return sgSeContext;
}

void *_SgGetHeadContext(void)
{
    return sgHeadContext;
}

void *_SgGetIop2EeContext(void)
{
    return sgIop2EeContext;
}

void *_SgGetPacketCntext(int page, int index)
{
    unsigned char *p = &sgPacketContext[index * 0x10];
    return (void *)(page * 0x1000 + (int)p);
}

/* The driver's tick: run every sequence context's event stream up to the next
 * delta time, then flush the four 64-bit key-on, key-off and dump masks the
 * events built to the IOP side and hand it the packet page the tick filled.
 * The sequence status word is read through a volatile view at every test, as
 * everywhere else in this file: the IOP side sets and clears its flags while
 * the EE walks the contexts.  The packet ring's page index at 0x3C and byte
 * count at 0x40 are read and written the same way, since the IOP consumes the
 * page this call just handed it. */
void _SgCalledTickProc(void)
{
    void *seq = _SgGetSeqContext(0);
    char *com = _SgGetComContext();
    unsigned char **head = _SgGetHeadContext();
    char *iop = _SgGetIop2EeContext();
    int i;

    _SgSeqSeRrEnd(seq);
    for (i = 0; i < 0x30; i++, seq += 0x54) {
        int st;

        if (*(volatile int *)seq & 0x2000) {
            continue;
        }
        st = *(volatile int *)seq & 0xF;
        if (st == 3 || st == 0xC) {
            while (*(int *)(seq + 0x14) <= 0) {
                int r = _SgTableEnvAdd(seq);

                if (r == -1) {
                    goto skip;
                }
                if (r == 1) {
                    switch (*(unsigned char *)(seq + 0x50) & 0xF0) {
                    case 0xF0:
                        switch (head[4][1]) {
                        case 0x2F:
                            _SgEndSeq(seq);
                            goto tick;
                        case 0x51:
                            _SgTempoChange(seq);
                            break;
                        }
                        break;
                    case 0xC0:
                        _SgProgChange(seq);
                        break;
                    case 0xB0:
                        switch (head[4][1]) {
                        case 1:
                            _SgContMod(seq);
                            break;
                        case 2:
                            _SgContModLoop(seq);
                            break;
                        case 6:
                            _SgContParam(seq);
                            break;
                        case 7:
                            _SgContVol(seq);
                            break;
                        case 10:
                            _SgContPan(seq);
                            break;
                        case 64:
                            _SgContDump(seq);
                            break;
                        case 65:
                            _SgContPolta(seq);
                            break;
                        case 96:
                            _SgContSeLoop(seq);
                            _SgDeltaTime(seq);
                            *(int *)(seq + 0x14) = 0;
                            goto tick;
                        case 98:
                            _SgContLoopCount(seq);
                            break;
                        case 99:
                            _SgContLoop(seq);
                            break;
                        }
                        break;
                    case 0xE0:
                        _SgBendForm(seq);
                        break;
                    case 0x80:
                        _SgSeqKeyOff(seq);
                        break;
                    case 0xA0:
                        _SgSeMain(seq);
                        break;
                    case 0x90:
                        _SgBgmMain(seq);
                        break;
                    }
                }
                _SgDeltaTime(seq);
            }
        tick:
            if (*(unsigned short *)(seq + 0x1E) != 0 || (*(volatile int *)seq & 8)) {
                *(int *)(seq + 0x14) = *(int *)(seq + 0x14) - *(int *)(seq + 0x10);
            }
            if (*(volatile int *)seq & 0x80) {
                *(int *)seq = *(volatile int *)seq & 0xFFFFFF7F;
                *(char *)(seq + 0x50) = *(unsigned char *)(seq + 0x24);
                *(char *)(seq + 0x51) = *(unsigned char *)(seq + 0x24);
                *(int *)(seq + 4) = *(int *)(seq + 0xC);
                if (*(volatile int *)seq & 4) {
                    *(int *)seq = *(volatile int *)seq | 8;
                } else {
                    *(int *)(seq + 0x14) = 0;
                    *(int *)seq = *(volatile int *)seq | 2;
                    *(int *)seq = *(volatile int *)seq & 0xFFFFFFBF;
                }
            }
        }
        _SgSetRealtimeVolume(seq);
    skip:;
    }
    _SgSetRealtimeTickProc();
    if (*(unsigned long long *)(com + 8) != *(unsigned long long *)com) {
        _SgSetPkAdd(0xC, 0, (int)(*(unsigned long long *)com & 0xFFFFFF),
                    (int)((*(unsigned long long *)com >> 24) & 0xFFFFFF));
        *(unsigned long long *)(com + 8) = *(unsigned long long *)com;
    }
    if (*(unsigned long long *)(com + 0x18) != *(unsigned long long *)(com + 0x10)) {
        _SgSetPkAdd(0xD, 0, (int)(*(unsigned long long *)(com + 0x10) & 0xFFFFFF),
                    (int)((*(unsigned long long *)(com + 0x10) >> 24) & 0xFFFFFF));
        *(unsigned long long *)(com + 0x18) = *(unsigned long long *)(com + 0x10);
    }
    if (*(unsigned long long *)(com + 0x20) != 0) {
        _SgSetPkAdd(0xA, 0, (int)(*(unsigned long long *)(com + 0x20) & 0xFFFFFF),
                    (int)((*(unsigned long long *)(com + 0x20) >> 24) & 0xFFFFFF));
        *(unsigned long long *)(com + 0x20) = 0;
    }
    if (*(unsigned long long *)(com + 0x28) != 0) {
        _SgSetPkAdd(0xB, 0, (int)(*(unsigned long long *)(com + 0x28) & 0xFFFFFF),
                    (int)((*(unsigned long long *)(com + 0x28) >> 24) & 0xFFFFFF));
        *(unsigned long long *)(com + 0x28) = 0;
    }
    _SgSndn2Remote(0x64, 1, _SgGetPacketCntext(*(int *)(com + 0x3C), 0), iop,
                   *(int *)(com + 0x40) << 4, 0x200);
    *(volatile int *)(com + 0x40) = 0;
    *(int *)(com + 0x3C) = (*(volatile int *)(com + 0x3C) + 1) & 1;
}

/* The EE to IOP packet ring in the common context: c[0xF] is the page the ring
 * lives in and c[0x10] the write index _SgGetPacketCntext resolves to a slot.
 * The index is read and bumped through a volatile view because the IOP side
 * polls it while the EE fills the ring. */
int _SgSetPkAdd(int cmd, int id, int word2, int word3)
{
    int *c = _SgGetComContext();
    volatile int *n = (volatile int *)&c[0x10];
    int *p = _SgGetPacketCntext(c[0xF], c[0x10]);

    if ((unsigned int)*n >= 0xFF) {
        return -1;
    }
    p[0] = cmd;
    p[1] = id;
    p[2] = word2;
    p[3] = word3;
    *n = *n + 1;
    return *n;
}

/* the pan table, 32 steps from hard left to hard right: each entry the
   two 7-bit channel levels, one per byte, that a voice's +0x20 takes */
static unsigned short panTable[32] /* derived name */ = {
    0x7F00, 0x7F08, 0x7F10, 0x7F18, 0x7F20, 0x7F28, 0x7F30, 0x7F38, 0x7F40, 0x7F48, 0x7F50,
    0x7F58, 0x7F60, 0x7F68, 0x7F70, 0x7878, 0x7878, 0x707F, 0x687F, 0x607F, 0x587F, 0x507F,
    0x487F, 0x407F, 0x387F, 0x307F, 0x287F, 0x207F, 0x187F, 0x107F, 0x087F, 0x007F,
};

/* Sound-effect note event: the event's program byte picks a record in the
 * head context's SE table at head[1], _SgSeKeyOnSlot finds a voice for it and
 * the whole voice slot is filled from that record, the sequence and the
 * program record, then keyed on through the pitch, volume and two register
 * packets.  A zero velocity is a key-off, a program below the table's base or
 * a full slot table leaves the cursor advanced and nothing else. */
int _SgSeMain(int *seq)
{
    int *vab = _SgGetVabContext(*(unsigned short *)((char *)seq + 0x18));
    char *com = _SgGetComContext();
    unsigned char **head = _SgGetHeadContext();
    long long mask = 1;
    unsigned char *s;
    int n;
    int off;
    int slot;

    if (head[4][2] == 0) {
        _SgSeKeyOff((char *)seq);
        return 0;
    }
    n = head[4][1] - head[0][6];
    if (n < 0) {
        seq[1] += 4;
        return 0;
    }
    off = n << 4;
    head[1] += off;
    *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A) = 0x40;
    slot = _SgSeKeyOnSlot(head[1][0], head[1][1], *(unsigned short *)((char *)seq + 0x18));
    if (slot == -1) {
        seq[1] += 4;
        return 0;
    }
    mask <<= slot;
    s = _SgGetSlotContext(slot);
    if (head[1][0xF] & 1) {
        *(int *)s = *(volatile int *)s | 4;
    } else {
        *(int *)s = *(volatile int *)s & 0xFFFFFFFB;
    }
    *(int *)s = *(volatile int *)s & 0xFFFFFFD7;
    s[0x4E] = head[4][1];
    s[0x4F] = *(unsigned char *)((char *)seq + 0x4E);
    s[0x50] = *(unsigned char *)((char *)seq + 0x4C);
    *(short *)(s + 0xC) = n;
    *(short *)(s + 0x10) = 0;
    *(int *)(s + 4) = *(int *)(com + 0x34);
    s[0x51] = 2;
    *(int *)(s + 8) = 0;
    s[0x52] = head[1][1];
    s[0x53] = head[1][0];
    s[0x54] = *(unsigned char *)((char *)seq + 0x18);
    s[0x55] = *(unsigned char *)((char *)seq + 0x1A);
    s[0x56] = *(unsigned char *)((char *)seq + 0x1C);
    *(short *)(s + 0x16) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1E);
    *(short *)(s + 0x18) = head[0][1];
    *(short *)(s + 0x1A) = head[4][2];
    *(short *)(s + 0x1C) = head[1][0xB];
    *(short *)(s + 0x1E) = head[2][0];
    *(short *)(s + 0x20) = panTable[head[1][0xC] >> 2];
    *(short *)(s + 0x22) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x13);
    *(short *)(s + 0x24) = *(char *)(head[1] + 3);
    *(short *)(s + 0x26) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A);
    *(short *)(s + 0x28) = head[1][0xD];
    *(short *)(s + 0x14) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1C);
    *(short *)(s + 0x2C) = head[4][3];
    *(short *)(s + 0x2A) = head[1][2];
    *(short *)(s + 0x2E) = head[1][0xA];
    *(short *)(s + 0x30) = head[1][0xC];
    *(int *)(s + 0x44) = 0;
    /* The vibrato enable bit is written through the volatile view on both
     * arms.  No data-model reason beyond the status word's is known for this
     * field. */
    if (head[1][0xF] & 0x20) {
        int e = head[1][0xE];

        *(volatile int *)s = *(volatile int *)s | 0x10;
        *(short *)(s + 0x12) = 0x7F;
        *(short *)(s + 0xE) = e;
    } else {
        *(volatile int *)s = *(volatile int *)s & 0xFFFFFFEF;
    }
    _SgPitchTableVag(slot, head[1][2], head[4][1], *(char *)(head[1] + 3),
                     *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A),
                     head[1][0xD], seq[0x10]);
    _SgSeqSeVolume(slot, seq);
    _SgSetPkAdd(3, slot, (vab[1] + *(unsigned short *)(head[1] + 4)) << vab[2], 0);
    _SgSetPkAdd(2, slot, *(unsigned short *)(head[1] + 6), *(unsigned short *)(head[1] + 8));
    if (seq[0] & 0x1000) {
        *(long long *)com = *(long long *)com | mask;
    } else if (head[1][0xF] & 0x80) {
        *(long long *)com = *(long long *)com | mask;
    } else {
        *(long long *)com = *(long long *)com & ~mask;
    }
    *(long long *)(com + 0x20) = *(long long *)(com + 0x20) | mask;
    if (head[1][0xF] & 2) {
        *(long long *)(com + 0x10) = *(long long *)(com + 0x10) | mask;
        _SgSetPkAdd(0x32, 8, slot, head[1][2]);
    } else {
        *(long long *)(com + 0x10) = *(long long *)(com + 0x10) & ~mask;
    }
    head[1] -= off;
    *(int *)(com + 0x34) = *(int *)(com + 0x34) + 1;
    seq[1] += 4;
    return 0;
}

/* Sequence note event: the program record picked by the event's note byte can
 * key on a whole chord, so the note count comes out of the record table's
 * first byte (0xFF means the single note the event names, bit 7 means keep
 * going after this one) and each note takes its own voice slot.  The slot is
 * filled from the record, the sequence and the program record, then keyed on
 * through the pan curve, the pitch and volume calls and the two register
 * packets. */
int _SgBgmMain(int *seq)
{
    int *vab = _SgGetVabContext(*(unsigned short *)((char *)seq + 0x18));
    char *com = _SgGetComContext();
    unsigned char **head = _SgGetHeadContext();
    long long mask;
    unsigned char *s;
    int cont = 0;
    int i;
    int off;
    int slot;
    int n;
    int first;

    if (head[4][2] == 0) {
        _SgSeqKeyOff(seq);
        return -1;
    }
    if (head[0][6] - head[4][1] > 0) {
        seq[1] += 3;
        return -1;
    }
    if (head[0][0] == 0xFF) {
        n = head[4][1] - head[0][6];
        first = n;
    } else {
        if (head[0][0] & 0x80) {
            n = head[0][0];
            cont = 1;
            n -= 0x80;
        } else {
            n = head[0][0];
        }
        first = 0;
    }
    for (i = first; i < n + 1; i++) {
        if (_SgIntoKeyOn(head[0][0], i, head[4][1]) == 0) {
            continue;
        }
        slot = _SgSeqKeyOnSlot(i);
        mask = 1;
        if (slot == -1) {
            break;
        }
        off = i << 4;
        head[1] += off;
        if (head[1][0xD] == 0xFF) {
            head[1] -= off;
            break;
        }
        mask <<= slot;
        s = _SgGetSlotContext(slot);
        if (head[1][0xF] & 1) {
            *(int *)s = *(volatile int *)s | 4;
        } else {
            *(int *)s = *(volatile int *)s & 0xFFFFFFFB;
        }
        *(int *)s = *(volatile int *)s & 0xFFFFFFDF;
        s[0x4E] = head[4][1];
        s[0x4F] = *(unsigned char *)((char *)seq + 0x4E);
        s[0x50] = *(unsigned char *)((char *)seq + 0x4C);
        *(short *)(s + 0xC) = i;
        *(short *)(s + 0x10) = 0;
        *(int *)(s + 4) = *(int *)(com + 0x34);
        s[0x51] = 1;
        *(int *)(s + 8) = 0;
        s[0x52] = head[1][1];
        s[0x53] = head[1][0];
        s[0x54] = *(unsigned char *)((char *)seq + 0x18);
        *(short *)(s + 0x16) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1E);
        *(short *)(s + 0x18) = head[0][1];
        *(short *)(s + 0x1A) = head[4][2];
        *(short *)(s + 0x1C) = head[1][0xB];
        *(short *)(s + 0x1E) = head[2][0];
        *(short *)(s + 0x20) = panTable[_SgPan(0, *(unsigned short *)((char *)seq + 0x4E)) >> 2];
        *(short *)(s + 0x22) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x13);
        *(short *)(s + 0x24) = *(char *)(head[1] + 3);
        *(short *)(s + 0x26) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A);
        *(short *)(s + 0x28) = head[1][0xD];
        *(short *)(s + 0x14) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1C);
        *(short *)(s + 0x2C) = head[4][3];
        *(short *)(s + 0x2A) = head[1][2];
        *(short *)(s + 0x2E) = head[1][0xA];
        *(short *)(s + 0x30) = *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x14);
        if (*(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1B) == 0x7F) {
            *(int *)s = *(volatile int *)s | 8;
        }
        if (head[1][0xF] & 0x20) {
            unsigned char *lfo = head[2] + 0x19;

            if (lfo[*(unsigned short *)((char *)seq + 0x4E) << 4] == 0) {
                goto nolfo;
            }
            *(int *)s = *(volatile int *)s | 0x10;
            *(short *)(s + 0xE) = head[1][0xE];
            *(short *)(s + 0x12) = lfo[*(unsigned short *)((char *)seq + 0x4E) << 4];
        } else {
        nolfo:
            *(short *)(s + 0x12) = 0;
            *(int *)s = *(volatile int *)s & 0xFFFFFFEF;
        }
        _SgPitchTableVag(slot, head[1][2], head[4][1], *(char *)(head[1] + 3),
                         *(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A),
                         head[1][0xD], 0x1000);
        _SgSeqSeVolume(slot, seq);
        _SgSetPkAdd(3, slot, (vab[1] + *(unsigned short *)(head[1] + 4)) << vab[2], 0);
        _SgSetPkAdd(2, slot, *(unsigned short *)(head[1] + 6), *(unsigned short *)(head[1] + 8));
        *(long long *)(com + 0x20) = *(long long *)(com + 0x20) | mask;
        if (head[1][0xF] & 0x80) {
            *(long long *)com = *(long long *)com | mask;
        } else {
            *(long long *)com = *(long long *)com & ~mask;
        }
        head[1] -= off;
        *(long long *)(com + 0x10) = *(long long *)(com + 0x10) & ~mask;
        *(int *)(com + 0x34) = *(int *)(com + 0x34) + 1;
        if (cont == 0) {
            break;
        }
    }
    seq[1] += 3;
    return 0;
}

/* One realtime tick over the 48 voice slots: the vibrato curve, the portamento
 * ramp and the two 16-bit fades, then the per sequence realtime flags and the
 * 127 SE volume entries.  The vibrato curve is the vab header's table at 0x38,
 * read as 16-bit start offsets and as 8-bit samples.
 *
 * The DEBUG build traces one voice (the one a debugger sets in sgTraceVoice):
 * when its portamento or its realtime pitch request is looked at, it prints
 * the voice's tick counter (0x10, cleared at key-on) and the last vab header
 * this tick has read.  Retail builds the voice as 0 and the trace as nothing.
 * The trace, its voice test, the field it fetches and the names are ours. */
#ifdef DEBUG

/* The DEBUG build's trace voice, defined nowhere in this tree */
extern int sgTraceVoice;

#define SG_TRACE_VOICE sgTraceVoice
#define SG_TRACE(what, v, t, h)                                                                    \
    printf("sg voice %2d %s tick %3d hd %08x\n", (v), (what), (t), (int)(h))
#else
#define SG_TRACE_VOICE 0
#define SG_TRACE(what, v, t, h)
#endif

void _SgSetRealtimeTickProc(void)
{
    int step;
    int range;
    unsigned char *s = _SgGetSlotContext(0);
    unsigned char *curve = 0;
    unsigned short *curve_ofs = 0;
    int *hd = 0; /* the last vab header read, NULL until one is */
    int *com = _SgGetComContext();
    int *q;
    unsigned int i;
    int one = 1;

    for (i = 0; i < 0x30; i++, s += 0x58) {
        unsigned short note;
        unsigned short bend;
        short fine;
        unsigned char porta;
        int ok;
        unsigned char rt;
        int upd;
        unsigned int tick;

        if (*(volatile int *)s & 0x100) {
            continue;
        }
        if (s[0x51] == 0 || s[0x51] == 3) {
            continue;
        }
        if (s[0x50] >= 0x30) {
            continue;
        }
        q = _SgGetSeqContext(s[0x50]);
        if ((*(volatile int *)q & 5) == 0) {
            continue;
        }
        porta = 0;
        ok = 0;
        note = s[0x4E];
        fine = *(short *)(s + 0x24);
        bend = *(unsigned short *)(s + 0x26);
        range = *(unsigned short *)(s + 0x28);
        step = *(unsigned short *)(s + 0x2A);
        if (*(volatile int *)s & 0x10) {
            if ((unsigned int)(*(volatile unsigned short *)((char *)q + 0x18) - 1) < 0x7F) {
                int *vab = _SgGetVabContext(*(unsigned short *)((char *)q + 0x18));
                int t = vab[0];

                /* t holds the header word, then the tag, then the table id */
                if (t != 0 && vab[2] != 0) {
                    hd = (int *)t;
                    t = hd[3];
                    if (t == 0x64685353) {
                        t = hd[6];
                        if (t != 0xFFFFFFFF) {
                            if (hd[0xE] == 0) {
                                *(volatile int *)s = *(volatile int *)s & 0xFFFFFFEF;
                            } else {
                                curve = (unsigned char *)hd[0xE];
                                curve_ofs = (unsigned short *)hd[0xE];
                                ok = 1;
                            }
                        }
                    }
                }
            }
            if (ok != one) {
                *(volatile int *)s = *(volatile int *)s & 0xFFFFFFEF;
            }
        }
        if ((*(volatile int *)s & 0x20) && s[0x51] == 2) {
            porta = one;
            if (i == SG_TRACE_VOICE) {
                tick = *(unsigned short *)(s + 0x10);
                SG_TRACE("porta", i, tick, hd);
            }
            if (*(short *)(s + 0x4C) < one) {
                porta = 0;
            }
        }
        rt = one;
        if (i == SG_TRACE_VOICE) {
            tick = *(unsigned short *)(s + 0x10);
            SG_TRACE("pitch", i, tick, hd);
        }
        if ((*(volatile int *)q & 0x400) == 0) {
            rt = 0;
        }
        if (ok) {
            int depth;

            if (*(unsigned short *)((char *)com + 0x3A) == 0x3C &&
                (*(unsigned short *)(s + 0x14) & 0xFFF) == 0x78) {
                if (*(unsigned short *)(s + 0x14) & 0xF000) {
                    unsigned short left = (*(unsigned short *)(s + 0x14) >> 12) - 1;

                    *(short *)(s + 0x14) = (*(unsigned short *)(s + 0x14) & 0xFFF) | (left << 12);
                    *(short *)(s + 0x10) =
                        *(unsigned short *)(s + 0x10) + (*(unsigned short *)(s + 0x14) & 0xFFF);
                } else {
                    *(short *)(s + 0x14) = *(unsigned short *)(s + 0x14) | 0x6000;
                }
            } else {
                *(short *)(s + 0x10) =
                    *(unsigned short *)(s + 0x10) + (*(unsigned short *)(s + 0x14) & 0xFFF);
            }
            tick = *(unsigned short *)(s + 0x10);
            if (tick >= 0xF0) {
                tick = *(short *)(s + 0x10) = (*(unsigned short *)(s + 0x14) & 0xFFF) >> 1;
            }
            depth = *(unsigned short *)(s + 0x12);
            bend = curve[curve_ofs[*(unsigned short *)(s + 0xE) + 1] + (tick >> 2)] * depth / 255 -
                   (((depth + 1) >> 1) - 0x40);
            if (s[0x51] == one) {
                if (*(unsigned short *)(s + 0x26) >= 0x40) {
                    int d = (*(unsigned short *)(s + 0x26) - 0x40) * *(unsigned short *)(s + 0x28);

                    note = note + d / 64;
                    fine = fine + (d / 4 - d / 64 * 16);
                } else {
                    int d = (0x40 - *(unsigned short *)(s + 0x26)) * *(unsigned short *)(s + 0x28);

                    note = note - d / 64;
                    fine = fine + (d / 64 * 16 - d / 4);
                }
                range = 1;
            }
        }
        if (porta) {
            float v;

            *(unsigned short *)(s + 0x4C) -= 1;
            *(float *)(s + 0x48) = *(float *)(s + 0x48) + *(float *)(s + 0x44);
            if (*(float *)(s + 0x48) > 480.0) {
                *(float *)(s + 0x48) = 480.0;
            }
            if (*(float *)(s + 0x48) < -480.0) {
                *(float *)(s + 0x48) = -480.0;
            }
            v = *(float *)(s + 0x48);
            note = note + (int)(v / 12.0f);
            fine = fine + (int)v % 12 * 4 / 3;
            if (*(short *)(s + 0x4C) <= 0) {
                *(volatile int *)s = *(volatile int *)s & 0xFFFFFFDF;
            }
        }
        if (rt || ok || porta) {
            _SgPitchTableVag(i, step, note, fine, bend, range, q[0x10]);
        }
        upd = one;
        if (s[0x51] != 2) {
            continue;
        }
        if ((*(volatile int *)q & 0x800) == 0) {
            upd = 0;
        }
        if (*(volatile int *)s & 0x40) {
            if (*(unsigned short *)(s + 0x34) == *(unsigned short *)(s + 0x36)) {
                *(volatile int *)s = *(volatile int *)s & 0xFFFFFFBF;
            } else {
                unsigned short n = *(unsigned short *)(s + 0x38) & 0x7FFF;

                *(short *)(s + 0x38) = n;
                if (n != 0) {
                    *(short *)(s + 0x1A) = _SgfadeParam(s[0x34], s[0x36], s[0x3A], s[0x38]);
                    *(short *)(s + 0x38) = *(unsigned short *)(s + 0x38) - 1;
                } else {
                    *(short *)(s + 0x1A) = *(unsigned short *)(s + 0x34);
                    *(volatile int *)s = *(volatile int *)s & 0xFFFFFFBF;
                }
                upd = 1;
            }
        }
        if (*(volatile int *)s & 0x80) {
            if (*(unsigned short *)(s + 0x3C) == *(unsigned short *)(s + 0x3E)) {
                *(volatile int *)s = *(volatile int *)s & 0xFFFFFF7F;
            } else {
                unsigned short n = *(unsigned short *)(s + 0x40) & 0x7FFF;

                *(short *)(s + 0x40) = n;
                if (n != 0) {
                    *(short *)(s + 0x30) = _SgfadeParam(s[0x3C], s[0x3E], s[0x42], s[0x40]);
                    *(short *)(s + 0x40) = *(unsigned short *)(s + 0x40) - 1;
                } else {
                    *(short *)(s + 0x30) = *(unsigned short *)(s + 0x3C);
                    *(volatile int *)s = *(volatile int *)s & 0xFFFFFF7F;
                }
                *(short *)(s + 0x20) = panTable[*(unsigned short *)(s + 0x30) >> 2];
                upd = 1;
            }
        }
        if (upd) {
            _SgSeqSeVolume(i, q);
        }
    }
    q = _SgGetSeqContext(0);
    for (i = 0; i < 0x30; i++, q = (int *)((char *)q + 0x54)) {
        if (*(volatile int *)q & 0x400) {
            *q = *(volatile int *)q & 0xFFFFFBFF;
        }
        if (*(volatile int *)q & 0x800) {
            *q = *(volatile int *)q & 0xFFFFF7FF;
        }
    }
    for (i = 1; i < 0x80; i++) {
        int v = _SgGetSeVolValue(i);

        if (v & 0x80) {
            int *vab = _SgGetVabContext(i);
            int *hd = (int *)vab[0];

            if (hd != 0 && vab[2] != 0) {
                unsigned char *p = (unsigned char *)hd[0x10];

                /* The level goes back to the volume table as the byte just
                 * stored. */
                if (p != 0 && hd[3] == 0x64685353) {
                    *p = v & 0x7F;
                    _SgSetSeVolValue(i, *p);
                }
            }
        }
    }
}

/* Realtime volume: mode 1 takes the SE volume table's value for the vab and
 * writes it at the head of the vab's 0x40 block, mode 2 takes the sequence's
 * own 0x34 level and paints it over every channel the 0x38 mask selects.  The
 * status word is read through a volatile view at each test, as elsewhere in
 * this file. Every matching voice then gets its 0x1E level and, for a keyed
 * voice, its 0x16 channel level, and is re-levelled through _SgSeqSeVolume. */
int _SgSetRealtimeVolume(int *seq)
{
    unsigned char *base;
    unsigned char *s;
    int mode = 0;
    int i;
    int k;

    if ((*(volatile int *)seq & 5) == 4) {
        int v = _SgGetSeVolValue(*(unsigned short *)((char *)seq + 0x18));

        if (v & 0x80) {
            *(int *)((char *)seq + 0x30) = 0xFFFF;
            mode = 1;
            *(int *)((char *)seq + 0x34) = v & 0x7F;
        }
    } else if ((*(volatile int *)seq & 5) == 1) {
        if (*(volatile int *)seq & 0x200) {
            mode = 2;
            seq[0] = *(volatile int *)seq & 0xFFFFFDFF;
        }
    }
    switch (mode) {
    case 1:
        base = (unsigned char *)*(
            int *)(*(int *)_SgGetVabContext(*(unsigned short *)((char *)seq + 0x18)) + 0x40);
        *base = *((unsigned char *)seq + 0x34);
        break;
    case 2:
        base = (unsigned char *)seq[2];
        if (*(int *)((char *)seq + 0x30) == 0xFFFF) {
            *base = *((unsigned char *)seq + 0x34);
        }
        if (*(int *)((char *)seq + 0x38) != 0) {
            for (i = 0; i < 0x10; i++) {
                if ((*(int *)((char *)seq + 0x38) >> i) & 1) {
                    *(char *)(base + (i << 4) + 0x1E) = *((unsigned char *)seq + 0x3C);
                }
            }
        }
        break;
    default:
        return -1;
    }
    s = _SgGetSlotContext(0);
    for (i = 0; i < 0x30; i++, s += 0x58) {
        k = s[0x51];
        if (k != 0 && k != 3 && s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
            *(short *)(s + 0x1E) = *base;
            if (k == 1) {
                unsigned char *vp = base + 0x1E;

                *(short *)(s + 0x16) = vp[s[0x4F] << 4];
            }
            _SgSeqSeVolume(i, seq);
        }
    }
    return 0;
}

/* Both arms fill the same three head slots from one table pointer (tb) and
 * one data pointer (p, reloaded from the header in the flag-4 arm). */
int _SgTableEnvAdd(int *seq)
{
    int *head = _SgGetHeadContext();
    int *vab;
    char *hdr;
    char *p;
    unsigned char *e;
    int magic;
    int cur;
    int b;
    int ret;
    unsigned char *tb;

    /* volatile: the vab id at 0x18 is read twice, once for the range check
       and again for the _SgGetVabContext call. */
    if ((unsigned int)(*(volatile unsigned short *)((char *)seq + 0x18) - 1) >= 127) {
        return -1;
    }
    vab = _SgGetVabContext(*(unsigned short *)((char *)seq + 0x18));
    hdr = (char *)vab[0];
    if (hdr == 0) {
        return -1;
    }
    p = (char *)seq[2];
    if (p == 0) {
        return -1;
    }
    magic = *(int *)(hdr + 0xC);
    head[3] = (int)hdr;
    if (magic != 0x64685353) {
        return -1;
    }
    cur = seq[1];
    e = (unsigned char *)(p + cur);
    head[4] = (int)e;
    b = *e;
    if (b & 0x80) {
        *((char *)seq + 0x50) = b;
        *((char *)seq + 0x51) = b;
    } else {
        seq[1] = cur - 1;
        *((char *)seq + 0x50) = *((unsigned char *)seq + 0x51);
        head[4] = (int)(p + (cur - 1));
    }
    ret = 1;
    if (seq[0] & 4) {
        *(short *)((char *)seq + 0x4E) = *(unsigned short *)((char *)seq + 0x4C);
        tb = *(unsigned char **)(head[3] + 0x44);
        p = *(char **)(head[3] + 0x40);
        head[0] = (int)tb + *(unsigned short *)(tb + *(unsigned char *)(head[4] + 3) * 2 + 2);
        head[2] = (int)p;
        head[1] = head[0] + 8;
    } else {
        int n;

        head[2] = (int)p;
        tb = (unsigned char *)*(int *)(head[3] + 0x30);
        *(short *)((char *)seq + 0x4E) = *((unsigned char *)seq + 0x50) & 0xF;
        n = *(unsigned char *)(p + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x12);
        head[0] = (int)tb + *(unsigned short *)(tb + n * 2 + 2);
        head[1] = head[0] + 8;
        if (*((unsigned char *)seq + 0x50) < 0xA0) {
            if (*(unsigned short *)(tb + n * 2 + 2) == 0xFFFF || *(unsigned short *)tb < n ||
                *(unsigned int *)(head[3] + 0x10) == 0xFFFFFFFF) {
                ret = 0;
                seq[1] += 3;
            }
        }
    }
    return ret;
}

int _SgSeqKeyOnSlot(int note)
{
    int *mgr = _SgGetComContext();
    int best_idx = -1;
    int best_val = -1;
    int i;
    int idx;
    unsigned char *obj;
    int divisor;
    int one;

    i = 0;
    do {
        idx = (unsigned int)mgr[0xC] % 0x30;
        obj = _SgGetSlotContext(idx);
        if ((*(int *)obj & 0x100) == 0 && obj[0x51] == 0) {
            return idx;
        }
        mgr[0xC]++;
    } while (++i < 0x30);
    divisor = 0x30;
    one = 1;
    i = 0x2F;
    do {
        idx = (unsigned int)mgr[0xC] % divisor;
        obj = _SgGetSlotContext(idx);
        if ((*(int *)obj & 0x100) == 0 && obj[0x51] == one) {
            int v = *(int *)(obj + 4);
            if ((unsigned int)v < (unsigned int)best_val) {
                best_idx = idx;
                best_val = v;
            }
        }
        mgr[0xC]++;
    } while (--i >= 0);
    return best_idx;
}

/* Pick a voice slot for an SE key-on.  Pass one looks for a slot already
 * keyed by this sound (state 2) with the same 0x53 owner and 0x54 key, pass
 * two for a free slot, and the last pass steals the cheapest slot: a keyed-off
 * slot (state 1) wins over a sounding one (state 2), and a sounding slot is
 * only stolen when its 0x52 priority is at or below the caller's.  The
 * rotating cursor mgr[0xC] is what spreads the search over the 48 slots. */
int _SgSeKeyOnSlot(int owner, int pri, int vab)
{
    int *mgr = _SgGetComContext();
    int off_idx = -1;
    int off_val = -1;
    int on_idx = -1;
    int on_val = -1;
    int i;
    int idx;
    unsigned char *obj;
    int divisor;
    int one;
    int two;

    if (owner != 0) {
        i = 0;
        do {
            idx = (unsigned int)mgr[0xC] % 0x30;
            obj = _SgGetSlotContext(idx);
            if ((*(int *)obj & 0x100) == 0 && obj[0x51] == 2 && obj[0x53] == owner &&
                obj[0x54] == vab) {
                return idx;
            }
            mgr[0xC]++;
        } while (++i < 0x30);
    }
    i = 0;
    do {
        idx = (unsigned int)mgr[0xC] % 0x30;
        obj = _SgGetSlotContext(idx);
        if ((*(int *)obj & 0x100) == 0 && obj[0x51] == 0) {
            return idx;
        }
        mgr[0xC]++;
    } while (++i < 0x30);
    divisor = 0x30;
    one = 1;
    two = 2;
    i = 0x2F;
    do {
        idx = (unsigned int)mgr[0xC] % divisor;
        obj = _SgGetSlotContext(idx);
        if ((*(int *)obj & 0x100) == 0) {
            if (obj[0x51] == one) {
                int v = *(int *)(obj + 4);
                if ((unsigned int)v < (unsigned int)off_val) {
                    off_idx = idx;
                    off_val = v;
                }
            } else if (obj[0x51] == two) {
                if (obj[0x52] <= pri) {
                    int v = *(int *)(obj + 4);
                    if ((unsigned int)v < (unsigned int)on_val) {
                        on_idx = idx;
                        on_val = v;
                    }
                }
            }
        }
        mgr[0xC]++;
    } while (--i >= 0);
    if (off_idx != -1) {
        return off_idx;
    }
    if (on_idx != -1) {
        return on_idx;
    }
    return -1;
}

int _SgSeKeyOff(char *seq)
{
    long long mask16 = 0;
    long long mask19 = 0;
    char *com = (char *)_SgGetComContext();
    char *head = (char *)_SgGetHeadContext();
    char *elem = (char *)_SgGetSlotContext(0);
    int i;
    for (i = 0; i < 0x30; i++, elem += 0x58) {
        char *q;
        if (*(unsigned char *)(elem + 0x51) != 2) {
            continue;
        }
        if (!(*(int *)elem & 4)) {
            continue;
        }
        q = *(char **)(head + 0x10);
        if (*(unsigned short *)(elem + 0x2C) != *(unsigned char *)(q + 3)) {
            continue;
        }
        if (*(unsigned char *)(elem + 0x4E) != *(unsigned char *)(q + 1)) {
            continue;
        }
        if (*(unsigned char *)(elem + 0x54) != *(unsigned short *)(seq + 0x18)) {
            continue;
        }
        if (*(unsigned char *)(elem + 0x50) == *(unsigned short *)(seq + 0x4C)) {
            mask16 |= (1LL << i);
        } else {
            mask19 |= (1LL << i);
        }
    }
    if (mask16 == 0) {
        mask16 = mask19;
    }
    for (i = 0; i < 0x30; i++) {
        if ((mask16 >> i) & 1) {
            *(long long *)(com + 0x28) |= (1LL << i);
        }
    }
    *(int *)(seq + 4) += 4;
    return 0;
}

/* Key off every voice the sequence owns: the 48 slots are matched on the
 * program byte e[1], the sequence's own 0x4E and 0x4C ids, an active 0x51 and
 * the 0x18 channel, and each match sets the sequence's bit in the common
 * context's 64-bit key-off mask at 0x28.  The slot status word is read through
 * a volatile view at every test because the tick proc updates it while the
 * sequence runs; the write-back is plain. */
int _SgSeqKeyOff(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    char *com = _SgGetComContext();
    int *head = _SgGetHeadContext();
    unsigned char *e = (unsigned char *)head[4];
    int i;

    for (i = 0; i < 0x30; i++, s += 0x58) {
        if (s[0x4E] == e[1]) {
            if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E)) {
                if (s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
                    if (s[0x51] == 1) {
                        if (s[0x54] == *(unsigned short *)((char *)seq + 0x18)) {
                            if ((*(volatile int *)s & 4) == 0) {
                                *(int *)s = *(volatile int *)s & 0xFFFFFFF7;
                            } else if ((*(volatile int *)s & 8) != 0) {
                                *(int *)s = *(volatile int *)s & 0xFFFFFFF7;
                            }
                            *(long long *)(com + 0x28) =
                                *(long long *)(com + 0x28) | ((long long)1 << i);
                        }
                    }
                }
            }
        }
    }
    seq[1] += 3;
    return 0;
}

int _SgIntoKeyOn(int count, int note, int key)
{
    int *r = (int *)_SgGetHeadContext();
    if (count == 0xFF) {
        count = 1;
    } else {
        int off = note << 4;
        int base = r[1] + off;
        unsigned char *b = (unsigned char *)base;
        r[1] = base;
        if (key < b[0]) {
            count = 0;
        } else {
            int c = b[1];
            count = (c >= key);
        }
        r[1] = base - off;
    }
    return count;
}

int _SgPitchTableVag(int slot, int step, int note, int fine, int bend, int range, int pitch)
{
    _SgSetPkAdd(4, slot, (step << 24) | (note << 16) | ((fine & 0xFF) << 8) | bend,
                (range << 24) | pitch);
    return 0;
}

/* Fold the slot's six 16-bit envelope and volume terms into one 64-bit product
 * and scale it by the two channel volumes at 0x44 and 0x48, giving the left and
 * right levels the IOP packet carries.  With the common context's 0x38 flag set
 * both sides take the larger magnitude, and a non-zero 0x2E folds the slot's
 * own attenuation into the top byte. */
int _SgSeqSeVolume(int voice, int *seq)
{
    unsigned char *slot = _SgGetSlotContext(voice);
    int *com = _SgGetComContext();
    long long m;
    short l;
    short r;

    m = (long long)*(unsigned short *)(slot + 0x16) * *(unsigned short *)(slot + 0x22);
    m = m * ((long long)*(unsigned short *)(slot + 0x1C) * *(unsigned short *)(slot + 0x1A));
    m = m * ((long long)*(unsigned short *)(slot + 0x18) * *(unsigned short *)(slot + 0x1E));
    l = (m * (*(unsigned short *)(slot + 0x20) >> 8) * seq[0x11]) >> 46;
    r = (m * (*(unsigned short *)(slot + 0x20) & 0xFF) * seq[0x12]) >> 46;
    if (*(unsigned short *)((char *)com + 0x38) == 1) {
        l = (l < 0) ? -l : l;
        r = (r < 0) ? -r : r;
        if (r < l) {
            r = l;
        } else {
            l = r;
        }
    }
    l = (l & 0xFFFF) >> 1;
    r = (r & 0xFFFF) >> 1;
    if (*(unsigned short *)(slot + 0x2E) != 0) {
        l = (*(unsigned short *)(slot + 0x2E) << 8) | (l >> 7);
        r = (*(unsigned short *)(slot + 0x2E) << 8) | (r >> 7);
    }
    _SgSetPkAdd(1, voice, l, r);
    return 0;
}

/* The head context's three tables: h[0] the common block, h[1] the slot table
 * and h[2] the sequence table, both indexed by a 16-byte record.  The slot
 * pointer is advanced over the record for the read and put back afterwards. */
int _SgPan(int tone, int ch)
{
    int *h = _SgGetHeadContext();
    unsigned char *seq = (unsigned char *)(h[2] + ch * 16);
    int v;

    h[1] += tone * 16;
    v = seq[0x14] + *(unsigned char *)(h[1] + 0xC) - 0x80;
    v += *(unsigned char *)(h[0] + 2);
    h[1] -= tone * 16;
    if (v >= 0x80) {
        v = 0x7F;
    }
    if (v < 0) {
        v = 0;
    }
    return v;
}

/* End of sequence: seq is the sequence context, seq[0] its status word, seq[1] the
 * event cursor, seq[5] the repeat state and 0x4C the sequence id the voices carry
 * at slot offset 0x50.  Every status word here is read through a volatile view
 * (the tick proc and the IOP both touch these while the sequence runs) and
 * written back plainly. */
void _SgEndSeq(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    int i;

    if (*(volatile int *)seq & 4) {
        seq[5] = 0;
        seq[0] = *(volatile int *)seq & 0xFFFFEFF7;
    } else {
        seq[1] = 0x110;
        seq[0] = *(volatile int *)seq & 0xFFFFFFFD;
    }
    seq[0] = *(volatile int *)seq | 0x40;
    for (i = 0; i < 48; i++, s += 0x58) {
        if (s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
            if (s[0x51] == 1) {
                *(int *)s = *(volatile int *)s & 0xFFFFFFEF;
                *(short *)(s + 0x26) = 0x40;
            }
        }
    }
    *((char *)seq + 0x51) = *((unsigned char *)seq + 0x50);
}

void _SgTempoChange(int *seq)
{
    unsigned char *p = (unsigned char *)(seq[2] + seq[1]);
    void *q = _SgGetComContext();
    *(unsigned short *)((char *)seq + 0x1E) = p[2] | (p[3] << 8);
    *(int *)((char *)seq + 0x10) =
        ((((int)*(unsigned short *)((char *)seq + 0x20) * *(unsigned short *)((char *)seq + 0x1E))
          << 12) /
         *(unsigned short *)((char *)q + 0x3A)) /
        0x3C;
    seq[1] += 4;
}

void _SgProgChange(int *seq)
{
    int *p;
    unsigned short idx;
    char *base;
    char *v2;
    if ((*(seq + 0)) & 2) {
        p = (int *)_SgGetHeadContext();
        idx = *((unsigned short *)(((char *)seq) + 0x4E));
        v2 = (char *)(*((int *)(((char *)p) + 0x10)));
        ;
        *((((char *)(*((int *)(((char *)p) + 8)))) + (idx << 4)) + 0x12) =
            *((unsigned char *)(v2 + 1));
        idx = *((unsigned short *)(((char *)seq) + 0x4E));
        base = ((char *)(*((int *)(((char *)p) + 8)))) + (idx << 4);
        *(base + 0x1A) = 0x40;
        idx = *((unsigned short *)(((char *)seq) + 0x4E));
        base = ((char *)(*((int *)(((char *)p) + 8)))) + (idx << 4);
        *(base + 0x1B) = 0x40;
    }
    *(seq + 1) += 2;
}

/* Modulation controller: with bit 8 of the status word set the event carries
 * its own slot key (0x2C against the event byte 3 and 0x4E against byte 4)
 * and the cursor advances 5, otherwise the value lands in the program record
 * at 0x19 and the sequence keys the slots itself for a 3 byte event.  Both
 * loops read AND write the slot status word through a volatile view. */
void _SgContMod(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    int *head = _SgGetHeadContext();
    int i;

    if (seq[0] & 8) {
        for (i = 0; i < 48; i++, s += 0x58) {
            if (s[0x51] == 2) {
                if (*(unsigned short *)(s + 0x2C) == *(unsigned char *)(head[4] + 3)) {
                    if (s[0x4E] == *(unsigned char *)(head[4] + 4)) {
                        if (s[0x54] == *(unsigned short *)((char *)seq + 0x18)) {
                            if (s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
                                *(short *)(s + 0x12) = *(unsigned char *)(head[4] + 2);
                                *(volatile int *)s = *(volatile int *)s | 0x10;
                            }
                        }
                    }
                }
            }
        }
        seq[1] += 5;
    } else {
        *(char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x19) =
            *(unsigned char *)(head[4] + 2);
        for (i = 0; i < 48; i++, s += 0x58) {
            if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E)) {
                if (s[0x54] == *(unsigned short *)((char *)seq + 0x18)) {
                    if (s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
                        if (s[0x51] == 1) {
                            *(short *)(s + 0x12) = *(unsigned char *)(head[4] + 2);
                            *(volatile int *)s = *(volatile int *)s | 0x10;
                        }
                    }
                }
            }
        }
        seq[1] += 3;
    }
}

/* Modulation loop-rate controller: the event byte maps to a tick period,
 * 240 / (60 - value * 58 / 127), which is stored as the slot's 0x14 rate and,
 * on the sequence-keyed path, into the program record at 0x1C. */
void _SgContModLoop(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    int *head = _SgGetHeadContext();
    int v = 240 / (60 - *(unsigned char *)(head[4] + 2) * 58 / 127);
    int i;

    if (seq[0] & 8) {
        for (i = 0; i < 48; i++, s += 0x58) {
            if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
                *(unsigned short *)(s + 0x2C) == *(unsigned char *)(head[4] + 3) &&
                s[0x4E] == *(unsigned char *)(head[4] + 4) &&
                s[0x54] == *(unsigned short *)((char *)seq + 0x18) &&
                s[0x50] == *(unsigned short *)((char *)seq + 0x4C) && s[0x51] == 2) {
                *(short *)(s + 0x14) = v;
            }
        }
        seq[1] += 5;
    } else {
        *(char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1C) = v;
        for (i = 0; i < 48; i++, s += 0x58) {
            if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
                s[0x54] == *(unsigned short *)((char *)seq + 0x18) &&
                s[0x50] == *(unsigned short *)((char *)seq + 0x4C) && s[0x51] == 1) {
                *(short *)(s + 0x14) = v;
            }
        }
        seq[1] += 3;
    }
}

/* Portamento controller: every keyed voice (state 2) of the event's channel
 * whose program, note and track match takes a glide time of the common tempo
 * times the event's rate over 15 at 0x4C and a per-tick pitch step of the
 * signed or unsigned depth byte over that time at 0x44.  The state and the
 * divisor sit in locals. */
void _SgContPolta(char *seq)
{
    char *p;
    char *mgr;
    char *ctx;
    char *s;
    int i;
    int n;
    int v;
    int two;

    p = (char *)_SgGetSlotContext(0);
    mgr = (char *)_SgGetComContext();
    ctx = (char *)_SgGetHeadContext();
    two = 2;
    n = 15;
    i = 47;
    do {
        if (*(unsigned char *)(p + 0x51) == two &&
            *(unsigned char *)(p + 0x54) == *(unsigned short *)(seq + 0x18)) {
            s = *(char **)(ctx + 0x10);
            if (*(unsigned short *)(p + 0x2C) == *(unsigned char *)(s + 0x4) &&
                *(unsigned char *)(p + 0x4E) == *(unsigned char *)(s + 0x5) &&
                *(unsigned char *)(p + 0x50) == *(unsigned short *)(seq + 0x4C)) {
                *(int *)p |= 0x20;
                v = (*(unsigned short *)(mgr + 0x3A) * *(unsigned char *)(s + 0x2)) / n;
                *(short *)(p + 0x4C) = (short)v;
                if (*(unsigned char *)(s + 0x3) & 0x80) {
                    *(float *)(p + 0x44) = (float)*(signed char *)(s + 0x3) / (float)(short)v;
                } else {
                    *(float *)(p + 0x44) = (float)*(unsigned char *)(s + 0x3) / (float)(short)v;
                }
            }
        }
        i -= 1;
        p += 0x58;
    } while (i >= 0);
    *(int *)(seq + 0x4) += 6;
}

/* Volume controller: with bit 8 of the status word set the event keys the
 * voices itself and their 0x34 target, 0x36 current and 0x38/0x3A step are
 * refreshed from the event and the common tempo; otherwise the value lands in
 * the program record at 0x13 and each matching voice is re-levelled through
 * _SgSeqSeVolume. */
void _SgContVol(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    char *com = _SgGetComContext();
    int *head = _SgGetHeadContext();
    int i;

    if (seq[0] & 8) {
        for (i = 0; i < 48; i++, s += 0x58) {
            if (s[0x51] == 2 && s[0x54] == *(unsigned short *)((char *)seq + 0x18)) {
                unsigned char *e = (unsigned char *)head[4];

                if (*(unsigned short *)(s + 0x2C) == e[4] && s[0x4E] == e[5] &&
                    s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
                    int v;

                    *(int *)s |= 0x40;
                    *(short *)(s + 0x34) = e[3];
                    *(short *)(s + 0x36) = *(unsigned short *)(s + 0x1A);
                    v = (e[2] << 2) * *(unsigned short *)(com + 0x3A) / 60;
                    *(short *)(s + 0x38) = v;
                    *(short *)(s + 0x3A) = v;
                }
            }
        }
        seq[1] += 6;
    } else {
        *(char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x13) =
            *(unsigned char *)(head[4] + 2);
        for (i = 0; i < 48; i++, s += 0x58) {
            if (s[0x51] == 1 && s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
                s[0x54] == *(unsigned short *)((char *)seq + 0x18) &&
                s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
                *(short *)(s + 0x22) = *(unsigned char *)(head[4] + 2);
                _SgSeqSeVolume(i, seq);
            }
        }
        seq[1] += 3;
    }
}

/* Pan controller, the pattern of _SgContVol: with bit 8 of the status word set
 * the event keys the voices itself and their 0x3C target, 0x3E current and
 * 0x40/0x42 step are refreshed from the event and the common tempo; otherwise
 * the value lands in the program record at 0x14 and every matching voice is
 * panned through _SgPan and the pan curve.  The voice's own pan byte pair at
 * 0xC is read through the slot table entry for the voice's index, not through
 * the walker. */
void _SgContPan(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    char *com = _SgGetComContext();
    int *head = _SgGetHeadContext();
    int i;

    if (seq[0] & 8) {
        if (*(unsigned short *)(com + 0x38) != 1) {
            for (i = 0; i < 48; i++, s += 0x58) {
                if (s[0x51] == 2 && s[0x54] == *(unsigned short *)((char *)seq + 0x18)) {
                    unsigned char *e = (unsigned char *)head[4];

                    if (*(unsigned short *)(s + 0x2C) == e[4] && s[0x4E] == e[5] &&
                        s[0x50] == *(unsigned short *)((char *)seq + 0x4C)) {
                        int v;

                        *(int *)s |= 0x80;
                        *(short *)(s + 0x3C) = e[3];
                        *(short *)(s + 0x3E) = *(unsigned short *)(s + 0x30);
                        v = (e[2] << 2) * *(unsigned short *)(com + 0x3A) / 60;
                        *(short *)(s + 0x40) = v;
                        *(short *)(s + 0x42) = v;
                    }
                }
            }
        }
        seq[1] += 6;
    } else {
        *(char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x14) =
            *(unsigned char *)(head[4] + 2);
        if (*(unsigned short *)(com + 0x38) != 1) {
            for (i = 0; i < 48; i++, s += 0x58) {
                if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
                    s[0x54] == *(unsigned short *)((char *)seq + 0x18) &&
                    s[0x50] == *(unsigned short *)((char *)seq + 0x4C) && s[0x51] == 1) {
                    unsigned char *p = sgSlotContext[i];

                    *(short *)(s + 0x30) = *(unsigned char *)(head[4] + 2);
                    *(short *)(s + 0x20) =
                        panTable[_SgPan(*(unsigned short *)(p + 0xC),
                                        *(unsigned short *)((char *)seq + 0x4E)) >>
                                 2];
                    _SgSeqSeVolume(i, seq);
                }
            }
        }
        seq[1] += 3;
    }
}

/* Dump (damper) controller: the program record's 0x1B byte takes the event's
 * value, and when it goes to zero every voice the sequence holds either gets
 * its bit set in the common context's 64-bit key-off mask or, if the damper is
 * still down, is marked 8.  The slot status word is read through a volatile
 * view at both sites. */
void _SgContDump(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    char *com = _SgGetComContext();
    int *head = _SgGetHeadContext();
    int i;

    *(char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1B) =
        *(unsigned char *)(head[4] + 1);
    if (*(unsigned char *)(head[4] + 1) == 0) {
        for (i = 0; i < 0x30; i++, s += 0x58) {
            if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
                s[0x54] == *(unsigned short *)((char *)seq + 0x18) &&
                s[0x50] == *(unsigned short *)((char *)seq + 0x4C) &&
                (*(volatile int *)s & 4) != 0) {
                if (*(unsigned char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) +
                                       0x1B) == 0) {
                    *(long long *)(com + 0x28) = *(long long *)(com + 0x28) | ((long long)1 << i);
                } else {
                    *(int *)s = *(volatile int *)s | 8;
                }
            }
        }
    }
    seq[1] += 3;
}

/* The SE loop event: seq[0] carries the voice flags, seq[1] the event cursor,
 * seq[2] the sequence data base and seq[3] the loop target offset.  The event
 * bytes are e[2] and e[3] (the 16-bit loop target) and e[4] the repeat count;
 * the running count lives at 0x22 and the byte the loop jumps to at 0x24.
 * The flag word is read and written through a volatile view because the tick
 * proc that runs the voice updates it. */
void _SgContSeLoop(int *seq)
{
    int *p = _SgGetHeadContext();
    unsigned char *e = (unsigned char *)*(int *)((char *)p + 0x10);
    char *tbl = (char *)seq[2];

    *(volatile int *)seq |= 0x80;
    if (e[4] != 0) {
        if (*(unsigned short *)((char *)seq + 0x22) == e[4]) {
            *(short *)((char *)seq + 0x22) = 0;
            *(volatile int *)seq &= 0xFFFFFF7F;
        } else {
            int v;

            seq[3] = (e[3] << 8) + e[2];
            v = *(unsigned char *)(tbl + seq[3]);
            *(unsigned short *)((char *)seq + 0x22) = *(unsigned short *)((char *)seq + 0x22) + 1;
            *(short *)((char *)seq + 0x24) = v;
        }
    } else {
        int w;

        seq[3] = (e[3] << 8) + e[2];
        w = *(unsigned char *)(tbl + seq[3]);
        *(short *)((char *)seq + 0x24) = w;
    }
    seq[1] += 5;
}

/* Parameter controller: the event's 0x2A selector picks one of the SPU voice
 * register fields in the head context's register block at head[1], packs the
 * event byte into it and then pushes the two packed words to every voice the
 * sequence holds through _SgSetPkAdd command 2.  The selectors that touch the
 * global reverb, and the one that only latches the 0x26 value, do not touch
 * any voice and just advance the cursor.  Every field is cleared by storing
 * the masked halfword back before the new bits are ORed in. */
void _SgContParam(int *seq)
{
    int *head = _SgGetHeadContext();
    unsigned char *s = _SgGetSlotContext(0);
    int i;

    switch (*(unsigned short *)((char *)seq + 0x2A)) {
    case 0: {
        unsigned char *e = (unsigned char *)head[4];

        *(short *)((char *)seq + 0x26) = e[2];
        *(short *)((char *)seq + 0x2A) = 0;
        goto end;
    }
    case 4: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 6) = *(unsigned short *)(r + 6) & 0xFF;
        *(short *)(r + 6) = *(unsigned short *)(r + 6) | ((0x7F - e[2]) << 8);
        break;
    }
    case 5: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 6) = *(unsigned short *)(r + 6) & 0xFF;
        *(short *)(r + 6) = (*(unsigned short *)(r + 6) | ((0x7F - e[2]) << 8)) | 0x8000;
        break;
    }
    case 6: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 6) = *(unsigned short *)(r + 6) & 0xFF0F;
        *(short *)(r + 6) = *(unsigned short *)(r + 6) | (((0x7F - e[2]) >> 3) << 4);
        break;
    }
    case 7: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 6) = *(unsigned short *)(r + 6) & 0xFFF0;
        *(short *)(r + 6) = *(unsigned short *)(r + 6) | (e[2] >> 3);
        break;
    }
    case 8: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 8) = *(unsigned short *)(r + 8) & 0x3F;
        *(short *)(r + 8) = *(unsigned short *)(r + 8) | ((0x7F - e[2]) << 6);
        *(short *)(r + 8) =
            *(unsigned short *)(r + 8) | (0x4000 - *(unsigned short *)((char *)seq + 0x2E));
        break;
    }
    case 9: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 8) = *(unsigned short *)(r + 8) & 0x3F;
        *(short *)(r + 8) = *(unsigned short *)(r + 8) | ((0x7F - e[2]) << 6) | 0x8000u;
        *(short *)(r + 8) =
            *(unsigned short *)(r + 8) | (0x4000 - *(unsigned short *)((char *)seq + 0x2E));
        break;
    }
    case 10: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 8) = *(unsigned short *)(r + 8) & 0xFFC0;
        *(short *)(r + 8) = *(unsigned short *)(r + 8) | ((0x7F - e[2]) >> 2);
        break;
    }
    case 11: {
        unsigned char *r = (unsigned char *)head[1];
        unsigned char *e = (unsigned char *)head[4];

        *(short *)(r + 8) = *(unsigned short *)(r + 8) & 0xFFC0;
        *(short *)(r + 8) = (*(unsigned short *)(r + 8) | ((0x7F - e[2]) >> 2)) | 0x20;
        break;
    }
    case 12: {
        unsigned char *e = (unsigned char *)head[4];

        if (e[2] >= 0x41) {
            *(short *)((char *)seq + 0x2E) = 0x4000;
        } else {
            *(short *)((char *)seq + 0x2E) = 0;
        }
        break;
    }
    case 15:
        SgSetReverbType(0, *(unsigned char *)(head[4] + 2));
        SgSetReverbType(1, *(unsigned char *)(head[4] + 2));
        goto end;
    case 16:
        SgSetReverbDepth(0, *(unsigned char *)(head[4] + 2), *(unsigned char *)(head[4] + 2));
        SgSetReverbDepth(1, *(unsigned char *)(head[4] + 2), *(unsigned char *)(head[4] + 2));
        goto end;
    case 17:
        SgSetReverbFeedback(0, *(unsigned char *)(head[4] + 2));
        SgSetReverbFeedback(1, *(unsigned char *)(head[4] + 2));
        goto end;
    case 18:
    case 19:
        SgSetReverbDelaytime(0, *(unsigned char *)(head[4] + 2));
        SgSetReverbDelaytime(1, *(unsigned char *)(head[4] + 2));
        goto end;
    }
    for (i = 0; i < 0x30; i++, s += 0x58) {
        if (s[0x51] == 1 && s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
            s[0x50] == *(unsigned short *)((char *)seq + 0x4C) &&
            (*(unsigned short *)((char *)seq + 0x2C) == 0xFF ||
             *(unsigned short *)(s + 0xC) == *(unsigned short *)((char *)seq + 0x2C))) {
            unsigned char *r = (unsigned char *)head[1];

            _SgSetPkAdd(2, i, *(unsigned short *)(r + 6), *(unsigned short *)(r + 8));
        }
    }
end:
    seq[1] += 3;
}

void _SgContLoopCount(void *seq)
{
    void *s0 = seq;
    void *p = _SgGetHeadContext();
    int t = *(unsigned short *)((char *)s0 + 0x28);
    int val;
    if (t == 1)
        goto case1;
    if (t >= 2)
        goto ge2;
    if (t == 0)
        goto case0;
    goto done;
ge2:
    if (t == 2)
        goto case12;
    goto done;
case0:
    {
        int q0 = *(int *)((char *)p + 0x10);
        int b0 = *(unsigned char *)((char *)q0 + 0x2);
        *(short *)((char *)s0 + 0x2A) = 0;
        *(short *)((char *)s0 + 0x26) = b0;
        goto done;
    }
case1:
case12:
    {
        int q12 = *(int *)((char *)p + 0x10);
        int b12 = *(unsigned char *)((char *)q12 + 0x2);
        *(short *)((char *)s0 + 0x2A) = b12;
    }
done:
    val = *(int *)((char *)s0 + 0x4);
    *(int *)((char *)s0 + 0x4) = val + 3;
}

/* Loop controller dispatch on the event byte: controller numbers 0 to 15 park
 * the value at 0x2C, 0x10 arms the loop, 0x14 latches the cursor into 0xC,
 * 0x1E steps the repeat count against its limit at 0x26 and 0x7F resets.  The
 * status word is read through a volatile view at every update, as elsewhere in
 * this file. */
void _SgContLoop(int *seq)
{
    int *head = _SgGetHeadContext();
    unsigned char *e = (unsigned char *)head[4];

    switch (e[2]) {
    case 0 ... 0xF:
        *(short *)((char *)seq + 0x2C) = e[2];
        *(short *)((char *)seq + 0x28) = 2;
        break;
    case 0x14:
        *(short *)((char *)seq + 0x24) = *(unsigned char *)((char *)seq + 0x50);
        *(int *)((char *)seq + 0xC) = seq[1];
        *(short *)((char *)seq + 0x28) = 0;
        *(short *)((char *)seq + 0x2A) = 0;
        break;
    case 0x1E:
        if (*(unsigned short *)((char *)seq + 0x26) == 0x7F) {
            seq[0] = *(volatile int *)seq | 0x80;
        } else {
            if (*(unsigned short *)((char *)seq + 0x22) >=
                *(unsigned short *)((char *)seq + 0x26)) {
                *(int *)((char *)seq + 0xC) = 0;
                seq[0] = *(volatile int *)seq & 0xFFFFFF7F;
                *(short *)((char *)seq + 0x22) = 0;
                *(short *)((char *)seq + 0x28) = 0;
                break;
            }
            *(short *)((char *)seq + 0x22) = *(unsigned short *)((char *)seq + 0x22) + 1;
            seq[0] = *(volatile int *)seq | 0x80;
        }
        *(short *)((char *)seq + 0x28) = 0;
        break;
    case 0x10:
        *(short *)((char *)seq + 0x28) = 1;
        break;
    case 0x7F:
        *(short *)((char *)seq + 0x28) = 2;
        *(short *)((char *)seq + 0x2C) = 0xFF;
        break;
    }
    seq[1] += 3;
}

void _SgBendForm(int *seq)
{
    unsigned char *s = _SgGetSlotContext(0);
    int *head = _SgGetHeadContext();
    int i;

    *(char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A) =
        *(unsigned char *)(head[4] + 1);
    for (i = 0; i < 0x30; i++, s += 0x58) {
        if (s[0x4F] == *(unsigned short *)((char *)seq + 0x4E) &&
            s[0x54] == *(unsigned short *)((char *)seq + 0x18) &&
            s[0x50] == *(unsigned short *)((char *)seq + 0x4C) && s[0x51] == 1) {
            *(short *)(s + 0x26) = *(unsigned char *)(head[4] + 1);
            _SgPitchTableVag(
                i, *(unsigned short *)(s + 0x2A), s[0x4E], *(short *)(s + 0x24),
                *(unsigned char *)(head[2] + (*(unsigned short *)((char *)seq + 0x4E) << 4) + 0x1A),
                *(unsigned short *)(s + 0x28), 0x1000);
        }
    }
    seq[1] += 2;
}

void _SgDeltaTime(char *s)
{
    unsigned char *base = *(unsigned char **)(s + 0x8);
    int acc = 0;
    unsigned char b;
    do {
        int idx = *(int *)(s + 0x4);
        acc <<= 7;
        b = base[idx];
        idx++;
        *(int *)(s + 0x4) = idx;
        acc |= (b & 0x7F);
    } while (b & 0x80);
    if (*(unsigned short *)(s + 0x1E) & 0xFFFF) {
        *(int *)(s + 0x14) += acc << 12;
    }
}

/* Release pass: a slot that is not held (0x100), whose IOP mailbox word (24
 * words to a row) says the voice is done and whose 0x8 counter has run two
 * ticks is cleared and its keys marked free; every keyed slot's counter then
 * advances.  Then every sequence context that is live and keyed (0x44) but has
 * no slot left playing it is cleared.  The status word is read through the
 * file's volatile view at both tests; the slot search reuses s, and the
 * sequence walker is the argument itself, stepped in place. */
void _SgSeqSeRrEnd(int *seq)
{
    char *q = (char *)seq;
    char *iop = _SgGetIop2EeContext();
    unsigned char *s = _SgGetSlotContext(0);
    int i;
    int j;

    for (i = 0; i < 48; i++, s += 0x58) {
        if ((*(int *)s & 0x100) == 0) {
            if (*(int *)(iop + ((i / 24) * 0x60 + (i % 24) * 4)) < 2 && s[0x51] != 3 &&
                (unsigned int)*(int *)(s + 8) >= 2) {
                memset(s, 0, 0x58);
                s[0x50] = 0xFF;
                s[0x56] = 0xFF;
                s[0x55] = 0xFF;
                s[0x54] = 0xFF;
            }
            if (s[0x51] != 0) {
                *(int *)(s + 8) = *(int *)(s + 8) + 1;
            }
        }
    }
    for (i = 0; i < 48; i++, q += 0x54) {
        if ((*(volatile int *)q & 0x2000) == 0) {
            if ((*(volatile int *)q & 0x44) == 0x44) {
                s = _SgGetSlotContext(0);
                for (j = 0; j < 48; j++, s += 0x58) {
                    if (s[0x50] == i) {
                        goto next;
                    }
                }
                memset(_SgGetSeqContext(i), 0, 0x54);
            }
        }
    next:;
    }
}

int _SgfadeParam(int target, int start, int total, int left)
{
    return ((target & 0xFF) + ((start & 0xFF) - (target & 0xFF)) * (left & 0xFF) / (total & 0xFF)) &
           0xFF;
}

/* Bring the driver up: hand the IOP side the uncached-accelerated address of
 * the EE to IOP mailbox, clear every context block, mark all 48 slots free and
 * set the common context's default tempo. */
void _SgInit(int hot)
{
    int buf[16];
    void *se = _SgSetSeContext();
    void *pk = _SgGetPacketCntext(0, 0);
    unsigned char *slot = _SgGetSlotContext(0);
    void *vab = _SgGetVabContext(0);
    char *com = _SgGetComContext();
    void *seq = _SgGetSeqContext(0);
    int i;

    sgIop2EeContext = (char *)((int)sgIop2EeBuf | 0x20000000);
    buf[0] = 0x1E;
    buf[1] = hot;
    buf[4] = 0;
    _SgSndn2Remote(0x65, 0, buf, buf, 0x40, 0x40);
    memset(slot, 0, 0x1080);
    memset(vab, 0, 0x600);
    memset(seq, 0, 0xFC0);
    memset(com, 0, 0x50);
    memset(pk, 0, 0x1000);
    memset(sgIop2EeContext, 0, 0x200);
    memset(se, 0, 0x200);
    for (i = 0; i < 48; i++, slot += 0x58) {
        slot[0x50] = 0xFF;
        slot[0x56] = 0xFF;
        slot[0x55] = 0xFF;
        slot[0x54] = 0xFF;
    }
    /* the common context is what the IOP side polls, so these four go out in
       the order they are written */
    *(volatile int *)(com + 0x48) = 0;
    *(volatile int *)(com + 0x44) = 1;
    *(volatile short *)(com + 0x3A) = 0x3C;
    *(volatile int *)(com + 0x40) = 0;
}

int _SgSndn2Remote(int rpc_number, int mode, void *sendbuf, void *recvbuf, int ssize, int rsize)
{
    return sceSifCallRpc(&sgClient, rpc_number, mode, sendbuf, ssize, recvbuf, rsize, 0, 0);
}

int SgSndn2RemoteInit(void)
{
    /* the IOP module writes the bind result into the client-data block, so the
       poll of its server word is volatile */
    volatile sceSifRpcClientData *cd = &sgClient;
    int i;

    FlushCache(0);
    sceSifInitRpc(0);
    do {
        if (sceSifBindRpc(&sgClient, 0x736E646E, 0) < 0) {
            return -1;
        }
        /* The spin's zero words are not source: ee-gcc pads any loop too
           short for the R5900 short-loop erratum with nops
           (mips.c:mips_r5900_lengthen_loops). */
        i = 10000;
        do {
            i--;
        } while (i > 0);
    } while (cd->serve == 0);
    return 0;
}

int SgSndn2RemoteSync(void)
{
    int ret = 0;
    int *p = _SgGetComContext();
    if (p[0x44 / 4] != 0) {
        ret = sceSifCheckStatRpc(&sgClient);
    }
    return ret;
}

void SgInit(void)
{
    _SgInit(0);
}

void SgInitHot(void)
{
    _SgInit(1);
}

void SgQuit(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        _SgSetPkAdd(0xB, i, 0xFFFFFF, 0);
        _SgSetPkAdd(0x28, i, 0, 0);
    }
    _SgSetPkAdd(0x1F, 0, 0, 0);
}

void SgCalledTickProc(void)
{
    void *r = _SgGetComContext();
    if (*(int *)((char *)r + 0x44)) {
        _SgCalledTickProc();
    }
}

void SgSetDigitalOutputMode(int mode)
{
    _SgSetPkAdd(0x32, 0xA, mode, 0);
}

int SgDmaWrite(unsigned int iop, unsigned int spu, unsigned int size)
{
    _SgDmaCommon(0x20, iop, spu, size);
    return 0;
}

int SgDmaRead(unsigned int spu, unsigned int iop, unsigned int size)
{
    _SgDmaCommon(0x21, iop, spu, size);
    return 0;
}

void _SgDmaCommon(int cmd, unsigned int iop, unsigned int spu, unsigned int size)
{
    /* the transfer counter at +0x48 is shared with the IOP side */
    volatile int *com = (volatile int *)_SgGetComContext();
    unsigned int w1;
    unsigned int w2;
    unsigned int w3;

    com[0x48 / 4] = com[0x48 / 4] + 1;
    w3 = (spu << 24) | (size & 0xFFFFFF);
    w2 = (iop << 16) | ((spu >> 8) & 0xFFFF);
    w1 = (com[0x48 / 4] << 8) | ((iop >> 16) & 0xFF);
    _SgSetPkAdd(cmd, w1, w2, w3);
}

int SgGetDmaTransferStatus(int mode)
{
    int ret = -1;
    /* both words are the EE and IOP ends of the same transfer counter; the
       spin below only terminates because the IOP updates +0x1C0 */
    volatile int *com = (volatile int *)_SgGetComContext();
    volatile int *i2e = (volatile int *)_SgGetIop2EeContext();

    if (mode != 0) {
        if (mode == 1) {
            while (i2e[0x1C0 / 4] != com[0x48 / 4]) {
                ;
            }
            ret = 1;
        }
    } else {
        ret = (i2e[0x1C0 / 4] == com[0x48 / 4]);
    }
    return ret;
}

int SgVabOpen(int iop, int *hd, int spu)
{
    int r;
    r = SgVabOpenFakeBody(hd, spu);
    if (r != -1) {
        SgDmaWrite(iop, spu, hd[1]);
    }
    return r;
}

int SgVabOpenFakeBody(int *hd, int spu)
{
    char *v;
    int ret = -1;
    int i;

    v = (char *)_SgGetVabContext(1);
    if (hd[0xC / 4] != 0x64685353) {
        return -1;
    }
    for (i = 1; i < 0x80; i++, v += 0xC) {
        if (*(int *)(v + 8) == 0) {
            if (*(unsigned int *)((char *)hd + 0x7C) == 0xFFFFFFFF) {
                *(int *)(v + 8) = 3;
                *(int *)(v + 4) = (unsigned int)spu >> 3;
            } else {
                *(int *)(v + 8) = 4;
                *(int *)(v + 4) = (unsigned int)spu >> 4;
            }
            *(int *)v = (int)hd;
            ret = i;
            hd[0x30 / 4] = hd[0x10 / 4] + (int)hd;
            hd[0x38 / 4] = hd[0x18 / 4] + (int)hd;
            hd[0x3C / 4] = hd[0x1C / 4] + (int)hd;
            hd[0x40 / 4] = hd[0x20 / 4] + (int)hd;
            hd[0x44 / 4] = hd[0x24 / 4] + (int)hd;
            break;
        }
    }
    return ret;
}

int SgVabClose(int vab)
{
    void *t;
    char *p;
    int i;
    int rv = -1;

    if ((unsigned int)(vab - 1) < 0x7F) {
        t = _SgGetVabContext(vab);
        if (*(int *)((char *)t + 8) != 0) {
            p = (char *)_SgGetSlotContext(0);
            for (i = 0; i < 0x30; i++, p += 0x58) {
                if (*(unsigned char *)(p + 0x54) == vab &&
                    (unsigned int)*(unsigned char *)(p + 0x50) < 0x30 &&
                    *(unsigned char *)(p + 0x51) != 3) {
                    int *obj = (int *)_SgGetSeqContext(*(unsigned char *)(p + 0x50));
                    int *q = (int *)_SgGetComContext();
                    *(long long *)((char *)q + 0x28) |= (long long)1 << i;
                    *obj |= 0x2000;
                    memset((char *)obj + 4, 0, 0x50);
                    *obj = 0;
                    *(int *)p |= 0x100;
                    memset(p + 4, 0, 0x54);
                    *(unsigned char *)(p + 0x50) = 0xFF;
                    *(unsigned char *)(p + 0x56) = 0xFF;
                    *(unsigned char *)(p + 0x55) = 0xFF;
                    *(unsigned char *)(p + 0x54) = 0xFF;
                    *(int *)p = 0;
                }
            }
        }
        memset(t, 0, 0xC);
        rv = 0;
    }
    return rv;
}

int SgBgmOpen(int vab, void *sq)
{
    char *obj;
    void *mgr;
    void *t;
    int i;
    int rv = -1;

    if ((unsigned int)vab < 0x80) {
        obj = (char *)_SgGetSeqContext(0);
        mgr = _SgGetComContext();
        t = _SgGetVabContext(vab);
        if (*(int *)((char *)sq + 0xC) == 0x71735353 && *(int *)((char *)t + 8) != 0) {
            for (i = 0; i < 0x30; i++, obj += 0x54) {
                *(volatile int *)obj |= 0x2000;
                if ((*(volatile int *)obj & 0xF) == 0) {
                    *(short *)(obj + 0x4C) = (short)i;
                    rv = i;
                    *(short *)(obj + 0x18) = (short)vab;
                    *(int *)(obj + 8) = (int)sq;
                    *(short *)(obj + 0x20) = *(unsigned short *)((char *)sq + 2);
                    *(volatile int *)obj |= 1;
                    *(short *)(obj + 0x1E) = *(unsigned short *)((char *)sq + 4);
                    *(int *)(obj + 4) = 0x110;
                    *(int *)(obj + 0x40) = 0x1000;
                    *(int *)(obj + 0x44) = 0x1000;
                    *(int *)(obj + 0x48) = 0x1000;
                    *(int *)(obj + 0x10) = (int)((*(volatile unsigned short *)(obj + 0x20) *
                                                  *(unsigned short *)(obj + 0x1E))
                                                 << 0xC) /
                                           (int)*(unsigned short *)((char *)mgr + 0x3A) / 0x3C;
                }
                *(volatile int *)obj &= 0xFFFFDFFF;
                if (rv != -1) {
                    break;
                }
            }
        }
    }
    return rv;
}

int SgBgmClose(int id)
{
    volatile int *p;
    int ret = -1;
    if ((unsigned int)id < 0x30) {
        p = (volatile int *)_SgGetSeqContext(id);
        p[0] |= 0x2000;
        if ((p[0] & 3) == 1) {
            memset((char *)p + 4, 0, 0x50);
            p[0] = 0;
            ret = 0;
        }
        p[0] &= 0xFFFFDFFF;
    }
    return ret;
}

void SgSetReverbEndAddr(int core, int addr)
{
    _SgSetPkAdd(0x14, core, addr, 0);
}

void SgSetReverbType(int core, int type)
{
    _SgSetPkAdd(0x15, core, type, 0);
}

void SgSetReverbDepth(int core, int left, int right)
{
    _SgSetPkAdd(0x16, core, left, right);
}

void SgSetReverbDelaytime(int core, int time)
{
    _SgSetPkAdd(0x17, core, time, 0);
}

void SgSetReverbFeedback(int core, int feedback)
{
    _SgSetPkAdd(0x18, core, feedback, 0);
}

void SgSetOutputMode(int mode)
{
    void *r = _SgGetComContext();
    *(short *)((char *)r + 0x38) = mode;
}

void SgSetTickMode(int tick)
{
    void *r = _SgGetComContext();
    *(short *)((char *)r + 0x3A) = tick;
}

int SgGetSlotStatus(int kind, int slot)
{
    int ret = 0;
    char *sc = (char *)_SgGetSlotContext(slot);
    char *sq = (char *)_SgGetSeqContext(0);

    switch (kind) {
    case 0:
        switch (*(unsigned char *)(sc + 0x51)) {
        case 1:
            ret = 1;
            break;
        case 2:
            ret = 2;
            break;
        case 3:
            ret = 4;
            break;
        }
        break;
    case 1:
        sq += slot * 0x54;
        /* the sequencer writes this word from the IOP side */
        if ((*(volatile int *)sq & 5) == 1) {
            ret = 1;
        }
        if ((*(volatile int *)sq & 5) == 4) {
            ret = 2;
        }
        break;
    }
    return ret;
}

void SgSetMasterVol(int core, int left, int right)
{
    _SgSetPkAdd(0x28, core, left, right);
}

int SgSetBgmVol(unsigned int id, int vol, int mask)
{
    int ret = -1;
    if (id < 0x30 && vol >= 0 && vol < 0x80) {
        int *p = (int *)_SgGetSeqContext(id);
        *(volatile int *)p |= 0x2000;
        if (mask == 0xFFFF) {
            ret = p[0x34 / 4];
            p[0x30 / 4] = mask;
            p[0x34 / 4] = vol;
        } else {
            p[0x38 / 4] = mask;
            ret = 0;
            p[0x3C / 4] = vol;
        }
        *(volatile int *)p |= 0x200;
        *(volatile int *)p &= 0xFFFFDFFF;
    }
    return ret;
}

int SgSetSeMasterVol(int vab, int vol)
{
    int ret = -1;
    if ((unsigned int)(vab - 1) < 0x7F && vol >= 0) {
        if (vol < 0x80) {
            int *p = (int *)_SgGetVabContext(vab);
            if (p[2] != 0) {
                ret = _SgGetSeVolValue(vab) & 0x7F;
                _SgSetSeVolValue(vab, vol | 0x80);
            }
        }
    }
    return ret;
}

void SgBgmPlay(unsigned int id)
{
    volatile int *p;
    void *t;

    if (id < 0x30) {
        /* the sequence object's status word is written by the IOP side, so
           every read and write of it is volatile */
        p = (volatile int *)_SgGetSeqContext(id);
        p[0] |= 0x2000;
        if (*(unsigned short *)((char *)p + 0x18) >= 1 &&
            *(unsigned short *)((char *)p + 0x18) <= 127 && (p[0] & 1)) {
            t = _SgGetVabContext(*(unsigned short *)((char *)p + 0x18));
            if (*(int *)((char *)t + 8) != 0) {
                p[0] |= 2;
                p[0] &= 0xFFFFFF8F;
            }
        }
        p[0] &= 0xFFFFDFFF;
    }
}

void SgBgmStop(unsigned int id, int mode)
{
    volatile int *seq;
    char *sl;
    char *com;
    char *body;
    char *q;
    int i;
    int want;
    int st;

    if (id >= 0x30) {
        return;
    }
    seq = (volatile int *)_SgGetSeqContext(id);
    seq[0] |= 0x2000;
    if (*(unsigned short *)((char *)seq + 0x18) < 1 ||
        *(unsigned short *)((char *)seq + 0x18) > 127) {
        goto cleanup;
    }
    if ((seq[0] & 1) == 0) {
        goto cleanup;
    }
    if (*(int *)((char *)_SgGetVabContext(*(unsigned short *)((char *)seq + 0x18)) + 8) == 0) {
        goto cleanup;
    }
    sl = (char *)_SgGetSlotContext(0);
    com = (char *)_SgGetComContext();
    if (mode < 0) {
        goto cleanup;
    }
    if (mode < 2) {
        seq[0] &= 0xFFFFFFDD;
        *(int *)((char *)seq + 4) = 0x110;
        want = mode;
        *(short *)((char *)seq + 0x22) = 0;
        seq[0] |= 0x10;
    } else {
        if (mode >= 4) {
            goto cleanup;
        }
        if ((seq[0] & 2) == 0) {
            if ((seq[0] & 0x50) == 0) {
                seq[0] |= 2;
                *(int *)seq &= 0xFFFFFFDF;
            }
            goto cleanup;
        }
        seq[0] &= 0xFFFFFFFD;
        want = mode - 2;
        seq[0] |= 0x20;
    }
    for (i = 0; i < 0x30; i++, sl += 0x58) {
        if (*(unsigned char *)(sl + 0x50) == id) {
            st = *(unsigned char *)(sl + 0x51);
            if (st == 1) {
                *(volatile int *)sl |= 0x100;
                *(short *)(sl + 0x12) = 0;
                *(short *)(sl + 0x26) = 0x40;
                *(volatile int *)sl &= 0xFFFFFFEF;
                *(volatile int *)sl &= 0xFFFFFEFF;
                *(long long *)(com + 0x28) |= (long long)1 << i;
                if (want == st) {
                    _SgSetPkAdd(2, i, 0, 0);
                }
            }
        }
    }
    body = *(char **)((char *)seq + 8);
    if (*(int *)(body + 0xC) == 0x71735353) {
        q = body + 0x109;
        for (i = 0xF; i >= 0; i--, q -= 0x10) {
            *q = 0;
        }
    }
cleanup:
    seq[0] &= 0xFFFFDFFF;
}

void SgSetBgmTempo(unsigned int id, int tempo)
{
    if (id < 0x30 && tempo >= 0 && tempo < 0x3C0) {
        int *p = (int *)_SgGetSeqContext(id);
        void *q = _SgGetComContext();
        *(short *)((char *)p + 0x1E) = tempo;
        *(volatile int *)p |= 0x2000;
        *(int *)((char *)p + 0x10) =
            ((((int)*(unsigned short *)((char *)p + 0x20) * *(unsigned short *)((char *)p + 0x1E))
              << 12) /
             *(unsigned short *)((char *)q + 0x3A)) /
            0x3C;
        *(volatile int *)p &= 0xFFFFDFFF;
    }
}

int SgGetBgmTempo(unsigned int id)
{
    int ret = -1;
    if (id < 0x30) {
        void *r = _SgGetSeqContext(id);
        ret = *(unsigned short *)((char *)r + 0x1E);
    }
    return ret;
}

int SgGetBgmStatus(int id)
{
    volatile int *p;
    int ret = -1;
    if ((unsigned int)id < 0x30) {
        p = (volatile int *)_SgGetSeqContext(id);
        p[0] |= 0x2000;
        if (p[0] & 1) {
            ret = (p[0] >> 1) & 1;
            if (p[0] & 0x20) {
                ret |= 2;
            }
        }
        p[0] &= 0xFFFFDFFF;
    }
    return ret;
}

int SgGetBgmChStatus(unsigned int id, int channel, int kind)
{
    int ret = -1;
    volatile int *p;
    char *ch;
    int v;

    if (id < 0x30 && channel >= 0 && channel < 0x10) {
        p = (volatile int *)_SgGetSeqContext(id);
        p[0] |= 0x2000;
        if (p[0] & 1) {
            ch = *(char **)((char *)p + 8);
            if (kind == 0) {
                v = *(unsigned char *)(ch + (channel << 4) + 0x12);
                if (v != 0xFF) {
                    ret = v;
                }
            }
        }
        p[0] &= 0xFFFFDFFF;
    }
    return ret;
}

int SgSetBgmPanpot(unsigned int id, int pan)
{
    int ret = -1;
    volatile int *p;
    char *body;
    char *tone;
    int i;

    if (id < 0x30 && pan >= 0 && pan < 0x80) {
        p = (volatile int *)_SgGetSeqContext(id);
        p[0] |= 0x2000;
        if ((p[0] & 5) == 1) {
            body = *(char **)_SgGetVabContext(*(unsigned short *)((char *)p + 0x18));
            tone = body + *(int *)(body + 0x10);
            for (i = 0; i < *(unsigned short *)tone + 1; i++) {
                *(char *)(tone + *(unsigned short *)(tone + i * 2 + 2) + 2) = pan;
            }
            ret = 0;
        }
        p[0] &= 0xFFFFDFFF;
    }
    return ret;
}

int SgSePlay(int vabflags, int prog, int tone)
{
    int ret = -1;
    int vabid = vabflags & 0x7F;
    volatile int *p;
    char *com;
    char *vab;
    char *body;
    unsigned short *tbl;
    unsigned int h;
    int i;

    if ((unsigned int)(vabid - 1) < 0x7F && prog >= 0 && prog < 0x80 && tone >= 0 && tone < 0x80) {
        p = (volatile int *)_SgGetSeqContext(0);
        for (i = 0; i < 0x30; i++, p = (volatile int *)((char *)p + 0x54)) {
            p[0] |= 0x2000;
            if ((p[0] & 0xF) == 0) {
                com = (char *)_SgGetComContext();
                vab = (char *)_SgGetVabContext(vabid);
                body = *(char **)vab;
                tbl = *(unsigned short **)(body + 0x3C);
                if (*(int *)(vab + 8) == 0 || *(int *)(body + 0xC) != 0x64685353 ||
                    *(unsigned int *)(body + 0x20) == 0xFFFFFFFF ||
                    (unsigned int)tbl == 0xFFFFFFFF || tbl[0] < prog || tbl[prog + 1] == 0xFFFF ||
                    tbl[h = tbl[prog + 1] / 2] < tone) {
                    p[0] &= 0xFFFFDFFF;
                    return ret;
                }
                {
                    p[0] |= 0xC;
                    *(int *)((char *)p + 8) = (int)((char *)tbl + tbl[tone + h + 1]);
                    *(short *)((char *)p + 0x1E) = 0x78;
                    *(short *)((char *)p + 0x18) = vabid;
                    *(short *)((char *)p + 0x1A) = prog;
                    *(short *)((char *)p + 0x1C) = tone;
                    *(short *)((char *)p + 0x4C) = i;
                    *(int *)((char *)p + 0x40) = 0x1000;
                    *(int *)((char *)p + 0x44) = 0x1000;
                    *(int *)((char *)p + 0x48) = 0x1000;
                    *(int *)((char *)p + 0x10) =
                        ((0x78 * 240) << 12) / 0x3C / *(unsigned short *)(com + 0x3A);
                    if (vabflags & 0x8000) {
                        p[0] |= 0x1000;
                    }
                    ret = i;
                }
            }
            p[0] &= 0xFFFFDFFF;
            if (ret != -1) {
                break;
            }
        }
    }
    return ret;
}

void SgSeStop(int id)
{
    unsigned int idx = id & 0x7FFF;
    if (idx < 0x30) {
        volatile int *seq = _SgGetSeqContext(idx);
        *seq |= 0x2000;
        if (*seq & 0x4) {
            int mask8000 = id & 0x8000;
            SgSlot *slot = _SgGetSlotContext(0);
            char *com = _SgGetComContext();
            int i;
            *seq &= 0xFFFFFF77;
            *seq |= 0x40;
            for (i = 0; i < 0x30; i++, slot++) {
                if (slot->state != 2)
                    continue;
                if (slot->owner != idx)
                    continue;
                if (mask8000) {
                    _SgSetPkAdd(2, i, 0, 0);
                }
                *(long long *)(com + 0x28) |= 1LL << i;
            }
        }
        *seq &= 0xFFFFDFFF;
    }
}

void SgSeStopAll(int immediate)
{
    int i;
    volatile int *p = (volatile int *)_SgGetSeqContext(0);
    for (i = 0; i < 0x30; i++) {
        p[0] |= 0x2000;
        if ((p[0] & 5) == 4) {
            SgSeStop(i | (immediate << 15));
        }
        p[0] &= 0xFFFFDFFF;
        p = (volatile int *)((char *)p + 0x54);
    }
}

void SgSetSeVolDirect(unsigned int id, int left, int right)
{
    if (id < 0x30 && left >= -0x1000 && left < 0x1001 && right >= -0x1000 && right < 0x1001) {
        int *p = (int *)_SgGetSeqContext(id);
        p[0x44 / 4] = left;
        p[0x48 / 4] = right;
        *(volatile int *)p |= 0x2000;
        *(volatile int *)p |= 0x800;
        *(volatile int *)p &= 0xFFFFDFFF;
    }
}

void SgSetSePitchDirect(unsigned int id, int pitch)
{
    volatile int *p;
    int v, v2, v3;
    if (id >= 0x30)
        return;
    if (pitch < 0)
        return;
    if (pitch >= 0x4000)
        return;
    p = (volatile int *)_SgGetSeqContext(id);
    v = p[0];
    *(int *)((char *)p + 0x40) = pitch;
    v |= 0x2000;
    p[0] = v;
    v2 = p[0];
    v2 |= 0x400;
    p[0] = v2;
    v3 = p[0];
    v3 = (int)((unsigned int)v3 & 0xFFFFDFFFU);
    p[0] = v3;
}

int SgGetSpuSlotMalloc(int mode)
{
    char *com;
    char *sl;
    int found = -1;
    unsigned int best = 0xFFFFFFFF;
    int first;
    int last;
    int i;
    int st;
    unsigned int t;

    com = (char *)_SgGetComContext();
    switch (mode) {
    case 0:
        first = 0;
        last = 0x30;
        break;
    case 1:
        first = 0;
        last = 0x18;
        break;
    case 2:
        first = 0x18;
        last = 0x30;
        break;
    default:
        return -1;
    }
    sl = (char *)_SgGetSlotContext(first);
    for (i = first; i < last; i++, sl += 0x58) {
        st = *(unsigned char *)(sl + 0x51);
        if (st == 0) {
            *(volatile int *)sl |= 0x100;
            *(unsigned char *)(sl + 0x51) = 3;
            found = i;
            *(volatile int *)sl &= 0xFFFFFEFF;
            goto done;
        }
        t = *(unsigned int *)(sl + 4);
        if (t < best) {
            if (st != 3) {
                best = t;
                found = i;
            }
        }
    }
    if (found == -1) {
        return -1;
    }
    sl = (char *)_SgGetSlotContext(found);
    *(int *)sl |= 0x100;
    _SgSetPkAdd(2, found, 0, 0);
    *(long long *)(com + 0x28) |= (long long)1 << found;
    memset(sl + 4, 0, 0x54);
    *(unsigned char *)(sl + 0x51) = 3;
    *(int *)sl = 0;
    *(unsigned char *)(sl + 0x50) = 0xFF;
    *(unsigned char *)(sl + 0x56) = 0xFF;
    *(unsigned char *)(sl + 0x55) = 0xFF;
    *(unsigned char *)(sl + 0x54) = 0xFF;
done:
    *(long long *)(com + 0x10) &= ~((long long)1 << found);
    return found;
}

int SgSetSpuSlotFree(unsigned int slot)
{
    if (slot < 0x30) {
        unsigned char *p = (unsigned char *)_SgGetSlotContext(slot);
        if (p[0x51] == 3) {
            p[0x51] = 0;
        }
    }
    return -1;
}

void SgStAdpcmInit(void)
{
    _SgSetPkAdd(0x3C, 0, 0, 0);
}

void SgStAdpcmQuit(void)
{
    _SgSetPkAdd(0x3D, 0, 0, 0);
}

int SgStAdpcmOpen(void *req)
{
    char *p = (char *)req;
    int c;
    int b0;
    int v4;
    int v10;
    int v14;
    int v8;
    unsigned int w1;
    unsigned int w2;
    unsigned int w3;

    _SgGetComContext();
    c = *(int *)(p + 0xC);
    /* RECONSTRUCTION: the empty asm with the record as its memory output
     * stands in for Sony's text: the object reads the 0xC word first, then
     * the other five words after both `lui 0xff` mask constants, which
     * places a write of the record between those reads; the reads
     * themselves are plain. */
    __asm__("" : "=m"(*(struct { int w[6]; } *)p));
    b0 = *(unsigned char *)p;
    v4 = *(int *)(p + 4);
    v10 = *(int *)(p + 0x10);
    v14 = *(int *)(p + 0x14);
    v8 = *(int *)(p + 8);
    w3 = (c << 24) | (v8 & 0xFFFFFF);
    w2 = (v10 << 16) | (((unsigned int)c >> 8) & 0xFFFF);
    w1 = (b0 << 24) | (v4 & 0xFF0000) | (v14 & 0xFF00) | (((unsigned int)v10 & 0xFF0000) >> 16);
    _SgSetPkAdd(0x3E, w1, w2, w3);
    return 0;
}

int SgStAdpcmClose(unsigned int ch)
{
    int ret = -1;
    if (ch < 0x30) {
        _SgSetPkAdd(0x3F, ch, 0, 0);
        ret = 0;
    }
    return ret;
}

int SgStAdpcmChannelVolume(unsigned long long mask, unsigned int left, int right)
{
    int ret = -1;
    if (left < 0x4000 && right >= 0 && right < 0x4000 && (mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x40, (int)(mask & 0xFFFFFF), (int)((mask >> 24) & 0xFFFFFF),
                    (left << 16) | right);
        ret = 0;
    }
    return ret;
}

int SgStAdpcmChannelPitch(unsigned long long mask, int pitch)
{
    int ret = -1;
    if (((mask & 0xFF000000) == 0) && (pitch >= 0) && (pitch <= 0x2EE00)) {
        _SgSetPkAdd(0x41, (int)(mask & 0xFFFFFF), (int)((mask >> 24) & 0xFFFFFF), pitch);
        ret = 0;
    }
    return ret;
}

int SgStAdpcmPlay(unsigned long long mask)
{
    int ret = -1;
    if ((mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x42, (int)(mask & 0xFFFFFF), (int)((mask >> 24) & 0xFFFFFF), 0);
        ret = 0;
    }
    return ret;
}

int SgStAdpcmStop(unsigned long long mask)
{
    int ret = -1;
    if ((mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x43, (int)(mask & 0xFFFFFF), (int)((mask >> 24) & 0xFFFFFF), 0);
        ret = 0;
    }
    return ret;
}

int SgStAdpcmIopReadAddr(int ch)
{
    int ret = 0;
    if ((unsigned int)ch < 0x30) {
        char *base = _SgGetIop2EeContext();
        ret = *(int *)(base + (ch % 0x18) * 4 + (ch / 0x18) * 0x60 + 0xC0);
    }
    return ret;
}

void SgStPcmInit(void)
{
    _SgSetPkAdd(0x46, 0, 0, 0);
}

void SgStPcmQuit(void)
{
    _SgSetPkAdd(0x47, 0, 0, 0);
}

int SgStPcmOpen(int *req)
{
    unsigned int n1, n2;
    int v, v2, ret;
    ret = -1;
    n1 = req[2];
    if ((unsigned int)0x1FFFFF < n1)
        goto done;
    n2 = req[3];
    if ((unsigned int)0x1FFFFF < n2)
        goto done;
    v = req[0];
    if (v < 0)
        goto done;
    if (v < 0x10) {
        v2 = req[1];
        _SgSetPkAdd(0x48, (v << 24) | v2, n1, n2);
        ret = 0;
    }
done:
    return ret;
}

int SgStPcmClose(unsigned int ch)
{
    int ret = -1;
    if (ch < 0x10) {
        _SgSetPkAdd(0x49, ch, 0, 0);
        ret = 0;
    }
    return ret;
}

void SgStPcmSetEffect(int effect)
{
    _SgSetPkAdd(0x4E, effect, 0, 0);
}

int SgStPcmPlay(unsigned long long mask)
{
    if ((mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x4B, (int)mask, 0, 0);
    }
    return 0;
}

int SgStPcmStop(unsigned long long mask)
{
    if ((mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x4C, (int)mask, 0, 0);
    }
    return 0;
}

int SgStPcmLseek(unsigned int ch, unsigned int offset)
{
    int ret = -1;
    if (ch < 0x10) {
        if (offset <= 0x1FFFFF) {
            _SgSetPkAdd(0x4D, (int)ch, (int)offset, 0);
            ret = 0;
        }
    }
    return ret;
}

void SgStPcmVolume(unsigned long long mask, unsigned int left, int right)
{
    if (left <= 0x7FFF && right >= 0 && right <= 0x7FFF && (mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x4A, (int)mask, left, right);
    }
}

int SgStPcmIopReadAddr(unsigned int ch)
{
    int ret = 0;
    if (ch < 0x10) {
        char *iop = _SgGetIop2EeContext();
        ret = *(int *)(iop + (ch << 2) + 0x180);
    }
    return ret;
}

int SgStPcmBufMode(int mode, long mask, int addr)
{
    int ret;
    ret = -1;
    if ((unsigned int)mode < 2 && (unsigned int)addr <= 0x1FFFFF && (mask & 0xFF000000) == 0) {
        _SgSetPkAdd(0x4F, mask, addr, mode);
        ret = 0;
    }
    return ret;
}
