/*
 * ico2/ito/include/mv_vobuf.h
 *
 * The declarations of what mv_vobuf.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MV_VOBUF_H
#define MV_VOBUF_H

/* One video-out frame's DMA tag block: its display state and the two-field
   transfer packets mv_disp hands to the GIF, one slot per field. */
typedef struct VoTag {      /* field names derived */
    int status;             /* 0x00: 2 decoded, 1 field 0 shown, 0 free */
    char pad4[60];          /* 0x04 */
    char packet[5][157440]; /* 0x40, the per-field image transfer packets */
} VoTag; /* derived name */

/* One decoded picture: 720 x 576 RGBA32 (JPN and KOR 720 x 480). */
typedef struct VoData { /* field names derived */
    unsigned int pixel[414720];
} VoData; /* derived name */

/* The video-out ring the decoder fills and mv_disp drains. */
typedef struct VoBuf {  /* field names derived */
    VoData *data;       /* 0x00 */
    VoTag *tag;         /* 0x04 */
    volatile int idx;   /* 0x08, the slot the decoder writes next */
    volatile int count; /* 0x0C, the slots decoded and not yet shown */
    int max;            /* 0x10 */
} VoBuf;                /* derived name */

extern VoBuf voBuf;
int voBufCreate(VoBuf *self);
void voBufDecCount(VoBuf *self);
void voBufDelete(VoBuf *self);
VoData *voBufGetData(VoBuf *self);
VoTag *voBufGetTag(VoBuf *self);
void voBufIncCount(VoBuf *self);
int voBufIsFull(VoBuf *self);
void voBufReset(VoBuf *self);

#endif /* MV_VOBUF_H */
