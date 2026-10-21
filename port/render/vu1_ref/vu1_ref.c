/* vu1_ref.c: VU1 state and the upload routines (the SET_* entries), shared
 * by the program references. Line numbers are ico2/vusrc files. */
#include "vu1_ref_internal.h"

void vu1ref_init(Vu1Ref *r)
{
    memset(r, 0, sizeof(*r));
    r->vf[0][3] = 1.0f;
}

void vu1ref_load_common(Vu1Ref *r, const float qw[16][4])
{
    memcpy(r->mem[0], qw, 16 * sizeof(Vec4));
}

void vu1ref_set_uv_offset(Vu1Ref *r, const float qw[4])
{
    /* vu1_common.h:57-62: lq vf30, 2; move.xy vf30, vf31; sq vf30, 2 */
    r->mem[2][0] = qw[0];
    r->mem[2][1] = qw[1];
}

void vu1ref_normal_set_matrix(Vu1Ref *r, const float qw[12][4])
{
    /* normal_c.vsm:54-81 (normal_l.vsm:59-86 is the same) */
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
    memcpy(r->mem[16], qw, 12 * sizeof(Vec4));
    v4_copy(r->mem[37], r->mem[3]);
    v4_copy(r->vf[31], r->mem[3]);
}

void vu1ref_normal_set_light(Vu1Ref *r, const float qw[8][4])
{
    /* normal_c.vsm:88-105 */
    memcpy(r->mem[28], qw, 8 * sizeof(Vec4));
    v4_copy(r->vf[31], qw[7]);
}

void vu1ref_cluster_set_matrix(Vu1Ref *r, const float (*qw)[4])
{
    /* cluster.vsm:69-87: vi06 = qw[0].x; mem[2].w = qw[0].w; then a
     * do-while copies vi06 quadwords from qw[1] to mem[16..] (decrement,
     * load, branch while non-zero, store in the delay slot) */
    int32_t n = vu_int(qw[0][0]);
    if (n <= 0) {
        n = 1; /* the VU would wrap a 16-bit count of 0; the game never sends one */
    }
    r->vf[30][0] = r->mem[2][0];
    r->vf[30][1] = r->mem[2][1];
    r->vf[30][2] = r->mem[2][2];
    r->vf[30][3] = qw[0][3];
    r->mem[2][3] = qw[0][3];
    int dst = 16;
    int src = 1;
    do {
        n -= 1;
        v4_copy(r->vf[28], qw[src++]);
        v4_copy(r->mem[dst++ & (VU1_MEM_QW - 1)], r->vf[28]);
    } while (n != 0);
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[9 + i], r->mem[4 + i]);
    }
    r->vi[6] = 0;
}

void vu1ref_cluster_set_light(Vu1Ref *r, const float qw[8][4])
{
    /* cluster.vsm:98-107 */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[13 + i], qw[i]);
    }
}

void vu1ref_mesh_set_matrix(Vu1Ref *r, const float qw[4][4])
{
    /* mesh.vsm:85-94: vf14.w = 16777214, vf13 = 0, vf14.xy = 4094 (z kept) */
    r->vf[14][3] = 16777214.0f;
    memset(r->vf[13], 0, sizeof(Vec4));
    r->vf[14][0] = 4094.0f;
    r->vf[14][1] = 4094.0f;
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
}

void vu1ref_mesh_set_light(Vu1Ref *r, const float qw[8][4])
{
    /* mesh.vsm:102-111 */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[5 + i], qw[i]);
    }
}

void vu1ref_particle_set_matrix(Vu1Ref *r, const float qw[8][4])
{
    /* particle.vsm:43-52, then falls into BEGIN_PARTICLE (E bit) */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
}

int vu1ref_kicks(const VuBatchOut *out, int *kicks)
{
    int n = 0;
    for (int k = 2; k < out->count; k++) {
        if (!vu_gs_adc(&out->v[k])) {
            kicks[n++] = k;
        }
    }
    return n;
}

int vu1ref_static_kicks(const float *stw, const int *batchFirst, int count, int *kicks)
{
    int n = 0;
    for (int k = 0; k < count; k++) {
        if (k - batchFirst[k] < 2) {
            continue;
        }
        if (stw && (stw[k] < 1.0f || stw[k - 1] < 1.0f)) {
            continue;
        }
        kicks[n++] = k;
    }
    return n;
}

int vu1ref_region_draw(int extended, int freeCamera, int insideA, int insideB, int insideC,
                       int hide, int reaches, int front, int joined, int stripPasses)
{
    /* vu_content_hidden */
    if (hide) {
        return VU1REF_DRAW_NONE;
    }
    if (insideA && insideB && insideC) {
        return VU1REF_DRAW_EXACT;
    }
    /* vu_region_rule: the free camera, then the cut (vu_cut_rows) */
    if (freeCamera || !reaches) {
        return VU1REF_DRAW_CLIPPED;
    }
    /* VU_RULE_STRIP: not a card */
    if (extended && front && (joined || stripPasses)) {
        return VU1REF_DRAW_CLIPPED;
    }
    return VU1REF_DRAW_NONE;
}

int vu1ref_region_between(int drawT, int cardT, int shownPrev, int shownCur)
{
    if (drawT != VU1REF_DRAW_NONE || cardT) {
        return drawT;
    }
    return shownPrev || shownCur ? VU1REF_DRAW_CLIPPED : VU1REF_DRAW_NONE;
}

/* whether static kick k (vertices k-2..k) is drawn: vu1ref_static_kicks */
static int staticKick(const float *stw, const int *batchFirst, int k)
{
    return k - batchFirst[k] >= 2 && (!stw || (stw[k] >= 1.0f && stw[k - 1] >= 1.0f));
}

int vu1ref_strip_passes(const VuGsVertex *v, const float *stw, const int *batchFirst, int count,
                        int k)
{
    if (k < 0 || k >= count || !staticKick(stw, batchFirst, k)) {
        return 0;
    }
    /* the run of consecutive static kicks around k */
    int lo = k, hi = k;
    while (lo - 1 >= 0 && staticKick(stw, batchFirst, lo - 1)) {
        lo--;
    }
    while (hi + 1 < count && staticKick(stw, batchFirst, hi + 1)) {
        hi++;
    }
    for (int j = lo; j <= hi; j++) {
        if (v[j - 2].inside && v[j - 1].inside && v[j].inside) {
            return 1;
        }
    }
    return 0;
}

int vu1ref_particle_draw(int extended, int freeCamera, int drawn, int shown, int reaches)
{
    if (!shown) {
        return VU1REF_DRAW_NONE;
    }
    if (drawn) {
        return VU1REF_DRAW_EXACT;
    }
    if (freeCamera || extended) {
        return VU1REF_DRAW_CLIPPED;
    }
    return reaches ? VU1REF_DRAW_NONE : VU1REF_DRAW_CLIPPED;
}

/* vu_clip_corners in two dimensions: poly[0..n) cut to s * (p[axis] - at)
 * >= 0; returns the corner count (at most n + 1) */
static int cutPolygon(float (*poly)[2], int n, int axis, float at, float sgn)
{
    float q[VU1REF_POLY_MAX][2];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const float *a = poly[i], *b = poly[i + 1 < n ? i + 1 : 0];
        const float da = sgn * (a[axis] - at), db = sgn * (b[axis] - at);
        if (da >= 0.0f && m < VU1REF_POLY_MAX) {
            q[m][0] = a[0];
            q[m][1] = a[1];
            m++;
        }
        if ((da >= 0.0f) != (db >= 0.0f) && m < VU1REF_POLY_MAX) {
            const float t = da / (da - db);
            q[m][0] = a[0] + (b[0] - a[0]) * t;
            q[m][1] = a[1] + (b[1] - a[1]) * t;
            m++;
        }
    }
    memcpy(poly, q, (size_t)m * sizeof(q[0]));
    return m;
}

int vu1ref_polygon_reaches_picture(const float (*poly)[2], int n, const float pic[4])
{
    float p[VU1REF_POLY_MAX][2];
    if (n < 3 || n > VU1REF_POLY_MAX - 2) {
        return n >= 3; /* not a polygon this cuts: counted as reaching */
    }
    memcpy(p, poly, (size_t)n * sizeof(p[0]));
    /* the cut (vu_cut_rows): the picture's rows first, then the columns */
    n = cutPolygon(p, n, 1, pic[2], 1.0f);
    n = cutPolygon(p, n, 1, pic[3], -1.0f);
    if (n < 3) {
        return 0;
    }
    float lo = p[0][0], hi = p[0][0];
    for (int i = 1; i < n; i++) {
        lo = p[i][0] < lo ? p[i][0] : lo;
        hi = p[i][0] > hi ? p[i][0] : hi;
    }
    return lo < pic[1] && hi > pic[0];
}

int vu1ref_rect_reaches_picture(float x0, float y0, float x1, float y1, const float pic[4])
{
    /* vu_rect_reaches_picture */
    const float xl = x0 < x1 ? x0 : x1, xh = x0 < x1 ? x1 : x0;
    const float yl = y0 < y1 ? y0 : y1, yh = y0 < y1 ? y1 : y0;
    return xl < pic[1] && xh > pic[0] && yl < pic[3] && yh > pic[2];
}

/* vu_common.hlsli vu_rcp: 1 / x, a zero (or denormal) divisor gives +-Fmax */
static float cardRcp(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    if ((u & 0x7F800000u) == 0u) {
        const uint32_t f = (u & 0x80000000u) | 0x7F7FFFFFu;
        float r;
        memcpy(&r, &f, 4);
        return r;
    }
    return 1.0f / x;
}

/* vu1ref_region_inside's position h of the vertex v and the bounds of
 * its kind's region test */
static void regionVertex(int kind, const float (*mem)[4], const float (*bones)[4],
                         const float (*v)[4], Vec4 h, const float **loOut, const float **hiOut)
{
    static const float lo0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static const float hi4094[4] = {4094.0f, 4094.0f, 0.0f, 16777214.0f};
    const float *lo = lo0, *hi = hi4094;
    if (kind == VU1REF_CARD_CLUSTER) {
        /* vu_skin_vertex: P = B0 (pos, 1) w0 + B1 (pos, 1) w1, then mem[4..7] */
        uint32_t b0, b1;
        memcpy(&b0, &v[2][0], 4);
        memcpy(&b1, &v[2][2], 4);
        b0 -= 16u;
        b1 -= 16u;
        const uint32_t i0 = b0 < 236u ? b0 : 236u, i1 = b1 < 236u ? b1 : 236u;
        Vec4 p0, p1;
        vu_mat(p0, bones[i0], bones[i0 + 1], bones[i0 + 2], bones[i0 + 3], v[0], 1.0f);
        vu_mat(p1, bones[i1], bones[i1 + 1], bones[i1 + 2], bones[i1 + 3], v[0], 1.0f);
        Vec4 pb;
        for (int i = 0; i < 3; i++) {
            const float t = p0[i] * v[2][1];
            pb[i] = t + p1[i] * v[2][3];
        }
        pb[3] = 1.0f;
        vu_mat(h, mem[4], mem[5], mem[6], mem[7], pb, 1.0f);
        lo = mem[0];
        hi = mem[1];
    } else {
        /* vu_prelit_vertex, vu_lit_vertex (the region mode: vf00.w),
         * vu_grid_vertex */
        vu_mat(h, mem[16], mem[17], mem[18], mem[19], v[0], 1.0f);
    }
    *loOut = lo;
    *hiOut = hi;
}

int vu1ref_region_inside(int kind, const float (*mem)[4], const float (*bones)[4],
                         const float (*v)[4])
{
    Vec4 h;
    const float *lo, *hi;
    regionVertex(kind, mem, bones, v, h, &lo, &hi);
    const float q = cardRcp(h[3]);
    const float px = h[0] * q, py = h[1] * q;
    /* vu_inside */
    return lo[0] < px && lo[1] < py && lo[3] < h[3] && px < hi[0] && py < hi[1] && h[3] < hi[3];
}

/* vu_common.hlsli vu_ftoi4: x * 16 truncated, saturated at the int32
 * bounds (NaN: 0, never saturated) */
static int32_t shaderFtoi4(float x)
{
    const float v = x * 16.0f;
    if (v >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (v <= -2147483648.0f) {
        return INT32_MIN;
    }
    return v == v ? (int32_t)v : 0;
}

int vu1ref_region_z_saturated(int kind, const float (*mem)[4], const float (*bones)[4],
                              const float (*v)[4])
{
    Vec4 h;
    const float *lo, *hi;
    regionVertex(kind, mem, bones, v, h, &lo, &hi);
    if (!(h[3] > 0.0f)) {
        return 0;
    }
    const float q = cardRcp(h[3]);
    const int32_t gx = shaderFtoi4(h[0] * q), gy = shaderFtoi4(h[1] * q);
    const int32_t gz = shaderFtoi4(h[2] * q);
    return gz == INT32_MAX && gx != INT32_MAX && gx != INT32_MIN && gy != INT32_MAX &&
           gy != INT32_MIN;
}

int vu1ref_strip_card(int kind, const float (*mem)[4], const float (*bones)[4], const float (*v)[4],
                      uint32_t qpv, uint32_t count, uint32_t *tested)
{
    if (count < 3) {
        return 1;
    }
    int i0 = vu1ref_region_inside(kind, mem, bones, v);
    int i1 = vu1ref_region_inside(kind, mem, bones, v + qpv);
    for (uint32_t k = 2; k < count; k++) {
        const int i2 = vu1ref_region_inside(kind, mem, bones, v + (size_t)k * qpv);
        if (tested) {
            (*tested)++;
        }
        if (i0 && i1 && i2) {
            return 0;
        }
        i0 = i1;
        i1 = i2;
    }
    return 1;
}
