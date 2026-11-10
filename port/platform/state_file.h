/*
 * port/platform/state_file.h
 *
 * The save state file (<pref>/states/<slug>/slot<n>.ics, state.h): a raw
 * container, little-endian, in three parts:
 *
 *   the header     512 bytes, uncompressed: the magic "ICOSTATE", the
 *                  format version, the build id, the commit text, the
 *                  platform tag, the disc slug, the simulation keys' hash,
 *                  the save's time, play time, stage, Main tick and vsync,
 *                  the section count, the length of the keys text, the
 *                  file's length and a CRC-32 over the header, the section
 *                  table and the keys text
 *   the table      64 bytes per section: name, offset, packed and raw
 *                  sizes, the address the bytes lived at in the saving
 *                  process, the CRC-32 of the raw bytes, the caller's flags
 *   the keys text  "key=value\n" for every simulation key (sim_keys.h), so
 *                  a refusal names the first key that differs
 *
 * then each section's bytes, raw deflate (miniz tdefl, level 1).  A write
 * goes to <path>.tmp, is synced (ico_fsync) and renamed over <path>
 * (ico_rename_replace), on a writer thread: one write in flight at a time.
 *
 * The build id (16 bytes) is two XXH3-64 hashes (port/render/xxh3.h, the
 * second over the same text behind a fixed prefix) of a text naming the
 * commit, the compiler (__VERSION__), the platform tag, the format
 * version, the scheduler's table size, the fiber stack size, the arena
 * size, the game data ranges' sizes (game_data_range.h: hi - lo of the
 * data range then the bss range) and every registered block's name, size
 * and flags (state_registry.h) in order.  A state loads only into a
 * program whose build id is the same.
 */
#ifndef ICO_PLATFORM_STATE_FILE_H
#define ICO_PLATFORM_STATE_FILE_H

#include <stddef.h>
#include <stdint.h>

#include "state.h"

#define ICO_STATE_MAGIC "ICOSTATE" /* 8 bytes, no NUL */
#define ICO_STATE_HEADER_BYTES 512
#define ICO_STATE_TABLE_ENTRY_BYTES 64
#define ICO_STATE_MAX_SECTIONS 32
#define ICO_STATE_NAME_BYTES 16 /* a section's name, NUL included */
#define ICO_STATE_KEYS_MAX 4096 /* the keys text, NUL included */

/* One section as the file holds it (read_header fills these). */
typedef struct IcoStateSectionInfo {
    char name[ICO_STATE_NAME_BYTES];
    uint64_t offset;       /* of the packed bytes, from the file's start */
    uint64_t packed_bytes; /* deflated */
    uint64_t raw_bytes;
    uint64_t addr;  /* where the bytes lived in the saving process (0: not a range) */
    uint32_t crc;   /* CRC-32 of the raw bytes */
    uint32_t flags; /* the caller's */
} IcoStateSectionInfo;

/* The header.  ico_state_header_init fills the identity (version, build
   id, commit, platform); the caller fills the rest before a write.
   read_header fills everything, the sections too. */
typedef struct IcoStateHeader {
    uint32_t version;
    uint8_t build_id[16];
    char commit[48];
    char platform[16];
    char disc[16];
    uint64_t settings_hash; /* ico_sim_keys_hash */
    int64_t saved_at;       /* time(NULL) */
    uint32_t play_frames;
    int32_t stage_no;
    uint32_t main_tick;
    uint32_t vsync;
    char keys[ICO_STATE_KEYS_MAX]; /* the "key=value\n" text */
    /* read_header: */
    uint64_t file_bytes;
    int n_sections;
    IcoStateSectionInfo sections[ICO_STATE_MAX_SECTIONS];
} IcoStateHeader;

/* One section to write.  The bytes are read on the writer thread: they
   must stay as they are until the write is done (ico_state_file_poll or
   ico_state_file_flush says so). */
typedef struct IcoStateSection {
    const char *name; /* up to 15 characters */
    const void *data;
    uint64_t bytes;
    uint64_t addr;
    uint32_t flags;
} IcoStateSection;

/* "windows-x64", "linux-x64", "linux-arm64", "android-arm64",
   "android-x64", "macos-arm64", "macos-x64", "ios-arm64" (else
   "unknown"): a state from another platform is refused. */
const char *ico_state_platform_tag(void);

/* The build id over the text described above; commit is the program's
   ICO_BUILD_COMMIT (the platform layer does not see the generated header).
   Call after the registry is complete (ico_state_register_port).  When
   text is not NULL it gets the hashed text (size bytes, truncated), for a
   developer's log. */
void ico_state_build_id(const char *commit, uint8_t out[16], char *text, size_t size);

/* Zeroes h and fills version, build_id, commit and platform. */
void ico_state_header_init(IcoStateHeader *h, const char *commit);

/* The validity of a saved header against this run's (both from
   header_init plus disc, settings_hash and keys): ICO_STATE_OK, or in this
   order of precedence ICO_STATE_OTHER_PLATFORM (detail: the saved
   platform), ICO_STATE_OTHER_BUILD (another format version or build id;
   detail: the saved commit), ICO_STATE_OTHER_DISC (the saved slug) or
   ICO_STATE_OTHER_SETTINGS (the first key whose value differs, as
   "key=saved (now cur)"; the keys text decides it, the hash only when the
   texts agree).  A key this run lists that the saved text lacks (a key a
   later version added) is equal when the key-default function (below)
   says this run's value is its default; the saved text must then be whole
   (its own hash is the saved settings_hash), else "the settings differ".
   Without the function such a key differs ("key=(absent) (now cur)").  A
   key only the saved text lists is equal when this version retired it
   (ico_state_set_key_retired), under the same whole-text rule. */
IcoStateResult ico_state_header_check(const IcoStateHeader *saved, const IcoStateHeader *now,
                                      char *detail, size_t n);
/* The same without the build id: what a state of another build of this
   format must pass before its "syms" section is asked (state_xbuild.h). */
IcoStateResult ico_state_header_check_other_build(const IcoStateHeader *saved,
                                                  const IcoStateHeader *now, char *detail,
                                                  size_t n);

/* 1 when `value` (value_n bytes) means the simulation key's default (the
   key is key_n bytes, not NUL-terminated): sim_keys.h
   ico_sim_key_is_default, which state_host.c sets at its start (the
   platform layer does not see the config).  NULL: no key reads as a
   default (the tests' and tools' header checks). */
typedef int (*IcoStateKeyDefaultFn)(const char *key, size_t key_n, const char *value,
                                    size_t value_n);
void ico_state_set_key_default(IcoStateKeyDefaultFn fn);

/* The keys `now` lists that `saved` lacks, as "a, b" into out (size bytes,
   truncated; "" for none); their count.  For the load's log line. */
int ico_state_keys_absent(const char *saved, const char *now, char *out, size_t size);

/* 1 when the simulation key (key_n bytes, not NUL-terminated) is one this
   version retired: sim_keys.h ico_sim_key_retired (v0.11.3: the fifteen
   difficulty rows, which a state now carries as its own block), set by
   state_host.c at its start.  A key the saved text lists, `now` lacks and
   this function names is passed over whatever its value (the saved text
   must then be whole, as for an absent key).  NULL: none is retired (such
   a key differs: "key=value (now absent)"). */
typedef int (*IcoStateKeyRetiredFn)(const char *key, size_t key_n);
void ico_state_set_key_retired(IcoStateKeyRetiredFn fn);

/* The retired keys (above) `saved` lists and `now` lacks, as
   "key=value, key=value" into out (size bytes, truncated; "" for none);
   their count.  For the load's log line. */
int ico_state_keys_retired(const char *saved, const char *now, char *out, size_t size);

/* --- writing (the writer thread) --- */

/* What a finished write did. */
typedef struct IcoStateWriteResult {
    IcoStateResult result; /* ICO_STATE_OK or ICO_STATE_IO */
    char why[160];         /* the failure, "" on success */
    int n_sections;
    IcoStateSectionInfo sections[ICO_STATE_MAX_SECTIONS]; /* raw and packed sizes */
    uint64_t file_bytes;
    double write_ms; /* on the writer thread, deflate and sync included */
} IcoStateWriteResult;

/* Starts writing h and the n sections to path on the writer thread
   (h and s are copied; the section bytes are not, see IcoStateSection).
   ICO_STATE_OK started; ICO_STATE_BUSY while a write is in flight or its
   result was not collected; ICO_STATE_IO for a bad argument (too many
   sections, a long name, a long path) or when no thread could be made. */
IcoStateResult ico_state_file_write(const char *path, const IcoStateHeader *h,
                                    const IcoStateSection *s, int n);
/* 1 while a write runs or its result waits to be collected. */
int ico_state_file_busy(void);
/* Collects a finished write without waiting: 1 and *out (when not NULL);
   0 while it still runs or when there is none. */
int ico_state_file_poll(IcoStateWriteResult *out);
/* Waits for the write in flight and collects it: 1 and *out (when not
   NULL); 0 when there was none. */
int ico_state_file_flush(IcoStateWriteResult *out);

/* --- reading --- */

/* Reads and checks the header, the section table and the keys text:
   ICO_STATE_OK; ICO_STATE_EMPTY when there is no file; ICO_STATE_BAD_FILE
   when it is shorter than it says (truncated), the magic or the header's
   CRC is wrong, the version is not one of ICO_STATE_FORMAT_OLDEST ..
   ICO_STATE_FORMAT_VERSION (an older version's header reads the same; the
   header check then refuses it as another build), or the table does not
   fit the file. */
IcoStateResult ico_state_file_read_header(const char *path, IcoStateHeader *out);

/* Asked once per section in file order: the buffer to inflate it into
   (s->raw_bytes long), or NULL to skip the section. */
typedef void *(*IcoStateSectionFn)(void *user, const IcoStateSectionInfo *s);
/* Reads the header (into *h when not NULL) and inflates every section the
   callback wants.  ICO_STATE_OK, or what read_header returns, or
   ICO_STATE_BAD_FILE when a section's bytes do not inflate to exactly its
   raw size and CRC (the buffers then hold partial data). */
IcoStateResult ico_state_file_read(const char *path, IcoStateHeader *h, IcoStateSectionFn fn,
                                   void *user);

#endif /* ICO_PLATFORM_STATE_FILE_H */
