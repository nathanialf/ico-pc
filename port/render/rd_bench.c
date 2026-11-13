/*
 * port/render/rd_bench.c
 *
 * The rc-only bench, the renderer's side (rd_bench.h).
 */
#include "rd_bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "rd_date_one.h"
#include "rd_lamps.h"
#include "rd_mesh.h"
#include "rd_paste_spec.h"
#include "rd_present_scene.h"
#include "rd_vis.h"
#include "rd_volume_fs.h"

/* ------------------------------------------------------- classification */

static uint32_t srcOfTex(uint32_t texId, void *user)
{
    (void)user;
    const RdTexRec *t = texId ? rd__tex_rec(texId) : NULL;
    return t && t->kind == RD_TEXKIND_TARGET ? t->target : 0u;
}

/* list 0's mesh draw's screen box under its VU matrix (rd_vis.c
   cmdVerdict's bound of the whole stream, rd_vis_hiz.c's area of it) */
static double areaOf(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, void *user)
{
    (void)user;
    if (c->type != RDC_MESH || !f->payload) {
        return -1.0;
    }
    const RdMeshRec *m = rd__mesh_rec(c->u[0]);
    const uint64_t after = sizeof(RdVuPayload) + sizeof(RdVuBlock);
    if (!m || !m->vu || !m->stream || !m->vertexCount || !m->qwPerVertex || c->u[2] < after ||
        (uint64_t)c->u[1] + c->u[2] > f->payloadSize) {
        return -1.0;
    }
    RdVuPayload p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    if (p.clip != RD_VU_CLIP_REGION && p.clip != RD_VU_CLIP_SCISSOR) {
        return -1.0;
    }
    float mem[36][4];
    memcpy(mem, f->payload + c->u[1] + sizeof(RdVuPayload), sizeof(mem));
    RdVisBox box;
    double area = 0.0;
    if (!rd__vis_box((const float (*)[4])m->stream, m->vertexCount, m->qwPerVertex, &box) ||
        !box.n ||
        !rd__vis_hiz_screen_area((const float (*)[4]) & mem[16], &box, p.clip == RD_VU_CLIP_SCISSOR,
                                 &area)) {
        return -1.0;
    }
    const double target = (double)st->gsW * (double)st->gsH;
    return target > 0.0 ? area / target : -1.0;
}

static uint32_t tid(RdTargetId t)
{
    return rd_target(t).id;
}

/* rd_replay.c perfTargetPost: the effect a target names (before the state
   takes it) */
static int targetPost(const RdStateBlock *st, const RdCmd *c)
{
    const uint32_t col = c->u[0];
    const uint32_t aa0 = tid(RD_TARGET_AA0), aa1 = tid(RD_TARGET_AA1);
    if (col == aa0 || col == aa1 ||
        (col == tid(RD_TARGET_SCENE) && (st->color == aa0 || st->color == aa1))) {
        return RD_PERF_POST_SOFTEN;
    }
    if (col == tid(RD_TARGET_AURA_WORK) || col == tid(RD_TARGET_AURA_TAP) ||
        col == tid(RD_TARGET_FEED128)) {
        return RD_PERF_POST_AURA;
    }
    return -1;
}

static bool isShadowLevel(uint32_t t)
{
    return t && (t == tid(RD_TARGET_SHADOW0) || t == tid(RD_TARGET_SHADOW1) ||
                 t == tid(RD_TARGET_SHADOW2));
}

/* the class of an effect the perf classes name (RD_PERF_POST_*), 0 none */
static uint32_t effectBit(int post)
{
    switch (post) {
    case RD_PERF_POST_FOG:
        return RD_BENCH_BIT(RD_BENCH_C_FOG);
    case RD_PERF_POST_AURA:
        return RD_BENCH_BIT(RD_BENCH_C_AURA);
    case RD_PERF_POST_REDUCTION:
        return RD_BENCH_BIT(RD_BENCH_C_REDUCTION);
    case RD_PERF_POST_MOTION_BLUR:
        return RD_BENCH_BIT(RD_BENCH_C_MOTION_BLUR);
    case RD_PERF_POST_DOF:
        return RD_BENCH_BIT(RD_BENCH_C_DOF);
    case RD_PERF_POST_SOFTEN:
        return RD_BENCH_BIT(RD_BENCH_C_SOFTEN);
    default:
        return 0;
    }
}

static uint32_t classOf(const RdFrame *f, int l, const RdCmd *c, const RdStateBlock *st, int post,
                        uint32_t src, RdBenchAreaFn area, void *user)
{
    uint32_t k = RD_BENCH_BIT(RD_BENCH_C_ACTION);
    const bool vu = c->type >= RDC_MESH && c->type <= RDC_PARTICLES;
    const uint32_t dst = st->color;
    const uint32_t scene = tid(RD_TARGET_SCENE);
    const bool work2 = dst == tid(RD_TARGET_WORK2) || dst == tid(RD_TARGET_WORK2_PAD);
    /* an effect other than the glow in force (the fog, the aura, ...): its
       own, whatever texture the state still names */
    const bool effect = post >= 0 && post != RD_PERF_POST_GLOW;
    if (c->type == RDC_SHADOW_STRIP) {
        k |= RD_BENCH_BIT(RD_BENCH_C_VOLUMES);
    } else if (c->type == RDC_SHADOW_RESET || c->type == RDC_SHADOW_RESOLVE) {
        k |= RD_BENCH_BIT(RD_BENCH_C_WINDOW);
    } else if (effect) {
        k |= effectBit(post);
    } else if (isShadowLevel(dst)) {
        k |= RD_BENCH_BIT(RD_BENCH_C_LEVELS);
    } else if (dst == scene && isShadowLevel(src)) {
        k |= RD_BENCH_BIT(RD_BENCH_C_COMPOSITES);
    } else if (dst == scene && src &&
               (src == tid(RD_TARGET_WORK3) || src == tid(RD_TARGET_WORK3_HELD))) {
        k |= RD_BENCH_BIT(RD_BENCH_C_PASTE);
    } else if (post == RD_PERF_POST_GLOW || (vu && work2)) {
        if (work2) {
            k |= RD_BENCH_BIT(vu ? RD_BENCH_C_LAMPS : RD_BENCH_C_LAMPS_SETUP);
        } else if (dst == tid(RD_TARGET_WORK0) || dst == tid(RD_TARGET_WORK1) ||
                   dst == tid(RD_TARGET_WORK3)) {
            k |= RD_BENCH_BIT(RD_BENCH_C_CHAIN);
        } else if (dst == scene) {
            k |= RD_BENCH_BIT(RD_BENCH_C_MASK);
        } else {
            k |= RD_BENCH_BIT(RD_BENCH_C_GLOW_OTHER);
        }
    }
    if (l == 0 && vu && (st->ds.test.ate || st->ds.abe)) {
        k |= RD_BENCH_BIT(RD_BENCH_C_L0_ALPHA);
    }
    if (l == 0 && c->type == RDC_MESH && area && area(f, c, st, user) > RD_BENCH_LARGE_SHARE) {
        k |= RD_BENCH_BIT(RD_BENCH_C_L0_LARGE);
    }
    return k;
}

void rd_bench_classify(const RdFrame *f, RdBenchSrcFn src, RdBenchAreaFn area, void *user,
                       uint32_t *const cls[RD_LIST_COUNT])
{
    if (!src) {
        src = srcOfTex;
    }
    if (!area) {
        area = areaOf;
    }
    RdStateBlock st = f->startState;
    int post = -1, targetEffect = -1;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            cls[l][i] = 0;
            if (c->type == RDC_TARGET) {
                post = targetEffect = targetPost(&st, c);
            }
            if (rd__cmd_is_state(c->type)) {
                (void)rd__apply_state(&st, c);
                continue;
            }
            if (c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT) {
                continue;
            }
            if (c->type == RDC_POST_STUB && rd__perf_post_of_kind(c->b[0]) >= 0) {
                post = rd__perf_post_of_kind(c->b[0]);
            }
            uint32_t from = st.ds.texEnabled ? src(st.tex, user) : 0u;
            if (c->type == RDC_POST_STUB && f->payload &&
                (uint64_t)c->u[1] + sizeof(RdPostRec) <= f->payloadSize) {
                RdPostRec p;
                memcpy(&p, f->payload + c->u[1], sizeof(p));
                if (p.src) {
                    from = p.src; /* a source the record names (rd_no_volume.c reads it so) */
                }
            }
            cls[l][i] = classOf(f, l, c, &st, post, from, area, user);
        }
        /* the next list's work is the effect of the target in force */
        post = targetEffect;
    }
}

uint32_t rd_bench_mask(RdFrame *f, RdCmd *const orig[RD_LIST_COUNT],
                       uint32_t *const cls[RD_LIST_COUNT], uint32_t classMask, uint32_t listMask)
{
    uint32_t n = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        RdCmdList *list = &f->lists[l];
        if (!list->count || !orig[l]) {
            continue;
        }
        memcpy(list->cmds, orig[l], (size_t)list->count * sizeof(RdCmd));
        const bool whole = ((listMask >> l) & 1u) != 0;
        for (uint32_t i = 0; i < list->count; i++) {
            const uint32_t k = cls[l][i];
            if ((whole && (k & RD_BENCH_BIT(RD_BENCH_C_ACTION))) || (k & classMask)) {
                list->cmds[i].type = RDC_NOP;
                n++;
            }
        }
    }
    return n;
}

/* ------------------------------------------------------------ switches */

/* what each switch was before the bench changed it (taken at its first
   change, given back by rd_bench_switches_restore) */
static struct {
    bool taken;
    uint8_t filter;
    uint32_t crack;
    bool set[RD_BENCH_S_COUNT];
} s_sw;

static void takeBefore(void)
{
    if (!s_sw.taken) {
        s_sw.taken = true;
        s_sw.filter = g_rd.filterUpgrade;
        s_sw.crack = rd__crack_fill_bench_save();
    }
}

void rd_bench_switches_restore(void)
{
    if (!s_sw.taken) {
        return;
    }
    if (s_sw.set[RD_BENCH_S_NV_BREAK] || s_sw.set[RD_BENCH_S_NV_OPEN]) {
        rd__no_volume_bench_force(-1, -1);
    }
    if (s_sw.set[RD_BENCH_S_DATE_ONE]) {
        rd__date_one_force(-1);
    }
    if (s_sw.set[RD_BENCH_S_OWN_PASS]) {
        rd__shadow_merge_force(-1);
    }
    if (s_sw.set[RD_BENCH_S_LAMPS_1X] || s_sw.set[RD_BENCH_S_LAMPS_FULL]) {
        rd__lamps_force(-1);
    }
    if (s_sw.set[RD_BENCH_S_FILTER]) {
        g_rd.filterUpgrade = s_sw.filter;
    }
    if (s_sw.set[RD_BENCH_S_SCENE]) {
        rd__present_scene_force(-1);
    }
    if (s_sw.set[RD_BENCH_S_NO_CRACK]) {
        rd__crack_fill_bench_restore(s_sw.crack);
    }
    if (s_sw.set[RD_BENCH_S_VOLUME_FS]) {
        rd__volume_fs_force(-1);
    }
    if (s_sw.set[RD_BENCH_S_PASTE_SPEC]) {
        rd__paste_spec_force(-1);
    }
    memset(&s_sw, 0, sizeof(s_sw));
}

/* the variant's switch set (after every other restored) */
static void switchSet(int sw)
{
    if (sw <= RD_BENCH_S_NONE || sw >= RD_BENCH_S_COUNT) {
        return;
    }
    takeBefore();
    s_sw.set[sw] = true;
    switch (sw) {
    case RD_BENCH_S_NV_BREAK:
        rd__no_volume_bench_force(0, rd__no_volume_bench_mode() == 1 ? 0 : 1);
        break;
    case RD_BENCH_S_NV_OPEN:
        rd__no_volume_bench_force(1, 0);
        break;
    case RD_BENCH_S_DATE_ONE:
        rd__date_one_force(rd__date_one_mode() ? 0 : 1);
        break;
    case RD_BENCH_S_OWN_PASS:
        rd__shadow_merge_force(0);
        break;
    case RD_BENCH_S_LAMPS_1X:
        rd__lamps_force((int)(rd__lamps_bits() & ~(RD_LAMPS_LOWRESMS | RD_LAMPS_LOWRESMS8)));
        break;
    case RD_BENCH_S_LAMPS_FULL:
        rd__lamps_force((int)(rd__lamps_bits() & ~RD_LAMPS_LOWRES_ANY));
        break;
    case RD_BENCH_S_FILTER:
        g_rd.filterUpgrade =
            (uint8_t)(s_sw.filter == RD_FILTER_UPGRADE_ANISOTROPIC ? RD_FILTER_UPGRADE_TRILINEAR
                                                                   : RD_FILTER_UPGRADE_ANISOTROPIC);
        break;
    case RD_BENCH_S_SCENE:
        rd__present_scene_force(1);
        break;
    case RD_BENCH_S_NO_CRACK:
        rd__crack_fill_set_enabled(false);
        break;
    case RD_BENCH_S_VOLUME_FS:
        rd__volume_fs_force(rd__volume_fs_on() ? 0 : 1);
        break;
    case RD_BENCH_S_PASTE_SPEC:
        rd__paste_spec_force(rd__paste_spec_on() ? 0 : 1);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ the frame */

static bool prepare(RdBenchFrame *b)
{
    bool ok = true;
    for (int l = 0; l < RD_LIST_COUNT && ok; l++) {
        const uint32_t n = b->f.lists[l].count;
        if (!n) {
            continue;
        }
        b->orig[l] = malloc((size_t)n * sizeof(RdCmd));
        b->cls[l] = calloc(n, sizeof(uint32_t));
        ok = b->orig[l] && b->cls[l];
        if (ok) {
            memcpy(b->orig[l], b->f.lists[l].cmds, (size_t)n * sizeof(RdCmd));
        }
        b->commands += n;
    }
    if (!ok) {
        return false;
    }
    rd_bench_classify(&b->f, NULL, NULL, NULL, b->cls);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < b->f.lists[l].count; i++) {
            const uint32_t k = b->cls[l][i];
            for (int c = 0; c < RD_BENCH_C_COUNT; c++) {
                b->classCount[c] += (k >> c) & 1u;
            }
            b->listActions[l] += k & 1u;
        }
    }
    return true;
}

static void releaseCopies(RdBenchFrame *b)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (b->orig[l] && b->f.lists[l].cmds && b->f.lists[l].count) {
            memcpy(b->f.lists[l].cmds, b->orig[l], (size_t)b->f.lists[l].count * sizeof(RdCmd));
        }
        free(b->orig[l]);
        free(b->cls[l]);
        b->orig[l] = NULL;
        b->cls[l] = NULL;
    }
}

bool rd_bench_frame_load(RdBenchFrame *b, const void *bytes, size_t len, const char *path)
{
    memset(b, 0, sizeof(*b));
    if (!bytes || !len || !path || !g_rd.hasDevice) {
        return false;
    }
    FILE *fp = ico_fopen(path, "wb");
    if (!fp) {
        return false;
    }
    const bool wrote = fwrite(bytes, 1, len, fp) == len;
    const bool closed = fclose(fp) == 0;
    const bool loaded = wrote && closed && rd__load_frame(path, &b->f);
    (void)ico_remove(path);
    if (!loaded) {
        return false;
    }
    b->owned = true;
    b->bytes = len;
    if (!prepare(b)) {
        rd_bench_frame_free(b);
        return false;
    }
    return true;
}

bool rd_bench_frame_adopt(RdBenchFrame *b, const RdFrame *f)
{
    memset(b, 0, sizeof(*b));
    if (!f) {
        return false;
    }
    b->f = *f;
    b->owned = false;
    if (!prepare(b)) {
        releaseCopies(b);
        return false;
    }
    return true;
}

/* the load's images (named "dump" by rd__load_frame) and meshes, each
   once */
static void destroyMade(RdBenchFrame *b)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &b->f.lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            if (c->type == RDC_TEXTURE) {
                RdTexRec *t = rd__tex_rec(c->u[0]);
                if (t && t->live && t->kind == RD_TEXKIND_IMAGE && strcmp(t->name, "dump") == 0) {
                    rd_destroy_texture((RdTex){c->u[0]});
                }
            } else if (c->type == RDC_MESH || c->type == RDC_SKINNED) {
                const RdMeshRec *m = rd__mesh_rec(c->u[0]);
                if (m && m->live) {
                    rd_destroy_vu_mesh((RdMesh){c->u[0]});
                }
            }
        }
    }
    const uint32_t st[2] = {b->f.startState.tex, b->f.endState.tex};
    for (int k = 0; k < 2; k++) {
        RdTexRec *t = st[k] ? rd__tex_rec(st[k]) : NULL;
        if (t && t->live && t->kind == RD_TEXKIND_IMAGE && strcmp(t->name, "dump") == 0) {
            rd_destroy_texture((RdTex){st[k]});
        }
    }
}

void rd_bench_frame_free(RdBenchFrame *b)
{
    rd_bench_settle();
    releaseCopies(b);
    if (b->owned) {
        if (g_rd.hasDevice && !rhi_device_lost()) {
            rhi_wait_idle(); /* nothing in flight reads what goes */
        }
        destroyMade(b);
        rd__frame_free(&b->f);
    }
    memset(b, 0, sizeof(*b));
}

/* ------------------------------------------------------------- variants */

static void put(RdBenchVariant *out, int max, int *n, const char *name, uint32_t classes,
                uint32_t lists, int sw, int knock, int sum)
{
    if (*n >= max) {
        return;
    }
    RdBenchVariant *v = &out[(*n)++];
    memset(v, 0, sizeof(*v));
    snprintf(v->name, sizeof(v->name), "%s", name);
    v->classes = classes;
    v->lists = lists;
    v->sw = sw;
    v->knock = (uint8_t)knock;
    v->sum = (uint8_t)sum;
}

#define HAS(c) (b->classCount[(c)] != 0)
#define BIT(c) RD_BENCH_BIT(c)

int rd_bench_variants(const RdBenchFrame *b, RdBenchVariant *out, int max)
{
    int n = 0;
    char name[96];
    put(out, max, &n, "baseline", 0, 0, RD_BENCH_S_NONE, 0, 0);
    /* the lists, each whole */
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (b->listActions[l]) {
            snprintf(name, sizeof(name), "without list %d", l);
            put(out, max, &n, name, 0, 1u << l, RD_BENCH_S_NONE, 1, 1);
        }
    }
    /* the parts (sum 2: disjoint; the volumes and the levels lie inside the
       window's knock-out, list 0's alpha and large draws inside list 0's) */
    if (HAS(RD_BENCH_C_VOLUMES)) {
        put(out, max, &n, "without the shadow volumes' draws", BIT(RD_BENCH_C_VOLUMES), 0,
            RD_BENCH_S_NONE, 1, 0);
    }
    if (HAS(RD_BENCH_C_WINDOW)) {
        put(out, max, &n, "without the shadow window (reset, volumes, count, levels)",
            BIT(RD_BENCH_C_WINDOW) | BIT(RD_BENCH_C_VOLUMES) | BIT(RD_BENCH_C_LEVELS), 0,
            RD_BENCH_S_NONE, 1, 2);
    }
    if (HAS(RD_BENCH_C_LEVELS)) {
        put(out, max, &n, "without the shadow levels", BIT(RD_BENCH_C_LEVELS), 0, RD_BENCH_S_NONE,
            1, 0);
    }

    static const struct {
        int c;
        const char *name;
    } kParts[] = {
        {RD_BENCH_C_COMPOSITES, "without the shadow composites"},
        {RD_BENCH_C_FOG, "without the fog (and the crack fill it arms)"},
        {RD_BENCH_C_LAMPS_SETUP, "without the lamps' set-up (WORK2's clears, the depth fill)"},
        {RD_BENCH_C_LAMPS, "without the lamps' meshes"},
        {RD_BENCH_C_MASK, "without the glow mask"},
        {RD_BENCH_C_CHAIN, "without the blur chain"},
        {RD_BENCH_C_PASTE, "without the glow paste"},
        {RD_BENCH_C_GLOW_OTHER, "without the glow's other draws"},
        {RD_BENCH_C_AURA, "without the aura"},
        {RD_BENCH_C_MOTION_BLUR, "without the motion blur"},
        {RD_BENCH_C_DOF, "without the depth of field"},
        {RD_BENCH_C_SOFTEN, "without the softening"},
        {RD_BENCH_C_REDUCTION, "without the reduction (its draws: DISPLAY not drawn)"},
    };

    for (size_t i = 0; i < sizeof(kParts) / sizeof(kParts[0]); i++) {
        if (HAS(kParts[i].c)) {
            put(out, max, &n, kParts[i].name, BIT(kParts[i].c), 0, RD_BENCH_S_NONE, 1, 2);
        }
    }
    if (HAS(RD_BENCH_C_FOG) && g_rd.hasDevice) {
        put(out, max, &n, "without the crack fill", 0, 0, RD_BENCH_S_NO_CRACK, 1, 0);
    }
    if (HAS(RD_BENCH_C_L0_ALPHA)) {
        put(out, max, &n, "list 0 without its alpha-tested and blended draws",
            BIT(RD_BENCH_C_L0_ALPHA), 0, RD_BENCH_S_NONE, 1, 0);
    }
    if (HAS(RD_BENCH_C_L0_LARGE)) {
        put(out, max, &n, "list 0 without its draws over a quarter of the picture",
            BIT(RD_BENCH_C_L0_LARGE), 0, RD_BENCH_S_NONE, 1, 0);
    }
    /* the structural switches (no device: none) */
    if (!g_rd.hasDevice) {
        return n;
    }
    if (HAS(RD_BENCH_C_WINDOW)) {
        const int mode = rd__no_volume_bench_mode();
        put(out, max, &n,
            mode == 1 ? "the no-volume break mode off" : "the no-volume break mode on", 0, 0,
            RD_BENCH_S_NV_BREAK, 0, 0);
        if (mode != 2) {
            put(out, max, &n,
                "the open no-volume mode (SCENE's pass not ended; pictures not "
                "kept)",
                0, 0, RD_BENCH_S_NV_OPEN, 0, 0);
        }
    }
    if (HAS(RD_BENCH_C_COMPOSITES)) {
        put(out, max, &n,
            rd__date_one_mode() ? "DATE's shared set-up off" : "DATE's shared set-up on", 0, 0,
            RD_BENCH_S_DATE_ONE, 0, 0);
    }
    if (HAS(RD_BENCH_C_VOLUMES) && rd__shadow_merge_on()) {
        put(out, max, &n, "the shadow volumes in a pass of their own", 0, 0, RD_BENCH_S_OWN_PASS, 0,
            0);
    }
    if (HAS(RD_BENCH_C_LAMPS)) {
        const uint32_t bits = rd__lamps_bits();
        if (bits & (RD_LAMPS_LOWRESMS | RD_LAMPS_LOWRESMS8)) {
            put(out, max, &n, "the lamps' pass at 1 sample", 0, 0, RD_BENCH_S_LAMPS_1X, 0, 0);
        }
        if (bits & RD_LAMPS_LOWRES_ANY) {
            put(out, max, &n, "the lamps' target at full size (not the GS size)", 0, 0,
                RD_BENCH_S_LAMPS_FULL, 0, 0);
        }
    }
    if (g_rd.filterUpgrade == RD_FILTER_UPGRADE_TRILINEAR ||
        g_rd.filterUpgrade == RD_FILTER_UPGRADE_ANISOTROPIC) {
        put(out, max, &n,
            g_rd.filterUpgrade == RD_FILTER_UPGRADE_ANISOTROPIC
                ? "the texture filter trilinear (not anisotropic)"
                : "the texture filter anisotropic (not trilinear)",
            0, 0, RD_BENCH_S_FILTER, 0, 0);
    }
    if (HAS(RD_BENCH_C_REDUCTION) && !rd__present_scene_on()) {
        put(out, max, &n, "the present from SCENE (no reduction where it may)", 0, 0,
            RD_BENCH_S_SCENE, 0, 0);
    }
    /* the volumes' stencil-only entry (rd_volume_fs.c) and the glow's paste
       by its own entry (rd_paste_spec.c), each the other way, where its
       self-test passed (a force cannot take either past its self-test) */
    if (HAS(RD_BENCH_C_VOLUMES) && rd__volume_fs_test_state(NULL) == 1) {
        put(out, max, &n,
            rd__volume_fs_on() ? "the shadow volumes by sprite_ps (not the stencil-only entry)"
                               : "the shadow volumes by the stencil-only entry (date_clear_ps)",
            0, 0, RD_BENCH_S_VOLUME_FS, 0, 0);
    }
    if (HAS(RD_BENCH_C_PASTE) && rd__paste_spec_test_state(NULL) == 1) {
        put(out, max, &n,
            rd__paste_spec_on() ? "the glow paste by fx_frame_ps (not its own entry)"
                                : "the glow paste by its own entry (fx_paste_ps)",
            0, 0, RD_BENCH_S_PASTE_SPEC, 0, 0);
    }
    /* another package's switch is a row here: an RD_BENCH_S_* value
       (rd_bench.h), its case in switchSet (the variant's value) and in
       rd_bench_switches_restore (its force back to -1), and a put() here
       when the frame has the class it acts on */
    return n;
}

#undef HAS
#undef BIT

uint32_t rd_bench_apply(RdBenchFrame *b, const RdBenchVariant *v)
{
    rd_bench_settle();
    rd_bench_switches_restore();
    const uint32_t n = rd_bench_mask(&b->f, b->orig, b->cls, v->classes, v->lists);
    switchSet(v->sw);
    return n;
}

bool rd_bench_post(RdBenchFrame *b)
{
    if (!g_rd.hasDevice || rhi_device_lost()) {
        return false;
    }
    return rd__replay_frame_async(&b->f, 0, true);
}

void rd_bench_settle(void)
{
    (void)rd_thread_drain();
}
