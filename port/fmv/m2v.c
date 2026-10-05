/*
 * port/fmv/m2v.c
 *
 * m2v.h over Ittiam libmpeg2's ivd API (decoder/ivd.h, decoder/impeg2d.h).
 * The call sequence is the library's own fuzzer's
 * (fuzzer/mpeg2_dec_fuzzer.cpp, android-16.0.0_r4): IV_CMD_GET_NUM_MEM_REC,
 * IV_CMD_FILL_NUM_MEM_REC, the records allocated by the caller, IV_CMD_INIT,
 * then IVD_CMD_CTL_SETPARAMS in header mode for the first sequence header and
 * in frame mode for the pictures, IVD_CMD_VIDEO_DECODE per access unit and
 * IVD_CMD_CTL_FLUSH at the end.
 *
 * Choices:
 *   - deinterlace off (u4_deinterlace 0): the IPU hands frame pictures out
 *     as they are coded, and the game's streams are frame pictures
 *     (docs/port/FMV.md, "Streams");
 *   - shared display buffers off: the library copies each output picture
 *     into the three planes this file owns (IV_YUV_420P);
 *   - one core, threads not kept (ithread_single.c).
 */
#include "m2v.h"
#include <stdlib.h>
#include <string.h>
#include "iv_datatypedef.h"
#include "iv.h"
#include "ivd.h"
#include "impeg2d.h"

struct IcoM2v {
    iv_obj_t *codec;
    iv_mem_rec_t *recs;
    uint32_t nrecs;
    int have_header;
    uint32_t w, h, aspect;
    uint8_t *planes[3];
    uint32_t plane_size[3];
    uint32_t frames, errors;
    int flushing;
};

/* --- aligned allocation (the library asks for its records' alignment) ---- */

static void *aligned_alloc_rec(size_t size, size_t align)
{
    uint8_t *raw, *p;

    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    raw = malloc(size + align + sizeof(void *));
    if (raw == NULL) {
        return NULL;
    }
    p = (uint8_t *)(((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1));
    ((void **)p)[-1] = raw;
    return p;
}

static void aligned_free_rec(void *p)
{
    if (p != NULL) {
        free(((void **)p)[-1]);
    }
}

static IV_API_CALL_STATUS_T api(IcoM2v *d, void *ip, void *op)
{
    return impeg2d_api_function(d != NULL ? d->codec : NULL, ip, op);
}

static void set_mode(IcoM2v *d, IVD_VIDEO_DECODE_MODE_T mode)
{
    ivd_ctl_set_config_ip_t ip;
    ivd_ctl_set_config_op_t op;

    memset(&ip, 0, sizeof(ip));
    memset(&op, 0, sizeof(op));
    ip.u4_disp_wd = 0;
    ip.e_frm_skip_mode = IVD_SKIP_NONE;
    ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
    ip.e_vid_dec_mode = mode;
    ip.e_cmd = IVD_CMD_VIDEO_CTL;
    ip.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
    ip.u4_size = sizeof(ip);
    op.u4_size = sizeof(op);
    api(d, &ip, &op);
}

IcoM2v *ico_m2v_create(void)
{
    IcoM2v *d = calloc(1, sizeof(*d));
    iv_num_mem_rec_ip_t nip;
    iv_num_mem_rec_op_t nop;
    impeg2d_fill_mem_rec_ip_t fip;
    impeg2d_fill_mem_rec_op_t fop;
    impeg2d_init_ip_t iip;
    impeg2d_init_op_t iop;
    uint32_t i;

    if (d == NULL) {
        return NULL;
    }
    memset(&nip, 0, sizeof(nip));
    memset(&nop, 0, sizeof(nop));
    nip.u4_size = sizeof(nip);
    nop.u4_size = sizeof(nop);
    nip.e_cmd = IV_CMD_GET_NUM_MEM_REC;
    if (api(NULL, &nip, &nop) != IV_SUCCESS) {
        free(d);
        return NULL;
    }
    d->nrecs = nop.u4_num_mem_rec;
    d->recs = calloc(d->nrecs, sizeof(iv_mem_rec_t));
    if (d->recs == NULL) {
        free(d);
        return NULL;
    }
    for (i = 0; i < d->nrecs; i++) {
        d->recs[i].u4_size = sizeof(iv_mem_rec_t);
    }
    memset(&fip, 0, sizeof(fip));
    memset(&fop, 0, sizeof(fop));
    fip.s_ivd_fill_mem_rec_ip_t.u4_size = sizeof(fip);
    fip.s_ivd_fill_mem_rec_ip_t.e_cmd = IV_CMD_FILL_NUM_MEM_REC;
    fip.s_ivd_fill_mem_rec_ip_t.pv_mem_rec_location = d->recs;
    fip.s_ivd_fill_mem_rec_ip_t.u4_max_frm_wd = ICO_M2V_MAX_W;
    fip.s_ivd_fill_mem_rec_ip_t.u4_max_frm_ht = ICO_M2V_MAX_H;
    fip.u4_share_disp_buf = 0;
    fip.u4_deinterlace = 0;
    fip.u4_keep_threads_active = 0;
    fip.e_output_format = IV_YUV_420P;
    fop.s_ivd_fill_mem_rec_op_t.u4_size = sizeof(fop);
    if (api(NULL, &fip, &fop) != IV_SUCCESS) {
        free(d->recs);
        free(d);
        return NULL;
    }
    d->nrecs = fop.s_ivd_fill_mem_rec_op_t.u4_num_mem_rec_filled;
    for (i = 0; i < d->nrecs; i++) {
        d->recs[i].pv_base = aligned_alloc_rec(d->recs[i].u4_mem_size, d->recs[i].u4_mem_alignment);
        if (d->recs[i].pv_base == NULL) {
            ico_m2v_destroy(d);
            return NULL;
        }
        memset(d->recs[i].pv_base, 0, d->recs[i].u4_mem_size);
    }
    d->codec = (iv_obj_t *)d->recs[0].pv_base;
    d->codec->pv_fxns = (void *)impeg2d_api_function;
    d->codec->u4_size = sizeof(iv_obj_t);

    memset(&iip, 0, sizeof(iip));
    memset(&iop, 0, sizeof(iop));
    iip.s_ivd_init_ip_t.u4_size = sizeof(iip);
    iip.s_ivd_init_ip_t.e_cmd = (IVD_API_COMMAND_TYPE_T)IV_CMD_INIT;
    iip.s_ivd_init_ip_t.pv_mem_rec_location = d->recs;
    iip.s_ivd_init_ip_t.u4_frm_max_wd = ICO_M2V_MAX_W;
    iip.s_ivd_init_ip_t.u4_frm_max_ht = ICO_M2V_MAX_H;
    iip.s_ivd_init_ip_t.u4_num_mem_rec = d->nrecs;
    iip.s_ivd_init_ip_t.e_output_format = IV_YUV_420P;
    iip.u4_share_disp_buf = 0;
    iip.u4_deinterlace = 0;
    iip.u4_keep_threads_active = 0;
    iop.s_ivd_init_op_t.u4_size = sizeof(iop);
    if (api(d, &iip, &iop) != IV_SUCCESS) {
        ico_m2v_destroy(d);
        return NULL;
    }
    set_mode(d, IVD_DECODE_HEADER);
    return d;
}

void ico_m2v_destroy(IcoM2v *d)
{
    uint32_t i;

    if (d == NULL) {
        return;
    }
    if (d->codec != NULL) {
        iv_retrieve_mem_rec_ip_t rip;
        iv_retrieve_mem_rec_op_t rop;

        memset(&rip, 0, sizeof(rip));
        memset(&rop, 0, sizeof(rop));
        rip.u4_size = sizeof(rip);
        rop.u4_size = sizeof(rop);
        rip.e_cmd = IV_CMD_RETRIEVE_MEMREC;
        rip.pv_mem_rec_location = d->recs;
        api(d, &rip, &rop);
    }
    for (i = 0; d->recs != NULL && i < d->nrecs; i++) {
        aligned_free_rec(d->recs[i].pv_base);
    }
    free(d->recs);
    for (i = 0; i < 3; i++) {
        free(d->planes[i]);
    }
    free(d);
}

/* The output planes for a w x h picture.  The library writes rows at its
   frame stride, the width rounded up to a macroblock (impeg2d_pic_proc.c
   u2_frame_width), and checks the buffers against stride x h
   (check_app_out_buf_size): a width that is not a multiple of 16 would
   otherwise fail every picture. */
static int alloc_planes(IcoM2v *d, uint32_t w, uint32_t h)
{
    uint32_t cw, ch;

    w = (w + 15u) & ~15u;
    cw = (w + 1) / 2;
    ch = (h + 1) / 2;
    uint32_t sz[3] = {w * h, cw * ch, cw * ch};
    int i;

    for (i = 0; i < 3; i++) {
        if (d->plane_size[i] < sz[i]) {
            uint8_t *p = realloc(d->planes[i], sz[i]);
            if (p == NULL) {
                return -1;
            }
            d->planes[i] = p;
            d->plane_size[i] = sz[i];
        }
    }
    return 0;
}

/* The sequence header's size and aspect, read directly from the unit (the
   library reports the size, not the aspect code). */
static void scan_seq_header(IcoM2v *d, const uint8_t *p, size_t len)
{
    size_t i;

    for (i = 0; i + 8 <= len; i++) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && p[i + 3] == 0xB3) {
            d->w = ((uint32_t)p[i + 4] << 4) | (p[i + 5] >> 4);
            d->h = ((uint32_t)(p[i + 5] & 0x0F) << 8) | p[i + 6];
            d->aspect = p[i + 7] >> 4;
            return;
        }
    }
}

static void fill_out(IcoM2v *d, const ivd_video_decode_op_t *op, IcoM2vFrame *out)
{
    out->y = d->planes[0];
    out->u = d->planes[1];
    out->v = d->planes[2];
    out->pitch[0] = op->s_disp_frm_buf.u4_y_strd;
    out->pitch[1] = op->s_disp_frm_buf.u4_u_strd;
    out->pitch[2] = op->s_disp_frm_buf.u4_v_strd;
    out->w = op->s_disp_frm_buf.u4_y_wd;
    out->h = op->s_disp_frm_buf.u4_y_ht;
    d->frames++;
}

static IV_API_CALL_STATUS_T decode_call(IcoM2v *d, const uint8_t *p, size_t len,
                                        ivd_video_decode_op_t *op)
{
    ivd_video_decode_ip_t ip;

    memset(&ip, 0, sizeof(ip));
    memset(op, 0, sizeof(*op));
    ip.u4_size = sizeof(ip);
    ip.e_cmd = IVD_CMD_VIDEO_DECODE;
    ip.pv_stream_buffer = (void *)p;
    ip.u4_num_Bytes = (UWORD32)len;
    ip.s_out_buffer.u4_num_bufs = 3;
    ip.s_out_buffer.pu1_bufs[0] = d->planes[0];
    ip.s_out_buffer.pu1_bufs[1] = d->planes[1];
    ip.s_out_buffer.pu1_bufs[2] = d->planes[2];
    ip.s_out_buffer.u4_min_out_buf_size[0] = d->plane_size[0];
    ip.s_out_buffer.u4_min_out_buf_size[1] = d->plane_size[1];
    ip.s_out_buffer.u4_min_out_buf_size[2] = d->plane_size[2];
    op->u4_size = sizeof(*op);
    return api(d, &ip, op);
}

int ico_m2v_decode(IcoM2v *d, const uint8_t *au, size_t len, IcoM2vFrame *out)
{
    ivd_video_decode_op_t op;
    IV_API_CALL_STATUS_T r;

    if (d == NULL || au == NULL || len == 0) {
        return -1;
    }
    scan_seq_header(d, au, len);
    if (!d->have_header) {
        /* the first unit opens with the sequence header: header mode reads
           it and reports the size, frame mode decodes the rest */
        r = decode_call(d, au, len, &op);
        if (op.u4_pic_wd == 0 || op.u4_pic_ht == 0 || op.u4_pic_wd > ICO_M2V_MAX_W ||
            op.u4_pic_ht > ICO_M2V_MAX_H || alloc_planes(d, op.u4_pic_wd, op.u4_pic_ht) != 0) {
            d->errors++;
            return -1;
        }
        (void)r;
        d->have_header = 1;
        set_mode(d, IVD_DECODE_FRAME);
        if (op.u4_num_bytes_consumed >= len) {
            return 0;
        }
        au += op.u4_num_bytes_consumed;
        len -= op.u4_num_bytes_consumed;
    }
    r = decode_call(d, au, len, &op);
    if (r != IV_SUCCESS && !op.u4_output_present) {
        d->errors++;
        return -1;
    }
    if (op.u4_output_present) {
        fill_out(d, &op, out);
        return 1;
    }
    return 0;
}

int ico_m2v_flush(IcoM2v *d, IcoM2vFrame *out)
{
    ivd_video_decode_op_t op;

    if (d == NULL || !d->have_header) {
        return 0;
    }
    if (!d->flushing) {
        ivd_ctl_flush_ip_t ip;
        ivd_ctl_flush_op_t fop;

        memset(&ip, 0, sizeof(ip));
        memset(&fop, 0, sizeof(fop));
        ip.e_cmd = IVD_CMD_VIDEO_CTL;
        ip.e_sub_cmd = IVD_CMD_CTL_FLUSH;
        ip.u4_size = sizeof(ip);
        fop.u4_size = sizeof(fop);
        if (api(d, &ip, &fop) != IV_SUCCESS) {
            return 0;
        }
        d->flushing = 1;
    }
    if (decode_call(d, NULL, 0, &op) == IV_SUCCESS && op.u4_output_present) {
        fill_out(d, &op, out);
        return 1;
    }
    return 0;
}

uint32_t ico_m2v_frames_out(const IcoM2v *d)
{
    return d != NULL ? d->frames : 0;
}

uint32_t ico_m2v_errors(const IcoM2v *d)
{
    return d != NULL ? d->errors : 0;
}

void ico_m2v_seq_info(const IcoM2v *d, uint32_t *w, uint32_t *h, uint32_t *aspect_code)
{
    if (w != NULL) {
        *w = d != NULL ? d->w : 0;
    }
    if (h != NULL) {
        *h = d != NULL ? d->h : 0;
    }
    if (aspect_code != NULL) {
        *aspect_code = d != NULL ? d->aspect : 0;
    }
}
