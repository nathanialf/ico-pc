/*
 * port/game/state_host.c
 *
 * Save states (port/platform/state.h): the save and the load at the host
 * loop's quiescent point, the slots' files, the thumbnail and the [dev]
 * keys.  Compiled into the program (ico_pc), both builds; the renderer's
 * part (the textures, the meshes, the scene targets, the picture) only in
 * the window build.  docs/port/PLATFORM.md "Save states" has the sequences.
 *
 * A state's sections (state_file.h), by name:
 *   arena   the EE RAM arena, whole (arena.h)
 *   gdata   the game's initialised data, gbss its zero-filled data
 *           (game_data_range.h)
 *   reg     every registered block (state_registry.h) without the AUDIO
 *           flag, back to back in registration order
 *   audio   every AUDIO block, the same way, copied under the audio lock
 *   sched   the scheduler's tables (sched.h)
 *   fibers  every live thread's fiber: a record, its control block and the
 *           used part of its stack (fiber.h IcoFiberImage)
 *   pobj    the decoded tables of the model being loaded
 *           (ico2/common/src/PObj.c), when there is one
 *   ranges  where the saving process had its image, arena, pool, game data
 *           and pobj block, and its mesh generations (StateRanges below)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arena.h"
#include "fiber.h"
#include "fpenv.h"
#include "game_data_range.h"
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "ico_build_commit.h" /* ICO_BUILD_COMMIT, generated (port/platform/CMakeLists.txt) */
#include "ico_thread.h"
#include "sched.h"
#include "sim_keys.h"
#include "state.h"
#include "state_file.h"
#include "state_port.h"
#include "state_registry.h"
#include "state_reloc.h"
#include "state_rules.h"
#include "trace_host.h"
#include "xxh3.h"
#include "../audio/audio_host.h"
#include "../audio/audio_lock.h"
#include "../audio/mix_gain.h"
#include "../core/region.h"
#include "../data/cdvd_host.h"
#include "../include/ico_credits.h"
#include "../include/ico_gamestate.h"
#include "../input/input_record.h"
#include "../save/mc_host.h"
#include "../ui/settings.h"
#include "achievements.h"
#include "options.h"
#include "video_options.h"

#ifndef ICO_HEADLESS
#include "rd_mesh.h" /* rd_drop_vu_meshes */
#include "rd_png.h"  /* rd_write_png */
#endif

/* The game's (common/src/main.c, layout_texture.c, fumi/ios/ios.c) */
extern int systemStatus[];
extern int stage_no;
extern int current_layout_id;
struct IosMemPart;
extern struct IosMemPart *ios_partition_root;
/* ico2/common/src/PObj.c: the decoded tables of the model being loaded */
void **PObj_GetImageTables(size_t *bytes);
void PObj_SetImageTables(void **p, unsigned int count);
#ifndef ICO_HEADLESS
/* ico2/seki: the scratch stream, the textures' serials, the scene latch */
void pac_HostStateFixup(int freeLive);
unsigned int tex_HostNextSerial(void);
void tex_HostReserialAll(unsigned int liveNext);
void gsb_HostSceneRelatch(void);
#endif
/* port/game/photo_mode.c, port/ui/gallery.c, port/fmv/movie.c */
int ico_photo_active(void);
int gallery_playing(void);
int ico_movie_open(void);

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define STATE_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define STATE_SANITIZED 1
#endif
#endif

/* --- what a state holds besides the memory --------------------------------- */

#define RANGES_MAGIC 0x52414E47u /* "RANG" */

#define RANGE_ARENA_PINNED 1u
#define RANGE_POOL_PINNED 2u
#define RANGE_FIXED_IMAGE 4u

typedef struct StateRanges {
    uint32_t magic, flags;
    uint64_t image_lo, image_hi, text_lo, text_hi;
    uint64_t arena_lo, arena_hi;
    uint64_t pool_lo, pool_hi, slot_bytes;
    uint64_t gdata_lo, gdata_hi, gbss_lo, gbss_hi;
    uint64_t pobj_lo, pobj_bytes;
    uint32_t mesh_gen_high, reserved;
} StateRanges;

/* one fiber in the fibers section, followed by its control block and its
   stack bytes, each padded to 16 */
typedef struct FiberRec {
    uint64_t slot_base;
    uint32_t slot, ctrl_bytes, stack_off, stack_bytes;
} FiberRec;

enum {
    SEC_ARENA,
    SEC_GDATA,
    SEC_GBSS,
    SEC_REG,
    SEC_AUDIO,
    SEC_SCHED,
    SEC_FIBERS,
    SEC_POBJ,
    SEC_RANGES,
    SEC_COUNT
};

static const char *const sec_names[SEC_COUNT] = {"arena", "gdata",  "gbss", "reg",   "audio",
                                                 "sched", "fibers", "pobj", "ranges"};

/* A copy of the memory a state holds: the save's (written by the writer
   thread), the load's staging (inflated from the file) and its backup (the
   live memory before the load). */
typedef struct Copy {
    void *p[SEC_COUNT];
    uint64_t n[SEC_COUNT];
} Copy;

static void copy_free(Copy *c)
{
    int i;

    for (i = 0; i < SEC_COUNT; i++) {
        free(c->p[i]);
    }
    memset(c, 0, sizeof *c);
}

/* --- start-up ----------------------------------------------------------------- */

static int s_inited;
static int s_on;
static char s_off_why[200] = "not started";
static char s_dir[ICO_PATH_MAX];
static int s_dir_made;
static IcoStateHeader s_now; /* this run's identity; disc, keys and hash refreshed per use */
static IcoMemRange s_gdata[2];
static size_t s_reg_bytes, s_audio_bytes;
static void (*s_done_fn)(int, int, IcoStateResult, const char *, void *);
static void *s_done_user;
static int s_cut_next; /* a load's second camera cut, at the next poll */

static void flush_at_exit(void);

static void turn_off(const char *why)
{
    snprintf(s_off_why, sizeof s_off_why, "%s", why);
    s_on = 0;
}

static void init(void)
{
    char why[160];
    char pref[ICO_PATH_MAX];
    IcoStateImage img;
    int i;

    if (s_inited) {
        return;
    }
    if (ico_arena_base() == NULL) {
        /* before ico_host_init: asked again later */
        s_on = 0;
        snprintf(s_off_why, sizeof s_off_why, "not started");
        return;
    }
    s_inited = 1;
    s_on = 1;
    s_off_why[0] = '\0';
#ifdef STATE_SANITIZED
    turn_off("a sanitizer build");
#endif
    if (s_on && strcmp(ico_fiber_backend(), "asm") != 0) {
        snprintf(why, sizeof why, "the fiber backend is %s", ico_fiber_backend());
        turn_off(why);
    }
    if (s_on && ico_state_register_port() != 0) {
        turn_off("the port's blocks are not all registered");
    }
    if (s_on && ico_game_data_ranges_verify(why, sizeof why) != 0) {
        fprintf(stderr,
                "state: the game's data ranges are not bracketed on this build (%s): save states "
                "are off\n",
                why);
        turn_off("the game's data ranges are not bracketed");
    }
    if (s_on && ico_game_data_ranges(s_gdata) != 0) {
        turn_off("the game's data ranges are not bracketed");
    }
    if (s_on && ico_state_image_range(&img) != 0) {
        turn_off("the program's image is unknown");
    }
    if (s_on && ico_fiber_pool_base() == NULL) {
        turn_off("the fiber pool is missing");
    }
    if (s_on && (ico_host_pref_dir(pref, sizeof pref) != 0 ||
                 snprintf(s_dir, sizeof s_dir, "%s/states/%s", pref, ico_region_cur()->slug) >=
                     (int)sizeof s_dir)) {
        s_dir[0] = '\0';
        turn_off("no folder for the states");
    }
    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if ((e->flags & ICO_STATE_AUDIO) != 0) {
            s_audio_bytes += e->n;
        } else {
            s_reg_bytes += e->n;
        }
    }
    ico_state_header_init(&s_now, ICO_BUILD_COMMIT);
    if (!s_on) {
        fprintf(stderr, "state: save states are off: %s\n", s_off_why);
        return;
    }
    ico_host_at_shutdown(flush_at_exit);
    fprintf(stderr,
            "state: save states on: %d blocks (%llu bytes, %llu of them audio), build id "
            "%02x%02x%02x%02x%02x%02x%02x%02x, %s, arena %s, fiber pool %s, folder %s\n",
            ico_state_registry_count(), (unsigned long long)(s_reg_bytes + s_audio_bytes),
            (unsigned long long)s_audio_bytes, s_now.build_id[0], s_now.build_id[1],
            s_now.build_id[2], s_now.build_id[3], s_now.build_id[4], s_now.build_id[5],
            s_now.build_id[6], s_now.build_id[7],
#if defined(ICO_STATE_FIXED_IMAGE) && ICO_STATE_FIXED_IMAGE
            "fixed image",
#else
            "relocatable image",
#endif
            ico_arena_pinned() ? "pinned" : "not pinned",
            ico_fiber_pool_pinned() ? "pinned" : "not pinned", s_dir);
}

int ico_state_available(void)
{
    init();
    return s_on;
}

const char *ico_state_off_reason(void)
{
    init();
    return s_on ? "" : s_off_why;
}

const char *ico_state_dir(void)
{
    init();
    if (s_dir[0] != '\0' && !s_dir_made) {
        char states[ICO_PATH_MAX];
        const char *slash = strrchr(s_dir, '/');

        if (slash != NULL && (size_t)(slash - s_dir) < sizeof states) {
            memcpy(states, s_dir, (size_t)(slash - s_dir));
            states[slash - s_dir] = '\0';
            (void)ico_make_dir(states);
        }
        s_dir_made = ico_make_dir(s_dir) == 0;
    }
    return s_dir;
}

/* this run's header: the identity, the disc and the simulation keys now */
static const IcoStateHeader *now_header(void)
{
    snprintf(s_now.disc, sizeof s_now.disc, "%s", ico_region_cur()->slug);
    s_now.settings_hash = ico_sim_keys_hash(s_now.keys, sizeof s_now.keys);
    return &s_now;
}

void ico_state_set_done_callback(IcoStateDoneFn fn, void *user)
{
    s_done_fn = fn;
    s_done_user = user;
}

static const char *const s_action_names[3] = {"save", "load", "delete"};

static void done(int action, int slot, IcoStateResult r, const char *detail)
{
    if (r != ICO_STATE_OK) {
        fprintf(stderr, "state: refused %s slot %d: %s%s%s\n", s_action_names[action], slot,
                ico_state_result_text(r), detail[0] != '\0' ? ": " : "", detail);
    }
    if (s_done_fn != NULL) {
        s_done_fn(action, slot, r, detail, s_done_user);
    }
}

/* --- the conditions ----------------------------------------------------------- */

static void sample(IcoStateConds *c)
{
    const IcoRegionInfo *ri = ico_region_cur();

    c->available = s_on;
    c->layout = current_layout_id;
    c->layout_pause = ri->layout_pause;
    c->layout_count = ri->layout_count;
    c->stage = stage_no;
    c->stage_title = ri->stage_title;
    c->paused = systemStatus[5];
    c->stage_switch = systemStatus[6];
    c->card_busy = ico_mc_host_pending();
    c->disc_idle = ico_cdvd_host_idle();
    c->movie = ico_movie_open();
    c->photo = ico_photo_active();
    c->credits = ico_credits_active();
    c->gallery = gallery_playing() >= 0;
}

int ico_state_can_save_here(void)
{
    IcoStateConds c;

    init();
    sample(&c);
    return ico_state_on_pause_menu(&c);
}

/* --- copying the memory ------------------------------------------------------ */

static void *dup_bytes(const void *p, size_t n)
{
    void *q = malloc(n ? n : 1);

    if (q != NULL && n != 0) {
        memcpy(q, p, n);
    }
    return q;
}

/* the registered blocks with (audio 1) or without the AUDIO flag, back to
   back */
static void *pack_reg(int audio, uint64_t *bytes)
{
    const size_t total = audio ? s_audio_bytes : s_reg_bytes;
    unsigned char *out = (unsigned char *)malloc(total ? total : 1);
    size_t at = 0;
    int i;

    *bytes = total;
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if (((e->flags & ICO_STATE_AUDIO) != 0) == (audio != 0)) {
            memcpy(out + at, e->p, e->n);
            at += e->n;
        }
    }
    return out;
}

static void unpack_reg(int audio, const unsigned char *in)
{
    size_t at = 0;
    int i;

    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if (((e->flags & ICO_STATE_AUDIO) != 0) == (audio != 0)) {
            memcpy(e->p, in + at, e->n);
            at += e->n;
        }
    }
}

#define PAD16(n) (((n) + 15u) & ~(uint64_t)15u)

/* the scheduler's tables and every fiber, copied (the images point into
   live memory, fiber.h) */
static int copy_sched(Copy *c)
{
    static IcoSchedImage img;
    uint64_t total = 16;
    unsigned char *blob;
    int i;

    if (ico_sched_state_save(&img) != 0) {
        return -1;
    }
    for (i = 0; i < img.n_fibers; i++) {
        total +=
            sizeof(FiberRec) + PAD16(img.fibers[i].ctrl_bytes) + PAD16(img.fibers[i].stack_bytes);
    }
    c->p[SEC_SCHED] = dup_bytes(img.tables, img.tables_bytes);
    c->n[SEC_SCHED] = img.tables_bytes;
    blob = (unsigned char *)calloc(1, (size_t)total);
    c->p[SEC_FIBERS] = blob;
    c->n[SEC_FIBERS] = total;
    if (c->p[SEC_SCHED] == NULL || blob == NULL) {
        return -1;
    }
    memcpy(blob, &img.n_fibers, sizeof img.n_fibers);
    blob += 16;
    for (i = 0; i < img.n_fibers; i++) {
        const IcoFiberImage *f = &img.fibers[i];
        FiberRec r;

        r.slot_base = f->slot_base;
        r.slot = f->slot;
        r.ctrl_bytes = f->ctrl_bytes;
        r.stack_off = f->stack_off;
        r.stack_bytes = f->stack_bytes;
        memcpy(blob, &r, sizeof r);
        blob += sizeof r;
        memcpy(blob, f->ctrl, f->ctrl_bytes);
        blob += PAD16(f->ctrl_bytes);
        memcpy(blob, f->stack, f->stack_bytes);
        blob += PAD16(f->stack_bytes);
    }
    return 0;
}

/* a fibers section into an image whose pointers are into it; -1 when the
   section does not hold what it says */
static int parse_fibers(const Copy *c, IcoSchedImage *img)
{
    const unsigned char *p = (const unsigned char *)c->p[SEC_FIBERS];
    const unsigned char *end = p + c->n[SEC_FIBERS];
    int n, i;

    img->tables = c->p[SEC_SCHED];
    img->tables_bytes = (size_t)c->n[SEC_SCHED];
    img->n_fibers = 0;
    if (p == NULL || c->n[SEC_FIBERS] < 16) {
        return -1;
    }
    memcpy(&n, p, sizeof n);
    if (n < 0 || n > ICO_SCHED_MAX_THREADS) {
        return -1;
    }
    p += 16;
    for (i = 0; i < n; i++) {
        IcoFiberImage *f = &img->fibers[i];
        FiberRec r;

        if ((size_t)(end - p) < sizeof r) {
            return -1;
        }
        memcpy(&r, p, sizeof r);
        p += sizeof r;
        if ((uint64_t)(end - p) < PAD16(r.ctrl_bytes) ||
            (uint64_t)(end - p) - PAD16(r.ctrl_bytes) < PAD16(r.stack_bytes)) {
            return -1;
        }
        f->slot_base = r.slot_base;
        f->slot = r.slot;
        f->ctrl_bytes = r.ctrl_bytes;
        f->stack_off = r.stack_off;
        f->stack_bytes = r.stack_bytes;
        f->ctrl = p;
        p += PAD16(r.ctrl_bytes);
        f->stack = p;
        p += PAD16(r.stack_bytes);
    }
    img->n_fibers = n;
    return 0;
}

static void fill_ranges(StateRanges *r, const void *pobj, size_t pobj_bytes)
{
    IcoStateImage img;
    const uint64_t pool = (uint64_t)(uintptr_t)ico_fiber_pool_base();

    memset(r, 0, sizeof *r);
    r->magic = RANGES_MAGIC;
    (void)ico_state_image_range(&img);
    r->image_lo = img.lo;
    r->image_hi = img.hi;
    r->text_lo = img.text_lo;
    r->text_hi = img.text_hi;
    r->arena_lo = (uint64_t)(uintptr_t)ico_arena_base();
    r->arena_hi = r->arena_lo + ico_arena_size();
    r->slot_bytes = ico_fiber_pool_slot_bytes();
    r->pool_lo = pool;
    r->pool_hi = pool + (uint64_t)ICO_FIBER_POOL_SLOTS * r->slot_bytes;
    r->gdata_lo = s_gdata[ICO_GAME_DATA].lo;
    r->gdata_hi = s_gdata[ICO_GAME_DATA].hi;
    r->gbss_lo = s_gdata[ICO_GAME_BSS].lo;
    r->gbss_hi = s_gdata[ICO_GAME_BSS].hi;
    r->pobj_lo = (uint64_t)(uintptr_t)pobj;
    r->pobj_bytes = pobj != NULL ? pobj_bytes : 0;
    r->flags = (ico_arena_pinned() ? RANGE_ARENA_PINNED : 0u) |
               (ico_fiber_pool_pinned() ? RANGE_POOL_PINNED : 0u);
#if defined(ICO_STATE_FIXED_IMAGE) && ICO_STATE_FIXED_IMAGE
    r->flags |= RANGE_FIXED_IMAGE;
#endif
#ifndef ICO_HEADLESS
    r->mesh_gen_high = rd_vu_mesh_gen_high();
#endif
}

/* Every section of a state from the live memory (the audio blocks under
   the audio lock).  0, or -1 (c freed) when memory ran out or the
   scheduler is not at its quiescent point. */
static int copy_live(Copy *c)
{
    size_t pobj_bytes = 0;
    void **pobj = PObj_GetImageTables(&pobj_bytes);
    StateRanges *rg;
    int i;

    memset(c, 0, sizeof *c);
    ico_audio_lock();
    c->p[SEC_AUDIO] = pack_reg(1, &c->n[SEC_AUDIO]);
    ico_audio_unlock();
    c->p[SEC_REG] = pack_reg(0, &c->n[SEC_REG]);
    c->n[SEC_ARENA] = ico_arena_size();
    c->p[SEC_ARENA] = dup_bytes(ico_arena_base(), (size_t)c->n[SEC_ARENA]);
    c->n[SEC_GDATA] = s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo;
    c->p[SEC_GDATA] = dup_bytes((const void *)s_gdata[ICO_GAME_DATA].lo, (size_t)c->n[SEC_GDATA]);
    c->n[SEC_GBSS] = s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo;
    c->p[SEC_GBSS] = dup_bytes((const void *)s_gdata[ICO_GAME_BSS].lo, (size_t)c->n[SEC_GBSS]);
    c->n[SEC_POBJ] = pobj != NULL ? pobj_bytes : 0;
    c->p[SEC_POBJ] = dup_bytes(pobj, (size_t)c->n[SEC_POBJ]);
    rg = (StateRanges *)malloc(sizeof *rg);
    c->p[SEC_RANGES] = rg;
    c->n[SEC_RANGES] = sizeof *rg;
    if (rg != NULL) {
        fill_ranges(rg, pobj, pobj_bytes);
    }
    if (copy_sched(c) != 0) {
        copy_free(c);
        return -1;
    }
    for (i = 0; i < SEC_COUNT; i++) {
        if (c->p[i] == NULL) {
            copy_free(c);
            return -1;
        }
    }
    return 0;
}

/* --- saving ------------------------------------------------------------------- */

static struct {
    int slot; /* 0: none in flight */
    Copy copy;
    double copy_ms;
    int fibers;
    char path[ICO_PATH_MAX];
} s_save;

static struct {
    int slot;      /* waiting for a picture (0: none) */
    int polls;     /* polls since the save */
    IcoThread *th; /* the PNG's writer */
    int done;      /* set by the writer (__atomic) */
    int ok;
    int w, h;
    unsigned char *px;
    char path[ICO_PATH_MAX];
} s_thumb;

static unsigned s_list_gen; /* a save, a load or a delete since the listing */

static void save_finished(const IcoStateWriteResult *w)
{
    char sizes[640];
    size_t at = 0;
    int i;

    sizes[0] = '\0';
    for (i = 0; i < w->n_sections; i++) {
        const int n = snprintf(sizes + at, sizeof sizes - at, "%s%s %llu -> %llu", i ? ", " : "",
                               w->sections[i].name, (unsigned long long)w->sections[i].raw_bytes,
                               (unsigned long long)w->sections[i].packed_bytes);
        if (n < 0 || (size_t)n >= sizeof sizes - at) {
            break;
        }
        at += (size_t)n;
    }
    if (w->result == ICO_STATE_OK) {
        fprintf(stderr,
                "state: saved slot %d (%s): %s; %d fibers; copy %.1f ms, write %.1f ms, file %llu "
                "bytes\n",
                s_save.slot, s_save.path, sizes, s_save.fibers, s_save.copy_ms, w->write_ms,
                (unsigned long long)w->file_bytes);
        done(0, s_save.slot, ICO_STATE_OK, "");
    } else {
        done(0, s_save.slot, w->result, w->why);
    }
    copy_free(&s_save.copy);
    s_save.slot = 0;
    s_list_gen++;
}

/* a write in flight finished: its result logged and reported */
static void collect_save(int wait)
{
    IcoStateWriteResult w;

    if (s_save.slot == 0) {
        return;
    }
    if (wait ? ico_state_file_flush(&w) : ico_state_file_poll(&w)) {
        save_finished(&w);
    }
}

static void flush_at_exit(void)
{
    collect_save(1);
    if (s_thumb.th != NULL) {
        ico_thread_join(s_thumb.th);
        s_thumb.th = NULL;
    }
}

static void perform_save(int slot)
{
    IcoStateConds c;
    IcoStateSection secs[SEC_COUNT];
    IcoStateHeader *h = &s_now;
    const char *what = "";
    IcoStateResult r;
    double t0;
    int i;

    sample(&c);
    r = ico_state_conditions(&c, 1, &what);
    if (r == ICO_STATE_OK && (s_save.slot != 0 || ico_state_file_busy() || s_thumb.th != NULL)) {
        r = ICO_STATE_BUSY;
    }
    if (r == ICO_STATE_OK &&
        ico_state_slot_path(s_save.path, sizeof s_save.path, ico_state_dir(), slot, ".ics") != 0) {
        r = ICO_STATE_IO;
        what = "the state's path does not fit";
    }
    if (r != ICO_STATE_OK) {
        done(0, slot, r, what);
        return;
    }
    t0 = ico_thread_now_ms();
    if (copy_live(&s_save.copy) != 0) {
        done(0, slot, ICO_STATE_NOT_PAUSED, "the scheduler is not at rest or memory ran out");
        return;
    }
    s_save.copy_ms = ico_thread_now_ms() - t0;
    memcpy(&s_save.fibers, s_save.copy.p[SEC_FIBERS], sizeof s_save.fibers);
    now_header();
    h->saved_at = (int64_t)time(NULL);
    h->play_frames = ico_gs_play_frames();
    h->stage_no = stage_no;
    h->main_tick = ico_host_main_ticks();
    h->vsync = ico_host_vsync_count();
    for (i = 0; i < SEC_COUNT; i++) {
        secs[i].name = sec_names[i];
        secs[i].data = s_save.copy.p[i];
        secs[i].bytes = s_save.copy.n[i];
        secs[i].addr = 0;
        secs[i].flags = 0;
    }
    secs[SEC_ARENA].addr = (uint64_t)(uintptr_t)ico_arena_base();
    secs[SEC_GDATA].addr = s_gdata[ICO_GAME_DATA].lo;
    secs[SEC_GBSS].addr = s_gdata[ICO_GAME_BSS].lo;
    r = ico_state_file_write(s_save.path, h, secs, SEC_COUNT);
    if (r != ICO_STATE_OK) {
        copy_free(&s_save.copy);
        done(0, slot, r, "the writer could not start");
        return;
    }
    s_save.slot = slot;
#ifndef ICO_HEADLESS
    /* the picture: the next presented frame (the window asks
       ico_state_wants_thumbnail after this poll's step) */
    s_thumb.slot = slot;
    s_thumb.polls = 0;
#endif
}

/* --- the thumbnail ------------------------------------------------------------ */

int ico_state_wants_thumbnail(void)
{
    return s_thumb.slot > 0 && s_thumb.polls >= 1 && s_thumb.th == NULL;
}

#ifndef ICO_HEADLESS
static void thumb_write(void *arg)
{
    char tmp[ICO_PATH_MAX + 8];
    int ok;

    (void)arg;
    snprintf(tmp, sizeof tmp, "%s.tmp", s_thumb.path);
    ok = rd_write_png(tmp, s_thumb.px, (uint32_t)s_thumb.w, (uint32_t)s_thumb.h,
                      (uint32_t)s_thumb.w * 4u, 0) &&
         ico_rename_replace(tmp, s_thumb.path) == 0;
    if (!ok) {
        (void)ico_remove(tmp);
    }
    s_thumb.ok = ok;
    __atomic_store_n(&s_thumb.done, 1, __ATOMIC_RELEASE);
}
#endif

void ico_state_thumbnail(const unsigned char *rgba, int w, int h)
{
#ifndef ICO_HEADLESS
    int dw, dh;

    if (!ico_state_wants_thumbnail() || rgba == NULL || w < 1 || h < 1) {
        return;
    }
    if (ico_state_slot_path(s_thumb.path, sizeof s_thumb.path, ico_state_dir(), s_thumb.slot,
                            ".png") != 0) {
        s_thumb.slot = 0;
        return;
    }
    ico_state_thumb_size(w, h, &dw, &dh);
    s_thumb.px = (unsigned char *)malloc((size_t)dw * (size_t)dh * 4);
    if (s_thumb.px == NULL) {
        s_thumb.slot = 0;
        return;
    }
    ico_state_thumb_scale(rgba, w, h, s_thumb.px, dw, dh);
    s_thumb.w = dw;
    s_thumb.h = dh;
    s_thumb.done = 0;
    s_thumb.ok = 0;
    rd_png_prepare();
    s_thumb.th = ico_thread_create(thumb_write, NULL);
    if (s_thumb.th == NULL) {
        thumb_write(NULL);
    }
#else
    (void)rgba;
    (void)w;
    (void)h;
#endif
}

static void thumb_poll(void)
{
    if (s_thumb.slot == 0) {
        return;
    }
    if (s_thumb.th == NULL && s_thumb.px == NULL) {
        /* about two seconds without a present (a minimized window): none */
        if (++s_thumb.polls > 120) {
            fprintf(stderr, "state: slot %d has no picture (no frame was presented)\n",
                    s_thumb.slot);
            s_thumb.slot = 0;
        }
        return;
    }
    if (s_thumb.th != NULL && !__atomic_load_n(&s_thumb.done, __ATOMIC_ACQUIRE)) {
        return;
    }
    if (s_thumb.th != NULL) {
        ico_thread_join(s_thumb.th);
        s_thumb.th = NULL;
    }
    if (s_thumb.ok) {
        fprintf(stderr, "state: slot %d's picture written (%s, %dx%d)\n", s_thumb.slot,
                s_thumb.path, s_thumb.w, s_thumb.h);
    } else {
        fprintf(stderr, "state: slot %d's picture could not be written (%s)\n", s_thumb.slot,
                s_thumb.path);
    }
    free(s_thumb.px);
    s_thumb.px = NULL;
    s_thumb.slot = 0;
    s_list_gen++;
}

/* --- loading ------------------------------------------------------------------ */

typedef struct LoadCtx {
    Copy *c;
    int bad;
    char why[160];
} LoadCtx;

static void *load_section(void *user, const IcoStateSectionInfo *s)
{
    LoadCtx *l = (LoadCtx *)user;
    int i;

    for (i = 0; i < SEC_COUNT; i++) {
        if (strcmp(s->name, sec_names[i]) == 0 && l->c->p[i] == NULL) {
            l->c->p[i] = malloc(s->raw_bytes ? (size_t)s->raw_bytes : 1);
            l->c->n[i] = s->raw_bytes;
            if (l->c->p[i] == NULL) {
                l->bad = 1;
                snprintf(l->why, sizeof l->why, "no memory for the %s section (%llu bytes)",
                         s->name, (unsigned long long)s->raw_bytes);
            }
            return l->c->p[i];
        }
    }
    return NULL;
}

/* the saved sections against this build's sizes; 0 or -1 with why */
static int check_sizes(const Copy *c, const StateRanges *rg, char *why, size_t n)
{
    const uint64_t want[SEC_COUNT] = {ico_arena_size(),
                                      s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo,
                                      s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo,
                                      s_reg_bytes,
                                      s_audio_bytes,
                                      0,
                                      0,
                                      rg != NULL ? rg->pobj_bytes : 0,
                                      sizeof(StateRanges)};
    const void *sched;
    size_t sched_n;
    int i;

    ico_sched_state_block(&sched, &sched_n);
    (void)sched;
    for (i = 0; i < SEC_COUNT; i++) {
        uint64_t w = i == SEC_SCHED ? (uint64_t)sched_n : want[i];

        if (c->p[i] == NULL) {
            snprintf(why, n, "the %s section is missing", sec_names[i]);
            return -1;
        }
        if (i != SEC_FIBERS && c->n[i] != w) {
            snprintf(why, n, "the %s section is %llu bytes, this build's %llu", sec_names[i],
                     (unsigned long long)c->n[i], (unsigned long long)w);
            return -1;
        }
    }
    if (rg->magic != RANGES_MAGIC || rg->slot_bytes != ico_fiber_pool_slot_bytes()) {
        snprintf(why, n, "the ranges do not fit this build (a fiber slot of %llu bytes, here %llu)",
                 (unsigned long long)rg->slot_bytes,
                 (unsigned long long)ico_fiber_pool_slot_bytes());
        return -1;
    }
    return 0;
}

static void set_range(IcoStateReloc *r, int which, uint64_t lo, uint64_t hi, uint64_t now)
{
    r->range[which].lo = lo;
    r->range[which].hi = hi;
    r->range[which].delta = (int64_t)(now - lo);
}

/* the relocation from the saving process's ranges to this one's (pobj_now:
   the load's copy of the saved pobj block) */
static void build_reloc(IcoStateReloc *r, const StateRanges *rg, const void *pobj_now)
{
    IcoStateImage img;

    memset(r, 0, sizeof *r);
    (void)ico_state_image_range(&img);
    set_range(r, ICO_RELOC_MODULE, rg->image_lo, rg->image_hi, img.lo);
    set_range(r, ICO_RELOC_ARENA, rg->arena_lo, rg->arena_hi,
              (uint64_t)(uintptr_t)ico_arena_base());
    set_range(r, ICO_RELOC_POOL, rg->pool_lo, rg->pool_hi,
              (uint64_t)(uintptr_t)ico_fiber_pool_base());
    if (rg->pobj_bytes != 0 && pobj_now != NULL) {
        set_range(r, ICO_RELOC_POBJ, rg->pobj_lo, rg->pobj_lo + rg->pobj_bytes,
                  (uint64_t)(uintptr_t)pobj_now);
    }
}

/* this process's own ranges, nothing moved (the rollback) */
static void identity_reloc(IcoStateReloc *r)
{
    StateRanges rg;
    int i;

    fill_ranges(&rg, NULL, 0);
    build_reloc(r, &rg, NULL);
    for (i = 0; i < ICO_RELOC_RANGES; i++) {
        r->range[i].delta = 0;
    }
}

/* the memory from a copy (staging or backup): the game data, the arena and
   the registered blocks; the scheduler is put back by the caller */
static void put_memory(const Copy *c)
{
    memcpy((void *)s_gdata[ICO_GAME_DATA].lo, c->p[SEC_GDATA], (size_t)c->n[SEC_GDATA]);
    memcpy((void *)s_gdata[ICO_GAME_BSS].lo, c->p[SEC_GBSS], (size_t)c->n[SEC_GBSS]);
    memcpy(ico_arena_base(), c->p[SEC_ARENA], (size_t)c->n[SEC_ARENA]);
    unpack_reg(0, (const unsigned char *)c->p[SEC_REG]);
    unpack_reg(1, (const unsigned char *)c->p[SEC_AUDIO]);
}

/* the conservative scan over every restored region but the scheduler's
   (ico_sched_state_restore moved its own); counts per region */
typedef struct ScanCounts {
    uint64_t arena, gdata, gbss, reg, pobj;
} ScanCounts;

static void scan_all(const IcoStateReloc *r, void *pobj, size_t pobj_bytes, ScanCounts *k)
{
    int i;

    memset(k, 0, sizeof *k);
    ico_state_reloc_scan(r, ico_arena_base(), ico_arena_size(), &k->arena);
    ico_state_reloc_scan(r, (void *)s_gdata[ICO_GAME_DATA].lo,
                         s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo, &k->gdata);
    ico_state_reloc_scan(r, (void *)s_gdata[ICO_GAME_BSS].lo,
                         s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo, &k->gbss);
    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if ((e->flags & ICO_STATE_SCAN) != 0) {
            ico_state_reloc_scan(r, e->p, e->n, &k->reg);
        }
    }
    if (pobj != NULL) {
        ico_state_reloc_scan(r, pobj, pobj_bytes, &k->pobj);
    }
}

static int any_moved(const IcoStateReloc *r)
{
    int i;

    for (i = 0; i < ICO_RELOC_RANGES; i++) {
        if (r->range[i].delta != 0 && r->range[i].hi > r->range[i].lo) {
            return 1;
        }
    }
    return 0;
}

static const char *delta_word(int which, const StateRanges *rg)
{
    switch (which) {
    case ICO_RELOC_MODULE:
        return (rg->flags & RANGE_FIXED_IMAGE) != 0 ? " (fixed image)" : "";
    case ICO_RELOC_ARENA:
        return (rg->flags & RANGE_ARENA_PINNED) != 0 && ico_arena_pinned() ? " (pinned)" : "";
    case ICO_RELOC_POOL:
        return (rg->flags & RANGE_POOL_PINNED) != 0 && ico_fiber_pool_pinned() ? " (pinned)" : "";
    }
    return "";
}

/* the failed load's way back: every region from the backup, the scheduler
   with nothing moved; the game goes on as before the load.  Called with
   the audio lock held; a way back that fails lets the lock go before the
   fatal end (review finding 9: ico_host_fatal's box and exit close the
   audio device, which waits for the callback blocked on that lock), which
   never returns */
static void roll_back(const Copy *backup, void *pobj_new)
{
    static IcoSchedImage img;
    IcoStateReloc id;

    put_memory(backup);
    free(pobj_new);
#ifndef ICO_HEADLESS
    pac_HostStateFixup(0);
#endif
    identity_reloc(&id);
    if (parse_fibers(backup, &img) != 0 || ico_sched_state_restore(&img, &id) != 0) {
        ico_audio_unlock();
        ico_host_fatal(NULL, "state: a failed load could not put the game back; it cannot go on");
    }
}

static void perform_load(int slot)
{
    static IcoStateHeader saved;
    static IcoSchedImage img;
    IcoStateConds c;
    Copy stage, backup;
    LoadCtx lc;
    IcoStateReloc rel;
    IcoStateCheck chk;
    ScanCounts k;
    char path[ICO_PATH_MAX];
    char detail[256];
    const char *what = "";
    const StateRanges *rg;
    size_t live_pobj_bytes = 0;
    void **live_pobj;
    void *pobj_new = NULL;
    unsigned int live_tex = 0;
    IcoStateResult r;
    double t0;
    int moved;
    int i;

    (void)live_tex;
    detail[0] = '\0';
    /* a save in flight first (it may be this slot's) */
    collect_save(1);
    sample(&c);
    r = ico_state_conditions(&c, 0, &what);
    if (r != ICO_STATE_OK) {
        done(1, slot, r, what);
        return;
    }
    if (ico_state_slot_path(path, sizeof path, ico_state_dir(), slot, ".ics") != 0) {
        done(1, slot, ICO_STATE_EMPTY, "");
        return;
    }
    r = ico_state_file_read_header(path, &saved);
    if (r == ICO_STATE_OK) {
        r = ico_state_header_check(&saved, now_header(), detail, sizeof detail);
    }
    if (r != ICO_STATE_OK) {
        done(1, slot, r, detail);
        return;
    }
    t0 = ico_thread_now_ms();
    memset(&stage, 0, sizeof stage);
    memset(&backup, 0, sizeof backup);
    memset(&lc, 0, sizeof lc);
    lc.c = &stage;
    r = ico_state_file_read(path, NULL, load_section, &lc);
    rg = stage.n[SEC_RANGES] == sizeof(StateRanges) ? (const StateRanges *)stage.p[SEC_RANGES]
                                                    : NULL;
    if (r == ICO_STATE_OK && lc.bad) {
        r = ICO_STATE_BAD_FILE;
        snprintf(detail, sizeof detail, "%s", lc.why);
    } else if (r == ICO_STATE_OK &&
               (rg == NULL || check_sizes(&stage, rg, detail, sizeof detail) != 0 ||
                parse_fibers(&stage, &img) != 0)) {
        r = ICO_STATE_BAD_FILE;
        if (detail[0] == '\0') {
            snprintf(detail, sizeof detail, "the fibers section does not hold what it says");
        }
    }
    if (r == ICO_STATE_OK && rg->pobj_bytes != 0) {
        pobj_new = dup_bytes(stage.p[SEC_POBJ], (size_t)rg->pobj_bytes);
        if (pobj_new == NULL) {
            r = ICO_STATE_BAD_FILE;
            snprintf(detail, sizeof detail, "no memory for the model tables");
        }
    }
    if (r != ICO_STATE_OK) {
        copy_free(&stage);
        free(pobj_new);
        done(1, slot, r, detail);
        return;
    }
    build_reloc(&rel, rg, pobj_new);
    moved = any_moved(&rel);
    ico_state_reloc_set_log(ico_opt_developer_mode());
    s_thumb.slot = s_thumb.th == NULL ? 0 : s_thumb.slot; /* no picture of the loaded game */

    ico_audio_lock();
#ifndef ICO_HEADLESS
    live_tex = tex_HostNextSerial();
    pac_HostStateFixup(1);
#endif
    live_pobj = PObj_GetImageTables(&live_pobj_bytes);
    /* the backup: the live memory, the scheduler's tables and fibers copied
       (its images point into the live slots, which the restore discards) */
    backup.p[SEC_REG] = pack_reg(0, &backup.n[SEC_REG]);
    backup.p[SEC_AUDIO] = pack_reg(1, &backup.n[SEC_AUDIO]);
    backup.n[SEC_ARENA] = ico_arena_size();
    backup.p[SEC_ARENA] = dup_bytes(ico_arena_base(), (size_t)backup.n[SEC_ARENA]);
    backup.n[SEC_GDATA] = stage.n[SEC_GDATA];
    backup.p[SEC_GDATA] =
        dup_bytes((const void *)s_gdata[ICO_GAME_DATA].lo, (size_t)backup.n[SEC_GDATA]);
    backup.n[SEC_GBSS] = stage.n[SEC_GBSS];
    backup.p[SEC_GBSS] =
        dup_bytes((const void *)s_gdata[ICO_GAME_BSS].lo, (size_t)backup.n[SEC_GBSS]);
    if (copy_sched(&backup) != 0 || backup.p[SEC_REG] == NULL || backup.p[SEC_AUDIO] == NULL ||
        backup.p[SEC_ARENA] == NULL || backup.p[SEC_GDATA] == NULL || backup.p[SEC_GBSS] == NULL) {
        ico_audio_unlock();
        copy_free(&stage);
        copy_free(&backup);
        free(pobj_new);
        done(1, slot, ICO_STATE_BUSY, "no memory for the backup, or the scheduler is not at rest");
        return;
    }

    /* the restore */
    put_memory(&stage);
    if (ico_sched_state_restore(&img, &rel) != 0) {
        roll_back(&backup, pobj_new);
        ico_audio_unlock();
        copy_free(&stage);
        copy_free(&backup);
        done(1, slot, ICO_STATE_SELF_CHECK,
             "the threads could not be put back (the backup was restored)");
        return;
    }
    memset(&k, 0, sizeof k);
    if (moved) {
        scan_all(&rel, pobj_new, pobj_new != NULL ? (size_t)rg->pobj_bytes : 0, &k);
    }
#ifndef ICO_HEADLESS
    pac_HostStateFixup(0);
#endif
    PObj_SetImageTables((void **)pobj_new,
                        pobj_new != NULL ? (unsigned int)(rg->pobj_bytes / sizeof(void *)) : 0u);
    memset(&chk, 0, sizeof chk);
    chk.heap_root = ios_partition_root;
    chk.arena_lo = (uint64_t)(uintptr_t)ico_arena_base();
    chk.arena_hi = chk.arena_lo + ico_arena_size();
    (void)ico_state_image_range(&chk.image);
    if (ico_state_reloc_check(&chk) != 0) {
        roll_back(&backup, pobj_new);
        ico_audio_unlock();
        copy_free(&stage);
        copy_free(&backup);
        snprintf(detail, sizeof detail, "%s; the game went on as before (backup restored)",
                 chk.why);
        done(1, slot, ICO_STATE_SELF_CHECK, detail);
        return;
    }
    /* committed: the old model tables go, the renderer's handles follow */
    free(live_pobj);
#ifndef ICO_HEADLESS
    tex_HostReserialAll(live_tex);
    rd_drop_vu_meshes(rg->mesh_gen_high);
    gsb_HostSceneRelatch();
#endif
    ico_audio_unlock();
    ico_audio_host_flush();
    (void)ico_audio_gain_reapply();
    /* the cut: the presenter does not blend the old picture into the
       restored one (now and at the next poll, when the first restored frame
       is the one being recorded) */
    ico_video_camera_cut();
    s_cut_next = 1;
    ico_gs_rebaseline();
    ico_ach_host_state_loaded(ico_host_main_ticks());
    ico_input_record_state_loaded(slot, ico_host_main_ticks());
    ico_fpenv_sim_enter();

    {
        char deltas[320];
        size_t at = 0;
        static const char *const names[ICO_RELOC_RANGES] = {"module", "arena", "pool", "pobj"};

        deltas[0] = '\0';
        for (i = 0; i < ICO_RELOC_RANGES; i++) {
            const int n = snprintf(
                deltas + at, sizeof deltas - at, "%s%s %s0x%llx%s", i ? ", " : "", names[i],
                rel.range[i].delta < 0 ? "-" : "+",
                (unsigned long long)(rel.range[i].delta < 0 ? -(uint64_t)rel.range[i].delta
                                                            : (uint64_t)rel.range[i].delta),
                i == ICO_RELOC_POBJ && rg->pobj_bytes == 0 ? " (none)" : delta_word(i, rg));
            if (n < 0 || (size_t)n >= sizeof deltas - at) {
                break;
            }
            at += (size_t)n;
        }
        fprintf(stderr,
                "state: loaded slot %d (saved at Main tick %u, stage %d): deltas %s; words "
                "rewritten %s arena %llu, gdata %llu, gbss %llu, registry %llu, pobj %llu; "
                "checks %u partitions, %u blocks, %u threads, %u handlers; %d fibers; apply "
                "%.1f ms\n",
                slot, saved.main_tick, saved.stage_no, deltas, moved ? "" : "(nothing moved)",
                (unsigned long long)k.arena, (unsigned long long)k.gdata,
                (unsigned long long)k.gbss, (unsigned long long)k.reg, (unsigned long long)k.pobj,
                chk.partitions, chk.nodes, chk.threads, chk.handlers, img.n_fibers,
                ico_thread_now_ms() - t0);
    }
    copy_free(&stage);
    copy_free(&backup);
    s_list_gen++;
    ui_settings_sync_after_state_load();
    done(1, slot, ICO_STATE_OK, "");
}

static void perform_delete(int slot)
{
    char path[ICO_PATH_MAX];
    int had = 0;

    collect_save(1);
    if (!s_on) {
        done(2, slot, ICO_STATE_OFF, s_off_why);
        return;
    }
    if (ico_state_slot_path(path, sizeof path, ico_state_dir(), slot, ".ics") == 0) {
        had = ico_remove(path) == 0;
    }
    if (ico_state_slot_path(path, sizeof path, ico_state_dir(), slot, ".png") == 0) {
        (void)ico_remove(path);
    }
    s_list_gen++;
    if (!had) {
        done(2, slot, ICO_STATE_EMPTY, "");
        return;
    }
    fprintf(stderr, "state: deleted slot %d\n", slot);
    done(2, slot, ICO_STATE_OK, "");
}

/* --- the listing --------------------------------------------------------------- */

void ico_state_list(IcoStateSlotInfo out[ICO_STATE_SLOTS])
{
    static IcoStateSlotInfo cache[ICO_STATE_SLOTS];
    static unsigned cache_gen = ~0u;
    static uint64_t cache_keys;
    const IcoStateHeader *now;

    init();
    if (!s_on) {
        memset(out, 0, sizeof(IcoStateSlotInfo) * ICO_STATE_SLOTS);
        return;
    }
    now = now_header();
    if (cache_gen != s_list_gen || cache_keys != now->settings_hash) {
        ico_state_list_dir(ico_state_dir(), now, cache);
        cache_gen = s_list_gen;
        cache_keys = now->settings_hash;
    }
    memcpy(out, cache, sizeof cache);
}

/* --- requests and the poll ----------------------------------------------------- */

/* the latched request: (action + 1) << 8 | slot, 0 for none */
static int s_req;

static void request(int action, int slot)
{
    if (slot < 1 || slot > ICO_STATE_SLOTS) {
        return;
    }
    __atomic_store_n(&s_req, ((action + 1) << 8) | slot, __ATOMIC_RELEASE);
}

void ico_state_request_save(int slot)
{
    request(0, slot);
}

void ico_state_request_load(int slot)
{
    request(1, slot);
}

void ico_state_request_delete(int slot)
{
    request(2, slot);
}

static struct {
    unsigned int save_tick, load_tick, hash_tick;
    int save_slot, load_slot;
    int save_done, load_done;
    unsigned int last_tick;
} s_dev;

void ico_state_dev_set(unsigned int save_tick, int save_slot, unsigned int load_tick, int load_slot,
                       unsigned int hash_tick)
{
    s_dev.save_tick = save_tick;
    s_dev.save_slot = save_slot;
    s_dev.load_tick = load_tick;
    s_dev.load_slot = load_slot;
    s_dev.hash_tick = hash_tick;
    s_dev.save_done = s_dev.load_done = 0;
    s_dev.last_tick = 0;
    if (save_tick != 0 || load_tick != 0 || hash_tick != 0) {
        fprintf(stderr,
                "state: [dev] save at Main tick %u slot %d, load at %u slot %d, hash at %u (0: "
                "none)\n",
                save_tick, save_slot, load_tick, load_slot, hash_tick);
    }
}

static void dev_hash(unsigned int tick)
{
    uint64_t h[3];

    h[0] = xxh3_64(ico_arena_base(), ico_arena_size());
    h[1] = xxh3_64((const void *)s_gdata[ICO_GAME_DATA].lo,
                   s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo);
    h[2] = xxh3_64((const void *)s_gdata[ICO_GAME_BSS].lo,
                   s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo);
    fprintf(stderr, "state: hash %016llx of arena+gdata+gbss at tick %u\n",
            (unsigned long long)xxh3_64(h, sizeof h), tick);
}

/* the [dev] keys: the hash each time the tick reaches its value (a load
   brings the tick back below it), the save and the load once */
static void dev_poll(void)
{
    const unsigned int tick = ico_host_main_ticks();
    const unsigned int last = s_dev.last_tick;

    s_dev.last_tick = tick;
    if (s_dev.hash_tick != 0 && tick >= s_dev.hash_tick &&
        (last < s_dev.hash_tick || last > tick) && s_on) {
        dev_hash(tick);
    }
    if (s_dev.save_tick != 0 && !s_dev.save_done && tick >= s_dev.save_tick) {
        s_dev.save_done = 1;
        perform_save(s_dev.save_slot);
        collect_save(1); /* the load that may follow reads the file */
    }
    if (s_dev.load_tick != 0 && !s_dev.load_done && tick >= s_dev.load_tick) {
        s_dev.load_done = 1;
        perform_load(s_dev.load_slot);
        s_dev.last_tick = ico_host_main_ticks();
    }
}

void ico_state_host_poll(void)
{
    int req;

    init();
    if (s_cut_next) {
        ico_video_camera_cut();
        s_cut_next = 0;
    }
    collect_save(0);
    thumb_poll();
    if (s_dev.save_tick != 0 || s_dev.load_tick != 0 || s_dev.hash_tick != 0) {
        dev_poll();
    }
    req = __atomic_exchange_n(&s_req, 0, __ATOMIC_ACQ_REL);
    if (req == 0) {
        return;
    }
    switch ((req >> 8) - 1) {
    case 0:
        perform_save(req & 0xFF);
        break;
    case 1:
        perform_load(req & 0xFF);
        break;
    default:
        perform_delete(req & 0xFF);
        break;
    }
}
