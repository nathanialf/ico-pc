/*
 * port/compat/libmpeg.h
 *
 * The host build's libmpeg.h: the declarations the game uses, from
 * sce/libmpeg/libmpeg.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_LIBMPEG_H
#define ICO_COMPAT_LIBMPEG_H

/* the decoder handle sceMpegCreate registers: the picture size and count,
 * the two fields' time stamps and flags, and its internal record */
typedef struct {
    int width, height, frameCount, pad0C;
    long long pts, dts;       /* 0x10, 0x18 */
    long long flags;          /* 0x20 */
    long long pts2nd, dts2nd; /* 0x28, 0x30 */
    long long flags2nd;       /* 0x38 */
    struct MpegSys *sys;      /* 0x40 */
} sceMpeg;

/* a callback sceMpegAddCallback registers: called with the handle, the
 * callback's data record (its first word the type) and the registered data */
typedef int (*sceMpegCallback)(sceMpeg *mp, void *cbdata, void *anyData);
sceMpegCallback sceMpegAddCallback(sceMpeg *mp, int type, sceMpegCallback func, void *data);
int sceMpegAddStrCallback(sceMpeg *mp, int type, int ch, sceMpegCallback func, void *data);
int sceMpegClearRefBuff(sceMpeg *mp);
int sceMpegCreate(sceMpeg *mp, void *buf, int size);
int sceMpegDelete(sceMpeg *m);
int sceMpegDemuxPssRing(sceMpeg *mp, void *buf, int size, int ring, int ringSize);
int sceMpegGetPicture(sceMpeg *mp, void *buf, int size); /* EE: an unsigned int address */
void sceMpegInit(void);
int sceMpegIsEnd(sceMpeg *mp);
int sceMpegIsRefBuffEmpty(sceMpeg *mp);
void sceMpegReset(sceMpeg *mp);

/* The library's internal symbols are in libmpeg_internal.h. */

#endif /* ICO_COMPAT_LIBMPEG_H */
