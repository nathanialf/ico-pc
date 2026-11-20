/* rd_ledger_live.c: the renderer's side of the scene ledger (rd_ledger.h,
 * rd_ledger_live.h).
 *
 * Two halves.  The game's thread records the frames (the draws, the box
 * cull's drops, the meshes built, the effects) into s_cur without a lock:
 * a draw costs the mesh's cached row (s_meshRow, by the mesh's slot and
 * the stage's epoch) and three additions.  The replay (the render thread)
 * records its visibility test's skips into s_rep under s_lock, taken once
 * a picture and once a skip; the game's thread takes the lock once at a
 * stage's end to add s_rep into s_cur.  A replay of a frame recorded before
 * the stage began (the render thread runs a frame or two behind) is not
 * counted in the new stage: the stage's lateEvents. */
#include "rd_ledger_live.h"

#include <stdio.h>
#include <string.h>

#include "rd_vis.h"

static RdLedgerStage s_cur;     /* the stage in play, the game's half */
static RdLedgerTable s_rep;     /* the replay's half, under s_lock */
static uint32_t s_repPictures;  /* under s_lock */
static uint32_t s_repBetween;   /* under s_lock */
static uint32_t s_repLate;      /* under s_lock */
static uint32_t s_repFirst;     /* the stage's first frame, read by the replay */
static RdLedgerStage s_scratch; /* a stage being written (the game's thread) */
static RdLedgerKept s_kept;
static char s_text[RD_LEDGER_TEXT];
static int s_open;
static char s_lock;

/* the row of each mesh slot this stage: valid while id and epoch match */
typedef struct MeshRow {
    uint32_t id, epoch;
    int32_t row;
} MeshRow;

static MeshRow s_meshRow[RD_MAX_MESHES];
static uint32_t s_epoch = 1;

static void lock(void)
{
    while (__atomic_test_and_set(&s_lock, __ATOMIC_ACQUIRE)) {}
}

static int trylock(void)
{
    return !__atomic_test_and_set(&s_lock, __ATOMIC_ACQUIRE);
}

static void unlock(void)
{
    __atomic_clear(&s_lock, __ATOMIC_RELEASE);
}

static uint32_t tickNow(void)
{
    return g_rd.frameCounter;
}

static const char *meshName(const RdMeshRec *m)
{
    return m != NULL && m->name[0] != '\0' ? m->name : "(no mesh)";
}

static int rowOfMesh(const RdMeshRec *m, uint32_t id)
{
    const uint32_t k = (id & 0xFFFFu) - 1u;
    if (m == NULL || id == 0 || k >= RD_MAX_MESHES) {
        return rd_ledger_slot(&s_cur.models, meshName(m));
    }
    MeshRow *r = &s_meshRow[k];
    if (r->id != id || r->epoch != s_epoch) {
        r->id = id;
        r->epoch = s_epoch;
        r->row = rd_ledger_slot(&s_cur.models, meshName(m));
    }
    return r->row;
}

void rd__ledger_built(const char *name)
{
    rd_ledger_note_built(&s_cur.models, rd_ledger_slot(&s_cur.models, name));
}

void rd__ledger_build_failed(const char *name)
{
    rd_ledger_note_drop(&s_cur.models, rd_ledger_slot(&s_cur.models, name), RD_LEDGER_NO_MESH,
                        tickNow(), 0, 1);
}

void rd__ledger_draw(const RdMeshRec *m, uint32_t meshId, uint32_t first, uint32_t n)
{
    uint32_t idx = 0;
    if (first == 0 && n == m->batchCount) {
        idx = m->indexCount;
    } else {
        for (uint32_t b = first; b < first + n && b < m->batchCount; b++) {
            idx += m->batches[b].indexCount;
        }
    }
    rd_ledger_note_draw(&s_cur.models, rowOfMesh(m, meshId), tickNow(), idx / 3u);
}

void rd__ledger_draw_out(const RdMeshRec *m, uint32_t meshId, int reason)
{
    rd_ledger_note_drop(&s_cur.models, rowOfMesh(m, meshId), reason, tickNow(), 0, 1);
}

void rd__ledger_box(const char *model, int part, int rule, int views)
{
    const int reason = rule == 3  ? RD_LEDGER_BOX_PART
                       : part < 0 ? RD_LEDGER_BOX_OBJECT
                                  : RD_LEDGER_BOX_NODE;
    rd_ledger_note_drop(&s_cur.models, rd_ledger_slot(&s_cur.models, model), reason, tickNow(),
                        views > 1, 1);
}

void rd__ledger_picture(uint32_t frame)
{
    const uint32_t first = __atomic_load_n(&s_repFirst, __ATOMIC_ACQUIRE);
    lock();
    if (frame >= first) {
        s_repPictures++;
        s_repBetween += g_rdPerf.interpolated != 0;
    }
    unlock();
}

void rd__ledger_cull(const RdCmd *c, int v, uint32_t frame)
{
    const char *name;
    switch (c->type) {
    case RDC_MESH:
    case RDC_SKINNED:
        name = meshName(rd__mesh_rec(c->u[0]));
        break;
    case RDC_GRID:
        name = "(a grid)";
        break;
    case RDC_PARTICLES:
        name = "(particles)";
        break;
    default:
        name = "(a draw)";
        break;
    }
    const int reason = v == RD_VIS_OCCLUDED ? RD_LEDGER_OCCLUDED : RD_LEDGER_FRUSTUM;
    const uint32_t first = __atomic_load_n(&s_repFirst, __ATOMIC_ACQUIRE);
    lock();
    if (frame >= first) {
        rd_ledger_note_drop(&s_rep, rd_ledger_slot(&s_rep, name), reason, g_rdPerf.replay,
                            g_rdPerf.interpolated != 0, 1);
    } else {
        s_repLate++;
    }
    unlock();
}

void rd__ledger_post(int kind, const RdPostParams *p)
{
    int fx;
    float value = (float)p->fix;
    switch (kind) {
    case RD_POST_FOG:
        fx = RD_LEDGER_FX_FOG;
        value = p->scalar[0];
        break;
    case RD_POST_FADE:
        fx = RD_LEDGER_FX_FADE;
        value = (float)p->rgba[3];
        break;
    case RD_POST_LETTERBOX:
        fx = RD_LEDGER_FX_LETTERBOX;
        value = (float)p->lines;
        break;
    case RD_POST_BRIGHTNESS:
        fx = RD_LEDGER_FX_BRIGHTNESS;
        break;
    case RD_POST_FILM_NOISE:
        fx = RD_LEDGER_FX_FILM_NOISE;
        break;
    case RD_POST_MOTION_BLUR:
        fx = RD_LEDGER_FX_MOTION_BLUR;
        break;
    case RD_POST_DOF:
        fx = RD_LEDGER_FX_DOF;
        break;
    case RD_POST_FLARE:
        fx = RD_LEDGER_FX_FLARE;
        break;
    case RD_POST_BLOOM:
        fx = RD_LEDGER_FX_GLOW;
        break;
    case RD_POST_AURA:
        fx = RD_LEDGER_FX_AURA;
        break;
    case RD_POST_EYE_BLUR:
        fx = RD_LEDGER_FX_EYE_BLUR;
        break;
    case RD_POST_AA_DOWNSAMPLE:
        fx = RD_LEDGER_FX_AA;
        break;
    default:
        return;
    }
    rd_ledger_note_fx(&s_cur, fx, tickNow(), value);
}

void rd__ledger_shadow(uint32_t tris)
{
    rd_ledger_note_fx(&s_cur, RD_LEDGER_FX_SHADOWS, tickNow(), (float)tris);
}

/* s_cur with the replay's half added into out; the replay's half reset
   too when reset (a stage's end).  locked 0: the replay's half was busy
   (the crash path does not wait) and is left out */
static int stageNow(RdLedgerStage *out, int reset, int mayWait)
{
    *out = s_cur;
    out->lastTick = tickNow();
    if (mayWait) {
        lock();
    } else if (!trylock()) {
        return 0;
    }
    rd_ledger_table_add(&out->models, &s_rep);
    out->pictures += s_repPictures;
    out->between += s_repBetween;
    out->lateEvents += s_repLate;
    if (reset) {
        rd_ledger_table_reset(&s_rep);
        s_repPictures = s_repBetween = s_repLate = 0;
    }
    unlock();
    return 1;
}

/* the text's lines to the log, one rd__log a line */
static void logText(const char *text)
{
    const char *p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        const int n = nl != NULL ? (int)(nl - p) : (int)strlen(p);
        rd__log("%.*s", n, p);
        p += n + (nl != NULL);
    }
}

static int stageHasContent(const RdLedgerStage *s)
{
    if (s->models.n > 0 || s->pictures > 0) {
        return 1;
    }
    for (int fx = 0; fx < RD_LEDGER_FX_COUNT; fx++) {
        if (s->fx[fx].ticks > 0) {
            return 1;
        }
    }
    return 0;
}

void rd_ledger_scene(int stage, const char *name)
{
    const char *nm = name != NULL ? name : "";
    if (s_open && stage == s_cur.stage && strncmp(nm, s_cur.name, sizeof s_cur.name - 1) == 0) {
        return;
    }
    const uint32_t first = tickNow() + 1u;
    /* the replay counts from the new stage's first frame on */
    __atomic_store_n(&s_repFirst, first, __ATOMIC_RELEASE);
    if (s_open) {
        (void)stageNow(&s_scratch, 1, 1);
        if (stageHasContent(&s_scratch)) {
            (void)rd_ledger_format(&s_scratch, "ended", s_text, sizeof s_text);
            logText(s_text);
            rd_ledger_keep(&s_kept, s_text);
        }
    } else {
        rd_ledger_stage_begin(&s_scratch, -1, "", first);
    }
    rd_ledger_stage_begin(&s_cur, stage, nm, first);
    rd_ledger_carry(&s_cur, &s_scratch);
    s_epoch++;
    s_open = 1;
}

void rd__ledger_log_now(const char *when)
{
    if (!s_open) {
        return;
    }
    (void)stageNow(&s_scratch, 0, 1);
    (void)rd_ledger_format(&s_scratch, when, s_text, sizeof s_text);
    logText(s_text);
}

size_t rd_ledger_report_text(char *out, size_t cap)
{
    /* the report's own buffers (a crash handler's: no stack of 60 KB) */
    static RdLedgerStage now;
    static char text[RD_LEDGER_TEXT + 160];
    text[0] = '\0';
    if (s_open) {
        const int whole = stageNow(&now, 0, 0);
        const size_t n = rd_ledger_format(&now, "so far, at this report", text, RD_LEDGER_TEXT);
        if (!whole) {
            snprintf(text + n, sizeof text - n,
                     "scene ledger: the port's visibility test's part of the stage in play was "
                     "being written and is left out\n");
        }
    }
    return rd_ledger_kept_text(&s_kept, text, out, cap);
}
