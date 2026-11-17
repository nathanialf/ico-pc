/*
 * port/game/state_host.c
 *
 * Save states (port/platform/state.h): the save and the load at the host
 * loop's quiescent point, the slots' files, the thumbnail (the pause
 * picture, else the frame after the save) and the [dev] keys.  Compiled into the program (ico_pc), both builds; the renderer's
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
 *           and pobj block, its mesh generations, and the game thread's
 *           thread pointer and stack guard (StateRanges below)
 *   syms    what every word that points into the program points into, and
 *           the registry's blocks by name (state_xbuild.h), so a later build
 *           whose code and data those words need are unchanged can load it
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arena.h"
#include "code_hash.h"
#include "diag_host.h"
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
#include "state_hash.h"
#include "state_port.h"
#include "state_registry.h"
#include "state_reloc.h"
#include "state_rules.h"
#include "state_syms.h"
#include "state_xbuild.h"
#include "trace_host.h"
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif
#include "../audio/audio_host.h"
#include "../audio/audio_lock.h"
#include "../audio/mix_gain.h"
#include "../core/region.h"
#include "../data/cdvd_host.h"
#include "../include/ico_credits.h"
#include "../include/ico_unused.h"
#include "../include/ico_gamestate.h"
#include "../input/input_record.h"
#include "../save/mc_host.h"
#include "../save/state_checkpoint.h"
#include "../save/state_verdicts.h"
#include "../ui/settings.h"
#include "achievements.h"
#include "difficulty.h"
#include "options.h"
#include "state_checkpoint_live.h"
#include "video_options.h"

#ifndef ICO_HEADLESS
#include "rd.h"        /* rd_capture_shot, rd_present_box_size */
#include "rd_mesh.h"   /* rd_drop_vu_meshes, rd_vu_mesh_each_field */
#include "rd_thread.h" /* rd_thread_drain: a load with the render thread idle */
#include "rd_png.h"    /* rd_write_png */
#endif

/* The game's (common/src/main.c, layout_texture.c, fumi/ios/ios.c) */
extern int systemStatus[];
extern int stage_no;
extern int current_layout_id;
struct IosMemPart;
extern struct IosMemPart *ios_partition_root;
/* port/data/reserved/tables.c: the loader's room in the game's data sections */
const void *ico_tables_room(void);
/* ico2/common/src/PObj.c: the decoded tables of the model being loaded */
void **PObj_GetImageTables(size_t *bytes);
void PObj_SetImageTables(void **p, unsigned int count);
void *PObj_HostStateWord(size_t *bytes);
#ifndef ICO_HEADLESS
/* ico2/seki: the scratch stream, the textures' serials, the scene latch,
   and the host's words in the game's data the hash leaves out */
void pac_HostStateFixup(int freeLive);
unsigned int tex_HostNextSerial(void);
void tex_HostReserialAll(unsigned int liveNext);
void gsb_HostSceneRelatch(void);
void pac_HostStateWords(void (*fn)(const void *p, size_t n, int ptr, void *user), void *user);
void *tex_HostStateBlock(size_t *bytes);
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

/* "RAN2": the layout with the thread pointer and the stack guard ("RANG"
   had neither) */
#define RANGES_MAGIC 0x52414E32u

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
    /* the game thread's (state_reloc.h "The saving process's own values"):
       the fibers' frames hold them, and a later process has others */
    uint64_t tls_base, stack_guard;
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
    SEC_SYMS,
    /* where the game's checkpoint globals are (state_checkpoint.h): a later
       version continues the state from its checkpoint by it */
    SEC_CKPT,
    SEC_COUNT
};

static const char *const sec_names[SEC_COUNT] = {
    "arena", "gdata", "gbss", "reg", "audio", "sched", "fibers", "pobj", "ranges", "syms", "ckpt"};

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

/* --- the host blocks the hash leaves out ---------------------------------------- */

/* Every model tables block (PObj.c) this process has seen: the game's
   memory points into them (a model's part records), so those words are
   host addresses a second run of the same play holds differently
   (state_hash.h, the catch-all).  Noted at each poll and at a load; a block
   that comes and goes between two polls is not seen (a [dev] state_dump_at
   dump names its words then). */
static IcoStateHashBlocks s_host_blocks;

static void note_host_blocks(void)
{
    size_t bytes = 0;
    void **p = PObj_GetImageTables(&bytes);

    ico_state_hash_note_block(&s_host_blocks, (uint64_t)(uintptr_t)p, p != NULL ? bytes : 0);
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

/* the start line's verdict on one range (ico_state_reloc_ambiguous) */
static const char *scan_words(int fixed, uint64_t at)
{
    if (fixed) {
        return "fixed or pinned: the same address in a later run";
    }
    if ((at >> 32) >= ICO_STATE_SCAN_HIGH_MIN) {
        return "moved by the scan when elsewhere";
    }
    return "below the bound: when elsewhere, a load continues from the checkpoint";
}

/* what a state of this platform records of the saving process besides its
   ranges (state_reloc.h "The saving process's own values"), for the log */
static const char *proc_words(unsigned need)
{
    switch (need & (ICO_STATE_PROC_TLS | ICO_STATE_PROC_GUARD)) {
    case ICO_STATE_PROC_TLS | ICO_STATE_PROC_GUARD:
        return "thread pointer recorded, stack guard recorded";
    case ICO_STATE_PROC_TLS:
        return "thread pointer recorded";
    case ICO_STATE_PROC_GUARD:
        return "stack guard recorded";
    }
    return "no thread pointer needed";
}

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
    /* the loader's room in the game's data sections joins the link where
       the loader's statics were (port/data/reserved/tables.c) */
    (void)ico_tables_room();
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
    /* a simulation key a later version added reads as its default in an
       earlier version's state (sim_keys.h) */
    ico_state_set_key_default(ico_sim_key_is_default);
    /* the difficulty rows an earlier version's state lists among its
       simulation keys: its record is built from them (difficulty.h), so
       they never refuse it */
    ico_state_set_key_retired(ico_sim_key_retired);
    if (!s_on) {
        fprintf(stderr, "state: save states are off: %s\n", s_off_why);
        return;
    }
    ico_host_at_shutdown(flush_at_exit);
    fprintf(stderr,
            "state: save states on: %d blocks (%llu bytes, %llu of them audio), build id "
            "%02x%02x%02x%02x%02x%02x%02x%02x, %s, arena %s, fiber pool %s, %s, folder %s\n",
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
            ico_fiber_pool_pinned() ? "pinned" : "not pinned",
            proc_words(ico_state_proc_needs(ico_state_platform_tag())), s_dir);
    {
        /* what a load in a later run can move (ico_state_reloc_ambiguous):
           a range whose addresses' upper half is below the bound continues
           from its checkpoint when it lies elsewhere in that run */
        IcoStateImage img;
        const uint64_t arena = (uint64_t)(uintptr_t)ico_arena_base();
        const uint64_t pool = (uint64_t)(uintptr_t)ico_fiber_pool_base();
        int fixed = 0;

#if defined(ICO_STATE_FIXED_IMAGE) && ICO_STATE_FIXED_IMAGE
        fixed = 1;
#endif
        if (ico_state_image_range(&img) != 0) {
            memset(&img, 0, sizeof img);
        }
        fprintf(stderr,
                "state: a later run moves a state's addresses only where their upper half is at "
                "least 0x%x: the image 0x%llx-0x%llx (upper half 0x%llx: %s), the arena at 0x%llx "
                "(0x%llx: %s), the fiber pool at 0x%llx (0x%llx: %s)\n",
                ICO_STATE_SCAN_HIGH_MIN, (unsigned long long)img.lo, (unsigned long long)img.hi,
                (unsigned long long)(img.lo >> 32), scan_words(fixed, img.lo),
                (unsigned long long)arena, (unsigned long long)(arena >> 32),
                scan_words(ico_arena_pinned(), arena), (unsigned long long)pool,
                (unsigned long long)(pool >> 32), scan_words(ico_fiber_pool_pinned(), pool));
    }
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
    c->unused = ico_unused_active();
    c->gallery = gallery_playing() >= 0;
    c->locked = ico_difficulty_states_locked();
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

/* off NULL: back to back in this build's order; else each block from its
   offset in the saved section (another build's order, state_xbuild.h), a
   FRESH block the state lacks (ICO_XB_BLOCK_ABSENT) left as it is */
static void unpack_reg(int audio, const unsigned char *in, const uint64_t *off)
{
    size_t at = 0;
    int i;

    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if (((e->flags & ICO_STATE_AUDIO) != 0) == (audio != 0)) {
            if (off != NULL && off[i] == ICO_XB_BLOCK_ABSENT) {
                continue;
            }
            memcpy(e->p, in + (off != NULL ? (size_t)off[i] : at), e->n);
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
    {
        /* on the game thread (copy_live runs at the poll, the thread that
           resumes every fiber) */
        IcoStateProc pv;

        ico_state_proc_read(&pv);
        r->tls_base = pv.tls_base;
        r->stack_guard = pv.stack_guard;
    }
}

static size_t ckpt_record(void *out);

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
    c->p[SEC_CKPT] = malloc(ICO_CKPT_RECORD_BYTES);
    c->n[SEC_CKPT] = c->p[SEC_CKPT] != NULL ? ckpt_record(c->p[SEC_CKPT]) : 0;
    if (copy_sched(c) != 0) {
        copy_free(c);
        return -1;
    }
    for (i = 0; i < SEC_COUNT; i++) {
        if (c->p[i] == NULL && i != SEC_SYMS) {
            copy_free(c);
            return -1;
        }
    }
    return 0;
}

/* --- states of other versions (state_xbuild.h) ------------------------------------ */

/* The game data range's words before the game runs: a later build takes a
   saved word that equals one of them to its own word at the same offset
   (state_xbuild.h, points).  A constructor takes them, after the loader
   relocated the initialised pointers and before main runs any game code. */
static uint64_t *s_snap;
static size_t s_snap_words;

#if defined(__GNUC__)
__attribute__((constructor)) static void snap_game_data(void)
{
    IcoMemRange r[2];

    if (ico_game_data_ranges(r) != 0 || r[ICO_GAME_DATA].hi <= r[ICO_GAME_DATA].lo) {
        return;
    }
    s_snap_words = (size_t)(r[ICO_GAME_DATA].hi - r[ICO_GAME_DATA].lo) / 8;
    s_snap = (uint64_t *)malloc(s_snap_words ? s_snap_words * 8 : 1);
    if (s_snap == NULL) {
        s_snap_words = 0;
        return;
    }
    memcpy(s_snap, (const void *)r[ICO_GAME_DATA].lo, s_snap_words * 8);
}
#endif

/* the symbol file read whole: at most this much (a link map is 2-4 MB) */
#define SYMS_FILE_MAX (64ll << 20)

static IcoSymTab *s_tab;
static int s_tab_tried;

/* The symbols lock: the program's table (s_tab, read once) and its units'
   code hashes (computed on first use, state_syms.c) are shared, and so are
   the static work arrays of build_syms, legacy_record and xb_probe; the save
   (the writer's build_syms), a load's check and the Save states listing's
   verdict (another thread may ask) each hold it for their whole use of the
   table (state_xbuild.c and state_legacy.c keep no statics of their own: a
   legacy table is loaded per check and freed). Made before main.
   Lock order: the verdicts' work lock (state_verdicts.h) first, then this
   one, never the other way.  A load's check and the listing's worker reach
   xb_probe through other_build with the work lock held; the save's
   build_syms takes this lock alone, outside the work lock, so the work lock
   does not cover the save and this lock is still needed.  Code holding this
   lock takes no other lock and calls neither the game nor the renderer. */
static IcoMutex *s_xb_lock;

__attribute__((constructor)) static void xb_lock_make(void)
{
    s_xb_lock = ico_mutex_create();
}

static void xb_lock(void)
{
    if (s_xb_lock != NULL) {
        ico_mutex_lock(s_xb_lock);
    }
}

static void xb_unlock(void)
{
    if (s_xb_lock != NULL) {
        ico_mutex_unlock(s_xb_lock);
    }
}

/* The program's symbol table (state_syms.h), read at the first save, load
   or listing that needs it and kept; NULL when there is none (logged once:
   this build's states then load only into this build, and no other
   version's state loads here). */
static IcoSymTab *sym_table(void)
{
    char name[160], why[160];
    IcoSymTabEnv env;
    IcoStateImage img;
    long long len = -1;
    int gz = 0;
    char *raw = NULL, *text = NULL;
    size_t n = 0;
    double t0;
    int i;

    if (s_tab_tried) {
        return s_tab;
    }
    s_tab_tried = 1;
    t0 = ico_thread_now_ms();
    snprintf(name, sizeof name, "the symbol file");
    why[0] = '\0';
    if (!ico_diag_symbols_info(name, sizeof name, &gz, &len)) {
        snprintf(why, sizeof why, "no symbol file beside the program (a build without one)");
    } else if (len <= 0 || len > SYMS_FILE_MAX) {
        snprintf(why, sizeof why, "its size is %lld bytes", len);
    } else if ((raw = (char *)malloc((size_t)len)) == NULL ||
               ico_diag_symbols_read(raw, (size_t)len, 0) != len) {
        snprintf(why, sizeof why, "it could not be read");
    } else if (gz) {
        text = ico_symtab_gunzip(raw, (size_t)len, &n);
        if (text == NULL) {
            snprintf(why, sizeof why, "it does not inflate");
        }
    } else {
        text = raw;
        raw = NULL;
        n = (size_t)len;
    }
    if (text != NULL && ico_state_image_range(&img) != 0) {
        snprintf(why, sizeof why, "the program's image is unknown");
    } else if (text != NULL) {
        memset(&env, 0, sizeof env);
        env.anchor = "ico_diag_init";
        env.anchor_addr = (uint64_t)(uintptr_t)ico_diag_init;
        env.check = "ico_diag_log";
        env.check_addr = (uint64_t)(uintptr_t)ico_diag_log;
        env.image_lo = img.lo;
        env.image_hi = img.hi;
        env.text_lo = img.text_lo;
        env.text_hi = img.text_hi;
        for (i = 0; i < 2; i++) {
            env.game_lo[i] = s_gdata[i].lo;
            env.game_hi[i] = s_gdata[i].hi;
        }
        env.arch = ico_code_arch_native();
        s_tab = ico_symtab_parse(text, n, &env, why, sizeof why);
    }
    free(raw);
    free(text);
    if (s_tab != NULL) {
        const size_t bytes = s_tab->n_unit * (sizeof(IcoSymUnit) + 4) +
                             s_tab->n_data * (sizeof(IcoSymData) + 4) + s_tab->str_n;

        fprintf(stderr,
                "state: symbols: %zu functions, %zu data symbols, %zu game data items (layout "
                "%016llx) from %s in %.1f ms, %zu KB; the code 0x%llx-0x%llx from %s (%zu "
                "sections)\n",
                s_tab->n_unit, s_tab->n_data, s_tab->n_layout,
                (unsigned long long)s_tab->layout_hash, name, ico_thread_now_ms() - t0,
                bytes / 1024, (unsigned long long)s_tab->code_lo,
                (unsigned long long)s_tab->code_hi,
                s_tab->code_from ? "its code sections"
                                 : "the loader's segments (it names no code section)",
                s_tab->n_sec);
    } else {
        fprintf(stderr,
                "state: symbols: none (%s: %s); a state saved here loads only into this build, and "
                "no other build's state loads here\n",
                name, why);
    }
    return s_tab;
}

/* the version a commit text names ("vX.Y.Z", "vX.Y.Z-3-g0123456789"), a
   trailing "-dirty" left out */
static void version_text(const char *commit, char *out, size_t n)
{
    size_t k = strlen(commit);

    if (k >= 6 && strcmp(commit + k - 6, "-dirty") == 0) {
        k -= 6;
    }
    if (k == 0) {
        snprintf(out, n, "unknown");
        return;
    }
    snprintf(out, n, "%.*s", (int)k, commit);
}

static IcoStateRefusal s_refusal;

void ico_state_last_refusal(IcoStateRefusal *out)
{
    *out = s_refusal;
}

static void set_refusal(IcoStateBlocker b, const char *saved_commit)
{
    s_refusal.blocker = b;
    version_text(saved_commit, s_refusal.saved_version, sizeof s_refusal.saved_version);
    version_text(ICO_BUILD_COMMIT, s_refusal.this_version, sizeof s_refusal.this_version);
}

static IcoStateBlocker blocker_of(int kind)
{
    switch (kind) {
    case ICO_XB_CODE:
        return ICO_STATE_BLOCK_CODE;
    case ICO_XB_DATA:
    case ICO_XB_BLOCKS:
        return ICO_STATE_BLOCK_DATA;
    }
    return ICO_STATE_BLOCK_PINNED;
}

/* the words of n copied bytes that lay at `addr`, split at the stack
   pointer's red zone: below it (dead frames) soft, from it up strict */
static void xb_scan_stack(IcoXbRefs *r, const IcoFiberImage *f)
{
    const uint64_t base = f->slot_base + f->stack_off;
    const uint64_t sp = ico_fiber_image_sp(f);
    const uint64_t live = sp >= 128 ? sp - 128 : 0;
    const unsigned char *p = (const unsigned char *)f->stack;

    if (sp == 0 || live <= base) {
        ico_xb_scan(r, p, f->stack_bytes, base, 1);
    } else if (live >= base + f->stack_bytes) {
        ico_xb_scan(r, p, f->stack_bytes, base, 0);
    } else {
        const size_t dead = (size_t)(live - base) & ~(size_t)7;

        ico_xb_scan(r, p, dead, base, 0);
        ico_xb_scan(r, p + dead, f->stack_bytes - dead, base + dead, 1);
    }
}

/* The "syms" section of a save from its copy (state_xbuild.h): every word
   of the copied regions that points into the program (the regions a load
   relocates: the arena, the game's data, the scanned blocks, the model
   tables, the scheduler's tables, the fibers' control blocks and stacks)
   and the registry's blocks.  0, or -1 when memory ran out. */
static int build_syms_locked(Copy *c, IcoXbStats *st)
{
    static IcoSchedImage img;
    IcoXbProc p;
    IcoXbRefs *r;
    const void *sched;
    size_t sched_n, n = 0;
    size_t at[2] = {0, 0};
    void *sec = NULL;
    int i;

    memset(&p, 0, sizeof p);
    p.tab = sym_table();
    (void)ico_state_image_range(&p.image);
    for (i = 0; i < 2; i++) {
        p.game_lo[i] = s_gdata[i].lo;
        p.game_hi[i] = s_gdata[i].hi;
    }
    p.snap = s_snap;
    p.snap_words = s_snap_words;
    r = ico_xb_begin(&p);
    if (r == NULL) {
        return -1;
    }
    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);
        const int audio = (e->flags & ICO_STATE_AUDIO) != 0;
        const unsigned char *copy = (const unsigned char *)c->p[audio ? SEC_AUDIO : SEC_REG];

        ico_xb_block(r, e->name, e->n, e->flags, (uint64_t)(uintptr_t)e->p, at[audio]);
        if ((e->flags & ICO_STATE_SCAN) != 0) {
            char what[48];

            snprintf(what, sizeof what, "the block %s", e->name);
            ico_xb_where(r, what);
            ico_xb_scan(r, copy + at[audio], e->n, (uint64_t)(uintptr_t)e->p, 1);
        }
        at[audio] += e->n;
    }
    ico_xb_where(r, "the arena");
    ico_xb_scan(r, c->p[SEC_ARENA], (size_t)c->n[SEC_ARENA], (uint64_t)(uintptr_t)ico_arena_base(),
                1);
    ico_xb_where(r, "the game's data");
    ico_xb_scan(r, c->p[SEC_GDATA], (size_t)c->n[SEC_GDATA], s_gdata[ICO_GAME_DATA].lo, 1);
    ico_xb_where(r, "the game's bss");
    ico_xb_scan(r, c->p[SEC_GBSS], (size_t)c->n[SEC_GBSS], s_gdata[ICO_GAME_BSS].lo, 1);
    if (c->n[SEC_POBJ] != 0) {
        size_t pobj_bytes = 0;
        void **pobj = PObj_GetImageTables(&pobj_bytes);

        ico_xb_where(r, "the model tables");
        ico_xb_scan(r, c->p[SEC_POBJ], (size_t)c->n[SEC_POBJ], (uint64_t)(uintptr_t)pobj, 1);
    }
    ico_sched_state_block(&sched, &sched_n);
    ico_xb_where(r, "the scheduler's table");
    ico_xb_scan(r, c->p[SEC_SCHED], (size_t)c->n[SEC_SCHED], (uint64_t)(uintptr_t)sched, 1);
    if (parse_fibers(c, &img) == 0) {
        for (i = 0; i < img.n_fibers; i++) {
            const IcoFiberImage *f = &img.fibers[i];
            char what[48];

            ico_xb_frames(r, (int)f->slot + 1);

            snprintf(what, sizeof what, "fiber slot %u's control page", (unsigned)f->slot);
            ico_xb_where(r, what);
            ico_xb_scan(r, f->ctrl, f->ctrl_bytes, f->slot_base, 1);
            snprintf(what, sizeof what, "fiber slot %u's stack", (unsigned)f->slot);
            ico_xb_where(r, what);
            xb_scan_stack(r, f);
        }
        ico_xb_frames(r, 0);
    }
    if (ico_xb_finish(r, &sec, &n, st) != 0) {
        return -1;
    }
    c->p[SEC_SYMS] = sec;
    c->n[SEC_SYMS] = n;
    return 0;
}

static int build_syms(Copy *c, IcoXbStats *st)
{
    int rc;

    xb_lock();
    rc = build_syms_locked(c, st);
    xb_unlock();
    return rc;
}

/* why a state saved here loads only into this build, for the save's line */
static const char *pinned_why(unsigned flags)
{
    if ((flags & ICO_XB_TABLE) == 0) {
        return "this build has no symbol file";
    }
    if ((flags & ICO_XB_LOW_IMAGE) != 0) {
        return "the program lies below 4 GB, where a program address cannot be told from a number";
    }
    if ((flags & ICO_XB_OVERFLOW) != 0) {
        return "it points into the program in more places than the record holds";
    }
    return "";
}

/* The words a save named nothing (state_xbuild.h ico_xb_where), one line
   each: the save's own list, or the one a refused load read from the
   state. */
static void log_unnamed(int slot, const char *when, const IcoXbUnnamed *list, unsigned listed,
                        unsigned left)
{
    unsigned i;

    for (i = 0; i < listed; i++) {
        const IcoXbUnnamed *u = &list[i];

        fprintf(
            stderr,
            "state: slot %d %s word %u of %u named by nothing: 0x%llx, %s; section %s, %s+0x%llx; "
            "found in %s at 0x%llx\n",
            slot, when, i + 1, left, (unsigned long long)u->word, ico_xb_why_text(u->reason),
            u->section[0] != '\0' ? u->section : "none named",
            u->sym[0] != '\0' ? u->sym : "no symbol", (unsigned long long)u->sym_off,
            u->where[0] != '\0' ? u->where : "a place not recorded", (unsigned long long)u->at);
    }
}

/* the save's own image (state_xbuild.h): above an upper half of 1 a word
   nothing names pins the state; at 0 or 1 it is a number */
static int unnamed_pins(const IcoXbStats *x)
{
    IcoStateImage img;

    return (x->flags & ICO_XB_UNNAMED) != 0 && ico_state_image_range(&img) == 0 &&
           (img.lo >> 32) > 1u;
}

/* --- saving ------------------------------------------------------------------- */

static struct {
    int slot; /* 0: none in flight */
    Copy copy;
    double copy_ms, syms_ms;
    int fibers;
    IcoXbStats syms;
    char path[ICO_PATH_MAX];
} s_save;

static struct {
    int slot;      /* waiting for a picture (0: none) */
    int polls;     /* polls since the save */
    IcoThread *th; /* the PNG's writer, or the pause picture's copy */
    int done;      /* set by the writer (__atomic) */
    int ok;
    int copy; /* the picture is the pause picture's copy (no frame waited for) */
    int w, h;
    unsigned char *px;
    char path[ICO_PATH_MAX];
    char from[ICO_PATH_MAX]; /* the copy's source (the pause picture) */
} s_thumb;

/* The pause picture (state_rules.h ico_state_shot_step): the frame before
   the pause menu's dark backdrop, taken from the present by rd_capture_shot
   when the menu opens from play, written to <dir>/pause.png on its own
   thread and copied to slot<n>.png by a save made before the game goes
   back to play.  A load puts the loaded slot's own picture there
   (shot_from_slot), so a save in the same pause keeps the game as loaded.
   Without one a save takes the frame after it (the DISPLAY frame, menu and
   all: ico_state_thumbnail). */
#ifndef ICO_HEADLESS
enum { SHOT_NONE, SHOT_ARMED, SHOT_WRITING, SHOT_READY };

static struct {
    int state;
    int last;      /* the layout at the last poll (ico_state_shot_step) */
    int stale;     /* dropped while being written: none once written */
    IcoThread *th; /* the writer */
    int done;      /* set by the writer (__atomic) */
    int ok;
    unsigned char *px; /* the box as taken (w x h), freed after the write */
    int w, h, dw, dh;
    double ms; /* the writer's time */
    unsigned taken;
    int from_slot; /* n: the writer copies slot<n>.png, the loaded state's (0: a frame) */
    char path[ICO_PATH_MAX];
    char why[96]; /* why there is none, for the save's line */
} s_shot = {.last = -1};

static void shot_to_slot(int slot);
static void shot_join(void);
static void shot_forget(const char *why);
#endif
static void shot_from_slot(int slot);

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
        const IcoXbStats *x = &s_save.syms;
        const char *pin = pinned_why(x->flags);

        if (pin[0] == '\0' && unnamed_pins(x)) {
            pin = "it holds a program address no symbol names";
        }

        fprintf(stderr,
                "state: saved slot %d (%s): %s; %d fibers; copy %.1f ms, write %.1f ms, file %llu "
                "bytes\n",
                s_save.slot, s_save.path, sizes, s_save.fibers, s_save.copy_ms, w->write_ms,
                (unsigned long long)w->file_bytes);
        if (pin[0] != '\0') {
            fprintf(stderr,
                    "state: slot %d loads only into this build: %s (%u program words, %u "
                    "blocks; %u naming nothing, first 0x%llx)\n",
                    s_save.slot, pin, x->values, x->blocks, x->left,
                    (unsigned long long)x->first_unnamed);
            if ((x->flags & ICO_XB_UNNAMED) != 0) {
                log_unnamed(s_save.slot, "saved:", x->list, x->listed, x->left);
            }
        } else {
            fprintf(stderr,
                    "state: slot %d can carry over to a later version that keeps %u functions "
                    "(%u with a thread inside), %u data symbols (%u of a repeated name, by their "
                    "contents), %u start-up values and texts "
                    "(%u of them read-only data found by its bytes, %u texts), %u blocks and the "
                    "game's data layout (%u program words, %u of them naming nothing and kept "
                    "as numbers, first 0x%llx; %u page words of the threads, %u ADRPs making "
                    "them; code from %s; record %zu bytes in %.1f ms)\n",
                    s_save.slot, x->units, x->positions, x->data, x->data_masked, x->points,
                    x->windows, x->texts, x->blocks, x->values, x->left,
                    (unsigned long long)x->first_unnamed, x->pages, x->producers,
                    x->code_from ? "the symbol file's sections" : "the loader's segments", x->bytes,
                    s_save.syms_ms);
        }
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

static void vd_join(void);

static void flush_at_exit(void)
{
    collect_save(1);
    vd_join();
    if (s_thumb.th != NULL) {
        ico_thread_join(s_thumb.th);
        s_thumb.th = NULL;
    }
#ifndef ICO_HEADLESS
    shot_join();
#endif
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
    /* one at a time, the picture included: a second save while the
       first's picture is still to come (the next present, or its thread)
       is refused as busy, so the first slot keeps its picture (review
       finding 11) */
    if (r == ICO_STATE_OK &&
        (s_save.slot != 0 || ico_state_file_busy() || s_thumb.th != NULL || s_thumb.slot != 0)) {
        r = ICO_STATE_BUSY;
        what = "the last save's picture is still being taken";
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
    t0 = ico_thread_now_ms();
    if (build_syms(&s_save.copy, &s_save.syms) != 0) {
        copy_free(&s_save.copy);
        done(0, slot, ICO_STATE_IO, "memory ran out for the symbols record");
        return;
    }
    s_save.syms_ms = ico_thread_now_ms() - t0;
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
    /* the picture: the pause picture's copy, else the next presented frame
       (the window asks ico_state_wants_thumbnail after this poll's step) */
    s_thumb.slot = slot;
    s_thumb.polls = 0;
    shot_to_slot(slot);
#endif
}

/* --- the thumbnail ------------------------------------------------------------ */

int ico_state_wants_thumbnail(void)
{
    return s_thumb.slot > 0 && s_thumb.polls >= 1 && s_thumb.th == NULL && !s_thumb.copy;
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
    uint32_t bw = 0, bh = 0;
    int dw, dh;

    if (!ico_state_wants_thumbnail() || rgba == NULL || w < 1 || h < 1) {
        return;
    }
    if (ico_state_slot_path(s_thumb.path, sizeof s_thumb.path, ico_state_dir(), s_thumb.slot,
                            ".png") != 0) {
        s_thumb.slot = 0;
        return;
    }
    /* DISPLAY has half the lines it is shown with (and a wide frame's
       columns): the picture's shape is the presentation box's */
    ico_state_thumb_size(w, h, rd_present_box_size(&bw, &bh) ? (double)bw / (double)bh : 0.0, &dw,
                         &dh);
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
    if (s_thumb.th == NULL && s_thumb.px == NULL && !s_thumb.copy) {
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
        fprintf(stderr, "state: slot %d's picture written (%s, %dx%d%s)\n", s_thumb.slot,
                s_thumb.path, s_thumb.w, s_thumb.h,
                s_thumb.copy ? ", the pause picture" : ", the frame after the save");
    } else {
        fprintf(stderr, "state: slot %d's picture could not be written (%s)\n", s_thumb.slot,
                s_thumb.path);
    }
    free(s_thumb.px);
    s_thumb.px = NULL;
    s_thumb.slot = 0;
    s_thumb.copy = 0;
    s_list_gen++;
}

/* --- the pause picture ---------------------------------------------------------- */

#ifndef ICO_HEADLESS
/* the pause picture's line: the first 8, then every 100th */
static int shot_log_now(void)
{
    s_shot.taken++;
    return s_shot.taken <= 8 || s_shot.taken % 100 == 0;
}

static void shot_write(void *arg)
{
    char tmp[ICO_PATH_MAX + 8];
    unsigned char *small;
    const double t0 = ico_thread_now_ms();
    int ok = 0;

    (void)arg;
    small = (unsigned char *)malloc((size_t)s_shot.dw * (size_t)s_shot.dh * 4);
    if (small != NULL) {
        ico_state_thumb_scale(s_shot.px, s_shot.w, s_shot.h, small, s_shot.dw, s_shot.dh);
        snprintf(tmp, sizeof tmp, "%s.tmp", s_shot.path);
        ok = rd_write_png(tmp, small, (uint32_t)s_shot.dw, (uint32_t)s_shot.dh,
                          (uint32_t)s_shot.dw * 4u, 0) &&
             ico_rename_replace(tmp, s_shot.path) == 0;
        if (!ok) {
            (void)ico_remove(tmp);
        }
        free(small);
    }
    s_shot.ms = ico_thread_now_ms() - t0;
    s_shot.ok = ok;
    __atomic_store_n(&s_shot.done, 1, __ATOMIC_RELEASE);
}

/* rd_capture_shot_arm's answer, after the present that took it (or dropped
   it), taken here on the game thread (shot_poll) */
static void shot_taken(uint8_t *rgba, uint32_t w, uint32_t h, void *user)
{
    (void)user;
    if (s_shot.state != SHOT_ARMED || rgba == NULL) {
        free(rgba);
        if (s_shot.state == SHOT_ARMED) {
            s_shot.state = SHOT_NONE;
            snprintf(s_shot.why, sizeof s_shot.why,
                     "the frame before the pause menu was not presented");
        }
        return;
    }
    s_shot.px = rgba;
    s_shot.w = (int)w;
    s_shot.h = (int)h;
    s_shot.from_slot = 0;
    ico_state_thumb_size(s_shot.w, s_shot.h, 0.0, &s_shot.dw, &s_shot.dh);
    s_shot.done = 0;
    s_shot.ok = 0;
    s_shot.stale = 0;
    s_shot.state = SHOT_WRITING;
    rd_png_prepare();
    s_shot.th = ico_thread_create(shot_write, NULL);
    if (s_shot.th == NULL) {
        shot_write(NULL);
    }
}

static void shot_join(void)
{
    if (s_shot.th != NULL) {
        ico_thread_join(s_shot.th);
        s_shot.th = NULL;
    }
}

/* once a host step: the writer collected, then the game's layout */
static void shot_poll(void)
{
    const IcoRegionInfo *rg = ico_region_cur();
    uint8_t *rgba = NULL;
    uint32_t rw = 0, rh = 0;

    /* the present's answer, deferred: the window's render thread presents
       and hands nothing to this file's state itself */
    if (rd_capture_shot_take(&rgba, &rw, &rh)) {
        shot_taken(rgba, rw, rh, NULL);
    }

    if (s_shot.state == SHOT_WRITING && __atomic_load_n(&s_shot.done, __ATOMIC_ACQUIRE)) {
        shot_join();
        free(s_shot.px);
        s_shot.px = NULL;
        if (!s_shot.ok && s_shot.from_slot > 0) {
            s_shot.state = SHOT_NONE;
            snprintf(s_shot.why, sizeof s_shot.why, "the loaded slot %d has no picture to keep",
                     s_shot.from_slot);
            fprintf(stderr,
                    "state: the pause picture: slot %d's picture could not be copied (none, or "
                    "unreadable): a save before going back to play takes the frame after it\n",
                    s_shot.from_slot);
        } else if (!s_shot.ok) {
            s_shot.state = SHOT_NONE;
            snprintf(s_shot.why, sizeof s_shot.why, "the pause picture could not be written");
            fprintf(stderr, "state: the pause picture could not be written (%s)\n", s_shot.path);
        } else if (s_shot.stale) {
            s_shot.state = SHOT_NONE;
        } else if (s_shot.from_slot > 0) {
            s_shot.state = SHOT_READY;
            fprintf(stderr,
                    "state: the pause picture: slot %d's own, the game as loaded, %dx%d, copied in "
                    "%.1f ms (%s)\n",
                    s_shot.from_slot, s_shot.dw, s_shot.dh, s_shot.ms, s_shot.path);
        } else {
            s_shot.state = SHOT_READY;
            if (shot_log_now()) {
                fprintf(stderr,
                        "state: the pause picture: the game before the pause menu, %dx%d "
                        "from the %dx%d picture, written in %.1f ms (%s; picture %u)\n",
                        s_shot.dw, s_shot.dh, s_shot.w, s_shot.h, s_shot.ms, s_shot.path,
                        s_shot.taken);
            }
        }
    }
    switch (
        ico_state_shot_step(&s_shot.last, current_layout_id, rg->layout_play, rg->layout_pause)) {
    case ICO_STATE_SHOT_TAKE:
        if (!s_on) {
            break;
        }
        if (s_shot.state == SHOT_WRITING) {
            /* the last one still being written (a pause, play, a pause
               within its write): this pause has none */
            s_shot.stale = 1;
            snprintf(s_shot.why, sizeof s_shot.why,
                     "the last pause picture was still being written");
            break;
        }
        rd_capture_shot_cancel();
        if (ico_state_pause_path(s_shot.path, sizeof s_shot.path, ico_state_dir()) != 0) {
            s_shot.state = SHOT_NONE;
            snprintf(s_shot.why, sizeof s_shot.why, "the pause picture's path does not fit");
        } else if (rd_capture_shot_arm()) {
            s_shot.state = SHOT_ARMED;
        } else {
            s_shot.state = SHOT_NONE;
            snprintf(s_shot.why, sizeof s_shot.why, "no renderer for the pause picture");
        }
        break;
    case ICO_STATE_SHOT_DROP:
        if (s_shot.state != SHOT_NONE) {
            shot_forget("the game went back to play since the pause menu opened");
        }
        break;
    case ICO_STATE_SHOT_KEEP:
        break;
    }
}

static void thumb_copy(void *arg)
{
    (void)arg;
    s_thumb.ok = ico_state_copy_file(s_thumb.from, s_thumb.path) == 0;
    __atomic_store_n(&s_thumb.done, 1, __ATOMIC_RELEASE);
}

/* a save's picture: the pause picture copied to slot<n>.png, or (none)
   the frame after the save, as the line says */
static void shot_to_slot(int slot)
{
    if (s_shot.state != SHOT_READY || ico_state_slot_path(s_thumb.path, sizeof s_thumb.path,
                                                          ico_state_dir(), slot, ".png") != 0) {
        fprintf(stderr,
                "state: slot %d's picture: the frame after the save (no pause picture: %s)\n", slot,
                s_shot.state == SHOT_ARMED     ? "the frame before the pause menu is still to come"
                : s_shot.state == SHOT_WRITING ? "the pause picture is still being written"
                : s_shot.why[0] != '\0'        ? s_shot.why
                                               : "none was taken since the program started");
        return;
    }
    snprintf(s_thumb.from, sizeof s_thumb.from, "%s", s_shot.path);
    s_thumb.copy = 1;
    s_thumb.w = s_shot.dw;
    s_thumb.h = s_shot.dh;
    s_thumb.done = 0;
    s_thumb.ok = 0;
    s_thumb.th = ico_thread_create(thumb_copy, NULL);
    if (s_thumb.th == NULL) {
        thumb_copy(NULL);
        s_thumb.th = NULL;
        thumb_poll(); /* collected now: no thread to wait for */
    }
}

/* the writer's copy of the loaded slot's picture to pause.png */
static void shot_copy(void *arg)
{
    const double t0 = ico_thread_now_ms();

    (void)arg;
    s_shot.ok =
        ico_state_picture_to_pause(ico_state_dir(), s_shot.from_slot, &s_shot.dw, &s_shot.dh) == 0;
    s_shot.ms = ico_thread_now_ms() - t0;
    __atomic_store_n(&s_shot.done, 1, __ATOMIC_RELEASE);
}

/* after a load: the pause picture before it showed the game as it was, so
   it goes; the loaded slot's own picture takes its place (copied on the
   writer), so the next save in the same pause keeps the game as loaded */
static void shot_from_slot(int slot)
{
    if (s_shot.state == SHOT_ARMED) {
        rd_capture_shot_cancel();
    }
    if (s_shot.state == SHOT_WRITING) {
        /* the last pause's still being written: it is the game before the
           load, so it is waited for and dropped */
        shot_join();
        free(s_shot.px);
        s_shot.px = NULL;
    }
    s_shot.state = SHOT_NONE;
    snprintf(s_shot.why, sizeof s_shot.why, "a state was loaded since the pause menu opened");
    if (ico_state_pause_path(s_shot.path, sizeof s_shot.path, ico_state_dir()) != 0) {
        return;
    }
    s_shot.from_slot = slot;
    s_shot.dw = s_shot.dh = 0;
    s_shot.done = 0;
    s_shot.ok = 0;
    s_shot.stale = 0;
    s_shot.state = SHOT_WRITING;
    s_shot.th = ico_thread_create(shot_copy, NULL);
    if (s_shot.th == NULL) {
        shot_copy(NULL);
    }
}

/* no pause picture any more (the game played on, a state was loaded): why
   for the next save's line */
static void shot_forget(const char *why)
{
    if (s_shot.state == SHOT_ARMED) {
        rd_capture_shot_cancel();
    }
    if (s_shot.state == SHOT_WRITING) {
        s_shot.stale = 1;
    } else {
        s_shot.state = SHOT_NONE;
    }
    snprintf(s_shot.why, sizeof s_shot.why, "%s", why);
}
#else
static void shot_poll(void) {}

static void shot_from_slot(int slot)
{
    (void)slot;
}
#endif

/* --- loading ------------------------------------------------------------------ */

typedef struct LoadCtx {
    Copy *c;
    unsigned want; /* the sections to read (1 << SEC_*), 0 every one */
    int xb;        /* another build's state: its reg and audio sections lack the FRESH blocks
                      it was saved without, so their sizes are bounds here (check_sizes does
                      the exact sum from the translation's offsets) */
    int bad;
    char why[160];
} LoadCtx;

/* Section i's size in this build (*exact 1), or for the two whose size
   the state decides, its bound (*exact 0): the fibers' every slot of the
   pool with its record, the model tables at most two of the arena's bytes
   (a pointer for each 4-byte EE word of a model image that lives in it) */
static uint64_t section_size(int i, int *exact)
{
    const void *sched;
    size_t sched_n;

    *exact = 1;
    switch (i) {
    case SEC_ARENA:
        return ico_arena_size();
    case SEC_GDATA:
        return s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo;
    case SEC_GBSS:
        return s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo;
    case SEC_REG:
        return s_reg_bytes;
    case SEC_AUDIO:
        return s_audio_bytes;
    case SEC_SCHED:
        ico_sched_state_block(&sched, &sched_n);
        (void)sched;
        return (uint64_t)sched_n;
    case SEC_FIBERS:
        *exact = 0;
        return 16 + (uint64_t)ICO_FIBER_POOL_SLOTS *
                        (sizeof(FiberRec) + PAD16((uint64_t)ico_fiber_pool_slot_bytes()) + 16);
    case SEC_POBJ:
        *exact = 0;
        return 2 * (uint64_t)ico_arena_size();
    case SEC_SYMS:
        *exact = 0;
        return ICO_XB_MAX_BYTES;
    case SEC_CKPT:
        *exact = 0;
        return 4096;
    default:
        return sizeof(StateRanges);
    }
}

static void *load_section(void *user, const IcoStateSectionInfo *s)
{
    LoadCtx *l = (LoadCtx *)user;
    int i;

    for (i = 0; i < SEC_COUNT; i++) {
        if (strcmp(s->name, sec_names[i]) == 0 && l->c->p[i] == NULL &&
            (l->want == 0 || (l->want & (1u << i)) != 0)) {
            int exact;
            const uint64_t want = section_size(i, &exact);

            if (l->xb && (i == SEC_REG || i == SEC_AUDIO)) {
                exact = 0;
            }

            /* review finding 10: the size the file claims against this
               build's before anything is allocated for it */
            if (exact ? s->raw_bytes != want : s->raw_bytes > want) {
                l->bad = 1;
                snprintf(l->why, sizeof l->why, "the %s section is %llu bytes, this build's %s%llu",
                         s->name, (unsigned long long)s->raw_bytes, exact ? "" : "at most ",
                         (unsigned long long)want);
                return NULL;
            }
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
/* block_off: another build's (state_xbuild.h): the reg and audio sections
   hold every block but the FRESH ones the state lacks, each inside its
   section; NULL: this build's, every block */
static int check_sizes(const Copy *c, const StateRanges *rg, const uint64_t *block_off, char *why,
                       size_t n)
{
    uint64_t want[SEC_COUNT] = {ico_arena_size(),
                                s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo,
                                s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo,
                                s_reg_bytes,
                                s_audio_bytes,
                                0,
                                0,
                                rg != NULL ? rg->pobj_bytes : 0,
                                sizeof(StateRanges),
                                0,
                                0};
    const void *sched;
    size_t sched_n;
    int i;

    for (i = 0; block_off != NULL && i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if (block_off[i] == ICO_XB_BLOCK_ABSENT) {
            want[(e->flags & ICO_STATE_AUDIO) != 0 ? SEC_AUDIO : SEC_REG] -= e->n;
        }
    }

    ico_sched_state_block(&sched, &sched_n);
    (void)sched;
    for (i = 0; i < SEC_COUNT; i++) {
        uint64_t w = i == SEC_SCHED ? (uint64_t)sched_n : want[i];

        if (i == SEC_CKPT) {
            continue; /* a state from before v0.11.9 has none; the exact load needs none */
        }
        if (c->p[i] == NULL) {
            snprintf(why, n, "the %s section is missing", sec_names[i]);
            return -1;
        }
        if (i != SEC_FIBERS && i != SEC_SYMS && c->n[i] != w) {
            snprintf(why, n, "the %s section is %llu bytes, this build's %llu", sec_names[i],
                     (unsigned long long)c->n[i], (unsigned long long)w);
            return -1;
        }
    }
    for (i = 0; block_off != NULL && i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);
        const int sec = (e->flags & ICO_STATE_AUDIO) != 0 ? SEC_AUDIO : SEC_REG;

        if (block_off[i] != ICO_XB_BLOCK_ABSENT &&
            (block_off[i] > c->n[sec] || c->n[sec] - block_off[i] < e->n)) {
            snprintf(why, n, "the block %s lies outside the %s section", e->name, sec_names[sec]);
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

/* --- states of earlier versions: legacy tables (state_legacy.h) ------------- */

/* A packaged file of the legacy tables: Android's APK assets under
   state_legacy/ (android/app/build.gradle copies port/state_legacy).  The
   other platforms ship none: their earlier states were named whole. */
static int legacy_read(void *user, const char *path, void **data, size_t *n, char *err,
                       size_t err_n)
{
#if defined(__ANDROID__)
    char name[200];
    SDL_IOStream *io;
    Sint64 size;
    void *b;

    (void)user;
    *data = NULL;
    *n = 0;
    snprintf(name, sizeof name, "state_legacy/%s", path);
    io = SDL_IOFromFile(name, "rb");
    if (io == NULL) {
        snprintf(err, err_n, "the APK's asset %s does not open: %s", name, SDL_GetError());
        return -1;
    }
    size = SDL_GetIOSize(io);
    if (size <= 0 || size > (64ll << 20)) {
        snprintf(err, err_n, "the asset's size is %lld bytes (%s)", (long long)size,
                 size < 0 ? SDL_GetError() : "outside 1 byte to 64 MB");
        SDL_CloseIO(io);
        return -1;
    }
    b = malloc((size_t)size);
    if (b == NULL) {
        snprintf(err, err_n, "no memory for its %lld bytes", (long long)size);
        SDL_CloseIO(io);
        return -1;
    }
    if (SDL_ReadIO(io, b, (size_t)size) != (size_t)size) {
        snprintf(err, err_n, "reading its %lld bytes failed: %s", (long long)size, SDL_GetError());
        free(b);
        SDL_CloseIO(io);
        return -1;
    }
    SDL_CloseIO(io);
    *data = b;
    *n = (size_t)size;
    return 0;
#else
    (void)user;
    (void)path;
    *data = NULL;
    *n = 0;
    snprintf(err, err_n, "this platform packages no legacy tables");
    return -1;
#endif
}

typedef struct LegacyScan {
    IcoXbRefs *r;
    const Copy *c;
    int bad;
} LegacyScan;

/* each block of the earlier version's record: recorded, and its bytes
   scanned when it held host pointers, as that version's save did */
static void legacy_block(void *user, const char *name, uint64_t size, unsigned flags, uint64_t addr,
                         uint64_t offset)
{
    LegacyScan *ls = (LegacyScan *)user;
    const int audio = (flags & ICO_STATE_AUDIO) != 0;
    const int sec = audio ? SEC_AUDIO : SEC_REG;

    ico_xb_block(ls->r, name, size, flags, addr, offset);
    if ((flags & ICO_STATE_SCAN) != 0) {
        char what[48];

        if (ls->c->p[sec] == NULL || offset > ls->c->n[sec] || size > ls->c->n[sec] - offset) {
            ls->bad = 1;
            return;
        }
        snprintf(what, sizeof what, "the block %s", name);
        ico_xb_where(ls->r, what);
        ico_xb_scan(ls->r, (const unsigned char *)ls->c->p[sec] + offset, (size_t)size, addr, 1);
    }
}

/* An earlier Android version's format 2 record named again with that
   version's legacy table (state_xbuild.h IcoXbLegacy): the whole state is
   read and scanned as that version's save scanned it, against that
   version's program, under today's rules.  0 and the new record
   (malloc'd), 1 when no table is packaged for that version, -1 and why. */
static int legacy_record(const char *path, const char *commit, void **out, size_t *out_n, char *why,
                         size_t why_n)
{
    static IcoSchedImage img;
    char name[64];
    IcoLegacyTable *lt = NULL;
    IcoSymTab *ot = NULL;
    IcoSymTabEnv env;
    IcoXbLegacy lg;
    IcoXbProc p;
    IcoXbRefs *r = NULL;
    IcoXbStats st;
    LegacyScan ls;
    const StateRanges *rg;
    Copy c;
    LoadCtx lc;
    int i, rc = -1;

    *out = NULL;
    *out_n = 0;
    if (ico_legacy_find(legacy_read, NULL, ico_state_platform_tag(), commit, name, sizeof name) !=
        1) {
        return 1;
    }
    memset(&c, 0, sizeof c);
    memset(&lc, 0, sizeof lc);
    lc.c = &c;
    lc.xb = 1;
    if (ico_state_file_read(path, NULL, load_section, &lc) != ICO_STATE_OK || lc.bad) {
        snprintf(why, why_n, "its records could not be read%s%s", lc.why[0] ? ": " : "", lc.why);
        goto done;
    }
    rg = c.n[SEC_RANGES] == sizeof(StateRanges) ? (const StateRanges *)c.p[SEC_RANGES] : NULL;
    if (rg == NULL || rg->magic != RANGES_MAGIC || c.p[SEC_SYMS] == NULL) {
        snprintf(why, why_n, "its records could not be read");
        goto done;
    }
    lt = ico_legacy_load(legacy_read, NULL, name, why, why_n);
    if (lt == NULL) {
        goto done;
    }
    memset(&env, 0, sizeof env);
    env.image_lo = rg->image_lo;
    env.image_hi = rg->image_hi;
    env.text_lo = rg->text_lo;
    env.text_hi = rg->text_hi;
    env.game_lo[ICO_GAME_DATA] = rg->gdata_lo;
    env.game_hi[ICO_GAME_DATA] = rg->gdata_hi;
    env.game_lo[ICO_GAME_BSS] = rg->gbss_lo;
    env.game_hi[ICO_GAME_BSS] = rg->gbss_hi;
    env.arch = ICO_CODE_A64;
    ot = ico_legacy_symtab(lt, &env, why, why_n);
    if (ot == NULL || ico_xb_legacy_verify(c.p[SEC_SYMS], (size_t)c.n[SEC_SYMS], ot, rg->image_lo,
                                           lt->hash, lt->n_hash, why, why_n) != 0) {
        goto done;
    }
    memset(&lg, 0, sizeof lg);
    lg.lt = lt;
    lg.old_lo = rg->image_lo;
    lg.v2 = c.p[SEC_SYMS];
    lg.v2_n = (size_t)c.n[SEC_SYMS];
    lg.now_tab = sym_table();
    lg.now_snap = s_snap;
    lg.now_snap_words = s_snap_words;
    for (i = 0; i < 2; i++) {
        lg.now_game_lo[i] = s_gdata[i].lo;
        lg.now_game_hi[i] = s_gdata[i].hi;
    }
    memset(&p, 0, sizeof p);
    p.tab = ot;
    p.image.lo = rg->image_lo;
    p.image.hi = rg->image_hi;
    p.image.text_lo = rg->text_lo;
    p.image.text_hi = rg->text_hi;
    p.game_lo[ICO_GAME_DATA] = rg->gdata_lo;
    p.game_hi[ICO_GAME_DATA] = rg->gdata_hi;
    p.game_lo[ICO_GAME_BSS] = rg->gbss_lo;
    p.game_hi[ICO_GAME_BSS] = rg->gbss_hi;
    p.legacy = &lg;
    r = ico_xb_begin(&p);
    if (r == NULL) {
        snprintf(why, why_n, "no memory to read it again");
        goto done;
    }
    ls.r = r;
    ls.c = &c;
    ls.bad = 0;
    if (ico_xb_record_blocks(c.p[SEC_SYMS], (size_t)c.n[SEC_SYMS], legacy_block, &ls) != 0 ||
        ls.bad) {
        snprintf(why, why_n, "its blocks do not match its records");
        goto done;
    }
    /* the regions that version's save scanned (build_syms) */
    ico_xb_where(r, "the arena");
    ico_xb_scan(r, c.p[SEC_ARENA], (size_t)c.n[SEC_ARENA], rg->arena_lo, 1);
    ico_xb_where(r, "the game's data");
    ico_xb_scan(r, c.p[SEC_GDATA], (size_t)c.n[SEC_GDATA], rg->gdata_lo, 1);
    ico_xb_where(r, "the game's bss");
    ico_xb_scan(r, c.p[SEC_GBSS], (size_t)c.n[SEC_GBSS], rg->gbss_lo, 1);
    if (c.n[SEC_POBJ] != 0) {
        ico_xb_where(r, "the model tables");
        ico_xb_scan(r, c.p[SEC_POBJ], (size_t)c.n[SEC_POBJ], rg->pobj_lo, 1);
    }
    ico_xb_where(r, "the scheduler's table");
    ico_xb_scan(r, c.p[SEC_SCHED], (size_t)c.n[SEC_SCHED], 0, 1);
    if (parse_fibers(&c, &img) == 0) {
        for (i = 0; i < img.n_fibers; i++) {
            const IcoFiberImage *f = &img.fibers[i];
            char what[48];

            ico_xb_frames(r, (int)f->slot + 1);
            snprintf(what, sizeof what, "fiber slot %u's control page", (unsigned)f->slot);
            ico_xb_where(r, what);
            ico_xb_scan(r, f->ctrl, f->ctrl_bytes, f->slot_base, 1);
            snprintf(what, sizeof what, "fiber slot %u's stack", (unsigned)f->slot);
            ico_xb_where(r, what);
            xb_scan_stack(r, f);
        }
        ico_xb_frames(r, 0);
    }
    rc = ico_xb_finish(r, out, out_n, &st);
    r = NULL;
    if (rc != 0) {
        snprintf(why, why_n, "no memory to read it again");
        goto done;
    }
    fprintf(stderr,
            "state: read again with the legacy table %s: %u functions (%u with a thread inside), "
            "%u data symbols, %u start-up values and texts (%u read-only data found by its bytes), "
            "%u page words of the threads, %u ADRPs making them; %u words naming nothing\n",
            name, st.units, st.positions, st.data, st.points, st.windows, st.pages, st.producers,
            st.left);
done:
    if (r != NULL) {
        void *tmp = NULL;
        size_t tn = 0;

        (void)ico_xb_finish(r, &tmp, &tn, &st); /* frees r */
        free(tmp);
    }
    ico_symtab_free(ot);
    ico_legacy_free(lt);
    copy_free(&c);
    return rc;
}

/* A state of another build of this format: its "syms" and "ranges"
   sections against this build (state_xbuild.h ico_xb_check).  Returns the
   verdict's kind, *v filled (the caller frees it with
   ico_xb_verdict_free). */
/* the last xb_probe's legacy naming, ms (0: none ran); for the listing's
   timing line (written under the symbols lock inside the verdicts' work
   lock, read by vd_compute under the work lock) */
static double s_legacy_ms;

static int xb_probe_locked(const char *path, const char *commit, IcoXbVerdict *v)
{
    static const char *names[ICO_STATE_MAX_ENTRIES];
    static uint64_t sizes[ICO_STATE_MAX_ENTRIES], addrs[ICO_STATE_MAX_ENTRIES];
    static unsigned flags[ICO_STATE_MAX_ENTRIES];
    const StateRanges *rg;
    IcoXbNow now;
    Copy c;
    LoadCtx lc;
    IcoStateResult r;
    int i, k;

    memset(&c, 0, sizeof c);
    memset(&lc, 0, sizeof lc);
    lc.c = &c;
    lc.want = 1u << SEC_SYMS | 1u << SEC_RANGES;
    r = ico_state_file_read(path, NULL, load_section, &lc);
    rg = c.n[SEC_RANGES] == sizeof(StateRanges) ? (const StateRanges *)c.p[SEC_RANGES] : NULL;
    if (r != ICO_STATE_OK || lc.bad || rg == NULL || rg->magic != RANGES_MAGIC ||
        c.p[SEC_SYMS] == NULL) {
        memset(v, 0, sizeof *v);
        v->kind = ICO_XB_BAD;
        snprintf(v->why, sizeof v->why, "its records could not be read%s%s",
                 lc.why[0] != '\0' ? ": " : "", lc.why);
        copy_free(&c);
        return v->kind;
    }
    memset(&now, 0, sizeof now);
    now.tab = sym_table();
    (void)ico_state_image_range(&now.image);
    for (i = 0; i < 2; i++) {
        now.game_lo[i] = s_gdata[i].lo;
        now.game_hi[i] = s_gdata[i].hi;
    }
    now.snap = s_snap;
    now.snap_words = s_snap_words;
    now.n_blocks = ico_state_registry_count();
    for (i = 0; i < now.n_blocks; i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        names[i] = e->name;
        sizes[i] = e->n;
        flags[i] = e->flags;
        addrs[i] = (uint64_t)(uintptr_t)e->p;
    }
    now.block_name = names;
    now.block_size = sizes;
    now.block_flags = flags;
    now.block_addr = addrs;
    now.old_game_lo[ICO_GAME_DATA] = rg->gdata_lo;
    now.old_game_hi[ICO_GAME_DATA] = rg->gdata_hi;
    now.old_game_lo[ICO_GAME_BSS] = rg->gbss_lo;
    now.old_game_hi[ICO_GAME_BSS] = rg->gbss_hi;
    now.old_image_lo = rg->image_lo;
    /* an earlier Android version's format 2 record: named again with its
       version's legacy table when one is packaged */
    if (c.n[SEC_SYMS] >= 20 && ((const unsigned char *)c.p[SEC_SYMS])[4] == 2 &&
        ((const unsigned char *)c.p[SEC_SYMS])[16] == ICO_CODE_A64) {
        void *sec3 = NULL;
        size_t n3 = 0;
        char why[200];
        const double lt0 = ico_thread_now_ms();
        const int lr = legacy_record(path, commit, &sec3, &n3, why, sizeof why);

        s_legacy_ms = ico_thread_now_ms() - lt0;

        if (lr == 0) {
            k = ico_xb_check(sec3, n3, &now, v);
            free(sec3);
            copy_free(&c);
            return k;
        }
        if (lr < 0) {
            memset(v, 0, sizeof *v);
            v->kind = ICO_XB_PINNED;
            snprintf(v->why, sizeof v->why, "its version's legacy table could not read it: %.140s",
                     why);
            copy_free(&c);
            return v->kind;
        }
        /* no table for its version: of another game data layout, the
           check below says so (the data refusal); of this layout, the
           player's line is the pinned one (translated, no new text) and the
           log says why */
        if (now.tab != NULL && c.n[SEC_SYMS] >= 48) {
            uint64_t lay = 0;

            for (i = 7; i >= 0; i--) {
                lay = lay << 8 | ((const unsigned char *)c.p[SEC_SYMS])[40 + i];
            }
            if (lay == now.tab->layout_hash) {
                memset(v, 0, sizeof *v);
                v->kind = ICO_XB_PINNED;
                snprintf(v->why, sizeof v->why,
                         "its build has no legacy table here (port/state_legacy/index.txt), so "
                         "its words into read-only data and its page words cannot be named");
                copy_free(&c);
                return v->kind;
            }
        }
    }
    k = ico_xb_check(c.p[SEC_SYMS], (size_t)c.n[SEC_SYMS], &now, v);
    copy_free(&c);
    return k;
}

/* xb_probe under the symbols lock (any thread) */
static int xb_probe(const char *path, const char *commit, IcoXbVerdict *v)
{
    int k;

    xb_lock();
    k = xb_probe_locked(path, commit, v);
    xb_unlock();
    return k;
}

/* the FRESH blocks another build's state lacks (they keep this run's
   bytes), named in one line */
static void log_fresh_blocks(int slot, const IcoXbVerdict *v)
{
    char names[256];
    size_t at = 0;
    int i;

    if (v->fresh == 0 || v->block_off == NULL) {
        return;
    }
    names[0] = '\0';
    for (i = 0; i < ico_state_registry_count() && at + 1 < sizeof names; i++) {
        if (v->block_off[i] == ICO_XB_BLOCK_ABSENT) {
            const int w = snprintf(names + at, sizeof names - at, "%s%s", at > 0 ? ", " : "",
                                   ico_state_registry_entry(i)->name);

            at = w < 0 ? at : at + (size_t)w < sizeof names ? at + (size_t)w : sizeof names - 1;
        }
    }
    fprintf(stderr,
            "state: slot %d was saved before %u of the blocks registered here existed (%s): they "
            "start from this run's values\n",
            slot, v->fresh, names);
}

/* 1 when the registered block `name` is one of the FRESH blocks another
   build's state lacks (v: its translation), so it kept this run's bytes */
static int state_block_absent(const IcoXbVerdict *v, const char *name)
{
    int i;

    if (v->fresh == 0 || v->block_off == NULL) {
        return 0;
    }
    for (i = 0; i < ico_state_registry_count(); i++) {
        if (strcmp(ico_state_registry_entry(i)->name, name) == 0) {
            return v->block_off[i] == ICO_XB_BLOCK_ABSENT;
        }
    }
    return 0;
}

/* The "difficulty.record" block's place in a reg section: this build's
   order (block_off NULL) or another build's offsets; 0 when the state has
   none (a FRESH block it lacks) or the block is not registered */
static int record_place(const uint64_t *block_off, uint64_t *off, uint64_t *n)
{
    uint64_t at = 0;
    int i;

    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if ((e->flags & ICO_STATE_AUDIO) != 0) {
            continue;
        }
        if (strcmp(e->name, "difficulty.record") == 0) {
            if (block_off != NULL && block_off[i] == ICO_XB_BLOCK_ABSENT) {
                return 0;
            }
            *off = block_off != NULL ? block_off[i] : at;
            *n = e->n;
            return 1;
        }
        at += e->n;
    }
    return 0;
}

/* 1 when the staged reg section holds a record with Permadeath on */
static int staged_permadeath(const Copy *c, const uint64_t *block_off)
{
    uint64_t off = 0, n = 0;

    return c->p[SEC_REG] != NULL && record_place(block_off, &off, &n) &&
           ico_state_record_permadeath((const unsigned char *)c->p[SEC_REG], c->n[SEC_REG], off, n);
}

/* The listing's check of a slot's file: its reg section read alone (the
   record's bytes), 1 when it was saved on a Permadeath journey.  The
   listing's verdict (block_off NULL) is kept per slot with the file's size and
   time, so a rebuild of the list does not inflate every slot's reg section
   again (2 MB and more each) while the file is the same. */
static int slot_permadeath(int slot, const uint64_t *block_off)
{
    static struct {
        int known, verdict;
        unsigned long long size;
        long long mtime;
    } s_seen[ICO_STATE_SLOTS + 1];

    char path[ICO_PATH_MAX];
    Copy c;
    LoadCtx lc;
    int k = 0;
    unsigned long long size = 0;
    long long mtime = 0;
    const int cached = block_off == NULL && slot >= 1 && slot <= ICO_STATE_SLOTS;

    if (ico_state_slot_path(path, sizeof path, ico_state_dir(), slot, ".ics") != 0) {
        return 0;
    }
    if (cached) {
        if (ico_path_kind(path, &size, &mtime) != 0) {
            s_seen[slot].known = 0;
            return 0;
        }
        if (s_seen[slot].known && s_seen[slot].size == size && s_seen[slot].mtime == mtime) {
            return s_seen[slot].verdict;
        }
    }
    memset(&c, 0, sizeof c);
    memset(&lc, 0, sizeof lc);
    lc.c = &c;
    lc.want = 1u << SEC_REG;
    lc.xb = block_off != NULL;
    if (ico_state_file_read(path, NULL, load_section, &lc) == ICO_STATE_OK && !lc.bad) {
        k = staged_permadeath(&c, block_off);
    }
    copy_free(&c);
    if (cached) {
        s_seen[slot].known = 1;
        s_seen[slot].verdict = k;
        s_seen[slot].size = size;
        s_seen[slot].mtime = mtime;
    }
    if (k) {
        fprintf(stderr,
                "state: slot %d was saved on a Permadeath journey: it cannot be loaded here\n",
                slot);
    }
    return k;
}

/* A state that header_check called another build's: OK when it can be
   translated into this one (*xb 1, v the translation), else the refusal
   with detail and the box's blocker in *blk (and, with box set, given to
   set_refusal).  now: this run's header (a copy taken on the game thread
   when the listing's worker asks: now_header() is the game thread's).
   Called under the verdicts' work lock (state_verdicts.h): xb_probe's and
   legacy_record's statics and the legacy tables assume one caller; xb_probe
   then takes the symbols lock inside it (the order at s_xb_lock), against
   the save's build_syms. */
static IcoStateResult other_build(const char *path, const IcoStateHeader *saved,
                                  const IcoStateHeader *now, IcoXbVerdict *v, int *xb, char *detail,
                                  size_t n, int box, IcoStateBlocker *blk)
{
    char sv[48], tv[48];
    IcoStateResult r;
    int k;

    *xb = 0;
    memset(v, 0, sizeof *v);
    version_text(saved->commit, sv, sizeof sv);
    version_text(ICO_BUILD_COMMIT, tv, sizeof tv);
    *blk = ICO_STATE_BLOCK_NONE;
    if (saved->version < ICO_STATE_FORMAT_VERSION) {
        *blk = ICO_STATE_BLOCK_OLD;
        if (box) {
            set_refusal(ICO_STATE_BLOCK_OLD, saved->commit);
        }
        snprintf(detail, n,
                 "%s -> %s: made before save states could carry over to later versions (format "
                 "%u)",
                 sv, tv, saved->version);
        return ICO_STATE_OTHER_BUILD;
    }
    r = ico_state_header_check_other_build(saved, now, detail, n);
    if (r != ICO_STATE_OK) {
        return r;
    }
    k = xb_probe(path, saved->commit, v);
    if (k == ICO_XB_OK) {
        *xb = 1;
        return ICO_STATE_OK;
    }
    snprintf(detail, n, "%s -> %s: %s", sv, tv, v->why);
    ico_xb_verdict_free(v);
    /* the result the checkpoint's route reads (state_checkpoint.h) */
    r = ico_ckpt_xb_result(k);
    if (r == ICO_STATE_OTHER_BUILD) {
        *blk = blocker_of(k);
        if (box) {
            set_refusal(*blk, saved->commit);
        }
    }
    return r;
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
   the registered blocks (block_off: unpack_reg's); the scheduler is put
   back by the caller */
static void put_memory(const Copy *c, const uint64_t *block_off)
{
    memcpy((void *)s_gdata[ICO_GAME_DATA].lo, c->p[SEC_GDATA], (size_t)c->n[SEC_GDATA]);
    memcpy((void *)s_gdata[ICO_GAME_BSS].lo, c->p[SEC_GBSS], (size_t)c->n[SEC_GBSS]);
    memcpy(ico_arena_base(), c->p[SEC_ARENA], (size_t)c->n[SEC_ARENA]);
    unpack_reg(0, (const unsigned char *)c->p[SEC_REG], block_off);
    unpack_reg(1, (const unsigned char *)c->p[SEC_AUDIO], block_off);
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

/* the load line's word on one of the saving process's values (the guard's
   value itself is not logged) */
static const char *value_note(uint64_t old, uint64_t now)
{
    if (old == 0 && now == 0) {
        return " not needed";
    }
    if (old == 0) {
        return " not recorded";
    }
    return old == now ? " unchanged" : "";
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

    put_memory(backup, NULL);
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

/* --- the checkpoint (port/save/state_checkpoint.h) ---------------------------------- */

/* this program's own places of the checkpoint globals (its layout) */
static int self_layout(IcoCkptLayout *out)
{
    return ico_ckpt_live_self(out, ico_state_platform_tag(), s_gdata[ICO_GAME_DATA].lo,
                              s_gdata[ICO_GAME_DATA].hi, s_gdata[ICO_GAME_BSS].lo,
                              s_gdata[ICO_GAME_BSS].hi);
}

/* the "ckpt" section a save writes: its size (0: none) */
static size_t ckpt_record(void *out)
{
    static int logged;
    IcoCkptLayout self;
    int k;

    if (self_layout(&self) != 0) {
        return 0;
    }
    if (!logged) {
        char line[320];
        size_t at = 0;

        logged = 1;
        line[0] = '\0';
        for (k = 0; k < ICO_CKPT_G_COUNT && at < sizeof line; k++) {
            const IcoCkptPlace *g = &self.g[k];
            const int n = g->range == ICO_CKPT_RANGE_NONE
                              ? snprintf(line + at, sizeof line - at, "%s%s nowhere", k ? ", " : "",
                                         ico_ckpt_global_name(k))
                              : snprintf(line + at, sizeof line - at, "%s%s %s+0x%x", k ? ", " : "",
                                         ico_ckpt_global_name(k), g->range ? "gbss" : "gdata",
                                         (unsigned)g->off);

            if (n < 0) {
                break;
            }
            at += (size_t)n;
        }
        fprintf(stderr,
                "state: each state names its checkpoint (a later version continues it from "
                "there): %s\n",
                line);
    }
    return ico_ckpt_record_write(self.g, out, ICO_CKPT_RECORD_BYTES);
}

/* the last load that went through the checkpoint (ico_state_last_load_checkpoint) */
static struct {
    int slot;   /* armed, waiting for the Main tick (0 none) */
    int loaded; /* the last load that succeeded went this way */
    char saved_version[48];
} s_ckpt;

int ico_state_last_load_checkpoint(char *saved_version, size_t n)
{
    if (n > 0) {
        snprintf(saved_version, n, "%s", s_ckpt.loaded ? s_ckpt.saved_version : "");
    }
    return s_ckpt.loaded;
}

/* the Main thread's word from state_checkpoint_live.c: the load ran (the
   stage switch began) or was refused there */
static void ckpt_done(int ok, int stage, const char *why)
{
    const int slot = s_ckpt.slot;

    s_ckpt.slot = 0;
    s_list_gen++;
    if (!ok) {
        done(1, slot, ICO_STATE_STAGE_SWITCH, why);
        return;
    }
    (void)stage;
    s_ckpt.loaded = 1;
    s_thumb.slot = s_thumb.th == NULL ? 0 : s_thumb.slot; /* no picture of the old game */
    ui_settings_sync_after_state_checkpoint();
    done(1, slot, ICO_STATE_OK, "");
}

/* What a state's checkpoint gives here: 0 with *plan and *ap filled (ap may
   be NULL: the listing), or the refusal (OTHER_DISC, PERMADEATH by rule;
   OTHER_BUILD with why when nothing can be continued). */
static IcoStateResult ckpt_plan(const char *path, const char *disc_now, IcoCkptState *st,
                                IcoCkptPlan *plan, IcoCkptApply *ap, char *why, size_t n)
{
    const IcoStateEntry *rec_e = NULL;
    const unsigned char *rec = NULL;
    IcoCkptLayout self;
    IcoCkptInput in;
    char bwhy[160];
    int i, k;

    if (ico_ckpt_state_read(path, st) != 0) {
        snprintf(why, n, "%s", st->why);
        return ICO_STATE_BAD_FILE;
    }
    if (strncmp(st->h.disc, disc_now, sizeof st->h.disc) != 0) {
        snprintf(why, n, "it is of the %s disc", st->h.disc);
        return ICO_STATE_OTHER_DISC;
    }
    /* the journey's record, by the state's own block list */
    bwhy[0] = '\0';
    for (i = 0; i < ico_state_registry_count(); i++) {
        if (strcmp(ico_state_registry_entry(i)->name, "difficulty.record") == 0) {
            rec_e = ico_state_registry_entry(i);
        }
    }
    k = rec_e != NULL
            ? ico_ckpt_state_block(st, "difficulty.record", rec_e->n, &rec, bwhy, sizeof bwhy)
            : 0;
    if (k > 0 && ico_state_record_permadeath(st->p[ICO_CKPT_S_REG], st->n[ICO_CKPT_S_REG],
                                             (uint64_t)(rec - st->p[ICO_CKPT_S_REG]), rec_e->n)) {
        snprintf(why, n, "the state was saved on a Permadeath journey");
        return ICO_STATE_PERMADEATH;
    }
    ico_ckpt_state_input(st, self_layout(&self) == 0 ? &self : NULL, &in);
    if (ico_ckpt_locate(&in, plan) != 0) {
        snprintf(why, n, "no checkpoint found in it: %s", plan->why);
        return ICO_STATE_OTHER_BUILD;
    }
    if (plan->checkpoint == 0) {
        snprintf(why, n,
                 "it was saved before its journey's first checkpoint (systemStatus[3] 0, found by "
                 "%s): there is nothing to continue from",
                 plan->source);
        return ICO_STATE_OTHER_BUILD;
    }
    if (ap == NULL) {
        return ICO_STATE_OK;
    }
    memset(ap, 0, sizeof *ap);
    memcpy(ap->image, plan->g[ICO_CKPT_G_IMAGE], ICO_CKPT_IMAGE_BYTES);
    if (plan->g[ICO_CKPT_G_PREVIEW] != NULL) {
        ap->have_preview = 1;
        memcpy(ap->preview, plan->g[ICO_CKPT_G_PREVIEW], sizeof ap->preview);
    }
    /* the card block's option words: the screen word only where the
       disc's block holds it (not the 2001 discs), Players on every disc
       (the 2001 discs keep it beside the save) */
    if (plan->g[ICO_CKPT_G_SCREEN] != NULL && !ico_ckpt_disc_2001(st->h.disc)) {
        ap->have_screen = 1;
        memcpy(&ap->screen, plan->g[ICO_CKPT_G_SCREEN], 4);
    }
    if (plan->g[ICO_CKPT_G_GIRL] != NULL) {
        ap->have_girl = 1;
        memcpy(&ap->girl, plan->g[ICO_CKPT_G_GIRL], 4);
    }
    if (k > 0 && rec_e->n <= sizeof ap->record) {
        ap->have_record = 1;
        ap->record_n = rec_e->n;
        memcpy(ap->record, rec, rec_e->n);
    } else if (k < 0) {
        fprintf(stderr, "state: %s: its difficulty is built from its keys\n", bwhy);
    }
    snprintf(ap->keys, sizeof ap->keys, "%s", st->h.keys);
    return ICO_STATE_OK;
}

/* --- the slots' verdicts, on a worker (the listing) and reused by a load --- */

/* What a slot of another build or platform does at Load, worked out once
   per file (size, time) and settings: the exact path's verdict (other_build,
   an earlier Android record's legacy naming included) and, when that refuses
   and the route says so, the checkpoint's (ckpt_plan).  The listing hands the
   slots to a worker thread (port/save/state_verdicts.h) and lists them from
   their headers at once; the rows fill as the verdicts come.  A Load takes
   the slot's verdict when the file and the settings are the same (the
   translation, or the checkpoint's apply), waiting for the worker when it is
   on that work, else works it out itself.  Every verdict is worked out under
   the verdicts' work lock: other_build, xb_probe, legacy_record, ckpt_plan
   and slot_permadeath keep statics, and the legacy tables' reader assumes
   one caller.  The worker reads only what the game thread set up before it
   started (s_dir, s_gdata, the registry, s_snap, the symbol table read
   first, a copy of this run's header). */
typedef struct SlotVerdict {
    IcoStateResult exact; /* OK: it resumes exactly */
    IcoStateBlocker blk;  /* the refusal box's blocker */
    char detail[384];
    IcoXbVerdict xv; /* exact OK: the translation (the load's input) */
    int have_xv;
    int ckpt_tried;
    IcoStateResult ckpt; /* the checkpoint's result (OK: it continues from it) */
    char ckpt_why[384];
    int ckpt_stage;
    char ckpt_source[128];
    IcoCkptApply *ap; /* ckpt OK: the load's input (malloc'd) */
    double ms_exact, ms_legacy, ms_ckpt;
} SlotVerdict;

static IcoVerdicts *s_vd;
static IcoStateHeader s_vd_now; /* this run's header as the worker's job was given */

static int vd_key_now(int slot, void *user, IcoVdKey *key)
{
    char path[ICO_PATH_MAX];

    (void)user;
    memset(key, 0, sizeof *key);
    key->keys = s_vd_now.settings_hash;
    return ico_state_slot_path(path, sizeof path, s_dir, slot, ".ics") != 0 ||
                   ico_path_kind(path, &key->size, &key->mtime) != 0
               ? -1
               : 0;
}

static void vd_release(void *res, void *user)
{
    SlotVerdict *e = (SlotVerdict *)res;

    (void)user;
    if (e->have_xv) {
        ico_xb_verdict_free(&e->xv);
        e->have_xv = 0;
    }
    free(e->ap);
    e->ap = NULL;
}

/* Issue 218's rules in the verdict, from the file (perform_load applies the
   same ones, ico_state_move_check's parts, to its staged sections before the
   restore): ICO_STATE_OK, or the result the load would route to its
   checkpoint (SELF_CHECK for this build's state, OTHER_BUILD for another
   build's) with detail.  The ranges record alone, and for another build's
   state of an image below the bound its data, threads and scheduler's
   table. */
static IcoStateResult vd_move_rules(const char *path, int xb, const IcoXbVerdict *xv, char *detail,
                                    size_t n)
{
    static uint64_t nums[4096];
    static const char *const names[ICO_RELOC_RANGES] = {"program's image", "arena", "fiber pool",
                                                        "model tables"};
    static const int data_secs[] = {SEC_ARENA, SEC_GDATA, SEC_GBSS, SEC_REG, SEC_AUDIO};
    const void *data[5], *held[2];
    size_t data_n[5], held_n[2], nk = 0;
    uint64_t held_value = 0;
    IcoStateReloc rel;
    IcoStateXlate xl;
    IcoStateMoveIn in;
    StateRanges rg;
    Copy c;
    LoadCtx lc;
    int verdict, i;

    memset(&c, 0, sizeof c);
    memset(&lc, 0, sizeof lc);
    lc.c = &c;
    lc.xb = xb;
    lc.want = 1u << SEC_RANGES;
    if (ico_state_file_read(path, NULL, load_section, &lc) != ICO_STATE_OK || lc.bad ||
        c.n[SEC_RANGES] != sizeof(StateRanges)) {
        copy_free(&c);
        return ICO_STATE_OK; /* the load reads the file again and decides */
    }
    memcpy(&rg, c.p[SEC_RANGES], sizeof rg);
    copy_free(&c);
    if (rg.magic != RANGES_MAGIC) {
        return ICO_STATE_OK;
    }
    build_reloc(&rel, &rg, NULL);
    memset(&xl, 0, sizeof xl);
    if (xb && xv != NULL) {
        xl.span = xv->span;
        xl.n_span = xv->n_span;
        xl.point = xv->point;
        xl.n_point = xv->n_point;
        rel.xlate = &xl;
    }
    memset(&in, 0, sizeof in);
    in.rel = &rel;
    in.saved_image_lo = rg.image_lo;
    in.text_lo = rg.text_lo;
    in.text_hi = rg.text_hi;
    if (rel.xlate != NULL && (rg.image_lo >> 32) < ICO_STATE_SCAN_HIGH_MIN &&
        ico_state_reloc_ambiguous(&rel) < 0) {
        /* the numbers inside the old code: the whole state */
        memset(&lc, 0, sizeof lc);
        lc.c = &c;
        lc.xb = xb;
        lc.want = 1u << SEC_ARENA | 1u << SEC_GDATA | 1u << SEC_GBSS | 1u << SEC_REG |
                  1u << SEC_AUDIO | 1u << SEC_FIBERS | 1u << SEC_SCHED;
        if (ico_state_file_read(path, NULL, load_section, &lc) != ICO_STATE_OK || lc.bad) {
            copy_free(&c);
            return ICO_STATE_OK;
        }
        for (i = 0; i < 5; i++) {
            data[i] = c.p[data_secs[i]];
            data_n[i] = (size_t)c.n[data_secs[i]];
        }
        held[0] = c.p[SEC_FIBERS];
        held_n[0] = (size_t)c.n[SEC_FIBERS];
        held[1] = c.p[SEC_SCHED];
        held_n[1] = (size_t)c.n[SEC_SCHED];
        in.data = data;
        in.data_n = data_n;
        in.n_data = 5;
        in.held = held;
        in.held_n = held_n;
        in.n_held = 2;
    }
    verdict = ico_state_move_check(&in, nums, sizeof nums / sizeof nums[0], &nk, &held_value);
    copy_free(&c);
    if (verdict >= 0 && verdict < ICO_RELOC_RANGES) {
        snprintf(detail, n,
                 "the %s it was saved with lies elsewhere in this run and its addresses "
                 "cannot be told from the game's numbers",
                 names[verdict]);
        return xb ? ICO_STATE_OTHER_BUILD : ICO_STATE_SELF_CHECK;
    }
    if (verdict == ICO_STATE_MOVE_TOO_MANY) {
        snprintf(detail, n,
                 "its data holds more numbers that read as addresses inside the "
                 "earlier version's code than a load keeps");
        return ICO_STATE_OTHER_BUILD;
    }
    if (verdict == ICO_STATE_MOVE_HELD) {
        snprintf(detail, n,
                 "0x%llx inside the earlier version's code is a number in its data and a word "
                 "of a thread's stack or the scheduler's table",
                 (unsigned long long)held_value);
        return ICO_STATE_OTHER_BUILD;
    }
    return ICO_STATE_OK;
}

/* Works out slot's verdict (the work lock held: the worker or a load). */
static int vd_compute(int slot, void *user, void *res, IcoVdKey *key)
{
    static IcoStateHeader h;
    static IcoCkptState st;
    SlotVerdict *out = (SlotVerdict *)res;
    const IcoStateHeader *now = &s_vd_now;
    IcoCkptPlan plan;
    char path[ICO_PATH_MAX];
    double t0;
    int xb = 0;

    (void)user;
    if (vd_key_now(slot, NULL, key) != 0 ||
        ico_state_slot_path(path, sizeof path, s_dir, slot, ".ics") != 0 ||
        ico_state_file_read_header(path, &h) != ICO_STATE_OK) {
        return -1;
    }
    t0 = ico_thread_now_ms();
    s_legacy_ms = 0;
    out->exact = ico_state_header_check(&h, now, out->detail, sizeof out->detail);
    if (out->exact == ICO_STATE_OTHER_BUILD) {
        out->exact = other_build(path, &h, now, &out->xv, &xb, out->detail, sizeof out->detail, 0,
                                 &out->blk);
        out->have_xv = out->exact == ICO_STATE_OK && xb;
        if (out->have_xv && slot_permadeath(slot, out->xv.block_off)) {
            out->exact = ICO_STATE_PERMADEATH;
            snprintf(out->detail, sizeof out->detail, "%s",
                     "the state was saved on a Permadeath journey");
            ico_xb_verdict_free(&out->xv);
            out->have_xv = 0;
        }
    }
    if (out->exact == ICO_STATE_OK) {
        /* issue 218: what the load decides before its restore */
        out->exact = vd_move_rules(path, xb, out->have_xv ? &out->xv : NULL, out->detail,
                                   sizeof out->detail);
        if (out->exact != ICO_STATE_OK && out->have_xv) {
            ico_xb_verdict_free(&out->xv);
            out->have_xv = 0;
        }
    }
    out->ms_exact = ico_thread_now_ms() - t0;
    out->ms_legacy = s_legacy_ms;
    if (out->exact != ICO_STATE_OK && ico_ckpt_route(out->exact, 1) == ICO_CKPT_ROUTE_CHECKPOINT) {
        t0 = ico_thread_now_ms();
        out->ckpt_tried = 1;
        out->ap = (IcoCkptApply *)malloc(sizeof *out->ap);
        out->ckpt = out->ap == NULL ? ICO_STATE_BUSY
                                    : ckpt_plan(path, now->disc, &st, &plan, out->ap, out->ckpt_why,
                                                sizeof out->ckpt_why);
        if (out->ckpt == ICO_STATE_OK) {
            out->ckpt_stage = plan.image_stage;
            snprintf(out->ckpt_source, sizeof out->ckpt_source, "%s", plan.source);
        } else {
            free(out->ap);
            out->ap = NULL;
        }
        ico_ckpt_state_free(&st);
        out->ms_ckpt = ico_thread_now_ms() - t0;
    }
    if (out->exact == ICO_STATE_OK) {
        fprintf(stderr, "state: slot %d (saved by %s) loads here\n", slot, h.commit);
    } else if (out->ckpt_tried && out->ckpt == ICO_STATE_OK) {
        fprintf(stderr,
                "state: slot %d (saved by %s, %s) cannot resume here (%s%s%s): Load continues "
                "from its last checkpoint, stage %d (found by %s)\n",
                slot, h.commit, h.platform, ico_state_result_text(out->exact),
                out->detail[0] != '\0' ? ": " : "", out->detail, out->ckpt_stage, out->ckpt_source);
    } else {
        fprintf(stderr, "state: slot %d (saved by %s) cannot be loaded here: %s%s%s%s%s\n", slot,
                h.commit, ico_state_result_text(out->exact), out->detail[0] != '\0' ? ": " : "",
                out->detail, out->ckpt_tried ? "; its checkpoint: " : "",
                out->ckpt_tried ? out->ckpt_why : "");
    }
    fprintf(stderr,
            "state: slot %d's verdict in %.1f ms: exact %.1f ms (legacy table %.1f ms), "
            "checkpoint %.1f ms\n",
            slot, out->ms_exact + out->ms_ckpt, out->ms_exact, out->ms_legacy, out->ms_ckpt);
    return 0;
}

/* The verdicts, made at the first need (the game thread: init has run) */
static IcoVerdicts *vd(void)
{
    if (s_vd == NULL) {
        IcoVdOps ops;

        memset(&ops, 0, sizeof ops);
        ops.compute = vd_compute;
        ops.key_now = vd_key_now;
        ops.release = vd_release;
        ops.result_bytes = sizeof(SlotVerdict);
        ops.slots = ICO_STATE_SLOTS;
        s_vd_now = *now_header();
        s_vd = ico_vd_create(&ops);
    }
    return s_vd;
}

/* the header copy the work reads, renewed while no work runs (the settings
   may change between listings) */
static void vd_refresh_now(void)
{
    if (vd() != NULL && !ico_vd_running(s_vd)) {
        ico_vd_lock_work(s_vd);
        s_vd_now = *now_header();
        ico_vd_unlock_work(s_vd);
    }
}

typedef struct VdTake {
    IcoXbVerdict *xv;
    int *xb;
    IcoStateResult r;
    IcoStateBlocker blk;
    char *detail;
    size_t n;
    IcoCkptApply *ap;
    int got_ap, stage;
    int spent; /* the translation was handed to an earlier load */
    char *source;
    size_t source_n;
} VdTake;

static void vd_take_exact_fn(void *res, void *ctx)
{
    SlotVerdict *e = (SlotVerdict *)res;
    VdTake *t = (VdTake *)ctx;

    if (e->exact == ICO_STATE_OK && !e->have_xv) {
        t->spent = 1; /* an earlier load took the translation: work it out again */
        return;
    }
    t->r = e->exact;
    t->blk = e->blk;
    snprintf(t->detail, t->n, "%s", e->detail);
    *t->xb = e->have_xv;
    memset(t->xv, 0, sizeof *t->xv);
    if (e->have_xv) {
        *t->xv = e->xv; /* handed over */
        e->have_xv = 0;
    }
}

/* A load's exact verdict for slot (the listing's when fresh, else worked
   out now); the translation is the caller's (ico_xb_verdict_free) */
static IcoStateResult vd_load_exact(int slot, const char *path, const IcoStateHeader *saved,
                                    IcoXbVerdict *xv, int *xb, char *detail, size_t n)
{
    IcoStateBlocker blk = ICO_STATE_BLOCK_NONE;
    VdTake t;
    int rc;

    memset(&t, 0, sizeof t);
    t.xv = xv;
    t.xb = xb;
    t.detail = detail;
    t.n = n;
    vd_refresh_now();
    rc = vd() != NULL ? ico_vd_with(s_vd, slot, vd_take_exact_fn, &t) : -1;
    if (rc < 0 || t.spent) {
        IcoStateResult r;

        ico_vd_lock_work(s_vd);
        r = other_build(path, saved, now_header(), xv, xb, detail, n, 1, &blk);
        ico_vd_unlock_work(s_vd);
        return r;
    }
    fprintf(stderr, "state: slot %d: %s verdict (%s)\n", slot, rc > 0 ? "the listing's" : "a fresh",
            t.r == ICO_STATE_OK ? "it resumes exactly" : ico_state_result_text(t.r));
    if (t.blk != ICO_STATE_BLOCK_NONE) {
        set_refusal(t.blk, saved->commit);
    }
    return t.r;
}

static void vd_take_ckpt_fn(void *res, void *ctx)
{
    SlotVerdict *e = (SlotVerdict *)res;
    VdTake *t = (VdTake *)ctx;

    if (e->ckpt_tried && e->ckpt == ICO_STATE_OK && e->ap != NULL) {
        memcpy(t->ap, e->ap, sizeof *t->ap);
        t->got_ap = 1;
        t->stage = e->ckpt_stage;
        snprintf(t->source, t->source_n, "%.100s", e->ckpt_source);
    }
}

/* A load's checkpoint for slot from the slot's verdict: 1 with *ap, the
   stage and the source, else 0 (the caller plans it) */
static int vd_take_ckpt(int slot, IcoCkptApply *ap, int *stage, char *source, size_t n)
{
    VdTake t;

    memset(&t, 0, sizeof t);
    t.ap = ap;
    t.source = source;
    t.source_n = n;
    if (vd() == NULL || ico_vd_with(s_vd, slot, vd_take_ckpt_fn, &t) < 0 || !t.got_ap) {
        return 0;
    }
    *stage = t.stage;
    return 1;
}

/* The listing's verdict for a slot of another build or platform into info:
   1 when known, 0 when it is to come (queued for the worker) */
static int vd_list(int slot, IcoStateSlotInfo *info)
{
    SlotVerdict e;

    if (vd() == NULL || !ico_vd_list(s_vd, slot, &e)) {
        return 0;
    }
    if (e.exact == ICO_STATE_OK) {
        info->loadable = 1;
        info->why_not = ICO_STATE_OK;
    } else if (e.ckpt_tried && e.ckpt == ICO_STATE_OK) {
        info->loadable = 1;
        info->why_not = ICO_STATE_OK;
        info->checkpoint = 1;
    } else if (e.ckpt_tried && (e.ckpt == ICO_STATE_OTHER_DISC || e.ckpt == ICO_STATE_PERMADEATH)) {
        info->loadable = 0;
        info->why_not = e.ckpt;
    } else {
        info->loadable = 0;
        info->why_not = e.exact;
    }
    return 1;
}

static void vd_join(void)
{
    ico_vd_destroy(s_vd);
    s_vd = NULL;
}

/* Load's second way: a state that could not resume where it was saved
   (exact_r, detail: why) continues from the checkpoint it holds, through
   the game's own load at the end of the next Main tick (the result comes
   from there: ckpt_done) */
static void load_checkpoint(int slot, const char *path, const IcoStateHeader *saved,
                            IcoStateResult exact_r, const char *detail, int other)
{
    static IcoCkptState st;
    static IcoCkptApply ap;
    IcoCkptPlan plan;
    char why[384], sv[48];
    IcoStateResult r;
    const double t0 = ico_thread_now_ms();

    version_text(saved->commit, sv, sizeof sv);
    if (s_ckpt.slot != 0 || ico_ckpt_live_pending()) {
        done(1, slot, ICO_STATE_BUSY, "a checkpoint load is waiting for the game");
        return;
    }
    why[0] = '\0';
    memset(&plan, 0, sizeof plan);
    vd_refresh_now();
    if (vd_take_ckpt(slot, &ap, &plan.image_stage, plan.source, sizeof plan.source)) {
        r = ICO_STATE_OK;
    } else {
        ico_vd_lock_work(vd());
        r = ckpt_plan(path, now_header()->disc, &st, &plan, &ap, why, sizeof why);
        ico_vd_unlock_work(vd());
    }
    if (r != ICO_STATE_OK) {
        ico_ckpt_state_free(&st);
        fprintf(stderr,
                "state: slot %d (version %s, %s) cannot resume where it was saved (%s%s%s), and "
                "its checkpoint cannot be loaded either: %s\n",
                slot, sv, saved->platform, ico_state_result_text(exact_r),
                detail[0] != '\0' ? ": " : "", detail, why);
        if (r == ICO_STATE_OTHER_DISC || r == ICO_STATE_PERMADEATH) {
            done(1, slot, r, why);
            return;
        }
        if (other) {
            set_refusal(ICO_STATE_BLOCK_NO_CHECKPOINT, saved->commit);
            done(1, slot, ICO_STATE_OTHER_BUILD, why);
        } else {
            done(1, slot, exact_r, why);
        }
        return;
    }
    ap.slot = slot;
    ap.done = ckpt_done;
    s_ckpt.slot = slot;
    snprintf(s_ckpt.saved_version, sizeof s_ckpt.saved_version, "%s", sv);
    memset(&s_refusal, 0, sizeof s_refusal);
    fprintf(stderr,
            "state: slot %d (version %s, %s) cannot resume where it was saved (%s%s%s): it "
            "continues from its last checkpoint, stage %d (saved in stage %d), found by %s; "
            "the game's own load runs at the end of the next Main tick (%.1f ms)\n",
            slot, sv, saved->platform, ico_state_result_text(exact_r),
            detail[0] != '\0' ? ": " : "", detail, plan.image_stage, saved->stage_no, plan.source,
            ico_thread_now_ms() - t0);
    ico_ckpt_state_free(&st);
    ico_ckpt_live_arm(&ap);
}

/* the merged points of a load that keeps numbers inside the old code as
   they are (keep_code_numbers), freed at the next load */
static IcoStateSpan *s_kept_points;

/* Issue 218, another build's state whose image's upper half is below the
   scan's bound (Windows' fixed base 0x140000000): a float from 2.0 to about
   142 next to the integer 1 reads as an address in that image, and one
   that lands inside a function's code would be moved by the function's
   displacement (a keyframe's value changed).  A data structure holds a
   function's start, never a byte inside it, so such words of the game's
   memory and the registered blocks are numbers: each becomes an identity
   point of the translation (looked up before the spans), kept as it is.
   0, or -1 with `detail` when the state goes to its checkpoint instead: a
   thread's stack or the scheduler's table holds one of the values too (it
   may be a return address there), or more of them than the load keeps. */
static int keep_code_numbers(int slot, const IcoStateHeader *saved, const Copy *c,
                             const StateRanges *rg, IcoStateXlate *xl, const IcoXbVerdict *xv,
                             char *detail, size_t detail_n)
{
    enum { KEEP_MAX = 4096 };

    static uint64_t nums[KEEP_MAX];
    static const int secs[] = {SEC_ARENA, SEC_GDATA, SEC_GBSS, SEC_REG, SEC_AUDIO};
    size_t nk = 0, found = 0, i, a, b, at;
    char sv[48];
    IcoStateSpan *m;

    for (i = 0; i < sizeof secs / sizeof secs[0]; i++) {
        found += ico_state_code_numbers(xl, rg->text_lo, rg->text_hi, c->p[secs[i]],
                                        (size_t)c->n[secs[i]], nums, KEEP_MAX, &nk);
    }
    if (nk == 0) {
        return 0;
    }
    version_text(saved->commit, sv, sizeof sv);
    if (found > nk) {
        snprintf(detail, detail_n,
                 "its data holds more than %d numbers that read as addresses inside the code of "
                 "version %s",
                 (int)KEEP_MAX, sv);
        fprintf(stderr, "state: slot %d: %s: it continues from its checkpoint\n", slot, detail);
        set_refusal(ICO_STATE_BLOCK_DATA, saved->commit);
        return -1;
    }
    {
        uint64_t both = 0;

        if (ico_state_region_holds(c->p[SEC_FIBERS], (size_t)c->n[SEC_FIBERS], nums, nk, &both) ||
            ico_state_region_holds(c->p[SEC_SCHED], (size_t)c->n[SEC_SCHED], nums, nk, &both)) {
            snprintf(detail, detail_n,
                     "0x%llx inside the code of version %s is a number in its data and a word of a "
                     "thread's stack or the scheduler's table, which may be a return address there",
                     (unsigned long long)both, sv);
            fprintf(stderr, "state: slot %d: %s: it continues from its checkpoint\n", slot, detail);
            set_refusal(ICO_STATE_BLOCK_DATA, saved->commit);
            return -1;
        }
    }
    m = (IcoStateSpan *)malloc((xv->n_point + nk) * sizeof *m);
    if (m == NULL) {
        snprintf(detail, detail_n, "no memory for the translation's %zu kept numbers", nk);
        set_refusal(ICO_STATE_BLOCK_DATA, saved->commit);
        return -1;
    }
    /* both lists sorted, no value in both (ico_state_code_numbers skips the
       points): one sorted list */
    for (a = 0, b = 0, at = 0; a < xv->n_point || b < nk;) {
        if (b >= nk || (a < xv->n_point && xv->point[a].lo < nums[b])) {
            m[at++] = xv->point[a++];
        } else {
            m[at].lo = m[at].hi = m[at].now = nums[b++];
            at++;
        }
    }
    free(s_kept_points);
    s_kept_points = m;
    xl->point = m;
    xl->n_point = at;
    fprintf(stderr,
            "state: slot %d from version %s: %zu values in its data lie inside that version's "
            "code at no function's start, numbers next to a small integer rather than addresses "
            "(first 0x%llx, last 0x%llx): kept as they are\n",
            slot, sv, nk, (unsigned long long)nums[0], (unsigned long long)nums[nk - 1]);
    return 0;
}

/* A load the exact path refused: the checkpoint's way where
   ico_ckpt_route says so, else the refusal as before.  saved: the state's
   header, NULL when it could not be read. */
static void load_refused(int slot, const char *path, const IcoStateHeader *saved, IcoStateResult r,
                         const char *detail)
{
    const int other =
        saved != NULL && (saved->version != s_now.version ||
                          memcmp(saved->build_id, s_now.build_id, sizeof saved->build_id) != 0 ||
                          strncmp(saved->platform, s_now.platform, sizeof saved->platform) != 0);

    if (saved == NULL || ico_ckpt_route(r, other) != ICO_CKPT_ROUTE_CHECKPOINT) {
        done(1, slot, r, detail);
        return;
    }
    load_checkpoint(slot, path, saved, r, detail, other);
}

static void perform_load(int slot)
{
    static IcoStateHeader saved;
    static IcoSchedImage img;
    IcoStateConds c;
    Copy stage, backup;
    LoadCtx lc;
    IcoStateReloc rel;
    IcoStateProc proc;
    uint64_t value_words[ICO_RELOC_VALUES];
    IcoStateCheck chk;
    ScanCounts k;
    static IcoXbVerdict xv;
    IcoStateXlate xl;
    uint64_t missed = 0, first_missed = 0;
    int xb = 0;
    char path[ICO_PATH_MAX];
    char detail[384];
    const char *what = "";
    const StateRanges *rg;
    size_t live_pobj_bytes = 0;
    void **live_pobj;
    void *pobj_new = NULL;
    unsigned int live_tex = 0;
    IcoStateResult r;
    double t0;
    int moved;
    int header_ok = 0;
    int i;

    (void)live_tex;
    detail[0] = '\0';
    memset(&s_refusal, 0, sizeof s_refusal);
    s_ckpt.loaded = 0;
    ico_xb_verdict_free(&xv);
    free(s_kept_points);
    s_kept_points = NULL;
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
        header_ok = 1;
        r = ico_state_header_check(&saved, now_header(), detail, sizeof detail);
    }
    if (r == ICO_STATE_OTHER_BUILD) {
        /* another build's state (the platform is this one's): translated
           when everything it points into is unchanged here */
        r = vd_load_exact(slot, path, &saved, &xv, &xb, detail, sizeof detail);
        if (r != ICO_STATE_OK && xv.by_unnamed) {
            /* the words its save named nothing, as the state lists them
               (a format 2 state has no list) */
            log_unnamed(slot, "refused:", xv.list, xv.listed, xv.left);
        }
    }
    if (r != ICO_STATE_OK) {
        /* the checkpoint's way where the refusal allows it (state_checkpoint.h) */
        load_refused(slot, path, header_ok ? &saved : NULL, r, detail);
        return;
    }
    if (xb) {
        char sv[48], tv[48];

        version_text(saved.commit, sv, sizeof sv);
        version_text(ICO_BUILD_COMMIT, tv, sizeof tv);
        fprintf(stderr,
                "state: slot %d was saved by version %s, this is %s: everything it points into is "
                "unchanged here, so it is translated (%u functions, %u data symbols, %u start-up "
                "values and texts, %zu spans, %zu points; %u dead-frame words without one; %u "
                "words naming nothing kept as numbers, first 0x%llx; %u texts had identical "
                "twins, the first taken)\n",
                slot, sv, tv, xv.units, xv.data, xv.points, xv.n_span, xv.n_point, xv.soft_left,
                xv.left, (unsigned long long)xv.first_left, xv.twins);
        log_fresh_blocks(slot, &xv);
    }
    {
        char keys[256];
        const int nk = ico_state_keys_absent(saved.keys, s_now.keys, keys, sizeof keys);

        if (nk > 0) {
            fprintf(stderr,
                    "state: slot %d was saved before the setting%s %s existed: read as %s default, "
                    "which %s now\n",
                    slot, nk > 1 ? "s" : "", keys, nk > 1 ? "their" : "its",
                    nk > 1 ? "they have" : "it has");
        }
    }
    {
        char keys[256];
        const int nd = ico_state_keys_defaulted(saved.keys, s_now.keys, keys, sizeof keys);

        if (nd > 0) {
            fprintf(stderr,
                    "state: slot %d was saved with the setting%s %s unset and %s written as %s "
                    "default now, or the other way round: the same setting%s\n",
                    slot, nd > 1 ? "s" : "", keys, nd > 1 ? "they are" : "it is",
                    nd > 1 ? "their" : "its", nd > 1 ? "s" : "");
        }
    }
    {
        char keys[ICO_STATE_KEYS_MAX];
        const int nr = ico_state_keys_retired(saved.keys, s_now.keys, keys, sizeof keys);

        if (nr > 0) {
            fprintf(stderr,
                    "state: slot %d lists %d difficulty row%s among its settings (made before they "
                    "were its own block): not compared: %s\n",
                    slot, nr, nr > 1 ? "s" : "", keys);
        }
    }
    t0 = ico_thread_now_ms();
    memset(&stage, 0, sizeof stage);
    memset(&backup, 0, sizeof backup);
    memset(&lc, 0, sizeof lc);
    lc.c = &stage;
    lc.xb = xb;
    r = ico_state_file_read(path, NULL, load_section, &lc);
    rg = stage.n[SEC_RANGES] == sizeof(StateRanges) ? (const StateRanges *)stage.p[SEC_RANGES]
                                                    : NULL;
    if (r == ICO_STATE_OK && lc.bad) {
        r = ICO_STATE_BAD_FILE;
        snprintf(detail, sizeof detail, "%s", lc.why);
    } else if (r == ICO_STATE_OK &&
               (rg == NULL ||
                check_sizes(&stage, rg, xb ? xv.block_off : NULL, detail, sizeof detail) != 0 ||
                parse_fibers(&stage, &img) != 0)) {
        r = ICO_STATE_BAD_FILE;
        if (detail[0] == '\0') {
            snprintf(detail, sizeof detail, "the fibers section does not hold what it says");
        }
    }
    if (xb && r == ICO_STATE_BAD_FILE && rg != NULL && rg->magic == RANGES_MAGIC) {
        /* another build whose scheduler, fiber slot or a section's size is
           other than this one's: what it holds changed, it is not damaged */
        char sv[48], tv[48], why[384];

        version_text(saved.commit, sv, sizeof sv);
        version_text(ICO_BUILD_COMMIT, tv, sizeof tv);
        snprintf(why, sizeof why, "%s", detail);
        snprintf(detail, sizeof detail, "%s -> %s: %.250s", sv, tv, why);
        set_refusal(ICO_STATE_BLOCK_DATA, saved.commit);
        r = ICO_STATE_OTHER_BUILD;
    }
    /* a state saved on a Permadeath journey never loads, whatever journey
       is in force now (from the title too): its record, read from the
       file's own bytes, before anything is restored */
    if (r == ICO_STATE_OK && staged_permadeath(&stage, xb ? xv.block_off : NULL)) {
        r = ICO_STATE_PERMADEATH;
        snprintf(detail, sizeof detail, "%s", "the state was saved on a Permadeath journey");
    }
    /* a state without the saving process's thread pointer or stack guard
       where this platform's frames hold them: its fibers would resume with
       another process's values, so it is refused before anything changes */
    if (r == ICO_STATE_OK &&
        ico_state_proc_check(saved.platform, rg->tls_base, rg->stack_guard) != 0) {
        r = ICO_STATE_OTHER_BUILD;
        snprintf(detail, sizeof detail, "no thread-pointer record");
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
        ico_xb_verdict_free(&xv);
        load_refused(slot, path, &saved, r, detail);
        return;
    }
    build_reloc(&rel, rg, pobj_new);
    if (xb) {
        /* another build: the program's words by what each points into */
        memset(&xl, 0, sizeof xl);
        xl.span = xv.span;
        xl.n_span = xv.n_span;
        xl.point = xv.point;
        xl.n_point = xv.n_point;
        /* arm64 page words: each fiber's own (fiber.c sets the slot) */
        xl.fiber_point = (const IcoStateSpan *const *)xv.fiber_point;
        xl.fiber_n_point = xv.fiber_n_point;
        xl.n_fiber = xv.n_fiber;
        xl.missed = &missed;
        xl.first_missed = &first_missed;
        rel.xlate = &xl;
        if ((rg->image_lo >> 32) < ICO_STATE_SCAN_HIGH_MIN &&
            keep_code_numbers(slot, &saved, &stage, rg, &xl, &xv, detail, sizeof detail) != 0) {
            copy_free(&stage);
            free(pobj_new);
            ico_xb_verdict_free(&xv);
            load_refused(slot, path, &saved, ICO_STATE_OTHER_BUILD, detail);
            return;
        }
    }
    /* a range that moved in this run whose saved addresses' upper half is
       a small integer: the game's own pairs of 32-bit numbers read as its
       addresses, and the scan would move them too (issue 218: the Apple
       image above the 4 GB __PAGEZERO; an arena or fiber pool that could
       not be pinned at its address).  Nothing has changed yet: the state
       continues from its checkpoint instead */
    {
        const int amb = ico_state_reloc_ambiguous(&rel);

        if (amb >= 0) {
            static const char *const names[ICO_RELOC_RANGES] = {"program's image", "arena",
                                                                "fiber pool", "model tables"};
            const IcoStateRange *g = &rel.range[amb];
            const size_t words =
                ico_state_reloc_count(&rel, amb, stage.p[SEC_ARENA], (size_t)stage.n[SEC_ARENA]);

            fprintf(stderr,
                    "state: slot %d cannot be moved into this run exactly: the %s it was saved "
                    "with (0x%llx-0x%llx) lies elsewhere now (%s0x%llx) and its addresses' upper "
                    "half 0x%llx is below 0x%x, where the game's own pairs of numbers read as "
                    "addresses in it (%zu words of the saved arena lie in it; which of them are "
                    "addresses cannot be told), so nothing is moved and it continues from its "
                    "checkpoint\n",
                    slot, names[amb], (unsigned long long)g->lo, (unsigned long long)g->hi,
                    g->delta < 0 ? "-" : "+",
                    (unsigned long long)(g->delta < 0 ? -(uint64_t)g->delta : (uint64_t)g->delta),
                    (unsigned long long)(g->lo >> 32), ICO_STATE_SCAN_HIGH_MIN, words);
            snprintf(detail, sizeof detail,
                     "the %s it was saved with lies elsewhere in this run and its addresses "
                     "cannot be told from the game's numbers",
                     names[amb]);
            copy_free(&stage);
            free(pobj_new);
            ico_xb_verdict_free(&xv);
            load_refused(slot, path, &saved, ICO_STATE_SELF_CHECK, detail);
            return;
        }
    }
    /* the saving process's thread pointer and stack guard -> this one's
       (this is the game thread, which resumes every fiber) */
    ico_state_proc_read(&proc);
    ico_state_reloc_set_values(&rel, rg->tls_base, proc.tls_base, rg->stack_guard, proc.stack_guard,
                               value_words);
    /* the regions are scanned when a range moved, a value differs or the
       state is another build's */
    moved = any_moved(&rel) || ico_state_reloc_values_on(&rel) || xb;
    /* the hash's host blocks: the live tables, the saved ones, the copy */
    note_host_blocks();
    ico_state_hash_note_block(&s_host_blocks, rg->pobj_lo, rg->pobj_bytes);
    ico_state_hash_note_block(&s_host_blocks, (uint64_t)(uintptr_t)pobj_new,
                              pobj_new != NULL ? rg->pobj_bytes : 0);
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
    put_memory(&stage, xb ? xv.block_off : NULL);
    if (ico_sched_state_restore(&img, &rel) != 0) {
        roll_back(&backup, pobj_new);
        ico_audio_unlock();
        copy_free(&stage);
        copy_free(&backup);
        ico_xb_verdict_free(&xv);
        load_refused(slot, path, &saved, ICO_STATE_SELF_CHECK,
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
        ico_xb_verdict_free(&xv);
        load_refused(slot, path, &saved, ICO_STATE_SELF_CHECK, detail);
        return;
    }
    /* committed: the old model tables go, the renderer's handles follow */
    free(live_pobj);
#ifndef ICO_HEADLESS
    /* the render thread idle while the renderer's textures and meshes go
       (its next picture is of the restored game) */
    (void)rd_thread_drain();
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
    /* the journey's difficulty: the record the state holds (its block put
       back above), or for a state made before records (another build's
       without the block: this run's bytes were kept) one built from the
       state's own keys text, never from config.toml; either way the
       variants' switches follow (ico_variants_apply inside) */
    {
        const int absent = xb && state_block_absent(&xv, "difficulty.record");

        fprintf(stderr, "state: slot %d's difficulty: %s\n", slot,
                absent ? "no record in the state (made before states held one): built from its keys"
                       : "the state's record");
        ico_difficulty_state_loaded(absent, saved.keys);
    }
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
                "thread pointer 0x%llx->0x%llx%s (%llu words), stack guard%s (%llu words); "
                "checks %u partitions, %u blocks, %u threads, %u handlers; %d fibers; apply "
                "%.1f ms\n",
                slot, saved.main_tick, saved.stage_no, deltas, moved ? "" : "(nothing moved)",
                (unsigned long long)k.arena, (unsigned long long)k.gdata,
                (unsigned long long)k.gbss, (unsigned long long)k.reg, (unsigned long long)k.pobj,
                (unsigned long long)rg->tls_base, (unsigned long long)proc.tls_base,
                value_note(rg->tls_base, proc.tls_base),
                (unsigned long long)value_words[ICO_RELOC_TLS],
                value_note(rg->stack_guard, proc.stack_guard),
                (unsigned long long)value_words[ICO_RELOC_GUARD], chk.partitions, chk.nodes,
                chk.threads, chk.handlers, img.n_fibers, ico_thread_now_ms() - t0);
        if (xb) {
            fprintf(stderr,
                    "state: slot %d from version %s: %llu program words had no translation and "
                    "were left as they were (first 0x%llx)\n",
                    slot, saved.commit, (unsigned long long)missed,
                    (unsigned long long)first_missed);
        }
    }
    ico_xb_verdict_free(&xv);
    copy_free(&stage);
    copy_free(&backup);
    s_list_gen++;
    ui_settings_sync_after_state_load();
    /* the pause picture showed the game before the load: the loaded slot's
       own picture takes its place */
    shot_from_slot(slot);
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
    if (s_thumb.slot == slot) {
        /* the picture still to come for this slot is dropped first, so no
           .png is left behind the delete (review finding 11) */
        if (s_thumb.th != NULL) {
            ico_thread_join(s_thumb.th);
            s_thumb.th = NULL;
        }
        free(s_thumb.px);
        s_thumb.px = NULL;
        s_thumb.slot = 0;
        fprintf(stderr, "state: slot %d's picture still to come dropped (the slot is deleted)\n",
                slot);
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

/* 1 when this build's state in slot has a range the scan may not move in
   this run (issue 218: an iOS state after a restart): the listing then
   takes the slot's verdict, which names its checkpoint, as for another
   build's state.  The ranges record alone (152 bytes). */
static int slot_moves_refused(int slot)
{
    char path[ICO_PATH_MAX], why[8];

    return ico_state_slot_path(path, sizeof path, s_dir, slot, ".ics") == 0 &&
           vd_move_rules(path, 0, NULL, why, sizeof why) != ICO_STATE_OK;
}

void ico_state_list(IcoStateSlotInfo out[ICO_STATE_SLOTS])
{
    static IcoStateSlotInfo cache[ICO_STATE_SLOTS];
    static unsigned cache_gen = ~0u, cache_vd = ~0u;
    static uint64_t cache_keys;
    const IcoStateHeader *now;
    unsigned vd_gen;

    init();
    if (!s_on) {
        memset(out, 0, sizeof(IcoStateSlotInfo) * ICO_STATE_SLOTS);
        return;
    }
    now = now_header();
    vd_refresh_now();
    vd_gen = vd() != NULL ? ico_vd_gen(s_vd) : 0;
    if (cache_gen != s_list_gen || cache_keys != now->settings_hash || cache_vd != vd_gen) {
        int i, pending = 0;

        ico_state_list_dir(ico_state_dir(), now, cache);
        for (i = 0; i < ICO_STATE_SLOTS; i++) {
            if (cache[i].present &&
                ((!cache[i].loadable && (cache[i].why_not == ICO_STATE_OTHER_BUILD ||
                                         cache[i].why_not == ICO_STATE_OTHER_PLATFORM)) ||
                 (cache[i].loadable && slot_moves_refused(i + 1)))) {
                /* another build's or platform's state: its verdict from the
                   worker; until it comes the row stands on its header and a
                   Load works it out (vd_load_exact, load_checkpoint) */
                if (!vd_list(i + 1, &cache[i])) {
                    pending = 1;
                    cache[i].loadable = 1;
                    cache[i].why_not = ICO_STATE_OK;
                    cache[i].pending = 1;
                }
            } else if (cache[i].present && cache[i].loadable && slot_permadeath(i + 1, NULL)) {
                /* this build's state of a Permadeath journey */
                cache[i].loadable = 0;
                cache[i].why_not = ICO_STATE_PERMADEATH;
            }
        }
        if (pending) {
            (void)sym_table(); /* read here: its first call is not for two threads */
            if (ico_vd_kick(s_vd) < 0) {
                /* no thread: worked out above (ico_vd_kick ran the queue) */
                for (i = 0; i < ICO_STATE_SLOTS; i++) {
                    if (cache[i].pending && vd_list(i + 1, &cache[i])) {
                        cache[i].pending = 0;
                    }
                }
            } else {
                fprintf(stderr, "state: the slots' verdicts are worked out on a thread; the page "
                                "lists them from their headers meanwhile\n");
            }
        }
        cache_gen = s_list_gen;
        cache_keys = now->settings_hash;
        cache_vd = vd() != NULL ? ico_vd_gen(s_vd) : 0;
    }
    memcpy(out, cache, sizeof cache);
    if (ico_difficulty_states_locked()) {
        /* a Permadeath journey in force: no slot loads (the load refuses
           the same, ico_state_conditions); the cache keeps each slot's own
           verdict for when another journey is in force */
        int i;

        for (i = 0; i < ICO_STATE_SLOTS; i++) {
            if (out[i].present) {
                out[i].loadable = 0;
                out[i].why_not = ICO_STATE_PERMADEATH;
            }
        }
    }
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
    unsigned int save_tick, load_tick, hash_tick, dump_tick;
    int save_slot, load_slot;
    int save_done, load_done;
    int dumps; /* dumps written this run */
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

void ico_state_dev_set_dump(unsigned int dump_tick)
{
    s_dev.dump_tick = dump_tick;
    s_dev.dumps = 0;
    if (dump_tick != 0) {
        fprintf(stderr, "state: [dev] dump of arena+gdata+gbss at Main tick %u\n", dump_tick);
    }
}

/* --- the [dev] hash and dump --------------------------------------------------- */

/* the three regions the hash covers, in its order */
static void dev_regions(IcoStateHashRegion rg[3])
{
    rg[0].name = "arena";
    rg[0].p = ico_arena_base();
    rg[0].n = ico_arena_size();
    rg[1].name = "gdata";
    rg[1].p = (const void *)s_gdata[ICO_GAME_DATA].lo;
    rg[1].n = (size_t)(s_gdata[ICO_GAME_DATA].hi - s_gdata[ICO_GAME_DATA].lo);
    rg[2].name = "gbss";
    rg[2].p = (const void *)s_gdata[ICO_GAME_BSS].lo;
    rg[2].n = (size_t)(s_gdata[ICO_GAME_BSS].hi - s_gdata[ICO_GAME_BSS].lo);
}

/* the typed fields (state_hash.h), grown as they come */
typedef struct FieldList {
    IcoStateHashField *f;
    int n, cap;
    int failed;
} FieldList;

static void field_add(FieldList *l, const void *p, size_t n, int kind)
{
    if (p == NULL || n == 0 || l->failed) {
        return;
    }
    if (l->n == l->cap) {
        const int cap = l->cap ? l->cap * 2 : 64;
        IcoStateHashField *g =
            (IcoStateHashField *)realloc(l->f, (size_t)cap * sizeof(IcoStateHashField));

        if (g == NULL) {
            l->failed = 1;
            return;
        }
        l->f = g;
        l->cap = cap;
    }
    l->f[l->n].p = p;
    l->f[l->n].n = n;
    l->f[l->n].kind = kind;
    l->n++;
}

#ifndef ICO_HEADLESS
static void pac_field(const void *p, size_t n, int ptr, void *user)
{
    field_add((FieldList *)user, p, n, ptr ? ICO_STATE_MASK_PTR : ICO_STATE_MASK_ID);
}

static void mesh_field(const void *field, void *user)
{
    field_add((FieldList *)user, field, 4, ICO_STATE_MASK_ID);
}
#endif

/* The host's words in the game's memory, by their typed locations: the
   model tables block's pointer (PObj.c); in the window build the packet
   scratch stream's words (Packet.c), the textures' host record (Texture.c)
   and every mesh id field a packet holds (rd_mesh.h). */
static void dev_fields(FieldList *l, const IcoStateHashRegion *rg, int nrg)
{
    size_t n = 0;
    void *w = PObj_HostStateWord(&n);

    field_add(l, w, n, ICO_STATE_MASK_PTR);
#ifndef ICO_HEADLESS
    pac_HostStateWords(pac_field, l);
    w = tex_HostStateBlock(&n);
    field_add(l, w, n, ICO_STATE_MASK_ID);
    {
        int i;

        for (i = 0; i < nrg; i++) {
            (void)rd_vu_mesh_each_field(rg[i].p, (const char *)rg[i].p + rg[i].n, mesh_field, l);
        }
    }
#else
    (void)rg;
    (void)nrg;
#endif
}

static const char *const s_mask_kinds[3] = {"pointer", "host-id", "block-pointer"};

typedef struct MaskOut {
    FILE *f;
    const IcoStateHashRegion *rg;
} MaskOut;

static void mask_line(int region, size_t off, size_t n, int kind, void *user)
{
    MaskOut *m = (MaskOut *)user;

    fprintf(m->f, "%s 0x%llx %llu %s\n", m->rg[region].name, (unsigned long long)off,
            (unsigned long long)n, s_mask_kinds[kind >= 0 && kind < 3 ? kind : 0]);
}

/* The masked hash of the three regions (state_hash.h); masked lines into
   `masked` when not NULL.  0, or -1 when memory ran out. */
static int dev_measure(IcoStateHashResult *res, FILE *masked)
{
    IcoStateHashRegion rg[3];
    FieldList l;
    MaskOut m;
    int r;

    memset(&l, 0, sizeof l);
    dev_regions(rg);
    note_host_blocks();
    dev_fields(&l, rg, 3);
    m.f = masked;
    m.rg = rg;
    r = l.failed ? -1
                 : ico_state_hash_regions(rg, 3, l.f, l.n, &s_host_blocks,
                                          masked != NULL ? mask_line : NULL, &m, res);
    free(l.f);
    return r;
}

/* The hash the round trip compares: of the game's state, the host's words
   left out (state_hash.h; docs/port/PLATFORM.md "Save states", the hash
   rule).  With nothing to leave out it is the plain hash of the three
   regions. */
static void dev_hash(unsigned int tick)
{
    IcoStateHashResult res;

    if (dev_measure(&res, NULL) != 0) {
        fprintf(stderr, "state: hash at tick %u: no memory for the regions' copy\n", tick);
        return;
    }
    fprintf(stderr, "state: hash %016llx of arena+gdata+gbss at tick %u\n",
            (unsigned long long)res.hash, tick);
    fprintf(stderr,
            "state: hash masked %llu pointer words, %llu host ids at tick %u (%d host blocks, "
            "region hashes arena %016llx gdata %016llx gbss %016llx)\n",
            (unsigned long long)res.ptr_words, (unsigned long long)res.host_ids, tick,
            s_host_blocks.n, (unsigned long long)res.region_hash[0],
            (unsigned long long)res.region_hash[1], (unsigned long long)res.region_hash[2]);
}

static int write_file(const char *path, const void *p, size_t n)
{
    FILE *f = ico_fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(p, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}

/* [dev] state_dump_at: the three regions raw into
   <states>/dump-<tick>-<k>/ (k counts this run's dumps from 1) with
   regions.txt (each region's address, size and masked hash, the hash, the
   addresses of two of the game's globals for the link map) and masked.txt
   (each range the hash left out); tools/state_region_diff.py compares two
   such folders. */
static void dev_dump(unsigned int tick)
{
    static const struct {
        const char *name;
        const void *p;
    } syms[2] = {{"systemStatus", systemStatus}, {"stage_no", &stage_no}};

    IcoStateHashRegion rg[3];
    IcoStateHashResult res;
    char dir[ICO_PATH_MAX];
    char path[ICO_PATH_MAX + 32];
    FILE *f;
    int bad = 0;
    int i;

    s_dev.dumps++;
    if (snprintf(dir, sizeof dir, "%s/dump-%u-%d", ico_state_dir(), tick, s_dev.dumps) >=
            (int)sizeof dir ||
        ico_make_dir(dir) != 0) {
        fprintf(stderr, "state: dump at tick %u: no folder %s\n", tick, dir);
        return;
    }
    dev_regions(rg);
    for (i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s.bin", dir, rg[i].name);
        bad |= write_file(path, rg[i].p, rg[i].n) != 0;
    }
    snprintf(path, sizeof path, "%s/masked.txt", dir);
    f = ico_fopen(path, "w");
    if (f == NULL || dev_measure(&res, f) != 0) {
        bad = 1;
        memset(&res, 0, sizeof res);
    }
    if (f != NULL) {
        bad |= fclose(f) != 0;
    }
    snprintf(path, sizeof path, "%s/regions.txt", dir);
    f = ico_fopen(path, "w");
    if (f != NULL) {
        fprintf(
            f,
            "# a [dev] state_dump_at dump (port/game/state_host.c); tools/state_region_diff.py\n");
        fprintf(f, "tick %u\nmain_tick %u\nhash %016llx\nmasked %llu %llu\n", tick,
                ico_host_main_ticks(), (unsigned long long)res.hash,
                (unsigned long long)res.ptr_words, (unsigned long long)res.host_ids);
        for (i = 0; i < 3; i++) {
            fprintf(f, "region %s 0x%llx %llu %016llx\n", rg[i].name,
                    (unsigned long long)(uintptr_t)rg[i].p, (unsigned long long)rg[i].n,
                    (unsigned long long)res.region_hash[i]);
        }
        for (i = 0; i < 2; i++) {
            fprintf(f, "symbol %s 0x%llx\n", syms[i].name,
                    (unsigned long long)(uintptr_t)syms[i].p);
        }
        bad |= fclose(f) != 0;
    } else {
        bad = 1;
    }
    fprintf(stderr,
            "state: dumped arena+gdata+gbss at tick %u to %s (hash %016llx, masked %llu pointer "
            "words, %llu host ids)%s\n",
            tick, dir, (unsigned long long)res.hash, (unsigned long long)res.ptr_words,
            (unsigned long long)res.host_ids, bad ? ": a file could not be written" : "");
}

/* the value a [dev] tick key waits for is reached at this poll: the first
   poll at or past it, again after a load brought the tick back below it */
static int dev_reached(unsigned int at, unsigned int tick, unsigned int last)
{
    return at != 0 && tick >= at && (last < at || last > tick);
}

/* the [dev] keys: the hash and the dump each time the tick reaches its
   value (a load brings the tick back below it), the save and the load
   once */
static void dev_poll(void)
{
    const unsigned int tick = ico_host_main_ticks();
    const unsigned int last = s_dev.last_tick;

    s_dev.last_tick = tick;
    if (s_on && dev_reached(s_dev.hash_tick, tick, last)) {
        dev_hash(tick);
    }
    if (s_on && dev_reached(s_dev.dump_tick, tick, last)) {
        dev_dump(tick);
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
    shot_poll();
    if (s_on) {
        note_host_blocks();
    }
    if (s_dev.save_tick != 0 || s_dev.load_tick != 0 || s_dev.hash_tick != 0 ||
        s_dev.dump_tick != 0) {
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
