/*
 * port/render/rd_hold_key.c
 *
 * The paused scene's hold, its CPU half (rd_hold.h): the change counters,
 * the frame's rules, the keys and the decision.  Nothing here touches the
 * device, so the unit test (test/rd_hold_test.c) runs it on frames recorded
 * without one.
 */
#include <stdio.h>
#include <string.h>

#include "rd_hold.h"
#include "rd_layer.h"
#include "rd_velocity.h"
#include "xxh3.h"

/* rd_dump.c: every payload range command c names lies in the payload */
bool rd__dump_cmd_valid(const uint8_t *payload, uint32_t psz, const RdCmd *c);

/* ------------------------------------------------------------- counters */

/* bumped on the game thread (meshes, textures) and the render thread (a
 * texture's mips marked for upload in a replay), read on the render thread */
static uint64_t s_meshGen, s_texGen;

void rd__hold_mesh_changed(void)
{
    __atomic_add_fetch(&s_meshGen, 1u, __ATOMIC_RELAXED);
}

void rd__hold_tex_changed(void)
{
    __atomic_add_fetch(&s_texGen, 1u, __ATOMIC_RELAXED);
}

uint64_t rd__hold_mesh_gen(void)
{
    return __atomic_load_n(&s_meshGen, __ATOMIC_RELAXED);
}

uint64_t rd__hold_tex_gen(void)
{
    return __atomic_load_n(&s_texGen, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------ the rules */

/* a target id's named slot (RdTargetId, or a display layer slot), -1 for a
 * temporary or none */
static int slotOf(uint32_t id)
{
    if (id == 0) {
        return -1;
    }
    const int slot = (int)(id & 0xFFFFu) - 1;
    return slot >= 0 && slot < RD_MAX_TARGETS ? slot : -1;
}

/* the picture before: DISPLAY, its held copy, the display layer's slots */
static bool isPicture(int slot)
{
    return slot == RD_TARGET_DISPLAY || slot == RD_TARGET_DISPLAY_HELD ||
           slot >= RD_LAYER_SLOT_FIRST;
}

/* the feedback lists 0-10 keep across replays */
static bool isCarried(int slot)
{
    return slot == RD_TARGET_FEED128 || slot == RD_TARGET_FEED_HELD || slot == RD_TARGET_WORK3_HELD;
}

/* the target a texture id names (RD_TEXKIND_TARGET), 0 for an image or none;
 * *depthView the depth view */
static uint32_t texTarget(uint32_t tex, bool *depthView)
{
    const RdTexRec *t = tex ? rd__tex_rec(tex) : NULL;
    *depthView = false;
    if (!t || t->kind != RD_TEXKIND_TARGET) {
        return 0;
    }
    *depthView = t->view == RD_VIEW_DEPTH;
    return t->target;
}

static bool isDraw(uint8_t type)
{
    return type == RDC_SCREEN || (type >= RDC_MESH && type <= RDC_PARTICLES) ||
           type == RDC_SHADOW_STRIP || type == RDC_SHADOW_RESET || type == RDC_SHADOW_RESOLVE ||
           type == RDC_POST_STUB;
}

/* the targets command c (state st) names: up to 6 */
static uint32_t touched(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, uint32_t out[6])
{
    uint32_t n = 0;
    bool dv = false;
    switch (c->type) {
    case RDC_CLEAR:
        out[n++] = c->u[0];
        break;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
    case RDC_UPSCALE_POINT:
        out[n++] = c->u[0];
        out[n++] = c->u[1];
        break;
    default:
        if (!isDraw(c->type)) {
            break;
        }
        out[n++] = st->color;
        out[n++] = st->depth;
        if (st->ds.texEnabled || c->type == RDC_POST_STUB) {
            out[n++] = texTarget(st->tex, &dv);
        }
        if (c->type == RDC_POST_STUB) {
            RdPostRec p;
            memcpy(&p, f->payload + c->u[1], sizeof(p)); /* in range: checked before */
            out[n++] = p.src;
            out[n++] = p.dst;
        }
        break;
    }
    return n;
}

/* a list 11-12 command the hold keeps: drawn into SCENE or DISPLAY, reading
 * no target but SCENE's colour and no depth of any kind */
static bool menuOk(const RdFrame *f, const RdCmd *c, const RdStateBlock *st)
{
    const int scene = RD_TARGET_SCENE, disp = RD_TARGET_DISPLAY;
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return true;
    case RDC_CLEAR: {
        const int s = slotOf(c->u[0]);
        return s == scene || s == disp;
    }
    case RDC_SCREEN:
        break;
    case RDC_POST_STUB:
        if (c->b[0] == RD_POST_FOG) {
            return false;
        }
        break;
    default:
        return false; /* copies, exact blends, VU draws, shadows, the upscale point */
    }
    const int dst = slotOf(st->color);
    if (dst != scene && dst != disp) {
        return false;
    }
    bool dv = false;
    if (st->ds.texEnabled || c->type == RDC_POST_STUB) {
        const uint32_t t = texTarget(st->tex, &dv);
        if (t && (slotOf(t) != scene || dv)) {
            return false;
        }
    }
    if (c->type == RDC_POST_STUB) {
        RdPostRec p;
        memcpy(&p, f->payload + c->u[1], sizeof(p));
        if ((p.src && slotOf(p.src) != scene) ||
            (p.dst && slotOf(p.dst) != scene && slotOf(p.dst) != disp)) {
            return false;
        }
    }
    const RdDrawState *d = &st->ds;
    if (st->depth != 0 && d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) {
        return false; /* a Z test reads the depth */
    }
    return d->test.date == RD_DATE_OFF && st->aa1 == 0;
}

int rd__hold_scan(const RdFrame *f, RdHoldScan *s)
{
    memset(s, 0, sizeof(*s));
    s->badList = -1;
    if (!f) {
        return RD_HOLD_RULE_PAYLOAD;
    }
    if (f->keep) {
        return RD_HOLD_RULE_KEEP;
    }
    RdStateBlock st = f->startState;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            if (rd__apply_state(&st, c)) {
                continue;
            }
            if (!rd__dump_cmd_valid(f->payload, f->payloadSize, c)) {
                return RD_HOLD_RULE_PAYLOAD;
            }
            if (l >= RD_HOLD_LIST) {
                if (!menuOk(f, c, &st)) {
                    s->badList = (int8_t)l;
                    s->badCmd = i;
                    return RD_HOLD_RULE_MENU;
                }
                s->menuDraws +=
                    l == RD_HOLD_LIST && c->type != RDC_NOP && c->type != RDC_OVERLAY_TEXT;
                continue;
            }
            uint32_t ids[6];
            const uint32_t n = touched(f, c, &st, ids);
            for (uint32_t k = 0; k < n; k++) {
                const int slot = slotOf(ids[k]);
                if (slot < 0) {
                    continue;
                }
                if (isPicture(slot)) {
                    return RD_HOLD_RULE_PICTURE;
                }
                if (!isCarried(slot)) {
                    continue;
                }
                bool seen = false;
                for (uint32_t j = 0; j < s->crossN; j++) {
                    seen = seen || slotOf(s->cross[j]) == slot;
                }
                if (!seen && s->crossN < RD_HOLD_CROSS_MAX) {
                    s->cross[s->crossN++] = ids[k];
                }
            }
        }
    }
    return RD_HOLD_RULE_OK;
}

const char *rd__hold_rule_text(int rule)
{
    switch (rule) {
    case RD_HOLD_RULE_OK:
        return "the frame qualifies";
    case RD_HOLD_RULE_KEEP:
        return "a keep frame";
    case RD_HOLD_RULE_PICTURE:
        return "the scene reads the picture before (the motion blur)";
    case RD_HOLD_RULE_PAYLOAD:
        return "a command names bytes outside the payload";
    case RD_HOLD_RULE_MENU:
        return "lists 11-12 draw or read what the hold cannot keep";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------- the keys */

static uint64_t mix(uint64_t acc, uint64_t part)
{
    const uint64_t two[2] = {acc, part};
    return xxh3_64(two, sizeof(two));
}

static uint64_t bytes(const void *p, size_t n)
{
    return n ? xxh3_64(p, n) : 0;
}

/* the payload bytes [off, off + n) of f (in range: rd__dump_cmd_valid) */
static uint64_t span(const RdFrame *f, uint32_t off, uint64_t n)
{
    return off <= f->payloadSize && n <= (uint64_t)(f->payloadSize - off)
               ? bytes(f->payload + off, (size_t)n)
               : ~0ull;
}

/* one command: its bytes with the payload offsets zeroed, then the bytes
 * they name */
static uint64_t cmdKey(const RdFrame *f, const RdCmd *c)
{
    RdCmd k = *c;
    uint64_t h = 0;
    switch (c->type) {
    case RDC_SCREEN:
        k.u[0] = 0;
        h = span(f, c->u[0], (uint64_t)c->u[1] * sizeof(RdScreenVtx));
        break;
    case RDC_COPY:
        k.u[2] = 0;
        h = span(f, c->u[2], sizeof(RdCopyRec));
        break;
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES: {
        k.u[1] = 0;
        h = span(f, c->u[1], c->u[2]);
        if (c->type == RDC_MESH || c->type == RDC_SKINNED) {
            /* the interpolation's scratch mesh (rd_interp.c morphDraw): one
               id from present to present, its stream, indices and batches
               rewritten at each (a blend step, another source mesh), none
               of it counted by rd__hold_mesh_changed.  Its stream serial is
               new at every rewrite (scratchFor, rd__mesh_stream_written),
               so a list that draws one never keys equal to another
               present's: never held, never the self-test's picture */
            const RdMeshRec *m = rd__mesh_rec(c->u[0]);
            if (m && m->transient) {
                h = mix(h, m->streamSerial);
            }
        }
        if ((c->u[3] & RD_VU_PREV) != 0u && c->type != RDC_PARTICLES) {
            /* the VuPrevCB (velocity): its bytes, the offset left out */
            k.u[3] = c->u[3] & ~RD_VU_PREV_OFF;
            const uint8_t *prev = rd__velocity_prev(f, c);
            RdVuPayload ph;
            if (prev) {
                memcpy(&ph, prev, sizeof(ph));
                h = mix(h, bytes(prev, (size_t)rd__velocity_prev_size(&ph)));
            } else {
                h = mix(h, ~0ull);
            }
        }
        break;
    }
    case RDC_WORLD_PRIMS:
        k.u[1] = 0;
        h = span(f, c->u[1], c->u[2]);
        break;
    case RDC_SHADOW_STRIP:
        k.u[1] = 0;
        h = span(f, c->u[1],
                 c->b[0] == RD_SHADOW_TRIS ? ((uint64_t)c->u[0] + c->u[3]) * sizeof(RdScreenVtx)
                                           : (uint64_t)c->u[0] * 16u);
        break;
    case RDC_POST_STUB: {
        k.u[1] = 0;
        RdPostRec p;
        memcpy(&p, f->payload + c->u[1], sizeof(p));
        const uint32_t lut = p.lutOffset;
        p.lutOffset = lut == ~0u ? ~0u : 0u;
        h = bytes(&p, sizeof(p));
        if (lut != ~0u) {
            h = mix(h, span(f, lut,
                            c->b[0] == RD_POST_REDUCTION ? 4u * sizeof(RdScreenVtx) : 256u * 4u));
        }
        break;
    }
    case RDC_OVERLAY_TEXT:
        k.u[1] = 0;
        h = span(f, c->u[1], c->u[2]);
        break;
    default:
        break;
    }
    return mix(bytes(&k, sizeof(k)), h);
}

uint64_t rd__hold_lists_key(const RdFrame *f)
{
    uint64_t h = 0;
    for (int l = 0; l < RD_HOLD_LIST; l++) {
        const RdCmdList *cl = &f->lists[l];
        h = mix(h, cl->count);
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            if (!rd__cmd_is_state(c->type) && c->type != RDC_NOP &&
                !rd__dump_cmd_valid(f->payload, f->payloadSize, c)) {
                return mix(h, ~0ull); /* rd__hold_scan refuses the frame */
            }
            h = mix(h, cmdKey(f, c));
        }
    }
    h = mix(h, bytes(&f->startState, sizeof(f->startState)));
    h = mix(h, f->hasCamera ? bytes(&f->camera, sizeof(f->camera)) : 0);
    h = mix(h, f->hasVu ? bytes(&f->vu, sizeof(f->vu)) : 0);
    h = mix(h, f->hasTickCams ? bytes(f->tickCams, sizeof(f->tickCams)) : 0);
    uint32_t sc[16];
    memset(sc, 0, sizeof(sc));
    sc[0] = f->hasCamera;
    sc[1] = f->keep;
    sc[2] = f->gsW;
    sc[3] = f->gsH;
    memcpy(&sc[4], &f->aspect, 4);
    memcpy(&sc[5], &f->wExact, 4);
    sc[6] = f->cullW;
    sc[7] = f->hasVu;
    sc[8] = f->headValid;
    sc[9] = f->headStart[0];
    sc[10] = f->headEnd[0];
    sc[11] = f->headClear[0];
    sc[12] = f->headTarget[0];
    sc[13] = f->cut;
    sc[14] = f->freeCamera;
    sc[15] = f->hasTickCams;
    return mix(h, bytes(sc, sizeof(sc)));
}

/* a named target's part of the state key: its record's generation, sizes,
 * scale, format and its colour texture with its snapshot's (as a pair: the
 * crack fill's swap exchanges them, rd_reshow.c) */
static uint64_t targetKey(uint64_t h, RdTargetId id)
{
    const RdTargetRec *t = rd__target_rec(rd_target(id).id);
    if (!t) {
        return mix(h, 0);
    }
    const uint32_t a = t->color.id, b = t->snap.id;
    const uint32_t w[10] = {t->gen,
                            t->w,
                            t->h,
                            t->tw,
                            t->th,
                            (uint32_t)t->format,
                            b && b < a ? b : a,
                            b && b < a ? a : b,
                            0,
                            0};
    float s[3] = {t->sx, t->sy, t->ox};
    return mix(mix(h, bytes(w, sizeof(w))), bytes(s, sizeof(s)));
}

uint64_t rd__hold_state_key(void)
{
    uint64_t h = bytes(&g_rd.settings, sizeof(g_rd.settings));
    /* the levels' filter (g_rd.texLevelsFilter) is the decision's texture
       wait: it differs from filterUpgrade only until the next upload */
    const int32_t w[3] = {(int32_t)g_rd.filterUpgrade, (int32_t)g_rd.fogPath,
                          (int32_t)g_rd.dstMode};
    h = mix(h, bytes(w, sizeof(w)));
    h = targetKey(h, RD_TARGET_SCENE);
    return targetKey(h, RD_TARGET_DISPLAY);
}

/* ---------------------------------------------------------- the decision */

int rd__hold_why(const RdHoldIn *in)
{
    if (!in->paused) {
        return RD_HOLD_NOT_PAUSED;
    }
    if (!in->on) {
        return RD_HOLD_OFF;
    }
    if (!in->device) {
        return RD_HOLD_DEVICE;
    }
    if (in->rule == RD_HOLD_RULE_PICTURE) {
        return RD_HOLD_PICTURE;
    }
    if (in->rule != RD_HOLD_RULE_OK) {
        return RD_HOLD_MENU;
    }
    if (!in->take) {
        return RD_HOLD_FIRST;
    }
    if (in->lists != in->refLists) {
        return RD_HOLD_LISTS;
    }
    if (in->state != in->refState) {
        return RD_HOLD_SETTINGS;
    }
    if (in->meshGen != in->refMeshGen || in->texGen != in->refTexGen || in->texWaiting) {
        return RD_HOLD_UPLOADS;
    }
    return in->settled ? RD_HOLD_HELD : RD_HOLD_FEEDBACK;
}

const char *rd__hold_why_text(int why)
{
    static const char *const k[RD_HOLD_COUNT] = {
        [RD_HOLD_HELD] = "from the held scene",
        [RD_HOLD_NOT_PAUSED] = "the game not paused",
        [RD_HOLD_OFF] = "off",
        [RD_HOLD_DEVICE] = "the device or settings",
        [RD_HOLD_PICTURE] = "the scene reads the picture before",
        [RD_HOLD_MENU] = "lists 11-12 the hold cannot keep",
        [RD_HOLD_FIRST] = "first",
        [RD_HOLD_LISTS] = "lists differ",
        [RD_HOLD_SETTINGS] = "settings",
        [RD_HOLD_UPLOADS] = "uploads",
        [RD_HOLD_FEEDBACK] = "feedback not settled",
        [RD_HOLD_OWED] = "scene work owed at list 11",
    };
    return why >= 0 && why < RD_HOLD_COUNT && k[why] ? k[why] : "unknown";
}
