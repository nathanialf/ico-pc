/*
 * port/fmv/m2v.h
 *
 * The MPEG-2 video decoder behind the FMV player: a thin wrapper over
 * Ittiam libmpeg2 (tools/fetch_deps.sh, Apache-2.0).  It
 * takes one access unit at a time (pss.h's ico_pss_es_unit_end cuts them:
 * the sequence and GOP headers that precede a picture travel with it) and
 * hands back decoded pictures in display order as 4:2:0 planes, as the
 * IPU's sceMpegGetPicture did before its colour conversion.
 *
 * Single-threaded; the library's generic C path, or its NEON routines on
 * arm64 (port/fmv/CMakeLists.txt, ICO_LIBMPEG2_NEON); for any size up
 * to ICO_M2V_MAX_W x ICO_M2V_MAX_H (the IPU's own limit through
 * sceMpegGetPicture: 1620 macroblocks, 720 x 576).
 */
#ifndef ICO_PORT_FMV_M2V_H
#define ICO_PORT_FMV_M2V_H

#include <stddef.h>
#include <stdint.h>

#define ICO_M2V_MAX_W 720
#define ICO_M2V_MAX_H 576

/* picture_structure (ISO/IEC 13818-2 6.3.10) */
enum { ICO_M2V_TOP_FIELD = 1, ICO_M2V_BOTTOM_FIELD = 2, ICO_M2V_FRAME_PICTURE = 3 };

/* The scan headers of a picture, as coded: the sequence header's
   frame_rate_code, the sequence extension's progressive_sequence and the
   picture coding extension's flags (ISO/IEC 13818-2 6.2.2.3, 6.2.3.1).
   Read by m2v.c from the units themselves: the library's own
   u4_progressive_frame_flag is always 1 (impeg2d_api_main.c, the end of
   impeg2d_api_video_decode).  An MPEG-1 stream (no extensions) reads as
   progressive frame pictures. */
typedef struct IcoM2vScan {
    uint8_t frame_rate_code;      /* 3 = 25 Hz, 4 = 29.97 Hz, ... */
    uint8_t progressive_sequence; /* 1: every picture progressive */
    uint8_t picture_structure;    /* ICO_M2V_*_FIELD / FRAME_PICTURE */
    uint8_t top_field_first;
    uint8_t repeat_first_field;
    uint8_t progressive_frame;
    uint8_t coding_type; /* 1 I, 2 P, 3 B, 4 D */
    uint8_t extensions;  /* 1 when the picture carried a picture coding extension */
} IcoM2vScan;

/* A decoded picture: planes owned by the decoder, valid until the next call
   on it. */
typedef struct IcoM2vFrame {
    const uint8_t *y, *u, *v; /* u = Cb, v = Cr */
    uint32_t pitch[3];        /* bytes per row of y, u, v */
    uint32_t w, h;            /* luma size; chroma is (w + 1) / 2 x (h + 1) / 2 */
    /* this picture's headers (display order: the held reference's own
       when an I or P picture comes out) */
    IcoM2vScan scan;
    /* the picture's two fields are from different instants: neither the
       sequence nor the picture says progressive */
    uint8_t interlaced;
    /* the decoder's own picture type of this output (1 I, 2 P, 3 B, 0
       unknown): equal to scan.coding_type when the display-order pairing
       holds */
    uint8_t out_type;
} IcoM2vFrame;

typedef struct IcoM2v IcoM2v;

/* NULL when the library cannot be set up (out of memory). */
IcoM2v *ico_m2v_create(void);
void ico_m2v_destroy(IcoM2v *d);
/* Decodes one access unit.  Returns 1 with *out set when a picture comes
   out (display order: an I or P picture is held until the next reference
   arrives), 0 when none does, -1 when the unit could not be decoded (the
   error is counted and the decoder goes on with the next unit). */
int ico_m2v_decode(IcoM2v *d, const uint8_t *au, size_t len, IcoM2vFrame *out);
/* After the last unit: returns 1 with *out for each picture still held,
   then 0. */
int ico_m2v_flush(IcoM2v *d, IcoM2vFrame *out);
/* Pictures output and units that failed, since create. */
uint32_t ico_m2v_frames_out(const IcoM2v *d);
uint32_t ico_m2v_errors(const IcoM2v *d);
/* Sequence-size changes followed (decoder reset, planes resized). */
uint32_t ico_m2v_resets(const IcoM2v *d);
/* The sequence header's picture size and aspect_ratio_information (1 square
   samples, 2 4:3, 3 16:9, 4 2.21:1); zeros before the first header. */
void ico_m2v_seq_info(const IcoM2v *d, uint32_t *w, uint32_t *h, uint32_t *aspect_code);
/* The header scan of one access unit on its own, without decoding.  *scan
   carries the sequence-level fields (frame_rate_code,
   progressive_sequence) from unit to unit: zero it before the first.
   Returns 1 when the unit holds a picture header, 0 if not. */
int ico_m2v_scan_unit(const uint8_t *au, size_t len, IcoM2vScan *scan);

#endif /* ICO_PORT_FMV_M2V_H */
