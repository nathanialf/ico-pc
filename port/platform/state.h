/*
 * port/platform/state.h   (CONTRACT for v0.9.6 save states; the A packages
 * implement it, the B packages build against it; change only by agreement
 * with the orchestrator)
 *
 * Save states ("suspend points"): a whole-memory snapshot of the running
 * game taken at the host loop's quiescent point (host_loop.c ico_host_step
 * returned: every game fiber parked in a kernel call, the rd frame closed),
 * written to <pref>/states/<disc slug>/slot<n>.ics (+ slot<n>.png), and put
 * back later by relocating the few host pointers that moved.
 *
 * What a state holds (plan.md "156 design"): the arena (arena.h), the
 * game's .data/.bss (game_data_range.h), the registered port globals
 * (state_registry.h), IOP RAM, the audio engine's state (under the audio
 * lock), the scheduler tables and every live fiber's control block and used
 * stack (fiber.h slots), pobjImageTables, and the address ranges those bytes
 * lived at.  Validity: the build id, the disc slug, the platform tag and
 * the hash of the simulation keys (sim_keys.h) must match.
 *
 * Threading: every function here runs on the host loop's thread except
 * ico_state_request_* (any thread; latched) and the writer thread inside
 * state_file.c.
 */
#ifndef ICO_PLATFORM_STATE_H
#define ICO_PLATFORM_STATE_H

#include <stddef.h>
#include <stdint.h>

#define ICO_STATE_SLOTS 4 /* slots 1..4 */
#define ICO_STATE_FORMAT_VERSION 1

/* Why a save or a load was refused, or ICO_STATE_OK. */
typedef enum IcoStateResult {
    ICO_STATE_OK = 0,
    ICO_STATE_OFF, /* the feature is off on this build (ranges not bracketed, a sanitizer build) */
    ICO_STATE_NOT_PAUSED, /* the game is not on the pause menu (saves) or not quiescent for a load */
    ICO_STATE_STAGE_SWITCH,   /* systemStatus[6] != 0 */
    ICO_STATE_CARD_BUSY,      /* a memory card command is pending */
    ICO_STATE_DISC_BUSY,      /* a disc command or stream is active */
    ICO_STATE_MOVIE,          /* a movie is open */
    ICO_STATE_PHOTO,          /* photo mode, the credits or the gallery is active */
    ICO_STATE_EMPTY,          /* no state in that slot */
    ICO_STATE_BAD_FILE,       /* unreadable, truncated, a wrong magic or version */
    ICO_STATE_OTHER_BUILD,    /* saved by another build of the program */
    ICO_STATE_OTHER_DISC,     /* saved from another disc */
    ICO_STATE_OTHER_PLATFORM, /* saved on another platform */
    ICO_STATE_OTHER_SETTINGS, /* a simulation setting differs (detail names the key) */
    ICO_STATE_SELF_CHECK, /* the restored memory failed its checks; the game went on as before */
    ICO_STATE_IO,         /* the file could not be written */
    ICO_STATE_BUSY        /* a save or load is already in flight */
} IcoStateResult;

/* What the UI shows for a slot (state_host.c fills it from the header). */
typedef struct IcoStateSlotInfo {
    int present;              /* 1 when a file exists */
    int loadable;             /* 1 when it would load (build, disc, platform, settings match) */
    IcoStateResult why_not;   /* when !loadable */
    unsigned int play_frames; /* the game's play time at the save (ico_gs_play_frames) */
    int stage_no;             /* for the area name (settings.c areaName) */
    long long saved_at;       /* time(NULL) at the save */
    char build_commit[48];    /* text, for the refusal popup */
    char png_path[260];       /* the thumbnail's path ("" when none) */
} IcoStateSlotInfo;

/* 1 when save states work on this build and start-up (game_data_range
   verified, not a sanitizer build, the state folder writable). */
int ico_state_available(void);
/* The reason it is off, for the log and the page's note; "" when on. */
const char *ico_state_off_reason(void);

/* Requests, from any thread or the UI: latched and performed at the next
   ico_state_host_poll.  A second request before the poll replaces the
   first.  slot 1..ICO_STATE_SLOTS. */
void ico_state_request_save(int slot);
void ico_state_request_load(int slot);
void ico_state_request_delete(int slot);
/* The host loop calls this once per step right after ico_host_step (the
   quiescent point).  Performs the latched request; the result goes to the
   completion callback below. */
void ico_state_host_poll(void);

/* The result of the last performed request (the UI's popup, the log line
   is written by state_host.c itself).  `detail` is a short UTF-8 text with
   the specifics (the key that differs, the other build's commit, the slot)
   or "". */
typedef void (*IcoStateDoneFn)(int action /* 0 save, 1 load, 2 delete */, int slot,
                               IcoStateResult result, const char *detail, void *user);
void ico_state_set_done_callback(IcoStateDoneFn fn, void *user);

/* Slot listing for the UI: fills out[0..ICO_STATE_SLOTS-1]; cheap (reads
   the headers; cached until a save or delete). */
void ico_state_list(IcoStateSlotInfo out[ICO_STATE_SLOTS]);

/* The folder of the current disc's states (<pref>/states/<slug>), made on
   demand; "" when the pref dir is unknown. */
const char *ico_state_dir(void);

/* The thumbnail: the window hands the next presented picture after a save
   (rd_read_presented) here; state_host.c downscales to 320 px wide and
   writes slot<n>.png on the writer thread.  rgba is 8-bit RGBA, row pitch
   w * 4.  No-op when no save is waiting for a picture. */
void ico_state_thumbnail(const unsigned char *rgba, int w, int h);
/* 1 while a save waits for its picture (the window arms a capture). */
int ico_state_wants_thumbnail(void);

/* --- [dev] keys for the headless round trip (state_roundtrip_headless) ---
 * state_save_at = "<tick>:<slot>", state_load_at = "<tick>:<slot>",
 * state_hash_at = "<tick>": at that Main tick the poll saves / loads / logs
 * "state: hash <xxh3-64> of arena+gdata+gbss at tick N" (the game's state:
 * the host's pointers and handles in it left out, port/platform/state_hash.h)
 * and "state: hash masked N pointer words, M host ids ...".  Parsed by
 * state_host.c from the config at init; 0 = off. */
void ico_state_dev_set(unsigned int save_tick, int save_slot, unsigned int load_tick, int load_slot,
                       unsigned int hash_tick);
/* state_dump_at = "<tick>": at that Main tick (again after a load brought
 * the tick back) the poll writes the arena, the game's data and bss raw to
 * <states folder>/dump-<tick>-<k>/ with regions.txt and masked.txt, k
 * counting the run's dumps from 1 (tools/state_region_diff.py compares two:
 * the differing bytes by symbol).  0 = off. */
void ico_state_dev_set_dump(unsigned int dump_tick);

#endif /* ICO_PLATFORM_STATE_H */
